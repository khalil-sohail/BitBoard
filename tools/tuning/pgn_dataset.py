#!/usr/bin/env python3
"""Deterministic private PGN ingestion and position-dataset derivation.

Phase 12 deliberately has no dependency on the Bitboard engine or Stockfish.  It
uses python-chess to parse and legally replay mainlines, then writes canonical
JSON/JSONL artifacts for later reference annotation.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
import shutil
import sys
import tempfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

try:
    import chess
    import chess.pgn
except ModuleNotFoundError:  # Reported cleanly by require_python_chess().
    chess = None  # type: ignore[assignment]


SCHEMA_VERSION = 1
DATASET_VERSION = "pgn-derived-v1"
TOOL_VERSION = "1"
DEFAULT_SPLIT_SEED = "bitboard-pgn-split-v1"
RESULTS = {"1-0", "0-1", "1/2-1/2"}
HEADER_NAMES = (
    "Event", "Site", "Date", "Round", "White", "Black", "WhiteElo",
    "BlackElo", "Result", "ECO", "Opening", "Variation", "TimeControl",
    "SetUp", "FEN",
)
JSONL_NAMES = (
    "positions.jsonl", "train.jsonl", "validation.jsonl", "test.jsonl",
    "skipped-games.jsonl",
)
CANONICAL_ARTIFACTS = (*JSONL_NAMES, "summary.json")


class DatasetError(Exception):
    """A deterministic input, build, or validation failure."""


@dataclass(frozen=True)
class BuildConfig:
    pgn_dir: Path
    output_dir: Path
    dataset_version: str = DATASET_VERSION
    min_ply: int = 12
    sample_every: int = 2
    include_terminal: bool = False
    train_ratio: int = 80
    validation_ratio: int = 10
    test_ratio: int = 10
    split_seed: str = DEFAULT_SPLIT_SEED
    minimum_split_games: int = 10
    strict: bool = False
    force: bool = False


def require_python_chess() -> None:
    if chess is None:
        raise DatasetError(
            "python-chess is required. Install tools/tuning/requirements.txt "
            "into a virtual environment; the dataset tool never installs dependencies."
        )


def sha256_bytes(data: bytes) -> str:
    return "sha256:" + hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def pretty_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n"


def discover_pgn_files(pgn_dir: Path) -> list[Path]:
    if not pgn_dir.is_dir():
        raise DatasetError(f"PGN directory does not exist: {pgn_dir}")
    files = sorted(
        (path for path in pgn_dir.iterdir() if path.is_file() and path.suffix == ".pgn"),
        key=lambda path: path.name,
    )
    if not files:
        raise DatasetError(f"No regular *.pgn files found directly under: {pgn_dir}")
    return files


def normalize_header(value: str) -> str:
    return " ".join(value.split())


def normalized_headers(game: Any) -> dict[str, str]:
    return {name: normalize_header(str(game.headers.get(name, ""))) for name in HEADER_NAMES}


if chess is not None:
    class ResultTrackingBuilder(chess.pgn.GameBuilder):  # type: ignore[misc]
        """Retains header and movetext results separately for conflict checks."""

        def __init__(self) -> None:
            super().__init__()
            self.header_result: str | None = None
            self.movetext_result: str | None = None

        def visit_header(self, tagname: str, tagvalue: str) -> None:
            if tagname == "Result":
                self.header_result = tagvalue
            super().visit_header(tagname, tagvalue)

        def visit_result(self, result: str) -> None:
            self.movetext_result = result
            super().visit_result(result)

        def handle_error(self, error: Exception) -> None:
            # Keep deterministic diagnostics in skipped-games.jsonl without
            # letting python-chess flood stderr for a tolerant corpus build.
            self.game.errors.append(error)

        def result(self) -> Any:
            game = super().result()
            game.phase12_header_result = self.header_result
            game.phase12_movetext_result = self.movetext_result
            return game
else:
    ResultTrackingBuilder = object  # type: ignore[misc,assignment]


def stable_game_id(game: Any, moves_uci: Sequence[str], result: str) -> str:
    identity = {
        "headers": normalized_headers(game),
        "mainlineUci": list(moves_uci),
        "result": result,
    }
    return sha256_bytes(canonical_json(identity).encode("utf-8"))


def normalized_fen(board: Any) -> str:
    # python-chess emits an en-passant square only when a legal capture exists.
    return board.fen(en_passant="legal")


def position_key(board: Any) -> str:
    return " ".join(normalized_fen(board).split()[:4])


def result_labels(result: str, white_to_move: bool) -> tuple[float, float, float]:
    if result == "1-0":
        white, black = 1.0, 0.0
    elif result == "0-1":
        white, black = 0.0, 1.0
    elif result == "1/2-1/2":
        white = black = 0.5
    else:
        raise DatasetError(f"Unsupported game result: {result!r}")
    return white, black, white if white_to_move else black


def material_phase(board: Any, ply: int) -> str:
    # A transparent metadata-only classifier. Initial non-pawn phase score is 24:
    # queen=4, rook=2, bishop/knight=1. Low phase score is an endgame; otherwise
    # the first 24 plies with substantial material are opening positions.
    weights = {
        chess.QUEEN: 4,
        chess.ROOK: 2,
        chess.BISHOP: 1,
        chess.KNIGHT: 1,
    }
    phase_score = sum(len(board.pieces(piece, color)) * weight
                      for piece, weight in weights.items()
                      for color in (chess.WHITE, chess.BLACK))
    if phase_score <= 8:
        return "endgame"
    if ply <= 24 and phase_score >= 18:
        return "opening"
    return "middlegame"


def parse_optional_int(value: str) -> int | None:
    value = value.strip()
    if not value:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def split_for_game(game_id: str, seed: str, ratios: tuple[int, int, int]) -> str:
    digest = hashlib.sha256((game_id + "\0" + seed).encode("utf-8")).digest()
    bucket = int.from_bytes(digest[:8], "big") % 10_000
    train_end = ratios[0] * 100
    validation_end = train_end + ratios[1] * 100
    if bucket < train_end:
        return "train"
    if bucket < validation_end:
        return "validation"
    return "test"


def validate_config(config: BuildConfig) -> None:
    if config.min_ply < 0:
        raise DatasetError("--min-ply must be non-negative")
    if config.sample_every < 1:
        raise DatasetError("--sample-every must be at least 1")
    if config.minimum_split_games < 0:
        raise DatasetError("--minimum-split-games must be non-negative")
    ratios = (config.train_ratio, config.validation_ratio, config.test_ratio)
    if any(ratio < 0 for ratio in ratios) or sum(ratios) != 100:
        raise DatasetError("train, validation, and test ratios must be non-negative and total 100")
    if not config.dataset_version or any(char.isspace() for char in config.dataset_version):
        raise DatasetError("--dataset-version must be a non-empty token without whitespace")
    if not config.split_seed:
        raise DatasetError("--split-seed must be non-empty")


def skipped_record(source: str, index: int, code: str, detail: str,
                   diagnostics: Sequence[str] = ()) -> dict[str, Any]:
    return {
        "detail": detail,
        "diagnostics": list(diagnostics),
        "reasonCode": code,
        "sourceFile": source,
        "sourceGameIndex": index,
    }


def extract_game_positions(
    game: Any,
    source_file: str,
    game_index: int,
    dataset_version: str,
    result: str,
    game_id: str,
) -> list[dict[str, Any]]:
    try:
        board = game.board()
    except Exception as error:
        raise DatasetError(f"invalid initial position: {error}") from error
    if not board.is_valid():
        raise DatasetError(f"invalid initial position status: {board.status()}")

    headers = normalized_headers(game)
    white_elo = parse_optional_int(headers["WhiteElo"])
    black_elo = parse_optional_int(headers["BlackElo"])
    average_elo = None
    if white_elo is not None and black_elo is not None:
        average_elo = (white_elo + black_elo) // 2

    positions: list[dict[str, Any]] = []
    for ply, move in enumerate(game.mainline_moves(), start=1):
        if not board.is_legal(move):
            raise DatasetError(f"illegal mainline move at ply {ply}: {move.uci()}")
        is_capture = board.is_capture(move)
        is_promotion = move.promotion is not None
        move_uci = move.uci()
        board.push(move)
        fen = normalized_fen(board)
        key = position_key(board)
        position_id = sha256_bytes(canonical_json({
            "fen": fen,
            "gameId": game_id,
            "ply": ply,
        }).encode("utf-8"))
        white_result, black_result, stm_result = result_labels(result, board.turn)
        record = {
            "averageElo": average_elo,
            "blackElo": black_elo,
            "blackResult": black_result,
            "eco": headers["ECO"] or None,
            "fen": fen,
            "fullmoveNumber": board.fullmove_number,
            "gameId": game_id,
            "gamePhase": material_phase(board, ply),
            "isCapture": is_capture,
            "isCheck": board.is_check(),
            "isPromotion": is_promotion,
            "lastMoveUci": move_uci,
            "opening": headers["Opening"] or None,
            "pieceCount": len(board.piece_map()),
            "ply": ply,
            "positionId": position_id,
            "positionKey": key,
            "result": result,
            "resultFromSideToMove": stm_result,
            "schemaVersion": SCHEMA_VERSION,
            "sideToMove": "white" if board.turn else "black",
            "sourceFile": source_file,
            "sourceGameIndex": game_index,
            "split": None,
            "timeControl": headers["TimeControl"] or None,
            "variation": headers["Variation"] or None,
            "whiteElo": white_elo,
            "whiteResult": white_result,
            "datasetVersion": dataset_version,
        }
        positions.append(record)
    return positions


def read_source_games(
    path: Path,
    source_name: str,
    config: BuildConfig,
    known_games: dict[str, tuple[str, int]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any], dict[str, int]]:
    positions: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    stats = Counter()
    game_index = 0

    with path.open("r", encoding="utf-8-sig", errors="strict") as handle:
        while True:
            try:
                game = chess.pgn.read_game(handle, Visitor=ResultTrackingBuilder)
            except Exception as error:
                stats["gamesAttempted"] += 1
                record = skipped_record(source_name, game_index, "pgn-parse-error", str(error))
                skipped.append(record)
                stats["gamesSkipped"] += 1
                if config.strict:
                    raise DatasetError(f"{source_name}:{game_index}: {error}") from error
                # Parser recovery after an exception is not guaranteed; do not loop on
                # the same damaged token stream or retain a partial game.
                break
            if game is None:
                break

            stats["gamesAttempted"] += 1
            diagnostics = [str(error) for error in getattr(game, "errors", [])]
            header_result = getattr(game, "phase12_header_result", None)
            movetext_result = getattr(game, "phase12_movetext_result", None)
            reason: tuple[str, str] | None = None
            if diagnostics:
                reason = ("pgn-parser-diagnostics", "parser reported fatal game errors")
            elif header_result not in RESULTS:
                reason = ("unsupported-result", f"header Result is {header_result!r}")
            elif movetext_result not in RESULTS:
                reason = ("unsupported-result", f"movetext result is {movetext_result!r}")
            elif header_result != movetext_result:
                reason = (
                    "conflicting-result",
                    f"header Result {header_result!r} differs from movetext {movetext_result!r}",
                )
            elif "FEN" in game.headers and game.headers.get("SetUp") != "1":
                reason = ("invalid-setup", "FEN header requires SetUp=1")
            elif game.headers.get("SetUp") == "1" and "FEN" not in game.headers:
                reason = ("invalid-setup", "SetUp=1 requires a FEN header")

            game_positions: list[dict[str, Any]] = []
            game_id = ""
            if reason is None:
                try:
                    moves_uci = [move.uci() for move in game.mainline_moves()]
                    game_id = stable_game_id(game, moves_uci, header_result)
                    if game_id in known_games:
                        first_source, first_index = known_games[game_id]
                        reason = (
                            "duplicate-game",
                            f"same normalized game as {first_source}:{first_index}",
                        )
                        stats["duplicateGames"] += 1
                    else:
                        game_positions = extract_game_positions(
                            game, source_name, game_index, config.dataset_version,
                            header_result, game_id,
                        )
                        if not game_positions:
                            reason = ("empty-mainline", "game has no mainline moves")
                except Exception as error:
                    reason = ("illegal-or-invalid-game", str(error))

            if reason is not None:
                skipped.append(skipped_record(
                    source_name, game_index, reason[0], reason[1], diagnostics,
                ))
                stats["gamesSkipped"] += 1
                if config.strict and reason[0] != "duplicate-game":
                    raise DatasetError(f"{source_name}:{game_index}: {reason[0]}: {reason[1]}")
            else:
                known_games[game_id] = (source_name, game_index)
                positions.extend(game_positions)
                stats["gamesAccepted"] += 1
                stats["positionsBeforeFiltering"] += len(game_positions)
            game_index += 1

    source_meta = {
        "fileSize": path.stat().st_size,
        "gamesAccepted": stats["gamesAccepted"],
        "gamesAttempted": stats["gamesAttempted"],
        "gamesSkipped": stats["gamesSkipped"],
        "path": source_name,
        "positionsAfterFiltering": 0,
        "positionsBeforeFiltering": stats["positionsBeforeFiltering"],
        "sha256": sha256_file(path),
    }
    return positions, skipped, source_meta, dict(stats)


def apply_filters(
    positions: Sequence[dict[str, Any]], config: BuildConfig,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    accepted: list[dict[str, Any]] = []
    counts = Counter()
    for record in positions:
        if record["ply"] < config.min_ply:
            counts["minimumPly"] += 1
            continue
        if (record["ply"] - config.min_ply) % config.sample_every != 0:
            counts["sampling"] += 1
            continue
        board = chess.Board(record["fen"])
        if not config.include_terminal and board.is_game_over(claim_draw=False):
            counts["terminal"] += 1
            continue
        accepted.append(record)
    return accepted, {
        "minimumPly": counts["minimumPly"],
        "sampling": counts["sampling"],
        "terminal": counts["terminal"],
    }


def deduplicate_positions(
    positions: Sequence[dict[str, Any]],
) -> tuple[list[dict[str, Any]], dict[str, int], list[dict[str, Any]]]:
    by_key: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in positions:
        by_key[record["positionKey"]].append(record)

    retained: list[dict[str, Any]] = []
    conflicts: list[dict[str, Any]] = []
    same_label_removed = 0
    conflicting_occurrences = 0
    for key, occurrences in by_key.items():
        labels = {record["resultFromSideToMove"] for record in occurrences}
        if len(labels) > 1:
            conflicting_occurrences += len(occurrences)
            conflicts.append({
                "labels": sorted(labels),
                "occurrences": [record["positionId"] for record in occurrences],
                "positionKey": key,
            })
            continue
        retained.append(occurrences[0])
        same_label_removed += len(occurrences) - 1
    return retained, {
        "sameLabelDuplicatesRemoved": same_label_removed,
        "conflictingLabelPositionKeys": len(conflicts),
        "conflictingLabelOccurrencesExcluded": conflicting_occurrences,
    }, conflicts


def jsonl_bytes(records: Sequence[dict[str, Any]]) -> bytes:
    if not records:
        return b"\n"
    return ("\n".join(canonical_json(record) for record in records) + "\n").encode("utf-8")


def write_bytes(path: Path, data: bytes) -> None:
    with path.open("wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if not line:
                continue
            value = json.loads(line)
            if not isinstance(value, dict):
                raise DatasetError(f"{path.name}:{line_number}: expected JSON object")
            records.append(value)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DatasetError(f"Invalid JSONL in {path}: {error}") from error
    return records


def validate_position_record(record: dict[str, Any], dataset_version: str) -> None:
    required = {
        "schemaVersion", "datasetVersion", "positionId", "gameId", "sourceFile",
        "sourceGameIndex", "ply", "fullmoveNumber", "sideToMove", "fen",
        "positionKey", "lastMoveUci", "result", "whiteResult", "blackResult",
        "resultFromSideToMove", "gamePhase", "split",
    }
    missing = required - record.keys()
    if missing:
        raise DatasetError(f"Position record missing fields: {sorted(missing)}")
    if record["schemaVersion"] != SCHEMA_VERSION or record["datasetVersion"] != dataset_version:
        raise DatasetError("Position schema or dataset version mismatch")
    if record["result"] not in RESULTS:
        raise DatasetError(f"Unsupported position result: {record['result']!r}")
    if record["resultFromSideToMove"] not in (0.0, 0.5, 1.0):
        raise DatasetError("Invalid resultFromSideToMove")
    if record["sideToMove"] not in ("white", "black"):
        raise DatasetError("Invalid sideToMove")
    if record["split"] not in ("train", "validation", "test"):
        raise DatasetError("Invalid split")
    if record["gamePhase"] not in ("opening", "middlegame", "endgame"):
        raise DatasetError("Invalid gamePhase")
    if Path(record["sourceFile"]).is_absolute():
        raise DatasetError("Absolute source path stored in position record")
    if not str(record["positionId"]).startswith("sha256:") or not str(record["gameId"]).startswith("sha256:"):
        raise DatasetError("Invalid position or game ID")
    try:
        board = chess.Board(record["fen"])
    except ValueError as error:
        raise DatasetError(f"Invalid FEN: {record['fen']}") from error
    if not board.is_valid() or normalized_fen(board) != record["fen"]:
        raise DatasetError(f"Non-normalized or invalid FEN: {record['fen']}")
    if position_key(board) != record["positionKey"]:
        raise DatasetError("positionKey does not match FEN")
    if (record["sideToMove"] == "white") != board.turn:
        raise DatasetError("sideToMove does not match FEN")


def validate_dataset(dataset_dir: Path) -> dict[str, Any]:
    require_python_chess()
    manifest_path = dataset_dir / "manifest.json"
    if not manifest_path.is_file():
        raise DatasetError(f"Missing manifest: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DatasetError(f"Invalid manifest: {error}") from error
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise DatasetError("Manifest schema version mismatch")
    dataset_version = manifest.get("datasetVersion")
    if not isinstance(dataset_version, str) or not dataset_version:
        raise DatasetError("Invalid manifest dataset version")

    for name in CANONICAL_ARTIFACTS:
        path = dataset_dir / name
        if not path.is_file():
            raise DatasetError(f"Missing artifact: {name}")
        data = path.read_bytes()
        if not data.endswith(b"\n"):
            raise DatasetError(f"Canonical artifact is not newline terminated: {name}")
        expected = manifest.get("artifacts", {}).get(name, {}).get("sha256")
        actual = sha256_bytes(data)
        if expected != actual:
            raise DatasetError(f"Artifact checksum mismatch for {name}: expected {expected}, got {actual}")

    positions = read_jsonl(dataset_dir / "positions.jsonl")
    split_records = {
        split: read_jsonl(dataset_dir / f"{split}.jsonl")
        for split in ("train", "validation", "test")
    }
    position_ids: set[str] = set()
    position_keys: set[str] = set()
    game_splits: dict[str, str] = {}
    for record in positions:
        validate_position_record(record, dataset_version)
        if record["positionId"] in position_ids:
            raise DatasetError(f"Duplicate positionId: {record['positionId']}")
        if record["positionKey"] in position_keys:
            raise DatasetError(f"Duplicate retained positionKey: {record['positionKey']}")
        position_ids.add(record["positionId"])
        position_keys.add(record["positionKey"])
        previous = game_splits.setdefault(record["gameId"], record["split"])
        if previous != record["split"]:
            raise DatasetError(f"Cross-split game leakage: {record['gameId']}")

    for split, records in split_records.items():
        if records != [record for record in positions if record["split"] == split]:
            raise DatasetError(f"{split}.jsonl does not exactly match positions.jsonl")
    counts = manifest.get("counts", {})
    if counts.get("finalPositions") != len(positions):
        raise DatasetError("Manifest finalPositions count mismatch")
    for name in JSONL_NAMES:
        expected_count = manifest["artifacts"][name]["records"]
        if expected_count != len(read_jsonl(dataset_dir / name)):
            raise DatasetError(f"Manifest record count mismatch for {name}")
    return {
        "games": len(game_splits),
        "positions": len(positions),
        "splits": {name: len(records) for name, records in split_records.items()},
        "manifest": manifest,
    }


def build_summary(
    source_files: Sequence[dict[str, Any]],
    skipped: Sequence[dict[str, Any]],
    raw_positions: int,
    filtered_positions: Sequence[dict[str, Any]],
    final_positions: Sequence[dict[str, Any]],
    filter_counts: dict[str, int],
    dedup_counts: dict[str, int],
    minimum_split_games: int,
) -> dict[str, Any]:
    games_per_split = {
        split: len({record["gameId"] for record in final_positions if record["split"] == split})
        for split in ("train", "validation", "test")
    }
    positions_per_split = Counter(record["split"] for record in final_positions)
    warnings: list[str] = []
    warnings.append(
        "source corpus is provisional and is not assumed sufficient or diverse enough for final tuning"
    )
    for split in ("validation", "test"):
        if games_per_split[split] < minimum_split_games:
            warnings.append(
                f"{split} split contains fewer than {minimum_split_games} games and is not suitable for final evaluation"
            )
    return {
        "corpusStatus": "provisional",
        "deduplication": dedup_counts,
        "duplicateGamesRemoved": sum(
            1 for record in skipped if record["reasonCode"] == "duplicate-game"
        ),
        "filterRemovals": filter_counts,
        "games": {
            "accepted": sum(source["gamesAccepted"] for source in source_files),
            "attempted": sum(source["gamesAttempted"] for source in source_files),
            "skipped": sum(source["gamesSkipped"] for source in source_files),
        },
        "phaseCounts": dict(sorted(Counter(record["gamePhase"] for record in final_positions).items())),
        "positions": {
            "afterFilteringBeforeDeduplication": len(filtered_positions),
            "extracted": raw_positions,
            "final": len(final_positions),
        },
        "resultCounts": dict(sorted(Counter(record["result"] for record in final_positions).items())),
        "splitGames": games_per_split,
        "splitPositions": {split: positions_per_split[split] for split in ("train", "validation", "test")},
        "warnings": warnings,
    }


def write_dataset(
    temp_dir: Path,
    config: BuildConfig,
    source_files: Sequence[dict[str, Any]],
    skipped: Sequence[dict[str, Any]],
    raw_positions: int,
    filtered_positions: Sequence[dict[str, Any]],
    final_positions: Sequence[dict[str, Any]],
    filter_counts: dict[str, int],
    dedup_counts: dict[str, int],
    conflicts: Sequence[dict[str, Any]],
) -> None:
    split_records = {
        split: [record for record in final_positions if record["split"] == split]
        for split in ("train", "validation", "test")
    }
    records_by_file = {
        "positions.jsonl": list(final_positions),
        "train.jsonl": split_records["train"],
        "validation.jsonl": split_records["validation"],
        "test.jsonl": split_records["test"],
        "skipped-games.jsonl": list(skipped),
    }
    for name, records in records_by_file.items():
        write_bytes(temp_dir / name, jsonl_bytes(records))

    summary = build_summary(
        source_files, skipped, raw_positions, filtered_positions, final_positions,
        filter_counts, dedup_counts, config.minimum_split_games,
    )
    # Conflict occurrence IDs make the exclusion auditable without adding another
    # artifact or storing raw PGN text.
    summary["conflictingPositions"] = list(conflicts)
    write_bytes(temp_dir / "summary.json", pretty_json(summary).encode("utf-8"))

    artifacts: dict[str, dict[str, Any]] = {}
    for name in CANONICAL_ARTIFACTS:
        artifacts[name] = {"sha256": sha256_file(temp_dir / name)}
        if name in records_by_file:
            artifacts[name]["records"] = len(records_by_file[name])
    manifest = {
        "artifacts": artifacts,
        "configuration": {
            "filters": {
                "includeTerminal": config.include_terminal,
                "minPly": config.min_ply,
                "sampleEvery": config.sample_every,
            },
            "minimumSplitGames": config.minimum_split_games,
            "splitRatios": {
                "test": config.test_ratio,
                "train": config.train_ratio,
                "validation": config.validation_ratio,
            },
            "splitSeed": config.split_seed,
            "strict": config.strict,
        },
        "counts": {
            "finalPositions": len(final_positions),
            "sourceFiles": len(source_files),
        },
        "datasetId": config.dataset_version,
        "datasetVersion": config.dataset_version,
        "deduplicationPolicy": {
            "conflictingLabels": "exclude-all-occurrences",
            "duplicateGames": "retain-first-deterministic-occurrence",
            "positionKey": "first-four-fields-of-legal-en-passant-normalized-FEN",
            "sameLabelPositions": "retain-first-deterministic-occurrence",
        },
        "schemaVersion": SCHEMA_VERSION,
        "sources": list(source_files),
        "parser": {
            "distribution": "python-chess",
            "distributionVersion": importlib.metadata.version("python-chess"),
            "libraryVersion": chess.__version__,
        },
        "tool": {"name": "pgn_dataset.py", "version": TOOL_VERSION},
    }
    write_bytes(temp_dir / "manifest.json", pretty_json(manifest).encode("utf-8"))


def install_atomically(temp_dir: Path, output_dir: Path, force: bool) -> None:
    backup: Path | None = None
    try:
        if output_dir.exists():
            if not force:
                raise DatasetError(f"Output directory already exists; use --force: {output_dir}")
            backup = output_dir.with_name(output_dir.name + ".phase12-backup")
            if backup.exists():
                raise DatasetError(f"Refusing replacement because backup path exists: {backup}")
            output_dir.rename(backup)
        temp_dir.rename(output_dir)
        if backup is not None:
            shutil.rmtree(backup)
    except Exception:
        if backup is not None and backup.exists() and not output_dir.exists():
            backup.rename(output_dir)
        raise


def build_dataset(config: BuildConfig) -> dict[str, Any]:
    require_python_chess()
    validate_config(config)
    if config.output_dir.exists() and not config.force:
        raise DatasetError(f"Output directory already exists; use --force: {config.output_dir}")
    files = discover_pgn_files(config.pgn_dir)
    output_parent = config.output_dir.parent
    output_parent.mkdir(parents=True, exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix=f".{config.output_dir.name}.tmp-", dir=output_parent))

    try:
        all_positions: list[dict[str, Any]] = []
        all_skipped: list[dict[str, Any]] = []
        source_files: list[dict[str, Any]] = []
        known_games: dict[str, tuple[str, int]] = {}
        source_checksums: dict[str, str] = {}
        for path in files:
            source_name = path.relative_to(config.pgn_dir).as_posix()
            positions, skipped, source_meta, _ = read_source_games(
                path, source_name, config, known_games,
            )
            source_checksums[source_name] = source_meta["sha256"]
            all_positions.extend(positions)
            all_skipped.extend(skipped)
            source_files.append(source_meta)

        if not any(source["gamesAccepted"] for source in source_files):
            raise DatasetError("No valid non-duplicate games remain")

        filtered, filter_counts = apply_filters(all_positions, config)
        after_by_source = Counter(record["sourceFile"] for record in filtered)
        for source in source_files:
            source["positionsAfterFiltering"] = after_by_source[source["path"]]
        deduplicated, dedup_counts, conflicts = deduplicate_positions(filtered)
        if not deduplicated:
            raise DatasetError("No accepted positions remain after filtering and deduplication")
        ratios = (config.train_ratio, config.validation_ratio, config.test_ratio)
        for record in deduplicated:
            record["split"] = split_for_game(record["gameId"], config.split_seed, ratios)

        # Detect corpus mutation during the build before publishing any artifact.
        for path in files:
            source_name = path.relative_to(config.pgn_dir).as_posix()
            if sha256_file(path) != source_checksums[source_name]:
                raise DatasetError(f"Source PGN changed during build: {source_name}")

        write_dataset(
            temp_dir, config, source_files, all_skipped, len(all_positions), filtered,
            deduplicated, filter_counts, dedup_counts, conflicts,
        )
        validation = validate_dataset(temp_dir)
        install_atomically(temp_dir, config.output_dir, config.force)
        return validation
    except Exception:
        if temp_dir.exists():
            shutil.rmtree(temp_dir)
        raise


def print_summary(dataset_dir: Path, validation: dict[str, Any]) -> None:
    manifest = validation["manifest"]
    summary = json.loads((dataset_dir / "summary.json").read_text(encoding="utf-8"))
    print(f"PGN files discovered: {manifest['counts']['sourceFiles']}")
    print(f"Games parsed/attempted: {summary['games']['attempted']}")
    print(f"Games accepted: {summary['games']['accepted']}")
    print(f"Games skipped: {summary['games']['skipped']}")
    print(f"Duplicate games removed: {summary['duplicateGamesRemoved']}")
    print(f"Positions extracted: {summary['positions']['extracted']}")
    print(f"Positions after filtering: {summary['positions']['afterFilteringBeforeDeduplication']}")
    print(f"Same-label duplicates removed: {summary['deduplication']['sameLabelDuplicatesRemoved']}")
    print(f"Conflicting-label positions excluded: {summary['deduplication']['conflictingLabelOccurrencesExcluded']}")
    print(f"Final positions: {summary['positions']['final']}")
    print(f"Split games: {canonical_json(summary['splitGames'])}")
    print(f"Split positions: {canonical_json(summary['splitPositions'])}")
    print(f"Phase counts: {canonical_json(summary['phaseCounts'])}")
    print(f"Result counts: {canonical_json(summary['resultCounts'])}")
    if summary["warnings"]:
        for warning in summary["warnings"]:
            print(f"Warning: {warning}")
    else:
        print("Warnings: none")
    print(f"Output path: {dataset_dir}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build", help="derive a deterministic dataset from PGNs")
    build.add_argument("--pgn-dir", type=Path, default=Path("tuning/pgn"))
    build.add_argument("--output-dir", type=Path, default=Path("tuning/datasets/pgn-derived-v1"))
    build.add_argument("--dataset-version", default=DATASET_VERSION)
    build.add_argument("--min-ply", type=int, default=12)
    build.add_argument("--sample-every", type=int, default=2)
    build.add_argument("--include-terminal", action="store_true")
    build.add_argument("--train-ratio", type=int, default=80)
    build.add_argument("--validation-ratio", type=int, default=10)
    build.add_argument("--test-ratio", type=int, default=10)
    build.add_argument("--split-seed", default=DEFAULT_SPLIT_SEED)
    build.add_argument("--minimum-split-games", type=int, default=10)
    build.add_argument("--strict", action="store_true")
    build.add_argument("--force", action="store_true")
    inspect = subparsers.add_parser("inspect", help="validate and summarize an existing dataset")
    inspect.add_argument("--dataset-dir", type=Path, default=Path("tuning/datasets/pgn-derived-v1"))
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            config = BuildConfig(
                pgn_dir=args.pgn_dir,
                output_dir=args.output_dir,
                dataset_version=args.dataset_version,
                min_ply=args.min_ply,
                sample_every=args.sample_every,
                include_terminal=args.include_terminal,
                train_ratio=args.train_ratio,
                validation_ratio=args.validation_ratio,
                test_ratio=args.test_ratio,
                split_seed=args.split_seed,
                minimum_split_games=args.minimum_split_games,
                strict=args.strict,
                force=args.force,
            )
            validation = build_dataset(config)
            print_summary(config.output_dir, validation)
        else:
            validation = validate_dataset(args.dataset_dir)
            print_summary(args.dataset_dir, validation)
        return 0
    except DatasetError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
