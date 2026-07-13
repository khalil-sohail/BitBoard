#!/usr/bin/env python3
"""Focused synthetic tests for the Phase 17 search harness."""

from __future__ import annotations

import copy
import json
import tempfile
from pathlib import Path

import chess

import search_tuning as tuning


def require(value: bool, message: str) -> None:
    if not value:raise AssertionError(message)


def fake_records() -> list[dict]:
    rows=[]
    for phase in tuning.PHASES:
        for side in ("white","black"):
            for index in range(20):
                rows.append({"positionId":f"{phase}-{side}-{index}","gameId":f"g-{phase}-{side}-{index}","fen":chess.STARTING_FEN,"gamePhase":phase,"sideToMove":side,"resultClass":("win","draw","loss")[index%3],"resultFromSideToMove":(1,.5,0)[index%3],"split":"test"})
    return rows


def metric(agreement:int,median:int,p90:int,changed:int=1)->dict:
    return {"positions":32,"stockfishAgreement":agreement,"medianNodes":median,"p90Nodes":p90,"differentBestMoveFromBaseline":changed,"depthCompletion":32,"legalPvRate":1.0}


def main() -> int:
    entries=tuning.registry_entries();require(len(entries)==14,"registry search count")
    chosen=tuning.selected_entries(entries);require(len(chosen)==3 and all(x["type"]=="integer" for x in chosen),"three safe scalars")
    base=tuning.read_json(tuning.DEFAULT_PROFILE);variants=tuning.generate_variants(base,entries)
    require(len(variants)==6,"six one-step variants")
    hashes=[]
    for item in variants:
        changed=[name for name in base["parameters"] if base["parameters"][name]!=item["profile"]["parameters"][name]]
        require(changed==[item["parameter"]],"one changed parameter")
        require(abs(item["variantValue"]-item["baselineValue"])==item["step"],"registry step")
        require(all(item["profile"]["parameters"][name]==base["parameters"][name] for name in base["parameters"] if name.startswith(("evaluation.","time.","opening."))),"evaluation/time/opening isolation")
        hashes.append(item["profile"]["canonicalHash"])
    require(hashes==[x["profile"]["canonicalHash"] for x in tuning.generate_variants(base,entries)],"stable profile hashes")

    bounded=copy.deepcopy(chosen[0]);bounded["minimum"]=bounded["currentValue"]
    custom=[bounded if x["name"]==bounded["name"] else x for x in entries]
    require(len(tuning.generate_variants(base,custom))==5,"bound direction skipped without clamping")
    suite,development,holdout=tuning.select_suite(fake_records())
    require((len(suite),len(development),len(holdout))==(48,32,16),"suite partitions")
    require(not ({x["positionId"] for x in development}&{x["positionId"] for x in holdout}),"partition overlap")
    require([x["positionId"] for x in suite]==[x["positionId"] for x in tuning.select_suite(fake_records())[0]],"selection determinism")

    board=chess.Board();first=sorted(board.legal_moves,key=lambda x:x.uci())[0];require(tuning.validate_pv(board.fen(),[first.uci()]),"legal PV")
    require(not tuning.validate_pv(board.fen(),["e2e5"]),"illegal PV rejection")
    baseline=metric(10,100,150,0);good=metric(10,99,149);bad_nodes=metric(10,116,196);bad_agreement=metric(9,90,100)
    require(tuning.eligible(good,[],[],baseline),"eligible node improvement")
    require(not tuning.eligible(bad_nodes,[],[],baseline),"node guardrail")
    require(not tuning.eligible(bad_agreement,[],[],baseline),"agreement guardrail")
    require(not tuning.eligible(good,[{"failure":"illegal"}],[],baseline),"hard failure rejection")
    ranked=[{"variantId":"b","metrics":good},{"variantId":"a","metrics":good}];ranked.sort(key=tuning.ranking_key);require(ranked[0]["variantId"]=="a","ranking tie break")

    source=(Path(__file__).parent/"search_tuning.py").read_text()
    require('self.send(f"go depth {depth}")' not in source,"harness must reuse UCI lifecycle")
    require("go nodes" not in source,"go nodes forbidden")
    require("wtime" not in source and "movetime" not in source,"clock controls forbidden")
    require("promotionEligible\":True" not in source.replace(" ",""),"no promotion path")
    require("Elo" not in source,"no Elo claim")
    require(tuning.SMOKE_STARTS==tuning.validation.STARTS[:6],"six smoke starts/color reversal source")
    print("Phase 17 search tuning tests passed")
    return 0


if __name__=="__main__":raise SystemExit(main())
