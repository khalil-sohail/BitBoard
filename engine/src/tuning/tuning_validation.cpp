#include "tuning/tuning_validation.hpp"

#include <algorithm>

namespace Tuning {
namespace {

void addError(TuningValidationResult& result, TuningFieldId field, TuningValidationCode code) {
    if (result.count < TuningValidationResult::kMaxErrors) {
        result.errors[result.count++] = {field, code};
    }
}

void validateInt(
    TuningValidationResult& result,
    TuningFieldId field,
    int value,
    int minimum,
    int maximum,
    int step
) {
    if (value < minimum) {
        addError(result, field, TuningValidationCode::BelowMinimum);
        return;
    }
    if (value > maximum) {
        addError(result, field, TuningValidationCode::AboveMaximum);
        return;
    }
    if (step <= 0 || ((value - minimum) % step) != 0) {
        addError(result, field, TuningValidationCode::InvalidStep);
    }
}

template <std::size_t N>
void validateArray(
    TuningValidationResult& result,
    TuningFieldId field,
    const std::array<int, N>& values,
    const std::array<int, N>& minimum,
    const std::array<int, N>& maximum,
    int step
) {
    for (std::size_t i = 0; i < N; ++i) {
        validateInt(result, field, values[i], minimum[i], maximum[i], step);
    }
}

template <typename Rep>
void validateRational(TuningValidationResult& result, TuningFieldId field, RationalValue<Rep> value) {
    if (!value.valid()) {
        addError(result, field, TuningValidationCode::InvalidRational);
    }
}

bool isRecognized(BookSelectionMode mode) {
    return mode == BookSelectionMode::Weighted ||
           mode == BookSelectionMode::Best ||
           mode == BookSelectionMode::TopNWeighted;
}

} // namespace

TuningValidationResult validateTuning(const EngineTuning& tuning) {
    TuningValidationResult result{};

    validateInt(result, TuningFieldId::EvaluationBishopBadHeavyPenalty, tuning.evaluation.piecePlacement.badBishopHeavyPenalty, 0, 200, 1);
    validateInt(result, TuningFieldId::EvaluationBishopBadLightPenalty, tuning.evaluation.piecePlacement.badBishopLightPenalty, 0, 160, 1);
    validateInt(result, TuningFieldId::EvaluationBishopPairEg, tuning.evaluation.bishopPair.endgame, 0, 160, 1);
    validateInt(result, TuningFieldId::EvaluationBishopPairMg, tuning.evaluation.bishopPair.middlegame, 0, 150, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameLatePhaseMax, tuning.evaluation.endgame.latePhaseMax, 0, 24, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameMopUpEgMargin, tuning.evaluation.endgame.mopUpEgMargin, 0, 800, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameMopUpMaterialMargin, tuning.evaluation.endgame.mopUpMaterialMargin, 0, 1200, 1);
    validateArray(result, TuningFieldId::EvaluationEndgameMopUpWeights, tuning.evaluation.endgame.mopUpWeights, {0, 0, 0, 0, 0, 0, 0}, {80, 80, 80, 14, 80, 80, 80}, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameScaleMinorOnlyClearEdge, tuning.evaluation.endgame.scaleMinorOnlyClearEdge, 0, 128, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameScaleMinorOnlyNearEqual, tuning.evaluation.endgame.scaleMinorOnlyNearEqual, 0, 128, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameScaleOppositeBishopsLowPawns, tuning.evaluation.endgame.scaleOppositeBishopsLowPawns, 0, 128, 1);
    validateInt(result, TuningFieldId::EvaluationEndgameScaleOppositeBishopsMinPawns, tuning.evaluation.endgame.scaleOppositeBishopsMinPawns, 0, 128, 1);
    validateArray(result, TuningFieldId::EvaluationKingAttackPressure, tuning.evaluation.kingSafety.attackPressure, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 500, 500, 500, 500, 500, 500, 500, 500}, 1);
    validateInt(result, TuningFieldId::EvaluationKingShieldMaxPawns, tuning.evaluation.kingSafety.shieldMaxPawns, 0, 8, 1);
    validateInt(result, TuningFieldId::EvaluationKingShieldPerPawnBonus, tuning.evaluation.kingSafety.shieldPerPawnBonus, 0, 80, 1);
    validateInt(result, TuningFieldId::EvaluationKingUncastledCenterPenalty, tuning.evaluation.kingSafety.uncastledCenterPenalty, 0, 150, 1);
    validateInt(result, TuningFieldId::EvaluationKingUncastledLostRightsPenalty, tuning.evaluation.kingSafety.uncastledLostRightsPenalty, 0, 180, 1);
    validateArray(result, TuningFieldId::EvaluationMaterialEg, tuning.evaluation.material.endgame, {0, 0, 0, 0, 0, 0}, {500, 1000, 1000, 1500, 2500, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationMaterialMg, tuning.evaluation.material.middlegame, {0, 0, 0, 0, 0, 0}, {500, 1000, 1000, 1500, 2500, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationMobilityEg, tuning.evaluation.mobility.endgame, {0, 0, 0, 0, 0, 0}, {0, 30, 30, 30, 30, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationMobilityMg, tuning.evaluation.mobility.middlegame, {0, 0, 0, 0, 0, 0}, {0, 30, 30, 30, 30, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationPawnsBackwardByRankEg, tuning.evaluation.pawns.backwardEgByRank, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 120, 120, 120, 120, 120, 120, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationPawnsBackwardByRankMg, tuning.evaluation.pawns.backwardMgByRank, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 120, 120, 120, 120, 120, 120, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationPawnsCandidateByRankEg, tuning.evaluation.pawns.candidateEgByRank, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 120, 120, 120, 120, 120, 120, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationPawnsCandidateByRankMg, tuning.evaluation.pawns.candidateMgByRank, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 120, 120, 120, 120, 120, 120, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationPawnsConnectedByRankEg, tuning.evaluation.pawns.connectedEgByRank, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 120, 120, 120, 120, 120, 120, 0}, 1);
    validateArray(result, TuningFieldId::EvaluationPawnsConnectedByRankMg, tuning.evaluation.pawns.connectedMgByRank, {0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 120, 120, 120, 120, 120, 120, 0}, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsDoubledPenalty, tuning.evaluation.pawns.doubledPenalty, 0, 120, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsIslandPenaltyEg, tuning.evaluation.pawns.islandPenaltyEg, 0, 80, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsIslandPenaltyMg, tuning.evaluation.pawns.islandPenaltyMg, 0, 80, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsIsolatedPenalty, tuning.evaluation.pawns.isolatedPenalty, 0, 120, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsPassedBlockedDivisor, tuning.evaluation.pawns.passedBlockedDivisor, 1, 8, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsPassedCountBonusEg, tuning.evaluation.pawns.passedCountBonusEg, 0, 120, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsPassedCountBonusMg, tuning.evaluation.pawns.passedCountBonusMg, 0, 80, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsPassedEgMultiplier, tuning.evaluation.pawns.passedEgMultiplier, 1, 8, 1);
    validateInt(result, TuningFieldId::EvaluationPawnsPassedRankSquareMultiplier, tuning.evaluation.pawns.passedRankSquareMultiplier, 0, 20, 1);
    validateArray(result, TuningFieldId::EvaluationPhaseIncrements, tuning.evaluation.phase.increments, {0, 0, 0, 0, 0, 0}, {0, 4, 4, 8, 16, 0}, 1);
    if (!tuning.evaluation.pieceSquare.middlegameRepresented) {
        addError(result, TuningFieldId::EvaluationPstMgPesto, TuningValidationCode::InvalidStructuralValue);
    }
    if (!tuning.evaluation.pieceSquare.endgameRepresented) {
        addError(result, TuningFieldId::EvaluationPstEgPesto, TuningValidationCode::InvalidStructuralValue);
    }
    validateInt(result, TuningFieldId::EvaluationQueenEarlyUndevelopedMinorPenalty, tuning.evaluation.piecePlacement.earlyQueenUndevelopedMinorPenalty, 0, 150, 1);
    validateInt(result, TuningFieldId::EvaluationRookTrappedPenalty, tuning.evaluation.piecePlacement.trappedRookPenalty, 0, 120, 1);
    validateArray(result, TuningFieldId::EvaluationRookActivityEg, tuning.evaluation.rookActivity.endgameArray(), {0, 0, 0}, {120, 120, 120}, 1);
    validateArray(result, TuningFieldId::EvaluationRookActivityMg, tuning.evaluation.rookActivity.middlegameArray(), {0, 0, 0}, {120, 120, 120}, 1);
    validateInt(result, TuningFieldId::EvaluationTaperScale, tuning.evaluation.endgame.taperScale, 1, 512, 1);

    validateInt(result, TuningFieldId::OpeningBookDepth, tuning.opening.depthPlies, 0, 100, 1);
    if (!isRecognized(tuning.opening.selectionMode)) {
        addError(result, TuningFieldId::OpeningSelectionMode, TuningValidationCode::InvalidEnum);
    }
    if (tuning.opening.selectionTopN == 0) {
        addError(result, TuningFieldId::OpeningSelectionTopN, TuningValidationCode::BelowMinimum);
    }

    validateInt(result, TuningFieldId::SearchAspirationWindowCp, tuning.search.aspiration.windowCp, 0, 500, 1);
    validateInt(result, TuningFieldId::SearchFutilityForwardMarginPerDepthCp, tuning.search.futility.forwardMarginPerDepthCp, 0, 1000, 1);
    validateInt(result, TuningFieldId::SearchFutilityReverseMarginPerDepthCp, tuning.search.futility.reverseMarginPerDepthCp, 0, 1000, 1);
    validateInt(result, TuningFieldId::SearchHistoryCap, tuning.search.moveOrdering.historyLimit, 1024, 65536, 1);
    validateRational(result, TuningFieldId::SearchLmrBase, tuning.search.lateMoveReduction.base);
    validateInt(result, TuningFieldId::SearchNullMoveReduction, tuning.search.nullMove.reduction, 0, 6, 1);
    validateInt(result, TuningFieldId::SearchOrderingPromotionBase, tuning.search.moveOrdering.promotionBaseScore, 0, 10000, 1);
    validateInt(result, TuningFieldId::SearchOrderingTtMoveScore, tuning.search.moveOrdering.transpositionMoveScore, 1, 2000000, 1);
    validateInt(result, TuningFieldId::SearchQuiescenceDeltaPruningMarginCp, tuning.search.quiescence.deltaMarginCp, 0, 1000, 1);
    validateInt(result, TuningFieldId::SearchSingularMarginCp, tuning.search.singularExtension.marginCp, 0, 500, 1);

    validateInt(result, TuningFieldId::TimeCriticalLowTimeReserveMs, tuning.time.stopPolicy.criticalLowTimeReserveMs, 0, 200, 1);
    validateInt(result, TuningFieldId::TimeCriticalLowTimeThresholdMs, tuning.time.stopPolicy.criticalLowTimeThresholdMs, 1, 1000, 1);
    validateInt(result, TuningFieldId::TimeExpectedMovesBase, tuning.time.allocation.expectedMovesBase, 1, 120, 1);
    validateInt(result, TuningFieldId::TimeExpectedMovesFloor, tuning.time.allocation.expectedMovesFloor, 1, 80, 1);
    validateRational(result, TuningFieldId::TimeHardStopFraction, tuning.time.stopPolicy.hardStopFraction);
    validateRational(result, TuningFieldId::TimeInstabilityMultiplier, tuning.time.allocation.instabilityMultiplier);
    validateInt(result, TuningFieldId::TimeInstabilityThresholdCp, tuning.time.allocation.instabilityThresholdCp, 0, 500, 1);
    validateRational(result, TuningFieldId::TimeMaxClockFraction, tuning.time.allocation.maximumClockFraction);
    validateInt(result, TuningFieldId::TimeMinimumMoveTimeMs, tuning.time.allocation.minimumMoveTimeMs, 1, 1000, 1);
    if (tuning.time.polling.nodeMask > 1048575ULL) {
        addError(result, TuningFieldId::TimePollingNodeMask, TuningValidationCode::AboveMaximum);
    }
    validateInt(result, TuningFieldId::TimeSafetyReserveMs, tuning.time.allocation.safetyReserveMs, 0, 1000, 1);
    validateRational(result, TuningFieldId::TimeSoftStopStablePercent, tuning.time.stopPolicy.stableSoftStopFraction);
    validateRational(result, TuningFieldId::TimeSoftStopUnstablePercent, tuning.time.stopPolicy.unstableSoftStopFraction);

    const auto lhs = tuning.time.stopPolicy.stableSoftStopFraction.numerator * tuning.time.stopPolicy.unstableSoftStopFraction.denominator;
    const auto rhs = tuning.time.stopPolicy.unstableSoftStopFraction.numerator * tuning.time.stopPolicy.stableSoftStopFraction.denominator;
    if (tuning.time.stopPolicy.stableSoftStopFraction.valid() &&
        tuning.time.stopPolicy.unstableSoftStopFraction.valid() &&
        lhs > rhs) {
        addError(result, TuningFieldId::TimeSoftStopStablePercent, TuningValidationCode::InvalidRelationship);
    }

    const auto unstableVsHardLhs = tuning.time.stopPolicy.unstableSoftStopFraction.numerator * tuning.time.stopPolicy.hardStopFraction.denominator;
    const auto unstableVsHardRhs = tuning.time.stopPolicy.hardStopFraction.numerator * tuning.time.stopPolicy.unstableSoftStopFraction.denominator;
    if (tuning.time.stopPolicy.unstableSoftStopFraction.valid() &&
        tuning.time.stopPolicy.hardStopFraction.valid() &&
        unstableVsHardLhs < unstableVsHardRhs) {
        addError(result, TuningFieldId::TimeHardStopFraction, TuningValidationCode::InvalidRelationship);
    }

    return result;
}

bool hasFieldNamed(std::string_view stableName) noexcept {
    return std::any_of(TUNING_FIELDS.begin(), TUNING_FIELDS.end(), [&](const TuningFieldMetadata& field) {
        return field.stableName == stableName;
    });
}

bool tuningFieldNamesAreUnique() noexcept {
    for (std::size_t i = 0; i < TUNING_FIELDS.size(); ++i) {
        for (std::size_t j = i + 1; j < TUNING_FIELDS.size(); ++j) {
            if (TUNING_FIELDS[i].stableName == TUNING_FIELDS[j].stableName) {
                return false;
            }
        }
    }
    return true;
}

bool tuningFieldsAreInRegistryOrder() noexcept {
    for (std::size_t i = 1; i < TUNING_FIELDS.size(); ++i) {
        if (!(TUNING_FIELDS[i - 1].stableName < TUNING_FIELDS[i].stableName)) {
            return false;
        }
    }
    return true;
}

} // namespace Tuning
