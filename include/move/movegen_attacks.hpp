#ifndef MOVE_MOVEGEN_ATTACKS_HPP
#define MOVE_MOVEGEN_ATTACKS_HPP

#include "board.hpp"

#include <cstdint>

namespace MoveGenAttacks {

inline int lsbIndex(uint64_t bb) {
    return __builtin_ctzll(bb);
}

inline int popLsb(uint64_t& bb) {
    const int sq = lsbIndex(bb);
    bb &= (bb - 1);
    return sq;
}

inline uint64_t squareMask(int square) {
    return 1ULL << square;
}

inline uint64_t knightAttacks(int square) {
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

inline uint64_t kingAttacks(int square) {
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

inline uint64_t bishopAttacks(int square, uint64_t occupied) {
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

inline uint64_t rookAttacks(int square, uint64_t occupied) {
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

inline Move buildMove(int from, int to, PieceType piece) {
    Move m;
    m.from = from;
    m.to = to;
    m.piece = piece;
    return m;
}

} // namespace MoveGenAttacks

#endif
