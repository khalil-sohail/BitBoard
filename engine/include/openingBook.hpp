#ifndef OPENING_BOOK_HPP
#define OPENING_BOOK_HPP

#include "board.hpp"

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

struct PolyGlotEntry {
    uint64_t key = 0;
    uint16_t move = 0;
    uint16_t weight = 0;
    uint32_t learn = 0;
};

class OpeningBook {
public:
    explicit OpeningBook(const std::string& bookPath = "openings");

    bool load();
    [[nodiscard]] std::optional<Move> getBookMove(const Board& board) const;
    [[nodiscard]] size_t lineCount() const;

private:
    std::filesystem::path m_bookPath;
    std::vector<PolyGlotEntry> m_entries;
    mutable std::mt19937 m_rng;

    [[nodiscard]] std::pair<size_t, size_t> findEntryRange(uint64_t hash) const;
    [[nodiscard]] std::optional<Move> decodePolyGlotMove(uint16_t encodedMove, const Board& board) const;
};

#endif
