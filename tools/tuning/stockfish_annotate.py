#!/usr/bin/env python3
"""Deterministic, resumable Stockfish reference annotation for Phase 12 datasets."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import math
import os
import shutil
import stat
import sys
import tempfile
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Mapping, Protocol, Sequence

try:
    import chess
    import chess.engine
except ModuleNotFoundError:
    chess = None  # type: ignore[assignment]

import pgn_dataset


SCHEMA_VERSION = 1
ANNOTATION_VERSION = "stockfish-reference-v1"
TOOL_VERSION = "1"
DEFAULT_DATASET = Path("tuning/datasets/pgn-derived-v1")
DEFAULT_ENGINE = Path("tuning/engines/stockfish")
DEFAULT_OUTPUT = Path("tuning/annotations/stockfish-reference-v1")
SPLITS = ("train", "validation", "test")
JSONL_ARTIFACTS = ("annotations.jsonl", "train.jsonl", "validation.jsonl", "test.jsonl", "failures.jsonl")
CANONICAL_ARTIFACTS = (*JSONL_ARTIFACTS, "progress.json", "summary.json")


class AnnotationError(Exception):
    """Configuration, engine, annotation, or validation failure."""


class RetryableEngineError(AnnotationError):
    """An engine failure that permits one controlled retry after restart."""


class AnalysisTimeout(RetryableEngineError):
    pass


@dataclass(frozen=True)
class AnalysisLimit:
    type: str
    value: int

    def as_json(self) -> dict[str, Any]:
        return {"type": self.type, "value": self.value}


@dataclass(frozen=True)
class AnnotationConfig:
    dataset_dir: Path
    engine_path: Path
    output_dir: Path
    limit: AnalysisLimit
    threads: int = 1
    hash_mb: int = 256
    multipv: int = 1
    split: str = "all"
    max_positions: int | None = None
    start_after_position_id: str | None = None
    position_timeout_seconds: float = 120.0
    checkpoint_every: int = 10
    max_engine_restarts: int = 3
    strict: bool = False
    resume: bool = False
    force: bool = False
    annotation_version: str = ANNOTATION_VERSION


class EngineAdapter(Protocol):
    identity: dict[str, Any]

    def analyse(self, board: Any, limit: AnalysisLimit, multipv: int) -> list[dict[str, Any]]: ...
    def close(self) -> None: ...


def require_python_chess() -> None:
    if chess is None:
        raise AnnotationError(
            "python-chess is required. Install tools/tuning/requirements.txt into a virtual environment."
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


def jsonl_bytes(records: Sequence[dict[str, Any]]) -> bytes:
    return (("\n".join(canonical_json(record) for record in records) + "\n") if records else "\n").encode("utf-8")


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if not line:
                continue
            value = json.loads(line)
            if not isinstance(value, dict):
                raise AnnotationError(f"{path.name}:{number}: expected a JSON object")
            records.append(value)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AnnotationError(f"Invalid JSONL in {path}: {error}") from error
    return records


def write_bytes(path: Path, data: bytes) -> None:
    with path.open("wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())


def safe_relative_path(path: Path) -> str:
    resolved = path.resolve()
    return Path(os.path.relpath(resolved, Path.cwd().resolve())).as_posix()


def validate_engine_binary(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise AnnotationError(f"Stockfish binary does not exist: {path}")
    if not path.is_file():
        raise AnnotationError(f"Stockfish path is not a regular file: {path}")
    mode = path.stat().st_mode
    if not stat.S_ISREG(mode) or not os.access(path, os.X_OK):
        raise AnnotationError(f"Stockfish binary is not executable: {path}")
    return {
        "binaryPath": safe_relative_path(path),
        "engineBinarySha256": sha256_file(path),
        "engineFileName": path.name,
        "fileSize": path.stat().st_size,
    }


def serialize_option(option: Any) -> dict[str, Any]:
    value = {
        "type": option.type,
        "default": option.default,
        "minimum": option.min,
        "maximum": option.max,
    }
    variants = getattr(option, "var", None)
    if variants:
        value["variants"] = list(variants)
    return value


def deterministic_engine_options(threads: int, hash_mb: int, multipv: int,
                                 supported: Mapping[str, Any]) -> dict[str, Any]:
    requested: dict[str, Any] = {
        "Threads": threads,
        "Hash": hash_mb,
        "Ponder": False,
        "MultiPV": multipv,
        "SyzygyPath": "",
    }
    if "UCI_AnalyseMode" in supported:
        requested["UCI_AnalyseMode"] = True
    if "UCI_LimitStrength" in supported:
        requested["UCI_LimitStrength"] = False
    if "Skill Level" in supported:
        requested["Skill Level"] = 20
    return requested


def validate_engine_option_values(options: Mapping[str, Any], supported: Mapping[str, Any]) -> None:
    for name in ("Threads", "Hash", "MultiPV"):
        if name not in supported:
            raise AnnotationError(f"Stockfish does not expose required UCI option: {name}")
        option = supported[name]
        value = options[name]
        if option.min is not None and value < option.min or option.max is not None and value > option.max:
            raise AnnotationError(f"{name}={value} is outside supported range {option.min}..{option.max}")


def verify_engine(path: Path, timeout: float = 15.0) -> dict[str, Any]:
    require_python_chess()
    binary = validate_engine_binary(path)
    try:
        engine = chess.engine.SimpleEngine.popen_uci(str(path), timeout=timeout)
    except Exception as error:
        raise AnnotationError(f"Stockfish UCI handshake failed: {error}") from error
    try:
        engine.ping()
        name = str(engine.id.get("name", "")).strip()
        author = str(engine.id.get("author", "")).strip()
        if not name:
            raise AnnotationError("Stockfish did not report an engine name")
        if not author:
            raise AnnotationError("Stockfish did not report an engine author")
        return {
            **binary,
            "engineName": name,
            "engineAuthor": author,
            "supportedOptions": {
                option_name: serialize_option(option)
                for option_name, option in sorted(engine.options.items())
            },
        }
    except Exception as error:
        if isinstance(error, AnnotationError):
            raise
        raise AnnotationError(f"Stockfish readiness check failed: {error}") from error
    finally:
        try:
            engine.quit()
        except Exception:
            engine.close()


class StockfishAdapter:
    """python-chess UCI adapter with explicit deterministic configuration."""

    def __init__(self, config: AnnotationConfig, identity: dict[str, Any]) -> None:
        self.config = config
        self.identity = identity
        try:
            self.engine = chess.engine.SimpleEngine.popen_uci(
                str(config.engine_path), timeout=min(config.position_timeout_seconds, 30.0)
            )
            self.engine.timeout = config.position_timeout_seconds
            options = deterministic_engine_options(
                config.threads, config.hash_mb, config.multipv, self.engine.options,
            )
            validate_engine_option_values(options, self.engine.options)
            # Ponder and MultiPV are managed by python-chess's analysis command.
            configurable = {
                key: value for key, value in options.items()
                if key not in {"Ponder", "MultiPV"} and key in self.engine.options
            }
            self.engine.configure(configurable)
            self.engine.ping()
        except Exception as error:
            raise RetryableEngineError(f"engine_start_failed: {error}") from error

    def analyse(self, board: Any, limit: AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
        chess_limit = chess.engine.Limit(**{limit.type: limit.value})
        try:
            raw = self.engine.analyse(
                board,
                chess_limit,
                multipv=multipv,
                info=chess.engine.INFO_ALL,
            )
        except (TimeoutError, asyncio.TimeoutError) as error:
            raise AnalysisTimeout(f"analysis_timeout: {error}") from error
        except (chess.engine.EngineTerminatedError, chess.engine.EngineError) as error:
            raise RetryableEngineError(f"protocol_or_engine_crash: {error}") from error
        return raw if isinstance(raw, list) else [raw]

    def close(self) -> None:
        try:
            self.engine.quit()
        except Exception:
            self.engine.close()


def validate_config(config: AnnotationConfig) -> None:
    if config.limit.type not in ("nodes", "depth") or config.limit.value < 1:
        raise AnnotationError("Analysis limit must be positive nodes or depth")
    if config.threads < 1 or config.hash_mb < 1 or config.multipv < 1:
        raise AnnotationError("Threads, Hash, and MultiPV must be positive")
    if config.split not in (*SPLITS, "all"):
        raise AnnotationError("--split must be train, validation, test, or all")
    if config.max_positions is not None and config.max_positions < 1:
        raise AnnotationError("--max-positions must be positive")
    if config.position_timeout_seconds <= 0:
        raise AnnotationError("--position-timeout-seconds must be positive")
    if config.checkpoint_every < 1:
        raise AnnotationError("--checkpoint-every must be positive")
    if config.max_engine_restarts < 0:
        raise AnnotationError("--max-engine-restarts must be non-negative")


def load_source_dataset(dataset_dir: Path) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, Any]]:
    require_python_chess()
    try:
        validation = pgn_dataset.validate_dataset(dataset_dir)
    except pgn_dataset.DatasetError as error:
        raise AnnotationError(f"Phase 12 dataset validation failed: {error}") from error
    manifest_path = dataset_dir / "manifest.json"
    summary_path = dataset_dir / "summary.json"
    if not summary_path.is_file():
        raise AnnotationError(f"Missing Phase 12 summary: {summary_path}")
    manifest = validation["manifest"]
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    positions = pgn_dataset.read_jsonl(dataset_dir / "positions.jsonl")
    if len({record["positionId"] for record in positions}) != len(positions):
        raise AnnotationError("Phase 12 positions contain duplicate positionId values")
    return manifest, positions, {
        "datasetDirectory": safe_relative_path(dataset_dir),
        "datasetId": manifest["datasetId"],
        "datasetVersion": manifest["datasetVersion"],
        "manifestSha256": sha256_file(manifest_path),
        "positionsSha256": sha256_file(dataset_dir / "positions.jsonl"),
        "summarySha256": sha256_file(summary_path),
        "totalPositions": len(positions),
        "corpusStatus": summary.get("corpusStatus", "unknown"),
    }


def select_positions(
    positions: Sequence[dict[str, Any]], split: str, max_positions: int | None,
    start_after_position_id: str | None,
) -> list[dict[str, Any]]:
    after = start_after_position_id is None
    found = after
    selected: list[dict[str, Any]] = []
    for record in positions:
        if not after:
            if record["positionId"] == start_after_position_id:
                after = True
                found = True
            continue
        if split != "all" and record["split"] != split:
            continue
        selected.append(record)
        if max_positions is not None and len(selected) >= max_positions:
            break
    if not found:
        raise AnnotationError(f"--start-after-position-id was not found: {start_after_position_id}")
    if not selected:
        raise AnnotationError("Position selection is empty")
    return selected


def compatibility_payload(
    config: AnnotationConfig, source: dict[str, Any], identity: dict[str, Any],
    options: dict[str, Any], selected: Sequence[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "annotationVersion": config.annotation_version,
        "engine": {
            "engineAuthor": identity["engineAuthor"],
            "engineBinarySha256": identity["engineBinarySha256"],
            "engineName": identity["engineName"],
        },
        "engineOptions": options,
        "executionPolicy": {
            "checkpointEvery": config.checkpoint_every,
            "maxEngineRestarts": config.max_engine_restarts,
            "positionTimeoutSeconds": config.position_timeout_seconds,
            "strict": config.strict,
        },
        "limit": config.limit.as_json(),
        "multipv": config.multipv,
        "schemaVersion": SCHEMA_VERSION,
        "selection": {
            "maxPositions": config.max_positions,
            "selectedCount": len(selected),
            "selectedFirstPositionId": selected[0]["positionId"],
            "selectedLastPositionId": selected[-1]["positionId"],
            "split": config.split,
            "startAfterPositionId": config.start_after_position_id,
        },
        "sourceDataset": {
            "manifestSha256": source["manifestSha256"],
            "positionsSha256": source["positionsSha256"],
        },
    }


def annotation_id(position_id: str, compatibility: dict[str, Any]) -> str:
    identity = {
        "annotationVersion": compatibility["annotationVersion"],
        "engine": compatibility["engine"],
        "engineOptions": compatibility["engineOptions"],
        "limit": compatibility["limit"],
        "multipv": compatibility["multipv"],
        "positionId": position_id,
        "schemaVersion": compatibility["schemaVersion"],
    }
    return sha256_bytes(canonical_json(identity).encode("utf-8"))


def normalize_score(score: Any, board: Any) -> dict[str, Any]:
    if score is None:
        raise AnnotationError("missing_score: Stockfish returned no score")
    white_score = score.white()
    mate = white_score.mate()
    if mate is not None:
        stm_mate = score.pov(board.turn).mate()
        if mate == 0 or stm_mate is None:
            raise AnnotationError("invalid mate score for non-terminal analysis")
        return {
            "mateDistance": abs(mate),
            "mateFromSideToMove": stm_mate,
            "mateWinner": "white" if mate > 0 else "black",
            "scoreType": "mate",
        }
    cp = white_score.score()
    if cp is None:
        raise AnnotationError("missing_score: Stockfish returned neither cp nor mate")
    return {
        "scoreFromSideToMoveCp": cp if board.turn == chess.WHITE else -cp,
        "scoreType": "cp",
        "scoreWhiteCp": cp,
    }


def validate_pv(board: Any, moves: Sequence[Any]) -> list[str]:
    if not moves:
        raise AnnotationError("missing_pv: non-terminal analysis returned an empty PV")
    replay = board.copy(stack=False)
    result: list[str] = []
    for move in moves:
        if move not in replay.legal_moves:
            raise AnnotationError(f"illegal_pv: {move.uci()} is illegal after {result}")
        result.append(move.uci())
        replay.push(move)
    return result


def normalize_lines(board: Any, infos: Sequence[dict[str, Any]], multipv: int) -> list[dict[str, Any]]:
    if len(infos) != multipv:
        raise AnnotationError(f"missing_pv: expected {multipv} lines, got {len(infos)}")
    lines: list[dict[str, Any]] = []
    root_moves: set[str] = set()
    for expected_rank, info in enumerate(infos, start=1):
        rank = int(info.get("multipv", expected_rank))
        if rank != expected_rank:
            raise AnnotationError(f"multipv rank mismatch: expected {expected_rank}, got {rank}")
        pv = validate_pv(board, info.get("pv", []))
        if pv[0] in root_moves:
            raise AnnotationError(f"duplicate MultiPV root move: {pv[0]}")
        root_moves.add(pv[0])
        line = {
            "move": pv[0],
            "pv": pv,
            "rank": rank,
            **normalize_score(info.get("score"), board),
        }
        for source_key, target_key in (
            ("depth", "depth"), ("seldepth", "seldepth"), ("nodes", "nodes"),
            ("hashfull", "hashfull"), ("tbhits", "tbhits"),
        ):
            if source_key in info:
                line[target_key] = int(info[source_key])
        lines.append(line)
    return lines


def terminal_reason(board: Any) -> str:
    if board.is_checkmate():
        return "checkmate"
    if board.is_stalemate():
        return "stalemate"
    if board.is_insufficient_material():
        return "insufficient-material"
    if board.is_seventyfive_moves():
        return "seventyfive-move-rule"
    if board.is_fivefold_repetition():
        return "fivefold-repetition"
    return "automatic-draw"


def base_annotation(record: dict[str, Any], config: AnnotationConfig,
                    identity: dict[str, Any], compatibility: dict[str, Any]) -> dict[str, Any]:
    return {
        "annotationId": annotation_id(record["positionId"], compatibility),
        "annotationVersion": config.annotation_version,
        "engineBinarySha256": identity["engineBinarySha256"],
        "engineName": identity["engineName"],
        "fen": record["fen"],
        "gameId": record["gameId"],
        "gamePhase": record["gamePhase"],
        "limit": config.limit.as_json(),
        "multipv": config.multipv,
        "ply": record["ply"],
        "positionId": record["positionId"],
        "result": record["result"],
        "resultFromSideToMove": record["resultFromSideToMove"],
        "schemaVersion": SCHEMA_VERSION,
        "sideToMove": record["sideToMove"],
        "sourceFile": record["sourceFile"],
        "sourceGameIndex": record["sourceGameIndex"],
        "split": record["split"],
    }


def annotate_position(record: dict[str, Any], config: AnnotationConfig,
                      identity: dict[str, Any], compatibility: dict[str, Any],
                      adapter: EngineAdapter | None) -> dict[str, Any]:
    try:
        board = chess.Board(record["fen"])
    except ValueError as error:
        raise AnnotationError(f"invalid_fen: {error}") from error
    annotation = base_annotation(record, config, identity, compatibility)
    if board.is_game_over(claim_draw=False):
        annotation.update({
            "bestMoveUci": None,
            "lines": [],
            "principalVariationUci": [],
            "scoreType": "terminal",
            "terminalReason": terminal_reason(board),
        })
        if board.is_checkmate():
            annotation["terminalWinner"] = "black" if board.turn == chess.WHITE else "white"
        else:
            annotation["terminalWinner"] = "draw"
        return annotation
    if adapter is None:
        raise RetryableEngineError("engine_not_ready: adapter is unavailable")
    infos = adapter.analyse(board, config.limit, config.multipv)
    lines = normalize_lines(board, infos, config.multipv)
    primary = lines[0]
    annotation.update({
        "bestMoveUci": primary["move"],
        "lines": lines,
        "principalVariationUci": primary["pv"],
    })
    for key in (
        "scoreType", "scoreWhiteCp", "scoreFromSideToMoveCp", "mateWinner",
        "mateDistance", "mateFromSideToMove", "depth", "seldepth", "nodes",
        "hashfull", "tbhits",
    ):
        if key in primary:
            annotation[key] = primary[key]
    return annotation


def failure_record(record: dict[str, Any], error: Exception, retryable: bool) -> dict[str, Any]:
    detail = str(error).replace(str(Path.cwd().resolve()), "<repo>")
    code = detail.split(":", 1)[0].strip().replace("-", "_")
    allowed = {
        "invalid_fen", "engine_start_failed", "engine_not_ready", "analysis_timeout",
        "engine_crash", "missing_score", "missing_pv", "illegal_best_move",
        "illegal_pv", "protocol_error", "protocol_or_engine_crash", "unexpected_error",
    }
    if code not in allowed:
        code = "unexpected_error"
    return {
        "detail": detail,
        "engineState": "restart-required" if retryable else "usable-or-not-started",
        "fen": record["fen"],
        "positionId": record["positionId"],
        "reasonCode": code,
        "retryable": retryable,
        "split": record["split"],
    }


def append_durable(path: Path, record: dict[str, Any]) -> None:
    with path.open("a", encoding="utf-8", newline="\n") as handle:
        handle.write(canonical_json(record) + "\n")
        handle.flush()
        os.fsync(handle.fileno())


def work_paths(output_dir: Path) -> dict[str, Path]:
    work = output_dir.with_name(output_dir.name + ".work")
    return {
        "dir": work,
        "config": work / "config.json",
        "annotations": work / "completed.jsonl",
        "failures": work / "failures.jsonl",
        "progress": work / "progress.json",
    }


def prepare_work_state(config: AnnotationConfig, compatibility: dict[str, Any]) -> dict[str, Path]:
    paths = work_paths(config.output_dir)
    if config.force:
        if config.output_dir.exists():
            shutil.rmtree(config.output_dir)
        if paths["dir"].exists():
            shutil.rmtree(paths["dir"])
    if config.output_dir.exists():
        raise AnnotationError(f"Completed output exists; use a new directory or --force: {config.output_dir}")
    if paths["dir"].exists():
        if not config.resume:
            raise AnnotationError(f"Incomplete work state exists; use --resume or --force: {paths['dir']}")
        stored = json.loads(paths["config"].read_text(encoding="utf-8"))
        if stored != compatibility:
            raise AnnotationError("Resume configuration is incompatible with existing work state")
    else:
        if config.resume:
            raise AnnotationError(f"No incomplete work state exists to resume: {paths['dir']}")
        paths["dir"].parent.mkdir(parents=True, exist_ok=True)
        paths["dir"].mkdir()
        write_bytes(paths["config"], pretty_json(compatibility).encode("utf-8"))
        write_bytes(paths["annotations"], b"")
        write_bytes(paths["failures"], b"")
        write_bytes(paths["progress"], pretty_json({
            "annotations": 0, "complete": False, "failures": 0, "selected": compatibility["selection"]["selectedCount"]
        }).encode("utf-8"))
    return paths


def checkpoint(paths: dict[str, Path], selected: int, annotations: int,
               failures: int, restarts: int, last_position_id: str | None,
               complete: bool = False) -> None:
    write_bytes(paths["progress"], pretty_json({
        "annotations": annotations,
        "complete": complete,
        "engineRestarts": restarts,
        "failures": failures,
        "lastProcessedPositionId": last_position_id,
        "processed": annotations + failures,
        "selected": selected,
    }).encode("utf-8"))


def engine_factory_default(config: AnnotationConfig, identity: dict[str, Any]) -> EngineAdapter:
    return StockfishAdapter(config, identity)


def run_annotation_work(
    config: AnnotationConfig, identity: dict[str, Any], compatibility: dict[str, Any],
    selected: Sequence[dict[str, Any]], paths: dict[str, Path],
    adapter_factory: Callable[[AnnotationConfig, dict[str, Any]], EngineAdapter] = engine_factory_default,
) -> dict[str, Any]:
    existing_annotations = read_jsonl(paths["annotations"])
    existing_failures = read_jsonl(paths["failures"])
    completed = {record["positionId"] for record in (*existing_annotations, *existing_failures)}
    if len(completed) != len(existing_annotations) + len(existing_failures):
        raise AnnotationError("Incomplete work state contains duplicate position IDs")
    selected_ids = {record["positionId"] for record in selected}
    if not completed <= selected_ids:
        raise AnnotationError("Incomplete work state contains positions outside the current selection")

    adapter: EngineAdapter | None = None
    restarts = 0
    start = time.monotonic()
    elapsed_analysis = 0.0
    analysis_attempts = 0
    last_position_id: str | None = None

    def start_engine(is_restart: bool) -> EngineAdapter:
        nonlocal restarts
        if is_restart:
            if restarts >= config.max_engine_restarts:
                raise RetryableEngineError("engine_crash: maximum engine restart count exceeded")
            restarts += 1
        return adapter_factory(config, identity)

    try:
        for index, record in enumerate(selected, start=1):
            position_id = record["positionId"]
            if position_id in completed:
                last_position_id = position_id
                continue
            board = chess.Board(record["fen"])

            try:
                if not board.is_game_over(claim_draw=False) and adapter is None:
                    adapter = start_engine(False)
                analysis_start = time.monotonic()
                annotation = annotate_position(record, config, identity, compatibility, adapter)
                elapsed_analysis += time.monotonic() - analysis_start
                analysis_attempts += 1
            except RetryableEngineError as first_error:
                if adapter is not None:
                    adapter.close()
                    adapter = None
                try:
                    adapter = start_engine(True)
                    analysis_start = time.monotonic()
                    annotation = annotate_position(record, config, identity, compatibility, adapter)
                    elapsed_analysis += time.monotonic() - analysis_start
                    analysis_attempts += 1
                except Exception as final_error:
                    retryable = isinstance(final_error, RetryableEngineError)
                    failure = failure_record(record, final_error, retryable)
                    append_durable(paths["failures"], failure)
                    existing_failures.append(failure)
                    completed.add(position_id)
                    last_position_id = position_id
                    checkpoint(paths, len(selected), len(existing_annotations), len(existing_failures), restarts, last_position_id)
                    if config.strict:
                        raise AnnotationError(f"Strict annotation stopped at {position_id}: {final_error}") from final_error
                    continue
            except Exception as error:
                failure = failure_record(record, error, False)
                append_durable(paths["failures"], failure)
                existing_failures.append(failure)
                completed.add(position_id)
                last_position_id = position_id
                checkpoint(paths, len(selected), len(existing_annotations), len(existing_failures), restarts, last_position_id)
                if config.strict:
                    raise AnnotationError(f"Strict annotation stopped at {position_id}: {error}") from error
                continue

            append_durable(paths["annotations"], annotation)
            existing_annotations.append(annotation)
            completed.add(position_id)
            last_position_id = position_id
            if index % config.checkpoint_every == 0 or index == len(selected):
                checkpoint(paths, len(selected), len(existing_annotations), len(existing_failures), restarts, last_position_id)
    finally:
        if adapter is not None:
            adapter.close()

    if not existing_annotations:
        raise AnnotationError("No valid annotations were produced")
    checkpoint(paths, len(selected), len(existing_annotations), len(existing_failures), restarts, last_position_id, complete=True)
    return {
        "annotations": existing_annotations,
        "elapsedSeconds": time.monotonic() - start,
        "failures": existing_failures,
        "analysisSeconds": elapsed_analysis,
        "analysisAttempts": analysis_attempts,
        "engineRestarts": restarts,
    }


def summary_for_records(annotations: Sequence[dict[str, Any]], failures: Sequence[dict[str, Any]],
                        selected: int, available: int, restarts: int) -> dict[str, Any]:
    score_counts = Counter(record["scoreType"] for record in annotations)
    split_counts = Counter(record["split"] for record in annotations)
    depths = [record["depth"] for record in annotations if "depth" in record]
    nodes = [record["nodes"] for record in annotations if "nodes" in record]
    return {
        "averageDepth": sum(depths) / len(depths) if depths else None,
        "averageNodes": sum(nodes) / len(nodes) if nodes else None,
        "completeDatasetCoverage": selected == available,
        "coveragePercent": selected * 100.0 / available,
        "engineRestarts": restarts,
        "failures": len(failures),
        "positionsAnnotated": len(annotations),
        "positionsSelected": selected,
        "scoreCounts": {
            "cp": score_counts["cp"],
            "mate": score_counts["mate"],
            "terminal": score_counts["terminal"],
        },
        "splitCounts": {split: split_counts[split] for split in SPLITS},
        "sourcePositionsAvailable": available,
        "warnings": ([] if selected == available else ["annotation covers only a deterministic subset of the source dataset"]),
    }


def write_final_artifacts(
    temp_dir: Path, config: AnnotationConfig, source: dict[str, Any], identity: dict[str, Any],
    options: dict[str, Any], compatibility: dict[str, Any], selected: Sequence[dict[str, Any]],
    annotations: Sequence[dict[str, Any]], failures: Sequence[dict[str, Any]], restarts: int,
) -> None:
    order = {record["positionId"]: index for index, record in enumerate(selected)}
    sorted_annotations = sorted(annotations, key=lambda record: order[record["positionId"]])
    sorted_failures = sorted(failures, key=lambda record: order[record["positionId"]])
    records_by_file = {
        "annotations.jsonl": sorted_annotations,
        "train.jsonl": [record for record in sorted_annotations if record["split"] == "train"],
        "validation.jsonl": [record for record in sorted_annotations if record["split"] == "validation"],
        "test.jsonl": [record for record in sorted_annotations if record["split"] == "test"],
        "failures.jsonl": sorted_failures,
    }
    for name, records in records_by_file.items():
        write_bytes(temp_dir / name, jsonl_bytes(records))
    summary = summary_for_records(sorted_annotations, sorted_failures, len(selected), source["totalPositions"], restarts)
    progress = {
        "annotations": len(sorted_annotations),
        "complete": True,
        "failures": len(sorted_failures),
        "processed": len(sorted_annotations) + len(sorted_failures),
        "selected": len(selected),
    }
    write_bytes(temp_dir / "progress.json", pretty_json(progress).encode("utf-8"))
    write_bytes(temp_dir / "summary.json", pretty_json(summary).encode("utf-8"))
    artifacts: dict[str, dict[str, Any]] = {}
    for name in CANONICAL_ARTIFACTS:
        artifacts[name] = {"sha256": sha256_file(temp_dir / name)}
        if name in records_by_file:
            artifacts[name]["records"] = len(records_by_file[name])
    manifest = {
        "annotationVersion": config.annotation_version,
        "artifacts": artifacts,
        "canonicalFieldPolicy": {
            "included": "scores, PVs, deterministic search counters, identity, configuration, and source joins",
            "omittedAsVolatile": ["elapsedTime", "nps", "wallClockTimestamps"],
        },
        "completeDatasetCoverage": len(selected) == source["totalPositions"],
        "compatibility": compatibility,
        "counts": {
            "annotations": len(sorted_annotations),
            "failures": len(sorted_failures),
            "selectedPositions": len(selected),
            "totalAvailablePositions": source["totalPositions"],
        },
        "engine": {
            "binaryPath": identity["binaryPath"],
            "engineAuthor": identity["engineAuthor"],
            "engineBinarySha256": identity["engineBinarySha256"],
            "engineFileName": identity["engineFileName"],
            "engineName": identity["engineName"],
            "fileSize": identity["fileSize"],
            "supportedOptions": identity["supportedOptions"],
        },
        "engineOptions": options,
        "executionPolicy": compatibility["executionPolicy"],
        "limit": config.limit.as_json(),
        "multipv": config.multipv,
        "schemaVersion": SCHEMA_VERSION,
        "selection": compatibility["selection"],
        "sourceDataset": source,
        "tool": {"name": "stockfish_annotate.py", "version": TOOL_VERSION},
    }
    write_bytes(temp_dir / "manifest.json", pretty_json(manifest).encode("utf-8"))


def publish_final(temp_dir: Path, output_dir: Path) -> None:
    if output_dir.exists():
        raise AnnotationError(f"Refusing to replace completed output during publication: {output_dir}")
    temp_dir.rename(output_dir)


def validate_line(line: dict[str, Any], board: Any) -> None:
    if line.get("rank") is None or line.get("move") is None or not line.get("pv"):
        raise AnnotationError("Malformed analysis line")
    pv = []
    for move_text in line["pv"]:
        try:
            pv.append(chess.Move.from_uci(move_text))
        except ValueError as error:
            raise AnnotationError(f"Illegal PV UCI syntax: {move_text}") from error
    normalized = validate_pv(board, pv)
    if normalized != line["pv"] or line["move"] != line["pv"][0]:
        raise AnnotationError("PV or root move does not match legal normalized UCI sequence")


def validate_annotation_record(record: dict[str, Any], manifest: dict[str, Any]) -> None:
    required = {
        "annotationId", "annotationVersion", "schemaVersion", "positionId", "gameId",
        "split", "fen", "sideToMove", "result", "resultFromSideToMove", "gamePhase",
        "engineName", "engineBinarySha256", "limit", "multipv", "scoreType",
    }
    missing = required - record.keys()
    if missing:
        raise AnnotationError(f"Annotation record missing fields: {sorted(missing)}")
    if record["annotationVersion"] != manifest["annotationVersion"] or record["schemaVersion"] != SCHEMA_VERSION:
        raise AnnotationError("Annotation schema/version mismatch")
    if record["engineName"] != manifest["engine"]["engineName"] or record["engineBinarySha256"] != manifest["engine"]["engineBinarySha256"]:
        raise AnnotationError("Annotation engine identity mismatch")
    if record["split"] not in SPLITS or Path(record["sourceFile"]).is_absolute():
        raise AnnotationError("Invalid split or absolute source path")
    board = chess.Board(record["fen"])
    if not board.is_valid():
        raise AnnotationError("Invalid annotation FEN")
    if (record["sideToMove"] == "white") != board.turn:
        raise AnnotationError("Annotation side-to-move mismatch")
    expected_id = annotation_id(record["positionId"], manifest["compatibility"])
    if record["annotationId"] != expected_id:
        raise AnnotationError("Annotation ID mismatch")
    score_type = record["scoreType"]
    if score_type == "cp":
        if not isinstance(record.get("scoreWhiteCp"), int) or not isinstance(record.get("scoreFromSideToMoveCp"), int):
            raise AnnotationError("Invalid centipawn score schema")
        expected_stm = record["scoreWhiteCp"] if board.turn else -record["scoreWhiteCp"]
        if record["scoreFromSideToMoveCp"] != expected_stm:
            raise AnnotationError("Centipawn perspective mismatch")
    elif score_type == "mate":
        if record.get("mateWinner") not in ("white", "black") or not isinstance(record.get("mateDistance"), int) or record["mateDistance"] < 1:
            raise AnnotationError("Invalid mate score schema")
        expected_sign = 1 if record["mateWinner"] == record["sideToMove"] else -1
        if int(math.copysign(1, record.get("mateFromSideToMove", 0))) != expected_sign:
            raise AnnotationError("Mate perspective mismatch")
    elif score_type == "terminal":
        if not board.is_game_over(claim_draw=False) or record.get("bestMoveUci") is not None or record.get("lines") != []:
            raise AnnotationError("Invalid terminal annotation")
        return
    else:
        raise AnnotationError(f"Unknown score type: {score_type}")
    lines = record.get("lines")
    if not isinstance(lines, list) or len(lines) != manifest["multipv"]:
        raise AnnotationError("MultiPV line count mismatch")
    roots: set[str] = set()
    for expected_rank, line in enumerate(lines, start=1):
        if line.get("rank") != expected_rank:
            raise AnnotationError("MultiPV ranks are not ordered")
        validate_line(line, board)
        if line["move"] in roots:
            raise AnnotationError("Duplicate MultiPV root move")
        roots.add(line["move"])
    if record.get("bestMoveUci") != lines[0]["move"] or record.get("principalVariationUci") != lines[0]["pv"]:
        raise AnnotationError("Primary best move/PV does not match rank 1")


def validate_annotation_corpus(annotation_dir: Path) -> dict[str, Any]:
    require_python_chess()
    manifest_path = annotation_dir / "manifest.json"
    if not manifest_path.is_file():
        raise AnnotationError(f"Missing annotation manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise AnnotationError("Annotation manifest schema mismatch")
    for name in CANONICAL_ARTIFACTS:
        path = annotation_dir / name
        if not path.is_file():
            raise AnnotationError(f"Missing annotation artifact: {name}")
        data = path.read_bytes()
        if not data.endswith(b"\n"):
            raise AnnotationError(f"Artifact is not newline terminated: {name}")
        expected = manifest.get("artifacts", {}).get(name, {}).get("sha256")
        if expected != sha256_bytes(data):
            raise AnnotationError(f"Artifact checksum mismatch: {name}")

    source = manifest["sourceDataset"]
    source_dir = Path(source["datasetDirectory"])
    if source_dir.is_dir():
        if sha256_file(source_dir / "manifest.json") != source["manifestSha256"]:
            raise AnnotationError("Source dataset manifest checksum mismatch")
        if sha256_file(source_dir / "positions.jsonl") != source["positionsSha256"]:
            raise AnnotationError("Source dataset positions checksum mismatch")
        pgn_dataset.validate_dataset(source_dir)
        source_positions = pgn_dataset.read_jsonl(source_dir / "positions.jsonl")
        source_order = {record["positionId"]: index for index, record in enumerate(source_positions)}
        source_by_id = {record["positionId"]: record for record in source_positions}
    else:
        raise AnnotationError(f"Source dataset directory is unavailable for joined validation: {source_dir}")

    annotations = read_jsonl(annotation_dir / "annotations.jsonl")
    failures = read_jsonl(annotation_dir / "failures.jsonl")
    position_ids: set[str] = set()
    previous_order = -1
    for record in annotations:
        validate_annotation_record(record, manifest)
        position_id = record["positionId"]
        if position_id in position_ids:
            raise AnnotationError(f"Duplicate annotation positionId: {position_id}")
        if position_id not in source_by_id:
            raise AnnotationError(f"Annotation position absent from source dataset: {position_id}")
        source_record = source_by_id[position_id]
        for key in ("fen", "gameId", "split", "sideToMove", "result", "resultFromSideToMove", "gamePhase"):
            if record[key] != source_record[key]:
                raise AnnotationError(f"Annotation/source mismatch for {position_id}: {key}")
        current_order = source_order[position_id]
        if current_order <= previous_order:
            raise AnnotationError("Annotations are not in canonical source-dataset order")
        previous_order = current_order
        position_ids.add(position_id)
    for failure in failures:
        if failure["positionId"] in position_ids:
            raise AnnotationError("Position appears in both annotations and failures")
        position_ids.add(failure["positionId"])

    for split in SPLITS:
        records = read_jsonl(annotation_dir / f"{split}.jsonl")
        if records != [record for record in annotations if record["split"] == split]:
            raise AnnotationError(f"{split}.jsonl does not match annotation split fields")
    if manifest["counts"]["annotations"] != len(annotations) or manifest["counts"]["failures"] != len(failures):
        raise AnnotationError("Manifest annotation/failure counts mismatch")
    if manifest["counts"]["selectedPositions"] != len(annotations) + len(failures):
        raise AnnotationError("Selected coverage does not match annotations plus failures")
    for name in JSONL_ARTIFACTS:
        if manifest["artifacts"][name]["records"] != len(read_jsonl(annotation_dir / name)):
            raise AnnotationError(f"Manifest record count mismatch: {name}")
    return {"manifest": manifest, "annotations": annotations, "failures": failures}


def annotate_dataset(
    config: AnnotationConfig,
    adapter_factory: Callable[[AnnotationConfig, dict[str, Any]], EngineAdapter] = engine_factory_default,
    identity_override: dict[str, Any] | None = None,
) -> dict[str, Any]:
    validate_config(config)
    _, positions, source = load_source_dataset(config.dataset_dir)
    selected = select_positions(positions, config.split, config.max_positions, config.start_after_position_id)
    identity = identity_override or verify_engine(config.engine_path)
    supported_for_values: dict[str, Any]
    if identity_override is None:
        # Reopen adapter options are validated on startup; identity stores serialized options.
        class Bounds:
            def __init__(self, item: dict[str, Any]) -> None:
                self.min = item.get("minimum")
                self.max = item.get("maximum")
        supported_for_values = {name: Bounds(item) for name, item in identity["supportedOptions"].items()}
    else:
        supported_for_values = identity.get("optionObjects", {})
    options = deterministic_engine_options(config.threads, config.hash_mb, config.multipv, supported_for_values)
    validate_engine_option_values(options, supported_for_values)
    compatibility = compatibility_payload(config, source, identity, options, selected)
    paths = prepare_work_state(config, compatibility)
    runtime = run_annotation_work(config, identity, compatibility, selected, paths, adapter_factory)
    temp_dir = Path(tempfile.mkdtemp(prefix=f".{config.output_dir.name}.final-", dir=config.output_dir.parent))
    try:
        write_final_artifacts(
            temp_dir, config, source, identity, options, compatibility, selected,
            runtime["annotations"], runtime["failures"], runtime["engineRestarts"],
        )
        validation = validate_annotation_corpus(temp_dir)
        publish_final(temp_dir, config.output_dir)
        shutil.rmtree(paths["dir"])
    except Exception:
        if temp_dir.exists():
            shutil.rmtree(temp_dir)
        raise
    summary = json.loads((config.output_dir / "summary.json").read_text(encoding="utf-8"))
    summary.update({
        "analysisAttemptsThisRun": runtime["analysisAttempts"],
        "averageElapsedSecondsThisRun": (
            runtime["analysisSeconds"] / runtime["analysisAttempts"] if runtime["analysisAttempts"] else 0.0
        ),
        "elapsedSecondsThisRun": runtime["elapsedSeconds"],
    })
    return {"summary": summary, "validation": validation}


def print_engine(identity: dict[str, Any]) -> None:
    print(f"Engine name: {identity['engineName']}")
    print(f"Engine author: {identity['engineAuthor']}")
    print(f"Binary path: {identity['binaryPath']}")
    print(f"Binary size: {identity['fileSize']}")
    print(f"Binary SHA-256: {identity['engineBinarySha256']}")
    print(f"Supported UCI options: {', '.join(identity['supportedOptions'])}")
    print("UCI handshake: ok")
    print("isready: ok")


def print_annotation_summary(output_dir: Path, summary: dict[str, Any]) -> None:
    counts = summary["scoreCounts"]
    print(f"Source positions available: {summary['sourcePositionsAvailable']}")
    print(f"Positions selected: {summary['positionsSelected']}")
    print(f"Positions annotated: {summary['positionsAnnotated']}")
    print(f"CP annotations: {counts['cp']}")
    print(f"Mate annotations: {counts['mate']}")
    print(f"Terminal annotations: {counts['terminal']}")
    print(f"Failures: {summary['failures']}")
    print(f"Engine restarts: {summary['engineRestarts']}")
    print(f"Average depth: {summary['averageDepth']}")
    print(f"Average nodes: {summary['averageNodes']}")
    if "averageElapsedSecondsThisRun" in summary:
        print(f"Average elapsed seconds (non-canonical): {summary['averageElapsedSecondsThisRun']:.6f}")
    print(f"Split counts: {canonical_json(summary['splitCounts'])}")
    print(f"Coverage: {summary['coveragePercent']:.6f}%")
    for warning in summary["warnings"]:
        print(f"Warning: {warning}")
    print(f"Output path: {output_dir}")


def resolve_limit(args: argparse.Namespace) -> AnalysisLimit:
    if args.nodes is not None and args.depth is not None:
        raise AnnotationError("Specify exactly one of --nodes and --depth")
    if args.depth is not None:
        return AnalysisLimit("depth", args.depth)
    return AnalysisLimit("nodes", args.nodes if args.nodes is not None else 100_000)


def add_selection_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--dataset-dir", type=Path, default=DEFAULT_DATASET)
    parser.add_argument("--split", choices=(*SPLITS, "all"), default="all")
    parser.add_argument("--max-positions", type=int)
    parser.add_argument("--start-after-position-id")
    limits = parser.add_mutually_exclusive_group()
    limits.add_argument("--nodes", type=int)
    limits.add_argument("--depth", type=int)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    verify = commands.add_parser("verify-engine", help="verify Stockfish UCI identity and readiness")
    verify.add_argument("--engine", type=Path, default=DEFAULT_ENGINE)
    verify.add_argument("--startup-timeout-seconds", type=float, default=15.0)

    annotate = commands.add_parser("annotate", help="annotate a deterministic Phase 12 selection")
    add_selection_options(annotate)
    annotate.add_argument("--engine", type=Path, default=DEFAULT_ENGINE)
    annotate.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    annotate.add_argument("--threads", type=int, default=1)
    annotate.add_argument("--hash-mb", type=int, default=256)
    annotate.add_argument("--multipv", type=int, default=1)
    annotate.add_argument("--position-timeout-seconds", type=float, default=120.0)
    annotate.add_argument("--checkpoint-every", type=int, default=10)
    annotate.add_argument("--max-engine-restarts", type=int, default=3)
    annotate.add_argument("--strict", action="store_true")
    annotate.add_argument("--resume", action="store_true")
    annotate.add_argument("--force", action="store_true")

    inspect = commands.add_parser("inspect", help="validate an existing annotation corpus without Stockfish")
    inspect.add_argument("--annotation-dir", type=Path, default=DEFAULT_OUTPUT)

    estimate = commands.add_parser("estimate", help="estimate deterministic selection cost without launching Stockfish")
    add_selection_options(estimate)
    estimate.add_argument("--assumed-positions-per-second", type=float, default=1.0)
    estimate.add_argument("--estimated-bytes-per-record", type=int, default=1500)
    return parser


def config_from_args(args: argparse.Namespace) -> AnnotationConfig:
    return AnnotationConfig(
        dataset_dir=args.dataset_dir,
        engine_path=args.engine,
        output_dir=args.output_dir,
        limit=resolve_limit(args),
        threads=args.threads,
        hash_mb=args.hash_mb,
        multipv=args.multipv,
        split=args.split,
        max_positions=args.max_positions,
        start_after_position_id=args.start_after_position_id,
        position_timeout_seconds=args.position_timeout_seconds,
        checkpoint_every=args.checkpoint_every,
        max_engine_restarts=args.max_engine_restarts,
        strict=args.strict,
        resume=args.resume,
        force=args.force,
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "verify-engine":
            print_engine(verify_engine(args.engine, args.startup_timeout_seconds))
        elif args.command == "annotate":
            config = config_from_args(args)
            result = annotate_dataset(config)
            print_annotation_summary(config.output_dir, result["summary"])
        elif args.command == "inspect":
            result = validate_annotation_corpus(args.annotation_dir)
            summary = json.loads((args.annotation_dir / "summary.json").read_text(encoding="utf-8"))
            print_annotation_summary(args.annotation_dir, summary)
            print("Annotation validation: ok")
        else:
            limit = resolve_limit(args)
            _, positions, source = load_source_dataset(args.dataset_dir)
            selected = select_positions(positions, args.split, args.max_positions, args.start_after_position_id)
            if args.assumed_positions_per_second <= 0 or args.estimated_bytes_per_record < 1:
                raise AnnotationError("Estimate rates and record size must be positive")
            seconds = len(selected) / args.assumed_positions_per_second
            print(f"Source positions available: {source['totalPositions']}")
            print(f"Positions selected: {len(selected)}")
            print(f"Analysis limit: {limit.type}={limit.value}")
            print(f"Total requested nodes: {len(selected) * limit.value if limit.type == 'nodes' else 'not applicable for depth limit'}")
            print(f"Assumed positions per second (no engine launched): {args.assumed_positions_per_second}")
            print(f"Estimated runtime seconds: {seconds:.3f}")
            print(f"Estimated output bytes: {len(selected) * args.estimated_bytes_per_record}")
        return 0
    except (AnnotationError, pgn_dataset.DatasetError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
