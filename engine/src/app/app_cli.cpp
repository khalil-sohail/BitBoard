#include "app/app_cli.hpp"

#include "app/app_text.hpp"
#include "openingBook.hpp"
#include "search.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace {

void clearTerminal() {
    std::cout << "\033[2J\033[1;1H";
}

} // namespace

namespace AppCli {

void runCliMode(Board& board, const AppOptions::Options& options) {
    OpeningBook openingBook(options.bookPath);

    if (openingBook.lineCount() > 0) {
        std::cout << "[System] Opening book loaded: " << openingBook.lineCount() << " moves found.\n";
    } else {
        std::cout << "[System] Failed to load book: " << options.bookPath << ". Falling back to search.\n";
    }

    std::string input;
    std::string message = options.message;

    while (true) {
        if (options.showBoard) {
            clearTerminal();
        }

        std::cout << "Bitboard chess (terminal mode)\n";
        std::cout << "Enter moves in coordinate format (e2e4) or simple algebraic (Nf3, O-O).\n";
        std::cout << "Commands: moves, board, quit\n";
        std::cout << "You play: " << AppText::colorToString(options.userColor)
                  << " | AI plays: " << AppText::colorToString(options.aiColor) << "\n";
        std::cout << "Search depth: " << options.searchDepth << "\n";

        if (options.showBoard) {
            board.printBoard(options.userColor);
        }

        if (!message.empty()) {
            std::cout << "\nSystem: " << message << "\n";
            message.clear();
        }

        const std::vector<Move> legal = board.generateLegalMoves();
        if (legal.empty()) {
            if (board.inCheck(board.sideToMove())) {
                std::cout << ((board.sideToMove() == Color::White) ? "White" : "Black") << " is checkmated.\n";
            } else {
                std::cout << "Stalemate.\n";
            }
            break;
        }

        if (board.sideToMove() == options.aiColor) {
            std::optional<Move> bookMove = openingBook.getBookMove(board);
            if (bookMove.has_value()) {
                if (board.applyMove(*bookMove)) {
                    const std::string moveText = AppText::moveToCompactString(board, *bookMove);
                    board.recordSanMove(moveText);
                    message = "AI played (book): " + moveText;
                    continue;
                }
                message = "Opening book produced an invalid move for current position.";
                continue;
            }

            const Move bestMove = findBestMove(board, options.searchDepth);
            if (bestMove.from >= 0 && board.applyMove(bestMove)) {
                const std::string moveText = AppText::moveToCompactString(board, bestMove);
                board.recordSanMove(moveText);
                message = "AI played: " + moveText;
                continue;
            }

            message = "AI failed to apply a searched move.";
            continue;
        }

        std::cout << AppText::colorToString(board.sideToMove()) << " to move > ";
        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "quit" || input == "exit") {
            break;
        }
        if (input == "board") {
            message = "Board refreshed.";
            continue;
        }
        if (input == "moves") {
            message = AppText::movesToText(board, legal);
            continue;
        }

        ParseResult parsed = board.parseMove(input);
        if (!parsed.move.has_value()) {
            message = "Rejected: " + parsed.error;
            continue;
        }

        if (!board.applyMove(*parsed.move)) {
            message = "Rejected: move became illegal during validation.";
            continue;
        }

        board.recordSanMove(AppText::compactInput(input));
    }
}

} // namespace AppCli
