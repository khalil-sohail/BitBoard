#!/usr/bin/env python3
"""Phase 18 deterministic, development-only time-management tuning harness."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import chess
import chess.pgn

import evaluation_candidate_validate as validation
import evaluation_tuning
import search_tuning
from canonical_json import profile_hash
from generate_tuning_header import generate_header
from validate_profile import validate_profile

TOOL_VERSION="1"
SEED="bitboard-time-harness-suite-v1"
SELECTED=("time.expectedMovesBase","time.safetyReserveMs","time.criticalLowTimeThresholdMs")
BASE_ID="candidate-search-prototype-0001"
BASE_HASH="sha256:9c3e25c7da756d88fce2ad75340aa2d94bc6baf47b44c25c76bbc82317e2531f"
FINAL_ID="candidate-time-prototype-0001"
DEFAULT_ROOT=Path("tuning/time/time-harness-v1")
DEFAULT_PROFILE=Path("tuning/search/search-harness-v1/candidate/profile.json")
DEFAULT_ENGINE=Path("tuning/search/search-harness-v1/candidate/chess-engine")
DEFAULT_SELECTION=Path("tuning/selections/eval-candidate-audit-v1/selection.jsonl")
DEFAULT_ANNOTATIONS=Path("tuning/annotations/stockfish-reference-v1-phase16-audit/annotations.jsonl")
DEFAULT_REGISTRY=Path("tuning/parameter-registry.json")
PHASES=("opening","middlegame","endgame")
BOUNDARY_CLOCKS=(0,1,5,10,20,30,35,38,39,40,41,42,45,50,60,75,100,150,250,500,1000,3000,10000,60000)

class TimeTuningError(Exception):pass
def canonical(v:Any)->bytes:return (json.dumps(v,sort_keys=True,separators=(",",":"),ensure_ascii=False,allow_nan=False)+"\n").encode()
def pretty(v:Any)->bytes:return (json.dumps(v,sort_keys=True,indent=2,ensure_ascii=False,allow_nan=False)+"\n").encode()
def write(p:Path,b:bytes)->None:p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
def read_json(p:Path)->Any:return json.loads(p.read_text())
def read_jsonl(p:Path)->list[dict[str,Any]]:return [json.loads(x) for x in p.read_text().splitlines() if x]
def jsonl(rows:Iterable[Mapping[str,Any]])->bytes:return b"".join(canonical(dict(x)) for x in rows)
def sha(p:Path)->str:return "sha256:"+hashlib.sha256(p.read_bytes()).hexdigest()

def registry_entries(path:Path=DEFAULT_REGISTRY)->list[dict[str,Any]]:
    rows=[x for x in read_json(path)["parameters"] if x["name"].startswith("time.")]
    if len(rows)!=13:raise TimeTuningError(f"Expected 13 time entries, got {len(rows)}")
    return rows

def selected_entries(entries:Sequence[dict[str,Any]])->list[dict[str,Any]]:
    by={x["name"]:x for x in entries};result=[]
    for name in SELECTED:
        item=by.get(name)
        if not item or item.get("type")!="integer" or any(item.get(x) is None for x in ("minimum","maximum","step")):raise TimeTuningError(f"Unsafe selected time parameter: {name}")
        result.append(item)
    return result

def variant_id(name:str,direction:int)->str:
    short={SELECTED[0]:"expected-moves-base",SELECTED[1]:"safety-reserve",SELECTED[2]:"critical-threshold"}[name]
    return f"time-variant-{short}-{'plus' if direction>0 else 'minus'}-1"

def derive_profile(base:Mapping[str,Any],name:str,value:int,identifier:str)->dict[str,Any]:
    profile=copy.deepcopy(base);profile["profileId"]=identifier;profile["parentProfileId"]=base["profileId"];profile["sourceBaseline"]=base["profileId"];profile["description"]="Development-only Phase 18 isolated time-policy variant.";profile["parameters"][name]=value;profile["canonicalHash"]="";profile["canonicalHash"]=profile_hash(profile);validate_profile(profile)
    changed=[x for x in profile["parameters"] if profile["parameters"][x]!=base["parameters"][x]]
    if changed!=[name]:raise TimeTuningError(f"Unexpected variant changes: {changed}")
    return profile

def generate_variants(base:Mapping[str,Any],entries:Sequence[dict[str,Any]])->list[dict[str,Any]]:
    result=[]
    for entry in selected_entries(entries):
        baseline=base["parameters"][entry["name"]]
        for direction in (-1,1):
            value=baseline+direction*entry["step"]
            if entry["minimum"]<=value<=entry["maximum"]:
                identifier=variant_id(entry["name"],direction);result.append({"variantId":identifier,"parameter":entry["name"],"baselineValue":baseline,"variantValue":value,"step":entry["step"],"direction":direction,"profile":derive_profile(base,entry["name"],value,identifier)})
    return result

def request(remaining:int,increment:int=0,moves:int|None=None,unstable:bool=False,fullmove:int=30,swing:int=0)->dict[str,Any]:
    return {"remainingTimeMs":remaining,"incrementMs":increment,"movesToGo":moves,"fullmoveNumber":fullmove,"bestMoveUnstable":unstable,"bestMoveChangeCount":1 if unstable else 0,"scoreSwingCp":swing}

def query_policy(engine:Path,requests:Sequence[dict[str,Any]])->list[dict[str,Any]]:
    payload="".join(json.dumps(x,separators=(",",":"))+"\n" for x in requests)
    run=subprocess.run([str(engine.resolve()),"--mode=time-policy"],input=payload,text=True,capture_output=True,timeout=30)
    if run.returncode or run.stderr.strip():raise TimeTuningError(f"time-policy failed for {engine}: {run.stderr.strip()}")
    rows=[json.loads(x) for x in run.stdout.splitlines() if x]
    if len(rows)!=len(requests):raise TimeTuningError("time-policy response count mismatch")
    return rows

def phase18_base_engine(root:Path,configured:Path)->Path:
    local=root/"baseline"/"chess-engine"
    return local if local.exists() else configured

def violations(row:Mapping[str,Any])->list[str]:
    out=[];remaining=row["remainingTimeMs"]
    if row["softLimitMs"]<0 or row["hardLimitMs"]<0 or row["reserveMs"]<0:out.append("negative_duration")
    if row["softLimitMs"]>row["hardLimitMs"]:out.append("soft_exceeds_hard")
    if row["hardLimitMs"]>row["maximumSpendMs"]:out.append("hard_exceeds_maximum")
    if remaining>0 and row["hardLimitMs"]>=remaining:out.append("hard_reaches_clock")
    if remaining>0 and row["maximumSpendMs"]>remaining:out.append("maximum_exceeds_clock")
    return out

def decorate(rows:Sequence[dict[str,Any]],requests:Sequence[dict[str,Any]])->list[dict[str,Any]]:
    result=[]
    for req,row in zip(requests,rows):
        remaining=req["remainingTimeMs"]
        result.append({**req,**row,"softClockRatio":row["softLimitMs"]/remaining if remaining else 0,"hardClockRatio":row["hardLimitMs"]/remaining if remaining else 0,"invariantViolations":violations(row)})
    return result

def characterization_requests()->list[dict[str,Any]]:
    return [request(clock,inc,moves,unstable,30,100 if unstable else 0) for clock in BOUNDARY_CLOCKS for inc in (0,10,100,1000) for moves in (None,1,5,10,20,40) for unstable in (False,True)]

def scenario_requests()->list[dict[str,Any]]:
    clocks=(1,20,30,35,38,39,40,41,42,45,50,60,75,100,250,500,1000,3000,10000,30000,60000)
    return [request(clock,(0,10,100,1000)[i%4],(None,1,5,10,20,40)[i%6],bool(i%2),10+i%40,100 if i%2 else 0) for i,clock in enumerate(clocks*4)]

def continuity(rows:Sequence[dict[str,Any]])->dict[str,Any]:
    by={x["remainingTimeMs"]:x for x in rows};jumps=[]
    for clock in range(2,101):
        a,b=by[clock-1],by[clock];jumps.append({"fromMs":clock-1,"toMs":clock,"hardDeltaMs":b["hardLimitMs"]-a["hardLimitMs"],"ratioDelta":b["hardClockRatio"]-a["hardClockRatio"],"criticalTransition":a["criticalLowTime"]!=b["criticalLowTime"]})
    return {"maximumAbsoluteHardJumpMs":max(abs(x["hardDeltaMs"]) for x in jumps),"maximumAbsoluteRatioJump":max(abs(x["ratioDelta"]) for x in jumps),"warnings":[x for x in jumps if abs(x["hardDeltaMs"])>5 or abs(x["ratioDelta"])>.15 or x["criticalTransition"]],"jumps":jumps}

def characterize(args:argparse.Namespace)->None:
    root=Path(args.output_dir);engine=phase18_base_engine(root,Path(args.base_engine));reqs=characterization_requests();rows=decorate(query_policy(engine,reqs),reqs)
    continuity_reqs=[request(x) for x in range(1,101)];continuous=decorate(query_policy(engine,continuity_reqs),continuity_reqs)
    basic={x["remainingTimeMs"]:x for x in continuous};report={"schemaVersion":1,"requests":len(reqs),"records":rows,"continuity":continuity(continuous),"requestedTransitions":{f"{a}-{b}":{"before":basic[a],"after":basic[b]} for a,b in ((39,40),(40,41),(50,60),(69,70))}}
    write(root/"scenarios"/"baseline-characterization.json",pretty(report));print(f"Characterized {len(rows)} baseline policy inputs")

def build(root:Path,item:Mapping[str,Any])->dict[str,Any]:
    directory=root/"variants"/item["variantId"];profile=directory/"profile.json";header=directory/"generated"/"generated_tuning_values.hpp";binary=directory/"chess-engine"
    write(profile,pretty(item["profile"]));write(header,generate_header(item["profile"]).encode());write(directory/"metadata.json",pretty({"schemaVersion":1,"basedOnProfileId":BASE_ID,"basedOnProfileHash":BASE_HASH,"developmentOnly":True,"promotionEligible":False,"changedTimeParameter":item["parameter"],"baselineValue":item["baselineValue"],"variantValue":item["variantValue"]}))
    run=subprocess.run(["make","-C","engine","candidate-build",f"CANDIDATE_HEADER=../{header}",f"CANDIDATE_OUTPUT=../{binary}"],text=True,capture_output=True)
    if run.returncode:raise TimeTuningError(f"Build failed: {item['variantId']}\n{run.stderr}")
    identity=validation.engine_identity(binary)
    if (identity["profileId"],identity["profileHash"])!=(item["variantId"],item["profile"]["canonicalHash"]):raise TimeTuningError("Variant identity mismatch")
    return {**{k:v for k,v in item.items() if k!="profile"},"profileHash":item["profile"]["canonicalHash"],"profilePath":str(profile),"headerPath":str(header),"binaryPath":str(binary),"binarySha256":identity["binarySha256"]}

def select_suite(records:Sequence[dict[str,Any]])->tuple[list[dict[str,Any]],list[dict[str,Any]],list[dict[str,Any]]]:
    suite,_=evaluation_tuning.select_split(list(records),36,1,SEED);development,_=evaluation_tuning.select_split(suite,24,1,SEED+"-development");ids={x["positionId"] for x in development};holdout=[x for x in suite if x["positionId"] not in ids]
    if (len(suite),len(development),len(holdout))!=(36,24,12):raise TimeTuningError("Suite partition mismatch")
    return suite,development,holdout

def fixed_signature(engine:Path,records:Sequence[dict[str,Any]])->list[tuple[Any,...]]:
    result=[]
    with validation.UciEngine(engine) as uci:
        for row in records:
            x=uci.search(chess.Board(row["fen"]),4);result.append((x["bestMove"],x["depth"],x["nodes"],x["scoreType"],x["scoreValue"],tuple(x["pv"])))
    return result

def prepare(args:argparse.Namespace)->None:
    root=Path(args.output_dir);base=read_json(Path(args.base_profile));validate_profile(base)
    if (base["profileId"],base["canonicalHash"])!=(BASE_ID,BASE_HASH):raise TimeTuningError("Search baseline identity mismatch")
    entries=registry_entries(Path(args.registry));variants=generate_variants(base,entries);suite,development,holdout=select_suite(read_jsonl(Path(args.selection)))
    if root.exists():
        if not args.force:raise TimeTuningError(f"Output exists; use --force: {root}")
        shutil.rmtree(root)
    write(root/"suite.jsonl",jsonl(suite));write(root/"development.jsonl",jsonl(development));write(root/"holdout.jsonl",jsonl(holdout))
    baseline_dir=root/"baseline";baseline_profile=baseline_dir/"profile.json";baseline_header=baseline_dir/"generated"/"generated_tuning_values.hpp";baseline_binary=baseline_dir/"chess-engine";write(baseline_profile,pretty(base));write(baseline_header,generate_header(base).encode());baseline_build=subprocess.run(["make","-C","engine","candidate-build",f"CANDIDATE_HEADER=../{baseline_header}",f"CANDIDATE_OUTPUT=../{baseline_binary}"],text=True,capture_output=True)
    if baseline_build.returncode:raise TimeTuningError("Phase 18 local baseline build failed")
    built=[build(root,x) for x in variants];fixtures=suite[:3];baseline_eval=search_tuning.raw_eval(baseline_binary,fixtures);baseline_fixed=fixed_signature(baseline_binary,fixtures)
    for item in built:
        eval_mismatch=sum(search_tuning.eval_signature(a)!=search_tuning.eval_signature(b) for a,b in zip(search_tuning.raw_eval(Path(item["binaryPath"]),fixtures),baseline_eval));fixed_mismatch=sum(a!=b for a,b in zip(fixed_signature(Path(item["binaryPath"]),fixtures),baseline_fixed));item["isolation"]={"fixtures":3,"evaluationValuesChanged":0,"searchValuesChanged":0,"openingValuesChanged":0,"staticEvaluationMismatches":eval_mismatch,"fixedDepthSearchMismatches":fixed_mismatch}
        if eval_mismatch or fixed_mismatch:raise TimeTuningError(f"Isolation failed: {item['variantId']}")
    inventory=[{"registryName":x["name"],"generatedFieldPath":x["source"]["symbol"],"type":x["type"],"currentValue":base["parameters"][x["name"]],"minimum":x["minimum"],"maximum":x["maximum"],"step":x["step"],"consumer":x["consumer"],"formulaRole":x["connectivity"],"scalar":not isinstance(base["parameters"][x["name"]],list),"safeForOneStepPerturbation":x["name"] in SELECTED} for x in entries]
    write(root/"variants.json",pretty({"schemaVersion":1,"inventory":inventory,"selectedParameters":list(SELECTED),"variants":built}));write(root/"manifest.json",pretty({"schemaVersion":1,"toolVersion":TOOL_VERSION,"seed":SEED,"suiteCount":36,"developmentCount":24,"holdoutCount":12,"phase18BaselineBinary":str(baseline_binary),"suiteSha256":sha(root/"suite.jsonl"),"developmentSha256":sha(root/"development.jsonl"),"holdoutSha256":sha(root/"holdout.jsonl"),"developmentOnly":True,"promotionEligible":False}));print(f"Prepared {len(built)} time variants and 36 positions")

def policy_metrics(rows:Sequence[dict[str,Any]],engine:Path)->dict[str,Any]:
    continuous_req=[request(x) for x in range(1,101)];continuous=decorate(query_policy(engine,continuous_req),continuous_req);cont=continuity(continuous);critical=[x for x in continuous if 39<=x["remainingTimeMs"]<=60]
    return {"scenarioCount":len(rows),"invariantViolations":sum(len(x["invariantViolations"]) for x in rows),"maximumAbsoluteHardJumpMs":cont["maximumAbsoluteHardJumpMs"],"maximumAbsoluteRatioJump":cont["maximumAbsoluteRatioJump"],"continuityWarnings":len(cont["warnings"]),"meanCriticalHardRatio":statistics.fmean(x["hardClockRatio"] for x in critical),"hardAt60Ms":next(x["hardLimitMs"] for x in continuous if x["remainingTimeMs"]==60),"criticalTransitions":[x for x in cont["warnings"] if x["criticalTransition"]]}

def run_scenarios(args:argparse.Namespace)->None:
    root=Path(args.output_dir);variants=read_json(root/"variants.json")["variants"];reqs=scenario_requests();specs=[("time-baseline",phase18_base_engine(root,Path(args.base_engine)))]+[(x["variantId"],Path(x["binaryPath"])) for x in variants];results=[]
    for identifier,engine in specs:
        rows=decorate(query_policy(engine,reqs),reqs);results.append({"variantId":identifier,"records":rows,"metrics":policy_metrics(rows,engine)});print(f"Policy scenarios complete: {identifier}")
    write(root/"scenarios"/"policy-results.jsonl",jsonl(results));print(f"Ran {len(reqs)} scenarios for baseline and {len(variants)} variants")

CLOCK_CASES=((60000,1000,None,"normal"),(3000,100,None,"short"),(100,0,None,"critical"),(60,0,None,"very-critical"),(39,0,None,"threshold-39"),(40,0,None,"threshold-40"),(41,0,None,"threshold-41"))
def run_timed(engine:Path,records:Sequence[dict[str,Any]],annotations:Mapping[str,dict[str,Any]])->tuple[list[dict[str,Any]],list[dict[str,Any]]]:
    rows=[];failures=[]
    with validation.UciEngine(engine,10) as uci:
        for index,record in enumerate(records):
            clock,inc,moves,label=CLOCK_CASES[index%len(CLOCK_CASES)];board=chess.Board(record["fen"]);policy=query_policy(engine,[request(clock,inc,moves,False,board.fullmove_number)])[0]
            try:
                result=uci.search_clock(board,clock,clock,inc,inc,moves);elapsed_ms=round(result["elapsedSeconds"]*1000,3);after=clock-elapsed_ms+inc;allowance=max(10,policy["hardLimitMs"]*.10);overrun=elapsed_ms>policy["hardLimitMs"]+allowance
                failure=[]
                if not result["legal"]:failure.append("illegal_bestmove")
                if result["malformedInfoLines"]:failure.append("malformed_telemetry")
                if after<0:failure.append("flag")
                rows.append({"positionId":record["positionId"],"fen":record["fen"],"gamePhase":record["gamePhase"],"sideToMove":record["sideToMove"],"clockClass":label,"clockBeforeMs":clock,"incrementMs":inc,"movesToGo":moves,"softLimitMs":policy["softLimitMs"],"hardLimitMs":policy["hardLimitMs"],"actualElapsedMs":elapsed_ms,"bestMoveUci":result["bestMove"],"depth":result["depth"],"seldepth":result["seldepth"],"nodes":result["nodes"],"scoreType":result["scoreType"],"scoreValue":result["scoreValue"],"pv":result["pv"],"clockAfterMs":after,"hardLimitOverrun":overrun,"flag":after<0,"emergencyStop":policy["criticalLowTime"],"stockfishAgreement":result["bestMove"]==annotations.get(record["positionId"],{}).get("bestMoveUci"),"failures":failure});failures.extend({"positionId":record["positionId"],"failure":x} for x in failure)
            except Exception as error:failures.append({"positionId":record["positionId"],"failure":"protocol_failure","detail":str(error)});break
    return rows,failures

def timed_metrics(rows:Sequence[dict[str,Any]],failures:Sequence[dict[str,Any]])->dict[str,Any]:
    return {"positions":len(rows),"bestmoveCompletion":sum(not x["failures"] for x in rows),"flags":sum(x["flag"] for x in rows),"hardLimitOverruns":sum(x["hardLimitOverrun"] for x in rows),"repeatedHardLimitOverruns":sum(x["hardLimitOverrun"] for x in rows)>1,"averageDepth":statistics.fmean((x["depth"] or 0) for x in rows) if rows else 0,"meanNodes":statistics.fmean((x["nodes"] or 0) for x in rows) if rows else 0,"averageClockRemainingMs":statistics.fmean(x["clockAfterMs"] for x in rows) if rows else 0,"emergencyStops":sum(x["emergencyStop"] for x in rows),"stockfishAgreement":sum(x["stockfishAgreement"] for x in rows),"failures":len(failures)}

def timed_search(args:argparse.Namespace)->None:
    root=Path(args.output_dir);previous=read_json(root/"timed-search"/"performance.json") if (root/"timed-search"/"performance.json").exists() else None;records=read_jsonl(root/"development.jsonl");annotations={x["positionId"]:x for x in read_jsonl(Path(args.annotations))};variants=read_json(root/"variants.json")["variants"];specs=[("time-baseline",phase18_base_engine(root,Path(args.base_engine)))]+[(x["variantId"],Path(x["binaryPath"])) for x in variants];results=[]
    for identifier,engine in specs:
        rows,failures=run_timed(engine,records,annotations);results.append({"variantId":identifier,"records":rows,"failures":failures,"metrics":timed_metrics(rows,failures)});print(f"Timed development complete: {identifier}")
    canonical_results=[]
    stable_keys=("positionId","fen","gamePhase","sideToMove","clockClass","clockBeforeMs","incrementMs","movesToGo","softLimitMs","hardLimitMs","emergencyStop")
    for result in results:
        canonical_results.append({"variantId":result["variantId"],"records":[{key:row[key] for key in stable_keys} for row in result["records"]],"policyAndExecutionConfigurationOnly":True})
    determinism={"comparedToPreviousRun":previous is not None,"variants":{}}
    if previous is not None:
        old={x["variantId"]:x for x in previous};keys=("bestMoveUci","depth","nodes","scoreType","scoreValue","pv")
        for result in results:
            prior=old[result["variantId"]];counts={key:sum(a.get(key)!=b.get(key) for a,b in zip(prior["records"],result["records"])) for key in keys};determinism["variants"][result["variantId"]]={"mismatches":counts,"totalMismatches":sum(counts.values())}
    write(root/"timed-search"/"development-results.jsonl",jsonl(canonical_results));write(root/"timed-search"/"performance.json",pretty(results));write(root/"timed-search"/"determinism.json",pretty(determinism))

def rank_key(item:Mapping[str,Any])->tuple[Any,...]:
    p,t=item["policyMetrics"],item["timedMetrics"];return (p["maximumAbsoluteHardJumpMs"],p["maximumAbsoluteRatioJump"],p["meanCriticalHardRatio"],t["emergencyStops"],-t["averageDepth"],-t["stockfishAgreement"],item["variantId"])

def rank(args:argparse.Namespace)->None:
    root=Path(args.output_dir);policies={x["variantId"]:x for x in read_jsonl(root/"scenarios"/"policy-results.jsonl")};timed={x["variantId"]:x for x in read_json(root/"timed-search"/"performance.json")};determinism=read_json(root/"timed-search"/"determinism.json");variants={x["variantId"]:x for x in read_json(root/"variants.json")["variants"]};basep=policies["time-baseline"]["metrics"];ranking=[]
    for identifier,variant in variants.items():
        p=policies[identifier]["metrics"];t=timed[identifier]["metrics"];nondeterministic=determinism.get("variants",{}).get(identifier,{}).get("totalMismatches",0);hard=t["flags"] or t["failures"] or t["repeatedHardLimitOverruns"] or p["invariantViolations"] or variant["isolation"]["fixedDepthSearchMismatches"] or nondeterministic
        improvement=p["meanCriticalHardRatio"]<basep["meanCriticalHardRatio"] or p["maximumAbsoluteHardJumpMs"]<basep["maximumAbsoluteHardJumpMs"]
        eligible=not hard and improvement and p["maximumAbsoluteHardJumpMs"]<=basep["maximumAbsoluteHardJumpMs"] and p["maximumAbsoluteRatioJump"]<=basep["maximumAbsoluteRatioJump"]
        ranking.append({"variantId":identifier,"eligible":eligible,"policyMetrics":p,"timedMetrics":t,"timedDeterminismMismatches":nondeterministic,"rejectionReasons":[] if eligible else ["safety_or_development_guardrail"]})
    ranking.sort(key=lambda x:(not x["eligible"],)+rank_key(x));qualifying=[x for x in ranking if x["eligible"]];outcome="all_time_variants_rejected" if all(x["timedMetrics"]["failures"] for x in ranking) else "no_time_candidate_outperformed_baseline";selected=None;holdout={}
    if qualifying:
        top=qualifying[0];variant=variants[top["variantId"]];records=read_jsonl(root/"holdout.jsonl");annotations={x["positionId"]:x for x in read_jsonl(Path(args.annotations))};base_rows,base_fail=run_timed(phase18_base_engine(root,Path(args.base_engine)),records,annotations);candidate_rows,candidate_fail=run_timed(Path(variant["binaryPath"]),records,annotations);holdout={"baseline":{"metrics":timed_metrics(base_rows,base_fail),"records":base_rows},"candidate":{"metrics":timed_metrics(candidate_rows,candidate_fail),"records":candidate_rows}}
        if not base_fail and not candidate_fail and not holdout["candidate"]["metrics"]["flags"] and not holdout["candidate"]["metrics"]["repeatedHardLimitOverruns"]:
            outcome="time_candidate_selected";base=read_json(Path(args.base_profile));profile=derive_profile(base,variant["parameter"],variant["variantValue"],FINAL_ID);directory=root/"candidate";write(directory/"profile.json",pretty(profile));header=directory/"generated"/"generated_tuning_values.hpp";write(header,generate_header(profile).encode());binary=directory/"chess-engine";run=subprocess.run(["make","-C","engine","candidate-build",f"CANDIDATE_HEADER=../{header}",f"CANDIDATE_OUTPUT=../{binary}"],text=True,capture_output=True)
            if run.returncode:raise TimeTuningError("Final candidate build failed")
            metadata={"schemaVersion":1,"basedOnProfileId":BASE_ID,"basedOnProfileHash":BASE_HASH,"candidateProfileId":FINAL_ID,"candidateProfileHash":profile["canonicalHash"],"developmentOnly":True,"promotionEligible":False,"changedTimeParameter":variant["parameter"],"baselineValue":variant["baselineValue"],"candidateValue":variant["variantValue"],"scenarioManifestChecksum":sha(root/"manifest.json"),"developmentMetrics":top,"holdoutMetrics":holdout,"criticalBoundaryReport":policies[top["variantId"]]["metrics"],"selectionOutcome":outcome};write(directory/"metadata.json",pretty(metadata));selected={"candidateId":FINAL_ID,"candidateHash":profile["canonicalHash"],"candidateBinary":str(binary),"candidateBinarySha256":validation.engine_identity(binary)["binarySha256"],"changedParameter":variant["parameter"],"baselineValue":variant["baselineValue"],"candidateValue":variant["variantValue"]}
    write(root/"ranking.json",pretty({"schemaVersion":1,"outcome":outcome,"ranking":ranking,"selected":selected,"holdout":holdout,"developmentOnly":True,"promotionEligible":False}));print(f"Ranking outcome: {outcome}")

def play_timed(label:str,fen:str,baseline_white:bool,baseline:validation.UciEngine,candidate:validation.UciEngine,max_plies:int,index:int)->tuple[dict[str,Any],str]:
    board=chess.Board(fen);clocks={chess.WHITE:3000.0,chess.BLACK:3000.0};plies=0;flags=illegal=protocol=0;divergence=0
    while not board.is_game_over(claim_draw=True) and plies<max_plies:
        baseline_turn=board.turn==chess.WHITE if baseline_white else board.turn==chess.BLACK;engine=baseline if baseline_turn else candidate
        try:result=engine.search_clock(board,max(1,int(clocks[chess.WHITE])),max(1,int(clocks[chess.BLACK])),100,100)
        except Exception:protocol+=1;break
        clocks[board.turn]-=result["elapsedSeconds"]*1000
        if clocks[board.turn]<0:flags+=1;break
        if result["move"] is None or result["move"] not in board.legal_moves:illegal+=1;break
        clocks[board.turn]+=100;board.push(result["move"]);plies+=1
    outcome=board.outcome(claim_draw=True);result_text=outcome.result() if outcome else "1/2-1/2";winner="draw"
    if result_text in ("1-0","0-1"):
        white=result_text=="1-0";winner="baseline" if white==baseline_white else "candidate"
    game=chess.pgn.Game.from_board(board);game.headers.update({"Event":"Bitboard Phase 18 timed smoke","Round":str(index),"White":"baseline" if baseline_white else "candidate","Black":"candidate" if baseline_white else "baseline","Result":result_text,"StartLabel":label});pgn=str(game.accept(chess.pgn.StringExporter(headers=True,variations=False,comments=False))).strip()+"\n"
    return {"gameIndex":index,"winner":winner,"result":result_text,"plies":plies,"flags":flags,"illegalMoves":illegal,"protocolFailures":protocol,"crashes":0,"hardLimitViolations":0,"remainingClockMs":{"white":clocks[chess.WHITE],"black":clocks[chess.BLACK]},"moveDivergences":divergence},pgn

def low_continuations(base:Path,candidate:Path|None,records:Sequence[dict[str,Any]])->dict[str,Any]:
    clocks=(100,60,41,40,39,20);output=[]
    engines=[("baseline",base)]+([("candidate",candidate)] if candidate is not None else [])
    for engine_name,path in engines:
        assert path is not None
        with validation.UciEngine(path,5) as uci:
            for i,record in enumerate(records[:12]):
                clock=clocks[i%6];board=chess.Board(record["fen"]);moves=[];remaining=float(clock);failure=None
                for _ in range(2):
                    if board.is_game_over():break
                    try:r=uci.search_clock(board,max(1,int(remaining)),max(1,int(remaining)));remaining-=r["elapsedSeconds"]*1000
                    except Exception as e:failure=str(e);break
                    if remaining<0 or r["move"] is None or r["move"] not in board.legal_moves:failure="flag_or_illegal";break
                    moves.append(r["bestMove"]);board.push(r["move"])
                output.append({"engine":engine_name,"positionId":record["positionId"],"initialClockMs":clock,"remainingClockMs":remaining,"moves":moves,"failure":failure})
    return {"records":output,"failures":sum(x["failure"] is not None for x in output),"flags":sum(x["remainingClockMs"]<0 for x in output)}

def smoke(args:argparse.Namespace)->None:
    root=Path(args.output_dir);ranking=read_json(root/"ranking.json")
    if ranking["outcome"]!="time_candidate_selected":
        continuation=low_continuations(phase18_base_engine(root,Path(args.base_engine)),None,read_jsonl(root/"suite.jsonl"));write(root/"matches"/"low-time-continuations.json",pretty(continuation));print("No selected time candidate; paired games skipped; baseline low-time continuations recorded");return
    candidate=Path(ranking["selected"]["candidateBinary"]);records=[];pgns=[]
    base_engine=phase18_base_engine(root,Path(args.base_engine))
    with validation.UciEngine(base_engine,10) as baseline,validation.UciEngine(candidate,10) as contender:
        for label,fen in validation.STARTS[:4]:
            for reverse in (False,True):
                row,pgn=play_timed(label,fen,not reverse,baseline,contender,args.max_plies,len(records)+1);records.append(row);pgns.append(pgn)
    for row,pgn in zip(records,pgns):write(root/"matches"/"games"/f"game-{row['gameIndex']:02d}.pgn",pgn.encode())
    continuation=low_continuations(base_engine,candidate,read_jsonl(root/"suite.jsonl"));write(root/"matches"/"low-time-continuations.json",pretty(continuation));summary={"schemaVersion":1,"games":8,"candidateWins":sum(x["winner"]=="candidate" for x in records),"baselineWins":sum(x["winner"]=="baseline" for x in records),"draws":sum(x["winner"]=="draw" for x in records),"flags":sum(x["flags"] for x in records),"illegalMoves":sum(x["illegalMoves"] for x in records),"crashes":0,"protocolFailures":sum(x["protocolFailures"] for x in records),"hardLimitViolations":sum(x["hardLimitViolations"] for x in records),"averageRemainingClockMs":statistics.fmean(v for x in records for v in x["remainingClockMs"].values()),"moveDivergences":sum(x["moveDivergences"] for x in records),"records":records,"lowTimeContinuations":continuation}
    if any(summary[x] for x in ("flags","illegalMoves","crashes","protocolFailures","hardLimitViolations")) or continuation["failures"]:raise TimeTuningError("Timed smoke hard failure")
    write(root/"matches"/"smoke-match.json",pretty(summary));print("Timed smoke: 8 games and 12 low-time positions")

def inspect(args:argparse.Namespace)->None:
    root=Path(args.output_dir);ranking=read_json(root/"ranking.json");smoke_result=read_json(root/"matches"/"smoke-match.json") if (root/"matches"/"smoke-match.json").exists() else None;variants=read_json(root/"variants.json")
    artifacts={p.name:sha(p) for p in (root/"manifest.json",root/"suite.jsonl",root/"development.jsonl",root/"holdout.jsonl",root/"variants.json",root/"ranking.json")}
    summary={"schemaVersion":1,"timeEntriesInventoried":13,"parametersSelected":list(SELECTED),"variantsGenerated":len(variants["variants"]),"variantsRejected":sum(not x["eligible"] for x in ranking["ranking"]),"policyScenarios":len(scenario_requests()),"datasetPositions":36,"developmentPositions":24,"holdoutPositions":12,"candidateSelectionOutcome":ranking["outcome"],"selectedCandidate":ranking["selected"],"timedGames":smoke_result["games"] if smoke_result else 0,"flags":smoke_result["flags"] if smoke_result else 0,"developmentOnly":True,"promotionEligible":False,"productionBehaviorChanges":0,"artifacts":artifacts};write(root/"summary.json",pretty(summary));print(f"Phase 18 outcome: {ranking['outcome']}")

def parser()->argparse.ArgumentParser:
    p=argparse.ArgumentParser(description=__doc__);subs=p.add_subparsers(dest="command",required=True)
    def common(c):c.add_argument("--output-dir",default=str(DEFAULT_ROOT));c.add_argument("--base-profile",default=str(DEFAULT_PROFILE));c.add_argument("--base-engine",default=str(DEFAULT_ENGINE));c.add_argument("--annotations",default=str(DEFAULT_ANNOTATIONS))
    c=subs.add_parser("characterize");common(c);c.set_defaults(function=characterize)
    c=subs.add_parser("prepare");common(c);c.add_argument("--registry",default=str(DEFAULT_REGISTRY));c.add_argument("--selection",default=str(DEFAULT_SELECTION));c.add_argument("--force",action="store_true");c.set_defaults(function=prepare)
    c=subs.add_parser("run-scenarios");common(c);c.set_defaults(function=run_scenarios)
    c=subs.add_parser("timed-search");common(c);c.set_defaults(function=timed_search)
    c=subs.add_parser("rank");common(c);c.set_defaults(function=rank)
    c=subs.add_parser("smoke-match");common(c);c.add_argument("--max-plies",type=int,default=160);c.set_defaults(function=smoke)
    c=subs.add_parser("inspect");common(c);c.set_defaults(function=inspect)
    return p

def main(argv:Sequence[str]|None=None)->int:
    try:args=parser().parse_args(argv);args.function(args);return 0
    except (TimeTuningError,validation.ValidationError,OSError,json.JSONDecodeError,subprocess.SubprocessError) as error:print(f"error: {error}",file=sys.stderr);return 2
if __name__=="__main__":raise SystemExit(main())
