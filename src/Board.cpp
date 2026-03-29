#include "board.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

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

constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> MG_VALUE = {
    82, 337, 365, 477, 1025, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> EG_VALUE = {
    94, 281, 297, 512, 936, 0
};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> GAME_PHASE_INC = {
    0, 1, 1, 2, 4, 0
};

constexpr std::array<std::array<int, 64>, static_cast<size_t>(PieceType::Count)> MG_PESTO = {{
    {{
         0,   0,   0,   0,   0,   0,   0,   0,
        98, 134,  61,  95,  68, 126,  34, -11,
        -6,   7,  26,  31,  65,  56,  25, -20,
       -14,  13,   6,  21,  23,  12,  17, -23,
       -27,  -2,  -5,  12,  17,   6,  10, -25,
       -26,  -4,  -4, -10,   3,   3,  33, -12,
       -35,  -1, -20, -23, -15,  24,  38, -22,
         0,   0,   0,   0,   0,   0,   0,   0
    }},
    {{
      -167, -89, -34, -49,  61, -97, -15, -107,
       -73, -41,  72,  36,  23,  62,   7,  -17,
       -47,  60,  37,  65,  84, 129,  73,   44,
        -9,  17,  19,  53,  37,  69,  18,   22,
       -13,   4,  16,  13,  28,  19,  21,   -8,
       -23,  -9,  12,  10,  19,  17,  25,  -16,
       -29, -53, -12,  -3,  -1,  18, -14,  -19,
      -105, -21, -58, -33, -17, -28, -19,  -23
    }},
    {{
       -29,   4, -82, -37, -25, -42,   7,  -8,
       -26,  16, -18, -13,  30,  59,  18, -47,
       -16,  37,  43,  40,  35,  50,  37,  -2,
        -4,   5,  19,  50,  37,  37,   7,  -2,
        -6,  13,  13,  26,  34,  12,  10,   4,
         0,  15,  15,  15,  14,  27,  18,  10,
         4,  15,  16,   0,   7,  21,  33,   1,
       -33,  -3, -14, -21, -13, -12, -39, -21
    }},
    {{
        32,  42,  32,  51, 63,  9,  31,  43,
        27,  32,  58,  62, 80, 67,  26,  44,
        -5,  19,  26,  36, 17, 45,  61,  16,
       -24, -11,   7,  26, 24, 35,  -8, -20,
       -36, -26, -12,  -1,  9, -7,   6, -23,
       -45, -25, -16, -17,  3,  0,  -5, -33,
       -44, -16, -20,  -9, -1, 11,  -6, -71,
       -19, -13,   1,  17, 16,  7, -37, -26
    }},
    {{
       -28,   0,  29,  12, 59, 44,  43,  45,
       -24, -39,  -5,   1,-16, 57,  28,  54,
       -13, -17,   7,   8, 29, 56,  47,  57,
       -27, -27, -16, -16, -1, 17,  -2,   1,
        -9, -26,  -9, -10, -2, -4,   3,  -3,
       -14,   2, -11,  -2, -5,  2,  14,   5,
       -35,  -8,  11,   2,  8, 15,  -3,   1,
        -1, -18,  -9,  10,-15,-25, -31, -50
    }},
    {{
       -65,  23,  16, -15, -56, -34,   2,  13,
        29,  -1, -20,  -7,  -8,  -4, -38, -29,
        -9,  24,   2, -16, -20,   6,  22, -22,
       -17, -20, -12, -27, -30, -25, -14, -36,
       -49,  -1, -27, -39, -46, -44, -33, -51,
       -14, -14, -22, -46, -44, -30, -15, -27,
         1,   7,  -8, -64, -43, -16,   9,   8,
       -15,  36,  12, -54,   8, -28,  24,  14
    }}
}};

constexpr std::array<std::array<int, 64>, static_cast<size_t>(PieceType::Count)> EG_PESTO = {{
    {{
         0,   0,   0,   0,   0,   0,   0,   0,
       178, 173, 158, 134, 147, 132, 165, 187,
        94, 100,  85,  67,  56,  53,  82,  84,
        32,  24,  13,   5,  -2,   4,  17,  17,
        13,   9,  -3,  -7,  -7,  -8,   3,  -1,
         4,   7,  -6,   1,   0,  -5,  -1,  -8,
        13,   8,   8,  10,  13,   0,   2,  -7,
         0,   0,   0,   0,   0,   0,   0,   0
    }},
    {{
       -58, -38, -13, -28, -31, -27, -63, -99,
       -25,  -8, -25,  -2,  -9, -25, -24, -52,
       -24, -20,  10,   9,  -1,  -9, -19, -41,
       -17,   3,  22,  22,  22,  11,   8, -18,
       -18,  -6,  16,  25,  16,  17,   4, -18,
       -23,  -3,  -1,  15,  10,  -3, -20, -22,
       -42, -20, -10,  -5,  -2, -20, -23, -44,
       -29, -51, -23, -15, -22, -18, -50, -64
    }},
    {{
       -14, -21, -11,  -8,  -7,  -9, -17, -24,
        -8,  -4,   7, -12,  -3, -13,  -4, -14,
         2,  -8,   0,  -1,  -2,   6,   0,   4,
        -3,   9,  12,   9,  14,  10,   3,   2,
        -6,   3,  13,  19,   7,  10,  -3,  -9,
       -12,  -3,   8,  10,  13,   3,  -7, -15,
       -14, -18,  -7,  -1,   4,  -9, -15, -27,
       -23,  -9, -23,  -5,  -9, -16,  -5, -17
    }},
    {{
        13, 10, 18, 15, 12, 12,  8,  5,
        11, 13, 13, 11, -3,  3,  8,  3,
         7,  7,  7,  5,  4, -3, -5, -3,
         4,  3, 13,  1,  2,  1, -1,  2,
         3,  5,  8,  4, -5, -6, -8,-11,
        -4,  0, -5, -1, -7,-12, -8,-16,
        -6, -6,  0,  2, -9, -9,-11, -3,
        -9,  2,  3, -1, -5,-13,  4,-20
    }},
    {{
        -9,  22,  22,  27,  27,  19,  10,  20,
       -17,  20,  32,  41,  58,  25,  30,   0,
       -20,   6,   9,  49,  47,  35,  19,   9,
         3,  22,  24,  45,  57,  40,  57,  36,
       -18,  28,  19,  47,  31,  34,  39,  23,
       -16, -27,  15,   6,   9,  17,  10,   5,
       -22, -23, -30, -16, -16, -23, -36, -32,
       -33, -28, -22, -43,  -5, -32, -20, -41
    }},
    {{
       -74, -35, -18, -18, -11,  15,   4, -17,
       -12,  17,  14,  17,  17,  38,  23,  11,
        10,  17,  23,  15,  20,  45,  44,  13,
        -8,  22,  24,  27,  26,  33,  26,   3,
       -18,  -4,  21,  24,  27,  23,   9, -11,
       -19,  -3,  11,  21,  23,  16,   7,  -9,
       -27, -11,   4,  13,  14,   4,  -5, -17,
       -53, -34, -21, -11, -28, -14, -24, -43
    }}
}};

constexpr uint64_t kPolyglotRandom[781] = {
#include "polyglot_random_values.inc"
};

constexpr int mirrorSquare(int square) {
    return square ^ 56;
}

constexpr int pestoSquare(Color color, int square) {
    return (color == Color::White) ? square : mirrorSquare(square);
}

constexpr int fileOf(int square) {
    return square & 7;
}

constexpr int rankOf(int square) {
    return square >> 3;
}

int passedPawnCount(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
    int count = 0;
    uint64_t pawns = ownPawns;

    while (pawns != 0ULL) {
        const int square = lsbIndex(pawns);
        pawns &= (pawns - 1);

        const int file = fileOf(square);
        const int rank = rankOf(square);

        bool isPassed = true;
        for (int df = -1; df <= 1; ++df) {
            const int f = file + df;
            if (f < 0 || f > 7) {
                continue;
            }

            const uint64_t fileMask = Board::FILE_A << f;
            const uint64_t enemyOnFile = enemyPawns & fileMask;

            uint64_t forwardMask = 0ULL;
            if (color == Color::White) {
                if (rank < 7) {
                    forwardMask = ~((1ULL << ((rank + 1) * 8)) - 1ULL);
                }
            } else {
                if (rank > 0) {
                    forwardMask = (1ULL << (rank * 8)) - 1ULL;
                }
            }

            if ((enemyOnFile & forwardMask) != 0ULL) {
                isPassed = false;
                break;
            }
        }

        if (isPassed) {
            ++count;
        }
    }

    return count;
}

int rookOpenFileBonus(uint64_t rooks, uint64_t allPawns) {
    int bonus = 0;
    uint64_t bb = rooks;

    while (bb != 0ULL) {
        const int square = lsbIndex(bb);
        bb &= (bb - 1);

        const int file = fileOf(square);
        const uint64_t fileMask = Board::FILE_A << file;
        if ((allPawns & fileMask) == 0ULL) {
            bonus += 20;
        }
    }

    return bonus;
}

} // namespace

void Board::addPieceEval(Color color, PieceType piece, int square) {
    const int c = static_cast<int>(color);
    const int p = static_cast<int>(piece);
    const int idx = pestoSquare(color, square);

    m_mgScore[c] += MG_VALUE[static_cast<size_t>(p)] + MG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
    m_egScore[c] += EG_VALUE[static_cast<size_t>(p)] + EG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
    m_gamePhase += GAME_PHASE_INC[static_cast<size_t>(p)];
}

void Board::removePieceEval(Color color, PieceType piece, int square) {
    const int c = static_cast<int>(color);
    const int p = static_cast<int>(piece);
    const int idx = pestoSquare(color, square);

    m_mgScore[c] -= MG_VALUE[static_cast<size_t>(p)] + MG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
    m_egScore[c] -= EG_VALUE[static_cast<size_t>(p)] + EG_PESTO[static_cast<size_t>(p)][static_cast<size_t>(idx)];
    m_gamePhase -= GAME_PHASE_INC[static_cast<size_t>(p)];
}

void Board::resetEvalStateFromBoard() {
    m_mgScore = {0, 0};
    m_egScore = {0, 0};
    m_gamePhase = 0;

    for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
        for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
            uint64_t bb = m_bitboards[color][piece];
            while (bb != 0ULL) {
                const int square = lsbIndex(bb);
                bb &= (bb - 1);
                addPieceEval(static_cast<Color>(color), static_cast<PieceType>(piece), square);
            }
        }
    }
}

int Board::evaluate() const {
    int mg = m_mgScore[WHITE_IDX] - m_mgScore[BLACK_IDX];
    int eg = m_egScore[WHITE_IDX] - m_egScore[BLACK_IDX];

    const int whiteBishops = std::popcount(m_bitboards[WHITE_IDX][BISHOP_IDX]);
    const int blackBishops = std::popcount(m_bitboards[BLACK_IDX][BISHOP_IDX]);

    if (whiteBishops >= 2) {
        mg += 30;
        eg += 40;
    }
    if (blackBishops >= 2) {
        mg -= 30;
        eg -= 40;
    }

    const uint64_t allPawns = m_bitboards[WHITE_IDX][PAWN_IDX] | m_bitboards[BLACK_IDX][PAWN_IDX];
    mg += rookOpenFileBonus(m_bitboards[WHITE_IDX][ROOK_IDX], allPawns);
    mg -= rookOpenFileBonus(m_bitboards[BLACK_IDX][ROOK_IDX], allPawns);

    const int passedWhite = passedPawnCount(Color::White, m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][PAWN_IDX]);
    const int passedBlack = passedPawnCount(Color::Black, m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][PAWN_IDX]);
    mg += 20 * (passedWhite - passedBlack);
    eg += 40 * (passedWhite - passedBlack);

    const int phase = std::clamp(m_gamePhase, 0, 24);
    return (mg * phase + eg * (24 - phase)) / 24;
}

int Board::computeStaticEvaluation() const {
    std::array<int, 2> mgScore = {0, 0};
    std::array<int, 2> egScore = {0, 0};
    int phase = 0;

    for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
        for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
            uint64_t bb = m_bitboards[color][piece];
            while (bb != 0ULL) {
                const int square = lsbIndex(bb);
                bb &= (bb - 1);

                const int idx = pestoSquare(static_cast<Color>(color), square);
                mgScore[color] += MG_VALUE[static_cast<size_t>(piece)] + MG_PESTO[static_cast<size_t>(piece)][static_cast<size_t>(idx)];
                egScore[color] += EG_VALUE[static_cast<size_t>(piece)] + EG_PESTO[static_cast<size_t>(piece)][static_cast<size_t>(idx)];
                phase += GAME_PHASE_INC[static_cast<size_t>(piece)];
            }
        }
    }

    int mg = mgScore[WHITE_IDX] - mgScore[BLACK_IDX];
    int eg = egScore[WHITE_IDX] - egScore[BLACK_IDX];

    const int whiteBishops = std::popcount(m_bitboards[WHITE_IDX][BISHOP_IDX]);
    const int blackBishops = std::popcount(m_bitboards[BLACK_IDX][BISHOP_IDX]);

    if (whiteBishops >= 2) {
        mg += 30;
        eg += 40;
    }
    if (blackBishops >= 2) {
        mg -= 30;
        eg -= 40;
    }

    const uint64_t allPawns = m_bitboards[WHITE_IDX][PAWN_IDX] | m_bitboards[BLACK_IDX][PAWN_IDX];
    mg += rookOpenFileBonus(m_bitboards[WHITE_IDX][ROOK_IDX], allPawns);
    mg -= rookOpenFileBonus(m_bitboards[BLACK_IDX][ROOK_IDX], allPawns);

    const int passedWhite = passedPawnCount(Color::White, m_bitboards[WHITE_IDX][PAWN_IDX], m_bitboards[BLACK_IDX][PAWN_IDX]);
    const int passedBlack = passedPawnCount(Color::Black, m_bitboards[BLACK_IDX][PAWN_IDX], m_bitboards[WHITE_IDX][PAWN_IDX]);
    mg += 20 * (passedWhite - passedBlack);
    eg += 40 * (passedWhite - passedBlack);

    const int clampedPhase = std::clamp(phase, 0, 24);
    return (mg * clampedPhase + eg * (24 - clampedPhase)) / 24;
}

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
