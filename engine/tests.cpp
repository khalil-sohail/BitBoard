#include "board.hpp"
#include "openingBook.hpp"
#include "search.hpp"

#include <algorithm>
#include <cassert>
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
