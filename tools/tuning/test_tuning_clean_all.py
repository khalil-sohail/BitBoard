#!/usr/bin/env python3
"""Focused safety tests for the top-level tuning-clean-all target."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATED = (
    "annotations", "builds", "candidates", "datasets", "features", "promotion",
    "runs", "search", "selections", "time", "validation",
)
PRESERVED_DIRS = ("pgn", "engines", "profiles", "schema")
PRESERVED_FILES = ("parameter-registry.json", "pipeline-config.json", "promotion-policy.json")


class TuningCleanAllTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bitboard-tuning-clean-")
        self.repo = Path(self.temporary.name)
        shutil.copy2(ROOT / "Makefile", self.repo / "Makefile")
        subprocess.run(["git", "init", "--quiet"], cwd=self.repo, check=True)
        for name in GENERATED + PRESERVED_DIRS:
            (self.repo / "tuning" / name).mkdir(parents=True)
        for name in PRESERVED_FILES:
            path = self.repo / "tuning" / name
            path.write_text(f"preserve {name}\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_make(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["make", "tuning-clean-all", *arguments], cwd=self.repo,
            text=True, capture_output=True, check=False,
        )

    def test_missing_confirmation_fails_without_removing_generated_files(self) -> None:
        generated = self.repo / "tuning" / "runs" / "keep-unconfirmed"
        generated.write_text("generated\n", encoding="utf-8")
        result = self.run_make()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CONFIRM=1 is required", result.stderr)
        self.assertTrue(generated.is_file())

    def test_tracked_file_blocks_cleanup_and_prints_its_path(self) -> None:
        tracked = self.repo / "tuning" / "datasets" / "tracked.jsonl"
        tracked.write_text("tracked\n", encoding="utf-8")
        subprocess.run(["git", "add", "--", "tuning/datasets/tracked.jsonl"], cwd=self.repo, check=True)
        other = self.repo / "tuning" / "runs" / "generated"
        other.write_text("generated\n", encoding="utf-8")
        result = self.run_make("CONFIRM=1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tuning/datasets/tracked.jsonl", result.stderr)
        self.assertTrue(tracked.is_file())
        self.assertTrue(other.is_file())

    def test_generated_contents_removed_and_preserved_inputs_untouched(self) -> None:
        for name in GENERATED:
            directory = self.repo / "tuning" / name
            (directory / "nested").mkdir()
            (directory / "nested" / "artifact").write_text("generated\n", encoding="utf-8")
            (directory / ".hidden").write_text("generated hidden\n", encoding="utf-8")
        sentinels: list[Path] = []
        for name in PRESERVED_DIRS:
            sentinel = self.repo / "tuning" / name / "sentinel"
            sentinel.write_text("preserved\n", encoding="utf-8")
            sentinels.append(sentinel)
        sentinels.extend(self.repo / "tuning" / name for name in PRESERVED_FILES)
        preserved_contents = {sentinel: sentinel.read_bytes() for sentinel in sentinels}
        outside = self.repo / "outside-generated-roots"
        outside.write_text("must not be removed\n", encoding="utf-8")

        result = self.run_make("CONFIRM=1", "TUNING_GENERATED_DIRS=outside-generated-roots")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Removed", result.stdout)
        self.assertIn("Preserved: tuning/pgn", result.stdout)
        for name in GENERATED:
            directory = self.repo / "tuning" / name
            self.assertTrue(directory.is_dir())
            self.assertEqual(list(directory.iterdir()), [])
        for sentinel in sentinels:
            self.assertEqual(sentinel.read_bytes(), preserved_contents[sentinel])
        self.assertTrue(outside.is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
