#ifndef EVAL_MASKS_HPP
#define EVAL_MASKS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace EvalMask {

struct EvalMasks {
    std::array<uint64_t, 8> FILE_MASKS{};
    std::array<std::array<uint64_t, 64>, 2> PASSED_PAWN_MASKS{};
    std::array<std::array<uint64_t, 64>, 2> KING_SHIELD_MASKS{};

    constexpr EvalMasks() noexcept {
        for (int i = 0; i < 8; ++i) {
            FILE_MASKS[static_cast<size_t>(i)] = 0x0101010101010101ULL << i;
        }

        for (int color = 0; color < 2; ++color) {
            for (int sq = 0; sq < 64; ++sq) {
                const int file = sq & 7;
                const int rank = sq >> 3;
                const bool isWhite = (color == 0);
                uint64_t mask = 0ULL;

                for (int df = -1; df <= 1; ++df) {
                    const int targetFile = file + df;
                    if (targetFile < 0 || targetFile > 7) {
                        continue;
                    }

                    if (isWhite) {
                        for (int targetRank = rank + 1; targetRank < 8; ++targetRank) {
                            mask |= (1ULL << (targetRank * 8 + targetFile));
                        }
                    } else {
                        for (int targetRank = rank - 1; targetRank >= 0; --targetRank) {
                            mask |= (1ULL << (targetRank * 8 + targetFile));
                        }
                    }
                }

                PASSED_PAWN_MASKS[static_cast<size_t>(color)][static_cast<size_t>(sq)] = mask;

                uint64_t shieldMask = 0ULL;
                const int startFile = std::max(0, file - 1);
                const int endFile = std::min(7, file + 1);

                for (int f = startFile; f <= endFile; ++f) {
                    if (isWhite) {
                        const int rank1 = rank + 1;
                        const int rank2 = rank + 2;
                        if (rank1 < 8) {
                            shieldMask |= (1ULL << (rank1 * 8 + f));
                        }
                        if (rank2 < 8) {
                            shieldMask |= (1ULL << (rank2 * 8 + f));
                        }
                    } else {
                        const int rank1 = rank - 1;
                        const int rank2 = rank - 2;
                        if (rank1 >= 0) {
                            shieldMask |= (1ULL << (rank1 * 8 + f));
                        }
                        if (rank2 >= 0) {
                            shieldMask |= (1ULL << (rank2 * 8 + f));
                        }
                    }
                }

                KING_SHIELD_MASKS[static_cast<size_t>(color)][static_cast<size_t>(sq)] = shieldMask;
            }
        }
    }
};

extern const EvalMasks MASKS;

} // namespace EvalMask

#endif
