#!/usr/bin/env python3
"""Phase 15 deterministic development-only evaluation tuning prototype."""

from __future__ import annotations

import argparse
import copy
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
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import evaluation_features
import pgn_dataset
from canonical_json import profile_hash
from generate_tuning_header import generate_header
from validate_profile import validate_profile

SCHEMA_VERSION = 1
TOOL_VERSION = "1"
ALGORITHM_VERSION = "deterministic-stratified-sha256-v1"
FIT_METHOD = "baseline-centered-ridge-regression-v1"
DEFAULT_SEED = "bitboard-eval-prototype-selection-v1"
DEFAULT_LAMBDAS = (0.01, 0.1, 1.0, 10.0, 100.0, 1000.0)
SPLITS = ("train", "validation", "test")
PHASES = ("opening", "middlegame", "endgame")
SIDES = ("white", "black")
RESULTS = ("win", "draw", "loss")
ALLOWLIST = (
    "evaluation.bishopPair.mg",
    "evaluation.bishopPair.eg",
    "evaluation.pawns.doubledPenalty",
    "evaluation.pawns.isolatedPenalty",
    "evaluation.pawns.islandPenalty.mg",
    "evaluation.pawns.islandPenalty.eg",
    "evaluation.pawns.passedCountBonus.mg",
    "evaluation.pawns.passedCountBonus.eg",
    "evaluation.king.shieldPerPawnBonus",
    "evaluation.queen.earlyUndevelopedMinorPenalty",
)
DISCONNECTED = "evaluation.pawns.passedBlockedDivisor"


class TuningError(Exception):
    pass


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def pretty_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n"


def sha256_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def jsonl(records: Iterable[Mapping[str, Any]]) -> bytes:
    rows = [canonical_json(record) for record in records]
    return (("\n".join(rows) + "\n") if rows else "\n").encode()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TuningError(f"Expected JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    result = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        value = json.loads(line)
        if not isinstance(value, dict):
            raise TuningError(f"{path}:{number}: expected object")
        result.append(value)
    return result


def result_class(value: float) -> str:
    try:
        return {1.0: "win", 0.5: "draw", 0.0: "loss"}[value]
    except KeyError as error:
        raise TuningError(f"Unsupported resultFromSideToMove: {value}") from error


def stable_rank(seed: str, record: Mapping[str, Any]) -> str:
    payload = seed + "\0" + str(record["positionId"])
    return hashlib.sha256(payload.encode()).hexdigest()


def counts_summary(records: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    split = Counter(str(r["split"]) for r in records)
    side = Counter(str(r["sideToMove"]) for r in records)
    phase = Counter(str(r["gamePhase"]) for r in records)
    results = Counter(str(r.get("resultClass") or result_class(float(r["resultFromSideToMove"]))) for r in records)
    games = Counter(str(r["gameId"]) for r in records)
    return {
        "positions": len(records),
        "splits": {name: split[name] for name in SPLITS},
        "sideToMove": {name: side[name] for name in SIDES},
        "gamePhase": {name: phase[name] for name in PHASES},
        "resultClass": {name: results[name] for name in RESULTS},
        "uniqueGames": len(games),
        "maximumPositionsPerGame": max(games.values(), default=0),
    }


def targets(total: int, labels: Sequence[str]) -> dict[str, int]:
    base, extra = divmod(total, len(labels))
    return {label: base + (index < extra) for index, label in enumerate(labels)}


def select_split(records: Sequence[dict[str, Any]], count: int, max_per_game: int, seed: str) -> tuple[list[dict[str, Any]], list[str]]:
    if len(records) < count:
        raise TuningError(f"Split has only {len(records)} positions; {count} requested")
    ranked = sorted(records, key=lambda record: (stable_rank(seed, record), record["positionId"]))
    side_target = targets(count, SIDES)
    phase_target = targets(count, PHASES)
    result_target = targets(count, RESULTS)
    combined = [(side, phase, result) for side in SIDES for phase in PHASES for result in RESULTS]
    combined_target = targets(count, ["|".join(item) for item in combined])
    chosen: list[dict[str, Any]] = []
    chosen_ids: set[str] = set()
    game_counts: Counter[str] = Counter()
    dimension_counts: Counter[tuple[str, str]] = Counter()
    stratum_counts: Counter[str] = Counter()

    def add(record: dict[str, Any]) -> None:
        rc = result_class(float(record["resultFromSideToMove"]))
        key = f"{record['sideToMove']}|{record['gamePhase']}|{rc}"
        chosen.append(record); chosen_ids.add(record["positionId"]); game_counts[record["gameId"]] += 1
        dimension_counts["side", record["sideToMove"]] += 1
        dimension_counts["phase", record["gamePhase"]] += 1
        dimension_counts["result", rc] += 1
        stratum_counts[key] += 1

    # First satisfy fine-grained strata without breaking any marginal target.
    for record in ranked:
        rc = result_class(float(record["resultFromSideToMove"]))
        key = f"{record['sideToMove']}|{record['gamePhase']}|{rc}"
        if stratum_counts[key] >= combined_target[key] or game_counts[record["gameId"]] >= max_per_game:
            continue
        if dimension_counts["side", record["sideToMove"]] >= side_target[record["sideToMove"]]:
            continue
        if dimension_counts["phase", record["gamePhase"]] >= phase_target[record["gamePhase"]]:
            continue
        if dimension_counts["result", rc] >= result_target[rc]:
            continue
        add(record)
    # Deterministic deficit-aware fallback; priorities are side, phase, result.
    while len(chosen) < count:
        candidates = [record for record in ranked if record["positionId"] not in chosen_ids and game_counts[record["gameId"]] < max_per_game]
        if not candidates:
            break
        def need(record: Mapping[str, Any]) -> tuple[int, int, int, str, str]:
            rc = result_class(float(record["resultFromSideToMove"]))
            return (
                side_target[record["sideToMove"]] - dimension_counts["side", record["sideToMove"]],
                phase_target[record["gamePhase"]] - dimension_counts["phase", record["gamePhase"]],
                result_target[rc] - dimension_counts["result", rc],
                stable_rank(seed, record), str(record["positionId"]),
            )
        candidates.sort(key=lambda record: (-need(record)[0], -need(record)[1], -need(record)[2], need(record)[3], need(record)[4]))
        add(candidates[0])
    if len(chosen) != count:
        raise TuningError(f"Could select only {len(chosen)}/{count} with max-per-game={max_per_game}")
    warnings = []
    for label, expected, actual in (
        *((f"side:{v}", side_target[v], dimension_counts["side", v]) for v in SIDES),
        *((f"phase:{v}", phase_target[v], dimension_counts["phase", v]) for v in PHASES),
        *((f"result:{v}", result_target[v], dimension_counts["result", v]) for v in RESULTS),
    ):
        if expected != actual:
            warnings.append(f"{label} target={expected} actual={actual} deficit={max(0, expected-actual)}")
    return chosen, warnings


def select_command(args: argparse.Namespace) -> None:
    dataset, output = Path(args.dataset_dir), Path(args.output_dir)
    validation = pgn_dataset.validate_dataset(dataset)
    manifest = validation["manifest"]
    positions = pgn_dataset.read_jsonl(dataset / "positions.jsonl")
    if not any(record["sideToMove"] == "white" for record in positions) or not any(record["sideToMove"] == "black" for record in positions):
        raise TuningError("Source dataset is one-sided; build a separate --sample-every 1 development dataset")
    if output.exists() and not args.force:
        raise TuningError(f"Output exists; use --force: {output}")
    selected: list[dict[str, Any]] = []
    warnings: list[str] = []
    requested = {"train": args.train_count, "validation": args.validation_count, "test": args.test_count}
    for split in SPLITS:
        records = [record for record in positions if record["split"] == split]
        split_selected, split_warnings = select_split(records, requested[split], args.max_per_game, args.seed + "\0" + split)
        selected.extend(split_selected); warnings.extend(f"{split}: {warning}" for warning in split_warnings)
    selection = []
    for index, record in enumerate(selected, 1):
        rc = result_class(float(record["resultFromSideToMove"]))
        selection.append({key: record[key] for key in (
            "positionId", "gameId", "split", "fen", "sideToMove", "gamePhase", "result",
            "resultFromSideToMove", "sourceFile", "sourceGameIndex", "ply",
        )} | {"resultClass": rc, "selectionRank": index, "stratum": f"{record['split']}|{record['gamePhase']}|{record['sideToMove']}|{rc}"})
    temp = Path(tempfile.mkdtemp(prefix=output.name + ".tmp-", dir=output.parent if output.parent.exists() else Path.cwd()))
    try:
        by_split = {split: [record for record in selection if record["split"] == split] for split in SPLITS}
        write(temp / "selection.jsonl", jsonl(selection))
        for split in SPLITS: write(temp / f"{split}.jsonl", jsonl(by_split[split]))
        summary = {"schemaVersion": 1, "overall": counts_summary(selection), "bySplit": {split: counts_summary(by_split[split]) for split in SPLITS}, "warnings": warnings}
        write(temp / "summary.json", pretty_json(summary).encode())
        artifacts = {name: {"sha256": sha256_file(temp / name), **({"records": len(selection) if name == "selection.jsonl" else len(by_split[name[:-6]])} if name.endswith(".jsonl") else {})} for name in ("selection.jsonl", "train.jsonl", "validation.jsonl", "test.jsonl", "summary.json")}
        selection_manifest = {
            "schemaVersion": 1, "selectionAlgorithmVersion": ALGORITHM_VERSION, "selectionSeed": args.seed,
            "maximumPositionsPerGame": args.max_per_game, "requestedCounts": requested,
            "actualCounts": {split: len(by_split[split]) for split in SPLITS},
            "sourceDataset": {"datasetId": manifest["datasetId"], "datasetVersion": manifest["datasetVersion"], "manifestSha256": sha256_file(dataset / "manifest.json"), "positionsSha256": sha256_file(dataset / "positions.jsonl")},
            "selectedPositionsSha256": artifacts["selection.jsonl"]["sha256"], "stratumCounts": dict(sorted(Counter(record["stratum"] for record in selection).items())),
            "warnings": warnings, "deficits": warnings, "artifacts": artifacts,
            "tool": {"name": "evaluation_tuning.py", "version": TOOL_VERSION},
        }
        write(temp / "manifest.json", pretty_json(selection_manifest).encode())
        if output.exists(): shutil.rmtree(output)
        output.parent.mkdir(parents=True, exist_ok=True); os.replace(temp, output)
    except Exception:
        shutil.rmtree(temp, ignore_errors=True); raise
    print(f"Selected train={args.train_count} validation={args.validation_count} test={args.test_count}")
    print(f"Selection SHA-256: {sha256_file(output/'selection.jsonl')}")


def solve(matrix: list[list[float]], vector: list[float]) -> list[float]:
    n = len(vector); augmented = [matrix[i][:] + [vector[i]] for i in range(n)]
    for column in range(n):
        pivot = max(range(column, n), key=lambda row: (abs(augmented[row][column]), -row))
        if abs(augmented[pivot][column]) < 1e-12:
            raise TuningError("Singular ridge system")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(n):
            if row == column: continue
            factor = augmented[row][column]
            augmented[row] = [left - factor * right for left, right in zip(augmented[row], augmented[column])]
    return [augmented[row][-1] for row in range(n)]


def ridge(rows: Sequence[Sequence[float]], targets_: Sequence[float], regularization: float) -> list[float]:
    size = len(ALLOWLIST); matrix = [[0.0] * size for _ in range(size)]; vector = [0.0] * size
    for row, target in zip(rows, targets_):
        for i in range(size):
            vector[i] += row[i] * target
            for j in range(size): matrix[i][j] += row[i] * row[j]
    for i in range(size): matrix[i][i] += regularization
    return solve(matrix, vector)


def coefficient(record: Mapping[str, Any], name: str) -> int:
    return evaluation_features.perturbed_score(record, name, "scalar", 1) - int(record["scores"]["finalWhiteCp"])


def predict_exact(record: Mapping[str, Any], deltas: Mapping[str, int]) -> int:
    scores = record["scores"]; mg = int(scores["middlegameWhiteCp"]); eg = int(scores["endgameWhiteCp"])
    for name, delta in deltas.items():
        raw = int(record["features"][name]["coefficients"].get("scalar", 0)) * delta
        affects_mg, affects_eg = evaluation_features.stage_for(name)
        if affects_mg: mg += raw
        if affects_eg: eg += raw
    phase = int(record["phase"]["clamped"])
    tapered = math.trunc((mg * phase + eg * (24 - phase)) / 24)
    no_pawn = math.trunc(tapered / 2) if int(scores["noPawnScaledWhiteCp"]) != int(scores["taperedWhiteCp"]) else tapered
    if scores["insufficientMaterialDraw"]: return 0
    return math.trunc(no_pawn * int(scores["lowMaterialScale"]) / 128)


def quantize(continuous_delta: float, baseline: int, registry: Mapping[str, Any], cap: int) -> tuple[int, dict[str, Any]]:
    step = int(registry["step"]); minimum = int(registry["minimum"]); maximum = int(registry["maximum"])
    continuous = baseline + continuous_delta
    units = (Decimal(str(continuous - minimum)) / Decimal(step)).quantize(Decimal("1"), rounding=ROUND_HALF_UP)
    raw_quantized = minimum + int(units) * step
    quantized = min(max(raw_quantized, minimum), maximum)
    candidate = min(max(quantized, baseline - cap), baseline + cap)
    # Ensure the cap/bounds result is still on the registry lattice.
    candidate = minimum + int(Decimal(str((candidate - minimum) / step)).quantize(Decimal("1"), rounding=ROUND_HALF_UP)) * step
    return candidate, {"baselineValue": baseline, "continuousValue": continuous, "quantizedValue": quantized, "candidateValue": candidate, "absoluteDelta": abs(candidate-baseline), "registryBounds": {"minimum": minimum, "maximum": maximum}, "registryStep": step, "prototypeCap": cap, "clamped": candidate != raw_quantized}


def correlation(xs: Sequence[float], ys: Sequence[float]) -> float | None:
    if len(xs) < 2: return None
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    numerator = sum((x-mx)*(y-my) for x,y in zip(xs,ys)); dx = sum((x-mx)**2 for x in xs); dy = sum((y-my)**2 for y in ys)
    return numerator / math.sqrt(dx*dy) if dx and dy else None


def metric(records: Sequence[Mapping[str, Any]], predictions: Sequence[int]) -> dict[str, Any]:
    targets_ = [int(record["stockfish"]["scoreWhiteCp"]) for record in records]
    errors = [prediction-target for prediction,target in zip(predictions,targets_)]
    absolute = [abs(error) for error in errors]
    return {"records": len(records), "meanSignedError": statistics.fmean(errors) if errors else None, "mae": statistics.fmean(absolute) if absolute else None, "rmse": math.sqrt(statistics.fmean(error*error for error in errors)) if errors else None, "medianAbsoluteError": statistics.median(absolute) if absolute else None, "correlation": correlation(predictions, targets_)}


def metric_report(records: Sequence[Mapping[str, Any]], deltas: Mapping[str, int]) -> dict[str, Any]:
    baseline = [int(record["scores"]["finalWhiteCp"]) for record in records]; candidate = [predict_exact(record,deltas) for record in records]
    def groups(key) -> dict[str, Any]:
        labels = sorted(set(key(record) for record in records)); result = {}
        for label in labels:
            indexes = [i for i,record in enumerate(records) if key(record)==label]; subset=[records[i] for i in indexes]
            result[label]={"baseline":metric(subset,[baseline[i] for i in indexes]),"candidate":metric(subset,[candidate[i] for i in indexes])}
        return result
    return {"baseline": metric(records,baseline), "candidate": metric(records,candidate), "breakdowns": {"gamePhase":groups(lambda r:r["gamePhase"]),"sideToMove":groups(lambda r:r["sideToMove"]),"resultClass":groups(lambda r: result_class(1.0 if (r["gameResult"]=="1-0" and r["sideToMove"]=="white") or (r["gameResult"]=="0-1" and r["sideToMove"]=="black") else 0.5 if r["gameResult"]=="1/2-1/2" else 0.0))}}


def artifact_info(directory: Path, names: Sequence[str]) -> dict[str, Any]:
    return {name: {"sha256": sha256_file(directory/name), **({"records": len(read_jsonl(directory/name))} if name.endswith(".jsonl") else {})} for name in names}


def fit_command(args: argparse.Namespace) -> None:
    feature_dir, baseline_path, output = Path(args.feature_dir), Path(args.baseline_profile), Path(args.output_dir)
    if output.exists() and not args.force: raise TuningError(f"Output exists; use --force: {output}")
    feature_manifest = read_json(feature_dir/"manifest.json")
    if feature_manifest.get("bitboard",{}).get("profileId") != "builtin-default-v1": raise TuningError("Features must use builtin-default-v1")
    records = [record for record in read_jsonl(feature_dir/"features.jsonl") if record.get("stockfish",{}).get("scoreType")=="cp"]
    coverage = len(records) / int(feature_manifest.get("selection",{}).get("selectedCount", len(records)))
    if coverage < 0.95: raise TuningError(f"CP coverage {coverage:.3%} is below 95%")
    by_split = {split:[record for record in records if record["split"]==split] for split in SPLITS}
    if not all(by_split.values()): raise TuningError("Every split needs CP records")
    baseline = read_json(baseline_path); validate_profile(baseline)
    registry_doc = read_json(Path(args.registry)); registry = {item["name"]:item for item in registry_doc["parameters"]}
    if DISCONNECTED in ALLOWLIST: raise TuningError("Disconnected parameter entered the allowlist")
    train_rows = [[float(coefficient(record,name)) for name in ALLOWLIST] for record in by_split["train"]]
    clipped = Counter(); train_targets=[]
    for record in by_split["train"]:
        original=int(record["stockfish"]["scoreWhiteCp"]); target=max(-args.cp_clip,min(args.cp_clip,original))
        if target != original: clipped["positive" if original>0 else "negative"] += 1
        train_targets.append(float(target-int(record["scores"]["finalWhiteCp"])))
    candidates=[]
    for regularization in DEFAULT_LAMBDAS:
        continuous=ridge(train_rows,train_targets,regularization); values={}; details={}
        for name,delta in zip(ALLOWLIST,continuous):
            base=int(baseline["parameters"][name]); candidate,detail=quantize(delta,base,registry[name],args.max_parameter_delta); values[name]=candidate; details[name]=detail
        deltas={name:values[name]-int(baseline["parameters"][name]) for name in ALLOWLIST}
        validation_metrics=metric_report(by_split["validation"],deltas)
        candidates.append({"lambda":regularization,"values":values,"details":details,"deltas":deltas,"validation":validation_metrics,"changed":sum(value!=0 for value in deltas.values()),"totalAbsoluteDelta":sum(abs(value) for value in deltas.values())})
    candidates.sort(key=lambda item:(item["validation"]["candidate"]["mae"],item["validation"]["candidate"]["rmse"],item["changed"],item["totalAbsoluteDelta"],-item["lambda"]))
    selected=candidates[0]
    if selected["changed"] == 0: raise TuningError("Prototype produced no quantized parameter changes")
    candidate_profile=copy.deepcopy(baseline); candidate_profile.update({"profileId":args.candidate_id,"parentProfileId":baseline["profileId"],"sourceBaseline":baseline["profileId"],"description":"Development-only Phase 15 evaluation tuning prototype."})
    for name,value in selected["values"].items(): candidate_profile["parameters"][name]=value
    unexpected=[name for name,value in candidate_profile["parameters"].items() if value!=baseline["parameters"][name] and name not in ALLOWLIST]
    if unexpected: raise TuningError(f"Unexpected changed parameters: {unexpected}")
    candidate_profile["canonicalHash"]=profile_hash(candidate_profile); validate_profile(candidate_profile)
    deltas=selected["deltas"]; metrics={"schemaVersion":1,"candidateFrozenBeforeTest":True,"splits":{split:metric_report(by_split[split],deltas) for split in SPLITS}}
    changed=[{"registryName":name,**selected["details"][name]} for name in ALLOWLIST if deltas[name]]
    temp=Path(tempfile.mkdtemp(prefix=output.name+".tmp-",dir=output.parent if output.parent.exists() else Path.cwd()))
    try:
        write(temp/"profile.json",pretty_json(candidate_profile).encode())
        metadata={"schemaVersion":1,"basedOnProfileId":baseline["profileId"],"basedOnProfileHash":baseline["canonicalHash"],"candidateProfileId":args.candidate_id,"candidateProfileHash":candidate_profile["canonicalHash"],"developmentOnly":True,"promotionEligible":False,"prototypeOutcome":"prototype_metric_improvement" if metrics["splits"]["validation"]["candidate"]["mae"] < metrics["splits"]["validation"]["baseline"]["mae"] else "prototype_no_validation_gain","selectionManifestChecksum":feature_manifest.get("selection",{}).get("explicitSelection",{}).get("manifestSha256"),"annotationManifestChecksum":feature_manifest["sourceAnnotations"]["manifestSha256"],"featureManifestChecksum":sha256_file(feature_dir/"manifest.json"),"featureDirectory":os.path.relpath(feature_dir.resolve(),Path.cwd().resolve()),"fittingMethod":FIT_METHOD,"regularization":selected["lambda"],"cpClippingLimit":args.cp_clip,"selectedParameters":list(ALLOWLIST),"changedParameters":[item["registryName"] for item in changed],"excludedParameters":[{"registryName":DISCONNECTED,"excludedReason":"disconnected_production_feature"}]}
        write(temp/"metadata.json",pretty_json(metadata).encode()); write(temp/"changed-parameters.json",pretty_json({"schemaVersion":1,"parameters":changed}).encode())
        fit_report={"schemaVersion":1,"method":FIT_METHOD,"lambdaGrid":list(DEFAULT_LAMBDAS),"selectedLambda":selected["lambda"],"cpClip":args.cp_clip,"prototypeDeltaCap":args.max_parameter_delta,"records":{"train":len(by_split["train"]),"validation":len(by_split["validation"]),"test":len(by_split["test"])},"clipping":{"recordsClipped":sum(clipped.values()),"positiveClips":clipped["positive"],"negativeClips":clipped["negative"]},"lambdaResults":[{"lambda":item["lambda"],"changedParameters":item["changed"],"totalAbsoluteDelta":item["totalAbsoluteDelta"],"validationMAE":item["validation"]["candidate"]["mae"],"validationRMSE":item["validation"]["candidate"]["rmse"]} for item in sorted(candidates,key=lambda x:x["lambda"])],"parameterDetails":selected["details"]}
        write(temp/"fit-report.json",pretty_json(fit_report).encode()); write(temp/"metrics.json",pretty_json(metrics).encode())
        names=("profile.json","metadata.json","fit-report.json","metrics.json","changed-parameters.json")
        manifest={"schemaVersion":1,"candidateProfileId":args.candidate_id,"candidateProfileHash":candidate_profile["canonicalHash"],"developmentOnly":True,"promotionEligible":False,"artifacts":artifact_info(temp,names),"tool":{"name":"evaluation_tuning.py","version":TOOL_VERSION}}
        write(temp/"manifest.json",pretty_json(manifest).encode())
        if output.exists(): shutil.rmtree(output)
        output.parent.mkdir(parents=True,exist_ok=True); os.replace(temp,output)
    except Exception:
        shutil.rmtree(temp,ignore_errors=True); raise
    print(f"Candidate: {args.candidate_id} {candidate_profile['canonicalHash']}")
    print(f"Selected lambda: {selected['lambda']}; changed parameters: {selected['changed']}")


def generate_header_command(args: argparse.Namespace) -> None:
    candidate=Path(args.candidate_dir); profile=read_json(candidate/"profile.json"); validate_profile(profile)
    output=Path(args.output); content=generate_header(profile)
    if output.exists() and args.check:
        if output.read_text(encoding="utf-8") != content: raise TuningError("Candidate header differs")
    elif args.check: raise TuningError("Candidate header is missing")
    else: write(output,content.encode())
    print(f"Candidate header: {output}")


def raw_engine(engine: Path, records: Sequence[Mapping[str,Any]]) -> list[dict[str,Any]]:
    process=subprocess.run([str(engine.resolve()),"--mode=eval-features"],input="".join(str(r["fen"])+"\n" for r in records),text=True,capture_output=True)
    if process.returncode: raise TuningError(f"Engine export failed: {process.stderr.strip()}")
    values=[json.loads(line) for line in process.stdout.splitlines() if line]
    if len(values)!=len(records): raise TuningError(f"Engine returned {len(values)}/{len(records)} feature records")
    return values


def validate_command(args: argparse.Namespace) -> None:
    candidate=Path(args.candidate_dir); metadata=read_json(candidate/"metadata.json"); changed=read_json(candidate/"changed-parameters.json")["parameters"]
    if metadata.get("developmentOnly") is not True or metadata.get("promotionEligible") is not False: raise TuningError("Candidate status is unsafe")
    feature_dir=Path(metadata["featureDirectory"]); records=[r for r in read_jsonl(feature_dir/"features.jsonl") if r.get("stockfish",{}).get("scoreType")=="cp"]
    baseline_values=raw_engine(Path(args.baseline_engine),records); candidate_values=raw_engine(Path(args.candidate_engine),records)
    deltas={item["registryName"]:item["candidateValue"]-item["baselineValue"] for item in changed}
    mismatches=[]; changed_positions=0; maximum=0
    for record,baseline,candidate_value in zip(records,baseline_values,candidate_values):
        expected=predict_exact(record,deltas); actual=int(candidate_value["scores"]["finalWhiteCp"]); actual_baseline=int(baseline["scores"]["finalWhiteCp"])
        mismatch=abs(expected-actual); maximum=max(maximum,mismatch)
        if mismatch or actual_baseline!=int(record["scores"]["finalWhiteCp"]): mismatches.append(record["positionId"])
        changed_positions += actual != actual_baseline
    report={"schemaVersion":1,"positionsChecked":len(records),"predictedVersusActualMismatches":len(mismatches),"maximumMismatch":maximum,"baselineVersusCandidateChangedPositions":changed_positions,"developmentOnly":True,"promotionEligible":False}
    write(candidate/"validation-report.json",pretty_json(report).encode())
    if mismatches: raise TuningError(f"Analytical verification failed for {len(mismatches)} positions")
    if not changed_positions: raise TuningError("Candidate changes no selected position")
    print(canonical_json(report))


def inspect_command(args: argparse.Namespace) -> None:
    candidate=Path(args.candidate_dir); manifest=read_json(candidate/"manifest.json"); metadata=read_json(candidate/"metadata.json")
    for name,item in manifest["artifacts"].items():
        if sha256_file(candidate/name)!=item["sha256"]: raise TuningError(f"Candidate artifact checksum mismatch: {name}")
    print(f"Candidate: {metadata['candidateProfileId']} {metadata['candidateProfileHash']}")
    print(f"Development-only: {metadata['developmentOnly']}; promotion eligible: {metadata['promotionEligible']}")


def parser() -> argparse.ArgumentParser:
    result=argparse.ArgumentParser(description=__doc__); commands=result.add_subparsers(dest="command",required=True)
    select=commands.add_parser("select"); select.add_argument("--dataset-dir",required=True); select.add_argument("--output-dir",required=True); select.add_argument("--train-count",type=int,default=700); select.add_argument("--validation-count",type=int,default=150); select.add_argument("--test-count",type=int,default=150); select.add_argument("--max-per-game",type=int,default=2); select.add_argument("--seed",default=DEFAULT_SEED); select.add_argument("--force",action="store_true"); select.set_defaults(function=select_command)
    fit=commands.add_parser("fit"); fit.add_argument("--feature-dir",required=True); fit.add_argument("--baseline-profile",default="tuning/profiles/builtin-default-v1.json"); fit.add_argument("--registry",default="tuning/parameter-registry.json"); fit.add_argument("--output-dir",required=True); fit.add_argument("--candidate-id",default="candidate-eval-prototype-0001"); fit.add_argument("--cp-clip",type=int,default=1500); fit.add_argument("--max-parameter-delta",type=int,default=20); fit.add_argument("--force",action="store_true"); fit.set_defaults(function=fit_command)
    inspect=commands.add_parser("inspect"); inspect.add_argument("--candidate-dir",required=True); inspect.set_defaults(function=inspect_command)
    header=commands.add_parser("generate-header"); header.add_argument("--candidate-dir",required=True); header.add_argument("--output",required=True); header.add_argument("--check",action="store_true"); header.set_defaults(function=generate_header_command)
    validate=commands.add_parser("validate"); validate.add_argument("--candidate-dir",required=True); validate.add_argument("--baseline-engine",required=True); validate.add_argument("--candidate-engine",required=True); validate.set_defaults(function=validate_command)
    return result


def main(argv: Sequence[str]|None=None) -> int:
    try:
        args=parser().parse_args(argv); args.function(args); return 0
    except (TuningError,pgn_dataset.DatasetError,evaluation_features.FeatureError,OSError,json.JSONDecodeError,subprocess.SubprocessError) as error:
        print(f"error: {error}",file=sys.stderr); return 2


if __name__ == "__main__": raise SystemExit(main())
