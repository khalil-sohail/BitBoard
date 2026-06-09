#ifndef EVAL_PARAM_REGISTRY_HPP
#define EVAL_PARAM_REGISTRY_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace EvalTuning {

/**
 * Represents a single tunable evaluation parameter.
 *
 * Each parameter holds a pointer to its live value in EvalWeights,
 * along with metadata for the optimizer (bounds, default, name).
 */
struct TunableParam {
    const char* name;     // Human-readable parameter name
    int* value;           // Pointer to the live weight in EvalWeights
    int defaultValue;     // Factory default for reset
    int minValue;         // Lower bound for optimizer
    int maxValue;         // Upper bound for optimizer
};

/**
 * Returns the global parameter registry.
 *
 * The registry is populated once during static initialization and
 * contains pointers to every tunable weight in EvalWeights.
 */
std::vector<TunableParam>& getParamRegistry();

/**
 * Returns the number of registered tunable parameters.
 */
size_t paramCount();

/**
 * Reset all registered parameters to their factory defaults.
 */
void resetToDefaults();

/**
 * Load tuned parameter values from a plain-text file.
 *
 * File format (one per line):
 *   PARAM_NAME = VALUE
 *
 * Lines starting with '#' or empty lines are ignored.
 * Unknown parameter names are silently skipped.
 *
 * Returns true if the file was read successfully.
 */
bool loadFromFile(const std::string& path);

/**
 * Save current parameter values to a plain-text file.
 *
 * Writes in the same format consumed by loadFromFile, with
 * the default value annotated in a trailing comment.
 *
 * Returns true if the file was written successfully.
 */
bool saveToFile(const std::string& path);

/**
 * Print all parameters and their current values to stdout.
 */
void printParams();

} // namespace EvalTuning

#endif
