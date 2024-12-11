#ifndef BOARD_HPP
#define BOARD_HPP

#include <array>
#include <vector>
#include <iostream>
#include <map>
#include <algorithm>
#include <sstream>
#include <string>

enum ChessPiece {
    Empty = 0,
    WhitePawn = 1, BlackPawn = -1,
    WhiteRook = 2, BlackRook = -2,
    WhiteKnight = 3, BlackKnight = -3,
    WhiteBishop = 4, BlackBishop = -4,
    WhiteQueen = 5, BlackQueen = -5,
    WhiteKing = 6, BlackKing = -6
};

class Board {
    private:
        // std::array<int, 64> board = {
        //     BlackRook,   BlackKnight, BlackBishop, BlackQueen, BlackKing, BlackBishop, BlackKnight, BlackRook,
        //     BlackPawn,   BlackPawn,   BlackPawn,   BlackPawn,  BlackPawn, BlackPawn,   BlackPawn,   BlackPawn,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     Empty,       Empty,       Empty,       Empty,      Empty,     Empty,       Empty,       Empty,
        //     WhitePawn,   WhitePawn,   WhitePawn,   WhitePawn,  WhitePawn, WhitePawn,   WhitePawn,   WhitePawn,
        //     WhiteRook,   WhiteKnight, WhiteBishop, WhiteQueen, WhiteKing, WhiteBishop, WhiteKnight, WhiteRook
        // };
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
    public:
        // Board();

        void printBoard();
        void printPossibleMoves(const std::map<int, std::vector<int>>& allMoves);
        std::vector<int> generatePawnMoves(int position, bool isWhite);
        std::vector<int> generateBishopMoves(int position, bool isWhite);
        std::vector<int> generateKnightMoves(int position, bool isWhite);
        std::vector<int> generateRookMoves(int position, bool isWhite);
        std::vector<int> generateQueenMoves(int position, bool isWhite);
        std::vector<int> generateKingMoves(int position, bool isWhite);
        std::map<int, std::vector<int>> generateAllMoves(bool isWhite);

        int moveTo(int ip, int fp);
};





#endif