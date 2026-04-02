#include "search/search_internal.hpp"

namespace SearchInternal {

std::vector<SearchTypes::TTEntry> g_TT(SearchConstants::TT_SIZE);
std::array<std::array<Move, 2>, SearchConstants::MAX_PLY> g_killerMoves{};
uint64_t g_nodesSearched = 0;

void clearTT() {
    g_TT.assign(SearchConstants::TT_SIZE, SearchTypes::TTEntry{});
}

void clearKillers() {
    for (auto& arr : g_killerMoves) {
        arr[0] = Move{};
        arr[1] = Move{};
    }
}

bool shouldAbortSearch() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return true;
    }

    ++g_nodesSearched;
    if ((g_nodesSearched & SearchConstants::TIME_CHECK_MASK) == 0ULL) {
        checkTime();
    }

    return timeAborted.load(std::memory_order_relaxed);
}

} // namespace SearchInternal

std::atomic<bool> timeAborted{false};
std::chrono::time_point<std::chrono::steady_clock> startTime;
long long allocatedTimeMs = 2000;
uint64_t qNodes = 0;
uint64_t deltaPruneSkips = 0;
uint64_t ttHits = 0;
uint64_t ttCutoffs = 0;
uint64_t ttStores = 0;

void checkTime() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return;
    }

    const auto now = SearchInternal::SearchClock::now();
    const long long usedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    if (usedMs >= allocatedTimeMs) {
        timeAborted.store(true, std::memory_order_relaxed);
    }
}