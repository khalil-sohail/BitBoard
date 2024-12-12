#include "board.hpp"

void Board::printPossibleMoves(const std::map<int, std::vector<int>>& allMoves) {
    for (const auto& entry : allMoves) {
        int position = entry.first;
        const std::vector<int>& moves = entry.second;

        int row = position / 8;
        int col = position % 8;

        std::cout << "Piece at (" << row << ", " << col << ") can move to:\n";

        for (int move : moves) {
            int moveRow = move / 8;
            int moveCol = move % 8;
            std::cout << "  (" << moveRow << ", " << moveCol << ")\n";
        }

        std::cout << "\n";
    }
}

void Board::printBoard() {
    std::cout << " 0  1  2  3  4  5  6  7\n\n";
    for (int i = 0; i < 64; ++i){
        if (i != 0 && i % 8 == 0) {
            std::cout << "   " << i;
            std::cout << "\n";
        }
         if (board[i] == Empty) {
            std::cout << " . ";
        }
         if (board[i] == WhitePawn) {
            std::cout << " ♙ ";
        }
         if (board[i] == WhiteRook) {
            std::cout << " ♜ ";
        }
         if (board[i] == WhiteKnight) {
            std::cout << " ♞ ";
        }
         if (board[i] == WhiteBishop) {
            std::cout << " ♝ ";
        }
         if (board[i] == WhiteQueen) {
            std::cout << " ♛ ";
        }
         if (board[i] == WhiteKing) {
            std::cout << " ♚ ";
        }
         if (board[i] == BlackPawn) {
            std::cout << " ♟️ ";
        }
         if (board[i] == BlackRook) {
            std::cout << " ♖ ";
        }
         if (board[i] == BlackKnight) {
            std::cout << " ♘ ";
        }
         if (board[i] == BlackBishop) {
            std::cout << " ♗ ";
        }
         if (board[i] == BlackQueen) {
            std::cout << " ♕ ";
        }
         if (board[i] == BlackKing) {
            std::cout << " ♔ ";
        }
    }
    std::cout << "   " << 64;
    std::cout << "\n";
}

