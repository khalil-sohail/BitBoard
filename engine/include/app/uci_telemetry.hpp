#ifndef APP_UCI_TELEMETRY_HPP
#define APP_UCI_TELEMETRY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UciTelemetry {

enum class ScoreBound { Exact, Lower, Upper };

// Scores use the root side-to-move perspective. UCI mate distances are full
// moves: positive means the root side mates, negative means it is mated.
std::optional<std::string> formatScore(int score, ScoreBound bound = ScoreBound::Exact);

std::optional<std::string> formatSearchInfo(
    int depth,
    int multipv,
    int score,
    std::uint64_t nodes,
    long long elapsedMs,
    const std::vector<std::string>& pv,
    ScoreBound bound = ScoreBound::Exact
);

// Write and flush one complete line under the shared UCI output lock.
void writeLine(std::string_view line);

} // namespace UciTelemetry

#endif
