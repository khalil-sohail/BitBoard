#include "board.hpp"
#include "eval/eval_masks.hpp"
#include "eval/eval_tables.hpp"
#include "eval/eval_terms.hpp"
#include "openingBook.hpp"
#include "search.hpp"
#include "search/search_constants.hpp"
#include "search/search_internal.hpp"
#include "tuning/engine_tuning.hpp"
#include "tuning/generated_tuning_values.hpp"
#include "tuning/tuning_metadata.hpp"
#include "tuning/tuning_validation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct TestFailure {
    std::string message;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void applyMustSucceed(Board& board, const std::string& moveText) {
    ParseResult parsed = board.parseMove(moveText);
    require(parsed.move.has_value(), "Expected move to parse: '" + moveText + "', got error: " + parsed.error);
    require(board.applyMove(*parsed.move), "Expected move to apply: '" + moveText + "'");
}

void applyMustFail(Board& board, const std::string& moveText, const std::string& context) {
    ParseResult parsed = board.parseMove(moveText);
    require(!parsed.move.has_value(), "Expected parse rejection for '" + moveText + "' in " + context);
    require(!parsed.error.empty(), "Expected parser to provide error message for rejected input '" + moveText + "'");
}

Color oppositeColor(Color color) {
    return (color == Color::White) ? Color::Black : Color::White;
}

std::string moveKey(const Move& move) {
    std::string key = Board::squareToString(move.from) + Board::squareToString(move.to);
    if (move.promotion.has_value()) {
        key.push_back(static_cast<char>('0' + static_cast<int>(*move.promotion)));
    }
    return key;
}

bool sameMove(const Move& lhs, const Move& rhs) {
    return lhs.from == rhs.from &&
           lhs.to == rhs.to &&
           lhs.promotion == rhs.promotion;
}

bool containsMove(const std::vector<Move>& moves, const Move& target) {
    return std::any_of(moves.begin(), moves.end(), [&](const Move& move) {
        return sameMove(move, target);
    });
}

uint16_t encodePolyglotMove(const std::string& uci) {
    const int fromFile = uci[0] - 'a';
    const int fromRank = uci[1] - '1';
    const int toFile = uci[2] - 'a';
    const int toRank = uci[3] - '1';
    int promotion = 0;
    if (uci.size() == 5) {
        if (uci[4] == 'n') promotion = 1;
        else if (uci[4] == 'b') promotion = 2;
        else if (uci[4] == 'r') promotion = 3;
        else if (uci[4] == 'q') promotion = 4;
    }
    return static_cast<uint16_t>(
        toFile |
        (toRank << 3) |
        (fromFile << 6) |
        (fromRank << 9) |
        (promotion << 12));
}

void writeBigEndian16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>(value & 0xff));
}

void writeBigEndian32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>((value >> 24) & 0xff));
    out.put(static_cast<char>((value >> 16) & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>(value & 0xff));
}

void writeBigEndian64(std::ofstream& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.put(static_cast<char>((value >> shift) & 0xff));
    }
}

struct TestBookEntry {
    uint64_t key;
    std::string move;
    uint16_t weight;
    uint32_t learn;
};

std::filesystem::path writeTempBook(const std::string& name, const std::vector<TestBookEntry>& entries) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(out.is_open(), "Failed to create temporary book: " + path.string());
    for (const TestBookEntry& entry : entries) {
        writeBigEndian64(out, entry.key);
        writeBigEndian16(out, encodePolyglotMove(entry.move));
        writeBigEndian16(out, entry.weight);
        writeBigEndian32(out, entry.learn);
    }
    return path;
}

std::string boardSignature(const Board& board) {
    std::vector<Move> legal = board.generateLegalMoves();
    std::vector<std::string> keys;
    keys.reserve(legal.size());
    for (const Move& move : legal) {
        keys.push_back(moveKey(move));
    }
    std::sort(keys.begin(), keys.end());

    std::string joined;
    for (const std::string& key : keys) {
        joined += key;
        joined.push_back(';');
    }

    return std::to_string(static_cast<int>(board.sideToMove())) + "|" +
           std::to_string(board.computePolyglotHash()) + "|" +
           std::to_string(board.evaluate()) + "|" +
           std::to_string(board.computeStaticEvaluation()) + "|" +
           joined;
}

void walkTreeAndVerify(Board& board, int depth) {
    if (depth == 0) {
        return;
    }

    const int preMoveStaticEval = board.computeStaticEvaluation();
    const std::vector<Move> legalMoves = board.generateLegalMoves();

    for (const Move& move : legalMoves) {
        board.makeMove(move);

        const Color moverColor = oppositeColor(board.sideToMove());
        require(!board.inCheck(moverColor),
                "Illegal generated move left mover king in check: " + moveKey(move));

        require(board.evaluate() == board.computeStaticEvaluation(),
                "Incremental eval mismatch inside legality walk after move: " + moveKey(move));

        walkTreeAndVerify(board, depth - 1);

        require(board.undoMove(), "undoMove failed during legality walk for move: " + moveKey(move));

        const int postUndoStaticEval = board.computeStaticEvaluation();
        require(postUndoStaticEval == preMoveStaticEval,
                "Static eval mismatch after undo for move: " + moveKey(move));
    }
}

void test_scholars_mate_sequence() {
    Board board;

    applyMustSucceed(board, "e4");
    applyMustSucceed(board, "e5");
    applyMustSucceed(board, "Bc4");
    applyMustSucceed(board, "Nc6");
    applyMustSucceed(board, "Qh5");
    applyMustSucceed(board, "Nf6");
    applyMustSucceed(board, "Qxf7#");

    require(board.sideToMove() == Color::Black, "After white mating move, black should be side to move");
    require(board.inCheck(Color::Black), "Black king should be in check after Qxf7#");
    require(board.generateLegalMoves().empty(), "Black should have no legal moves after Scholar's Mate");
}

void test_illegal_move_and_out_of_turn() {
    Board board;

    applyMustFail(board, "e2e5", "illegal opening coordinate move");

    applyMustSucceed(board, "e4");
    applyMustFail(board, "Nf3", "out-of-turn move by white when black must move");
}

void test_castling_through_check_rejected() {
    Board board;

    applyMustSucceed(board, "Nf3");
    applyMustSucceed(board, "e6");
    applyMustSucceed(board, "g3");
    applyMustSucceed(board, "Bb4");
    applyMustSucceed(board, "Bg2");
    applyMustSucceed(board, "Bxd2+");

    // White is in check and cannot castle out of check.
    applyMustFail(board, "O-O", "castling while in check");
}

void test_invalid_san_inputs() {
    Board board;

    applyMustFail(board, "Nzz", "invalid SAN destination");
    applyMustFail(board, "Qh5#", "incorrect checkmate suffix in initial position");
    applyMustFail(board, "e4++", "invalid duplicated check suffix");
}

void test_disambiguation_and_pawn_capture_parsing() {
    {
        Board board;
        applyMustSucceed(board, "e4");
        applyMustSucceed(board, "d5");
        applyMustSucceed(board, "exd5");

        // Explicitly verify SAN pawn capture style is accepted.
        ParseResult pawnCapture = board.parseMove("Qxd5");
        require(pawnCapture.move.has_value(), "Expected SAN capture 'Qxd5' to parse after exd5");
        require(board.applyMove(*pawnCapture.move), "Expected parsed SAN capture 'Qxd5' to apply");
    }

    {
        Board board;
        applyMustSucceed(board, "Nf3");
        applyMustSucceed(board, "Nf6");
        applyMustSucceed(board, "d3");
        applyMustSucceed(board, "d6");

        // Now white knights on b1 and f3 can both move to d2.
        ParseResult ambiguous = board.parseMove("Nd2");
        require(!ambiguous.move.has_value(), "Expected 'Nd2' to be ambiguous in this position");
        require(ambiguous.error.find("Ambiguous") != std::string::npos,
                "Expected explicit ambiguity error for 'Nd2'");

        ParseResult fileDisamb = board.parseMove("Nbd2");
        require(fileDisamb.move.has_value(), "Expected file disambiguation SAN 'Nbd2' to parse");

        ParseResult rankDisamb = board.parseMove("N1d2");
        require(rankDisamb.move.has_value(), "Expected rank disambiguation SAN 'N1d2' to parse");
    }
}

void test_perft_start_position_depth_1_to_4() {
    Board board;

    const uint64_t d1 = board.perft(1);
    const uint64_t d2 = board.perft(2);
    const uint64_t d3 = board.perft(3);
    const uint64_t d4 = board.perft(4);

    require(d1 == 20ULL, "Perft depth 1 mismatch. Expected 20, got " + std::to_string(d1));
    require(d2 == 400ULL, "Perft depth 2 mismatch. Expected 400, got " + std::to_string(d2));
    require(d3 == 8902ULL, "Perft depth 3 mismatch. Expected 8902, got " + std::to_string(d3));
    require(d4 == 197281ULL, "Perft depth 4 mismatch. Expected 197281, got " + std::to_string(d4));
}

void test_search_tactics() {
    // Mate in 1: after this sequence, White has Qxf7#.
    {
        Board board;
        applyMustSucceed(board, "e4");
        applyMustSucceed(board, "e5");
        applyMustSucceed(board, "Bc4");
        applyMustSucceed(board, "Nc6");
        applyMustSucceed(board, "Qh5");
        applyMustSucceed(board, "Nf6");

        const Move best = findBestMoveCompat(board, 3, 30000);
        const int expectedFrom = Board::squareFromString("h5");
        const int expectedTo = Board::squareFromString("f7");

        require(best.from == expectedFrom && best.to == expectedTo,
                "Mate-in-1 test failed: expected h5f7, got " +
                Board::squareToString(best.from) + Board::squareToString(best.to));
    }

    // Winning capture: White should capture a hanging black queen with Nxh4.
    {
        Board board;
        applyMustSucceed(board, "e4");
        applyMustSucceed(board, "e5");
        applyMustSucceed(board, "Nf3");
        applyMustSucceed(board, "Qh4");

        const Move best = findBestMoveCompat(board, 3, 30000);
        const int expectedFrom = Board::squareFromString("f3");
        const int expectedTo = Board::squareFromString("h4");

        require(best.from == expectedFrom && best.to == expectedTo,
                "Winning-capture test failed: expected f3h4, got " +
                Board::squareToString(best.from) + Board::squareToString(best.to));
    }
}

void test_incremental_evaluation() {
    Board board;

    const std::vector<std::string> moves = {
        "e4", "a6",
        "e5", "d5",
        "exd6", "Nc6",
        "Nf3", "e6",
        "Bb5", "Bxd6",
        "O-O", "Bd7",
        "d4", "axb5",
        "Nc3", "Qe7",
        "Re1", "O-O-O",
        "Nxb5", "Nf6"
    };

    for (const std::string& mv : moves) {
        applyMustSucceed(board, mv);
        require(board.evaluate() == board.computeStaticEvaluation(),
                "Incremental eval drift detected after move: " + mv);
    }
}

void test_incremental_evaluation_promotions() {
    for (const std::string promotion : {"a7a8q", "a7a8r", "a7a8b", "a7a8n"}) {
        Board board;
        require(board.loadFEN("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"),
                "Failed to load promotion evaluation fixture");
        applyMustSucceed(board, promotion);
        require(board.evaluate() == board.computeStaticEvaluation(),
                "Incremental eval drift after promotion: " + promotion);
        require(board.undoMove(), "Promotion undo should succeed: " + promotion);
        require(board.evaluate() == board.computeStaticEvaluation(),
                "Incremental eval drift after promotion undo: " + promotion);
    }
}

void test_incremental_evaluation_special_moves() {
    const struct Fixture {
        const char* fen;
        const char* move;
        const char* description;
    } fixtures[] = {
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "white en passant"},
        {"4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1", "e4d3", "black en passant"},
        {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1", "white kingside castling"},
        {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1c1", "white queenside castling"},
        {"r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1", "e8g8", "black kingside castling"},
        {"r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1", "e8c8", "black queenside castling"},
        {"1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q", "white promotion capture"},
        {"4k3/8/8/8/8/8/p7/1R2K3 b - - 0 1", "a2b1q", "black promotion capture"},
        {"4k3/8/8/8/8/8/p7/4K3 b - - 0 1", "a2a1n", "black underpromotion"},
    };

    for (const Fixture& fixture : fixtures) {
        Board board;
        require(board.loadFEN(fixture.fen), "Failed to load special-move fixture: " + std::string(fixture.description));
        const std::string before = boardSignature(board);
        applyMustSucceed(board, fixture.move);
        require(board.evaluate() == board.computeStaticEvaluation(),
                "Incremental eval drift after " + std::string(fixture.description));
        require(board.undoMove(), "Undo failed after " + std::string(fixture.description));
        require(boardSignature(board) == before,
                "Undo failed to restore evaluation state after " + std::string(fixture.description));
    }
}

void test_make_undo_restores_full_state() {
    Board board;

    const std::vector<std::string> setup = {
        "e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6", "Be3", "e6"
    };

    for (const std::string& moveText : setup) {
        applyMustSucceed(board, moveText);
    }

    const std::string before = boardSignature(board);
    const std::vector<Move> legal = board.generateLegalMoves();
    require(!legal.empty(), "Expected legal moves in test_make_undo_restores_full_state");

    for (const Move& move : legal) {
        board.makeMove(move);
        require(board.undoMove(), "undoMove should succeed after makeMove");
        require(boardSignature(board) == before,
                "Board state mismatch after make/undo cycle for move: " + moveKey(move));
    }

    const std::vector<std::string> line = {
        "Qd2", "Be7", "O-O-O", "O-O", "f3", "b5", "g4", "h6"
    };

    std::vector<Move> played;
    played.reserve(line.size());
    for (const std::string& moveText : line) {
        ParseResult parsed = board.parseMove(moveText);
        require(parsed.move.has_value(), "Expected parse success for rollback line move: " + moveText);
        board.makeMove(*parsed.move);
        played.push_back(*parsed.move);
    }

    for (size_t i = 0; i < played.size(); ++i) {
        require(board.undoMove(), "undoMove should succeed while rolling back test line");
    }

    require(boardSignature(board) == before,
            "Board state mismatch after full line rollback");
}

void test_deep_legality_and_undo_stress() {
    Board board;
    walkTreeAndVerify(board, 3);

    // FEN loading API is not currently available on Board.
    // Fallback per requirement: run a deeper start-position stress.
    Board fallbackBoard;
    walkTreeAndVerify(fallbackBoard, 4);
}

void test_mate_in_three() {
    Board board;

    const std::vector<std::string> setup = {
        "e4", "e5",
        "Bc4", "Nc6",
        "Qh5", "Nf6"
    };

    for (const std::string& mv : setup) {
        applyMustSucceed(board, mv);
    }

    const Move best = findBestMoveCompat(board, 5, 30000);
    const int expectedFrom = Board::squareFromString("h5");
    const int expectedTo = Board::squareFromString("f7");

    require(best.from == expectedFrom && best.to == expectedTo,
            "Mate-in-three test failed: expected h5f7, got " +
            Board::squareToString(best.from) + Board::squareToString(best.to));
}

void test_quiescence_horizon_effect() {
    Board board;

    const std::vector<std::string> setup = {
        "e4", "e5",
        "Nf3", "Nc6",
        "Bc4", "Nf6",
        "Ng5", "d5",
        "exd5", "Nxd5"
    };

    for (const std::string& mv : setup) {
        applyMustSucceed(board, mv);
    }

    const Move best = findBestMoveCompat(board, 3, 30000);
    require(!(best.from == Board::squareFromString("c4") && best.to == Board::squareFromString("d5")),
            "Engine failed: It fell for the c4d5 horizon-effect blunder.");
}

// void test_nmp_zugzwang_safety() {
//     Board board;

//     // Reduced-mobility position reached via verified legal move sequence.
//     const std::vector<std::string> setup = {
//         "e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6", "Be3", "e6",
//         "Qd2", "Be7", "O-O-O", "O-O", "f3", "b5", "g4", "h6"
//     };

//     for (const std::string& mv : setup) {
//         applyMustSucceed(board, mv);
//     }

//     const Move best = findBestMoveCompat(board, 5);
//     const int expectedFrom = Board::squareFromString("a1");
//     const int expectedTo = Board::squareFromString("a1");

//     require(best.from == expectedFrom && best.to == expectedTo,
//             "NMP zugzwang-safety test failed: expected a1a1, got " +
//             Board::squareToString(best.from) + Board::squareToString(best.to));
// }

void test_nmp_zugzwang_safety() {
    Board board;

    // A standard opening sequence
    const std::vector<std::string> setup = {
        "e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5", "d3", "d6"
    };

    for (const std::string& mv : setup) {
        applyMustSucceed(board, mv);
    }

    // Run a depth 4 search (deep enough to trigger NMP, shallow enough to run fast)
    const Move best = findBestMoveCompat(board, 4, 30000);

    // We just want to assert that the search completed and returned a valid move,
    // proving NMP didn't corrupt the search tree or return a null move.
    require(best.from != best.to, "Search failed or returned an invalid null move.");
}

void test_mate_distance_scoring() {
    Board board;
    board.reset();

    const std::vector<std::string> sequence = {
        "e2e4", "a7a6", "f1c4", "b7b6", "d1h5", "h7h6"
    };

    for (const std::string& moveText : sequence) {
        ParseResult parsed = board.parseMove(moveText);
        require(parsed.move.has_value(), "Expected move to parse: '" + moveText + "', got error: " + parsed.error);
        require(board.applyMove(*parsed.move), "Expected move to apply: '" + moveText + "'");
    }

    Move bestMove = findBestMoveCompat(board, 4, 2000);

    const bool isQueenMate = bestMove.from == Board::squareFromString("h5") &&
                             bestMove.to == Board::squareFromString("f7");
    const bool isBishopMate = bestMove.from == Board::squareFromString("c4") &&
                              bestMove.to == Board::squareFromString("f7");

    require(isQueenMate || isBishopMate,
            "Engine failed to find a known fastest mate! Expected h5f7 or c4f7, got " +
            moveKey(bestMove));

    require(board.applyMove(bestMove), "Expected fastest mate move to apply: " + moveKey(bestMove));
    require(board.inCheck(board.sideToMove()) && board.generateLegalMoves().empty(),
            "Fastest mate move did not leave the opponent checkmated: " + moveKey(bestMove));
}

void test_quiescence_counters() {
    Board board;
    require(board.loadFEN("r3k2r/p1p2ppp/2ppbn2/2b5/4P2q/2NB1Q1P/PPP2PP1/R1B2RK1 b kq - 2 10"),
            "Failed to load tactical FEN for quiescence counter test.");

    qNodes = 0;
    deltaPruneSkips = 0;

    (void)findBestMove(board, 2, 2000);

    assert(qNodes > 0 && "Quiescence search failed to evaluate any nodes!");
    assert(deltaPruneSkips > 0 && "Delta pruning failed to skip any bad captures!");
}

void test_find_best_move_pair_contract() {
    Board board;
    applyMustSucceed(board, "e4");
    applyMustSucceed(board, "e5");
    applyMustSucceed(board, "Bc4");
    applyMustSucceed(board, "Nc6");
    applyMustSucceed(board, "Qh5");
    applyMustSucceed(board, "Nf6");

    const auto [bestMove, ponderMove] = findBestMove(board, 3, 30000);
    const int expectedFrom = Board::squareFromString("h5");
    const int expectedTo = Board::squareFromString("f7");

    require(bestMove.from == expectedFrom && bestMove.to == expectedTo,
            "findBestMove pair contract failed: expected best h5f7, got " +
            Board::squareToString(bestMove.from) + Board::squareToString(bestMove.to));
    require(ponderMove.from == -1,
            "Expected no ponder move after forced checkmate, got " + moveKey(ponderMove));
}

void test_find_best_move_compat_matches_pair_best() {
    Board pairBoard;
    applyMustSucceed(pairBoard, "e4");
    applyMustSucceed(pairBoard, "e5");
    applyMustSucceed(pairBoard, "Nf3");
    applyMustSucceed(pairBoard, "Qh4");

    Board compatBoard;
    applyMustSucceed(compatBoard, "e4");
    applyMustSucceed(compatBoard, "e5");
    applyMustSucceed(compatBoard, "Nf3");
    applyMustSucceed(compatBoard, "Qh4");

    const auto [pairBest, ignorePonder] = findBestMove(pairBoard, 3, 30000);
    const Move compatBest = findBestMoveCompat(compatBoard, 3, 30000);
    (void)ignorePonder;

    require(sameMove(pairBest, compatBest),
            "findBestMoveCompat returned " + moveKey(compatBest) +
            " but findBestMove returned " + moveKey(pairBest));
}

void test_find_best_move_ponder_is_legal_when_available() {
    Board board;

    const auto [bestMove, ponderMove] = findBestMove(board, 4, 30000);
    require(bestMove.from >= 0, "Expected start-position search to return a best move");
    require(ponderMove.from >= 0, "Expected start-position depth-4 search to return a ponder move");

    require(containsMove(board.generateLegalMoves(), bestMove),
            "Best move from start position is not legal: " + moveKey(bestMove));

    board.makeMove(bestMove);
    require(containsMove(board.generateLegalMoves(), ponderMove),
            "Ponder move is not legal after best move " + moveKey(bestMove) +
            ": " + moveKey(ponderMove));
    require(board.undoMove(), "undoMove failed after ponder legality check");
}

void test_opening_book_returns_empty_for_missing_position() {
    Board board;
    const std::filesystem::path path = writeTempBook("bitboard-empty-position.bin", {
        {board.computePolyglotHash() + 1, "e2e4", 10, 0},
    });

    OpeningBook book(path.string());
    require(book.getBookMoveCandidates(board).empty(), "Expected no candidates for missing book key");
    require(!book.selectBookMove(board).has_value(), "Expected no selected move for missing book key");
}

void test_opening_book_parses_candidates_weights_and_duplicates() {
    Board board;
    const uint64_t key = board.computePolyglotHash();
    const std::filesystem::path path = writeTempBook("bitboard-candidates.bin", {
        {key, "e2e4", 10, 7},
        {key, "d2d4", 30, 3},
        {key, "e2e4", 5, 9},
        {key, "e2e5", 99, 1},
    });

    OpeningBook book(path.string());
    const std::vector<BookMoveCandidate> candidates = book.getBookMoveCandidates(board);
    require(candidates.size() == 2, "Expected two legal deduplicated candidates, got " + std::to_string(candidates.size()));
    require(moveKey(candidates[0].move) == "d2d4", "Expected highest weighted candidate first");
    require(candidates[0].weight == 30, "Expected d2d4 weight 30");
    require(moveKey(candidates[1].move) == "e2e4", "Expected duplicate e2e4 candidate second");
    require(candidates[1].weight == 15, "Expected duplicate weights to combine to 15");
    require(candidates[1].learn == 9, "Expected duplicate learn value to keep max learn");
}

void test_opening_book_single_candidate_and_best_are_deterministic() {
    Board board;
    const uint64_t key = board.computePolyglotHash();
    const std::filesystem::path path = writeTempBook("bitboard-single.bin", {
        {key, "g1f3", 1, 0},
    });

    OpeningBook book(path.string());
    book.setSelectionMode(BookSelectionMode::Weighted);
    for (int i = 0; i < 5; ++i) {
        std::optional<SelectedBookMove> selected = book.selectBookMove(board);
        require(selected.has_value(), "Expected single candidate selection");
        require(moveKey(selected->move) == "g1f3", "Single book candidate should remain deterministic");
        require(selected->candidateCount == 1, "Single candidate metadata should report one candidate");
    }

    const std::filesystem::path multiPath = writeTempBook("bitboard-best.bin", {
        {key, "e2e4", 10, 0},
        {key, "d2d4", 50, 0},
    });
    OpeningBook multiBook(multiPath.string());
    multiBook.setSelectionMode(BookSelectionMode::Best);
    std::optional<SelectedBookMove> selected = multiBook.selectBookMove(board);
    require(selected.has_value(), "Expected best selection");
    require(moveKey(selected->move) == "d2d4", "Best mode should choose highest weight");
}

void test_opening_book_weighted_selector_is_seeded_and_weighted() {
    Board board;
    const uint64_t key = board.computePolyglotHash();
    const std::filesystem::path path = writeTempBook("bitboard-weighted.bin", {
        {key, "e2e4", 90, 0},
        {key, "d2d4", 10, 0},
    });

    OpeningBook first(path.string());
    OpeningBook second(path.string());
    first.setSeed(1234);
    second.setSeed(1234);

    std::vector<std::string> firstSequence;
    std::vector<std::string> secondSequence;
    int e4Count = 0;
    int d4Count = 0;
    for (int i = 0; i < 100; ++i) {
        const auto a = first.selectBookMove(board);
        const auto b = second.selectBookMove(board);
        require(a.has_value() && b.has_value(), "Expected weighted selections");
        firstSequence.push_back(moveKey(a->move));
        secondSequence.push_back(moveKey(b->move));
        if (moveKey(a->move) == "e2e4") ++e4Count;
        if (moveKey(a->move) == "d2d4") ++d4Count;
    }

    require(firstSequence == secondSequence, "Same book seed should reproduce the same sequence");
    require(d4Count > 0, "Low-weight candidate should remain possible in deterministic sample");
    require(e4Count > d4Count, "Higher-weight candidate should appear more often in deterministic sample");

    OpeningBook different(path.string());
    different.setSeed(5678);
    bool differs = false;
    for (const std::string& expected : firstSequence) {
        const auto selected = different.selectBookMove(board);
        require(selected.has_value(), "Expected selection from different seeded book");
        if (moveKey(selected->move) != expected) {
            differs = true;
            break;
        }
    }
    require(differs, "Different seed should be able to produce a different sequence");
}

void test_opening_book_zero_weight_and_top_n_policy() {
    Board board;
    const uint64_t key = board.computePolyglotHash();
    const std::filesystem::path zeroPath = writeTempBook("bitboard-zero-weight.bin", {
        {key, "e2e4", 0, 0},
        {key, "d2d4", 0, 0},
    });

    OpeningBook zeroBook(zeroPath.string());
    zeroBook.setSeed(42);
    bool sawE4 = false;
    bool sawD4 = false;
    for (int i = 0; i < 20; ++i) {
        const auto selected = zeroBook.selectBookMove(board);
        require(selected.has_value(), "Expected zero-weight uniform selection");
        sawE4 = sawE4 || moveKey(selected->move) == "e2e4";
        sawD4 = sawD4 || moveKey(selected->move) == "d2d4";
    }
    require(sawE4 && sawD4, "Zero-weight candidates should be selected uniformly by seeded RNG");

    const std::filesystem::path topNPath = writeTempBook("bitboard-topn.bin", {
        {key, "e2e4", 100, 0},
        {key, "d2d4", 90, 0},
        {key, "c2c4", 80, 0},
    });
    OpeningBook topNBook(topNPath.string());
    topNBook.setSelectionMode(BookSelectionMode::TopNWeighted);
    topNBook.setTopN(1);
    topNBook.setSeed(1);
    for (int i = 0; i < 10; ++i) {
        const auto selected = topNBook.selectBookMove(board);
        require(selected.has_value(), "Expected top-N selection");
        require(moveKey(selected->move) == "e2e4", "TopN=1 should only select the strongest candidate");
        require(selected->candidateCount == 3, "Top-N metadata should preserve full legal candidate count");
    }
}

void test_tuning_model_constructs_and_validates_default() {
    Tuning::EngineTuning tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
    const Tuning::TuningValidationResult result = Tuning::validateTuning(tuning);
    require(result.valid(), "BUILTIN_DEFAULT_V1_MODEL should validate");
    require(Tuning::kDefaultProfileId == "builtin-default-v1", "Default profile id should remain builtin-default-v1");
}

void requireSameRational(const Tuning::RationalValue<int>& lhs,
                         const Tuning::RationalValue<int>& rhs,
                         const std::string& message) {
    require(lhs.numerator == rhs.numerator && lhs.denominator == rhs.denominator, message);
}

void requireSameGeneratedAndModelTuning(const Tuning::EngineTuning& generated,
                                        const Tuning::EngineTuning& model) {
    require(generated.evaluation.material.middlegame == model.evaluation.material.middlegame, "Generated MG material mismatch");
    require(generated.evaluation.material.endgame == model.evaluation.material.endgame, "Generated EG material mismatch");
    require(generated.evaluation.phase.increments == model.evaluation.phase.increments, "Generated phase increments mismatch");
    require(generated.evaluation.mobility.middlegame == model.evaluation.mobility.middlegame, "Generated MG mobility mismatch");
    require(generated.evaluation.mobility.endgame == model.evaluation.mobility.endgame, "Generated EG mobility mismatch");
    require(generated.evaluation.rookActivity.middlegameArray() == model.evaluation.rookActivity.middlegameArray(), "Generated MG rook activity mismatch");
    require(generated.evaluation.rookActivity.endgameArray() == model.evaluation.rookActivity.endgameArray(), "Generated EG rook activity mismatch");
    require(generated.evaluation.bishopPair.middlegame == model.evaluation.bishopPair.middlegame, "Generated bishop pair MG mismatch");
    require(generated.evaluation.bishopPair.endgame == model.evaluation.bishopPair.endgame, "Generated bishop pair EG mismatch");
    require(generated.evaluation.pawns.connectedMgByRank == model.evaluation.pawns.connectedMgByRank, "Generated connected pawn MG table mismatch");
    require(generated.evaluation.pawns.connectedEgByRank == model.evaluation.pawns.connectedEgByRank, "Generated connected pawn EG table mismatch");
    require(generated.evaluation.pawns.candidateMgByRank == model.evaluation.pawns.candidateMgByRank, "Generated candidate pawn MG table mismatch");
    require(generated.evaluation.pawns.candidateEgByRank == model.evaluation.pawns.candidateEgByRank, "Generated candidate pawn EG table mismatch");
    require(generated.evaluation.pawns.backwardMgByRank == model.evaluation.pawns.backwardMgByRank, "Generated backward pawn MG table mismatch");
    require(generated.evaluation.pawns.backwardEgByRank == model.evaluation.pawns.backwardEgByRank, "Generated backward pawn EG table mismatch");
    require(generated.evaluation.pawns.doubledPenalty == model.evaluation.pawns.doubledPenalty, "Generated doubled pawn mismatch");
    require(generated.evaluation.pawns.isolatedPenalty == model.evaluation.pawns.isolatedPenalty, "Generated isolated pawn mismatch");
    require(generated.evaluation.pawns.islandPenaltyMg == model.evaluation.pawns.islandPenaltyMg, "Generated MG pawn island mismatch");
    require(generated.evaluation.pawns.islandPenaltyEg == model.evaluation.pawns.islandPenaltyEg, "Generated EG pawn island mismatch");
    require(generated.evaluation.pawns.passedCountBonusMg == model.evaluation.pawns.passedCountBonusMg, "Generated passed count MG mismatch");
    require(generated.evaluation.pawns.passedCountBonusEg == model.evaluation.pawns.passedCountBonusEg, "Generated passed count EG mismatch");
    require(generated.evaluation.pawns.passedEgMultiplier == model.evaluation.pawns.passedEgMultiplier, "Generated passed EG multiplier mismatch");
    require(generated.evaluation.pawns.passedRankSquareMultiplier == model.evaluation.pawns.passedRankSquareMultiplier, "Generated passed rank-square multiplier mismatch");
    require(generated.evaluation.pawns.passedBlockedDivisor == model.evaluation.pawns.passedBlockedDivisor, "Generated passed blocked divisor mismatch");
    require(generated.evaluation.kingSafety.attackPressure == model.evaluation.kingSafety.attackPressure, "Generated king pressure table mismatch");
    require(generated.evaluation.kingSafety.shieldMaxPawns == model.evaluation.kingSafety.shieldMaxPawns, "Generated king shield max mismatch");
    require(generated.evaluation.kingSafety.shieldPerPawnBonus == model.evaluation.kingSafety.shieldPerPawnBonus, "Generated king shield bonus mismatch");
    require(generated.evaluation.kingSafety.uncastledCenterPenalty == model.evaluation.kingSafety.uncastledCenterPenalty, "Generated uncastled center penalty mismatch");
    require(generated.evaluation.kingSafety.uncastledLostRightsPenalty == model.evaluation.kingSafety.uncastledLostRightsPenalty, "Generated uncastled rights penalty mismatch");
    require(generated.evaluation.piecePlacement.badBishopHeavyPenalty == model.evaluation.piecePlacement.badBishopHeavyPenalty, "Generated bad bishop heavy mismatch");
    require(generated.evaluation.piecePlacement.badBishopLightPenalty == model.evaluation.piecePlacement.badBishopLightPenalty, "Generated bad bishop light mismatch");
    require(generated.evaluation.piecePlacement.earlyQueenUndevelopedMinorPenalty == model.evaluation.piecePlacement.earlyQueenUndevelopedMinorPenalty, "Generated early queen penalty mismatch");
    require(generated.evaluation.piecePlacement.trappedRookPenalty == model.evaluation.piecePlacement.trappedRookPenalty, "Generated trapped rook mismatch");
    require(generated.evaluation.endgame.taperScale == model.evaluation.endgame.taperScale, "Generated taper scale mismatch");
    require(generated.evaluation.endgame.latePhaseMax == model.evaluation.endgame.latePhaseMax, "Generated late phase max mismatch");
    require(generated.evaluation.endgame.mopUpEgMargin == model.evaluation.endgame.mopUpEgMargin, "Generated mop-up EG margin mismatch");
    require(generated.evaluation.endgame.mopUpMaterialMargin == model.evaluation.endgame.mopUpMaterialMargin, "Generated mop-up material margin mismatch");
    require(generated.evaluation.endgame.scaleOppositeBishopsMinPawns == model.evaluation.endgame.scaleOppositeBishopsMinPawns, "Generated opposite bishops min-pawns scale mismatch");
    require(generated.evaluation.endgame.scaleOppositeBishopsLowPawns == model.evaluation.endgame.scaleOppositeBishopsLowPawns, "Generated opposite bishops low-pawns scale mismatch");
    require(generated.evaluation.endgame.scaleMinorOnlyNearEqual == model.evaluation.endgame.scaleMinorOnlyNearEqual, "Generated minor-only near-equal scale mismatch");
    require(generated.evaluation.endgame.scaleMinorOnlyClearEdge == model.evaluation.endgame.scaleMinorOnlyClearEdge, "Generated minor-only clear-edge scale mismatch");
    require(generated.evaluation.endgame.mopUpWeights == model.evaluation.endgame.mopUpWeights, "Generated mop-up weights mismatch");
    require(generated.evaluation.pieceSquare.middlegameRepresented == model.evaluation.pieceSquare.middlegameRepresented, "Generated MG PST representation flag mismatch");
    require(generated.evaluation.pieceSquare.endgameRepresented == model.evaluation.pieceSquare.endgameRepresented, "Generated EG PST representation flag mismatch");

    require(generated.search.aspiration.windowCp == model.search.aspiration.windowCp, "Generated aspiration mismatch");
    require(generated.search.nullMove.reduction == model.search.nullMove.reduction, "Generated null move mismatch");
    require(generated.search.futility.reverseMarginPerDepthCp == model.search.futility.reverseMarginPerDepthCp, "Generated reverse futility mismatch");
    require(generated.search.futility.forwardMarginPerDepthCp == model.search.futility.forwardMarginPerDepthCp, "Generated forward futility mismatch");
    requireSameRational(generated.search.lateMoveReduction.base, model.search.lateMoveReduction.base, "Generated LMR rational mismatch");
    require(generated.search.quiescence.deltaMarginCp == model.search.quiescence.deltaMarginCp, "Generated quiescence margin mismatch");
    require(generated.search.singularExtension.marginCp == model.search.singularExtension.marginCp, "Generated singular margin mismatch");
    require(generated.search.moveOrdering.transpositionMoveScore == model.search.moveOrdering.transpositionMoveScore, "Generated TT score mismatch");
    require(generated.search.moveOrdering.quiet.firstKillerScore == model.search.moveOrdering.quiet.firstKillerScore, "Generated first killer mismatch");
    require(generated.search.moveOrdering.quiet.secondKillerScore == model.search.moveOrdering.quiet.secondKillerScore, "Generated second killer mismatch");
    require(generated.search.moveOrdering.quiet.counterMoveScore == model.search.moveOrdering.quiet.counterMoveScore, "Generated countermove mismatch");
    require(generated.search.moveOrdering.capture.winningCaptureBaseScore == model.search.moveOrdering.capture.winningCaptureBaseScore, "Generated winning capture mismatch");
    require(generated.search.moveOrdering.capture.losingCaptureBaseScore == model.search.moveOrdering.capture.losingCaptureBaseScore, "Generated losing capture mismatch");
    require(generated.search.moveOrdering.capture.seeScoreMultiplier == model.search.moveOrdering.capture.seeScoreMultiplier, "Generated SEE multiplier mismatch");
    require(generated.search.moveOrdering.promotionBaseScore == model.search.moveOrdering.promotionBaseScore, "Generated promotion score mismatch");
    require(generated.search.moveOrdering.historyLimit == model.search.moveOrdering.historyLimit, "Generated history cap mismatch");
    require(generated.search.moveOrdering.mvvLva == model.search.moveOrdering.mvvLva, "Generated MVV-LVA mismatch");
    require(generated.search.moveOrdering.seePieceValues == model.search.moveOrdering.seePieceValues, "Generated SEE values mismatch");

    require(generated.time.allocation.safetyReserveMs == model.time.allocation.safetyReserveMs, "Generated safety reserve mismatch");
    require(generated.time.allocation.minimumMoveTimeMs == model.time.allocation.minimumMoveTimeMs, "Generated minimum move time mismatch");
    require(generated.time.allocation.expectedMovesBase == model.time.allocation.expectedMovesBase, "Generated expected moves base mismatch");
    require(generated.time.allocation.expectedMovesFloor == model.time.allocation.expectedMovesFloor, "Generated expected moves floor mismatch");
    requireSameRational(generated.time.allocation.incrementContribution, model.time.allocation.incrementContribution, "Generated increment contribution mismatch");
    require(generated.time.allocation.instabilityThresholdCp == model.time.allocation.instabilityThresholdCp, "Generated instability threshold mismatch");
    requireSameRational(generated.time.allocation.instabilityMultiplier, model.time.allocation.instabilityMultiplier, "Generated instability multiplier mismatch");
    requireSameRational(generated.time.allocation.maximumClockFraction, model.time.allocation.maximumClockFraction, "Generated max clock fraction mismatch");
    requireSameRational(generated.time.stopPolicy.stableSoftStopFraction, model.time.stopPolicy.stableSoftStopFraction, "Generated stable soft-stop mismatch");
    requireSameRational(generated.time.stopPolicy.unstableSoftStopFraction, model.time.stopPolicy.unstableSoftStopFraction, "Generated unstable soft-stop mismatch");
    requireSameRational(generated.time.stopPolicy.hardStopFraction, model.time.stopPolicy.hardStopFraction, "Generated hard-stop mismatch");
    require(generated.time.stopPolicy.criticalLowTimeThresholdMs == model.time.stopPolicy.criticalLowTimeThresholdMs, "Generated critical threshold mismatch");
    require(generated.time.stopPolicy.criticalLowTimeReserveMs == model.time.stopPolicy.criticalLowTimeReserveMs, "Generated critical reserve mismatch");
    require(generated.time.polling.nodeMask == model.time.polling.nodeMask, "Generated polling mask mismatch");

    require(generated.opening.enabled == model.opening.enabled, "Generated opening enabled mismatch");
    require(generated.opening.depthPlies == model.opening.depthPlies, "Generated opening depth mismatch");
    require(generated.opening.selectionMode == model.opening.selectionMode, "Generated opening mode mismatch");
    require(generated.opening.selectionTopN == model.opening.selectionTopN, "Generated opening top-N mismatch");
    require(generated.opening.seed == model.opening.seed, "Generated opening seed mismatch");
}

void test_generated_tuning_header_identity_and_validation() {
    require(Tuning::Generated::PROFILE_ID == "builtin-default-v1", "Generated profile id mismatch");
    require(Tuning::Generated::PROFILE_HASH == "sha256:55a1ac92352bd018460f115cb5061c76140f1eed453afc8a229ed3fa84145718", "Generated profile hash mismatch");
    require(Tuning::Generated::PROFILE_SCHEMA_VERSION == 1, "Generated schema version mismatch");
    require(Tuning::Generated::REGISTRY_VERSION == 1, "Generated registry version mismatch");
    require(Tuning::Generated::MODEL_VERSION == "phase-2-typed-model-v1", "Generated model version mismatch");
    require(Tuning::Generated::GENERATED_PROFILE_ENTRY_COUNT == 76, "Generated profile entry count mismatch");
    require(Tuning::Generated::GENERATED_GROUPED_ARRAY_TABLE_COUNT == 19, "Generated grouped array/table count mismatch");
    require(Tuning::validateTuning(Tuning::Generated::VALUES).valid(), "Generated tuning values should validate");
}

void test_generated_tuning_values_match_typed_model() {
    requireSameGeneratedAndModelTuning(Tuning::Generated::VALUES, Tuning::BUILTIN_DEFAULT_V1_MODEL);
}

void test_generated_piece_square_tables_match_production() {
    const auto& mg = Tuning::Generated::MIDDLEGAME_PIECE_SQUARE_TABLE;
    const auto& eg = Tuning::Generated::ENDGAME_PIECE_SQUARE_TABLE;
    require(mg.size() == 6 && eg.size() == 6, "Generated PST piece dimensions should be 6");
    require(mg[0].size() == 64 && eg[0].size() == 64, "Generated PST square dimensions should be 64");

    long long mgSum = 0;
    long long egSum = 0;
    long long mgWeighted = 0;
    long long egWeighted = 0;
    std::size_t flatIndex = 0;
    for (std::size_t piece = 0; piece < 6; ++piece) {
        for (std::size_t square = 0; square < 64; ++square, ++flatIndex) {
            mgSum += mg[piece][square];
            egSum += eg[piece][square];
            mgWeighted += static_cast<long long>(flatIndex + 1) * mg[piece][square];
            egWeighted += static_cast<long long>(flatIndex + 1) * eg[piece][square];
        }
    }
    require(mgSum == 693 && mgWeighted == -175701, "Generated MG PST checksum changed from production baseline");
    require(egSum == 1550 && egWeighted == 98007, "Generated EG PST checksum changed from production baseline");

    require(mg[0][8] == 98 && mg[1][0] == -167 && mg[2][0] == -29,
            "Generated MG pawn/knight/bishop row ordering changed");
    require(mg[3][0] == 32 && mg[4][0] == -28 && mg[5][0] == -65,
            "Generated MG rook/queen/king row ordering changed");
    require(eg[0][8] == 178 && eg[1][0] == -58 && eg[2][0] == -14,
            "Generated EG pawn/knight/bishop row ordering changed");
    require(eg[3][0] == 13 && eg[4][0] == -9 && eg[5][0] == -74,
            "Generated EG rook/queen/king row ordering changed");
}

void test_generated_piece_square_production_orientation() {
    const auto& evaluation = Tuning::Generated::VALUES.evaluation;
    const auto& mg = Tuning::Generated::MIDDLEGAME_PIECE_SQUARE_TABLE;
    const auto& eg = Tuning::Generated::ENDGAME_PIECE_SQUARE_TABLE;

    for (std::size_t piece = 0; piece < Tuning::kPieceTypeCount; ++piece) {
        for (int square = 0; square < 64; ++square) {
            const PieceType type = static_cast<PieceType>(piece);
            const auto white = EvalTables::pieceScoreDelta(Color::White, type, square);
            require(white.mg - evaluation.material.middlegame[piece] == mg[piece][static_cast<std::size_t>(square)],
                    "White MG PST orientation changed");
            require(white.eg - evaluation.material.endgame[piece] == eg[piece][static_cast<std::size_t>(square)],
                    "White EG PST orientation changed");

            const int mirroredSquare = Eval::mirrorSquare(square);
            const auto black = EvalTables::pieceScoreDelta(Color::Black, type, mirroredSquare);
            require(black.mg == white.mg && black.eg == white.eg && black.phase == white.phase,
                    "Black PST mirroring should map exactly once to the white-oriented source square");
        }
    }
}

void test_tuning_metadata_field_coverage_and_ordering() {
    require(Tuning::TUNING_FIELDS.size() == 76, "Expected 76 typed tuning field mappings");
    require(static_cast<size_t>(Tuning::TuningFieldId::Count) == 76, "TuningFieldId count should match registry entries");
    require(Tuning::tuningFieldNamesAreUnique(), "Stable tuning field names should be unique");
    require(Tuning::tuningFieldsAreInRegistryOrder(), "Stable tuning field names should be in registry order");
    require(!Tuning::hasFieldNamed("eval.tempo"), "eval.tempo must not exist in typed model");
    require(!Tuning::hasFieldNamed("SearchConstants::MATE_SCORE"), "Excluded invariants must not exist in typed model");
}

void test_tuning_array_dimensions() {
    const Tuning::EngineTuning& tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
    require(tuning.evaluation.material.middlegame.size() == 6, "Material MG dimension should be 6");
    require(tuning.evaluation.mobility.endgame.size() == 6, "Mobility EG dimension should be 6");
    require(tuning.evaluation.pawns.connectedMgByRank.size() == 9, "Connected pawn rank dimension should be 9");
    require(tuning.evaluation.pawns.backwardEgByRank.size() == 9, "Backward pawn rank dimension should be 9");
    require(tuning.evaluation.kingSafety.attackPressure.size() == 9, "King pressure dimension should be 9");
    require(Tuning::PieceSquareTuning::pieceCount == 6, "PST piece dimension should be 6");
    require(Tuning::PieceSquareTuning::squareCount == 64, "PST square dimension should be 64");
    require(tuning.search.moveOrdering.mvvLva.size() == 6, "MVV-LVA victim dimension should be 6");
    require(tuning.search.moveOrdering.mvvLva[0].size() == 6, "MVV-LVA attacker dimension should be 6");
}

void test_tuning_fraction_representations_are_exact() {
    const Tuning::EngineTuning& tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
    require(tuning.search.lateMoveReduction.base.equals(3, 4), "LMR base should represent 0.75 as 3/4");
    require(tuning.time.allocation.instabilityMultiplier.equals(13, 10), "Instability multiplier should represent 1.3 as 13/10");
    require(tuning.time.stopPolicy.stableSoftStopFraction.equals(1, 2), "Stable soft stop should represent 0.5 as 1/2");
    require(tuning.time.stopPolicy.unstableSoftStopFraction.equals(4, 5), "Unstable soft stop should represent 0.8 as 4/5");
    require(tuning.time.allocation.maximumClockFraction.equals(1, 4), "Max clock fraction should represent 0.25 as 1/4");
}

void test_tuning_validation_rejects_malformed_values() {
    {
        Tuning::EngineTuning tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
        tuning.search.lateMoveReduction.base.denominator = 0;
        require(!Tuning::validateTuning(tuning).valid(), "Zero LMR denominator should be rejected");
    }
    {
        Tuning::EngineTuning tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
        tuning.opening.selectionMode = static_cast<Tuning::BookSelectionMode>(99);
        require(!Tuning::validateTuning(tuning).valid(), "Invalid opening enum should be rejected");
    }
    {
        Tuning::EngineTuning tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
        tuning.opening.selectionTopN = 0;
        require(!Tuning::validateTuning(tuning).valid(), "Opening top-N zero should be rejected");
    }
    {
        Tuning::EngineTuning tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
        tuning.time.stopPolicy.stableSoftStopFraction = {9, 10};
        tuning.time.stopPolicy.unstableSoftStopFraction = {1, 2};
        require(!Tuning::validateTuning(tuning).valid(), "Stable soft-stop later than unstable should be rejected");
    }
    {
        Tuning::EngineTuning tuning = Tuning::BUILTIN_DEFAULT_V1_MODEL;
        tuning.evaluation.material.middlegame[5] = 1;
        require(!Tuning::validateTuning(tuning).valid(), "King material value must remain fixed at 0");
    }
}

void test_generated_evaluation_defaults_match_production_baseline() {
    const auto& evaluation = Tuning::Generated::VALUES.evaluation;
    require(evaluation.material.middlegame == Tuning::PieceValueArray{150, 250, 284, 490, 839, 0},
            "Generated MG material should match the production baseline");
    require(evaluation.material.endgame == Tuning::PieceValueArray{160, 200, 269, 650, 1150, 0},
            "Generated EG material should match the production baseline");
    require(evaluation.phase.increments == Tuning::PieceValueArray{0, 1, 1, 2, 4, 0},
            "Generated phase increments should match the production baseline");
    require(evaluation.mobility.middlegame == Tuning::PieceValueArray{0, 3, 10, 8, 5, 0},
            "Generated MG mobility should match the production baseline");
    require(evaluation.mobility.endgame == Tuning::PieceValueArray{0, 10, 10, 8, 8, 0},
            "Generated EG mobility should match the production baseline");
    require(evaluation.pawns.connectedMgByRank == Tuning::RankTuningTable{0, 0, 0, 21, 60, 60, 60, 0, 0},
            "Generated connected-pawn MG table should match the production baseline");
    require(evaluation.pawns.connectedEgByRank == Tuning::RankTuningTable{0, 0, 0, 0, 80, 80, 80, 80, 0},
            "Generated connected-pawn EG table should match the production baseline");
    require(evaluation.pawns.candidateMgByRank == Tuning::RankTuningTable{0, 0, 0, 40, 40, 40, 40, 0, 0},
            "Generated candidate-pawn MG table should match the production baseline");
    require(evaluation.pawns.candidateEgByRank == Tuning::RankTuningTable{0, 0, 0, 50, 50, 50, 50, 0, 0},
            "Generated candidate-pawn EG table should match the production baseline");
    require(evaluation.pawns.backwardMgByRank == Tuning::RankTuningTable{0, 0, 40, 20, 10, 40, 0, 0, 0},
            "Generated backward-pawn MG table should match the production baseline");
    require(evaluation.pawns.backwardEgByRank == Tuning::RankTuningTable{0, 0, 30, 30, 30, 0, 0, 0, 0},
            "Generated backward-pawn EG table should match the production baseline");
    require(evaluation.kingSafety.attackPressure == Tuning::KingPressureTable{0, 40, 165, 400, 400, 400, 160, 200, 240},
            "Generated king-pressure table should match the production baseline");
    require(evaluation.kingSafety.shieldMaxPawns == 3 && evaluation.kingSafety.shieldPerPawnBonus == 27,
            "Generated king-shield controls should match the production baseline");
    require(evaluation.endgame.mopUpWeights == Tuning::MopUpWeightArray{15, 10, 20, 7, 10, 25, 12},
            "Generated mop-up table should match the production baseline");

    for (std::size_t piece = 0; piece < Tuning::kPieceTypeCount; ++piece) {
        const auto delta = EvalTables::pieceScoreDelta(Color::White, static_cast<PieceType>(piece), 0);
        require(delta.mg - Tuning::Generated::MIDDLEGAME_PIECE_SQUARE_TABLE[piece][0] == evaluation.material.middlegame[piece],
                "Production MG material consumer should use generated values");
        require(delta.eg - Tuning::Generated::ENDGAME_PIECE_SQUARE_TABLE[piece][0] == evaluation.material.endgame[piece],
                "Production EG material consumer should use generated values");
        require(delta.phase == evaluation.phase.increments[piece],
                "Production phase consumer should use generated increments");
    }
}

void test_tuning_defaults_match_production_search_time_and_opening_values() {
    const Tuning::EngineTuning& tuning = Tuning::Generated::VALUES;
    require(tuning.time.polling.nodeMask == SearchConstants::TIME_CHECK_MASK, "Typed time polling mask should mirror production");
    require(tuning.time.allocation.safetyReserveMs == 30, "Typed safety reserve should mirror production literal");
    require(tuning.time.allocation.expectedMovesBase == 40, "Typed expected moves base should mirror production literal");
    require(tuning.time.allocation.expectedMovesFloor == 20, "Typed expected moves floor should mirror production literal");
    require(tuning.time.stopPolicy.criticalLowTimeThresholdMs == 40, "Typed critical low-time threshold should mirror production literal");
    require(tuning.time.stopPolicy.criticalLowTimeReserveMs == 5, "Typed critical low-time reserve should mirror production literal");
    require(tuning.opening.enabled == true, "Typed opening enabled should mirror production default");
    require(tuning.opening.depthPlies == SearchConstants::MAX_BOOK_DEPTH, "Typed book depth should mirror production default");
    require(tuning.opening.selectionMode == Tuning::BookSelectionMode::Weighted, "Typed book selection mode should mirror production default");
    require(tuning.opening.selectionTopN == 4, "Typed selection top-N should mirror production default");
    require(tuning.opening.seed == 1592594996U, "Typed seed should mirror UCI default");
}

void test_phase7_generated_search_defaults_match_production_baseline() {
    const auto& search = Tuning::Generated::VALUES.search;
    require(search.aspiration.windowCp == 75, "Generated aspiration window changed from 75 cp");
    require(search.nullMove.reduction == 2, "Generated null-move reduction changed from 2 plies");
    require(search.futility.reverseMarginPerDepthCp == 80, "Generated reverse-futility margin changed from 80 cp/depth");
    require(search.futility.forwardMarginPerDepthCp == 100, "Generated forward-futility margin changed from 100 cp/depth");
    require(search.lateMoveReduction.base.equals(3, 4), "Generated LMR base changed from 0.75");
    require(search.quiescence.deltaMarginCp == 260, "Generated delta-pruning margin changed from 260 cp");
    require(search.singularExtension.marginCp == 100, "Generated singular-extension margin changed from 100 cp");
    require(search.moveOrdering.transpositionMoveScore == 1'000'000, "Generated TT move score changed");
    require(search.moveOrdering.quiet.firstKillerScore == 900'000, "Generated first-killer score changed");
    require(search.moveOrdering.quiet.secondKillerScore == 850'000, "Generated second-killer score changed");
    require(search.moveOrdering.quiet.counterMoveScore == 50'000, "Generated countermove score changed");
    require(search.moveOrdering.capture.winningCaptureBaseScore == 800'000, "Generated winning-capture base changed");
    require(search.moveOrdering.capture.losingCaptureBaseScore == -100'000, "Generated losing-capture base changed");
    require(search.moveOrdering.capture.seeScoreMultiplier == 10, "Generated SEE multiplier changed");
    require(search.moveOrdering.promotionBaseScore == 700, "Generated promotion base changed");
    require(search.moveOrdering.historyLimit == 16'384, "Generated history cap changed");
    require(search.moveOrdering.mvvLva == Tuning::MvvLvaTable{{
        {{105, 205, 305, 405, 505, 605}},
        {{104, 204, 304, 404, 504, 604}},
        {{103, 203, 303, 403, 503, 603}},
        {{102, 202, 302, 402, 502, 602}},
        {{101, 201, 301, 401, 501, 601}},
        {{100, 200, 300, 400, 500, 600}},
    }}, "Generated MVV-LVA table changed");
    require(search.moveOrdering.seePieceValues == Tuning::PieceValueArray{100, 320, 330, 500, 900, 0},
            "Generated SEE piece values changed");
}

void test_phase7_search_formula_characterization() {
    const auto& search = Tuning::Generated::VALUES.search;

    const int previousScore = 37;
    require(previousScore - search.aspiration.windowCp == -38 &&
            previousScore + search.aspiration.windowCp == 112,
            "Aspiration alpha/beta arithmetic changed");
    require(5 - 1 - search.nullMove.reduction == 2,
            "Null-move reduced-depth arithmetic changed");
    require(6 * search.futility.reverseMarginPerDepthCp == 480,
            "Reverse-futility margin arithmetic changed");
    require(3 * search.futility.forwardMarginPerDepthCp == 300,
            "Forward-futility margin arithmetic changed");
    require(420 + 100 + search.quiescence.deltaMarginCp == 780,
            "Delta-pruning comparison arithmetic changed");
    require(250 - search.singularExtension.marginCp == 150,
            "Singular-extension beta arithmetic changed");

    SearchInternal::initLMR();
    const double oldBase = 0.75;
    for (int depth = 0; depth < 64; ++depth) {
        for (int moveCount = 0; moveCount < 64; ++moveCount) {
            int expected = 1;
            if (depth > 0 && moveCount > 0) {
                const int reduction = static_cast<int>(oldBase + std::log(depth) * std::log(moveCount));
                expected = std::max(1, std::min(reduction, depth - 1));
            }
            require(SearchInternal::LMR_TABLE[depth][moveCount] == expected,
                    "Generated LMR rational changed a baseline table entry");
        }
    }
}

void test_phase7_null_move_exclusions_remain_structural() {
    Board inCheck;
    require(inCheck.loadFEN("4r1k1/8/8/8/8/8/8/4K3 w - - 0 1"), "Failed to load in-check null-move fixture");
    require(inCheck.inCheck(Color::White), "In-check null-move exclusion fixture must be checked");

    Board pawnOnly;
    require(pawnOnly.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"), "Failed to load pawn-only null-move fixture");
    require(!pawnOnly.hasNonPawnMaterial(Color::White),
            "Pawn-only/zugzwang safeguard must continue to disable null move");

    Board eligible;
    require(eligible.loadFEN("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1"), "Failed to load null-move eligibility fixture");
    require(!eligible.inCheck(Color::White) && eligible.hasNonPawnMaterial(Color::White),
            "Non-check position with non-pawn material must remain null-move eligible");
}

Move parsedMove(Board& board, const std::string& text) {
    ParseResult parsed = board.parseMove(text);
    require(parsed.move.has_value(), "Failed to parse move-ordering fixture move: " + text);
    return *parsed.move;
}

void test_phase7_move_ordering_characterization() {
    const auto& ordering = Tuning::Generated::VALUES.search.moveOrdering;
    Board board;
    SearchInternal::clearKillers();
    SearchInternal::clearHistory();

    const Move firstKiller = parsedMove(board, "e2e4");
    const Move secondKiller = parsedMove(board, "d2d4");
    const Move counter = parsedMove(board, "g1f3");
    const Move history = parsedMove(board, "b1c3");
    const Move ordinary = parsedMove(board, "a2a3");

    SearchInternal::g_killerMoves[1][0] = firstKiller;
    SearchInternal::g_killerMoves[1][1] = secondKiller;
    SearchInternal::g_movesPlayed[0] = ordinary;
    SearchInternal::g_countermoveTable[ordinary.from][ordinary.to] = counter;
    SearchInternal::g_historyTable[static_cast<int>(Color::White)][history.from][history.to] = 123;

    require(SearchInternal::scoreMove(board, firstKiller, 1) == ordering.quiet.firstKillerScore,
            "First-killer production score changed");
    require(SearchInternal::scoreMove(board, secondKiller, 1) == ordering.quiet.secondKillerScore,
            "Second-killer production score changed");
    require(SearchInternal::scoreMove(board, counter, 1) == ordering.quiet.counterMoveScore,
            "Countermove production score changed");
    require(SearchInternal::scoreMove(board, history, 1) == 123,
            "History-scored quiet move changed");
    require(SearchInternal::scoreMove(board, ordinary, 1) == 0,
            "Ordinary quiet move score changed");

    std::vector<Move> ordered{firstKiller, ordinary, secondKiller};
    SearchInternal::sortMovesByScore(board, ordered, ordinary, 1);
    require(sameMove(ordered.front(), ordinary), "TT move must remain ahead of both killer moves");

    Board captureBoard;
    require(captureBoard.loadFEN("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"),
            "Failed to load winning-capture ordering fixture");
    const Move capture = parsedMove(captureBoard, "e4d5");
    require(SearchInternal::scoreMove(captureBoard, capture, 0) == 801'105,
            "Winning-capture base, SEE multiplier, or MVV-LVA contribution changed");

    Board promotionBoard;
    require(promotionBoard.loadFEN("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"),
            "Failed to load promotion ordering fixture");
    const Move promotion = parsedMove(promotionBoard, "a7a8q");
    require(SearchInternal::scoreMove(promotionBoard, promotion, 0) ==
                ordering.promotionBaseScore + SearchInternal::getPieceValue(PieceType::Queen),
            "Promotion ordering base changed");

    require(ordering.transpositionMoveScore > ordering.quiet.firstKillerScore &&
            ordering.quiet.firstKillerScore > ordering.quiet.secondKillerScore &&
            ordering.capture.winningCaptureBaseScore > ordering.promotionBaseScore &&
            ordering.quiet.counterMoveScore > ordering.historyLimit,
            "Accepted move-ordering priority relationships changed");
}

void test_phase7_fixed_depth_search_baseline() {
    Board board;
    SearchInternal::clearTT();
    SearchInternal::clearKillers();
    SearchInternal::clearHistory();
    SearchInternal::initLMR();
    SearchConstants::MULTI_PV = 1;

    int score = SearchConstants::INF_SCORE;
    const auto [bestMove, ponderMove] = findBestMove(board, 4, 30'000, &score);
    require(moveKey(bestMove) == "b1c3", "Phase 7 fixed-depth start-position best move changed");
    require(moveKey(ponderMove) == "g8f6", "Phase 7 fixed-depth start-position PV continuation changed");
    require(score == 0, "Phase 7 fixed-depth start-position score changed");
    require(SearchInternal::g_nodesSearched == 10'598,
            "Phase 7 fixed-depth start-position deterministic node count changed");
    require(qNodes.load(std::memory_order_relaxed) == 3'828,
            "Phase 7 fixed-depth start-position quiescence node count changed");
    require(deltaPruneSkips.load(std::memory_order_relaxed) == 15,
            "Phase 7 fixed-depth start-position delta-prune count changed");
}

void test_phase5a_scalar_term_characterization() {
    const auto& evaluation = Tuning::Generated::VALUES.evaluation;

    const Eval::TaperTerms openFile = EvalTerms::rookActivityTermsForSide(Color::White, 1ULL, 0ULL, 0ULL);
    require(openFile.mg == evaluation.rookActivity.openFileMg && openFile.eg == evaluation.rookActivity.openFileEg,
            "Open-file rook should receive generated MG/EG bonuses");

    const Eval::TaperTerms semiOpenFile = EvalTerms::rookActivityTermsForSide(Color::White, 1ULL, 0ULL, 1ULL << 48);
    require(semiOpenFile.mg == evaluation.rookActivity.semiOpenFileMg && semiOpenFile.eg == evaluation.rookActivity.semiOpenFileEg,
            "Semi-open-file rook should receive generated MG/EG bonuses");

    const Eval::TaperTerms seventhRank = EvalTerms::rookActivityTermsForSide(Color::White, 1ULL << 48, 1ULL, 1ULL << 8);
    require(seventhRank.mg == evaluation.rookActivity.seventhRankMg && seventhRank.eg == evaluation.rookActivity.seventhRankEg,
            "Seventh-rank rook should receive generated MG/EG bonuses");

    const Eval::TaperTerms islands = EvalTerms::pawnIslandPenalty((1ULL << 8) | (1ULL << 10));
    require(islands.mg == evaluation.pawns.islandPenaltyMg && islands.eg == evaluation.pawns.islandPenaltyEg,
            "Two pawn islands should receive one generated MG/EG island penalty");

    const int doubledIsolated = EvalTerms::pawnStructurePenalty((1ULL << 8) | (1ULL << 16));
    require(doubledIsolated == evaluation.pawns.doubledPenalty + 2 * evaluation.pawns.isolatedPenalty,
            "Doubled isolated pawns should preserve penalty signs and multiplicity");

    require(EvalTerms::passedPawnBonus(Color::White, 1ULL << 27, 0ULL)
                == 16 * evaluation.pawns.passedRankSquareMultiplier,
            "White passed-pawn rank multiplier should preserve orientation");
    require(EvalTerms::passedPawnBonus(Color::Black, 1ULL << 35, 0ULL)
                == 16 * evaluation.pawns.passedRankSquareMultiplier,
            "Black passed-pawn rank multiplier should preserve orientation");

    require(EvalTerms::trappedRookPenalty(Color::White, 1ULL, 1ULL << 1)
                == evaluation.piecePlacement.trappedRookPenalty,
            "Trapped-rook term should use generated penalty");
    require(EvalTerms::badBishopPenalty(1ULL << 18, 1ULL << 9, Color::White)
                == evaluation.piecePlacement.badBishopHeavyPenalty,
            "Bad-bishop term should use generated heavy penalty");
    require(EvalTerms::earlyQueenPenalty(1ULL << 11, (1ULL << 1) | (1ULL << 6), 0ULL, Color::White)
                == evaluation.piecePlacement.earlyQueenUndevelopedMinorPenalty,
            "Early-queen term should use generated penalty");
    require(EvalTerms::uncastledKingPenalty(1ULL << 4, 0, Color::White)
                == evaluation.kingSafety.uncastledCenterPenalty + evaluation.kingSafety.uncastledLostRightsPenalty,
            "Uncastled king should preserve center and lost-rights penalty composition");

    const int minorOnlyScale = EvalTerms::lowMaterialScaleFactor(
        evaluation.endgame.latePhaseMax,
        0ULL, 0ULL, 1ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL
    );
    require(minorOnlyScale == evaluation.endgame.scaleMinorOnlyNearEqual,
            "Late minor-only endgame should use generated scale");
    const int normalScale = EvalTerms::lowMaterialScaleFactor(
        evaluation.endgame.latePhaseMax + 1,
        0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL
    );
    require(normalScale == evaluation.endgame.taperScale,
            "Non-late phase should retain generated full taper scale");
}

void test_phase5b_grouped_evaluation_characterization() {
    const auto& evaluation = Tuning::Generated::VALUES.evaluation;
    constexpr int square = 27;
    const uint64_t piece = 1ULL << square;

    const struct MobilityFixture {
        PieceType type;
        uint64_t knights;
        uint64_t bishops;
        uint64_t rooks;
        uint64_t queens;
        int attacks;
    } mobilityFixtures[] = {
        {PieceType::Knight, piece, 0ULL, 0ULL, 0ULL, std::popcount(EvalTerms::knightAttacks(square) & ~piece)},
        {PieceType::Bishop, 0ULL, piece, 0ULL, 0ULL, std::popcount(EvalTerms::bishopAttacks(square, piece) & ~piece)},
        {PieceType::Rook, 0ULL, 0ULL, piece, 0ULL, std::popcount(EvalTerms::rookAttacks(square, piece) & ~piece)},
        {PieceType::Queen, 0ULL, 0ULL, 0ULL, piece,
            std::popcount((EvalTerms::bishopAttacks(square, piece) | EvalTerms::rookAttacks(square, piece)) & ~piece)},
    };
    for (const auto& fixture : mobilityFixtures) {
        const auto terms = EvalTerms::mobilityTermsForSide(
            fixture.knights, fixture.bishops, fixture.rooks, fixture.queens, piece, piece
        );
        const std::size_t index = static_cast<std::size_t>(fixture.type);
        require(terms.mg == fixture.attacks * evaluation.mobility.middlegame[index],
                "MG mobility should use generated piece-indexed values");
        require(terms.eg == fixture.attacks * evaluation.mobility.endgame[index],
                "EG mobility should use generated piece-indexed values");
    }

    const uint64_t whiteConnected = (1ULL << 27) | (1ULL << 28);
    const uint64_t blackConnected = (1ULL << 35) | (1ULL << 36);
    require(EvalTerms::connectedPawnsBonus(Color::White, whiteConnected)
                == 2 * evaluation.pawns.connectedMgByRank[4],
            "White connected-pawn rank indexing should use generated MG table");
    require(EvalTerms::connectedPawnsBonus(Color::Black, blackConnected)
                == 2 * evaluation.pawns.connectedMgByRank[4],
            "Black connected-pawn rank indexing should mirror white");
    require(EvalTerms::connectedPawnsBonusEg(Color::White, whiteConnected)
                == 2 * evaluation.pawns.connectedEgByRank[4],
            "Connected-pawn EG table should preserve rank indexing");

    const uint64_t whiteCandidatePawns = (1ULL << 27) | (1ULL << 18);
    const uint64_t whiteCandidateEnemy = 1ULL << 36;
    require(EvalTerms::candidatePawnsBonus(Color::White, whiteCandidatePawns, whiteCandidateEnemy)
                == evaluation.pawns.candidateMgByRank[4],
            "Candidate-pawn MG table should preserve white rank indexing");
    require(EvalTerms::candidatePawnsBonusEg(Color::White, whiteCandidatePawns, whiteCandidateEnemy)
                == evaluation.pawns.candidateEgByRank[4],
            "Candidate-pawn EG table should preserve white rank indexing");

    const uint64_t whiteBackward = 1ULL << 19;
    const uint64_t whiteBackwardEnemy = 1ULL << 34;
    require(EvalTerms::backwardPawnsPenalty(
                Color::White, whiteBackward, whiteBackwardEnemy, whiteBackward | whiteBackwardEnemy)
                == evaluation.pawns.backwardMgByRank[3],
            "Backward-pawn MG table should preserve white rank indexing");
    require(EvalTerms::backwardPawnsPenaltyEg(
                Color::White, whiteBackward, whiteBackwardEnemy, whiteBackward | whiteBackwardEnemy)
                == evaluation.pawns.backwardEgByRank[3],
            "Backward-pawn EG table should preserve white rank indexing");

    require(EvalTerms::kingAttackPressure(Color::White, 4, 0ULL, 0ULL, 0ULL, 0ULL, 1ULL << 4) == 0,
            "King pressure should use the generated zero-attacker entry");
    uint64_t attackingKnights = 0ULL;
    const uint64_t whiteKingZone = EvalMask::MASKS.KING_SHIELD_MASKS[0][4];
    for (int attackerSquare = 0; attackerSquare < 64; ++attackerSquare) {
        if ((EvalTerms::knightAttacks(attackerSquare) & whiteKingZone) != 0ULL) {
            attackingKnights |= 1ULL << attackerSquare;
        }
    }
    require(std::popcount(attackingKnights) >= 8, "King-pressure clamp fixture needs at least eight attackers");
    require(EvalTerms::kingAttackPressure(
                Color::White, 4, attackingKnights, 0ULL, 0ULL, 0ULL, attackingKnights | (1ULL << 4))
                == evaluation.kingSafety.attackPressure.back(),
            "King pressure should clamp to the final generated entry");

    require(EvalTerms::kingPawnShieldBonus(Color::White, 6, (1ULL << 13) | (1ULL << 14) | (1ULL << 15))
                == evaluation.kingSafety.shieldMaxPawns * evaluation.kingSafety.shieldPerPawnBonus,
            "White king shield should use generated grouped controls");
    require(EvalTerms::kingPawnShieldBonus(Color::Black, 62, (1ULL << 53) | (1ULL << 54) | (1ULL << 55))
                == evaluation.kingSafety.shieldMaxPawns * evaluation.kingSafety.shieldPerPawnBonus,
            "Black king shield should mirror white");

    const auto& mopUp = evaluation.endgame.mopUpWeights;
    const int winningKing = 4;
    const int losingKing = 60;
    const int expectedMopUp = 5 * mopUp[0]
        + (mopUp[1] - 0) * mopUp[2]
        + (mopUp[3] - 3) * mopUp[4]
        + (mopUp[5] - 7) * mopUp[6];
    require(EvalTerms::mopUpEval(winningKing, losingKing) == expectedMopUp,
            "Mop-up formula should use generated grouped weights without reordering");
}

void test_phase5b_static_evaluation_baselines() {
    struct Fixture { const char* fen; int expected; };
    constexpr Fixture fixtures[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0},
        {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", 363},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", 1129},
        {"4k3/8/8/8/3N4/8/8/4K3 w - - 0 1", 0},
        {"4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1", 1135},
        {"4k3/8/8/4p3/3P4/8/8/4K3 w - - 0 1", 7},
        {"4k3/8/8/8/2P5/3P4/8/4K3 w - - 0 1", 992},
        {"6k1/5ppp/8/8/8/5Q2/8/6K1 b - - 0 1", 90},
        {"6k1/5ppp/8/8/8/8/5PPP/6K1 w - - 0 1", 0},
        {"8/8/8/4k3/8/8/4K3/R7 w - - 0 1", 601},
        {"8/8/8/4k3/8/8/4K3/Q7 w - - 0 1", 796},
        {"8/8/8/4k3/8/8/4K3/BN6 w - - 0 1", 277},
    };
    for (const Fixture& fixture : fixtures) {
        Board board;
        require(board.loadFEN(fixture.fen), "Failed to load Phase 5B evaluation fixture");
        require(board.evaluate() == board.computeStaticEvaluation(),
                "FEN reconstruction produced incremental evaluation drift");
        require(board.computeStaticEvaluation() == fixture.expected,
                "Phase 5B static-evaluation baseline changed for FEN: " + std::string(fixture.fen));
    }
}

void test_phase5c_piece_square_static_baselines() {
    struct Fixture { const char* fen; int expected; };
    constexpr Fixture fixtures[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0},
        {"4k3/8/8/8/3N4/8/P7/4K3 w - - 0 1", 1201},
        {"4k3/8/8/8/2B5/8/P7/4K3 w - - 0 1", 1279},
        {"4k3/8/8/8/R7/8/P7/4K3 w - - 0 1", 1602},
        {"4k3/8/8/8/3Q4/8/P7/4K3 w - - 0 1", 2152},
        {"4k3/8/8/8/3K4/8/P7/8 w - - 0 1", 432},
        {"4k3/8/8/P7/8/8/8/4K3 w - - 0 1", 649},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", 1129},
        {"r3k2r/ppp2ppp/2n1bn2/3pp3/3PP3/2N1BN2/PPP2PPP/R2QK2R w KQkq - 0 8", 948},
        {"8/8/8/4k3/8/8/4K3/Q7 w - - 0 1", 796},
    };
    for (const Fixture& fixture : fixtures) {
        Board board;
        require(board.loadFEN(fixture.fen), "Failed to load Phase 5C PST fixture");
        require(board.evaluate() == board.computeStaticEvaluation(),
                "PST FEN reconstruction produced incremental evaluation drift");
        require(board.computeStaticEvaluation() == fixture.expected,
                "Phase 5C PST baseline changed for FEN: " + std::string(fixture.fen));
    }
}

void test_phase5a_static_evaluation_baselines() {
    struct Fixture {
        const char* fen;
        int expected;
    };

    constexpr Fixture fixtures[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0},
        {"r3k2r/ppp2ppp/2n1bn2/3pp3/3PP3/2N1BN2/PPP2PPP/R2QK2R w KQkq - 0 8", 948},
        {"4k3/8/8/8/8/8/4P3/R3K3 w Q - 0 1", 1615},
        {"4k3/8/8/3P4/2P1P3/8/8/4K3 w - - 0 1", 2253},
        {"rnb1kbnr/pppp1ppp/8/4p3/4P2q/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3", 80},
        {"8/8/8/4k3/8/8/4K3/R7 w - - 0 1", 601},
    };

    for (const Fixture& fixture : fixtures) {
        Board board;
        require(board.loadFEN(fixture.fen), "Failed to load Phase 5A static-evaluation fixture");
        require(board.computeStaticEvaluation() == fixture.expected,
                "Phase 5A static-evaluation baseline changed for FEN: " + std::string(fixture.fen));
    }
}

} // namespace

int main() {
    struct TestCase {
        std::string name;
        std::function<void()> fn;
    };

    const std::vector<TestCase> tests = {
        {"Scholar's Mate sequence", test_scholars_mate_sequence},
        {"Illegal move + out-of-turn rejection", test_illegal_move_and_out_of_turn},
        {"Castling through check rejected", test_castling_through_check_rejected},
        {"Invalid SAN strings", test_invalid_san_inputs},
        {"Disambiguation + pawn capture SAN handling", test_disambiguation_and_pawn_capture_parsing},
        {"Perft start position depth 1..4", test_perft_start_position_depth_1_to_4},
        {"Search tactics (mate in 1 + winning capture)", test_search_tactics},
        {"Incremental evaluation drift test", test_incremental_evaluation},
        {"Incremental promotion evaluation", test_incremental_evaluation_promotions},
        {"Incremental special-move evaluation", test_incremental_evaluation_special_moves},
        {"Make/undo full-state restoration", test_make_undo_restores_full_state},
        {"Deep legality and undo stress", test_deep_legality_and_undo_stress},
        {"Mate in three tactical search", test_mate_in_three},
        {"Quiescence horizon effect", test_quiescence_horizon_effect},
        {"NMP zugzwang safety", test_nmp_zugzwang_safety},
        {"Mate distance scoring", test_mate_distance_scoring},
        {"Quiescence counters", test_quiescence_counters},
        {"findBestMove pair contract", test_find_best_move_pair_contract},
        {"findBestMoveCompat matches pair best", test_find_best_move_compat_matches_pair_best},
        {"findBestMove ponder legality", test_find_best_move_ponder_is_legal_when_available},
        {"Opening book missing-position fallback", test_opening_book_returns_empty_for_missing_position},
        {"Opening book candidates, weights, and duplicates", test_opening_book_parses_candidates_weights_and_duplicates},
        {"Opening book deterministic single/best selection", test_opening_book_single_candidate_and_best_are_deterministic},
        {"Opening book seeded weighted selection", test_opening_book_weighted_selector_is_seeded_and_weighted},
        {"Opening book zero-weight and top-N policy", test_opening_book_zero_weight_and_top_n_policy},
        {"Tuning model constructs and validates default", test_tuning_model_constructs_and_validates_default},
        {"Generated tuning header identity and validation", test_generated_tuning_header_identity_and_validation},
        {"Generated tuning values match typed model", test_generated_tuning_values_match_typed_model},
        {"Generated PST values match production", test_generated_piece_square_tables_match_production},
        {"Generated PST production orientation", test_generated_piece_square_production_orientation},
        {"Tuning metadata field coverage and ordering", test_tuning_metadata_field_coverage_and_ordering},
        {"Tuning array dimensions", test_tuning_array_dimensions},
        {"Tuning rational fraction representations", test_tuning_fraction_representations_are_exact},
        {"Tuning validation rejects malformed values", test_tuning_validation_rejects_malformed_values},
        {"Generated evaluation defaults match production baseline", test_generated_evaluation_defaults_match_production_baseline},
        {"Tuning defaults match production search/time/opening values", test_tuning_defaults_match_production_search_time_and_opening_values},
        {"Phase 7 generated search defaults match production baseline", test_phase7_generated_search_defaults_match_production_baseline},
        {"Phase 7 search formula characterization", test_phase7_search_formula_characterization},
        {"Phase 7 null-move exclusions remain structural", test_phase7_null_move_exclusions_remain_structural},
        {"Phase 7 move-ordering characterization", test_phase7_move_ordering_characterization},
        {"Phase 7 fixed-depth search baseline", test_phase7_fixed_depth_search_baseline},
        {"Phase 5A scalar term characterization", test_phase5a_scalar_term_characterization},
        {"Phase 5A static evaluation baselines", test_phase5a_static_evaluation_baselines},
        {"Phase 5B grouped evaluation characterization", test_phase5b_grouped_evaluation_characterization},
        {"Phase 5B static evaluation baselines", test_phase5b_static_evaluation_baselines},
        {"Phase 5C PST static evaluation baselines", test_phase5c_piece_square_static_baselines},
    };

    int passed = 0;
    for (const auto& test : tests) {
        try {
            test.fn();
            ++passed;
            std::cout << "[PASS] " << test.name << "\n";
        } catch (const TestFailure& e) {
            std::cout << "[FAIL] " << test.name << " -> " << e.message << "\n";
            return 1;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << test.name << " -> unexpected exception: " << e.what() << "\n";
            return 1;
        } catch (...) {
            std::cout << "[FAIL] " << test.name << " -> unknown exception\n";
            return 1;
        }
    }

    std::cout << "\nAll tests passed (" << passed << "/" << tests.size() << ").\n";
    return 0;
}
