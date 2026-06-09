#include "move/move_constants.hpp"
#include "move/move_hash.hpp"

namespace MoveHash {

const uint64_t kPolyglotRandom[781] = {
#include "polyglot_random_values.inc"
};

const uint64_t SIDE_TO_MOVE_HASH = kPolyglotRandom[780];

uint64_t squareMask(int square) {
    return 1ULL << square;
}

int colorToPolyglotPivot(Color color) {
    return (color == Color::Black) ? 0 : 1;
}

uint64_t pieceHash(Color color, PieceType piece, int square) {
    const int polyPieceIndex = static_cast<int>(piece) * 2 + colorToPolyglotPivot(color);
    const int randomIndex = 64 * polyPieceIndex + square;
    return kPolyglotRandom[randomIndex];
}

uint64_t castlingHash(uint8_t rights) {
    uint64_t key = 0ULL;
    if (rights & MoveConstants::CASTLE_WK) key ^= kPolyglotRandom[768];
    if (rights & MoveConstants::CASTLE_WQ) key ^= kPolyglotRandom[769];
    if (rights & MoveConstants::CASTLE_BK) key ^= kPolyglotRandom[770];
    if (rights & MoveConstants::CASTLE_BQ) key ^= kPolyglotRandom[771];
    return key;
}

uint64_t enPassantHash(Color sideToMove,
                       int enPassantSquare,
                       const std::array<std::array<uint64_t, static_cast<size_t>(PieceType::Count)>, 2>& bitboards) {
    if (enPassantSquare < 0 || enPassantSquare >= 64) {
        return 0ULL;
    }

    uint64_t epMask = 1ULL << enPassantSquare;
    if (sideToMove == Color::White) {
        epMask >>= 8;
    } else {
        epMask <<= 8;
    }

    const uint64_t adjacentPawns =
        ((epMask & ~Board::FILE_A) >> 1) |
        ((epMask & ~Board::FILE_H) << 1);

    const int us = static_cast<int>(sideToMove);
    const uint64_t ownPawns = bitboards[us][MoveConstants::PAWN_IDX];
    if ((adjacentPawns & ownPawns) == 0ULL) {
        return 0ULL;
    }

    const int epFile = enPassantSquare % 8;
    return kPolyglotRandom[772 + epFile];
}

} // namespace MoveHash