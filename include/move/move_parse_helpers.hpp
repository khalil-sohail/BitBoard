#ifndef MOVE_MOVE_PARSE_HELPERS_HPP
#define MOVE_MOVE_PARSE_HELPERS_HPP

#include "board.hpp"

#include <string>
#include <vector>

namespace MoveParseHelpers {

PieceType pieceFromLetter(char c);
std::string trim(const std::string& s);
bool isPieceLetter(char c);

std::string describeMove(const Move& m);
std::string joinMoves(const std::vector<Move>& moves);

bool fileMatches(int square, char file);
bool rankMatches(int square, char rank);

bool isMateAfterMove(const Board& board, const Move& mv);
bool isCheckAfterMove(const Board& board, const Move& mv);

} // namespace MoveParseHelpers

#endif