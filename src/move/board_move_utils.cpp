#include "board.hpp"

#include <cctype>
#include <string>

Color Board::sideToMove() const {
    return m_sideToMove;
}

std::string Board::squareToString(int square) {
    if (square < 0 || square >= 64) {
        return "??";
    }
    const char file = static_cast<char>('a' + (square % 8));
    const char rank = static_cast<char>('1' + (square / 8));
    return std::string{file, rank};
}

int Board::squareFromString(const std::string& coord) {
    if (coord.size() != 2) {
        return -1;
    }
    const char file = static_cast<char>(std::tolower(static_cast<unsigned char>(coord[0])));
    const char rank = coord[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return -1;
    }
    return (rank - '1') * 8 + (file - 'a');
}