// #include <SFML/Graphics.hpp>
#include "board.hpp"

int main()
{
    Board board;
    std::string line;
    int nbr[3];
    int i;

    board.printBoard();
    // std::cout << "white\n" << std::endl;
    // board.printPossibleMoves(board.generateAllMoves(true));
    std::cout << "black\n" << std::endl;
    board.printPossibleMoves(board.generateAllMoves(false));

        // bot play white
    // board.moveTo(true);
    // board.printBoard();
    // while (std::getline(std::cin, line)) {
    //     std::istringstream is(line);
    //     std::string word;
    //     i = 0;
    //     while (is >> word) {
    //         nbr[i++] = std::atoi(word.c_str());
    //     }
    //     if (i > 1) {
    //         board.moveTo(nbr[0], nbr[1]);
    //         board.printBoard();
    //         board.moveTo(true);
    //         board.printBoard();
    //         std::cout << "evalution= " << board.eval() << std::endl;
    //     }
    // }

        // bot play black
    // board.printBoard();
    // while (std::getline(std::cin, line)) {
    //     std::istringstream is(line);
    //     std::string word;
    //     i = 0;
    //     while (is >> word) {
    //         nbr[i++] = std::atoi(word.c_str());
    //     }
    //     if (i > 1) {
    //         board.moveTo(nbr[0], nbr[1]);
    //         board.printBoard();
    //         board.moveTo(false);
    //         board.printBoard();
    //         std::cout << "evalution= " << board.eval() << std::endl;
    //     }
    // }

    return (0);
}
