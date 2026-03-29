#include "search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

namespace {

constexpr int INF_SCORE = 1'000'000'000;
constexpr int MATE_SCORE = 1'000'000;
constexpr int NULL_MOVE_REDUCTION = 2;
constexpr uint64_t TIME_CHECK_MASK = 2047ULL;

using SearchClock = std::chrono::steady_clock;

SearchClock::time_point g_searchStartTime;
long long g_searchTimeLimitMs = 2000;
uint64_t g_nodesSearched = 0;

constexpr std::array<std::array<int, static_cast<size_t>(PieceType::Count)>, static_cast<size_t>(PieceType::Count)> MVV_LVA = {{
    {{105, 205, 305, 405, 505, 605}},
    {{104, 204, 304, 404, 504, 604}},
    {{103, 203, 303, 403, 503, 603}},
    {{102, 202, 302, 402, 502, 602}},
    {{101, 201, 301, 401, 501, 601}},
    {{100, 200, 300, 400, 500, 600}}
}};

bool hasTimedOut() {
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        SearchClock::now() - g_searchStartTime
    ).count();
    return elapsedMs >= g_searchTimeLimitMs;
}

inline bool shouldAbortSearch() {
    if (g_timeToAbort.load(std::memory_order_relaxed)) {
        return true;
    }

    ++g_nodesSearched;
    if ((g_nodesSearched & TIME_CHECK_MASK) != 0ULL) {
        return false;
    }

    if (hasTimedOut()) {
        g_timeToAbort.store(true, std::memory_order_relaxed);
        return true;
    }

    return false;
}

int scoreMove(const Board& board, const Move& move) {
    if (!move.isCapture) {
        return 0;
    }

    PieceType victim = PieceType::Pawn;
    if (move.isEnPassant) {
        victim = PieceType::Pawn;
    } else {
        const auto captured = board.pieceAt(move.to);
        if (!captured.has_value()) {
            return 0;
        }
        victim = captured->second;
    }

    const int victimIdx = static_cast<int>(victim);
    const int attackerIdx = static_cast<int>(move.piece);
    return MVV_LVA[static_cast<size_t>(victimIdx)][static_cast<size_t>(attackerIdx)];
}

void sortMovesByScore(const Board& board, std::vector<Move>& moves) {
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& lhs, const Move& rhs) {
        return scoreMove(board, lhs) > scoreMove(board, rhs);
    });
}

void prioritizeMove(std::vector<Move>& moves, const Move& preferred) {
    auto it = std::find_if(moves.begin(), moves.end(), [&](const Move& move) {
        return move.from == preferred.from &&
               move.to == preferred.to &&
               move.piece == preferred.piece &&
               move.promotion == preferred.promotion;
    });

    if (it != moves.end()) {
        std::iter_swap(moves.begin(), it);
    }
}

} // namespace

std::atomic<bool> g_timeToAbort{false};

int quiescenceSearch(Board& board, int alpha, int beta, int colorMultiplier) {
    if (shouldAbortSearch()) {
        return alpha;
    }

    const int standPat = colorMultiplier * board.evaluate();
    if (standPat >= beta) {
        return beta;
    }
    if (standPat > alpha) {
        alpha = standPat;
    }

    std::vector<Move> legal = board.generateLegalMoves();
    std::vector<Move> captures;
    captures.reserve(legal.size());
    for (const Move& move : legal) {
        if (move.isCapture) {
            captures.push_back(move);
        }
    }
    sortMovesByScore(board, captures);

    for (const Move& move : captures) {

        board.makeMove(move);
        const int score = -quiescenceSearch(board, -beta, -alpha, -colorMultiplier);
        board.undoMove();

        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot) {
    if (shouldAbortSearch()) {
        return alpha;
    }

    if (depth == 0) {
        return quiescenceSearch(board, alpha, beta, colorMultiplier);
    }

    const bool sideInCheck = board.inCheck(board.sideToMove());
    if (!isRoot && !sideInCheck && depth >= 4) {
        board.makeNullMove();
        const int nullScore = -negamax(
            board,
            depth - 1 - NULL_MOVE_REDUCTION,
            -beta,
            -beta + 1,
            -colorMultiplier,
            false
        );
        board.undoMove();

        if (nullScore >= beta) {
            return beta;
        }
    }

    std::vector<Move> legal = board.generateLegalMoves();
    if (legal.empty()) {
        if (sideInCheck) {
            return -MATE_SCORE;
        }
        return 0;
    }

    sortMovesByScore(board, legal);

    int bestScore = -INF_SCORE;

    for (const Move& move : legal) {
        board.makeMove(move);
        const int score = -negamax(board, depth - 1, -beta, -alpha, -colorMultiplier, false);
        board.undoMove();
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

Move findBestMove(Board& board, int maxDepth, long long timeLimitMs) {
    std::vector<Move> rootMoves = board.generateLegalMoves();
    if (rootMoves.empty()) {
        return Move{};
    }

    const int rootColorMultiplier = (board.sideToMove() == Color::White) ? 1 : -1;

    g_searchStartTime = SearchClock::now();
    g_searchTimeLimitMs = std::max(1LL, timeLimitMs);
    g_nodesSearched = 0;
    g_timeToAbort.store(false, std::memory_order_relaxed);

    sortMovesByScore(board, rootMoves);

    Move previousIterationBest = rootMoves.front();
    Move bestCompletedMove = rootMoves.front();

    for (int currentDepth = 1; currentDepth <= maxDepth; ++currentDepth) {
        if (g_timeToAbort.load(std::memory_order_relaxed)) {
            break;
        }

        std::vector<Move> moves = rootMoves;
        prioritizeMove(moves, previousIterationBest);

        int depthBestScore = -INF_SCORE;
        Move depthBestMove = moves.front();
        bool completedDepth = true;

        for (const Move& move : moves) {
            if (shouldAbortSearch()) {
                completedDepth = false;
                break;
            }

            board.makeMove(move);
            const int score = -negamax(
                board,
                std::max(0, currentDepth - 1),
                -INF_SCORE,
                INF_SCORE,
                -rootColorMultiplier,
                false
            );
            board.undoMove();

            if (g_timeToAbort.load(std::memory_order_relaxed)) {
                completedDepth = false;
                break;
            }

            if (score > depthBestScore) {
                depthBestScore = score;
                depthBestMove = move;
            }
        }

        if (!completedDepth) {
            break;
        }

        bestCompletedMove = depthBestMove;
        previousIterationBest = depthBestMove;
    }

    return bestCompletedMove;
}
