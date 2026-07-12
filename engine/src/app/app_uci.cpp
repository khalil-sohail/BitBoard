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

std::atomic<int> g_lastSearchScore{0};
std::atomic<int> g_prevSearchScore{0};
std::atomic<bool> g_hasOneScore{false};
std::atomic<bool> g_hasTwoScores{false};

static long long g_ponderHitTimeLimitMs = 0;
static std::atomic<bool> g_isPondering{false};

// ── Background search thread management ──────────────────────────────────────
// The search runs in g_searchThread so that the main UCI loop can keep reading
// stdin (to receive 'stop', 'ponderhit', 'quit', etc.) while the engine thinks.
// All access to g_searchThread is done on the main thread only, so no mutex
// is needed around it.
static std::thread g_searchThread;

static void stopSearch() {
    g_isPondering.store(false, std::memory_order_relaxed);
    timeAborted.store(true, std::memory_order_relaxed);
    if (g_searchThread.joinable()) {
        g_searchThread.join();
    }
    // Leave timeAborted = true; caller resets it before launching next search.
}

void runUciMode(Board& board, const AppOptions::Options& options) {
    (void)options.searchDepth;
    std::cout.setf(std::ios::unitbuf);
    
    int base_fullmove = 1;

    // Ensure a clean initial state on startup
    SearchInternal::initLMR();
    SearchInternal::clearTT();
    SearchInternal::clearKillers();
    SearchInternal::clearHistory();

    std::optional<OpeningBook> openingBook;
    bool openingBookStatusPrinted = false;
    BookSelectionMode bookSelectionMode = BookSelectionMode::Weighted;
    size_t bookSelectionTopN = 4;
    auto ensureOpeningBookLoaded = [&]() {
        if (!openingBook.has_value()) {
            openingBook.emplace(options.bookPath);
            openingBook->load();
            openingBook->setSelectionMode(bookSelectionMode);
            openingBook->setTopN(bookSelectionTopN);
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
            std::cout << "option name BookSelection type combo default weighted var best var weighted var top-n-weighted" << std::endl;
            std::cout << "option name BookSelectionTopN type spin default 4 min 1 max 32" << std::endl;
            std::cout << "option name BookSeed type spin default 1592594996 min 0 max 2147483647" << std::endl;
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
            base_fullmove = 1;
            g_hasOneScore.store(false, std::memory_order_relaxed);
            g_hasTwoScores.store(false, std::memory_order_relaxed);
            SearchInternal::clearTT();
            SearchInternal::clearKillers();
            SearchInternal::clearHistory();
        }

        // ── stop ─────────────────────────────────────────────────────────────
        else if (input.rfind("stop", 0) == 0 &&
                 (input.size() == 4 || std::isspace(static_cast<unsigned char>(input[4])))) {
            stopSearch();
        }

        // ── ponderhit ────────────────────────────────────────────────────────
        else if (input.rfind("ponderhit", 0) == 0) {
            g_isPondering.store(false, std::memory_order_relaxed);
            // Switch the already-running search thread from infinite ponder time
            // to the actual calculated time limit.
            allocatedTimeMs.store(AppUci::g_ponderHitTimeLimitMs, std::memory_order_relaxed);
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
            } else if (optName == "BookSelection") {
                if (optValue == "best") {
                    bookSelectionMode = BookSelectionMode::Best;
                } else if (optValue == "weighted") {
                    bookSelectionMode = BookSelectionMode::Weighted;
                } else if (optValue == "top-n-weighted") {
                    bookSelectionMode = BookSelectionMode::TopNWeighted;
                } else {
                    std::cout << "info string Invalid BookSelection value: " << optValue << std::endl;
                    continue;
                }
                if (openingBook.has_value()) {
                    openingBook->setSelectionMode(bookSelectionMode);
                }
                std::cout << "info string BookSelection set to " << optValue << std::endl;
            } else if (optName == "BookSelectionTopN") {
                try {
                    bookSelectionTopN = static_cast<size_t>(std::max(1, std::stoi(optValue)));
                    if (openingBook.has_value()) {
                        openingBook->setTopN(bookSelectionTopN);
                    }
                    std::cout << "info string BookSelectionTopN set to " << bookSelectionTopN << std::endl;
                } catch (...) {
                    std::cout << "info string Invalid BookSelectionTopN value: " << optValue << std::endl;
                }
            } else if (optName == "BookSeed") {
                try {
                    const uint32_t seed = static_cast<uint32_t>(std::stoul(optValue));
                    ensureOpeningBookLoaded();
                    openingBook->setSeed(seed);
                    std::cout << "info string BookSeed set" << std::endl;
                } catch (...) {
                    std::cout << "info string Invalid BookSeed value: " << optValue << std::endl;
                }
            } else if (optName == "MultiPV") {
                try {
                    int n = std::stoi(optValue);
                    SearchConstants::MULTI_PV = std::max(1, std::min(n, 8));
                    std::cout << "info string MultiPV set to " << SearchConstants::MULTI_PV << std::endl;
                } catch (...) {
                    std::cout << "info string Invalid MultiPV value: " << optValue << std::endl;
                }
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
                    base_fullmove = 1;
                    g_hasOneScore.store(false, std::memory_order_relaxed);
                    g_hasTwoScores.store(false, std::memory_order_relaxed);
                } else if (token == "fen") {
                    base_fullmove = 1;
                    std::string fen;
                    std::vector<std::string> fenTokens;
                    for (int i = 0; i < 6 && iss >> token && token != "moves"; ++i) {
                        if (i > 0) fen += " ";
                        fen += token;
                        fenTokens.push_back(token);
                    }
                    if (fenTokens.size() == 6) {
                        try { base_fullmove = std::max(1, std::stoi(fenTokens[5])); } catch(...) {}
                    }
                    if (!fen.empty() && !board.loadFEN(fen)) {
                        std::cout << "info string Invalid FEN: " << fen << std::endl;
                    } else if (!fen.empty()) {
                        board.clearSanHistory();
                        g_hasOneScore.store(false, std::memory_order_relaxed);
                        g_hasTwoScores.store(false, std::memory_order_relaxed);
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
            int movestogo = 0;
            bool localPonder = false;

            std::istringstream iss(input);
            std::string token;
            while (iss >> token) {
                if      (token == "wtime")    { iss >> wtime; }
                else if (token == "btime")    { iss >> btime; }
                else if (token == "winc")     { iss >> winc; }
                else if (token == "binc")     { iss >> binc; }
                else if (token == "movetime") { iss >> movetime; }
                else if (token == "depth")    { iss >> parsedDepth; }
                else if (token == "movestogo"){ iss >> movestogo; }
                else if (token == "ponder")   { localPonder = true; }
            }

            // Stop any previous search (ponder or timed) before launching a
            // new one.  timeAborted will be cleared inside the new thread
            // launch sequence below.
            stopSearch();
            timeAborted.store(false, std::memory_order_relaxed);

            // ── Opening book (synchronous, instantaneous) ─────────────────
            // The book check is decoupled from parsedDepth so that Training
            // Mode (which always sends a depth cap) still consults the book.
            if (!localPonder && openingBook.has_value() && SearchConstants::USE_OPENING_BOOK &&
                board.sanHistory().size() <
                    static_cast<size_t>(SearchConstants::MAX_BOOK_DEPTH)) {
                std::optional<SelectedBookMove> bookMove = openingBook->selectBookMove(board);
                if (bookMove.has_value()) {
                    std::vector<Move> legalMoves = board.generateLegalMoves();
                    const auto it = std::find_if(
                        legalMoves.begin(), legalMoves.end(),
                        [&](const Move& legalMove) {
                            return legalMove.from      == bookMove->move.from &&
                                   legalMove.to        == bookMove->move.to   &&
                                   legalMove.promotion == bookMove->move.promotion;
                        });
                    if (it != legalMoves.end()) {
                        const std::string bm = AppText::moveToCompactString(board, bookMove->move);
                        std::cout << "info string book move " << bm
                                  << " candidates " << bookMove->candidateCount
                                  << " weight " << bookMove->weight << std::endl;
                        std::cout << "bestmove " << bm << std::endl;
                        continue; // back to getline — no thread needed
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
            if (!hasTime && movetime <= 0) {
                timeLimitMs = 999'999'999LL;
            } else if (movetime > 0) {
                timeLimitMs = std::max(10LL, static_cast<long long>(movetime));
            } else if (timeLeft > 0) {
                // Subtract a flat network RTT buffer upfront so that the
                // bestmove response reaches the server before the flag falls.
                const long long safeTimeLeft = std::max(1LL, timeLeft - 30LL);
                
                int move_number = base_fullmove + (board.sanHistory().size() / 2);
                int expected_moves_remaining = std::max(20, 40 - move_number);
                if (movestogo > 0) {
                    expected_moves_remaining = movestogo;
                }

                long long allocated_time = (safeTimeLeft / expected_moves_remaining) + increment;

                if (g_hasTwoScores.load(std::memory_order_relaxed)) {
                    if (std::abs(g_lastSearchScore.load(std::memory_order_relaxed) - g_prevSearchScore.load(std::memory_order_relaxed)) > 50) {
                        allocated_time = static_cast<long long>(allocated_time * 1.3);
                    }
                }

                long long hard_cap = std::max(1LL, safeTimeLeft / 4);

                // Standard case: clamp between a 10ms minimum and the 25% hard cap
                timeLimitMs = std::max(10LL, std::min(allocated_time, hard_cap));

                // Critical low-time panic case: if we have less than 40ms
                // (after the buffer), use exactly what remains minus a tiny
                // internal scheduler buffer.
                if (safeTimeLeft < 40) {
                    timeLimitMs = std::max(1LL, safeTimeLeft - 5LL);
                }
            }

            if (localPonder) {
                AppUci::g_ponderHitTimeLimitMs = timeLimitMs; // Save the real time limit
                timeLimitMs = 999'999'999LL;                  // Override for infinite ponder
                g_isPondering.store(true, std::memory_order_relaxed);
            } else {
                g_isPondering.store(false, std::memory_order_relaxed);
            }

            const int maxDepthToSearch = (parsedDepth > 0) ? parsedDepth : 64;

            // ── Launch search in background thread ─────────────────────────
            // Board is captured by VALUE so the main thread can safely receive
            // new 'position' commands without touching the search's board copy.
            Board boardCopy = board;

            g_searchThread = std::thread([boardCopy = std::move(boardCopy),
                                          maxDepthToSearch,
                                          timeLimitMs]() mutable {
                int searchScore = 0;
                auto [bestMove, ponderMove] = findBestMove(boardCopy, maxDepthToSearch, timeLimitMs, &searchScore);

                if (!g_isPondering.load(std::memory_order_relaxed)) {
                    if (g_hasOneScore.load(std::memory_order_relaxed)) {
                        g_prevSearchScore.store(g_lastSearchScore.load(std::memory_order_relaxed), std::memory_order_relaxed);
                        g_hasTwoScores.store(true, std::memory_order_relaxed);
                    }
                    g_lastSearchScore.store(searchScore, std::memory_order_relaxed);
                    g_hasOneScore.store(true, std::memory_order_relaxed);
                }

                std::string bestMoveText = "0000";
                if (bestMove.from >= 0 && bestMove.to >= 0) {
                    bestMoveText = AppText::moveToCompactString(boardCopy, bestMove);
                }

                std::string line = "bestmove " + bestMoveText;
                if (ponderMove.from >= 0) {
                    line += " ponder " + AppText::moveToCompactString(boardCopy, ponderMove);
                }

                // If findBestMove returned early (1 legal move, mate found, max depth),
                // we must NOT print bestmove if we are still supposed to be pondering.
                // Block here until the main thread sends ponderhit or stop.
                while (g_isPondering.load(std::memory_order_relaxed) && 
                       !timeAborted.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
