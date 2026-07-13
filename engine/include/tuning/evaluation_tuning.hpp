#ifndef TUNING_EVALUATION_TUNING_HPP
#define TUNING_EVALUATION_TUNING_HPP

#include "tuning/tuning_types.hpp"

namespace Tuning {

struct MaterialTuning {
    PieceValueArray middlegame{};
    PieceValueArray endgame{};
};

struct PhaseTuning {
    PieceValueArray increments{};
};

struct MobilityTuning {
    PieceValueArray middlegame{};
    PieceValueArray endgame{};
};

struct RookActivityTuning {
    int openFileMg = 0;
    int openFileEg = 0;
    int semiOpenFileMg = 0;
    int semiOpenFileEg = 0;
    int seventhRankMg = 0;
    int seventhRankEg = 0;

    [[nodiscard]] constexpr RookActivityArray middlegameArray() const noexcept {
        return {openFileMg, semiOpenFileMg, seventhRankMg};
    }

    [[nodiscard]] constexpr RookActivityArray endgameArray() const noexcept {
        return {openFileEg, semiOpenFileEg, seventhRankEg};
    }
};

struct BishopPairTuning {
    int middlegame = 0;
    int endgame = 0;
};

struct PawnStructureTuning {
    RankTuningTable connectedMgByRank{};
    RankTuningTable connectedEgByRank{};
    RankTuningTable candidateMgByRank{};
    RankTuningTable candidateEgByRank{};
    RankTuningTable backwardMgByRank{};
    RankTuningTable backwardEgByRank{};
    int doubledPenalty = 0;
    int isolatedPenalty = 0;
    int islandPenaltyMg = 0;
    int islandPenaltyEg = 0;
    int passedCountBonusMg = 0;
    int passedCountBonusEg = 0;
    int passedEgMultiplier = 0;
    int passedRankSquareMultiplier = 0;
    int passedBlockedDivisor = 1;
};

struct KingSafetyTuning {
    KingPressureTable attackPressure{};
    int shieldMaxPawns = 0;
    int shieldPerPawnBonus = 0;
    int uncastledCenterPenalty = 0;
    int uncastledLostRightsPenalty = 0;
};

struct PiecePlacementTuning {
    int badBishopHeavyPenalty = 0;
    int badBishopLightPenalty = 0;
    int earlyQueenUndevelopedMinorPenalty = 0;
    int trappedRookPenalty = 0;
};

struct EndgameScaleTuning {
    int taperScale = 1;
    int latePhaseMax = 0;
    int mopUpEgMargin = 0;
    int mopUpMaterialMargin = 0;
    int scaleOppositeBishopsMinPawns = 0;
    int scaleOppositeBishopsLowPawns = 0;
    int scaleMinorOnlyNearEqual = 0;
    int scaleMinorOnlyClearEdge = 0;
    MopUpWeightArray mopUpWeights{};
};

struct PieceSquareTuning {
    static constexpr std::size_t pieceCount = kPieceTypeCount;
    static constexpr std::size_t squareCount = kBoardSquareCount;
    bool middlegameRepresented = true;
    bool endgameRepresented = true;
};

struct EvaluationTuning {
    MaterialTuning material;
    PhaseTuning phase;
    MobilityTuning mobility;
    RookActivityTuning rookActivity;
    BishopPairTuning bishopPair;
    PawnStructureTuning pawns;
    KingSafetyTuning kingSafety;
    PiecePlacementTuning piecePlacement;
    EndgameScaleTuning endgame;
    PieceSquareTuning pieceSquare;
};

static_assert(kPieceTypeCount == 6);
static_assert(kRankTableSize == 9);
static_assert(kKingPressureTableSize == 9);
static_assert(kBoardSquareCount == 64);

} // namespace Tuning

#endif
