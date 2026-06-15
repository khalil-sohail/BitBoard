#include "search/search_internal.hpp"

namespace SearchInternal {

std::vector<SearchTypes::TTEntry> g_TT(SearchConstants::TT_SIZE);
std::array<std::array<Move, 2>, SearchConstants::MAX_PLY> g_killerMoves{};
std::array<std::array<std::array<int, 64>, 64>, 2> g_historyTable{};
uint64_t g_nodesSearched = 0;

void clearTT() {
    g_TT.assign(SearchConstants::TT_SIZE, SearchTypes::TTEntry{});
}

void resizeTT(size_t mb) {
    // Clamp to 1MB - 32GB
    mb = std::max(size_t{1}, std::min(size_t{32768}, mb));
    
    size_t targetBytes = mb * 1024 * 1024;
    size_t targetEntries = targetBytes / sizeof(SearchTypes::TTEntry);
    
    // Find nearest power of two <= targetEntries
    size_t entries = 1;
    while ((entries << 1) <= targetEntries) {
        entries <<= 1;
    }
    
    // Safely free old memory
    std::vector<SearchTypes::TTEntry>().swap(g_TT);
    
    SearchConstants::TT_SIZE = entries;
    SearchConstants::TT_SIZE_MASK = entries - 1;
    
    g_TT.assign(SearchConstants::TT_SIZE, SearchTypes::TTEntry{});
}

void clearKillers() {
    for (auto& arr : g_killerMoves) {
        arr[0] = Move{};
        arr[1] = Move{};
    }
}

void clearHistory() {
    for (int color = 0; color < 2; ++color) {
        for (int from = 0; from < 64; ++from) {
            for (int to = 0; to < 64; ++to) {
                g_historyTable[color][from][to] = 0;
            }
        }
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
std::atomic<long long> allocatedTimeMs{2000};
std::atomic<uint64_t> qNodes{0};
std::atomic<uint64_t> deltaPruneSkips{0};
std::atomic<uint64_t> ttHits{0};
std::atomic<uint64_t> ttCutoffs{0};
std::atomic<uint64_t> ttStores{0};

// Tracking for Complexity Extension (moved to app_uci.cpp)

void checkTime() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return;
    }

    const auto now = SearchInternal::SearchClock::now();
    const long long usedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    if (usedMs >= allocatedTimeMs.load(std::memory_order_relaxed)) {
        timeAborted.store(true, std::memory_order_relaxed);
    }
}