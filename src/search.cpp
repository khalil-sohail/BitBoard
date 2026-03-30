#include "search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

constexpr int INF_SCORE = 1'000'000'000;
constexpr int MATE_SCORE = 1'000'000;
constexpr int MAX_PLY = 64;
constexpr int NULL_MOVE_REDUCTION = 2;
constexpr uint64_t TIME_CHECK_MASK = 2047ULL;

enum class TTFlag {
    Exact,
    Alpha,
    Beta
};

struct TTEntry {
    uint64_t hash = 0ULL;
    int depth = 0;
    TTFlag flag = TTFlag::Exact;
    int score = 0;
    Move bestMove{};
};

constexpr size_t TT_SIZE = 1048576;
std::vector<TTEntry> g_TT(TT_SIZE);
std::array<std::array<Move, 2>, MAX_PLY> g_killerMoves{};

void clearTT() {
    g_TT.assign(TT_SIZE, TTEntry{});
}

void clearKillers() {
    for (auto& arr : g_killerMoves) {
        arr[0] = Move{};
        arr[1] = Move{};
    }
}

using SearchClock = std::chrono::steady_clock;
uint64_t g_nodesSearched = 0;

constexpr std::array<std::array<int, static_cast<size_t>(PieceType::Count)>, static_cast<size_t>(PieceType::Count)> MVV_LVA = {{
    {{105, 205, 305, 405, 505, 605}},
    {{104, 204, 304, 404, 504, 604}},
    {{103, 203, 303, 403, 503, 603}},
    {{102, 202, 302, 402, 502, 602}},
    {{101, 201, 301, 401, 501, 601}},
    {{100, 200, 300, 400, 500, 600}}
}};

constexpr std::array<int, static_cast<size_t>(PieceType::Count)> PIECE_VALUES = {
    100, 320, 330, 500, 900, 0
};

int getPieceValue(PieceType piece) {
    return PIECE_VALUES[static_cast<size_t>(piece)];
}

inline bool shouldAbortSearch() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return true;
    }

    ++g_nodesSearched;
    if ((g_nodesSearched & TIME_CHECK_MASK) == 0ULL) {
        checkTime();
    }

    return timeAborted.load(std::memory_order_relaxed);
}

int scoreMove(const Board& board, const Move& move) {
    int score = 0;

    if (move.promotion.has_value()) {
        score += 700 + getPieceValue(*move.promotion);
    }

    if (!move.isCapture) {
        return score;
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
    score += MVV_LVA[static_cast<size_t>(victimIdx)][static_cast<size_t>(attackerIdx)];
    return score;
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

std::atomic<bool> timeAborted{false};
std::chrono::time_point<std::chrono::steady_clock> startTime;
long long allocatedTimeMs = 2000;
uint64_t qNodes = 0;
uint64_t deltaPruneSkips = 0;

void checkTime() {
    if (timeAborted.load(std::memory_order_relaxed)) {
        return;
    }

    const auto now = SearchClock::now();
    const long long usedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    if (usedMs >= allocatedTimeMs) {
        timeAborted.store(true, std::memory_order_relaxed);
    }
}

int quiescenceSearch(Board& board, int alpha, int beta, int plyFromRoot) {
    if (shouldAbortSearch()) {
        return alpha;
    }

    ++qNodes;

    const bool inCheck = board.inCheck(board.sideToMove());
    int standPat = -INF_SCORE;

    if (!inCheck) {
        standPat = board.evaluateSideToMove();
        if (standPat >= beta) {
            return beta;
        }
        if (alpha < standPat) {
            alpha = standPat;
        }
    }

    std::vector<Move> moves;
    if (inCheck) {
        moves = board.generateLegalMoves();
    } else {
        moves = board.generatePseudoLegalCaptures();
    }
    sortMovesByScore(board, moves);

    for (const Move& move : moves) {
        if (!inCheck && move.isCapture) {
            int capturedValue = 0;
            if (move.isEnPassant) {
                capturedValue = getPieceValue(PieceType::Pawn);
            } else {
                const auto captured = board.pieceAt(move.to);
                if (captured.has_value()) {
                    capturedValue = getPieceValue(captured->second);
                }
            }

            if (standPat + capturedValue + 200 < alpha) {
                ++deltaPruneSkips;
                continue;
            }
        }

        const Color movedColor = board.sideToMove();
        board.makeMove(move);

        if (!inCheck && board.inCheck(movedColor)) {
            board.undoMove();
            continue;
        }

        const int score = -quiescenceSearch(board, -beta, -alpha, plyFromRoot + 1);
        board.undoMove();

        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    if (inCheck && moves.empty()) {
        return -9999 + plyFromRoot;
    }

    return alpha;
}

int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot, int plyFromRoot, bool allowNull) {
    (void)isRoot;

    if (shouldAbortSearch()) {
        return alpha;
    }

    if (plyFromRoot >= MAX_PLY) {
        return colorMultiplier * board.evaluate();
    }

    if (plyFromRoot > 0 && board.isRepetition()) {
        return 0;
    }

    int mateValue = MATE_SCORE - plyFromRoot;
    if (alpha < -mateValue) alpha = -mateValue;
    if (beta > mateValue - 1) beta = mateValue - 1;
    if (alpha >= beta) return alpha;

    const int originalAlpha = alpha;
    const uint64_t hash = board.getHash();
    const TTEntry& entry = g_TT[hash % TT_SIZE];

    Move ttBestMove{};
    if (entry.hash == hash) {
        ttBestMove = entry.bestMove;

        if (entry.depth >= depth) {
            if (entry.flag == TTFlag::Exact) {
                return entry.score;
            }
            if (entry.flag == TTFlag::Alpha && entry.score <= alpha) {
                return alpha;
            }
            if (entry.flag == TTFlag::Beta && entry.score >= beta) {
                return beta;
            }
        }
    }

    if (depth == 0) {
        return quiescenceSearch(board, alpha, beta, plyFromRoot);
    }

    const bool sideInCheck = board.inCheck(board.sideToMove());
    if (allowNull && depth >= 3 && !sideInCheck && board.hasNonPawnMaterial(board.sideToMove())) {
        board.makeNullMove();
        const int nullScore = -negamax(
            board,
            depth - 1 - NULL_MOVE_REDUCTION,
            -beta,
            -beta + 1,
            -colorMultiplier,
            false,
            plyFromRoot + 1,
            false
        );
        board.undoNullMove();

        if (nullScore >= beta) {
            return beta;
        }
    }

    std::vector<Move> legal = board.generateLegalMoves();
    if (legal.empty()) {
        if (sideInCheck) {
            return -MATE_SCORE + plyFromRoot;
        }
        return 0;
    }

    sortMovesByScore(board, legal);
    if (ttBestMove.from >= 0) {
        prioritizeMove(legal, ttBestMove);
    }
    if (g_killerMoves[static_cast<size_t>(plyFromRoot)][0].from >= 0) {
        prioritizeMove(legal, g_killerMoves[static_cast<size_t>(plyFromRoot)][0]);
    }
    if (g_killerMoves[static_cast<size_t>(plyFromRoot)][1].from >= 0) {
        prioritizeMove(legal, g_killerMoves[static_cast<size_t>(plyFromRoot)][1]);
    }

    int bestScore = -INF_SCORE;
    Move bestMoveFoundInLoop = legal.front();
    int moveCount = 0;

    for (const Move& move : legal) {
        ++moveCount;
        board.makeMove(move);
        const bool givesCheck = board.inCheck(board.sideToMove());
        const int extension = givesCheck ? 1 : 0;
        int score = 0;
        const bool isQuiet = !move.isCapture && !move.promotion.has_value();
        const bool canLMR = (depth >= 3 && moveCount >= 4 && isQuiet && !givesCheck && !sideInCheck);

        if (canLMR) {
            const int reducedDepth = depth - 2;
            score = -negamax(board, reducedDepth, -alpha - 1, -alpha, -colorMultiplier, false, plyFromRoot + 1);

            if (score > alpha && score < beta) {
                score = -negamax(board, depth - 1 + extension, -beta, -alpha, -colorMultiplier, false, plyFromRoot + 1);
            }
        } else {
            score = -negamax(board, depth - 1 + extension, -beta, -alpha, -colorMultiplier, false, plyFromRoot + 1);
        }
        board.undoMove();
        if (score > bestScore) {
            bestScore = score;
            bestMoveFoundInLoop = move;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (score >= beta) {
            if (!move.isCapture && !move.promotion.has_value()) {
                const Move& primaryKiller = g_killerMoves[static_cast<size_t>(plyFromRoot)][0];
                const bool isSameAsPrimary =
                    primaryKiller.from == move.from &&
                    primaryKiller.to == move.to;

                if (!isSameAsPrimary) {
                    g_killerMoves[static_cast<size_t>(plyFromRoot)][1] = primaryKiller;
                    g_killerMoves[static_cast<size_t>(plyFromRoot)][0] = move;
                }
            }
            break;
        }
    }

    TTFlag flag = TTFlag::Exact;
    if (bestScore <= originalAlpha) {
        flag = TTFlag::Alpha;
    } else if (bestScore >= beta) {
        flag = TTFlag::Beta;
    }
    g_TT[hash % TT_SIZE] = {hash, depth, flag, bestScore, bestMoveFoundInLoop};

    return bestScore;
}

Move findBestMove(Board& board, int maxDepth, long long timeLimitMs) {
    constexpr int WINDOW_SIZE = 50;

    qNodes = 0;
    deltaPruneSkips = 0;

    std::vector<Move> rootMoves = board.generateLegalMoves();
    if (rootMoves.empty()) {
        return Move{};
    }
    if (rootMoves.size() == 1) {
        return rootMoves.front();
    }

    long long hardTimeLimit = allocatedTimeMs;
    long long softTimeLimit = hardTimeLimit / 2;
    Move bestMoveLastIteration{};
    int stableCount = 0;

    const int rootColorMultiplier = (board.sideToMove() == Color::White) ? 1 : -1;

    allocatedTimeMs = std::max(1LL, timeLimitMs);
    startTime = SearchClock::now();
    g_nodesSearched = 0;
    timeAborted.store(false, std::memory_order_relaxed);

    hardTimeLimit = allocatedTimeMs;
    softTimeLimit = hardTimeLimit / 2;

    sortMovesByScore(board, rootMoves);

    Move previousIterationBest = rootMoves.front();
    Move bestCompletedMove = rootMoves.front();
    int previousIterationScore = 0;

    clearTT();
    clearKillers();

    auto moveToUci = [&](const Move& move) {
        std::string text = Board::squareToString(move.from) + Board::squareToString(move.to);
        if (move.promotion.has_value()) {
            char promo = 'q';
            switch (*move.promotion) {
                case PieceType::Knight: promo = 'n'; break;
                case PieceType::Bishop: promo = 'b'; break;
                case PieceType::Rook: promo = 'r'; break;
                case PieceType::Queen: promo = 'q'; break;
                default: break;
            }
            text.push_back(promo);
        }
        return text;
    };

    for (int currentDepth = 1; currentDepth <= maxDepth; ++currentDepth) {
        if (timeAborted.load(std::memory_order_relaxed)) {
            break;
        }

        std::vector<Move> moves = rootMoves;
        prioritizeMove(moves, previousIterationBest);

        int alpha = (currentDepth >= 4) ? previousIterationScore - WINDOW_SIZE : -INF_SCORE;
        int beta = (currentDepth >= 4) ? previousIterationScore + WINDOW_SIZE : INF_SCORE;

        int depthBestScore = -INF_SCORE;
        Move depthBestMove = moves.front();
        bool completedDepth = true;
        int localAlpha = alpha;

        for (const Move& move : moves) {
            if (shouldAbortSearch()) {
                completedDepth = false;
                break;
            }

            board.makeMove(move);
            const int score = -negamax(
                board,
                std::max(0, currentDepth - 1),
                -beta,
                -localAlpha,
                -rootColorMultiplier,
                false,
                0
            );
            board.undoMove();

            if (timeAborted.load(std::memory_order_relaxed)) {
                completedDepth = false;
                break;
            }

            if (score > depthBestScore) {
                depthBestScore = score;
                depthBestMove = move;
            }

            if (score > localAlpha) {
                localAlpha = score;
            }

            if (localAlpha >= beta) {
                break;
            }
        }

        if (completedDepth && currentDepth >= 4 && (depthBestScore <= alpha || depthBestScore >= beta)) {
            depthBestScore = -INF_SCORE;
            depthBestMove = moves.front();

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
                    false,
                    0
                );
                board.undoMove();

                if (timeAborted.load(std::memory_order_relaxed)) {
                    completedDepth = false;
                    break;
                }

                if (score > depthBestScore) {
                    depthBestScore = score;
                    depthBestMove = move;
                }
            }
        }

        if (!completedDepth) {
            break;
        }

        bestCompletedMove = depthBestMove;

        const bool sameAsLastIteration =
            currentDepth > 1 &&
            bestCompletedMove.from == bestMoveLastIteration.from &&
            bestCompletedMove.to == bestMoveLastIteration.to &&
            bestCompletedMove.piece == bestMoveLastIteration.piece &&
            bestCompletedMove.promotion == bestMoveLastIteration.promotion;

        if (sameAsLastIteration) {
            ++stableCount;
        } else {
            stableCount = 0;
            softTimeLimit = hardTimeLimit * 4 / 5;
        }

        bestMoveLastIteration = bestCompletedMove;

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            SearchClock::now() - startTime
        ).count();

        previousIterationBest = depthBestMove;
        previousIterationScore = depthBestScore;

        std::cout << "info depth " << currentDepth
                  << " score cp " << depthBestScore
                  << " pv " << moveToUci(bestCompletedMove) << "\n";

        if (elapsedMs > softTimeLimit && stableCount >= 2) {
            break;
        }

        if (elapsedMs > (hardTimeLimit * 3 / 4)) {
            break;
        }
    }

    std::cout << "info string qNodes: " << qNodes
              << " deltaSkips: " << deltaPruneSkips << "\n";

    return bestCompletedMove;
}
