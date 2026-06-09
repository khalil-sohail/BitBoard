#include "eval/eval_weights.hpp"

namespace EvalWeights {

std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE = {
    150, 250, 284, 490, 839, 0
};

std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE = {
    160, 200, 269, 650, 1150, 0
};

std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC = {
    0, 1, 1, 2, 4, 0
};

std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_MG = {
    0, 3, 10, 8, 5, 0
};

std::array<int, static_cast<size_t>(PieceType::Count)> MOBILITY_BONUS_EG = {
    0, 10, 10, 8, 8, 0
};

const size_t ROOK_ACTIVITY_OPEN_FILE = 0;
const size_t ROOK_ACTIVITY_SEMI_OPEN_FILE = 1;
const size_t ROOK_ACTIVITY_SEVENTH_RANK = 2;

std::array<int, 3> ROOK_ACTIVITY_BONUS_MG = {
    40, 30, 50
};

std::array<int, 3> ROOK_ACTIVITY_BONUS_EG = {
    40, 0, 50
};

std::array<int, 9> CONNECTED_PAWN_BONUS_MG_BY_RANK = {
    0, 0, 0, 21, 60, 60, 60, 0, 0
};

std::array<int, 9> CONNECTED_PAWN_BONUS_EG_BY_RANK = {
    0, 0, 0, 0, 80, 80, 80, 80, 0
};

std::array<int, 9> CANDIDATE_PAWN_BONUS_MG_BY_RANK = {
    0, 0, 0, 40, 40, 40, 40, 0, 0
};

std::array<int, 9> CANDIDATE_PAWN_BONUS_EG_BY_RANK = {
    0, 0, 0, 50, 50, 50, 50, 0, 0
};

std::array<int, 9> BACKWARD_PAWN_PENALTY_MG_BY_RANK = {
    0, 0, 40, 20, 10, 40, 0, 0, 0
};

std::array<int, 9> BACKWARD_PAWN_PENALTY_EG_BY_RANK = {
    0, 0, 30, 30, 30, 0, 0, 0, 0
};

int PAWN_ISLAND_PENALTY_MG = 20;
int PAWN_ISLAND_PENALTY_EG = 20;

std::array<int, 9> KING_ATTACK_PRESSURE_PENALTY = {
    0, 40, 165, 400, 400, 400, 160, 200, 240
};

int BISHOP_PAIR_BONUS_MG = 70;
int BISHOP_PAIR_BONUS_EG = 80;

int KING_SHIELD_MAX_PAWNS = 3;
int KING_SHIELD_PER_PAWN_BONUS = 27;

int PAWN_STRUCTURE_DOUBLED_PENALTY = 30;
int PAWN_STRUCTURE_ISOLATED_PENALTY = 30;

int PASSED_PAWN_COUNT_BONUS_MG = 0;
int PASSED_PAWN_COUNT_BONUS_EG = 6;
int PASSED_PAWN_EG_MULTIPLIER = 4;

int PASSED_PAWN_RANK_SQUARE_MULTIPLIER = 5;
int PASSED_PAWN_BLOCKED_DIVISOR = 2;

int TRAPPED_ROOK_PENALTY = 10;

int BAD_BISHOP_HEAVY_PENALTY = 100;
int BAD_BISHOP_LIGHT_PENALTY = 60;

int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY = 50;

int UNCASTLED_KING_CENTER_PENALTY = 50;
int UNCASTLED_KING_LOST_RIGHTS_PENALTY = 66;

int TAPER_SCALE = 128;
int LATE_ENDGAME_PHASE_MAX = 8;
int MOP_UP_EG_MARGIN = 220;
int MOP_UP_MATERIAL_MARGIN = 350;
int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS = 56;
int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS = 72;
int SCALE_MINOR_ONLY_NEAR_EQUAL = 64;
int SCALE_MINOR_ONLY_CLEAR_EDGE = 80;

int MOP_UP_CENTER_DISTANCE_WEIGHT = 15;
int MOP_UP_EDGE_DISTANCE_BASE = 10;
int MOP_UP_EDGE_PRESSURE_WEIGHT = 20;
int MOP_UP_CORNER_DISTANCE_CAP = 7;
int MOP_UP_CORNER_PRESSURE_WEIGHT = 10;
int MOP_UP_KING_DISTANCE_BASE = 25;
int MOP_UP_KING_DISTANCE_WEIGHT = 12;

} // namespace EvalWeights