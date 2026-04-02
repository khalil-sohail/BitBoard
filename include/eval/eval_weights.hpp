#ifndef EVAL_WEIGHTS_HPP
#define EVAL_WEIGHTS_HPP

#include "board.hpp"

#include <array>
#include <cstddef>

namespace EvalWeights {

extern const std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE;
extern const std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE;
extern const std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC;
extern const std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_MG;
extern const std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_EG;

extern const size_t ROOK_ACTIVITY_OPEN_FILE;
extern const size_t ROOK_ACTIVITY_SEMI_OPEN_FILE;
extern const size_t ROOK_ACTIVITY_SEVENTH_RANK;

extern const std::array<int, 3> ROOK_ACTIVITY_BONUS_MG;
extern const std::array<int, 3> ROOK_ACTIVITY_BONUS_EG;

extern const std::array<int, 9> CONNECTED_PAWN_BONUS_MG_BY_RANK;
extern const std::array<int, 9> CONNECTED_PAWN_BONUS_EG_BY_RANK;

extern const std::array<int, 9> CANDIDATE_PAWN_BONUS_MG_BY_RANK;
extern const std::array<int, 9> CANDIDATE_PAWN_BONUS_EG_BY_RANK;

extern const std::array<int, 9> BACKWARD_PAWN_PENALTY_MG_BY_RANK;
extern const std::array<int, 9> BACKWARD_PAWN_PENALTY_EG_BY_RANK;

extern const int PAWN_ISLAND_PENALTY_MG;
extern const int PAWN_ISLAND_PENALTY_EG;

extern const std::array<int, 9> KING_ATTACK_PRESSURE_PENALTY;

extern const int BISHOP_PAIR_BONUS_MG;
extern const int BISHOP_PAIR_BONUS_EG;

extern const int KING_SHIELD_MAX_PAWNS;
extern const int KING_SHIELD_PER_PAWN_BONUS;

extern const int PAWN_STRUCTURE_DOUBLED_PENALTY;
extern const int PAWN_STRUCTURE_ISOLATED_PENALTY;

extern const int PASSED_PAWN_COUNT_BONUS_MG;
extern const int PASSED_PAWN_COUNT_BONUS_EG;
extern const int PASSED_PAWN_EG_MULTIPLIER;

extern const int PASSED_PAWN_RANK_SQUARE_MULTIPLIER;
extern const int PASSED_PAWN_BLOCKED_DIVISOR;

extern const int TRAPPED_ROOK_PENALTY;

extern const int BAD_BISHOP_HEAVY_PENALTY;
extern const int BAD_BISHOP_LIGHT_PENALTY;

extern const int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY;

extern const int UNCASTLED_KING_CENTER_PENALTY;
extern const int UNCASTLED_KING_LOST_RIGHTS_PENALTY;

extern const int TAPER_SCALE;
extern const int LATE_ENDGAME_PHASE_MAX;
extern const int MOP_UP_EG_MARGIN;
extern const int MOP_UP_MATERIAL_MARGIN;
extern const int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS;
extern const int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS;
extern const int SCALE_MINOR_ONLY_NEAR_EQUAL;
extern const int SCALE_MINOR_ONLY_CLEAR_EDGE;

extern const int MOP_UP_CENTER_DISTANCE_WEIGHT;
extern const int MOP_UP_EDGE_DISTANCE_BASE;
extern const int MOP_UP_EDGE_PRESSURE_WEIGHT;
extern const int MOP_UP_CORNER_DISTANCE_CAP;
extern const int MOP_UP_CORNER_PRESSURE_WEIGHT;
extern const int MOP_UP_KING_DISTANCE_BASE;
extern const int MOP_UP_KING_DISTANCE_WEIGHT;

} // namespace EvalWeights

#endif
