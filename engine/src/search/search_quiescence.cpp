#include "search/search_internal.hpp"
#include "search/search_see.hpp"

int quiescenceSearch(Board& board, int alpha, int beta, int plyFromRoot) {
    if (SearchInternal::shouldAbortSearch()) {
        return 0;
    }

    qNodes.fetch_add(1, std::memory_order_relaxed);

    const bool inCheck = board.inCheck(board.sideToMove());
    int standPat = -SearchConstants::INF_SCORE;

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
    SearchInternal::sortMovesByScore(board, moves, Move{});

    for (const Move& move : moves) {
        if (!inCheck && move.isCapture) {
            int capturedValue = 0;
            if (move.isEnPassant) {
                capturedValue = SearchInternal::getPieceValue(PieceType::Pawn);
            } else {
                const auto captured = board.pieceAt(move.to);
                if (captured.has_value()) {
                    capturedValue = SearchInternal::getPieceValue(captured->second);
                }
            }

            if (standPat + capturedValue + SearchConstants::DELTA_PRUNING_MARGIN < alpha) {
                deltaPruneSkips.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            if (SearchInternal::see(board, move) < 0) {
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