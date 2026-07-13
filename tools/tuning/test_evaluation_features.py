#!/usr/bin/env python3
"""Focused Phase 14 exporter/join/sensitivity tests."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tuning"))
import evaluation_features as ef

ENGINE = ROOT / "engine/chess-engine"
REGISTRY = ROOT / "tuning/parameter-registry.json"
FIXTURES = ROOT / "tools/tuning/evaluation_feature_fixtures.json"


class Failure(Exception): pass


def require(condition: bool, message: str) -> None:
    if not condition: raise Failure(message)


def engine_run(fens: list[str]) -> tuple[bytes, bytes, int]:
    process = subprocess.run([str(ENGINE), "--mode=eval-features"], input=("\n".join(fens)+"\n").encode(), capture_output=True)
    return process.stdout, process.stderr, process.returncode


def main() -> int:
    passed = 0
    registry, _ = ef.load_registry(REGISTRY)
    fixtures = json.loads(FIXTURES.read_text())["fixtures"]
    fens = [fixture["fen"] for fixture in fixtures]

    out, err, code = engine_run(fens)
    require(code == 0 and not err, "synthetic exporter run failed")
    records = [json.loads(line) for line in out.splitlines()]
    require(len(records) == len(fixtures), "multiple-FEN processing count mismatch")
    for record in records:
        ef.validate_sparse_features(record["features"], registry)
        ef.validate_reconstruction(record)
        require(record["profileId"] == ef.EXPECTED_PROFILE_ID and record["profileHash"] == ef.EXPECTED_PROFILE_HASH, "profile identity mismatch")
        require(list(record["features"]) == sorted(record["features"]), "feature ordering is unstable")
        require(record["scores"]["finalFromSideToMoveCp"] == (record["scores"]["finalWhiteCp"] if record["sideToMove"] == "white" else -record["scores"]["finalWhiteCp"]), "perspective mismatch")
    passed += 18

    again, _, _ = engine_run(fens)
    require(again == out, "repeated export is not byte-identical")
    invalid_out, invalid_err, invalid_code = engine_run(["not a fen"])
    require(invalid_code == 0 and not invalid_out and b"invalid FEN" in invalid_err, "invalid FEN handling mismatch")
    passed += 3

    # Orientation, sparse arrays, and conditional activation.
    white, _, _ = engine_run(["4k3/8/8/8/3N4/8/P7/4K3 w - - 0 1"])
    black, _, _ = engine_run(["4k3/8/p7/3n4/8/8/8/4K3 b - - 0 1"])
    wr, br = json.loads(white), json.loads(black)
    require("knight.d4" in wr["features"]["evaluation.pst.mgPesto"]["coefficients"], "White PST orientation missing")
    require("knight.d4" in br["features"]["evaluation.pst.mgPesto"]["coefficients"], "Black PST mirroring missing")
    require(any(r["features"]["evaluation.mobility.mg"]["coefficients"] for r in records), "mobility not activated")
    require(any(r["features"]["evaluation.pawns.connectedByRank.mg"]["coefficients"] for r in records), "pawn rank arrays not activated")
    require(any(r["features"]["evaluation.king.attackPressure"]["coefficients"] for r in records), "king arrays not activated")
    require(any(r["features"]["evaluation.endgame.mopUpWeights"]["endgameContribution"] for r in records), "mop-up not activated")
    passed += 8

    uci = subprocess.run([str(ENGINE), "--mode=gui"], input=b"uci\nquit\n", capture_output=True, check=True).stdout
    require(b"uciok" in uci and ef.EXPECTED_PROFILE_HASH.encode() in uci, "normal UCI behavior/identity changed")
    passed += 1

    dataset = ROOT / "tuning/datasets/pgn-derived-v1"
    annotations = ROOT / "tuning/annotations/stockfish-reference-v1-dev"
    if dataset.exists() and annotations.exists():
        with tempfile.TemporaryDirectory(prefix="phase14-test-") as temporary:
            output = Path(temporary) / "features"
            command = [sys.executable, str(ROOT/"tools/tuning/evaluation_features.py"), "export", "--dataset-dir", str(dataset), "--annotation-dir", str(annotations), "--engine", str(ENGINE), "--output-dir", str(output), "--split", "validation", "--max-positions", "5"]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
            first = {path.name:path.read_bytes() for path in output.iterdir() if path.is_file()}
            subprocess.run(command+["--force"], cwd=ROOT, check=True, capture_output=True)
            second = {path.name:path.read_bytes() for path in output.iterdir() if path.is_file()}
            require(first == second, "joined artifacts are not deterministic")
            ef.inspect_command(type("Args",(),{"feature_dir":str(output)})())
            manifest=ef.read_json(output/"manifest.json");summary=ef.read_json(output/"summary.json")
            require(manifest["evaluationRegistry"]["entries"]==44, "registry mapping incomplete")
            require(summary["reconstruction"]["exact"]==5 and summary["reconstruction"]["finalMismatches"]==0, "integration reconstruction failed")
            require((output/"sensitivity.json").is_file() and (output/"correlations.json").is_file(), "analysis artifacts missing")
            passed += 12
    else:
        print("[SKIP] private Phase 12/13 integration artifacts are absent")

    print(f"Phase 14 evaluation feature tests passed: {passed}")
    return 0


if __name__ == "__main__":
    try: raise SystemExit(main())
    except Failure as error: print(f"[FAIL] {error}", file=sys.stderr); raise SystemExit(1)
