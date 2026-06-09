#ifndef EVAL_WEIGHTS_HPP
#define EVAL_WEIGHTS_HPP

#include "board.hpp"

#include <array>
#include <cstddef>

namespace EvalWeights {

// --- Material & Phase ---
extern std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE;
extern std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE;
extern std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC;
extern std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_MG;
extern std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_EG;

// --- Rook Activity ---
extern const size_t ROOK_ACTIVITY_OPEN_FILE;
extern const size_t ROOK_ACTIVITY_SEMI_OPEN_FILE;
extern const size_t ROOK_ACTIVITY_SEVENTH_RANK;

extern std::array<int, 3> ROOK_ACTIVITY_BONUS_MG;
extern std::array<int, 3> ROOK_ACTIVITY_BONUS_EG;

// --- Pawn Structure (rank-scaled) ---
extern std::array<int, 9> CONNECTED_PAWN_BONUS_MG_BY_RANK;
extern std::array<int, 9> CONNECTED_PAWN_BONUS_EG_BY_RANK;

extern std::array<int, 9> CANDIDATE_PAWN_BONUS_MG_BY_RANK;
extern std::array<int, 9> CANDIDATE_PAWN_BONUS_EG_BY_RANK;

extern std::array<int, 9> BACKWARD_PAWN_PENALTY_MG_BY_RANK;
extern std::array<int, 9> BACKWARD_PAWN_PENALTY_EG_BY_RANK;

extern int PAWN_ISLAND_PENALTY_MG;
extern int PAWN_ISLAND_PENALTY_EG;

// --- King Safety ---
extern std::array<int, 9> KING_ATTACK_PRESSURE_PENALTY;

extern int BISHOP_PAIR_BONUS_MG;
extern int BISHOP_PAIR_BONUS_EG;

extern int KING_SHIELD_MAX_PAWNS;
extern int KING_SHIELD_PER_PAWN_BONUS;

// --- Pawn Penalties ---
extern int PAWN_STRUCTURE_DOUBLED_PENALTY;
extern int PAWN_STRUCTURE_ISOLATED_PENALTY;

// --- Passed Pawns ---
extern int PASSED_PAWN_COUNT_BONUS_MG;
extern int PASSED_PAWN_COUNT_BONUS_EG;
extern int PASSED_PAWN_EG_MULTIPLIER;

extern int PASSED_PAWN_RANK_SQUARE_MULTIPLIER;
extern int PASSED_PAWN_BLOCKED_DIVISOR;

// --- Piece Placement Heuristics ---
extern int TRAPPED_ROOK_PENALTY;

extern int BAD_BISHOP_HEAVY_PENALTY;
extern int BAD_BISHOP_LIGHT_PENALTY;

extern int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY;

extern int UNCASTLED_KING_CENTER_PENALTY;
extern int UNCASTLED_KING_LOST_RIGHTS_PENALTY;

// --- Endgame & Scaling ---
extern int TAPER_SCALE;
extern int LATE_ENDGAME_PHASE_MAX;
extern int MOP_UP_EG_MARGIN;
extern int MOP_UP_MATERIAL_MARGIN;
extern int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS;
extern int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS;
extern int SCALE_MINOR_ONLY_NEAR_EQUAL;
extern int SCALE_MINOR_ONLY_CLEAR_EDGE;

extern int MOP_UP_CENTER_DISTANCE_WEIGHT;
extern int MOP_UP_EDGE_DISTANCE_BASE;
extern int MOP_UP_EDGE_PRESSURE_WEIGHT;
extern int MOP_UP_CORNER_DISTANCE_CAP;
extern int MOP_UP_CORNER_PRESSURE_WEIGHT;
extern int MOP_UP_KING_DISTANCE_BASE;
extern int MOP_UP_KING_DISTANCE_WEIGHT;

} // namespace EvalWeights

#endif
