#!/usr/bin/env python3
"""Focused tests for deterministic Phase 4 tuning header generation."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from canonical_json import profile_hash
from validate_profile import load_profile


ROOT = Path(__file__).resolve().parents[2]
PROFILE = ROOT / "tuning/profiles/builtin-default-v1.json"
REGISTRY = ROOT / "tuning/parameter-registry.json"
GENERATOR = ROOT / "tools/tuning/generate_tuning_header.py"
TRACKED_HEADER = ROOT / "engine/include/tuning/generated_tuning_values.hpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_generator(profile: Path, output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--profile",
            str(profile),
            "--registry",
            str(REGISTRY),
            "--output",
            str(output),
            *extra,
        ],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def write_profile(path: Path, profile: dict) -> None:
    path.write_text(json.dumps(profile, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    profile = load_profile(PROFILE)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        header = tmp_path / "generated_tuning_values.hpp"

        result = run_generator(PROFILE, header)
        require(result.returncode == 0, result.stderr)
        first = header.read_text(encoding="utf-8")
        require("Generated from the canonical tuning profile." in first, "header should contain generated marker")
        require("sha256:55a1ac92352bd018460f115cb5061c76140f1eed453afc8a229ed3fa84145718" in first, "header hash mismatch")
        require("builtin-default-v1" in first, "header profile id mismatch")
        require("RationalValue<int>{3, 4}" in first, "LMR rational should be emitted exactly")
        require("RationalValue<int>{13, 10}" in first, "instability rational should be emitted exactly")
        require("BookSelectionMode::Weighted" in first, "opening enum should use explicit C++ mapping")
        require("MIDDLEGAME_PIECE_SQUARE_TABLE" in first, "MG PST should be emitted")
        require("ENDGAME_PIECE_SQUARE_TABLE" in first, "EG PST should be emitted")
        require("82, 337, 365" not in first, "old Texel defaults must not appear")
        require("/home/" not in first and "tmp" not in first.lower(), "header must not contain machine paths")
        require("Generated on" not in first and "timestamp" not in first.lower(), "header must not contain generation timestamps")

        second = run_generator(PROFILE, header)
        require(second.returncode == 0, second.stderr)
        require(header.read_text(encoding="utf-8") == first, "identical generation should not rewrite bytes")

        check = run_generator(PROFILE, header, "--check")
        require(check.returncode == 0, check.stderr)

        stale = first.replace("GENERATED_PROFILE_ENTRY_COUNT = 76", "GENERATED_PROFILE_ENTRY_COUNT = 75")
        header.write_text(stale, encoding="utf-8")
        stale_check = run_generator(PROFILE, header, "--check")
        require(stale_check.returncode != 0, "stale generated header should fail --check")

        missing_check = run_generator(PROFILE, tmp_path / "missing.hpp", "--check")
        require(missing_check.returncode != 0, "missing generated header should fail --check")

        stale_hash_profile = copy.deepcopy(profile)
        stale_hash_profile["parameters"]["evaluation.bishopPair.mg"] += 1
        stale_hash_path = tmp_path / "stale-hash.json"
        write_profile(stale_hash_path, stale_hash_profile)
        before_files = set(tmp_path.iterdir())
        invalid = run_generator(stale_hash_path, tmp_path / "invalid.hpp")
        require(invalid.returncode != 0, "stale profile hash should fail before writing")
        require(not (tmp_path / "invalid.hpp").exists(), "invalid profile must not leave output")
        require(before_files == set(tmp_path.iterdir()), "invalid profile should not leave partial files")

        unknown = copy.deepcopy(profile)
        unknown["parameters"]["eval.tempo"] = 0
        unknown["canonicalHash"] = profile_hash(unknown)
        unknown_path = tmp_path / "unknown.json"
        write_profile(unknown_path, unknown)
        require(run_generator(unknown_path, tmp_path / "unknown.hpp").returncode != 0, "unknown parameter should fail")

        missing = copy.deepcopy(profile)
        missing["parameters"].pop("evaluation.bishopPair.mg")
        missing["canonicalHash"] = profile_hash(missing)
        missing_path = tmp_path / "missing-param.json"
        write_profile(missing_path, missing)
        require(run_generator(missing_path, tmp_path / "missing-param.hpp").returncode != 0, "missing parameter should fail")

        bad_enum = copy.deepcopy(profile)
        bad_enum["parameters"]["opening.selectionMode"] = "random"
        bad_enum["canonicalHash"] = profile_hash(bad_enum)
        bad_enum_path = tmp_path / "bad-enum.json"
        write_profile(bad_enum_path, bad_enum)
        require(run_generator(bad_enum_path, tmp_path / "bad-enum.hpp").returncode != 0, "unknown enum should fail")

        candidate = copy.deepcopy(profile)
        candidate["profileId"] = "candidate-eval-0001"
        candidate["parentProfileId"] = "builtin-default-v1"
        candidate["canonicalHash"] = profile_hash(candidate)
        candidate_path = tmp_path / "candidate.json"
        write_profile(candidate_path, candidate)
        candidate_header = tmp_path / "candidate-generated.hpp"
        candidate_result = run_generator(candidate_path, candidate_header)
        require(candidate_result.returncode == 0, candidate_result.stderr)
        require("candidate-eval-0001" in candidate_header.read_text(encoding="utf-8"), "candidate identity should be embedded")

        denied_candidate = run_generator(candidate_path, TRACKED_HEADER)
        require(denied_candidate.returncode != 0, "candidate profile should not overwrite builtin header by default")
        allowed_candidate = run_generator(
            candidate_path,
            tmp_path / "allowed-production-name.hpp",
            "--allow-development-production-header-overwrite",
        )
        require(allowed_candidate.returncode == 0, allowed_candidate.stderr)

        real_check = run_generator(PROFILE, TRACKED_HEADER, "--check")
        require(real_check.returncode == 0, real_check.stderr)

    print("header generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
