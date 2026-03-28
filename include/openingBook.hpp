#ifndef OPENING_BOOK_HPP
#define OPENING_BOOK_HPP

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

class OpeningBook {
public:
    explicit OpeningBook(const std::string& directoryPath = "openings");

    bool load();
    [[nodiscard]] std::optional<std::string> getBookMove(const std::vector<std::string>& currentHistory) const;
    [[nodiscard]] size_t lineCount() const;

private:
    std::filesystem::path m_directoryPath;
    std::vector<std::vector<std::string>> m_openingLines;
    mutable std::mt19937 m_rng;

    static std::vector<std::string> parseOpeningText(const std::string& text);
    static std::string normalizeMoveToken(const std::string& token);
};

#endif
