#include "time/time_management.hpp"

#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

constexpr const auto& TIME_TUNING = Tuning::Generated::VALUES.time;

}

namespace TimeManagement {

ClockBudget calculateClockBudget(
    long long timeLeftMs,
    long long incrementMs,
    int moveNumber,
    std::optional<int> movesToGo,
    bool hasTwoScores,
    int lastSearchScore,
    int previousSearchScore
) {
    ClockBudget result{};
    result.rawRemainingMs = std::max(0LL, timeLeftMs);
    result.transportReserveMs = std::min(
        result.rawRemainingMs,
        static_cast<long long>(TIME_TUNING.allocation.safetyReserveMs)
    );
    const long long afterTransport = result.rawRemainingMs - result.transportReserveMs;
    result.criticalLowTime =
        afterTransport < static_cast<long long>(TIME_TUNING.stopPolicy.criticalLowTimeThresholdMs);
    result.runtimeReserveMs = std::min(
        afterTransport,
        static_cast<long long>(TIME_TUNING.stopPolicy.criticalLowTimeReserveMs)
    );
    result.safeUsableMs = afterTransport - result.runtimeReserveMs;
    result.safeTimeLeftMs = result.safeUsableMs;

    result.expectedMovesRemaining = std::max(
        TIME_TUNING.allocation.expectedMovesFloor,
        TIME_TUNING.allocation.expectedMovesBase - moveNumber
    );
    if (movesToGo.has_value()) {
        result.expectedMovesRemaining = *movesToGo;
    }

    const long long safeIncrement = std::max(0LL, incrementMs);
    const long long baseAllocation = result.expectedMovesRemaining > 0
        ? result.safeUsableMs / result.expectedMovesRemaining
        : 0;
    result.allocatedBeforeCapMs = baseAllocation > std::numeric_limits<long long>::max() - safeIncrement
        ? std::numeric_limits<long long>::max()
        : baseAllocation + safeIncrement;

    if (hasTwoScores &&
        std::llabs(static_cast<long long>(lastSearchScore) - previousSearchScore) >
            TIME_TUNING.allocation.instabilityThresholdCp) {
        const auto& multiplier = TIME_TUNING.allocation.instabilityMultiplier;
        const long long denominator = std::max(1, multiplier.denominator);
        const long long numerator = std::max(0, multiplier.numerator);
        result.allocatedBeforeCapMs = result.allocatedBeforeCapMs >
                std::numeric_limits<long long>::max() / std::max(1LL, numerator)
            ? std::numeric_limits<long long>::max()
            : (result.allocatedBeforeCapMs * numerator) / denominator;
        result.instabilityApplied = true;
    }

    if (result.safeUsableMs <= 0) {
        result.immediateMove = true;
        result.maximumCapMs = 0;
        result.softBudgetMs = 0;
        result.hardBudgetMs = 0;
        result.timeLimitMs = 0;
        return result;
    }

    const long long capDenominator = std::max(
        1,
        TIME_TUNING.allocation.maximumClockFraction.denominator
    );
    result.maximumCapMs = std::max(1LL, result.safeUsableMs / capDenominator);
    const long long desired = std::max(
        static_cast<long long>(TIME_TUNING.allocation.minimumMoveTimeMs),
        result.allocatedBeforeCapMs
    );
    result.hardBudgetMs = std::min(result.maximumCapMs, desired);
    result.hardBudgetMs = std::min(result.hardBudgetMs, result.safeUsableMs);
    // A one-millisecond search budget cannot leave bounded time for search
    // unwinding and UCI response.  Treat it as an immediate-move request.
    if (result.hardBudgetMs <= 2) {
        result.immediateMove = true;
        result.softBudgetMs = 0;
        result.hardBudgetMs = 0;
        result.timeLimitMs = 0;
        return result;
    }
    const int stableNumerator = TIME_TUNING.stopPolicy.stableSoftStopFraction.numerator;
    const int stableDenominator = std::max(1, TIME_TUNING.stopPolicy.stableSoftStopFraction.denominator);
    result.softBudgetMs = result.hardBudgetMs * stableNumerator / stableDenominator;
    result.timeLimitMs = result.hardBudgetMs;

    return result;
}

StopDeadlines calculateStopDeadlines(long long allocatedTimeMs) {
    const int stablePercent =
        TIME_TUNING.stopPolicy.stableSoftStopFraction.numerator * 100 /
        TIME_TUNING.stopPolicy.stableSoftStopFraction.denominator;
    const int unstablePercent =
        TIME_TUNING.stopPolicy.unstableSoftStopFraction.numerator * 100 /
        TIME_TUNING.stopPolicy.unstableSoftStopFraction.denominator;

    return {
        .stableSoftMs = (allocatedTimeMs * stablePercent) / 100,
        .unstableSoftMs = (allocatedTimeMs * unstablePercent) / 100,
        .hardMs =
            allocatedTimeMs * TIME_TUNING.stopPolicy.hardStopFraction.numerator /
            TIME_TUNING.stopPolicy.hardStopFraction.denominator,
    };
}

} // namespace TimeManagement
