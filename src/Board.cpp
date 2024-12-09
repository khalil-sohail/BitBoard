#include "board.hpp"

Board::Board() {

}

void Board::pawnMove(int row, int col, ChessPiece piece) {
    int position = row * 8 + col;

    if (board[position] == piece) {
        auto validMoves = generatePawnMoves(position, piece > 0);

        if (!validMoves.empty()) {
            std::cout << "Valid moves for pawn at (" << row << ", " << col << "):\n";
            for (int move : validMoves) {
                std::cout << "(" << move / 8 << ", " << move % 8 << ")\n";
            }
        } else {
            std::cout << "No valid moves for this pawn.\n";
        }
    } else {
        std::cout << "No pawn found at this position.\n";
    }
}



std::map<int, std::vector<int>> Board::generateAllMoves(bool isWhite) {
    std::map<int, std::vector<int>> allMoves;

    for (int i = 0; i < 64; ++i) {
        if (board[i] == Empty) continue;

        bool pieceisWhite = board[i] > 0;
        if (pieceisWhite != isWhite) continue;

        if (board[i] == WhitePawn || board[i] == BlackPawn) {
            auto pawnMoves = generatePawnMoves(i, isWhite);
            if (!pawnMoves.empty()) {
                allMoves[i] = pawnMoves;
            }
        }
    }

    return allMoves;
}

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

std::vector<int> Board::generatePawnMoves(int position, bool isWhite) {
    std::vector<int> validMoves;
    int direction = isWhite ? 1 : -1;
    int startRow = position / 8;

    int forward = position + 8 * direction;
    if (forward >= 0 && forward < 64 && board[forward] == Empty) {
        validMoves.push_back(forward);

        int doubleForward = position + 16 * direction;
        if (((isWhite && startRow == 1) || (!isWhite && startRow == 6)) && board[doubleForward] == Empty) {
            validMoves.push_back(doubleForward);
        }
    }

    int captureLeft = position + 7 * direction;
    int captureRight = position + 9 * direction;

    if (captureLeft >= 0 && captureLeft < 64 && captureLeft / 8 == startRow + direction) {
        if (isWhite ? board[captureLeft] < 0 : board[captureLeft] > 0) {
            // std::cout << "HERE\n";
            validMoves.push_back(captureLeft);
        }
    }
    if (captureRight >= 0 && captureRight < 64 && captureRight / 8 == startRow + direction) {
        if (!isWhite ? board[captureRight] > 0 : board[captureRight] < 0) {
            validMoves.push_back(captureRight);
        }
    }

    return validMoves;
}































































































void Board::printBoard() {
    for (int i = 0; i < 64; ++i){
        if (i != 0 && i % 8 == 0) {
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
    std::cout << "\n";
}

