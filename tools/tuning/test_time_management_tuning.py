#!/usr/bin/env python3
"""Focused synthetic and protocol tests for the Phase 18 time harness."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

import chess
import evaluation_candidate_validate as validation

import time_management_tuning as tuning

def require(value:bool,message:str)->None:
    if not value:raise AssertionError(message)

def main()->int:
    entries=tuning.registry_entries();require(len(entries)==13,"time registry count")
    chosen=tuning.selected_entries(entries);require(len(chosen)==3 and all(x["type"]=="integer" for x in chosen),"three safe time scalars")
    base=tuning.read_json(Path("tuning/profiles/builtin-default-v1.json"));variants=tuning.generate_variants(base,entries);require(len(variants)==6,"six one-step variants")
    for item in variants:
        changed=[x for x in base["parameters"] if base["parameters"][x]!=item["profile"]["parameters"][x]]
        require(changed==[item["parameter"]],"one parameter per variant")
        require(abs(item["variantValue"]-item["baselineValue"])==item["step"],"registry step")
        entry=next(x for x in entries if x["name"]==item["parameter"]);require(entry["minimum"]<=item["variantValue"]<=entry["maximum"],"registry bounds")
        require(all(item["profile"]["parameters"][x]==base["parameters"][x] for x in base["parameters"] if not x.startswith("time.")),"evaluation/search/opening isolation")
    require(len(tuning.scenario_requests())==84,"deterministic 84-scenario matrix")
    require(len(tuning.characterization_requests())==24*4*6*2,"full boundary characterization matrix")
    engine=Path("engine/chess-engine")
    requests=[tuning.request(x) for x in (0,1,39,40,41,60,69,70,71,100)]+[tuning.request(1000,100,5,True,20,100)]
    first=tuning.decorate(tuning.query_policy(engine,requests),requests);second=tuning.decorate(tuning.query_policy(engine,requests),requests);require(first==second,"deterministic JSONL")
    require(all(x["profileId"]=="builtin-default-v1" for x in first),"profile identity")
    require(by_one:=(next(x for x in first if x["remainingTimeMs"]==1)),"one-ms record")
    require(by_one["invariantViolations"]==[] and by_one["immediateMove"] and by_one["hardLimitMs"]==0,"one-ms immediate policy")
    require(all(not x["invariantViolations"] for x in first),"allocation invariants")
    by={x["remainingTimeMs"]:x for x in first if x["incrementMs"]==0}
    require(by[39]["criticalLowTime"] and by[40]["criticalLowTime"] and by[41]["criticalLowTime"] and by[60]["criticalLowTime"],"critical raw clocks")
    require(by[69]["criticalLowTime"] and not by[70]["criticalLowTime"],"documented safe-domain transition")
    require(abs(by[70]["hardLimitMs"]-by[69]["hardLimitMs"])<=3,"smoothed transition")
    require(by[60]["hardLimitMs"]<=6,"conservative 60ms policy")
    require(by[39]["softLimitMs"]<=by[39]["hardLimitMs"],"soft/hard invariant")
    invalid=subprocess.run([str(engine.resolve()),"--mode=time-policy"],input="{}\n",text=True,capture_output=True,timeout=5);require(invalid.returncode==0 and not invalid.stdout and "invalid request" in invalid.stderr,"invalid request separation")
    continuous_requests=[tuning.request(x) for x in range(1,151)];continuous=tuning.decorate(tuning.query_policy(engine,continuous_requests),continuous_requests);metrics=tuning.continuity(continuous);require(metrics["maximumAbsoluteHardJumpMs"]<=3,"continuity guardrail")
    source=(Path(__file__).parent/"time_management_tuning.py").read_text();lifecycle=(Path(__file__).parent/"evaluation_candidate_validate.py").read_text()
    require("go nodes" not in source,"go nodes forbidden")
    require("search_clock" in source and "go wtime" in lifecycle and "movestogo" in lifecycle,"clock command construction")
    require('promotionEligible":True' not in source.replace(" ",""),"no promotion path")
    require(tuning.SELECTED[2]=="time.criticalLowTimeThresholdMs","critical parameter selected")
    with validation.UciEngine(engine,5) as uci:
        board=chess.Board()
        for clock in (0,1,2,5,10,39,40,41):
            result=uci.search_clock(board,clock,clock)
            require(result["legal"],f"legal immediate fallback at {clock}ms")
            require(result["stopReason"]=="immediate_move",f"immediate stop reason at {clock}ms")
            require(result["hardBudgetMs"]==0 and result["searchElapsedMs"]==0,f"zero immediate budget at {clock}ms")
            require(result["deadlineChecks"]==0,"immediate path did not search")
        terminal=chess.Board("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1")
        result=uci.search_clock(terminal,1,1)
        require(result["bestMove"]=="0000" and result["stopReason"]=="terminal","terminal clock handling")
    sample=[{"variantId":"b","policyMetrics":{"maximumAbsoluteHardJumpMs":1,"maximumAbsoluteRatioJump":.1,"meanCriticalHardRatio":.2},"timedMetrics":{"emergencyStops":1,"averageDepth":2,"stockfishAgreement":1}},{"variantId":"a","policyMetrics":{"maximumAbsoluteHardJumpMs":1,"maximumAbsoluteRatioJump":.1,"meanCriticalHardRatio":.2},"timedMetrics":{"emergencyStops":1,"averageDepth":2,"stockfishAgreement":1}}];sample.sort(key=tuning.rank_key);require(sample[0]["variantId"]=="a","deterministic ranking")
    print("Phase 18 time-management tuning tests passed")
    return 0

if __name__=="__main__":raise SystemExit(main())
