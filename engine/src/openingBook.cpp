#include "openingBook.hpp"

#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>

namespace {

constexpr const auto& OPENING_TUNING = Tuning::Generated::VALUES.opening;

static_assert(static_cast<int>(BookSelectionMode::Weighted) ==
              static_cast<int>(Tuning::BookSelectionMode::Weighted));
static_assert(static_cast<int>(BookSelectionMode::Best) ==
              static_cast<int>(Tuning::BookSelectionMode::Best));
static_assert(static_cast<int>(BookSelectionMode::TopNWeighted) ==
              static_cast<int>(Tuning::BookSelectionMode::TopNWeighted));

std::filesystem::path resolveBookPath(const std::filesystem::path& configuredPath) {
    std::error_code ec;

    if (std::filesystem::is_regular_file(configuredPath, ec)) {
        return configuredPath;
    }

    std::filesystem::path fallbackDir = configuredPath;
    ec.clear();
    if (!std::filesystem::is_directory(fallbackDir, ec)) {
        fallbackDir = fallbackDir.parent_path();
        if (fallbackDir.empty()) {
            fallbackDir = "./openings";
        }
    }

    std::filesystem::path fallback = fallbackDir / "performance.bin";
    ec.clear();
    if (std::filesystem::is_regular_file(fallback, ec)) {
        return fallback;
    }

    fallback = "./openings/performance.bin";
    ec.clear();
    if (std::filesystem::is_regular_file(fallback, ec)) {
        return fallback;
    }

    return configuredPath;
}

std::optional<PieceType> decodePromotion(int promotionCode) {
    switch (promotionCode) {
        case 0: return std::nullopt;
        case 1: return PieceType::Knight;
        case 2: return PieceType::Bishop;
        case 3: return PieceType::Rook;
        case 4: return PieceType::Queen;
        default: return std::nullopt;
    }
}

} // namespace

bool isOpeningBookEligible(
    bool isPondering,
    bool enabled,
    size_t historyPlyCount,
    int maximumDepth
) {
    return !isPondering && enabled &&
           historyPlyCount < static_cast<size_t>(maximumDepth);
}

size_t selectWeightedBookCandidateIndex(
    const std::vector<BookMoveCandidate>& candidates,
    uint64_t target
) {
    uint64_t cumulative = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        cumulative += static_cast<uint64_t>(candidates[i].weight);
        if (target < cumulative) {
            return i;
        }
    }
    return candidates.size() - 1;
}

OpeningBook::OpeningBook(const std::string& bookPath)
    : m_bookPath(bookPath),
      m_rng(OPENING_TUNING.seed),
      m_selectionMode(static_cast<BookSelectionMode>(OPENING_TUNING.selectionMode)),
      m_topN(OPENING_TUNING.selectionTopN) {
    load();
}

bool OpeningBook::load() {
    m_entries.clear();

    const std::filesystem::path resolvedPath = resolveBookPath(m_bookPath);

    std::ifstream file(resolvedPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    while (true) {
        PolyGlotEntry entry{};

        file.read(reinterpret_cast<char*>(&entry.key), sizeof(entry.key));
        if (file.eof()) {
            break;
        }

        file.read(reinterpret_cast<char*>(&entry.move), sizeof(entry.move));
        file.read(reinterpret_cast<char*>(&entry.weight), sizeof(entry.weight));
        file.read(reinterpret_cast<char*>(&entry.learn), sizeof(entry.learn));

        if (!file) {
            m_entries.clear();
            return false;
        }

#if __cpp_lib_byteswap >= 202110L
        entry.key = std::byteswap(entry.key);
        entry.move = std::byteswap(entry.move);
        entry.weight = std::byteswap(entry.weight);
        entry.learn = std::byteswap(entry.learn);
#else
        entry.key = __builtin_bswap64(entry.key);
        entry.move = __builtin_bswap16(entry.move);
        entry.weight = __builtin_bswap16(entry.weight);
        entry.learn = __builtin_bswap32(entry.learn);
#endif

        m_entries.push_back(entry);
    }

    std::sort(
        m_entries.begin(),
        m_entries.end(),
        [](const PolyGlotEntry& lhs, const PolyGlotEntry& rhs) {
            return lhs.key < rhs.key;
        });

    return true;
}

size_t OpeningBook::lineCount() const {
    return m_entries.size();
}

void OpeningBook::setSelectionMode(BookSelectionMode mode) {
    m_selectionMode = mode;
}

void OpeningBook::setTopN(size_t topN) {
    m_topN = std::max<size_t>(1, topN);
}

void OpeningBook::setSeed(uint32_t seed) {
    m_rng.seed(seed);
}

std::pair<size_t, size_t> OpeningBook::findEntryRange(uint64_t hash) const {
    auto lower = std::lower_bound(
        m_entries.begin(),
        m_entries.end(),
        hash,
        [](const PolyGlotEntry& entry, uint64_t key) {
            return entry.key < key;
        });

    auto upper = std::upper_bound(
        lower,
        m_entries.end(),
        hash,
        [](uint64_t key, const PolyGlotEntry& entry) {
            return key < entry.key;
        });

    return {
        static_cast<size_t>(std::distance(m_entries.begin(), lower)),
        static_cast<size_t>(std::distance(m_entries.begin(), upper))
    };
}

std::optional<Move> OpeningBook::decodePolyGlotMove(uint16_t encodedMove, const Board& board) const {
    int toFile = encodedMove & 0x7;
    int toRank = (encodedMove >> 3) & 0x7;
    int fromFile = (encodedMove >> 6) & 0x7;
    int fromRank = (encodedMove >> 9) & 0x7;
    int promotionCode = (encodedMove >> 12) & 0x7;

    std::optional<PieceType> promotion = decodePromotion(promotionCode);
    if (!promotion.has_value() && promotionCode != 0) {
        return std::nullopt;
    }

    int fromSquare = fromRank * 8 + fromFile;
    int toSquare = toRank * 8 + toFile;

    std::vector<Move> legalMoves = board.generateLegalMoves();

    auto findMatch = [&](int from, int to) -> std::optional<Move> {
        for (const Move& move : legalMoves) {
            if (move.from == from && move.to == to && move.promotion == promotion) {
                return move;
            }
        }
        return std::nullopt;
    };

    if (std::optional<Move> exact = findMatch(fromSquare, toSquare)) {
        return exact;
    }

    if (fromSquare == 4 && toSquare == 7) {
        return findMatch(4, 6);
    }
    if (fromSquare == 4 && toSquare == 0) {
        return findMatch(4, 2);
    }
    if (fromSquare == 60 && toSquare == 63) {
        return findMatch(60, 62);
    }
    if (fromSquare == 60 && toSquare == 56) {
        return findMatch(60, 58);
    }

    return std::nullopt;
}

std::optional<Move> OpeningBook::getBookMove(const Board& board) const {
    std::optional<SelectedBookMove> selected = selectBookMove(board);
    if (!selected.has_value()) {
        return std::nullopt;
    }
    return selected->move;
}

std::vector<BookMoveCandidate> OpeningBook::getBookMoveCandidates(const Board& board) const {
    if (m_entries.empty()) {
        return {};
    }

    const uint64_t hash = board.computePolyglotHash();
    const auto [beginIndex, endIndex] = findEntryRange(hash);

    if (beginIndex == endIndex) {
        return {};
    }

    std::vector<BookMoveCandidate> candidates;

    for (size_t i = beginIndex; i < endIndex; ++i) {
        std::optional<Move> decoded = decodePolyGlotMove(m_entries[i].move, board);
        if (decoded.has_value()) {
            auto duplicate = std::find_if(candidates.begin(), candidates.end(), [&](const BookMoveCandidate& candidate) {
                return candidate.move.from == decoded->from &&
                       candidate.move.to == decoded->to &&
                       candidate.move.promotion == decoded->promotion;
            });
            if (duplicate != candidates.end()) {
                duplicate->weight = static_cast<uint16_t>(std::min<uint32_t>(
                    std::numeric_limits<uint16_t>::max(),
                    static_cast<uint32_t>(duplicate->weight) + m_entries[i].weight));
                duplicate->learn = std::max(duplicate->learn, m_entries[i].learn);
            } else {
                candidates.push_back(BookMoveCandidate{*decoded, m_entries[i].weight, m_entries[i].learn});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const BookMoveCandidate& lhs, const BookMoveCandidate& rhs) {
        if (lhs.weight != rhs.weight) return lhs.weight > rhs.weight;
        if (lhs.move.from != rhs.move.from) return lhs.move.from < rhs.move.from;
        if (lhs.move.to != rhs.move.to) return lhs.move.to < rhs.move.to;
        return lhs.move.promotion < rhs.move.promotion;
    });

    return candidates;
}

std::optional<SelectedBookMove> OpeningBook::selectBookMove(const Board& board) const {
    std::vector<BookMoveCandidate> candidates = getBookMoveCandidates(board);
    if (candidates.empty()) {
        return std::nullopt;
    }

    const size_t originalCandidateCount = candidates.size();
    if (m_selectionMode == BookSelectionMode::Best || candidates.size() == 1) {
        const BookMoveCandidate& selected = candidates.front();
        return SelectedBookMove{selected.move, selected.weight, selected.learn, originalCandidateCount};
    }

    if (m_selectionMode == BookSelectionMode::TopNWeighted && candidates.size() > m_topN) {
        candidates.resize(m_topN);
    }

    uint64_t totalWeight = 0;
    for (const BookMoveCandidate& c : candidates) {
        totalWeight += static_cast<uint64_t>(c.weight);
    }

    if (totalWeight == 0) {
        std::uniform_int_distribution<size_t> uniform(0, candidates.size() - 1);
        const BookMoveCandidate& selected = candidates[uniform(m_rng)];
        return SelectedBookMove{selected.move, selected.weight, selected.learn, originalCandidateCount};
    }

    std::uniform_int_distribution<uint64_t> weighted(0, totalWeight - 1);
    const uint64_t target = weighted(m_rng);

    const BookMoveCandidate& selected =
        candidates[selectWeightedBookCandidateIndex(candidates, target)];
    return SelectedBookMove{selected.move, selected.weight, selected.learn, originalCandidateCount};
}
