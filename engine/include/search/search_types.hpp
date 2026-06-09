#ifndef SEARCH_SEARCH_TYPES_HPP
#define SEARCH_SEARCH_TYPES_HPP

#include "board.hpp"

#include <cstdint>

namespace SearchTypes {

enum class TTFlag {
    Exact,
    Alpha,
    Beta
};

struct TTEntry {
    uint64_t hash = 0ULL;
    int depth = 0;
    TTFlag flag = TTFlag::Exact;
    int score = 0;
    Move bestMove{};
};

} // namespace SearchTypes

#endif