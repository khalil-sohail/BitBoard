#ifndef TUNING_TIME_TUNING_HPP
#define TUNING_TIME_TUNING_HPP

#include "tuning/tuning_types.hpp"

#include <cstdint>

namespace Tuning {

struct TimeAllocationTuning {
    int safetyReserveMs = 0;
    int minimumMoveTimeMs = 0;
    int expectedMovesBase = 0;
    int expectedMovesFloor = 0;
    RationalValue<int> incrementContribution{1, 1};
    int instabilityThresholdCp = 0;
    RationalValue<int> instabilityMultiplier{1, 1};
    RationalValue<int> maximumClockFraction{1, 1};
};

struct TimeStopPolicyTuning {
    RationalValue<int> stableSoftStopFraction{1, 1};
    RationalValue<int> unstableSoftStopFraction{1, 1};
    RationalValue<int> hardStopFraction{1, 1};
    int criticalLowTimeThresholdMs = 0;
    int criticalLowTimeReserveMs = 0;
};

struct TimePollingTuning {
    std::uint64_t nodeMask = 0;
};

struct TimeTuning {
    TimeAllocationTuning allocation;
    TimeStopPolicyTuning stopPolicy;
    TimePollingTuning polling;
};

} // namespace Tuning

#endif
