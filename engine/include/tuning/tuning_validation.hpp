#ifndef TUNING_TUNING_VALIDATION_HPP
#define TUNING_TUNING_VALIDATION_HPP

#include "tuning/engine_tuning.hpp"
#include "tuning/tuning_metadata.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace Tuning {

enum class TuningValidationCode {
    BelowMinimum,
    AboveMaximum,
    InvalidStep,
    InvalidRational,
    InvalidEnum,
    InvalidRelationship,
    InvalidStructuralValue,
};

struct TuningValidationError {
    TuningFieldId field;
    TuningValidationCode code;
};

struct TuningValidationResult {
    static constexpr std::size_t kMaxErrors = 64;

    std::array<TuningValidationError, kMaxErrors> errors{};
    std::size_t count = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return count == 0;
    }
};

[[nodiscard]] TuningValidationResult validateTuning(const EngineTuning& tuning);

[[nodiscard]] bool hasFieldNamed(std::string_view stableName) noexcept;
[[nodiscard]] bool tuningFieldNamesAreUnique() noexcept;
[[nodiscard]] bool tuningFieldsAreInRegistryOrder() noexcept;

} // namespace Tuning

#endif
