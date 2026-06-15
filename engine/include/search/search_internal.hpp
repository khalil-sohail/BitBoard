#ifndef SEARCH_SEARCH_INTERNAL_HPP
#define SEARCH_SEARCH_INTERNAL_HPP

#include "search.hpp"
#include "search/search_constants.hpp"
#include "search/search_types.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace SearchInternal {

using SearchClock = std::chrono::steady_clock;

extern std::vector<SearchTypes::TTEntry> g_TT;
extern std::array<std::array<Move, 2>, SearchConstants::MAX_PLY> g_killerMoves;
extern std::array<std::array<std::array<int, 64>, 64>, 2> g_historyTable;
extern uint64_t g_nodesSearched;

extern std::array<std::array<Move, 64>, 64> g_countermoveTable;
extern std::array<Move, SearchConstants::MAX_PLY> g_movesPlayed;

void clearTT();
void resizeTT(size_t mb);
void clearKillers();
void clearHistory();
void initLMR();

extern std::array<std::array<int, 64>, 64> LMR_TABLE;

int getPieceValue(PieceType piece);
bool shouldAbortSearch();

int scoreMove(const Board& board, const Move& move, int plyFromRoot);
bool sameMove(const Move& lhs, const Move& rhs);
void sortMovesByScore(const Board& board, std::vector<Move>& moves, Move ttMove, int plyFromRoot);
void prioritizeMove(std::vector<Move>& moves, const Move& preferred);

} // namespace SearchInternal

#endif