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

struct BookMoveCandidate {
    Move move;
    uint16_t weight = 0;
    uint32_t learn = 0;
};

enum class BookSelectionMode {
    Weighted,
    Best,
    TopNWeighted,
};

struct SelectedBookMove {
    Move move;
    uint16_t weight = 0;
    uint32_t learn = 0;
    size_t candidateCount = 0;
};

class OpeningBook {
public:
    explicit OpeningBook(const std::string& bookPath = "openings");

    bool load();
    [[nodiscard]] std::optional<Move> getBookMove(const Board& board) const;
    [[nodiscard]] std::vector<BookMoveCandidate> getBookMoveCandidates(const Board& board) const;
    [[nodiscard]] std::optional<SelectedBookMove> selectBookMove(const Board& board) const;
    [[nodiscard]] size_t lineCount() const;
    void setSelectionMode(BookSelectionMode mode);
    void setTopN(size_t topN);
    void setSeed(uint32_t seed);

private:
    std::filesystem::path m_bookPath;
    std::vector<PolyGlotEntry> m_entries;
    mutable std::mt19937 m_rng;
    BookSelectionMode m_selectionMode = BookSelectionMode::Weighted;
    size_t m_topN = 4;

    [[nodiscard]] std::pair<size_t, size_t> findEntryRange(uint64_t hash) const;
    [[nodiscard]] std::optional<Move> decodePolyGlotMove(uint16_t encodedMove, const Board& board) const;
};

#endif
