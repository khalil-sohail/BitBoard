#include "time/time_management.hpp"

#include "tuning/generated_tuning_values.hpp"

#include <algorithm>
#include <cstdlib>

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

    result.safeTimeLeftMs = std::max(
        1LL,
        timeLeftMs - static_cast<long long>(TIME_TUNING.allocation.safetyReserveMs)
    );

    result.expectedMovesRemaining = std::max(
        TIME_TUNING.allocation.expectedMovesFloor,
        TIME_TUNING.allocation.expectedMovesBase - moveNumber
    );
    if (movesToGo.has_value()) {
        result.expectedMovesRemaining = *movesToGo;
    }

    result.allocatedBeforeCapMs =
        (result.safeTimeLeftMs / result.expectedMovesRemaining) + incrementMs;

    if (hasTwoScores &&
        std::abs(lastSearchScore - previousSearchScore) >
            TIME_TUNING.allocation.instabilityThresholdCp) {
        const auto& multiplier = TIME_TUNING.allocation.instabilityMultiplier;
        const double multiplierValue =
            static_cast<double>(multiplier.numerator) /
            static_cast<double>(multiplier.denominator);
        result.allocatedBeforeCapMs = static_cast<long long>(
            result.allocatedBeforeCapMs * multiplierValue
        );
        result.instabilityApplied = true;
    }

    result.maximumCapMs = std::max(
        1LL,
        result.safeTimeLeftMs /
            static_cast<long long>(TIME_TUNING.allocation.maximumClockFraction.denominator)
    );

    result.timeLimitMs = std::max(
        static_cast<long long>(TIME_TUNING.allocation.minimumMoveTimeMs),
        std::min(result.allocatedBeforeCapMs, result.maximumCapMs)
    );

    if (result.safeTimeLeftMs < TIME_TUNING.stopPolicy.criticalLowTimeThresholdMs) {
        result.timeLimitMs = std::max(
            1LL,
            result.safeTimeLeftMs -
                static_cast<long long>(TIME_TUNING.stopPolicy.criticalLowTimeReserveMs)
        );
    }

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
