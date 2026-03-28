#include "board.hpp"
#include "openingBook.hpp"
#include "search.hpp"

#include <cctype>
#include <iostream>
#include <sstream>

namespace {

enum class ExecutionMode {
    Cli,
    Gui
};

std::string g_pendingCliMessage;

bool hasFlag(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == flag) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> getFlagValue(int argc, char* argv[], const std::string& prefix) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return std::nullopt;
}

int parseDepthValue(const std::string& raw, std::string& message) {
    if (raw.empty()) {
        if (!message.empty()) message += "\n";
        message += "Invalid --depth value. Defaulting to 4.";
        return 4;
    }

    for (char c : raw) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            if (!message.empty()) message += "\n";
            message += "Invalid --depth value. Defaulting to 4.";
            return 4;
        }
    }

    int parsed = 4;
    try {
        parsed = std::stoi(raw);
    } catch (...) {
        if (!message.empty()) message += "\n";
        message += "Invalid --depth value. Defaulting to 4.";
        return 4;
    }

    if (parsed <= 0) {
        if (!message.empty()) message += "\n";
        message += "Depth must be positive. Defaulting to 4.";
        return 4;
    }
    return parsed;
}

int parseSearchDepth(int argc, char* argv[], std::string& message) {
    if (std::optional<std::string> value = getFlagValue(argc, argv, "--depth=")) {
        return parseDepthValue(*value, message);
    }

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "--depth") {
            return parseDepthValue(argv[i + 1], message);
        }
    }

    return 4;
}

ExecutionMode parseExecutionMode(int argc, char* argv[], std::string& message) {
    auto parseModeValue = [&](std::string value) -> ExecutionMode {
        for (char& c : value) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (value == "cli") {
            return ExecutionMode::Cli;
        }
        if (value == "gui") {
            return ExecutionMode::Gui;
        }
        if (!message.empty()) {
            message += "\n";
        }
        message += "Invalid --mode value. Use --mode=cli or --mode=gui. Defaulting to cli.";
        return ExecutionMode::Cli;
    };

    if (std::optional<std::string> value = getFlagValue(argc, argv, "--mode=")) {
        return parseModeValue(*value);
    }

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode") {
            return parseModeValue(argv[i + 1]);
        }
    }

    return ExecutionMode::Cli;
}

Color parseUserColor(int argc, char* argv[], std::string& message) {
    Color userColor = Color::White;

    // Preferred format: --color=white|black
    if (std::optional<std::string> value = getFlagValue(argc, argv, "--color=")) {
        std::string v = *value;
        for (char& c : v) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (v == "white") {
            return Color::White;
        }
        if (v == "black") {
            return Color::Black;
        }
        if (!message.empty()) message += "\n";
        message += "Invalid --color value. Use --color=white or --color=black. Defaulting to white.";
        return Color::White;
    }

    // Also accept split form: --color white|black
    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "--color") {
            std::string v = argv[i + 1];
            for (char& c : v) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (v == "white") {
                return Color::White;
            }
            if (v == "black") {
                return Color::Black;
            }
            if (!message.empty()) message += "\n";
            message += "Invalid --color value. Use --color=white or --color=black. Defaulting to white.";
            return Color::White;
        }
    }

    return userColor;
}

Color oppositeColor(Color c) {
    return (c == Color::White) ? Color::Black : Color::White;
}

std::string colorToString(Color c) {
    return (c == Color::White) ? "White" : "Black";
}

std::string moveToCompactString(const Board& board, const Move& move) {
    std::string text = Board::squareToString(move.from) + Board::squareToString(move.to);
    if (move.promotion.has_value()) {
        text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(board.pieceToChar(Color::White, *move.promotion)))));
    }
    return text;
}

void clearTerminal() {
    std::cout << "\033[2J\033[1;1H";
}

std::string movesToText(const Board& board, const std::vector<Move>& legal) {
    std::ostringstream oss;
    oss << "Legal moves (" << legal.size() << "):";
    for (const Move& m : legal) {
        oss << "\n" << Board::squareToString(m.from) << Board::squareToString(m.to);
        if (m.promotion.has_value()) {
            oss << board.pieceToChar(Color::White, *m.promotion);
        }
        if (m.isKingSideCastle) {
            oss << " (O-O)";
        }
        if (m.isQueenSideCastle) {
            oss << " (O-O-O)";
        }
    }
    return oss.str();
}

std::string compactInput(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

void runCliMode(Board& board,
                OpeningBook& openingBook,
                Color userColor,
                Color aiColor,
                int searchDepth,
                bool showBoard) {
    std::string input;
    std::string message = std::move(g_pendingCliMessage);
    g_pendingCliMessage.clear();

    while (true) {
        if (showBoard) {
            clearTerminal();
        }

        std::cout << "Bitboard chess (terminal mode)\n";
        std::cout << "Enter moves in coordinate format (e2e4) or simple algebraic (Nf3, O-O).\n";
        std::cout << "Commands: moves, board, quit\n";
        std::cout << "You play: " << colorToString(userColor) << " | AI plays: " << colorToString(aiColor) << "\n";
        std::cout << "Search depth: " << searchDepth << "\n";

        if (showBoard) {
            board.printBoard(userColor);
        }

        if (!message.empty()) {
            std::cout << "\nSystem: " << message << "\n";
            message.clear();
        }

        const std::vector<Move> legal = board.generateLegalMoves();
        if (legal.empty()) {
            if (board.inCheck(board.sideToMove())) {
                std::cout << ((board.sideToMove() == Color::White) ? "White" : "Black")
                          << " is checkmated.\n";
            } else {
                std::cout << "Stalemate.\n";
            }
            break;
        }

        if (board.sideToMove() == aiColor) {
            std::optional<std::string> bookMove = openingBook.getBookMove(board.sanHistory());
            if (bookMove.has_value()) {
                ParseResult parsedBookMove = board.parseMove(*bookMove);
                if (parsedBookMove.move.has_value() && board.applyMove(*parsedBookMove.move)) {
                    board.recordSanMove(*bookMove);
                    message = "AI played: " + *bookMove;
                    continue;
                }
                message = "Opening book produced an invalid move for current position: " + *bookMove;
                continue;
            }

            const Move bestMove = findBestMove(board, searchDepth);
            if (bestMove.from >= 0 && board.applyMove(bestMove)) {
                const std::string moveText = moveToCompactString(board, bestMove);
                board.recordSanMove(moveText);
                message = "AI played: " + moveText;
                continue;
            }

            message = "AI failed to apply a searched move.";
            continue;
        }

        std::cout << colorToString(board.sideToMove()) << " to move > ";
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
            message = movesToText(board, legal);
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

        board.recordSanMove(compactInput(input));
    }
}

void runGuiMode(Board& board, OpeningBook& openingBook, int searchDepth) {
    std::string input;
    while (true) {
        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "uci") {
            std::cout << "id name BitboardEngine\n";
            std::cout << "id author Khalil\n";
            std::cout << "uciok\n";
            continue;
        }

        if (input == "isready") {
            std::cout << "readyok\n";
            continue;
        }

        if (input == "ucinewgame") {
            board.reset();
            board.clearSanHistory();
            continue;
        }

        if (input.rfind("position", 0) == 0) {
            if (input.rfind("position startpos", 0) == 0) {
                board.reset();
                board.clearSanHistory();
            }

            const std::size_t movesPos = input.find(" moves ");
            if (movesPos != std::string::npos) {
                std::istringstream iss(input.substr(movesPos + 7));
                std::string moveToken;
                while (iss >> moveToken) {
                    ParseResult parsed = board.parseMove(moveToken);
                    if (parsed.move.has_value() && board.applyMove(*parsed.move)) {
                        board.recordSanMove(moveToken);
                    }
                }
            }
            continue;
        }

        if (input.rfind("go", 0) == 0) {
            int depth = searchDepth;
            {
                std::istringstream iss(input);
                std::string token;
                while (iss >> token) {
                    if (token == "depth") {
                        std::string depthToken;
                        if (iss >> depthToken) {
                            std::string ignore;
                            depth = parseDepthValue(depthToken, ignore);
                        }
                        break;
                    }
                }
            }

            std::string bestMoveText = "0000";

            std::optional<std::string> bookMove = openingBook.getBookMove(board.sanHistory());
            if (bookMove.has_value()) {
                ParseResult parsedBookMove = board.parseMove(*bookMove);
                if (parsedBookMove.move.has_value()) {
                    bestMoveText = moveToCompactString(board, *parsedBookMove.move);
                }
            }

            if (bestMoveText == "0000") {
                Move bestMove = findBestMove(board, depth);
                if (bestMove.from >= 0 && bestMove.to >= 0) {
                    bestMoveText = moveToCompactString(board, bestMove);
                }
            }

            std::cout << "bestmove " << bestMoveText << "\n";
            continue;
        }

        if (input == "quit") {
            break;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    Board board;
    OpeningBook openingBook("openings");
    std::string message;
    const bool showBoard = !hasFlag(argc, argv, "--no-board");

    Color userColor = parseUserColor(argc, argv, message);
    const Color aiColor = oppositeColor(userColor);
    const int searchDepth = parseSearchDepth(argc, argv, message);
    const ExecutionMode mode = parseExecutionMode(argc, argv, message);

    if (openingBook.lineCount() == 0) {
        if (!message.empty()) {
            message += "\n";
        }
        message += "Opening book not loaded. Place opening files under openings/.";
    }

    if (mode == ExecutionMode::Gui) {
        runGuiMode(board, openingBook, searchDepth);
    } else {
        g_pendingCliMessage = std::move(message);
        runCliMode(board, openingBook, userColor, aiColor, searchDepth, showBoard);
    }

    return 0;
}
