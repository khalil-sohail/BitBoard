#include "board.hpp"
#include "move/movegen_attacks.hpp"
#include "move/movegen_constants.hpp"

bool Board::isSquareAttacked(int square, Color byColor) const {
    const int by = static_cast<int>(byColor);
    const uint64_t target = MoveGenAttacks::squareMask(square);
    const uint64_t occ = occupancyAll();

    const uint64_t pawns = m_bitboards[by][MoveGenConstants::PAWN_IDX];
    uint64_t pawnAttacks = 0ULL;
    if (byColor == Color::White) {
        pawnAttacks |= (pawns << 7) & ~Board::FILE_H;
        pawnAttacks |= (pawns << 9) & ~Board::FILE_A;
    } else {
        pawnAttacks |= (pawns >> 7) & ~Board::FILE_A;
        pawnAttacks |= (pawns >> 9) & ~Board::FILE_H;
    }
    if (pawnAttacks & target) return true;

    uint64_t knights = m_bitboards[by][MoveGenConstants::KNIGHT_IDX];
    while (knights) {
        const int from = MoveGenAttacks::popLsb(knights);
        if (MoveGenAttacks::knightAttacks(from) & target) return true;
    }

    uint64_t bishops =
        m_bitboards[by][MoveGenConstants::BISHOP_IDX] | m_bitboards[by][MoveGenConstants::QUEEN_IDX];
    while (bishops) {
        const int from = MoveGenAttacks::popLsb(bishops);
        if (MoveGenAttacks::bishopAttacks(from, occ) & target) return true;
    }

    uint64_t rooks = m_bitboards[by][MoveGenConstants::ROOK_IDX] | m_bitboards[by][MoveGenConstants::QUEEN_IDX];
    while (rooks) {
        const int from = MoveGenAttacks::popLsb(rooks);
        if (MoveGenAttacks::rookAttacks(from, occ) & target) return true;
    }

    uint64_t king = m_bitboards[by][MoveGenConstants::KING_IDX];
    if (king && (MoveGenAttacks::kingAttacks(MoveGenAttacks::lsbIndex(king)) & target)) return true;

    return false;
}

bool Board::inCheck(Color color) const {
    const int c = static_cast<int>(color);
    const uint64_t king = m_bitboards[c][MoveGenConstants::KING_IDX];
    if (!king) return false;
    const int kingSquare = MoveGenAttacks::lsbIndex(king);
    const Color enemy = (color == Color::White) ? Color::Black : Color::White;
    return isSquareAttacked(kingSquare, enemy);
}
