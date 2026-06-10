#ifndef SEARCH_SEARCH_CONSTANTS_HPP
#define SEARCH_SEARCH_CONSTANTS_HPP

#include "board.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SearchConstants {

inline constexpr int INF_SCORE = 1'000'000'000;
inline constexpr int MATE_SCORE = 1'000'000;
inline constexpr int MAX_PLY = 64;
inline constexpr int NULL_MOVE_REDUCTION = 2;
inline constexpr int DELTA_PRUNING_MARGIN = 260;
inline constexpr int ASPIRATION_WINDOW_SIZE = 75;
inline constexpr uint64_t TIME_CHECK_MASK = 2047ULL;
inline constexpr size_t TT_SIZE = 1048576;
inline constexpr int TT_MOVE_SCORE = 1'000'000;

inline bool USE_OPENING_BOOK = true;
inline int MAX_BOOK_DEPTH = 30;

inline constexpr std::array<std::array<int, static_cast<size_t>(PieceType::Count)>, static_cast<size_t>(PieceType::Count)> MVV_LVA = {{
    {{105, 205, 305, 405, 505, 605}},
    {{104, 204, 304, 404, 504, 604}},
    {{103, 203, 303, 403, 503, 603}},
    {{102, 202, 302, 402, 502, 602}},
    {{101, 201, 301, 401, 501, 601}},
    {{100, 200, 300, 400, 500, 600}}
}};

inline constexpr std::array<int, static_cast<size_t>(PieceType::Count)> PIECE_VALUES = {
    100, 320, 330, 500, 900, 0
};

} // namespace SearchConstants

#endif