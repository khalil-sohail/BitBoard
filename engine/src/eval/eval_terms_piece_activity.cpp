#include "eval/eval_terms.hpp"
#include "eval/eval_masks.hpp"
#include "tuning/generated_tuning_values.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace EvalTerms {
namespace {

constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);

constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;
constexpr const auto& EVAL_TUNING = Tuning::Generated::VALUES.evaluation;

} // namespace

uint64_t knightAttacks(int square) {
    const uint64_t k = Eval::squareMask(square);

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
    const int file = Eval::fileOf(square);

    for (int r = rank + 1, f = file + 1; r < 8 && f < 8; ++r, ++f) {
        const int sq = r * 8 + f;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }
    for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; ++r, --f) {
        const int sq = r * 8 + f;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }
    for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; --r, ++f) {
        const int sq = r * 8 + f;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }
    for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
        const int sq = r * 8 + f;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }

    return attacks;
}

uint64_t rookAttacks(int square, uint64_t occupied) {
    uint64_t attacks = 0ULL;
    const int rank = square >> 3;
    const int file = Eval::fileOf(square);

    for (int r = rank + 1; r < 8; ++r) {
        const int sq = r * 8 + file;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }
    for (int r = rank - 1; r >= 0; --r) {
        const int sq = r * 8 + file;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }
    for (int f = file + 1; f < 8; ++f) {
        const int sq = rank * 8 + f;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }
    for (int f = file - 1; f >= 0; --f) {
        const int sq = rank * 8 + f;
        attacks |= Eval::squareMask(sq);
        if (occupied & Eval::squareMask(sq)) {
            break;
        }
    }

    return attacks;
}

Eval::TaperTerms mobilityTermsForSide(
    uint64_t ownKnights,
    uint64_t ownBishops,
    uint64_t ownRooks,
    uint64_t ownQueens,
    uint64_t ownOccupancy,
    uint64_t allOccupancy
) {
    Eval::TaperTerms terms{};

    uint64_t bb = ownKnights;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);

        const int mobility = std::popcount(knightAttacks(square) & ~ownOccupancy);
        terms.mg += mobility * EVAL_TUNING.mobility.middlegame[static_cast<size_t>(KNIGHT_IDX)];
        terms.eg += mobility * EVAL_TUNING.mobility.endgame[static_cast<size_t>(KNIGHT_IDX)];
    }

    bb = ownBishops;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);

        const int mobility = std::popcount(bishopAttacks(square, allOccupancy) & ~ownOccupancy);
        terms.mg += mobility * EVAL_TUNING.mobility.middlegame[static_cast<size_t>(BISHOP_IDX)];
        terms.eg += mobility * EVAL_TUNING.mobility.endgame[static_cast<size_t>(BISHOP_IDX)];
    }

    bb = ownRooks;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);

        const int mobility = std::popcount(rookAttacks(square, allOccupancy) & ~ownOccupancy);
        terms.mg += mobility * EVAL_TUNING.mobility.middlegame[static_cast<size_t>(ROOK_IDX)];
        terms.eg += mobility * EVAL_TUNING.mobility.endgame[static_cast<size_t>(ROOK_IDX)];
    }

    bb = ownQueens;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);

        const uint64_t queenAttacks = (bishopAttacks(square, allOccupancy) | rookAttacks(square, allOccupancy)) & ~ownOccupancy;
        const int mobility = std::popcount(queenAttacks);
        terms.mg += mobility * EVAL_TUNING.mobility.middlegame[static_cast<size_t>(QUEEN_IDX)];
        terms.eg += mobility * EVAL_TUNING.mobility.endgame[static_cast<size_t>(QUEEN_IDX)];
    }

    return terms;
}

Eval::TaperTerms rookActivityTermsForSide(Color color, uint64_t rooks, uint64_t ownPawns, uint64_t enemyPawns) {
    Eval::TaperTerms terms{};
    uint64_t bb = rooks;

    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);

        const uint64_t fileMask = EvalMask::MASKS.FILE_MASKS[static_cast<size_t>(Eval::fileOf(square))];
        const bool hasOwnPawn = (ownPawns & fileMask) != 0ULL;
        const bool hasEnemyPawn = (enemyPawns & fileMask) != 0ULL;

        if (!hasOwnPawn && !hasEnemyPawn) {
            terms.mg += EVAL_TUNING.rookActivity.openFileMg;
            terms.eg += EVAL_TUNING.rookActivity.openFileEg;
        } else if (!hasOwnPawn && hasEnemyPawn) {
            terms.mg += EVAL_TUNING.rookActivity.semiOpenFileMg;
            terms.eg += EVAL_TUNING.rookActivity.semiOpenFileEg;
        }

        const uint64_t rookSquare = Eval::squareMask(square);
        if ((color == Color::White && (rookSquare & Board::RANK_7) != 0ULL) ||
            (color == Color::Black && (rookSquare & Board::RANK_2) != 0ULL)) {
            terms.mg += EVAL_TUNING.rookActivity.seventhRankMg;
            terms.eg += EVAL_TUNING.rookActivity.seventhRankEg;
        }
    }

    return terms;
}

int trappedRookPenalty(Color color, uint64_t rooks, uint64_t king) {
    if (king == 0ULL) {
        return 0;
    }

    if (color == Color::White) {
        const bool rookInCorner = (rooks & ((1ULL << 0) | (1ULL << 7))) != 0ULL;
        const bool kingTrapsRook = (king & ((1ULL << 1) | (1ULL << 2) | (1ULL << 5) | (1ULL << 6))) != 0ULL;
        return (rookInCorner && kingTrapsRook) ? EVAL_TUNING.piecePlacement.trappedRookPenalty : 0;
    }

    const bool rookInCorner = (rooks & ((1ULL << 56) | (1ULL << 63))) != 0ULL;
    const bool kingTrapsRook = (king & ((1ULL << 57) | (1ULL << 58) | (1ULL << 61) | (1ULL << 62))) != 0ULL;
    return (rookInCorner && kingTrapsRook) ? EVAL_TUNING.piecePlacement.trappedRookPenalty : 0;
}

int badBishopPenalty(uint64_t ownPawns, uint64_t ownBishops, Color color) {
    int penalty = 0;
    if (color == Color::White) {
        if ((ownBishops & 512ULL) && (ownPawns & 262144ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopHeavyPenalty;
        if ((ownBishops & 16384ULL) && (ownPawns & 2097152ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopHeavyPenalty;
        if ((ownBishops & 4ULL) && (ownPawns & 2048ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopLightPenalty;
        if ((ownBishops & 32ULL) && (ownPawns & 4096ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopLightPenalty;
    } else {
        if ((ownBishops & 562949953421312ULL) && (ownPawns & 4398046511104ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopHeavyPenalty;
        if ((ownBishops & 18014398509481984ULL) && (ownPawns & 35184372088832ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopHeavyPenalty;
        if ((ownBishops & 288230376151711744ULL) && (ownPawns & 2251799813685248ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopLightPenalty;
        if ((ownBishops & 2305843009213693952ULL) && (ownPawns & 4503599627370496ULL)) penalty += EVAL_TUNING.piecePlacement.badBishopLightPenalty;
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
            penalty += EVAL_TUNING.piecePlacement.earlyQueenUndevelopedMinorPenalty * (undevelopedMinors - 1);
        }
    }
    return penalty;
}

int uncastledKingPenalty(uint64_t king, uint8_t castlingRights, Color color) {
    if (!king) return 0;
    int penalty = 0;
    int kingSq = Eval::lsbIndex(king);
    int file = kingSq % 8;

    if (file >= 2 && file <= 5) {
        penalty += EVAL_TUNING.kingSafety.uncastledCenterPenalty;

        if (color == Color::White && !(castlingRights & (CASTLE_WK | CASTLE_WQ))) {
            penalty += EVAL_TUNING.kingSafety.uncastledLostRightsPenalty;
        } else if (color == Color::Black && !(castlingRights & (CASTLE_BK | CASTLE_BQ))) {
            penalty += EVAL_TUNING.kingSafety.uncastledLostRightsPenalty;
        }
    }
    return penalty;
}

} // namespace EvalTerms
