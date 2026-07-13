#include "app/uci_telemetry.hpp"

#include "search/search_constants.hpp"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {

std::mutex outputMutex;

const char* boundToken(UciTelemetry::ScoreBound bound) {
    switch (bound) {
        case UciTelemetry::ScoreBound::Exact: return "";
        case UciTelemetry::ScoreBound::Lower: return " lowerbound";
        case UciTelemetry::ScoreBound::Upper: return " upperbound";
    }
    return "";
}

} // namespace

namespace UciTelemetry {

std::optional<std::string> formatScore(int score, ScoreBound bound) {
    std::ostringstream output;
    if (score >= SearchConstants::MATE_SCORE - SearchConstants::MAX_PLY &&
        score <= SearchConstants::MATE_SCORE) {
        // Root search makes the first move before entering negamax at ply zero.
        const int moves = std::max(1, (SearchConstants::MATE_SCORE - score + 2) / 2);
        output << "score mate " << moves;
    } else if (score <= -SearchConstants::MATE_SCORE + SearchConstants::MAX_PLY &&
               score >= -SearchConstants::MATE_SCORE) {
        const int moves = std::max(1, (SearchConstants::MATE_SCORE + score + 1) / 2);
        output << "score mate " << -moves;
    } else if (score > -SearchConstants::MATE_SCORE && score < SearchConstants::MATE_SCORE) {
        output << "score cp " << score;
    } else {
        // INF_SCORE and other internal sentinels have no UCI representation.
        return std::nullopt;
    }
    output << boundToken(bound);
    return output.str();
}

std::optional<std::string> formatSearchInfo(
    int depth,
    int multipv,
    int score,
    std::uint64_t nodes,
    long long elapsedMs,
    const std::vector<std::string>& pv,
    ScoreBound bound
) {
    const auto formattedScore = formatScore(score, bound);
    if (!formattedScore.has_value()) return std::nullopt;

    std::ostringstream output;
    output << "info depth " << depth
           << " multipv " << multipv
           << " " << *formattedScore
           << " nodes " << nodes
           << " time " << elapsedMs
           << " pv";
    for (const std::string& move : pv) output << " " << move;
    return output.str();
}

void writeLine(std::string_view line) {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
    std::cout.put('\n');
    std::cout.flush();
}

} // namespace UciTelemetry
