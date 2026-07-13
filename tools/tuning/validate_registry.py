#!/usr/bin/env python3
"""Validate the Phase 1 tuning parameter registry.

This validator intentionally checks the inventory contract only. It does not
load tuning profiles and does not alter engine values.
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from pathlib import Path
from typing import Any


VALID_CATEGORIES = {
    "evaluation.scalar",
    "evaluation.array",
    "evaluation.piece_square_table",
    "evaluation.phase",
    "search.pruning",
    "search.reduction",
    "search.extension",
    "search.move_ordering",
    "search.quiescence",
    "search.transposition",
    "time.allocation",
    "time.deadline",
    "time.polling",
    "opening.selection",
    "opening.randomness",
    "runtime.option",
    "rule.invariant",
    "safety.invariant",
    "protocol.invariant",
    "diagnostic",
    "unused",
}

VALID_TYPES = {
    "integer",
    "unsigned integer",
    "fixed-point/scaled integer",
    "enum",
    "boolean",
    "integer array",
}

REQUIRED_PARAMETER_FIELDS = {
    "name",
    "category",
    "type",
    "unit",
    "currentValue",
    "minimum",
    "maximum",
    "step",
    "boundsStatus",
    "requiresRebuild",
    "runtimeMutable",
    "risk",
    "tuningGroup",
    "source",
    "consumer",
    "connectivity",
    "dataRequirements",
    "recommendedMethod",
}


class RegistryError(Exception):
    pass


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_repo_file(relative: str) -> str:
    return (repo_root() / relative).read_text(encoding="utf-8")


def parse_int_assignment(text: str, symbol: str) -> int:
    pattern = re.compile(
        r"(?:inline\s+constexpr\s+|inline\s+|int\s+|size_t\s+|uint64_t\s+|const\s+double\s+)*"
        + re.escape(symbol)
        + r"\s*=\s*([^;]+);"
    )
    match = pattern.search(text)
    if not match:
        raise RegistryError(f"Could not find assignment for {symbol}")
    raw = match.group(1).strip().replace("'", "")
    if raw.endswith("ULL"):
        raw = raw[:-3]
    if raw.endswith("U"):
        raw = raw[:-1]
    if raw == "true":
        return 1
    if raw == "false":
        return 0
    return int(float(raw))


def generated_search_values() -> dict[str, Any]:
    generated = read_repo_file("engine/include/tuning/generated_tuning_values.hpp")
    match = re.search(r"\n    \.search = \{(?P<body>.*?)\n    \},\n    \.time = \{", generated, re.DOTALL)
    if not match:
        raise RegistryError("Could not find generated search tuning block")
    search = match.group("body")

    def scalar(field: str) -> int:
        field_match = re.search(r"\." + re.escape(field) + r"\s*=\s*(-?[0-9']+)", search)
        if not field_match:
            raise RegistryError(f"Could not find generated search field {field}")
        return int(field_match.group(1).replace("'", ""))

    def array(field: str, count: int) -> list[int]:
        field_match = re.search(
            r"\." + re.escape(field) + r"\s*=\s*\{(?P<body>.*?)\}",
            search,
            re.DOTALL,
        )
        if not field_match:
            raise RegistryError(f"Could not find generated search array {field}")
        values = [int(item) for item in re.findall(r"-?\d+", field_match.group("body"))]
        if len(values) != count:
            raise RegistryError(f"Generated search array {field} has {len(values)} values, expected {count}")
        return values

    mvv_lva_match = re.search(
        r"\.mvvLva\s*=\s*\{\{(?P<body>.*?)\}\},\n\s*\.seePieceValues",
        search,
        re.DOTALL,
    )
    if not mvv_lva_match:
        raise RegistryError("Could not find generated search MVV-LVA table")
    mvv_lva = [int(item) for item in re.findall(r"-?\d+", mvv_lva_match.group("body"))]
    if len(mvv_lva) != 36:
        raise RegistryError(f"Generated search MVV-LVA table has {len(mvv_lva)} values, expected 36")

    base_match = re.search(r"\.base\s*=\s*RationalValue<int>\{(-?\d+),\s*(-?\d+)\}", search)
    if not base_match:
        raise RegistryError("Could not find generated LMR rational")
    base_numerator, base_denominator = (int(value) for value in base_match.groups())

    return {
        "search.aspiration.windowCp": scalar("windowCp"),
        "search.nullMove.reduction": scalar("reduction"),
        "search.futility.reverseMarginPerDepthCp": scalar("reverseMarginPerDepthCp"),
        "search.futility.forwardMarginPerDepthCp": scalar("forwardMarginPerDepthCp"),
        "search.lmr.baseScaled100": base_numerator * 100 // base_denominator,
        "search.quiescence.deltaPruningMarginCp": scalar("deltaMarginCp"),
        "search.singular.marginCp": scalar("marginCp"),
        "search.history.cap": scalar("historyLimit"),
        "search.ordering.ttMoveScore": scalar("transpositionMoveScore"),
        "search.ordering.quietScores": [
            scalar("firstKillerScore"),
            scalar("secondKillerScore"),
            scalar("counterMoveScore"),
        ],
        "search.ordering.captureScores": [
            scalar("winningCaptureBaseScore"),
            scalar("losingCaptureBaseScore"),
            scalar("seeScoreMultiplier"),
        ],
        "search.ordering.promotionBase": scalar("promotionBaseScore"),
        "search.see.pieceValues": array("seePieceValues", 6),
    }


def generated_time_values() -> dict[str, Any]:
    generated = read_repo_file("engine/include/tuning/generated_tuning_values.hpp")
    match = re.search(r"\n    \.time = \{(?P<body>.*?)\n    \},\n    \.opening = \{", generated, re.DOTALL)
    if not match:
        raise RegistryError("Could not find generated time tuning block")
    time = match.group("body")

    def scalar(field: str) -> int:
        field_match = re.search(r"\." + re.escape(field) + r"\s*=\s*(-?[0-9']+)", time)
        if not field_match:
            raise RegistryError(f"Could not find generated time field {field}")
        return int(field_match.group(1).replace("'", ""))

    def rational(field: str) -> tuple[int, int]:
        field_match = re.search(
            r"\." + re.escape(field) + r"\s*=\s*RationalValue<int>\{(-?\d+),\s*(-?\d+)\}",
            time,
        )
        if not field_match:
            raise RegistryError(f"Could not find generated time rational {field}")
        return tuple(int(value) for value in field_match.groups())

    instability_numerator, instability_denominator = rational("instabilityMultiplier")
    maximum_numerator, maximum_denominator = rational("maximumClockFraction")
    stable_numerator, stable_denominator = rational("stableSoftStopFraction")
    unstable_numerator, unstable_denominator = rational("unstableSoftStopFraction")
    hard_numerator, hard_denominator = rational("hardStopFraction")

    if maximum_numerator != 1:
        raise RegistryError("Generated maximum clock fraction numerator must remain 1")

    return {
        "time.criticalLowTimeReserveMs": scalar("criticalLowTimeReserveMs"),
        "time.criticalLowTimeThresholdMs": scalar("criticalLowTimeThresholdMs"),
        "time.expectedMovesBase": scalar("expectedMovesBase"),
        "time.expectedMovesFloor": scalar("expectedMovesFloor"),
        "time.hardStopFraction": [hard_numerator, hard_denominator],
        "time.instabilityMultiplierPermille":
            instability_numerator * 1000 // instability_denominator,
        "time.instabilityThresholdCp": scalar("instabilityThresholdCp"),
        "time.maxClockFractionDenominator": maximum_denominator,
        "time.minimumMoveTimeMs": scalar("minimumMoveTimeMs"),
        "time.polling.nodeMask": scalar("nodeMask"),
        "time.safetyReserveMs": scalar("safetyReserveMs"),
        "time.softStop.stablePercent": stable_numerator * 100 // stable_denominator,
        "time.softStop.unstablePercent": unstable_numerator * 100 // unstable_denominator,
    }


def production_values() -> dict[str, Any]:
    search_constants = read_repo_file("engine/include/search/search_constants.hpp")
    app_uci = read_repo_file("engine/src/app/app_uci.cpp")

    values: dict[str, Any] = generated_search_values()
    values.update(generated_time_values())
    values.update({
        "opening.enabled": bool(parse_int_assignment(search_constants, "USE_OPENING_BOOK")),
        "opening.bookDepth": parse_int_assignment(search_constants, "MAX_BOOK_DEPTH"),
    })

    literal_checks = {
        "opening.selectionTopN": r"size_t bookSelectionTopN = (\d+);",
    }
    for name, pattern in literal_checks.items():
        match = re.search(pattern, app_uci)
        if not match:
            raise RegistryError(f"Could not find literal for {name}")
        values[name] = int(match.group(1))

    values["opening.selectionMode"] = "weighted"
    seed = re.search(r"BookSeed type spin default (\d+)", app_uci)
    values["opening.seed"] = int(seed.group(1))

    return values


def cpp_tuning_field_names() -> list[str]:
    metadata = read_repo_file("engine/include/tuning/tuning_metadata.hpp")
    body_match = re.search(r"TUNING_FIELDS\s*=\s*\{\{(?P<body>.*?)\}\};", metadata, re.DOTALL)
    if not body_match:
        raise RegistryError("Could not find TUNING_FIELDS manifest")
    body = body_match.group("body")
    names = re.findall(r'TuningFieldId::[A-Za-z0-9_]+,\s*"([^"]+)"', body)
    if not names:
        raise RegistryError("TUNING_FIELDS manifest contains no stable names")
    return names


def validate_value_type(param: dict[str, Any]) -> None:
    value = param["currentValue"]
    typ = param["type"]
    if typ == "integer":
        if isinstance(value, bool) or not isinstance(value, int):
            raise RegistryError(f"{param['name']} must have integer currentValue")
    elif typ == "unsigned integer":
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise RegistryError(f"{param['name']} must have unsigned integer currentValue")
    elif typ == "fixed-point/scaled integer":
        if isinstance(value, bool) or not isinstance(value, int):
            raise RegistryError(f"{param['name']} must use integer scaled metadata")
    elif typ == "boolean":
        if not isinstance(value, bool):
            raise RegistryError(f"{param['name']} must have boolean currentValue")
    elif typ == "enum":
        if not isinstance(value, str) or value not in param.get("allowedValues", []):
            raise RegistryError(f"{param['name']} enum currentValue is not allowed")
    elif typ == "integer array":
        if isinstance(value, str):
            if not value.startswith("source-table:"):
                raise RegistryError(f"{param['name']} source table marker is malformed")
        elif not (isinstance(value, list) and all(isinstance(v, int) and not isinstance(v, bool) for v in value)):
            raise RegistryError(f"{param['name']} must have integer-array currentValue")
        if "array" not in param:
            raise RegistryError(f"{param['name']} is an array but lacks array metadata")
    else:
        raise RegistryError(f"{param['name']} has unsupported type {typ}")


def validate_bounds(param: dict[str, Any]) -> None:
    if param["boundsStatus"] not in {"verified", "provisional", "unknown"}:
        raise RegistryError(f"{param['name']} has invalid boundsStatus")
    step = param["step"]
    if step is not None and (not isinstance(step, int) or step <= 0):
        raise RegistryError(f"{param['name']} step must be positive")
    if param["boundsStatus"] == "unknown":
        return
    value = param["currentValue"]
    minimum = param["minimum"]
    maximum = param["maximum"]
    if isinstance(value, list):
        if not (isinstance(minimum, list) and isinstance(maximum, list)):
            raise RegistryError(f"{param['name']} array bounds must be arrays")
        if not (len(value) == len(minimum) == len(maximum)):
            raise RegistryError(f"{param['name']} array bounds length mismatch")
        for idx, (v, lo, hi) in enumerate(zip(value, minimum, maximum)):
            if lo > hi or not (lo <= v <= hi):
                raise RegistryError(f"{param['name']}[{idx}]={v} outside [{lo},{hi}]")
    elif isinstance(value, int) and not isinstance(value, bool):
        if not (isinstance(minimum, int) and isinstance(maximum, int)):
            raise RegistryError(f"{param['name']} scalar bounds must be integers")
        if minimum > maximum or not (minimum <= value <= maximum):
            raise RegistryError(f"{param['name']}={value} outside [{minimum},{maximum}]")


def validate_registry(
    registry: dict[str, Any],
    *,
    check_production: bool = True,
    check_cpp_mapping: bool = True,
) -> dict[str, int]:
    if registry.get("schemaVersion") != 1:
        raise RegistryError("schemaVersion must be 1")
    if registry.get("engineBaseline") != "builtin-default-v1":
        raise RegistryError("engineBaseline must be builtin-default-v1")
    params = registry.get("parameters")
    if not isinstance(params, list) or not params:
        raise RegistryError("parameters must be a non-empty list")

    names = [p.get("name") for p in params]
    if len(names) != len(set(names)):
        raise RegistryError("duplicate parameter names found")
    if names != sorted(names):
        raise RegistryError("parameters must be sorted by name for deterministic ordering")

    excluded_names = {g["name"] for g in registry.get("excludedGroups", []) if "name" in g}
    if "eval.tempo" not in excluded_names:
        raise RegistryError("eval.tempo must be excluded while unconsumed")

    placeholder_names = {
        item["name"]
        for item in registry.get("classification", {}).get("placeholderParameters", [])
        if "name" in item
    }

    for param in params:
        missing = REQUIRED_PARAMETER_FIELDS - set(param)
        if missing:
            raise RegistryError(f"{param.get('name', '<unnamed>')} missing fields: {sorted(missing)}")
        if param["name"] in placeholder_names:
            raise RegistryError(f"{param['name']} is listed as placeholder and tunable")
        if param["name"] in excluded_names:
            raise RegistryError(f"{param['name']} appears both tunable and excluded")
        if param["category"] not in VALID_CATEGORIES:
            raise RegistryError(f"{param['name']} invalid category {param['category']}")
        if param["type"] not in VALID_TYPES:
            raise RegistryError(f"{param['name']} invalid type {param['type']}")
        if not param["consumer"].get("file") or not param["consumer"].get("function"):
            raise RegistryError(f"{param['name']} lacks consumer file/function")
        if not param["source"].get("file"):
            raise RegistryError(f"{param['name']} lacks source file")
        validate_value_type(param)
        validate_bounds(param)
        if param["type"] == "integer array":
            dims = param.get("array", {}).get("dimensions")
            if not isinstance(dims, list) or not all(isinstance(v, int) and v > 0 for v in dims):
                raise RegistryError(f"{param['name']} array dimensions are required")
            value = param["currentValue"]
            if isinstance(value, list):
                expected = 1
                for dim in dims:
                    expected *= dim
                if len(value) != expected:
                    raise RegistryError(f"{param['name']} value length does not match dimensions")
        if param["category"].endswith("invariant"):
            raise RegistryError(f"{param['name']} cannot be tunable invariant category")

    if check_production:
        actual = production_values()
        for param in params:
            name = param["name"]
            if name in actual and actual[name] != param["currentValue"]:
                raise RegistryError(
                    f"{name} currentValue {param['currentValue']!r} does not match production {actual[name]!r}"
                )

    mismatches = registry.get("oldRegistryComparison", [])
    if not mismatches:
        raise RegistryError("old registry mismatches must be recorded")
    mismatch_statuses = {item.get("status") for item in mismatches}
    if "outdated default" not in mismatch_statuses or "missing from old registry" not in mismatch_statuses:
        raise RegistryError("old registry comparison must detect outdated and missing entries")

    pst = [p for p in params if p["category"] == "evaluation.piece_square_table"]
    if len(pst) != 2:
        raise RegistryError("PST inventory must contain exactly MG and EG table parameters")
    for param in pst:
        if param["array"]["dimensions"] != [6, 64]:
            raise RegistryError(f"{param['name']} has wrong PST dimensions")

    mapping_stats = {
        "registry_entries": len(names),
        "mapped_typed_fields": 0,
        "missing_mappings": 0,
        "duplicate_mappings": 0,
        "unknown_mappings": 0,
    }

    if check_cpp_mapping:
        cpp_names = cpp_tuning_field_names()
        mapping_stats["mapped_typed_fields"] = len(cpp_names)
        mapping_stats["duplicate_mappings"] = len(cpp_names) - len(set(cpp_names))
        missing = sorted(set(names) - set(cpp_names))
        unknown = sorted(set(cpp_names) - set(names))
        mapping_stats["missing_mappings"] = len(missing)
        mapping_stats["unknown_mappings"] = len(unknown)
        if mapping_stats["duplicate_mappings"]:
            raise RegistryError("duplicate typed C++ mappings found")
        if missing:
            raise RegistryError(f"missing typed C++ mappings: {missing}")
        if unknown:
            raise RegistryError(f"unknown typed C++ mappings: {unknown}")
        if cpp_names != names:
            raise RegistryError("typed C++ field ordering does not match registry ordering")

    return mapping_stats


def expect_failure(name: str, registry: dict[str, Any], mutator) -> None:
    fixture = copy.deepcopy(registry)
    mutator(fixture)
    try:
        validate_registry(fixture, check_production=False)
    except RegistryError:
        return
    raise RegistryError(f"malformed fixture unexpectedly passed: {name}")


def run_malformed_fixture_tests(registry: dict[str, Any]) -> None:
    def first_array(r: dict[str, Any]) -> dict[str, Any]:
        return next(p for p in r["parameters"] if p["type"] == "integer array")

    expect_failure("duplicate names", registry, lambda r: r["parameters"].append(copy.deepcopy(r["parameters"][0])))
    expect_failure("missing consumer", registry, lambda r: r["parameters"][0]["consumer"].clear())
    expect_failure("placeholder accepted", registry, lambda r: r["parameters"].append({**copy.deepcopy(r["parameters"][0]), "name": "eval.tempo"}))
    expect_failure("bounds exclude current", registry, lambda r: r["parameters"][0].update({"minimum": [999] * 6}))
    expect_failure("integer uses float", registry, lambda r: r["parameters"][0].update({"type": "integer", "currentValue": 1.5, "minimum": 0, "maximum": 2}))
    expect_failure("array missing dimensions", registry, lambda r: first_array(r).pop("array", None))
    expect_failure("invariant tunable", registry, lambda r: r["parameters"][0].update({"category": "rule.invariant"}))
    expect_failure("nondeterministic order", registry, lambda r: r["parameters"].reverse())
    expect_failure("bad opening enum", registry, lambda r: next(p for p in r["parameters"] if p["name"] == "opening.selectionMode").update({"currentValue": "random"}))
    expect_failure("time watchdog conflation", registry, lambda r: r["parameters"].append({**copy.deepcopy(r["parameters"][0]), "name": "time.watchdogSafetyCeiling", "category": "safety.invariant"}))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("registry", type=Path)
    args = parser.parse_args()

    try:
        registry = load_json(args.registry)
        mapping_stats = validate_registry(registry)
        run_malformed_fixture_tests(registry)
    except (OSError, json.JSONDecodeError, RegistryError, AttributeError) as exc:
        print(f"registry validation failed: {exc}", file=sys.stderr)
        return 1

    print(f"registry validation passed: {len(registry['parameters'])} parameters")
    print(
        "registry entries: {registry_entries}, mapped typed fields/groups: {mapped_typed_fields}, "
        "missing mappings: {missing_mappings}, duplicate mappings: {duplicate_mappings}, "
        "unknown typed mappings: {unknown_mappings}".format(**mapping_stats)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
