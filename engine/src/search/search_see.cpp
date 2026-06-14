#include "search/search_see.hpp"
#include "search/search_constants.hpp"
#include "move/movegen_attacks.hpp"
#include "move/movegen_constants.hpp"

#include <algorithm>
#include <cstdint>

namespace SearchInternal {

static inline int getPieceValueFast(PieceType pt) {
    if (pt == PieceType::Count) return 0;
    return SearchConstants::PIECE_VALUES[static_cast<size_t>(pt)];
}

static std::pair<PieceType, int> getLeastValuableAttacker(const Board& board, int square, Color color, uint64_t occ) {
    const uint64_t target = MoveGenAttacks::squareMask(square);
    
    // Check Pawns
    uint64_t pawns = board.getBitboard(color, PieceType::Pawn) & occ;
    if (pawns) {
        uint64_t pawnAttacks = 0ULL;
        if (color == Color::White) {
            pawnAttacks |= (pawns << 7) & ~Board::FILE_H;
            pawnAttacks |= (pawns << 9) & ~Board::FILE_A;
        } else {
            pawnAttacks |= (pawns >> 7) & ~Board::FILE_A;
            pawnAttacks |= (pawns >> 9) & ~Board::FILE_H;
        }
        if (pawnAttacks & target) {
            uint64_t targetPawnMask = 0ULL;
            if (color == Color::White) {
                targetPawnMask |= (target >> 7) & ~Board::FILE_A;
                targetPawnMask |= (target >> 9) & ~Board::FILE_H;
            } else {
                targetPawnMask |= (target << 7) & ~Board::FILE_H;
                targetPawnMask |= (target << 9) & ~Board::FILE_A;
            }
            uint64_t attackingPawns = pawns & targetPawnMask;
            if (attackingPawns) {
                return {PieceType::Pawn, MoveGenAttacks::lsbIndex(attackingPawns)};
            }
        }
    }

    // Check Knights
    uint64_t knights = board.getBitboard(color, PieceType::Knight) & occ;
    if (knights) {
        uint64_t attackingKnights = MoveGenAttacks::knightAttacks(square) & knights;
        if (attackingKnights) {
            return {PieceType::Knight, MoveGenAttacks::lsbIndex(attackingKnights)};
        }
    }

    // Check Bishops
    uint64_t bishops = board.getBitboard(color, PieceType::Bishop) & occ;
    if (bishops) {
        uint64_t attackingBishops = MoveGenAttacks::bishopAttacks(square, occ) & bishops;
        if (attackingBishops) {
            return {PieceType::Bishop, MoveGenAttacks::lsbIndex(attackingBishops)};
        }
    }

    // Check Rooks
    uint64_t rooks = board.getBitboard(color, PieceType::Rook) & occ;
    if (rooks) {
        uint64_t attackingRooks = MoveGenAttacks::rookAttacks(square, occ) & rooks;
        if (attackingRooks) {
            return {PieceType::Rook, MoveGenAttacks::lsbIndex(attackingRooks)};
        }
    }

    // Check Queens
    uint64_t queens = board.getBitboard(color, PieceType::Queen) & occ;
    if (queens) {
        uint64_t attackingQueens = (MoveGenAttacks::bishopAttacks(square, occ) | MoveGenAttacks::rookAttacks(square, occ)) & queens;
        if (attackingQueens) {
            return {PieceType::Queen, MoveGenAttacks::lsbIndex(attackingQueens)};
        }
    }

    // Check King
    uint64_t king = board.getBitboard(color, PieceType::King) & occ;
    if (king) {
        uint64_t attackingKing = MoveGenAttacks::kingAttacks(square) & king;
        if (attackingKing) {
            return {PieceType::King, MoveGenAttacks::lsbIndex(attackingKing)};
        }
    }

    return {PieceType::Count, -1};
}

int see(const Board& board, const Move& move) {
    int gain[32];
    int d = 0;

    int targetSq = move.to;
    
    gain[d] = 0;
    if (move.isEnPassant) {
        gain[d] = getPieceValueFast(PieceType::Pawn);
    } else if (move.isCapture) {
        auto victim = board.pieceAt(targetSq);
        if (victim) {
            gain[d] = getPieceValueFast(victim->second);
        }
    }

    if (move.promotion) {
        gain[d] += getPieceValueFast(*move.promotion) - getPieceValueFast(PieceType::Pawn);
    }

    uint64_t occ = board.occupancyAll();
    
    occ &= ~MoveGenAttacks::squareMask(move.from);
    if (move.isEnPassant) {
        int capSq = (board.sideToMove() == Color::White) ? move.to - 8 : move.to + 8;
        occ &= ~MoveGenAttacks::squareMask(capSq);
    }

    Color stm = (board.sideToMove() == Color::White) ? Color::Black : Color::White;
    PieceType currentAttackerPiece = move.promotion.value_or(move.piece);
    int attackerValue = getPieceValueFast(currentAttackerPiece);

    while (true) {
        d++;
        gain[d] = attackerValue - gain[d - 1];

        auto [nextAttackerPiece, fromSq] = getLeastValuableAttacker(board, targetSq, stm, occ);
        if (nextAttackerPiece == PieceType::Count) {
            break;
        }

        occ &= ~MoveGenAttacks::squareMask(fromSq);
        attackerValue = getPieceValueFast(nextAttackerPiece);
        stm = (stm == Color::White) ? Color::Black : Color::White;
    }

    while (--d) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    }

    return gain[0];
}

} // namespace SearchInternal
