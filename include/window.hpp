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

    void drawBoard() {
        window.clear(sf::Color::White);

        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                sf::RectangleShape square(sf::Vector2f(SQUARE_SIZE, SQUARE_SIZE));
                square.setPosition(col * SQUARE_SIZE, row * SQUARE_SIZE);
                
                square.setFillColor((row + col) % 2 == 0 ? 
                    sf::Color(240, 217, 181) : sf::Color(181, 136, 99)
                );

                window.draw(square);

                int pieceValue = board[row * 8 + col];
                if (pieceValue != Empty) {
                    sf::Sprite pieceSprite;
                    
                    int absValue = std::abs(pieceValue);
                    int textureIndex = -1;
                    
                    switch(absValue) {
                        case 1: textureIndex = 0; break;  // Pawn
                        case 3: textureIndex = 1; break;  // Knight
                        case 4: textureIndex = 2; break;  // Bishop
                        case 5: textureIndex = 3; break;  // Rook
                        case 9: textureIndex = 4; break;  // Queen
                        case 10: textureIndex = 5; break; // King
                    }
                    
                    if (textureIndex != -1) {
                        pieceSprite.setTexture(pieceValue > 0 ? 
                            whitePieceTextures[textureIndex] : 
                            blackPieceTextures[textureIndex]
                        );
                        
                        pieceSprite.setPosition(col * SQUARE_SIZE, row * SQUARE_SIZE);
                        pieceSprite.setScale(
                            SQUARE_SIZE / pieceSprite.getLocalBounds().width, 
                            SQUARE_SIZE / pieceSprite.getLocalBounds().height
                        );
                        window.draw(pieceSprite);
                    }
                }
            }
        }

        if (selectedPieceIndex.has_value()) {
            int selectedSquare = selectedPieceIndex.value();
            int col = selectedSquare % 8;
            int row = selectedSquare / 8;
            highlightSquare.setPosition(col * SQUARE_SIZE, row * SQUARE_SIZE);
            window.draw(highlightSquare);
        }

        // Display current turn
        // sf::Font font;
        // if (font.loadFromFile("arial.ttf")) {
        //     sf::Text turnText;
        //     turnText.setFont(font);
        //     turnText.setString(isWhiteTurn ? "White's Turn" : "Black's Turn");
        //     turnText.setCharacterSize(24);
        //     turnText.setFillColor(sf::Color::Black);
        //     turnText.setPosition(10, 10);
        //     window.draw(turnText);
        // }
    }

public:
    ChessDraw() 
        : window(sf::VideoMode(800, 800), "Chess Board") {
        // srand(time(nullptr));
        
        loadTextures();
        window.setFramerateLimit(60);
    }

    void render() {
        while (window.isOpen()) {
            sf::Event event;
            while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();
                if (isWhiteTurn && event.type == sf::Event::MouseButtonPressed) {
                    if (event.mouseButton.button == sf::Mouse::Left) {
                        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                        int clickedSquare = getSquareFromMousePos(mousePos);
                        if (!selectedPieceIndex.has_value()) {
                            if (board[clickedSquare] > 0) {
                                selectedPieceIndex = clickedSquare;
                            }
                        } 
                        else {
                            int fromSquare = selectedPieceIndex.value();
                            if (clickedSquare != fromSquare) {
                                if (moveTo(fromSquare, clickedSquare) == 0) {
                                    drawBoard();
                                    window.display();
                                    isWhiteTurn = false;
                                }
                            }
                            selectedPieceIndex.reset();
                        }
                    }
                }
            }

            if (!isWhiteTurn) {
                int blackMove = moveTo(false);
                if (blackMove != -1) {
                    std::cout << "Black AI made a move" << std::endl;
                    isWhiteTurn = true;
                    drawBoard();
                    window.display();
                }
            }
            drawBoard();
            window.display();
        }
    }
};


#endif