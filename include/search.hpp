#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "board.hpp"

int negamax(Board& board, int depth, int alpha, int beta, int colorMultiplier);
Move findBestMove(Board& board, int depth);

#endif
