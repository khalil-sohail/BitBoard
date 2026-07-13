#ifndef EVAL_TABLES_HPP
#define EVAL_TABLES_HPP

#include "board.hpp"
#include "eval/eval_types.hpp"

namespace EvalTables {

[[nodiscard]] constexpr int pestoSquare(Color color, int square) noexcept {
    return (color == Color::White) ? square : Eval::mirrorSquare(square);
}

[[nodiscard]] Eval::PieceScoreDelta pieceScoreDelta(Color color, PieceType piece, int square);

} // namespace EvalTables

#endif
