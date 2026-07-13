#ifndef TIME_TIME_MANAGEMENT_HPP
#define TIME_TIME_MANAGEMENT_HPP

#include <optional>

namespace TimeManagement {

struct ClockBudget {
    long long safeTimeLeftMs = 0;
    int expectedMovesRemaining = 0;
    long long allocatedBeforeCapMs = 0;
    long long maximumCapMs = 0;
    long long timeLimitMs = 0;
    bool instabilityApplied = false;
};

struct StopDeadlines {
    long long stableSoftMs = 0;
    long long unstableSoftMs = 0;
    long long hardMs = 0;
};

[[nodiscard]] ClockBudget calculateClockBudget(
    long long timeLeftMs,
    long long incrementMs,
    int moveNumber,
    std::optional<int> movesToGo,
    bool hasTwoScores,
    int lastSearchScore,
    int previousSearchScore
);

[[nodiscard]] StopDeadlines calculateStopDeadlines(long long allocatedTimeMs);

} // namespace TimeManagement

#endif
