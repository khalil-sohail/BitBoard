#include "board.hpp"
#include "eval/eval_tables.hpp"

#include <cstdint>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);

} // namespace

void Board::addPieceEval(Color color, PieceType piece, int square) {
    const int c = static_cast<int>(color);
    const Eval::PieceScoreDelta delta = EvalTables::pieceScoreDelta(color, piece, square);

    m_mgScore[c] += delta.mg;
    m_egScore[c] += delta.eg;
    m_gamePhase += delta.phase;
}

void Board::removePieceEval(Color color, PieceType piece, int square) {
    const int c = static_cast<int>(color);
    const Eval::PieceScoreDelta delta = EvalTables::pieceScoreDelta(color, piece, square);

    m_mgScore[c] -= delta.mg;
    m_egScore[c] -= delta.eg;
    m_gamePhase -= delta.phase;
}

void Board::resetEvalStateFromBoard() {
    m_mgScore = {0, 0};
    m_egScore = {0, 0};
    m_gamePhase = 0;

    for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
        for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
            uint64_t bb = m_bitboards[color][piece];
            while (bb != 0ULL) {
                const int square = Eval::lsbIndex(bb);
                bb &= (bb - 1);
                addPieceEval(static_cast<Color>(color), static_cast<PieceType>(piece), square);
            }
        }
    }
}
