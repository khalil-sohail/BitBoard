#include "board.hpp"

// Board::Board() {}

std::map<int, std::vector<int>> Board::generateAllMoves(bool isWhite) {
    std::map<int, std::vector<int>> allMoves;

    for (int i = 0; i < 64; ++i) {
        if (board[i] == Empty) continue;

        bool pieceisWhite = board[i] > 0;
        if (pieceisWhite != isWhite) continue;

        // if (board[i] == WhitePawn || board[i] == BlackPawn) {
        //     auto pawnMoves = generatePawnMoves(i, isWhite);
        //     if (!pawnMoves.empty()) {
        //         allMoves[i] = pawnMoves;
        //     }
        // }
        // else if (board[i] == WhiteRook || board[i] == BlackRook) {
        //     auto rookMoves = generateRookMoves(i, isWhite);
        //     if (!rookMoves.empty()) {
        //         allMoves[i] = rookMoves;
        //     }
        // }
        // else if (board[i] == WhiteBishop || board[i] == BlackBishop) {
        //     auto rookMoves = generateBishopMoves(i, isWhite);
        //     if (!rookMoves.empty()) {
        //         allMoves[i] = rookMoves;
        //     }
        // }
        // else if (board[i] == WhiteQueen || board[i] == BlackQueen) {
        //     auto rookMoves = generateQueenMoves(i, isWhite);
        //     if (!rookMoves.empty()) {
        //         allMoves[i] = rookMoves;
        //     }
        // }
        else if (board[i] == WhiteKing || board[i] == BlackKing) {
            auto rookMoves = generateKingMoves(i, isWhite);
            if (!rookMoves.empty()) {
                allMoves[i] = rookMoves;
            }
        }
    }

    return allMoves;
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

std::vector<int> Board::generateRookMoves(int position, bool isWhite) {
    std::vector<int> validMoves;
    int idx;
                        // ROWS
                // left
    for (idx = position - 1; idx > 0 && ((idx + 1) % 8) != 0; --idx) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }
                // right
    for (idx = position + 1; idx % 8 != 0; ++idx) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }

                        // columns
                // up
    for (idx = position - 8; idx > 0; idx -= 8) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }
                // down
    for (idx = position + 8; idx < 64; idx += 8) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }

    return validMoves;
}

    
std::vector<int> Board::generateBishopMoves(int position, bool isWhite) {
    std::vector<int> validMoves;
    int idx;


                        // UP
                // left
    for (idx = position - 9; idx > 0; idx -= 9) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }
                // right
    for (idx = position - 7;  idx > 0 && idx % 8 != 0; idx -= 7) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }

                        // DOWN
                // left
    for (idx = position + 7; idx < 64 && (idx + 1) % 8 != 0; idx += 7) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }
                // right
    for (idx = position + 9;  idx < 64 && idx % 8 != 0; idx += 9) {
        if (board[idx] == Empty) {
            validMoves.push_back(idx);
        }
        else if (isWhite ? board[idx] < 0 : board[idx] > 0) {
            validMoves.push_back(idx);
            break;
        }
        else {
            break;
        }
    }
    

    return validMoves;
}


std::vector<int> Board::generateQueenMoves(int position, bool isWhite) {
    std::vector<int> validMoves = generateBishopMoves(position, isWhite);
    std::vector<int> RookValidMoves = generateRookMoves(position, isWhite);

    validMoves.insert(validMoves.end(), RookValidMoves.begin(), RookValidMoves.end());

    return validMoves;
}

std::vector<int> Board::generateKingMoves(int position, bool isWhite) {
    std::vector<int> validMoves;

    int newRow;
    int newCol;
    int newPos;
    int rowOffsets[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    int colOffsets[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    int row = position / 8;
    int col = position % 8;


    for (int i = 0; i < 8; ++i) {
        newRow = row + rowOffsets[i];
        newCol = col + colOffsets[i];
        if (newRow >= 0 && newRow < 8 && newCol >= 0 && newCol < 8) {
            newPos = newRow * 8 + newCol;
            if (board[newPos] == Empty || (isWhite ? board[newPos] < 0 : board[newPos] > 0)) {
                validMoves.push_back(newPos);
            }
        }
    }

    return validMoves;
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

