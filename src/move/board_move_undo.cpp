#include "board.hpp"
#include "move/move_hash.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

void Board::makeNullMove() {
    m_savedNullEP = m_enPassantSquare;
    m_hash ^= MoveHash::enPassantHash(m_sideToMove, m_enPassantSquare, m_bitboards);
    m_enPassantSquare = -1;
    m_sideToMove = (m_sideToMove == Color::White) ? Color::Black : Color::White;
    m_hash ^= MoveHash::SIDE_TO_MOVE_HASH;
}

void Board::undoNullMove() {
    m_sideToMove = (m_sideToMove == Color::White) ? Color::Black : Color::White;
    m_hash ^= MoveHash::SIDE_TO_MOVE_HASH;
    m_enPassantSquare = m_savedNullEP;
    m_hash ^= MoveHash::enPassantHash(m_sideToMove, m_enPassantSquare, m_bitboards);
}

bool Board::undoMove() {
    if (m_undoStack.empty()) {
        return false;
    }

    const UndoState& prev = m_undoStack.back();
    m_bitboards = prev.bitboards;
    m_sideToMove = prev.sideToMove;
    m_castlingRights = prev.castlingRights;
    m_enPassantSquare = prev.enPassantSquare;
    m_mgScore = prev.mgScore;
    m_egScore = prev.egScore;
    m_gamePhase = prev.gamePhase;
    m_undoStack.pop_back();

    assert(!m_hashHistory.empty() && "Missing hash history entry on undoMove");
    m_hash = m_hashHistory.back();
    m_hashHistory.pop_back();

    return true;
}

bool Board::applyMove(const Move& move) {
    const std::vector<Move> legal = generateLegalMoves();
    auto it = std::find_if(legal.begin(), legal.end(), [&](const Move& m) {
        return m.from == move.from &&
               m.to == move.to &&
               m.piece == move.piece &&
               m.promotion == move.promotion;
    });
    if (it == legal.end()) {
        return false;
    }
    makeMove(*it);
    return true;
}