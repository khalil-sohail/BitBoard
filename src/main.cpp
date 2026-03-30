#include "board.hpp"
#include "openingBook.hpp"
#include "search.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace {

enum class ExecutionMode {
    Cli,
    Gui
};

std::string g_pendingCliMessage;
constexpr const char* OPENINGS_ABSOLUTE_PATH = "/home/ksohail-/Documents/chess-engine/openings";

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
                Color userColor,
                Color aiColor,
                int searchDepth,
                bool showBoard,
                const std::string& bookPath) {
    OpeningBook openingBook(bookPath);

    if (openingBook.lineCount() > 0) {
        std::cout << "[System] Opening book loaded: " << openingBook.lineCount() << " moves found.\n";
    } else {
        std::cout << "[System] Failed to load book: " << bookPath << ". Falling back to search.\n";
    }

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
            std::optional<Move> bookMove = openingBook.getBookMove(board);
            if (bookMove.has_value()) {
                if (board.applyMove(*bookMove)) {
                    const std::string moveText = moveToCompactString(board, *bookMove);
                    board.recordSanMove(moveText);
                    message = "AI played (book): " + moveText;
                    continue;
                }
                message = "Opening book produced an invalid move for current position.";
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

void runGuiMode(Board& board, int searchDepth, const std::string& bookPath) {
    (void)searchDepth;
    std::cout.setf(std::ios::unitbuf);

    std::optional<OpeningBook> openingBook;
    bool openingBookStatusPrinted = false;
    auto ensureOpeningBookLoaded = [&]() {
        if (!openingBook.has_value()) {
            openingBook.emplace(bookPath);
            openingBook->load();
        }
        if (!openingBookStatusPrinted) {
            if (openingBook->lineCount() > 0) {
                std::cout << "[System] Opening book loaded: " << openingBook->lineCount() << " moves found." << std::endl;
            } else {
                std::cout << "info string Failed to load book: " << bookPath << ". Falling back to search." << std::endl;
            }
            openingBookStatusPrinted = true;
        }
    };

    std::string input;
    while (std::getline(std::cin, input)) {
        if (!input.empty() && input.back() == '\r') {
            input.pop_back();
        }

        if (input.rfind("uci", 0) == 0 &&
            (input.size() == 3 || std::isspace(static_cast<unsigned char>(input[3])))) {
            std::cout << "id name BitboardEngine" << std::endl;
            std::cout << "id author Khalil" << std::endl;
            std::cout << "uciok" << std::endl;
            ensureOpeningBookLoaded();
        } 
        else if (input.rfind("isready", 0) == 0) {
            std::cout << "readyok" << std::endl;
        } 
        else if (input.rfind("ucinewgame", 0) == 0) {
            board.reset();
            board.clearSanHistory();
        } 
        else if (input.rfind("position", 0) == 0) {
            std::istringstream iss(input);
            std::string token;
            iss >> token; // position
            if (iss >> token) {
                if (token == "startpos") {
                    board.reset();
                    board.clearSanHistory();
                } else if (token == "fen") {
                    std::string fen;
                    for (int i = 0; i < 6 && iss >> token && token != "moves"; ++i) {
                        if (i > 0) fen += " ";
                        fen += token;
                    }
                    if (!fen.empty() && !board.loadFEN(fen)) {
                        std::cout << "info string Invalid FEN: " << fen << std::endl;
                    } else if (!fen.empty()) {
                        board.clearSanHistory();
                    }
                }
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
        } 
        else if (input.rfind("go", 0) == 0) {
            ensureOpeningBookLoaded();

            int wtime = 0;
            int btime = 0;
            int winc = 0;
            int binc = 0;
            int movetime = 0;
            int parsedDepth = 0;

            std::istringstream iss(input);
            std::string token;
            while (iss >> token) {
                if (token == "wtime") {
                    iss >> wtime;
                } else if (token == "btime") {
                    iss >> btime;
                } else if (token == "winc") {
                    iss >> winc;
                } else if (token == "binc") {
                    iss >> binc;
                } else if (token == "movetime") {
                    iss >> movetime;
                } else if (token == "depth") {
                    iss >> parsedDepth;
                }
            }

            const bool whiteToMove = board.sideToMove() == Color::White;
            const long long timeLeft = whiteToMove ? wtime : btime;
            const long long increment = whiteToMove ? winc : binc;

            long long timeLimitMs = 2000;
            if (movetime > 0) {
                timeLimitMs = std::max(10LL, static_cast<long long>(movetime));
            } else if (timeLeft > 0) {
                uint64_t activePieces =
                    board.getBitboard(Color::White, PieceType::Knight) |
                    board.getBitboard(Color::White, PieceType::Bishop) |
                    board.getBitboard(Color::White, PieceType::Rook) |
                    board.getBitboard(Color::White, PieceType::Queen) |
                    board.getBitboard(Color::Black, PieceType::Knight) |
                    board.getBitboard(Color::Black, PieceType::Bishop) |
                    board.getBitboard(Color::Black, PieceType::Rook) |
                    board.getBitboard(Color::Black, PieceType::Queen);

                int pieceCount = std::popcount(activePieces);

                int movesToGo = 30;
                if (pieceCount >= 10) {
                    movesToGo = 20;
                } else if (pieceCount <= 6) {
                    movesToGo = 40;
                }

                timeLimitMs = (timeLeft / movesToGo) + (increment * 3 / 4) - 50;
                if (timeLimitMs < 10) {
                    timeLimitMs = 10;
                }
            }

            const int maxDepthToSearch = (parsedDepth > 0) ? parsedDepth : 64;

            std::string bestMoveText = "0000";
            if (openingBook.has_value()) {
                std::optional<Move> bookMove = openingBook->getBookMove(board);
                if (bookMove.has_value()) {
                    std::vector<Move> legalMoves = board.generateLegalMoves();
                    const auto it = std::find_if(legalMoves.begin(), legalMoves.end(), [&](const Move& legalMove) {
                        return legalMove.from == bookMove->from &&
                               legalMove.to == bookMove->to &&
                               legalMove.promotion == bookMove->promotion;
                    });
                    if (it != legalMoves.end()) {
                        bestMoveText = moveToCompactString(board, *bookMove);
                    }
                }
            }

            if (bestMoveText == "0000") {
                Move bestMove = findBestMove(board, maxDepthToSearch, timeLimitMs);
                if (bestMove.from >= 0 && bestMove.to >= 0) {
                    bestMoveText = moveToCompactString(board, bestMove);
                }
            }

            std::cout << "bestmove " << bestMoveText << std::endl;
        }
        else if (input.rfind("quit", 0) == 0) {
            break;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    Board board;
    std::string message;
    const bool showBoard = !hasFlag(argc, argv, "--no-board");

    std::string bookPath = OPENINGS_ABSOLUTE_PATH;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string value;

        if (arg.rfind("--book=", 0) == 0) {
            value = arg.substr(std::string("--book=").size());
        } else if (arg.rfind("book=", 0) == 0) {
            value = arg.substr(std::string("book=").size());
        } else {
            continue;
        }

        if (value.empty()) {
            continue;
        }

        const bool hasSlash = value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
        if (hasSlash) {
            bookPath = value;
        } else {
            const std::filesystem::path defaultBookPath(OPENINGS_ABSOLUTE_PATH);
            const std::filesystem::path defaultBookDir =
                defaultBookPath.has_extension() ? defaultBookPath.parent_path() : defaultBookPath;
            bookPath = (defaultBookDir / value).string();
        }
    }

    Color userColor = parseUserColor(argc, argv, message);
    const Color aiColor = oppositeColor(userColor);
    const int searchDepth = parseSearchDepth(argc, argv, message);
    const ExecutionMode mode = parseExecutionMode(argc, argv, message);

    if (mode == ExecutionMode::Gui) {
        runGuiMode(board, searchDepth, bookPath);
    } else {
        g_pendingCliMessage = std::move(message);
        runCliMode(board, userColor, aiColor, searchDepth, showBoard, bookPath);
    }

    return 0;
}
