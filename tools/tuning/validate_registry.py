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


def parse_int_array_initializer(text: str, symbol: str) -> list[int]:
    pattern = re.compile(
        r"(?:std::array<[^=]+>\s+)?" + re.escape(symbol) + r"\s*=\s*\{(?P<body>.*?)\};",
        re.DOTALL,
    )
    match = pattern.search(text)
    if not match:
        raise RegistryError(f"Could not find array initializer for {symbol}")
    return [int(item) for item in re.findall(r"-?\d+", match.group("body"))]


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


def production_values() -> dict[str, Any]:
    eval_weights = read_repo_file("engine/src/eval/eval_weights.cpp")
    search_constants = read_repo_file("engine/include/search/search_constants.hpp")
    app_uci = read_repo_file("engine/src/app/app_uci.cpp")
    search_state = read_repo_file("engine/src/search/search_state.cpp")
    search_negamax = read_repo_file("engine/src/search/search_negamax.cpp")
    search_ordering = read_repo_file("engine/src/search/search_move_ordering.cpp")
    search_root = read_repo_file("engine/src/search/search_root.cpp")

    values: dict[str, Any] = {
        "evaluation.material.mg": parse_int_array_initializer(eval_weights, "MG_VALUE"),
        "evaluation.material.eg": parse_int_array_initializer(eval_weights, "EG_VALUE"),
        "evaluation.phase.increments": parse_int_array_initializer(eval_weights, "GAME_PHASE_INC"),
        "evaluation.mobility.mg": parse_int_array_initializer(eval_weights, "MOBILITY_BONUS_MG"),
        "evaluation.mobility.eg": parse_int_array_initializer(eval_weights, "MOBILITY_BONUS_EG"),
        "evaluation.rookActivity.mg": parse_int_array_initializer(eval_weights, "ROOK_ACTIVITY_BONUS_MG"),
        "evaluation.rookActivity.eg": parse_int_array_initializer(eval_weights, "ROOK_ACTIVITY_BONUS_EG"),
        "evaluation.pawns.connectedByRank.mg": parse_int_array_initializer(eval_weights, "CONNECTED_PAWN_BONUS_MG_BY_RANK"),
        "evaluation.pawns.connectedByRank.eg": parse_int_array_initializer(eval_weights, "CONNECTED_PAWN_BONUS_EG_BY_RANK"),
        "evaluation.pawns.candidateByRank.mg": parse_int_array_initializer(eval_weights, "CANDIDATE_PAWN_BONUS_MG_BY_RANK"),
        "evaluation.pawns.candidateByRank.eg": parse_int_array_initializer(eval_weights, "CANDIDATE_PAWN_BONUS_EG_BY_RANK"),
        "evaluation.pawns.backwardByRank.mg": parse_int_array_initializer(eval_weights, "BACKWARD_PAWN_PENALTY_MG_BY_RANK"),
        "evaluation.pawns.backwardByRank.eg": parse_int_array_initializer(eval_weights, "BACKWARD_PAWN_PENALTY_EG_BY_RANK"),
        "evaluation.king.attackPressure": parse_int_array_initializer(eval_weights, "KING_ATTACK_PRESSURE_PENALTY"),
        "evaluation.pawns.islandPenalty.mg": parse_int_assignment(eval_weights, "PAWN_ISLAND_PENALTY_MG"),
        "evaluation.pawns.islandPenalty.eg": parse_int_assignment(eval_weights, "PAWN_ISLAND_PENALTY_EG"),
        "evaluation.bishopPair.mg": parse_int_assignment(eval_weights, "BISHOP_PAIR_BONUS_MG"),
        "evaluation.bishopPair.eg": parse_int_assignment(eval_weights, "BISHOP_PAIR_BONUS_EG"),
        "evaluation.king.shieldMaxPawns": parse_int_assignment(eval_weights, "KING_SHIELD_MAX_PAWNS"),
        "evaluation.king.shieldPerPawnBonus": parse_int_assignment(eval_weights, "KING_SHIELD_PER_PAWN_BONUS"),
        "evaluation.pawns.doubledPenalty": parse_int_assignment(eval_weights, "PAWN_STRUCTURE_DOUBLED_PENALTY"),
        "evaluation.pawns.isolatedPenalty": parse_int_assignment(eval_weights, "PAWN_STRUCTURE_ISOLATED_PENALTY"),
        "evaluation.pawns.passedCountBonus.mg": parse_int_assignment(eval_weights, "PASSED_PAWN_COUNT_BONUS_MG"),
        "evaluation.pawns.passedCountBonus.eg": parse_int_assignment(eval_weights, "PASSED_PAWN_COUNT_BONUS_EG"),
        "evaluation.pawns.passedEgMultiplier": parse_int_assignment(eval_weights, "PASSED_PAWN_EG_MULTIPLIER"),
        "evaluation.pawns.passedRankSquareMultiplier": parse_int_assignment(eval_weights, "PASSED_PAWN_RANK_SQUARE_MULTIPLIER"),
        "evaluation.pawns.passedBlockedDivisor": parse_int_assignment(eval_weights, "PASSED_PAWN_BLOCKED_DIVISOR"),
        "evaluation.rook.trappedPenalty": parse_int_assignment(eval_weights, "TRAPPED_ROOK_PENALTY"),
        "evaluation.bishop.badHeavyPenalty": parse_int_assignment(eval_weights, "BAD_BISHOP_HEAVY_PENALTY"),
        "evaluation.bishop.badLightPenalty": parse_int_assignment(eval_weights, "BAD_BISHOP_LIGHT_PENALTY"),
        "evaluation.queen.earlyUndevelopedMinorPenalty": parse_int_assignment(eval_weights, "EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY"),
        "evaluation.king.uncastledCenterPenalty": parse_int_assignment(eval_weights, "UNCASTLED_KING_CENTER_PENALTY"),
        "evaluation.king.uncastledLostRightsPenalty": parse_int_assignment(eval_weights, "UNCASTLED_KING_LOST_RIGHTS_PENALTY"),
        "evaluation.taper.scale": parse_int_assignment(eval_weights, "TAPER_SCALE"),
        "evaluation.endgame.latePhaseMax": parse_int_assignment(eval_weights, "LATE_ENDGAME_PHASE_MAX"),
        "evaluation.endgame.mopUpEgMargin": parse_int_assignment(eval_weights, "MOP_UP_EG_MARGIN"),
        "evaluation.endgame.mopUpMaterialMargin": parse_int_assignment(eval_weights, "MOP_UP_MATERIAL_MARGIN"),
        "evaluation.endgame.scaleOppositeBishopsMinPawns": parse_int_assignment(eval_weights, "SCALE_OPPOSITE_BISHOPS_MIN_PAWNS"),
        "evaluation.endgame.scaleOppositeBishopsLowPawns": parse_int_assignment(eval_weights, "SCALE_OPPOSITE_BISHOPS_LOW_PAWNS"),
        "evaluation.endgame.scaleMinorOnlyNearEqual": parse_int_assignment(eval_weights, "SCALE_MINOR_ONLY_NEAR_EQUAL"),
        "evaluation.endgame.scaleMinorOnlyClearEdge": parse_int_assignment(eval_weights, "SCALE_MINOR_ONLY_CLEAR_EDGE"),
        "evaluation.endgame.mopUpWeights": [
            parse_int_assignment(eval_weights, "MOP_UP_CENTER_DISTANCE_WEIGHT"),
            parse_int_assignment(eval_weights, "MOP_UP_EDGE_DISTANCE_BASE"),
            parse_int_assignment(eval_weights, "MOP_UP_EDGE_PRESSURE_WEIGHT"),
            parse_int_assignment(eval_weights, "MOP_UP_CORNER_DISTANCE_CAP"),
            parse_int_assignment(eval_weights, "MOP_UP_CORNER_PRESSURE_WEIGHT"),
            parse_int_assignment(eval_weights, "MOP_UP_KING_DISTANCE_BASE"),
            parse_int_assignment(eval_weights, "MOP_UP_KING_DISTANCE_WEIGHT"),
        ],
        "search.aspiration.windowCp": parse_int_assignment(search_constants, "ASPIRATION_WINDOW_SIZE"),
        "search.nullMove.reduction": parse_int_assignment(search_constants, "NULL_MOVE_REDUCTION"),
        "search.futility.reverseMarginPerDepthCp": parse_int_assignment(search_constants, "REVERSE_FUTILITY_MARGIN"),
        "search.futility.forwardMarginPerDepthCp": parse_int_assignment(search_constants, "FORWARD_FUTILITY_MARGIN"),
        "search.quiescence.deltaPruningMarginCp": parse_int_assignment(search_constants, "DELTA_PRUNING_MARGIN"),
        "search.ordering.ttMoveScore": parse_int_assignment(search_constants, "TT_MOVE_SCORE"),
        "search.see.pieceValues": parse_int_array_initializer(search_constants, "PIECE_VALUES"),
        "time.polling.nodeMask": parse_int_assignment(search_constants, "TIME_CHECK_MASK"),
        "opening.enabled": bool(parse_int_assignment(search_constants, "USE_OPENING_BOOK")),
        "opening.bookDepth": parse_int_assignment(search_constants, "MAX_BOOK_DEPTH"),
        "search.lmr.baseScaled100": int(float(re.search(r"const double base = ([0-9.]+);", search_state).group(1)) * 100),
        "search.singular.marginCp": int(re.search(r"const int margin\s+=\s+(\d+); // one pawn", search_negamax).group(1)),
        "search.history.cap": int(re.search(r"std::min\((\d+), hist \+ bonus\)", search_negamax).group(1)),
        "search.ordering.quietScores": [
            int(re.search(r"score \+= (\d+);\n\s+} else if", search_ordering).group(1)),
            int(re.search(r"score \+= (\d+);\n\s+}\n\n\s+bool isCounterMove", search_ordering).group(1)),
            int(re.search(r"score \+= (\d+);\n\s+}\n\s+\n\s+score \+= SearchInternal::g_historyTable", search_ordering).group(1)),
        ],
        "search.ordering.captureScores": [
            int(re.search(r"score \+= (\d+) \+ seeVal \* 10 \+ mvvLva;", search_ordering).group(1)),
            int(re.search(r"score \+= (-\d+) \+ seeVal \* 10 \+ mvvLva;", search_ordering).group(1)),
            10,
        ],
        "search.ordering.promotionBase": int(re.search(r"score \+= (\d+) \+ getPieceValue", search_ordering).group(1)),
        "time.softStop.stablePercent": int(re.search(r"int softLimitFraction = (\d+);", search_root).group(1)),
        "time.softStop.unstablePercent": int(re.search(r"stableCount = 0;\n\s+softLimitFraction = (\d+);", search_root).group(1)),
        "time.hardStopFraction": [3, 4],
    }

    literal_checks = {
        "time.safetyReserveMs": r"timeLeft - (\d+)LL",
        "time.minimumMoveTimeMs": r"std::max\((\d+)LL, static_cast<long long>\(\*goCommand\.moveTimeMs\)\)",
        "time.expectedMovesFloor": r"std::max\((\d+), 40 - move_number\)",
        "time.expectedMovesBase": r"std::max\(20, (\d+) - move_number\)",
        "time.instabilityThresholdCp": r"g_prevSearchScore\.load\(std::memory_order_relaxed\)\) > (\d+)\)",
        "time.maxClockFractionDenominator": r"safeTimeLeft / (\d+)\)",
        "time.criticalLowTimeThresholdMs": r"safeTimeLeft < (\d+)",
        "time.criticalLowTimeReserveMs": r"safeTimeLeft - (\d+)LL",
        "opening.selectionTopN": r"size_t bookSelectionTopN = (\d+);",
    }
    for name, pattern in literal_checks.items():
        match = re.search(pattern, app_uci)
        if not match:
            raise RegistryError(f"Could not find literal for {name}")
        values[name] = int(match.group(1))

    instability = re.search(r"allocated_time \* ([0-9.]+)\)", app_uci)
    values["time.instabilityMultiplierPermille"] = int(float(instability.group(1)) * 1000)
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
