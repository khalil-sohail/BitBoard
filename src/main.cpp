// #include <SFML/Graphics.hpp>
#include "board.hpp"

int main()
{
    Board board;
    std::map<int, std::vector<int>> allowed;
    std::string line;
    int nbr[3];
    int i;
    
    board.printBoard();
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string word;
        i = 0;
        while (is >> word) {
            nbr[i++] = std::atoi(word.c_str());
        }
        board.moveTo(nbr[0], nbr[1]);

        board.printBoard();
    }

    // allowed = board.generateAllMoves(true);
    // std::cout << "white\n" << std::endl;
    // board.printPossibleMoves(allowed);
    // std::cout << "black\n" << std::endl;
    // board.printPossibleMoves(board.generateAllMoves(false));


    // std::map<int, std::vector<int>>::iterator it = allowed.begin();
    // while (it != allowed.end()) {
    //     std::cout << "Key: " << it->first
    //          << ", Value: " << it->second << std::endl;
    //     ++it;
    // }
    return (0);
}
