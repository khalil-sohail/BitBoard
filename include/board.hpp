#ifndef BOARD_HPP
#define BOARD_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class Color : int {
    White = 0,
    Black = 1
};

enum class PieceType : int {
    Pawn = 0,
    Knight = 1,
    Bishop = 2,
    Rook = 3,
    Queen = 4,
    King = 5,
    Count = 6
};

struct Move {
    int from = -1;
    int to = -1;
    PieceType piece = PieceType::Pawn;
    std::optional<PieceType> promotion;
    bool isCapture = false;
    bool isDoublePush = false;
    bool isKingSideCastle = false;
    bool isQueenSideCastle = false;
    bool isEnPassant = false;
};

struct ParseResult {
    std::optional<Move> move;
    std::string error;
};

class Board {
private:
    std::array<std::array<uint64_t, static_cast<size_t>(PieceType::Count)>, 2> m_bitboards{};
    Color m_sideToMove = Color::White;
    uint8_t m_castlingRights = 0b1111; // WK WQ BK BQ
    int m_enPassantSquare = -1;
    int m_halfmoveClock = 0;
    int m_fullmoveNumber = 1;
    std::vector<std::string> m_sanHistory;

public:
    static constexpr uint64_t FILE_A = 0x0101010101010101ULL;
    static constexpr uint64_t FILE_H = 0x8080808080808080ULL;
    static constexpr uint64_t RANK_1 = 0x00000000000000FFULL;
    static constexpr uint64_t RANK_2 = 0x000000000000FF00ULL;
    static constexpr uint64_t RANK_4 = 0x00000000FF000000ULL;
    static constexpr uint64_t RANK_5 = 0x000000FF00000000ULL;
    static constexpr uint64_t RANK_7 = 0x00FF000000000000ULL;
    static constexpr uint64_t RANK_8 = 0xFF00000000000000ULL;

    Board();
    void reset();

    [[nodiscard]] uint64_t occupancy(Color color) const;
    [[nodiscard]] uint64_t occupancyAll() const;
    [[nodiscard]] bool isSquareOccupied(int square) const;
    [[nodiscard]] bool hasPiece(Color color, PieceType piece, int square) const;
    [[nodiscard]] std::optional<std::pair<Color, PieceType>> pieceAt(int square) const;

    [[nodiscard]] std::vector<Move> generatePseudoLegalMoves() const;
    [[nodiscard]] std::vector<Move> generateLegalMoves() const;
    [[nodiscard]] uint64_t perft(int depth) const;
    [[nodiscard]] int evaluate() const;
    [[nodiscard]] bool isSquareAttacked(int square, Color byColor) const;
    [[nodiscard]] bool inCheck(Color color) const;

    bool applyMove(const Move& move);
    void makeMove(const Move& move);

    [[nodiscard]] ParseResult parseMove(const std::string& input) const;
    [[nodiscard]] static int squareFromString(const std::string& coord);
    [[nodiscard]] static std::string squareToString(int square);

    [[nodiscard]] Color sideToMove() const;
    [[nodiscard]] char pieceToChar(Color color, PieceType piece) const;

    void recordSanMove(const std::string& sanMove) { m_sanHistory.push_back(sanMove); }
    void clearSanHistory() { m_sanHistory.clear(); }
    [[nodiscard]] const std::vector<std::string>& sanHistory() const { return m_sanHistory; }

    void printBoard(Color perspective = Color::White) const;
    void printMoves(const std::vector<Move>& moves) const;
};

#endif