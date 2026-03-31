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
    struct UndoState {
        std::array<std::array<uint64_t, static_cast<size_t>(PieceType::Count)>, 2> bitboards{};
        Color sideToMove = Color::White;
        uint8_t castlingRights = 0;
        int enPassantSquare = -1;
        std::array<int, 2> mgScore{};
        std::array<int, 2> egScore{};
        int gamePhase = 0;
    };

    std::array<std::array<uint64_t, static_cast<size_t>(PieceType::Count)>, 2> m_bitboards{};
    Color m_sideToMove = Color::White;
    uint8_t m_castlingRights = 0b1111; // WK WQ BK BQ
    int m_enPassantSquare = -1;
    std::array<int, 2> m_mgScore{};
    std::array<int, 2> m_egScore{};
    int m_gamePhase = 0;
    uint64_t m_hash = 0ULL;
    std::vector<uint64_t> m_hashHistory;
    std::vector<std::string> m_sanHistory;
    std::vector<UndoState> m_undoStack;

    void resetEvalStateFromBoard();
    void addPieceEval(Color color, PieceType piece, int square);
    void removePieceEval(Color color, PieceType piece, int square);
    [[nodiscard]] int applyBonusTermsAndTaper(int mg, int eg, int phase, bool noWhitePawns, bool noBlackPawns) const;

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
    bool loadFEN(const std::string& fen);

    [[nodiscard]] uint64_t occupancy(Color color) const;
    [[nodiscard]] uint64_t occupancyAll() const;
    [[nodiscard]] uint64_t getBitboard(Color color, PieceType piece) const {
        return m_bitboards[static_cast<int>(color)][static_cast<int>(piece)];
    }
    [[nodiscard]] bool isSquareOccupied(int square) const;
    [[nodiscard]] bool hasPiece(Color color, PieceType piece, int square) const;
    [[nodiscard]] std::optional<std::pair<Color, PieceType>> pieceAt(int square) const;

    [[nodiscard]] std::vector<Move> generatePseudoLegalMoves() const;
    [[nodiscard]] std::vector<Move> generatePseudoLegalCaptures() const;
    [[nodiscard]] std::vector<Move> generateLegalMoves() const;
    [[nodiscard]] uint64_t perft(int depth) const;
    [[nodiscard]] int evaluate() const;
    [[nodiscard]] int evaluateSideToMove() const;
    [[nodiscard]] int computeStaticEvaluation() const;
#if !defined(NDEBUG) || defined(EVAL_TUNING_DIAGNOSTICS)
    void printEvalBreakdown() const;
#endif
    [[nodiscard]] uint64_t getHash() const { return m_hash; }
    [[nodiscard]] uint64_t computePolyglotHash() const;
    [[nodiscard]] bool isRepetition() const;
    [[nodiscard]] bool isSquareAttacked(int square, Color byColor) const;
    [[nodiscard]] bool inCheck(Color color) const;

    bool applyMove(const Move& move);
    void makeMove(const Move& move);
    void makeNullMove();
    void undoNullMove();
    bool undoMove();
    [[nodiscard]] bool hasNonPawnMaterial(Color color) const;

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