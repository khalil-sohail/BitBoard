#ifndef TUNING_SEARCH_TUNING_HPP
#define TUNING_SEARCH_TUNING_HPP

#include "tuning/tuning_types.hpp"

namespace Tuning {

struct AspirationTuning {
    int windowCp = 0;
};

struct NullMoveTuning {
    int reduction = 0;
};

struct FutilityTuning {
    int reverseMarginPerDepthCp = 0;
    int forwardMarginPerDepthCp = 0;
};

struct LateMoveReductionTuning {
    RationalValue<int> base{};
};

struct QuiescenceTuning {
    int deltaMarginCp = 0;
};

struct SingularExtensionTuning {
    int marginCp = 0;
};

struct QuietMoveOrderingTuning {
    int firstKillerScore = 0;
    int secondKillerScore = 0;
    int counterMoveScore = 0;
};

struct CaptureOrderingTuning {
    int winningCaptureBaseScore = 0;
    int losingCaptureBaseScore = 0;
    int seeScoreMultiplier = 0;
};

struct MoveOrderingTuning {
    int transpositionMoveScore = 0;
    QuietMoveOrderingTuning quiet;
    CaptureOrderingTuning capture;
    int promotionBaseScore = 0;
    int historyLimit = 0;
    MvvLvaTable mvvLva{};
    PieceValueArray seePieceValues{};
};

struct SearchTuning {
    AspirationTuning aspiration;
    NullMoveTuning nullMove;
    FutilityTuning futility;
    LateMoveReductionTuning lateMoveReduction;
    QuiescenceTuning quiescence;
    SingularExtensionTuning singularExtension;
    MoveOrderingTuning moveOrdering;
};

} // namespace Tuning

#endif
