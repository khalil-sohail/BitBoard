#!/usr/bin/env python3
"""Find likely evaluation hallucinations from self-play PGN losses.

This script scans games where the candidate lost and searches candidate moves for:
1) Overconfidence delusion: eval after candidate move >= positive threshold.
2) Refutation delusion: eval drop after opponent reply >= drop threshold.

Scores are sourced from PGN eval comments when available, with optional UCI probe fallback.
For the top offending positions, it runs the engine's custom `eval` command and prints
its feature breakdown output for diagnosis.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import chess
import chess.engine
import chess.pgn


MATE_SCORE_CP = 100_000
PGN_EVAL_TAG_RE = re.compile(r"\[%eval\s+([^\]]+)\]", re.IGNORECASE)
PLAIN_CP_RE = re.compile(r"\bcp\s*[:=]\s*(-?\d+)\b", re.IGNORECASE)
PLAIN_EVAL_RE = re.compile(r"\beval\s*[:=]\s*([+-]?\d+(?:\.\d+)?)\b", re.IGNORECASE)


@dataclasses.dataclass
class Incident:
	severity_cp: int
	reason: str
	game_index: int
	round_id: str
	opening: str
	result: str
	candidate_color: chess.Color
	ply: int
	candidate_move_san: str
	candidate_move_uci: str
	opponent_reply_san: Optional[str]
	opponent_reply_uci: Optional[str]
	candidate_eval_cp: int
	opponent_eval_cp: Optional[int]
	drop_cp: Optional[int]
	fen: str
	candidate_eval_source: str
	opponent_eval_source: Optional[str]


class UciScoreSession:
	"""Quick score probes via python-chess UCI wrapper."""

	def __init__(
		self,
		engine_path: str,
		engine_args: str,
		probe_depth: int,
		probe_movetime_ms: int,
	) -> None:
		self.engine_path = engine_path
		self.engine_args = engine_args
		self.probe_depth = probe_depth
		self.probe_movetime_ms = probe_movetime_ms
		self.engine: Optional[chess.engine.SimpleEngine] = None
		self.cache: Dict[Tuple[str, chess.Color], Optional[int]] = {}

	def start(self) -> None:
		cmd = [self.engine_path, *shlex.split(self.engine_args)]
		self.engine = chess.engine.SimpleEngine.popen_uci(cmd)

	def close(self) -> None:
		if self.engine is None:
			return
		try:
			self.engine.quit()
		except Exception:
			pass
		finally:
			self.engine = None

	def score_position_cp(self, board: chess.Board, candidate_color: chess.Color) -> Optional[int]:
		key = (board.fen(), candidate_color)
		if key in self.cache:
			return self.cache[key]

		if self.engine is None:
			raise RuntimeError("UCI session not started")

		info = self.engine.analyse(
			board,
			chess.engine.Limit(
				depth=self.probe_depth,
				time=max(self.probe_movetime_ms, 1) / 1000.0,
			),
		)
		score_obj = info.get("score")
		if score_obj is None:
			self.cache[key] = None
			return None

		candidate_cp = score_obj.pov(candidate_color).score(mate_score=MATE_SCORE_CP)
		if candidate_cp is None:
			self.cache[key] = None
			return None

		self.cache[key] = candidate_cp
		return candidate_cp


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Find candidate-eval hallucinations in lost self-play games")
	parser.add_argument(
		"--pgn",
		default="bench/phase7_selfplay_20260402_100g.pgn",
		help="Input PGN path",
	)
	parser.add_argument(
		"--engine",
		default="./bench/bin/phase7_candidate",
		help="Candidate engine binary path",
	)
	parser.add_argument(
		"--engine-args",
		default="--mode=gui --book=__NO_BOOK__",
		help="Args passed to engine process",
	)
	parser.add_argument(
		"--top-n",
		type=int,
		default=5,
		help="Number of worst hallucinations to print",
	)
	parser.add_argument(
		"--positive-threshold-cp",
		type=int,
		default=150,
		help="Overconfidence threshold in centipawns (e.g., 150 == +1.50)",
	)
	parser.add_argument(
		"--drop-threshold-cp",
		type=int,
		default=250,
		help="Reply crash threshold in centipawns",
	)
	parser.add_argument(
		"--score-source",
		choices=["pgn", "uci", "auto"],
		default="auto",
		help=(
			"How to obtain per-move eval scores: pgn comments only, "
			"uci probe only, or auto (pgn then uci fallback)"
		),
	)
	parser.add_argument(
		"--probe-depth",
		type=int,
		default=1,
		help="UCI probe depth for score-source=uci/auto",
	)
	parser.add_argument(
		"--probe-movetime-ms",
		type=int,
		default=15,
		help="UCI probe movetime for score-source=uci/auto",
	)
	parser.add_argument(
		"--eval-timeout-sec",
		type=float,
		default=10.0,
		help="Timeout for each eval breakdown subprocess",
	)
	parser.add_argument(
		"--max-games",
		type=int,
		default=0,
		help="Optional limit for number of games to scan (0 = all)",
	)
	return parser.parse_args()


def parse_pgn_eval_token_to_white_cp(token: str) -> Optional[int]:
	token = token.strip()
	if not token:
		return None

	# PGN eval standard: decimal pawn score or mate notation.
	# Examples: 0.34, -1.25, #3, #-2, +#5.
	if "#" in token:
		sign = -1 if token.startswith("-") or "#-" in token else 1
		digits = "".join(ch for ch in token if ch.isdigit())
		if not digits:
			return sign * (MATE_SCORE_CP - 100)
		distance = int(digits)
		return sign * (MATE_SCORE_CP - min(distance, 1000) * 100)

	try:
		return int(round(float(token) * 100.0))
	except ValueError:
		return None


def detect_candidate_color(game: chess.pgn.Game) -> Optional[chess.Color]:
	white_name = game.headers.get("White", "").strip().lower()
	black_name = game.headers.get("Black", "").strip().lower()

	white_is_candidate = "candidate" in white_name
	black_is_candidate = "candidate" in black_name

	if white_is_candidate and not black_is_candidate:
		return chess.WHITE
	if black_is_candidate and not white_is_candidate:
		return chess.BLACK
	return None


def candidate_lost(result: str, candidate_color: chess.Color) -> bool:
	if result == "1-0":
		return candidate_color == chess.BLACK
	if result == "0-1":
		return candidate_color == chess.WHITE
	return False


def eval_from_pgn_comment_cp(node: chess.pgn.ChildNode, candidate_color: chess.Color) -> Optional[int]:
	# Preferred path: python-chess built-in eval parser (%eval tags).
	try:
		eval_obj = node.eval()
	except Exception:
		eval_obj = None

	if eval_obj is not None:
		score = eval_obj.pov(candidate_color).score(mate_score=MATE_SCORE_CP)
		if score is not None:
			return score

	comment = node.comment or ""
	if not comment:
		return None

	tag_match = PGN_EVAL_TAG_RE.search(comment)
	if tag_match:
		white_cp = parse_pgn_eval_token_to_white_cp(tag_match.group(1))
		if white_cp is None:
			return None
		return white_cp if candidate_color == chess.WHITE else -white_cp

	cp_match = PLAIN_CP_RE.search(comment)
	if cp_match:
		return int(cp_match.group(1))

	eval_match = PLAIN_EVAL_RE.search(comment)
	if eval_match:
		value = float(eval_match.group(1))
		return int(round(value * 100.0))

	return None


def get_eval_cp(
	node: chess.pgn.ChildNode,
	candidate_color: chess.Color,
	score_source: str,
	score_session: Optional[UciScoreSession],
) -> Tuple[Optional[int], Optional[str]]:
	if score_source in ("pgn", "auto"):
		pgn_cp = eval_from_pgn_comment_cp(node, candidate_color)
		if pgn_cp is not None:
			return pgn_cp, "pgn"

	if score_source in ("uci", "auto") and score_session is not None:
		try:
			uci_cp = score_session.score_position_cp(node.board(), candidate_color)
		except Exception:
			uci_cp = None
		if uci_cp is not None:
			return uci_cp, "uci"

	return None, None


def run_eval_breakdown(engine_path: str, engine_args: str, fen: str, timeout_sec: float) -> str:
	cmd = [engine_path, *shlex.split(engine_args)]
	payload = f"uci\nisready\nposition fen {fen}\neval\nquit\n"

	try:
		completed = subprocess.run(
			cmd,
			input=payload,
			capture_output=True,
			text=True,
			timeout=timeout_sec,
			check=False,
		)
	except Exception as exc:
		return f"<eval subprocess failed: {exc}>"

	raw = completed.stdout
	if completed.stderr:
		raw += "\n" + completed.stderr

	filtered: List[str] = []
	for line in raw.splitlines():
		line = line.strip()
		if not line:
			continue
		if line.startswith(("id name", "id author", "uciok", "readyok", "bestmove")):
			continue
		if "Opening book loaded" in line or "Failed to load book" in line:
			continue
		filtered.append(line)

	if not filtered:
		return "<no eval breakdown captured; eval diagnostics may be disabled in this build>"

	return "\n".join(filtered)


def analyze_pgn(args: argparse.Namespace) -> Tuple[List[Incident], Dict[str, int]]:
	pgn_path = Path(args.pgn)
	if not pgn_path.exists():
		raise FileNotFoundError(f"PGN file not found: {pgn_path}")

	score_session: Optional[UciScoreSession] = None
	if args.score_source in ("uci", "auto"):
		score_session = UciScoreSession(
			engine_path=args.engine,
			engine_args=args.engine_args,
			probe_depth=args.probe_depth,
			probe_movetime_ms=args.probe_movetime_ms,
		)
		score_session.start()

	incidents: List[Incident] = []
	stats = {
		"games_total": 0,
		"games_with_candidate": 0,
		"games_candidate_lost": 0,
		"candidate_moves_scanned": 0,
		"moves_with_eval": 0,
		"eval_from_pgn": 0,
		"eval_from_uci": 0,
		"incidents": 0,
	}

	try:
		with pgn_path.open("r", encoding="utf-8", errors="replace") as handle:
			while True:
				if args.max_games > 0 and stats["games_total"] >= args.max_games:
					break

				game = chess.pgn.read_game(handle)
				if game is None:
					break

				stats["games_total"] += 1

				candidate_color = detect_candidate_color(game)
				if candidate_color is None:
					continue

				stats["games_with_candidate"] += 1

				result = game.headers.get("Result", "*")
				if not candidate_lost(result, candidate_color):
					continue

				stats["games_candidate_lost"] += 1

				board = game.board()
				for node in game.mainline():
					mover = board.turn
					board.push(node.move)

					if mover != candidate_color:
						continue

					stats["candidate_moves_scanned"] += 1

					cand_cp, cand_source = get_eval_cp(node, candidate_color, args.score_source, score_session)
					if cand_cp is None:
						continue

					stats["moves_with_eval"] += 1
					if cand_source == "pgn":
						stats["eval_from_pgn"] += 1
					elif cand_source == "uci":
						stats["eval_from_uci"] += 1

					reply_node = node.variations[0] if node.variations else None
					reply_cp: Optional[int] = None
					reply_source: Optional[str] = None
					if reply_node is not None:
						reply_cp, reply_source = get_eval_cp(reply_node, candidate_color, args.score_source, score_session)
						if reply_source == "pgn":
							stats["eval_from_pgn"] += 1
						elif reply_source == "uci":
							stats["eval_from_uci"] += 1

					drop_cp = None
					if reply_cp is not None:
						drop_cp = cand_cp - reply_cp

					overconfident = cand_cp >= args.positive_threshold_cp
					crash_refuted = drop_cp is not None and drop_cp >= args.drop_threshold_cp
					if not (overconfident or crash_refuted):
						continue

					reasons: List[str] = []
					severity_candidates: List[int] = []
					if overconfident:
						reasons.append("high_positive")
						severity_candidates.append(cand_cp)
					if crash_refuted and drop_cp is not None:
						reasons.append("reply_crash")
						severity_candidates.append(drop_cp)

					severity = max(severity_candidates)

					opponent_reply_san = reply_node.san() if reply_node is not None else None
					opponent_reply_uci = reply_node.move.uci() if reply_node is not None else None

					incident = Incident(
						severity_cp=severity,
						reason=",".join(reasons),
						game_index=stats["games_total"],
						round_id=game.headers.get("Round", "?"),
						opening=game.headers.get("Opening", "?"),
						result=result,
						candidate_color=candidate_color,
						ply=node.ply(),
						candidate_move_san=node.san(),
						candidate_move_uci=node.move.uci(),
						opponent_reply_san=opponent_reply_san,
						opponent_reply_uci=opponent_reply_uci,
						candidate_eval_cp=cand_cp,
						opponent_eval_cp=reply_cp,
						drop_cp=drop_cp,
						fen=node.board().fen(),
						candidate_eval_source=cand_source or "unknown",
						opponent_eval_source=reply_source,
					)
					incidents.append(incident)
					stats["incidents"] += 1
	finally:
		if score_session is not None:
			score_session.close()

	return incidents, stats


def top_unique_incidents(incidents: Sequence[Incident], top_n: int) -> List[Incident]:
	sorted_incidents = sorted(
		incidents,
		key=lambda inc: (
			inc.severity_cp,
			inc.drop_cp if inc.drop_cp is not None else -10**9,
			inc.candidate_eval_cp,
		),
		reverse=True,
	)

	best_per_fen: List[Incident] = []
	seen_fens = set()
	for inc in sorted_incidents:
		if inc.fen in seen_fens:
			continue
		seen_fens.add(inc.fen)
		best_per_fen.append(inc)
		if len(best_per_fen) >= top_n:
			break
	return best_per_fen


def format_cp(cp: Optional[int]) -> str:
	if cp is None:
		return "n/a"
	sign = "+" if cp >= 0 else ""
	return f"{sign}{cp}"


def color_name(color: chess.Color) -> str:
	return "White" if color == chess.WHITE else "Black"


def print_summary(stats: Dict[str, int], args: argparse.Namespace) -> None:
	print("=== Hallucination Scan Summary ===")
	print(f"PGN: {args.pgn}")
	print(f"Score source: {args.score_source}")
	if args.score_source in ("uci", "auto"):
		print(f"UCI probe: depth={args.probe_depth}, movetime={args.probe_movetime_ms}ms")
	print(
		"Thresholds: "
		f"positive>={args.positive_threshold_cp}cp, "
		f"drop>={args.drop_threshold_cp}cp"
	)
	print(f"Games parsed: {stats['games_total']}")
	print(f"Games with candidate identified: {stats['games_with_candidate']}")
	print(f"Candidate losses scanned: {stats['games_candidate_lost']}")
	print(f"Candidate moves scanned in losses: {stats['candidate_moves_scanned']}")
	print(f"Moves with eval available: {stats['moves_with_eval']}")
	print(f"Eval samples from PGN comments: {stats['eval_from_pgn']}")
	print(f"Eval samples from UCI probe: {stats['eval_from_uci']}")
	print(f"Delusion incidents found: {stats['incidents']}")


def main() -> int:
	args = parse_args()

	if args.top_n <= 0:
		raise SystemExit("--top-n must be > 0")
	if args.probe_depth <= 0:
		raise SystemExit("--probe-depth must be > 0")
	if args.probe_movetime_ms <= 0:
		raise SystemExit("--probe-movetime-ms must be > 0")

	incidents, stats = analyze_pgn(args)
	print_summary(stats, args)

	top = top_unique_incidents(incidents, args.top_n)
	if not top:
		print()
		print("No delusion incidents matched the configured thresholds.")
		print("Try lowering thresholds or enabling UCI fallback with --score-source auto/uci.")
		return 0

	print()
	print(f"=== Top {len(top)} Hallucinations ===")
	for idx, inc in enumerate(top, start=1):
		print()
		print(f"[{idx}] severity={inc.severity_cp}cp reason={inc.reason}")
		print(
			f"  game={inc.game_index} round={inc.round_id} opening={inc.opening} "
			f"candidate={color_name(inc.candidate_color)} result={inc.result}"
		)
		print(
			f"  candidate move: ply={inc.ply} {inc.candidate_move_san} ({inc.candidate_move_uci}) "
			f"eval={format_cp(inc.candidate_eval_cp)}cp source={inc.candidate_eval_source}"
		)
		if inc.opponent_reply_san is not None:
			print(
				f"  opponent reply: {inc.opponent_reply_san} ({inc.opponent_reply_uci}) "
				f"eval={format_cp(inc.opponent_eval_cp)}cp "
				f"source={inc.opponent_eval_source or 'n/a'} "
				f"drop={format_cp(inc.drop_cp)}cp"
			)
		else:
			print("  opponent reply: n/a")
		print(f"  fen: {inc.fen}")

		breakdown = run_eval_breakdown(
			engine_path=args.engine,
			engine_args=args.engine_args,
			fen=inc.fen,
			timeout_sec=args.eval_timeout_sec,
		)
		print("  eval breakdown output:")
		for line in breakdown.splitlines():
			print(f"    {line}")

	return 0


if __name__ == "__main__":
	sys.exit(main())
