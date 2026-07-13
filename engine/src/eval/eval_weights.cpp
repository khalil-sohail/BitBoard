#include "eval/eval_weights.hpp"
#include "tuning/generated_tuning_values.hpp"

namespace EvalWeights {

namespace {

constexpr const auto& EVAL_TUNING = Tuning::Generated::VALUES.evaluation;

} // namespace

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
    EVAL_TUNING.rookActivity.openFileMg,
    EVAL_TUNING.rookActivity.semiOpenFileMg,
    EVAL_TUNING.rookActivity.seventhRankMg
};

std::array<int, 3> ROOK_ACTIVITY_BONUS_EG = {
    EVAL_TUNING.rookActivity.openFileEg,
    EVAL_TUNING.rookActivity.semiOpenFileEg,
    EVAL_TUNING.rookActivity.seventhRankEg
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

int PAWN_ISLAND_PENALTY_MG = EVAL_TUNING.pawns.islandPenaltyMg;
int PAWN_ISLAND_PENALTY_EG = EVAL_TUNING.pawns.islandPenaltyEg;

std::array<int, 9> KING_ATTACK_PRESSURE_PENALTY = {
    0, 40, 165, 400, 400, 400, 160, 200, 240
};

int BISHOP_PAIR_BONUS_MG = EVAL_TUNING.bishopPair.middlegame;
int BISHOP_PAIR_BONUS_EG = EVAL_TUNING.bishopPair.endgame;

int KING_SHIELD_MAX_PAWNS = 3;
int KING_SHIELD_PER_PAWN_BONUS = 27;

int PAWN_STRUCTURE_DOUBLED_PENALTY = EVAL_TUNING.pawns.doubledPenalty;
int PAWN_STRUCTURE_ISOLATED_PENALTY = EVAL_TUNING.pawns.isolatedPenalty;

int PASSED_PAWN_COUNT_BONUS_MG = EVAL_TUNING.pawns.passedCountBonusMg;
int PASSED_PAWN_COUNT_BONUS_EG = EVAL_TUNING.pawns.passedCountBonusEg;
int PASSED_PAWN_EG_MULTIPLIER = EVAL_TUNING.pawns.passedEgMultiplier;

int PASSED_PAWN_RANK_SQUARE_MULTIPLIER = EVAL_TUNING.pawns.passedRankSquareMultiplier;
int PASSED_PAWN_BLOCKED_DIVISOR = EVAL_TUNING.pawns.passedBlockedDivisor;

int TRAPPED_ROOK_PENALTY = EVAL_TUNING.piecePlacement.trappedRookPenalty;

int BAD_BISHOP_HEAVY_PENALTY = EVAL_TUNING.piecePlacement.badBishopHeavyPenalty;
int BAD_BISHOP_LIGHT_PENALTY = EVAL_TUNING.piecePlacement.badBishopLightPenalty;

int EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY = EVAL_TUNING.piecePlacement.earlyQueenUndevelopedMinorPenalty;

int UNCASTLED_KING_CENTER_PENALTY = EVAL_TUNING.kingSafety.uncastledCenterPenalty;
int UNCASTLED_KING_LOST_RIGHTS_PENALTY = EVAL_TUNING.kingSafety.uncastledLostRightsPenalty;

int TAPER_SCALE = EVAL_TUNING.endgame.taperScale;
int LATE_ENDGAME_PHASE_MAX = EVAL_TUNING.endgame.latePhaseMax;
int MOP_UP_EG_MARGIN = EVAL_TUNING.endgame.mopUpEgMargin;
int MOP_UP_MATERIAL_MARGIN = EVAL_TUNING.endgame.mopUpMaterialMargin;
int SCALE_OPPOSITE_BISHOPS_MIN_PAWNS = EVAL_TUNING.endgame.scaleOppositeBishopsMinPawns;
int SCALE_OPPOSITE_BISHOPS_LOW_PAWNS = EVAL_TUNING.endgame.scaleOppositeBishopsLowPawns;
int SCALE_MINOR_ONLY_NEAR_EQUAL = EVAL_TUNING.endgame.scaleMinorOnlyNearEqual;
int SCALE_MINOR_ONLY_CLEAR_EDGE = EVAL_TUNING.endgame.scaleMinorOnlyClearEdge;

int MOP_UP_CENTER_DISTANCE_WEIGHT = 15;
int MOP_UP_EDGE_DISTANCE_BASE = 10;
int MOP_UP_EDGE_PRESSURE_WEIGHT = 20;
int MOP_UP_CORNER_DISTANCE_CAP = 7;
int MOP_UP_CORNER_PRESSURE_WEIGHT = 10;
int MOP_UP_KING_DISTANCE_BASE = 25;
int MOP_UP_KING_DISTANCE_WEIGHT = 12;

} // namespace EvalWeights
