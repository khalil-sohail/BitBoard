#include "board.hpp"

#include <cstdint>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);

constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;

inline int lsbIndex(uint64_t bb) {
    return __builtin_ctzll(bb);
}

constexpr uint64_t kPolyglotRandom[781] = {
#include "polyglot_random_values.inc"
};

} // namespace

uint64_t Board::computePolyglotHash() const {
    uint64_t hash = 0ULL;

    for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
        for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
            uint64_t bb = m_bitboards[color][piece];
            while (bb != 0ULL) {
                const int square = lsbIndex(bb);
                bb &= (bb - 1);

                const int colorPivot = (color == BLACK_IDX) ? 0 : 1;
                const int polyPieceIndex = piece * 2 + colorPivot;
                const int randomIndex = 64 * polyPieceIndex + square;
                hash ^= kPolyglotRandom[randomIndex];
            }
        }
    }

    if (m_castlingRights & CASTLE_WK) {
        hash ^= kPolyglotRandom[768];
    }
    if (m_castlingRights & CASTLE_WQ) {
        hash ^= kPolyglotRandom[769];
    }
    if (m_castlingRights & CASTLE_BK) {
        hash ^= kPolyglotRandom[770];
    }
    if (m_castlingRights & CASTLE_BQ) {
        hash ^= kPolyglotRandom[771];
    }

    if (m_enPassantSquare >= 0 && m_enPassantSquare < 64) {
        uint64_t epMask = 1ULL << m_enPassantSquare;
        if (m_sideToMove == Color::White) {
            epMask >>= 8;
        } else {
            epMask <<= 8;
        }

        const uint64_t adjacentPawns =
            ((epMask & ~Board::FILE_A) >> 1) |
            ((epMask & ~Board::FILE_H) << 1);

        const int us = static_cast<int>(m_sideToMove);
        const uint64_t ownPawns = m_bitboards[us][PAWN_IDX];

        if ((adjacentPawns & ownPawns) != 0ULL) {
            const int epFile = m_enPassantSquare % 8;
            hash ^= kPolyglotRandom[772 + epFile];
        }
    }

    if (m_sideToMove == Color::White) {
        hash ^= kPolyglotRandom[780];
    }

    return hash;
}

bool Board::isRepetition() const {
    const uint64_t currentHash = computePolyglotHash();
    for (auto it = m_hashHistory.rbegin(); it != m_hashHistory.rend(); ++it) {
        if (*it == currentHash) {
            return true;
        }
    }
    return false;
}
