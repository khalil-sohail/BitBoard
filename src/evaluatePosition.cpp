#include "board.hpp"

bool Board::isWhitePiece(int piece) { return (piece > 0); }
bool Board::isBlackPiece(int piece) { return (piece < 0); }

GameStage Board::evaluateGameStage(std::array<int, 64>& evaBoard) {
    int whiteMaterial = 0, blackMaterial = 0;
    for (int i = 0; i < 64; i++) {
        if (isWhitePiece(evaBoard[i])) 
            whiteMaterial += evaBoard[i];
        else if (isBlackPiece(evaBoard[i])) 
            blackMaterial += evaBoard[i];
    }

    if (whiteMaterial < 15 || blackMaterial > -15) 
        return ENDGAME;    
    return whiteMaterial > 30 && blackMaterial > -30 ? MIDDLEGAME : OPENING;
}

// Evaluate piece position using piece-square tables
double Board::evaluatePiecePosition(ChessPiece piece, int square, GameStage stage) {
    bool isItWhite = (piece > 0) ? true : false;
    if (piece == 1 || piece == -1) {
        return isItWhite ? WHITE_PAWN_SQUARES[square] : BLACK_PAWN_SQUARES[square];
    }
    if (piece == 3 || piece == -3) {
        return isItWhite ? WHITE_KNIGHT_SQUARES[square] : BLACK_KNIGHT_SQUARES[square];
    }
    if (piece == 4 || piece == -4) {
        return isItWhite ? WHITE_BISHOP_SQUARES[square] : BLACK_BISHOP_SQUARES[square];
    }
    if (piece == 5 || piece == -5) {
        return isItWhite ? WHITE_ROOK_SQUARES[square] : BLACK_ROOK_SQUARES[square];
    }
    if (piece == 9 || piece == -9) {
        return isItWhite ? WHITE_QUEEN_SQUARES[square] : BLACK_QUEEN_SQUARES[square];
    }
    if (piece == 10 || piece == -10) {
        if (stage == MIDDLEGAME)
            return isItWhite ? WHITE_KING_MG_SQUARES[square] : BLACK_KING_MG_SQUARES[square];
        else
            return isItWhite ? WHITE_KING_MG_SQUARES[square] : BLACK_KING_MG_SQUARES[square];
    }
    return (0.0);
}

// Main evaluation function
double Board::eval(std::array<int, 64>& evaBoard) {
    double score = 0.0;
    GameStage stage = evaluateGameStage(evaBoard);

    for (int square = 0; square < 64; square++) {
        int piece = evaBoard[square];
        
        piece = (piece == 4) ? 3 : piece;
        piece = (piece == -4) ? -3 : piece;
        if (piece != Empty) {
            score += piece + evaluatePiecePosition((ChessPiece)piece, square, stage);
        }
    }

    // Additional evaluation factors could be added here:
    // - King safety
    // - Pawn structure
    // - Piece mobility
    // - Control of key squares

    return score;
}
