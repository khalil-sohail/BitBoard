#include "app/app_time_policy.hpp"

#include "time/time_management.hpp"
#include "tuning/compiled_profile_identity.hpp"
#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::optional<long long> integerField(const std::string& line, const std::string& name) {
    const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return std::nullopt;
    try { return std::stoll(match[1].str()); } catch (...) { return std::nullopt; }
}

std::optional<bool> booleanField(const std::string& line, const std::string& name) {
    const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return std::nullopt;
    return match[1].str() == "true";
}

bool nullField(const std::string& line, const std::string& name) {
    return std::regex_search(line, std::regex("\\\"" + name + "\\\"\\s*:\\s*null"));
}

} // namespace

namespace AppTimePolicy {

int run() {
    constexpr auto identity = Tuning::compiledProfileIdentity();
    constexpr const auto& time = Tuning::Generated::VALUES.time;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(std::cin, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto remaining = integerField(line, "remainingTimeMs");
        const auto increment = integerField(line, "incrementMs");
        const auto fullmove = integerField(line, "fullmoveNumber");
        const auto unstable = booleanField(line, "bestMoveUnstable");
        const auto changes = integerField(line, "bestMoveChangeCount");
        const auto swing = integerField(line, "scoreSwingCp");
        const auto moves = integerField(line, "movesToGo");
        if (!remaining || !increment || !fullmove || !unstable || !changes || !swing ||
            (*remaining < 0) || (*increment < 0) || (*fullmove < 1) ||
            (!moves && !nullField(line, "movesToGo")) || (moves && *moves < 1) ||
            *fullmove > std::numeric_limits<int>::max() || *changes > std::numeric_limits<int>::max() ||
            *swing < std::numeric_limits<int>::min() || *swing > std::numeric_limits<int>::max()) {
            std::cerr << "time-policy: line " << lineNumber << ": invalid request\n";
            continue;
        }
        const bool hasScores = *unstable || *changes > 0 || *swing != 0;
        const auto budget = TimeManagement::calculateClockBudget(
            *remaining, *increment, static_cast<int>(*fullmove),
            moves ? std::optional<int>(static_cast<int>(*moves)) : std::nullopt,
            hasScores, static_cast<int>(*swing), 0
        );
        const auto deadlines = TimeManagement::calculateStopDeadlines(budget.timeLimitMs);
        const long long configuredSoft = *unstable ? deadlines.unstableSoftMs : deadlines.stableSoftMs;
        const long long effectiveSoft = std::min(configuredSoft, budget.timeLimitMs);
        std::cout << "{\"schemaVersion\":1"
                  << ",\"profileId\":\"" << identity.profileId << "\""
                  << ",\"profileHash\":\"" << identity.canonicalHash << "\""
                  << ",\"rawRemainingMs\":" << budget.rawRemainingMs
                  << ",\"remainingTimeMs\":" << budget.rawRemainingMs
                  << ",\"incrementMs\":" << *increment
                  << ",\"movesToGo\":" << (moves ? std::to_string(*moves) : "null")
                  << ",\"transportReserveMs\":" << budget.transportReserveMs
                  << ",\"runtimeReserveMs\":" << budget.runtimeReserveMs
                  << ",\"safetyReserveMs\":" << (budget.transportReserveMs + budget.runtimeReserveMs)
                  << ",\"safeUsableMs\":" << budget.safeUsableMs
                  << ",\"safeTimeLeftMs\":" << budget.safeUsableMs
                  << ",\"reserveMs\":" << (budget.transportReserveMs + budget.runtimeReserveMs)
                  << ",\"expectedMovesRemaining\":" << budget.expectedMovesRemaining
                  << ",\"allocatedBeforeCapMs\":" << budget.allocatedBeforeCapMs
                  << ",\"normalAllocationCapMs\":" << budget.maximumCapMs
                  << ",\"maximumSpendMs\":" << budget.safeUsableMs
                  << ",\"allocatedTimeMs\":" << budget.timeLimitMs
                  << ",\"softLimitMs\":" << effectiveSoft
                  << ",\"stableSoftLimitMs\":" << deadlines.stableSoftMs
                  << ",\"unstableSoftLimitMs\":" << deadlines.unstableSoftMs
                  << ",\"hardLimitMs\":" << budget.hardBudgetMs
                  << ",\"iterationHardStopMs\":" << deadlines.hardMs
                  << ",\"criticalLowTime\":" << (budget.criticalLowTime ? "true" : "false")
                  << ",\"immediateMove\":" << (budget.immediateMove ? "true" : "false")
                  << ",\"allocationMode\":\"" << (budget.immediateMove ? "immediate_move" : budget.criticalLowTime ? "critical" : "normal") << "\""
                  << ",\"instabilityApplied\":" << (budget.instabilityApplied ? "true" : "false")
                  << ",\"criticalThresholdMs\":" << time.stopPolicy.criticalLowTimeThresholdMs
                  << "}\n";
    }
    return (!std::cin.eof() && std::cin.fail()) ? 2 : 0;
}

} // namespace AppTimePolicy
