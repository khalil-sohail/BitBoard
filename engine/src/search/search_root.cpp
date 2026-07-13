#include "search/search_internal.hpp"
#include "app/uci_telemetry.hpp"
#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr const auto& SEARCH_TUNING = Tuning::Generated::VALUES.search;
constexpr const auto& TIME_TUNING = Tuning::Generated::VALUES.time;

}

static std::vector<std::string> extractPV(Board& board, const Move& rootMove, int maxPVLength) {
    auto moveToUciStr = [](const Move& move) {
        std::string text = Board::squareToString(move.from) + Board::squareToString(move.to);
        if (move.promotion.has_value()) {
            char promo = 'q';
            switch (*move.promotion) {
                case PieceType::Knight: promo = 'n'; break;
                case PieceType::Bishop: promo = 'b'; break;
                case PieceType::Rook:   promo = 'r'; break;
                case PieceType::Queen:  promo = 'q'; break;
                default: break;
            }
            text.push_back(promo);
        }
        return text;
    };

    std::vector<std::string> pv;
    pv.push_back(moveToUciStr(rootMove));

    // Apply the root move, then walk the TT for continuation
    board.makeMove(rootMove);
    int movesMade = 1;

    std::vector<uint64_t> seenHashes;
    seenHashes.push_back(board.getHash());

    while (static_cast<int>(pv.size()) < maxPVLength) {
        const uint64_t hash = board.getHash();
        const size_t ttIndex = hash % SearchConstants::TT_SIZE;
        const SearchTypes::TTEntry& entry = SearchInternal::g_TT[ttIndex];

        if (entry.hash != hash || entry.bestMove.from < 0) {
            break;
        }

        // Verify the TT move is legal in this position
        const std::vector<Move> legal = board.generateLegalMoves();
        bool found = false;
        Move legalMatch{};
        for (const Move& m : legal) {
            if (m.from == entry.bestMove.from && m.to == entry.bestMove.to &&
                m.promotion == entry.bestMove.promotion) {
                found = true;
                legalMatch = m;
                break;
            }
        }
        if (!found) break;

        pv.push_back(moveToUciStr(legalMatch));
        board.makeMove(legalMatch);
        ++movesMade;

        // Cycle detection
        uint64_t newHash = board.getHash();
        for (uint64_t h : seenHashes) {
            if (h == newHash) { goto done; }
        }
        seenHashes.push_back(newHash);
    }
done:

    // Undo all moves to restore board state
    for (int i = 0; i < movesMade; ++i) {
        board.undoMove();
    }

    return pv;
}


std::pair<Move, Move> findBestMove(Board& board, int maxDepth, long long timeLimitMs, int* outScore) {
    return findBestMove(board, maxDepth, timeLimitMs, outScore, true);
}

std::pair<Move, Move> findBestMove(Board& board, int maxDepth, long long timeLimitMs, int* outScore, bool useDeadline) {
    qNodes.store(0, std::memory_order_relaxed);
    deltaPruneSkips.store(0, std::memory_order_relaxed);
    ttHits.store(0, std::memory_order_relaxed);
    ttCutoffs.store(0, std::memory_order_relaxed);
    ttStores.store(0, std::memory_order_relaxed);
    deadlineChecks.store(0, std::memory_order_relaxed);
    lastDeadlineCheckElapsedMs.store(0, std::memory_order_relaxed);
    searchStopReason.store(SearchStopReason::None, std::memory_order_relaxed);
    deadlineActive.store(useDeadline, std::memory_order_relaxed);
    allocatedTimeMs.store(std::max(0LL, timeLimitMs), std::memory_order_relaxed);
    startTime = SearchInternal::SearchClock::now();
    SearchInternal::g_nodesSearched = 0;
    timeAborted.store(false, std::memory_order_relaxed);

    std::vector<Move> rootMoves = board.generateLegalMoves();
    if (rootMoves.empty()) {
        searchStopReason.store(SearchStopReason::Terminal, std::memory_order_relaxed);
        return {Move{}, Move{}};
    }
    if (useDeadline && timeLimitMs <= 0) {
        searchStopReason.store(SearchStopReason::ImmediateMove, std::memory_order_relaxed);
        return {rootMoves.front(), Move{}};
    }
    if (rootMoves.size() == 1) {
        searchStopReason.store(SearchStopReason::CompletedDepth, std::memory_order_relaxed);
        return {rootMoves.front(), Move{}};
    }

    Move bestMoveLastIteration{};
    int stableCount = 0;
    int softLimitFraction =
        TIME_TUNING.stopPolicy.stableSoftStopFraction.numerator * 100 /
        TIME_TUNING.stopPolicy.stableSoftStopFraction.denominator;
    const int rootColorMultiplier = (board.sideToMove() == Color::White) ? 1 : -1;
    SearchInternal::sortMovesByScore(board, rootMoves, Move{}, 0);

    Move previousIterationBest = rootMoves.front();
    Move bestCompletedMove = rootMoves.front();
    int previousIterationScore = 0;

    for (int currentDepth = 1; currentDepth <= maxDepth; ++currentDepth) {
        if (timeAborted.load(std::memory_order_relaxed)) {
            break;
        }

        std::vector<Move> moves = rootMoves;
        SearchInternal::prioritizeMove(moves, previousIterationBest);

        // ── Multi-PV: collect the top N root moves for this depth ──────────
        const int pvCount = std::max(1, std::min(SearchConstants::MULTI_PV, static_cast<int>(moves.size())));

        struct PVResult {
            Move   move;
            int    score;
            std::vector<std::string> pv;
        };
        std::vector<PVResult> pvResults;
        pvResults.reserve(pvCount);

        // Track moves already claimed by a higher-ranked PV so we skip them
        std::vector<Move> excludedMoves;

        bool completedDepth = true;

        for (int pvIdx = 0; pvIdx < pvCount; ++pvIdx) {
            if (timeAborted.load(std::memory_order_relaxed)) {
                completedDepth = false;
                break;
            }

            // Build candidate list: all root moves minus those already assigned to a better PV
            std::vector<Move> candidates;
            candidates.reserve(moves.size());
            for (const Move& m : moves) {
                bool excluded = false;
                for (const Move& ex : excludedMoves) {
                    if (SearchInternal::sameMove(m, ex)) { excluded = true; break; }
                }
                if (!excluded) candidates.push_back(m);
            }
            if (candidates.empty()) break;

            // Aspiration window only for the best (first) PV line
            int alpha, beta;
            if (pvIdx == 0 && currentDepth >= 4) {
                alpha = previousIterationScore - SEARCH_TUNING.aspiration.windowCp;
                beta  = previousIterationScore + SEARCH_TUNING.aspiration.windowCp;
            } else {
                alpha = -SearchConstants::INF_SCORE;
                beta  =  SearchConstants::INF_SCORE;
            }

            int    pvBestScore = -SearchConstants::INF_SCORE;
            Move   pvBestMove  = candidates.front();
            int    localAlpha  = alpha;

            for (const Move& move : candidates) {
                if (SearchInternal::checkDeadlineBoundary() || SearchInternal::shouldAbortSearch()) {
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

                if (score > pvBestScore) {
                    pvBestScore = score;
                    pvBestMove  = move;
                }
                if (score > localAlpha) {
                    localAlpha = score;
                }
                if (localAlpha >= beta) break;
            }

            // Aspiration miss: re-search with full window (only for PV 1)
            if (completedDepth && pvIdx == 0 && currentDepth >= 4 &&
                (pvBestScore <= alpha || pvBestScore >= beta)) {

                pvBestScore = -SearchConstants::INF_SCORE;
                pvBestMove  = candidates.front();

                for (const Move& move : candidates) {
                    if (SearchInternal::checkDeadlineBoundary() || SearchInternal::shouldAbortSearch()) {
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

                    if (score > pvBestScore) {
                        pvBestScore = score;
                        pvBestMove  = move;
                    }
                }
            }

            if (!completedDepth) break;

            std::vector<std::string> pvLine = extractPV(board, pvBestMove, currentDepth);
            pvResults.push_back({ pvBestMove, pvBestScore, pvLine });
            excludedMoves.push_back(pvBestMove);
        }

        if (!completedDepth) break;

        // ── Update iteration tracking (use PV-1 as the authoritative best) ─
        const PVResult& best = pvResults.front();
        bestCompletedMove = best.move;

        const bool sameAsLastIteration =
            currentDepth > 1 &&
            bestCompletedMove.from      == bestMoveLastIteration.from &&
            bestCompletedMove.to        == bestMoveLastIteration.to   &&
            bestCompletedMove.piece     == bestMoveLastIteration.piece &&
            bestCompletedMove.promotion == bestMoveLastIteration.promotion;

        if (sameAsLastIteration) {
            ++stableCount;
        } else {
            stableCount = 0;
            softLimitFraction =
                TIME_TUNING.stopPolicy.unstableSoftStopFraction.numerator * 100 /
                TIME_TUNING.stopPolicy.unstableSoftStopFraction.denominator;
        }
        bestMoveLastIteration  = bestCompletedMove;
        previousIterationBest  = best.move;
        previousIterationScore = best.score;

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            SearchInternal::SearchClock::now() - startTime
        ).count();

        // ── Emit one `info` line per PV ────────────────────────────────────
        for (int pvIdx = 0; pvIdx < static_cast<int>(pvResults.size()); ++pvIdx) {
            const PVResult& pv = pvResults[pvIdx];
            const auto line = UciTelemetry::formatSearchInfo(
                currentDepth,
                pvIdx + 1,
                pv.score,
                SearchInternal::g_nodesSearched,
                elapsedMs,
                pv.pv
            );
            if (line.has_value()) UciTelemetry::writeLine(*line);
        }

        long long currentHardLimit = allocatedTimeMs.load(std::memory_order_relaxed);
        long long currentSoftLimit = (currentHardLimit * softLimitFraction) / 100;
        if (elapsedMs > currentSoftLimit && stableCount >= 2) {
            searchStopReason.store(SearchStopReason::SoftLimit, std::memory_order_relaxed);
            break;
        }
        if (elapsedMs >
            (currentHardLimit * TIME_TUNING.stopPolicy.hardStopFraction.numerator /
             TIME_TUNING.stopPolicy.hardStopFraction.denominator)) {
            searchStopReason.store(SearchStopReason::SoftLimit, std::memory_order_relaxed);
            break;
        }
    }

    if (searchStopReason.load(std::memory_order_relaxed) == SearchStopReason::None) {
        searchStopReason.store(
            timeAborted.load(std::memory_order_relaxed)
                ? SearchStopReason::HardLimit
                : SearchStopReason::CompletedDepth,
            std::memory_order_relaxed
        );
    }

    const auto totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        SearchInternal::SearchClock::now() - startTime
    ).count();

    std::ostringstream statisticsLine;
    statisticsLine << "info string nodes: " << SearchInternal::g_nodesSearched
                   << " qNodes: "     << qNodes.load(std::memory_order_relaxed)
                   << " deltaSkips: " << deltaPruneSkips.load(std::memory_order_relaxed)
                   << " ttHits: "     << ttHits.load(std::memory_order_relaxed)
                   << " ttCutoffs: "  << ttCutoffs.load(std::memory_order_relaxed)
                   << " ttStores: "   << ttStores.load(std::memory_order_relaxed)
                   << " elapsedMs: "  << totalElapsedMs;
    UciTelemetry::writeLine(statisticsLine.str());

    // ── Extract ponder move from TT (PV continuation after bestCompletedMove) ──
    Move ponderMove{};
    if (bestCompletedMove.from >= 0) {
        board.makeMove(bestCompletedMove);
        const uint64_t pHash = board.getHash();
        const size_t   pIdx  = pHash & SearchConstants::TT_SIZE_MASK;
        const SearchTypes::TTEntry& pEntry = SearchInternal::g_TT[pIdx];
        if (pEntry.hash == pHash && pEntry.bestMove.from >= 0) {
            // Validate the TT ponder move is actually legal
            const std::vector<Move> legal = board.generateLegalMoves();
            for (const Move& m : legal) {
                if (m.from       == pEntry.bestMove.from &&
                    m.to         == pEntry.bestMove.to   &&
                    m.promotion  == pEntry.bestMove.promotion) {
                    ponderMove = m;
                    break;
                }
            }
        }
        board.undoMove();
    }

    if (outScore) {
        *outScore = previousIterationScore;
    }

    return {bestCompletedMove, ponderMove};
}

// Thin compatibility wrapper used by any code that still expects a single Move return.
Move findBestMoveCompat(Board& board, int maxDepth, long long timeLimitMs) {
    return findBestMove(board, maxDepth, timeLimitMs).first;
}
