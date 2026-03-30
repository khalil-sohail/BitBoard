#include "board.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int KING_IDX = static_cast<int>(PieceType::King);
constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;

inline int lsbIndex(uint64_t bb) {
	return __builtin_ctzll(bb);
}

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE = {
	82, 337, 365, 477, 1025, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE = {
	94, 281, 297, 512, 936, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC = {
	0, 1, 1, 2, 4, 0
};

constexpr std::array<std::array<int, 64>, static_cast<size_t>(PieceType::Count)> MG_PESTO = {{
	{{
		 0,   0,   0,   0,   0,   0,   0,   0,
		98, 134,  61,  95,  68, 126,  34, -11,
		-6,   7,  26,  31,  65,  56,  25, -20,
	   -14,  13,   6,  21,  23,  12,  17, -23,
	   -27,  -2,  -5,  12,  17,   6,  10, -25,
	   -26,  -4,  -4, -10,   3,   3,  33, -12,
	   -35,  -1, -20, -23, -15,  24,  38, -22,
		 0,   0,   0,   0,   0,   0,   0,   0
	}},
	{{
	  -167, -89, -34, -49,  61, -97, -15, -107,
	   -73, -41,  72,  36,  23,  62,   7,  -17,
	   -47,  60,  37,  65,  84, 129,  73,   44,
		-9,  17,  19,  53,  37,  69,  18,   22,
	   -13,   4,  16,  13,  28,  19,  21,   -8,
	   -23,  -9,  12,  10,  19,  17,  25,  -16,
	   -29, -53, -12,  -3,  -1,  18, -14,  -19,
	  -105, -21, -58, -33, -17, -28, -19,  -23
	}},
	{{
	   -29,   4, -82, -37, -25, -42,   7,  -8,
	   -26,  16, -18, -13,  30,  59,  18, -47,
	   -16,  37,  43,  40,  35,  50,  37,  -2,
		-4,   5,  19,  50,  37,  37,   7,  -2,
		-6,  13,  13,  26,  34,  12,  10,   4,
		 0,  15,  15,  15,  14,  27,  18,  10,
		 4,  15,  16,   0,   7,  21,  33,   1,
	   -33,  -3, -14, -21, -13, -12, -39, -21
	}},
	{{
		32,  42,  32,  51, 63,  9,  31,  43,
		27,  32,  58,  62, 80, 67,  26,  44,
		-5,  19,  26,  36, 17, 45,  61,  16,
	   -24, -11,   7,  26, 24, 35,  -8, -20,
	   -36, -26, -12,  -1,  9, -7,   6, -23,
	   -45, -25, -16, -17,  3,  0,  -5, -33,
	   -44, -16, -20,  -9, -1, 11,  -6, -71,
	   -19, -13,   1,  17, 16,  7, -37, -26
	}},
	{{
	   -28,   0,  29,  12, 59, 44,  43,  45,
	   -24, -39,  -5,   1,-16, 57,  28,  54,
	   -13, -17,   7,   8, 29, 56,  47,  57,
	   -27, -27, -16, -16, -1, 17,  -2,   1,
		-9, -26,  -9, -10, -2, -4,   3,  -3,
	   -14,   2, -11,  -2, -5,  2,  14,   5,
	   -35,  -8,  11,   2,  8, 15,  -3,   1,
		-1, -18,  -9,  10,-15,-25, -31, -50
	}},
	{{
	   -65,  23,  16, -15, -56, -34,   2,  13,
		29,  -1, -20,  -7,  -8,  -4, -38, -29,
		-9,  24,   2, -16, -20,   6,  22, -22,
	   -17, -20, -12, -27, -30, -25, -14, -36,
	   -49,  -1, -27, -39, -46, -44, -33, -51,
	   -14, -14, -22, -46, -44, -30, -15, -27,
		 1,   7,  -8, -64, -43, -16,   9,   8,
	   -15,  36,  12, -54,   8, -28,  24,  14
	}}
}};

constexpr std::array<std::array<int, 64>, static_cast<size_t>(PieceType::Count)> EG_PESTO = {{
	{{
		 0,   0,   0,   0,   0,   0,   0,   0,
	   178, 173, 158, 134, 147, 132, 165, 187,
		94, 100,  85,  67,  56,  53,  82,  84,
		32,  24,  13,   5,  -2,   4,  17,  17,
		13,   9,  -3,  -7,  -7,  -8,   3,  -1,
		 4,   7,  -6,   1,   0,  -5,  -1,  -8,
		13,   8,   8,  10,  13,   0,   2,  -7,
		 0,   0,   0,   0,   0,   0,   0,   0
	}},
	{{
	   -58, -38, -13, -28, -31, -27, -63, -99,
	   -25,  -8, -25,  -2,  -9, -25, -24, -52,
	   -24, -20,  10,   9,  -1,  -9, -19, -41,
	   -17,   3,  22,  22,  22,  11,   8, -18,
	   -18,  -6,  16,  25,  16,  17,   4, -18,
	   -23,  -3,  -1,  15,  10,  -3, -20, -22,
	   -42, -20, -10,  -5,  -2, -20, -23, -44,
	   -29, -51, -23, -15, -22, -18, -50, -64
	}},
	{{
	   -14, -21, -11,  -8,  -7,  -9, -17, -24,
		-8,  -4,   7, -12,  -3, -13,  -4, -14,
		 2,  -8,   0,  -1,  -2,   6,   0,   4,
		-3,   9,  12,   9,  14,  10,   3,   2,
		-6,   3,  13,  19,   7,  10,  -3,  -9,
	   -12,  -3,   8,  10,  13,   3,  -7, -15,
	   -14, -18,  -7,  -1,   4,  -9, -15, -27,
	   -23,  -9, -23,  -5,  -9, -16,  -5, -17
	}},
	{{
		13, 10, 18, 15, 12, 12,  8,  5,
		11, 13, 13, 11, -3,  3,  8,  3,
		 7,  7,  7,  5,  4, -3, -5, -3,
		 4,  3, 13,  1,  2,  1, -1,  2,
		 3,  5,  8,  4, -5, -6, -8,-11,
		-4,  0, -5, -1, -7,-12, -8,-16,
		-6, -6,  0,  2, -9, -9,-11, -3,
		-9,  2,  3, -1, -5,-13,  4,-20
	}},
	{{
		-9,  22,  22,  27,  27,  19,  10,  20,
	   -17,  20,  32,  41,  58,  25,  30,   0,
	   -20,   6,   9,  49,  47,  35,  19,   9,
		 3,  22,  24,  45,  57,  40,  57,  36,
	   -18,  28,  19,  47,  31,  34,  39,  23,
	   -16, -27,  15,   6,   9,  17,  10,   5,
	   -22, -23, -30, -16, -16, -23, -36, -32,
	   -33, -28, -22, -43,  -5, -32, -20, -41
	}},
	{{
	   -74, -35, -18, -18, -11,  15,   4, -17,
	   -12,  17,  14,  17,  17,  38,  23,  11,
		10,  17,  23,  15,  20,  45,  44,  13,
		-8,  22,  24,  27,  26,  33,  26,   3,
	   -18,  -4,  21,  24,  27,  23,   9, -11,
	   -19,  -3,  11,  21,  23,  16,   7,  -9,
	   -27, -11,   4,  13,  14,   4,  -5, -17,
	   -53, -34, -21, -11, -28, -14, -24, -43
	}}
}};

constexpr int mirrorSquare(int square) {
	return square ^ 56;
}

constexpr int pestoSquare(Color color, int square) {
	return (color == Color::White) ? square : mirrorSquare(square);
}

constexpr int fileOf(int square) {
	return square & 7;
}

struct EvalMasks {
	std::array<uint64_t, 8> FILE_MASKS{};
	std::array<std::array<uint64_t, 64>, 2> PASSED_PAWN_MASKS{};
	std::array<std::array<uint64_t, 64>, 2> KING_SHIELD_MASKS{};

	constexpr EvalMasks() {
		for (int i = 0; i < 8; ++i) {
			FILE_MASKS[static_cast<size_t>(i)] = 0x0101010101010101ULL << i;
		}

		for (int color = 0; color < 2; ++color) {
			for (int sq = 0; sq < 64; ++sq) {
				const int file = sq & 7;
				const int rank = sq >> 3;
				uint64_t mask = 0ULL;

				for (int df = -1; df <= 1; ++df) {
					const int targetFile = file + df;
					if (targetFile < 0 || targetFile > 7) {
						continue;
					}

					if (color == WHITE_IDX) {
						for (int targetRank = rank + 1; targetRank < 8; ++targetRank) {
							mask |= (1ULL << (targetRank * 8 + targetFile));
						}
					} else {
						for (int targetRank = rank - 1; targetRank >= 0; --targetRank) {
							mask |= (1ULL << (targetRank * 8 + targetFile));
						}
					}
				}

				PASSED_PAWN_MASKS[static_cast<size_t>(color)][static_cast<size_t>(sq)] = mask;

				uint64_t shieldMask = 0ULL;
				const int startFile = std::max(0, file - 1);
				const int endFile = std::min(7, file + 1);

				for (int f = startFile; f <= endFile; ++f) {
					if (color == WHITE_IDX) {
						const int rank1 = rank + 1;
						const int rank2 = rank + 2;
						if (rank1 < 8) {
							shieldMask |= (1ULL << (rank1 * 8 + f));
						}
						if (rank2 < 8) {
							shieldMask |= (1ULL << (rank2 * 8 + f));
						}
					} else {
						const int rank1 = rank - 1;
						const int rank2 = rank - 2;
						if (rank1 >= 0) {
							shieldMask |= (1ULL << (rank1 * 8 + f));
						}
						if (rank2 >= 0) {
							shieldMask |= (1ULL << (rank2 * 8 + f));
						}
					}
				}

				KING_SHIELD_MASKS[static_cast<size_t>(color)][static_cast<size_t>(sq)] = shieldMask;
			}
		}
	}
};

constexpr EvalMasks MASKS;

int passedPawnCount(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
	int count = 0;
	uint64_t pawns = ownPawns;
	const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;

	while (pawns != 0ULL) {
		const int square = lsbIndex(pawns);
		pawns &= (pawns - 1);

		if ((enemyPawns & MASKS.PASSED_PAWN_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(square)]) == 0ULL) {
			++count;
		}
	}

	return count;
}

int rookOpenFileBonus(uint64_t rooks, uint64_t allPawns) {
	int bonus = 0;
	uint64_t bb = rooks;

	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);

		const int file = fileOf(square);
		if ((allPawns & MASKS.FILE_MASKS[static_cast<size_t>(file)]) == 0ULL) {
			bonus += 20;
		}
	}

	return bonus;
}

int kingPawnShieldBonus(Color color, int kingSquare, uint64_t ownPawns) {
	const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;
	const uint64_t shield = MASKS.KING_SHIELD_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(kingSquare)] & ownPawns;
	const int count = std::popcount(shield);
	return std::min(count, 3) * 15;
}

int pawnStructurePenalty(uint64_t ownPawns) {
	int penalty = 0;

	for (int file = 0; file < 8; ++file) {
		const uint64_t filePawns = ownPawns & MASKS.FILE_MASKS[static_cast<size_t>(file)];
		if (filePawns == 0ULL) {
			continue;
		}

		const int count = std::popcount(filePawns);
		if (count > 1) {
			penalty += (count - 1) * 15;
		}

		uint64_t adjMask = 0ULL;
		if (file > 0) {
			adjMask |= MASKS.FILE_MASKS[static_cast<size_t>(file - 1)];
		}
		if (file < 7) {
			adjMask |= MASKS.FILE_MASKS[static_cast<size_t>(file + 1)];
		}

		if ((ownPawns & adjMask) == 0ULL) {
			penalty += count * 20;
		}
	}

	return penalty;
}

int mopUpEval(int winningKingSq, int losingKingSq) {
	const int losingFile = fileOf(losingKingSq);
	const int losingRank = losingKingSq >> 3;
	const int centerDistance = std::abs(losingFile - 3) + std::abs(losingRank - 3);

	const int winningFile = fileOf(winningKingSq);
	const int winningRank = winningKingSq >> 3;
	const int kingDistance = std::abs(winningFile - losingFile) + std::abs(winningRank - losingRank);

	return centerDistance * 10 + (14 - kingDistance) * 4;
}

int passedPawnBonus(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
	int bonus = 0;
	uint64_t pawns = ownPawns;

	while (pawns != 0ULL) {
		const int square = lsbIndex(pawns);
		pawns &= (pawns - 1);

		const int sq = square;
		const int file = fileOf(sq);
		const int rank = sq >> 3;

		uint64_t mask = 0ULL;
		const int startFile = std::max(0, file - 1);
		const int endFile = std::min(7, file + 1);

		if (color == Color::White) {
			for (int targetRank = rank + 1; targetRank < 8; ++targetRank) {
				for (int targetFile = startFile; targetFile <= endFile; ++targetFile) {
					mask |= (1ULL << (targetRank * 8 + targetFile));
				}
			}
		} else {
			for (int targetRank = rank - 1; targetRank >= 0; --targetRank) {
				for (int targetFile = startFile; targetFile <= endFile; ++targetFile) {
					mask |= (1ULL << (targetRank * 8 + targetFile));
				}
			}
		}

		if ((mask & enemyPawns) == 0ULL) {
			const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
			int pawnBonus = (relativeRank * relativeRank) * 5;
			uint64_t forwardSq = (color == Color::White) ? (1ULL << (sq + 8)) : (1ULL << (sq - 8));
			uint64_t oppOcc = enemyPawns;

			if (forwardSq & oppOcc) {
				pawnBonus /= 2;
			}
			bonus += pawnBonus;
		}
	}

	return bonus;
}

int trappedRookPenalty(Color color, uint64_t rooks, uint64_t king) {
	if (king == 0ULL) {
		return 0;
	}

	if (color == Color::White) {
		const bool rookInCorner = (rooks & ((1ULL << 0) | (1ULL << 7))) != 0ULL;
		const bool kingTrapsRook = (king & ((1ULL << 1) | (1ULL << 2) | (1ULL << 5) | (1ULL << 6))) != 0ULL;
		return (rookInCorner && kingTrapsRook) ? 50 : 0;
	}

	const bool rookInCorner = (rooks & ((1ULL << 56) | (1ULL << 63))) != 0ULL;
	const bool kingTrapsRook = (king & ((1ULL << 57) | (1ULL << 58) | (1ULL << 61) | (1ULL << 62))) != 0ULL;
	return (rookInCorner && kingTrapsRook) ? 50 : 0;
}

int badBishopPenalty(uint64_t ownPawns, uint64_t ownBishops, Color color) {
	int penalty = 0;
	if (color == Color::White) {
		if ((ownBishops & 512ULL) && (ownPawns & 262144ULL)) penalty += 150;
		if ((ownBishops & 16384ULL) && (ownPawns & 2097152ULL)) penalty += 150;
		if ((ownBishops & 4ULL) && (ownPawns & 2048ULL)) penalty += 120;
		if ((ownBishops & 32ULL) && (ownPawns & 4096ULL)) penalty += 120;
	} else {
		if ((ownBishops & 562949953421312ULL) && (ownPawns & 4398046511104ULL)) penalty += 150;
		if ((ownBishops & 18014398509481984ULL) && (ownPawns & 35184372088832ULL)) penalty += 150;
		if ((ownBishops & 288230376151711744ULL) && (ownPawns & 2251799813685248ULL)) penalty += 120;
		if ((ownBishops & 2305843009213693952ULL) && (ownPawns & 4503599627370496ULL)) penalty += 120;
	}
	return penalty;
}

int earlyQueenPenalty(uint64_t ownQueen, uint64_t ownKnights, uint64_t ownBishops, Color color) {
	if (!ownQueen) return 0;

	int penalty = 0;
	uint64_t startingRank = (color == Color::White) ? Board::RANK_1 : Board::RANK_8;

	if ((ownQueen & startingRank) == 0ULL) {
		int undevelopedMinors = std::popcount((ownKnights | ownBishops) & startingRank);
		if (undevelopedMinors >= 2) {
			penalty += 40 * (undevelopedMinors - 1);
		}
	}
	return penalty;
}

int uncastledKingPenalty(uint64_t king, uint8_t castlingRights, Color color) {
	if (!king) return 0;
	int penalty = 0;
	int kingSq = lsbIndex(king);
	int file = kingSq % 8;

	if (file >= 2 && file <= 5) {
		penalty += 40;

		if (color == Color::White && !(castlingRights & (CASTLE_WK | CASTLE_WQ))) {
			penalty += 120;
		} else if (color == Color::Black && !(castlingRights & (CASTLE_BK | CASTLE_BQ))) {
			penalty += 120;
		}
	}
	return penalty;
}

} // namespace

void Board::addPieceEval(Color color, PieceType piece, int square) {
	const int c = static_cast<int>(color);
	const int p = static_cast<int>(piece);
	const int idx = pestoSquare(color, square);

	m_mgScore[c] += MG_VALUE[static_cast<size_t>(p)] + MG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
	m_egScore[c] += EG_VALUE[static_cast<size_t>(p)] + EG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
	m_gamePhase += GAME_PHASE_INC[static_cast<size_t>(p)];
}

void Board::removePieceEval(Color color, PieceType piece, int square) {
	const int c = static_cast<int>(color);
	const int p = static_cast<int>(piece);
	const int idx = pestoSquare(color, square);

	m_mgScore[c] -= MG_VALUE[static_cast<size_t>(p)] + MG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
	m_egScore[c] -= EG_VALUE[static_cast<size_t>(p)] + EG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
	m_gamePhase -= GAME_PHASE_INC[static_cast<size_t>(p)];
}

void Board::resetEvalStateFromBoard() {
	m_mgScore = {0, 0};
	m_egScore = {0, 0};
	m_gamePhase = 0;

	for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
		for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
			uint64_t bb = m_bitboards[color][piece];
			while (bb != 0ULL) {
				const int square = lsbIndex(bb);
				bb &= (bb - 1);
				addPieceEval(static_cast<Color>(color), static_cast<PieceType>(piece), square);
			}
		}
	}
}

int Board::evaluate() const {
	bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
	bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);
	bool noWhiteMajors = ((m_bitboards[WHITE_IDX][ROOK_IDX] | m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Queen)]) == 0);
	bool noBlackMajors = ((m_bitboards[BLACK_IDX][ROOK_IDX] | m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Queen)]) == 0);

	if (noWhitePawns && noBlackPawns && noWhiteMajors && noBlackMajors) {
		int whiteMinors = std::popcount(m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Knight)] | m_bitboards[WHITE_IDX][BISHOP_IDX]);
		int blackMinors = std::popcount(m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Knight)] | m_bitboards[BLACK_IDX][BISHOP_IDX]);
		if (whiteMinors < 2 && blackMinors < 2) {
			return 0;
		}
	}

	int mg = m_mgScore[WHITE_IDX] - m_mgScore[BLACK_IDX];
	int eg = m_egScore[WHITE_IDX] - m_egScore[BLACK_IDX];

	const int whiteBishops = std::popcount(m_bitboards[WHITE_IDX][BISHOP_IDX]);
	const int blackBishops = std::popcount(m_bitboards[BLACK_IDX][BISHOP_IDX]);

	if (whiteBishops >= 2) {
		mg += 30;
		eg += 40;
	}
	if (blackBishops >= 2) {
		mg -= 30;
		eg -= 40;
	}

	const uint64_t allPawns = m_bitboards[WHITE_IDX][PAWN_IDX] | m_bitboards[BLACK_IDX][PAWN_IDX];
	mg += rookOpenFileBonus(m_bitboards[WHITE_IDX][ROOK_IDX], allPawns);
	mg -= rookOpenFileBonus(m_bitboards[BLACK_IDX][ROOK_IDX], allPawns);

	const int whiteKingSquare = lsbIndex(m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackKingSquare = lsbIndex(m_bitboards[BLACK_IDX][KING_IDX]);
	mg += kingPawnShieldBonus(Color::White, whiteKingSquare, m_bitboards[WHITE_IDX][PAWN_IDX]);
	mg -= kingPawnShieldBonus(Color::Black, blackKingSquare, m_bitboards[BLACK_IDX][PAWN_IDX]);

	const int whitePawnPenalty = pawnStructurePenalty(m_bitboards[WHITE_IDX][PAWN_IDX]);
	const int blackPawnPenalty = pawnStructurePenalty(m_bitboards[BLACK_IDX][PAWN_IDX]);
	mg -= whitePawnPenalty;
	eg -= whitePawnPenalty;
	mg += blackPawnPenalty;
	eg += blackPawnPenalty;

	const int passedWhite = passedPawnCount(Color::White, m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][PAWN_IDX]);
	const int passedBlack = passedPawnCount(Color::Black, m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][PAWN_IDX]);
	mg += 20 * (passedWhite - passedBlack);
	eg += 40 * (passedWhite - passedBlack);

	const int whitePassed = passedPawnBonus(Color::White, m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][PAWN_IDX]);
	const int blackPassed = passedPawnBonus(Color::Black, m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][PAWN_IDX]);
	mg += whitePassed;
	mg -= blackPassed;
	eg += 2 * whitePassed;
	eg -= 2 * blackPassed;

	const int whiteTrapped = trappedRookPenalty(Color::White, m_bitboards[WHITE_IDX][ROOK_IDX], m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackTrapped = trappedRookPenalty(Color::Black, m_bitboards[BLACK_IDX][ROOK_IDX], m_bitboards[BLACK_IDX][KING_IDX]);
	mg -= whiteTrapped;
	mg += blackTrapped;

	const int whiteBadBishop = badBishopPenalty(m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][BISHOP_IDX], Color::White);
	const int blackBadBishop = badBishopPenalty(m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][BISHOP_IDX], Color::Black);
	const int whiteEarlyQueen = earlyQueenPenalty(
		m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Queen)],
		m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Knight)],
		m_bitboards[WHITE_IDX][BISHOP_IDX],
		Color::White
	);
	const int blackEarlyQueen = earlyQueenPenalty(
		m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Queen)],
		m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Knight)],
		m_bitboards[BLACK_IDX][BISHOP_IDX],
		Color::Black
	);

	mg -= (whiteBadBishop + whiteEarlyQueen);
	mg += (blackBadBishop + blackEarlyQueen);
	eg -= whiteBadBishop;
	eg += blackBadBishop;

	int whiteUncastled = uncastledKingPenalty(m_bitboards[WHITE_IDX][KING_IDX], m_castlingRights, Color::White);
	int blackUncastled = uncastledKingPenalty(m_bitboards[BLACK_IDX][KING_IDX], m_castlingRights, Color::Black);
	mg -= whiteUncastled;
	mg += blackUncastled;

	if (eg > 200) {
		eg += mopUpEval(whiteKingSquare, blackKingSquare);
	} else if (eg < -200) {
		eg -= mopUpEval(blackKingSquare, whiteKingSquare);
	}

	const int phase = std::clamp(m_gamePhase, 0, 24);
	int score = (mg * phase + eg * (24 - phase)) / 24;

	if (score > 0 && noWhitePawns) {
		score /= 2;
	} else if (score < 0 && noBlackPawns) {
		score /= 2;
	}

	return score;
}

int Board::evaluateSideToMove() const {
	const int eval = evaluate();
	return (m_sideToMove == Color::White) ? eval : -eval;
}

int Board::computeStaticEvaluation() const {
	bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
	bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);
	bool noWhiteMajors = ((m_bitboards[WHITE_IDX][ROOK_IDX] | m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Queen)]) == 0);
	bool noBlackMajors = ((m_bitboards[BLACK_IDX][ROOK_IDX] | m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Queen)]) == 0);

	if (noWhitePawns && noBlackPawns && noWhiteMajors && noBlackMajors) {
		int whiteMinors = std::popcount(m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Knight)] | m_bitboards[WHITE_IDX][BISHOP_IDX]);
		int blackMinors = std::popcount(m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Knight)] | m_bitboards[BLACK_IDX][BISHOP_IDX]);
		if (whiteMinors < 2 && blackMinors < 2) {
			return 0;
		}
	}

	std::array<int, 2> mgScore = {0, 0};
	std::array<int, 2> egScore = {0, 0};
	int phase = 0;

	for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
		for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
			uint64_t bb = m_bitboards[color][piece];
			while (bb != 0ULL) {
				const int square = lsbIndex(bb);
				bb &= (bb - 1);

				const int idx = pestoSquare(static_cast<Color>(color), square);
				mgScore[color] += MG_VALUE[static_cast<size_t>(piece)] + MG_PESTO[static_cast<size_t>(piece)][static_cast<size_t>(idx)];
				egScore[color] += EG_VALUE[static_cast<size_t>(piece)] + EG_PESTO[static_cast<size_t>(piece)][static_cast<size_t>(idx)];
				phase += GAME_PHASE_INC[static_cast<size_t>(piece)];
			}
		}
	}

	int mg = mgScore[WHITE_IDX] - mgScore[BLACK_IDX];
	int eg = egScore[WHITE_IDX] - egScore[BLACK_IDX];

	const int whiteBishops = std::popcount(m_bitboards[WHITE_IDX][BISHOP_IDX]);
	const int blackBishops = std::popcount(m_bitboards[BLACK_IDX][BISHOP_IDX]);

	if (whiteBishops >= 2) {
		mg += 30;
		eg += 40;
	}
	if (blackBishops >= 2) {
		mg -= 30;
		eg -= 40;
	}

	const uint64_t allPawns = m_bitboards[WHITE_IDX][PAWN_IDX] | m_bitboards[BLACK_IDX][PAWN_IDX];
	mg += rookOpenFileBonus(m_bitboards[WHITE_IDX][ROOK_IDX], allPawns);
	mg -= rookOpenFileBonus(m_bitboards[BLACK_IDX][ROOK_IDX], allPawns);

	const int whiteKingSquare = lsbIndex(m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackKingSquare = lsbIndex(m_bitboards[BLACK_IDX][KING_IDX]);
	mg += kingPawnShieldBonus(Color::White, whiteKingSquare, m_bitboards[WHITE_IDX][PAWN_IDX]);
	mg -= kingPawnShieldBonus(Color::Black, blackKingSquare, m_bitboards[BLACK_IDX][PAWN_IDX]);

	const int whitePawnPenalty = pawnStructurePenalty(m_bitboards[WHITE_IDX][PAWN_IDX]);
	const int blackPawnPenalty = pawnStructurePenalty(m_bitboards[BLACK_IDX][PAWN_IDX]);
	mg -= whitePawnPenalty;
	eg -= whitePawnPenalty;
	mg += blackPawnPenalty;
	eg += blackPawnPenalty;

	const int passedWhite = passedPawnCount(Color::White, m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][PAWN_IDX]);
	const int passedBlack = passedPawnCount(Color::Black, m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][PAWN_IDX]);
	mg += 20 * (passedWhite - passedBlack);
	eg += 40 * (passedWhite - passedBlack);

	const int whitePassed = passedPawnBonus(Color::White, m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][PAWN_IDX]);
	const int blackPassed = passedPawnBonus(Color::Black, m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][PAWN_IDX]);
	mg += whitePassed;
	mg -= blackPassed;
	eg += 2 * whitePassed;
	eg -= 2 * blackPassed;

	const int whiteTrapped = trappedRookPenalty(Color::White, m_bitboards[WHITE_IDX][ROOK_IDX], m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackTrapped = trappedRookPenalty(Color::Black, m_bitboards[BLACK_IDX][ROOK_IDX], m_bitboards[BLACK_IDX][KING_IDX]);
	mg -= whiteTrapped;
	mg += blackTrapped;

	const int whiteBadBishop = badBishopPenalty(m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][BISHOP_IDX], Color::White);
	const int blackBadBishop = badBishopPenalty(m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][BISHOP_IDX], Color::Black);
	const int whiteEarlyQueen = earlyQueenPenalty(
		m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Queen)],
		m_bitboards[WHITE_IDX][static_cast<int>(PieceType::Knight)],
		m_bitboards[WHITE_IDX][BISHOP_IDX],
		Color::White
	);
	const int blackEarlyQueen = earlyQueenPenalty(
		m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Queen)],
		m_bitboards[BLACK_IDX][static_cast<int>(PieceType::Knight)],
		m_bitboards[BLACK_IDX][BISHOP_IDX],
		Color::Black
	);

	mg -= (whiteBadBishop + whiteEarlyQueen);
	mg += (blackBadBishop + blackEarlyQueen);
	eg -= whiteBadBishop;
	eg += blackBadBishop;

	int whiteUncastled = uncastledKingPenalty(m_bitboards[WHITE_IDX][KING_IDX], m_castlingRights, Color::White);
	int blackUncastled = uncastledKingPenalty(m_bitboards[BLACK_IDX][KING_IDX], m_castlingRights, Color::Black);
	mg -= whiteUncastled;
	mg += blackUncastled;

	if (eg > 200) {
		eg += mopUpEval(whiteKingSquare, blackKingSquare);
	} else if (eg < -200) {
		eg -= mopUpEval(blackKingSquare, whiteKingSquare);
	}

	const int clampedPhase = std::clamp(phase, 0, 24);
	int score = (mg * clampedPhase + eg * (24 - clampedPhase)) / 24;

	if (score > 0 && noWhitePawns) {
		score /= 2;
	} else if (score < 0 && noBlackPawns) {
		score /= 2;
	}

	return score;
}
