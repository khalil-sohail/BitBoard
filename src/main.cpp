// #include <SFML/Graphics.hpp>
#include "board.hpp"

int main()
{
    Board board;
    std::map<int, std::vector<int>> allowed;

    board.printBoard();
    allowed = board.generateAllMoves(true);
    // std::cout << "size = " << allowed.size() << std::endl;
    board.printPossibleMoves(allowed);
    std::cout << "\n||||||\n\n" << std::endl;
    board.printPossibleMoves(board.generateAllMoves(false));


    // std::map<int, std::vector<int>>::iterator it = allowed.begin();
    // while (it != allowed.end()) {
    //     std::cout << "Key: " << it->first
    //          << ", Value: " << it->second << std::endl;
    //     ++it;
    // }
    return (0);
}
