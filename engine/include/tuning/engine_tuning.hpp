#ifndef TUNING_ENGINE_TUNING_HPP
#define TUNING_ENGINE_TUNING_HPP

#include "eval/eval_weights.hpp"
#include "search/search_constants.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace EngineTuning {

inline constexpr std::string_view DEFAULT_PROFILE_ID = "builtin-default-v1";

enum class ParameterType {
    Integer,
    Boolean,
};

enum class ParameterCategory {
    Search,
    Time,
    Evaluation,
    Opening,
};

enum class RestartRequirement {
    None,
    NewSearch,
    NewGame,
    EngineRestart,
};

struct IntParameterDefinition {
    std::string_view name;
    int defaultValue;
    int minimum;
    int maximum;
    int step;
    ParameterCategory category;
    RestartRequirement restart;

    [[nodiscard]] constexpr bool isValid(int value) const noexcept {
        return value >= minimum && value <= maximum && ((value - minimum) % step) == 0;
    }
};

struct BoolParameterDefinition {
    std::string_view name;
    bool defaultValue;
    ParameterCategory category;
    RestartRequirement restart;
};

inline constexpr std::array<IntParameterDefinition, 8> SEARCH_PARAMETERS = {{
    {"search.aspirationWindowCp", SearchConstants::ASPIRATION_WINDOW_SIZE, 0, 500, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
    {"search.nullMoveReduction", SearchConstants::NULL_MOVE_REDUCTION, 0, 6, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
    {"search.deltaPruningMarginCp", SearchConstants::DELTA_PRUNING_MARGIN, 0, 1000, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
    {"search.reverseFutilityMarginCp", SearchConstants::REVERSE_FUTILITY_MARGIN, 0, 1000, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
    {"search.forwardFutilityMarginCp", SearchConstants::FORWARD_FUTILITY_MARGIN, 0, 1000, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
    {"search.maxPly", SearchConstants::MAX_PLY, 1, 128, 1, ParameterCategory::Search, RestartRequirement::EngineRestart},
    {"search.timeCheckNodeIntervalMask", static_cast<int>(SearchConstants::TIME_CHECK_MASK), 0, 1'048'575, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
    {"search.multiPvDefault", 1, 1, 8, 1, ParameterCategory::Search, RestartRequirement::NewSearch},
}};

inline constexpr std::array<IntParameterDefinition, 10> TIME_PARAMETERS = {{
    {"time.safetyReserveMs", 30, 0, 1000, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.estimatedMovesFloor", 20, 1, 80, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.estimatedMovesBase", 40, 1, 120, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.maximumClockFractionDenominator", 4, 1, 20, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.incrementContributionPercent", 100, 0, 200, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.instabilityThresholdCp", 50, 0, 500, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.instabilityMultiplierPercent", 130, 100, 300, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.softLimitInitialPercent", 50, 1, 100, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.softLimitUnstablePercent", 80, 1, 100, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
    {"time.minimumMoveTimeMs", 10, 1, 1000, 1, ParameterCategory::Time, RestartRequirement::NewSearch},
}};

inline const std::array<IntParameterDefinition, 22> EVALUATION_PARAMETERS = {{
    {"eval.pawnMg", EvalWeights::MG_VALUE[0], 0, 500, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.knightMg", EvalWeights::MG_VALUE[1], 0, 1000, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.bishopMg", EvalWeights::MG_VALUE[2], 0, 1000, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.rookMg", EvalWeights::MG_VALUE[3], 0, 1500, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.queenMg", EvalWeights::MG_VALUE[4], 0, 2500, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.pawnEg", EvalWeights::EG_VALUE[0], 0, 500, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.knightEg", EvalWeights::EG_VALUE[1], 0, 1000, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.bishopEg", EvalWeights::EG_VALUE[2], 0, 1000, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.rookEg", EvalWeights::EG_VALUE[3], 0, 1500, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.queenEg", EvalWeights::EG_VALUE[4], 0, 2500, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.bishopPairMg", EvalWeights::BISHOP_PAIR_BONUS_MG, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.bishopPairEg", EvalWeights::BISHOP_PAIR_BONUS_EG, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.doubledPawnPenalty", EvalWeights::PAWN_STRUCTURE_DOUBLED_PENALTY, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.isolatedPawnPenalty", EvalWeights::PAWN_STRUCTURE_ISOLATED_PENALTY, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.pawnIslandPenaltyMg", EvalWeights::PAWN_ISLAND_PENALTY_MG, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.pawnIslandPenaltyEg", EvalWeights::PAWN_ISLAND_PENALTY_EG, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.passedPawnCountBonusMg", EvalWeights::PASSED_PAWN_COUNT_BONUS_MG, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.passedPawnCountBonusEg", EvalWeights::PASSED_PAWN_COUNT_BONUS_EG, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.kingShieldPerPawnBonus", EvalWeights::KING_SHIELD_PER_PAWN_BONUS, 0, 300, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.tempo", 0, -100, 100, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.taperScale", EvalWeights::TAPER_SCALE, 1, 512, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
    {"eval.lateEndgamePhaseMax", EvalWeights::LATE_ENDGAME_PHASE_MAX, 0, 24, 1, ParameterCategory::Evaluation, RestartRequirement::NewSearch},
}};

inline constexpr std::array<IntParameterDefinition, 3> OPENING_PARAMETERS = {{
    {"opening.bookDepth", 30, 0, 100, 1, ParameterCategory::Opening, RestartRequirement::NewGame},
    {"opening.bookSelectionTopN", 4, 1, 32, 1, ParameterCategory::Opening, RestartRequirement::NewGame},
    {"opening.bookSeed", 1592594996, 0, 2147483647, 1, ParameterCategory::Opening, RestartRequirement::NewGame},
}};

inline constexpr std::array<BoolParameterDefinition, 1> OPENING_BOOLEAN_PARAMETERS = {{
    {"opening.enabled", true, ParameterCategory::Opening, RestartRequirement::NewGame},
}};

} // namespace EngineTuning

#endif
