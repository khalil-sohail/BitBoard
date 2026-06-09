#include "board.hpp"

std::vector<Move> Board::generateLegalMoves() const {
    std::vector<Move> legal;
    std::vector<Move> pseudo = generatePseudoLegalMoves();
    legal.reserve(pseudo.size());

    for (const Move& mv : pseudo) {
        Board copy = *this;
        copy.makeMove(mv);
        if (!copy.inCheck(m_sideToMove)) {
            legal.push_back(mv);
        }
    }

    return legal;
}

uint64_t Board::perft(int depth) const {
    if (depth < 0) {
        return 0;
    }
    if (depth == 0) {
        return 1;
    }

    const std::vector<Move> legal = generateLegalMoves();
    if (depth == 1) {
        return static_cast<uint64_t>(legal.size());
    }

    uint64_t nodes = 0;
    for (const Move& mv : legal) {
        Board copy = *this;
        copy.makeMove(mv);
        nodes += copy.perft(depth - 1);
    }
    return nodes;
}
