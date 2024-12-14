#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <iostream>
#include <exception>
#include "board.hpp"


class ChessDraw : public Board {
private:
    sf::RenderWindow window;
    sf::Texture whitePieceTextures[6];
    sf::Texture blackPieceTextures[6];
    const float SQUARE_SIZE = 100.0f;
    
    bool isWhiteTurn = true;
    std::optional<int> selectedPieceIndex;
    sf::RectangleShape highlightSquare;

    void loadTextures();
    int getSquareFromMousePos(sf::Vector2i mousePos);
    void drawBoard();
public:
    ChessDraw();
    void render();
};


#endif