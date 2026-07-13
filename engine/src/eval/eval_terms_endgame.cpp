#include "eval/eval_terms.hpp"
#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace EvalTerms {
namespace {

constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);
constexpr const auto& EVAL_TUNING = Tuning::Generated::VALUES.evaluation;
constexpr std::size_t MOP_UP_CENTER_DISTANCE_WEIGHT = 0;
constexpr std::size_t MOP_UP_EDGE_DISTANCE_BASE = 1;
constexpr std::size_t MOP_UP_EDGE_PRESSURE_WEIGHT = 2;
constexpr std::size_t MOP_UP_CORNER_DISTANCE_CAP = 3;
constexpr std::size_t MOP_UP_CORNER_PRESSURE_WEIGHT = 4;
constexpr std::size_t MOP_UP_KING_DISTANCE_BASE = 5;
constexpr std::size_t MOP_UP_KING_DISTANCE_WEIGHT = 6;

} // namespace

int endgameMaterialValue(uint64_t pawns, uint64_t knights, uint64_t bishops, uint64_t rooks, uint64_t queens) {
    return std::popcount(pawns) * EVAL_TUNING.material.endgame[PAWN_IDX]
        + std::popcount(knights) * EVAL_TUNING.material.endgame[KNIGHT_IDX]
        + std::popcount(bishops) * EVAL_TUNING.material.endgame[BISHOP_IDX]
        + std::popcount(rooks) * EVAL_TUNING.material.endgame[ROOK_IDX]
        + std::popcount(queens) * EVAL_TUNING.material.endgame[QUEEN_IDX];
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

    const int whiteSquare = Eval::lsbIndex(whiteBishops);
    const int blackSquare = Eval::lsbIndex(blackBishops);
    const int whiteColor = ((whiteSquare >> 3) + Eval::fileOf(whiteSquare)) & 1;
    const int blackColor = ((blackSquare >> 3) + Eval::fileOf(blackSquare)) & 1;
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
    if (phase > EVAL_TUNING.endgame.latePhaseMax) {
        return EVAL_TUNING.endgame.taperScale;
    }

    int scale = EVAL_TUNING.endgame.taperScale;

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
                scale = std::min(scale, EVAL_TUNING.endgame.scaleMinorOnlyNearEqual);
            } else if (minorDiff == 2) {
                scale = std::min(scale, EVAL_TUNING.endgame.scaleMinorOnlyClearEdge);
            }
        }

        const bool bishopOnlyEndgame = (whiteKnightsCount == 0 && blackKnightsCount == 0
            && whiteBishopsCount == 1 && blackBishopsCount == 1);

        if (bishopOnlyEndgame
            && totalPawns <= 6
            && areOppositeColoredSingleBishops(whiteBishops, blackBishops)) {
            scale = std::min(
                scale,
                (totalPawns <= 2)
                    ? EVAL_TUNING.endgame.scaleOppositeBishopsMinPawns
                    : EVAL_TUNING.endgame.scaleOppositeBishopsLowPawns
            );
        }
    }

    return scale;
}

int mopUpEval(int winningKingSq, int losingKingSq) {
    const int losingFile = Eval::fileOf(losingKingSq);
    const int losingRank = losingKingSq >> 3;
    const int centerDistance = std::abs(losingFile - 3) + std::abs(losingRank - 3);
    const int edgeDistance = std::min({losingFile, 7 - losingFile, losingRank, 7 - losingRank});
    const int cornerDistance = std::min({
        losingFile + losingRank,
        losingFile + (7 - losingRank),
        (7 - losingFile) + losingRank,
        (7 - losingFile) + (7 - losingRank)
    });

    const int winningFile = Eval::fileOf(winningKingSq);
    const int winningRank = winningKingSq >> 3;
    const int kingDistance = std::abs(winningFile - losingFile) + std::abs(winningRank - losingRank);

    const auto& weights = EVAL_TUNING.endgame.mopUpWeights;
    const int edgePressure = (weights[MOP_UP_EDGE_DISTANCE_BASE] - edgeDistance) * weights[MOP_UP_EDGE_PRESSURE_WEIGHT];
    const int cornerPressure = (weights[MOP_UP_CORNER_DISTANCE_CAP] - std::min(cornerDistance, weights[MOP_UP_CORNER_DISTANCE_CAP])) * weights[MOP_UP_CORNER_PRESSURE_WEIGHT];

    return centerDistance * weights[MOP_UP_CENTER_DISTANCE_WEIGHT]
        + edgePressure
        + cornerPressure
        + (weights[MOP_UP_KING_DISTANCE_BASE] - kingDistance) * weights[MOP_UP_KING_DISTANCE_WEIGHT];
}

} // namespace EvalTerms
