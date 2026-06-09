#include "board.hpp"
#include "eval/eval_tables.hpp"

#include <array>
#include <cstdint>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);

} // namespace

int Board::evaluate() const {
    bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
    bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);

    int mg = m_mgScore[WHITE_IDX] - m_mgScore[BLACK_IDX];
    int eg = m_egScore[WHITE_IDX] - m_egScore[BLACK_IDX];

    return applyBonusTermsAndTaper(mg, eg, m_gamePhase, noWhitePawns, noBlackPawns);
}

int Board::evaluateSideToMove() const {
    const int eval = evaluate();
    return (m_sideToMove == Color::White) ? eval : -eval;
}

int Board::computeStaticEvaluation() const {
    bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
    bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);

    std::array<int, 2> mgScore = {0, 0};
    std::array<int, 2> egScore = {0, 0};
    int phase = 0;

    for (int color = WHITE_IDX; color <= BLACK_IDX; ++color) {
        for (int piece = 0; piece < static_cast<int>(PieceType::Count); ++piece) {
            uint64_t bb = m_bitboards[color][piece];
            while (bb != 0ULL) {
                const int square = Eval::lsbIndex(bb);
                bb &= (bb - 1);

                const Eval::PieceScoreDelta delta = EvalTables::pieceScoreDelta(
                    static_cast<Color>(color),
                    static_cast<PieceType>(piece),
                    square
                );
                mgScore[color] += delta.mg;
                egScore[color] += delta.eg;
                phase += delta.phase;
            }
        }
    }

    int mg = mgScore[WHITE_IDX] - mgScore[BLACK_IDX];
    int eg = egScore[WHITE_IDX] - egScore[BLACK_IDX];

    return applyBonusTermsAndTaper(mg, eg, phase, noWhitePawns, noBlackPawns);
}
