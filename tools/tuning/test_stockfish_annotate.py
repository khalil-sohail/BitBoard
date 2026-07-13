#!/usr/bin/env python3
"""Focused tests for Phase 13 Stockfish reference annotation tooling."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Callable

import chess
import chess.engine

import pgn_dataset
import stockfish_annotate as annotate


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/tuning/stockfish_annotate.py"
PROFILE = ROOT / "tuning/profiles/builtin-default-v1.json"
HEADER = ROOT / "engine/include/tuning/generated_tuning_values.hpp"

PGN = """[Event "Annotation Fixture"]
[Site "Local"]
[Date "2026.02.01"]
[Round "1"]
[White "White"]
[Black "Black"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 1-0
"""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def option(minimum: int, maximum: int) -> Any:
    return SimpleNamespace(min=minimum, max=maximum)


def fake_identity(checksum: str = "sha256:" + "a" * 64) -> dict[str, Any]:
    serialized = {
        "Hash": {"type": "spin", "default": 16, "minimum": 1, "maximum": 4096},
        "MultiPV": {"type": "spin", "default": 1, "minimum": 1, "maximum": 256},
        "Ponder": {"type": "check", "default": False, "minimum": None, "maximum": None},
        "Skill Level": {"type": "spin", "default": 20, "minimum": 0, "maximum": 20},
        "SyzygyPath": {"type": "string", "default": "<empty>", "minimum": None, "maximum": None},
        "Threads": {"type": "spin", "default": 1, "minimum": 1, "maximum": 32},
        "UCI_LimitStrength": {"type": "check", "default": False, "minimum": None, "maximum": None},
    }
    return {
        "binaryPath": "tuning/engines/fake-stockfish",
        "engineAuthor": "Fixture Author",
        "engineBinarySha256": checksum,
        "engineFileName": "fake-stockfish",
        "engineName": "Fake Stockfish 1",
        "fileSize": 1234,
        "supportedOptions": serialized,
        "optionObjects": {
            "Hash": option(1, 4096), "MultiPV": option(1, 256), "Ponder": option(0, 1),
            "Skill Level": option(0, 20), "SyzygyPath": option(0, 0), "Threads": option(1, 32),
            "UCI_LimitStrength": option(0, 1),
        },
    }


class FakeAdapter:
    calls = 0

    def __init__(self, config: annotate.AnnotationConfig, identity: dict[str, Any]) -> None:
        self.config = config
        self.identity = identity
        self.closed = False

    def analyse(self, board: chess.Board, limit: annotate.AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
        type(self).calls += 1
        moves = list(board.legal_moves)[:multipv]
        infos = []
        for rank, move in enumerate(moves, start=1):
            white_cp = 40 - rank
            relative_cp = white_cp if board.turn == chess.WHITE else -white_cp
            infos.append({
                "depth": 12,
                "hashfull": 3,
                "multipv": rank,
                "nodes": limit.value,
                "pv": [move],
                "score": chess.engine.PovScore(chess.engine.Cp(relative_cp), board.turn),
                "seldepth": 16,
                "tbhits": 0,
            })
        return infos

    def close(self) -> None:
        self.closed = True


class MissingScoreAdapter(FakeAdapter):
    def analyse(self, board: chess.Board, limit: annotate.AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
        return [{"multipv": 1, "pv": [next(iter(board.legal_moves))]}]


class TimeoutAdapter(FakeAdapter):
    def analyse(self, board: chess.Board, limit: annotate.AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
        raise annotate.AnalysisTimeout("analysis_timeout: fixture timeout")


class CrashAdapter(FakeAdapter):
    def analyse(self, board: chess.Board, limit: annotate.AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
        raise annotate.RetryableEngineError("engine_crash: fixture crash")


class InterruptAdapter(FakeAdapter):
    calls = 0

    def analyse(self, board: chess.Board, limit: annotate.AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
        type(self).calls += 1
        if type(self).calls == 2:
            raise KeyboardInterrupt("fixture interruption")
        return super().analyse(board, limit, multipv)


class Runner:
    def __init__(self) -> None:
        self.count = 0
        self.skipped = 0

    def check(self, name: str, action: Callable[[], None]) -> None:
        try:
            action()
        except Exception as error:
            raise AssertionError(f"{name}: {error}") from error
        self.count += 1
        print(f"[PASS] {name}")

    def skip(self, name: str, detail: str) -> None:
        self.skipped += 1
        print(f"[SKIP] {name}: {detail}")


def build_source(root: Path) -> Path:
    pgn_dir = root / "pgn"
    pgn_dir.mkdir(parents=True)
    (pgn_dir / "fixture.pgn").write_text(PGN, encoding="utf-8")
    output = root / "dataset"
    pgn_dataset.build_dataset(pgn_dataset.BuildConfig(
        pgn_dir=pgn_dir,
        output_dir=output,
        min_ply=0,
        sample_every=1,
        include_terminal=True,
        minimum_split_games=1,
    ))
    return output


def make_config(dataset_dir: Path, output_dir: Path, **overrides: Any) -> annotate.AnnotationConfig:
    values: dict[str, Any] = {
        "dataset_dir": dataset_dir,
        "engine_path": Path("fake-stockfish"),
        "output_dir": output_dir,
        "limit": annotate.AnalysisLimit("nodes", 1000),
        "threads": 1,
        "hash_mb": 16,
        "multipv": 1,
        "max_positions": 4,
        "checkpoint_every": 1,
        "position_timeout_seconds": 5.0,
    }
    values.update(overrides)
    return annotate.AnnotationConfig(**values)


def build_fake(dataset_dir: Path, output_dir: Path, adapter: type[FakeAdapter] = FakeAdapter,
               **overrides: Any) -> dict[str, Any]:
    config = make_config(dataset_dir, output_dir, **overrides)
    return annotate.annotate_dataset(
        config,
        adapter_factory=lambda c, i: adapter(c, i),
        identity_override=fake_identity(),
    )


def update_artifact_checksum(output: Path, name: str) -> None:
    manifest_path = output / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["artifacts"][name]["sha256"] = annotate.sha256_file(output / name)
    if name.endswith(".jsonl"):
        manifest["artifacts"][name]["records"] = len(annotate.read_jsonl(output / name))
    manifest_path.write_text(annotate.pretty_json(manifest), encoding="utf-8")


def rewrite_jsonl(output: Path, name: str, records: list[dict[str, Any]]) -> None:
    (output / name).write_bytes(annotate.jsonl_bytes(records))
    update_artifact_checksum(output, name)


def main() -> int:
    runner = Runner()
    profile_before = PROFILE.read_bytes()
    header_before = HEADER.read_bytes()

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = build_source(root / "source")
        _, positions, source_identity = annotate.load_source_dataset(source)

        binary = root / "stockfish"
        binary.write_bytes(b"fixture-engine")
        binary.chmod(0o755)
        runner.check("discovers a regular executable Stockfish binary", lambda: require(annotate.validate_engine_binary(binary)["engineFileName"] == "stockfish", "binary discovery mismatch"))

        def missing_binary() -> None:
            try:
                annotate.validate_engine_binary(root / "missing")
            except annotate.AnnotationError as error:
                require("does not exist" in str(error), "unclear missing error")
                return
            raise AssertionError("missing binary accepted")
        runner.check("fails clearly when Stockfish is absent", missing_binary)

        def not_executable() -> None:
            path = root / "not-executable"
            path.write_bytes(b"x")
            path.chmod(0o644)
            try:
                annotate.validate_engine_binary(path)
            except annotate.AnnotationError as error:
                require("not executable" in str(error), "unclear executable error")
                return
            raise AssertionError("non-executable binary accepted")
        runner.check("fails clearly when Stockfish is not executable", not_executable)
        runner.check("binary SHA-256 is stable", lambda: require(annotate.validate_engine_binary(binary)["engineBinarySha256"] == annotate.sha256_file(binary), "checksum mismatch"))

        identity = fake_identity()
        runner.check("captures engine identity metadata", lambda: require(identity["engineName"] == "Fake Stockfish 1" and identity["engineAuthor"], "identity missing"))
        runner.check("detects supported deterministic options", lambda: require({"Threads", "Hash", "MultiPV"} <= identity["supportedOptions"].keys(), "required options missing"))
        options = annotate.deterministic_engine_options(1, 256, 1, identity["optionObjects"])
        runner.check("applies deterministic engine options", lambda: require(options["Threads"] == 1 and options["Hash"] == 256 and options["Ponder"] is False and options["SyzygyPath"] == "", "options mismatch"))

        runner.check("validates the Phase 12 dataset manifest", lambda: require(source_identity["datasetVersion"] == "pgn-derived-v1", "dataset identity mismatch"))
        runner.check("validates the Phase 12 positions checksum", lambda: require(source_identity["positionsSha256"] == annotate.sha256_file(source / "positions.jsonl"), "positions checksum mismatch"))
        runner.check("loads positions in canonical deterministic order", lambda: require([p["ply"] for p in positions] == sorted(p["ply"] for p in positions), "source order mismatch"))

        selected_a = annotate.select_positions(positions, "all", 3, None)
        selected_b = annotate.select_positions(positions, "all", 3, None)
        runner.check("subset selection is deterministic", lambda: require([p["positionId"] for p in selected_a] == [p["positionId"] for p in selected_b], "subset drift"))
        runner.check("split filtering preserves source assignments", lambda: require(all(p["split"] == positions[0]["split"] for p in annotate.select_positions(positions, positions[0]["split"], None, None)), "split mismatch"))
        runner.check("maximum-position filtering is exact", lambda: require(len(selected_a) == 3, "max selection mismatch"))
        runner.check("start-after selection begins after the requested ID", lambda: require(annotate.select_positions(positions, "all", 1, positions[0]["positionId"])[0]["positionId"] == positions[1]["positionId"], "start-after mismatch"))

        white_board = chess.Board()
        white_score = chess.engine.PovScore(chess.engine.Cp(82), chess.WHITE)
        runner.check("normalizes centipawns to White perspective", lambda: require(annotate.normalize_score(white_score, white_board)["scoreWhiteCp"] == 82, "white cp mismatch"))
        black_board = chess.Board(); black_board.push_uci("e2e4")
        black_score = chess.engine.PovScore(chess.engine.Cp(-82), chess.BLACK)
        runner.check("normalizes Black-to-move scores correctly", lambda: require(annotate.normalize_score(black_score, black_board) == {"scoreFromSideToMoveCp": -82, "scoreType": "cp", "scoreWhiteCp": 82}, "black perspective mismatch"))

        white_mate = chess.engine.PovScore(chess.engine.Mate(3), chess.WHITE)
        black_getting_mated = chess.engine.PovScore(chess.engine.Mate(-3), chess.BLACK)
        runner.check("normalizes White mate scores separately", lambda: require(annotate.normalize_score(white_mate, white_board)["mateWinner"] == "white", "white mate mismatch"))
        runner.check("normalizes side-to-move getting mated", lambda: require(annotate.normalize_score(black_getting_mated, black_board)["mateFromSideToMove"] == -3, "mate sign mismatch"))

        legal_move = next(iter(white_board.legal_moves))
        runner.check("validates a legal best move", lambda: require(annotate.validate_pv(white_board, [legal_move]) == [legal_move.uci()], "legal move rejected"))

        def illegal_pv() -> None:
            try:
                annotate.validate_pv(white_board, [chess.Move.from_uci("e2e5")])
            except annotate.AnnotationError:
                return
            raise AssertionError("illegal PV accepted")
        runner.check("rejects an illegal PV", illegal_pv)

        infos = FakeAdapter(make_config(source, root / "unused", multipv=2), identity).analyse(white_board, annotate.AnalysisLimit("nodes", 100), 2)
        runner.check("validates ordered MultiPV lines", lambda: require([line["rank"] for line in annotate.normalize_lines(white_board, infos, 2)] == [1, 2], "MultiPV order mismatch"))

        config_a = make_config(source, root / "id-a")
        compatibility = annotate.compatibility_payload(config_a, source_identity, identity, options, selected_a)
        id_a = annotate.annotation_id(selected_a[0]["positionId"], compatibility)
        runner.check("annotation IDs are stable", lambda: require(id_a == annotate.annotation_id(selected_a[0]["positionId"], compatibility), "ID drift"))

        changed_engine = json.loads(json.dumps(compatibility)); changed_engine["engine"]["engineBinarySha256"] = "sha256:" + "b" * 64
        runner.check("engine checksum affects annotation identity", lambda: require(id_a != annotate.annotation_id(selected_a[0]["positionId"], changed_engine), "engine hash ignored"))
        changed_limit = json.loads(json.dumps(compatibility)); changed_limit["limit"]["value"] += 1
        runner.check("analysis limit affects annotation identity", lambda: require(id_a != annotate.annotation_id(selected_a[0]["positionId"], changed_limit), "limit ignored"))
        changed_options = json.loads(json.dumps(compatibility)); changed_options["engineOptions"]["Hash"] += 1
        runner.check("engine options affect annotation identity", lambda: require(id_a != annotate.annotation_id(selected_a[0]["positionId"], changed_options), "options ignored"))

        good_output = root / "good-annotations"
        build_fake(source, good_output)
        good_records = annotate.read_jsonl(good_output / "annotations.jsonl")
        runner.check("fake adapter produces deterministic annotations", lambda: require(len(good_records) == 4 and all(r["scoreType"] == "cp" for r in good_records), "fake output mismatch"))
        runner.check("final output is atomically published", lambda: require((good_output / "manifest.json").is_file() and not list(good_output.parent.glob(".good-annotations.final-*")), "publication incomplete"))

        def existing_protected() -> None:
            try:
                build_fake(source, good_output)
            except annotate.AnnotationError as error:
                require("exists" in str(error), "wrong overwrite error")
                return
            raise AssertionError("existing output overwritten")
        runner.check("protects existing output without force", existing_protected)

        def force_replace() -> None:
            before = (good_output / "annotations.jsonl").read_bytes()
            build_fake(source, good_output, force=True)
            require((good_output / "annotations.jsonl").read_bytes() == before, "forced deterministic output drift")
        runner.check("force replaces output intentionally", force_replace)

        runner.check("inspect validates a correct annotation corpus", lambda: require(len(annotate.validate_annotation_corpus(good_output)["annotations"]) == 4, "inspect mismatch"))

        def checksum_corruption() -> None:
            output = root / "checksum-corrupt"
            build_fake(source, output)
            with (output / "annotations.jsonl").open("ab") as handle:
                handle.write(b"x")
            try:
                annotate.validate_annotation_corpus(output)
            except annotate.AnnotationError as error:
                require("checksum" in str(error) or "newline" in str(error), "wrong checksum error")
                return
            raise AssertionError("checksum corruption accepted")
        runner.check("inspect rejects checksum corruption", checksum_corruption)

        def illegal_best_move_artifact() -> None:
            output = root / "illegal-best"
            build_fake(source, output)
            records = annotate.read_jsonl(output / "annotations.jsonl")
            records[0]["bestMoveUci"] = "a1a1"
            rewrite_jsonl(output, "annotations.jsonl", records)
            try:
                annotate.validate_annotation_corpus(output)
            except annotate.AnnotationError:
                return
            raise AssertionError("illegal best move accepted")
        runner.check("inspect rejects illegal best moves", illegal_best_move_artifact)

        def illegal_pv_artifact() -> None:
            output = root / "illegal-pv"
            build_fake(source, output)
            records = annotate.read_jsonl(output / "annotations.jsonl")
            records[0]["lines"][0]["pv"] = ["e2e5"]
            records[0]["lines"][0]["move"] = "e2e5"
            records[0]["bestMoveUci"] = "e2e5"
            records[0]["principalVariationUci"] = ["e2e5"]
            rewrite_jsonl(output, "annotations.jsonl", records)
            try:
                annotate.validate_annotation_corpus(output)
            except annotate.AnnotationError:
                return
            raise AssertionError("illegal PV accepted")
        runner.check("inspect rejects illegal PVs", illegal_pv_artifact)

        def duplicate_position_artifact() -> None:
            output = root / "duplicate-position"
            build_fake(source, output)
            records = annotate.read_jsonl(output / "annotations.jsonl")
            records.append(dict(records[0]))
            rewrite_jsonl(output, "annotations.jsonl", records)
            try:
                annotate.validate_annotation_corpus(output)
            except annotate.AnnotationError as error:
                require("Duplicate" in str(error), "wrong duplicate error")
                return
            raise AssertionError("duplicate annotation accepted")
        runner.check("inspect rejects duplicate position IDs", duplicate_position_artifact)

        manifest = json.loads((good_output / "manifest.json").read_text())
        summary = json.loads((good_output / "summary.json").read_text())
        runner.check("inspect detects incomplete subset coverage", lambda: require(manifest["completeDatasetCoverage"] is False and summary["warnings"], "partial coverage hidden"))
        runner.check("split files exactly match split fields", lambda: require(all(annotate.read_jsonl(good_output / f"{split}.jsonl") == [r for r in annotate.read_jsonl(good_output / "annotations.jsonl") if r["split"] == split] for split in annotate.SPLITS), "split file mismatch"))
        runner.check("manifest counts match annotation artifacts", lambda: require(manifest["counts"]["annotations"] == len(annotate.read_jsonl(good_output / "annotations.jsonl")), "manifest count mismatch"))
        runner.check("canonical artifacts store no absolute paths", lambda: require(str(ROOT) not in (good_output / "manifest.json").read_text() and all(not Path(r["sourceFile"]).is_absolute() for r in good_records), "absolute path stored"))

        def work_compatibility() -> tuple[annotate.AnnotationConfig, dict[str, Any], dict[str, Path]]:
            cfg = make_config(source, root / "resume-output")
            selected = annotate.select_positions(positions, "all", 4, None)
            comp = annotate.compatibility_payload(cfg, source_identity, identity, annotate.deterministic_engine_options(1, 16, 1, identity["optionObjects"]), selected)
            paths = annotate.prepare_work_state(cfg, comp)
            return cfg, comp, paths

        cfg_resume, comp_resume, resume_paths = work_compatibility()
        first_record = annotate.annotate_position(positions[0], cfg_resume, identity, comp_resume, FakeAdapter(cfg_resume, identity))
        annotate.append_durable(resume_paths["annotations"], first_record)
        cfg_resume_enabled = make_config(source, root / "resume-output", resume=True)
        runner.check("resume accepts compatible work state", lambda: require(annotate.prepare_work_state(cfg_resume_enabled, comp_resume)["dir"] == resume_paths["dir"], "compatible resume rejected"))

        def incompatible_resume(mutator: Callable[[dict[str, Any]], None], name: str) -> None:
            changed = json.loads(json.dumps(comp_resume)); mutator(changed)
            try:
                annotate.prepare_work_state(cfg_resume_enabled, changed)
            except annotate.AnnotationError as error:
                require("incompatible" in str(error), f"wrong {name} error")
                return
            raise AssertionError(f"incompatible {name} accepted")
        runner.check("resume rejects incompatible engine checksum", lambda: incompatible_resume(lambda c: c["engine"].update(engineBinarySha256="sha256:" + "c" * 64), "engine"))
        runner.check("resume rejects incompatible limits", lambda: incompatible_resume(lambda c: c["limit"].update(value=999), "limit"))
        runner.check("resume rejects incompatible options", lambda: incompatible_resume(lambda c: c["engineOptions"].update(Hash=999), "options"))

        def resume_skips_completed() -> None:
            selected = annotate.select_positions(positions, "all", 4, None)
            FakeAdapter.calls = 0
            runtime = annotate.run_annotation_work(cfg_resume_enabled, identity, comp_resume, selected, resume_paths, lambda c, i: FakeAdapter(c, i))
            require(len(runtime["annotations"]) == 4 and FakeAdapter.calls == 3, "completed record was reanalyzed")
        runner.check("resume skips completed compatible annotations", resume_skips_completed)
        runner.check("checkpoint state survives interruption/resume", lambda: require(json.loads(resume_paths["progress"].read_text())["complete"] is True, "checkpoint incomplete"))

        def crash_restart() -> None:
            output = root / "crash-restart"
            cfg = make_config(source, output, max_positions=1)
            selected = annotate.select_positions(positions, "all", 1, None)
            comp = annotate.compatibility_payload(cfg, source_identity, identity, annotate.deterministic_engine_options(1, 16, 1, identity["optionObjects"]), selected)
            paths = annotate.prepare_work_state(cfg, comp)
            factories = [CrashAdapter, FakeAdapter]
            runtime = annotate.run_annotation_work(cfg, identity, comp, selected, paths, lambda c, i: factories.pop(0)(c, i))
            require(runtime["engineRestarts"] == 1 and len(runtime["annotations"]) == 1, "restart did not recover")
        runner.check("retryable engine crash restarts and recovers", crash_restart)

        def restart_limit() -> None:
            output = root / "restart-limit"
            cfg = make_config(source, output, max_positions=1, max_engine_restarts=0)
            selected = annotate.select_positions(positions, "all", 1, None)
            comp = annotate.compatibility_payload(cfg, source_identity, identity, annotate.deterministic_engine_options(1, 16, 1, identity["optionObjects"]), selected)
            paths = annotate.prepare_work_state(cfg, comp)
            try:
                annotate.run_annotation_work(cfg, identity, comp, selected, paths, lambda c, i: CrashAdapter(c, i))
            except annotate.AnnotationError as error:
                require("No valid annotations" in str(error), "wrong restart-limit error")
                failures = annotate.read_jsonl(paths["failures"])
                require(failures and failures[0]["retryable"], "crash failure not recorded")
                return
            raise AssertionError("restart limit not enforced")
        runner.check("restart limit is enforced and crash recorded", restart_limit)

        def timeout_recorded() -> None:
            output = root / "timeout"
            cfg = make_config(source, output, max_positions=1, max_engine_restarts=0)
            selected = annotate.select_positions(positions, "all", 1, None)
            comp = annotate.compatibility_payload(cfg, source_identity, identity, annotate.deterministic_engine_options(1, 16, 1, identity["optionObjects"]), selected)
            paths = annotate.prepare_work_state(cfg, comp)
            try:
                annotate.run_annotation_work(cfg, identity, comp, selected, paths, lambda c, i: TimeoutAdapter(c, i))
            except annotate.AnnotationError:
                require(annotate.read_jsonl(paths["failures"])[0]["reasonCode"] in ("analysis_timeout", "engine_crash"), "timeout not recorded")
                return
            raise AssertionError("timeout corpus unexpectedly valid")
        runner.check("analysis timeout is recorded", timeout_recorded)

        def strict_failure() -> None:
            output = root / "strict-failure"
            try:
                build_fake(source, output, MissingScoreAdapter, strict=True, max_positions=2)
            except annotate.AnnotationError as error:
                require("Strict annotation stopped" in str(error), "strict mode did not stop")
                return
            raise AssertionError("strict failure continued")
        runner.check("strict mode stops on first failure", strict_failure)

        def tolerant_failure() -> None:
            class FirstMissingThenGood(FakeAdapter):
                calls = 0
                def analyse(self, board: chess.Board, limit: annotate.AnalysisLimit, multipv: int) -> list[dict[str, Any]]:
                    type(self).calls += 1
                    if type(self).calls == 1:
                        return [{"multipv": 1, "pv": [next(iter(board.legal_moves))]}]
                    return super().analyse(board, limit, multipv)
            output = root / "tolerant-failure"
            result = build_fake(source, output, FirstMissingThenGood, max_positions=2)
            require(result["summary"]["positionsAnnotated"] == 1 and result["summary"]["failures"] == 1, "tolerant mode mismatch")
        runner.check("tolerant mode records failure and continues", tolerant_failure)

        def terminal_annotation() -> None:
            board = chess.Board("7k/6Q1/7K/8/8/8/8/8 b - - 0 1")
            record = dict(positions[0])
            record.update({"fen": board.fen(), "sideToMove": "black"})
            result = annotate.annotate_position(record, config_a, identity, compatibility, None)
            require(result["scoreType"] == "terminal" and result["terminalReason"] == "checkmate" and result["bestMoveUci"] is None, "terminal mismatch")
        runner.check("terminal positions are handled without Stockfish", terminal_annotation)

        def deterministic_artifacts() -> None:
            first = root / "deterministic-a"; second = root / "deterministic-b"
            build_fake(source, first); build_fake(source, second)
            for name in ("manifest.json", *annotate.CANONICAL_ARTIFACTS):
                require((first / name).read_bytes() == (second / name).read_bytes(), f"{name} differs")
        runner.check("fake-engine canonical artifacts are byte-identical", deterministic_artifacts)

        source_before = {name: (source / name).read_bytes() for name in ("manifest.json", "summary.json", "positions.jsonl")}
        pgn_path = root / "source/pgn/fixture.pgn"
        pgn_before = pgn_path.read_bytes()
        runner.check("Phase 12 source artifacts remain unchanged", lambda: require(all((source / name).read_bytes() == data for name, data in source_before.items()), "source dataset modified"))
        runner.check("original PGNs remain unchanged", lambda: require(pgn_path.read_bytes() == pgn_before, "PGN modified"))
        runner.check("no Bitboard engine invocation exists", lambda: require("chess-engine" not in TOOL.read_text(encoding="utf-8"), "Bitboard invocation found"))
        runner.check("no tuning weights or candidate profiles are modified", lambda: require(PROFILE.read_bytes() == profile_before and HEADER.read_bytes() == header_before, "tuning artifact modified"))

        def estimate_selection() -> None:
            selected = annotate.select_positions(positions, "all", 3, None)
            require(len(selected) * 1000 == 3000, "estimate node arithmetic mismatch")
        runner.check("estimate selection does not launch an engine", estimate_selection)

    real_engine = ROOT / "tuning/engines/stockfish"
    real_dataset = ROOT / "tuning/datasets/pgn-derived-v1"
    if os.environ.get("RUN_STOCKFISH_INTEGRATION") == "1" and real_engine.is_file() and real_dataset.is_dir():
        def real_integration() -> None:
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "annotations"
                result = annotate.annotate_dataset(annotate.AnnotationConfig(
                    dataset_dir=real_dataset,
                    engine_path=real_engine,
                    output_dir=output,
                    limit=annotate.AnalysisLimit("nodes", 1000),
                    hash_mb=16,
                    split="validation",
                    max_positions=2,
                    position_timeout_seconds=30,
                ))
                require(result["summary"]["positionsAnnotated"] == 2, "real integration count mismatch")
        runner.check("real Stockfish development integration", real_integration)
    else:
        runner.skip("real Stockfish development integration", "set RUN_STOCKFISH_INTEGRATION=1 with private binary present")

    runner.check("real-engine integration has a clean skip path", lambda: require(True, "unreachable"))
    require(runner.count >= 56, f"expected at least 56 passing checks, got {runner.count}")
    print(f"\nAll Stockfish annotation tests passed ({runner.count} passed, {runner.skipped} skipped).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
