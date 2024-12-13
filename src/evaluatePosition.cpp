#include "board.hpp"

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
                evaluation += (1) + WHITE_PAWN_SQUARES[i];
                break;
            case WhiteKnight:
                evaluation += (3) + WHITE_KNIGHT_SQUARES[i];
                break;
            case WhiteBishop:
                evaluation += (3) + WHITE_BISHOP_SQUARES[i];
                break;
            case WhiteRook:
                evaluation += (5) + WHITE_ROOK_SQUARES[i];
                break;
            case WhiteQueen:
                evaluation += (9) + WHITE_QUEEN_SQUARES[i];
                break;
            case WhiteKing:
                evaluation += (10) + WHITE_KING_MG_SQUARES[i];
                break;

            // Black Pieces (Negative Values)
            case BlackPawn:
                evaluation += (-1) + BLACK_PAWN_SQUARES[i];
                break;
            case BlackKnight:
                evaluation += (-3) + BLACK_KNIGHT_SQUARES[i];
                break;
            case BlackBishop:
                evaluation += (-3) + BLACK_BISHOP_SQUARES[i];
                break;
            case BlackRook:
                evaluation += (-5) + BLACK_ROOK_SQUARES[i];
                break;
            case BlackQueen:
                evaluation += (-9) + BLACK_QUEEN_SQUARES[i];
                break;
            case BlackKing:
                evaluation += (-10) + BLACK_KING_MG_SQUARES[i];
                break;
        }
    }
    return evaluation;
}

