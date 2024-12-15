#ifndef BOARD_HPP
#define BOARD_HPP

#include <array>
#include <vector>
#include <iostream>
#include <map>
#include <algorithm>
#include <sstream>
#include <limits>
#include <string>
#include <SFML/Graphics.hpp>
#include "pieces.hpp"

enum ChessPiece {
    Empty = 0,
    WhitePawn = 1,   BlackPawn = -1,
    WhiteKnight = 3, BlackKnight = -3,
    WhiteBishop = 4, BlackBishop = -4,
    WhiteRook = 5,   BlackRook = -5,
    WhiteQueen = 9,  BlackQueen = -9,
    WhiteKing = 100000,  BlackKing = -100000
};

enum GameStage {
    OPENING,
    MIDDLEGAME,
    ENDGAME
};

class Board {
    private:
        static constexpr double MATERIAL_WEIGHT = 1.0;
        static constexpr double POSITION_WEIGHT = 0.5;
        static constexpr double MOBILITY_WEIGHT = 0.2;
    protected:
        std::array<int, 64> board = {
            BlackRook,   BlackKnight, BlackBishop, BlackQueen, BlackKing, BlackBishop, BlackKnight, BlackRook,
            BlackPawn,   BlackPawn,   BlackPawn,   BlackPawn,  BlackPawn, BlackPawn,   BlackPawn,   BlackPawn,
            Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
            Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
            Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
            Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
            WhitePawn,   WhitePawn,   WhitePawn,   WhitePawn,  WhitePawn, WhitePawn,   WhitePawn,   WhitePawn,
            WhiteRook,   WhiteKnight, WhiteBishop, WhiteQueen, WhiteKing, WhiteBishop, WhiteKnight, WhiteRook
        };
        // std::array<int, 64> board = {
        //     BlackRook,   Empty,       Empty,       Empty,      BlackKing, Empty,       Empty,       BlackRook,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       WhitePawn,       Empty,
        //     WhiteRook,   Empty,       Empty,       Empty,      WhiteKing, Empty,       Empty,       WhiteRook
        // };
        std::map<int, std::vector<int>> allowed;
        double evalBar;
        int bestFrom;
        int bestTo;
        int lastBestTo;
        int botMoves;
        bool canWhiteCastleKingSide = true;
        bool canWhiteCastleQueenSide = true;
        bool canWhiteKingCastle = true;
        bool canBlackKingCastle = true;
        bool canBlackCastleKingSide = true;
        bool canBlackCastleQueenSide = true;
    public:
        // Board() { }

        void printBoard();
        void printPossibleMoves(const std::map<int, std::vector<int>>& allMoves);
        std::vector<int> generatePawnMoves(int position, bool isWhite);
        std::vector<int> generateBishopMoves(int position, bool isWhite);
        std::vector<int> generateKnightMoves(int position, bool isWhite);
        std::vector<int> generateRookMoves(int position, bool isWhite);
        std::vector<int> generateQueenMoves(int position, bool isWhite);
        std::vector<int> generateKingMoves(int position, bool isWhite);
        std::map<int, std::vector<int>> generateAllMoves(bool isWhite);

        double eval(std::array<int, 64>& evaBoard);
        GameStage evaluateGameStage(std::array<int, 64>& evaBoard);
        double evaluatePiecePosition(ChessPiece piece, int square, GameStage stage);
        bool isWhitePiece(int piece);
        bool isBlackPiece(int piece);

        double minimaxi(int depth, bool isWhite, double alpha, double beta, Board& currentBoard);
        double minimaxi(int depth, bool isWhite, Board& currentBoard);

        int whiteCastlingKingSide(std::array<int, 64>& tmpBoard);
        int whiteCastlingQueenSide(std::array<int, 64>& tmpBoard);
        int blackCastlingKingSide(std::array<int, 64>& tmpBoard);
        int blackCastlingQueenSide(std::array<int, 64>& tmpBoard);

        void undoMove(int from, int to);
        void move(int from, int to);
        int moveTo(int ip, int fp);
        int moveTo(bool isWhite);

        std::array<int, 64>& getBoard() {
            return (board);
        }
};

#include "window.hpp"




#endif