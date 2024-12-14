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
    
    if (botMoves < 8)
        res = evaluateOpeningsPosition();
    else if (botMoves < 8)
        res = evaluateMiddlePosition();
    else
        res = evaluateEndPosition();
    return (res);
}

double Board::evaluateOpeningsPosition() {
    double evaluation = 0.0;

    for (int i = 0; i < 64; i++) {
        if (lastBestTo == i)
            evaluation -= 1;
        switch (board[i]) {
            case WhitePawn:
                evaluation += (1) + WHITE_OPENINGS_PAWN_SQUARES[i];
                break;
            case BlackPawn:
                evaluation += (-1) + BLACK_OPENINGS_PAWN_SQUARES[i];
                break;

            case WhiteKnight:
                evaluation += (3) + WHITE_OPENINGS_KNIGHT_SQUARES[i];
                break;
            case BlackKnight:
                evaluation += (-3) + BLACK_OPENINGS_KNIGHT_SQUARES[i];
                break;

            case WhiteBishop:
                evaluation += (3) + WHITE_OPENINGS_BISHOP_SQUARES[i];
                break;
            case BlackBishop:
                evaluation += (-3) + BLACK_OPENINGS_BISHOP_SQUARES[i];
                break;

            case WhiteRook:
                evaluation += (5) + WHITE_OPENINGS_ROOK_SQUARES[i];
                break;
            case BlackRook:
                evaluation += (-5) + BLACK_OPENINGS_ROOK_SQUARES[i];
                break;

            case WhiteQueen:
                evaluation += (9) + WHITE_OPENINGS_QUEEN_SQUARES[i];
                break;
            case BlackQueen:
                evaluation += (-9) + BLACK_OPENINGS_QUEEN_SQUARES[i];
                break;

            case WhiteKing:
                evaluation += (1000) + WHITE_OPENINGS_KING_SQUARES[i];
                break;
            case BlackKing:
                evaluation += (-1000) + BLACK_OPENINGS_KING_SQUARES[i];
                break;
        }
    }
    return evaluation;
}


double Board::evaluateMiddlePosition() {
    double evaluation = 0.0;

    for (int i = 0; i < 64; i++) {
        switch (board[i]) {
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


double Board::evaluateEndPosition() {
    double evaluation = 0.0;

    for (int i = 0; i < 64; i++) {
        switch (board[i]) {
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

