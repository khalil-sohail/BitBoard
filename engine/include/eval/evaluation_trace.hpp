#ifndef EVAL_EVALUATION_TRACE_HPP
#define EVAL_EVALUATION_TRACE_HPP

#include <map>
#include <string>
#include <vector>

namespace Eval {

inline constexpr int EVALUATION_FEATURE_SCHEMA_VERSION = 1;
inline constexpr const char* EVALUATION_FEATURE_MODEL_VERSION = "bitboard-eval-features-v1";

struct FeatureValue {
    std::map<std::string, int> coefficients;
    int middlegameContribution = 0;
    int endgameContribution = 0;
    std::string classification = "linear";
};

struct WeightedTerm {
    std::string name;
    int middlegame = 0;
    int endgame = 0;
};

struct EvaluationFeatureTrace {
    int baseMiddlegameScore = 0;
    int baseEndgameScore = 0;
    int middlegameScore = 0;
    int endgameScore = 0;
    int phase = 0;
    int clampedPhase = 0;
    int taperedScore = 0;
    int noPawnScaledScore = 0;
    int lowMaterialScale = 0;
    int finalScore = 0;
    int sideToMoveScore = 0;
    bool insufficientMaterialDraw = false;
    std::map<std::string, FeatureValue> features;
    std::vector<WeightedTerm> terms;
};

} // namespace Eval

#endif
