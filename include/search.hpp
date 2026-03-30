#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "board.hpp"

#include <atomic>
#include <cstdint>

extern std::atomic<bool> g_timeToAbort;
extern uint64_t qNodes;
extern uint64_t deltaPruneSkips;

int quiescenceSearch(Board& board, int alpha, int beta);
int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot = false, int plyFromRoot = 0);
Move findBestMove(Board& board, int maxDepth, long long timeLimitMs = 2000);

#endif
