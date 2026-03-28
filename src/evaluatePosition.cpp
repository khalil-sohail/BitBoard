#include "board.hpp"

#include <array>
#include <bit>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);
constexpr int KING_IDX = static_cast<int>(PieceType::King);

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> PIECE_VALUES = {
    100,   // Pawn
    320,   // Knight
    330,   // Bishop
    500,   // Rook
    900,   // Queen
    0      // King
};

constexpr std::array<int, 64> PAWN_PST = {
      0,   0,   0,   0,   0,   0,   0,   0,
      5,  10,  10, -20, -20,  10,  10,   5,
      5,  -5, -10,   0,   0, -10,  -5,   5,
      0,   0,   0,  20,  25,   0,   0,   0,
      5,   5,  10,  25,  25,  10,   5,   5,
     10,  10,  20,  30,  30,  20,  10,  10,
     50,  50,  50,  50,  50,  50,  50,  50,
      0,   0,   0,   0,   0,   0,   0,   0
};

constexpr std::array<int, 64> KNIGHT_PST = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

constexpr std::array<int, 64> BISHOP_PST = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20
};

constexpr std::array<int, 64> ROOK_PST = {
      0,   0,   0,   5,   5,   0,   0,   0,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
      5,  10,  10,  10,  10,  10,  10,   5,
      0,   0,   0,   0,   0,   0,   0,   0
};

constexpr std::array<int, 64> QUEEN_PST = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,   5,   5,   5,   0, -10,
     -5,   0,   5,   5,   5,   5,   0,  -5,
      0,   0,   5,   5,   5,   5,   0,  -5,
    -10,   5,   5,   5,   5,   5,   0, -10,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20
};

constexpr std::array<int, 64> KING_PST = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,   0,   0,   0,   0,  20,  20,
     20,  30,  10,   0,   0,  10,  30,  20
};

constexpr const std::array<int, 64>& pstForPiece(PieceType piece) {
    switch (piece) {
        case PieceType::Pawn:   return PAWN_PST;
        case PieceType::Knight: return KNIGHT_PST;
        case PieceType::Bishop: return BISHOP_PST;
        case PieceType::Rook:   return ROOK_PST;
        case PieceType::Queen:  return QUEEN_PST;
        case PieceType::King:   return KING_PST;
        case PieceType::Count:  return PAWN_PST;
    }
    return PAWN_PST;
}

constexpr int mirrorForBlack(int square) {
    return square ^ 56;
}

} // namespace

int Board::evaluate() const {
    int score = 0;

    // Material balance from white perspective.
    for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
        const int value = PIECE_VALUES[static_cast<size_t>(p)];
        const int whiteCount = std::popcount(m_bitboards[WHITE_IDX][p]);
        const int blackCount = std::popcount(m_bitboards[BLACK_IDX][p]);
        score += value * (whiteCount - blackCount);
    }

    // Positional balance from white perspective.
    for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
        const PieceType piece = static_cast<PieceType>(p);
        const auto& pst = pstForPiece(piece);

        uint64_t whitePieces = m_bitboards[WHITE_IDX][p];
        while (whitePieces != 0ULL) {
            const int square = std::countr_zero(whitePieces);
            score += pst[static_cast<size_t>(square)];
            whitePieces &= (whitePieces - 1);
        }

        uint64_t blackPieces = m_bitboards[BLACK_IDX][p];
        while (blackPieces != 0ULL) {
            const int square = std::countr_zero(blackPieces);
            score -= pst[static_cast<size_t>(mirrorForBlack(square))];
            blackPieces &= (blackPieces - 1);
        }
    }

    return score;
}
