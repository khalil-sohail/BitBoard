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
        if (*it == 4)
            canBlackKingCastle = false;
        else if (*it == 60)
            canWhiteKingCastle = false;
        else if (*it == 7)
            canBlackCastleKingSide = false;
        else if (*it == 63)
            canWhiteCastleKingSide = false;
        else if (*it == 0)
            canBlackCastleQueenSide = false;
        else if (*it == 56)
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

int Board::whiteCastlingKingSide(std::array<int, 64>& tmpBoard) {
    if (tmpBoard[61] != Empty || tmpBoard[62] != Empty)
        return (1);
    canBlackKingCastle = false;
    tmpBoard[62] = tmpBoard[60];
    tmpBoard[60] = Empty;
    tmpBoard[61] = tmpBoard[63];
    tmpBoard[63] = Empty;
    return (0);
}

int Board::whiteCastlingQueenSide(std::array<int, 64>& tmpBoard) {
    if (tmpBoard[57] != Empty || tmpBoard[58] != Empty || tmpBoard[59] != Empty)
        return (1);
    canBlackKingCastle = false;
    tmpBoard[58] = tmpBoard[60];
    tmpBoard[60] = Empty;
    tmpBoard[59] = tmpBoard[56];
    tmpBoard[56] = Empty;
    return (0);
}


int Board::blackCastlingKingSide(std::array<int, 64>& tmpBoard) {
    if (tmpBoard[5] != Empty || tmpBoard[6] != Empty)
        return (1);
    canBlackKingCastle = false;
    tmpBoard[6] = tmpBoard[4];
    tmpBoard[4] = Empty;
    tmpBoard[5] = tmpBoard[7];
    tmpBoard[7] = Empty;
    return (0);
}

int Board::blackCastlingQueenSide(std::array<int, 64>& tmpBoard) {
    if (tmpBoard[1] != Empty || tmpBoard[2] != Empty || tmpBoard[3] != Empty)
        return (1);
    canBlackKingCastle = false;
    tmpBoard[2] = tmpBoard[4];
    tmpBoard[4] = Empty;
    tmpBoard[3] = tmpBoard[0];
    tmpBoard[0] = Empty;
    return (0);
}

int iD = 3;

int Board::moveTo(bool isWhite) {
    bool kingSide = (isWhite) ? canWhiteCastleKingSide : canBlackCastleKingSide ;
    bool queenSide = (isWhite) ?  canWhiteCastleQueenSide: canBlackCastleQueenSide ;
    // double eva = minimaxi(iD, isWhite, -1000000, 1000000, *this);
    double eva = minimaxi(iD, isWhite, *this);
    
    if (canBlackKingCastle && (kingSide || queenSide)) {
        std::array<int, 64> b1 = board;
        std::array<int, 64> b2 = board;
        int t1, t2;
        if (kingSide) {
            (isWhite) ? t1 = whiteCastlingKingSide(b1) : t1 = blackCastlingKingSide(b1);
        }
        if (queenSide) {
            (isWhite) ? t2 = whiteCastlingQueenSide(b2) : t2 = blackCastlingQueenSide(b2);
        }
        double tmpEva1 = eval(b1);
        double tmpEva2 = eval(b2);
        if (t1 == 0 && tmpEva1 < eva) {
            board = b1;
            eva = tmpEva1;
        }
        else if (t2 == 0 && tmpEva2 < eva) {
            board = b2;
            eva = tmpEva2;
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

double Board::minimaxi(int depth, bool isWhite, double alpha, double beta, Board& currentBoard) {
    if (depth == 0) return currentBoard.eval(currentBoard.getBoard());

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
                double currEval = minimaxi(depth - 1, alpha, beta, false, nextBoard);
                alpha = (alpha > currEval) ? alpha : currEval;
                if (currEval > maxEval) {
                    maxEval = currEval;
                    bestFromLocal = from;
                    bestToLocal = to;
                }
                if (beta <= alpha)
                    break;
            }
            if (beta <= alpha)
                break;
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
                double currEval = minimaxi(depth - 1, alpha, beta, true, nextBoard);
                beta = (beta < currEval) ? beta : currEval;
                if (currEval < minEval) {
                    minEval = currEval;
                    bestFromLocal = from;
                    bestToLocal = to;
                }
                if (beta <= alpha)
                    break;
            }
            if (beta <= alpha)
                break;
        }

        if (depth == iD) {
            bestFrom = bestFromLocal;
            bestTo = bestToLocal;
        }
        return minEval;
    }
}

double Board::minimaxi(int depth, bool isWhite, Board& currentBoard) {
    if (depth == 0) return currentBoard.eval(currentBoard.getBoard());

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


