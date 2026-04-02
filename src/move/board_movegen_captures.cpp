#include "board.hpp"
#include "move/movegen_attacks.hpp"
#include "move/movegen_constants.hpp"

std::vector<Move> Board::generatePseudoLegalCaptures() const {
    std::vector<Move> moves;

    const Color usColor = m_sideToMove;
    const Color themColor = (usColor == Color::White) ? Color::Black : Color::White;
    const int us = static_cast<int>(usColor);

    const uint64_t ownOcc = occupancy(usColor);
    const uint64_t oppOcc = occupancy(themColor);
    const uint64_t allOcc = ownOcc | oppOcc;

    const uint64_t pawns = m_bitboards[us][MoveGenConstants::PAWN_IDX];
    if (usColor == Color::White) {
        uint64_t capLeft = ((pawns & ~Board::FILE_A) << 7) & oppOcc;
        uint64_t capRight = ((pawns & ~Board::FILE_H) << 9) & oppOcc;

        uint64_t capLeftNormal = capLeft & ~Board::RANK_8;
        uint64_t capRightNormal = capRight & ~Board::RANK_8;
        uint64_t capLeftPromo = capLeft & Board::RANK_8;
        uint64_t capRightPromo = capRight & Board::RANK_8;

        while (capLeftNormal) {
            const int to = MoveGenAttacks::popLsb(capLeftNormal);
            Move m = MoveGenAttacks::buildMove(to - 7, to, PieceType::Pawn);
            m.isCapture = true;
            moves.push_back(m);
        }
        while (capRightNormal) {
            const int to = MoveGenAttacks::popLsb(capRightNormal);
            Move m = MoveGenAttacks::buildMove(to - 9, to, PieceType::Pawn);
            m.isCapture = true;
            moves.push_back(m);
        }
        while (capLeftPromo) {
            const int to = MoveGenAttacks::popLsb(capLeftPromo);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = MoveGenAttacks::buildMove(to - 7, to, PieceType::Pawn);
                m.isCapture = true;
                m.promotion = promo;
                moves.push_back(m);
            }
        }
        while (capRightPromo) {
            const int to = MoveGenAttacks::popLsb(capRightPromo);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = MoveGenAttacks::buildMove(to - 9, to, PieceType::Pawn);
                m.isCapture = true;
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        if (m_enPassantSquare != -1) {
            const uint64_t epMask = MoveGenAttacks::squareMask(m_enPassantSquare);
            uint64_t epLeft = ((pawns & ~Board::FILE_A) << 7) & epMask;
            uint64_t epRight = ((pawns & ~Board::FILE_H) << 9) & epMask;
            if (epLeft) {
                Move m = MoveGenAttacks::buildMove(m_enPassantSquare - 7, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
            if (epRight) {
                Move m = MoveGenAttacks::buildMove(m_enPassantSquare - 9, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
        }
    } else {
        uint64_t capLeft = ((pawns & ~Board::FILE_A) >> 9) & oppOcc;
        uint64_t capRight = ((pawns & ~Board::FILE_H) >> 7) & oppOcc;

        uint64_t capLeftNormal = capLeft & ~Board::RANK_1;
        uint64_t capRightNormal = capRight & ~Board::RANK_1;
        uint64_t capLeftPromo = capLeft & Board::RANK_1;
        uint64_t capRightPromo = capRight & Board::RANK_1;

        while (capLeftNormal) {
            const int to = MoveGenAttacks::popLsb(capLeftNormal);
            Move m = MoveGenAttacks::buildMove(to + 9, to, PieceType::Pawn);
            m.isCapture = true;
            moves.push_back(m);
        }
        while (capRightNormal) {
            const int to = MoveGenAttacks::popLsb(capRightNormal);
            Move m = MoveGenAttacks::buildMove(to + 7, to, PieceType::Pawn);
            m.isCapture = true;
            moves.push_back(m);
        }
        while (capLeftPromo) {
            const int to = MoveGenAttacks::popLsb(capLeftPromo);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = MoveGenAttacks::buildMove(to + 9, to, PieceType::Pawn);
                m.isCapture = true;
                m.promotion = promo;
                moves.push_back(m);
            }
        }
        while (capRightPromo) {
            const int to = MoveGenAttacks::popLsb(capRightPromo);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = MoveGenAttacks::buildMove(to + 7, to, PieceType::Pawn);
                m.isCapture = true;
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        if (m_enPassantSquare != -1) {
            const uint64_t epMask = MoveGenAttacks::squareMask(m_enPassantSquare);
            uint64_t epLeft = ((pawns & ~Board::FILE_A) >> 9) & epMask;
            uint64_t epRight = ((pawns & ~Board::FILE_H) >> 7) & epMask;
            if (epLeft) {
                Move m = MoveGenAttacks::buildMove(m_enPassantSquare + 9, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
            if (epRight) {
                Move m = MoveGenAttacks::buildMove(m_enPassantSquare + 7, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
        }
    }

    auto addCapturesFromMask = [&](uint64_t pieces, PieceType piece, auto attacksFn) {
        uint64_t bb = pieces;
        while (bb) {
            const int from = MoveGenAttacks::popLsb(bb);
            uint64_t targets = attacksFn(from) & oppOcc;
            while (targets) {
                const int to = MoveGenAttacks::popLsb(targets);
                Move m = MoveGenAttacks::buildMove(from, to, piece);
                m.isCapture = true;
                moves.push_back(m);
            }
        }
    };

    addCapturesFromMask(m_bitboards[us][MoveGenConstants::KNIGHT_IDX], PieceType::Knight, [&](int from) {
        return MoveGenAttacks::knightAttacks(from);
    });

    addCapturesFromMask(m_bitboards[us][MoveGenConstants::BISHOP_IDX], PieceType::Bishop, [&](int from) {
        return MoveGenAttacks::bishopAttacks(from, allOcc);
    });

    addCapturesFromMask(m_bitboards[us][MoveGenConstants::ROOK_IDX], PieceType::Rook, [&](int from) {
        return MoveGenAttacks::rookAttacks(from, allOcc);
    });

    addCapturesFromMask(m_bitboards[us][MoveGenConstants::QUEEN_IDX], PieceType::Queen, [&](int from) {
        return MoveGenAttacks::bishopAttacks(from, allOcc) | MoveGenAttacks::rookAttacks(from, allOcc);
    });

    uint64_t king = m_bitboards[us][MoveGenConstants::KING_IDX];
    if (king) {
        const int from = MoveGenAttacks::lsbIndex(king);
        uint64_t targets = MoveGenAttacks::kingAttacks(from) & oppOcc;
        while (targets) {
            const int to = MoveGenAttacks::popLsb(targets);
            Move m = MoveGenAttacks::buildMove(from, to, PieceType::King);
            m.isCapture = true;
            moves.push_back(m);
        }
    }

    return moves;
}
