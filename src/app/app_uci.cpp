#include "app/app_uci.hpp"

#include "app/app_text.hpp"
#include "openingBook.hpp"
#include "search.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace AppUci {

void runUciMode(Board& board, const AppOptions::Options& options) {
    (void)options.searchDepth;
    std::cout.setf(std::ios::unitbuf);

    std::optional<OpeningBook> openingBook;
    bool openingBookStatusPrinted = false;
    auto ensureOpeningBookLoaded = [&]() {
        if (!openingBook.has_value()) {
            openingBook.emplace(options.bookPath);
            openingBook->load();
        }
        if (!openingBookStatusPrinted) {
            if (openingBook->lineCount() > 0) {
                std::cout << "[System] Opening book loaded: " << openingBook->lineCount() << " moves found." << std::endl;
            } else {
                std::cout << "info string Failed to load book: " << options.bookPath << ". Falling back to search." << std::endl;
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
        else if ((input.rfind("eval", 0) == 0 &&
                  (input.size() == 4 || std::isspace(static_cast<unsigned char>(input[4])))) ||
                 input == "d") {
#if !defined(NDEBUG) || defined(EVAL_TUNING_DIAGNOSTICS)
            board.printEvalBreakdown();
#else
            std::cout << "info string eval diagnostics disabled in this build" << std::endl;
#endif
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
            const bool hasTime = (input.find("wtime") != std::string::npos ||
                                  input.find("btime") != std::string::npos);

            long long timeLimitMs = 2000;
            if (!hasTime && movetime <= 0) {
                timeLimitMs = 999999999LL;
            } else if (movetime > 0) {
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
            if (parsedDepth <= 0 && openingBook.has_value()) {
                std::optional<Move> bookMove = openingBook->getBookMove(board);
                if (bookMove.has_value()) {
                    std::vector<Move> legalMoves = board.generateLegalMoves();
                    const auto it = std::find_if(legalMoves.begin(), legalMoves.end(), [&](const Move& legalMove) {
                        return legalMove.from == bookMove->from &&
                               legalMove.to == bookMove->to &&
                               legalMove.promotion == bookMove->promotion;
                    });
                    if (it != legalMoves.end()) {
                        bestMoveText = AppText::moveToCompactString(board, *bookMove);
                    }
                }
            }

            if (bestMoveText == "0000") {
                Move bestMove = findBestMove(board, maxDepthToSearch, timeLimitMs);
                if (bestMove.from >= 0 && bestMove.to >= 0) {
                    bestMoveText = AppText::moveToCompactString(board, bestMove);
                }
            } else {
                std::cout << "info depth 1 score cp 0 pv " << bestMoveText << std::endl;
            }

            std::cout << "bestmove " << bestMoveText << std::endl;
        }
        else if (input.rfind("quit", 0) == 0) {
            break;
        }
    }
}

} // namespace AppUci
