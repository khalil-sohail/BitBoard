#include "board.hpp"
#include "eval/eval_param_registry.hpp"
#include "eval/eval_weights.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ========================================================================
// Data Structures
// ========================================================================

struct LabeledPosition {
    std::string fen;
    double result; // 1.0 = white win, 0.5 = draw, 0.0 = black win
};

// ========================================================================
// Dataset Loading
// ========================================================================

/**
 * Parse the result string from an EPD c9 tag.
 * Returns the result from white's perspective: 1.0, 0.5, or 0.0.
 * Returns -1.0 on parse failure.
 */
static double parseResult(const std::string& resultStr) {
    if (resultStr.find("1-0") != std::string::npos) return 1.0;
    if (resultStr.find("0-1") != std::string::npos) return 0.0;
    if (resultStr.find("1/2") != std::string::npos) return 0.5;
    return -1.0;
}

/**
 * Load labeled positions from an EPD file.
 *
 * Expected format per line:
 *   <FEN> c9 "<result>";
 */
static std::vector<LabeledPosition> loadDataset(const std::string& path) {
    std::vector<LabeledPosition> positions;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "error: cannot open dataset: " << path << "\n";
        return positions;
    }

    std::string line;
    int lineNum = 0;
    int parseErrors = 0;

    while (std::getline(file, line)) {
        ++lineNum;
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find the c9 tag that separates FEN from result.
        auto c9pos = line.find("c9");
        if (c9pos == std::string::npos) {
            ++parseErrors;
            continue;
        }

        std::string fen = line.substr(0, c9pos);
        // Trim trailing whitespace from FEN.
        while (!fen.empty() && std::isspace(static_cast<unsigned char>(fen.back()))) {
            fen.pop_back();
        }

        std::string resultPart = line.substr(c9pos);
        double result = parseResult(resultPart);
        if (result < 0.0) {
            ++parseErrors;
            continue;
        }

        positions.push_back({fen, result});
    }

    if (parseErrors > 0) {
        std::cerr << "warning: " << parseErrors << " lines skipped due to parse errors\n";
    }

    return positions;
}

// ========================================================================
// Sigmoid & Error Function
// ========================================================================

/**
 * Texel sigmoid function.
 *
 * Maps a centipawn evaluation to a [0, 1] win probability:
 *   sigma(q, K) = 1 / (1 + 10^(-K * q / 400))
 *
 * K is a scaling constant typically in [1.0, 2.0].
 */
static double sigmoid(double evalCp, double K) {
    return 1.0 / (1.0 + std::pow(10.0, -K * evalCp / 400.0));
}

/**
 * Compute mean squared error over the entire dataset.
 *
 * E = (1/N) * sum( (R_i - sigma(q_i))^2 )
 *
 * Where:
 *   R_i = game result (1.0, 0.5, 0.0)
 *   q_i = static eval from white's perspective
 */
static double computeError(const std::vector<LabeledPosition>& positions, double K) {
    if (positions.empty()) return 0.0;

    double totalError = 0.0;
    Board board;

    for (const auto& pos : positions) {
        board.loadFEN(pos.fen);
        // computeStaticEvaluation returns score from white's perspective.
        int evalCp = board.computeStaticEvaluation();
        double predicted = sigmoid(static_cast<double>(evalCp), K);
        double diff = pos.result - predicted;
        totalError += diff * diff;
    }

    return totalError / static_cast<double>(positions.size());
}

// ========================================================================
// K-Factor Calibration
// ========================================================================

/**
 * Find the optimal K scaling constant via ternary search.
 *
 * The error function E(K) is unimodal in K, so ternary search
 * converges quickly to the minimum.
 */
static double calibrateK(const std::vector<LabeledPosition>& positions) {
    double lo = 0.1;
    double hi = 5.0;

    std::cout << "Calibrating K-factor...\n";

    for (int iter = 0; iter < 50; ++iter) {
        double m1 = lo + (hi - lo) / 3.0;
        double m2 = hi - (hi - lo) / 3.0;

        double e1 = computeError(positions, m1);
        double e2 = computeError(positions, m2);

        if (e1 < e2) {
            hi = m2;
        } else {
            lo = m1;
        }

        if (hi - lo < 0.0001) {
            break;
        }
    }

    double optimalK = (lo + hi) / 2.0;
    double errorAtK = computeError(positions, optimalK);
    std::cout << "  Optimal K = " << std::fixed << std::setprecision(4) << optimalK
              << " (MSE = " << std::setprecision(8) << errorAtK << ")\n";

    return optimalK;
}

// ========================================================================
// Coordinate Descent Optimizer
// ========================================================================

/**
 * Texel tuning via coordinate descent.
 *
 * For each epoch:
 *   For each tunable parameter:
 *     1. Try incrementing by step size, compute error
 *     2. Try decrementing by step size, compute error
 *     3. Keep whichever direction improved the error, or restore
 *
 * Converges when no parameter improves in a full epoch.
 */
static void coordinateDescent(
    const std::vector<LabeledPosition>& positions,
    double K,
    int maxEpochs,
    int initialStep,
    bool verbose
) {
    auto& params = EvalTuning::getParamRegistry();
    const size_t numParams = params.size();

    double bestError = computeError(positions, K);
    std::cout << "\nStarting coordinate descent\n";
    std::cout << "  Parameters: " << numParams << "\n";
    std::cout << "  Positions: " << positions.size() << "\n";
    std::cout << "  Initial MSE: " << std::fixed << std::setprecision(8) << bestError << "\n";
    std::cout << "  Initial step: " << initialStep << "\n";
    std::cout << "  Max epochs: " << maxEpochs << "\n\n";

    int step = initialStep;
    int totalImproved = 0;

    auto epochStart = std::chrono::steady_clock::now();

    for (int epoch = 1; epoch <= maxEpochs; ++epoch) {
        int improvedThisEpoch = 0;

        for (size_t i = 0; i < numParams; ++i) {
            auto& param = params[i];
            const int original = *param.value;

            // Try increment.
            int tryUp = std::min(original + step, param.maxValue);
            if (tryUp != original) {
                *param.value = tryUp;
                double errorUp = computeError(positions, K);

                if (errorUp < bestError) {
                    bestError = errorUp;
                    ++improvedThisEpoch;
                    if (verbose) {
                        std::cout << "  " << param.name << ": " << original
                                  << " -> " << tryUp << " (MSE=" << bestError << ")\n";
                    }
                    continue; // Keep the improvement.
                }
                *param.value = original; // Restore.
            }

            // Try decrement.
            int tryDown = std::max(original - step, param.minValue);
            if (tryDown != original) {
                *param.value = tryDown;
                double errorDown = computeError(positions, K);

                if (errorDown < bestError) {
                    bestError = errorDown;
                    ++improvedThisEpoch;
                    if (verbose) {
                        std::cout << "  " << param.name << ": " << original
                                  << " -> " << tryDown << " (MSE=" << bestError << ")\n";
                    }
                    continue; // Keep the improvement.
                }
                *param.value = original; // Restore.
            }
        }

        totalImproved += improvedThisEpoch;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - epochStart).count();

        std::cout << "Epoch " << epoch
                  << "  step=" << step
                  << "  improved=" << improvedThisEpoch
                  << "  MSE=" << std::setprecision(8) << bestError
                  << "  elapsed=" << elapsed << "s\n";

        // If no improvement with current step, halve the step.
        if (improvedThisEpoch == 0) {
            if (step <= 1) {
                std::cout << "\nConverged at step=1 with no further improvement.\n";
                break;
            }
            step = std::max(1, step / 2);
            std::cout << "  No improvement, reducing step to " << step << "\n";
        }
    }

    auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - epochStart).count();

    std::cout << "\n=== Tuning Complete ===\n";
    std::cout << "Total parameter improvements: " << totalImproved << "\n";
    std::cout << "Final MSE: " << std::setprecision(8) << bestError << "\n";
    std::cout << "Total time: " << totalElapsed << "s\n";
}

// ========================================================================
// Usage & Main
// ========================================================================

static void printUsage(const char* argv0) {
    std::cout << "Texel Evaluation Tuner\n\n"
              << "Usage:\n"
              << "  " << argv0 << " --dataset <path.epd> [options]\n\n"
              << "Options:\n"
              << "  --dataset <path>    Input EPD dataset (required)\n"
              << "  --output <path>     Output tuned params file (default: data/tuned_weights.txt)\n"
              << "  --load <path>       Load initial weights from file before tuning\n"
              << "  --max-epochs <N>    Maximum optimization epochs (default: 200)\n"
              << "  --step <N>          Initial step size in centipawns (default: 4)\n"
              << "  --k-factor <K>      Override K-factor (default: auto-calibrate)\n"
              << "  --verbose           Print every parameter change\n"
              << "  --print-params      Print all parameters and exit\n"
              << "  --print-error       Print current MSE and exit\n"
              << "  --help              Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string datasetPath;
    std::string outputPath = "data/tuned_weights.txt";
    std::string loadPath;
    int maxEpochs = 200;
    int initialStep = 4;
    double kFactor = -1.0; // negative = auto-calibrate
    bool verbose = false;
    bool printOnly = false;
    bool errorOnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dataset" && i + 1 < argc) {
            datasetPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--load" && i + 1 < argc) {
            loadPath = argv[++i];
        } else if (arg == "--max-epochs" && i + 1 < argc) {
            maxEpochs = std::atoi(argv[++i]);
        } else if (arg == "--step" && i + 1 < argc) {
            initialStep = std::atoi(argv[++i]);
        } else if (arg == "--k-factor" && i + 1 < argc) {
            kFactor = std::atof(argv[++i]);
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--print-params") {
            printOnly = true;
        } else if (arg == "--print-error") {
            errorOnly = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "error: unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Initialize parameter registry.
    auto& params = EvalTuning::getParamRegistry();
    std::cout << "Texel Tuner initialized with " << params.size() << " parameters\n";

    // Load weights from file if specified.
    if (!loadPath.empty()) {
        if (EvalTuning::loadFromFile(loadPath)) {
            std::cout << "Loaded weights from: " << loadPath << "\n";
        } else {
            std::cerr << "warning: could not load weights from: " << loadPath << "\n";
        }
    }

    // Print-only mode.
    if (printOnly) {
        EvalTuning::printParams();
        return 0;
    }

    // Require dataset for error or tuning modes.
    if (datasetPath.empty()) {
        std::cerr << "error: --dataset is required\n";
        printUsage(argv[0]);
        return 1;
    }

    // Load dataset.
    std::cout << "Loading dataset: " << datasetPath << "\n";
    auto positions = loadDataset(datasetPath);
    if (positions.empty()) {
        std::cerr << "error: no positions loaded from dataset\n";
        return 1;
    }
    std::cout << "Loaded " << positions.size() << " positions\n";

    // Calibrate or use provided K-factor.
    double K = kFactor;
    if (K < 0.0) {
        K = calibrateK(positions);
    } else {
        std::cout << "Using provided K-factor: " << K << "\n";
    }

    // Error-only mode.
    if (errorOnly) {
        double error = computeError(positions, K);
        std::cout << "MSE = " << std::fixed << std::setprecision(8) << error << "\n";
        return 0;
    }

    // Run optimization.
    coordinateDescent(positions, K, maxEpochs, initialStep, verbose);

    // Print changed parameters.
    std::cout << "\n=== Changed Parameters ===\n";
    int changedCount = 0;
    for (const auto& param : params) {
        if (*param.value != param.defaultValue) {
            int delta = *param.value - param.defaultValue;
            std::cout << param.name << " = " << *param.value
                      << " (was " << param.defaultValue
                      << ", delta=" << (delta > 0 ? "+" : "") << delta << ")\n";
            ++changedCount;
        }
    }
    if (changedCount == 0) {
        std::cout << "(no parameters changed from defaults)\n";
    }

    // Save tuned weights.
    if (EvalTuning::saveToFile(outputPath)) {
        std::cout << "\nTuned weights saved to: " << outputPath << "\n";
    } else {
        std::cerr << "error: could not save weights to: " << outputPath << "\n";
        return 1;
    }

    return 0;
}
