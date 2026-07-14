#!/usr/bin/env python3
"""Focused Phase 21 orchestration and public-interface tests."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tuning_pipeline as pipeline


class ReleaseIdentityTests(unittest.TestCase):
    def test_valid_examples_and_executable_names(self) -> None:
        for value in ("v2", "v3", "2026.1", "exp-001", "full-corpus-v1"):
            with self.subTest(value=value):
                self.assertEqual(pipeline.normalize_release_id(value), value)
                self.assertEqual(pipeline.executable_name(value), f"chess-engine-{value}")

    def test_case_normalization_is_canonical(self) -> None:
        self.assertEqual(pipeline.normalize_release_id("V2"), "v2")

    def test_missing_invalid_traversal_and_metacharacters(self) -> None:
        for value in ("", " v2", "v2 ", "../v2", "/v2", "a/b", "a\\b", "a..b", "v2;rm", "v2$HOME", "v2."):
            with self.subTest(value=value):
                with self.assertRaises(pipeline.PipelineError):
                    pipeline.normalize_release_id(value)


class PgnDiscoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(dir=pipeline.ROOT / "tuning", prefix="pipeline-pgn-test-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.temp, ignore_errors=True)

    def write(self, name: str, value: bytes = b"[Event \"x\"]\n\n1. e4 e5 1/2-1/2\n") -> None:
        (self.temp / name).write_bytes(value)

    def test_one_multiple_stable_and_non_pgn_ignored(self) -> None:
        self.write("z.pgn", b"z"); self.write("a.pgn", b"a"); self.write("ignore.txt", b"x")
        found = pipeline.discover_pgns(self.temp)
        self.assertEqual([Path(item["path"]).name for item in found], ["a.pgn", "z.pgn"])
        self.assertTrue(all(item["sha256"].startswith("sha256:") for item in found))

    def test_no_pgn(self) -> None:
        self.write("readme.txt")
        with self.assertRaisesRegex(pipeline.PipelineError, "no regular"):
            pipeline.discover_pgns(self.temp)

    def test_empty_pgn(self) -> None:
        self.write("empty.pgn", b"")
        with self.assertRaisesRegex(pipeline.PipelineError, "empty PGN"):
            pipeline.discover_pgns(self.temp)

    def test_duplicate_content(self) -> None:
        self.write("a.pgn", b"same"); self.write("b.pgn", b"same")
        with self.assertRaisesRegex(pipeline.PipelineError, "duplicate PGN content"):
            pipeline.discover_pgns(self.temp)

    def test_checksum_deterministic(self) -> None:
        self.write("a.pgn", b"stable")
        self.assertEqual(pipeline.discover_pgns(self.temp), pipeline.discover_pgns(self.temp))


class StateAndResumeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.base = Path(tempfile.mkdtemp(dir=pipeline.ROOT / "tuning", prefix="pipeline-state-test-"))
        self.runs = self.base / "runs"
        self.subject = pipeline.Pipeline("unit-v1", "prototype", run_root=self.runs)
        self.subject.run_dir.mkdir(parents=True)

    def tearDown(self) -> None:
        shutil.rmtree(self.base, ignore_errors=True)

    def complete(self, stage_id: str, output: Path) -> tuple[dict, pipeline.Stage]:
        state = self.subject.initial_state(); stage = pipeline.STAGE_BY_ID[stage_id]
        output.parent.mkdir(parents=True, exist_ok=True); output.write_text("valid", encoding="utf-8")
        record = state["stages"][stage_id]
        record.update(status="completed", inputChecksums=self.subject.stage_inputs(stage, state),
                      configurationChecksum=pipeline.sha256_bytes(pipeline.canonical_bytes(self.subject.stage_config(stage))),
                      toolSchemaVersion=pipeline.TOOL_VERSION, outputChecksums={pipeline.repo_path(output): pipeline.sha256_file(output)})
        return state, stage

    def test_new_run_initialization_and_graph(self) -> None:
        state = self.subject.initial_state()
        self.assertTrue(all(record["status"] == "pending" for record in state["stages"].values()))
        self.assertEqual(pipeline.STAGE_BY_ID["annotation"].dependencies, ("dataset",))
        self.assertEqual(pipeline.STAGE_BY_ID["evaluation_fit"].dependencies, ("evaluation_features",))
        self.assertEqual(pipeline.STAGE_BY_ID["search_tuning"].dependencies, ("evaluation_validation",))
        self.assertEqual(pipeline.STAGE_BY_ID["time_safety"].dependencies, ("search_build",))
        self.assertEqual(pipeline.STAGE_BY_ID["promotion_inspection"].dependencies, ("match_validation",))

    def test_completed_stage_reused_then_missing_or_changed_output_invalidates(self) -> None:
        output = self.subject.run_dir / "inputs/preflight.json"; state, stage = self.complete("preflight", output)
        self.assertTrue(self.subject.reusable(stage, state["stages"][stage.stage_id], state))
        output.unlink(); self.assertFalse(self.subject.reusable(stage, state["stages"][stage.stage_id], state))
        output.write_text("changed", encoding="utf-8"); self.assertFalse(self.subject.reusable(stage, state["stages"][stage.stage_id], state))

    def test_configuration_change_invalidates(self) -> None:
        output = self.subject.run_dir / "inputs/preflight.json"; state, stage = self.complete("preflight", output)
        self.subject.mode = "full"; self.subject.mode_config = self.subject.config["modes"]["full"]
        self.assertFalse(self.subject.reusable(stage, state["stages"][stage.stage_id], state))

    def test_stale_propagates_downstream(self) -> None:
        state = self.subject.initial_state()
        for stage in pipeline.STAGES:
            state["stages"][stage.stage_id]["status"] = "completed"
        state["stages"]["dataset"]["configurationChecksum"] = "sha256:" + "0" * 64
        stale = self.subject.mark_stale(state)
        self.assertIn("dataset", stale); self.assertIn("final_verification", stale)

    def test_failed_and_running_are_resumable(self) -> None:
        state = self.subject.initial_state(); state["stages"]["annotation"]["status"] = "failed"
        self.assertFalse(self.subject.reusable(pipeline.STAGE_BY_ID["annotation"], state["stages"]["annotation"], state))
        state["stages"]["annotation"]["status"] = "running"; self.subject.save_state(state)
        self.assertEqual(self.subject.load_state()["stages"]["annotation"]["status"], "failed")

    def test_atomic_write_leaves_no_temporary_file(self) -> None:
        target = self.subject.run_dir / "state.json"; pipeline.atomic_write(target, b"one\n"); pipeline.atomic_write(target, b"two\n")
        self.assertEqual(target.read_bytes(), b"two\n")
        self.assertEqual(list(target.parent.glob(".state.json.*.tmp")), [])

    def test_concurrent_lock_and_safe_stale_lock(self) -> None:
        with pipeline.RunLock(self.subject.run_dir):
            with self.assertRaisesRegex(pipeline.PipelineError, "locked by live process"):
                with pipeline.RunLock(self.subject.run_dir): pass
        lock = self.subject.run_dir / ".lock"; lock.mkdir(); pipeline.write_json(lock / "owner.json", {"pid": 99999999})
        with pipeline.RunLock(self.subject.run_dir): self.assertTrue(lock.is_dir())

    def test_stale_search_summary_checksum_forces_rerun(self) -> None:
        state = self.subject.initial_state(); output = self.subject.run_dir / "search/summary.json"; state, stage = self.complete("search_tuning", output)
        output.write_text('{"variantResultsSha256":"sha256:70e579"}\n', encoding="utf-8")
        self.assertFalse(self.subject.reusable(stage, state["stages"][stage.stage_id], state))


class CandidateAndPolicyTests(unittest.TestCase):
    def test_candidate_outcomes_are_representable(self) -> None:
        outcomes = ("evaluation_only", "evaluation_and_search", "evaluation_search_and_time")
        self.assertEqual(len(set(outcomes)), 3)

    def test_time_and_match_default_off_and_promotion_never_automatic(self) -> None:
        config = pipeline.read_json(pipeline.DEFAULT_CONFIG)
        self.assertFalse(config["time"]["tuneByDefault"])
        self.assertFalse(config["match"]["runByDefault"])
        self.assertFalse(config["promotion"]["automaticPromotion"])
        self.assertEqual(config["match"]["minimumGames"], 1000)

    def test_stage_status_vocabulary(self) -> None:
        self.assertEqual(pipeline.STATUSES, {"pending", "running", "completed", "failed", "blocked", "skipped", "stale"})


class InterfaceAndDocumentationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (pipeline.ROOT / "Makefile").read_text(encoding="utf-8")
        cls.docs = (pipeline.ROOT / "docs/TUNING_PIPELINE.md").read_text(encoding="utf-8")

    def test_make_requires_release_id(self) -> None:
        result = subprocess.run(["make", "tune"], cwd=pipeline.ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 2); self.assertIn("RELEASE_ID is required", result.stderr)

    def test_make_targets_exist_and_invoke_pipeline(self) -> None:
        for target in ("tune", "tune-resume", "tune-inspect", "tune-verify", "tune-clean", "tune-match", "tune-promote-prepare"):
            with self.subTest(target=target): self.assertIn(f"{target}:", self.makefile)
        self.assertIn("tuning_pipeline.py run", self.makefile); self.assertIn("tuning_pipeline.py resume", self.makefile)
        self.assertNotIn("tuning_pipeline.py promote --", self.makefile)

    def test_overrides_are_wired(self) -> None:
        for variable in ("STOCKFISH", "STOCKFISH_NODES", "TUNE_TIME", "RUN_MATCH", "TUNE_MODE", "CONFIRM"):
            with self.subTest(variable=variable): self.assertIn(f"$({variable})", self.makefile)

    def test_documentation_commands_and_paths_match(self) -> None:
        for command in ("make tune RELEASE_ID=v2", "make tune-resume RELEASE_ID=v2", "make tune-inspect RELEASE_ID=v2",
                        "make tune-verify RELEASE_ID=v2", "make tune-match RELEASE_ID=v2", "make tune-clean RELEASE_ID=v2 CONFIRM=1"):
            with self.subTest(command=command): self.assertIn(command, self.docs)
        self.assertIn("tuning/runs/v2/release/chess-engine-v2", self.docs)
        self.assertIn("never promotes automatically", self.docs)

    def test_private_runs_ignored_and_production_output_not_targeted(self) -> None:
        ignored = (pipeline.ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("tuning/runs/", ignored)
        source = (pipeline.ROOT / "tools/tuning/tuning_pipeline.py").read_text(encoding="utf-8")
        self.assertNotIn('OUTPUT=../engine/chess-engine"', source)


if __name__ == "__main__":
    unittest.main()
