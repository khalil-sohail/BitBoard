#!/usr/bin/env python3
"""Phase 21.1 bounded corpus storage, sampling, and resume tests."""

from __future__ import annotations

import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import chess
import chess.pgn

from tools.tuning import pgn_dataset as dataset


def generated_pgn(count: int) -> str:
    documents = []
    for index in range(count):
        board = chess.Board(); game = chess.pgn.Game(); node = game
        for ply in range(28):
            moves = sorted(board.legal_moves, key=lambda move: move.uci())
            move = moves[(index * 11 + ply * 7) % len(moves)]
            board.push(move); node = node.add_variation(move)
            if board.is_game_over(): break
        game.headers.update({"Event": f"Synthetic {index}", "Result": ("1-0", "0-1", "1/2-1/2")[index % 3]})
        documents.append(str(game.accept(chess.pgn.StringExporter(headers=True, variations=False, comments=False))))
    return "\n\n".join(documents) + "\n"


class StreamingDatasetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name); self.pgn = self.root / "pgn"; self.pgn.mkdir()
        (self.pgn / "games.pgn").write_text(generated_pgn(80), encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def config(self, name: str = "dataset", **values):
        defaults = dict(pgn_dir=self.pgn, output_dir=self.root / name, min_ply=4,
            maximum_retained_positions=180, maximum_positions_per_game=6,
            phase_quotas={"opening": 2, "middlegame": 3, "endgame": 1},
            transaction_games=10, progress_games=1000, maximum_rss_mb=512, warning_rss_mb=400)
        defaults.update(values); return dataset.BuildConfig(**defaults)

    def test_source_has_no_whole_corpus_materialization(self) -> None:
        source = Path(dataset.__file__).read_text(encoding="utf-8")
        self.assertNotIn("all_positions", source)
        self.assertNotIn('positions.jsonl").read_text', source)
        self.assertNotIn('"\\n".join', source)

    def test_bounded_sqlite_build_and_single_export(self) -> None:
        output = self.root / "dataset"; validation = dataset.build_dataset(self.config())
        self.assertLessEqual(validation["positions"], 180)
        self.assertTrue((output / "work/dataset.sqlite").is_file())
        self.assertTrue((output / "positions.jsonl").is_file())
        self.assertFalse(any((output / f"{split}.jsonl").exists() for split in dataset.SPLITS))
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertFalse(manifest["storage"]["duplicateSplitArtifacts"])
        connection = sqlite3.connect(output / "work/dataset.sqlite")
        self.assertEqual(connection.execute("PRAGMA integrity_check").fetchone()[0], "ok"); connection.close()

    def test_per_game_phase_caps_and_stable_rank(self) -> None:
        records = []
        for phase, count in (("opening", 8), ("middlegame", 8), ("endgame", 8)):
            for index in range(count):
                records.append({"gamePhase": phase, "positionKey": f"{phase}-{index}", "positionId": f"id-{phase}-{index}", "ply": index})
        config = self.config()
        first = dataset.per_game_sample(records, config, "game")
        second = dataset.per_game_sample(list(reversed(records)), config, "game")
        self.assertEqual([row["positionId"] for row in first], [row["positionId"] for row in second])
        self.assertLessEqual(len(first), 6)
        counts = {phase: sum(row["gamePhase"] == phase for row in first) for phase in dataset.PHASES}
        self.assertEqual(counts, {"opening": 2, "middlegame": 3, "endgame": 1})

    def test_game_identity_ignores_source_headers(self) -> None:
        import io
        one = chess.pgn.read_game(io.StringIO(generated_pgn(1)), Visitor=dataset.ResultTrackingBuilder)
        text = generated_pgn(1).replace('[Event "Synthetic 0"]', '[Event "Renamed"]')
        two = chess.pgn.read_game(io.StringIO(text), Visitor=dataset.ResultTrackingBuilder)
        moves = lambda game: [move.uci() for move in game.mainline_moves()]
        self.assertEqual(dataset.stable_game_id(one, moves(one), "1-0"), dataset.stable_game_id(two, moves(two), "1-0"))

    def test_position_identity_preserves_chess_state(self) -> None:
        base = chess.Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        no_castle = chess.Board("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1")
        black = chess.Board("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1")
        self.assertNotEqual(dataset.position_key(base), dataset.position_key(no_castle))
        self.assertNotEqual(dataset.position_key(base), dataset.position_key(black))
        ep = chess.Board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2")
        self.assertTrue(dataset.position_key(ep).endswith(" d6"))

    def test_duplicate_games_and_positions_use_constraints(self) -> None:
        (self.pgn / "duplicate.pgn").write_text((self.pgn / "games.pgn").read_text(), encoding="utf-8")
        dataset.build_dataset(self.config())
        summary = json.loads((self.root / "dataset/summary.json").read_text())
        self.assertGreater(summary["games"]["duplicate"], 0)
        connection = sqlite3.connect(self.root / "dataset/work/dataset.sqlite")
        with self.assertRaises(sqlite3.IntegrityError):
            connection.execute("INSERT INTO games SELECT * FROM games LIMIT 1")
        connection.close()

    def test_capped_prototype_stops_early(self) -> None:
        dataset.build_dataset(self.config(maximum_accepted_games=25, scan_all_input_games=False))
        summary = json.loads((self.root / "dataset/summary.json").read_text())
        self.assertEqual(summary["games"]["accepted"], 25)
        self.assertLess(summary["games"]["attempted"], 80)

    def test_interrupt_then_resume_uses_checkpoint(self) -> None:
        original = dataset.process_game; calls = 0
        def interrupted(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 26: raise KeyboardInterrupt()
            return original(*args, **kwargs)
        with mock.patch.object(dataset, "process_game", side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt): dataset.build_dataset(self.config(transaction_games=5))
        database = self.root / "dataset/work/dataset.sqlite"
        connection = sqlite3.connect(database); before = connection.execute("SELECT COUNT(*) FROM games").fetchone()[0]; connection.close()
        self.assertGreaterEqual(before, 20)
        validation = dataset.build_dataset(self.config(resume=True, transaction_games=5))
        self.assertGreater(validation["positions"], 0)
        attempted = json.loads((self.root / "dataset/summary.json").read_text())["games"]["attempted"]
        self.assertEqual(attempted, 80)

    def test_changed_resume_configuration_is_rejected(self) -> None:
        with mock.patch.object(dataset, "process_game", side_effect=KeyboardInterrupt()):
            with self.assertRaises(KeyboardInterrupt): dataset.build_dataset(self.config())
        with self.assertRaisesRegex(dataset.DatasetError, "configuration or input checksum changed"):
            dataset.build_dataset(self.config(resume=True, sampling_seed="changed"))

    def test_memory_guard_aborts_without_manifest(self) -> None:
        with mock.patch.object(dataset, "current_rss_mb", return_value=999):
            with self.assertRaisesRegex(dataset.DatasetError, "RSS guardrail"):
                dataset.build_dataset(self.config(maximum_rss_mb=128, warning_rss_mb=100))
        self.assertFalse((self.root / "dataset/manifest.json").exists())
        self.assertTrue((self.root / "dataset/work/dataset.sqlite").exists())

    def test_balanced_selection_materializes_only_request(self) -> None:
        dataset.build_dataset(self.config(maximum_retained_positions=900))
        # Synthetic games may not fill every fine-grained stratum; fallback must still be exact.
        selected = dataset.select_balanced_positions(self.root / "dataset", "train", 20, 2, "selection")
        self.assertEqual(len(selected), 20)
        self.assertLessEqual(max(__import__("collections").Counter(row["gameId"] for row in selected).values()), 2)

    def test_estimator_creates_no_dataset(self) -> None:
        report = dataset.estimate_corpus(self.config(), samples_per_file=5)
        self.assertIn("measured", report); self.assertIn("estimated", report)
        self.assertFalse((self.root / "dataset").exists())


if __name__ == "__main__":
    unittest.main()
