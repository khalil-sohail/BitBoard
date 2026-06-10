#include "search/search_internal.hpp"

#include <cstddef>
#include <cstdint>

int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot, int plyFromRoot, bool allowNull) {
    (void)isRoot;

    if (SearchInternal::shouldAbortSearch()) {
        return alpha;
    }

    if (plyFromRoot >= SearchConstants::MAX_PLY) {
        return colorMultiplier * board.evaluate();
    }

    if (plyFromRoot > 0 && board.isRepetition()) {
        return 0;
    }

    int mateValue = SearchConstants::MATE_SCORE - plyFromRoot;
    if (alpha < -mateValue) alpha = -mateValue;
    if (beta > mateValue - 1) beta = mateValue - 1;
    if (alpha >= beta) return alpha;

    const int originalAlpha = alpha;
    const uint64_t hash = board.getHash();
    const size_t ttIndex = hash % SearchConstants::TT_SIZE;
    const SearchTypes::TTEntry& entry = SearchInternal::g_TT[ttIndex];

    Move ttBestMove{};
    if (entry.hash == hash) {
        ++ttHits;
        ttBestMove = entry.bestMove;

        if (entry.depth >= depth) {
            if (entry.flag == SearchTypes::TTFlag::Exact) {
                ++ttCutoffs;
                return entry.score;
            }
            if (entry.flag == SearchTypes::TTFlag::Alpha && entry.score <= alpha) {
                ++ttCutoffs;
                return alpha;
            }
            if (entry.flag == SearchTypes::TTFlag::Beta && entry.score >= beta) {
                ++ttCutoffs;
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
            depth - 1 - SearchConstants::NULL_MOVE_REDUCTION,
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
            return -SearchConstants::MATE_SCORE + plyFromRoot;
        }
        return 0;
    }

    SearchInternal::sortMovesByScore(board, legal, ttBestMove);
    if (SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0].from >= 0) {
        SearchInternal::prioritizeMove(legal, SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0]);
    }
    if (SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][1].from >= 0) {
        SearchInternal::prioritizeMove(legal, SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][1]);
    }

    int bestScore = -SearchConstants::INF_SCORE;
    Move bestMoveFoundInLoop = legal.front();
    int moveCount = 0;
    bool isFirstMove = true;

    for (const Move& move : legal) {
        ++moveCount;
        board.makeMove(move);
        const bool givesCheck = board.inCheck(board.sideToMove());
        const int extension = givesCheck ? 1 : 0;
        int score = 0;
        const bool isQuiet = !move.isCapture && !move.promotion.has_value();
        const bool canLMR = (depth >= 3 && moveCount >= 4 && isQuiet && !givesCheck && !sideInCheck);

        if (isFirstMove) {
            score = -negamax(board, depth - 1 + extension, -beta, -alpha, -colorMultiplier, false, plyFromRoot + 1);
        } else {
            if (canLMR) {
                const int reducedDepth = depth - 2;
                score = -negamax(board, reducedDepth, -alpha - 1, -alpha, -colorMultiplier, false, plyFromRoot + 1);

                if (score > alpha && score < beta) {
                    score = -negamax(board, depth - 1 + extension, -alpha - 1, -alpha, -colorMultiplier, false, plyFromRoot + 1);
                }
            } else {
                score = -negamax(board, depth - 1 + extension, -alpha - 1, -alpha, -colorMultiplier, false, plyFromRoot + 1);
            }

            if (score > alpha && score < beta) {
                score = -negamax(board, depth - 1 + extension, -beta, -alpha, -colorMultiplier, false, plyFromRoot + 1);
            }
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
            if (isQuiet) {
                SearchInternal::g_historyTable[static_cast<int>(board.sideToMove())][move.from][move.to] += depth * depth;
                
                const Move& primaryKiller = SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0];
                const bool isSameAsPrimary =
                    primaryKiller.from == move.from &&
                    primaryKiller.to == move.to;

                if (!isSameAsPrimary) {
                    SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][1] = primaryKiller;
                    SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0] = move;
                }
            }
            break;
        }
        isFirstMove = false;
    }

    SearchTypes::TTFlag flag = SearchTypes::TTFlag::Exact;
    if (bestScore <= originalAlpha) {
        flag = SearchTypes::TTFlag::Alpha;
    } else if (bestScore >= beta) {
        flag = SearchTypes::TTFlag::Beta;
    }
    SearchTypes::TTEntry& slot = SearchInternal::g_TT[ttIndex];
    if (slot.hash == 0ULL || slot.hash == hash || depth >= slot.depth) {
        ++ttStores;
        slot = {hash, depth, flag, bestScore, bestMoveFoundInLoop};
    }

    return bestScore;
}