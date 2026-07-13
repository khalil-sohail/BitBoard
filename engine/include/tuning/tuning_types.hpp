#ifndef TUNING_TUNING_TYPES_HPP
#define TUNING_TUNING_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace Tuning {

inline constexpr std::size_t kPieceTypeCount = 6;
inline constexpr std::size_t kBoardSquareCount = 64;
inline constexpr std::size_t kRankTableSize = 9;
inline constexpr std::size_t kRookActivityCount = 3;
inline constexpr std::size_t kKingPressureTableSize = 9;
inline constexpr std::size_t kMopUpWeightCount = 7;

using PieceValueArray = std::array<int, kPieceTypeCount>;
using RankTuningTable = std::array<int, kRankTableSize>;
using KingPressureTable = std::array<int, kKingPressureTableSize>;
using MopUpWeightArray = std::array<int, kMopUpWeightCount>;
using RookActivityArray = std::array<int, kRookActivityCount>;
using PieceSquareTable = std::array<std::array<int, kBoardSquareCount>, kPieceTypeCount>;
using MvvLvaTable = std::array<std::array<int, kPieceTypeCount>, kPieceTypeCount>;

template <typename Rep = int>
struct RationalValue {
    Rep numerator = 0;
    Rep denominator = 1;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return denominator > 0;
    }

    [[nodiscard]] constexpr bool equals(Rep expectedNumerator, Rep expectedDenominator) const noexcept {
        return numerator == expectedNumerator && denominator == expectedDenominator;
    }
};

template <typename Rep = int>
struct ScaledValue {
    Rep value = 0;
    Rep scale = 1;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return scale > 0;
    }
};

enum class BoundsStatus {
    Verified,
    Provisional,
    Unknown,
};

enum class RuntimeMutability {
    RebuildOnly,
    RuntimeMutable,
};

enum class TuningRisk {
    Low,
    Medium,
    High,
};

enum class TuningCategory {
    EvaluationScalar,
    EvaluationArray,
    EvaluationPieceSquareTable,
    EvaluationPhase,
    SearchPruning,
    SearchReduction,
    SearchExtension,
    SearchMoveOrdering,
    SearchQuiescence,
    TimeAllocation,
    TimeDeadline,
    TimePolling,
    OpeningSelection,
    OpeningRandomness,
};

enum class TuningValueType {
    Integer,
    UnsignedInteger,
    ScaledInteger,
    Enum,
    Boolean,
    IntegerArray,
};

enum class MappingEquivalence {
    CompileTimeEquivalence,
    RuntimeTestEquivalence,
    StructurallyRepresentedOnly,
};

struct IntegerBounds {
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    BoundsStatus status = BoundsStatus::Unknown;

    [[nodiscard]] constexpr bool contains(int value) const noexcept {
        if (status == BoundsStatus::Unknown) {
            return true;
        }
        return step > 0 && value >= minimum && value <= maximum && ((value - minimum) % step) == 0;
    }
};

} // namespace Tuning

#endif
