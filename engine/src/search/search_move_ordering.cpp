#include "search/search_internal.hpp"
#include "search/search_see.hpp"
#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <cstddef>

namespace {

constexpr const auto& SEARCH_TUNING = Tuning::Generated::VALUES.search;

}

namespace SearchInternal {

int getPieceValue(PieceType piece) {
    return SEARCH_TUNING.moveOrdering.seePieceValues[static_cast<size_t>(piece)];
}

int scoreMove(const Board& board, const Move& move, int plyFromRoot) {
    int score = 0;

    if (move.promotion.has_value()) {
        score += SEARCH_TUNING.moveOrdering.promotionBaseScore + getPieceValue(*move.promotion);
    }

    if (!move.isCapture) {
        if (sameMove(move, SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0])) {
            score += SEARCH_TUNING.moveOrdering.quiet.firstKillerScore;
        } else if (sameMove(move, SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][1])) {
            score += SEARCH_TUNING.moveOrdering.quiet.secondKillerScore;
        }

        bool isCounterMove = false;
        if (plyFromRoot > 0) {
            Move prevMove = SearchInternal::g_movesPlayed[plyFromRoot - 1];
            if (prevMove.from >= 0 && prevMove.to >= 0) {
                Move counterMove = SearchInternal::g_countermoveTable[prevMove.from][prevMove.to];
                if (sameMove(move, counterMove)) {
                    isCounterMove = true;
                }
            }
        }
        
        if (isCounterMove) {
            score += SEARCH_TUNING.moveOrdering.quiet.counterMoveScore;
        }
        
        score += SearchInternal::g_historyTable[static_cast<int>(board.sideToMove())][move.from][move.to];
        return score;
    }

    int seeVal = SearchInternal::see(board, move);
    
    PieceType victim = PieceType::Pawn;
    if (move.isEnPassant) {
        victim = PieceType::Pawn;
    } else {
        const auto captured = board.pieceAt(move.to);
        if (captured.has_value()) {
            victim = captured->second;
        }
    }
    const int victimIdx = static_cast<int>(victim);
    const int attackerIdx = static_cast<int>(move.piece);
    const int mvvLva = SEARCH_TUNING.moveOrdering.mvvLva[static_cast<size_t>(victimIdx)][static_cast<size_t>(attackerIdx)];

    if (seeVal >= 0) {
        // Winning or equal capture: sort high
        score += SEARCH_TUNING.moveOrdering.capture.winningCaptureBaseScore
               + seeVal * SEARCH_TUNING.moveOrdering.capture.seeScoreMultiplier + mvvLva;
    } else {
        // Losing capture: sort low, below quiet moves
        score += SEARCH_TUNING.moveOrdering.capture.losingCaptureBaseScore
               + seeVal * SEARCH_TUNING.moveOrdering.capture.seeScoreMultiplier + mvvLva;
    }

    return score;
}

bool sameMove(const Move& lhs, const Move& rhs) {
    return lhs.from == rhs.from &&
           lhs.to == rhs.to &&
           lhs.piece == rhs.piece &&
           lhs.promotion == rhs.promotion;
}

void sortMovesByScore(const Board& board, std::vector<Move>& moves, Move ttMove, int plyFromRoot) {
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& lhs, const Move& rhs) {
        const int lhsScore = sameMove(lhs, ttMove) ? SEARCH_TUNING.moveOrdering.transpositionMoveScore : scoreMove(board, lhs, plyFromRoot);
        const int rhsScore = sameMove(rhs, ttMove) ? SEARCH_TUNING.moveOrdering.transpositionMoveScore : scoreMove(board, rhs, plyFromRoot);
        return lhsScore > rhsScore;
    });
}

void prioritizeMove(std::vector<Move>& moves, const Move& preferred) {
    auto it = std::find_if(moves.begin(), moves.end(), [&](const Move& move) {
        return sameMove(move, preferred);
    });

    if (it != moves.end()) {
        std::iter_swap(moves.begin(), it);
    }
}

} // namespace SearchInternal
