#ifndef SEARCH_SEARCH_CONSTANTS_HPP
#define SEARCH_SEARCH_CONSTANTS_HPP

#include <cstddef>

namespace SearchConstants {

inline constexpr int INF_SCORE = 1'000'000'000;
inline constexpr int MATE_SCORE = 1'000'000;
inline constexpr int MAX_PLY = 64;
inline size_t TT_SIZE = 1048576;
inline size_t TT_SIZE_MASK = 1048575; // TT_SIZE - 1, assuming power of two

inline int MULTI_PV = 1; // Number of principal variations to report (1 = standard)

} // namespace SearchConstants

#endif
