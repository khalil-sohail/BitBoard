#include "openingBook.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>

namespace {

bool hasBinExtension(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    std::string lowered;
    lowered.reserve(ext.size());
    for (char c : ext) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered == ".bin";
}

std::filesystem::path resolveBookPath(const std::filesystem::path& configuredPath) {
    std::error_code ec;

    if (std::filesystem::is_regular_file(configuredPath, ec)) {
        return configuredPath;
    }

    ec.clear();
    if (std::filesystem::is_directory(configuredPath, ec)) {
        std::vector<std::filesystem::path> candidates;
        for (const auto& entry : std::filesystem::directory_iterator(configuredPath, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (hasBinExtension(entry.path())) {
                candidates.push_back(entry.path());
            }
        }

        if (!candidates.empty()) {
            std::sort(candidates.begin(), candidates.end());
            return candidates.front();
        }
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

OpeningBook::OpeningBook(const std::string& bookPath)
    : m_bookPath(bookPath),
      m_rng(std::random_device{}()) {
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
    if (m_entries.empty()) {
        return std::nullopt;
    }

    const uint64_t hash = board.computePolyglotHash();
    const auto [beginIndex, endIndex] = findEntryRange(hash);

    if (beginIndex == endIndex) {
        return std::nullopt;
    }

    struct Candidate {
        Move move;
        uint16_t weight;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(endIndex - beginIndex);

    for (size_t i = beginIndex; i < endIndex; ++i) {
        std::optional<Move> decoded = decodePolyGlotMove(m_entries[i].move, board);
        if (decoded.has_value()) {
            candidates.push_back(Candidate{*decoded, m_entries[i].weight});
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    uint64_t totalWeight = 0;
    for (const Candidate& c : candidates) {
        totalWeight += static_cast<uint64_t>(c.weight);
    }

    if (totalWeight == 0) {
        std::uniform_int_distribution<size_t> uniform(0, candidates.size() - 1);
        return candidates[uniform(m_rng)].move;
    }

    std::uniform_int_distribution<uint64_t> weighted(0, totalWeight - 1);
    const uint64_t target = weighted(m_rng);

    uint64_t cumulative = 0;
    for (const Candidate& c : candidates) {
        cumulative += static_cast<uint64_t>(c.weight);
        if (target < cumulative) {
            return c.move;
        }
    }

    return candidates.back().move;
}
