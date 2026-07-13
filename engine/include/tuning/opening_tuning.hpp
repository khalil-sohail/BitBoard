#ifndef TUNING_OPENING_TUNING_HPP
#define TUNING_OPENING_TUNING_HPP

#include <cstdint>

namespace Tuning {

enum class BookSelectionMode {
    Weighted,
    Best,
    TopNWeighted,
};

struct OpeningTuning {
    bool enabled = true;
    int depthPlies = 0;
    BookSelectionMode selectionMode = BookSelectionMode::Weighted;
    unsigned int selectionTopN = 1;
    std::uint32_t seed = 0;
};

} // namespace Tuning

#endif
