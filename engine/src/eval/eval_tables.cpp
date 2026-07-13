#include "eval/eval_tables.hpp"
#include "tuning/generated_tuning_values.hpp"

#include <cstddef>

namespace {

constexpr const auto& EVAL_TUNING = Tuning::Generated::VALUES.evaluation;
constexpr const auto& MG_PST = Tuning::Generated::MIDDLEGAME_PIECE_SQUARE_TABLE;
constexpr const auto& EG_PST = Tuning::Generated::ENDGAME_PIECE_SQUARE_TABLE;

static_assert(MG_PST.size() == Tuning::kPieceTypeCount);
static_assert(EG_PST.size() == Tuning::kPieceTypeCount);
static_assert(MG_PST[0].size() == Tuning::kBoardSquareCount);
static_assert(EG_PST[0].size() == Tuning::kBoardSquareCount);

} // namespace

namespace EvalTables {

Eval::PieceScoreDelta pieceScoreDelta(Color color, PieceType piece, int square) {
    const std::size_t pieceIdx = static_cast<std::size_t>(piece);
    const std::size_t pstSquare = static_cast<std::size_t>(pestoSquare(color, square));

    return {
        EVAL_TUNING.material.middlegame[pieceIdx] + MG_PST[pieceIdx][pstSquare],
        EVAL_TUNING.material.endgame[pieceIdx] + EG_PST[pieceIdx][pstSquare],
        EVAL_TUNING.phase.increments[pieceIdx]
    };
}

} // namespace EvalTables
