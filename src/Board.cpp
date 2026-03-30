#include "board.hpp"

#include <cctype>
#include <cstdint>
#include <sstream>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);

constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;

inline int lsbIndex(uint64_t bb) {
    return __builtin_ctzll(bb);
}

constexpr uint64_t kPolyglotRandom[781] = {
#include "polyglot_random_values.inc"
};

} // namespace

uint64_t Board::computePolyglotHash() const {
    uint64_t hash = 0ULL;

    for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
        for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
            uint64_t bb = m_bitboards[color][piece];
            while (bb != 0ULL) {
                const int square = lsbIndex(bb);
                bb &= (bb - 1);

                const int colorPivot = (color == BLACK_IDX) ? 0 : 1;
                const int polyPieceIndex = piece * 2 + colorPivot;
                const int randomIndex = 64 * polyPieceIndex + square;
                hash ^= kPolyglotRandom[randomIndex];
            }
        }
    }

    if (m_castlingRights & CASTLE_WK) {
        hash ^= kPolyglotRandom[768];
    }
    if (m_castlingRights & CASTLE_WQ) {
        hash ^= kPolyglotRandom[769];
    }
    if (m_castlingRights & CASTLE_BK) {
        hash ^= kPolyglotRandom[770];
    }
    if (m_castlingRights & CASTLE_BQ) {
        hash ^= kPolyglotRandom[771];
    }

    if (m_enPassantSquare >= 0 && m_enPassantSquare < 64) {
        uint64_t epMask = 1ULL << m_enPassantSquare;
        if (m_sideToMove == Color::White) {
            epMask >>= 8;
        } else {
            epMask <<= 8;
        }

        const uint64_t adjacentPawns =
            ((epMask & ~Board::FILE_A) >> 1) |
            ((epMask & ~Board::FILE_H) << 1);

        const int us = static_cast<int>(m_sideToMove);
        const uint64_t ownPawns = m_bitboards[us][PAWN_IDX];

        if ((adjacentPawns & ownPawns) != 0ULL) {
            const int epFile = m_enPassantSquare % 8;
            hash ^= kPolyglotRandom[772 + epFile];
        }
    }

    if (m_sideToMove == Color::White) {
        hash ^= kPolyglotRandom[780];
    }

    return hash;
}

bool Board::isRepetition() const {
    const uint64_t currentHash = m_hash;
    for (auto it = m_hashHistory.rbegin(); it != m_hashHistory.rend(); ++it) {
        if (*it == currentHash) {
            return true;
        }
    }
    return false;
}

bool Board::hasNonPawnMaterial(Color color) const {
    const int c = static_cast<int>(color);
    return (m_bitboards[c][static_cast<int>(PieceType::Knight)] |
            m_bitboards[c][static_cast<int>(PieceType::Bishop)] |
            m_bitboards[c][static_cast<int>(PieceType::Rook)] |
            m_bitboards[c][static_cast<int>(PieceType::Queen)]) != 0ULL;
}

bool Board::loadFEN(const std::string& fen) {
    m_bitboards = {};
    m_mgScore = {0, 0};
    m_egScore = {0, 0};
    m_gamePhase = 0;
    m_castlingRights = 0;
    m_enPassantSquare = -1;
    m_hashHistory.clear();
    m_undoStack.clear();

    std::istringstream iss(fen);
    std::string pieces;
    std::string active;
    std::string castling;
    std::string enpassant;
    if (!(iss >> pieces >> active >> castling >> enpassant)) {
        return false;
    }

    int rank = 7;
    int file = 0;
    for (char c : pieces) {
        if (c == '/') {
            if (file != 8) {
                return false;
            }
            --rank;
            file = 0;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            file += (c - '0');
            if (file > 8) {
                return false;
            }
            continue;
        }

        if (!std::isalpha(static_cast<unsigned char>(c)) || rank < 0 || file > 7) {
            return false;
        }

        const Color color = std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black;
        const char p = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        PieceType piece = PieceType::Pawn;
        switch (p) {
            case 'p': piece = PieceType::Pawn; break;
            case 'n': piece = PieceType::Knight; break;
            case 'b': piece = PieceType::Bishop; break;
            case 'r': piece = PieceType::Rook; break;
            case 'q': piece = PieceType::Queen; break;
            case 'k': piece = PieceType::King; break;
            default: return false;
        }

        const int square = rank * 8 + file;
        m_bitboards[static_cast<int>(color)][static_cast<int>(piece)] |= (1ULL << square);
        addPieceEval(color, piece, square);
        ++file;
    }

    if (rank != 0 || file != 8) {
        return false;
    }

    m_sideToMove = (active == "b") ? Color::Black : Color::White;

    if (castling != "-") {
        if (castling.find('K') != std::string::npos) m_castlingRights |= CASTLE_WK;
        if (castling.find('Q') != std::string::npos) m_castlingRights |= CASTLE_WQ;
        if (castling.find('k') != std::string::npos) m_castlingRights |= CASTLE_BK;
        if (castling.find('q') != std::string::npos) m_castlingRights |= CASTLE_BQ;
    }

    if (enpassant != "-") {
        const int epSquare = squareFromString(enpassant);
        if (epSquare == -1) {
            return false;
        }
        m_enPassantSquare = epSquare;
    }

    m_hash = computePolyglotHash();
    return true;
}
