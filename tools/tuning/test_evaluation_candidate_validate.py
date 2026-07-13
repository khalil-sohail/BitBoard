#!/usr/bin/env python3
"""Focused synthetic tests for Phase 16 candidate validation."""

from __future__ import annotations

import json
import os
import shutil
import signal
import tempfile
import time
from pathlib import Path

import chess

import evaluation_candidate_validate as validation
import evaluation_tuning
import stockfish_annotate


ROOT = Path(__file__).resolve().parents[2]


def require(value: bool, message: str) -> None:
    if not value: raise AssertionError(message)


def executable(path: Path, source: str) -> Path:
    path.write_text(source, encoding="utf-8"); path.chmod(0o755); return path


def process_gone(pid: int) -> bool:
    try: os.kill(pid, 0); return False
    except ProcessLookupError: return True


def fixture_records() -> list[dict]:
    records=[]
    for index in range(120):
        for side in validation.SIDES:
            records.append({"positionId":f"p-{index}-{side}","gameId":f"g-{index}","split":"test","sideToMove":side,"gamePhase":validation.PHASES[index%3],"resultFromSideToMove":(1.0,.5,0.0)[(index//3)%3]})
    return records


def main() -> int:
    mate_line=b"info depth 4 score mate -3 lowerbound nodes 12 time 1 pv g6g7 h8g8\r\n"
    for cuts in (
        [len(mate_line)],
        [4,13,len(mate_line)],
        [31,32,36,len(mate_line)],
        [len(mate_line)-1,len(mate_line)],
        list(range(1,len(mate_line)+1)),
    ):
        buffer=validation.IncrementalLineBuffer();start=0
        for end in cuts:buffer.feed(mate_line[start:end]);start=end
        line=buffer.pop();require(line is not None,"chunked mate line was dropped")
        parsed=validation.parse_uci_search_info(line)
        require(parsed is not None and parsed["scoreValue"]==-3 and parsed["scoreBound"]=="lowerbound","chunked mate parse")
        require(buffer.pop() is None and not buffer.incomplete,"line buffer did not drain")
    buffer=validation.IncrementalLineBuffer()
    buffer.feed(b"info depth 1 score cp 2\ninfo depth 2 score mate 1 upper")
    require(validation.parse_uci_search_info(buffer.pop() or "")["scoreValue"]==2,"multiple-line first record")
    require(buffer.pop() is None and buffer.incomplete.endswith(b"upper"),"incomplete trailing record was parsed")
    buffer.feed(b"bound pv g6g7\n")
    require(validation.parse_uci_search_info(buffer.pop() or "")["scoreBound"]=="upperbound","split bound/PV parse")
    empty=validation.IncrementalLineBuffer();require(empty.pop() is None and not empty.incomplete,"empty shutdown buffer")
    try:validation.parse_uci_search_info("info depth 3 score mate --499499999 pv g5g6");raise AssertionError("malformed mate accepted")
    except validation.ValidationError:pass

    # Frozen private integration check is cleanly conditional.
    if validation.DEFAULT_CANDIDATE.is_dir() and validation.DEFAULT_CANDIDATE_ENGINE.is_file():
        frozen=validation.verify_frozen()
        require(frozen["profile"]["canonicalHash"]==validation.EXPECTED_CANDIDATE_HASH,"candidate identity")
        require(frozen["metadata"]["developmentOnly"] is True and frozen["metadata"]["promotionEligible"] is False,"candidate status")
        require(frozen["baseline"]["profileHash"]==validation.EXPECTED_BASELINE_HASH,"baseline identity")
        mate_board=chess.Board("1Q6/7k/8/8/6Q1/8/8/6K1 w - - 1 51")
        sentinel_board=chess.Board("8/8/4k3/6PP/8/8/1P6/6K1 w - - 1 42")
        for engine_path in (validation.DEFAULT_BASELINE_ENGINE,validation.DEFAULT_CANDIDATE_ENGINE):
            with validation.UciEngine(engine_path,10) as engine:
                mate_first=engine.search(mate_board,4);mate_second=engine.search(mate_board,4)
                sentinel=engine.search(sentinel_board,4)
            require(mate_first["legal"] and mate_first["bestMove"]==mate_second["bestMove"],"real mate search legality/determinism")
            require(mate_first["mateScores"] and all(score["scoreValue"]==1 for score in mate_first["mateScores"]),"real mate telemetry")
            require(not mate_first["malformedInfoLines"] and not sentinel["malformedInfoLines"],"real malformed mate telemetry")
            require(mate_first["bestMove"]=="b8h2" and sentinel["bestMove"]=="g5g6","real regression bestmove")
        with tempfile.TemporaryDirectory() as temporary:
            copied=Path(temporary)/"candidate";shutil.copytree(validation.DEFAULT_CANDIDATE,copied)
            metadata=validation.read_json(copied/"metadata.json");metadata["promotionEligible"]=True
            (copied/"metadata.json").write_text(validation.pretty_json(metadata))
            try:validation.verify_frozen(copied);raise AssertionError("unsafe candidate accepted")
            except validation.ValidationError:pass
    else:
        print("[SKIP] frozen private candidate integration")

    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary);pid_path=directory/"pid"
        stubborn=executable(directory/"stubborn.py",f'''#!{ROOT}/.venv/bin/python
import os,signal,sys,time
open({str(pid_path)!r},"w").write(str(os.getpid()))
signal.signal(signal.SIGTERM,signal.SIG_IGN)
for line in sys.stdin:
 if line.strip()=="uci": print("id name Fake 1\\nid author Test\\nuciok",flush=True)
 elif line.strip()=="isready": print("readyok",flush=True)
 elif line.strip()=="quit":
  while True: time.sleep(1)
''')
        identity=stockfish_annotate.verify_engine(stubborn,.3)
        require(identity["engineName"]=="Fake 1","normal handshake")
        pid=int(pid_path.read_text());require(process_gone(pid),"non-exiting fake engine was orphaned")

        timeout_pid=directory/"timeout-pid"
        silent=executable(directory/"silent.py",f'''#!{ROOT}/.venv/bin/python
import os,time
open({str(timeout_pid)!r},"w").write(str(os.getpid()))
time.sleep(30)
''')
        try:stockfish_annotate.verify_engine(silent,.2);raise AssertionError("handshake timeout accepted")
        except stockfish_annotate.AnnotationError:pass
        require(process_gone(int(timeout_pid.read_text())),"timed-out fake engine was orphaned")

        log=directory/"commands.log"
        fake_source=f'''#!{ROOT}/.venv/bin/python
import chess,sys
board=chess.Board()
for raw in sys.stdin:
 line=raw.strip();open({str(log)!r},"a").write(line+"\\n")
 if line=="uci": print("id name Fake Search\\nid author Test\\nuciok",flush=True)
 elif line=="isready": print("readyok",flush=True)
 elif line.startswith("position fen "): board=chess.Board(line[13:])
 elif line.startswith("go depth "):
  depth=int(line.split()[-1]);move=sorted(board.legal_moves,key=lambda m:m.uci())[0]
  print(f"info depth {{depth}} score cp 0 nodes 10 time 1 pv {{move.uci()}}",flush=True);print("bestmove "+move.uci(),flush=True)
 elif line=="quit": break
'''
        fake=executable(directory/"fake-search.py",fake_source)
        with validation.UciEngine(fake) as engine:
            first=engine.search(chess.Board(),6);second=engine.search(chess.Board(),6)
        require(first["bestMove"]==second["bestMove"] and first["legal"],"deterministic legal search")
        commands=log.read_text();require("go depth 6" in commands and "go nodes" not in commands,"fixed-depth command")

        malformed_fake=executable(directory/"malformed-search.py",f'''#!{ROOT}/.venv/bin/python
import chess,sys
board=chess.Board()
for raw in sys.stdin:
 line=raw.strip()
 if line=="uci": print("id name Fake Malformed\\nid author Test\\nuciok",flush=True)
 elif line=="isready": print("readyok",flush=True)
 elif line.startswith("position fen "): board=chess.Board(line[13:])
 elif line.startswith("go depth "):
  move=sorted(board.legal_moves,key=lambda m:m.uci())[0]
  print("info depth 1 score mate --499499999 pv "+move.uci(),flush=True)
  print("info depth 2 score mate 1 lowerbound pv "+move.uci(),flush=True)
  print("bestmove "+move.uci(),flush=True)
 elif line=="quit": break
''')
        with validation.UciEngine(malformed_fake) as engine:
            diagnostic=engine.search(chess.Board(),2)
        require(diagnostic["legal"] and diagnostic["malformedInfoLines"]==1,"malformed complete line classification")
        require(diagnostic["mateScores"] and diagnostic["mateScores"][0]["scoreBound"]=="lowerbound","valid mate retained after malformed line")

        games,pgns=validation.run_smoke(fake,fake,1,2)
        games2,pgns2=validation.run_smoke(fake,fake,1,2)
        require(games==games2 and pgns==pgns2 and len(games)==20,"deterministic paired smoke")
        require(all(r["maximumPlyAdjudication"] and not r["illegalMoves"] for r in games),"maximum-ply/legal handling")
        require(all("[Result \"1/2-1/2\"]" in pgn for pgn in pgns),"PGN generation")
        require(sum(r["baselineColor"]=="white" for r in games)==10,"color reversal")

    source=fixture_records();phase15_positions={"p-0-white"};phase15_games={"g-0"}
    eligible=[r for r in source if r["split"]=="test" and r["positionId"] not in phase15_positions and r["gameId"] not in phase15_games]
    first,warnings=evaluation_tuning.select_split(eligible,90,1,"audit-seed");second,_=evaluation_tuning.select_split(eligible,90,1,"audit-seed")
    require([r["positionId"] for r in first]==[r["positionId"] for r in second],"audit selection determinism")
    require(not ({r["gameId"] for r in first}&phase15_games) and all(r["split"]=="test" for r in first),"Phase 15/test exclusion")
    summary=evaluation_tuning.counts_summary(first);require(summary["maximumPositionsPerGame"]==1 and summary["sideToMove"]=={"white":45,"black":45},"game cap/side balance")
    require(set(k for k,v in summary["gamePhase"].items() if v)==set(validation.PHASES),"phase balance")
    require(set(k for k,v in summary["resultClass"].items() if v)==set(validation.RESULTS),"result balance")
    require(not warnings,"stable fallback warnings")

    baseline=validation.evaluation_tuning.metric([],[]);require(baseline["records"]==0,"independent metric helper")
    source_text=(ROOT/"tools/tuning/evaluation_candidate_validate.py").read_text()
    require("go nodes" not in source_text.replace('"go nodes"',""),"validation tool must not issue go nodes")
    require("profile[\"parameters\"]" not in source_text,"Phase 16 must not retrain or mutate parameters")
    print("Phase 16 candidate validation tests passed")
    return 0


if __name__=="__main__":raise SystemExit(main())
