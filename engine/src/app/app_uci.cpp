#include "app/app_uci.hpp"

#include "app/app_text.hpp"
#include "openingBook.hpp"
#include "search.hpp"
#include "search/search_internal.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace AppUci {

// ── Background search thread management ──────────────────────────────────────
// The search runs in g_searchThread so that the main UCI loop can keep reading
// stdin (to receive 'stop', 'ponderhit', 'quit', etc.) while the engine thinks.
// All access to g_searchThread is done on the main thread only, so no mutex
// is needed around it.
static std::thread g_searchThread;

static void stopSearch() {
    timeAborted.store(true, std::memory_order_relaxed);
    if (g_searchThread.joinable()) {
        g_searchThread.join();
    }
    // Leave timeAborted = true; caller resets it before launching next search.
}

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
                std::cout << "info string Opening book loaded: " << openingBook->lineCount() << " entries." << std::endl;
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

        // ── uci ──────────────────────────────────────────────────────────────
        if (input.rfind("uci", 0) == 0 &&
            (input.size() == 3 || std::isspace(static_cast<unsigned char>(input[3])))) {
            std::cout << "id name BitboardEngine" << std::endl;
            std::cout << "id author Khalil" << std::endl;
            std::cout << "option name Hash type spin default 16 min 1 max 32768" << std::endl;
            std::cout << "option name Clear Hash type button" << std::endl;
            std::cout << "option name OwnBook type check default true" << std::endl;
            std::cout << "option name BookDepth type spin default 30 min 0 max 100" << std::endl;
            std::cout << "option name MultiPV type spin default 1 min 1 max 8" << std::endl;
            std::cout << "option name Ponder type check default false" << std::endl;
            std::cout << "uciok" << std::endl;
            ensureOpeningBookLoaded();
        }

        // ── isready ──────────────────────────────────────────────────────────
        else if (input.rfind("isready", 0) == 0) {
            std::cout << "readyok" << std::endl;
        }

        // ── ucinewgame ───────────────────────────────────────────────────────
        else if (input.rfind("ucinewgame", 0) == 0) {
            stopSearch();
            board.reset();
            board.clearSanHistory();
        }

        // ── stop ─────────────────────────────────────────────────────────────
        else if (input.rfind("stop", 0) == 0 &&
                 (input.size() == 4 || std::isspace(static_cast<unsigned char>(input[4])))) {
            stopSearch();
        }

        // ── ponderhit ────────────────────────────────────────────────────────
        // Our backend always sends 'stop' + 'go wtime…' instead of a bare
        // 'ponderhit', but we implement this for completeness / Lichess GUI
        // compatibility.  Here we treat it identically to 'stop': terminate
        // the infinite search; the GUI / backend will immediately issue a
        // fresh timed 'go' command.
        else if (input.rfind("ponderhit", 0) == 0) {
            stopSearch();
        }

        // ── setoption ────────────────────────────────────────────────────────
        else if (input.rfind("setoption", 0) == 0) {
            std::istringstream optIss(input);
            std::string tok, optName, optValue;
            optIss >> tok; // "setoption"

            bool readingName = false, readingValue = false;
            while (optIss >> tok) {
                if (tok == "name")  { readingName = true;  readingValue = false; continue; }
                if (tok == "value") { readingValue = true; readingName  = false; continue; }
                if (readingName)  { if (!optName.empty())  optName  += " "; optName  += tok; }
                if (readingValue) { if (!optValue.empty()) optValue += " "; optValue += tok; }
            }

            if (optName == "Hash") {
                try {
                    size_t mb = std::stoull(optValue);
                    SearchInternal::resizeTT(mb);
                    std::cout << "info string Hash set to " << mb << " MB (" << SearchConstants::TT_SIZE << " entries)" << std::endl;
                } catch (...) {
                    std::cout << "info string Invalid Hash value: " << optValue << std::endl;
                }
            } else if (optName == "Clear Hash") {
                SearchInternal::clearTT();
                std::cout << "info string Transposition table cleared" << std::endl;
            } else if (optName == "OwnBook") {
                if (optValue == "true")  SearchConstants::USE_OPENING_BOOK = true;
                else if (optValue == "false") SearchConstants::USE_OPENING_BOOK = false;
                std::cout << "info string OwnBook set to " << (SearchConstants::USE_OPENING_BOOK ? "true" : "false") << std::endl;
            } else if (optName == "BookDepth") {
                try {
                    SearchConstants::MAX_BOOK_DEPTH = std::stoi(optValue);
                    std::cout << "info string BookDepth set to " << SearchConstants::MAX_BOOK_DEPTH << std::endl;
                } catch (...) {
                    std::cout << "info string Invalid BookDepth value: " << optValue << std::endl;
                }
            } else if (optName == "MultiPV") {
                try {
                    int n = std::stoi(optValue);
                    SearchConstants::MULTI_PV = std::max(1, std::min(n, 8));
                    std::cout << "info string MultiPV set to " << SearchConstants::MULTI_PV << std::endl;
                } catch (...) {
                    std::cout << "info string Invalid MultiPV value: " << optValue << std::endl;
                }
            } else if (optName == "Ponder") {
                // Advisory option — we support pondering implicitly via 'go ponder'.
                // No engine-side flag needs to be stored.
                std::cout << "info string Ponder acknowledged" << std::endl;
            } else {
                std::cout << "info string Unknown option: " << optName << std::endl;
            }
        }

        // ── position ─────────────────────────────────────────────────────────
        else if (input.rfind("position", 0) == 0) {
            // Stop any ongoing search before updating the board so we don't
            // race with the search thread reading board state.
            stopSearch();

            std::istringstream iss(input);
            std::string token;
            iss >> token; // "position"
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
                std::istringstream iss2(input.substr(movesPos + 7));
                std::string moveToken;
                while (iss2 >> moveToken) {
                    ParseResult parsed = board.parseMove(moveToken);
                    if (parsed.move.has_value() && board.applyMove(*parsed.move)) {
                        board.recordSanMove(moveToken);
                    }
                }
            }
        }

        // ── eval / d ─────────────────────────────────────────────────────────
        else if ((input.rfind("eval", 0) == 0 &&
                  (input.size() == 4 || std::isspace(static_cast<unsigned char>(input[4])))) ||
                 input == "d") {
#if !defined(NDEBUG) || defined(EVAL_TUNING_DIAGNOSTICS)
            board.printEvalBreakdown();
#else
            std::cout << "info string eval diagnostics disabled in this build" << std::endl;
#endif
        }

        // ── go ───────────────────────────────────────────────────────────────
        else if (input.rfind("go", 0) == 0) {
            ensureOpeningBookLoaded();

            int wtime     = 0;
            int btime     = 0;
            int winc      = 0;
            int binc      = 0;
            int movetime  = 0;
            int parsedDepth = 0;
            bool goPonder  = false;

            std::istringstream iss(input);
            std::string token;
            while (iss >> token) {
                if      (token == "wtime")    { iss >> wtime; }
                else if (token == "btime")    { iss >> btime; }
                else if (token == "winc")     { iss >> winc; }
                else if (token == "binc")     { iss >> binc; }
                else if (token == "movetime") { iss >> movetime; }
                else if (token == "depth")    { iss >> parsedDepth; }
                else if (token == "ponder")   { goPonder = true; }
            }

            // Stop any previous search (ponder or timed) before launching a
            // new one.  timeAborted will be cleared inside the new thread
            // launch sequence below.
            stopSearch();
            timeAborted.store(false, std::memory_order_relaxed);

            // ── Opening book (synchronous, instantaneous) ─────────────────
            if (!goPonder && parsedDepth <= 0 &&
                openingBook.has_value() && SearchConstants::USE_OPENING_BOOK) {
                if (board.sanHistory().size() <
                    static_cast<size_t>(SearchConstants::MAX_BOOK_DEPTH)) {
                    std::optional<Move> bookMove = openingBook->getBookMove(board);
                    if (bookMove.has_value()) {
                        std::vector<Move> legalMoves = board.generateLegalMoves();
                        const auto it = std::find_if(
                            legalMoves.begin(), legalMoves.end(),
                            [&](const Move& legalMove) {
                                return legalMove.from      == bookMove->from &&
                                       legalMove.to        == bookMove->to   &&
                                       legalMove.promotion == bookMove->promotion;
                            });
                        if (it != legalMoves.end()) {
                            const std::string bm = AppText::moveToCompactString(board, *bookMove);
                            std::cout << "info depth 1 score cp 0 pv " << bm << std::endl;
                            std::cout << "bestmove " << bm << std::endl;
                            continue; // back to getline — no thread needed
                        }
                    }
                }
            }

            // ── Compute time budget ────────────────────────────────────────
            const bool whiteToMove = board.sideToMove() == Color::White;
            const long long timeLeft  = whiteToMove ? wtime : btime;
            const long long increment = whiteToMove ? winc  : binc;
            const bool hasTime = (input.find("wtime") != std::string::npos ||
                                  input.find("btime") != std::string::npos);

            long long timeLimitMs = 2000;
            if (goPonder) {
                timeLimitMs = 999'999'999LL; // will be cut short by 'stop'
            } else if (!hasTime && movetime <= 0) {
                timeLimitMs = 999'999'999LL;
            } else if (movetime > 0) {
                timeLimitMs = std::max(10LL, static_cast<long long>(movetime));
            } else if (timeLeft > 0) {
                long long allocated_time = (timeLeft / 40) + (increment / 2);
                long long hard_cap = std::max(1LL, timeLeft / 4);

                // Standard case: clamp between a 10ms minimum and the 25% hard cap
                timeLimitMs = std::max(10LL, std::min(allocated_time, hard_cap));

                // Critical low-time panic case: if we have less than 40ms, use exactly what we have minus a tiny lag buffer
                if (timeLeft < 40) {
                    timeLimitMs = std::max(1LL, timeLeft - 5);
                }
            }

            const int maxDepthToSearch = (parsedDepth > 0) ? parsedDepth : 64;

            // ── Launch search in background thread ─────────────────────────
            // Board is captured by VALUE so the main thread can safely receive
            // new 'position' commands without touching the search's board copy.
            Board boardCopy = board;

            g_searchThread = std::thread([boardCopy = std::move(boardCopy),
                                          maxDepthToSearch,
                                          timeLimitMs,
                                          goPonder]() mutable {
                auto [bestMove, ponderMove] = findBestMove(boardCopy, maxDepthToSearch, timeLimitMs, goPonder);

                std::string bestMoveText = "0000";
                if (bestMove.from >= 0 && bestMove.to >= 0) {
                    bestMoveText = AppText::moveToCompactString(boardCopy, bestMove);
                }

                std::string line = "bestmove " + bestMoveText;
                if (ponderMove.from >= 0) {
                    line += " ponder " + AppText::moveToCompactString(boardCopy, ponderMove);
                }
                std::cout << line << std::endl;
            });
        }

        // ── quit ─────────────────────────────────────────────────────────────
        else if (input.rfind("quit", 0) == 0) {
            stopSearch();
            break;
        }
    }

    // Ensure the search thread is joined before the function returns.
    stopSearch();
}

} // namespace AppUci
