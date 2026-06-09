#include "board.hpp"
#include "move/movegen_attacks.hpp"
#include "move/movegen_constants.hpp"

Board::Board() {
    reset();
}

void Board::reset() {
    m_bitboards = {};
    m_sideToMove = Color::White;
    m_castlingRights = 0b1111;
    m_enPassantSquare = -1;
    m_hashHistory.clear();
    m_undoStack.clear();
    m_sanHistory.clear();

    m_bitboards[MoveGenConstants::WHITE_IDX][MoveGenConstants::PAWN_IDX] = Board::RANK_2;
    m_bitboards[MoveGenConstants::BLACK_IDX][MoveGenConstants::PAWN_IDX] = Board::RANK_7;

    m_bitboards[MoveGenConstants::WHITE_IDX][MoveGenConstants::ROOK_IDX] =
        MoveGenAttacks::squareMask(0) | MoveGenAttacks::squareMask(7);
    m_bitboards[MoveGenConstants::BLACK_IDX][MoveGenConstants::ROOK_IDX] =
        MoveGenAttacks::squareMask(56) | MoveGenAttacks::squareMask(63);

    m_bitboards[MoveGenConstants::WHITE_IDX][MoveGenConstants::KNIGHT_IDX] =
        MoveGenAttacks::squareMask(1) | MoveGenAttacks::squareMask(6);
    m_bitboards[MoveGenConstants::BLACK_IDX][MoveGenConstants::KNIGHT_IDX] =
        MoveGenAttacks::squareMask(57) | MoveGenAttacks::squareMask(62);

    m_bitboards[MoveGenConstants::WHITE_IDX][MoveGenConstants::BISHOP_IDX] =
        MoveGenAttacks::squareMask(2) | MoveGenAttacks::squareMask(5);
    m_bitboards[MoveGenConstants::BLACK_IDX][MoveGenConstants::BISHOP_IDX] =
        MoveGenAttacks::squareMask(58) | MoveGenAttacks::squareMask(61);

    m_bitboards[MoveGenConstants::WHITE_IDX][MoveGenConstants::QUEEN_IDX] = MoveGenAttacks::squareMask(3);
    m_bitboards[MoveGenConstants::BLACK_IDX][MoveGenConstants::QUEEN_IDX] = MoveGenAttacks::squareMask(59);

    m_bitboards[MoveGenConstants::WHITE_IDX][MoveGenConstants::KING_IDX] = MoveGenAttacks::squareMask(4);
    m_bitboards[MoveGenConstants::BLACK_IDX][MoveGenConstants::KING_IDX] = MoveGenAttacks::squareMask(60);

    resetEvalStateFromBoard();
    m_hash = computePolyglotHash();
}
