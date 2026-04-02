#ifndef APP_APP_TEXT_HPP
#define APP_APP_TEXT_HPP

#include "board.hpp"

#include <string>
#include <vector>

namespace AppText {

std::string colorToString(Color color);
std::string moveToCompactString(const Board& board, const Move& move);
std::string movesToText(const Board& board, const std::vector<Move>& legalMoves);
std::string compactInput(const std::string& input);

} // namespace AppText

#endif
