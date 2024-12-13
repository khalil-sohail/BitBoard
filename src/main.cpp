#include "board.hpp"
class ChessRenderer : public Board {
private:
    sf::RenderWindow window;
    sf::Texture whitePieceTextures[6];  // White piece textures
    sf::Texture blackPieceTextures[6];  // Black piece textures
    const float SQUARE_SIZE = 100.0f;

    void loadTextures() {
        const std::string whitePieceFiles[6] = {
            "TPNG/white_pawn.png", "TPNG/white_knight.png", "TPNG/white_bishop.png", 
            "TPNG/white_rook.png", "TPNG/white_queen.png", "TPNG/white_king.png"
        };
        const std::string blackPieceFiles[6] = {
            "TPNG/black_pawn.png", "TPNG/black_knight.png", "TPNG/black_bishop.png", 
            "TPNG/black_rook.png", "TPNG/black_queen.png", "TPNG/black_king.png"
        };

        // Load white piece textures
        for (int i = 0; i < 6; ++i) {
            if (!whitePieceTextures[i].loadFromFile(whitePieceFiles[i])) {
                std::cerr << "Failed to load texture: " << whitePieceFiles[i] << std::endl;
            }
        }

        // Load black piece textures
        for (int i = 0; i < 6; ++i) {
            if (!blackPieceTextures[i].loadFromFile(blackPieceFiles[i])) {
                std::cerr << "Failed to load texture: " << blackPieceFiles[i] << std::endl;
            }
        }
    }

    void drawBoard() {
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                // Draw square
                sf::RectangleShape square(sf::Vector2f(SQUARE_SIZE, SQUARE_SIZE));
                square.setPosition(col * SQUARE_SIZE, row * SQUARE_SIZE);
                
                // Alternate square colors
                square.setFillColor((row + col) % 2 == 0 ? 
                    sf::Color(240, 217, 181) :  // Light square 
                    sf::Color(181, 136, 99)     // Dark square
                );

                window.draw(square);

                // Draw piece if not empty
                int pieceValue = board[row * 8 + col];
                if (pieceValue != Empty) {
                    sf::Sprite pieceSprite;
                    
                    // Determine piece type and color
                    int absValue = std::abs(pieceValue);
                    int textureIndex = -1;
                    
                    // Map absolute piece values to texture indices
                    switch(absValue) {
                        case 1: textureIndex = 0; break;  // Pawn
                        case 3: textureIndex = 1; break;  // Knight
                        case 4: textureIndex = 2; break;  // Bishop
                        case 5: textureIndex = 3; break;  // Rook
                        case 9: textureIndex = 4; break;  // Queen
                        case 10: textureIndex = 5; break; // King
                    }
                    
                    // Select texture based on piece color
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
    }

public:
    ChessRenderer() 
        : window(sf::VideoMode(800, 800), "Chess Board") {
        loadTextures();
        window.setFramerateLimit(60);
    }

    void render() {
        while (window.isOpen()) {
            sf::Event event;
            while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();
            }

            window.clear(sf::Color::White);
            drawBoard();
            window.display();
        }
    }
};


int main() {
    ChessRenderer renderer;
    renderer.render();
    return 0;
}



























// int main()
// {
//     Board board;
//     std::string line;
//     int nbr[3];
//     int i;

//     // board.printBoard();
//     // std::cout << "white\n" << std::endl;
//     // board.printPossibleMoves(board.generateAllMoves(true));
//     // std::cout << "black\n" << std::endl;
//     // board.printPossibleMoves(board.generateAllMoves(false));

//         // bot play white
//     board.moveTo(true);
//     board.printBoard();
//     while (std::getline(std::cin, line)) {
//         std::istringstream is(line);
//         std::string word;
//         i = 0;
//         while (is >> word) {
//             nbr[i++] = std::atoi(word.c_str());
//         }
//         if (i > 1) {
//             if  (board.moveTo(nbr[0], nbr[1]) != 0)
//                 continue;
//             board.printBoard();
//             board.moveTo(true);
//             board.printBoard();
//             std::cout << "evalution= " << board.eval() << std::endl;
//         }
//     }

//         // bot play black
//     // board.printBoard();
//     // while (std::getline(std::cin, line)) {
//     //     std::istringstream is(line);
//     //     std::string word;
//     //     i = 0;
//     //     while (is >> word) {
//     //         nbr[i++] = std::atoi(word.c_str());
//     //     }
//     //     if (i > 1) {
//     //         board.moveTo(nbr[0], nbr[1]);
//     //         board.printBoard();
//     //         board.moveTo(false);
//     //         board.printBoard();
//     //         std::cout << "evalution= " << board.eval() << std::endl;
//     //     }
//     // }

//     return (0);
// }
