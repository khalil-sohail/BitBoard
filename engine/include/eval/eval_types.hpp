#ifndef EVAL_TYPES_HPP
#define EVAL_TYPES_HPP

#include <cstdint>

namespace Eval {

struct TaperTerms {
    int mg = 0;
    int eg = 0;
};

struct PieceScoreDelta {
    int mg = 0;
    int eg = 0;
    int phase = 0;
};

[[nodiscard]] constexpr int mirrorSquare(int square) noexcept {
    return square ^ 56;
}

[[nodiscard]] constexpr int fileOf(int square) noexcept {
    return square & 7;
}

[[nodiscard]] constexpr uint64_t squareMask(int square) noexcept {
    return 1ULL << square;
}

[[nodiscard]] inline int lsbIndex(uint64_t bb) noexcept {
    return __builtin_ctzll(bb);
}

} // namespace Eval

#endif
