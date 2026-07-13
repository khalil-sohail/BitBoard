#include "search/search_internal.hpp"
#include "tuning/active_tuning_values.hpp"

#include <cstddef>
#include <cstdint>

namespace {

constexpr const auto& SEARCH_TUNING = Tuning::Generated::VALUES.search;

}

// Helper: returns true if 'move' matches 'ref' (both are valid and identical).
static inline bool isSameMove(const Move& move, const Move& ref) {
    return ref.from >= 0 &&
           move.from == ref.from &&
           move.to   == ref.to   &&
           move.promotion == ref.promotion;
}

int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier,
            bool isRoot, int plyFromRoot, bool allowNull,
            Move excludedMove, bool skipTTWrite) {
    (void)isRoot;

    if (SearchInternal::shouldAbortSearch()) {
        return 0;
    }

    if (plyFromRoot >= SearchConstants::MAX_PLY) {
        return colorMultiplier * board.evaluate();
    }

    if (plyFromRoot > 0 && board.isRepetition()) {
        return 0;
    }

    int mateValue = SearchConstants::MATE_SCORE - plyFromRoot;
    if (alpha < -mateValue) alpha = -mateValue;
    if (beta  >  mateValue - 1) beta  =  mateValue - 1;
    if (alpha >= beta) return alpha;

    const int      originalAlpha = alpha;
    const uint64_t hash          = board.getHash();
    const size_t   ttIndex       = hash & SearchConstants::TT_SIZE_MASK;

    // When we are in an exclusion search, the hash should be decorated so we
    // don't accidentally reuse (or pollute) the TT entry of the real node.
    // The simplest safe approach: just don't look up or store when skipTTWrite
    // is true and an excludedMove is active (exclusion search nodes are
    // ephemeral – they don't need their own TT entries).
    const bool inExclusionSearch = (excludedMove.from >= 0);

    const SearchTypes::TTEntry& entry = SearchInternal::g_TT[ttIndex];

    Move ttBestMove{};
    bool ttHit = false;

    if (!inExclusionSearch && entry.hash == hash) {
        ttHits.fetch_add(1, std::memory_order_relaxed);
        ttHit       = true;
        ttBestMove  = entry.bestMove;

        if (entry.depth >= depth) {
            if (entry.flag == SearchTypes::TTFlag::Exact) {
                ttCutoffs.fetch_add(1, std::memory_order_relaxed);
                return entry.score;
            }
            if (entry.flag == SearchTypes::TTFlag::Alpha && entry.score <= alpha) {
                ttCutoffs.fetch_add(1, std::memory_order_relaxed);
                return alpha;
            }
            if (entry.flag == SearchTypes::TTFlag::Beta && entry.score >= beta) {
                ttCutoffs.fetch_add(1, std::memory_order_relaxed);
                return beta;
            }
        }
    }

    if (depth == 0) {
        return quiescenceSearch(board, alpha, beta, plyFromRoot);
    }

    const bool sideInCheck = board.inCheck(board.sideToMove());

    // ── Null-Move Pruning ─────────────────────────────────────────────────
    if (allowNull && !inExclusionSearch &&
        depth >= 3 && !sideInCheck &&
        board.hasNonPawnMaterial(board.sideToMove())) {
        board.makeNullMove();
        const int nullScore = -negamax(
            board,
            depth - 1 - SEARCH_TUNING.nullMove.reduction,
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

    // ── Singular Extension Detection ──────────────────────────────────────
    // Conditions (all must hold):
    //   1. Depth is high enough (avoids wasted work at shallow nodes).
    //   2. We have a real TT hit from a *previous* search iteration.
    //   3. The TT entry is deep enough to be trustworthy.
    //   4. The TT flag indicates the move was genuinely good (Exact = PV, Beta = lowerbound).
    //   5. We are NOT already inside an exclusion search (no recursion).
    int singularExtension = 0;

    if (!inExclusionSearch &&
        depth >= 6 &&
        ttHit &&
        ttBestMove.from >= 0 &&
        entry.depth >= depth - 3 &&
        (entry.flag == SearchTypes::TTFlag::Exact ||
         entry.flag == SearchTypes::TTFlag::Beta)) {

        // Clamp the TT score so we don't use a mate score as our margin base.
        const int ttScore  = entry.score;
        const int margin   = SEARCH_TUNING.singularExtension.marginCp; // one pawn
        const int singBeta = ttScore - margin;

        // Exclusion search: search all moves *except* ttBestMove at reduced
        // depth with a null window centred on singBeta.  skipTTWrite=true
        // ensures these ephemeral nodes do not corrupt the TT.
        const int singScore = negamax(
            board,
            depth / 2,          // reduced depth
            singBeta - 1,       // alpha (null window lower bound)
            singBeta,           // beta  (null window upper bound)
            colorMultiplier,
            false,
            plyFromRoot,        // same ply – we haven't made a move
            false,              // don't allow null inside exclusion
            ttBestMove,         // exclude the TT move
            true                // don't write results to TT
        );

        // If every other move failed to reach singBeta, the TT move is singular.
        if (singScore < singBeta) {
            singularExtension = 1;
        }
    }

    // ── Reverse Futility Pruning (Static Null Move Pruning) ───────────────
    const bool isPvNode = (alpha != beta - 1);
    int staticEval = 0;
    bool staticEvalComputed = false;

    if (!inExclusionSearch && !sideInCheck && !isPvNode) {
        staticEval = colorMultiplier * board.evaluate();
        staticEvalComputed = true;
        
        if (depth <= 6) {
            const int margin = depth * SEARCH_TUNING.futility.reverseMarginPerDepthCp;
            if (staticEval - margin >= beta) {
                return staticEval - margin;
            }
        }
    }

    // ── Move Generation & Ordering ────────────────────────────────────────
    std::vector<Move> legal = board.generateLegalMoves();
    if (legal.empty()) {
        if (sideInCheck) {
            return -SearchConstants::MATE_SCORE + plyFromRoot;
        }
        return 0; // stalemate
    }

    SearchInternal::sortMovesByScore(board, legal, ttBestMove, plyFromRoot);

    // ── Main Move Loop ────────────────────────────────────────────────────
    int  bestScore           = -SearchConstants::INF_SCORE;
    Move bestMoveFoundInLoop = legal.front();
    int  moveCount           = 0;
    bool isFirstMove         = true;
    std::vector<Move> quietMovesSearched;

    for (const Move& move : legal) {
        // Skip the move that the exclusion search is probing around.
        if (isSameMove(move, excludedMove)) {
            continue;
        }

        ++moveCount;
        SearchInternal::g_movesPlayed[plyFromRoot] = move;
        board.makeMove(move);
        const bool givesCheck = board.inCheck(board.sideToMove());

        // Extension: check extensions + singular extension on the TT move.
        int extension = givesCheck ? 1 : 0;
        if (singularExtension > 0 && isSameMove(move, ttBestMove)) {
            extension = std::max(extension, singularExtension);
        }

        const bool isQuiet = !move.isCapture && !move.promotion.has_value();

        // ── Forward Futility Pruning ──────────────────────────────────────────
        if (depth <= 3 && !sideInCheck && !isPvNode && isQuiet && !givesCheck) {
            bool isKiller = isSameMove(move, SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0]) ||
                            isSameMove(move, SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][1]);
            if (!isKiller && staticEvalComputed) {
                const int margin = depth * SEARCH_TUNING.futility.forwardMarginPerDepthCp;
                if (staticEval + margin <= alpha) {
                    board.undoMove();
                    continue;
                }
            }
        }

        int        score   = 0;
        const bool canLMR  = (depth >= 3 && moveCount >= 4 && isQuiet && !givesCheck && !sideInCheck);

        if (isFirstMove) {
            score = -negamax(board, depth - 1 + extension, -beta, -alpha,
                             -colorMultiplier, false, plyFromRoot + 1);
        } else {
            bool doFullDepthSearch = true;

            if (canLMR) {
                int safeDepth = std::min(depth, 63);
                int safeMoveCount = std::min(moveCount, 63);
                int reduction = SearchInternal::LMR_TABLE[safeDepth][safeMoveCount];
                int reducedDepth = std::max(0, depth - 1 - reduction + extension);
                
                score = -negamax(board, reducedDepth, -alpha - 1, -alpha,
                                 -colorMultiplier, false, plyFromRoot + 1);

                // If the reduced search failed to beat alpha, we can skip the full depth search
                if (score <= alpha) {
                    doFullDepthSearch = false;
                }
            }

            if (doFullDepthSearch) {
                score = -negamax(board, depth - 1 + extension, -alpha - 1, -alpha,
                                 -colorMultiplier, false, plyFromRoot + 1);

                if (score > alpha && score < beta) {
                    score = -negamax(board, depth - 1 + extension, -beta, -alpha,
                                     -colorMultiplier, false, plyFromRoot + 1);
                }
            }
        }

        board.undoMove();

        if (score > bestScore) {
            bestScore           = score;
            bestMoveFoundInLoop = move;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (score >= beta) {
            if (isQuiet) {
                int bonus = depth * depth;
                int& hist = SearchInternal::g_historyTable[static_cast<int>(board.sideToMove())][move.from][move.to];
                hist = std::min(SEARCH_TUNING.moveOrdering.historyLimit, hist + bonus);

                for (const Move& qm : quietMovesSearched) {
                    int& penaltyHist = SearchInternal::g_historyTable[static_cast<int>(board.sideToMove())][qm.from][qm.to];
                    penaltyHist = std::max(-SEARCH_TUNING.moveOrdering.historyLimit, penaltyHist - bonus);
                }

                if (plyFromRoot > 0) {
                    Move prevMove = SearchInternal::g_movesPlayed[plyFromRoot - 1];
                    if (prevMove.from >= 0 && prevMove.to >= 0) {
                        SearchInternal::g_countermoveTable[prevMove.from][prevMove.to] = move;
                    }
                }

                const Move& primaryKiller   = SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0];
                const bool  isSameAsPrimary =
                    primaryKiller.from == move.from &&
                    primaryKiller.to   == move.to;

                if (!isSameAsPrimary) {
                    SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][1] = primaryKiller;
                    SearchInternal::g_killerMoves[static_cast<size_t>(plyFromRoot)][0] = move;
                }
            }
            break;
        }
        
        if (isQuiet) {
            quietMovesSearched.push_back(move);
        }
        
        isFirstMove = false;
    }

    // ── TT Store ──────────────────────────────────────────────────────────
    // Never write during an exclusion search – these are ephemeral sub-trees
    // whose results would overwrite valid entries for the real position.
    // Also, do not write to the TT if the search was aborted, as the score is fake.
    if (!skipTTWrite && !inExclusionSearch && !SearchInternal::shouldAbortSearch()) {
        SearchTypes::TTFlag flag = SearchTypes::TTFlag::Exact;
        if (bestScore <= originalAlpha) {
            flag = SearchTypes::TTFlag::Alpha;
        } else if (bestScore >= beta) {
            flag = SearchTypes::TTFlag::Beta;
        }
        SearchTypes::TTEntry& slot = SearchInternal::g_TT[ttIndex];
        if (slot.hash == 0ULL || slot.hash == hash || depth >= slot.depth) {
            ttStores.fetch_add(1, std::memory_order_relaxed);
            slot = {hash, depth, flag, bestScore, bestMoveFoundInLoop};
        }
    }

    return bestScore;
}
