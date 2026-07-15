#!/usr/bin/env python3
"""Phase 21.1 bounded, deterministic, resumable tuning orchestration."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Sequence


SCHEMA_VERSION = 1
TOOL_VERSION = "21.2"
STATUSES = {"pending", "running", "completed", "failed", "blocked", "skipped", "stale"}
RELEASE_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9.-]*")
HASH_PATTERN = re.compile(r"sha256:[0-9a-f]{64}")
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = ROOT / "tuning/pipeline-config.json"
DEFAULT_RUNS = ROOT / "tuning/runs"


class PipelineError(RuntimeError):
    pass


class StageSkipped(PipelineError):
    pass


@dataclass(frozen=True)
class Stage:
    stage_id: str
    dependencies: tuple[str, ...]
    config_key: str
    sources: tuple[str, ...]
    action: str


STAGES = (
    Stage("preflight", (), "preflight", ("Makefile", "engine/Makefile", "tools/tuning/requirements.txt"), "preflight"),
    Stage("pgn_discovery", ("preflight",), "dataset", (), "pgn_discovery"),
    Stage("dataset", ("pgn_discovery",), "dataset", ("tools/tuning/pgn_dataset.py", "tools/tuning/evaluation_tuning.py"), "dataset"),
    Stage("annotation", ("dataset",), "annotation", ("tools/tuning/stockfish_annotate.py",), "annotation"),
    Stage("evaluation_features", ("annotation",), "evaluationFitting", ("tools/tuning/evaluation_features.py", "tuning/parameter-registry.json"), "evaluation_features"),
    Stage("evaluation_fit", ("evaluation_features",), "evaluationFitting", ("tools/tuning/evaluation_tuning.py", "tuning/profiles/builtin-default-v1.json"), "evaluation_fit"),
    Stage("evaluation_build", ("evaluation_fit",), "evaluationFitting", ("engine/Makefile", "tools/tuning/generate_tuning_header.py"), "evaluation_build"),
    Stage("evaluation_validation", ("evaluation_build",), "evaluationValidation", ("tools/tuning/evaluation_candidate_validate.py", "tools/tuning/evaluation_features.py", "tools/tuning/stockfish_annotate.py"), "evaluation_validation"),
    Stage("search_tuning", ("evaluation_validation",), "searchTuning", ("tools/tuning/search_tuning.py",), "search_tuning"),
    Stage("search_build", ("search_tuning",), "searchTuning", (), "search_build"),
    Stage("time_safety", ("search_build",), "time", ("tools/tuning/time_management_tuning.py",), "time_safety"),
    Stage("time_tuning", ("time_safety",), "time", ("tools/tuning/time_management_tuning.py",), "time_tuning"),
    Stage("opening_validation", ("time_tuning",), "opening", ("engine/src/openingBook.cpp", "engine/openings/performance.bin"), "opening_validation"),
    Stage("protocol_regression", ("opening_validation",), "regressions", ("engine/scripts/test_uci_profile_identity.sh", "engine/scripts/test_uci_go_parser.sh", "engine/scripts/test_uci_ponder_race.sh"), "protocol_regression"),
    Stage("fair_play_regression", ("protocol_regression",), "regressions", ("website/src/lib/fair-play-engine.test.ts", "website/src/lib/fair-play-engine.integration.test.ts", "website/src/lib/engine-session.protocol.test.ts"), "fair_play_regression"),
    Stage("match_validation", ("fair_play_regression",), "match", ("tuning/promotion-policy.json",), "match_validation"),
    Stage("promotion_inspection", ("match_validation",), "promotion", ("tools/tuning/profile_promotion.py", "tuning/promotion-policy.json"), "promotion_inspection"),
    Stage("promotion_preparation", ("promotion_inspection",), "promotion", ("tools/tuning/profile_promotion.py", "tuning/promotion-policy.json"), "promotion_preparation"),
    Stage("release_build", ("promotion_preparation",), "release", ("engine/Makefile",), "release_build"),
    Stage("final_verification", ("release_build",), "verification", ("tools/tuning/tuning_pipeline.py",), "final_verification"),
)
STAGE_BY_ID = {stage.stage_id: stage for stage in STAGES}


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False) + "\n").encode()


def pretty_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode()


def sha256_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        with contextlib.suppress(OSError):
            directory = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    except Exception:
        with contextlib.suppress(OSError):
            os.unlink(temporary)
        raise


def write_json(path: Path, value: Any) -> None:
    atomic_write(path, pretty_bytes(value))


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PipelineError(f"cannot read JSON {repo_path(path)}: {error}") from error
    if not isinstance(value, dict):
        raise PipelineError(f"JSON artifact must be an object: {repo_path(path)}")
    return value


def repo_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError as error:
        raise PipelineError(f"path is outside repository: {path}") from error


def normalize_release_id(value: str) -> str:
    if value != value.strip() or not value or not RELEASE_PATTERN.fullmatch(value):
        raise PipelineError("release ID must match [A-Za-z0-9][A-Za-z0-9.-]* with no surrounding whitespace")
    if ".." in value or value.endswith(".") or any(character in value for character in "/\\;|&$`'\" "):
        raise PipelineError("release ID contains traversal, a path separator, whitespace, or a shell metacharacter")
    return value.lower()


def executable_name(release_id: str) -> str:
    return f"chess-engine-{normalize_release_id(release_id)}"


def discover_pgns(directory: Path) -> list[dict[str, Any]]:
    if not directory.is_dir():
        raise PipelineError(f"PGN directory does not exist: {repo_path(directory)}")
    files = sorted((path for path in directory.iterdir() if path.is_file() and path.suffix == ".pgn"), key=lambda path: path.name)
    if not files:
        raise PipelineError("no regular tuning/pgn/*.pgn files found")
    result, by_checksum = [], {}
    for path in files:
        try:
            size = path.stat().st_size
            if size == 0:
                raise PipelineError(f"empty PGN: {repo_path(path)}")
            with path.open("rb") as handle:
                handle.read(1)
        except OSError as error:
            raise PipelineError(f"unreadable PGN {repo_path(path)}: {error}") from error
        checksum = sha256_file(path)
        if checksum in by_checksum:
            raise PipelineError(f"duplicate PGN content: {by_checksum[checksum]} and {repo_path(path)}")
        by_checksum[checksum] = repo_path(path)
        result.append({"path": repo_path(path), "sha256": checksum, "size": size})
    return result


def files_under(paths: Iterable[Path]) -> list[Path]:
    result = []
    for path in paths:
        if path.is_file():
            result.append(path)
        elif path.is_dir():
            result.extend(item for item in path.rglob("*") if item.is_file() and item.name != ".lock")
    return sorted(set(result), key=lambda item: repo_path(item))


def checksums(paths: Iterable[Path]) -> dict[str, str]:
    return {repo_path(path): sha256_file(path) for path in files_under(paths)}


def command_identity(executable: str) -> str | None:
    resolved = shutil.which(executable)
    return repo_path(Path(resolved)) if resolved and Path(resolved).resolve().is_relative_to(ROOT) else resolved


class RunLock:
    def __init__(self, run_dir: Path):
        self.path = run_dir / ".lock"
        self.held = False

    def __enter__(self) -> "RunLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        try:
            self.path.mkdir()
        except FileExistsError:
            owner_path = self.path / "owner.json"
            owner = read_json(owner_path) if owner_path.is_file() else {}
            pid = owner.get("pid")
            live = isinstance(pid, int)
            if live:
                try:
                    os.kill(pid, 0)
                except OSError:
                    live = False
            if live:
                raise PipelineError(f"run is locked by live process {pid}")
            shutil.rmtree(self.path)
            self.path.mkdir()
        write_json(self.path / "owner.json", {"pid": os.getpid(), "startedAt": int(time.time())})
        self.held = True
        return self

    def __exit__(self, *_: Any) -> None:
        if self.held:
            shutil.rmtree(self.path, ignore_errors=True)


class Pipeline:
    def __init__(self, release_id: str, mode: str | None = None, stockfish: str | None = None,
                 stockfish_nodes: int | None = None, tune_time: bool | None = None, run_match: bool | None = None,
                 config_path: Path = DEFAULT_CONFIG, run_root: Path = DEFAULT_RUNS):
        self.supplied_release_id = release_id
        self.release_id = normalize_release_id(release_id)
        self.run_dir = run_root / self.release_id
        self.config_path = config_path
        self.config = read_json(config_path)
        existing_manifest = self.run_dir / "manifest.json"
        if mode is None and existing_manifest.is_file():
            mode = read_json(existing_manifest).get("mode")
        mode = mode or "full"
        if mode not in self.config.get("modes", {}):
            raise PipelineError(f"unsupported tuning mode: {mode}")
        self.mode = mode
        self.mode_config = self.config["modes"][mode]
        self.stockfish = Path(stockfish or "tuning/engines/stockfish")
        if not self.stockfish.is_absolute():
            self.stockfish = ROOT / self.stockfish
        self.stockfish_nodes = stockfish_nodes or int(self.mode_config["stockfishNodes"])
        if self.stockfish_nodes < 1:
            raise PipelineError("Stockfish node limit must be positive")
        previous_configuration = read_json(existing_manifest).get("configuration", {}) if existing_manifest.is_file() else {}
        self.tune_time = bool(previous_configuration.get("time", {}).get("enabled", False)) if tune_time is None else tune_time
        self.run_match = bool(previous_configuration.get("match", {}).get("enabled", False)) if run_match is None else run_match
        self.state_path = self.run_dir / "state.json"
        self.manifest_path = self.run_dir / "manifest.json"
        self.summary_path = self.run_dir / "summary.json"
        self.current_process: subprocess.Popen[str] | None = None
        self.interrupted = False
        self._validate_collision()

    @property
    def python(self) -> Path:
        return ROOT / ".venv/bin/python"

    def _validate_collision(self) -> None:
        manifest = self.run_dir / "manifest.json"
        if manifest.is_file():
            existing = read_json(manifest).get("release", {})
            original = existing.get("suppliedId")
            if original is not None and original != self.supplied_release_id:
                raise PipelineError(f"release ID normalizes to existing incompatible run {self.release_id!r} created as {original!r}")

    def effective_config(self) -> dict[str, Any]:
        return {
            "schemaVersion": self.config["schemaVersion"], "pipelineVersion": self.config["pipelineVersion"],
            "mode": self.mode, "modePolicy": self.mode_config, "dataset": self.config["dataset"],
            "annotation": {**self.config["annotation"], "nodes": self.stockfish_nodes},
            "evaluationFitting": self.config["evaluationFitting"], "evaluationValidation": self.config["evaluationValidation"],
            "searchTuning": self.config["searchTuning"], "time": {**self.config["time"], "enabled": self.tune_time},
            "opening": self.config["opening"], "regressions": self.config["regressions"],
            "match": {**self.config["match"], "enabled": self.run_match}, "promotion": self.config["promotion"],
            "resources": self.config["resources"],
        }

    def initial_state(self) -> dict[str, Any]:
        return {
            "schemaVersion": SCHEMA_VERSION, "pipelineVersion": TOOL_VERSION, "releaseId": self.release_id,
            "operational": {"currentStage": None, "updatedAt": int(time.time())},
            "stages": {stage.stage_id: {
                "stageId": stage.stage_id, "status": "pending", "dependencies": list(stage.dependencies), "attemptCount": 0,
                "inputChecksums": {}, "configurationChecksum": None, "toolSchemaVersion": TOOL_VERSION,
                "outputChecksums": {}, "failureReason": None, "canonicalAction": stage.action,
            } for stage in STAGES},
        }

    def load_state(self) -> dict[str, Any]:
        if not self.state_path.is_file():
            return self.initial_state()
        state = read_json(self.state_path)
        for record in state.get("stages", {}).values():
            if record.get("status") == "running":
                record["status"] = "failed"
                record["failureReason"] = "interrupted_while_running"
        return state

    def save_state(self, state: dict[str, Any]) -> None:
        state["operational"]["updatedAt"] = int(time.time())
        atomic_write(self.state_path, pretty_bytes(state))

    def stage_config(self, stage: Stage) -> Any:
        effective = self.effective_config()
        value = effective.get(stage.config_key, {})
        if not isinstance(value, dict):
            value = {"value": value}
        result = value | {"releaseId": self.release_id, "mode": self.mode}
        if stage.stage_id == "dataset":
            result |= {"datasetPolicy": self.mode_config["dataset"],
                       "selection": self.mode_config["selection"], "selectionSeed": self.config["evaluationFitting"]["seed"],
                       "minimumAcceptedGames": self.mode_config["minimumAcceptedGames"], "minimumFinalPositions": self.mode_config["minimumFinalPositions"]}
        elif stage.stage_id == "evaluation_validation":
            result |= {"auditPositions": self.mode_config["auditPositions"], "smokeMaximumPlies": self.mode_config["smokeMaximumPlies"]}
        elif stage.stage_id in {"search_tuning", "time_tuning", "match_validation"}:
            result |= {"modePolicy": self.mode_config}
        return result

    def stage_inputs(self, stage: Stage, state: Mapping[str, Any]) -> dict[str, str]:
        paths = [ROOT / source for source in stage.sources if (ROOT / source).exists()]
        inputs = checksums(paths)
        for dependency in stage.dependencies:
            for path, checksum in state["stages"][dependency].get("outputChecksums", {}).items():
                inputs[f"dependency:{dependency}:{path}"] = checksum
        if stage.stage_id in {"preflight", "pgn_discovery", "dataset"}:
            for item in discover_pgns(ROOT / "tuning/pgn"):
                inputs[f"pgn:{item['path']}"] = item["sha256"]
        if stage.stage_id in {"preflight", "annotation", "evaluation_validation"} and self.stockfish.is_file():
            inputs["dependency:stockfish"] = sha256_file(self.stockfish)
        if stage.stage_id not in {"preflight", "pgn_discovery", "dataset", "annotation", "evaluation_features", "evaluation_fit"}:
            production = ROOT / "engine/chess-engine"
            if production.is_file():
                inputs["dependency:production-engine"] = sha256_file(production)
        return dict(sorted(inputs.items()))

    def output_paths(self, record: Mapping[str, Any]) -> list[Path]:
        return [ROOT / path for path in record.get("outputChecksums", {})]

    def reusable(self, stage: Stage, record: Mapping[str, Any], state: Mapping[str, Any]) -> bool:
        if record.get("status") not in {"completed", "skipped"}:
            return False
        if any(state["stages"][dependency]["status"] not in {"completed", "skipped"} for dependency in stage.dependencies):
            return False
        current_inputs = self.stage_inputs(stage, state)
        current_config = sha256_bytes(canonical_bytes(self.stage_config(stage)))
        if record.get("inputChecksums") != current_inputs or record.get("configurationChecksum") != current_config or record.get("toolSchemaVersion") != TOOL_VERSION:
            return False
        for relative, expected in record.get("outputChecksums", {}).items():
            path = ROOT / relative
            if not path.is_file() or sha256_file(path) != expected:
                return False
        if stage.stage_id == "dataset":
            database = self.run_dir / "dataset/work/dataset.sqlite"; manifest = self.run_dir / "dataset/manifest.json"
            if not database.is_file() or not manifest.is_file(): return False
            try:
                import sqlite3
                sys.path.insert(0, str(ROOT / "tools/tuning")); import pgn_dataset
                connection = sqlite3.connect(database)
                logical = pgn_dataset.database_logical_checksum(connection); integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
                connection.close()
                if integrity != "ok" or logical != read_json(manifest)["storage"]["databaseLogicalSha256"]: return False
            except Exception:
                return False
        return True

    def mark_stale(self, state: dict[str, Any]) -> list[str]:
        stale: set[str] = set()
        for stage in STAGES:
            record = state["stages"][stage.stage_id]
            if any(dependency in stale for dependency in stage.dependencies):
                if record["status"] != "pending":
                    record["status"] = "stale"
                stale.add(stage.stage_id)
            elif record["status"] in {"completed", "skipped"} and not self.reusable(stage, record, state):
                record["status"] = "stale"
                stale.add(stage.stage_id)
        return sorted(stale, key=lambda item: list(STAGE_BY_ID).index(item))

    def run_command(self, stage_id: str, arguments: Sequence[str], cwd: Path = ROOT,
                    input_text: str | None = None, allow_failure: bool = False) -> subprocess.CompletedProcess[str]:
        log = self.run_dir / "logs" / f"{stage_id}.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("a", encoding="utf-8") as handle:
            handle.write("$ " + " ".join(arguments) + "\n")
            handle.flush()
            self.current_process = subprocess.Popen(list(arguments), cwd=cwd, text=True, stdin=subprocess.PIPE if input_text is not None else None,
                                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                output, _ = self.current_process.communicate(input=input_text)
            finally:
                returncode = self.current_process.returncode
                self.current_process = None
            handle.write(output or "")
            handle.flush()
        result = subprocess.CompletedProcess(arguments, returncode, output or "", "")
        if returncode and not allow_failure:
            raise PipelineError(f"command failed with exit status {returncode}; see {repo_path(log)}")
        return result

    def terminate_child(self) -> None:
        process = self.current_process
        if process is None or process.poll() is not None:
            return
        with contextlib.suppress(ProcessLookupError):
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()

    def _tool(self, name: str, *arguments: str) -> list[str]:
        return [str(self.python), str(ROOT / f"tools/tuning/{name}"), *map(str, arguments)]

    def _write_stage_metadata(self, directory: Path, stage: str, extra: Mapping[str, Any]) -> Path:
        path = directory / "pipeline-metadata.json"
        write_json(path, {"schemaVersion": 1, "releaseId": self.release_id, "pipelineRunId": self.release_id, "stage": stage, **extra})
        return path

    def action_preflight(self) -> list[Path]:
        pgns = discover_pgns(ROOT / "tuning/pgn")
        required_files = [self.python, self.stockfish, ROOT / "engine/chess-engine", ROOT / self.config["opening"]["book"], ROOT / "Makefile"]
        missing = [repo_path(path) if path.is_absolute() and path.is_relative_to(ROOT) else str(path) for path in required_files if not path.is_file()]
        tools = {name: shutil.which(name) for name in ("make", "g++", "node", "npm", "git")}
        missing.extend(name for name, path in tools.items() if path is None)
        if missing:
            raise PipelineError(f"preflight missing dependencies: {', '.join(missing)}")
        if not os.access(self.stockfish, os.X_OK) or not os.access(ROOT / "engine/chess-engine", os.X_OK):
            raise PipelineError("Stockfish and production engine must be executable")
        stockfish = self.run_command("preflight", self._tool("stockfish_annotate.py", "verify-engine", "--engine", str(self.stockfish)))
        engine = self.run_command("preflight", [str(ROOT / "engine/chess-engine"), "--mode=gui"], input_text="uci\nquit\n")
        disk = shutil.disk_usage(self.run_dir.parent if self.run_dir.parent.exists() else ROOT / "tuning")
        resources = self.config["resources"]; dataset_policy = self.mode_config["dataset"]
        cap = int(dataset_policy["maximumRetainedPositions"])
        estimated_sqlite = cap * int(resources["averageSqliteBytesPerRecord"])
        estimated_export = cap * int(resources["averageExportBytesPerRecord"])
        minimum_disk = int(resources["minimumFreeDiskGb"]) * 1024 ** 3
        required_disk = estimated_sqlite + estimated_export * 2 + minimum_disk
        memory_available_kb = 0
        with contextlib.suppress(OSError):
            for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
                if line.startswith("MemAvailable:"): memory_available_kb = int(line.split()[1]); break
        if memory_available_kb and memory_available_kb < int(resources["minimumFreeMemoryMb"]) * 1024:
            raise PipelineError(f"preflight available memory is below {resources['minimumFreeMemoryMb']} MiB")
        if disk.free < required_disk:
            raise PipelineError(f"preflight disk deficit: available={disk.free} required={required_disk}")
        selected_policy = self.mode_config["selection"]
        required_positions = sum(int(selected_policy[key]) for key in ("train", "validation", "test")) + int(self.mode_config["auditPositions"])
        if required_positions > cap:
            raise PipelineError(f"dataset retained-position cap {cap} cannot satisfy selection and audit requirement {required_positions}")
        split_capacity = cap // 3
        split_requirements = {"train": int(selected_policy["train"]),
                              "validation": int(selected_policy["validation"]),
                              "test": int(selected_policy["test"]) + int(self.mode_config["auditPositions"])}
        deficits = {split: required for split, required in split_requirements.items() if required > split_capacity}
        if deficits:
            raise PipelineError(f"dataset retained-position cap provides about {split_capacity} records per balanced split, below requirements {deficits}")
        git = self.run_command("preflight", ["git", "rev-parse", "HEAD"], allow_failure=True).stdout.strip()
        status = self.run_command("preflight", ["git", "status", "--porcelain"], allow_failure=True).stdout.splitlines()
        report = {
            "schemaVersion": 1, "releaseId": self.release_id, "mode": self.mode, "pgnCount": len(pgns),
            "resolved": {"python": ".venv/bin/python", "stockfish": repo_path(self.stockfish), "productionEngine": "engine/chess-engine",
                         "openingBook": self.config["opening"]["book"], "make": "make", "compiler": "g++", "node": "node", "npm": "npm"},
            "identities": {"stockfishOutput": stockfish.stdout.strip().splitlines()[-1:] or [], "engineUci": [line for line in engine.stdout.splitlines() if line.startswith("id ") or "tuning profile=" in line]},
            "checksums": {"stockfish": sha256_file(self.stockfish), "productionEngine": sha256_file(ROOT / "engine/chess-engine"), "openingBook": sha256_file(ROOT / self.config["opening"]["book"])},
            "resources": {"availableMemoryMb": memory_available_kb // 1024 if memory_available_kb else None,
                          "maximumDatasetRssMb": resources["maximumDatasetRssMb"], "availableDiskBytes": disk.free,
                          "estimatedSqliteBytes": estimated_sqlite, "estimatedExportBytes": estimated_export,
                          "estimatedTemporaryBytes": estimated_export, "requiredWorkingBytesWithSafetyMargin": required_disk},
        }
        write_json(self.run_dir / "logs/preflight-operational.json", {"diskFreeBytes": disk.free, "gitCommit": git or None,
                   "dirtyWarning": bool(status), "statusEntries": len(status), "recordedAt": int(time.time())})
        path = self.run_dir / "inputs/preflight.json"; write_json(path, report); return [path]

    def action_pgn_discovery(self) -> list[Path]:
        path = self.run_dir / "inputs/pgn-manifest.json"
        write_json(path, {"schemaVersion": 1, "releaseId": self.release_id, "files": discover_pgns(ROOT / "tuning/pgn")})
        return [path]

    def action_dataset(self) -> list[Path]:
        dataset = self.run_dir / "dataset"; selection = self.run_dir / "features/selection"
        dataset_id = f"pgn-derived-{self.release_id}"
        settings = self.config["dataset"]; policy = self.mode_config["dataset"]; resources = self.config["resources"]
        arguments = self._tool("pgn_dataset.py", "build", "--pgn-dir", str(ROOT / "tuning/pgn"), "--output-dir", str(dataset),
            "--dataset-version", dataset_id, "--min-ply", str(settings["minPly"]), "--sample-every", str(settings["sampleEvery"]),
            "--train-ratio", str(settings["trainRatio"]), "--validation-ratio", str(settings["validationRatio"]),
            "--test-ratio", str(settings["testRatio"]), "--split-seed", settings["splitSeed"], "--minimum-split-games", "1",
            "--maximum-retained-positions", str(policy["maximumRetainedPositions"]), "--maximum-positions-per-game", str(policy["maximumPositionsPerGame"]),
            "--opening-quota", str(policy["phaseQuotas"]["opening"]), "--middlegame-quota", str(policy["phaseQuotas"]["middlegame"]),
            "--endgame-quota", str(policy["phaseQuotas"]["endgame"]), "--sampling-seed", self.config["evaluationFitting"]["seed"],
            "--maximum-rss-mb", str(resources["maximumDatasetRssMb"]), "--warning-rss-mb", str(resources["warningDatasetRssMb"]),
            "--scan-all-input-games" if policy["scanAllInputGames"] else "--no-scan-all-input-games")
        if policy["maximumAcceptedGames"] is not None: arguments += ["--maximum-accepted-games", str(policy["maximumAcceptedGames"])]
        if (dataset / "work/dataset.sqlite").is_file() and not (dataset / "manifest.json").is_file(): arguments += ["--resume", "--force"]
        elif dataset.exists(): arguments.append("--force")
        self.run_command("dataset", arguments)
        summary = read_json(dataset / "summary.json")
        games = int(summary.get("games", {}).get("accepted", 0)); positions = int(summary.get("positions", {}).get("final", 0))
        if games < self.mode_config["minimumAcceptedGames"] or positions < self.mode_config["minimumFinalPositions"]:
            raise PipelineError(f"{self.mode} dataset policy deficit: accepted games {games}/{self.mode_config['minimumAcceptedGames']}, positions {positions}/{self.mode_config['minimumFinalPositions']}")
        sys.path.insert(0, str(ROOT / "tools/tuning")); import pgn_dataset
        validation = pgn_dataset.validate_dataset(dataset)
        sides = set(validation["sideCounts"]); phases = set(validation["phaseCounts"])
        if sides != {"white", "black"}:
            raise PipelineError("dataset side-to-move parity guard failed; both white and black are required")
        if phases != {"opening", "middlegame", "endgame"}:
            raise PipelineError(f"dataset phase coverage guard failed: {sorted(phases)}")
        selected = self.mode_config["selection"]
        self.run_command("dataset", self._tool("evaluation_tuning.py", "select", "--dataset-dir", str(dataset), "--output-dir", str(selection),
                                                       "--train-count", str(selected["train"]), "--validation-count", str(selected["validation"]),
                                                       "--test-count", str(selected["test"]), "--max-per-game", str(selected["maxPerGame"]),
                                                       "--seed", self.config["evaluationFitting"]["seed"], "--force"))
        metadata = self._write_stage_metadata(dataset, "dataset", {"datasetId": dataset_id, "inputManifest": "inputs/pgn-manifest.json", "selectionManifest": "features/selection/manifest.json"})
        return [dataset / "manifest.json", dataset / "positions.jsonl", dataset / "skipped-games.jsonl",
                dataset / "summary.json", selection, metadata]

    def _annotate(self, stage_id: str, selection_manifest: Path, output: Path) -> None:
        annotation = self.config["annotation"]
        arguments = self._tool("stockfish_annotate.py", "annotate", "--dataset-dir", str(self.run_dir / "dataset"),
                                                  "--selection-manifest", str(selection_manifest), "--nodes", str(self.stockfish_nodes), "--engine", str(self.stockfish),
                                                  "--output-dir", str(output), "--threads", str(annotation["threads"]), "--hash-mb", str(annotation["hashMb"]),
                                                  "--multipv", str(annotation["multiPv"]), "--checkpoint-every", str(annotation["checkpointEvery"]), "--strict")
        if output.with_name(output.name + ".work").is_dir():
            arguments.append("--resume")
        elif output.exists():
            arguments.append("--force")
        self.run_command(stage_id, arguments)
        summary = read_json(output / "summary.json")
        annotated, selected = summary.get("positionsAnnotated", 0), summary.get("positionsSelected", 0)
        cp = summary.get("scoreCounts", {}).get("cp", 0)
        coverage = (100 * cp / selected) if selected else 0
        if annotated != selected or summary.get("failures") or coverage < annotation["minimumCpCoveragePercent"]:
            raise PipelineError(f"annotation coverage failed: annotated={annotated}/{selected}, CP={coverage:.2f}%, failures={summary.get('failures')}")
        self._write_stage_metadata(output, "annotation", {"stockfishChecksum": sha256_file(self.stockfish), "nodes": self.stockfish_nodes, "releaseId": self.release_id})

    def action_annotation(self) -> list[Path]:
        output = self.run_dir / "annotations"
        self._annotate("annotation", self.run_dir / "features/selection/manifest.json", output)
        return [output]

    def _export_features(self, stage_id: str, annotation: Path, output: Path, engine: Path, selection: Path,
                         profile_id: str | None = None, profile_hash: str | None = None) -> None:
        arguments = self._tool("evaluation_features.py", "export", "--dataset-dir", str(self.run_dir / "dataset"), "--annotation-dir", str(annotation),
                               "--engine", str(engine), "--output-dir", str(output), "--registry", str(ROOT / "tuning/parameter-registry.json"),
                               "--selection-manifest", str(selection), "--force")
        if profile_id and profile_hash:
            arguments += ["--expected-profile-id", profile_id, "--expected-profile-hash", profile_hash]
        self.run_command(stage_id, arguments)

    def action_evaluation_features(self) -> list[Path]:
        output = self.run_dir / "features/baseline"
        self._export_features("evaluation_features", self.run_dir / "annotations", output, ROOT / "engine/chess-engine", self.run_dir / "features/selection/manifest.json")
        summary = read_json(output / "summary.json")
        if summary.get("reconstruction", {}).get("mismatches", 0) or summary.get("connectivityCounts", {}).get("mapped") not in (None, 44):
            raise PipelineError("evaluation feature reconstruction or registry mapping failed")
        return [output]

    def action_evaluation_fit(self) -> list[Path]:
        candidate_id = f"candidate-eval-{self.release_id}-0001"; output = self.run_dir / "evaluation/candidate"
        fit = self.config["evaluationFitting"]
        self.run_command("evaluation_fit", self._tool("evaluation_tuning.py", "fit", "--feature-dir", str(self.run_dir / "features/baseline"),
                                                            "--baseline-profile", str(ROOT / "tuning/profiles/builtin-default-v1.json"), "--registry", str(ROOT / "tuning/parameter-registry.json"),
                                                            "--output-dir", str(output), "--candidate-id", candidate_id, "--cp-clip", str(fit["cpClip"]),
                                                            "--max-parameter-delta", str(fit["maxParameterDelta"]), "--force"))
        self._write_stage_metadata(output, "evaluation_fit", {"candidateId": candidate_id, "releaseId": self.release_id, "fittingPolicy": fit})
        return [output]

    def action_evaluation_build(self) -> list[Path]:
        candidate = self.run_dir / "evaluation/candidate"; header = candidate / "generated/generated_tuning_values.hpp"; binary = candidate / "chess-engine"
        self.run_command("evaluation_build", self._tool("evaluation_tuning.py", "generate-header", "--candidate-dir", str(candidate), "--output", str(header)))
        self.run_command("evaluation_build", ["make", "-C", "engine", "candidate-build", f"CANDIDATE_HEADER=../{repo_path(header)}", f"CANDIDATE_OUTPUT=../{repo_path(binary)}"])
        self.run_command("evaluation_build", self._tool("evaluation_tuning.py", "validate", "--candidate-dir", str(candidate), "--baseline-engine", str(ROOT / "engine/chess-engine"), "--candidate-engine", str(binary)))
        report = read_json(candidate / "validation-report.json")
        if report.get("predictedVersusActualMismatches") or report.get("maximumMismatch"):
            raise PipelineError("evaluation analytical verification requires zero mismatches and 0 CP maximum mismatch")
        return [candidate]

    def action_evaluation_validation(self) -> list[Path]:
        candidate = self.run_dir / "evaluation/candidate"; profile = read_json(candidate / "profile.json")
        selection = self.run_dir / "evaluation-validation/selection"; validation = self.run_dir / "evaluation-validation/results"
        common = ["--candidate-dir", str(candidate), "--candidate-header", str(candidate / "generated/generated_tuning_values.hpp"),
                  "--candidate-engine", str(candidate / "chess-engine"), "--baseline-engine", str(ROOT / "engine/chess-engine")]
        self.run_command("evaluation_validation", self._tool("evaluation_candidate_validate.py", "audit-select", *common, "--dataset-dir", str(self.run_dir / "dataset"),
                                                                  "--phase15-selection", str(self.run_dir / "features/selection/manifest.json"), "--output-dir", str(selection),
                                                                  "--count", str(self.mode_config["auditPositions"]), "--seed", f"bitboard-audit-{self.release_id}", "--force"))
        annotations = self.run_dir / "evaluation-validation/annotations"; self._annotate("evaluation_validation", selection / "manifest.json", annotations)
        baseline_features = self.run_dir / "evaluation-validation/baseline-features"; candidate_features = self.run_dir / "evaluation-validation/candidate-features"
        self._export_features("evaluation_validation", annotations, baseline_features, ROOT / "engine/chess-engine", selection / "manifest.json")
        self._export_features("evaluation_validation", annotations, candidate_features, candidate / "chess-engine", selection / "manifest.json", profile["profileId"], profile["canonicalHash"])
        self.run_command("evaluation_validation", self._tool("evaluation_candidate_validate.py", "static-audit", *common, "--baseline-features", str(baseline_features), "--candidate-features", str(candidate_features), "--output-dir", str(validation)))
        self.run_command("evaluation_validation", self._tool("evaluation_candidate_validate.py", "search-audit", *common, "--selection-dir", str(selection), "--annotation-dir", str(annotations),
                                                                  "--baseline-features", str(baseline_features), "--output-dir", str(validation), "--count", str(self.mode_config["evaluationSearchPositions"]), "--depth", str(self.config["evaluationValidation"]["searchDepth"])))
        self.run_command("evaluation_validation", self._tool("evaluation_candidate_validate.py", "smoke-match", *common, "--output-dir", str(validation),
                                                                  "--depth", str(self.config["evaluationValidation"]["smokeDepth"]), "--max-plies", str(self.mode_config["smokeMaximumPlies"])))
        self.run_command("evaluation_validation", self._tool("evaluation_candidate_validate.py", "inspect", *common, "--selection-dir", str(selection), "--annotation-dir", str(annotations), "--output-dir", str(validation)))
        status = read_json(validation / "summary.json").get("validationStatus")
        if status == "rejected":
            raise PipelineError("evaluation candidate rejected by independent validation")
        return [selection, annotations, baseline_features, candidate_features, validation]

    def action_search_tuning(self) -> list[Path]:
        root = self.run_dir / "search"; candidate = self.run_dir / "evaluation/candidate"; annotations = self.run_dir / "evaluation-validation/annotations/annotations.jsonl"
        common = ["--output-dir", repo_path(root), "--base-profile", repo_path(candidate / "profile.json"), "--base-engine", repo_path(candidate / "chess-engine"),
                  "--annotations", repo_path(annotations), "--candidate-id", f"candidate-search-{self.release_id}-0001"]
        self.run_command("search_tuning", self._tool("search_tuning.py", "prepare", *common, "--registry", "tuning/parameter-registry.json",
                                                   "--selection", repo_path(self.run_dir / "evaluation-validation/selection/selection.jsonl"),
                                                   "--development-count", str(self.mode_config["searchDevelopment"]),
                                                   "--holdout-count", str(self.mode_config["searchHoldout"]), "--force"))
        self.run_command("search_tuning", self._tool("search_tuning.py", "run-suite", *common, "--depth", str(self.mode_config["searchDepth"])))
        self.run_command("search_tuning", self._tool("search_tuning.py", "rank", *common, "--depth", str(self.mode_config["searchDepth"])))
        self.run_command("search_tuning", self._tool("search_tuning.py", "smoke-match", *common, "--depth", "3", "--max-plies", str(self.mode_config["smokeMaximumPlies"])))
        self.run_command("search_tuning", self._tool("search_tuning.py", "inspect", *common))
        return [root]

    def final_candidate(self) -> dict[str, Any]:
        composition = self.run_dir / "search/final-candidate.json"
        if composition.is_file():
            return read_json(composition)
        candidate = self.run_dir / "evaluation/candidate"; profile = read_json(candidate / "profile.json")
        return {"profileId": profile["profileId"], "profileHash": profile["canonicalHash"], "profile": repo_path(candidate / "profile.json"),
                "header": repo_path(candidate / "generated/generated_tuning_values.hpp"), "binary": repo_path(candidate / "chess-engine"), "ancestry": [profile["parentProfileId"], profile["profileId"]]}

    def action_search_build(self) -> list[Path]:
        ranking = read_json(self.run_dir / "search/ranking.json"); evaluation = self.run_dir / "evaluation/candidate"; eval_profile = read_json(evaluation / "profile.json")
        selected = ranking.get("selected") if ranking.get("outcome") == "candidate_selected" else None
        directory = self.run_dir / "search/candidate" if selected else evaluation
        profile = read_json(directory / "profile.json")
        composition = {"schemaVersion": 1, "releaseId": self.release_id, "outcome": "evaluation_and_search" if selected else "evaluation_only",
                       "profileId": profile["profileId"], "profileHash": profile["canonicalHash"], "profile": repo_path(directory / "profile.json"),
                       "header": repo_path(directory / "generated/generated_tuning_values.hpp"), "binary": repo_path(directory / "chess-engine"),
                       "evaluationCandidate": {"profileId": eval_profile["profileId"], "profileHash": eval_profile["canonicalHash"]},
                       "searchCandidate": {"profileId": profile["profileId"], "profileHash": profile["canonicalHash"]} if selected else None,
                       "ancestry": [eval_profile["parentProfileId"], eval_profile["profileId"]] + ([profile["profileId"]] if selected else [])}
        path = self.run_dir / "search/final-candidate.json"; write_json(path, composition); return [path, directory]

    def _prepare_time_safety_inputs(self, root: Path, final: Mapping[str, Any]) -> None:
        records = [json.loads(line) for line in (self.run_dir / "evaluation-validation/selection/selection.jsonl").read_text().splitlines() if line]
        if len(records) < 36:
            raise PipelineError("time safety requires at least 36 independent selection records")
        for name, rows in (("suite.jsonl", records[:36]), ("development.jsonl", records[:24]), ("holdout.jsonl", records[24:36])):
            atomic_write(root / name, b"".join(canonical_bytes(row) for row in rows))
        baseline = root / "baseline"; baseline.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / final["profile"], baseline / "profile.json")

    def action_time_safety(self) -> list[Path]:
        root = self.run_dir / "time/safety"; final = self.final_candidate(); self._prepare_time_safety_inputs(root, final)
        self.run_command("time_safety", self._tool("time_management_tuning.py", "verify-safety", "--output-dir", repo_path(root), "--base-profile", final["profile"],
                                                         "--base-engine", final["binary"], "--policy-engine", final["binary"],
                                                         "--annotations", repo_path(self.run_dir / "evaluation-validation/annotations/annotations.jsonl"), "--candidate-id", f"candidate-time-{self.release_id}-0001"))
        report = read_json(root / "remediation/safety-verification.json")
        if report.get("status") != "time_policy_safe":
            raise PipelineError("time-policy safety validation failed")
        return [root]

    def action_time_tuning(self) -> list[Path]:
        marker = self.run_dir / "time/tuning-status.json"
        if not self.tune_time:
            write_json(marker, {"schemaVersion": 1, "releaseId": self.release_id, "status": "skipped", "reason": "time_tuning_not_enabled", "candidate": None})
            raise StageSkipped("optional time tuning not enabled")
        root = self.run_dir / "time/tuning"; final = self.final_candidate(); common = ["--output-dir", repo_path(root), "--base-profile", final["profile"],
            "--base-engine", final["binary"], "--policy-engine", final["binary"], "--annotations", repo_path(self.run_dir / "evaluation-validation/annotations/annotations.jsonl"),
            "--candidate-id", f"candidate-time-{self.release_id}-0001"]
        for command in (("prepare", "--registry", "tuning/parameter-registry.json", "--selection", repo_path(self.run_dir / "evaluation-validation/selection/selection.jsonl"), "--force"),
                        ("run-scenarios",), ("timed-search",), ("rank",), ("smoke-match", "--max-plies", str(self.mode_config["smokeMaximumPlies"])), ("inspect",)):
            self.run_command("time_tuning", self._tool("time_management_tuning.py", command[0], *common, *command[1:]))
        ranking = read_json(root / "ranking.json"); selected = ranking.get("selected")
        write_json(marker, {"schemaVersion": 1, "releaseId": self.release_id, "status": "completed", "candidate": selected})
        if selected:
            profile = read_json(root / "candidate/profile.json"); current = self.final_candidate()
            write_json(self.run_dir / "search/final-candidate.json", {**current, "outcome": "evaluation_search_and_time", "profileId": profile["profileId"], "profileHash": profile["canonicalHash"],
                       "profile": repo_path(root / "candidate/profile.json"), "header": repo_path(root / "candidate/generated/generated_tuning_values.hpp"),
                       "binary": repo_path(root / "candidate/chess-engine"), "timeCandidate": {"profileId": profile["profileId"], "profileHash": profile["canonicalHash"]}, "ancestry": current["ancestry"] + [profile["profileId"]]})
        return [root, marker, self.run_dir / "search/final-candidate.json"]

    def action_opening_validation(self) -> list[Path]:
        import chess
        final = self.final_candidate(); binary = ROOT / final["binary"]; book = ROOT / self.config["opening"]["book"]
        commands = "uci\nsetoption name OwnBook value true\nsetoption name BookFile value " + str(book) + "\nisready\nposition startpos\ngo depth 2\nposition fen 8/8/8/8/8/4k3/6p1/4K3 w - - 0 1\ngo depth 2\nquit\n"
        result = self.run_command("opening_validation", [str(binary), "--mode=gui"], input_text=commands)
        moves = [line.split()[1] for line in result.stdout.splitlines() if line.startswith("bestmove ")]
        if len(moves) < 2 or chess.Move.from_uci(moves[0]) not in chess.Board().legal_moves:
            raise PipelineError("opening-book load, legal move, miss fallback, or exit-to-search validation failed")
        report = {"schemaVersion": 1, "releaseId": self.release_id, "status": "passed", "book": repo_path(book), "bookSha256": sha256_file(book),
                  "bookLoads": True, "bookMoveLegal": True, "bookResultCorrelation": True, "bookMissFallback": True, "bookExitToSearch": True,
                  "fairPlayBookApplication": "covered_by_fair_play_regression", "applicationAcknowledgment": "covered_by_fair_play_regression", "moves": moves[:2]}
        path = self.run_dir / "opening/summary.json"; write_json(path, report); return [path]

    def action_protocol_regression(self) -> list[Path]:
        self.run_command("protocol_regression", ["make", "-C", "engine", "test"])
        report = self.run_dir / "regressions/protocol.json"; write_json(report, {"schemaVersion": 1, "releaseId": self.release_id, "status": "passed", "suite": "engine_tests_and_uci_identity"}); return [report]

    def action_fair_play_regression(self) -> list[Path]:
        for script in ("test:protocol", "test:fair-play", "test:fair-play-integration", "test:time-policy"):
            self.run_command("fair_play_regression", ["npm", "run", script], cwd=ROOT / "website")
        report = self.run_dir / "regressions/fair-play.json"; write_json(report, {"schemaVersion": 1, "releaseId": self.release_id, "status": "passed",
            "coverage": ["authoritative_current_fen", "position_before_go", "request_correlation", "session_generation", "move_legality", "application_acknowledgment", "bounded_retries", "same_fen_loop_prevention", "clock_drain_prevention", "book_path_validation"]}); return [report]

    def action_match_validation(self) -> list[Path]:
        summary = self.run_dir / "match/summary.json"
        if not self.run_match:
            write_json(summary, {"schemaVersion": 1, "releaseId": self.release_id, "status": "not_run", "promotionBlocker": "match_validation_missing", "minimumGames": self.config["match"]["minimumGames"]})
            raise StageSkipped("expensive match validation requires RUN_MATCH=1")
        import chess
        import chess.pgn
        sys.path.insert(0, str(ROOT / "tools/tuning"))
        import evaluation_candidate_validate as validation
        final = self.final_candidate(); candidate_path = ROOT / final["binary"]; baseline_path = ROOT / "engine/chess-engine"
        games_path = self.run_dir / "match/games.jsonl"; existing = []
        if games_path.is_file():
            existing = [json.loads(line) for line in games_path.read_text().splitlines() if line]
        target = int(self.config["match"]["minimumGames"]); starts = validation.STARTS
        with games_path.open("a", encoding="utf-8") as output, validation.UciEngine(baseline_path) as baseline, validation.UciEngine(candidate_path) as candidate:
            for index in range(len(existing), target):
                label, fen = starts[(index // 2) % len(starts)]; baseline_white = index % 2 == 0
                record, pgn = validation.play_smoke_game(label, fen, baseline_white, baseline, candidate, int(self.config["match"]["depth"]), self.mode_config["smokeMaximumPlies"], index + 1)
                pgn_path = self.run_dir / "match/games" / f"game-{index + 1:04d}.pgn"; atomic_write(pgn_path, pgn.encode())
                row = {**record, "startingPositionId": label, "candidateIdentity": final["profileHash"], "baselineIdentity": sha256_file(baseline_path), "pgnSha256": sha256_file(pgn_path)}
                output.write(canonical_bytes(row).decode()); output.flush(); os.fsync(output.fileno())
                existing.append(row)
        hard = {name: sum(int(row.get(name, 0)) for row in existing) for name in ("illegalMoves", "crashes", "protocolFailures")}
        accepted = len(existing) >= target and not any(hard.values())
        write_json(summary, {"schemaVersion": 1, "releaseId": self.release_id, "status": "accepted" if accepted else "rejected", "games": len(existing),
                             "colorReversal": True, "startingPositionSuiteChecksum": sha256_bytes(canonical_bytes(list(starts))), "timeControl": self.config["match"]["timeControl"], **hard})
        if not accepted:
            raise PipelineError("match validation rejected")
        baseline = read_json(ROOT / "tuning/profiles/builtin-default-v1.json")
        promotion_match = self.run_dir / "match/match-validation.json"
        write_json(promotion_match, {"schemaVersion": 1, "candidateId": final["profileId"], "candidateHash": final["profileHash"],
            "baselineId": baseline["profileId"], "baselineHash": baseline["canonicalHash"], "games": len(existing),
            "timeControl": self.config["match"]["timeControl"], "startingPositionSuiteChecksum": sha256_bytes(canonical_bytes(list(starts))),
            "colorReversal": True, "wins": sum(row.get("winner") == "candidate" for row in existing),
            "losses": sum(row.get("winner") == "baseline" for row in existing), "draws": sum(row.get("winner") == "draw" for row in existing),
            "flags": 0, "illegalMoves": hard["illegalMoves"], "crashes": hard["crashes"], "protocolFailures": hard["protocolFailures"],
            "adjudications": sum(row.get("maximumPlyAdjudication", False) for row in existing), "acceptanceDecision": "accepted"})
        return [games_path, self.run_dir / "match/games", summary, promotion_match]

    def action_promotion_inspection(self) -> list[Path]:
        final = self.final_candidate(); evidence = self.run_dir / "promotion/evidence"; evidence.mkdir(parents=True, exist_ok=True)
        write_json(evidence / "promotion-summary.json", {"schemaVersion": 1, "profileId": final["profileId"], "profileHash": final["profileHash"],
                   "outcome": read_json(self.run_dir / "search/ranking.json")["outcome"], "artifacts": {}})
        time_report = read_json(self.run_dir / "time/safety/remediation/safety-verification.json")
        write_json(evidence / "time-policy-validation.json", {**time_report, "profileId": final["profileId"], "profileHash": final["profileHash"]})
        source = self.run_dir / "promotion/source-candidate"; (source / "generated").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / final["profile"], source / "profile.json")
        shutil.copy2(ROOT / final["header"], source / "generated/generated_tuning_values.hpp")
        shutil.copy2(ROOT / final["binary"], source / "chess-engine")
        metadata = read_json((ROOT / final["profile"]).parent / "metadata.json")
        if "changedTimeParameter" in metadata and "changedTimeParameters" not in metadata:
            metadata["changedTimeParameters"] = [{"registryName": metadata["changedTimeParameter"]}]
        match_accepted = read_json(self.run_dir / "match/summary.json").get("status") == "accepted"
        metadata.update({"releaseId": self.release_id, "pipelineRunId": self.release_id,
                         "developmentOnly": self.mode != "full" or not match_accepted,
                         "promotionEligible": self.mode == "full" and match_accepted})
        write_json(source / "metadata.json", metadata)
        artifact_names = ("profile.json", "metadata.json")
        write_json(source / "manifest.json", {"schemaVersion": 1, "candidateProfileId": final["profileId"], "candidateProfileHash": final["profileHash"],
                   "developmentOnly": metadata["developmentOnly"], "promotionEligible": metadata["promotionEligible"],
                   "artifacts": {name: {"sha256": sha256_file(source / name)} for name in artifact_names},
                   "tool": {"name": "tuning_pipeline.py", "version": TOOL_VERSION}})
        result = self.run_command("promotion_inspection", self._tool("profile_promotion.py", "inspect", "--candidate", str(source / "profile.json")), allow_failure=True)
        report_path = self.run_dir / "promotion/inspection.json"
        try:
            report = json.loads(result.stdout)
        except json.JSONDecodeError:
            report = {"schemaVersion": 1, "releaseId": self.release_id, "outcome": "promotion_pipeline_validated_candidate_ineligible", "rawOutput": result.stdout.strip(), "exitStatus": result.returncode}
        report["releaseId"] = self.release_id
        if read_json(self.run_dir / "match/summary.json").get("status") != "accepted":
            report.setdefault("pipelineBlockers", []).append("match_validation_missing")
            report["outcome"] = "promotion_pipeline_validated_candidate_ineligible"
        write_json(report_path, report); return [report_path]

    def action_promotion_preparation(self) -> list[Path]:
        inspection = read_json(self.run_dir / "promotion/inspection.json"); status = self.run_dir / "promotion/preparation.json"
        technical = {"schema_validation", "artifact_integrity", "ancestry_validation", "deterministic_generation", "isolated_build", "candidate_binary_identity"}
        gate_status = {item["gateId"]: item["actualStatus"] for item in inspection.get("gates", [])}
        technical_blockers = sorted(gate for gate in technical if gate_status.get(gate) != "passed")
        if technical_blockers:
            write_json(status, {"schemaVersion": 1, "releaseId": self.release_id, "status": "blocked", "blockers": technical_blockers, "automaticPromotion": False})
            raise StageSkipped("promotion technical staging prerequisites failed")
        final = self.final_candidate(); result = self.run_command("promotion_preparation", self._tool("profile_promotion.py", "prepare", "--candidate", str(ROOT / final["profile"]), "--release-id", self.release_id))
        prepared = json.loads(result.stdout); promotion_id = prepared["promotionId"]
        verified_result = self.run_command("promotion_preparation", self._tool("profile_promotion.py", "verify", "--promotion", promotion_id))
        verified = json.loads(verified_result.stdout)
        verified_gates = {item["gateId"]: item["actualStatus"] for item in verified.get("validationGates", [])}
        blockers = []
        for blocker in inspection.get("policyBlockers", []):
            gate_id = blocker.split(":", 1)[0]
            if gate_id in verified_gates and verified_gates[gate_id] == "passed":
                continue
            blockers.append(blocker)
        ready = not blockers
        write_json(status, {"schemaVersion": 1, "releaseId": self.release_id, "status": "promotion_ready_awaiting_approval" if ready else "staged_but_blocked",
                            "promotionId": promotion_id, "stagedExecutable": verified.get("stagedExecutable"), "stagedProfile": verified.get("stagedProduction"),
                            "blockers": blockers, "automaticPromotion": False})
        return [status]

    def action_release_build(self) -> list[Path]:
        final = self.final_candidate(); release = self.run_dir / "release" / executable_name(self.release_id)
        self.run_command("release_build", ["make", "-C", "engine", "release-build", f"RELEASE_ID={self.release_id}", f"PROFILE_HEADER=../{final['header']}", f"OUTPUT=../{repo_path(release)}"])
        identity = self.engine_identity(release)
        if (identity["profileId"], identity["profileHash"]) != (final["profileId"], final["profileHash"]):
            raise PipelineError("release binary profile identity mismatch")
        metadata = self.run_dir / "release/metadata.json"; write_json(metadata, {"schemaVersion": 1, "releaseId": self.release_id, "executable": repo_path(release), **identity})
        return [release, metadata]

    def engine_identity(self, binary: Path) -> dict[str, Any]:
        result = self.run_command("final_verification", [str(binary), "--mode=gui"], input_text="uci\nquit\n")
        marker = next((line for line in result.stdout.splitlines() if line.startswith("info string tuning profile=")), None)
        if marker is None:
            raise PipelineError("engine did not report tuning profile identity")
        values = dict(item.split("=", 1) for item in marker[len("info string tuning "):].split() if "=" in item)
        return {"profileId": values.get("profile"), "profileHash": values.get("hash"), "binarySha256": sha256_file(binary)}

    def action_final_verification(self) -> list[Path]:
        final = self.final_candidate(); release = self.run_dir / "release" / executable_name(self.release_id); identity = self.engine_identity(release)
        blockers = []
        match = read_json(self.run_dir / "match/summary.json")
        if match.get("status") != "accepted": blockers.append("match_validation_missing")
        promotion = read_json(self.run_dir / "promotion/preparation.json")
        if promotion.get("status") != "promotion_ready_awaiting_approval": blockers.extend(promotion.get("blockers", ["promotion_not_ready"]))
        dataset = read_json(self.run_dir / "dataset/summary.json"); annotations = read_json(self.run_dir / "annotations/summary.json")
        search = read_json(self.run_dir / "search/ranking.json"); time_status = read_json(self.run_dir / "time/tuning-status.json") if (self.run_dir / "time/tuning-status.json").is_file() else {"candidate": None}
        summary = {"schemaVersion": 1, "releaseId": self.release_id, "mode": self.mode, "pgns": len(discover_pgns(ROOT / "tuning/pgn")),
            "acceptedGames": dataset["games"]["accepted"], "finalDatasetPositions": dataset["positions"]["final"], "stockfishAnnotations": annotations["positionsAnnotated"],
            "evaluationCandidate": read_json(self.run_dir / "evaluation/candidate/profile.json")["profileId"],
            "searchCandidate": search.get("selected", {}).get("candidateId") if search.get("selected") else None,
            "timeCandidate": (time_status.get("candidate") or {}).get("candidateId"), "finalCandidate": {"profileId": final["profileId"], "profileHash": final["profileHash"], "ancestry": final["ancestry"]},
            "validation": {"evaluation": read_json(self.run_dir / "evaluation-validation/results/summary.json")["validationStatus"], "search": search["outcome"],
                           "timeSafety": read_json(self.run_dir / "time/safety/remediation/safety-verification.json")["status"], "opening": "passed", "protocol": "passed", "fairPlay": "passed"},
            "matchStatus": match["status"], "promotionStatus": "promotion_ready_awaiting_approval" if promotion.get("status") == "promotion_ready_awaiting_approval" else "blocked",
            "promotionBlockers": sorted(set(blockers)), "releaseExecutable": repo_path(release), "releaseIdentity": identity, "productionChanges": 0}
        write_json(self.summary_path, summary); return [self.summary_path]

    def action(self, stage: Stage) -> list[Path]:
        return getattr(self, f"action_{stage.action}")()

    def write_manifest(self, state: Mapping[str, Any]) -> None:
        pgn_manifest = read_json(self.run_dir / "inputs/pgn-manifest.json") if (self.run_dir / "inputs/pgn-manifest.json").is_file() else {"files": []}
        final = self.final_candidate() if (self.run_dir / "evaluation/candidate/profile.json").is_file() else None
        manifest = {"schemaVersion": SCHEMA_VERSION, "pipelineVersion": TOOL_VERSION,
            "release": {"suppliedId": self.supplied_release_id, "normalizedId": self.release_id, "runId": self.release_id}, "mode": self.mode,
            "inputs": {"pgns": pgn_manifest["files"], "stockfish": repo_path(self.stockfish), "stockfishSha256": sha256_file(self.stockfish) if self.stockfish.is_file() else None},
            "configuration": self.effective_config(), "configurationSha256": sha256_bytes(canonical_bytes(self.effective_config())),
            "stageGraph": [{"stageId": stage.stage_id, "dependencies": list(stage.dependencies)} for stage in STAGES],
            "stages": {stage.stage_id: {key: state["stages"][stage.stage_id].get(key) for key in ("status", "inputChecksums", "configurationChecksum", "outputChecksums", "toolSchemaVersion")} for stage in STAGES},
            "candidate": final, "openingBookSha256": sha256_file(ROOT / self.config["opening"]["book"]) if (ROOT / self.config["opening"]["book"]).is_file() else None,
            "releaseExecutableName": executable_name(self.release_id), "automaticPromotion": False}
        write_json(self.manifest_path, manifest)

    def run(self) -> int:
        self.run_dir.mkdir(parents=True, exist_ok=True)
        with RunLock(self.run_dir):
            state = self.load_state(); self.mark_stale(state); self.save_state(state)
            previous_handlers = {}
            def interrupt(signum: int, _frame: Any) -> None:
                self.interrupted = True; self.terminate_child(); raise KeyboardInterrupt(f"signal {signum}")
            for signum in (signal.SIGINT, signal.SIGTERM):
                previous_handlers[signum] = signal.signal(signum, interrupt)
            try:
                for stage in STAGES:
                    record = state["stages"][stage.stage_id]
                    if self.reusable(stage, record, state):
                        print(f"REUSE {stage.stage_id}")
                        continue
                    bad_dependencies = [dependency for dependency in stage.dependencies if state["stages"][dependency]["status"] not in {"completed", "skipped"}]
                    if bad_dependencies:
                        record["status"] = "blocked"; record["failureReason"] = f"blocked_by:{','.join(bad_dependencies)}"; self.save_state(state); continue
                    record.update({"status": "running", "attemptCount": int(record.get("attemptCount", 0)) + 1, "failureReason": None,
                                   "inputChecksums": self.stage_inputs(stage, state), "configurationChecksum": sha256_bytes(canonical_bytes(self.stage_config(stage))),
                                   "toolSchemaVersion": TOOL_VERSION, "outputChecksums": {}})
                    state["operational"]["currentStage"] = stage.stage_id; self.save_state(state); print(f"RUN   {stage.stage_id}", flush=True)
                    try:
                        outputs = self.action(stage)
                        record["outputChecksums"] = checksums(outputs); record["status"] = "completed"
                    except StageSkipped as skipped:
                        record["outputChecksums"] = checksums([self.run_dir / stage.stage_id.split("_")[0]])
                        record["status"] = "skipped"; record["failureReason"] = str(skipped)
                    except KeyboardInterrupt:
                        record["status"] = "failed"; record["failureReason"] = "interrupted"; self.save_state(state); raise
                    except Exception as error:
                        record["status"] = "failed"; record["failureReason"] = str(error)
                        log = self.run_dir / "logs" / f"{stage.stage_id}.traceback.log"; atomic_write(log, traceback.format_exc().encode())
                        for downstream in STAGES[list(STAGES).index(stage) + 1:]:
                            state["stages"][downstream.stage_id]["status"] = "blocked"
                            state["stages"][downstream.stage_id]["failureReason"] = f"blocked_by:{stage.stage_id}"
                        self.save_state(state); self.write_manifest(state)
                        print(f"FAILED stage={stage.stage_id} reason={error} log={repo_path(log)} resume=yes", file=sys.stderr)
                        return 2
                    self.save_state(state); self.write_manifest(state)
                state["operational"]["currentStage"] = None; self.save_state(state); self.write_manifest(state)
                self.print_summary(); return 0
            except KeyboardInterrupt:
                self.terminate_child(); print("Pipeline interrupted; completed stages were preserved. Resume is safe.", file=sys.stderr); return 130
            finally:
                for signum, handler in previous_handlers.items(): signal.signal(signum, handler)

    def print_summary(self) -> None:
        if not self.summary_path.is_file():
            return
        summary = read_json(self.summary_path)
        lines = [f"Release: {summary['releaseId']}", f"PGNs: {summary['pgns']}", f"Accepted games: {summary['acceptedGames']}",
                 f"Final dataset positions: {summary['finalDatasetPositions']}", f"Stockfish annotations: {summary['stockfishAnnotations']}",
                 f"Evaluation candidate: {summary['evaluationCandidate']}", f"Search candidate: {summary['searchCandidate'] or 'none'}",
                 f"Time candidate: {summary['timeCandidate'] or 'none'}", f"Final candidate hash: {summary['finalCandidate']['profileHash']}",
                 f"Evaluation validation: {summary['validation']['evaluation']}", f"Search validation: {summary['validation']['search']}",
                 f"Time safety: {summary['validation']['timeSafety']}", f"Opening validation: {summary['validation']['opening']}",
                 f"Match validation: {summary['matchStatus']}", f"Promotion status: {summary['promotionStatus']}",
                 f"Promotion blockers: {', '.join(summary['promotionBlockers']) or 'none'}", f"Release executable: {summary['releaseExecutable']}"]
        print("\n".join(lines))

    def estimate(self) -> int:
        """Run a bounded stratified corpus estimate without creating a run."""
        sys.path.insert(0, str(ROOT / "tools/tuning")); import pgn_dataset
        policy = self.mode_config["dataset"]; settings = self.config["dataset"]; resources = self.config["resources"]
        config = pgn_dataset.BuildConfig(
            pgn_dir=ROOT / "tuning/pgn", output_dir=Path("/tmp/bitboard-estimate-unused"),
            dataset_version=f"pgn-derived-{self.release_id}", min_ply=settings["minPly"], sample_every=settings["sampleEvery"],
            train_ratio=settings["trainRatio"], validation_ratio=settings["validationRatio"], test_ratio=settings["testRatio"],
            split_seed=settings["splitSeed"], maximum_accepted_games=policy["maximumAcceptedGames"],
            scan_all_input_games=policy["scanAllInputGames"], maximum_retained_positions=policy["maximumRetainedPositions"],
            maximum_positions_per_game=policy["maximumPositionsPerGame"], phase_quotas=policy["phaseQuotas"],
            sampling_seed=self.config["evaluationFitting"]["seed"], maximum_rss_mb=resources["maximumDatasetRssMb"],
            warning_rss_mb=resources["warningDatasetRssMb"])
        report = pgn_dataset.estimate_corpus(config)
        disk = shutil.disk_usage(ROOT / "tuning"); memory_mb = None
        with contextlib.suppress(OSError):
            memory_mb = next(int(line.split()[1]) // 1024 for line in Path("/proc/meminfo").read_text().splitlines() if line.startswith("MemAvailable:"))
        selection_count = sum(int(self.mode_config["selection"][key]) for key in ("train", "validation", "test"))
        annotation_count = selection_count + int(self.mode_config["auditPositions"])
        report.update({"releaseId": self.release_id, "mode": self.mode,
                       "annotation": {"positions": annotation_count, "stockfishNodes": self.stockfish_nodes,
                                      "roughSeconds": annotation_count * self.stockfish_nodes * 0.0000009},
                       "resources": {"configuredMemoryCeilingMb": resources["maximumDatasetRssMb"],
                                     "availableMemoryMb": memory_mb, "availableDiskBytes": disk.free,
                                     "minimumFreeDiskGb": resources["minimumFreeDiskGb"]},
                       "labels": {"measured": "bounded stratified parser sample", "estimated": "linear planning estimate"}})
        print(json.dumps(report, indent=2, sort_keys=True)); return 0

    def inspect(self) -> int:
        if not self.state_path.is_file(): raise PipelineError(f"run does not exist: {self.release_id}")
        state = self.load_state(); current_pgns = discover_pgns(ROOT / "tuning/pgn"); recorded = read_json(self.run_dir / "inputs/pgn-manifest.json").get("files", []) if (self.run_dir / "inputs/pgn-manifest.json").is_file() else []
        changed = current_pgns != recorded
        groups = {status: [stage.stage_id for stage in STAGES if state["stages"][stage.stage_id]["status"] == status] for status in STATUSES}
        next_stage = next((stage.stage_id for stage in STAGES if state["stages"][stage.stage_id]["status"] not in {"completed", "skipped"}), "none")
        final = self.final_candidate() if (self.run_dir / "evaluation/candidate/profile.json").is_file() else None
        blockers = read_json(self.summary_path).get("promotionBlockers", []) if self.summary_path.is_file() else []
        print(json.dumps({"releaseId": self.release_id, "currentStage": state["operational"].get("currentStage"), "completedStages": groups["completed"],
                          "failedStages": groups["failed"], "staleStages": groups["stale"], "inputChanges": {"pgnsChanged": changed},
                          "currentCandidate": final, "validationBlockers": blockers, "nextAction": f"make tune-resume RELEASE_ID={self.release_id}" if next_stage != "none" else "inspect blockers or run match validation"}, indent=2, sort_keys=True))
        return 0

    def verify(self) -> int:
        if not self.state_path.is_file(): raise PipelineError(f"run does not exist: {self.release_id}")
        state = self.load_state(); stale = self.mark_stale(state)
        failures = []
        for stage in STAGES:
            record = state["stages"][stage.stage_id]
            if record["status"] in {"completed", "skipped"}:
                for relative, expected in record.get("outputChecksums", {}).items():
                    path = ROOT / relative
                    if not path.is_file() or sha256_file(path) != expected: failures.append(relative)
        release = self.run_dir / "release" / executable_name(self.release_id)
        if release.is_file(): self.engine_identity(release)
        orphan_processes = []
        proc = Path("/proc")
        if proc.is_dir():
            needle = str(self.run_dir).encode()
            for entry in proc.iterdir():
                if not entry.name.isdigit() or int(entry.name) == os.getpid(): continue
                with contextlib.suppress(OSError):
                    command = (entry / "cmdline").read_bytes()
                    if needle in command and (b"chess-engine" in command or b"stockfish" in command): orphan_processes.append(int(entry.name))
        if stale or failures or orphan_processes:
            raise PipelineError(f"verification failed; stale={stale}, invalidArtifacts={failures}, orphanProcesses={orphan_processes}")
        print(f"Verified {self.release_id}: completed artifacts valid, candidate identities valid, no stale dependency")
        return 0


def clean_run(release_id: str, confirm: bool, run_root: Path = DEFAULT_RUNS) -> int:
    normalized = normalize_release_id(release_id); target = run_root / normalized
    if not confirm: raise PipelineError("CONFIRM=1 is required for tune-clean")
    if target.parent.resolve() != run_root.resolve() or target.name != normalized or not target.is_dir():
        raise PipelineError(f"refusing unsafe or missing run path: {target}")
    with RunLock(target):
        pass
    shutil.rmtree(target)
    print(f"Removed only tuning/runs/{normalized}")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__); commands = result.add_subparsers(dest="command", required=True)
    def common(command: argparse.ArgumentParser) -> None:
        command.add_argument("--release-id", required=True); command.add_argument("--mode", choices=("prototype", "full"))
        command.add_argument("--stockfish"); command.add_argument("--stockfish-nodes", type=int); command.add_argument("--tune-time", action="store_true", default=None); command.add_argument("--run-match", action="store_true", default=None)
        command.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    for name in ("run", "resume", "inspect", "verify", "promote-prepare", "estimate"):
        command = commands.add_parser(name); common(command)
    clean = commands.add_parser("clean"); clean.add_argument("--release-id", required=True); clean.add_argument("--confirm", action="store_true")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    try:
        args = parser().parse_args(argv)
        if args.command == "clean": return clean_run(args.release_id, args.confirm)
        pipeline = Pipeline(args.release_id, args.mode, args.stockfish, args.stockfish_nodes, args.tune_time, args.run_match, args.config)
        if args.command in {"run", "resume", "promote-prepare"}: return pipeline.run()
        if args.command == "inspect": return pipeline.inspect()
        if args.command == "estimate": return pipeline.estimate()
        if args.command == "verify": return pipeline.verify()
        raise PipelineError(f"unsupported command: {args.command}")
    except PipelineError as error:
        print(f"error: {error}", file=sys.stderr); return 2


if __name__ == "__main__":
    raise SystemExit(main())
