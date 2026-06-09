#include "board.hpp"
#include "move/movegen_attacks.hpp"
#include "move/movegen_constants.hpp"

std::vector<Move> Board::generatePseudoLegalMoves() const {
    std::vector<Move> moves;

    const Color usColor = m_sideToMove;
    const Color themColor = (usColor == Color::White) ? Color::Black : Color::White;
    const int us = static_cast<int>(usColor);

    const uint64_t ownOcc = occupancy(usColor);
    const uint64_t oppOcc = occupancy(themColor);
    const uint64_t allOcc = ownOcc | oppOcc;
    const uint64_t empty = ~allOcc;

    const uint64_t pawns = m_bitboards[us][MoveGenConstants::PAWN_IDX];
    if (usColor == Color::White) {
        uint64_t singlePush = (pawns << 8) & empty;
        uint64_t promoPush = singlePush & Board::RANK_8;
        uint64_t normalPush = singlePush & ~Board::RANK_8;

        while (normalPush) {
            const int to = MoveGenAttacks::popLsb(normalPush);
            moves.push_back(MoveGenAttacks::buildMove(to - 8, to, PieceType::Pawn));
        }

        while (promoPush) {
            const int to = MoveGenAttacks::popLsb(promoPush);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = MoveGenAttacks::buildMove(to - 8, to, PieceType::Pawn);
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        uint64_t dbl = (((pawns & Board::RANK_2) << 8) & empty);
        dbl = (dbl << 8) & empty & Board::RANK_4;
        while (dbl) {
            const int to = MoveGenAttacks::popLsb(dbl);
            Move m = MoveGenAttacks::buildMove(to - 16, to, PieceType::Pawn);
            m.isDoublePush = true;
            moves.push_back(m);
        }

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
        uint64_t singlePush = (pawns >> 8) & empty;
        uint64_t promoPush = singlePush & Board::RANK_1;
        uint64_t normalPush = singlePush & ~Board::RANK_1;

        while (normalPush) {
            const int to = MoveGenAttacks::popLsb(normalPush);
            moves.push_back(MoveGenAttacks::buildMove(to + 8, to, PieceType::Pawn));
        }

        while (promoPush) {
            const int to = MoveGenAttacks::popLsb(promoPush);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = MoveGenAttacks::buildMove(to + 8, to, PieceType::Pawn);
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        uint64_t dbl = (((pawns & Board::RANK_7) >> 8) & empty);
        dbl = (dbl >> 8) & empty & Board::RANK_5;
        while (dbl) {
            const int to = MoveGenAttacks::popLsb(dbl);
            Move m = MoveGenAttacks::buildMove(to + 16, to, PieceType::Pawn);
            m.isDoublePush = true;
            moves.push_back(m);
        }

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

    auto addMovesFromMask = [&](uint64_t pieces, PieceType piece, auto attacksFn) {
        uint64_t bb = pieces;
        while (bb) {
            const int from = MoveGenAttacks::popLsb(bb);
            uint64_t targets = attacksFn(from) & ~ownOcc;
            while (targets) {
                const int to = MoveGenAttacks::popLsb(targets);
                Move m = MoveGenAttacks::buildMove(from, to, piece);
                m.isCapture = (oppOcc & MoveGenAttacks::squareMask(to)) != 0ULL;
                moves.push_back(m);
            }
        }
    };

    addMovesFromMask(m_bitboards[us][MoveGenConstants::KNIGHT_IDX], PieceType::Knight, [&](int from) {
        return MoveGenAttacks::knightAttacks(from);
    });

    addMovesFromMask(m_bitboards[us][MoveGenConstants::BISHOP_IDX], PieceType::Bishop, [&](int from) {
        return MoveGenAttacks::bishopAttacks(from, allOcc);
    });

    addMovesFromMask(m_bitboards[us][MoveGenConstants::ROOK_IDX], PieceType::Rook, [&](int from) {
        return MoveGenAttacks::rookAttacks(from, allOcc);
    });

    addMovesFromMask(m_bitboards[us][MoveGenConstants::QUEEN_IDX], PieceType::Queen, [&](int from) {
        return MoveGenAttacks::bishopAttacks(from, allOcc) | MoveGenAttacks::rookAttacks(from, allOcc);
    });

    uint64_t king = m_bitboards[us][MoveGenConstants::KING_IDX];
    if (king) {
        const int from = MoveGenAttacks::lsbIndex(king);
        uint64_t targets = MoveGenAttacks::kingAttacks(from) & ~ownOcc;
        while (targets) {
            const int to = MoveGenAttacks::popLsb(targets);
            Move m = MoveGenAttacks::buildMove(from, to, PieceType::King);
            m.isCapture = (oppOcc & MoveGenAttacks::squareMask(to)) != 0ULL;
            moves.push_back(m);
        }

        if (usColor == Color::White && from == 4) {
            if ((m_castlingRights & MoveGenConstants::CASTLE_WK) &&
                !(allOcc & (MoveGenAttacks::squareMask(5) | MoveGenAttacks::squareMask(6))) &&
                !isSquareAttacked(4, Color::Black) && !isSquareAttacked(5, Color::Black) &&
                !isSquareAttacked(6, Color::Black)) {
                Move m = MoveGenAttacks::buildMove(4, 6, PieceType::King);
                m.isKingSideCastle = true;
                moves.push_back(m);
            }
            if ((m_castlingRights & MoveGenConstants::CASTLE_WQ) &&
                !(allOcc & (MoveGenAttacks::squareMask(1) | MoveGenAttacks::squareMask(2) |
                            MoveGenAttacks::squareMask(3))) &&
                !isSquareAttacked(4, Color::Black) && !isSquareAttacked(3, Color::Black) &&
                !isSquareAttacked(2, Color::Black)) {
                Move m = MoveGenAttacks::buildMove(4, 2, PieceType::King);
                m.isQueenSideCastle = true;
                moves.push_back(m);
            }
        }
        if (usColor == Color::Black && from == 60) {
            if ((m_castlingRights & MoveGenConstants::CASTLE_BK) &&
                !(allOcc & (MoveGenAttacks::squareMask(61) | MoveGenAttacks::squareMask(62))) &&
                !isSquareAttacked(60, Color::White) && !isSquareAttacked(61, Color::White) &&
                !isSquareAttacked(62, Color::White)) {
                Move m = MoveGenAttacks::buildMove(60, 62, PieceType::King);
                m.isKingSideCastle = true;
                moves.push_back(m);
            }
            if ((m_castlingRights & MoveGenConstants::CASTLE_BQ) &&
                !(allOcc & (MoveGenAttacks::squareMask(57) | MoveGenAttacks::squareMask(58) |
                            MoveGenAttacks::squareMask(59))) &&
                !isSquareAttacked(60, Color::White) && !isSquareAttacked(59, Color::White) &&
                !isSquareAttacked(58, Color::White)) {
                Move m = MoveGenAttacks::buildMove(60, 58, PieceType::King);
                m.isQueenSideCastle = true;
                moves.push_back(m);
            }
        }
    }

    return moves;
}
