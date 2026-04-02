#include "search/search_internal.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

Move findBestMove(Board& board, int maxDepth, long long timeLimitMs) {
    qNodes = 0;
    deltaPruneSkips = 0;
    ttHits = 0;
    ttCutoffs = 0;
    ttStores = 0;

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
    startTime = SearchInternal::SearchClock::now();
    SearchInternal::g_nodesSearched = 0;
    timeAborted.store(false, std::memory_order_relaxed);

    hardTimeLimit = allocatedTimeMs;
    softTimeLimit = hardTimeLimit / 2;

    SearchInternal::sortMovesByScore(board, rootMoves, Move{});

    Move previousIterationBest = rootMoves.front();
    Move bestCompletedMove = rootMoves.front();
    int previousIterationScore = 0;

    SearchInternal::clearTT();
    SearchInternal::clearKillers();

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
        SearchInternal::prioritizeMove(moves, previousIterationBest);

        int alpha = (currentDepth >= 4) ? previousIterationScore - SearchConstants::ASPIRATION_WINDOW_SIZE : -SearchConstants::INF_SCORE;
        int beta = (currentDepth >= 4) ? previousIterationScore + SearchConstants::ASPIRATION_WINDOW_SIZE : SearchConstants::INF_SCORE;

        int depthBestScore = -SearchConstants::INF_SCORE;
        Move depthBestMove = moves.front();
        bool completedDepth = true;
        int localAlpha = alpha;

        for (const Move& move : moves) {
            if (SearchInternal::shouldAbortSearch()) {
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
            depthBestScore = -SearchConstants::INF_SCORE;
            depthBestMove = moves.front();

            for (const Move& move : moves) {
                if (SearchInternal::shouldAbortSearch()) {
                    completedDepth = false;
                    break;
                }

                board.makeMove(move);
                const int score = -negamax(
                    board,
                    std::max(0, currentDepth - 1),
                    -SearchConstants::INF_SCORE,
                    SearchConstants::INF_SCORE,
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
            SearchInternal::SearchClock::now() - startTime
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

    const auto totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        SearchInternal::SearchClock::now() - startTime
    ).count();

    std::cout << "info string nodes: " << SearchInternal::g_nodesSearched
              << " qNodes: " << qNodes
              << " deltaSkips: " << deltaPruneSkips
              << " ttHits: " << ttHits
              << " ttCutoffs: " << ttCutoffs
              << " ttStores: " << ttStores
              << " elapsedMs: " << totalElapsedMs << "\n";

    return bestCompletedMove;
}