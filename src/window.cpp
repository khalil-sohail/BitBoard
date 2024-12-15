#include "window.hpp"

void ChessDraw::loadTextures() {
    const std::string whitePieceFiles[6] = {
        "TPNG/white_pawn.png", "TPNG/white_knight.png", "TPNG/white_bishop.png", 
        "TPNG/white_rook.png", "TPNG/white_queen.png", "TPNG/white_king.png"
    };
    const std::string blackPieceFiles[6] = {
        "TPNG/black_pawn.png", "TPNG/black_knight.png", "TPNG/black_bishop.png", 
        "TPNG/black_rook.png", "TPNG/black_queen.png", "TPNG/black_king.png"
    };

    for (int i = 0; i < 6; ++i) {
        if (!whitePieceTextures[i].loadFromFile(whitePieceFiles[i])) {
            std::cerr << "Failed to load texture: " << whitePieceFiles[i] << std::endl;
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (!blackPieceTextures[i].loadFromFile(blackPieceFiles[i])) {
            std::cerr << "Failed to load texture: " << blackPieceFiles[i] << std::endl;
        }
    }
    highlightSquare.setSize(sf::Vector2f(SQUARE_SIZE, SQUARE_SIZE));
    highlightSquare.setFillColor(sf::Color(255, 255, 0, 100));
}

int ChessDraw::getSquareFromMousePos(sf::Vector2i mousePos) {
    int col = mousePos.x / static_cast<int>(SQUARE_SIZE);
    int row = mousePos.y / static_cast<int>(SQUARE_SIZE);
    return row * 8 + col;
}

void ChessDraw::drawBoard() {
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
                    case 1: textureIndex = 0; break;
                    case 3: textureIndex = 1; break;
                    case 4: textureIndex = 2; break;
                    case 5: textureIndex = 3; break;
                    case 9: textureIndex = 4; break;
                    case 100000: textureIndex = 5; break;
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
}

ChessDraw::ChessDraw() : window(sf::VideoMode(800, 800), "Chess Board") {
    botMoves = 0;
    lastBestTo = -1;
    loadTextures();
    window.setFramerateLimit(60);
}

void ChessDraw::render() {
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
                        if (canWhiteKingCastle && board[fromSquare] == WhiteKing && board[clickedSquare] == WhiteRook) {
                            if (clickedSquare == 63 && canWhiteCastleKingSide) {
                                if (whiteCastlingKingSide(board) == 0)
                                    isWhiteTurn = false;
                            }
                            else if (clickedSquare == 56 && canWhiteCastleQueenSide) {
                                if (whiteCastlingQueenSide(board) == 0)
                                    isWhiteTurn = false;
                            }
                        }
                        else if (clickedSquare != fromSquare) {
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

int	closeWindow()
{
    exit(EXIT_SUCCESS);
    return (0);
}
