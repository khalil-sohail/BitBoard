#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "board.hpp"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <utility>

extern std::atomic<bool> timeAborted;
extern std::chrono::time_point<std::chrono::steady_clock> startTime;
extern std::atomic<long long> allocatedTimeMs;
extern std::atomic<uint64_t> qNodes;
extern std::atomic<uint64_t> deltaPruneSkips;
extern std::atomic<uint64_t> ttHits;
extern std::atomic<uint64_t> ttCutoffs;
extern std::atomic<uint64_t> ttStores;

void checkTime();

int quiescenceSearch(Board& board, int alpha, int beta, int plyFromRoot);
int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot = false, int plyFromRoot = 0, bool allowNull = true, Move excludedMove = Move{}, bool skipTTWrite = false);

// Returns {bestMove, ponderMove}.  ponderMove.from == -1 when unavailable.
// An optional outScore pointer can be provided to retrieve the final search score.
std::pair<Move, Move> findBestMove(Board& board, int maxDepth, long long timeLimitMs = 2000, int* outScore = nullptr);

Move findBestMoveCompat(Board& board, int maxDepth, long long timeLimitMs = 2000);

#endif
