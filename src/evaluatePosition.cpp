#include "board.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);
constexpr int KING_IDX = static_cast<int>(PieceType::King);
constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;

inline int lsbIndex(uint64_t bb) {
	return __builtin_ctzll(bb);
}

namespace EvalWeights {

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE = {
	82, 337, 365, 477, 1025, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE = {
	94, 281, 297, 512, 936, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC = {
	0, 1, 1, 2, 4, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_MG = {
	0, 2, 2, 1, 1, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_EG = {
	0, 2, 3, 2, 1, 0
};

constexpr size_t ROOK_ACTIVITY_OPEN_FILE = 0;
constexpr size_t ROOK_ACTIVITY_SEMI_OPEN_FILE = 1;
constexpr size_t ROOK_ACTIVITY_SEVENTH_RANK = 2;

constexpr std::array<int, 3> ROOK_ACTIVITY_BONUS_MG = {
	15, 10, 20
};

constexpr std::array<int, 3> ROOK_ACTIVITY_BONUS_EG = {
	15, 10, 20
};

constexpr std::array<int, 9> CONNECTED_PAWN_BONUS_MG_BY_RANK = {
	0, 0, 2, 5, 9, 15, 24, 0, 0
};

constexpr std::array<int, 9> CONNECTED_PAWN_BONUS_EG_BY_RANK = {
	0, 0, 3, 7, 12, 20, 32, 0, 0
};

constexpr std::array<int, 9> CANDIDATE_PAWN_BONUS_MG_BY_RANK = {
	0, 0, 1, 3, 6, 10, 14, 0, 0
};

constexpr std::array<int, 9> CANDIDATE_PAWN_BONUS_EG_BY_RANK = {
	0, 0, 2, 5, 9, 14, 20, 0, 0
};

constexpr std::array<int, 9> BACKWARD_PAWN_PENALTY_MG_BY_RANK = {
	0, 0, 6, 9, 12, 15, 18, 0, 0
};

constexpr std::array<int, 9> BACKWARD_PAWN_PENALTY_EG_BY_RANK = {
	0, 0, 4, 6, 8, 10, 12, 0, 0
};

constexpr int PAWN_ISLAND_PENALTY_MG = 4;
constexpr int PAWN_ISLAND_PENALTY_EG = 5;

constexpr std::array<int, 9> KING_ATTACK_PRESSURE_PENALTY = {
	0, 8, 22, 45, 80, 120, 160, 200, 240
};

constexpr int BISHOP_PAIR_BONUS_MG = 30;
constexpr int BISHOP_PAIR_BONUS_EG = 40;

constexpr int KING_SHIELD_MAX_PAWNS = 3;
constexpr int KING_SHIELD_PER_PAWN_BONUS = 15;

constexpr int PAWN_STRUCTURE_DOUBLED_PENALTY = 10;
constexpr int PAWN_STRUCTURE_ISOLATED_PENALTY = 10;

constexpr int PASSED_PAWN_COUNT_BONUS_MG = 10;
constexpr int PASSED_PAWN_COUNT_BONUS_EG = 20;
constexpr int PASSED_PAWN_EG_MULTIPLIER = 1;

constexpr int PASSED_PAWN_RANK_SQUARE_MULTIPLIER = 2;
constexpr int PASSED_PAWN_BLOCKED_DIVISOR = 2;

constexpr int TRAPPED_ROOK_PENALTY = 50;

constexpr int BAD_BISHOP_HEAVY_PENALTY = 50;
constexpr int BAD_BISHOP_LIGHT_PENALTY = 25;

constexpr int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY = 20;

constexpr int UNCASTLED_KING_CENTER_PENALTY = 20;
constexpr int UNCASTLED_KING_LOST_RIGHTS_PENALTY = 50;

constexpr int TAPER_SCALE = 128;
constexpr int LATE_ENDGAME_PHASE_MAX = 8;
constexpr int MOP_UP_EG_MARGIN = 220;
constexpr int MOP_UP_MATERIAL_MARGIN = 350;
constexpr int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS = 56;
constexpr int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS = 72;
constexpr int SCALE_MINOR_ONLY_NEAR_EQUAL = 64;
constexpr int SCALE_MINOR_ONLY_CLEAR_EDGE = 80;

constexpr int MOP_UP_CENTER_DISTANCE_WEIGHT = 6;
constexpr int MOP_UP_EDGE_DISTANCE_BASE = 3;
constexpr int MOP_UP_EDGE_PRESSURE_WEIGHT = 8;
constexpr int MOP_UP_CORNER_DISTANCE_CAP = 7;
constexpr int MOP_UP_CORNER_PRESSURE_WEIGHT = 2;
constexpr int MOP_UP_KING_DISTANCE_BASE = 14;
constexpr int MOP_UP_KING_DISTANCE_WEIGHT = 4;

} // namespace EvalWeights

constexpr const auto& MG_VALUE = EvalWeights::MG_VALUE;
constexpr const auto& EG_VALUE = EvalWeights::EG_VALUE;
constexpr const auto& GAME_PHASE_INC = EvalWeights::GAME_PHASE_INC;
constexpr const auto& MOBILITY_BONUS_MG = EvalWeights::MOBILITY_BONUS_MG;
constexpr const auto& MOBILITY_BONUS_EG = EvalWeights::MOBILITY_BONUS_EG;
constexpr size_t ROOK_ACTIVITY_OPEN_FILE = EvalWeights::ROOK_ACTIVITY_OPEN_FILE;
constexpr size_t ROOK_ACTIVITY_SEMI_OPEN_FILE = EvalWeights::ROOK_ACTIVITY_SEMI_OPEN_FILE;
constexpr size_t ROOK_ACTIVITY_SEVENTH_RANK = EvalWeights::ROOK_ACTIVITY_SEVENTH_RANK;
constexpr const auto& ROOK_ACTIVITY_BONUS_MG = EvalWeights::ROOK_ACTIVITY_BONUS_MG;
constexpr const auto& ROOK_ACTIVITY_BONUS_EG = EvalWeights::ROOK_ACTIVITY_BONUS_EG;
constexpr const auto& CONNECTED_PAWN_BONUS_MG_BY_RANK = EvalWeights::CONNECTED_PAWN_BONUS_MG_BY_RANK;
constexpr const auto& CONNECTED_PAWN_BONUS_EG_BY_RANK = EvalWeights::CONNECTED_PAWN_BONUS_EG_BY_RANK;
constexpr const auto& CANDIDATE_PAWN_BONUS_MG_BY_RANK = EvalWeights::CANDIDATE_PAWN_BONUS_MG_BY_RANK;
constexpr const auto& CANDIDATE_PAWN_BONUS_EG_BY_RANK = EvalWeights::CANDIDATE_PAWN_BONUS_EG_BY_RANK;
constexpr const auto& BACKWARD_PAWN_PENALTY_MG_BY_RANK = EvalWeights::BACKWARD_PAWN_PENALTY_MG_BY_RANK;
constexpr const auto& BACKWARD_PAWN_PENALTY_EG_BY_RANK = EvalWeights::BACKWARD_PAWN_PENALTY_EG_BY_RANK;
constexpr int PAWN_ISLAND_PENALTY_MG = EvalWeights::PAWN_ISLAND_PENALTY_MG;
constexpr int PAWN_ISLAND_PENALTY_EG = EvalWeights::PAWN_ISLAND_PENALTY_EG;
constexpr const auto& KING_ATTACK_PRESSURE_PENALTY = EvalWeights::KING_ATTACK_PRESSURE_PENALTY;
constexpr int BISHOP_PAIR_BONUS_MG = EvalWeights::BISHOP_PAIR_BONUS_MG;
constexpr int BISHOP_PAIR_BONUS_EG = EvalWeights::BISHOP_PAIR_BONUS_EG;
constexpr int KING_SHIELD_MAX_PAWNS = EvalWeights::KING_SHIELD_MAX_PAWNS;
constexpr int KING_SHIELD_PER_PAWN_BONUS = EvalWeights::KING_SHIELD_PER_PAWN_BONUS;
constexpr int PAWN_STRUCTURE_DOUBLED_PENALTY = EvalWeights::PAWN_STRUCTURE_DOUBLED_PENALTY;
constexpr int PAWN_STRUCTURE_ISOLATED_PENALTY = EvalWeights::PAWN_STRUCTURE_ISOLATED_PENALTY;
constexpr int PASSED_PAWN_COUNT_BONUS_MG = EvalWeights::PASSED_PAWN_COUNT_BONUS_MG;
constexpr int PASSED_PAWN_COUNT_BONUS_EG = EvalWeights::PASSED_PAWN_COUNT_BONUS_EG;
constexpr int PASSED_PAWN_EG_MULTIPLIER = EvalWeights::PASSED_PAWN_EG_MULTIPLIER;
constexpr int PASSED_PAWN_RANK_SQUARE_MULTIPLIER = EvalWeights::PASSED_PAWN_RANK_SQUARE_MULTIPLIER;
constexpr int PASSED_PAWN_BLOCKED_DIVISOR = EvalWeights::PASSED_PAWN_BLOCKED_DIVISOR;
constexpr int TRAPPED_ROOK_PENALTY = EvalWeights::TRAPPED_ROOK_PENALTY;
constexpr int BAD_BISHOP_HEAVY_PENALTY = EvalWeights::BAD_BISHOP_HEAVY_PENALTY;
constexpr int BAD_BISHOP_LIGHT_PENALTY = EvalWeights::BAD_BISHOP_LIGHT_PENALTY;
constexpr int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY = EvalWeights::EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY;
constexpr int UNCASTLED_KING_CENTER_PENALTY = EvalWeights::UNCASTLED_KING_CENTER_PENALTY;
constexpr int UNCASTLED_KING_LOST_RIGHTS_PENALTY = EvalWeights::UNCASTLED_KING_LOST_RIGHTS_PENALTY;
constexpr int TAPER_SCALE = EvalWeights::TAPER_SCALE;
constexpr int LATE_ENDGAME_PHASE_MAX = EvalWeights::LATE_ENDGAME_PHASE_MAX;
constexpr int MOP_UP_EG_MARGIN = EvalWeights::MOP_UP_EG_MARGIN;
constexpr int MOP_UP_MATERIAL_MARGIN = EvalWeights::MOP_UP_MATERIAL_MARGIN;
constexpr int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS = EvalWeights::SCALE_OPPOSITE_BISHOPS_MIN_PAWNS;
constexpr int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS = EvalWeights::SCALE_OPPOSITE_BISHOPS_LOW_PAWNS;
constexpr int SCALE_MINOR_ONLY_NEAR_EQUAL = EvalWeights::SCALE_MINOR_ONLY_NEAR_EQUAL;
constexpr int SCALE_MINOR_ONLY_CLEAR_EDGE = EvalWeights::SCALE_MINOR_ONLY_CLEAR_EDGE;
constexpr int MOP_UP_CENTER_DISTANCE_WEIGHT = EvalWeights::MOP_UP_CENTER_DISTANCE_WEIGHT;
constexpr int MOP_UP_EDGE_DISTANCE_BASE = EvalWeights::MOP_UP_EDGE_DISTANCE_BASE;
constexpr int MOP_UP_EDGE_PRESSURE_WEIGHT = EvalWeights::MOP_UP_EDGE_PRESSURE_WEIGHT;
constexpr int MOP_UP_CORNER_DISTANCE_CAP = EvalWeights::MOP_UP_CORNER_DISTANCE_CAP;
constexpr int MOP_UP_CORNER_PRESSURE_WEIGHT = EvalWeights::MOP_UP_CORNER_PRESSURE_WEIGHT;
constexpr int MOP_UP_KING_DISTANCE_BASE = EvalWeights::MOP_UP_KING_DISTANCE_BASE;
constexpr int MOP_UP_KING_DISTANCE_WEIGHT = EvalWeights::MOP_UP_KING_DISTANCE_WEIGHT;

struct TaperTerms {
	int mg = 0;
	int eg = 0;
};

struct PieceScoreDelta {
	int mg = 0;
	int eg = 0;
	int phase = 0;
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

constexpr uint64_t squareMask(int square) {
	return 1ULL << square;
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

uint64_t knightAttacks(int square) {
	const uint64_t k = squareMask(square);

	constexpr uint64_t FILE_B = Board::FILE_A << 1;
	constexpr uint64_t FILE_G = Board::FILE_H >> 1;
	constexpr uint64_t NOT_FILE_A = ~Board::FILE_A;
	constexpr uint64_t NOT_FILE_H = ~Board::FILE_H;
	constexpr uint64_t NOT_FILE_AB = ~(Board::FILE_A | FILE_B);
	constexpr uint64_t NOT_FILE_GH = ~(FILE_G | Board::FILE_H);

	return ((k << 17) & NOT_FILE_A) |
	       ((k << 15) & NOT_FILE_H) |
	       ((k << 10) & NOT_FILE_AB) |
	       ((k << 6) & NOT_FILE_GH) |
	       ((k >> 17) & NOT_FILE_H) |
	       ((k >> 15) & NOT_FILE_A) |
	       ((k >> 10) & NOT_FILE_GH) |
	       ((k >> 6) & NOT_FILE_AB);
}

uint64_t bishopAttacks(int square, uint64_t occupied) {
	uint64_t attacks = 0ULL;
	const int rank = square >> 3;
	const int file = fileOf(square);

	for (int r = rank + 1, f = file + 1; r < 8 && f < 8; ++r, ++f) {
		const int sq = r * 8 + f;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}
	for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; ++r, --f) {
		const int sq = r * 8 + f;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}
	for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; --r, ++f) {
		const int sq = r * 8 + f;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}
	for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
		const int sq = r * 8 + f;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}

	return attacks;
}

uint64_t rookAttacks(int square, uint64_t occupied) {
	uint64_t attacks = 0ULL;
	const int rank = square >> 3;
	const int file = fileOf(square);

	for (int r = rank + 1; r < 8; ++r) {
		const int sq = r * 8 + file;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}
	for (int r = rank - 1; r >= 0; --r) {
		const int sq = r * 8 + file;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}
	for (int f = file + 1; f < 8; ++f) {
		const int sq = rank * 8 + f;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}
	for (int f = file - 1; f >= 0; --f) {
		const int sq = rank * 8 + f;
		attacks |= squareMask(sq);
		if (occupied & squareMask(sq)) {
			break;
		}
	}

	return attacks;
}

TaperTerms mobilityTermsForSide(
	uint64_t ownKnights,
	uint64_t ownBishops,
	uint64_t ownRooks,
	uint64_t ownQueens,
	uint64_t ownOccupancy,
	uint64_t allOccupancy
) {
	TaperTerms terms{};

	uint64_t bb = ownKnights;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);

		const int mobility = std::popcount(knightAttacks(square) & ~ownOccupancy);
		terms.mg += mobility * MOBILITY_BONUS_MG[static_cast<size_t>(KNIGHT_IDX)];
		terms.eg += mobility * MOBILITY_BONUS_EG[static_cast<size_t>(KNIGHT_IDX)];
	}

	bb = ownBishops;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);

		const int mobility = std::popcount(bishopAttacks(square, allOccupancy) & ~ownOccupancy);
		terms.mg += mobility * MOBILITY_BONUS_MG[static_cast<size_t>(BISHOP_IDX)];
		terms.eg += mobility * MOBILITY_BONUS_EG[static_cast<size_t>(BISHOP_IDX)];
	}

	bb = ownRooks;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);

		const int mobility = std::popcount(rookAttacks(square, allOccupancy) & ~ownOccupancy);
		terms.mg += mobility * MOBILITY_BONUS_MG[static_cast<size_t>(ROOK_IDX)];
		terms.eg += mobility * MOBILITY_BONUS_EG[static_cast<size_t>(ROOK_IDX)];
	}

	bb = ownQueens;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);

		const uint64_t queenAttacks = (bishopAttacks(square, allOccupancy) | rookAttacks(square, allOccupancy)) & ~ownOccupancy;
		const int mobility = std::popcount(queenAttacks);
		terms.mg += mobility * MOBILITY_BONUS_MG[static_cast<size_t>(QUEEN_IDX)];
		terms.eg += mobility * MOBILITY_BONUS_EG[static_cast<size_t>(QUEEN_IDX)];
	}

	return terms;
}

TaperTerms rookActivityTermsForSide(Color color, uint64_t rooks, uint64_t ownPawns, uint64_t enemyPawns) {
	TaperTerms terms{};
	uint64_t bb = rooks;

	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);

		const uint64_t fileMask = MASKS.FILE_MASKS[static_cast<size_t>(fileOf(square))];
		const bool hasOwnPawn = (ownPawns & fileMask) != 0ULL;
		const bool hasEnemyPawn = (enemyPawns & fileMask) != 0ULL;

		if (!hasOwnPawn && !hasEnemyPawn) {
			terms.mg += ROOK_ACTIVITY_BONUS_MG[ROOK_ACTIVITY_OPEN_FILE];
			terms.eg += ROOK_ACTIVITY_BONUS_EG[ROOK_ACTIVITY_OPEN_FILE];
		} else if (!hasOwnPawn && hasEnemyPawn) {
			terms.mg += ROOK_ACTIVITY_BONUS_MG[ROOK_ACTIVITY_SEMI_OPEN_FILE];
			terms.eg += ROOK_ACTIVITY_BONUS_EG[ROOK_ACTIVITY_SEMI_OPEN_FILE];
		}

		const uint64_t rookSquare = squareMask(square);
		if ((color == Color::White && (rookSquare & Board::RANK_7) != 0ULL) ||
		    (color == Color::Black && (rookSquare & Board::RANK_2) != 0ULL)) {
			terms.mg += ROOK_ACTIVITY_BONUS_MG[ROOK_ACTIVITY_SEVENTH_RANK];
			terms.eg += ROOK_ACTIVITY_BONUS_EG[ROOK_ACTIVITY_SEVENTH_RANK];
		}
	}

	return terms;
}

int connectedPawnsBonusByRank(Color color, uint64_t ownPawns, const std::array<int, 9>& bonusByRank) {
	const uint64_t phalanx = ((ownPawns & ~Board::FILE_A) >> 1) | ((ownPawns & ~Board::FILE_H) << 1);
	uint64_t support = 0ULL;

	if (color == Color::White) {
		support = ((ownPawns & ~Board::FILE_A) << 7) | ((ownPawns & ~Board::FILE_H) << 9);
	} else {
		support = ((ownPawns & ~Board::FILE_A) >> 9) | ((ownPawns & ~Board::FILE_H) >> 7);
	}

	uint64_t connected = ownPawns & (phalanx | support);
	int bonus = 0;

	while (connected != 0ULL) {
		const int square = lsbIndex(connected);
		connected &= (connected - 1);

		const int rank = square >> 3;
		const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
		bonus += bonusByRank[static_cast<size_t>(std::clamp(relativeRank, 0, 8))];
	}

	return bonus;
}

int connectedPawnsBonus(Color color, uint64_t ownPawns) {
	return connectedPawnsBonusByRank(color, ownPawns, CONNECTED_PAWN_BONUS_MG_BY_RANK);
}

int connectedPawnsBonusEg(Color color, uint64_t ownPawns) {
	return connectedPawnsBonusByRank(color, ownPawns, CONNECTED_PAWN_BONUS_EG_BY_RANK);
}

uint64_t pawnAttacks(Color color, uint64_t pawns) {
	if (color == Color::White) {
		return ((pawns & ~Board::FILE_A) << 7) | ((pawns & ~Board::FILE_H) << 9);
	}
	return ((pawns & ~Board::FILE_A) >> 9) | ((pawns & ~Board::FILE_H) >> 7);
}

bool hasAdjacentPawnSupport(Color color, uint64_t ownPawns, int square) {
	const int rank = square >> 3;
	const int file = fileOf(square);

	for (int df = -1; df <= 1; df += 2) {
		const int adjFile = file + df;
		if (adjFile < 0 || adjFile > 7) {
			continue;
		}

		if (color == Color::White) {
			for (int r = 0; r <= rank; ++r) {
				if ((ownPawns & squareMask(r * 8 + adjFile)) != 0ULL) {
					return true;
				}
			}
		} else {
			for (int r = 7; r >= rank; --r) {
				if ((ownPawns & squareMask(r * 8 + adjFile)) != 0ULL) {
					return true;
				}
			}
		}
	}

	return false;
}

int candidatePawnsBonusByRank(Color color, uint64_t ownPawns, uint64_t enemyPawns, const std::array<int, 9>& bonusByRank) {
	int bonus = 0;
	uint64_t pawns = ownPawns;
	const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;

	while (pawns != 0ULL) {
		const int square = lsbIndex(pawns);
		pawns &= (pawns - 1);

		const uint64_t frontMask = MASKS.PASSED_PAWN_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(square)];
		if ((enemyPawns & frontMask) == 0ULL) {
			continue;
		}

		const uint64_t fileMask = MASKS.FILE_MASKS[static_cast<size_t>(fileOf(square))];
		const uint64_t sameFileFront = frontMask & fileMask;
		if ((enemyPawns & sameFileFront) != 0ULL) {
			continue;
		}

		const uint64_t adjacentFront = frontMask & ~fileMask;
		const int enemyAdjacent = std::popcount(enemyPawns & adjacentFront);
		if (enemyAdjacent > 1) {
			continue;
		}

		const bool hasSupport = hasAdjacentPawnSupport(color, ownPawns, square);
		if (!hasSupport && enemyAdjacent > 0) {
			continue;
		}

		const int rank = square >> 3;
		const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
		bonus += bonusByRank[static_cast<size_t>(std::clamp(relativeRank, 0, 8))];
	}

	return bonus;
}

int candidatePawnsBonus(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
	return candidatePawnsBonusByRank(color, ownPawns, enemyPawns, CANDIDATE_PAWN_BONUS_MG_BY_RANK);
}

int candidatePawnsBonusEg(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
	return candidatePawnsBonusByRank(color, ownPawns, enemyPawns, CANDIDATE_PAWN_BONUS_EG_BY_RANK);
}

int backwardPawnsPenaltyByRank(
	Color color,
	uint64_t ownPawns,
	uint64_t enemyPawns,
	uint64_t allOcc,
	const std::array<int, 9>& penaltyByRank
) {
	int penalty = 0;
	uint64_t pawns = ownPawns;
	const Color enemyColor = (color == Color::White) ? Color::Black : Color::White;
	const uint64_t enemyPawnAttackMap = pawnAttacks(enemyColor, enemyPawns);

	while (pawns != 0ULL) {
		const int square = lsbIndex(pawns);
		pawns &= (pawns - 1);

		if (hasAdjacentPawnSupport(color, ownPawns, square)) {
			continue;
		}

		const int advanceSquare = (color == Color::White) ? square + 8 : square - 8;
		if (advanceSquare < 0 || advanceSquare >= 64) {
			continue;
		}

		const uint64_t advanceMask = squareMask(advanceSquare);
		const bool blocked = (allOcc & advanceMask) != 0ULL;
		const bool pressured = (enemyPawnAttackMap & advanceMask) != 0ULL;
		if (!blocked && !pressured) {
			continue;
		}

		const int rank = square >> 3;
		const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
		penalty += penaltyByRank[static_cast<size_t>(std::clamp(relativeRank, 0, 8))];
	}

	return penalty;
}

int backwardPawnsPenalty(Color color, uint64_t ownPawns, uint64_t enemyPawns, uint64_t allOcc) {
	return backwardPawnsPenaltyByRank(color, ownPawns, enemyPawns, allOcc, BACKWARD_PAWN_PENALTY_MG_BY_RANK);
}

int backwardPawnsPenaltyEg(Color color, uint64_t ownPawns, uint64_t enemyPawns, uint64_t allOcc) {
	return backwardPawnsPenaltyByRank(color, ownPawns, enemyPawns, allOcc, BACKWARD_PAWN_PENALTY_EG_BY_RANK);
}

int pawnIslands(uint64_t ownPawns) {
	unsigned int occupiedFiles = 0;
	for (int file = 0; file < 8; ++file) {
		if ((ownPawns & MASKS.FILE_MASKS[static_cast<size_t>(file)]) != 0ULL) {
			occupiedFiles |= (1U << file);
		}
	}

	int islands = 0;
	bool inIsland = false;
	for (int file = 0; file < 8; ++file) {
		const bool hasPawn = (occupiedFiles & (1U << file)) != 0U;
		if (hasPawn && !inIsland) {
			++islands;
		}
		inIsland = hasPawn;
	}

	return islands;
}

TaperTerms pawnIslandPenalty(uint64_t ownPawns) {
	const int islands = pawnIslands(ownPawns);
	const int extraIslands = std::max(0, islands - 1);
	return {
		extraIslands * PAWN_ISLAND_PENALTY_MG,
		extraIslands * PAWN_ISLAND_PENALTY_EG
	};
}

int kingAttackPressure(
	Color color,
	int kingSquare,
	uint64_t enemyKnights,
	uint64_t enemyBishops,
	uint64_t enemyRooks,
	uint64_t enemyQueens,
	uint64_t allOcc
) {
	const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;
	const uint64_t kingZone = MASKS.KING_SHIELD_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(kingSquare)];

	if (kingZone == 0ULL) {
		return 0;
	}

	int attackers = 0;

	uint64_t bb = enemyKnights;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);
		if ((knightAttacks(square) & kingZone) != 0ULL) {
			++attackers;
		}
	}

	bb = enemyBishops;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);
		if ((bishopAttacks(square, allOcc) & kingZone) != 0ULL) {
			++attackers;
		}
	}

	bb = enemyRooks;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);
		if ((rookAttacks(square, allOcc) & kingZone) != 0ULL) {
			++attackers;
		}
	}

	bb = enemyQueens;
	while (bb != 0ULL) {
		const int square = lsbIndex(bb);
		bb &= (bb - 1);
		const uint64_t attacks = bishopAttacks(square, allOcc) | rookAttacks(square, allOcc);
		if ((attacks & kingZone) != 0ULL) {
			++attackers;
		}
	}

	const size_t idx = std::min(static_cast<size_t>(attackers), KING_ATTACK_PRESSURE_PENALTY.size() - 1);
	return KING_ATTACK_PRESSURE_PENALTY[idx];
}

int kingPawnShieldBonus(Color color, int kingSquare, uint64_t ownPawns) {
	const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;
	const uint64_t shield = MASKS.KING_SHIELD_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(kingSquare)] & ownPawns;
	const int count = std::popcount(shield);
	return std::min(count, KING_SHIELD_MAX_PAWNS) * KING_SHIELD_PER_PAWN_BONUS;
}

int endgameMaterialValue(uint64_t pawns, uint64_t knights, uint64_t bishops, uint64_t rooks, uint64_t queens) {
	return std::popcount(pawns) * EG_VALUE[PAWN_IDX]
		+ std::popcount(knights) * EG_VALUE[KNIGHT_IDX]
		+ std::popcount(bishops) * EG_VALUE[BISHOP_IDX]
		+ std::popcount(rooks) * EG_VALUE[ROOK_IDX]
		+ std::popcount(queens) * EG_VALUE[QUEEN_IDX];
}

bool hasInsufficientMaterialDraw(
	uint64_t whitePawns,
	uint64_t blackPawns,
	uint64_t whiteKnights,
	uint64_t blackKnights,
	uint64_t whiteBishops,
	uint64_t blackBishops,
	uint64_t whiteRooks,
	uint64_t blackRooks,
	uint64_t whiteQueens,
	uint64_t blackQueens
) {
	if ((whitePawns | blackPawns | whiteRooks | blackRooks | whiteQueens | blackQueens) != 0ULL) {
		return false;
	}

	const int whiteMinors = std::popcount(whiteKnights | whiteBishops);
	const int blackMinors = std::popcount(blackKnights | blackBishops);

	// Preserve the established dead-position recognition set: KvK, K+minor vs K, K+minor vs K+minor.
	return whiteMinors < 2 && blackMinors < 2;
}

bool areOppositeColoredSingleBishops(uint64_t whiteBishops, uint64_t blackBishops) {
	if (std::popcount(whiteBishops) != 1 || std::popcount(blackBishops) != 1) {
		return false;
	}

	const int whiteSquare = lsbIndex(whiteBishops);
	const int blackSquare = lsbIndex(blackBishops);
	const int whiteColor = ((whiteSquare >> 3) + fileOf(whiteSquare)) & 1;
	const int blackColor = ((blackSquare >> 3) + fileOf(blackSquare)) & 1;
	return whiteColor != blackColor;
}

int lowMaterialScaleFactor(
	int phase,
	uint64_t whitePawns,
	uint64_t blackPawns,
	uint64_t whiteKnights,
	uint64_t blackKnights,
	uint64_t whiteBishops,
	uint64_t blackBishops,
	uint64_t whiteRooks,
	uint64_t blackRooks,
	uint64_t whiteQueens,
	uint64_t blackQueens
) {
	if (phase > LATE_ENDGAME_PHASE_MAX) {
		return TAPER_SCALE;
	}

	int scale = TAPER_SCALE;

	const int whitePawnsCount = std::popcount(whitePawns);
	const int blackPawnsCount = std::popcount(blackPawns);
	const int totalPawns = whitePawnsCount + blackPawnsCount;

	const int whiteKnightsCount = std::popcount(whiteKnights);
	const int blackKnightsCount = std::popcount(blackKnights);
	const int whiteBishopsCount = std::popcount(whiteBishops);
	const int blackBishopsCount = std::popcount(blackBishops);

	const int whiteMajors = std::popcount(whiteRooks | whiteQueens);
	const int blackMajors = std::popcount(blackRooks | blackQueens);

	if (whiteMajors == 0 && blackMajors == 0) {
		const int whiteMinors = whiteKnightsCount + whiteBishopsCount;
		const int blackMinors = blackKnightsCount + blackBishopsCount;
		const int minorDiff = std::abs(whiteMinors - blackMinors);

		if (totalPawns == 0) {
			if (minorDiff <= 1) {
				scale = std::min(scale, SCALE_MINOR_ONLY_NEAR_EQUAL);
			} else if (minorDiff == 2) {
				scale = std::min(scale, SCALE_MINOR_ONLY_CLEAR_EDGE);
			}
		}

		const bool bishopOnlyEndgame = (whiteKnightsCount == 0 && blackKnightsCount == 0
			&& whiteBishopsCount == 1 && blackBishopsCount == 1);

		if (bishopOnlyEndgame
			&& totalPawns <= 6
			&& areOppositeColoredSingleBishops(whiteBishops, blackBishops)) {
			scale = std::min(scale, (totalPawns <= 2) ? SCALE_OPPOSITE_BISHOPS_MIN_PAWNS : SCALE_OPPOSITE_BISHOPS_LOW_PAWNS);
		}
	}

	return scale;
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
			penalty += (count - 1) * PAWN_STRUCTURE_DOUBLED_PENALTY;
		}

		uint64_t adjMask = 0ULL;
		if (file > 0) {
			adjMask |= MASKS.FILE_MASKS[static_cast<size_t>(file - 1)];
		}
		if (file < 7) {
			adjMask |= MASKS.FILE_MASKS[static_cast<size_t>(file + 1)];
		}

		if ((ownPawns & adjMask) == 0ULL) {
			penalty += count * PAWN_STRUCTURE_ISOLATED_PENALTY;
		}
	}

	return penalty;
}

int mopUpEval(int winningKingSq, int losingKingSq) {
	const int losingFile = fileOf(losingKingSq);
	const int losingRank = losingKingSq >> 3;
	const int centerDistance = std::abs(losingFile - 3) + std::abs(losingRank - 3);
	const int edgeDistance = std::min({losingFile, 7 - losingFile, losingRank, 7 - losingRank});
	const int cornerDistance = std::min({
		losingFile + losingRank,
		losingFile + (7 - losingRank),
		(7 - losingFile) + losingRank,
		(7 - losingFile) + (7 - losingRank)
	});

	const int winningFile = fileOf(winningKingSq);
	const int winningRank = winningKingSq >> 3;
	const int kingDistance = std::abs(winningFile - losingFile) + std::abs(winningRank - losingRank);

	const int edgePressure = (MOP_UP_EDGE_DISTANCE_BASE - edgeDistance) * MOP_UP_EDGE_PRESSURE_WEIGHT;
	const int cornerPressure = (MOP_UP_CORNER_DISTANCE_CAP - std::min(cornerDistance, MOP_UP_CORNER_DISTANCE_CAP)) * MOP_UP_CORNER_PRESSURE_WEIGHT;

	return centerDistance * MOP_UP_CENTER_DISTANCE_WEIGHT
		+ edgePressure
		+ cornerPressure
		+ (MOP_UP_KING_DISTANCE_BASE - kingDistance) * MOP_UP_KING_DISTANCE_WEIGHT;
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
			int pawnBonus = (relativeRank * relativeRank) * PASSED_PAWN_RANK_SQUARE_MULTIPLIER;
			uint64_t forwardSq = (color == Color::White) ? (1ULL << (sq + 8)) : (1ULL << (sq - 8));
			uint64_t oppOcc = enemyPawns;

			if (forwardSq & oppOcc) {
				pawnBonus /= PASSED_PAWN_BLOCKED_DIVISOR;
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
		return (rookInCorner && kingTrapsRook) ? TRAPPED_ROOK_PENALTY : 0;
	}

	const bool rookInCorner = (rooks & ((1ULL << 56) | (1ULL << 63))) != 0ULL;
	const bool kingTrapsRook = (king & ((1ULL << 57) | (1ULL << 58) | (1ULL << 61) | (1ULL << 62))) != 0ULL;
	return (rookInCorner && kingTrapsRook) ? TRAPPED_ROOK_PENALTY : 0;
}

int badBishopPenalty(uint64_t ownPawns, uint64_t ownBishops, Color color) {
	int penalty = 0;
	if (color == Color::White) {
		if ((ownBishops & 512ULL) && (ownPawns & 262144ULL)) penalty += BAD_BISHOP_HEAVY_PENALTY;
		if ((ownBishops & 16384ULL) && (ownPawns & 2097152ULL)) penalty += BAD_BISHOP_HEAVY_PENALTY;
		if ((ownBishops & 4ULL) && (ownPawns & 2048ULL)) penalty += BAD_BISHOP_LIGHT_PENALTY;
		if ((ownBishops & 32ULL) && (ownPawns & 4096ULL)) penalty += BAD_BISHOP_LIGHT_PENALTY;
	} else {
		if ((ownBishops & 562949953421312ULL) && (ownPawns & 4398046511104ULL)) penalty += BAD_BISHOP_HEAVY_PENALTY;
		if ((ownBishops & 18014398509481984ULL) && (ownPawns & 35184372088832ULL)) penalty += BAD_BISHOP_HEAVY_PENALTY;
		if ((ownBishops & 288230376151711744ULL) && (ownPawns & 2251799813685248ULL)) penalty += BAD_BISHOP_LIGHT_PENALTY;
		if ((ownBishops & 2305843009213693952ULL) && (ownPawns & 4503599627370496ULL)) penalty += BAD_BISHOP_LIGHT_PENALTY;
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
			penalty += EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY * (undevelopedMinors - 1);
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
		penalty += UNCASTLED_KING_CENTER_PENALTY;

		if (color == Color::White && !(castlingRights & (CASTLE_WK | CASTLE_WQ))) {
			penalty += UNCASTLED_KING_LOST_RIGHTS_PENALTY;
		} else if (color == Color::Black && !(castlingRights & (CASTLE_BK | CASTLE_BQ))) {
			penalty += UNCASTLED_KING_LOST_RIGHTS_PENALTY;
		}
	}
	return penalty;
}

PieceScoreDelta pieceScoreDelta(Color color, PieceType piece, int square) {
	const size_t pieceIdx = static_cast<size_t>(piece);
	const int idx = pestoSquare(color, square);
	return {
		MG_VALUE[pieceIdx] + MG_PESTO[pieceIdx][static_cast<size_t>(idx)],
		EG_VALUE[pieceIdx] + EG_PESTO[pieceIdx][static_cast<size_t>(idx)],
		GAME_PHASE_INC[pieceIdx]
	};
}

} // namespace

void Board::addPieceEval(Color color, PieceType piece, int square) {
	const int c = static_cast<int>(color);
	const PieceScoreDelta delta = pieceScoreDelta(color, piece, square);

	m_mgScore[c] += delta.mg;
	m_egScore[c] += delta.eg;
	m_gamePhase += delta.phase;
}

void Board::removePieceEval(Color color, PieceType piece, int square) {
	const int c = static_cast<int>(color);
	const PieceScoreDelta delta = pieceScoreDelta(color, piece, square);

	m_mgScore[c] -= delta.mg;
	m_egScore[c] -= delta.eg;
	m_gamePhase -= delta.phase;
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

	int mg = m_mgScore[WHITE_IDX] - m_mgScore[BLACK_IDX];
	int eg = m_egScore[WHITE_IDX] - m_egScore[BLACK_IDX];

	return applyBonusTermsAndTaper(mg, eg, m_gamePhase, noWhitePawns, noBlackPawns);
}

int Board::evaluateSideToMove() const {
	const int eval = evaluate();
	return (m_sideToMove == Color::White) ? eval : -eval;
}

int Board::computeStaticEvaluation() const {
	bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
	bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);

	std::array<int, 2> mgScore = {0, 0};
	std::array<int, 2> egScore = {0, 0};
	int phase = 0;

	for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
		for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
			uint64_t bb = m_bitboards[color][piece];
			while (bb != 0ULL) {
				const int square = lsbIndex(bb);
				bb &= (bb - 1);

				const PieceScoreDelta delta = pieceScoreDelta(
					static_cast<Color>(color),
					static_cast<PieceType>(piece),
					square
				);
				mgScore[color] += delta.mg;
				egScore[color] += delta.eg;
				phase += delta.phase;
			}
		}
	}

	int mg = mgScore[WHITE_IDX] - mgScore[BLACK_IDX];
	int eg = egScore[WHITE_IDX] - egScore[BLACK_IDX];

	return applyBonusTermsAndTaper(mg, eg, phase, noWhitePawns, noBlackPawns);
}

#if !defined(NDEBUG) || defined(EVAL_TUNING_DIAGNOSTICS)
void Board::printEvalBreakdown() const {
	bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
	bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);

	int mg = m_mgScore[WHITE_IDX] - m_mgScore[BLACK_IDX];
	int eg = m_egScore[WHITE_IDX] - m_egScore[BLACK_IDX];
	const int phase = m_gamePhase;

	const uint64_t whitePawns = m_bitboards[WHITE_IDX][PAWN_IDX];
	const uint64_t blackPawns = m_bitboards[BLACK_IDX][PAWN_IDX];
	const uint64_t whiteKnights = m_bitboards[WHITE_IDX][KNIGHT_IDX];
	const uint64_t blackKnights = m_bitboards[BLACK_IDX][KNIGHT_IDX];
	const uint64_t whiteBishops = m_bitboards[WHITE_IDX][BISHOP_IDX];
	const uint64_t blackBishops = m_bitboards[BLACK_IDX][BISHOP_IDX];
	const uint64_t whiteRooks = m_bitboards[WHITE_IDX][ROOK_IDX];
	const uint64_t blackRooks = m_bitboards[BLACK_IDX][ROOK_IDX];
	const uint64_t whiteQueens = m_bitboards[WHITE_IDX][QUEEN_IDX];
	const uint64_t blackQueens = m_bitboards[BLACK_IDX][QUEEN_IDX];

	std::cout << "=== Eval Breakdown (white-relative) ===\n";
	std::cout << "base.mg=" << mg << " base.eg=" << eg << " phase=" << phase << "/24\n";

	if (hasInsufficientMaterialDraw(
		whitePawns,
		blackPawns,
		whiteKnights,
		blackKnights,
		whiteBishops,
		blackBishops,
		whiteRooks,
		blackRooks,
		whiteQueens,
		blackQueens
	)) {
		std::cout << "insufficient_material_draw=1 final=0\n";
		return;
	}

	auto printTerm = [](const char* label, int mgDelta, int egDelta) {
		std::cout << label << ": mg=" << mgDelta << " eg=" << egDelta << "\n";
	};

	const int whiteBishopsCount = std::popcount(whiteBishops);
	const int blackBishopsCount = std::popcount(blackBishops);
	int bishopPairMg = 0;
	int bishopPairEg = 0;
	if (whiteBishopsCount >= 2) {
		bishopPairMg += BISHOP_PAIR_BONUS_MG;
		bishopPairEg += BISHOP_PAIR_BONUS_EG;
	}
	if (blackBishopsCount >= 2) {
		bishopPairMg -= BISHOP_PAIR_BONUS_MG;
		bishopPairEg -= BISHOP_PAIR_BONUS_EG;
	}
	mg += bishopPairMg;
	eg += bishopPairEg;
	printTerm("bishop_pair", bishopPairMg, bishopPairEg);

	const uint64_t whiteOcc = occupancy(Color::White);
	const uint64_t blackOcc = occupancy(Color::Black);
	const uint64_t allOcc = whiteOcc | blackOcc;
	const int clampedPhase = std::clamp(phase, 0, 24);

	const TaperTerms whiteMobility = mobilityTermsForSide(
		whiteKnights,
		whiteBishops,
		whiteRooks,
		whiteQueens,
		whiteOcc,
		allOcc
	);
	const TaperTerms blackMobility = mobilityTermsForSide(
		blackKnights,
		blackBishops,
		blackRooks,
		blackQueens,
		blackOcc,
		allOcc
	);
	const int mobilityMg = whiteMobility.mg - blackMobility.mg;
	const int mobilityEg = whiteMobility.eg - blackMobility.eg;
	mg += mobilityMg;
	eg += mobilityEg;
	printTerm("mobility", mobilityMg, mobilityEg);

	const TaperTerms whiteRookActivity = rookActivityTermsForSide(
		Color::White,
		whiteRooks,
		whitePawns,
		blackPawns
	);
	const TaperTerms blackRookActivity = rookActivityTermsForSide(
		Color::Black,
		blackRooks,
		blackPawns,
		whitePawns
	);
	const int rookActivityMg = whiteRookActivity.mg - blackRookActivity.mg;
	const int rookActivityEg = whiteRookActivity.eg - blackRookActivity.eg;
	mg += rookActivityMg;
	eg += rookActivityEg;
	printTerm("rook_activity", rookActivityMg, rookActivityEg);

	const int whiteKingSquare = lsbIndex(m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackKingSquare = lsbIndex(m_bitboards[BLACK_IDX][KING_IDX]);

	const int whiteConnectedMg = connectedPawnsBonus(Color::White, whitePawns);
	const int blackConnectedMg = connectedPawnsBonus(Color::Black, blackPawns);
	const int whiteConnectedEg = connectedPawnsBonusEg(Color::White, whitePawns);
	const int blackConnectedEg = connectedPawnsBonusEg(Color::Black, blackPawns);
	const int connectedMg = whiteConnectedMg - blackConnectedMg;
	const int connectedEg = whiteConnectedEg - blackConnectedEg;
	mg += connectedMg;
	eg += connectedEg;
	printTerm("connected_pawns", connectedMg, connectedEg);

	const int whiteCandidateMg = candidatePawnsBonus(Color::White, whitePawns, blackPawns);
	const int blackCandidateMg = candidatePawnsBonus(Color::Black, blackPawns, whitePawns);
	const int whiteCandidateEg = candidatePawnsBonusEg(Color::White, whitePawns, blackPawns);
	const int blackCandidateEg = candidatePawnsBonusEg(Color::Black, blackPawns, whitePawns);
	const int candidateMg = whiteCandidateMg - blackCandidateMg;
	const int candidateEg = whiteCandidateEg - blackCandidateEg;
	mg += candidateMg;
	eg += candidateEg;
	printTerm("candidate_pawns", candidateMg, candidateEg);

	const int whiteKingPressure = kingAttackPressure(
		Color::White,
		whiteKingSquare,
		blackKnights,
		blackBishops,
		blackRooks,
		blackQueens,
		allOcc
	);
	const int blackKingPressure = kingAttackPressure(
		Color::Black,
		blackKingSquare,
		whiteKnights,
		whiteBishops,
		whiteRooks,
		whiteQueens,
		allOcc
	);
	const int kingPressureMg = -(whiteKingPressure - blackKingPressure);
	mg += kingPressureMg;
	printTerm("king_attack_pressure", kingPressureMg, 0);

	const int kingShieldMg = kingPawnShieldBonus(Color::White, whiteKingSquare, whitePawns)
		- kingPawnShieldBonus(Color::Black, blackKingSquare, blackPawns);
	mg += kingShieldMg;
	printTerm("king_shield", kingShieldMg, 0);

	const int whitePawnPenalty = pawnStructurePenalty(whitePawns);
	const int blackPawnPenalty = pawnStructurePenalty(blackPawns);
	const int pawnStructureMg = -whitePawnPenalty + blackPawnPenalty;
	const int pawnStructureEg = -whitePawnPenalty + blackPawnPenalty;
	mg += pawnStructureMg;
	eg += pawnStructureEg;
	printTerm("pawn_structure", pawnStructureMg, pawnStructureEg);

	const int whiteBackwardMg = backwardPawnsPenalty(Color::White, whitePawns, blackPawns, allOcc);
	const int blackBackwardMg = backwardPawnsPenalty(Color::Black, blackPawns, whitePawns, allOcc);
	const int whiteBackwardEg = backwardPawnsPenaltyEg(Color::White, whitePawns, blackPawns, allOcc);
	const int blackBackwardEg = backwardPawnsPenaltyEg(Color::Black, blackPawns, whitePawns, allOcc);
	const int backwardMg = -whiteBackwardMg + blackBackwardMg;
	const int backwardEg = -whiteBackwardEg + blackBackwardEg;
	mg += backwardMg;
	eg += backwardEg;
	printTerm("backward_pawns", backwardMg, backwardEg);

	const TaperTerms whiteIslands = pawnIslandPenalty(whitePawns);
	const TaperTerms blackIslands = pawnIslandPenalty(blackPawns);
	const int islandsMg = -whiteIslands.mg + blackIslands.mg;
	const int islandsEg = -whiteIslands.eg + blackIslands.eg;
	mg += islandsMg;
	eg += islandsEg;
	printTerm("pawn_islands", islandsMg, islandsEg);

	const int passedWhite = passedPawnCount(Color::White, whitePawns, blackPawns);
	const int passedBlack = passedPawnCount(Color::Black, blackPawns, whitePawns);
	const int passedCountDelta = passedWhite - passedBlack;
	const int passedCountMg = PASSED_PAWN_COUNT_BONUS_MG * passedCountDelta;
	const int passedCountEg = PASSED_PAWN_COUNT_BONUS_EG * passedCountDelta;
	mg += passedCountMg;
	eg += passedCountEg;
	printTerm("passed_count", passedCountMg, passedCountEg);

	const int whitePassed = passedPawnBonus(Color::White, whitePawns, blackPawns);
	const int blackPassed = passedPawnBonus(Color::Black, blackPawns, whitePawns);
	const int passedPathMg = whitePassed - blackPassed;
	const int passedPathEg = PASSED_PAWN_EG_MULTIPLIER * (whitePassed - blackPassed);
	mg += passedPathMg;
	eg += passedPathEg;
	printTerm("passed_advancement", passedPathMg, passedPathEg);

	const int whiteTrapped = trappedRookPenalty(Color::White, whiteRooks, m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackTrapped = trappedRookPenalty(Color::Black, blackRooks, m_bitboards[BLACK_IDX][KING_IDX]);
	const int trappedRookMg = -whiteTrapped + blackTrapped;
	mg += trappedRookMg;
	printTerm("trapped_rook", trappedRookMg, 0);

	const int whiteBadBishop = badBishopPenalty(whitePawns, whiteBishops, Color::White);
	const int blackBadBishop = badBishopPenalty(blackPawns, blackBishops, Color::Black);
	const int whiteEarlyQueen = earlyQueenPenalty(whiteQueens, whiteKnights, whiteBishops, Color::White);
	const int blackEarlyQueen = earlyQueenPenalty(blackQueens, blackKnights, blackBishops, Color::Black);
	const int developmentMg = -(whiteBadBishop + whiteEarlyQueen) + (blackBadBishop + blackEarlyQueen);
	const int developmentEg = -whiteBadBishop + blackBadBishop;
	mg += developmentMg;
	eg += developmentEg;
	printTerm("development", developmentMg, developmentEg);

	const int whiteUncastled = uncastledKingPenalty(m_bitboards[WHITE_IDX][KING_IDX], m_castlingRights, Color::White);
	const int blackUncastled = uncastledKingPenalty(m_bitboards[BLACK_IDX][KING_IDX], m_castlingRights, Color::Black);
	const int uncastledMg = -whiteUncastled + blackUncastled;
	mg += uncastledMg;
	printTerm("uncastled", uncastledMg, 0);

	int mopUpEg = 0;
	const int whiteMaterial = endgameMaterialValue(whitePawns, whiteKnights, whiteBishops, whiteRooks, whiteQueens);
	const int blackMaterial = endgameMaterialValue(blackPawns, blackKnights, blackBishops, blackRooks, blackQueens);
	const int materialAdvantage = whiteMaterial - blackMaterial;

	if (clampedPhase <= LATE_ENDGAME_PHASE_MAX) {
		if (eg > MOP_UP_EG_MARGIN && materialAdvantage >= MOP_UP_MATERIAL_MARGIN) {
			mopUpEg = mopUpEval(whiteKingSquare, blackKingSquare);
		} else if (eg < -MOP_UP_EG_MARGIN && materialAdvantage <= -MOP_UP_MATERIAL_MARGIN) {
			mopUpEg = -mopUpEval(blackKingSquare, whiteKingSquare);
		}
	}
	eg += mopUpEg;
	printTerm("mop_up", 0, mopUpEg);

	const int tapered = (mg * clampedPhase + eg * (24 - clampedPhase)) / 24;
	std::cout << "tapered_score=" << tapered << "\n";

	int noPawnScaled = tapered;
	if (noPawnScaled > 0 && noWhitePawns) {
		noPawnScaled /= 2;
	} else if (noPawnScaled < 0 && noBlackPawns) {
		noPawnScaled /= 2;
	}
	std::cout << "no_pawn_scaled_score=" << noPawnScaled << "\n";

	const int lowMaterialScale = lowMaterialScaleFactor(
		clampedPhase,
		whitePawns,
		blackPawns,
		whiteKnights,
		blackKnights,
		whiteBishops,
		blackBishops,
		whiteRooks,
		blackRooks,
		whiteQueens,
		blackQueens
	);

	const int finalScore = (noPawnScaled * lowMaterialScale) / TAPER_SCALE;
	std::cout << "low_material_scale=" << lowMaterialScale << "/" << TAPER_SCALE << "\n";
	std::cout << "final_score=" << finalScore << "\n";
}
#endif

[[nodiscard]] int Board::applyBonusTermsAndTaper(int mg, int eg, int phase, bool noWhitePawns, bool noBlackPawns) const {
	const uint64_t whitePawns = m_bitboards[WHITE_IDX][PAWN_IDX];
	const uint64_t blackPawns = m_bitboards[BLACK_IDX][PAWN_IDX];
	const uint64_t whiteKnights = m_bitboards[WHITE_IDX][KNIGHT_IDX];
	const uint64_t blackKnights = m_bitboards[BLACK_IDX][KNIGHT_IDX];
	const uint64_t whiteBishops = m_bitboards[WHITE_IDX][BISHOP_IDX];
	const uint64_t blackBishops = m_bitboards[BLACK_IDX][BISHOP_IDX];
	const uint64_t whiteRooks = m_bitboards[WHITE_IDX][ROOK_IDX];
	const uint64_t blackRooks = m_bitboards[BLACK_IDX][ROOK_IDX];
	const uint64_t whiteQueens = m_bitboards[WHITE_IDX][QUEEN_IDX];
	const uint64_t blackQueens = m_bitboards[BLACK_IDX][QUEEN_IDX];

	if (hasInsufficientMaterialDraw(
		whitePawns,
		blackPawns,
		whiteKnights,
		blackKnights,
		whiteBishops,
		blackBishops,
		whiteRooks,
		blackRooks,
		whiteQueens,
		blackQueens
	)) {
		return 0;
	}

	const int whiteBishopsCount = std::popcount(whiteBishops);
	const int blackBishopsCount = std::popcount(blackBishops);

	if (whiteBishopsCount >= 2) {
		mg += BISHOP_PAIR_BONUS_MG;
		eg += BISHOP_PAIR_BONUS_EG;
	}
	if (blackBishopsCount >= 2) {
		mg -= BISHOP_PAIR_BONUS_MG;
		eg -= BISHOP_PAIR_BONUS_EG;
	}

	const uint64_t whiteOcc = occupancy(Color::White);
	const uint64_t blackOcc = occupancy(Color::Black);
	const uint64_t allOcc = whiteOcc | blackOcc;
	const int clampedPhase = std::clamp(phase, 0, 24);

	const TaperTerms whiteMobility = mobilityTermsForSide(
		whiteKnights,
		whiteBishops,
		whiteRooks,
		whiteQueens,
		whiteOcc,
		allOcc
	);
	const TaperTerms blackMobility = mobilityTermsForSide(
		blackKnights,
		blackBishops,
		blackRooks,
		blackQueens,
		blackOcc,
		allOcc
	);

	mg += whiteMobility.mg - blackMobility.mg;
	eg += whiteMobility.eg - blackMobility.eg;

	const TaperTerms whiteRookActivity = rookActivityTermsForSide(
		Color::White,
		whiteRooks,
		whitePawns,
		blackPawns
	);
	const TaperTerms blackRookActivity = rookActivityTermsForSide(
		Color::Black,
		blackRooks,
		blackPawns,
		whitePawns
	);

	mg += whiteRookActivity.mg - blackRookActivity.mg;
	eg += whiteRookActivity.eg - blackRookActivity.eg;

	const int whiteKingSquare = lsbIndex(m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackKingSquare = lsbIndex(m_bitboards[BLACK_IDX][KING_IDX]);

	const int whiteConnectedMg = connectedPawnsBonus(Color::White, whitePawns);
	const int blackConnectedMg = connectedPawnsBonus(Color::Black, blackPawns);
	const int whiteConnectedEg = connectedPawnsBonusEg(Color::White, whitePawns);
	const int blackConnectedEg = connectedPawnsBonusEg(Color::Black, blackPawns);

	mg += whiteConnectedMg - blackConnectedMg;
	eg += whiteConnectedEg - blackConnectedEg;

	const int whiteCandidateMg = candidatePawnsBonus(Color::White, whitePawns, blackPawns);
	const int blackCandidateMg = candidatePawnsBonus(Color::Black, blackPawns, whitePawns);
	const int whiteCandidateEg = candidatePawnsBonusEg(Color::White, whitePawns, blackPawns);
	const int blackCandidateEg = candidatePawnsBonusEg(Color::Black, blackPawns, whitePawns);

	mg += whiteCandidateMg - blackCandidateMg;
	eg += whiteCandidateEg - blackCandidateEg;

	const int whiteKingPressure = kingAttackPressure(
		Color::White,
		whiteKingSquare,
		blackKnights,
		blackBishops,
		blackRooks,
		blackQueens,
		allOcc
	);
	const int blackKingPressure = kingAttackPressure(
		Color::Black,
		blackKingSquare,
		whiteKnights,
		whiteBishops,
		whiteRooks,
		whiteQueens,
		allOcc
	);

	mg -= (whiteKingPressure - blackKingPressure);

	mg += kingPawnShieldBonus(Color::White, whiteKingSquare, whitePawns);
	mg -= kingPawnShieldBonus(Color::Black, blackKingSquare, blackPawns);

	const int whitePawnPenalty = pawnStructurePenalty(whitePawns);
	const int blackPawnPenalty = pawnStructurePenalty(blackPawns);
	mg -= whitePawnPenalty;
	eg -= whitePawnPenalty;
	mg += blackPawnPenalty;
	eg += blackPawnPenalty;

	const int whiteBackwardMg = backwardPawnsPenalty(Color::White, whitePawns, blackPawns, allOcc);
	const int blackBackwardMg = backwardPawnsPenalty(Color::Black, blackPawns, whitePawns, allOcc);
	const int whiteBackwardEg = backwardPawnsPenaltyEg(Color::White, whitePawns, blackPawns, allOcc);
	const int blackBackwardEg = backwardPawnsPenaltyEg(Color::Black, blackPawns, whitePawns, allOcc);
	mg -= whiteBackwardMg;
	mg += blackBackwardMg;
	eg -= whiteBackwardEg;
	eg += blackBackwardEg;

	const TaperTerms whiteIslands = pawnIslandPenalty(whitePawns);
	const TaperTerms blackIslands = pawnIslandPenalty(blackPawns);
	mg -= whiteIslands.mg;
	mg += blackIslands.mg;
	eg -= whiteIslands.eg;
	eg += blackIslands.eg;

	const int passedWhite = passedPawnCount(Color::White, whitePawns, blackPawns);
	const int passedBlack = passedPawnCount(Color::Black, blackPawns, whitePawns);
	mg += PASSED_PAWN_COUNT_BONUS_MG * (passedWhite - passedBlack);
	eg += PASSED_PAWN_COUNT_BONUS_EG * (passedWhite - passedBlack);

	const int whitePassed = passedPawnBonus(Color::White, whitePawns, blackPawns);
	const int blackPassed = passedPawnBonus(Color::Black, blackPawns, whitePawns);
	mg += whitePassed;
	mg -= blackPassed;
	eg += PASSED_PAWN_EG_MULTIPLIER * whitePassed;
	eg -= PASSED_PAWN_EG_MULTIPLIER * blackPassed;

	const int whiteTrapped = trappedRookPenalty(Color::White, whiteRooks, m_bitboards[WHITE_IDX][KING_IDX]);
	const int blackTrapped = trappedRookPenalty(Color::Black, blackRooks, m_bitboards[BLACK_IDX][KING_IDX]);
	mg -= whiteTrapped;
	mg += blackTrapped;

	const int whiteBadBishop = badBishopPenalty(whitePawns, whiteBishops, Color::White);
	const int blackBadBishop = badBishopPenalty(blackPawns, blackBishops, Color::Black);
	const int whiteEarlyQueen = earlyQueenPenalty(
		whiteQueens,
		whiteKnights,
		whiteBishops,
		Color::White
	);
	const int blackEarlyQueen = earlyQueenPenalty(
		blackQueens,
		blackKnights,
		blackBishops,
		Color::Black
	);

	mg -= (whiteBadBishop + whiteEarlyQueen);
	mg += (blackBadBishop + blackEarlyQueen);
	eg -= whiteBadBishop;
	eg += blackBadBishop;

	const int whiteUncastled = uncastledKingPenalty(m_bitboards[WHITE_IDX][KING_IDX], m_castlingRights, Color::White);
	const int blackUncastled = uncastledKingPenalty(m_bitboards[BLACK_IDX][KING_IDX], m_castlingRights, Color::Black);
	mg -= whiteUncastled;
	mg += blackUncastled;

	const int whiteMaterial = endgameMaterialValue(whitePawns, whiteKnights, whiteBishops, whiteRooks, whiteQueens);
	const int blackMaterial = endgameMaterialValue(blackPawns, blackKnights, blackBishops, blackRooks, blackQueens);
	const int materialAdvantage = whiteMaterial - blackMaterial;

	if (clampedPhase <= LATE_ENDGAME_PHASE_MAX) {
		if (eg > MOP_UP_EG_MARGIN && materialAdvantage >= MOP_UP_MATERIAL_MARGIN) {
			eg += mopUpEval(whiteKingSquare, blackKingSquare);
		} else if (eg < -MOP_UP_EG_MARGIN && materialAdvantage <= -MOP_UP_MATERIAL_MARGIN) {
			eg -= mopUpEval(blackKingSquare, whiteKingSquare);
		}
	}

	int score = (mg * clampedPhase + eg * (24 - clampedPhase)) / 24;

	if (score > 0 && noWhitePawns) {
		score /= 2;
	} else if (score < 0 && noBlackPawns) {
		score /= 2;
	}

	const int lowMaterialScale = lowMaterialScaleFactor(
		clampedPhase,
		whitePawns,
		blackPawns,
		whiteKnights,
		blackKnights,
		whiteBishops,
		blackBishops,
		whiteRooks,
		blackRooks,
		whiteQueens,
		blackQueens
	);

	score = (score * lowMaterialScale) / TAPER_SCALE;

	return score;
}
