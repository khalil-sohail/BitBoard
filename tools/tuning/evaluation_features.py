#!/usr/bin/env python3
"""Phase 14 deterministic Bitboard evaluation-feature export and sensitivity analysis."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

SCHEMA_VERSION = 1
FEATURE_MODEL_VERSION = "bitboard-eval-features-v1"
EXPECTED_PROFILE_ID = "builtin-default-v1"
EXPECTED_PROFILE_HASH = "sha256:55a1ac92352bd018460f115cb5061c76140f1eed453afc8a229ed3fa84145718"
DEFAULT_DATASET = Path("tuning/datasets/pgn-derived-v1")
DEFAULT_ANNOTATIONS = Path("tuning/annotations/stockfish-reference-v1-dev")
DEFAULT_ENGINE = Path("engine/chess-engine")
DEFAULT_OUTPUT = Path("tuning/features/eval-features-v1-dev")
DEFAULT_REGISTRY = Path("tuning/parameter-registry.json")
DEFAULT_FIXTURES = Path(__file__).with_name("evaluation_feature_fixtures.json")
SPLITS = ("train", "validation", "test")
JSONL_FILES = ("features.jsonl", "train.jsonl", "validation.jsonl", "test.jsonl", "failures.jsonl")
ANALYSIS_FILES = ("sensitivity.json", "sensitivity-groups.json", "correlations.json")
PIECES = ("pawn", "knight", "bishop", "rook", "queen", "king")
ROOK_INDEXES = ("open", "semiOpen", "seventhRank")


class FeatureError(Exception):
    pass


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def pretty_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n"


def sha256_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise FeatureError(f"Cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise FeatureError(f"Expected JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as error:
        raise FeatureError(f"Cannot read JSONL {path}: {error}") from error
    for number, line in enumerate(lines, 1):
        if not line:
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise FeatureError(f"Invalid JSONL {path}:{number}: {error}") from error
        if not isinstance(value, dict):
            raise FeatureError(f"Expected object at {path}:{number}")
        result.append(value)
    return result


def jsonl_bytes(records: Sequence[dict[str, Any]]) -> bytes:
    return (("\n".join(canonical_json(record) for record in records) + "\n") if records else "\n").encode()


def write_atomic(path: Path, data: bytes) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def validate_artifact_manifest(directory: Path, manifest: Mapping[str, Any], label: str) -> None:
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict):
        raise FeatureError(f"{label} manifest has no artifacts object")
    for name, metadata in artifacts.items():
        if not isinstance(metadata, dict) or "sha256" not in metadata:
            continue
        path = directory / name
        if not path.is_file():
            raise FeatureError(f"{label} artifact is missing: {path}")
        actual = sha256_file(path)
        if actual != metadata["sha256"]:
            raise FeatureError(f"{label} checksum mismatch for {name}: {actual} != {metadata['sha256']}")


def load_registry(path: Path) -> tuple[dict[str, dict[str, Any]], str]:
    registry = read_json(path)
    parameters = registry.get("parameters")
    if not isinstance(parameters, list):
        raise FeatureError("Registry parameters must be an array")
    evaluation: dict[str, dict[str, Any]] = {}
    for parameter in parameters:
        if not isinstance(parameter, dict) or not str(parameter.get("name", "")).startswith("evaluation."):
            continue
        name = str(parameter["name"])
        if name in evaluation:
            raise FeatureError(f"Duplicate evaluation registry name: {name}")
        evaluation[name] = parameter
    if len(evaluation) != 44:
        raise FeatureError(f"Expected 44 evaluation registry entries, found {len(evaluation)}")
    profile_path = path.parent / "profiles" / "builtin-default-v1.json"
    profile_parameters = read_json(profile_path).get("parameters", {})
    for name, parameter in evaluation.items():
        if isinstance(parameter.get("currentValue"), str) and str(parameter["currentValue"]).startswith("source-table:"):
            if name not in profile_parameters:
                raise FeatureError(f"Canonical profile does not resolve registry source table: {name}")
            parameter = dict(parameter)
            parameter["currentValue"] = profile_parameters[name]
            evaluation[name] = parameter
    return evaluation, sha256_file(path)


def generated_field(name: str) -> str:
    replacements = {
        "evaluation.bishop.badHeavyPenalty": "evaluation.piecePlacement.badBishopHeavyPenalty",
        "evaluation.bishop.badLightPenalty": "evaluation.piecePlacement.badBishopLightPenalty",
        "evaluation.queen.earlyUndevelopedMinorPenalty": "evaluation.piecePlacement.earlyQueenUndevelopedMinorPenalty",
        "evaluation.rook.trappedPenalty": "evaluation.piecePlacement.trappedRookPenalty",
        "evaluation.taper.scale": "evaluation.endgame.taperScale",
        "evaluation.king.attackPressure": "evaluation.kingSafety.attackPressure",
        "evaluation.king.shieldMaxPawns": "evaluation.kingSafety.shieldMaxPawns",
        "evaluation.king.shieldPerPawnBonus": "evaluation.kingSafety.shieldPerPawnBonus",
        "evaluation.king.uncastledCenterPenalty": "evaluation.kingSafety.uncastledCenterPenalty",
        "evaluation.king.uncastledLostRightsPenalty": "evaluation.kingSafety.uncastledLostRightsPenalty",
        "evaluation.pst.mgPesto": "Tuning::Generated::MIDDLEGAME_PIECE_SQUARE_TABLE",
        "evaluation.pst.egPesto": "Tuning::Generated::ENDGAME_PIECE_SQUARE_TABLE",
        "evaluation.rookActivity.mg": "evaluation.rookActivity.middlegameArray()",
        "evaluation.rookActivity.eg": "evaluation.rookActivity.endgameArray()",
    }
    if name in replacements:
        return replacements[name]
    pawn = name.removeprefix("evaluation.pawns.")
    pawn_fields = {
        "connectedByRank.mg":"connectedMgByRank", "connectedByRank.eg":"connectedEgByRank",
        "candidateByRank.mg":"candidateMgByRank", "candidateByRank.eg":"candidateEgByRank",
        "backwardByRank.mg":"backwardMgByRank", "backwardByRank.eg":"backwardEgByRank",
        "islandPenalty.mg":"islandPenaltyMg", "islandPenalty.eg":"islandPenaltyEg",
        "passedCountBonus.mg":"passedCountBonusMg", "passedCountBonus.eg":"passedCountBonusEg",
    }
    if pawn in pawn_fields:
        return "evaluation.pawns." + pawn_fields[pawn]
    return name.replace(".mg", ".middlegame").replace(".eg", ".endgame")


def feature_metadata(registry: Mapping[str, Mapping[str, Any]]) -> list[dict[str, Any]]:
    result = []
    for name, parameter in sorted(registry.items()):
        array = parameter.get("type") == "integer array"
        pst = ".pst." in name
        conditional = any(token in name for token in ("latePhase", "Margin", "scale", "shieldMax", "BlockedDivisor"))
        representation = "piece-square sparse coefficients" if pst else "indexed sparse coefficients" if array else "scalar coefficient"
        classification = "piecewise linear" if conditional else "conditional linear" if any(token in name for token in ("bishopPair", "uncastled", "bad", "early", "trapped")) else "linear"
        if "mopUpWeights" in name:
            classification = "non-linear"
        result.append({
            "registryName": name,
            "generatedFieldPath": generated_field(name),
            "featureRepresentation": representation,
            "units": parameter.get("unit"),
            "stage": "MG" if name.endswith(".mg") or ".mgPesto" in name else "EG" if name.endswith(".eg") or ".egPesto" in name else "both/structural",
            "orientation": "white-minus-black; black PST squares mirrored to White orientation",
            "groupDimensions": parameter_dimensions(parameter),
            "classification": classification,
            "linear": classification == "linear",
            "conditional": classification != "linear",
            "directlyReconstructable": True,
        })
    return result


def parameter_dimensions(parameter: Mapping[str, Any]) -> list[int]:
    value = parameter.get("currentValue")
    if not isinstance(value, list):
        return []
    if value and isinstance(value[0], list):
        return [len(value), len(value[0])]
    return [len(value)]


def registry_indexes(name: str, value: Any) -> list[tuple[str, int]]:
    if not isinstance(value, list):
        return [("scalar", int(value))]
    if name.startswith("evaluation.pst."):
        return [(f"{PIECES[piece]}.{square_name(square)}", int(row[square])) for piece, row in enumerate(value) for square in range(64)]
    if name.startswith("evaluation.material") or name.startswith("evaluation.mobility") or name == "evaluation.phase.increments":
        return [(PIECES[index], int(item)) for index, item in enumerate(value)]
    if name.startswith("evaluation.rookActivity"):
        return [(ROOK_INDEXES[index], int(item)) for index, item in enumerate(value)]
    return [(str(index), int(item)) for index, item in enumerate(value)]


def square_name(square: int) -> str:
    return chr(ord("a") + square % 8) + str(square // 8 + 1)


def validate_sparse_features(features: Any, registry: Mapping[str, Any]) -> None:
    if not isinstance(features, dict):
        raise FeatureError("Engine record features must be an object")
    names = set(features)
    expected = set(registry)
    if names != expected:
        raise FeatureError(f"Engine feature coverage mismatch: missing={sorted(expected-names)} unknown={sorted(names-expected)}")
    for name, feature in features.items():
        if not isinstance(feature, dict) or not isinstance(feature.get("coefficients"), dict):
            raise FeatureError(f"Malformed feature: {name}")
        coefficients = feature["coefficients"]
        if any(not isinstance(index, str) or not isinstance(value, int) or value == 0 for index, value in coefficients.items()):
            raise FeatureError(f"Feature {name} contains invalid or explicit zero sparse coefficients")


def trunc_div(numerator: int, denominator: int) -> int:
    if denominator <= 0:
        raise FeatureError("Invalid evaluation denominator")
    return math.trunc(numerator / denominator)


def validate_reconstruction(record: Mapping[str, Any]) -> None:
    scores = record["scores"]
    terms = record["weightedTerms"]
    if not scores["insufficientMaterialDraw"]:
        mg = sum(int(term["middlegame"]) for term in terms)
        eg = sum(int(term["endgame"]) for term in terms)
        if mg != scores["middlegameWhiteCp"] or eg != scores["endgameWhiteCp"]:
            raise FeatureError(f"MG/EG reconstruction mismatch for {record.get('positionId')}: {(mg,eg)}")
        tapered = trunc_div(mg * record["phase"]["clamped"] + eg * (24 - record["phase"]["clamped"]), 24)
        if tapered != scores["taperedWhiteCp"]:
            raise FeatureError("Taper reconstruction mismatch")
    final = trunc_div(scores["noPawnScaledWhiteCp"] * scores["lowMaterialScale"], 128)
    if scores["insufficientMaterialDraw"]:
        final = 0
    if final != scores["finalWhiteCp"]:
        raise FeatureError("Final reconstruction mismatch")
    expected_stm = final if record["sideToMove"] == "white" else -final
    if expected_stm != scores["finalFromSideToMoveCp"]:
        raise FeatureError("Side-to-move perspective mismatch")


def run_engine(engine: Path, positions: Sequence[dict[str, Any]], registry: Mapping[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any]]:
    if not engine.is_file() or not os.access(engine, os.X_OK):
        raise FeatureError(f"Bitboard engine is not executable: {engine}")
    payload = "".join(str(position["fen"]) + "\n" for position in positions)
    process = subprocess.run([str(engine.resolve()), "--mode=eval-features"], input=payload, text=True, capture_output=True, check=False)
    if process.returncode:
        raise FeatureError(f"Feature exporter failed ({process.returncode}): {process.stderr.strip()}")
    raw = [json.loads(line) for line in process.stdout.splitlines() if line]
    failures = [{"schemaVersion": SCHEMA_VERSION, "message": line} for line in process.stderr.splitlines() if line]
    if len(raw) + len(failures) != len(positions):
        raise FeatureError(f"Feature exporter returned {len(raw)} records and {len(failures)} failures for {len(positions)} positions")
    records: list[dict[str, Any]] = []
    raw_by_fen: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for item in raw:
        raw_by_fen[item["fen"]].append(item)
    for position in positions:
        candidates = raw_by_fen.get(position["fen"], [])
        if not candidates:
            continue
        item = candidates.pop(0)
        validate_sparse_features(item["features"], registry)
        item["positionId"] = position["positionId"]
        validate_reconstruction(item)
        records.append(item)
    if not records:
        raise FeatureError("Feature exporter produced no usable records")
    identity = {key: records[0][key] for key in ("profileId", "profileHash", "profileSchemaVersion", "modelVersion", "featureModelVersion")}
    if identity["profileId"] != EXPECTED_PROFILE_ID or identity["profileHash"] != EXPECTED_PROFILE_HASH:
        raise FeatureError(f"Compiled profile mismatch: {identity}")
    if identity["featureModelVersion"] != FEATURE_MODEL_VERSION:
        raise FeatureError("Feature model version mismatch")
    if any(any(record[key] != value for key, value in identity.items()) for record in records):
        raise FeatureError("Bitboard identity changed within one export")
    return records, failures, identity


def select_positions(dataset_dir: Path, split: str, maximum: int | None) -> list[dict[str, Any]]:
    source = dataset_dir / ("positions.jsonl" if split == "all" else f"{split}.jsonl")
    positions = read_jsonl(source)
    return positions[:maximum] if maximum is not None else positions


def join_records(positions: Sequence[dict[str, Any]], annotations: Sequence[dict[str, Any]], engine_records: Sequence[dict[str, Any]], registry: Mapping[str, Mapping[str, Any]]) -> list[dict[str, Any]]:
    def unique(items: Sequence[dict[str, Any]], label: str) -> dict[str, dict[str, Any]]:
        result = {}
        for item in items:
            identifier = item.get("positionId")
            if not isinstance(identifier, str):
                raise FeatureError(f"{label} record lacks positionId")
            if identifier in result:
                raise FeatureError(f"Duplicate {label} positionId: {identifier}")
            result[identifier] = item
        return result
    annotation_by_id = unique(annotations, "annotation")
    engine_by_id = unique(engine_records, "engine feature")
    result = []
    current_values = {name: parameter["currentValue"] for name, parameter in sorted(registry.items())}
    for position in positions:
        identifier = position["positionId"]
        annotation = annotation_by_id.get(identifier)
        engine = engine_by_id.get(identifier)
        if annotation is None:
            raise FeatureError(f"Missing annotation for selected position: {identifier}")
        if engine is None:
            raise FeatureError(f"Missing engine features for selected position: {identifier}")
        if annotation.get("fen") != position.get("fen") or engine.get("fen") != position.get("fen"):
            raise FeatureError(f"FEN mismatch for {identifier}")
        score_type = annotation.get("scoreType")
        stockfish: dict[str, Any] = {"scoreType": score_type}
        if score_type == "cp":
            stockfish.update(scoreWhiteCp=annotation.get("scoreWhiteCp"), scoreFromSideToMoveCp=annotation.get("scoreFromSideToMoveCp"))
        elif score_type == "mate":
            stockfish.update(mateWhite=annotation.get("mateWhite"), mateFromSideToMove=annotation.get("mateFromSideToMove"))
        else:
            stockfish.update(terminal=annotation.get("terminal"))
        bitboard = int(engine["scores"]["finalWhiteCp"])
        joined = {
            "schemaVersion": SCHEMA_VERSION,
            "featureModelVersion": FEATURE_MODEL_VERSION,
            "positionId": identifier,
            "fen": position["fen"],
            "split": position["split"],
            "gamePhase": position["gamePhase"],
            "gameResult": position.get("result"),
            "sideToMove": position["sideToMove"],
            "profileId": engine["profileId"],
            "profileHash": engine["profileHash"],
            "phase": engine["phase"],
            "scores": engine["scores"],
            "features": engine["features"],
            "weightedTerms": engine["weightedTerms"],
            "currentParameters": current_values,
            "stockfish": stockfish,
        }
        if score_type == "cp":
            error = bitboard - int(stockfish["scoreWhiteCp"])
            joined["comparison"] = {"errorCp": error, "absoluteErrorCp": abs(error), "squaredErrorCp": error * error}
        else:
            joined["comparison"] = None
        result.append(joined)
    return result


def percentile(values: Sequence[float], percent: int) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * percent / 100
    low, high = math.floor(position), math.ceil(position)
    return ordered[low] if low == high else ordered[low] * (high-position) + ordered[high] * (position-low)


def correlation(xs: Sequence[float], ys: Sequence[float]) -> float | None:
    if len(xs) < 2 or len(xs) != len(ys):
        return None
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    numerator = sum((x-mx)*(y-my) for x,y in zip(xs,ys))
    denominator = math.sqrt(sum((x-mx)**2 for x in xs)*sum((y-my)**2 for y in ys))
    return numerator/denominator if denominator else None


def metric_block(records: Sequence[dict[str, Any]]) -> dict[str, Any]:
    cp = [record for record in records if record["comparison"] is not None]
    errors = [record["comparison"]["errorCp"] for record in cp]
    absolute = [abs(value) for value in errors]
    bitboard = [record["scores"]["finalWhiteCp"] for record in cp]
    stockfish = [record["stockfish"]["scoreWhiteCp"] for record in cp]
    return {
        "records": len(cp), "meanSignedErrorCp": statistics.fmean(errors) if errors else None,
        "meanAbsoluteErrorCp": statistics.fmean(absolute) if absolute else None,
        "rootMeanSquaredErrorCp": math.sqrt(statistics.fmean([value*value for value in errors])) if errors else None,
        "medianAbsoluteErrorCp": statistics.median(absolute) if absolute else None,
        "absoluteErrorPercentilesCp": {str(p): percentile(absolute,p) for p in (50,75,90,95,99)},
        "scoreCorrelation": correlation(bitboard, stockfish),
    }


def breakdown(records: Sequence[dict[str, Any]], key) -> dict[str, Any]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in records: groups[str(key(record))].append(record)
    return {name: metric_block(items) for name, items in sorted(groups.items())}


def summary_for(records: Sequence[dict[str, Any]], failures: Sequence[dict[str, Any]]) -> dict[str, Any]:
    cp = [record for record in records if record["comparison"] is not None]
    mates = [record for record in records if record["stockfish"]["scoreType"] == "mate"]
    metrics = metric_block(records)
    metrics["breakdowns"] = {
        "gamePhase": breakdown(cp, lambda r:r["gamePhase"]), "sideToMove": breakdown(cp, lambda r:r["sideToMove"]),
        "gameResult": breakdown(cp, lambda r:r["gameResult"]),
        "stockfishScoreRange": breakdown(cp, lambda r: "<-300" if r["stockfish"]["scoreWhiteCp"] < -300 else "-300..-101" if r["stockfish"]["scoreWhiteCp"] < -100 else "-100..100" if r["stockfish"]["scoreWhiteCp"] <= 100 else "101..300" if r["stockfish"]["scoreWhiteCp"] <= 300 else ">300"),
    }
    return {
        "schemaVersion": SCHEMA_VERSION, "featureModelVersion": FEATURE_MODEL_VERSION, "developmentOnly": True,
        "counts": {"positions":len(records),"failures":len(failures),"cpComparisons":len(cp),"mateAnnotations":len(mates),"terminalAnnotations":len(records)-len(cp)-len(mates),"nonzeroFeatureCoefficients":sum(len(feature["coefficients"]) for record in records for feature in record["features"].values())},
        "reconstruction": {"exact":len(records),"middlegameMismatches":0,"endgameMismatches":0,"phaseMismatches":0,"taperMismatches":0,"finalMismatches":0,"unexplainedResiduals":0},
        "metrics": metrics,
        "warning": "Development-only 100-position tooling validation; not evidence for engine strength or final tuning decisions.",
    }


def stage_for(name: str) -> tuple[bool, bool]:
    mg = name.endswith(".mg") or ".mgPesto" in name or name in {"evaluation.bishop.badHeavyPenalty","evaluation.bishop.badLightPenalty","evaluation.pawns.doubledPenalty","evaluation.pawns.isolatedPenalty"}
    eg = name.endswith(".eg") or ".egPesto" in name or name in {"evaluation.bishop.badHeavyPenalty","evaluation.bishop.badLightPenalty","evaluation.pawns.doubledPenalty","evaluation.pawns.isolatedPenalty"}
    if name == "evaluation.pawns.passedRankSquareMultiplier": mg = True
    if name in {"evaluation.king.attackPressure","evaluation.king.shieldPerPawnBonus","evaluation.king.uncastledCenterPenalty","evaluation.king.uncastledLostRightsPenalty","evaluation.queen.earlyUndevelopedMinorPenalty","evaluation.rook.trappedPenalty"}: mg = True
    return mg, eg


def perturbed_score(record: Mapping[str, Any], name: str, index: str, delta: int) -> int:
    scores, feature = record["scores"], record["features"][name]
    coefficient_value = int(feature["coefficients"].get(index, 0))
    mg, eg = int(scores["middlegameWhiteCp"]), int(scores["endgameWhiteCp"])
    phase = int(record["phase"]["clamped"])
    affects_mg, affects_eg = stage_for(name)
    if affects_mg: mg += coefficient_value * delta
    if affects_eg: eg += coefficient_value * delta
    if name == "evaluation.phase.increments": phase = max(0,min(24,phase+coefficient_value*delta))
    if name == "evaluation.king.shieldMaxPawns":
        raw=feature["coefficients"];white=int(raw.get("whiteRaw",0));black=-int(raw.get("blackRaw",0))
        current_cap=int(record["currentParameters"][name]);new_cap=current_cap+delta
        per_pawn=int(record["currentParameters"]["evaluation.king.shieldPerPawnBonus"])
        old=int(record["features"]["evaluation.king.shieldPerPawnBonus"]["middlegameContribution"])
        mg += (min(white,new_cap)-min(black,new_cap))*per_pawn-old
    mop_names={"evaluation.endgame.latePhaseMax","evaluation.endgame.mopUpEgMargin","evaluation.endgame.mopUpMaterialMargin","evaluation.endgame.mopUpWeights","evaluation.material.eg","evaluation.phase.increments"}
    if name in mop_names:
        parameters=record["currentParameters"];mop_feature=record["features"]["evaluation.endgame.mopUpWeights"]
        weights=list(map(int,parameters["evaluation.endgame.mopUpWeights"]))
        if name=="evaluation.endgame.mopUpWeights" and index.isdigit(): weights[int(index)]+=delta
        late=int(parameters["evaluation.endgame.latePhaseMax"])+(delta if name=="evaluation.endgame.latePhaseMax" else 0)
        eg_margin=int(parameters["evaluation.endgame.mopUpEgMargin"])+(delta if name=="evaluation.endgame.mopUpEgMargin" else 0)
        material_margin=int(parameters["evaluation.endgame.mopUpMaterialMargin"])+(delta if name=="evaluation.endgame.mopUpMaterialMargin" else 0)
        pre_eg=int(record["features"]["evaluation.endgame.mopUpEgMargin"]["coefficients"].get("endgameScore",0))
        if affects_eg: pre_eg += coefficient_value*delta
        advantage=int(record["features"]["evaluation.endgame.mopUpMaterialMargin"]["coefficients"].get("materialAdvantage",0))
        if name=="evaluation.material.eg": advantage += coefficient_value*delta
        def formula(prefix: str) -> int:
            state=mop_feature["coefficients"]
            center=int(state.get(prefix+".centerDistance",0));edge=int(state.get(prefix+".edgeDistance",0));corner=int(state.get(prefix+".cornerDistance",0));king=int(state.get(prefix+".kingDistance",0))
            return center*weights[0]+(weights[1]-edge)*weights[2]+(weights[3]-min(corner,weights[3]))*weights[4]+(weights[5]-king)*weights[6]
        new_mop=0
        if phase<=late:
            if pre_eg>eg_margin and advantage>=material_margin: new_mop=formula("whiteWins")
            elif pre_eg<-eg_margin and advantage<=-material_margin: new_mop=-formula("blackWins")
        eg += new_mop-int(mop_feature["endgameContribution"])
    tapered = trunc_div(mg*phase+eg*(24-phase),24)
    # The no-pawn halving condition is preserved as an exact rational ratio when active.
    original_tapered = int(scores["taperedWhiteCp"])
    no_pawn = tapered
    if int(scores["noPawnScaledWhiteCp"]) != original_tapered:
        no_pawn = trunc_div(tapered,2)
    scale = int(scores["lowMaterialScale"])
    if name.startswith("evaluation.endgame.scale") and feature["coefficients"].get("selected"):
        scale += delta
    denominator = 128 + delta if name == "evaluation.taper.scale" else 128
    if scores["insufficientMaterialDraw"]: return 0
    return trunc_div(no_pawn*scale,denominator)


def group_name(name: str) -> str:
    if ".pst." in name: return "PST MG" if ".mgPesto" in name else "PST EG"
    if ".material." in name or "bishopPair" in name: return "material"
    if ".mobility." in name: return "mobility"
    if ".pawns." in name: return "pawn structure"
    if ".king." in name: return "king safety"
    if "rookActivity" in name: return "rook activity"
    if ".bishop.bad" in name or ".queen." in name or ".rook.trapped" in name: return "piece placement"
    if ".endgame." in name: return "mop-up/endgame"
    return "phase/taper"


def classify(mean_abs: float, maximum: float, affected: int, bound_risk: bool) -> list[str]:
    labels = []
    if affected == 0: labels.append("inactive on current subset")
    elif mean_abs < 0.25: labels.append("low sensitivity")
    elif mean_abs < 2: labels.append("moderate sensitivity")
    else: labels.append("high sensitivity")
    if maximum >= 50: labels.append("unstable")
    if bound_risk: labels.append("bound-risk")
    return labels


def feature_active(feature: Mapping[str, Any]) -> bool:
    return bool(feature.get("coefficients")) or bool(feature.get("middlegameContribution")) or bool(feature.get("endgameContribution"))


def connectivity_report(records: Sequence[dict[str, Any]], synthetic: Sequence[dict[str, Any]], registry: Mapping[str, Mapping[str, Any]]) -> list[dict[str, Any]]:
    result=[]
    for name,parameter in sorted(registry.items()):
        classification=records[0]["features"][name]["classification"] if records else "unknown"
        connected=bool(parameter.get("consumer")) and name != "evaluation.pawns.passedBlockedDivisor"
        result.append({"registryName":name,"exported":True,"productionConnected":connected,"connectivityNote":"referenced by an unreachable enemy-pawn blocker branch" if not connected and name=="evaluation.pawns.passedBlockedDivisor" else None,"activatedInRealSubset":any(feature_active(record["features"][name]) for record in records),"activatedBySyntheticFixture":any(feature_active(record["features"][name]) for record in synthetic),"reconstructable":True,"sensitivityMethod":"analytical exported coefficient" if classification=="linear" else "explicit piecewise/structural recomputation"})
    return result


def sensitivity_analysis(records: Sequence[dict[str, Any]], registry: Mapping[str, Mapping[str, Any]]) -> tuple[dict[str, Any],dict[str,Any],dict[str,Any]]:
    entries=[]
    activation_vectors: dict[str,list[float]]={}
    for name, parameter in sorted(registry.items()):
        minimum,maximum,step=parameter.get("minimum"),parameter.get("maximum"),int(parameter.get("step") or 1)
        for index,current in registry_indexes(name,parameter["currentValue"]):
            low=minimum[int(index)] if isinstance(minimum,list) and index.isdigit() else minimum
            high=maximum[int(index)] if isinstance(maximum,list) and index.isdigit() else maximum
            minus_step=max(-step,int(low)-int(current)) if isinstance(low,(int,float)) else -step
            plus_step=min(step,int(high)-int(current)) if isinstance(high,(int,float)) else step
            deltas=[]; minus_deltas=[]; errors=[]; active=[]
            for record in records:
                baseline=int(record["scores"]["finalWhiteCp"]);plus=perturbed_score(record,name,index,plus_step);minus=perturbed_score(record,name,index,minus_step);delta=plus-baseline
                deltas.append(delta);minus_deltas.append(minus-baseline);active.append(float(record["features"][name]["coefficients"].get(index,0)))
                if record["comparison"] is not None: errors.append(abs(plus-int(record["stockfish"]["scoreWhiteCp"]))-record["comparison"]["absoluteErrorCp"])
            responses=[max(abs(plus),abs(minus)) for plus,minus in zip(deltas,minus_deltas)];affected=sum(value!=0 for value in responses);mean_abs=statistics.fmean(responses) if responses else 0.0;maximum_abs=max(responses,default=0)
            bound= isinstance(low,(int,float)) and isinstance(high,(int,float)) and (int(current)-int(low)<=step or int(high)-int(current)<=step)
            stable=f"{name}[{index}]" if index!="scalar" else name
            classifications=classify(mean_abs,maximum_abs,affected,bound)
            if name=="evaluation.pawns.passedBlockedDivisor": classifications=["disconnected"]+[label for label in classifications if label!="inactive on current subset"]
            entries.append({"name":stable,"registryName":name,"index":index,"currentValue":current,"step":step,"perturbations":{"minus":int(current)+minus_step,"default":current,"plus":int(current)+plus_step},"method":"analytical exported coefficient" if stage_for(name)!=(False,False) else "explicit structural recomputation","positionsAffected":affected,"meanAbsoluteChangeCp":mean_abs,"maximumAbsoluteChangeCp":maximum_abs,"signConsistency":abs(sum(1 if v>0 else -1 if v<0 else 0 for v in deltas))/affected if affected else None,"meanAbsoluteErrorDeltaCp":statistics.fmean(errors) if errors else None,"phaseDistribution":dict(sorted(Counter(record["gamePhase"] for record,value in zip(records,responses) if value).items())),"classifications":classifications})
            activation_vectors[stable]=active
    group_items=[]
    grouped: dict[str,list[dict[str,Any]]]=defaultdict(list)
    for entry in entries: grouped[group_name(entry["registryName"])].append(entry)
    for name,items in sorted(grouped.items()):
        vectors=[activation_vectors[item["name"]] for item in items]
        group_activation=[sum(abs(vector[position]) for vector in vectors) for position in range(len(records))]
        current_errors=[float(record["comparison"]["errorCp"]) for record in records if record["comparison"] is not None]
        error_activation=[group_activation[position] for position,record in enumerate(records) if record["comparison"] is not None]
        group_items.append({"name":name,"parameterIndexes":len(items),"positionsAffected":sum(value!=0 for value in group_activation),"positionsAffectedMaximumPerIndex":max((i["positionsAffected"] for i in items),default=0),"meanAbsoluteScoreResponseCp":statistics.fmean(i["meanAbsoluteChangeCp"] for i in items),"maximumResponseCp":max((i["maximumAbsoluteChangeCp"] for i in items),default=0),"featureSparsity":statistics.fmean(1-i["positionsAffected"]/len(records) for i in items) if records else 1,"meanStockfishErrorDirectionCp":statistics.fmean(i["meanAbsoluteErrorDeltaCp"] for i in items if i["meanAbsoluteErrorDeltaCp"] is not None) if any(i["meanAbsoluteErrorDeltaCp"] is not None for i in items) else None,"correlationWithCurrentError":correlation(error_activation,current_errors)})
    pairs=[]
    active_vectors={name:v for name,v in activation_vectors.items() if any(v)}
    names=sorted(active_vectors)
    for i,left in enumerate(names):
        for right in names[i+1:]:
            value=correlation(active_vectors[left],active_vectors[right])
            if value is not None and abs(value)>=0.9: pairs.append({"left":left,"right":right,"correlation":value,"classification":"redundant/correlated"})
    return ({"schemaVersion":1,"developmentOnly":True,"thresholds":{"disconnected":"no exported feature and no production consumer; none expected","inactive":"connected feature with no score delta on selected positions","low":"mean absolute response <0.25 cp","moderate":"0.25..<2 cp","high":">=2 cp","unstable":"maximum response >=50 cp","boundRisk":"within one step of a configured bound","redundantCorrelated":"absolute activation correlation >=0.90"},"entries":entries}, {"schemaVersion":1,"developmentOnly":True,"groups":group_items}, {"schemaVersion":1,"developmentOnly":True,"thresholdAbsoluteCorrelation":0.9,"pairs":pairs})


def artifact_metadata(directory: Path, names: Iterable[str]) -> dict[str, Any]:
    result={}
    for name in names:
        path=directory/name
        metadata={"sha256":sha256_file(path)}
        if name.endswith(".jsonl"): metadata["records"]=len(read_jsonl(path))
        result[name]=metadata
    return result


def update_analysis(directory: Path, registry: Mapping[str,Mapping[str,Any]]) -> None:
    records=read_jsonl(directory/"features.jsonl")
    sensitivity,groups,correlations=sensitivity_analysis(records,registry)
    for name,value in zip(ANALYSIS_FILES,(sensitivity,groups,correlations)): write_atomic(directory/name,pretty_json(value).encode())
    manifest=read_json(directory/"manifest.json")
    manifest["artifacts"]=artifact_metadata(directory,(*JSONL_FILES,"summary.json",*ANALYSIS_FILES))
    write_atomic(directory/"manifest.json",pretty_json(manifest).encode())


def export_command(args: argparse.Namespace) -> None:
    dataset_dir,annotation_dir,engine,output,registry_path=map(Path,(args.dataset_dir,args.annotation_dir,args.engine,args.output_dir,args.registry))
    if output.exists() and not args.force: raise FeatureError(f"Output already exists (use --force): {output}")
    dataset_manifest,annotation_manifest=read_json(dataset_dir/"manifest.json"),read_json(annotation_dir/"manifest.json")
    validate_artifact_manifest(dataset_dir,dataset_manifest,"Phase 12")
    validate_artifact_manifest(annotation_dir,annotation_manifest,"Phase 13")
    registry,registry_hash=load_registry(registry_path)
    positions=select_positions(dataset_dir,args.split,args.max_positions)
    annotation_records=read_jsonl(annotation_dir/"annotations.jsonl")
    selected_ids={position["positionId"] for position in positions}
    annotations=[record for record in annotation_records if record.get("positionId") in selected_ids]
    if len(annotations)!=len(positions): raise FeatureError(f"Selected {len(positions)} positions but found {len(annotations)} annotations")
    engine_records,failures,identity=run_engine(engine,positions,registry)
    fixture_document=read_json(DEFAULT_FIXTURES)
    fixture_positions=[{"positionId":f"synthetic:{fixture['name']}","fen":fixture["fen"]} for fixture in fixture_document.get("fixtures",[])]
    synthetic_records,synthetic_failures,synthetic_identity=run_engine(engine,fixture_positions,registry)
    if synthetic_failures or synthetic_identity != identity:
        raise FeatureError("Synthetic connectivity export failed or changed engine identity")
    joined=join_records(positions,annotations,engine_records,registry)
    summary=summary_for(joined,failures)
    summary["syntheticFixtures"]={"count":len(synthetic_records),"source":DEFAULT_FIXTURES.name}
    summary["connectivity"]=connectivity_report(joined,synthetic_records,registry)
    summary["connectivityCounts"]={"evaluationEntries":len(registry),"missingExported":0,"unknownExported":0,"unexplainedResidualTerms":0,"activatedInRealSubset":sum(item["activatedInRealSubset"] for item in summary["connectivity"]),"activatedBySyntheticFixture":sum(item["activatedBySyntheticFixture"] for item in summary["connectivity"]),"disconnected":sum(not item["productionConnected"] for item in summary["connectivity"])}
    parent=output.parent;parent.mkdir(parents=True,exist_ok=True)
    temporary=Path(tempfile.mkdtemp(prefix=output.name+".tmp-",dir=parent))
    try:
        by_split={split:[record for record in joined if record["split"]==split] for split in SPLITS}
        for name,records in (("features.jsonl",joined),("failures.jsonl",failures),*((f"{split}.jsonl",by_split[split]) for split in SPLITS)): write_atomic(temporary/name,jsonl_bytes(records))
        write_atomic(temporary/"summary.json",pretty_json(summary).encode())
        for name in ANALYSIS_FILES: write_atomic(temporary/name,pretty_json({"schemaVersion":1,"pending":True}).encode())
        manifest={"schemaVersion":1,"featureModelVersion":FEATURE_MODEL_VERSION,"tool":{"name":"evaluation_features.py","version":"1"},"sourceDataset":{"datasetId":dataset_manifest.get("datasetId"),"manifestSha256":sha256_file(dataset_dir/"manifest.json")},"sourceAnnotations":{"annotationVersion":annotation_manifest.get("annotationVersion"),"manifestSha256":sha256_file(annotation_dir/"manifest.json")},"bitboard":{**identity,"binarySha256":sha256_file(engine)},"evaluationRegistry":{"sha256":registry_hash,"entries":len(registry)},"selection":{"split":args.split,"maxPositions":args.max_positions,"selectedCount":len(positions)},"counts":{"records":len(joined),"failures":len(failures)},"featureMetadata":feature_metadata(registry),"artifacts":artifact_metadata(temporary,(*JSONL_FILES,"summary.json",*ANALYSIS_FILES))}
        write_atomic(temporary/"manifest.json",pretty_json(manifest).encode())
        # Validate every canonical record before publication.
        for record in read_jsonl(temporary/"features.jsonl"): validate_reconstruction(record);validate_sparse_features(record["features"],registry)
        backup=None
        if output.exists(): backup=output.with_name(output.name+".replaced");shutil.rmtree(backup,ignore_errors=True);os.replace(output,backup)
        os.replace(temporary,output)
        if backup: shutil.rmtree(backup)
    except Exception:
        shutil.rmtree(temporary,ignore_errors=True);raise
    update_analysis(output,registry)
    print(f"Exported {len(joined)} positions to {output}")
    print(f"Exact reconstructions: {summary['reconstruction']['exact']}; failures: {len(failures)}")


def inspect_command(args: argparse.Namespace) -> None:
    directory=Path(args.feature_dir);manifest=read_json(directory/"manifest.json")
    validate_artifact_manifest(directory,manifest,"Phase 14")
    summary=read_json(directory/"summary.json")
    print(f"Feature model: {manifest['featureModelVersion']}")
    print(f"Profile: {manifest['bitboard']['profileId']} {manifest['bitboard']['profileHash']}")
    print(f"Records: {summary['counts']['positions']} (CP {summary['counts']['cpComparisons']}, mate {summary['counts']['mateAnnotations']})")
    print(f"Exact reconstructions: {summary['reconstruction']['exact']}; final mismatches: {summary['reconstruction']['finalMismatches']}")
    print("Development-only: tooling validation, not final tuning evidence.")


def sensitivity_command(args: argparse.Namespace) -> None:
    directory=Path(args.feature_dir);registry,_=load_registry(Path(args.registry));update_analysis(directory,registry)
    analysis=read_json(directory/"sensitivity.json")
    print(f"Sensitivity entries: {len(analysis['entries'])}")
    print("Development-only: the 100-position subset is not sufficient for final sensitivity conclusions.")


def parser() -> argparse.ArgumentParser:
    result=argparse.ArgumentParser(description=__doc__);sub=result.add_subparsers(dest="command",required=True)
    export=sub.add_parser("export");export.add_argument("--dataset-dir",default=str(DEFAULT_DATASET));export.add_argument("--annotation-dir",default=str(DEFAULT_ANNOTATIONS));export.add_argument("--engine",default=str(DEFAULT_ENGINE));export.add_argument("--output-dir",default=str(DEFAULT_OUTPUT));export.add_argument("--registry",default=str(DEFAULT_REGISTRY));export.add_argument("--split",choices=(*SPLITS,"all"),default="validation");export.add_argument("--max-positions",type=int,default=100);export.add_argument("--force",action="store_true");export.set_defaults(function=export_command)
    inspect=sub.add_parser("inspect");inspect.add_argument("--feature-dir",default=str(DEFAULT_OUTPUT));inspect.set_defaults(function=inspect_command)
    sensitivity=sub.add_parser("sensitivity");sensitivity.add_argument("--feature-dir",default=str(DEFAULT_OUTPUT));sensitivity.add_argument("--registry",default=str(DEFAULT_REGISTRY));sensitivity.set_defaults(function=sensitivity_command)
    return result


def main(argv: Sequence[str] | None=None) -> int:
    try: args=parser().parse_args(argv);args.function(args);return 0
    except FeatureError as error: print(f"error: {error}",file=sys.stderr);return 2


if __name__=="__main__": raise SystemExit(main())
