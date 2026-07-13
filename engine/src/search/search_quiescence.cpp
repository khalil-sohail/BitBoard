#include "search/search_internal.hpp"
#include "search/search_see.hpp"
#include "tuning/active_tuning_values.hpp"

namespace {

constexpr const auto& SEARCH_TUNING = Tuning::Generated::VALUES.search;

}

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
    SearchInternal::sortMovesByScore(board, moves, Move{}, plyFromRoot);

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

            if (standPat + capturedValue + SEARCH_TUNING.quiescence.deltaMarginCp < alpha) {
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

        if (timeAborted.load(std::memory_order_relaxed)) {
            return 0;
        }

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
