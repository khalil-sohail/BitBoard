#!/usr/bin/env python3
"""Focused unit and integration tests for the Phase 12 PGN dataset pipeline."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path
from typing import Any, Callable

import chess
import chess.pgn

import pgn_dataset as dataset


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/tuning/pgn_dataset.py"
PROFILE = ROOT / "tuning/profiles/builtin-default-v1.json"
HEADER = ROOT / "engine/include/tuning/generated_tuning_values.hpp"

LEGAL = """[Event "Fixture"]
[Site "Local"]
[Date "2026.01.01"]
[Round "1"]
[White "White"]
[Black "Black"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0
"""

DRAW = """[Event "Draw Fixture"]
[Site "Local"]
[Date "2026.01.02"]
[Round "2"]
[White "Alpha"]
[Black "Beta"]
[Result "1/2-1/2"]

1. d4 d5 2. c4 e6 3. Nc3 Nf6 1/2-1/2
"""

BLACK_WIN = """[Event "Mate Fixture"]
[Site "Local"]
[Date "2026.01.03"]
[Round "3"]
[White "White"]
[Black "Black"]
[Result "0-1"]

1. f3 e5 2. g4 Qh4# 0-1
"""

PROMOTION = """[Event "Promotion"]
[Site "Local"]
[Date "2026.01.04"]
[Round "4"]
[White "White"]
[Black "Black"]
[Result "1-0"]
[SetUp "1"]
[FEN "4k3/P7/8/8/8/8/8/4K3 w - - 0 1"]

1. a8=Q+ Kf7 1-0
"""

CASTLING = """[Event "Castling"]
[Site "Local"]
[Date "2026.01.05"]
[Round "5"]
[White "White"]
[Black "Black"]
[Result "1/2-1/2"]
[SetUp "1"]
[FEN "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"]

1. O-O O-O-O 1/2-1/2
"""

EN_PASSANT = """[Event "En passant"]
[Site "Local"]
[Date "2026.01.06"]
[Round "6"]
[White "White"]
[Black "Black"]
[Result "1/2-1/2"]
[SetUp "1"]
[FEN "4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 1"]

1... d5 2. exd6 1/2-1/2
"""

MALFORMED = """[Event "Broken"]
[Site "Local"]
[Date "2026.01.07"]
[Round "7"]
[White "White"]
[Black "Black"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bh6 1-0
"""

UNFINISHED = """[Event "Unfinished"]
[Site "Local"]
[Date "2026.01.08"]
[Round "8"]
[White "White"]
[Black "Black"]
[Result "*"]

1. e4 e5 *
"""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def config(pgn_dir: Path, output: Path, **overrides: Any) -> dataset.BuildConfig:
    values: dict[str, Any] = {
        "pgn_dir": pgn_dir,
        "output_dir": output,
        "min_ply": 0,
        "sample_every": 1,
        "minimum_split_games": 1,
    }
    values.update(overrides)
    return dataset.BuildConfig(**values)


def build_fixture(root: Path, pgn_text: str = LEGAL, **overrides: Any) -> Path:
    pgn_dir = root / "pgn"
    output = root / "dataset"
    write(pgn_dir / "fixture.pgn", pgn_text)
    dataset.build_dataset(config(pgn_dir, output, **overrides))
    return output


def load_positions(output: Path) -> list[dict[str, Any]]:
    return dataset.read_jsonl(output / "positions.jsonl")


def parse_one(text: str) -> Any:
    import io
    return chess.pgn.read_game(io.StringIO(text), Visitor=dataset.ResultTrackingBuilder)


def rewrite_artifact(output: Path, name: str, records: list[dict[str, Any]]) -> None:
    (output / name).write_bytes(dataset.jsonl_bytes(records))
    manifest_path = output / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["artifacts"][name]["sha256"] = dataset.sha256_file(output / name)
    manifest["artifacts"][name]["records"] = len(records)
    manifest_path.write_text(dataset.pretty_json(manifest), encoding="utf-8")


class Runner:
    def __init__(self) -> None:
        self.count = 0
        self.skipped = 0

    def check(self, name: str, action: Callable[[], None]) -> None:
        try:
            action()
        except Exception as error:
            raise AssertionError(f"{name}: {error}") from error
        self.count += 1
        print(f"[PASS] {name}")

    def skip(self, name: str, detail: str) -> None:
        self.skipped += 1
        print(f"[SKIP] {name}: {detail}")


def main() -> int:
    runner = Runner()
    profile_before = PROFILE.read_bytes()
    header_before = HEADER.read_bytes()

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)

        def discovery_order() -> None:
            source = root / "discover"
            write(source / "z.pgn", LEGAL)
            write(source / "a.pgn", DRAW)
            write(source / "ignore.PGN", LEGAL)
            write(source / "note.txt", "not PGN")
            require([path.name for path in dataset.discover_pgn_files(source)] == ["a.pgn", "z.pgn"], "order/case mismatch")
        runner.check("discovers every lowercase PGN in lexical order", discovery_order)
        runner.check("ignores non-PGN and uppercase-extension files", lambda: require(
            all(path.suffix == ".pgn" for path in dataset.discover_pgn_files(root / "discover")), "unexpected file"))

        def no_pgn() -> None:
            empty = root / "empty"
            empty.mkdir()
            try:
                dataset.discover_pgn_files(empty)
            except dataset.DatasetError as error:
                require("No regular *.pgn" in str(error), "unclear error")
                return
            raise AssertionError("empty discovery accepted")
        runner.check("fails clearly when no PGNs exist", no_pgn)

        legal_output = build_fixture(root / "legal")
        legal_positions = load_positions(legal_output)
        runner.check("parses a standard legal game", lambda: require(len(legal_positions) == 6, "expected six plies"))

        def multiple_games() -> None:
            output = build_fixture(root / "multiple", LEGAL + "\n" + DRAW)
            summary = json.loads((output / "summary.json").read_text())
            require(summary["games"]["accepted"] == 2, "two games not accepted")
        runner.check("parses multiple games in one file", multiple_games)

        promotion_output = build_fixture(root / "promotion", PROMOTION, include_terminal=True)
        promotion_positions = load_positions(promotion_output)
        runner.check("parses custom-FEN games", lambda: require(promotion_positions[0]["fen"].startswith("Q3k3/"), "custom FEN not replayed"))

        def tolerant_malformed() -> None:
            output = build_fixture(root / "tolerant", MALFORMED + "\n" + LEGAL)
            skipped = dataset.read_jsonl(output / "skipped-games.jsonl")
            require(skipped and skipped[0]["reasonCode"] == "pgn-parser-diagnostics", "malformed game not diagnosed")
        runner.check("skips malformed PGN in tolerant mode", tolerant_malformed)

        def strict_malformed() -> None:
            pgn_dir = root / "strict" / "pgn"
            write(pgn_dir / "broken.pgn", MALFORMED + "\n" + LEGAL)
            try:
                dataset.build_dataset(config(pgn_dir, root / "strict" / "out", strict=True))
            except dataset.DatasetError:
                return
            raise AssertionError("strict mode accepted malformed game")
        runner.check("fails on malformed PGN in strict mode", strict_malformed)

        def unsupported_result() -> None:
            output = build_fixture(root / "unfinished", UNFINISHED + "\n" + LEGAL)
            skipped = dataset.read_jsonl(output / "skipped-games.jsonl")
            require(skipped[0]["reasonCode"] == "unsupported-result", "unfinished game not skipped")
        runner.check("rejects unsupported star results", unsupported_result)
        runner.check("replays legal moves correctly", lambda: require(legal_positions[-1]["lastMoveUci"] == "a7a6", "last move mismatch"))

        def illegal_move_diagnostic() -> None:
            game = parse_one(MALFORMED)
            require(bool(game.errors), "python-chess did not expose illegal token")
        runner.check("detects illegal mainline moves through parser diagnostics", illegal_move_diagnostic)
        runner.check("extracts positions after each mainline move", lambda: require(legal_positions[0]["lastMoveUci"] == "e2e4" and "4P3" in legal_positions[0]["fen"], "wrong boundary"))
        runner.check("computes correct ply numbers", lambda: require([p["ply"] for p in legal_positions] == list(range(1, 7)), "ply mismatch"))
        runner.check("computes correct fullmove numbers", lambda: require([p["fullmoveNumber"] for p in legal_positions[:3]] == [1, 2, 2], "fullmove mismatch"))
        runner.check("computes correct side to move", lambda: require([p["sideToMove"] for p in legal_positions[:2]] == ["black", "white"], "turn mismatch"))
        runner.check("produces legal normalized FEN", lambda: require(all(chess.Board(p["fen"]).is_valid() for p in legal_positions), "invalid FEN"))
        runner.check("position key ignores counters and preserves state", lambda: require(all(len(p["positionKey"].split()) == 4 for p in legal_positions), "key fields mismatch"))

        castling_output = build_fixture(root / "castling", CASTLING)
        castling_positions = load_positions(castling_output)
        runner.check("preserves castling rights in normalized keys", lambda: require(castling_positions[0]["positionKey"].split()[2] == "kq", "castling rights mismatch"))

        ep_output = build_fixture(root / "ep", EN_PASSANT)
        ep_positions = load_positions(ep_output)
        runner.check("preserves only legal en-passant state", lambda: require(ep_positions[0]["positionKey"].endswith(" d6") and ep_positions[1]["positionKey"].endswith(" -"), "en-passant normalization mismatch"))
        runner.check("handles promotion moves", lambda: require(promotion_positions[0]["isPromotion"] and promotion_positions[0]["lastMoveUci"] == "a7a8q", "promotion mismatch"))
        runner.check("handles castling moves", lambda: require(castling_positions[0]["lastMoveUci"] == "e1g1" and castling_positions[1]["lastMoveUci"] == "e8c8", "castling UCI mismatch"))

        mate_excluded = build_fixture(root / "mate-excluded", BLACK_WIN)
        mate_included = build_fixture(root / "mate-included", BLACK_WIN, include_terminal=True)
        runner.check("recognizes checkmate terminal positions", lambda: require(chess.Board(load_positions(mate_included)[-1]["fen"]).is_checkmate(), "not mate"))
        runner.check("excludes terminal positions by default", lambda: require(len(load_positions(mate_excluded)) == 3, "terminal retained"))
        runner.check("includes terminal positions with option", lambda: require(len(load_positions(mate_included)) == 4, "terminal missing"))

        filtered_output = build_fixture(root / "filters", LEGAL, min_ply=3, sample_every=2)
        filtered_positions = load_positions(filtered_output)
        runner.check("applies minimum-ply filtering", lambda: require(filtered_positions[0]["ply"] == 3, "minimum ply mismatch"))
        runner.check("applies deterministic sampling", lambda: require([p["ply"] for p in filtered_positions] == [3, 5], "sampling mismatch"))
        runner.check("produces correct white-win labels", lambda: require(legal_positions[1]["whiteResult"] == 1.0 and legal_positions[1]["blackResult"] == 0.0, "white label mismatch"))
        black_positions = load_positions(mate_included)
        runner.check("produces correct black-win labels", lambda: require(black_positions[1]["whiteResult"] == 0.0 and black_positions[1]["blackResult"] == 1.0, "black label mismatch"))
        draw_positions = load_positions(build_fixture(root / "draw", DRAW))
        runner.check("produces correct draw labels", lambda: require(all(p["resultFromSideToMove"] == 0.5 for p in draw_positions), "draw label mismatch"))
        runner.check("computes result from side-to-move", lambda: require(legal_positions[0]["resultFromSideToMove"] == 0.0 and legal_positions[1]["resultFromSideToMove"] == 1.0, "perspective mismatch"))

        def stable_game_ids() -> None:
            first = parse_one(LEGAL)
            commented = parse_one(LEGAL.replace("1. e4", "1. e4 {comment ignored}"))
            moves1 = [move.uci() for move in first.mainline_moves()]
            moves2 = [move.uci() for move in commented.mainline_moves()]
            require(dataset.stable_game_id(first, moves1, "1-0") == dataset.stable_game_id(commented, moves2, "1-0"), "comment changed ID")
        runner.check("produces stable normalized game IDs", stable_game_ids)
        runner.check("produces stable position IDs", lambda: require(load_positions(build_fixture(root / "stable", LEGAL))[0]["positionId"] == legal_positions[0]["positionId"], "position ID drift"))

        def duplicate_games() -> None:
            pgn_dir = root / "dupgames" / "pgn"
            write(pgn_dir / "a.pgn", LEGAL)
            write(pgn_dir / "b.pgn", LEGAL)
            output = root / "dupgames" / "out"
            dataset.build_dataset(config(pgn_dir, output))
            skipped = dataset.read_jsonl(output / "skipped-games.jsonl")
            require(len(skipped) == 1 and skipped[0]["reasonCode"] == "duplicate-game", "duplicate game mismatch")
        runner.check("detects duplicate games across files", duplicate_games)

        def dedup_same_label() -> None:
            records = [dict(legal_positions[0]), dict(legal_positions[0])]
            records[1]["positionId"] = "sha256:" + "f" * 64
            retained, counts, conflicts = dataset.deduplicate_positions(records)
            require(len(retained) == 1 and counts["sameLabelDuplicatesRemoved"] == 1 and not conflicts, "same-label dedup mismatch")
        runner.check("deduplicates identical position keys", dedup_same_label)
        runner.check("removes same-label duplicate occurrences", dedup_same_label)

        def dedup_conflict() -> None:
            records = [dict(legal_positions[0]), dict(legal_positions[0])]
            records[1]["positionId"] = "sha256:" + "e" * 64
            records[1]["resultFromSideToMove"] = 1.0
            retained, counts, conflicts = dataset.deduplicate_positions(records)
            require(not retained and counts["conflictingLabelPositionKeys"] == 1 and len(conflicts) == 1, "conflict mismatch")
        runner.check("detects conflicting duplicate labels", dedup_conflict)
        runner.check("excludes all conflicting-label occurrences", dedup_conflict)

        runner.check("assigns every complete game to one split", lambda: require(len({p["split"] for p in legal_positions}) == 1, "game leaked"))
        runner.check("produces stable hash-based splits", lambda: require(dataset.split_for_game(legal_positions[0]["gameId"], dataset.DEFAULT_SPLIT_SEED, (80, 10, 10)) == legal_positions[0]["split"], "split drift"))

        def unrelated_game_no_reshuffle() -> None:
            before = legal_positions[0]["split"]
            output = build_fixture(root / "additional", DRAW + "\n" + LEGAL)
            after = next(p["split"] for p in load_positions(output) if p["gameId"] == legal_positions[0]["gameId"])
            require(before == after, "unrelated game reshuffled split")
        runner.check("adding a game does not reshuffle existing games", unrelated_game_no_reshuffle)

        def invalid_ratios() -> None:
            try:
                dataset.validate_config(config(root, root / "x", train_ratio=81))
            except dataset.DatasetError:
                return
            raise AssertionError("invalid ratios accepted")
        runner.check("validates ratio totals", invalid_ratios)

        runner.check("supports small corpora with empty splits", lambda: require(sum(1 for value in json.loads((legal_output / "summary.json").read_text())["splitGames"].values() if value == 0) >= 1, "all splits unexpectedly occupied"))

        def deterministic_bytes() -> None:
            first = build_fixture(root / "determinism-a", LEGAL + "\n" + DRAW)
            second = build_fixture(root / "determinism-b", LEGAL + "\n" + DRAW)
            for name in ("manifest.json", *dataset.CANONICAL_ARTIFACTS):
                require((first / name).read_bytes() == (second / name).read_bytes(), f"{name} differs")
        runner.check("produces byte-identical deterministic output", deterministic_bytes)

        runner.check("records correct source SHA-256 checksums", lambda: require(
            json.loads((legal_output / "manifest.json").read_text())["sources"][0]["sha256"] == dataset.sha256_file(root / "legal/pgn/fixture.pgn"), "source checksum mismatch"))
        runner.check("records correct artifact checksums", lambda: require(
            all(meta["sha256"] == dataset.sha256_file(legal_output / name) for name, meta in json.loads((legal_output / "manifest.json").read_text())["artifacts"].items()), "artifact checksum mismatch"))
        runner.check("records the exact legal PGN parser version", lambda: require(
            json.loads((legal_output / "manifest.json").read_text())["parser"] == {
                "distribution": "python-chess", "distributionVersion": "1.999", "libraryVersion": chess.__version__
            }, "parser metadata mismatch"))
        runner.check("marks the current corpus provisional", lambda: require(
            json.loads((legal_output / "summary.json").read_text())["corpusStatus"] == "provisional", "corpus status mismatch"))

        def refuse_overwrite() -> None:
            try:
                dataset.build_dataset(config(root / "legal/pgn", legal_output))
            except dataset.DatasetError as error:
                require("--force" in str(error), "missing force guidance")
                return
            raise AssertionError("overwrite accepted")
        runner.check("refuses overwrite without force", refuse_overwrite)

        def force_replace() -> None:
            before = (legal_output / "manifest.json").read_bytes()
            dataset.build_dataset(config(root / "legal/pgn", legal_output, force=True))
            require((legal_output / "manifest.json").read_bytes() == before, "forced deterministic rebuild changed bytes")
        runner.check("replaces output safely with force", force_replace)

        def no_partial_output() -> None:
            pgn_dir = root / "failure" / "pgn"
            write(pgn_dir / "bad.pgn", UNFINISHED)
            output = root / "failure" / "out"
            try:
                dataset.build_dataset(config(pgn_dir, output))
            except dataset.DatasetError:
                require(not output.exists(), "partial final output left behind")
                require(not list(output.parent.glob(".out.tmp-*")), "temporary output left behind")
                return
            raise AssertionError("invalid corpus built")
        runner.check("leaves no partial dataset on failure", no_partial_output)
        runner.check("inspect validates a correct dataset", lambda: require(dataset.validate_dataset(legal_output)["positions"] == 6, "inspect count mismatch"))

        def checksum_mismatch() -> None:
            output = build_fixture(root / "tamper", LEGAL)
            with (output / "positions.jsonl").open("ab") as handle:
                handle.write(b" ")
            try:
                dataset.validate_dataset(output)
            except dataset.DatasetError as error:
                require("checksum mismatch" in str(error) or "newline" in str(error), "wrong tamper error")
                return
            raise AssertionError("tampered dataset accepted")
        runner.check("inspect rejects checksum mismatches", checksum_mismatch)

        def cross_split_leakage() -> None:
            output = build_fixture(root / "leak", LEGAL)
            records = dataset.read_jsonl(output / "positions.jsonl")
            original_split = records[0]["split"]
            other_split = "test" if original_split != "test" else "train"
            records[1]["split"] = other_split
            rewrite_artifact(output, "positions.jsonl", records)
            for split in ("train", "validation", "test"):
                rewrite_artifact(output, f"{split}.jsonl", [r for r in records if r["split"] == split])
            try:
                dataset.validate_dataset(output)
            except dataset.DatasetError as error:
                require("Cross-split" in str(error), "wrong leakage error")
                return
            raise AssertionError("cross-split leakage accepted")
        runner.check("inspect rejects cross-split game leakage", cross_split_leakage)

        runner.check("manifest counts match JSONL records", lambda: require(
            json.loads((legal_output / "manifest.json").read_text())["counts"]["finalPositions"] == len(dataset.read_jsonl(legal_output / "positions.jsonl")), "manifest count mismatch"))
        runner.check("stores no absolute source paths", lambda: require(all(not Path(p["sourceFile"]).is_absolute() for p in legal_positions), "absolute path stored"))

        def original_unchanged() -> None:
            source = root / "original" / "pgn/source.pgn"
            write(source, LEGAL)
            before = source.read_bytes()
            dataset.build_dataset(config(source.parent, root / "original/out"))
            require(source.read_bytes() == before, "source PGN modified")
        runner.check("never modifies original PGN files", original_unchanged)
        runner.check("requires no Stockfish binary", lambda: require("chess.engine" not in TOOL.read_text(encoding="utf-8"), "engine evaluator dependency found"))
        runner.check("requires no Bitboard engine process", lambda: require("subprocess" not in TOOL.read_text(encoding="utf-8"), "engine process dependency found"))

        def phase_classification() -> None:
            opening = chess.Board()
            endgame = chess.Board("8/8/8/8/8/8/4K3/6k1 w - - 0 1")
            require(dataset.material_phase(opening, 12) == "opening", "opening phase mismatch")
            require(dataset.material_phase(opening, 40) == "middlegame", "middlegame phase mismatch")
            require(dataset.material_phase(endgame, 40) == "endgame", "endgame phase mismatch")
        runner.check("classifies game phases deterministically", phase_classification)

        def cli_missing_dependency_message() -> None:
            original = dataset.chess
            try:
                dataset.chess = None
                try:
                    dataset.require_python_chess()
                except dataset.DatasetError as error:
                    require("requirements.txt" in str(error), "missing dependency guidance")
                    return
                raise AssertionError("missing dependency accepted")
            finally:
                dataset.chess = original
        runner.check("reports a clear missing-parser dependency error", cli_missing_dependency_message)

        def conflicting_results() -> None:
            text = LEGAL.replace('[Result "1-0"]', '[Result "0-1"]')
            output = build_fixture(root / "result-conflict", text + "\n" + DRAW)
            skipped = dataset.read_jsonl(output / "skipped-games.jsonl")
            require(skipped[0]["reasonCode"] == "conflicting-result", "conflict not rejected")
        runner.check("rejects header and movetext result conflicts", conflicting_results)

        def atomic_files_complete() -> None:
            require({"manifest.json", *dataset.CANONICAL_ARTIFACTS} <= {p.name for p in legal_output.iterdir()}, "artifact missing")
            require(not list(legal_output.parent.glob(".dataset.tmp-*")), "temporary sibling remains")
        runner.check("publishes complete artifacts atomically", atomic_files_complete)

        runner.check("keeps canonical profile and generated header unchanged", lambda: require(
            PROFILE.read_bytes() == profile_before and HEADER.read_bytes() == header_before,
            "production tuning artifact changed"))

    real_pgn_dir = ROOT / "tuning/pgn"
    if real_pgn_dir.is_dir() and any(real_pgn_dir.glob("*.pgn")):
        def real_integration() -> None:
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "real-dataset"
                validation = dataset.build_dataset(dataset.BuildConfig(
                    pgn_dir=real_pgn_dir,
                    output_dir=output,
                ))
                require(validation["positions"] > 0, "real corpus produced no positions")
        runner.check("real local PGN corpus integration", real_integration)
    else:
        runner.skip("real local PGN corpus integration", "ignored tuning/pgn/*.pgn is absent")

    require(runner.count >= 56, f"expected at least 56 passing checks, got {runner.count}")
    print(f"\nAll PGN dataset tests passed ({runner.count} passed, {runner.skipped} skipped).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
