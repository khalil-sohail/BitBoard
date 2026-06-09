#include "board.hpp"
#include "move/movegen_attacks.hpp"

uint64_t Board::occupancy(Color color) const {
    uint64_t occ = 0ULL;
    const auto& pcs = m_bitboards[static_cast<int>(color)];
    for (size_t i = 0; i < static_cast<size_t>(PieceType::Count); ++i) {
        occ |= pcs[i];
    }
    return occ;
}

uint64_t Board::occupancyAll() const {
    return occupancy(Color::White) | occupancy(Color::Black);
}

bool Board::isSquareOccupied(int square) const {
    return (occupancyAll() & MoveGenAttacks::squareMask(square)) != 0ULL;
}

bool Board::hasPiece(Color color, PieceType piece, int square) const {
    return (m_bitboards[static_cast<int>(color)][static_cast<int>(piece)] & MoveGenAttacks::squareMask(square)) != 0ULL;
}

std::optional<std::pair<Color, PieceType>> Board::pieceAt(int square) const {
    const uint64_t mask = MoveGenAttacks::squareMask(square);
    for (int c = 0; c < 2; ++c) {
        for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
            if (m_bitboards[c][p] & mask) {
                return std::make_pair(static_cast<Color>(c), static_cast<PieceType>(p));
            }
        }
    }
    return std::nullopt;
}
