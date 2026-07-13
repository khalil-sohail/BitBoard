#include "search/search_internal.hpp"
#include "tuning/active_tuning_values.hpp"
#include <algorithm>
#include <cmath>

namespace {

constexpr const auto& SEARCH_TUNING = Tuning::Generated::VALUES.search;
constexpr const auto& TIME_TUNING = Tuning::Generated::VALUES.time;

}

namespace SearchInternal {

std::vector<SearchTypes::TTEntry> g_TT(SearchConstants::TT_SIZE);
std::array<std::array<Move, 2>, SearchConstants::MAX_PLY> g_killerMoves{};
std::array<std::array<std::array<int, 64>, 64>, 2> g_historyTable{};
std::array<std::array<Move, 64>, 64> g_countermoveTable{};
std::array<Move, SearchConstants::MAX_PLY> g_movesPlayed{};
uint64_t g_nodesSearched = 0;

std::array<std::array<int, 64>, 64> LMR_TABLE{};

void initLMR() {
    const auto& baseRatio = SEARCH_TUNING.lateMoveReduction.base;
    const double base = static_cast<double>(baseRatio.numerator) /
                        static_cast<double>(baseRatio.denominator);
    for (int depth = 0; depth < 64; ++depth) {
        for (int moveCount = 0; moveCount < 64; ++moveCount) {
            if (depth > 0 && moveCount > 0) {
                int reduction = static_cast<int>(base + std::log(depth) * std::log(moveCount));
                LMR_TABLE[depth][moveCount] = std::max(1, std::min(reduction, depth - 1));
            } else {
                LMR_TABLE[depth][moveCount] = 1;
            }
        }
    }
}

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
    for (int from = 0; from < 64; ++from) {
        for (int to = 0; to < 64; ++to) {
            g_countermoveTable[from][to] = Move{};
        }
    }
}

bool shouldAbortSearch() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return true;
    }

    ++g_nodesSearched;
    std::uint64_t pollingMask = TIME_TUNING.polling.nodeMask;
    if (deadlineActive.load(std::memory_order_relaxed)) {
        const long long budget = allocatedTimeMs.load(std::memory_order_relaxed);
        if (budget <= 10) pollingMask = std::min<std::uint64_t>(pollingMask, 63ULL);
        else if (budget <= 50) pollingMask = std::min<std::uint64_t>(pollingMask, 127ULL);
        else if (budget <= 500) pollingMask = std::min<std::uint64_t>(pollingMask, 255ULL);
        else if (budget <= 5'000) pollingMask = std::min<std::uint64_t>(pollingMask, 1023ULL);
    }
    if ((g_nodesSearched & pollingMask) == 0ULL) {
        checkTime();
    }

    return timeAborted.load(std::memory_order_relaxed);
}

bool checkDeadlineBoundary() {
    if (!deadlineActive.load(std::memory_order_relaxed)) {
        return timeAborted.load(std::memory_order_relaxed);
    }
    checkTime();
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
std::atomic<bool> deadlineActive{false};
std::atomic<uint64_t> deadlineChecks{0};
std::atomic<long long> lastDeadlineCheckElapsedMs{0};
std::atomic<SearchStopReason> searchStopReason{SearchStopReason::None};

// Tracking for Complexity Extension (moved to app_uci.cpp)

void checkTime() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return;
    }

    const auto now = SearchInternal::SearchClock::now();
    const long long usedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    deadlineChecks.fetch_add(1, std::memory_order_relaxed);
    lastDeadlineCheckElapsedMs.store(usedMs, std::memory_order_relaxed);
    const long long budgetMs = allocatedTimeMs.load(std::memory_order_relaxed);
    // Millisecond clocks are truncated.  Begin unwinding one millisecond
    // before the advertised hard search budget so the completed command stays
    // within that budget instead of detecting expiry after it.
    const long long enforcementMs = budgetMs > 2 ? budgetMs - 2 : 0;
    if (usedMs >= enforcementMs) {
        searchStopReason.store(SearchStopReason::HardLimit, std::memory_order_relaxed);
        timeAborted.store(true, std::memory_order_relaxed);
    }
}

const char* searchStopReasonName(SearchStopReason reason) {
    switch (reason) {
        case SearchStopReason::SoftLimit: return "soft_limit";
        case SearchStopReason::HardLimit: return "hard_limit";
        case SearchStopReason::ImmediateMove: return "immediate_move";
        case SearchStopReason::CompletedDepth: return "completed_depth";
        case SearchStopReason::ExternalStop: return "external_stop";
        case SearchStopReason::Terminal: return "terminal";
        case SearchStopReason::None: return "none";
    }
    return "none";
}
