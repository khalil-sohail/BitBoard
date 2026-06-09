#ifndef EVAL_TABLES_HPP
#define EVAL_TABLES_HPP

#include "board.hpp"
#include "eval/eval_types.hpp"

#include <array>
#include <cstddef>

namespace EvalTables {

extern const std::array<std::array<int, 64>, static_cast<size_t>(PieceType::Count)> MG_PESTO;
extern const std::array<std::array<int, 64>, static_cast<size_t>(PieceType::Count)> EG_PESTO;

[[nodiscard]] constexpr int pestoSquare(Color color, int square) noexcept {
    return (color == Color::White) ? square : Eval::mirrorSquare(square);
}

[[nodiscard]] Eval::PieceScoreDelta pieceScoreDelta(Color color, PieceType piece, int square);

} // namespace EvalTables

#endif
