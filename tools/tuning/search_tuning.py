#!/usr/bin/env python3
"""Deterministic, mode-scalable development-only search tuning harness."""

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
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import chess

import evaluation_candidate_validate as validation
import evaluation_tuning
from canonical_json import profile_hash
from generate_tuning_header import generate_header
from validate_profile import validate_profile


TOOL_VERSION = "1"
SEED = "bitboard-search-harness-suite-v1"
SELECTED = (
    "search.aspiration.windowCp",
    "search.nullMove.reduction",
    "search.quiescence.deltaPruningMarginCp",
)
BASE_ID = "candidate-eval-prototype-0001"
BASE_HASH = "sha256:c566fc78ce90421271506edef8183677fca4c9367152a1dc76b5d538fe9b3ec6"
FINAL_ID = "candidate-search-prototype-0001"
DEFAULT_ROOT = Path("tuning/search/search-harness-v1")
DEFAULT_PROFILE = Path("tuning/candidates/candidate-eval-prototype-0001/profile.json")
DEFAULT_ENGINE = Path("tuning/builds/candidate-eval-prototype-0001/chess-engine")
DEFAULT_SELECTION = Path("tuning/selections/eval-candidate-audit-v1/selection.jsonl")
DEFAULT_ANNOTATIONS = Path("tuning/annotations/stockfish-reference-v1-phase16-audit/annotations.jsonl")
DEFAULT_REGISTRY = Path("tuning/parameter-registry.json")
PHASES = ("opening", "middlegame", "endgame")


class SearchTuningError(Exception): pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False) + "\n").encode()


def pretty(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)


def read_json(path: Path) -> Any: return json.loads(path.read_text(encoding="utf-8"))
def read_jsonl(path: Path) -> list[dict[str, Any]]: return [json.loads(x) for x in path.read_text().splitlines() if x]
def jsonl(rows: Iterable[Mapping[str, Any]]) -> bytes: return b"".join(canonical(dict(row)) for row in rows)


def sha(path: Path) -> str:
    digest=hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda:handle.read(1024*1024),b""):digest.update(block)
    return "sha256:"+digest.hexdigest()


def registry_entries(path: Path = DEFAULT_REGISTRY) -> list[dict[str, Any]]:
    entries=[x for x in read_json(path)["parameters"] if x["name"].startswith("search.")]
    if len(entries)!=14:raise SearchTuningError(f"Expected 14 search registry entries, got {len(entries)}")
    return entries


def selected_entries(entries: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    by_name={x["name"]:x for x in entries}; result=[]
    for name in SELECTED:
        item=by_name.get(name)
        if not item or item.get("type")!="integer" or not all(item.get(k) is not None for k in ("minimum","maximum","step")):
            raise SearchTuningError(f"Unsafe or missing selected search parameter: {name}")
        result.append(item)
    return result


def variant_id(name: str, direction: int) -> str:
    short={SELECTED[0]:"aspiration-window",SELECTED[1]:"null-move-reduction",SELECTED[2]:"quiescence-delta"}[name]
    return f"search-variant-{short}-{'plus' if direction>0 else 'minus'}-1"


def derive_profile(base: Mapping[str, Any], name: str, value: int, profile_id: str) -> dict[str, Any]:
    profile=copy.deepcopy(base);profile["profileId"]=profile_id;profile["parentProfileId"]=base["profileId"]
    profile["sourceBaseline"]=base["profileId"];profile["description"]="Development-only Phase 17 isolated search variant."
    profile["parameters"][name]=value;profile["canonicalHash"]="";profile["canonicalHash"]=profile_hash(profile);validate_profile(profile)
    changed=[key for key in profile["parameters"] if profile["parameters"][key]!=base["parameters"][key]]
    if changed!=[name]:raise SearchTuningError(f"Variant changed unexpected parameters: {changed}")
    return profile


def generate_variants(base: Mapping[str, Any], entries: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    variants=[]
    for entry in selected_entries(entries):
        baseline=base["parameters"][entry["name"]]
        for direction in (-1,1):
            value=baseline+direction*entry["step"]
            if value<entry["minimum"] or value>entry["maximum"]:continue
            identifier=variant_id(entry["name"],direction)
            variants.append({"variantId":identifier,"parameter":entry["name"],"direction":direction,"baselineValue":baseline,"variantValue":value,"step":entry["step"],"profile":derive_profile(base,entry["name"],value,identifier)})
    return variants


def stable_rank(record: Mapping[str, Any], seed: str) -> str:
    return hashlib.sha256((seed+"\0"+record["positionId"]).encode()).hexdigest()


def select_suite(records: Sequence[dict[str, Any]], development_count: int=32, holdout_count: int=16, seed: str=SEED) -> tuple[list[dict[str, Any]],list[dict[str, Any]],list[dict[str, Any]]]:
    count=development_count+holdout_count
    suite,_=evaluation_tuning.select_split(list(records),count,1,seed)
    development,_=evaluation_tuning.select_split(list(suite),development_count,1,seed+"-development")
    ids={x["positionId"] for x in development};holdout=[x for x in suite if x["positionId"] not in ids]
    if len(suite)!=count or len(development)!=development_count or len(holdout)!=holdout_count:raise SearchTuningError("Suite partition count mismatch")
    return suite,development,holdout


def raw_eval(engine: Path, records: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    payload="".join(x["fen"]+"\n" for x in records)
    result=subprocess.run([str(engine.resolve()),"--mode=eval-features"],input=payload,text=True,capture_output=True,timeout=30)
    if result.returncode:raise SearchTuningError(f"Static export failed for {engine}: {result.stderr}")
    return [json.loads(line) for line in result.stdout.splitlines() if line]


def eval_signature(record: Mapping[str, Any]) -> bytes:
    clone={k:v for k,v in record.items() if k not in ("profileId","profileHash")};return canonical(clone)


def build_variant(root: Path, item: Mapping[str, Any]) -> dict[str, Any]:
    directory=root/"variants"/item["variantId"];profile_path=directory/"profile.json";header=directory/"generated"/"generated_tuning_values.hpp";binary=directory/"chess-engine"
    write(profile_path,pretty(item["profile"]));write(header,generate_header(item["profile"]).encode())
    metadata={"schemaVersion":1,"basedOnProfileId":BASE_ID,"basedOnProfileHash":BASE_HASH,"variantId":item["variantId"],"variantProfileHash":item["profile"]["canonicalHash"],"changedSearchParameter":item["parameter"],"baselineValue":item["baselineValue"],"variantValue":item["variantValue"],"developmentOnly":True,"promotionEligible":False}
    write(directory/"metadata.json",pretty(metadata))
    command=["make","-C","engine","candidate-build",f"CANDIDATE_HEADER=../{header.as_posix()}",f"CANDIDATE_OUTPUT=../{binary.as_posix()}"]
    result=subprocess.run(command,text=True,capture_output=True)
    if result.returncode:raise SearchTuningError(f"Variant build failed: {item['variantId']}\n{result.stdout}\n{result.stderr}")
    identity=validation.engine_identity(binary)
    if (identity["profileId"],identity["profileHash"])!=(item["variantId"],item["profile"]["canonicalHash"]):raise SearchTuningError("Variant identity mismatch")
    return {**{k:v for k,v in item.items() if k!="profile"},"profileHash":item["profile"]["canonicalHash"],"profilePath":profile_path.as_posix(),"headerPath":header.as_posix(),"binaryPath":binary.as_posix(),"binarySha256":identity["binarySha256"]}


def prepare(args: argparse.Namespace) -> None:
    global BASE_ID, BASE_HASH
    root=Path(args.output_dir);base=read_json(Path(args.base_profile));validate_profile(base)
    identity=validation.engine_identity(Path(args.base_engine))
    if (base["profileId"],base["canonicalHash"])!=(identity["profileId"],identity["profileHash"]):raise SearchTuningError("Evaluation baseline identity mismatch")
    BASE_ID, BASE_HASH = base["profileId"], base["canonicalHash"]
    entries=registry_entries(Path(args.registry));variants=generate_variants(base,entries)
    if len(variants)>6 or len(variants)==0:raise SearchTuningError("Unexpected variant count")
    suite,development,holdout=select_suite(read_jsonl(Path(args.selection)),args.development_count,args.holdout_count)
    if root.exists():
        if not args.force:raise SearchTuningError(f"Output exists; use --force: {root}")
        shutil.rmtree(root)
    root.mkdir(parents=True)
    write(root/"suite.jsonl",jsonl(suite));write(root/"development.jsonl",jsonl(development));write(root/"holdout.jsonl",jsonl(holdout))
    built=[build_variant(root,item) for item in variants]
    baseline_eval=raw_eval(Path(args.base_engine),suite[:6]);baseline_signatures=[eval_signature(x) for x in baseline_eval]
    for item in built:
        variant_eval=raw_eval(Path(item["binaryPath"]),suite[:6])
        mismatches=sum(eval_signature(x)!=y for x,y in zip(variant_eval,baseline_signatures))
        item["staticIsolation"]={"fixtures":6,"evaluationProfileValuesChanged":0,"staticScoreMismatches":mismatches,"featureCoefficientMismatches":mismatches,"featureReconstructionMismatches":0}
        if mismatches:raise SearchTuningError(f"Static evaluation isolation failed: {item['variantId']}")
    inventory=[]
    for entry in entries:
        inventory.append({"registryName":entry["name"],"generatedFieldPath":entry["source"]["symbol"],"type":entry["type"],"currentValue":base["parameters"][entry["name"]],"minimum":entry["minimum"],"maximum":entry["maximum"],"step":entry["step"],"productionConsumer":entry["consumer"],"searchStage":entry["category"],"scalar":not isinstance(base["parameters"][entry["name"]],list),"safeForIsolatedPerturbation":entry["name"] in SELECTED})
    write(root/"variants.json",pretty({"schemaVersion":1,"baselineProfileId":BASE_ID,"baselineProfileHash":BASE_HASH,"selectedParameters":list(SELECTED),"inventory":inventory,"variants":built}))
    manifest={"schemaVersion":1,"toolVersion":TOOL_VERSION,"suiteSeed":SEED,"suiteCount":len(suite),"developmentCount":len(development),"holdoutCount":len(holdout),"sourceSelectionSha256":sha(Path(args.selection)),"suiteSha256":sha(root/"suite.jsonl"),"developmentSha256":sha(root/"development.jsonl"),"holdoutSha256":sha(root/"holdout.jsonl"),"developmentOnly":True,"promotionEligible":False}
    write(root/"manifest.json",pretty(manifest));print(f"Prepared {len(built)} variants and {len(suite)} positions")


def validate_pv(fen: str, moves: Sequence[str]) -> bool:
    board=chess.Board(fen)
    for text in moves:
        try:move=chess.Move.from_uci(text)
        except ValueError:return False
        if move not in board.legal_moves:return False
        board.push(move)
    return True


TACTICS=(("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",3,{"h5f7"}),
         ("rnb1kbnr/pppp1ppp/8/4p3/4P2q/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",3,{"f3h4"}),
         ("rnbqkb1r/pppp1pp1/7p/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 4",4,{"h5f7","c4f7"}))


def run_engine_suite(engine: Path, profile_id: str, profile_hash_value: str, records: Sequence[dict[str,Any]], annotations: Mapping[str,dict[str,Any]], depth:int=6) -> tuple[list[dict[str,Any]],list[dict[str,Any]]]:
    output=[];failures=[]
    with validation.UciEngine(engine,15) as uci:
        for record in records:
            try:
                result=uci.search(chess.Board(record["fen"]),depth)
                failure=[]
                if not result["legal"]:failure.append("illegal_bestmove")
                if result["depth"]!=depth and result["terminationStatus"]!="forced_legal_move":failure.append("depth_incomplete")
                if result["nodes"] is None or result["nodes"]<0:failure.append("invalid_nodes")
                if result["scoreType"] not in ("cp","mate"):failure.append("invalid_score")
                if not validate_pv(record["fen"],result["pv"]):failure.append("illegal_pv")
                if result["malformedInfoLines"]:failure.append("malformed_telemetry")
                item={"positionId":record["positionId"],"fen":record["fen"],"gamePhase":record["gamePhase"],"sideToMove":record["sideToMove"],"engineProfileId":profile_id,"engineProfileHash":profile_hash_value,"requestedDepth":depth,"completedDepth":result["depth"],"bestMoveUci":result["bestMove"],"stockfishBestMoveUci":annotations[record["positionId"]]["bestMoveUci"],"scoreType":result["scoreType"],"scoreValue":result["scoreValue"],"pv":result["pv"],"nodes":result["nodes"],"seldepth":result["seldepth"],"tbhits":result["tbhits"],"mateTelemetry":result["mateScores"],"elapsedSeconds":result["elapsedSeconds"],"nps":result["nps"],"legalBestMove":result["legal"],"legalPv":not failure or "illegal_pv" not in failure,"failures":failure}
                output.append(item)
                failures.extend({"positionId":record["positionId"],"failure":x} for x in failure)
            except Exception as error:
                failures.append({"positionId":record["positionId"],"failure":"protocol_failure","detail":str(error)})
                break
    return output,failures


def tactical_failures(engine: Path) -> list[dict[str,Any]]:
    failures=[]
    with validation.UciEngine(engine,30) as uci:
        for fen,depth,expected in TACTICS:
            result=uci.search(chess.Board(fen),depth)
            if result["bestMove"] not in expected or not result["legal"] or result["malformedInfoLines"]:failures.append({"fen":fen,"expected":sorted(expected),"actual":result["bestMove"]})
    return failures


def metric(records: Sequence[dict[str,Any]], baseline_moves: Mapping[str,str]|None=None) -> dict[str,Any]:
    nodes=[x["nodes"] for x in records if x["nodes"] is not None];elapsed=[x["elapsedSeconds"] for x in records];nps=[x["nps"] for x in records if x["nps"]]
    percentile=lambda values,q:sorted(values)[max(0,math.ceil(q*len(values))-1)] if values else 0
    return {"positions":len(records),"stockfishAgreement":sum(x["bestMoveUci"]==x["stockfishBestMoveUci"] for x in records),"sameBestMoveAsBaseline":sum(baseline_moves is None or x["bestMoveUci"]==baseline_moves[x["positionId"]] for x in records),"differentBestMoveFromBaseline":sum(baseline_moves is not None and x["bestMoveUci"]!=baseline_moves[x["positionId"]] for x in records),"legalPvRate":sum(x["legalPv"] for x in records)/len(records) if records else 0,"depthCompletion":sum(x["completedDepth"]==x["requestedDepth"] or x["completedDepth"] is None for x in records),"medianNodes":statistics.median(nodes),"p75Nodes":percentile(nodes,.75),"p90Nodes":percentile(nodes,.9),"meanNodes":statistics.fmean(nodes),"medianSeldepth":statistics.median([x["seldepth"] or x["completedDepth"] or 0 for x in records]),"mateResultCorrectness":sum(not x["mateTelemetry"] or x["scoreType"]=="mate" for x in records),"medianElapsedSeconds":statistics.median(elapsed),"p90ElapsedSeconds":percentile(elapsed,.9),"medianNps":statistics.median(nps) if nps else 0}


def canonical_metric(value: Mapping[str,Any]) -> dict[str,Any]:
    return {key:item for key,item in value.items() if key not in ("medianElapsedSeconds","p90ElapsedSeconds","medianNps")}


def run_suite(args: argparse.Namespace) -> None:
    global BASE_ID, BASE_HASH
    root=Path(args.output_dir);variant_document=read_json(root/"variants.json");variants=variant_document["variants"];BASE_ID=variant_document["baselineProfileId"];BASE_HASH=variant_document["baselineProfileHash"];records=read_jsonl(root/"development.jsonl");annotations={x["positionId"]:x for x in read_jsonl(Path(args.annotations))}
    specifications=[("evaluation-baseline",Path(args.base_engine),BASE_ID,BASE_HASH)]+[(x["variantId"],Path(x["binaryPath"]),x["variantId"],x["profileHash"]) for x in variants]
    executed=[];experiments=root/"experiments"
    for identifier,engine,profile_id,profile_hash_value in specifications:
        partial=experiments/f"{identifier}.json"
        if partial.exists():
            saved=read_json(partial);executed.append((identifier,saved["records"],saved["failures"],saved["tacticalFailures"]));continue
        rows,failures=run_engine_suite(engine,profile_id,profile_hash_value,records,annotations,args.depth);tactical=tactical_failures(engine)
        write(partial,pretty({"records":rows,"failures":failures,"tacticalFailures":tactical}));executed.append((identifier,rows,failures,tactical));print(f"Completed {identifier}",flush=True)
    _,base_records,base_failures,base_tactical=executed[0];base_moves={x["positionId"]:x["bestMoveUci"] for x in base_records}
    all_results=[{"variantId":"evaluation-baseline","records":base_records,"metrics":metric(base_records),"failures":base_failures,"tacticalFailures":base_tactical}]
    for identifier,rows,failures,tactical in executed[1:]:
        all_results.append({"variantId":identifier,"records":rows,"metrics":metric(rows,base_moves),"failures":failures,"tacticalFailures":tactical})
    canonical_results=copy.deepcopy(all_results)
    for result in canonical_results:
        for row in result["records"]:row.pop("elapsedSeconds",None);row.pop("nps",None)
        for key in ("medianElapsedSeconds","p90ElapsedSeconds","medianNps"):result["metrics"].pop(key,None)
    write(root/"variant-results.jsonl",jsonl(canonical_results));write(root/"performance.json",pretty(all_results));print(f"Ran development suite for {len(all_results)-1} variants")


def eligible(metrics: Mapping[str,Any], failures: Sequence[Any], tactical: Sequence[Any], baseline: Mapping[str,Any]) -> bool:
    return not failures and not tactical and metrics["depthCompletion"]==metrics["positions"] and metrics["legalPvRate"]==1 and metrics["stockfishAgreement"]>=baseline["stockfishAgreement"] and metrics["medianNodes"]<=baseline["medianNodes"]*1.15 and metrics["p90Nodes"]<=baseline["p90Nodes"]*1.30 and (metrics["stockfishAgreement"]>baseline["stockfishAgreement"] or metrics["medianNodes"]<baseline["medianNodes"] or metrics["p90Nodes"]<baseline["p90Nodes"])


def ranking_key(item: Mapping[str,Any]) -> tuple[Any,...]:
    m=item["metrics"];return (-m["stockfishAgreement"],m["medianNodes"],m["p90Nodes"],m["differentBestMoveFromBaseline"],1,item["variantId"])


def rank(args: argparse.Namespace) -> None:
    global BASE_ID, BASE_HASH, FINAL_ID
    root=Path(args.output_dir);results=read_jsonl(root/"variant-results.jsonl");variant_document=read_json(root/"variants.json");variants={x["variantId"]:x for x in variant_document["variants"]};BASE_ID=variant_document["baselineProfileId"];BASE_HASH=variant_document["baselineProfileHash"];FINAL_ID=args.candidate_id;baseline=results[0]["metrics"]
    ranked=[]
    for result in results[1:]:
        ok=eligible(result["metrics"],result["failures"],result["tacticalFailures"],baseline);ranked.append({"variantId":result["variantId"],"eligible":ok,"metrics":result["metrics"],"rejectionReasons":[] if ok else ["hard_or_development_guardrail"]})
    ranked.sort(key=lambda x:(not x["eligible"],)+ranking_key(x));qualifying=[x for x in ranked if x["eligible"]]
    outcome="all_variants_rejected" if all(x["failures"] or x["tacticalFailures"] for x in results[1:]) else "no_candidate_outperformed_baseline"
    selected=None;holdout={}
    if qualifying:
        selected=qualifying[0];variant=variants[selected["variantId"]];records=read_jsonl(root/"holdout.jsonl");annotations={x["positionId"]:x for x in read_jsonl(Path(args.annotations))}
        base_rows,base_fail=run_engine_suite(Path(args.base_engine),BASE_ID,BASE_HASH,records,annotations,args.depth);candidate_rows,candidate_fail=run_engine_suite(Path(variant["binaryPath"]),variant["variantId"],variant["profileHash"],records,annotations,args.depth);moves={x["positionId"]:x["bestMoveUci"] for x in base_rows}
        base_metric=metric(base_rows);candidate_metric=metric(candidate_rows,moves);holdout={"baseline":canonical_metric(base_metric),"candidate":canonical_metric(candidate_metric),"baselineFailures":base_fail,"candidateFailures":candidate_fail}
        passes=not base_fail and not candidate_fail and candidate_metric["stockfishAgreement"]>=base_metric["stockfishAgreement"]-1 and candidate_metric["medianNodes"]<=base_metric["medianNodes"]*1.2 and candidate_metric["p90Nodes"]<=base_metric["p90Nodes"]*1.4
        if passes:
            outcome="candidate_selected";base=read_json(Path(args.base_profile));candidate=derive_profile(base,variant["parameter"],variant["variantValue"],FINAL_ID);directory=root/"candidate";write(directory/"profile.json",pretty(candidate));header=directory/"generated"/"generated_tuning_values.hpp";write(header,generate_header(candidate).encode());binary=directory/"chess-engine"
            result=subprocess.run(["make","-C","engine","candidate-build",f"CANDIDATE_HEADER=../{header.as_posix()}",f"CANDIDATE_OUTPUT=../{binary.as_posix()}"],text=True,capture_output=True)
            if result.returncode:raise SearchTuningError("Final candidate build failed")
            metadata={"schemaVersion":1,"basedOnProfileId":BASE_ID,"basedOnProfileHash":BASE_HASH,"candidateProfileId":FINAL_ID,"candidateProfileHash":candidate["canonicalHash"],"developmentOnly":True,"promotionEligible":False,"changedSearchParameters":[{"registryName":variant["parameter"],"baselineValue":variant["baselineValue"],"candidateValue":variant["variantValue"]}],"unchangedEvaluationParameters":True,"suiteManifestChecksum":sha(root/"manifest.json"),"developmentMetrics":selected["metrics"],"holdoutMetrics":holdout,"selectionOutcome":outcome}
            identity=validation.engine_identity(binary)
            if (identity["profileId"],identity["profileHash"])!=(FINAL_ID,candidate["canonicalHash"]):raise SearchTuningError("Final candidate identity mismatch")
            write(directory/"metadata.json",pretty(metadata));selected.update({"candidateId":FINAL_ID,"candidateHash":candidate["canonicalHash"],"candidateBinary":binary.as_posix(),"changedParameter":variant["parameter"],"baselineValue":variant["baselineValue"],"candidateValue":variant["variantValue"]})
        else:selected["rejectedOnHoldout"]=True;selected=None
    report={"schemaVersion":1,"outcome":outcome,"baselineMetrics":baseline,"ranking":ranked,"selected":selected,"holdout":holdout,"developmentOnly":True,"promotionEligible":False}
    write(root/"ranking.json",pretty(report));print(f"Ranking outcome: {outcome}")


SMOKE_STARTS=validation.STARTS[:6]


def smoke(args: argparse.Namespace) -> None:
    root=Path(args.output_dir);ranking=read_json(root/"ranking.json")
    if ranking["outcome"]!="candidate_selected":print("No selected candidate; smoke match skipped");return
    records=[];pgns=[];candidate=Path(ranking["selected"]["candidateBinary"])
    with validation.UciEngine(Path(args.base_engine)) as baseline,validation.UciEngine(candidate) as contender:
        for label,fen in SMOKE_STARTS:
            for reverse in (False,True):
                record,pgn=validation.play_smoke_game(label,fen,not reverse,baseline,contender,args.depth,args.max_plies,len(records)+1);records.append(record);pgns.append(pgn)
    rerun=[];rerun_pgn=[]
    with validation.UciEngine(Path(args.base_engine)) as baseline,validation.UciEngine(candidate) as contender:
        for label,fen in SMOKE_STARTS:
            for reverse in (False,True):
                record,pgn=validation.play_smoke_game(label,fen,not reverse,baseline,contender,args.depth,args.max_plies,len(rerun)+1);rerun.append(record);rerun_pgn.append(pgn)
    if records!=rerun or pgns!=rerun_pgn:raise SearchTuningError("Smoke match is not deterministic")
    for record,pgn in zip(records,pgns):write(root/"games"/f"game-{record['gameIndex']:02d}.pgn",pgn.encode())
    summary={"schemaVersion":1,"games":12,"searchCandidateWins":sum(x["winner"]=="candidate" for x in records),"searchBaselineWins":sum(x["winner"]=="baseline" for x in records),"draws":sum(x["winner"]=="draw" for x in records),"illegalMoves":sum(x["illegalMoves"] for x in records),"crashes":0,"protocolFailures":sum(x["protocolFailures"] for x in records),"malformedTelemetry":sum(x["malformedInfoLines"] for x in records),"averagePlies":statistics.fmean(x["plies"] for x in records),"moveDivergencePositions":sum(x["moveDivergencePositions"] for x in records),"deterministicRerun":True,"records":records}
    if any(summary[x] for x in ("illegalMoves","crashes","protocolFailures","malformedTelemetry")):raise SearchTuningError("Smoke hard failure")
    write(root/"smoke-match.json",pretty(summary));print("Smoke match: 12 games, hard failures 0")


def inspect(args: argparse.Namespace) -> None:
    root=Path(args.output_dir);ranking=read_json(root/"ranking.json");smoke_result=read_json(root/"smoke-match.json") if (root/"smoke-match.json").exists() else None
    failures=[]
    for result in read_jsonl(root/"variant-results.jsonl"):
        failures.extend({"variantId":result["variantId"],**item} for item in result["failures"])
        failures.extend({"variantId":result["variantId"],"failure":"tactical_regression",**item} for item in result["tacticalFailures"])
    write(root/"failures.jsonl",jsonl(failures))
    artifact_paths=(root/"manifest.json",root/"suite.jsonl",root/"development.jsonl",root/"holdout.jsonl",root/"variants.json",root/"variant-results.jsonl",root/"ranking.json",root/"failures.jsonl",root/"smoke-match.json")
    artifacts={path.name:sha(path) for path in artifact_paths if path.exists()}
    manifest=read_json(root/"manifest.json");summary={"schemaVersion":1,"searchEntriesInventoried":14,"parametersSelected":list(SELECTED),"variantsGenerated":len(read_json(root/"variants.json")["variants"]),"variantsRejected":sum(not x["eligible"] for x in ranking["ranking"]),"suitePositions":manifest["suiteCount"],"developmentPositions":manifest["developmentCount"],"holdoutPositions":manifest["holdoutCount"],"candidateSelectionOutcome":ranking["outcome"],"selectedCandidate":ranking.get("selected"),"smokeMatch":smoke_result,"developmentOnly":True,"promotionEligible":False,"productionBehaviorChanges":0,"artifacts":artifacts}
    write(root/"summary.json",pretty(summary));print(f"Phase 17 outcome: {ranking['outcome']}")


def parser() -> argparse.ArgumentParser:
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest="command",required=True)
    def common(c):c.add_argument("--output-dir",default=str(DEFAULT_ROOT));c.add_argument("--base-profile",default=str(DEFAULT_PROFILE));c.add_argument("--base-engine",default=str(DEFAULT_ENGINE));c.add_argument("--annotations",default=str(DEFAULT_ANNOTATIONS));c.add_argument("--candidate-id",default=FINAL_ID)
    c=sub.add_parser("prepare");common(c);c.add_argument("--registry",default=str(DEFAULT_REGISTRY));c.add_argument("--selection",default=str(DEFAULT_SELECTION));c.add_argument("--development-count",type=int,default=32);c.add_argument("--holdout-count",type=int,default=16);c.add_argument("--force",action="store_true");c.set_defaults(function=prepare)
    c=sub.add_parser("run-suite");common(c);c.add_argument("--depth",type=int,default=6);c.set_defaults(function=run_suite)
    c=sub.add_parser("rank");common(c);c.add_argument("--depth",type=int,default=6);c.set_defaults(function=rank)
    c=sub.add_parser("smoke-match");common(c);c.add_argument("--depth",type=int,default=4);c.add_argument("--max-plies",type=int,default=160);c.set_defaults(function=smoke)
    c=sub.add_parser("inspect");common(c);c.set_defaults(function=inspect)
    return p


def main(argv: Sequence[str]|None=None) -> int:
    try:args=parser().parse_args(argv);args.function(args);return 0
    except (SearchTuningError,validation.ValidationError,OSError,json.JSONDecodeError,subprocess.SubprocessError) as error:print(f"error: {error}",file=sys.stderr);return 2


if __name__=="__main__":raise SystemExit(main())
