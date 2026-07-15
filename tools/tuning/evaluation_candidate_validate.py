#!/usr/bin/env python3
"""Phase 16 validation for the frozen Phase 15 evaluation candidate."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import select
import shutil
import sqlite3
import statistics
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Sequence

import chess
import chess.pgn

import evaluation_features
import evaluation_tuning
import pgn_dataset
from generate_tuning_header import generate_header
from validate_profile import validate_profile

EXPECTED_CANDIDATE_ID = "candidate-eval-prototype-0001"
EXPECTED_CANDIDATE_HASH = "sha256:c566fc78ce90421271506edef8183677fca4c9367152a1dc76b5d538fe9b3ec6"
EXPECTED_CANDIDATE_BINARY_SHA = "sha256:4730da02d9e78bc2c17a0d6c0019e394ddc2a72c3fed68ecd2310b52670a0c8c"
EXPECTED_BASELINE_ID = "builtin-default-v1"
EXPECTED_BASELINE_HASH = "sha256:55a1ac92352bd018460f115cb5061c76140f1eed453afc8a229ed3fa84145718"
AUDIT_SEED = "bitboard-eval-candidate-audit-v1"
SEARCH_SEED = "bitboard-eval-candidate-search-v1"
SPLITS = ("train", "validation", "test")
PHASES = ("opening", "middlegame", "endgame")
SIDES = ("white", "black")
RESULTS = ("win", "draw", "loss")
SCHEMA_VERSION = 1

DEFAULT_DATASET = Path("tuning/datasets/pgn-derived-v1-phase15-dev")
DEFAULT_PHASE15_SELECTION = Path("tuning/selections/eval-prototype-v1/manifest.json")
DEFAULT_AUDIT_SELECTION = Path("tuning/selections/eval-candidate-audit-v1")
DEFAULT_ANNOTATIONS = Path("tuning/annotations/stockfish-reference-v1-phase16-audit")
DEFAULT_BASELINE_FEATURES = Path("tuning/features/eval-features-v1-phase16-audit-baseline")
DEFAULT_CANDIDATE_FEATURES = Path("tuning/features/eval-features-v1-phase16-audit-candidate")
DEFAULT_CANDIDATE = Path("tuning/candidates/candidate-eval-prototype-0001")
DEFAULT_CANDIDATE_HEADER = Path("tuning/builds/candidate-eval-prototype-0001/generated/generated_tuning_values.hpp")
DEFAULT_CANDIDATE_ENGINE = Path("tuning/builds/candidate-eval-prototype-0001/chess-engine")
DEFAULT_BASELINE_ENGINE = Path("engine/chess-engine")
DEFAULT_OUTPUT = Path("tuning/validation/candidate-eval-prototype-0001")


class ValidationError(Exception):
    pass


class IncrementalLineBuffer:
    """Turn arbitrary subprocess byte chunks into newline-complete UCI lines."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self._lines: list[str] = []

    def feed(self, chunk: bytes) -> None:
        self._buffer.extend(chunk)
        while True:
            newline = self._buffer.find(b"\n")
            if newline < 0:
                return
            raw = bytes(self._buffer[:newline])
            del self._buffer[:newline + 1]
            if raw.endswith(b"\r"):
                raw = raw[:-1]
            self._lines.append(raw.decode("utf-8", errors="replace"))

    def pop(self) -> str | None:
        return self._lines.pop(0) if self._lines else None

    @property
    def incomplete(self) -> bytes:
        return bytes(self._buffer)


_INTEGER = re.compile(r"[+-]?\d+")
_UCI_MOVE = re.compile(r"[a-h][1-8][a-h][1-8][qrbn]?")


def parse_uci_search_info(line: str) -> dict[str, Any] | None:
    """Strictly parse one complete score-bearing UCI info line."""
    tokens = line.split()
    if len(tokens) < 4 or tokens[:2] != ["info", "depth"] or "score" not in tokens:
        return None
    if tokens.count("score") != 1:
        raise ValidationError(f"Malformed UCI info line: {line}")
    item: dict[str, Any] = {}
    for key in ("depth", "seldepth", "nodes", "time", "tbhits"):
        if key in tokens:
            index = tokens.index(key)
            if index + 1 >= len(tokens) or not _INTEGER.fullmatch(tokens[index + 1]):
                raise ValidationError(f"Malformed UCI info line: {line}")
            item[key] = int(tokens[index + 1])
    score_index = tokens.index("score")
    if score_index + 2 >= len(tokens):
        raise ValidationError(f"Malformed UCI info line: {line}")
    score_type, score_text = tokens[score_index + 1:score_index + 3]
    if score_type not in ("cp", "mate") or not _INTEGER.fullmatch(score_text):
        raise ValidationError(f"Malformed UCI info line: {line}")
    if tokens.count("cp") != (1 if score_type == "cp" else 0) or tokens.count("mate") != (1 if score_type == "mate" else 0):
        raise ValidationError(f"Malformed UCI info line: {line}")
    score_value = int(score_text)
    if score_type == "mate" and score_value == 0:
        raise ValidationError(f"Malformed UCI info line: {line}")
    item["scoreType"] = score_type
    item["scoreValue"] = score_value
    after_score = score_index + 3
    if after_score < len(tokens) and tokens[after_score] in ("lowerbound", "upperbound"):
        item["scoreBound"] = tokens[after_score]
    elif "lowerbound" in tokens or "upperbound" in tokens:
        raise ValidationError(f"Malformed UCI info line: {line}")
    if "pv" in tokens:
        pv_index = tokens.index("pv")
        pv = tokens[pv_index + 1:]
        if not pv or any(not _UCI_MOVE.fullmatch(move) for move in pv):
            raise ValidationError(f"Malformed UCI info line: {line}")
        item["pv"] = pv
    return item


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def pretty_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False, allow_nan=False) + "\n"


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
    if not isinstance(value, dict): raise ValidationError(f"Expected JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]


def relative(path: Path) -> str:
    return Path(os.path.relpath(path.resolve(), Path.cwd().resolve())).as_posix()


def engine_identity(path: Path, timeout: float = 10.0) -> dict[str, Any]:
    if not path.is_file() or not os.access(path, os.X_OK): raise ValidationError(f"Engine is not executable: {path}")
    process = subprocess.run([str(path.resolve()), "--mode=gui"], input="uci\nquit\n", text=True,
                             capture_output=True, timeout=timeout)
    if process.returncode: raise ValidationError(f"Engine identity failed: {process.stderr.strip()}")
    marker = "info string tuning profile="
    lines = [line for line in process.stdout.splitlines() if line.startswith(marker)]
    if len(lines) != 1: raise ValidationError(f"Engine reported {len(lines)} profile identities")
    tokens = dict(item.split("=", 1) for item in lines[0][len("info string tuning "):].split() if "=" in item)
    return {"profileId": tokens.get("profile"), "profileHash": tokens.get("hash"), "binarySha256": sha256_file(path)}


def verify_frozen(candidate_dir: Path = DEFAULT_CANDIDATE,
                  candidate_header: Path = DEFAULT_CANDIDATE_HEADER,
                  candidate_engine: Path = DEFAULT_CANDIDATE_ENGINE,
                  baseline_engine: Path = DEFAULT_BASELINE_ENGINE) -> dict[str, Any]:
    global EXPECTED_CANDIDATE_ID, EXPECTED_CANDIDATE_HASH, EXPECTED_CANDIDATE_BINARY_SHA
    global EXPECTED_BASELINE_ID, EXPECTED_BASELINE_HASH
    required = [candidate_dir/name for name in ("profile.json", "metadata.json", "manifest.json")]
    required += [candidate_header, candidate_engine, baseline_engine]
    missing = [str(path) for path in required if not path.is_file()]
    if missing: raise ValidationError(f"Frozen artifact(s) missing: {missing}")
    profile, metadata, manifest = (read_json(candidate_dir/name) for name in ("profile.json", "metadata.json", "manifest.json"))
    validate_profile(profile)
    candidate_id = profile.get("profileId")
    candidate_hash = profile.get("canonicalHash")
    if metadata.get("candidateProfileId") != candidate_id or metadata.get("candidateProfileHash") != candidate_hash or metadata.get("developmentOnly") is not True or metadata.get("promotionEligible") is not False:
        raise ValidationError("Frozen candidate metadata/status mismatch")
    for name, artifact in manifest.get("artifacts", {}).items():
        if sha256_file(candidate_dir/name) != artifact.get("sha256"):
            raise ValidationError(f"Candidate manifest checksum mismatch: {name}")
    expected_header = generate_header(profile).encode()
    if candidate_header.read_bytes() != expected_header:
        raise ValidationError("Candidate header does not match the frozen profile")
    baseline, candidate = engine_identity(baseline_engine), engine_identity(candidate_engine)
    baseline_profile = read_json(Path("tuning/profiles/builtin-default-v1.json"))
    if (baseline["profileId"], baseline["profileHash"]) != (baseline_profile["profileId"], baseline_profile["canonicalHash"]):
        raise ValidationError("Baseline engine identity mismatch")
    if (candidate["profileId"], candidate["profileHash"]) != (candidate_id, candidate_hash):
        raise ValidationError("Candidate engine identity mismatch")
    EXPECTED_CANDIDATE_ID, EXPECTED_CANDIDATE_HASH = str(candidate_id), str(candidate_hash)
    EXPECTED_CANDIDATE_BINARY_SHA = candidate["binarySha256"]
    EXPECTED_BASELINE_ID, EXPECTED_BASELINE_HASH = baseline["profileId"], baseline["profileHash"]
    return {"profile": profile, "metadata": metadata, "manifest": manifest,
            "candidateHeaderSha256": sha256_file(candidate_header), "baseline": baseline, "candidate": candidate}


def audit_select_command(args: argparse.Namespace) -> None:
    frozen = verify_frozen(Path(args.candidate_dir), Path(args.candidate_header), Path(args.candidate_engine), Path(args.baseline_engine))
    dataset, phase15, output = Path(args.dataset_dir), Path(args.phase15_selection), Path(args.output_dir)
    dataset_manifest = read_json(dataset/"manifest.json"); phase15_manifest = read_json(phase15)
    phase15_records = read_jsonl(phase15.parent/"selection.jsonl")
    excluded_positions = {record["positionId"] for record in phase15_records}; excluded_games = {record["gameId"] for record in phase15_records}
    database=dataset/"work/dataset.sqlite"
    connection=sqlite3.connect(database)
    try:
        connection.execute("CREATE TEMP TABLE excluded_games(game_id TEXT PRIMARY KEY)")
        connection.executemany("INSERT OR IGNORE INTO excluded_games VALUES(?)",((item,) for item in excluded_games))
        eligible_games=connection.execute("SELECT COUNT(DISTINCT game_id) FROM positions WHERE split='test' AND game_id NOT IN (SELECT game_id FROM excluded_games)").fetchone()[0]
    finally: connection.close()
    actual_target=min(args.count,eligible_games)
    try: selected=pgn_dataset.select_balanced_positions(dataset,"test",actual_target,1,args.seed,excluded_games)
    except pgn_dataset.DatasetError as error: raise ValidationError(str(error)) from error
    deficits=[]
    if actual_target < args.count:
        deficits.append(f"positions target={args.count} actual={actual_target} deficit={args.count-actual_target}; only {eligible_games} eligible test-split games remain after Phase 15 exclusion")
    records=[]
    for rank,record in enumerate(selected,1):
        rc=evaluation_tuning.result_class(float(record["resultFromSideToMove"]))
        records.append({key:record[key] for key in ("positionId","gameId","split","fen","sideToMove","gamePhase","result","resultFromSideToMove","sourceFile","sourceGameIndex","ply")} | {"resultClass":rc,"selectionRank":rank,"stratum":f"test|{record['gamePhase']}|{record['sideToMove']}|{rc}"})
    if any(record["split"]!="test" for record in records) or len({r["gameId"] for r in records})!=len(records): raise ValidationError("Audit split/game invariant failed")
    temp=Path(tempfile.mkdtemp(prefix=output.name+".tmp-",dir=output.parent if output.parent.exists() else Path.cwd()))
    try:
        write(temp/"selection.jsonl",jsonl(records)); summary=evaluation_tuning.counts_summary(records)|{"deficits":deficits,"phase15PositionOverlap":0,"phase15GameOverlap":0}
        write(temp/"summary.json",pretty_json(summary).encode())
        artifacts={name:{"sha256":sha256_file(temp/name),**({"records":len(records)} if name.endswith(".jsonl") else {})} for name in ("selection.jsonl","summary.json")}
        manifest={"schemaVersion":1,"selectionAlgorithmVersion":evaluation_tuning.ALGORITHM_VERSION,"selectionSeed":args.seed,"maximumPositionsPerGame":1,"requestedCounts":{"test":args.count},"actualCounts":{"test":len(records)},"eligibleGamesAfterExclusion":eligible_games,"sourceDataset":{"datasetId":dataset_manifest["datasetId"],"datasetVersion":dataset_manifest["datasetVersion"],"manifestSha256":sha256_file(dataset/"manifest.json"),"positionsSha256":sha256_file(dataset/"positions.jsonl")},"phase15Exclusion":{"manifestSha256":sha256_file(phase15),"selectionSha256":sha256_file(phase15.parent/"selection.jsonl"),"excludedPositions":len(excluded_positions),"excludedGames":len(excluded_games)},"candidateProfileHash":frozen["profile"]["canonicalHash"],"selectedPositionsSha256":artifacts["selection.jsonl"]["sha256"],"deficits":deficits,"warnings":deficits,"artifacts":artifacts}
        write(temp/"manifest.json",pretty_json(manifest).encode())
        if output.exists():
            if not args.force: raise ValidationError(f"Output exists; use --force: {output}")
            shutil.rmtree(output)
        output.parent.mkdir(parents=True,exist_ok=True); os.replace(temp,output)
    except Exception:
        shutil.rmtree(temp,ignore_errors=True); raise
    print(f"Audit selection: {len(records)} positions, {len(records)} games, checksum {sha256_file(output/'selection.jsonl')}")


def pct(candidate: float, baseline: float) -> float:
    return (candidate-baseline)*100.0/baseline if baseline else 0.0


def static_audit_command(args: argparse.Namespace) -> None:
    frozen=verify_frozen(Path(args.candidate_dir),Path(args.candidate_header),Path(args.candidate_engine),Path(args.baseline_engine))
    baseline_dir,candidate_dir,output=Path(args.baseline_features),Path(args.candidate_features),Path(args.output_dir)
    baseline_manifest,candidate_manifest=read_json(baseline_dir/"manifest.json"),read_json(candidate_dir/"manifest.json")
    if baseline_manifest["bitboard"]["profileHash"]!=EXPECTED_BASELINE_HASH or candidate_manifest["bitboard"]["profileHash"]!=EXPECTED_CANDIDATE_HASH: raise ValidationError("Feature profile identity mismatch")
    baseline=read_jsonl(baseline_dir/"features.jsonl"); candidate=read_jsonl(candidate_dir/"features.jsonl")
    candidate_by_id={record["positionId"]:record for record in candidate}; changed=read_json(Path(args.candidate_dir)/"changed-parameters.json")["parameters"]
    deltas={item["registryName"]:item["candidateValue"]-item["baselineValue"] for item in changed}
    cp_records=[]; baseline_predictions=[]; candidate_predictions=[]; delta_values=[]; mismatches=[]
    for record in baseline:
        other=candidate_by_id.get(record["positionId"])
        if other is None or other["fen"]!=record["fen"]: raise ValidationError(f"Candidate feature join missing: {record['positionId']}")
        if record["stockfish"]["scoreType"]!="cp": continue
        expected=evaluation_tuning.predict_exact(record,deltas); actual=int(other["scores"]["finalWhiteCp"])
        mismatch=actual-expected
        if mismatch: mismatches.append({"positionId":record["positionId"],"mismatchCp":mismatch})
        cp_records.append(record); baseline_predictions.append(int(record["scores"]["finalWhiteCp"])); candidate_predictions.append(actual); delta_values.append(actual-baseline_predictions[-1])
    def report(predictions: Sequence[int]) -> dict[str,Any]:
        overall=evaluation_tuning.metric(cp_records,predictions)
        breakdown={}
        keys={"gamePhase":lambda r:r["gamePhase"],"sideToMove":lambda r:r["sideToMove"],"resultClass":lambda r:evaluation_tuning.result_class(1.0 if (r["gameResult"]=="1-0" and r["sideToMove"]=="white") or (r["gameResult"]=="0-1" and r["sideToMove"]=="black") else .5 if r["gameResult"]=="1/2-1/2" else 0.0)}
        for group,key in keys.items():
            breakdown[group]={}
            for label in sorted({key(r) for r in cp_records}):
                indexes=[i for i,r in enumerate(cp_records) if key(r)==label]
                breakdown[group][label]=evaluation_tuning.metric([cp_records[i] for i in indexes],[predictions[i] for i in indexes])
        return {"overall":overall,"breakdowns":breakdown}
    baseline_metrics,candidate_metrics=report(baseline_predictions),report(candidate_predictions)
    phase_changes={phase:pct(candidate_metrics["breakdowns"]["gamePhase"][phase]["mae"],baseline_metrics["breakdowns"]["gamePhase"][phase]["mae"]) for phase in PHASES}
    mae_change=pct(candidate_metrics["overall"]["mae"],baseline_metrics["overall"]["mae"]); rmse_change=pct(candidate_metrics["overall"]["rmse"],baseline_metrics["overall"]["rmse"])
    if mismatches or mae_change>5 or rmse_change>7 or max(phase_changes.values())>20: classification="fail"
    elif mae_change>2 or rmse_change>3 or max(phase_changes.values())>10: classification="warning"
    else: classification="pass"
    absolute=[abs(value) for value in delta_values]; sorted_delta=sorted(delta_values)
    def percentile(values: Sequence[float], q: float) -> float:
        if not values:return 0.0
        index=(len(values)-1)*q; lower=math.floor(index); upper=math.ceil(index)
        return values[lower] if lower==upper else values[lower]+(values[upper]-values[lower])*(index-lower)
    result={"schemaVersion":1,"candidateProfileHash":EXPECTED_CANDIDATE_HASH,"records":len(cp_records),"baseline":baseline_metrics,"candidate":candidate_metrics,"percentageChanges":{"mae":mae_change,"rmse":rmse_change,"phaseMae":phase_changes},"analyticalVerification":{"positionsChecked":len(cp_records),"mismatches":len(mismatches),"maximumMismatchCp":max((abs(x["mismatchCp"]) for x in mismatches),default=0),"unchangedEvaluations":sum(value==0 for value in delta_values),"changedEvaluations":sum(value!=0 for value in delta_values),"meanAbsoluteDeltaCp":statistics.fmean(absolute),"medianDeltaCp":statistics.median(delta_values),"maximumAbsoluteDeltaCp":max(absolute,default=0),"percentilesCp":{str(q):percentile(sorted_delta,q/100) for q in (0,10,25,50,75,90,100)}},"guardrailClassification":classification,"developmentOnly":True,"promotionEligible":False}
    output.mkdir(parents=True,exist_ok=True); write(output/"static-audit.json",pretty_json(result).encode()); write(output/"failures.jsonl",jsonl(mismatches))
    print(f"Static audit: {classification}; CP={len(cp_records)}; analytical mismatches={len(mismatches)}")


class UciEngine:
    def __init__(self,path:Path,timeout:float=30.0):
        self.path=path;self.timeout=timeout;self.malformed_info_lines=0;self.process=subprocess.Popen([str(path.resolve()),"--mode=gui"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        self._line_buffer=IncrementalLineBuffer();self.malformed_info_records:list[str]=[]
        self.send("uci");self._until(lambda line:line=="uciok");self.send("setoption name OwnBook value false");self.send("isready");self._until(lambda line:line=="readyok")
    def send(self,text:str)->None:
        assert self.process.stdin is not None;self.process.stdin.write((text+"\n").encode());self.process.stdin.flush()
    def read(self)->str:
        assert self.process.stdout is not None
        deadline=time.monotonic()+self.timeout
        while True:
            line=self._line_buffer.pop()
            if line is not None:return line
            remaining=deadline-time.monotonic()
            if remaining<=0 or not select.select([self.process.stdout],[],[],remaining)[0]:raise ValidationError(f"UCI timeout: {self.path}")
            chunk=os.read(self.process.stdout.fileno(),4096)
            if not chunk:
                suffix=f" with incomplete line {self._line_buffer.incomplete!r}" if self._line_buffer.incomplete else ""
                raise ValidationError(f"UCI EOF: {self.path}{suffix}")
            self._line_buffer.feed(chunk)
    def _until(self,predicate:Callable[[str],bool])->list[str]:
        lines=[]
        while True:
            line=self.read();lines.append(line)
            if predicate(line):return lines
    def _search_command(self,board:chess.Board,command:str)->dict[str,Any]:
        malformed_before=self.malformed_info_lines;self.send("ucinewgame");self.send("position fen "+board.fen(en_passant="legal"));start=time.monotonic();self.send(command);lines=self._until(lambda line:line.startswith("bestmove "));elapsed=time.monotonic()-start
        info={};mate_scores=[];best_line=lines[-1];time_info={}
        for line in lines:
            tokens=line.split()
            if tokens[:4]==["info","string","time","stopReason"]:
                try:
                    time_info={"stopReason":tokens[4]}
                    for key in ("hardBudgetMs","searchElapsedMs","deadlineChecks","lastDeadlineCheckMs"):
                        index=tokens.index(key);time_info[key]=int(tokens[index+1])
                except (ValueError,IndexError):
                    self.malformed_info_lines+=1;self.malformed_info_records.append(line)
                continue
            try:item=parse_uci_search_info(line)
            except ValidationError:
                self.malformed_info_lines+=1;self.malformed_info_records.append(line);continue
            if item is None:continue
            if item["scoreType"]=="mate":mate_scores.append(item)
            info=item
        move_text=best_line.split()[1];move=None if move_text=="0000" else chess.Move.from_uci(move_text)
        legal=move is not None and move in board.legal_moves
        if not legal and not board.is_game_over(claim_draw=True):raise ValidationError(f"Illegal bestmove {move_text} for {board.fen()}")
        score_stm=info.get("scoreValue") if info.get("scoreType")=="cp" else None
        status="forced_legal_move" if info.get("depth") is None and board.legal_moves.count()==1 else "complete"
        response_overhead=max(0.0,elapsed*1000-time_info.get("searchElapsedMs",elapsed*1000))
        return {"bestMove":move_text,"move":move,"legal":legal,"depth":info.get("depth"),"seldepth":info.get("seldepth"),"nodes":info.get("nodes"),"tbhits":info.get("tbhits",0),"pv":info.get("pv",[]),"reportedTimeMs":info.get("time"),"elapsedSeconds":elapsed,"nps":(info.get("nodes",0)/elapsed if elapsed else None),"scoreType":info.get("scoreType"),"scoreValue":info.get("scoreValue"),"scoreFromSideToMove":score_stm,"scoreWhiteCp":score_stm if board.turn else (-score_stm if score_stm is not None else None),"mateScores":mate_scores,"malformedInfoLines":self.malformed_info_lines-malformed_before,"terminationStatus":status,**time_info,"uciResponseOverheadMs":response_overhead}
    def search(self,board:chess.Board,depth:int)->dict[str,Any]:
        return self._search_command(board,f"go depth {depth}")
    def search_clock(self,board:chess.Board,white_time_ms:int,black_time_ms:int,white_increment_ms:int=0,black_increment_ms:int=0,moves_to_go:int|None=None)->dict[str,Any]:
        command=f"go wtime {white_time_ms} btime {black_time_ms} winc {white_increment_ms} binc {black_increment_ms}"
        if moves_to_go is not None:command+=f" movestogo {moves_to_go}"
        return self._search_command(board,command)
    def close(self)->None:
        try:
            if self.process.poll() is None:self.send("quit")
            self.process.wait(timeout=2)
        except Exception:
            if self.process.poll() is None:self.process.terminate()
            try:self.process.wait(timeout=1)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=1)
        finally:
            for stream in (self.process.stdin,self.process.stdout,self.process.stderr):
                if stream:stream.close()
    def __enter__(self):return self
    def __exit__(self,*_):self.close()


def choose_search_subset(records: Sequence[dict[str,Any]], count:int=60)->list[dict[str,Any]]:
    selected,_=evaluation_tuning.select_split(list(records),count,1,SEARCH_SEED)
    return selected


def search_once(records:Sequence[dict[str,Any]],baseline_path:Path,candidate_path:Path,depth:int,features:Mapping[str,dict[str,Any]],annotations:Mapping[str,dict[str,Any]],changed_names:Sequence[str])->tuple[list[dict[str,Any]],list[dict[str,Any]]]:
    canonical=[];performance=[]
    with UciEngine(baseline_path) as baseline,UciEngine(candidate_path) as candidate:
        for record in records:
            board=chess.Board(record["fen"]);base=baseline.search(board,depth);cand=candidate.search(board,depth);annotation=annotations[record["positionId"]]
            activated=[name for name in changed_names if features[record["positionId"]]["features"][name]["coefficients"]]
            item={"positionId":record["positionId"],"fen":record["fen"],"gamePhase":record["gamePhase"],"sideToMove":record["sideToMove"],"baselineBestMove":base["bestMove"],"candidateBestMove":cand["bestMove"],"stockfishBestMove":annotation.get("bestMoveUci"),"baselineScoreWhiteCp":base["scoreWhiteCp"],"candidateScoreWhiteCp":cand["scoreWhiteCp"],"stockfishScoreWhiteCp":annotation.get("scoreWhiteCp"),"baselineDepth":base["depth"],"candidateDepth":cand["depth"],"baselineNodes":base["nodes"],"candidateNodes":cand["nodes"],"baselineLegal":base["legal"],"candidateLegal":cand["legal"],"malformedInfoLines":base["malformedInfoLines"]+cand["malformedInfoLines"],"terminationStatus":base["terminationStatus"] if base["terminationStatus"]==cand["terminationStatus"] else "complete","activatedChangedParameters":activated}
            canonical.append(item);performance.append({"positionId":record["positionId"],"baselineElapsedSeconds":base["elapsedSeconds"],"candidateElapsedSeconds":cand["elapsedSeconds"],"baselineNps":base["nps"],"candidateNps":cand["nps"]})
    return canonical,performance


def search_audit_command(args:argparse.Namespace)->None:
    verify_frozen(Path(args.candidate_dir),Path(args.candidate_header),Path(args.candidate_engine),Path(args.baseline_engine))
    selection=read_jsonl(Path(args.selection_dir)/"selection.jsonl");records=choose_search_subset(selection,args.count)
    features={r["positionId"]:r for r in read_jsonl(Path(args.baseline_features)/"features.jsonl")};annotations={r["positionId"]:r for r in read_jsonl(Path(args.annotation_dir)/"annotations.jsonl")};changed_names=[r["registryName"] for r in read_json(Path(args.candidate_dir)/"changed-parameters.json")["parameters"]]
    first,performance=search_once(records,Path(args.baseline_engine),Path(args.candidate_engine),args.depth,features,annotations,changed_names)
    second,_=search_once(records,Path(args.baseline_engine),Path(args.candidate_engine),args.depth,features,annotations,changed_names)
    if first!=second:raise ValidationError("Fixed-depth canonical search output is not deterministic")
    changed=[item for item in first if item["baselineBestMove"]!=item["candidateBestMove"]]
    base_agree=sum(item["baselineBestMove"]==item["stockfishBestMove"] for item in first);cand_agree=sum(item["candidateBestMove"]==item["stockfishBestMove"] for item in first)
    ratios=[item["candidateElapsedSeconds"]/item["baselineElapsedSeconds"] for item in performance if item["baselineElapsedSeconds"]>0]
    node_ratios=[item["candidateNodes"]/item["baselineNodes"] for item in first if item["baselineNodes"] and item["candidateNodes"]]
    nps_ratios=[item["candidateNps"]/item["baselineNps"] for item in performance if item["baselineNps"] and item["candidateNps"]]
    p90=lambda values:sorted(values)[max(0,math.ceil(.9*len(values))-1)]
    baseline_nodes=[item["baselineNodes"] for item in first if item["baselineNodes"] is not None];candidate_nodes=[item["candidateNodes"] for item in first if item["candidateNodes"] is not None]
    perf={"schemaVersion":1,"nonCanonicalTiming":True,"positions":len(performance),"completedDepth":{"baseline":sum(item["baselineDepth"]==args.depth for item in first),"candidate":sum(item["candidateDepth"]==args.depth for item in first)},"validForcedMovePositions":sum(item["terminationStatus"]=="forced_legal_move" for item in first),"baselineMedianNodes":statistics.median(baseline_nodes),"candidateMedianNodes":statistics.median(candidate_nodes),"medianNodeRatio":statistics.median(node_ratios),"p90NodeRatio":p90(node_ratios),"baselineMedianElapsedSeconds":statistics.median(x["baselineElapsedSeconds"] for x in performance),"candidateMedianElapsedSeconds":statistics.median(x["candidateElapsedSeconds"] for x in performance),"medianElapsedRatio":statistics.median(ratios),"p90ElapsedRatio":p90(ratios),"medianNpsRatio":statistics.median(nps_ratios),"p90NpsRatio":p90(nps_ratios),"severeSlowdownWarning":statistics.median(ratios)>1.25,"records":performance}
    output=Path(args.output_dir);output.mkdir(parents=True,exist_ok=True);write(output/"search-audit.jsonl",jsonl(first));write(output/"move-changes.jsonl",jsonl(changed));write(output/"performance.json",pretty_json(perf).encode())
    summary={"positions":len(first),"requestedDepth":args.depth,"sameBestMoves":len(first)-len(changed),"differentBestMoves":len(changed),"baselineStockfishAgreement":base_agree,"candidateStockfishAgreement":cand_agree,"bothAgree":sum(i["baselineBestMove"]==i["candidateBestMove"]==i["stockfishBestMove"] for i in first),"neitherAgree":sum(i["baselineBestMove"]!=i["stockfishBestMove"] and i["candidateBestMove"]!=i["stockfishBestMove"] for i in first),"illegalMoves":0,"crashes":0,"protocolFailures":0,"malformedInfoLines":sum(i["malformedInfoLines"] for i in first),"deterministicRerun":True}
    write(output/"search-summary.json",pretty_json(summary).encode());print(f"Search audit: {len(first)} positions, {len(changed)} changed best moves")


STARTS=(
 ("normal-start",chess.STARTING_FEN),("normal-developed","r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 6 5"),
 ("open-center","r1bq1rk1/ppp2ppp/2np1n2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 4 8"),("open-files","r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2N1PN2/PP2BPPP/R2Q1RK1 w - - 2 10"),
 ("closed-center","r1bq1rk1/pp2nppp/2p1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 2 9"),("closed-locked","r1bq1rk1/pp3ppp/2n1pn2/2pp4/2PP4/1PN1PN2/PB3PPP/R2Q1RK1 w - - 0 11"),
 ("queenless-middle","r3r1k1/ppp2ppp/2n2n2/3p4/3P4/2N1PN2/PPP2PPP/R3R1K1 w - - 0 14"),("queenless-rooks","4rrk1/pp3ppp/2p1bn2/3p4/3P4/2P1PN2/PP3PPP/3RR1K1 w - - 0 18"),
 ("pawn-endgame","8/5pk1/6p1/3p4/3P4/5P2/6PK/8 w - - 0 35"),("rook-endgame","8/5pk1/6p1/8/3R4/5P2/6PK/3r4 w - - 0 40"),
)


def play_smoke_game(label:str,fen:str,baseline_white:bool,baseline:UciEngine,candidate:UciEngine,depth:int,max_plies:int,index:int)->tuple[dict[str,Any],str]:
    board=chess.Board(fen);divergences=0;plies=0;failure=None;malformed=0
    while not board.is_game_over(claim_draw=True) and plies<max_plies:
        base=baseline.search(board,depth);cand=candidate.search(board,depth);divergences+=base["bestMove"]!=cand["bestMove"];malformed+=base["malformedInfoLines"]+cand["malformedInfoLines"]
        baseline_turn=board.turn==chess.WHITE if baseline_white else board.turn==chess.BLACK
        selected=base if baseline_turn else cand
        if selected["move"] is None or selected["move"] not in board.legal_moves:
            failure="illegal_move";break
        board.push(selected["move"]);plies+=1
    outcome=board.outcome(claim_draw=True);adjudicated=outcome is None and failure is None
    result=outcome.result() if outcome else ("1/2-1/2" if adjudicated else "*")
    termination=(outcome.termination.name.lower() if outcome else "maximum_ply" if adjudicated else failure)
    game=chess.pgn.Game.from_board(board);game.headers["Event"]="Bitboard Phase 16 smoke";game.headers["Site"]="Local";game.headers["Round"]=str(index);game.headers["White"]="baseline" if baseline_white else "candidate";game.headers["Black"]="candidate" if baseline_white else "baseline";game.headers["Result"]=result;game.headers["Termination"]=str(termination);game.headers["StartLabel"]=label
    exporter=chess.pgn.StringExporter(headers=True,variations=False,comments=False);pgn=str(game.accept(exporter)).strip()+"\n"
    winner="draw"
    if result in ("1-0","0-1"):
        white_won=result=="1-0";winner="baseline" if white_won==baseline_white else "candidate"
    record={"gameIndex":index,"startLabel":label,"startFen":fen,"baselineColor":"white" if baseline_white else "black","result":result,"winner":winner,"termination":termination,"maximumPlyAdjudication":adjudicated,"plies":plies,"moveDivergencePositions":divergences,"illegalMoves":1 if failure=="illegal_move" else 0,"crashes":0,"protocolFailures":0,"malformedInfoLines":malformed}
    return record,pgn


def run_smoke(baseline_path:Path,candidate_path:Path,depth:int,max_plies:int)->tuple[list[dict[str,Any]],list[str]]:
    records=[];pgns=[]
    with UciEngine(baseline_path) as baseline,UciEngine(candidate_path) as candidate:
        for start_index,(label,fen) in enumerate(STARTS):
            for reverse in (False,True):
                record,pgn=play_smoke_game(label,fen,not reverse,baseline,candidate,depth,max_plies,len(records)+1);records.append(record);pgns.append(pgn)
    return records,pgns


def smoke_match_command(args:argparse.Namespace)->None:
    verify_frozen(Path(args.candidate_dir),Path(args.candidate_header),Path(args.candidate_engine),Path(args.baseline_engine));output=Path(args.output_dir)
    records,pgns=run_smoke(Path(args.baseline_engine),Path(args.candidate_engine),args.depth,args.max_plies)
    rerun_records,rerun_pgns=run_smoke(Path(args.baseline_engine),Path(args.candidate_engine),args.depth,args.max_plies)
    if records!=rerun_records or pgns!=rerun_pgns:raise ValidationError("Smoke match canonical rerun is not deterministic")
    games=output/"games";games.mkdir(parents=True,exist_ok=True)
    for record,pgn in zip(records,pgns):write(games/f"game-{record['gameIndex']:02d}.pgn",pgn.encode())
    summary={"schemaVersion":1,"games":len(records),"candidateWins":sum(r["winner"]=="candidate" for r in records),"baselineWins":sum(r["winner"]=="baseline" for r in records),"draws":sum(r["winner"]=="draw" for r in records),"maximumPlyAdjudications":sum(r["maximumPlyAdjudication"] for r in records),"illegalMoves":sum(r["illegalMoves"] for r in records),"crashes":sum(r["crashes"] for r in records),"protocolFailures":sum(r["protocolFailures"] for r in records),"malformedInfoLines":sum(r["malformedInfoLines"] for r in records),"averageGameLengthPlies":statistics.fmean(r["plies"] for r in records),"moveDivergencePositions":sum(r["moveDivergencePositions"] for r in records),"deterministicRerun":True,"configuration":{"startingPositions":len(STARTS),"colorReversal":True,"depth":args.depth,"maximumPlies":args.max_plies,"openingBook":False,"ponder":False,"clocks":False},"records":records}
    write(output/"smoke-match.json",pretty_json(summary).encode());print(f"Smoke match: {len(records)} games, illegal={summary['illegalMoves']}, crashes={summary['crashes']}")


def inspect_command(args:argparse.Namespace)->None:
    frozen=verify_frozen(Path(args.candidate_dir),Path(args.candidate_header),Path(args.candidate_engine),Path(args.baseline_engine));output=Path(args.output_dir)
    static=read_json(output/"static-audit.json");search=read_json(output/"search-summary.json");smoke=read_json(output/"smoke-match.json");performance=read_json(output/"performance.json")
    technical_fail=static["guardrailClassification"]=="fail" or static["analyticalVerification"]["mismatches"] or search["illegalMoves"] or search["crashes"] or search["protocolFailures"] or smoke["illegalMoves"] or smoke["crashes"] or smoke["protocolFailures"]
    warnings=[]
    selection_manifest=read_json(Path(args.selection_dir)/"manifest.json")
    if selection_manifest.get("actualCounts",{}).get("test",0) < selection_manifest.get("requestedCounts",{}).get("test",0):warnings.append("audit_selection_corpus_deficit")
    if static["guardrailClassification"]=="warning":warnings.append("static_metric_guardrail")
    if performance["severeSlowdownWarning"]:warnings.append("candidate_median_elapsed_over_25_percent")
    if search.get("malformedInfoLines",0) or smoke.get("malformedInfoLines",0):warnings.append("preexisting_malformed_intermediate_mate_score_lines")
    status="rejected" if technical_fail else "validated_with_warnings" if warnings else "validated_for_experimental_use"
    names=["static-audit.json","search-audit.jsonl","move-changes.jsonl","performance.json","smoke-match.json","failures.jsonl","search-summary.json"]
    artifacts={name:{"sha256":sha256_file(output/name),**({"records":len(read_jsonl(output/name))} if name.endswith(".jsonl") else {})} for name in names}
    selection=Path(args.selection_dir);annotations=Path(args.annotation_dir)
    game_artifacts={path.name:{"sha256":sha256_file(path)} for path in sorted((output/"games").glob("game-*.pgn"))}
    smoke=read_json(output/"smoke-match.json")
    manifest={"schemaVersion":1,"candidateIdentity":frozen["candidate"],"candidateHeaderSha256":frozen["candidateHeaderSha256"],"baselineIdentity":frozen["baseline"],"phase15CandidateManifestSha256":sha256_file(Path(args.candidate_dir)/"manifest.json"),"auditSelectionSha256":sha256_file(selection/"selection.jsonl"),"stockfish":{"manifestSha256":sha256_file(annotations/"manifest.json"),"engineName":read_json(annotations/"manifest.json")["engine"]["engineName"],"binarySha256":read_json(annotations/"manifest.json")["engine"]["engineBinarySha256"]},"searchDepth":search.get("requestedDepth"),"gameConfiguration":smoke.get("configuration"),"validationStatus":status,"warnings":warnings,"developmentOnly":True,"promotionEligible":False,"artifacts":artifacts,"gameArtifacts":game_artifacts}
    write(output/"manifest.json",pretty_json(manifest).encode());summary={"schemaVersion":1,"validationStatus":status,"staticAudit":static["guardrailClassification"],"searchPositions":search["positions"],"changedBestMoves":search["differentBestMoves"],"smokeGames":smoke["games"],"engineFailures":search["crashes"]+search["protocolFailures"]+smoke["crashes"]+smoke["protocolFailures"],"warnings":warnings,"developmentOnly":True,"promotionEligible":False};write(output/"summary.json",pretty_json(summary).encode())
    print(f"Validation status: {status}")


def parser()->argparse.ArgumentParser:
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest="command",required=True)
    def common(command):
        command.add_argument("--candidate-dir",default=str(DEFAULT_CANDIDATE));command.add_argument("--candidate-header",default=str(DEFAULT_CANDIDATE_HEADER));command.add_argument("--candidate-engine",default=str(DEFAULT_CANDIDATE_ENGINE));command.add_argument("--baseline-engine",default=str(DEFAULT_BASELINE_ENGINE))
    select_command=commands.add_parser("audit-select");common(select_command);select_command.add_argument("--dataset-dir",default=str(DEFAULT_DATASET));select_command.add_argument("--phase15-selection",default=str(DEFAULT_PHASE15_SELECTION));select_command.add_argument("--output-dir",default=str(DEFAULT_AUDIT_SELECTION));select_command.add_argument("--count",type=int,default=300);select_command.add_argument("--seed",default=AUDIT_SEED);select_command.add_argument("--force",action="store_true");select_command.set_defaults(function=audit_select_command)
    static=commands.add_parser("static-audit");common(static);static.add_argument("--baseline-features",default=str(DEFAULT_BASELINE_FEATURES));static.add_argument("--candidate-features",default=str(DEFAULT_CANDIDATE_FEATURES));static.add_argument("--output-dir",default=str(DEFAULT_OUTPUT));static.set_defaults(function=static_audit_command)
    search=commands.add_parser("search-audit");common(search);search.add_argument("--selection-dir",default=str(DEFAULT_AUDIT_SELECTION));search.add_argument("--annotation-dir",default=str(DEFAULT_ANNOTATIONS));search.add_argument("--baseline-features",default=str(DEFAULT_BASELINE_FEATURES));search.add_argument("--output-dir",default=str(DEFAULT_OUTPUT));search.add_argument("--count",type=int,default=60);search.add_argument("--depth",type=int,default=6);search.set_defaults(function=search_audit_command)
    smoke=commands.add_parser("smoke-match");common(smoke);smoke.add_argument("--output-dir",default=str(DEFAULT_OUTPUT));smoke.add_argument("--depth",type=int,default=4);smoke.add_argument("--max-plies",type=int,default=160);smoke.set_defaults(function=smoke_match_command)
    inspect=commands.add_parser("inspect");common(inspect);inspect.add_argument("--selection-dir",default=str(DEFAULT_AUDIT_SELECTION));inspect.add_argument("--annotation-dir",default=str(DEFAULT_ANNOTATIONS));inspect.add_argument("--output-dir",default=str(DEFAULT_OUTPUT));inspect.set_defaults(function=inspect_command)
    return parser


def main(argv:Sequence[str]|None=None)->int:
    try:args=parser().parse_args(argv);args.function(args);return 0
    except (ValidationError,OSError,json.JSONDecodeError,subprocess.SubprocessError,pgn_dataset.DatasetError) as error:print(f"error: {error}",file=sys.stderr);return 2


if __name__=="__main__":raise SystemExit(main())
