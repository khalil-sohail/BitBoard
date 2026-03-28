#include "search.hpp"

#include <algorithm>
#include <limits>

namespace {

constexpr int INF_SCORE = 1'000'000'000;
constexpr int MATE_SCORE = 1'000'000;

} // namespace

int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier) {
    if (depth == 0) {
        return colorMultiplier * board.evaluate();
    }

    const std::vector<Move> legal = board.generateLegalMoves();
    if (legal.empty()) {
        if (board.inCheck(board.sideToMove())) {
            return -MATE_SCORE;
        }
        return 0;
    }

    int bestScore = -INF_SCORE;

    for (const Move& move : legal) {
        Board copy = board;
        copy.makeMove(move);

        const int score = -negamax(copy, depth - 1, -beta, -alpha, -colorMultiplier);
        if (score > bestScore) {
            bestScore = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    return bestScore;
}

Move findBestMove(Board& board, int depth) {
    const std::vector<Move> legal = board.generateLegalMoves();
    if (legal.empty()) {
        return Move{};
    }

    const int rootColorMultiplier = (board.sideToMove() == Color::White) ? 1 : -1;
    int bestScore = -INF_SCORE;
    Move bestMove = legal.front();

    for (const Move& move : legal) {
        Board copy = board;
        copy.makeMove(move);

        const int score = -negamax(copy, std::max(0, depth - 1), -INF_SCORE, INF_SCORE, -rootColorMultiplier);
        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
    }

    return bestMove;
}
