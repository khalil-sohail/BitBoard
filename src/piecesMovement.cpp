#include "board.hpp"

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
        if (*it == 63 || *it == 60)
            canWhiteCastleKingSide = false;
        if (*it == 56 || *it == 60)
            canWhiteCastleQueenSide = false;
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

int Board::whiteCastlingKingSide(bool isWhite) {
    if (board[61] != Empty || board[62] != Empty)
        return (1);
    (void)isWhite;
    canWhiteCastleQueenSide = false;
    canWhiteCastleKingSide = false;
        // king move
    board[62] = board[60];
    board[60] = Empty;
        // rook move
    board[61] = board[63];
    board[63] = Empty;
    return (0);
}

int Board::whiteCastlingQueenSide(bool isWhite) {
    if (board[57] != Empty || board[58] != Empty || board[59] != Empty)
        return (1);
    (void)isWhite;
    canWhiteCastleQueenSide = false;
    canWhiteCastleKingSide = false;
        // king move
    board[58] = board[60];
    board[60] = Empty;
        // rook move
    board[59] = board[56];
    board[56] = Empty;
    return (0);
}


int Board::blackCastlingKingSide(bool isWhite) {
    if (board[5] != Empty || board[6] != Empty)
        return (1);
    (void)isWhite;
    if (isWhite) {
        canWhiteCastleQueenSide = false;
        canWhiteCastleKingSide = false;
    }
    else {
        canBlackCastleQueenSide = false;
        canWhiteCastleKingSide = false;
    }
        // king move
    board[6] = board[4];
    board[4] = Empty;
        // rook move
    board[5] = board[7];
    board[7] = Empty;
    return (0);
}

int Board::blackCastlingQueenSide(bool isWhite) {
    if (board[1] != Empty || board[2] != Empty || board[3] != Empty)
        return (1);
    (void)isWhite;
    if (isWhite) {
        canWhiteCastleQueenSide = false;
        canWhiteCastleKingSide = false;
    }
    else {
        canBlackCastleQueenSide = false;
        canBlackCastleKingSide = false;
    }
        // king move
    board[2] = board[4];
    board[4] = Empty;
        // rook move
    board[3] = board[0];
    board[0] = Empty;
    return (0);
}

int iD = 3;

int Board::moveTo(bool isWhite) {
    double eva1 = -1000;
    double eva2 = -1000;
    bool   e1 = true;
    bool   e2 = true;
    double eva = minimaxi(iD, isWhite, *this);
    
    if (board[0] == BlackRook || board[7] == BlackRook) {
        std::cout << "HERE-> " << board[0]<< ", " << board[7] << "\n";
        Board nextBoard1 = *this;
        if (nextBoard1.blackCastlingKingSide(isWhite) == 1 || board[7] != BlackRook) {
            e1 = false;
        }
        else
            eva1 = nextBoard1.eval();
        
        Board nextBoard2 = *this;
        if (nextBoard2.blackCastlingQueenSide(isWhite) == 1 || board[0] != BlackRook) {
            e2 = false;
        }
        else
            eva2 = nextBoard2.eval();

        std::cout << eva << ", " << eva1 << ", " << eva2 << ", " << std::endl;

        if (e1 == true && eva1 < eva) {
            blackCastlingKingSide(isWhite);
            eva = eva1;
        }
        else if (e2 == true && eva2 < eva) {
            blackCastlingQueenSide(isWhite);
            eva = eva2;
        }
        else {
            board[bestTo] = board[bestFrom];
            board[bestFrom] = Empty;
        }
    }
    else {
        board[bestTo] = board[bestFrom];
        board[bestFrom] = Empty;
    }

    
    botMoves++;
    lastBestTo = bestTo;
    std::cout << "evalution-> " << eva << "\n";

    return 0;
}

double Board::minimaxi(int depth, bool isWhite, Board& currentBoard) {
    if (depth == 0) return currentBoard.eval();

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

        if (depth == iD) {
            bestFrom = bestFromLocal;
            bestTo = bestToLocal;
        }
        return minEval;
    }
}



