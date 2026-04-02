#ifndef MOVE_MOVE_CONSTANTS_HPP
#define MOVE_MOVE_CONSTANTS_HPP

#include "board.hpp"

#include <cstdint>

namespace MoveConstants {

inline constexpr int WHITE_IDX = static_cast<int>(Color::White);
inline constexpr int BLACK_IDX = static_cast<int>(Color::Black);
inline constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
inline constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);

inline constexpr uint8_t CASTLE_WK = 0b1000;
inline constexpr uint8_t CASTLE_WQ = 0b0100;
inline constexpr uint8_t CASTLE_BK = 0b0010;
inline constexpr uint8_t CASTLE_BQ = 0b0001;

} // namespace MoveConstants

#endif