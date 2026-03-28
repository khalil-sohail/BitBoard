#include "board.hpp"

#include <iostream>

char Board::pieceToChar(Color color, PieceType piece) const {
    char c = '.';
    switch (piece) {
        case PieceType::Pawn: c = 'p'; break;
        case PieceType::Knight: c = 'n'; break;
        case PieceType::Bishop: c = 'b'; break;
        case PieceType::Rook: c = 'r'; break;
        case PieceType::Queen: c = 'q'; break;
        case PieceType::King: c = 'k'; break;
        case PieceType::Count: break;
    }
    if (color == Color::White && c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - ('a' - 'A'));
    }
    return c;
}

void Board::printBoard(Color perspective) const {
    const bool whitePerspective = (perspective == Color::White);

    std::cout << "\n    ";
    for (int fileDisplay = 0; fileDisplay < 8; ++fileDisplay) {
        const int file = whitePerspective ? fileDisplay : (7 - fileDisplay);
        std::cout << static_cast<char>('a' + file) << ' ';
    }
    std::cout << "\n";
    std::cout << "  +-----------------+\n";
    for (int rankDisplay = 0; rankDisplay < 8; ++rankDisplay) {
        const int rank = whitePerspective ? (7 - rankDisplay) : rankDisplay;
        std::cout << rank + 1 << " | ";
        for (int fileDisplay = 0; fileDisplay < 8; ++fileDisplay) {
            const int file = whitePerspective ? fileDisplay : (7 - fileDisplay);
            const int sq = rank * 8 + file;
            const auto p = pieceAt(sq);
            if (p.has_value()) {
                std::cout << pieceToChar(p->first, p->second) << ' ';
            } else {
                std::cout << ". ";
            }
        }
        std::cout << "| " << rank + 1 << "\n";
    }
    std::cout << "  +-----------------+\n";
    std::cout << "    ";
    for (int fileDisplay = 0; fileDisplay < 8; ++fileDisplay) {
        const int file = whitePerspective ? fileDisplay : (7 - fileDisplay);
        std::cout << static_cast<char>('a' + file) << ' ';
    }
    std::cout << "\n\n";
}

void Board::printMoves(const std::vector<Move>& moves) const {
    for (const Move& m : moves) {
        std::cout << squareToString(m.from) << squareToString(m.to);
        if (m.promotion.has_value()) {
            std::cout << pieceToChar(Color::White, *m.promotion);
        }
        if (m.isKingSideCastle) std::cout << " (O-O)";
        if (m.isQueenSideCastle) std::cout << " (O-O-O)";
        std::cout << '\n';
    }
}

