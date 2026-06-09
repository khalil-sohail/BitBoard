#include "app/app_text.hpp"

#include <cctype>
#include <sstream>

namespace AppText {

std::string colorToString(Color color) {
    return (color == Color::White) ? "White" : "Black";
}

std::string moveToCompactString(const Board& board, const Move& move) {
    std::string text = Board::squareToString(move.from) + Board::squareToString(move.to);
    if (move.promotion.has_value()) {
        text.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(board.pieceToChar(Color::White, *move.promotion)))));
    }
    return text;
}

std::string movesToText(const Board& board, const std::vector<Move>& legalMoves) {
    std::ostringstream oss;
    oss << "Legal moves (" << legalMoves.size() << "):";
    for (const Move& move : legalMoves) {
        oss << "\n" << Board::squareToString(move.from) << Board::squareToString(move.to);
        if (move.promotion.has_value()) {
            oss << board.pieceToChar(Color::White, *move.promotion);
        }
        if (move.isKingSideCastle) {
            oss << " (O-O)";
        }
        if (move.isQueenSideCastle) {
            oss << " (O-O-O)";
        }
    }
    return oss.str();
}

std::string compactInput(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace AppText
