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

int	closeWindow()
{
    exit(EXIT_SUCCESS);
    return (0);
}
