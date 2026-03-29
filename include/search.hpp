#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "board.hpp"

#include <atomic>

extern std::atomic<bool> g_timeToAbort;

int quiescenceSearch(Board& board, int alpha, int beta, int colorMultiplier);
int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier, bool isRoot = false);
Move findBestMove(Board& board, int maxDepth, long long timeLimitMs = 2000);

#endif
