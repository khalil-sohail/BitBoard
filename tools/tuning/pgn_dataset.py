#!/usr/bin/env python3
"""Deterministic, bounded-memory PGN dataset derivation.

The builder scans PGNs sequentially, samples only a configured number of
positions from each game, and keeps a bounded, deduplicated pool in SQLite.
The database is also the durable checkpoint used to resume interrupted scans.
Only one canonical JSONL artifact is published; consumers filter its ``split``
field or query the private database through the helpers in this module.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.metadata
import json
import os
import resource
import shutil
import sqlite3
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Iterator, Mapping, Sequence

try:
    import chess
    import chess.pgn
except ModuleNotFoundError:
    chess = None  # type: ignore[assignment]


SCHEMA_VERSION = 2
DATABASE_SCHEMA_VERSION = 1
DATASET_VERSION = "pgn-derived-v1"
TOOL_VERSION = "2"
DEFAULT_SPLIT_SEED = "bitboard-pgn-split-v1"
DEFAULT_SAMPLING_SEED = "bitboard-corpus-sampling-v2"
RESULTS = {"1-0", "0-1", "1/2-1/2"}
PHASES = ("opening", "middlegame", "endgame")
SIDES = ("white", "black")
SPLITS = ("train", "validation", "test")
RESULT_CLASSES = ("white-win", "black-win", "draw")
HEADER_NAMES = (
    "Event", "Site", "Date", "Round", "White", "Black", "WhiteElo",
    "BlackElo", "Result", "ECO", "Opening", "Variation", "TimeControl",
    "SetUp", "FEN", "Variant",
)
JSONL_NAMES = ("positions.jsonl", "skipped-games.jsonl")
CANONICAL_ARTIFACTS = (*JSONL_NAMES, "summary.json")


class DatasetError(Exception):
    """A deterministic input, resource, build, or validation failure."""


@dataclass(frozen=True)
class BuildConfig:
    pgn_dir: Path
    output_dir: Path
    dataset_version: str = DATASET_VERSION
    min_ply: int = 12
    sample_every: int = 1
    include_terminal: bool = False
    train_ratio: int = 80
    validation_ratio: int = 10
    test_ratio: int = 10
    split_seed: str = DEFAULT_SPLIT_SEED
    minimum_split_games: int = 10
    strict: bool = False
    force: bool = False
    resume: bool = False
    maximum_accepted_games: int | None = None
    scan_all_input_games: bool = True
    maximum_retained_positions: int = 100_000
    maximum_positions_per_game: int = 6
    phase_quotas: Mapping[str, int] = field(default_factory=lambda: {
        "opening": 2, "middlegame": 3, "endgame": 1,
    })
    sampling_seed: str = DEFAULT_SAMPLING_SEED
    transaction_games: int = 250
    progress_games: int = 2_000
    maximum_rss_mb: int = 8_192
    warning_rss_mb: int = 6_144


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


def canonical_checksum(value: Any) -> str:
    return sha256_bytes(canonical_json(value).encode("utf-8"))


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
            self.game.errors.append(error)

        def result(self) -> Any:
            game = super().result()
            game.phase12_header_result = self.header_result
            game.phase12_movetext_result = self.movetext_result
            return game
else:
    ResultTrackingBuilder = object  # type: ignore[misc,assignment]


def stable_game_id(game: Any, moves_uci: Sequence[str], result: str) -> str:
    """Identity based on chess content, never display/source metadata."""
    board = game.board()
    variant = normalize_header(str(game.headers.get("Variant", "Standard"))) or "Standard"
    identity = {
        "initialFen": normalized_fen(board),
        "mainlineUci": list(moves_uci),
        "result": result,
        "variant": variant.lower(),
    }
    return sha256_bytes(canonical_json(identity).encode("utf-8"))


def normalized_fen(board: Any) -> str:
    return board.fen(en_passant="legal")


def position_key(board: Any) -> str:
    return " ".join(normalized_fen(board).split()[:4])


def result_class(result: str) -> str:
    return {"1-0": "white-win", "0-1": "black-win", "1/2-1/2": "draw"}[result]


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
    weights = {chess.QUEEN: 4, chess.ROOK: 2, chess.BISHOP: 1, chess.KNIGHT: 1}
    phase_score = sum(len(board.pieces(piece, color)) * weight
                      for piece, weight in weights.items()
                      for color in (chess.WHITE, chess.BLACK))
    if phase_score <= 8:
        return "endgame"
    if ply <= 24 and phase_score >= 18:
        return "opening"
    return "middlegame"


def parse_optional_int(value: str) -> int | None:
    try:
        return int(value.strip()) if value.strip() else None
    except ValueError:
        return None


def split_for_game(game_id: str, seed: str, ratios: tuple[int, int, int]) -> str:
    bucket = int.from_bytes(hashlib.sha256((game_id + "\0" + seed).encode()).digest()[:8], "big") % 10_000
    if bucket < ratios[0] * 100:
        return "train"
    if bucket < (ratios[0] + ratios[1]) * 100:
        return "validation"
    return "test"


def validate_config(config: BuildConfig) -> None:
    if config.min_ply < 0 or config.sample_every < 1:
        raise DatasetError("min-ply must be non-negative and sample-every at least 1")
    ratios = (config.train_ratio, config.validation_ratio, config.test_ratio)
    if any(value < 0 for value in ratios) or sum(ratios) != 100:
        raise DatasetError("train, validation, and test ratios must total 100")
    if not config.dataset_version or any(char.isspace() for char in config.dataset_version):
        raise DatasetError("dataset-version must be a non-empty token without whitespace")
    if config.maximum_accepted_games is not None and config.maximum_accepted_games < 1:
        raise DatasetError("maximum-accepted-games must be positive or omitted")
    if not config.scan_all_input_games and config.maximum_accepted_games is None:
        raise DatasetError("a bounded scan requires maximum-accepted-games")
    minimum_strata = len(SPLITS) * len(SIDES) * len(PHASES) * len(RESULT_CLASSES)
    if config.maximum_retained_positions < minimum_strata:
        raise DatasetError(f"maximum-retained-positions must allow all {minimum_strata} balance strata")
    if config.maximum_positions_per_game < 1:
        raise DatasetError("maximum-positions-per-game must be positive")
    if set(config.phase_quotas) != set(PHASES) or any(int(config.phase_quotas[p]) < 0 for p in PHASES):
        raise DatasetError("phase quotas must define non-negative opening, middlegame, and endgame values")
    if sum(int(config.phase_quotas[p]) for p in PHASES) > config.maximum_positions_per_game:
        raise DatasetError("phase quotas exceed maximum positions per game")
    if config.transaction_games < 1 or config.maximum_rss_mb < 128:
        raise DatasetError("invalid transaction or RSS policy")


def extract_game_positions(game: Any, source_file: str, game_index: int,
                           dataset_version: str, result: str, game_id: str) -> list[dict[str, Any]]:
    board = game.board()
    if not board.is_valid():
        raise DatasetError(f"invalid initial position status: {board.status()}")
    headers = normalized_headers(game)
    white_elo, black_elo = parse_optional_int(headers["WhiteElo"]), parse_optional_int(headers["BlackElo"])
    average_elo = (white_elo + black_elo) // 2 if white_elo is not None and black_elo is not None else None
    positions: list[dict[str, Any]] = []
    for ply, move in enumerate(game.mainline_moves(), 1):
        if not board.is_legal(move):
            raise DatasetError(f"illegal mainline move at ply {ply}: {move.uci()}")
        capture, promotion, move_uci = board.is_capture(move), move.promotion is not None, move.uci()
        board.push(move)
        fen, key = normalized_fen(board), position_key(board)
        white_result, black_result, stm_result = result_labels(result, board.turn)
        position_id = sha256_bytes(canonical_json({"fen": fen, "gameId": game_id, "ply": ply}).encode())
        positions.append({
            "averageElo": average_elo, "blackElo": black_elo, "blackResult": black_result,
            "datasetVersion": dataset_version, "eco": headers["ECO"] or None, "fen": fen,
            "fullmoveNumber": board.fullmove_number, "gameId": game_id,
            "gamePhase": material_phase(board, ply), "isCapture": capture, "isCheck": board.is_check(),
            "isPromotion": promotion, "lastMoveUci": move_uci, "opening": headers["Opening"] or None,
            "pieceCount": len(board.piece_map()), "ply": ply, "positionId": position_id,
            "positionKey": key, "result": result, "resultFromSideToMove": stm_result,
            "schemaVersion": SCHEMA_VERSION, "sideToMove": "white" if board.turn else "black",
            "sourceFile": source_file, "sourceGameIndex": game_index, "split": None,
            "timeControl": headers["TimeControl"] or None, "variation": headers["Variation"] or None,
            "whiteElo": white_elo, "whiteResult": white_result,
        })
    return positions


def apply_filters(positions: Sequence[dict[str, Any]], config: BuildConfig) -> tuple[list[dict[str, Any]], dict[str, int]]:
    accepted, counts = [], Counter()
    for record in positions:
        if record["ply"] < config.min_ply:
            counts["minimumPly"] += 1; continue
        if (record["ply"] - config.min_ply) % config.sample_every:
            counts["sampling"] += 1; continue
        if not config.include_terminal and chess.Board(record["fen"]).is_game_over(claim_draw=False):
            counts["terminal"] += 1; continue
        accepted.append(record)
    return accepted, {key: counts[key] for key in ("minimumPly", "sampling", "terminal")}


def deduplicate_positions(positions: Sequence[dict[str, Any]]) -> tuple[list[dict[str, Any]], dict[str, int], list[dict[str, Any]]]:
    """Small-sequence compatibility helper; corpus deduplication is SQLite-backed."""
    by_key: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in positions:
        by_key[record["positionKey"]].append(record)
    retained, conflicts, same, conflicting = [], [], 0, 0
    for key, occurrences in by_key.items():
        labels = {record["resultFromSideToMove"] for record in occurrences}
        if len(labels) > 1:
            conflicting += len(occurrences)
            conflicts.append({"positionKey": key, "labels": sorted(labels),
                              "occurrences": [record["positionId"] for record in occurrences]})
        else:
            retained.append(occurrences[0]); same += len(occurrences) - 1
    return retained, {"sameLabelDuplicatesRemoved": same,
                      "conflictingLabelPositionKeys": len(conflicts),
                      "conflictingLabelOccurrencesExcluded": conflicting}, conflicts


def per_game_sample(records: Sequence[dict[str, Any]], config: BuildConfig, game_id: str) -> list[dict[str, Any]]:
    selected: list[dict[str, Any]] = []
    for phase in PHASES:
        candidates = [record for record in records if record["gamePhase"] == phase]
        candidates.sort(key=lambda record: (
            hashlib.sha256((config.sampling_seed + "\0" + game_id + "\0" +
                            record["positionKey"] + "\0" + phase).encode()).hexdigest(),
            record["positionId"],
        ))
        selected.extend(candidates[:int(config.phase_quotas[phase])])
    selected.sort(key=lambda record: record["ply"])
    return selected[:config.maximum_positions_per_game]


def sampling_rank(config: BuildConfig, record: Mapping[str, Any]) -> str:
    value = "\0".join((config.sampling_seed, str(record["split"]), str(record["gamePhase"]),
                         str(record["sideToMove"]), result_class(str(record["result"])),
                         str(record["positionKey"])))
    return hashlib.sha256(value.encode()).hexdigest()


def current_rss_mb() -> float:
    status = Path("/proc/self/status")
    if status.is_file():
        with status.open("r", encoding="ascii") as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024
    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return value / 1024 if sys.platform != "darwin" else value / (1024 * 1024)


def configuration_identity(config: BuildConfig, sources: Sequence[Mapping[str, Any]]) -> str:
    return canonical_checksum({
        "schemaVersion": DATABASE_SCHEMA_VERSION, "datasetVersion": config.dataset_version,
        "minPly": config.min_ply, "sampleEvery": config.sample_every,
        "includeTerminal": config.include_terminal, "ratios": [config.train_ratio, config.validation_ratio, config.test_ratio],
        "splitSeed": config.split_seed, "maximumAcceptedGames": config.maximum_accepted_games,
        "scanAllInputGames": config.scan_all_input_games,
        "maximumRetainedPositions": config.maximum_retained_positions,
        "maximumPositionsPerGame": config.maximum_positions_per_game,
        "phaseQuotas": dict(config.phase_quotas), "samplingSeed": config.sampling_seed,
        "sources": list(sources),
    })


def open_database(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    connection.execute("PRAGMA journal_mode=WAL")
    connection.execute("PRAGMA synchronous=FULL")
    connection.execute("PRAGMA temp_store=FILE")
    connection.executescript("""
        CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS sources (
            path TEXT PRIMARY KEY, checksum TEXT NOT NULL, size INTEGER NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending', byte_offset INTEGER NOT NULL DEFAULT 0,
            game_index INTEGER NOT NULL DEFAULT 0, attempted INTEGER NOT NULL DEFAULT 0,
            accepted INTEGER NOT NULL DEFAULT 0, rejected INTEGER NOT NULL DEFAULT 0,
            duplicates INTEGER NOT NULL DEFAULT 0, positions_considered INTEGER NOT NULL DEFAULT 0,
            candidates INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS games (
            game_id TEXT PRIMARY KEY, source_file TEXT NOT NULL, source_game_index INTEGER NOT NULL,
            split TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS positions (
            position_key TEXT PRIMARY KEY, position_id TEXT NOT NULL UNIQUE, game_id TEXT NOT NULL,
            split TEXT NOT NULL, side TEXT NOT NULL, phase TEXT NOT NULL, result_class TEXT NOT NULL,
            sample_rank TEXT NOT NULL, payload TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS positions_stratum_rank
            ON positions(split, side, phase, result_class, sample_rank, position_id);
        CREATE INDEX IF NOT EXISTS positions_game ON positions(game_id);
        CREATE TABLE IF NOT EXISTS skipped (
            source_file TEXT NOT NULL, source_game_index INTEGER NOT NULL, reason_code TEXT NOT NULL,
            payload TEXT NOT NULL, PRIMARY KEY(source_file, source_game_index)
        );
    """)
    return connection


def initialize_database(connection: sqlite3.Connection, config_hash: str,
                        sources: Sequence[Mapping[str, Any]], resume: bool) -> None:
    stored = connection.execute("SELECT value FROM meta WHERE key='configurationChecksum'").fetchone()
    if stored:
        if stored[0] != config_hash:
            raise DatasetError("dataset resume configuration or input checksum changed")
        if not resume:
            raise DatasetError("incomplete dataset work exists; use --resume or --force")
    else:
        connection.execute("INSERT INTO meta VALUES('schemaVersion', ?)", (str(DATABASE_SCHEMA_VERSION),))
        connection.execute("INSERT INTO meta VALUES('configurationChecksum', ?)", (config_hash,))
        for source in sources:
            connection.execute("INSERT INTO sources(path,checksum,size) VALUES(?,?,?)",
                               (source["path"], source["sha256"], source["size"]))
        connection.commit()
    if connection.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
        raise DatasetError("SQLite integrity check failed")


def stratum_quota(cap: int, index: int) -> int:
    base, extra = divmod(cap, len(SPLITS) * len(SIDES) * len(PHASES) * len(RESULT_CLASSES))
    return base + (1 if index < extra else 0)


def prune_pool(connection: sqlite3.Connection, cap: int) -> None:
    index = 0
    for split in SPLITS:
        for side in SIDES:
            for phase in PHASES:
                for outcome in RESULT_CLASSES:
                    quota = stratum_quota(cap, index); index += 1
                    connection.execute("""
                        DELETE FROM positions WHERE rowid IN (
                            SELECT rowid FROM positions
                            WHERE split=? AND side=? AND phase=? AND result_class=?
                            ORDER BY sample_rank, position_id LIMIT -1 OFFSET ?
                        )
                    """, (split, side, phase, outcome, quota))


def skipped_record(source: str, index: int, code: str, detail: str,
                   diagnostics: Sequence[str] = ()) -> dict[str, Any]:
    return {"detail": detail, "diagnostics": list(diagnostics), "reasonCode": code,
            "sourceFile": source, "sourceGameIndex": index}


def record_skip(connection: sqlite3.Connection, record: Mapping[str, Any]) -> None:
    connection.execute("INSERT OR REPLACE INTO skipped VALUES(?,?,?,?)", (
        record["sourceFile"], record["sourceGameIndex"], record["reasonCode"], canonical_json(record)))


def process_game(connection: sqlite3.Connection, game: Any, source_name: str, game_index: int,
                 config: BuildConfig) -> tuple[str, int, int, dict[str, int]]:
    diagnostics = [str(error) for error in getattr(game, "errors", [])]
    header_result = getattr(game, "phase12_header_result", None)
    movetext_result = getattr(game, "phase12_movetext_result", None)
    variant = normalize_header(str(game.headers.get("Variant", "Standard"))).lower() or "standard"
    reason: tuple[str, str] | None = None
    if diagnostics:
        reason = ("pgn-parser-diagnostics", "parser reported fatal game errors")
    elif variant not in {"standard", "chess"}:
        reason = ("unsupported-variant", f"Variant is {variant!r}")
    elif header_result not in RESULTS or movetext_result not in RESULTS:
        reason = ("unsupported-result", f"result is {header_result!r}/{movetext_result!r}")
    elif header_result != movetext_result:
        reason = ("conflicting-result", "header and movetext results differ")
    elif "FEN" in game.headers and game.headers.get("SetUp") != "1":
        reason = ("invalid-setup", "FEN header requires SetUp=1")
    elif game.headers.get("SetUp") == "1" and "FEN" not in game.headers:
        reason = ("invalid-setup", "SetUp=1 requires a FEN header")
    if reason:
        record_skip(connection, skipped_record(source_name, game_index, *reason, diagnostics))
        if config.strict:
            raise DatasetError(f"{source_name}:{game_index}: {reason[0]}: {reason[1]}")
        return "rejected", 0, 0, {}
    try:
        moves = [move.uci() for move in game.mainline_moves()]
        game_id = stable_game_id(game, moves, str(header_result))
        if connection.execute("SELECT 1 FROM games WHERE game_id=?", (game_id,)).fetchone():
            record_skip(connection, skipped_record(source_name, game_index, "duplicate-game", "canonical game identity already accepted"))
            return "duplicate", 0, 0, {}
        records = extract_game_positions(game, source_name, game_index, config.dataset_version, str(header_result), game_id)
        if not records:
            record_skip(connection, skipped_record(source_name, game_index, "empty-mainline", "game has no mainline moves"))
            return "rejected", 0, 0, {}
        filtered, filter_counts = apply_filters(records, config)
        selected = per_game_sample(filtered, config, game_id)
        split = split_for_game(game_id, config.split_seed, (config.train_ratio, config.validation_ratio, config.test_ratio))
        connection.execute("INSERT INTO games VALUES(?,?,?,?)", (game_id, source_name, game_index, split))
        for record in selected:
            record["split"] = split
            rank = sampling_rank(config, record)
            payload = canonical_json(record)
            connection.execute("""
                INSERT INTO positions(position_key,position_id,game_id,split,side,phase,result_class,sample_rank,payload)
                VALUES(?,?,?,?,?,?,?,?,?)
                ON CONFLICT(position_key) DO UPDATE SET
                    position_id=excluded.position_id, game_id=excluded.game_id, split=excluded.split,
                    side=excluded.side, phase=excluded.phase, result_class=excluded.result_class,
                    sample_rank=excluded.sample_rank, payload=excluded.payload
                WHERE excluded.sample_rank < positions.sample_rank
                   OR (excluded.sample_rank = positions.sample_rank AND excluded.position_id < positions.position_id)
            """, (record["positionKey"], record["positionId"], game_id, split, record["sideToMove"],
                  record["gamePhase"], result_class(record["result"]), rank, payload))
        return "accepted", len(filtered), len(selected), filter_counts
    except Exception as error:
        record_skip(connection, skipped_record(source_name, game_index, "illegal-or-invalid-game", str(error), diagnostics))
        if config.strict:
            raise DatasetError(f"{source_name}:{game_index}: {error}") from error
        return "rejected", 0, 0, {}


def progress_report(connection: sqlite3.Connection, source: str, files_completed: int,
                    total_files: int, bytes_read: int, total_bytes: int, started: float,
                    maximum_rss_mb: int, database: Path) -> None:
    totals = connection.execute("SELECT COALESCE(SUM(attempted),0),COALESCE(SUM(accepted),0),"
                                "COALESCE(SUM(rejected),0),COALESCE(SUM(duplicates),0),"
                                "COALESCE(SUM(positions_considered),0) FROM sources").fetchone()
    retained = connection.execute("SELECT COUNT(*) FROM positions").fetchone()[0]
    rss = current_rss_mb()
    if rss > maximum_rss_mb:
        raise DatasetError(f"dataset RSS guardrail exceeded: {rss:.1f} MiB > {maximum_rss_mb} MiB")
    elapsed = max(time.monotonic() - started, 0.001)
    eta = (elapsed * (total_bytes - bytes_read) / bytes_read) if bytes_read else None
    size = database.stat().st_size if database.exists() else 0
    print(f"PROGRESS file={source} files={files_completed}/{total_files} bytes={bytes_read}/{total_bytes} "
          f"games={totals[0]} accepted={totals[1]} rejected={totals[2]} duplicates={totals[3]} "
          f"considered={totals[4]} retained={retained} rssMiB={rss:.1f} sqliteBytes={size} "
          f"elapsedSeconds={elapsed:.1f} etaSeconds={eta:.1f}" if eta is not None else "", flush=True)


def stream_jsonl(path: Path, rows: Iterable[Mapping[str, Any] | str]) -> tuple[str, int]:
    temporary = path.with_name(f".{path.name}.tmp")
    digest, count = hashlib.sha256(), 0
    try:
        with temporary.open("wb") as handle:
            for row in rows:
                line = (row if isinstance(row, str) else canonical_json(row)) + "\n"
                data = line.encode("utf-8"); handle.write(data); digest.update(data); count += 1
            handle.flush(); os.fsync(handle.fileno())
        os.replace(temporary, path)
    except Exception:
        with contextlib.suppress(OSError): temporary.unlink()
        raise
    return "sha256:" + digest.hexdigest(), count


def database_logical_checksum(connection: sqlite3.Connection) -> str:
    digest = hashlib.sha256()
    for table, columns, order in (
        ("games", "game_id,source_file,source_game_index,split", "game_id"),
        ("positions", "position_key,position_id,game_id,split,side,phase,result_class,sample_rank,payload", "position_key"),
        ("skipped", "source_file,source_game_index,reason_code,payload", "source_file,source_game_index"),
    ):
        for row in connection.execute(f"SELECT {columns} FROM {table} ORDER BY {order}"):
            digest.update(canonical_json(list(row)).encode()); digest.update(b"\n")
    return "sha256:" + digest.hexdigest()


def iter_positions(dataset_dir: Path, split: str = "all") -> Iterator[dict[str, Any]]:
    path = dataset_dir / "positions.jsonl"
    with path.open("r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise DatasetError(f"positions.jsonl:{number}: {error}") from error
            if split == "all" or record.get("split") == split:
                yield record


def select_balanced_positions(dataset_dir: Path, split: str, count: int,
                              max_per_game: int, seed: str,
                              excluded_games: Iterable[str] = ()) -> list[dict[str, Any]]:
    """Select a bounded balanced set directly from the dataset database.

    SQLite performs the deterministic ordering using a registered SHA-256
    function. Only the requested records are materialized in Python.
    """
    if split not in SPLITS or count < 1 or max_per_game < 1:
        raise DatasetError("invalid balanced selection request")
    database = dataset_dir / "work/dataset.sqlite"
    if not database.is_file():
        raise DatasetError(f"dataset selection database is missing: {database}")
    connection = sqlite3.connect(database)
    connection.create_function("selection_rank", 1, lambda position_id:
        hashlib.sha256((seed + "\0" + str(position_id)).encode()).hexdigest(), deterministic=True)
    try:
        connection.execute("CREATE TEMP TABLE excluded_games(game_id TEXT PRIMARY KEY)")
        connection.executemany("INSERT OR IGNORE INTO excluded_games VALUES(?)", ((item,) for item in excluded_games))
        chosen: list[dict[str, Any]] = []
        chosen_ids: set[str] = set(); game_counts: Counter[str] = Counter()
        strata = [(side, phase, outcome) for side in SIDES for phase in PHASES for outcome in RESULT_CLASSES]
        base, extra = divmod(count, len(strata))
        for index, (side, phase, outcome) in enumerate(strata):
            target = base + (index < extra); accepted = 0
            cursor = connection.execute("""
                SELECT position_id,game_id,payload FROM positions
                WHERE split=? AND side=? AND phase=? AND result_class=?
                  AND game_id NOT IN (SELECT game_id FROM excluded_games)
                ORDER BY selection_rank(position_id),position_id
            """, (split, side, phase, outcome))
            for position_id, game_id, payload in cursor:
                if game_counts[game_id] >= max_per_game: continue
                chosen.append(json.loads(payload)); chosen_ids.add(position_id); game_counts[game_id] += 1; accepted += 1
                if accepted >= target: break
        if len(chosen) < count:
            cursor = connection.execute("""
                SELECT position_id,game_id,payload FROM positions
                WHERE split=? AND game_id NOT IN (SELECT game_id FROM excluded_games)
                ORDER BY selection_rank(position_id),position_id
            """, (split,))
            for position_id, game_id, payload in cursor:
                if position_id in chosen_ids or game_counts[game_id] >= max_per_game: continue
                chosen.append(json.loads(payload)); chosen_ids.add(position_id); game_counts[game_id] += 1
                if len(chosen) >= count: break
        if len(chosen) != count:
            raise DatasetError(f"Could select only {len(chosen)}/{count} {split} positions with max-per-game={max_per_game}")
        return chosen
    finally:
        connection.close()


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    """Compatibility helper for small artifacts; corpus code uses iterators."""
    records = []
    with path.open("r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            if line.strip():
                try: records.append(json.loads(line))
                except json.JSONDecodeError as error: raise DatasetError(f"{path.name}:{number}: {error}") from error
    return records


def jsonl_bytes(records: Sequence[dict[str, Any]]) -> bytes:
    """Compatibility helper restricted to small test/selection artifacts."""
    return b"".join((canonical_json(record) + "\n").encode() for record in records) or b"\n"


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(data); handle.flush(); os.fsync(handle.fileno())


def validate_position_record(record: Mapping[str, Any], dataset_version: str) -> None:
    required = {"schemaVersion", "datasetVersion", "positionId", "gameId", "sourceFile",
                "sourceGameIndex", "ply", "sideToMove", "fen", "positionKey", "result",
                "resultFromSideToMove", "gamePhase", "split"}
    if required - record.keys():
        raise DatasetError(f"Position record missing fields: {sorted(required-record.keys())}")
    if record["schemaVersion"] != SCHEMA_VERSION or record["datasetVersion"] != dataset_version:
        raise DatasetError("Position schema or dataset version mismatch")
    board = chess.Board(record["fen"])
    if not board.is_valid() or position_key(board) != record["positionKey"]:
        raise DatasetError("invalid or non-canonical position state")
    if record["split"] not in SPLITS or record["sideToMove"] not in SIDES or record["gamePhase"] not in PHASES:
        raise DatasetError("invalid position stratum")
    if Path(str(record["sourceFile"])).is_absolute():
        raise DatasetError("absolute source path stored in position record")


def validate_dataset(dataset_dir: Path) -> dict[str, Any]:
    require_python_chess()
    manifest_path = dataset_dir / "manifest.json"
    if not manifest_path.is_file():
        raise DatasetError(f"Missing manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise DatasetError("Manifest schema version mismatch")
    dataset_version = manifest.get("datasetVersion")
    for name in CANONICAL_ARTIFACTS:
        path = dataset_dir / name
        if not path.is_file() or sha256_file(path) != manifest.get("artifacts", {}).get(name, {}).get("sha256"):
            raise DatasetError(f"Artifact checksum mismatch: {name}")
    count, position_ids, position_keys, game_splits = 0, set(), set(), {}
    split_counts, side_counts, phase_counts, result_counts = Counter(), Counter(), Counter(), Counter()
    for record in iter_positions(dataset_dir):
        validate_position_record(record, dataset_version)
        if record["positionId"] in position_ids or record["positionKey"] in position_keys:
            raise DatasetError("duplicate position identity")
        position_ids.add(record["positionId"]); position_keys.add(record["positionKey"])
        previous = game_splits.setdefault(record["gameId"], record["split"])
        if previous != record["split"]:
            raise DatasetError("Cross-split game leakage")
        count += 1; split_counts[record["split"]] += 1; side_counts[record["sideToMove"]] += 1
        phase_counts[record["gamePhase"]] += 1; result_counts[record["result"]] += 1
    if count != manifest.get("counts", {}).get("finalPositions"):
        raise DatasetError("Manifest position count mismatch")
    return {"manifest": manifest, "positions": count, "splitPositions": dict(split_counts),
            "sideCounts": dict(side_counts), "phaseCounts": dict(phase_counts), "resultCounts": dict(result_counts)}


def export_dataset(connection: sqlite3.Connection, config: BuildConfig,
                   sources: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    output = config.output_dir
    positions_checksum, positions_count = stream_jsonl(output / "positions.jsonl", (
        row[0] for row in connection.execute("SELECT payload FROM positions ORDER BY "
            "json_extract(payload,'$.sourceFile'),json_extract(payload,'$.sourceGameIndex'),json_extract(payload,'$.ply'),position_id")
    ))
    skipped_checksum, skipped_count = stream_jsonl(output / "skipped-games.jsonl", (
        row[0] for row in connection.execute("SELECT payload FROM skipped ORDER BY source_file,source_game_index")
    ))
    stats = connection.execute("SELECT COALESCE(SUM(attempted),0),COALESCE(SUM(accepted),0),"
                               "COALESCE(SUM(rejected),0),COALESCE(SUM(duplicates),0),"
                               "COALESCE(SUM(positions_considered),0),COALESCE(SUM(candidates),0) FROM sources").fetchone()
    split_counts = dict(connection.execute("SELECT split,COUNT(*) FROM positions GROUP BY split"))
    split_games = dict(connection.execute("SELECT split,COUNT(*) FROM games GROUP BY split"))
    phase_counts = dict(connection.execute("SELECT phase,COUNT(*) FROM positions GROUP BY phase"))
    side_counts = dict(connection.execute("SELECT side,COUNT(*) FROM positions GROUP BY side"))
    result_counts = dict(connection.execute("SELECT result_class,COUNT(*) FROM positions GROUP BY result_class"))
    represented = connection.execute("SELECT COUNT(DISTINCT game_id) FROM positions").fetchone()[0]
    summary = {
        "schemaVersion": SCHEMA_VERSION, "corpusStatus": "bounded-deterministic-sample",
        "games": {"attempted": stats[0], "accepted": stats[1], "rejected": stats[2],
                  "skipped": stats[2] + stats[3], "duplicate": stats[3], "represented": represented},
        "positions": {"considered": stats[4], "perGameCandidates": stats[5],
                      "deduplicatedOrEvicted": stats[5] - positions_count, "retained": positions_count,
                      "final": positions_count},
        "splitPositions": {split: split_counts.get(split, 0) for split in SPLITS},
        "splitGames": {split: split_games.get(split, 0) for split in SPLITS},
        "phaseCounts": {phase: phase_counts.get(phase, 0) for phase in PHASES},
        "sideToMoveCounts": {side: side_counts.get(side, 0) for side in SIDES},
        "resultCounts": {result: result_counts.get(result, 0) for result in RESULT_CLASSES},
        "bounded": True, "maximumRetainedPositions": config.maximum_retained_positions,
        "warnings": [],
    }
    write_bytes(output / "summary.json", pretty_json(summary).encode())
    artifacts = {
        "positions.jsonl": {"sha256": positions_checksum, "records": positions_count},
        "skipped-games.jsonl": {"sha256": skipped_checksum, "records": skipped_count},
        "summary.json": {"sha256": sha256_file(output / "summary.json")},
    }
    source_rows = [dict(row) for row in sources]
    db_logical = database_logical_checksum(connection)
    manifest = {
        "schemaVersion": SCHEMA_VERSION, "datasetId": config.dataset_version,
        "datasetVersion": config.dataset_version, "tool": {"name": "pgn_dataset.py", "version": TOOL_VERSION},
        "parser": {"distribution": "python-chess", "distributionVersion": importlib.metadata.version("python-chess"),
                   "libraryVersion": chess.__version__},
        "sources": source_rows, "counts": {"sourceFiles": len(sources), "finalPositions": positions_count},
        "configuration": {"filters": {"minPly": config.min_ply, "sampleEvery": config.sample_every,
                                         "includeTerminal": config.include_terminal},
                          "splitRatios": {"train": config.train_ratio, "validation": config.validation_ratio, "test": config.test_ratio},
                          "splitSeed": config.split_seed, "samplingSeed": config.sampling_seed,
                          "maximumAcceptedGames": config.maximum_accepted_games,
                          "scanAllInputGames": config.scan_all_input_games,
                          "maximumRetainedPositions": config.maximum_retained_positions,
                          "maximumPositionsPerGame": config.maximum_positions_per_game,
                          "phaseQuotas": dict(config.phase_quotas)},
        "storage": {"canonicalRecords": "positions.jsonl", "duplicateSplitArtifacts": False,
                    "databaseSchemaVersion": DATABASE_SCHEMA_VERSION, "databaseLogicalSha256": db_logical},
        "artifacts": artifacts,
    }
    write_bytes(output / "manifest.json", pretty_json(manifest).encode())
    return validate_dataset(output)


def build_dataset(config: BuildConfig) -> dict[str, Any]:
    require_python_chess(); validate_config(config)
    files = sorted(discover_pgn_files(config.pgn_dir), key=lambda path: path.name)
    sources = [{"path": path.relative_to(config.pgn_dir).as_posix(), "sha256": sha256_file(path),
                "size": path.stat().st_size} for path in files]
    output, work = config.output_dir, config.output_dir / "work"
    if (output / "manifest.json").exists():
        if not config.force: raise DatasetError(f"Output directory already exists; use --force: {output}")
        shutil.rmtree(output)
    elif output.exists() and not config.resume:
        if config.force: shutil.rmtree(output)
        else: raise DatasetError(f"Incomplete output exists; use --resume or --force: {output}")
    output.mkdir(parents=True, exist_ok=True); database = work / "dataset.sqlite"
    connection = open_database(database)
    config_hash = configuration_identity(config, sources)
    started, total_bytes, files_completed, global_accepted = time.monotonic(), sum(item["size"] for item in sources), 0, 0
    try:
        try:
            initialize_database(connection, config_hash, sources, config.resume)
        except DatasetError as error:
            if not (config.resume and config.force and "configuration or input checksum changed" in str(error)):
                raise
            connection.close(); shutil.rmtree(output); output.mkdir(parents=True); work = output / "work"; database = work / "dataset.sqlite"
            connection = open_database(database); initialize_database(connection, config_hash, sources, False)
        global_accepted = connection.execute("SELECT COALESCE(SUM(accepted),0) FROM sources").fetchone()[0]
        for file_index, (path, source) in enumerate(zip(files, sources)):
            state = connection.execute("SELECT status,byte_offset,game_index,attempted,accepted,rejected,duplicates,positions_considered,candidates FROM sources WHERE path=?", (source["path"],)).fetchone()
            if state[0] in {"completed", "capped"}:
                files_completed += 1
                if state[0] == "capped": break
                continue
            offset, game_index = state[1], state[2]
            counters = Counter({"attempted": state[3], "accepted": state[4], "rejected": state[5],
                                "duplicates": state[6], "positions_considered": state[7], "candidates": state[8]})
            with path.open("r", encoding="utf-8-sig", errors="strict") as handle:
                if offset: handle.seek(offset)
                batch = 0
                while True:
                    try: game = chess.pgn.read_game(handle, Visitor=ResultTrackingBuilder)
                    except Exception as error:
                        counters["attempted"] += 1; counters["rejected"] += 1
                        record_skip(connection, skipped_record(source["path"], game_index, "pgn-parse-error", str(error)))
                        if config.strict: raise DatasetError(f"{source['path']}:{game_index}: {error}") from error
                        game = None
                    if game is None: break
                    counters["attempted"] += 1
                    outcome, considered, candidates, _ = process_game(connection, game, source["path"], game_index, config)
                    counters[outcome if outcome != "duplicate" else "duplicates"] += 1
                    if outcome == "accepted":
                        global_accepted += 1; counters["positions_considered"] += considered; counters["candidates"] += candidates
                    game_index += 1; batch += 1; offset = handle.tell()
                    capped = config.maximum_accepted_games is not None and global_accepted >= config.maximum_accepted_games
                    if batch >= config.transaction_games or capped:
                        prune_pool(connection, config.maximum_retained_positions)
                        connection.execute("UPDATE sources SET byte_offset=?,game_index=?,attempted=?,accepted=?,rejected=?,duplicates=?,positions_considered=?,candidates=?,status=? WHERE path=?",
                                           (offset, game_index, counters["attempted"], counters["accepted"], counters["rejected"], counters["duplicates"], counters["positions_considered"], counters["candidates"], "capped" if capped else "running", source["path"]))
                        connection.commit(); batch = 0
                        if game_index % config.progress_games < config.transaction_games or capped:
                            bytes_read = sum(item["size"] for item in sources[:file_index]) + offset
                            progress_report(connection, source["path"], files_completed, len(files), bytes_read, total_bytes, started, config.maximum_rss_mb, database)
                        if capped: break
                    rss = current_rss_mb()
                    if rss >= config.warning_rss_mb and game_index % config.transaction_games == 1:
                        print(f"WARNING dataset RSS is {rss:.1f} MiB (hard limit {config.maximum_rss_mb} MiB)", file=sys.stderr)
                    if rss > config.maximum_rss_mb:
                        raise DatasetError(f"dataset RSS guardrail exceeded: {rss:.1f} MiB > {config.maximum_rss_mb} MiB")
                capped = config.maximum_accepted_games is not None and global_accepted >= config.maximum_accepted_games
                prune_pool(connection, config.maximum_retained_positions)
                connection.execute("UPDATE sources SET byte_offset=?,game_index=?,attempted=?,accepted=?,rejected=?,duplicates=?,positions_considered=?,candidates=?,status=? WHERE path=?",
                                   (offset, game_index, counters["attempted"], counters["accepted"], counters["rejected"], counters["duplicates"], counters["positions_considered"], counters["candidates"], "capped" if capped else "completed", source["path"]))
                connection.commit()
                if not capped: files_completed += 1
                completed_bytes = sum(item["size"] for item in sources[:file_index]) + (offset if capped else source["size"])
                progress_report(connection, source["path"], files_completed, len(files), completed_bytes, total_bytes, started, config.maximum_rss_mb, database)
                if capped: break
        accepted = connection.execute("SELECT COUNT(*) FROM games").fetchone()[0]
        retained = connection.execute("SELECT COUNT(*) FROM positions").fetchone()[0]
        if not accepted or not retained: raise DatasetError("No valid games or retained positions remain")
        connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        validation = export_dataset(connection, config, sources)
        write_bytes(work / "checkpoint.json", pretty_json({"schemaVersion": DATABASE_SCHEMA_VERSION,
                    "configurationChecksum": config_hash, "status": "completed", "gamesAccepted": accepted,
                    "positionsRetained": retained, "peakRssMb": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024}).encode())
        return validation
    except (KeyboardInterrupt, OSError, sqlite3.Error) as error:
        connection.rollback()
        for temporary in output.glob(".*.tmp"): temporary.unlink(missing_ok=True)
        if isinstance(error, KeyboardInterrupt): raise
        raise DatasetError(f"resumable dataset build failed: {error}") from error
    finally:
        connection.close()


def count_pgn_games(path: Path) -> int:
    count, first = 0, True
    with path.open("rb") as handle:
        for line in handle:
            if line.startswith(b"[Event "):
                count += 1
            first = False
    return count


def estimate_corpus(config: BuildConfig, samples_per_file: int = 30) -> dict[str, Any]:
    require_python_chess(); validate_config(config)
    files = discover_pgn_files(config.pgn_dir); rows = []
    started = time.monotonic()
    for path in files:
        size = path.stat().st_size
        for sample_index in range(samples_per_file):
            fraction = sample_index / samples_per_file
            with path.open("rb") as raw:
                raw.seek(int(size * fraction))
                if fraction: raw.readline()
                while True:
                    offset = raw.tell(); line = raw.readline()
                    if not line or line.startswith(b"[Event "): break
            with path.open("r", encoding="utf-8", errors="strict") as handle:
                handle.seek(offset); game = chess.pgn.read_game(handle, Visitor=ResultTrackingBuilder)
            if game is None: continue
            header_result, movetext_result = getattr(game, "phase12_header_result", None), getattr(game, "phase12_movetext_result", None)
            if getattr(game, "errors", []) or header_result not in RESULTS or header_result != movetext_result: continue
            moves = [move.uci() for move in game.mainline_moves()]; game_id = stable_game_id(game, moves, str(header_result))
            extracted = extract_game_positions(game, path.name, sample_index, config.dataset_version, str(header_result), game_id)
            filtered, _ = apply_filters(extracted, config); retained = per_game_sample(filtered, config, game_id)
            rows.append((len(extracted), len(filtered), len(retained)))
    sample_seconds = time.monotonic() - started
    games = sum(count_pgn_games(path) for path in files)
    accepted_limit = min(games, config.maximum_accepted_games) if config.maximum_accepted_games else games
    mean_eligible = sum(row[1] for row in rows) / len(rows) if rows else 0
    mean_retained = sum(row[2] for row in rows) / len(rows) if rows else 0
    retained = min(config.maximum_retained_positions, int(accepted_limit * mean_retained))
    bytes_per_record, sqlite_per_record = 900, 2_500
    return {
        "schemaVersion": 1,
        "measured": {"sampledGames": len(rows), "sampleSeconds": sample_seconds,
                     "eligiblePositionsPerGame": mean_eligible, "sampledRetainedPositionsPerGame": mean_retained,
                     "sampleParseGamesPerSecond": len(rows) / sample_seconds if sample_seconds else None},
        "estimated": {"inputGames": games, "acceptedGamesScanned": accepted_limit,
                      "retainedPositions": retained,
                      "datasetStageSeconds": accepted_limit / (len(rows) / sample_seconds) if rows and sample_seconds else None,
                      "sqliteBytes": retained * sqlite_per_record, "exportBytes": retained * bytes_per_record,
                      "temporaryBytes": retained * bytes_per_record,
                      "requiredWorkingBytes": retained * (sqlite_per_record + 2 * bytes_per_record)},
        "inputs": {"files": [path.name for path in files], "bytes": sum(path.stat().st_size for path in files)},
        "policy": {"maximumAcceptedGames": config.maximum_accepted_games, "scanAllInputGames": config.scan_all_input_games,
                   "maximumRetainedPositions": config.maximum_retained_positions,
                   "maximumPositionsPerGame": config.maximum_positions_per_game,
                   "maximumDatasetRssMb": config.maximum_rss_mb},
    }


def print_summary(dataset_dir: Path, validation: Mapping[str, Any]) -> None:
    summary = json.loads((dataset_dir / "summary.json").read_text(encoding="utf-8"))
    print(f"Games attempted: {summary['games']['attempted']}")
    print(f"Games accepted: {summary['games']['accepted']}")
    print(f"Duplicate games: {summary['games']['duplicate']}")
    print(f"Positions considered: {summary['positions']['considered']}")
    print(f"Positions retained: {summary['positions']['retained']}")
    print(f"Output path: {dataset_dir}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__); commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="derive a bounded deterministic dataset from PGNs")
    build.add_argument("--pgn-dir", type=Path, default=Path("tuning/pgn")); build.add_argument("--output-dir", type=Path, default=Path("tuning/datasets/pgn-derived-v1"))
    build.add_argument("--dataset-version", default=DATASET_VERSION); build.add_argument("--min-ply", type=int, default=12); build.add_argument("--sample-every", type=int, default=1)
    build.add_argument("--include-terminal", action="store_true"); build.add_argument("--train-ratio", type=int, default=80); build.add_argument("--validation-ratio", type=int, default=10); build.add_argument("--test-ratio", type=int, default=10)
    build.add_argument("--split-seed", default=DEFAULT_SPLIT_SEED); build.add_argument("--minimum-split-games", type=int, default=10); build.add_argument("--strict", action="store_true"); build.add_argument("--force", action="store_true"); build.add_argument("--resume", action="store_true")
    build.add_argument("--maximum-accepted-games", type=int); build.add_argument("--scan-all-input-games", action=argparse.BooleanOptionalAction, default=True)
    build.add_argument("--maximum-retained-positions", type=int, default=100_000); build.add_argument("--maximum-positions-per-game", type=int, default=6)
    build.add_argument("--opening-quota", type=int, default=2); build.add_argument("--middlegame-quota", type=int, default=3); build.add_argument("--endgame-quota", type=int, default=1)
    build.add_argument("--sampling-seed", default=DEFAULT_SAMPLING_SEED); build.add_argument("--transaction-games", type=int, default=250); build.add_argument("--progress-games", type=int, default=2_000)
    build.add_argument("--maximum-rss-mb", type=int, default=8_192); build.add_argument("--warning-rss-mb", type=int, default=6_144)
    inspect = commands.add_parser("inspect"); inspect.add_argument("--dataset-dir", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "inspect":
            validation = validate_dataset(args.dataset_dir); print_summary(args.dataset_dir, validation); return 0
        config = BuildConfig(pgn_dir=args.pgn_dir, output_dir=args.output_dir, dataset_version=args.dataset_version,
            min_ply=args.min_ply, sample_every=args.sample_every, include_terminal=args.include_terminal,
            train_ratio=args.train_ratio, validation_ratio=args.validation_ratio, test_ratio=args.test_ratio,
            split_seed=args.split_seed, minimum_split_games=args.minimum_split_games, strict=args.strict,
            force=args.force, resume=args.resume, maximum_accepted_games=args.maximum_accepted_games,
            scan_all_input_games=args.scan_all_input_games, maximum_retained_positions=args.maximum_retained_positions,
            maximum_positions_per_game=args.maximum_positions_per_game,
            phase_quotas={"opening": args.opening_quota, "middlegame": args.middlegame_quota, "endgame": args.endgame_quota},
            sampling_seed=args.sampling_seed, transaction_games=args.transaction_games, progress_games=args.progress_games,
            maximum_rss_mb=args.maximum_rss_mb, warning_rss_mb=args.warning_rss_mb)
        validation = build_dataset(config); print_summary(config.output_dir, validation); return 0
    except DatasetError as error:
        print(f"error: {error}", file=sys.stderr); return 2


if __name__ == "__main__":
    raise SystemExit(main())
