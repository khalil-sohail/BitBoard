#include "eval/eval_param_registry.hpp"
#include "eval/eval_weights.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace EvalTuning {

namespace {

/**
 * Helper to register a scalar parameter into the registry vector.
 */
void reg(std::vector<TunableParam>& registry, const char* name, int* value,
         int defaultVal, int minVal, int maxVal) {
    registry.push_back({name, value, defaultVal, minVal, maxVal});
}

/**
 * Helper to register each element of an array parameter.
 * Elements at indices in [skipStart, skipEnd) are skipped (sentinel slots).
 */
template <size_t N>
void regArray(std::vector<TunableParam>& registry, const char* baseName,
              std::array<int, N>& arr, const std::array<int, N>& defaults,
              int minVal, int maxVal, int skipStart = -1, int skipEnd = -1) {
    for (size_t i = 0; i < N; ++i) {
        if (skipStart >= 0 && static_cast<int>(i) >= skipStart &&
            static_cast<int>(i) < skipEnd) {
            continue;
        }
        // Build name like "CONNECTED_PAWN_BONUS_MG_BY_RANK[2]"
        std::string fullName = std::string(baseName) + "[" + std::to_string(i) + "]";
        // Use a static storage for the name string so the pointer remains valid.
        // We leak these intentionally — they live for the program lifetime.
        char* nameCopy = new char[fullName.size() + 1];
        std::copy(fullName.begin(), fullName.end(), nameCopy);
        nameCopy[fullName.size()] = '\0';
        registry.push_back({nameCopy, &arr[i], defaults[i], minVal, maxVal});
    }
}

/**
 * Build the full parameter registry. Called once on first access.
 */
std::vector<TunableParam> buildRegistry() {
    std::vector<TunableParam> registry;
    registry.reserve(128);

    using namespace EvalWeights;

    // --- Material values (skip King = index 5, always 0) ---
    // Pawn MG
    reg(registry, "MG_VALUE_PAWN",   &MG_VALUE[0], 82, 50, 150);
    reg(registry, "MG_VALUE_KNIGHT", &MG_VALUE[1], 337, 250, 450);
    reg(registry, "MG_VALUE_BISHOP", &MG_VALUE[2], 365, 250, 450);
    reg(registry, "MG_VALUE_ROOK",   &MG_VALUE[3], 477, 350, 600);
    reg(registry, "MG_VALUE_QUEEN",  &MG_VALUE[4], 1025, 800, 1200);

    reg(registry, "EG_VALUE_PAWN",   &EG_VALUE[0], 94, 60, 160);
    reg(registry, "EG_VALUE_KNIGHT", &EG_VALUE[1], 281, 200, 400);
    reg(registry, "EG_VALUE_BISHOP", &EG_VALUE[2], 297, 200, 420);
    reg(registry, "EG_VALUE_ROOK",   &EG_VALUE[3], 512, 400, 650);
    reg(registry, "EG_VALUE_QUEEN",  &EG_VALUE[4], 936, 750, 1150);

    // --- Mobility ---
    reg(registry, "MOBILITY_BONUS_MG_KNIGHT", &MOBILITY_BONUS_MG[1], 2, 0, 10);
    reg(registry, "MOBILITY_BONUS_MG_BISHOP", &MOBILITY_BONUS_MG[2], 2, 0, 10);
    reg(registry, "MOBILITY_BONUS_MG_ROOK",   &MOBILITY_BONUS_MG[3], 1, 0, 8);
    reg(registry, "MOBILITY_BONUS_MG_QUEEN",  &MOBILITY_BONUS_MG[4], 1, 0, 8);

    reg(registry, "MOBILITY_BONUS_EG_KNIGHT", &MOBILITY_BONUS_EG[1], 2, 0, 10);
    reg(registry, "MOBILITY_BONUS_EG_BISHOP", &MOBILITY_BONUS_EG[2], 3, 0, 10);
    reg(registry, "MOBILITY_BONUS_EG_ROOK",   &MOBILITY_BONUS_EG[3], 2, 0, 8);
    reg(registry, "MOBILITY_BONUS_EG_QUEEN",  &MOBILITY_BONUS_EG[4], 1, 0, 8);

    // --- Rook Activity ---
    reg(registry, "ROOK_ACTIVITY_BONUS_MG_OPEN",      &ROOK_ACTIVITY_BONUS_MG[0], 15, 0, 40);
    reg(registry, "ROOK_ACTIVITY_BONUS_MG_SEMI_OPEN",  &ROOK_ACTIVITY_BONUS_MG[1], 10, 0, 30);
    reg(registry, "ROOK_ACTIVITY_BONUS_MG_SEVENTH",    &ROOK_ACTIVITY_BONUS_MG[2], 20, 0, 50);
    reg(registry, "ROOK_ACTIVITY_BONUS_EG_OPEN",       &ROOK_ACTIVITY_BONUS_EG[0], 15, 0, 40);
    reg(registry, "ROOK_ACTIVITY_BONUS_EG_SEMI_OPEN",  &ROOK_ACTIVITY_BONUS_EG[1], 10, 0, 30);
    reg(registry, "ROOK_ACTIVITY_BONUS_EG_SEVENTH",    &ROOK_ACTIVITY_BONUS_EG[2], 20, 0, 50);

    // --- Connected Pawns (rank-scaled, skip sentinel indices 0,1,7,8) ---
    const std::array<int, 9> connMgDefaults = {0, 0, 2, 5, 9, 15, 24, 0, 0};
    const std::array<int, 9> connEgDefaults = {0, 0, 3, 7, 12, 20, 32, 0, 0};
    regArray(registry, "CONNECTED_PAWN_BONUS_MG_BY_RANK",
             CONNECTED_PAWN_BONUS_MG_BY_RANK, connMgDefaults, 0, 60, 0, 2);
    regArray(registry, "CONNECTED_PAWN_BONUS_EG_BY_RANK",
             CONNECTED_PAWN_BONUS_EG_BY_RANK, connEgDefaults, 0, 80, 0, 2);

    // --- Candidate Passers (rank-scaled) ---
    const std::array<int, 9> candMgDefaults = {0, 0, 1, 3, 6, 10, 14, 0, 0};
    const std::array<int, 9> candEgDefaults = {0, 0, 2, 5, 9, 14, 20, 0, 0};
    regArray(registry, "CANDIDATE_PAWN_BONUS_MG_BY_RANK",
             CANDIDATE_PAWN_BONUS_MG_BY_RANK, candMgDefaults, 0, 40, 0, 2);
    regArray(registry, "CANDIDATE_PAWN_BONUS_EG_BY_RANK",
             CANDIDATE_PAWN_BONUS_EG_BY_RANK, candEgDefaults, 0, 50, 0, 2);

    // --- Backward Pawns (rank-scaled) ---
    const std::array<int, 9> bwdMgDefaults = {0, 0, 6, 9, 12, 15, 18, 0, 0};
    const std::array<int, 9> bwdEgDefaults = {0, 0, 4, 6, 8, 10, 12, 0, 0};
    regArray(registry, "BACKWARD_PAWN_PENALTY_MG_BY_RANK",
             BACKWARD_PAWN_PENALTY_MG_BY_RANK, bwdMgDefaults, 0, 40, 0, 2);
    regArray(registry, "BACKWARD_PAWN_PENALTY_EG_BY_RANK",
             BACKWARD_PAWN_PENALTY_EG_BY_RANK, bwdEgDefaults, 0, 30, 0, 2);

    // --- Pawn Islands ---
    reg(registry, "PAWN_ISLAND_PENALTY_MG", &PAWN_ISLAND_PENALTY_MG, 4, 0, 20);
    reg(registry, "PAWN_ISLAND_PENALTY_EG", &PAWN_ISLAND_PENALTY_EG, 5, 0, 20);

    // --- King Attack Pressure (nonlinear scale, skip index 0 = 0 attackers) ---
    const std::array<int, 9> kapDefaults = {0, 8, 22, 45, 80, 120, 160, 200, 240};
    regArray(registry, "KING_ATTACK_PRESSURE_PENALTY",
             KING_ATTACK_PRESSURE_PENALTY, kapDefaults, 0, 400, 0, 1);

    // --- Bishop Pair ---
    reg(registry, "BISHOP_PAIR_BONUS_MG", &BISHOP_PAIR_BONUS_MG, 30, 10, 70);
    reg(registry, "BISHOP_PAIR_BONUS_EG", &BISHOP_PAIR_BONUS_EG, 40, 15, 80);

    // --- King Shield ---
    reg(registry, "KING_SHIELD_PER_PAWN_BONUS", &KING_SHIELD_PER_PAWN_BONUS, 15, 0, 40);

    // --- Pawn Structure ---
    reg(registry, "PAWN_STRUCTURE_DOUBLED_PENALTY",  &PAWN_STRUCTURE_DOUBLED_PENALTY,  10, 0, 30);
    reg(registry, "PAWN_STRUCTURE_ISOLATED_PENALTY", &PAWN_STRUCTURE_ISOLATED_PENALTY, 10, 0, 30);

    // --- Passed Pawns ---
    reg(registry, "PASSED_PAWN_COUNT_BONUS_MG",         &PASSED_PAWN_COUNT_BONUS_MG, 10, 0, 40);
    reg(registry, "PASSED_PAWN_COUNT_BONUS_EG",         &PASSED_PAWN_COUNT_BONUS_EG, 20, 0, 60);
    reg(registry, "PASSED_PAWN_EG_MULTIPLIER",          &PASSED_PAWN_EG_MULTIPLIER, 1, 1, 4);
    reg(registry, "PASSED_PAWN_RANK_SQUARE_MULTIPLIER", &PASSED_PAWN_RANK_SQUARE_MULTIPLIER, 2, 1, 6);
    reg(registry, "PASSED_PAWN_BLOCKED_DIVISOR",        &PASSED_PAWN_BLOCKED_DIVISOR, 2, 1, 4);

    // --- Piece Placement Heuristics ---
    reg(registry, "TRAPPED_ROOK_PENALTY",                   &TRAPPED_ROOK_PENALTY, 50, 10, 100);
    reg(registry, "BAD_BISHOP_HEAVY_PENALTY",               &BAD_BISHOP_HEAVY_PENALTY, 50, 10, 100);
    reg(registry, "BAD_BISHOP_LIGHT_PENALTY",               &BAD_BISHOP_LIGHT_PENALTY, 25, 5, 60);
    reg(registry, "EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY",  &EARLY_QUEEN_UNDEVELOPED_MINOR_PENALTY, 20, 5, 50);
    reg(registry, "UNCASTLED_KING_CENTER_PENALTY",          &UNCASTLED_KING_CENTER_PENALTY, 20, 5, 50);
    reg(registry, "UNCASTLED_KING_LOST_RIGHTS_PENALTY",     &UNCASTLED_KING_LOST_RIGHTS_PENALTY, 50, 10, 100);

    // --- Endgame & Scaling ---
    reg(registry, "MOP_UP_CENTER_DISTANCE_WEIGHT", &MOP_UP_CENTER_DISTANCE_WEIGHT, 6, 1, 15);
    reg(registry, "MOP_UP_EDGE_DISTANCE_BASE",     &MOP_UP_EDGE_DISTANCE_BASE, 3, 0, 10);
    reg(registry, "MOP_UP_EDGE_PRESSURE_WEIGHT",   &MOP_UP_EDGE_PRESSURE_WEIGHT, 8, 1, 20);
    reg(registry, "MOP_UP_CORNER_PRESSURE_WEIGHT", &MOP_UP_CORNER_PRESSURE_WEIGHT, 2, 0, 10);
    reg(registry, "MOP_UP_KING_DISTANCE_BASE",     &MOP_UP_KING_DISTANCE_BASE, 14, 5, 25);
    reg(registry, "MOP_UP_KING_DISTANCE_WEIGHT",   &MOP_UP_KING_DISTANCE_WEIGHT, 4, 1, 12);

    return registry;
}

} // namespace

std::vector<TunableParam>& getParamRegistry() {
    static std::vector<TunableParam> registry = buildRegistry();
    return registry;
}

size_t paramCount() {
    return getParamRegistry().size();
}

void resetToDefaults() {
    for (auto& param : getParamRegistry()) {
        *param.value = param.defaultValue;
    }
}

bool loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    auto& registry = getParamRegistry();
    std::string line;
    int loaded = 0;

    while (std::getline(file, line)) {
        // Skip comments and empty lines.
        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }

        std::string name = line.substr(0, eqPos);
        std::string valueStr = line.substr(eqPos + 1);

        // Trim whitespace.
        auto trimWs = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        };
        trimWs(name);
        trimWs(valueStr);

        // Strip trailing comment from value.
        auto commentPos = valueStr.find('#');
        if (commentPos != std::string::npos) {
            valueStr = valueStr.substr(0, commentPos);
            trimWs(valueStr);
        }

        // Find and set the parameter.
        for (auto& param : registry) {
            if (name == param.name) {
                try {
                    int val = std::stoi(valueStr);
                    val = std::clamp(val, param.minValue, param.maxValue);
                    *param.value = val;
                    ++loaded;
                } catch (...) {
                    // Skip invalid values.
                }
                break;
            }
        }
    }

    return loaded > 0;
}

bool saveToFile(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "# Texel-tuned evaluation parameters\n";
    file << "# Format: PARAM_NAME = VALUE  # default=DEFAULT\n\n";

    for (const auto& param : getParamRegistry()) {
        file << param.name << " = " << *param.value
             << "  # default=" << param.defaultValue << "\n";
    }

    return true;
}

void printParams() {
    std::cout << "=== Tunable Parameters (" << paramCount() << ") ===\n";
    for (const auto& param : getParamRegistry()) {
        std::cout << param.name << " = " << *param.value
                  << " [" << param.minValue << ", " << param.maxValue << "]"
                  << " default=" << param.defaultValue;
        if (*param.value != param.defaultValue) {
            int delta = *param.value - param.defaultValue;
            std::cout << " (delta=" << (delta > 0 ? "+" : "") << delta << ")";
        }
        std::cout << "\n";
    }
}

} // namespace EvalTuning
