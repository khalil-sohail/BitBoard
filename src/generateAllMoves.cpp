#include "board.hpp"

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);
constexpr int KING_IDX = static_cast<int>(PieceType::King);

constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;

int lsbIndex(uint64_t bb) {
    return __builtin_ctzll(bb);
}

int popLsb(uint64_t& bb) {
    const int sq = lsbIndex(bb);
    bb &= (bb - 1);
    return sq;
}

uint64_t squareMask(int square) {
    return 1ULL << square;
}

uint64_t knightAttacks(int square) {
    const uint64_t k = squareMask(square);
    const uint64_t notA = ~Board::FILE_A;
    const uint64_t notH = ~Board::FILE_H;
    const uint64_t notAB = ~(Board::FILE_A | (Board::FILE_A << 1));
    const uint64_t notGH = ~(Board::FILE_H | (Board::FILE_H >> 1));

    return ((k << 17) & notA) |
           ((k << 15) & notH) |
           ((k << 10) & notAB) |
           ((k << 6) & notGH) |
           ((k >> 17) & notH) |
           ((k >> 15) & notA) |
           ((k >> 10) & notGH) |
           ((k >> 6) & notAB);
}

uint64_t kingAttacks(int square) {
    const uint64_t k = squareMask(square);
    const uint64_t notA = ~Board::FILE_A;
    const uint64_t notH = ~Board::FILE_H;

    return (k << 8) |
           (k >> 8) |
           ((k << 1) & notA) |
           ((k >> 1) & notH) |
           ((k << 9) & notA) |
           ((k << 7) & notH) |
           ((k >> 7) & notA) |
           ((k >> 9) & notH);
}

uint64_t bishopAttacks(int square, uint64_t occupied) {
    uint64_t attacks = 0ULL;
    const int r = square / 8;
    const int f = square % 8;

    for (int nr = r + 1, nf = f + 1; nr < 8 && nf < 8; ++nr, ++nf) {
        const int s = nr * 8 + nf;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }
    for (int nr = r + 1, nf = f - 1; nr < 8 && nf >= 0; ++nr, --nf) {
        const int s = nr * 8 + nf;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }
    for (int nr = r - 1, nf = f + 1; nr >= 0 && nf < 8; --nr, ++nf) {
        const int s = nr * 8 + nf;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }
    for (int nr = r - 1, nf = f - 1; nr >= 0 && nf >= 0; --nr, --nf) {
        const int s = nr * 8 + nf;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }

    return attacks;
}

uint64_t rookAttacks(int square, uint64_t occupied) {
    uint64_t attacks = 0ULL;
    const int r = square / 8;
    const int f = square % 8;

    for (int nr = r + 1; nr < 8; ++nr) {
        const int s = nr * 8 + f;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }
    for (int nr = r - 1; nr >= 0; --nr) {
        const int s = nr * 8 + f;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }
    for (int nf = f + 1; nf < 8; ++nf) {
        const int s = r * 8 + nf;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }
    for (int nf = f - 1; nf >= 0; --nf) {
        const int s = r * 8 + nf;
        attacks |= squareMask(s);
        if (occupied & squareMask(s)) break;
    }

    return attacks;
}

Move buildMove(int from, int to, PieceType piece) {
    Move m;
    m.from = from;
    m.to = to;
    m.piece = piece;
    return m;
}

} // namespace

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

    m_bitboards[WHITE_IDX][PAWN_IDX] = Board::RANK_2;
    m_bitboards[BLACK_IDX][PAWN_IDX] = Board::RANK_7;

    m_bitboards[WHITE_IDX][ROOK_IDX] = squareMask(0) | squareMask(7);
    m_bitboards[BLACK_IDX][ROOK_IDX] = squareMask(56) | squareMask(63);

    m_bitboards[WHITE_IDX][KNIGHT_IDX] = squareMask(1) | squareMask(6);
    m_bitboards[BLACK_IDX][KNIGHT_IDX] = squareMask(57) | squareMask(62);

    m_bitboards[WHITE_IDX][BISHOP_IDX] = squareMask(2) | squareMask(5);
    m_bitboards[BLACK_IDX][BISHOP_IDX] = squareMask(58) | squareMask(61);

    m_bitboards[WHITE_IDX][QUEEN_IDX] = squareMask(3);
    m_bitboards[BLACK_IDX][QUEEN_IDX] = squareMask(59);

    m_bitboards[WHITE_IDX][KING_IDX] = squareMask(4);
    m_bitboards[BLACK_IDX][KING_IDX] = squareMask(60);

    resetEvalStateFromBoard();
}

uint64_t Board::occupancy(Color color) const {
    uint64_t occ = 0ULL;
    const auto& pcs = m_bitboards[static_cast<int>(color)];
    for (size_t i = 0; i < static_cast<size_t>(PieceType::Count); ++i) {
        occ |= pcs[i];
    }
    return occ;
}

uint64_t Board::occupancyAll() const {
    return occupancy(Color::White) | occupancy(Color::Black);
}

bool Board::isSquareOccupied(int square) const {
    return (occupancyAll() & squareMask(square)) != 0ULL;
}

bool Board::hasPiece(Color color, PieceType piece, int square) const {
    return (m_bitboards[static_cast<int>(color)][static_cast<int>(piece)] & squareMask(square)) != 0ULL;
}

std::optional<std::pair<Color, PieceType>> Board::pieceAt(int square) const {
    const uint64_t mask = squareMask(square);
    for (int c = 0; c < 2; ++c) {
        for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
            if (m_bitboards[c][p] & mask) {
                return std::make_pair(static_cast<Color>(c), static_cast<PieceType>(p));
            }
        }
    }
    return std::nullopt;
}

bool Board::isSquareAttacked(int square, Color byColor) const {
    const int by = static_cast<int>(byColor);
    const uint64_t target = squareMask(square);
    const uint64_t occ = occupancyAll();

    const uint64_t pawns = m_bitboards[by][PAWN_IDX];
    uint64_t pawnAttacks = 0ULL;
    if (byColor == Color::White) {
        pawnAttacks |= (pawns << 7) & ~Board::FILE_H;
        pawnAttacks |= (pawns << 9) & ~Board::FILE_A;
    } else {
        pawnAttacks |= (pawns >> 7) & ~Board::FILE_A;
        pawnAttacks |= (pawns >> 9) & ~Board::FILE_H;
    }
    if (pawnAttacks & target) return true;

    uint64_t knights = m_bitboards[by][KNIGHT_IDX];
    while (knights) {
        const int from = popLsb(knights);
        if (knightAttacks(from) & target) return true;
    }

    uint64_t bishops = m_bitboards[by][BISHOP_IDX] | m_bitboards[by][QUEEN_IDX];
    while (bishops) {
        const int from = popLsb(bishops);
        if (bishopAttacks(from, occ) & target) return true;
    }

    uint64_t rooks = m_bitboards[by][ROOK_IDX] | m_bitboards[by][QUEEN_IDX];
    while (rooks) {
        const int from = popLsb(rooks);
        if (rookAttacks(from, occ) & target) return true;
    }

    uint64_t king = m_bitboards[by][KING_IDX];
    if (king && (kingAttacks(lsbIndex(king)) & target)) return true;

    return false;
}

bool Board::inCheck(Color color) const {
    const int c = static_cast<int>(color);
    const uint64_t king = m_bitboards[c][KING_IDX];
    if (!king) return false;
    const int kingSquare = lsbIndex(king);
    const Color enemy = (color == Color::White) ? Color::Black : Color::White;
    return isSquareAttacked(kingSquare, enemy);
}

std::vector<Move> Board::generatePseudoLegalMoves() const {
    std::vector<Move> moves;

    const Color usColor = m_sideToMove;
    const Color themColor = (usColor == Color::White) ? Color::Black : Color::White;
    const int us = static_cast<int>(usColor);

    const uint64_t ownOcc = occupancy(usColor);
    const uint64_t oppOcc = occupancy(themColor);
    const uint64_t allOcc = ownOcc | oppOcc;
    const uint64_t empty = ~allOcc;

    const uint64_t pawns = m_bitboards[us][PAWN_IDX];
    if (usColor == Color::White) {
        uint64_t singlePush = (pawns << 8) & empty;
        uint64_t promoPush = singlePush & Board::RANK_8;
        uint64_t normalPush = singlePush & ~Board::RANK_8;

        while (normalPush) {
            const int to = popLsb(normalPush);
            moves.push_back(buildMove(to - 8, to, PieceType::Pawn));
        }

        while (promoPush) {
            const int to = popLsb(promoPush);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = buildMove(to - 8, to, PieceType::Pawn);
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        uint64_t dbl = (((pawns & Board::RANK_2) << 8) & empty);
        dbl = (dbl << 8) & empty & Board::RANK_4;
        while (dbl) {
            const int to = popLsb(dbl);
            Move m = buildMove(to - 16, to, PieceType::Pawn);
            m.isDoublePush = true;
            moves.push_back(m);
        }

        uint64_t leftTargets = ((pawns & ~Board::FILE_A) << 7);
        uint64_t rightTargets = ((pawns & ~Board::FILE_H) << 9);

        uint64_t capLeft = leftTargets & oppOcc;
        uint64_t capRight = rightTargets & oppOcc;

        uint64_t capPromo = (capLeft | capRight) & Board::RANK_8;
        uint64_t capNormal = (capLeft | capRight) & ~Board::RANK_8;

        while (capNormal) {
            const int to = popLsb(capNormal);
            Move m = buildMove(to - 7, to, PieceType::Pawn);
            if (!hasPiece(Color::White, PieceType::Pawn, m.from)) {
                m.from = to - 9;
            }
            m.isCapture = true;
            moves.push_back(m);
        }

        while (capPromo) {
            const int to = popLsb(capPromo);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = buildMove(to - 7, to, PieceType::Pawn);
                if (!hasPiece(Color::White, PieceType::Pawn, m.from)) {
                    m.from = to - 9;
                }
                m.isCapture = true;
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        if (m_enPassantSquare != -1) {
            const uint64_t epMask = squareMask(m_enPassantSquare);
            uint64_t epLeft = ((pawns & ~Board::FILE_A) << 7) & epMask;
            uint64_t epRight = ((pawns & ~Board::FILE_H) << 9) & epMask;
            if (epLeft) {
                Move m = buildMove(m_enPassantSquare - 7, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
            if (epRight) {
                Move m = buildMove(m_enPassantSquare - 9, m_enPassantSquare, PieceType::Pawn);
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
            const int to = popLsb(normalPush);
            moves.push_back(buildMove(to + 8, to, PieceType::Pawn));
        }

        while (promoPush) {
            const int to = popLsb(promoPush);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = buildMove(to + 8, to, PieceType::Pawn);
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        uint64_t dbl = (((pawns & Board::RANK_7) >> 8) & empty);
        dbl = (dbl >> 8) & empty & Board::RANK_5;
        while (dbl) {
            const int to = popLsb(dbl);
            Move m = buildMove(to + 16, to, PieceType::Pawn);
            m.isDoublePush = true;
            moves.push_back(m);
        }

        uint64_t leftTargets = ((pawns & ~Board::FILE_A) >> 9);
        uint64_t rightTargets = ((pawns & ~Board::FILE_H) >> 7);

        uint64_t capLeft = leftTargets & oppOcc;
        uint64_t capRight = rightTargets & oppOcc;

        uint64_t capPromo = (capLeft | capRight) & Board::RANK_1;
        uint64_t capNormal = (capLeft | capRight) & ~Board::RANK_1;

        while (capNormal) {
            const int to = popLsb(capNormal);
            Move m = buildMove(to + 7, to, PieceType::Pawn);
            if (!hasPiece(Color::Black, PieceType::Pawn, m.from)) {
                m.from = to + 9;
            }
            m.isCapture = true;
            moves.push_back(m);
        }

        while (capPromo) {
            const int to = popLsb(capPromo);
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
                Move m = buildMove(to + 7, to, PieceType::Pawn);
                if (!hasPiece(Color::Black, PieceType::Pawn, m.from)) {
                    m.from = to + 9;
                }
                m.isCapture = true;
                m.promotion = promo;
                moves.push_back(m);
            }
        }

        if (m_enPassantSquare != -1) {
            const uint64_t epMask = squareMask(m_enPassantSquare);
            uint64_t epLeft = ((pawns & ~Board::FILE_A) >> 9) & epMask;
            uint64_t epRight = ((pawns & ~Board::FILE_H) >> 7) & epMask;
            if (epLeft) {
                Move m = buildMove(m_enPassantSquare + 9, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
            if (epRight) {
                Move m = buildMove(m_enPassantSquare + 7, m_enPassantSquare, PieceType::Pawn);
                m.isCapture = true;
                m.isEnPassant = true;
                moves.push_back(m);
            }
        }
    }

    auto addMovesFromMask = [&](uint64_t pieces, PieceType piece, auto attacksFn) {
        uint64_t bb = pieces;
        while (bb) {
            const int from = popLsb(bb);
            uint64_t targets = attacksFn(from) & ~ownOcc;
            while (targets) {
                const int to = popLsb(targets);
                Move m = buildMove(from, to, piece);
                m.isCapture = (oppOcc & squareMask(to)) != 0ULL;
                moves.push_back(m);
            }
        }
    };

    addMovesFromMask(m_bitboards[us][KNIGHT_IDX], PieceType::Knight, [&](int from) {
        return knightAttacks(from);
    });

    addMovesFromMask(m_bitboards[us][BISHOP_IDX], PieceType::Bishop, [&](int from) {
        return bishopAttacks(from, allOcc);
    });

    addMovesFromMask(m_bitboards[us][ROOK_IDX], PieceType::Rook, [&](int from) {
        return rookAttacks(from, allOcc);
    });

    addMovesFromMask(m_bitboards[us][QUEEN_IDX], PieceType::Queen, [&](int from) {
        return bishopAttacks(from, allOcc) | rookAttacks(from, allOcc);
    });

    uint64_t king = m_bitboards[us][KING_IDX];
    if (king) {
        const int from = lsbIndex(king);
        uint64_t targets = kingAttacks(from) & ~ownOcc;
        while (targets) {
            const int to = popLsb(targets);
            Move m = buildMove(from, to, PieceType::King);
            m.isCapture = (oppOcc & squareMask(to)) != 0ULL;
            moves.push_back(m);
        }

        if (usColor == Color::White && from == 4) {
            if ((m_castlingRights & CASTLE_WK) && !(allOcc & (squareMask(5) | squareMask(6))) &&
                !isSquareAttacked(4, Color::Black) && !isSquareAttacked(5, Color::Black) && !isSquareAttacked(6, Color::Black)) {
                Move m = buildMove(4, 6, PieceType::King);
                m.isKingSideCastle = true;
                moves.push_back(m);
            }
            if ((m_castlingRights & CASTLE_WQ) && !(allOcc & (squareMask(1) | squareMask(2) | squareMask(3))) &&
                !isSquareAttacked(4, Color::Black) && !isSquareAttacked(3, Color::Black) && !isSquareAttacked(2, Color::Black)) {
                Move m = buildMove(4, 2, PieceType::King);
                m.isQueenSideCastle = true;
                moves.push_back(m);
            }
        }
        if (usColor == Color::Black && from == 60) {
            if ((m_castlingRights & CASTLE_BK) && !(allOcc & (squareMask(61) | squareMask(62))) &&
                !isSquareAttacked(60, Color::White) && !isSquareAttacked(61, Color::White) && !isSquareAttacked(62, Color::White)) {
                Move m = buildMove(60, 62, PieceType::King);
                m.isKingSideCastle = true;
                moves.push_back(m);
            }
            if ((m_castlingRights & CASTLE_BQ) && !(allOcc & (squareMask(57) | squareMask(58) | squareMask(59))) &&
                !isSquareAttacked(60, Color::White) && !isSquareAttacked(59, Color::White) && !isSquareAttacked(58, Color::White)) {
                Move m = buildMove(60, 58, PieceType::King);
                m.isQueenSideCastle = true;
                moves.push_back(m);
            }
        }
    }

    return moves;
}

std::vector<Move> Board::generateLegalMoves() const {
    std::vector<Move> legal;
    std::vector<Move> pseudo = generatePseudoLegalMoves();
    legal.reserve(pseudo.size());

    for (const Move& mv : pseudo) {
        Board copy = *this;
        copy.makeMove(mv);
        if (!copy.inCheck(m_sideToMove)) {
            legal.push_back(mv);
        }
    }

    return legal;
}

uint64_t Board::perft(int depth) const {
    if (depth < 0) {
        return 0;
    }
    if (depth == 0) {
        return 1;
    }

    const std::vector<Move> legal = generateLegalMoves();
    if (depth == 1) {
        return static_cast<uint64_t>(legal.size());
    }

    uint64_t nodes = 0;
    for (const Move& mv : legal) {
        Board copy = *this;
        copy.makeMove(mv);
        nodes += copy.perft(depth - 1);
    }
    return nodes;
}