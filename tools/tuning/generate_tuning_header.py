#!/usr/bin/env python3
"""Generate the tracked values-only C++ tuning header from a validated profile."""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Any

from profile_schema import PROFILE_ID, PROFILE_SCHEMA_VERSION, ProfileError, load_registry
from validate_profile import ProfileValidationError, load_profile, validate_profile


DEFAULT_OUTPUT = Path("engine/include/tuning/generated_tuning_values.hpp")
HEADER_GUARD_NOTE = "Generated from the canonical tuning profile."
RATIONAL_FIELDS = {
    "search.lmr.baseScaled100",
    "time.hardStopFraction",
    "time.instabilityMultiplierPermille",
    "time.maxClockFractionDenominator",
    "time.softStop.stablePercent",
    "time.softStop.unstablePercent",
}

OPENING_ENUM_TO_CPP = {
    "weighted": "BookSelectionMode::Weighted",
    "best": "BookSelectionMode::Best",
    "top-n-weighted": "BookSelectionMode::TopNWeighted",
}


class HeaderGenerationError(Exception):
    pass


def _cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def _cpp_int(value: int) -> str:
    return str(value)


def _cpp_uint(value: int, suffix: str = "U") -> str:
    if value < 0:
        raise HeaderGenerationError(f"unsigned value cannot be negative: {value}")
    return f"{value}{suffix}"


def _cpp_rational(value: dict[str, int]) -> str:
    return f"RationalValue<int>{{{value['numerator']}, {value['denominator']}}}"


def _array_initializer(values: list[Any], indent: int, nested: bool = False) -> list[str]:
    spaces = " " * indent
    lines: list[str] = []
    if values and isinstance(values[0], list):
        lines.append(spaces + "{{")
        for row in values:
            row_text = ", ".join(_cpp_int(item) for item in row)
            lines.append(" " * (indent + 4) + f"{{{{{row_text}}}}},")
        lines.append(spaces + "}}")
    else:
        text = ", ".join(_cpp_int(item) for item in values)
        if nested:
            lines.append(spaces + f"{{{{{text}}}}}")
        else:
            lines.append(spaces + f"{{{text}}}")
    return lines


def _field(lines: list[str], indent: int, name: str, value: str) -> None:
    lines.append(" " * indent + f".{name} = {value},")


def _field_array(lines: list[str], indent: int, name: str, values: list[Any], nested: bool = False) -> None:
    rendered = _array_initializer(values, indent + 4, nested=nested)
    lines.append(" " * indent + f".{name} = {rendered[0].lstrip()}")
    lines.extend(rendered[1:])
    lines[-1] += ","


def _emit_engine_tuning(parameters: dict[str, Any]) -> list[str]:
    p = parameters
    lines: list[str] = []
    lines.append("inline constexpr EngineTuning VALUES = {")
    lines.append("    .evaluation = {")
    lines.append("        .material = {")
    _field_array(lines, 12, "middlegame", p["evaluation.material.mg"])
    _field_array(lines, 12, "endgame", p["evaluation.material.eg"])
    lines.append("        },")
    lines.append("        .phase = {")
    _field_array(lines, 12, "increments", p["evaluation.phase.increments"])
    lines.append("        },")
    lines.append("        .mobility = {")
    _field_array(lines, 12, "middlegame", p["evaluation.mobility.mg"])
    _field_array(lines, 12, "endgame", p["evaluation.mobility.eg"])
    lines.append("        },")
    lines.append("        .rookActivity = {")
    _field(lines, 12, "openFileMg", _cpp_int(p["evaluation.rookActivity.mg"][0]))
    _field(lines, 12, "openFileEg", _cpp_int(p["evaluation.rookActivity.eg"][0]))
    _field(lines, 12, "semiOpenFileMg", _cpp_int(p["evaluation.rookActivity.mg"][1]))
    _field(lines, 12, "semiOpenFileEg", _cpp_int(p["evaluation.rookActivity.eg"][1]))
    _field(lines, 12, "seventhRankMg", _cpp_int(p["evaluation.rookActivity.mg"][2]))
    _field(lines, 12, "seventhRankEg", _cpp_int(p["evaluation.rookActivity.eg"][2]))
    lines.append("        },")
    lines.append("        .bishopPair = {")
    _field(lines, 12, "middlegame", _cpp_int(p["evaluation.bishopPair.mg"]))
    _field(lines, 12, "endgame", _cpp_int(p["evaluation.bishopPair.eg"]))
    lines.append("        },")
    lines.append("        .pawns = {")
    _field_array(lines, 12, "connectedMgByRank", p["evaluation.pawns.connectedByRank.mg"])
    _field_array(lines, 12, "connectedEgByRank", p["evaluation.pawns.connectedByRank.eg"])
    _field_array(lines, 12, "candidateMgByRank", p["evaluation.pawns.candidateByRank.mg"])
    _field_array(lines, 12, "candidateEgByRank", p["evaluation.pawns.candidateByRank.eg"])
    _field_array(lines, 12, "backwardMgByRank", p["evaluation.pawns.backwardByRank.mg"])
    _field_array(lines, 12, "backwardEgByRank", p["evaluation.pawns.backwardByRank.eg"])
    _field(lines, 12, "doubledPenalty", _cpp_int(p["evaluation.pawns.doubledPenalty"]))
    _field(lines, 12, "isolatedPenalty", _cpp_int(p["evaluation.pawns.isolatedPenalty"]))
    _field(lines, 12, "islandPenaltyMg", _cpp_int(p["evaluation.pawns.islandPenalty.mg"]))
    _field(lines, 12, "islandPenaltyEg", _cpp_int(p["evaluation.pawns.islandPenalty.eg"]))
    _field(lines, 12, "passedCountBonusMg", _cpp_int(p["evaluation.pawns.passedCountBonus.mg"]))
    _field(lines, 12, "passedCountBonusEg", _cpp_int(p["evaluation.pawns.passedCountBonus.eg"]))
    _field(lines, 12, "passedEgMultiplier", _cpp_int(p["evaluation.pawns.passedEgMultiplier"]))
    _field(lines, 12, "passedRankSquareMultiplier", _cpp_int(p["evaluation.pawns.passedRankSquareMultiplier"]))
    _field(lines, 12, "passedBlockedDivisor", _cpp_int(p["evaluation.pawns.passedBlockedDivisor"]))
    lines.append("        },")
    lines.append("        .kingSafety = {")
    _field_array(lines, 12, "attackPressure", p["evaluation.king.attackPressure"])
    _field(lines, 12, "shieldMaxPawns", _cpp_int(p["evaluation.king.shieldMaxPawns"]))
    _field(lines, 12, "shieldPerPawnBonus", _cpp_int(p["evaluation.king.shieldPerPawnBonus"]))
    _field(lines, 12, "uncastledCenterPenalty", _cpp_int(p["evaluation.king.uncastledCenterPenalty"]))
    _field(lines, 12, "uncastledLostRightsPenalty", _cpp_int(p["evaluation.king.uncastledLostRightsPenalty"]))
    lines.append("        },")
    lines.append("        .piecePlacement = {")
    _field(lines, 12, "badBishopHeavyPenalty", _cpp_int(p["evaluation.bishop.badHeavyPenalty"]))
    _field(lines, 12, "badBishopLightPenalty", _cpp_int(p["evaluation.bishop.badLightPenalty"]))
    _field(lines, 12, "earlyQueenUndevelopedMinorPenalty", _cpp_int(p["evaluation.queen.earlyUndevelopedMinorPenalty"]))
    _field(lines, 12, "trappedRookPenalty", _cpp_int(p["evaluation.rook.trappedPenalty"]))
    lines.append("        },")
    lines.append("        .endgame = {")
    _field(lines, 12, "taperScale", _cpp_int(p["evaluation.taper.scale"]))
    _field(lines, 12, "latePhaseMax", _cpp_int(p["evaluation.endgame.latePhaseMax"]))
    _field(lines, 12, "mopUpEgMargin", _cpp_int(p["evaluation.endgame.mopUpEgMargin"]))
    _field(lines, 12, "mopUpMaterialMargin", _cpp_int(p["evaluation.endgame.mopUpMaterialMargin"]))
    _field(lines, 12, "scaleOppositeBishopsMinPawns", _cpp_int(p["evaluation.endgame.scaleOppositeBishopsMinPawns"]))
    _field(lines, 12, "scaleOppositeBishopsLowPawns", _cpp_int(p["evaluation.endgame.scaleOppositeBishopsLowPawns"]))
    _field(lines, 12, "scaleMinorOnlyNearEqual", _cpp_int(p["evaluation.endgame.scaleMinorOnlyNearEqual"]))
    _field(lines, 12, "scaleMinorOnlyClearEdge", _cpp_int(p["evaluation.endgame.scaleMinorOnlyClearEdge"]))
    _field_array(lines, 12, "mopUpWeights", p["evaluation.endgame.mopUpWeights"])
    lines.append("        },")
    lines.append("        .pieceSquare = {")
    _field(lines, 12, "middlegameRepresented", "true")
    _field(lines, 12, "endgameRepresented", "true")
    lines.append("        },")
    lines.append("    },")
    lines.append("    .search = {")
    lines.append("        .aspiration = {")
    _field(lines, 12, "windowCp", _cpp_int(p["search.aspiration.windowCp"]))
    lines.append("        },")
    lines.append("        .nullMove = {")
    _field(lines, 12, "reduction", _cpp_int(p["search.nullMove.reduction"]))
    lines.append("        },")
    lines.append("        .futility = {")
    _field(lines, 12, "reverseMarginPerDepthCp", _cpp_int(p["search.futility.reverseMarginPerDepthCp"]))
    _field(lines, 12, "forwardMarginPerDepthCp", _cpp_int(p["search.futility.forwardMarginPerDepthCp"]))
    lines.append("        },")
    lines.append("        .lateMoveReduction = {")
    _field(lines, 12, "base", _cpp_rational(p["search.lmr.baseScaled100"]))
    lines.append("        },")
    lines.append("        .quiescence = {")
    _field(lines, 12, "deltaMarginCp", _cpp_int(p["search.quiescence.deltaPruningMarginCp"]))
    lines.append("        },")
    lines.append("        .singularExtension = {")
    _field(lines, 12, "marginCp", _cpp_int(p["search.singular.marginCp"]))
    lines.append("        },")
    lines.append("        .moveOrdering = {")
    _field(lines, 12, "transpositionMoveScore", _cpp_int(p["search.ordering.ttMoveScore"]))
    lines.append("            .quiet = {")
    _field(lines, 16, "firstKillerScore", _cpp_int(p["search.ordering.quietScores"][0]))
    _field(lines, 16, "secondKillerScore", _cpp_int(p["search.ordering.quietScores"][1]))
    _field(lines, 16, "counterMoveScore", _cpp_int(p["search.ordering.quietScores"][2]))
    lines.append("            },")
    lines.append("            .capture = {")
    _field(lines, 16, "winningCaptureBaseScore", _cpp_int(p["search.ordering.captureScores"][0]))
    _field(lines, 16, "losingCaptureBaseScore", _cpp_int(p["search.ordering.captureScores"][1]))
    _field(lines, 16, "seeScoreMultiplier", _cpp_int(p["search.ordering.captureScores"][2]))
    lines.append("            },")
    _field(lines, 12, "promotionBaseScore", _cpp_int(p["search.ordering.promotionBase"]))
    _field(lines, 12, "historyLimit", _cpp_int(p["search.history.cap"]))
    _field_array(lines, 12, "mvvLva", p["search.ordering.mvvLva"], nested=True)
    _field_array(lines, 12, "seePieceValues", p["search.see.pieceValues"])
    lines.append("        },")
    lines.append("    },")
    lines.append("    .time = {")
    lines.append("        .allocation = {")
    _field(lines, 12, "safetyReserveMs", _cpp_int(p["time.safetyReserveMs"]))
    _field(lines, 12, "minimumMoveTimeMs", _cpp_int(p["time.minimumMoveTimeMs"]))
    _field(lines, 12, "expectedMovesBase", _cpp_int(p["time.expectedMovesBase"]))
    _field(lines, 12, "expectedMovesFloor", _cpp_int(p["time.expectedMovesFloor"]))
    _field(lines, 12, "incrementContribution", "RationalValue<int>{1, 1}")
    _field(lines, 12, "instabilityThresholdCp", _cpp_int(p["time.instabilityThresholdCp"]))
    _field(lines, 12, "instabilityMultiplier", _cpp_rational(p["time.instabilityMultiplierPermille"]))
    _field(lines, 12, "maximumClockFraction", _cpp_rational(p["time.maxClockFractionDenominator"]))
    lines.append("        },")
    lines.append("        .stopPolicy = {")
    _field(lines, 12, "stableSoftStopFraction", _cpp_rational(p["time.softStop.stablePercent"]))
    _field(lines, 12, "unstableSoftStopFraction", _cpp_rational(p["time.softStop.unstablePercent"]))
    _field(lines, 12, "hardStopFraction", _cpp_rational(p["time.hardStopFraction"]))
    _field(lines, 12, "criticalLowTimeThresholdMs", _cpp_int(p["time.criticalLowTimeThresholdMs"]))
    _field(lines, 12, "criticalLowTimeReserveMs", _cpp_int(p["time.criticalLowTimeReserveMs"]))
    lines.append("        },")
    lines.append("        .polling = {")
    _field(lines, 12, "nodeMask", _cpp_uint(p["time.polling.nodeMask"], "ULL"))
    lines.append("        },")
    lines.append("    },")
    lines.append("    .opening = {")
    _field(lines, 8, "enabled", _cpp_bool(p["opening.enabled"]))
    _field(lines, 8, "depthPlies", _cpp_int(p["opening.bookDepth"]))
    enum_value = p["opening.selectionMode"]
    if enum_value not in OPENING_ENUM_TO_CPP:
        raise HeaderGenerationError(f"unknown opening selection mode: {enum_value}")
    _field(lines, 8, "selectionMode", OPENING_ENUM_TO_CPP[enum_value])
    _field(lines, 8, "selectionTopN", _cpp_uint(p["opening.selectionTopN"]))
    _field(lines, 8, "seed", _cpp_uint(p["opening.seed"], "U"))
    lines.append("    },")
    lines.append("};")
    return lines


def _emit_piece_square(name: str, values: list[list[int]]) -> list[str]:
    lines = [f"inline constexpr PieceSquareTable {name} = {{"]
    lines.append("    {")
    for row in values:
        lines.append("        {{" + ", ".join(_cpp_int(item) for item in row) + "}},")
    lines.append("    }")
    lines.append("};")
    return lines


def generate_header(profile: dict[str, Any]) -> str:
    parameters = profile["parameters"]
    lines: list[str] = [
        "// Generated from the canonical tuning profile.",
        "// Do not edit manually.",
        "// Regenerate with tools/tuning/generate_tuning_header.py.",
        "",
        "#pragma once",
        "",
        '#include "tuning/engine_tuning.hpp"',
        "",
        "#include <cstddef>",
        "#include <string_view>",
        "",
        "namespace Tuning::Generated {",
        "",
        f"inline constexpr std::string_view PROFILE_ID = {_cpp_string(profile['profileId'])};",
        f"inline constexpr std::string_view PROFILE_HASH = {_cpp_string(profile['canonicalHash'])};",
        f"inline constexpr int PROFILE_SCHEMA_VERSION = {profile['schemaVersion']};",
        f"inline constexpr int REGISTRY_VERSION = {profile['registryVersion']};",
        f"inline constexpr std::string_view MODEL_VERSION = {_cpp_string(profile['modelVersion'])};",
        f"inline constexpr std::string_view SOURCE_BASELINE = {_cpp_string(profile['sourceBaseline'])};",
        "inline constexpr std::size_t GENERATED_PROFILE_ENTRY_COUNT = 76;",
        "inline constexpr std::size_t GENERATED_GROUPED_ARRAY_TABLE_COUNT = 19;",
        "",
    ]
    lines.extend(_emit_piece_square("MIDDLEGAME_PIECE_SQUARE_TABLE", parameters["evaluation.pst.mgPesto"]))
    lines.append("")
    lines.extend(_emit_piece_square("ENDGAME_PIECE_SQUARE_TABLE", parameters["evaluation.pst.egPesto"]))
    lines.append("")
    lines.extend(_emit_engine_tuning(parameters))
    lines.extend([
        "",
        "static_assert(PROFILE_SCHEMA_VERSION == 1);",
        "static_assert(!PROFILE_ID.empty());",
        "static_assert(!PROFILE_HASH.empty());",
        "static_assert(MIDDLEGAME_PIECE_SQUARE_TABLE.size() == kPieceTypeCount);",
        "static_assert(MIDDLEGAME_PIECE_SQUARE_TABLE[0].size() == kBoardSquareCount);",
        "static_assert(ENDGAME_PIECE_SQUARE_TABLE.size() == kPieceTypeCount);",
        "static_assert(ENDGAME_PIECE_SQUARE_TABLE[0].size() == kBoardSquareCount);",
        "",
        "} // namespace Tuning::Generated",
        "",
    ])
    return "\n".join(lines)


def _atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp_name, path)
    except Exception:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


def _validate_output_policy(profile: dict[str, Any], output: Path, allow_development_overwrite: bool) -> None:
    if profile["profileId"] == PROFILE_ID:
        return
    if output.resolve() == DEFAULT_OUTPUT.resolve() and not allow_development_overwrite:
        raise HeaderGenerationError(
            "candidate profiles cannot overwrite engine/include/tuning/generated_tuning_values.hpp "
            "without --allow-development-production-header-overwrite"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="compare expected bytes without writing")
    parser.add_argument("--allow-development-production-header-overwrite", action="store_true")
    args = parser.parse_args()

    try:
        registry = load_registry(args.registry)
        profile = load_profile(args.profile)
        stats = validate_profile(profile, args.registry)
        if stats["profile_entries"] != len(registry["parameters"]):
            raise HeaderGenerationError("profile entry count does not match registry")
        _validate_output_policy(profile, args.output, args.allow_development_production_header_overwrite)
        content = generate_header(profile)
        existing = args.output.read_text(encoding="utf-8") if args.output.exists() else None
        if args.check:
            if existing is None:
                raise HeaderGenerationError(f"generated header is missing: {args.output}")
            if existing != content:
                raise HeaderGenerationError(f"generated header is stale: {args.output}")
            print(f"generated tuning header is up to date: {args.output}")
            return 0
        if existing == content:
            print(f"generated tuning header already up to date: {args.output}")
            return 0
        _atomic_write(args.output, content)
        print(f"wrote generated tuning header: {args.output}")
        print(f"profile entries: {stats['profile_entries']}, canonical hash: {profile['canonicalHash']}")
        return 0
    except (OSError, json.JSONDecodeError, ProfileError, ProfileValidationError, HeaderGenerationError) as exc:
        print(f"header generation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
