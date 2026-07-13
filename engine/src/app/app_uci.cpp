#include "app/app_uci.hpp"

#include "app/app_text.hpp"
#include "app/uci_telemetry.hpp"
#include "openingBook.hpp"
#include "search.hpp"
#include "search/search_internal.hpp"
#include "time/time_management.hpp"
#include "tuning/compiled_profile_identity.hpp"
#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr const auto& TIME_TUNING = Tuning::Generated::VALUES.time;
constexpr const auto& OPENING_TUNING = Tuning::Generated::VALUES.opening;

static_assert(static_cast<int>(BookSelectionMode::Weighted) ==
              static_cast<int>(Tuning::BookSelectionMode::Weighted));
static_assert(static_cast<int>(BookSelectionMode::Best) ==
              static_cast<int>(Tuning::BookSelectionMode::Best));
static_assert(static_cast<int>(BookSelectionMode::TopNWeighted) ==
              static_cast<int>(Tuning::BookSelectionMode::TopNWeighted));

constexpr const char* openingSelectionModeName(Tuning::BookSelectionMode mode) {
    switch (mode) {
        case Tuning::BookSelectionMode::Weighted: return "weighted";
        case Tuning::BookSelectionMode::Best: return "best";
        case Tuning::BookSelectionMode::TopNWeighted: return "top-n-weighted";
    }
    std::unreachable();
}

}

namespace AppUci {

std::atomic<int> g_lastSearchScore{0};
std::atomic<int> g_prevSearchScore{0};
std::atomic<bool> g_hasOneScore{false};
std::atomic<bool> g_hasTwoScores{false};

static long long g_ponderHitTimeLimitMs = 0;
static std::atomic<bool> g_isPondering{false};

enum class SearchLimitMode {
    Clock,
    MoveTime,
    Depth,
    Infinite,
};

struct ParsedGoCommand {
    SearchLimitMode mode = SearchLimitMode::Infinite;
    std::optional<std::int64_t> whiteTimeMs;
    std::optional<std::int64_t> blackTimeMs;
    std::optional<std::int64_t> whiteIncrementMs;
    std::optional<std::int64_t> blackIncrementMs;
    std::optional<int> movesToGo;
    std::optional<std::int64_t> moveTimeMs;
    std::optional<int> depth;
    bool ponder = false;
};

struct ParseGoResult {
    std::optional<ParsedGoCommand> command;
    std::string error;
};

template <typename Integer>
static std::optional<Integer> parseStrictInteger(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    Integer parsed{};
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }

    return parsed;
}

static bool nextToken(std::istringstream& iss, std::string& value) {
    return static_cast<bool>(iss >> value);
}

static ParseGoResult parseGoCommand(const std::string& input) {
    ParsedGoCommand command;
    std::istringstream iss(input);
    std::string token;
    iss >> token; // "go"

    bool sawClock = false;
    bool sawMoveTime = false;
    bool sawDepth = false;
    bool sawInfinite = false;
    std::unordered_set<std::string> seenTokens;

    auto reject = [](std::string reason) -> ParseGoResult {
        return { std::nullopt, std::move(reason) };
    };

    auto markToken = [&](const std::string& key) -> bool {
        return seenTokens.insert(key).second;
    };

    auto readIntegerToken = [&]() -> std::optional<std::int64_t> {
        std::string value;
        if (!nextToken(iss, value)) {
            return std::nullopt;
        }
        return parseStrictInteger<std::int64_t>(value);
    };

    while (iss >> token) {
        if (token == "ponder") {
            if (!markToken(token)) {
                return reject("duplicate ponder token");
            }
            command.ponder = true;
            continue;
        }

        if (token == "infinite") {
            if (!markToken(token)) {
                return reject("duplicate infinite token");
            }
            sawInfinite = true;
            continue;
        }

        if (token == "wtime" || token == "btime" || token == "winc" || token == "binc" ||
            token == "movetime" || token == "depth" || token == "movestogo") {
            if (!markToken(token)) {
                return reject("duplicate " + token + " token");
            }
            const auto parsed = readIntegerToken();
            if (!parsed.has_value()) {
                return reject("invalid integer for " + token);
            }

            if (*parsed < 0) {
                return reject("negative value for " + token);
            }

            if (token == "wtime") {
                command.whiteTimeMs = *parsed;
                sawClock = true;
            } else if (token == "btime") {
                command.blackTimeMs = *parsed;
                sawClock = true;
            } else if (token == "winc") {
                command.whiteIncrementMs = *parsed;
            } else if (token == "binc") {
                command.blackIncrementMs = *parsed;
            } else if (token == "movetime") {
                if (*parsed <= 0) {
                    return reject("movetime must be positive");
                }
                command.moveTimeMs = *parsed;
                sawMoveTime = true;
            } else if (token == "depth") {
                if (*parsed <= 0 || *parsed > std::numeric_limits<int>::max()) {
                    return reject("depth must be positive");
                }
                command.depth = static_cast<int>(*parsed);
                sawDepth = true;
            } else if (token == "movestogo") {
                if (*parsed <= 0 || *parsed > std::numeric_limits<int>::max()) {
                    return reject("movestogo must be positive");
                }
                command.movesToGo = static_cast<int>(*parsed);
            }
            continue;
        }
    }

    if (command.whiteIncrementMs.has_value() != command.blackIncrementMs.has_value()) {
        return reject("winc and binc must be provided together");
    }

    if ((command.whiteTimeMs.has_value() || command.blackTimeMs.has_value()) &&
        !(command.whiteTimeMs.has_value() && command.blackTimeMs.has_value())) {
        return reject("wtime and btime must be provided together");
    }

    if ((command.whiteIncrementMs.has_value() || command.blackIncrementMs.has_value()) &&
        !(command.whiteTimeMs.has_value() && command.blackTimeMs.has_value())) {
        return reject("increments require clock mode");
    }

    sawClock = command.whiteTimeMs.has_value() && command.blackTimeMs.has_value();

    int limitCount = 0;
    if (sawClock) ++limitCount;
    if (sawMoveTime) ++limitCount;
    if (sawDepth) ++limitCount;
    if (sawInfinite) ++limitCount;

    if (limitCount == 0) {
        return reject("missing search limit");
    }
    if (limitCount > 1) {
        return reject("conflicting search limits");
    }
    if (command.movesToGo.has_value() && !sawClock) {
        return reject("movestogo requires clock mode");
    }

    command.whiteIncrementMs = command.whiteIncrementMs.value_or(0);
    command.blackIncrementMs = command.blackIncrementMs.value_or(0);

    if (sawClock) {
        command.mode = SearchLimitMode::Clock;
    } else if (sawMoveTime) {
        command.mode = SearchLimitMode::MoveTime;
    } else if (sawDepth) {
        command.mode = SearchLimitMode::Depth;
    } else {
        command.mode = SearchLimitMode::Infinite;
    }

    return { command, "" };
}

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
    auto emit = [](const auto&... values) {
        std::ostringstream line;
        (line << ... << values);
        UciTelemetry::writeLine(line.str());
    };
    
    int base_fullmove = 1;

    // Ensure a clean initial state on startup
    SearchInternal::initLMR();
    SearchInternal::clearTT();
    SearchInternal::clearKillers();
    SearchInternal::clearHistory();

    std::optional<OpeningBook> openingBook;
    bool openingBookStatusPrinted = false;
    bool useOpeningBook = OPENING_TUNING.enabled;
    int maximumBookDepth = OPENING_TUNING.depthPlies;
    BookSelectionMode bookSelectionMode =
        static_cast<BookSelectionMode>(OPENING_TUNING.selectionMode);
    size_t bookSelectionTopN = OPENING_TUNING.selectionTopN;
    auto ensureOpeningBookLoaded = [&]() {
        if (!openingBook.has_value()) {
            openingBook.emplace(options.bookPath);
            openingBook->load();
            openingBook->setSelectionMode(bookSelectionMode);
            openingBook->setTopN(bookSelectionTopN);
        }
        if (!openingBookStatusPrinted) {
            if (openingBook->lineCount() > 0) {
                emit("info string Opening book loaded: ", openingBook->lineCount(), " entries.");
            } else {
                emit("info string Failed to load book: ", options.bookPath, ". Falling back to search.");
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
            emit("id name BitboardEngine");
            emit("id author Khalil");
            std::ostringstream identity;
            Tuning::reportCompiledProfileIdentity(identity);
            emit(identity.str());
            emit("option name Hash type spin default 16 min 1 max 32768");
            emit("option name Clear Hash type button");
            emit("option name OwnBook type check default ", OPENING_TUNING.enabled ? "true" : "false");
            emit("option name BookDepth type spin default ", OPENING_TUNING.depthPlies, " min 0 max 100");
            emit("option name BookSelection type combo default ", openingSelectionModeName(OPENING_TUNING.selectionMode),
                 " var best var weighted var top-n-weighted");
            emit("option name BookSelectionTopN type spin default ", OPENING_TUNING.selectionTopN, " min 1 max 32");
            emit("option name BookSeed type spin default ", OPENING_TUNING.seed, " min 0 max 2147483647");
            emit("option name MultiPV type spin default 1 min 1 max 8");
            emit("option name Ponder type check default false");
            emit("uciok");
            ensureOpeningBookLoaded();
        }

        // ── isready ──────────────────────────────────────────────────────────
        else if (input.rfind("isready", 0) == 0) {
            UciTelemetry::writeLine("readyok");
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
                    emit("info string Hash set to ", mb, " MB (", SearchConstants::TT_SIZE, " entries)");
                } catch (...) {
                    emit("info string Invalid Hash value: ", optValue);
                }
            } else if (optName == "Clear Hash") {
                SearchInternal::clearTT();
                emit("info string Transposition table cleared");
            } else if (optName == "OwnBook") {
                if (optValue == "true")  useOpeningBook = true;
                else if (optValue == "false") useOpeningBook = false;
                emit("info string OwnBook set to ", useOpeningBook ? "true" : "false");
            } else if (optName == "BookDepth") {
                try {
                    maximumBookDepth = std::stoi(optValue);
                    emit("info string BookDepth set to ", maximumBookDepth);
                } catch (...) {
                    emit("info string Invalid BookDepth value: ", optValue);
                }
            } else if (optName == "BookSelection") {
                if (optValue == "best") {
                    bookSelectionMode = BookSelectionMode::Best;
                } else if (optValue == "weighted") {
                    bookSelectionMode = BookSelectionMode::Weighted;
                } else if (optValue == "top-n-weighted") {
                    bookSelectionMode = BookSelectionMode::TopNWeighted;
                } else {
                    emit("info string Invalid BookSelection value: ", optValue);
                    continue;
                }
                if (openingBook.has_value()) {
                    openingBook->setSelectionMode(bookSelectionMode);
                }
                emit("info string BookSelection set to ", optValue);
            } else if (optName == "BookSelectionTopN") {
                try {
                    bookSelectionTopN = static_cast<size_t>(std::max(1, std::stoi(optValue)));
                    if (openingBook.has_value()) {
                        openingBook->setTopN(bookSelectionTopN);
                    }
                    emit("info string BookSelectionTopN set to ", bookSelectionTopN);
                } catch (...) {
                    emit("info string Invalid BookSelectionTopN value: ", optValue);
                }
            } else if (optName == "BookSeed") {
                try {
                    const uint32_t seed = static_cast<uint32_t>(std::stoul(optValue));
                    ensureOpeningBookLoaded();
                    openingBook->setSeed(seed);
                    emit("info string BookSeed set");
                } catch (...) {
                    emit("info string Invalid BookSeed value: ", optValue);
                }
            } else if (optName == "MultiPV") {
                try {
                    int n = std::stoi(optValue);
                    SearchConstants::MULTI_PV = std::max(1, std::min(n, 8));
                    emit("info string MultiPV set to ", SearchConstants::MULTI_PV);
                } catch (...) {
                    emit("info string Invalid MultiPV value: ", optValue);
                }
            } else {
                emit("info string Unknown option: ", optName);
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
                        emit("info string Invalid FEN: ", fen);
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
            emit("info string eval diagnostics disabled in this build");
#endif
        }

        // ── go ───────────────────────────────────────────────────────────────
        else if (input.rfind("go", 0) == 0) {
            const ParseGoResult parsedGo = parseGoCommand(input);
            if (!parsedGo.command.has_value()) {
                emit("info string error invalid go command: ", parsedGo.error);
                continue;
            }
            const ParsedGoCommand goCommand = *parsedGo.command;
            const bool localPonder = goCommand.ponder;

            ensureOpeningBookLoaded();

            // Stop any previous search (ponder or timed) before launching a
            // new one.  timeAborted will be cleared inside the new thread
            // launch sequence below.
            stopSearch();
            timeAborted.store(false, std::memory_order_relaxed);

            // ── Opening book (synchronous, instantaneous) ─────────────────
            // The book check is decoupled from parsedDepth so that Training
            // Mode (which always sends a depth cap) still consults the book.
            if (openingBook.has_value() &&
                isOpeningBookEligible(
                    localPonder,
                    useOpeningBook,
                    board.sanHistory().size(),
                    maximumBookDepth
                )) {
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
                        std::ostringstream bookInfo;
                        bookInfo << "info string book move " << bm
                                 << " candidates " << bookMove->candidateCount
                                 << " weight " << bookMove->weight;
                        UciTelemetry::writeLine(bookInfo.str());
                        UciTelemetry::writeLine("bestmove " + bm);
                        continue; // back to getline — no thread needed
                    }
                }
            }

            // ── Compute time budget ────────────────────────────────────────
            const bool whiteToMove = board.sideToMove() == Color::White;
            const long long timeLeft  = whiteToMove
                ? goCommand.whiteTimeMs.value_or(0)
                : goCommand.blackTimeMs.value_or(0);
            const long long increment = whiteToMove
                ? goCommand.whiteIncrementMs.value_or(0)
                : goCommand.blackIncrementMs.value_or(0);

            long long timeLimitMs = 2000;
            if (goCommand.mode == SearchLimitMode::Infinite) {
                timeLimitMs = 999'999'999LL;
            } else if (goCommand.mode == SearchLimitMode::MoveTime) {
                timeLimitMs = std::max(
                    static_cast<long long>(TIME_TUNING.allocation.minimumMoveTimeMs),
                    static_cast<long long>(*goCommand.moveTimeMs)
                );
            } else if (goCommand.mode == SearchLimitMode::Depth) {
                timeLimitMs = 999'999'999LL;
            } else if (goCommand.mode == SearchLimitMode::Clock && timeLeft > 0) {
                int move_number = base_fullmove + (board.sanHistory().size() / 2);
                const TimeManagement::ClockBudget budget =
                    TimeManagement::calculateClockBudget(
                        timeLeft,
                        increment,
                        move_number,
                        goCommand.movesToGo,
                        g_hasTwoScores.load(std::memory_order_relaxed),
                        g_lastSearchScore.load(std::memory_order_relaxed),
                        g_prevSearchScore.load(std::memory_order_relaxed)
                    );
                timeLimitMs = budget.timeLimitMs;
            }

            const long long realTimeLimitMs = timeLimitMs;
            if (localPonder) {
                // Ponder elapsed time is intentionally counted from the
                // original go ponder command.  ponderhit restores this real
                // budget instead of granting a fresh post-hit budget.
                AppUci::g_ponderHitTimeLimitMs = realTimeLimitMs;
                timeLimitMs = 999'999'999LL;                      // Override for infinite ponder
                g_isPondering.store(true, std::memory_order_relaxed);
            } else {
                g_isPondering.store(false, std::memory_order_relaxed);
            }

            const int maxDepthToSearch = goCommand.depth.value_or(64);

            // ── Launch search in background thread ─────────────────────────
            // Board is captured by VALUE so the main thread can safely receive
            // new 'position' commands without touching the search's board copy.
            Board boardCopy = board;

            g_searchThread = std::thread([boardCopy = std::move(boardCopy),
                                          maxDepthToSearch,
                                          timeLimitMs,
                                          realTimeLimitMs,
                                          localPonder]() mutable {
                int searchScore = 0;
                const long long effectiveTimeLimitMs =
                    (localPonder && !g_isPondering.load(std::memory_order_relaxed))
                        ? realTimeLimitMs
                        : timeLimitMs;
                auto [bestMove, ponderMove] = findBestMove(boardCopy, maxDepthToSearch, effectiveTimeLimitMs, &searchScore);

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

                UciTelemetry::writeLine(line);
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
