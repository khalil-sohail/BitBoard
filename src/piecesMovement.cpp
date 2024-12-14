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
            canCastleKingSide = false;
        if (*it == 56 || *it == 60)
            canCastleQueenSide = false;
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

int Board::CastlingKingSide() {
    if (board[61] != Empty || board[62] != Empty)
        return (1);
    canCastleQueenSide = false;
    canCastleKingSide = false;
        // king move
    board[62] = board[60];
    board[60] = Empty;
        // rook move
    board[61] = board[63];
    board[63] = Empty;
    return (0);
}

int Board::CastlingQueenSide() {
    if (board[57] != Empty || board[58] != Empty || board[59] != Empty)
        return (1);
    canCastleQueenSide = false;
    canCastleKingSide = false;
        // king move
    board[58] = board[60];
    board[60] = Empty;
        // rook move
    board[59] = board[56];
    board[56] = Empty;
    return (0);
}

int iD = 3;

int Board::moveTo(bool isWhite) {
    double eva = minimaxi(iD, isWhite, *this);

    std::cout << "evalution-> " << eva << "\n";

    board[bestTo] = board[bestFrom];
    board[bestFrom] = Empty;
    botMoves++;
    lastBestTo = bestTo;

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



