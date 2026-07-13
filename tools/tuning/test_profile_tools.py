#!/usr/bin/env python3
"""Focused tests for Phase 3 local tuning profile tooling."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from canonical_json import profile_hash
from profile_schema import expected_profile_parameters, load_registry
from validate_profile import ProfileValidationError, validate_profile


ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "tuning/parameter-registry.json"
BUILD = ROOT / "tools/tuning/build_default_profile.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_build(output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(BUILD),
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


def assert_invalid(profile: dict, message: str) -> None:
    try:
        validate_profile(profile, REGISTRY)
    except ProfileValidationError:
        return
    raise AssertionError(message)


def main() -> int:
    registry = load_registry(REGISTRY)
    expected = expected_profile_parameters(registry)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        profile_path = tmp_path / "builtin-default-v1.json"

        result = run_build(profile_path)
        require(result.returncode == 0, result.stderr)
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
        stats = validate_profile(profile, REGISTRY)

        require(stats["profile_entries"] == 76, "profile should contain exactly 76 entries")
        require(set(profile["parameters"]) == set(expected), "profile parameter set should match registry")
        require("eval.tempo" not in profile["parameters"], "eval.tempo must be absent")
        require(profile["parameters"]["evaluation.pst.mgPesto"] == expected["evaluation.pst.mgPesto"], "MG PST must match production")
        require(profile["parameters"]["evaluation.pst.egPesto"] == expected["evaluation.pst.egPesto"], "EG PST must match production")
        require(len(profile["parameters"]["evaluation.pst.mgPesto"]) == 6, "MG PST must have 6 rows")
        require(len(profile["parameters"]["evaluation.pst.mgPesto"][0]) == 64, "MG PST rows must have 64 entries")
        require(profile["parameters"]["search.lmr.baseScaled100"] == {"denominator": 4, "numerator": 3}, "LMR rational mismatch")
        require(profile["parameters"]["opening.selectionMode"] == "weighted", "opening enum should serialize as stable string")

        original_hash = profile["canonicalHash"]
        require(profile_hash(profile) == original_hash, "stored hash should validate")

        reordered = copy.deepcopy(profile)
        reordered["parameters"] = dict(reversed(list(reordered["parameters"].items())))
        require(profile_hash(reordered) == original_hash, "object key reordering should not change hash")

        pretty_reloaded = json.loads(json.dumps(profile, indent=4))
        require(profile_hash(pretty_reloaded) == original_hash, "pretty printing should not change hash")

        changed = copy.deepcopy(profile)
        changed["parameters"]["evaluation.bishopPair.mg"] += 1
        require(profile_hash(changed) != original_hash, "parameter changes must change hash")
        assert_invalid(changed, "stale hash should be rejected after parameter edit")

        array_reordered = copy.deepcopy(profile)
        values = array_reordered["parameters"]["evaluation.material.mg"]
        values[0], values[1] = values[1], values[0]
        require(profile_hash(array_reordered) != original_hash, "array reordering must change hash")

        missing = copy.deepcopy(profile)
        missing["parameters"].pop("evaluation.bishopPair.mg")
        missing["canonicalHash"] = profile_hash(missing)
        assert_invalid(missing, "missing parameter should be rejected")

        extra = copy.deepcopy(profile)
        extra["parameters"]["eval.tempo"] = 0
        extra["canonicalHash"] = profile_hash(extra)
        assert_invalid(extra, "eval.tempo should be rejected")

        bad_enum = copy.deepcopy(profile)
        bad_enum["parameters"]["opening.selectionMode"] = "random"
        bad_enum["canonicalHash"] = profile_hash(bad_enum)
        assert_invalid(bad_enum, "unknown enum should be rejected")

        zero_denominator = copy.deepcopy(profile)
        zero_denominator["parameters"]["time.hardStopFraction"] = {"denominator": 0, "numerator": 3}
        zero_denominator["canonicalHash"] = profile_hash(zero_denominator)
        assert_invalid(zero_denominator, "zero denominator should be rejected")

        non_reduced = copy.deepcopy(profile)
        non_reduced["parameters"]["search.lmr.baseScaled100"] = {"denominator": 8, "numerator": 6}
        non_reduced["canonicalHash"] = profile_hash(non_reduced)
        assert_invalid(non_reduced, "non-reduced rational should be rejected")

        before = profile_path.read_text(encoding="utf-8")
        second = run_build(profile_path)
        require(second.returncode == 0, second.stderr)
        require(profile_path.read_text(encoding="utf-8") == before, "identical immutable rebuild should not rewrite content")

        conflict = copy.deepcopy(profile)
        conflict["description"] = "Conflicting profile."
        conflict["canonicalHash"] = profile_hash(conflict)
        profile_path.write_text(json.dumps(conflict, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        denied = run_build(profile_path)
        require(denied.returncode != 0, "conflicting immutable rebuild should fail without development flag")
        allowed = run_build(profile_path, "--force-development-rebuild")
        require(allowed.returncode == 0, allowed.stderr)
        validate_profile(json.loads(profile_path.read_text(encoding="utf-8")), REGISTRY)

    require(expected["evaluation.material.mg"][0] == 150, "production MG pawn default expected")
    require(expected["evaluation.material.mg"][0] != 82, "old Texel MG pawn default must not be used")

    print("profile tool tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
