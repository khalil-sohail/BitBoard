#include "board.hpp"
#include "search.hpp"

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

        const Move best = findBestMove(board, 3);
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

        const Move best = findBestMove(board, 3);
        const int expectedFrom = Board::squareFromString("f3");
        const int expectedTo = Board::squareFromString("h4");

        require(best.from == expectedFrom && best.to == expectedTo,
                "Winning-capture test failed: expected f3h4, got " +
                Board::squareToString(best.from) + Board::squareToString(best.to));
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
