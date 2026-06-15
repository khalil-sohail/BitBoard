#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "board.hpp"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <utility>

extern std::atomic<bool> timeAborted;
extern std::chrono::time_point<std::chrono::steady_clock> startTime;
extern long long allocatedTimeMs;
extern uint64_t qNodes;
extern uint64_t deltaPruneSkips;
extern uint64_t ttHits;
extern uint64_t ttCutoffs;
extern uint64_t ttStores;

void checkTime();

int quiescenceSearch(Board& board, int alpha, int beta, int plyFromRoot);
int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot = false, int plyFromRoot = 0, bool allowNull = true, Move excludedMove = Move{}, bool skipTTWrite = false);

// Returns {bestMove, ponderMove}.  ponderMove.from == -1 when unavailable.
// When isPonder == true the search runs indefinitely until timeAborted is set.
std::pair<Move, Move> findBestMove(Board& board, int maxDepth, long long timeLimitMs = 2000, bool isPonder = false);

Move findBestMoveCompat(Board& board, int maxDepth, long long timeLimitMs = 2000);

#endif
