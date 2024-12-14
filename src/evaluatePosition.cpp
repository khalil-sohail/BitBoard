#include "board.hpp"

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

double Board::eval(std::array<int, 64>& evaBoard) {
    double res = 0.0;
    
    if (botMoves < 8)
        res = evaluateOpeningsPosition(evaBoard);
    else if (botMoves < 34)
        res = evaluateMiddlePosition(evaBoard);
    else
        res = evaluateEndPosition(evaBoard);
    return (res);
}

double Board::Eval() {
    double res = 0.0;
    
    if (botMoves < 8)
        res = evaluateOpeningsPosition(board);
    else if (botMoves < 34)
        res = evaluateMiddlePosition(board);
    else
        res = evaluateEndPosition(board);
    return (res);
}

double Board::evaluateOpeningsPosition(std::array<int, 64>& evaBoard) {
    double evaluation = 0.0;

    for (int i = 0; i < 64; i++) {
        // if (lastBestTo == i)
        //     evaluation -= 4;
        switch (evaBoard[i]) {
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


double Board::evaluateMiddlePosition(std::array<int, 64>& evaBoard) {
    double res = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (evaBoard[i] == -4)
            res += -3;
        else if (evaBoard[i] == 4)
            res += 3;
        else
            res += evaBoard[i];
    }
    return (res);
}

double Board::evaluateEndPosition(std::array<int, 64>& evaBoard) {
    double res = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (evaBoard[i] == -4)
            res += -3;
        else if (evaBoard[i] == 4)
            res += 3;
        else
            res += evaBoard[i];
    }
    return (res);
}

// double Board::evaluateMiddlePosition(std::array<int, 64>& evaBoard) {
//     double evaluation = 0.0;

//     for (int i = 0; i < 64; i++) {
//         switch (evaBoard[i]) {
//             case WhitePawn:
//                 evaluation += (1) + (WHITE_PAWN_SQUARES[i]) * 0.3;
//                 break;
//             case BlackPawn:
//                 evaluation += (-1) + (BLACK_PAWN_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteKnight:
//                 evaluation += (3) + (WHITE_KNIGHT_SQUARES[i]) * 0.3;
//                 break;
//             case BlackKnight:
//                 evaluation += (-3) + (BLACK_KNIGHT_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteBishop:
//                 evaluation += (3) + (WHITE_BISHOP_SQUARES[i]) * 0.3;
//                 break;
//             case BlackBishop:
//                 evaluation += (-3) + (BLACK_BISHOP_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteRook:
//                 evaluation += (5) + (WHITE_ROOK_SQUARES[i]) * 0.3;
//                 break;
//             case BlackRook:
//                 evaluation += (-5) + (BLACK_ROOK_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteQueen:
//                 evaluation += (9) + (WHITE_QUEEN_SQUARES[i]) * 0.3;
//                 break;
//             case BlackQueen:
//                 evaluation += (-9) + (BLACK_QUEEN_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteKing:
//                 evaluation += (1000) + (WHITE_KING_MG_SQUARES[i]) * 0.3;
//                 break;
//             case BlackKing:
//                 evaluation += (-1000) + (BLACK_KING_MG_SQUARES[i]) * 0.3;
//                 break;
//         }
//     }
//     return evaluation;
// }


// double Board::evaluateEndPosition(std::array<int, 64>& evaBoard) {
//     double evaluation = 0.0;

//     for (int i = 0; i < 64; i++) {
//         switch (evaBoard[i]) {
//             case WhitePawn:
//                 evaluation += (1) + (WHITE_PAWN_SQUARES[i]) * 0.3;
//                 break;
//             case BlackPawn:
//                 evaluation += (-1) + (BLACK_PAWN_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteKnight:
//                 evaluation += (3) + (WHITE_KNIGHT_SQUARES[i]) * 0.3;
//                 break;
//             case BlackKnight:
//                 evaluation += (-3) + (BLACK_KNIGHT_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteBishop:
//                 evaluation += (3) + (WHITE_BISHOP_SQUARES[i]) * 0.3;
//                 break;
//             case BlackBishop:
//                 evaluation += (-3) + (BLACK_BISHOP_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteRook:
//                 evaluation += (5) + (WHITE_ROOK_SQUARES[i]) * 0.3;
//                 break;
//             case BlackRook:
//                 evaluation += (-5) + (BLACK_ROOK_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteQueen:
//                 evaluation += (9) + (WHITE_QUEEN_SQUARES[i]) * 0.3;
//                 break;
//             case BlackQueen:
//                 evaluation += (-9) + (BLACK_QUEEN_SQUARES[i]) * 0.3;
//                 break;

//             case WhiteKing:
//                 evaluation += (1000) + (WHITE_KING_MG_SQUARES[i]) * 0.3;
//                 break;
//             case BlackKing:
//                 evaluation += (-1000) + (BLACK_KING_MG_SQUARES[i]) * 0.3;
//                 break;
//         }
//     }
//     return evaluation;
// }

