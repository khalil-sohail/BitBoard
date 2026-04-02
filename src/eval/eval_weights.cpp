#include "eval/eval_weights.hpp"

namespace EvalWeights {

const std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE = {
    82, 337, 365, 477, 1025, 0
};

const std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE = {
    94, 281, 297, 512, 936, 0
};

const std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC = {
    0, 1, 1, 2, 4, 0
};

const std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_MG = {
    0, 2, 2, 1, 1, 0
};

const std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_EG = {
    0, 2, 3, 2, 1, 0
};

const size_t ROOK_ACTIVITY_OPEN_FILE = 0;
const size_t ROOK_ACTIVITY_SEMI_OPEN_FILE = 1;
const size_t ROOK_ACTIVITY_SEVENTH_RANK = 2;

const std::array<int, 3> ROOK_ACTIVITY_BONUS_MG = {
    15, 10, 20
};

const std::array<int, 3> ROOK_ACTIVITY_BONUS_EG = {
    15, 10, 20
};

const std::array<int, 9> CONNECTED_PAWN_BONUS_MG_BY_RANK = {
    0, 0, 2, 5, 9, 15, 24, 0, 0
};

const std::array<int, 9> CONNECTED_PAWN_BONUS_EG_BY_RANK = {
    0, 0, 3, 7, 12, 20, 32, 0, 0
};

const std::array<int, 9> CANDIDATE_PAWN_BONUS_MG_BY_RANK = {
    0, 0, 1, 3, 6, 10, 14, 0, 0
};

const std::array<int, 9> CANDIDATE_PAWN_BONUS_EG_BY_RANK = {
    0, 0, 2, 5, 9, 14, 20, 0, 0
};

const std::array<int, 9> BACKWARD_PAWN_PENALTY_MG_BY_RANK = {
    0, 0, 6, 9, 12, 15, 18, 0, 0
};

const std::array<int, 9> BACKWARD_PAWN_PENALTY_EG_BY_RANK = {
    0, 0, 4, 6, 8, 10, 12, 0, 0
};

const int PAWN_ISLAND_PENALTY_MG = 4;
const int PAWN_ISLAND_PENALTY_EG = 5;

const std::array<int, 9> KING_ATTACK_PRESSURE_PENALTY = {
    0, 8, 22, 45, 80, 120, 160, 200, 240
};

const int BISHOP_PAIR_BONUS_MG = 30;
const int BISHOP_PAIR_BONUS_EG = 40;

const int KING_SHIELD_MAX_PAWNS = 3;
const int KING_SHIELD_PER_PAWN_BONUS = 15;

const int PAWN_STRUCTURE_DOUBLED_PENALTY = 10;
const int PAWN_STRUCTURE_ISOLATED_PENALTY = 10;

const int PASSED_PAWN_COUNT_BONUS_MG = 10;
const int PASSED_PAWN_COUNT_BONUS_EG = 20;
const int PASSED_PAWN_EG_MULTIPLIER = 1;

const int PASSED_PAWN_RANK_SQUARE_MULTIPLIER = 2;
const int PASSED_PAWN_BLOCKED_DIVISOR = 2;

const int TRAPPED_ROOK_PENALTY = 50;

const int BAD_BISHOP_HEAVY_PENALTY = 50;
const int BAD_BISHOP_LIGHT_PENALTY = 25;

const int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY = 20;

const int UNCASTLED_KING_CENTER_PENALTY = 20;
const int UNCASTLED_KING_LOST_RIGHTS_PENALTY = 50;

const int TAPER_SCALE = 128;
const int LATE_ENDGAME_PHASE_MAX = 8;
const int MOP_UP_EG_MARGIN = 220;
const int MOP_UP_MATERIAL_MARGIN = 350;
const int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS = 56;
const int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS = 72;
const int SCALE_MINOR_ONLY_NEAR_EQUAL = 64;
const int SCALE_MINOR_ONLY_CLEAR_EDGE = 80;

const int MOP_UP_CENTER_DISTANCE_WEIGHT = 6;
const int MOP_UP_EDGE_DISTANCE_BASE = 3;
const int MOP_UP_EDGE_PRESSURE_WEIGHT = 8;
const int MOP_UP_CORNER_DISTANCE_CAP = 7;
const int MOP_UP_CORNER_PRESSURE_WEIGHT = 2;
const int MOP_UP_KING_DISTANCE_BASE = 14;
const int MOP_UP_KING_DISTANCE_WEIGHT = 4;

} // namespace EvalWeights
