#include "board.hpp"
#include "move/move_constants.hpp"
#include "move/move_hash.hpp"

#include <cstdint>

void Board::makeMove(const Move& move) {
    m_hashHistory.push_back(m_hash);

    m_undoStack.push_back({
        .bitboards = m_bitboards,
        .sideToMove = m_sideToMove,
        .castlingRights = m_castlingRights,
        .enPassantSquare = m_enPassantSquare,
        .mgScore = m_mgScore,
        .egScore = m_egScore,
        .gamePhase = m_gamePhase,
    });

    const int us = static_cast<int>(m_sideToMove);
    const int them = (us == MoveConstants::WHITE_IDX) ? MoveConstants::BLACK_IDX : MoveConstants::WHITE_IDX;
    const Color usColor = m_sideToMove;
    const Color themColor = (usColor == Color::White) ? Color::Black : Color::White;
    const uint8_t oldCastlingRights = m_castlingRights;
    const int oldEnPassantSquare = m_enPassantSquare;
    const Color oldSideToMove = m_sideToMove;

    const uint64_t fromMask = MoveHash::squareMask(move.from);
    const uint64_t toMask = MoveHash::squareMask(move.to);
    const int pieceIdx = static_cast<int>(move.piece);

    m_hash ^= MoveHash::castlingHash(oldCastlingRights);
    m_hash ^= MoveHash::enPassantHash(oldSideToMove, oldEnPassantSquare, m_bitboards);

    m_hash ^= MoveHash::pieceHash(usColor, move.piece, move.from);

    removePieceEval(usColor, move.piece, move.from);

    m_bitboards[us][pieceIdx] &= ~fromMask;

    for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
        if (m_bitboards[them][p] & toMask) {
            m_hash ^= MoveHash::pieceHash(themColor, static_cast<PieceType>(p), move.to);
            removePieceEval(themColor, static_cast<PieceType>(p), move.to);
            m_bitboards[them][p] &= ~toMask;
        }
    }

    if (move.isEnPassant) {
        const int capSq = (m_sideToMove == Color::White) ? move.to - 8 : move.to + 8;
        m_hash ^= MoveHash::pieceHash(themColor, PieceType::Pawn, capSq);
        removePieceEval(themColor, PieceType::Pawn, capSq);
        m_bitboards[them][MoveConstants::PAWN_IDX] &= ~MoveHash::squareMask(capSq);
    }

    if (move.promotion.has_value()) {
        m_hash ^= MoveHash::pieceHash(usColor, *move.promotion, move.to);
        addPieceEval(usColor, *move.promotion, move.to);
        m_bitboards[us][static_cast<int>(*move.promotion)] |= toMask;
    } else {
        m_hash ^= MoveHash::pieceHash(usColor, move.piece, move.to);
        addPieceEval(usColor, move.piece, move.to);
        m_bitboards[us][pieceIdx] |= toMask;
    }

    if (move.isKingSideCastle) {
        if (m_sideToMove == Color::White) {
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 7);
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 5);
            removePieceEval(usColor, PieceType::Rook, 7);
            addPieceEval(usColor, PieceType::Rook, 5);
            m_bitboards[us][MoveConstants::ROOK_IDX] &= ~MoveHash::squareMask(7);
            m_bitboards[us][MoveConstants::ROOK_IDX] |= MoveHash::squareMask(5);
        } else {
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 63);
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 61);
            removePieceEval(usColor, PieceType::Rook, 63);
            addPieceEval(usColor, PieceType::Rook, 61);
            m_bitboards[us][MoveConstants::ROOK_IDX] &= ~MoveHash::squareMask(63);
            m_bitboards[us][MoveConstants::ROOK_IDX] |= MoveHash::squareMask(61);
        }
    }
    if (move.isQueenSideCastle) {
        if (m_sideToMove == Color::White) {
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 0);
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 3);
            removePieceEval(usColor, PieceType::Rook, 0);
            addPieceEval(usColor, PieceType::Rook, 3);
            m_bitboards[us][MoveConstants::ROOK_IDX] &= ~MoveHash::squareMask(0);
            m_bitboards[us][MoveConstants::ROOK_IDX] |= MoveHash::squareMask(3);
        } else {
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 56);
            m_hash ^= MoveHash::pieceHash(usColor, PieceType::Rook, 59);
            removePieceEval(usColor, PieceType::Rook, 56);
            addPieceEval(usColor, PieceType::Rook, 59);
            m_bitboards[us][MoveConstants::ROOK_IDX] &= ~MoveHash::squareMask(56);
            m_bitboards[us][MoveConstants::ROOK_IDX] |= MoveHash::squareMask(59);
        }
    }

    if (m_sideToMove == Color::White) {
        if (move.piece == PieceType::King) m_castlingRights &= ~(MoveConstants::CASTLE_WK | MoveConstants::CASTLE_WQ);
        if (move.piece == PieceType::Rook && move.from == 0) m_castlingRights &= ~MoveConstants::CASTLE_WQ;
        if (move.piece == PieceType::Rook && move.from == 7) m_castlingRights &= ~MoveConstants::CASTLE_WK;
        if (move.to == 56) m_castlingRights &= ~MoveConstants::CASTLE_BQ;
        if (move.to == 63) m_castlingRights &= ~MoveConstants::CASTLE_BK;
    } else {
        if (move.piece == PieceType::King) m_castlingRights &= ~(MoveConstants::CASTLE_BK | MoveConstants::CASTLE_BQ);
        if (move.piece == PieceType::Rook && move.from == 56) m_castlingRights &= ~MoveConstants::CASTLE_BQ;
        if (move.piece == PieceType::Rook && move.from == 63) m_castlingRights &= ~MoveConstants::CASTLE_BK;
        if (move.to == 0) m_castlingRights &= ~MoveConstants::CASTLE_WQ;
        if (move.to == 7) m_castlingRights &= ~MoveConstants::CASTLE_WK;
    }

    if (move.isDoublePush) {
        m_enPassantSquare = (m_sideToMove == Color::White) ? move.to - 8 : move.to + 8;
    } else {
        m_enPassantSquare = -1;
    }

    m_sideToMove = (m_sideToMove == Color::White) ? Color::Black : Color::White;
    m_hash ^= MoveHash::castlingHash(m_castlingRights);
    m_hash ^= MoveHash::enPassantHash(m_sideToMove, m_enPassantSquare, m_bitboards);
    m_hash ^= MoveHash::SIDE_TO_MOVE_HASH;

    // assert(m_hash == computePolyglotHash() && "Incremental hash desync in makeMove!");
}