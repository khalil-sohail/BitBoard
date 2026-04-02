#include "eval/eval_terms.hpp"
#include "eval/eval_weights.hpp"

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

} // namespace

int endgameMaterialValue(uint64_t pawns, uint64_t knights, uint64_t bishops, uint64_t rooks, uint64_t queens) {
    return std::popcount(pawns) * EvalWeights::EG_VALUE[PAWN_IDX]
        + std::popcount(knights) * EvalWeights::EG_VALUE[KNIGHT_IDX]
        + std::popcount(bishops) * EvalWeights::EG_VALUE[BISHOP_IDX]
        + std::popcount(rooks) * EvalWeights::EG_VALUE[ROOK_IDX]
        + std::popcount(queens) * EvalWeights::EG_VALUE[QUEEN_IDX];
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
    if (phase > EvalWeights::LATE_ENDGAME_PHASE_MAX) {
        return EvalWeights::TAPER_SCALE;
    }

    int scale = EvalWeights::TAPER_SCALE;

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
                scale = std::min(scale, EvalWeights::SCALE_MINOR_ONLY_NEAR_EQUAL);
            } else if (minorDiff == 2) {
                scale = std::min(scale, EvalWeights::SCALE_MINOR_ONLY_CLEAR_EDGE);
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
                    ? EvalWeights::SCALE_OPPOSITE_BISHOPS_MIN_PAWNS
                    : EvalWeights::SCALE_OPPOSITE_BISHOPS_LOW_PAWNS
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

    const int edgePressure = (EvalWeights::MOP_UP_EDGE_DISTANCE_BASE - edgeDistance) * EvalWeights::MOP_UP_EDGE_PRESSURE_WEIGHT;
    const int cornerPressure = (EvalWeights::MOP_UP_CORNER_DISTANCE_CAP - std::min(cornerDistance, EvalWeights::MOP_UP_CORNER_DISTANCE_CAP)) * EvalWeights::MOP_UP_CORNER_PRESSURE_WEIGHT;

    return centerDistance * EvalWeights::MOP_UP_CENTER_DISTANCE_WEIGHT
        + edgePressure
        + cornerPressure
        + (EvalWeights::MOP_UP_KING_DISTANCE_BASE - kingDistance) * EvalWeights::MOP_UP_KING_DISTANCE_WEIGHT;
}

} // namespace EvalTerms
