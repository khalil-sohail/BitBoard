#include "openingBook.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

bool isOnlyDigitsAndDots(const std::string& token) {
    if (token.empty()) return false;
    for (char c : token) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
            return false;
        }
    }
    return true;
}

std::string trim(const std::string& in) {
    const size_t first = in.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = in.find_last_not_of(" \t\r\n");
    return in.substr(first, last - first + 1);
}

} // namespace

OpeningBook::OpeningBook(const std::string& directoryPath)
    : m_directoryPath(directoryPath),
      m_rng(std::random_device{}()) {
    load();
}

bool OpeningBook::load() {
    m_openingLines.clear();

    std::error_code ec;
    if (!std::filesystem::exists(m_directoryPath, ec) || !std::filesystem::is_directory(m_directoryPath, ec)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_directoryPath, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::vector<std::string> line = parseOpeningText(buffer.str());
        if (!line.empty()) {
            m_openingLines.push_back(std::move(line));
        }
    }

    return !m_openingLines.empty();
}

size_t OpeningBook::lineCount() const {
    return m_openingLines.size();
}

std::string OpeningBook::normalizeMoveToken(const std::string& token) {
    std::string cleaned = trim(token);
    if (cleaned.empty()) {
        return "";
    }

    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\r'), cleaned.end());
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\n'), cleaned.end());

    while (!cleaned.empty() &&
           (cleaned.back() == ',' || cleaned.back() == ';' || cleaned.back() == '!' || cleaned.back() == '?')) {
        cleaned.pop_back();
    }

    const std::unordered_set<std::string> results = {"1-0", "0-1", "1/2-1/2", "*"};
    if (results.find(cleaned) != results.end()) {
        return "";
    }

    size_t prefix = 0;
    while (prefix < cleaned.size() && std::isdigit(static_cast<unsigned char>(cleaned[prefix]))) {
        ++prefix;
    }
    if (prefix > 0 && prefix < cleaned.size() && cleaned[prefix] == '.') {
        while (prefix < cleaned.size() && cleaned[prefix] == '.') {
            ++prefix;
        }
        cleaned = cleaned.substr(prefix);
    }

    if (isOnlyDigitsAndDots(cleaned)) {
        return "";
    }

    if (cleaned == "0-0") cleaned = "O-O";
    if (cleaned == "0-0-0") cleaned = "O-O-O";

    while (!cleaned.empty() && (cleaned.back() == '+' || cleaned.back() == '#')) {
        cleaned.pop_back();
    }

    return cleaned;
}

std::vector<std::string> OpeningBook::parseOpeningText(const std::string& text) {
    std::vector<std::string> moves;
    std::istringstream input(text);
    std::string token;

    while (input >> token) {
        if (!token.empty() && token.front() == '{') {
            while (!token.empty() && token.back() != '}' && (input >> token)) {
            }
            continue;
        }

        std::string normalized = normalizeMoveToken(token);
        if (!normalized.empty()) {
            moves.push_back(normalized);
        }
    }

    return moves;
}

std::optional<std::string> OpeningBook::getBookMove(const std::vector<std::string>& currentHistory) const {
    if (m_openingLines.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> normalizedHistory;
    normalizedHistory.reserve(currentHistory.size());
    for (const std::string& move : currentHistory) {
        std::string token = normalizeMoveToken(move);
        if (!token.empty()) {
            normalizedHistory.push_back(token);
        }
    }

    std::vector<std::string> candidates;
    for (const auto& line : m_openingLines) {
        if (normalizedHistory.size() >= line.size()) {
            continue;
        }

        bool isPrefixMatch = true;
        for (size_t i = 0; i < normalizedHistory.size(); ++i) {
            if (line[i] != normalizedHistory[i]) {
                isPrefixMatch = false;
                break;
            }
        }

        if (isPrefixMatch) {
            candidates.push_back(line[normalizedHistory.size()]);
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(m_rng)];
}
