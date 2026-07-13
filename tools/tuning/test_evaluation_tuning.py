#!/usr/bin/env python3
"""Focused synthetic tests for the Phase 15 prototype tooling."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

import evaluation_tuning as tuning


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def fixture_positions(split: str, games: int = 180) -> list[dict]:
    records = []
    for index in range(games):
        for side in tuning.SIDES:
            phase = tuning.PHASES[index % 3]
            result = tuning.RESULTS[(index // 3) % 3]
            result_value = {"win": 1.0, "draw": 0.5, "loss": 0.0}[result]
            records.append({
                "positionId": f"{split}-{index}-{side}", "gameId": f"{split}-game-{index}",
                "split": split, "sideToMove": side, "gamePhase": phase,
                "resultFromSideToMove": result_value,
            })
    return records


def main() -> int:
    source = fixture_positions("train")
    first, warnings = tuning.select_split(source, 180, 2, "seed")
    second, _ = tuning.select_split(source, 180, 2, "seed")
    require([r["positionId"] for r in first] == [r["positionId"] for r in second], "selection is not deterministic")
    require(len(first) == len({r["positionId"] for r in first}) == 180, "selection count/uniqueness")
    summary = tuning.counts_summary(first)
    require(summary["sideToMove"] == {"white": 90, "black": 90}, "side balance")
    require(set(k for k,v in summary["gamePhase"].items() if v) == set(tuning.PHASES), "phase coverage")
    require(set(k for k,v in summary["resultClass"].items() if v) == set(tuning.RESULTS), "result coverage")
    require(summary["maximumPositionsPerGame"] <= 2 and not warnings, "game cap/fallback")

    registry = {"minimum": 0, "maximum": 100, "step": 5}
    candidate, detail = tuning.quantize(31.0, 50, registry, 20)
    require(candidate == 70 and detail["clamped"], "prototype delta cap")
    require(detail["quantizedValue"] == 80 and candidate % 5 == 0, "bounds/step quantization")
    bounded, bounded_detail = tuning.quantize(-200.0, 50, registry, 100)
    require(bounded == 0 and bounded_detail["quantizedValue"] == 0, "registry bounds")

    rows = [[1.0] + [0.0] * 9, [2.0] + [0.0] * 9]
    # Positive regularization makes the otherwise empty columns invertible.
    fit1 = tuning.ridge(rows, [3.0, 6.0], 1.0)
    fit2 = tuning.ridge(rows, [3.0, 6.0], 1.0)
    require(fit1 == fit2 and fit1[0] > 0, "ridge determinism")
    require(tuning.DISCONNECTED not in tuning.ALLOWLIST and len(tuning.ALLOWLIST) == 10, "allowlist/disconnected exclusion")

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        selection = [{"positionId": "a"}, {"positionId": "a"}]
        (directory / "selection.jsonl").write_text("".join(tuning.canonical_json(r)+"\n" for r in selection))
        manifest = {"artifacts": {"selection.jsonl": {"sha256": tuning.sha256_file(directory/"selection.jsonl"), "records": 2}}, "sourceDataset": {"manifestSha256": "m", "positionsSha256": "p"}}
        (directory / "manifest.json").write_text(tuning.pretty_json(manifest))
        # Duplicate explicit IDs are rejected before dataset lookup.
        import stockfish_annotate
        try:
            stockfish_annotate.select_positions_from_manifest([], directory/"manifest.json", {"manifestSha256":"m","positionsSha256":"p","datasetDirectory":str(directory)})
            raise AssertionError("duplicate selection accepted")
        except stockfish_annotate.AnnotationError:
            pass

    print("Phase 15 synthetic tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
