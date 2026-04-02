#!/usr/bin/env python3
"""Phase 7 self-play harness for baseline vs candidate engines.

Runs paired-color games from a fixed opening list, writes a PGN file,
and produces a summary report with neutral/positive gate status.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import hashlib
import math
import pathlib
import shlex
import sys
from typing import List

import chess
import chess.engine
import chess.pgn


@dataclasses.dataclass
class OpeningSpec:
    name: str
    position_cmd: str


@dataclasses.dataclass
class GameStats:
    game_index: int
    opening_name: str
    candidate_white: bool
    result: str
    termination: str
    plies: int
    candidate_points: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Phase 7 paired self-play harness")
    parser.add_argument("--baseline-bin", required=True, help="Path to baseline engine binary")
    parser.add_argument("--candidate-bin", required=True, help="Path to candidate engine binary")
    parser.add_argument(
        "--openings",
        default="bench/phase7_openings.txt",
        help="Opening list file: name|position ...",
    )
    parser.add_argument("--games", type=int, default=100, help="Total games to play (recommended even)")
    parser.add_argument("--movetime-ms", type=int, default=200, help="Fixed movetime in milliseconds")
    parser.add_argument("--max-plies", type=int, default=240, help="Maximum plies before draw adjudication")
    parser.add_argument(
        "--engine-args",
        default="--mode=gui --book=__NO_BOOK__",
        help="Args passed to both engines",
    )
    parser.add_argument(
        "--baseline-extra-args",
        default="",
        help="Extra args appended to baseline engine command",
    )
    parser.add_argument(
        "--candidate-extra-args",
        default="",
        help="Extra args appended to candidate engine command",
    )
    parser.add_argument("--pgn", default="", help="Output PGN path")
    parser.add_argument("--summary", default="", help="Output summary path")
    return parser.parse_args()


def canonical_path(path: str) -> str:
    return str(pathlib.Path(path).expanduser().resolve())


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_openings(path: str) -> List[OpeningSpec]:
    openings: List[OpeningSpec] = []
    with open(path, "r", encoding="utf-8") as f:
        for line_number, raw in enumerate(f, start=1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue

            if "|" in line:
                name, position = line.split("|", 1)
                name = name.strip()
                position = position.strip()
            else:
                name = f"opening_{line_number}"
                position = line

            if not position.startswith("position "):
                raise ValueError(f"{path}:{line_number}: opening line must start with 'position '")

            openings.append(OpeningSpec(name=name, position_cmd=position))

    if not openings:
        raise ValueError(f"no openings found in {path}")

    return openings


def board_from_position_command(position_cmd: str) -> chess.Board:
    tokens = position_cmd.split()
    if len(tokens) < 2 or tokens[0] != "position":
        raise ValueError(f"invalid position command: {position_cmd}")

    idx = 1
    if tokens[idx] == "startpos":
        board = chess.Board()
        idx += 1
    elif tokens[idx] == "fen":
        idx += 1
        fen_tokens = []
        while idx < len(tokens) and tokens[idx] != "moves":
            fen_tokens.append(tokens[idx])
            idx += 1
        fen = " ".join(fen_tokens)
        board = chess.Board(fen)
    else:
        raise ValueError(f"unsupported position command root: {tokens[idx]}")

    if idx < len(tokens) and tokens[idx] == "moves":
        idx += 1
        while idx < len(tokens):
            board.push_uci(tokens[idx])
            idx += 1

    return board


def result_to_candidate_points(result: str, candidate_white: bool) -> float:
    if result == "1/2-1/2":
        return 0.5
    if result == "1-0":
        return 1.0 if candidate_white else 0.0
    if result == "0-1":
        return 0.0 if candidate_white else 1.0
    return 0.5


def run_single_game(
    game_index: int,
    opening: OpeningSpec,
    candidate_white: bool,
    candidate_engine: chess.engine.SimpleEngine,
    baseline_engine: chess.engine.SimpleEngine,
    movetime_ms: int,
    max_plies: int,
) -> tuple[chess.pgn.Game, GameStats]:
    # Older python-chess versions do not expose ucinewgame on SimpleEngine.
    # A fresh board is passed on every move, so omitting explicit ucinewgame
    # remains deterministic for this harness.

    board = board_from_position_command(opening.position_cmd)
    game = chess.pgn.Game()
    node = game

    game.headers["Event"] = "Phase7 Self-Play"
    game.headers["Site"] = "Local"
    game.headers["Date"] = dt.datetime.now(dt.timezone.utc).strftime("%Y.%m.%d")
    game.headers["Round"] = str(game_index)
    game.headers["White"] = "candidate" if candidate_white else "baseline"
    game.headers["Black"] = "baseline" if candidate_white else "candidate"
    game.headers["Opening"] = opening.name
    game.headers["TimeControl"] = f"{movetime_ms}ms/move"
    game.headers["SetUp"] = "1"
    game.headers["FEN"] = board.fen()

    plies = 0
    result = "1/2-1/2"
    termination = "draw"

    while True:
        if board.is_game_over(claim_draw=True):
            outcome = board.outcome(claim_draw=True)
            if outcome is not None:
                result = outcome.result()
                termination = outcome.termination.name.lower()
            break

        if plies >= max_plies:
            result = "1/2-1/2"
            termination = "max_plies"
            break

        candidate_to_move = board.turn == chess.WHITE if candidate_white else board.turn == chess.BLACK
        engine = candidate_engine if candidate_to_move else baseline_engine

        try:
            play_result = engine.play(board, chess.engine.Limit(time=max(movetime_ms, 1) / 1000.0))
        except chess.engine.EngineError:
            # Conservative adjudication: engine crash/invalid output loses.
            result = "0-1" if candidate_to_move else "1-0"
            termination = "engine_error"
            break

        move = play_result.move
        if move is None or move not in board.legal_moves:
            result = "0-1" if candidate_to_move else "1-0"
            termination = "illegal_move"
            break

        board.push(move)
        node = node.add_variation(move)
        plies += 1

    game.headers["Result"] = result
    game.headers["Termination"] = termination

    stats = GameStats(
        game_index=game_index,
        opening_name=opening.name,
        candidate_white=candidate_white,
        result=result,
        termination=termination,
        plies=plies,
        candidate_points=result_to_candidate_points(result, candidate_white),
    )
    return game, stats


def main() -> int:
    args = parse_args()

    if args.games <= 0:
        raise SystemExit("--games must be > 0")
    if args.movetime_ms <= 0:
        raise SystemExit("--movetime-ms must be > 0")
    if args.max_plies <= 0:
        raise SystemExit("--max-plies must be > 0")

    baseline_bin = canonical_path(args.baseline_bin)
    candidate_bin = canonical_path(args.candidate_bin)
    openings_file = canonical_path(args.openings)

    for binary_path, label in ((baseline_bin, "baseline"), (candidate_bin, "candidate")):
        p = pathlib.Path(binary_path)
        if not p.exists() or not p.is_file() or not p.stat().st_mode & 0o111:
            raise SystemExit(f"{label} binary is missing or not executable: {binary_path}")

    run_timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d_%H%M%S")
    pgn_path = canonical_path(args.pgn) if args.pgn else canonical_path(f"bench/phase7_selfplay_{run_timestamp}.pgn")
    summary_path = (
        canonical_path(args.summary)
        if args.summary
        else canonical_path(f"bench/phase7_selfplay_summary_{run_timestamp}.txt")
    )

    pathlib.Path(pgn_path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(summary_path).parent.mkdir(parents=True, exist_ok=True)

    openings = parse_openings(openings_file)

    shared_args = shlex.split(args.engine_args)
    baseline_cmd = [baseline_bin, *shared_args, *shlex.split(args.baseline_extra_args)]
    candidate_cmd = [candidate_bin, *shared_args, *shlex.split(args.candidate_extra_args)]

    baseline_sha = sha256_file(baseline_bin)
    candidate_sha = sha256_file(candidate_bin)

    if baseline_sha == candidate_sha:
        raise SystemExit("baseline and candidate binaries have identical SHA256; provide distinct binaries")

    candidate_engine = chess.engine.SimpleEngine.popen_uci(candidate_cmd)
    baseline_engine = chess.engine.SimpleEngine.popen_uci(baseline_cmd)

    all_stats: List[GameStats] = []
    try:
        rounds = math.ceil(args.games / 2)
        with open(pgn_path, "w", encoding="utf-8") as pgn_file:
            game_counter = 0
            for round_idx in range(rounds):
                opening = openings[round_idx % len(openings)]

                schedule = [True, False]
                for candidate_white in schedule:
                    if game_counter >= args.games:
                        break

                    game_counter += 1
                    game, stats = run_single_game(
                        game_index=game_counter,
                        opening=opening,
                        candidate_white=candidate_white,
                        candidate_engine=candidate_engine,
                        baseline_engine=baseline_engine,
                        movetime_ms=args.movetime_ms,
                        max_plies=args.max_plies,
                    )
                    print(game, file=pgn_file, end="\n\n")
                    all_stats.append(stats)

                    if game_counter % 10 == 0 or game_counter == args.games:
                        print(f"progress: {game_counter}/{args.games} games complete", flush=True)
    finally:
        candidate_engine.quit()
        baseline_engine.quit()

    total_games = len(all_stats)
    if total_games == 0:
        raise SystemExit("no games were completed")

    candidate_score = sum(g.candidate_points for g in all_stats)
    candidate_wins = sum(1 for g in all_stats if g.candidate_points == 1.0)
    draws = sum(1 for g in all_stats if g.candidate_points == 0.5)
    candidate_losses = sum(1 for g in all_stats if g.candidate_points == 0.0)

    candidate_white_games = [g for g in all_stats if g.candidate_white]
    candidate_black_games = [g for g in all_stats if not g.candidate_white]

    candidate_white_score = sum(g.candidate_points for g in candidate_white_games)
    candidate_black_score = sum(g.candidate_points for g in candidate_black_games)

    score_rate = candidate_score / total_games
    selfplay_gate = "PASS" if score_rate >= 0.50 else "FAIL"

    with open(summary_path, "w", encoding="utf-8") as summary_file:
        summary_file.write(f"run_timestamp={run_timestamp}\n")
        summary_file.write(f"openings_file={openings_file}\n")
        summary_file.write(f"baseline_path={baseline_bin}\n")
        summary_file.write(f"baseline_sha256={baseline_sha}\n")
        summary_file.write(f"candidate_path={candidate_bin}\n")
        summary_file.write(f"candidate_sha256={candidate_sha}\n")
        summary_file.write(f"total_games={total_games}\n")
        summary_file.write(f"movetime_ms={args.movetime_ms}\n")
        summary_file.write(f"max_plies={args.max_plies}\n")
        summary_file.write(f"candidate_points={candidate_score:.1f}\n")
        summary_file.write(f"candidate_score_rate={score_rate:.4f}\n")
        summary_file.write(f"candidate_wins={candidate_wins}\n")
        summary_file.write(f"draws={draws}\n")
        summary_file.write(f"candidate_losses={candidate_losses}\n")
        summary_file.write(f"candidate_white_games={len(candidate_white_games)}\n")
        summary_file.write(f"candidate_white_points={candidate_white_score:.1f}\n")
        summary_file.write(f"candidate_black_games={len(candidate_black_games)}\n")
        summary_file.write(f"candidate_black_points={candidate_black_score:.1f}\n")
        summary_file.write(f"neutral_to_positive_gate={selfplay_gate}\n")
        summary_file.write(f"pgn_output={pgn_path}\n")

    print("Self-play summary")
    print(f"- games: {total_games}")
    print(f"- candidate score: {candidate_score:.1f}/{total_games} ({score_rate * 100.0:.2f}%)")
    print(f"- W/D/L: {candidate_wins}/{draws}/{candidate_losses}")
    print(f"- neutral_to_positive_gate: {selfplay_gate}")
    print(f"- pgn: {pgn_path}")
    print(f"- summary: {summary_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
