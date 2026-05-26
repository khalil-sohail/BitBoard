#!/usr/bin/env python3
"""Generate labeled EPD datasets for Texel evaluation tuning.

Reads one or more PGN files, extracts mid-game and endgame positions with
known game outcomes, applies filtering heuristics, and writes a deduplicated
EPD file suitable for Texel-method weight optimization.

Output format (one position per line):
    <FEN> c9 "<result>";

Where <result> is one of: "1-0", "0-1", "1/2-1/2".

Usage:
    python3 scripts/texel_dataset_gen.py \\
        --pgn bench/phase7_selfplay_20260402_100g.pgn \\
        --output data/texel_dataset.epd \\
        --skip-plies 16 \\
        --min-pieces 6 \\
        --max-positions 0
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys
import time
from pathlib import Path
from typing import List, Optional, Set

import chess
import chess.pgn


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate labeled EPD dataset for Texel tuning",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  # From self-play PGN:\n"
            "  python3 scripts/texel_dataset_gen.py \\\n"
            "      --pgn bench/phase7_selfplay_20260402_100g.pgn \\\n"
            "      --output data/texel_dataset.epd\n"
            "\n"
            "  # From multiple PGN files with aggressive filtering:\n"
            "  python3 scripts/texel_dataset_gen.py \\\n"
            "      --pgn games1.pgn --pgn games2.pgn \\\n"
            "      --output data/texel_large.epd \\\n"
            "      --skip-plies 20 --min-pieces 8 --sample-rate 3\n"
        ),
    )
    parser.add_argument(
        "--pgn",
        action="append",
        required=True,
        help="Input PGN file path (can be specified multiple times)",
    )
    parser.add_argument(
        "--output",
        default="data/texel_dataset.epd",
        help="Output EPD file path (default: data/texel_dataset.epd)",
    )
    parser.add_argument(
        "--skip-plies",
        type=int,
        default=16,
        help=(
            "Skip the first N plies (half-moves) of each game to avoid "
            "opening book noise (default: 16, i.e., first 8 full moves)"
        ),
    )
    parser.add_argument(
        "--min-pieces",
        type=int,
        default=6,
        help=(
            "Minimum total pieces (non-king, non-pawn) on the board. "
            "Positions below this threshold are skipped to avoid trivial "
            "endgame positions. Kings are always present. (default: 6)"
        ),
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=1,
        help=(
            "Sample every Nth eligible position from each game. Set to 1 "
            "for maximum density, 3-5 for reduced correlation between "
            "adjacent positions. (default: 1)"
        ),
    )
    parser.add_argument(
        "--max-positions",
        type=int,
        default=0,
        help=(
            "Maximum number of positions to extract total (0 = unlimited). "
            "Useful for creating smaller test datasets. (default: 0)"
        ),
    )
    parser.add_argument(
        "--max-games",
        type=int,
        default=0,
        help="Maximum number of games to process (0 = unlimited). (default: 0)",
    )
    parser.add_argument(
        "--skip-draws",
        action="store_true",
        default=False,
        help="Skip drawn games entirely (reduces dataset but improves signal).",
    )
    parser.add_argument(
        "--skip-short-games",
        type=int,
        default=20,
        help=(
            "Skip games shorter than this many plies. Very short games are "
            "often adjudicated or engine crashes. (default: 20)"
        ),
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        default=False,
        help="Suppress progress output.",
    )
    return parser.parse_args()


def is_valid_result(result: str) -> bool:
    """Check if the game result is a valid, decisive or drawn outcome."""
    return result in ("1-0", "0-1", "1/2-1/2")


def count_non_pawn_non_king(board: chess.Board) -> int:
    """Count total non-pawn, non-king pieces on the board."""
    count = 0
    for piece_type in (chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN):
        count += len(board.pieces(piece_type, chess.WHITE))
        count += len(board.pieces(piece_type, chess.BLACK))
    return count


def position_hash(fen_position: str) -> int:
    """Hash the position part of a FEN for deduplication.

    Uses the first four FEN fields (piece placement, side to move,
    castling rights, en passant) to identify unique positions.
    """
    # Take only the first 4 fields of the FEN (skip halfmove and fullmove)
    fields = fen_position.split()
    key = " ".join(fields[:4])
    return hash(key)


def should_skip_position(board: chess.Board, min_pieces: int) -> bool:
    """Apply position-level filtering heuristics.

    Returns True if the position should be excluded from the dataset.
    """
    # Skip positions where the side to move is in check.
    # These have tactical noise that makes static eval unreliable.
    if board.is_check():
        return True

    # Skip positions with very few pieces (trivially drawn or won).
    if count_non_pawn_non_king(board) < min_pieces:
        return True

    # Skip positions where the game is already over.
    if board.is_game_over(claim_draw=True):
        return True

    return False


def extract_positions_from_game(
    game: chess.pgn.Game,
    result: str,
    skip_plies: int,
    min_pieces: int,
    sample_rate: int,
    seen_positions: Set[int],
) -> List[str]:
    """Extract labeled EPD lines from a single game.

    Returns a list of EPD strings, one per extracted position.
    """
    positions: List[str] = []
    board = game.board()
    ply = 0
    sample_counter = 0

    for node in game.mainline():
        board.push(node.move)
        ply += 1

        # Skip opening phase.
        if ply <= skip_plies:
            continue

        # Apply sampling rate.
        sample_counter += 1
        if sample_counter % sample_rate != 0:
            continue

        # Apply position-level filters.
        if should_skip_position(board, min_pieces):
            continue

        # Deduplicate by position hash.
        fen = board.fen()
        pos_hash = position_hash(fen)
        if pos_hash in seen_positions:
            continue
        seen_positions.add(pos_hash)

        # Emit EPD line.
        # Format: <FEN> c9 "<result>";
        epd_line = f'{fen} c9 "{result}";'
        positions.append(epd_line)

    return positions


def process_pgn_file(
    pgn_path: str,
    args: argparse.Namespace,
    seen_positions: Set[int],
    total_positions: List[str],
    stats: dict,
) -> bool:
    """Process a single PGN file and append positions to total_positions.

    Returns False if max_positions limit was reached.
    """
    if not os.path.exists(pgn_path):
        print(f"warning: PGN file not found: {pgn_path}", file=sys.stderr)
        return True

    file_size_mb = os.path.getsize(pgn_path) / (1024 * 1024)
    if not args.quiet:
        print(f"Processing: {pgn_path} ({file_size_mb:.1f} MB)")

    with open(pgn_path, "r", encoding="utf-8", errors="replace") as handle:
        while True:
            # Check game limit.
            if args.max_games > 0 and stats["games_total"] >= args.max_games:
                return False

            # Check position limit.
            if args.max_positions > 0 and len(total_positions) >= args.max_positions:
                return False

            game = chess.pgn.read_game(handle)
            if game is None:
                break

            stats["games_total"] += 1

            # Get and validate result.
            result = game.headers.get("Result", "*")
            if not is_valid_result(result):
                stats["games_invalid_result"] += 1
                continue

            # Optional: skip draws.
            if args.skip_draws and result == "1/2-1/2":
                stats["games_draws_skipped"] += 1
                continue

            # Count game length.
            game_plies = sum(1 for _ in game.mainline())
            if game_plies < args.skip_short_games:
                stats["games_too_short"] += 1
                continue

            stats["games_processed"] += 1

            # Extract positions from this game.
            positions = extract_positions_from_game(
                game=game,
                result=result,
                skip_plies=args.skip_plies,
                min_pieces=args.min_pieces,
                sample_rate=args.sample_rate,
                seen_positions=seen_positions,
            )

            total_positions.extend(positions)
            stats["positions_extracted"] += len(positions)

            # Progress reporting.
            if not args.quiet and stats["games_total"] % 100 == 0:
                print(
                    f"  games: {stats['games_total']}, "
                    f"processed: {stats['games_processed']}, "
                    f"positions: {len(total_positions)}",
                    flush=True,
                )

    return True


def print_summary(stats: dict, output_path: str, elapsed: float) -> None:
    """Print dataset generation summary."""
    print()
    print("=== Texel Dataset Generation Summary ===")
    print(f"Output: {output_path}")
    print(f"Elapsed: {elapsed:.1f}s")
    print(f"Games parsed: {stats['games_total']}")
    print(f"Games processed: {stats['games_processed']}")
    print(f"Games skipped (invalid result): {stats['games_invalid_result']}")
    print(f"Games skipped (draws): {stats['games_draws_skipped']}")
    print(f"Games skipped (too short): {stats['games_too_short']}")
    print(f"Positions extracted: {stats['positions_extracted']}")
    print(f"Positions after dedup: {stats['positions_final']}")

    if stats["positions_final"] > 0:
        print()
        print("Result distribution:")
        for result_key in ("1-0", "0-1", "1/2-1/2"):
            count = stats.get(f"result_{result_key}", 0)
            pct = (count / stats["positions_final"]) * 100.0
            print(f"  {result_key}: {count} ({pct:.1f}%)")

    print()
    if stats["positions_final"] >= 10000:
        print(f"Dataset ready for tuning ({stats['positions_final']} positions).")
    elif stats["positions_final"] >= 1000:
        print(
            f"Small dataset ({stats['positions_final']} positions). "
            "Consider adding more PGN sources for better tuning quality."
        )
    else:
        print(
            f"WARNING: Very small dataset ({stats['positions_final']} positions). "
            "Tuning results will be unreliable. Add more PGN data."
        )


def main() -> int:
    args = parse_args()

    if args.skip_plies < 0:
        raise SystemExit("--skip-plies must be >= 0")
    if args.min_pieces < 0:
        raise SystemExit("--min-pieces must be >= 0")
    if args.sample_rate < 1:
        raise SystemExit("--sample-rate must be >= 1")

    # Validate PGN inputs exist.
    for pgn_path in args.pgn:
        if not os.path.exists(pgn_path):
            print(f"error: PGN file not found: {pgn_path}", file=sys.stderr)
            return 1

    # Ensure output directory exists.
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    stats = {
        "games_total": 0,
        "games_processed": 0,
        "games_invalid_result": 0,
        "games_draws_skipped": 0,
        "games_too_short": 0,
        "positions_extracted": 0,
        "positions_final": 0,
    }

    seen_positions: Set[int] = set()
    all_positions: List[str] = []

    start_time = time.monotonic()

    if not args.quiet:
        print("Texel Dataset Generator")
        print(f"  Skip plies: {args.skip_plies}")
        print(f"  Min pieces: {args.min_pieces}")
        print(f"  Sample rate: every {args.sample_rate}")
        print(f"  Max positions: {'unlimited' if args.max_positions == 0 else args.max_positions}")
        print(f"  Max games: {'unlimited' if args.max_games == 0 else args.max_games}")
        print(f"  Skip draws: {args.skip_draws}")
        print(f"  Skip short games: <{args.skip_short_games} plies")
        print(f"  Input PGN(s): {len(args.pgn)}")
        print()

    # Process each PGN file.
    for pgn_path in args.pgn:
        should_continue = process_pgn_file(
            pgn_path=pgn_path,
            args=args,
            seen_positions=seen_positions,
            total_positions=all_positions,
            stats=stats,
        )
        if not should_continue:
            break

    # Enforce max_positions limit on final list.
    if args.max_positions > 0 and len(all_positions) > args.max_positions:
        all_positions = all_positions[: args.max_positions]

    stats["positions_final"] = len(all_positions)

    # Count result distribution.
    for line in all_positions:
        if '"1-0"' in line:
            stats["result_1-0"] = stats.get("result_1-0", 0) + 1
        elif '"0-1"' in line:
            stats["result_0-1"] = stats.get("result_0-1", 0) + 1
        elif '"1/2-1/2"' in line:
            stats["result_1/2-1/2"] = stats.get("result_1/2-1/2", 0) + 1

    # Write output EPD file.
    with open(output_path, "w", encoding="utf-8") as f:
        for line in all_positions:
            f.write(line + "\n")

    elapsed = time.monotonic() - start_time

    if not args.quiet:
        print_summary(stats, str(output_path), elapsed)

    return 0


if __name__ == "__main__":
    sys.exit(main())
