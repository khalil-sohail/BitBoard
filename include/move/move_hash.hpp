#ifndef MOVE_MOVE_HASH_HPP
#define MOVE_MOVE_HASH_HPP

#include "board.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MoveHash {

extern const uint64_t kPolyglotRandom[781];
extern const uint64_t SIDE_TO_MOVE_HASH;

uint64_t squareMask(int square);
int colorToPolyglotPivot(Color color);
uint64_t pieceHash(Color color, PieceType piece, int square);
uint64_t castlingHash(uint8_t rights);

uint64_t enPassantHash(
    Color sideToMove,
    int enPassantSquare,
    const std::array<std::array<uint64_t, static_cast<size_t>(PieceType::Count)>, 2>& bitboards
);

} // namespace MoveHash

#endif