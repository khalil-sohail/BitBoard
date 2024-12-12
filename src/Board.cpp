#include "board.hpp"

// Board::Board() {}

std::map<int, std::vector<int>> Board::generateAllMoves(bool isWhite) {
    std::map<int, std::vector<int>> allMoves;

    for (int i = 0; i < 64; ++i) {
        if (board[i] == Empty) continue;
        bool pieceisWhite = board[i] > 0;
        if (pieceisWhite != isWhite) continue;
        else if (board[i] == WhitePawn || board[i] == BlackPawn) {
            auto pawnMoves = generatePawnMoves(i, isWhite);
            if (pawnMoves.empty() == 0) {
                allMoves[i] = pawnMoves;
            }
        }
        else if (board[i] == WhiteRook || board[i] == BlackRook) {
            auto rookMoves = generateRookMoves(i, isWhite);
            if (rookMoves.empty() == 0) {
                allMoves[i] = rookMoves;
            }
        }
        else if (board[i] == WhiteBishop || board[i] == BlackBishop) {
            auto rookMoves = generateBishopMoves(i, isWhite);
            if (rookMoves.empty() == 0) {
                allMoves[i] = rookMoves;
            }
        }
        else if (board[i] == WhiteQueen || board[i] == BlackQueen) {
            auto rookMoves = generateQueenMoves(i, isWhite);
            if (rookMoves.empty() == 0) {
                allMoves[i] = rookMoves;
            }
        }
        else if (board[i] == WhiteKing || board[i] == BlackKing) {
            auto rookMoves = generateKingMoves(i, isWhite);
            if (rookMoves.empty() == 0) {
                allMoves[i] = rookMoves;
            }
        }
        else if (board[i] == WhiteKnight || board[i] == BlackKnight) {
            auto rookMoves = generateKnightMoves(i, isWhite);
            if (rookMoves.empty() == 0) {
                allMoves[i] = rookMoves;
            }
        }
    }

    return allMoves;
}

std::vector<int> Board::generatePawnMoves(int position, bool isWhite) {
    std::vector<int> validMoves;
    int direction = isWhite ? -1 : 1;
    int startRow = position / 8;

    int forward = position + 8 * direction;
    if (forward >= 0 && forward < 64 && board[forward] == Empty) {
        validMoves.push_back(forward);

        int doubleForward = position + 16 * direction;
        if (((isWhite && startRow == 6) || (!isWhite && startRow == 1)) && board[doubleForward] == Empty) {
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

std::vector<int> Board::generateKnightMoves(int position, bool isWhite) {
    std::vector<int> validMoves;
    int newRow;
    int newCol;
    int newPos;
    int rowOffsets[8] = {-2, +2, -2, +2, -1, -1, +1, +1};
    int colOffsets[8] = {-1, -1, +1, +1, -2, +2, -2, +2};
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


int Board::moveTo(int ip, int fp) {
    std::vector<int> validMoves;


    bool isWhite = board[ip] > 0;
    if (ip >= 64 || ip < 0 || board[ip] == Empty) {
        std::cout << "square is empty";
        return (1);
    }
    else if (board[ip] == WhitePawn || board[ip] == BlackPawn) {
        auto pawnMoves = generatePawnMoves(ip, isWhite);
        if (pawnMoves.empty() == 0) {
            validMoves = pawnMoves;
        }
    }
    else if (board[ip] == WhiteRook || board[ip] == BlackRook) {
        auto rookMoves = generateRookMoves(ip, isWhite);
        if (rookMoves.empty() == 0) {
            validMoves = rookMoves;
        }
    }
    else if (board[ip] == WhiteBishop || board[ip] == BlackBishop) {
        auto rookMoves = generateBishopMoves(ip, isWhite);
        if (rookMoves.empty() == 0) {
            validMoves = rookMoves;
        }
    }
    else if (board[ip] == WhiteQueen || board[ip] == BlackQueen) {
        auto rookMoves = generateQueenMoves(ip, isWhite);
        if (rookMoves.empty() == 0) {
            validMoves = rookMoves;
        }
    }
    else if (board[ip] == WhiteKing || board[ip] == BlackKing) {
        auto rookMoves = generateKingMoves(ip, isWhite);
        if (rookMoves.empty() == 0) {
            validMoves = rookMoves;
        }
    }
    else if (board[ip] == WhiteKnight || board[ip] == BlackKnight) {
        auto rookMoves = generateKnightMoves(ip, isWhite);
        if (rookMoves.empty() == 0) {
            validMoves = rookMoves;
        }
    }

    auto it = std::find(validMoves.begin(), validMoves.end(), fp);
    if (it != validMoves.end()) {
        board[*it] = board[ip];
        board[ip] = Empty;
    }
    else {
        std::cout << "piece Not found\n";
        return (1);
    }
    return (0);
}

void Board::move(int from, int to) {
    board[to] = board[from];
    board[from] = Empty;
}

void Board::undoMove(int from, int to) {
    board[from] = board[to];
    board[to] = Empty;
}

int iD = 3;

int Board::moveTo(bool isWhite) {
    int eva = minimaxi(iD, isWhite, *this);

    std::cout << "evalution-> " << eva << "\n";

    board[bestTo] = board[bestFrom];
    board[bestFrom] = Empty;

    return 0;
}

double Board::minimaxi(int depth, bool isWhite, Board& currentBoard) {
    if (depth == 0) return currentBoard.eval(); // Use currentBoard for evaluation

    std::map<int, std::vector<int>> allowed = currentBoard.generateAllMoves(isWhite);
    if (isWhite) {
        double maxEval = std::numeric_limits<double>::lowest();
        int bestFromLocal = -1, bestToLocal = -1;

        for (const auto& it : allowed) {
            int from = it.first;
            const std::vector<int>& itV = it.second;
            for (int m : itV) {
                double to = m;
                Board nextBoard = currentBoard;
                nextBoard.move(from, to);
                double currEval = minimaxi(depth - 1, false, nextBoard);
                
                if (currEval > maxEval) {
                    maxEval = currEval;
                    bestFromLocal = from;
                    bestToLocal = to;
                }
            }
        }
        
        // Only update global best move at the root level
        if (depth == iD) {
            bestFrom = bestFromLocal;
            bestTo = bestToLocal;
        }
        return maxEval;
    }
    else {
        double minEval = std::numeric_limits<double>::max();
        int bestFromLocal = -1, bestToLocal = -1;

        for (const auto& it : allowed) {
            int from = it.first;
            const std::vector<int>& itV = it.second;
            for (int m : itV) {
                double to = m;
                Board nextBoard = currentBoard;
                nextBoard.move(from, to);
                double currEval = minimaxi(depth - 1, true, nextBoard);
                
                if (currEval < minEval) {
                    minEval = currEval;
                    bestFromLocal = from;
                    bestToLocal = to;
                }
            }
        }
        
        // Only update global best move at the root level
        if (depth == iD) {
            bestFrom = bestFromLocal;
            bestTo = bestToLocal;
        }
        return minEval;
    }
}

double Board::eval(std::array<int, 64> sBoard) {
    double res = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (sBoard[i] == -4)
            res += -3;
        else if (sBoard[i] == 4)
            res += 3;
        else
            res += sBoard[i];
    }
    return (res);
}

double Board::eval() {
    double res = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (board[i] == -4)
            res += -3;
        else if (board[i] == 4)
            res += 3;
        else
            res += board[i];
    }
    return (res);
}

double Board::evaluatePosition() {
    double evaluation = 0.0;

    for (int i = 0; i < 64; i++) {
        int piece = board[i];
        
        switch (piece) {
            // White Pieces (Positive Values)
            case WhitePawn:
                evaluation += (1) + WHITE_PAWN_SQUARES[i] / 10.0;
                break;
            case WhiteKnight:
                evaluation += (3) + WHITE_KNIGHT_SQUARES[i] / 10.0;
                break;
            case WhiteBishop:
                evaluation += (3) + WHITE_BISHOP_SQUARES[i] / 10.0;
                break;
            case WhiteRook:
                evaluation += (5) + WHITE_ROOK_SQUARES[i] / 10.0;
                break;
            case WhiteQueen:
                evaluation += (9) + WHITE_QUEEN_SQUARES[i] / 10.0;
                break;
            case WhiteKing:
                evaluation += (10) + WHITE_KING_MG_SQUARES[i] / 10.0;
                break;

            // Black Pieces (Negative Values)
            case BlackPawn:
                evaluation += (1) + BLACK_PAWN_SQUARES[i] / 10.0;
                break;
            case BlackKnight:
                evaluation += (3) + BLACK_KNIGHT_SQUARES[i] / 10.0;
                break;
            case BlackBishop:
                evaluation += (3) + BLACK_BISHOP_SQUARES[i] / 10.0;
                break;
            case BlackRook:
                evaluation += (5) + BLACK_ROOK_SQUARES[i] / 10.0;
                break;
            case BlackQueen:
                evaluation += (9) + BLACK_QUEEN_SQUARES[i] / 10.0;
                break;
            case BlackKing:
                evaluation += (10) + BLACK_KING_MG_SQUARES[i] / 10.0;
                break;
        }
    }

    // Interpretation:
    // Positive value: White is winning
    // Negative value: Black is winning
    // Closer to zero: More balanced position
    return evaluation;
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

