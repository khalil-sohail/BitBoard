#include "board.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>
#include <sstream>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);

constexpr uint8_t CASTLE_WK = 0b1000;
constexpr uint8_t CASTLE_WQ = 0b0100;
constexpr uint8_t CASTLE_BK = 0b0010;
constexpr uint8_t CASTLE_BQ = 0b0001;

constexpr uint64_t kPolyglotRandom[781] = {
#include "polyglot_random_values.inc"
};

uint64_t squareMask(int square) {
    return 1ULL << square;
}

int colorToPolyglotPivot(Color color) {
    return (color == Color::Black) ? 0 : 1;
}

uint64_t pieceHash(Color color, PieceType piece, int square) {
    const int polyPieceIndex = static_cast<int>(piece) * 2 + colorToPolyglotPivot(color);
    const int randomIndex = 64 * polyPieceIndex + square;
    return kPolyglotRandom[randomIndex];
}

uint64_t castlingHash(uint8_t rights) {
    uint64_t key = 0ULL;
    if (rights & CASTLE_WK) key ^= kPolyglotRandom[768];
    if (rights & CASTLE_WQ) key ^= kPolyglotRandom[769];
    if (rights & CASTLE_BK) key ^= kPolyglotRandom[770];
    if (rights & CASTLE_BQ) key ^= kPolyglotRandom[771];
    return key;
}

uint64_t enPassantHash(Color sideToMove,
                       int enPassantSquare,
                       const std::array<std::array<uint64_t, static_cast<size_t>(PieceType::Count)>, 2>& bitboards) {
    if (enPassantSquare < 0 || enPassantSquare >= 64) {
        return 0ULL;
    }

    uint64_t epMask = 1ULL << enPassantSquare;
    if (sideToMove == Color::White) {
        epMask >>= 8;
    } else {
        epMask <<= 8;
    }

    const uint64_t adjacentPawns =
        ((epMask & ~Board::FILE_A) >> 1) |
        ((epMask & ~Board::FILE_H) << 1);

    const int us = static_cast<int>(sideToMove);
    const uint64_t ownPawns = bitboards[us][PAWN_IDX];
    if ((adjacentPawns & ownPawns) == 0ULL) {
        return 0ULL;
    }

    const int epFile = enPassantSquare % 8;
    return kPolyglotRandom[772 + epFile];
}

constexpr uint64_t SIDE_TO_MOVE_HASH = kPolyglotRandom[780];

PieceType pieceFromLetter(char c) {
    switch (c) {
        case 'N': return PieceType::Knight;
        case 'B': return PieceType::Bishop;
        case 'R': return PieceType::Rook;
        case 'Q': return PieceType::Queen;
        case 'K': return PieceType::King;
        default: return PieceType::Pawn;
    }
}

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool isPieceLetter(char c) {
    return c == 'K' || c == 'Q' || c == 'R' || c == 'B' || c == 'N';
}

std::string describeMove(const Move& m) {
    return Board::squareToString(m.from) + Board::squareToString(m.to);
}

std::string joinMoves(const std::vector<Move>& moves) {
    std::ostringstream oss;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << describeMove(moves[i]);
    }
    return oss.str();
}

bool fileMatches(int square, char file) {
    return (square % 8) == (file - 'a');
}

bool rankMatches(int square, char rank) {
    return (square / 8) == (rank - '1');
}

bool isMateAfterMove(const Board& board, const Move& mv) {
    Board copy = board;
    copy.makeMove(mv);
    const Color side = copy.sideToMove();
    return copy.inCheck(side) && copy.generateLegalMoves().empty();
}

bool isCheckAfterMove(const Board& board, const Move& mv) {
    Board copy = board;
    copy.makeMove(mv);
    return copy.inCheck(copy.sideToMove());
}

} // namespace

Color Board::sideToMove() const {
    return m_sideToMove;
}

std::string Board::squareToString(int square) {
    if (square < 0 || square >= 64) {
        return "??";
    }
    const char file = static_cast<char>('a' + (square % 8));
    const char rank = static_cast<char>('1' + (square / 8));
    return std::string{file, rank};
}

int Board::squareFromString(const std::string& coord) {
    if (coord.size() != 2) {
        return -1;
    }
    const char file = static_cast<char>(std::tolower(static_cast<unsigned char>(coord[0])));
    const char rank = coord[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return -1;
    }
    return (rank - '1') * 8 + (file - 'a');
}

void Board::makeMove(const Move& move) {
    m_hashHistory.push_back(m_hash);

    m_undoStack.push_back({
        .bitboards = m_bitboards,
        .sideToMove = m_sideToMove,
        .castlingRights = m_castlingRights,
        .enPassantSquare = m_enPassantSquare,
        .mgScore = m_mgScore,
        .egScore = m_egScore,
        .gamePhase = m_gamePhase,
    });

    const int us = static_cast<int>(m_sideToMove);
    const int them = (us == WHITE_IDX) ? BLACK_IDX : WHITE_IDX;
    const Color usColor = m_sideToMove;
    const Color themColor = (usColor == Color::White) ? Color::Black : Color::White;
    const uint8_t oldCastlingRights = m_castlingRights;
    const int oldEnPassantSquare = m_enPassantSquare;
    const Color oldSideToMove = m_sideToMove;

    const uint64_t fromMask = squareMask(move.from);
    const uint64_t toMask = squareMask(move.to);
    const int pieceIdx = static_cast<int>(move.piece);

    m_hash ^= castlingHash(oldCastlingRights);
    m_hash ^= enPassantHash(oldSideToMove, oldEnPassantSquare, m_bitboards);

    m_hash ^= pieceHash(usColor, move.piece, move.from);

    removePieceEval(usColor, move.piece, move.from);

    m_bitboards[us][pieceIdx] &= ~fromMask;

    for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
        if (m_bitboards[them][p] & toMask) {
            m_hash ^= pieceHash(themColor, static_cast<PieceType>(p), move.to);
            removePieceEval(themColor, static_cast<PieceType>(p), move.to);
            m_bitboards[them][p] &= ~toMask;
        }
    }

    if (move.isEnPassant) {
        const int capSq = (m_sideToMove == Color::White) ? move.to - 8 : move.to + 8;
        m_hash ^= pieceHash(themColor, PieceType::Pawn, capSq);
        removePieceEval(themColor, PieceType::Pawn, capSq);
        m_bitboards[them][PAWN_IDX] &= ~squareMask(capSq);
    }

    if (move.promotion.has_value()) {
        m_hash ^= pieceHash(usColor, *move.promotion, move.to);
        addPieceEval(usColor, *move.promotion, move.to);
        m_bitboards[us][static_cast<int>(*move.promotion)] |= toMask;
    } else {
        m_hash ^= pieceHash(usColor, move.piece, move.to);
        addPieceEval(usColor, move.piece, move.to);
        m_bitboards[us][pieceIdx] |= toMask;
    }

    if (move.isKingSideCastle) {
        if (m_sideToMove == Color::White) {
            m_hash ^= pieceHash(usColor, PieceType::Rook, 7);
            m_hash ^= pieceHash(usColor, PieceType::Rook, 5);
            removePieceEval(usColor, PieceType::Rook, 7);
            addPieceEval(usColor, PieceType::Rook, 5);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(7);
            m_bitboards[us][ROOK_IDX] |= squareMask(5);
        } else {
            m_hash ^= pieceHash(usColor, PieceType::Rook, 63);
            m_hash ^= pieceHash(usColor, PieceType::Rook, 61);
            removePieceEval(usColor, PieceType::Rook, 63);
            addPieceEval(usColor, PieceType::Rook, 61);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(63);
            m_bitboards[us][ROOK_IDX] |= squareMask(61);
        }
    }
    if (move.isQueenSideCastle) {
        if (m_sideToMove == Color::White) {
            m_hash ^= pieceHash(usColor, PieceType::Rook, 0);
            m_hash ^= pieceHash(usColor, PieceType::Rook, 3);
            removePieceEval(usColor, PieceType::Rook, 0);
            addPieceEval(usColor, PieceType::Rook, 3);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(0);
            m_bitboards[us][ROOK_IDX] |= squareMask(3);
        } else {
            m_hash ^= pieceHash(usColor, PieceType::Rook, 56);
            m_hash ^= pieceHash(usColor, PieceType::Rook, 59);
            removePieceEval(usColor, PieceType::Rook, 56);
            addPieceEval(usColor, PieceType::Rook, 59);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(56);
            m_bitboards[us][ROOK_IDX] |= squareMask(59);
        }
    }

    if (m_sideToMove == Color::White) {
        if (move.piece == PieceType::King) m_castlingRights &= ~(CASTLE_WK | CASTLE_WQ);
        if (move.piece == PieceType::Rook && move.from == 0) m_castlingRights &= ~CASTLE_WQ;
        if (move.piece == PieceType::Rook && move.from == 7) m_castlingRights &= ~CASTLE_WK;
        if (move.to == 56) m_castlingRights &= ~CASTLE_BQ;
        if (move.to == 63) m_castlingRights &= ~CASTLE_BK;
    } else {
        if (move.piece == PieceType::King) m_castlingRights &= ~(CASTLE_BK | CASTLE_BQ);
        if (move.piece == PieceType::Rook && move.from == 56) m_castlingRights &= ~CASTLE_BQ;
        if (move.piece == PieceType::Rook && move.from == 63) m_castlingRights &= ~CASTLE_BK;
        if (move.to == 0) m_castlingRights &= ~CASTLE_WQ;
        if (move.to == 7) m_castlingRights &= ~CASTLE_WK;
    }

    if (move.isDoublePush) {
        m_enPassantSquare = (m_sideToMove == Color::White) ? move.to - 8 : move.to + 8;
    } else {
        m_enPassantSquare = -1;
    }

    m_sideToMove = (m_sideToMove == Color::White) ? Color::Black : Color::White;
    m_hash ^= castlingHash(m_castlingRights);
    m_hash ^= enPassantHash(m_sideToMove, m_enPassantSquare, m_bitboards);
    m_hash ^= SIDE_TO_MOVE_HASH;

    // assert(m_hash == computePolyglotHash() && "Incremental hash desync in makeMove!");
}

void Board::makeNullMove() {
    m_hashHistory.push_back(m_hash);

    m_undoStack.push_back({
        .bitboards = m_bitboards,
        .sideToMove = m_sideToMove,
        .castlingRights = m_castlingRights,
        .enPassantSquare = m_enPassantSquare,
        .mgScore = m_mgScore,
        .egScore = m_egScore,
        .gamePhase = m_gamePhase,
    });

    m_hash ^= enPassantHash(m_sideToMove, m_enPassantSquare, m_bitboards);
    m_enPassantSquare = -1;
    m_sideToMove = (m_sideToMove == Color::White) ? Color::Black : Color::White;
    m_hash ^= SIDE_TO_MOVE_HASH;
}

void Board::undoNullMove() {
    (void)undoMove();
}

bool Board::undoMove() {
    if (m_undoStack.empty()) {
        return false;
    }

    const UndoState& prev = m_undoStack.back();
    m_bitboards = prev.bitboards;
    m_sideToMove = prev.sideToMove;
    m_castlingRights = prev.castlingRights;
    m_enPassantSquare = prev.enPassantSquare;
    m_mgScore = prev.mgScore;
    m_egScore = prev.egScore;
    m_gamePhase = prev.gamePhase;
    m_undoStack.pop_back();

    assert(!m_hashHistory.empty() && "Missing hash history entry on undoMove");
    m_hash = m_hashHistory.back();
    m_hashHistory.pop_back();

    return true;
}

bool Board::applyMove(const Move& move) {
    const std::vector<Move> legal = generateLegalMoves();
    auto it = std::find_if(legal.begin(), legal.end(), [&](const Move& m) {
        return m.from == move.from &&
               m.to == move.to &&
               m.piece == move.piece &&
               m.promotion == move.promotion;
    });
    if (it == legal.end()) {
        return false;
    }
    makeMove(*it);
    return true;
}

ParseResult Board::parseMove(const std::string& input) const {
    ParseResult out;
    auto fail = [&](const std::string& msg) {
        out.error = msg;
        std::cout << "info string PARSE ERROR: " << msg << " (input: " << input << ")\n";
        return out;
    };

    std::string token = trim(input);
    if (token.empty()) {
        return fail("Empty input. Enter a move like e2e4, Nf3, or O-O.");
    }

    std::vector<Move> legal = generateLegalMoves();
    if (legal.empty()) {
        return fail("No legal moves available in this position.");
    }

    bool requiresCheck = false;
    bool requiresMate = false;
    if (!token.empty() && (token.back() == '+' || token.back() == '#')) {
        requiresMate = token.back() == '#';
        requiresCheck = token.back() == '+';
        token.pop_back();
        if (!token.empty() && (token.back() == '+' || token.back() == '#')) {
            return fail("Invalid SAN suffix. Use at most one of '+' or '#'.");
        }
    }

    if (token == "O-O" || token == "0-0" || token == "o-o") {
        std::vector<Move> matches;
        for (const Move& m : legal) {
            if (m.isKingSideCastle) matches.push_back(m);
        }
        if (matches.empty()) {
            return fail("Kingside castling is not legal in this position.");
        }
        if (requiresMate && !isMateAfterMove(*this, matches.front())) {
            return fail("Move does not result in checkmate, but '#' was provided.");
        }
        if (requiresCheck && !isCheckAfterMove(*this, matches.front())) {
            return fail("Move does not give check, but '+' was provided.");
        }
        out.move = matches.front();
        return out;
    }

    if (token == "O-O-O" || token == "0-0-0" || token == "o-o-o") {
        std::vector<Move> matches;
        for (const Move& m : legal) {
            if (m.isQueenSideCastle) matches.push_back(m);
        }
        if (matches.empty()) {
            return fail("Queenside castling is not legal in this position.");
        }
        if (requiresMate && !isMateAfterMove(*this, matches.front())) {
            return fail("Move does not result in checkmate, but '#' was provided.");
        }
        if (requiresCheck && !isCheckAfterMove(*this, matches.front())) {
            return fail("Move does not give check, but '+' was provided.");
        }
        out.move = matches.front();
        return out;
    }

    // Coordinate notation: e2e4 or e7e8q
    if (token.size() >= 4) {
        const char f1 = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
        const char r1 = token[1];
        const char f2 = static_cast<char>(std::tolower(static_cast<unsigned char>(token[2])));
        const char r2 = token[3];

        const bool looksLikeCoordinate = (f1 >= 'a' && f1 <= 'h') &&
                                         (r1 >= '1' && r1 <= '8') &&
                                         (f2 >= 'a' && f2 <= 'h') &&
                                         (r2 >= '1' && r2 <= '8');

        if (!looksLikeCoordinate) {
            // Not coordinate notation; continue to SAN parsing.
        } else {
            const int from = squareFromString(token.substr(0, 2));
            const int to = squareFromString(token.substr(2, 2));
            if (from == -1 || to == -1) {
                return fail("Invalid coordinate move. Expected format like e2e4.");
            }

            std::optional<PieceType> promotion;
            if (token.size() > 4) {
                char promoChar = static_cast<char>(std::toupper(static_cast<unsigned char>(token[4])));
                PieceType p = pieceFromLetter(promoChar);
                if (p == PieceType::Pawn || p == PieceType::King) {
                    return fail("Invalid promotion piece. Use one of Q, R, B, N.");
                }
                promotion = p;
            }

            std::vector<Move> matches;
            for (const Move& m : legal) {
                if (m.from == from && m.to == to && m.promotion == promotion) {
                    matches.push_back(m);
                }
            }
            if (matches.empty()) {
                return fail("Illegal move for current position: " + token + ".");
            }

            const Move& chosen = matches.front();
            if (requiresMate && !isMateAfterMove(*this, chosen)) {
                return fail("Move does not result in checkmate, but '#' was provided.");
            }
            if (requiresCheck && !isCheckAfterMove(*this, chosen)) {
                return fail("Move does not give check, but '+' was provided.");
            }

            out.move = chosen;
            return out;
        }
    }

    // SAN parsing
    // Grammar (supported):
    //  Piece? disambiguation? x? destination (=Promotion)?
    //  Examples: Nf3, Nbd2, N1d2, Raxc1, exd5, e8=Q, exd8=Q
    PieceType piece = PieceType::Pawn;
    size_t pos = 0;
    if (!token.empty() && isPieceLetter(token[0])) {
        piece = pieceFromLetter(token[0]);
        pos = 1;
    }

    size_t eqPos = token.find('=');
    std::optional<PieceType> promotion;
    if (eqPos != std::string::npos) {
        if (eqPos + 1 >= token.size()) {
            return fail("Invalid SAN promotion syntax. Use e8=Q or exd8=Q.");
        }
        char promoChar = static_cast<char>(std::toupper(static_cast<unsigned char>(token[eqPos + 1])));
        PieceType p = pieceFromLetter(promoChar);
        if (p == PieceType::Pawn || p == PieceType::King) {
            return fail("Invalid promotion piece in SAN. Use Q, R, B, or N.");
        }
        promotion = p;
        if (piece != PieceType::Pawn) {
            return fail("Only pawns may promote in SAN notation.");
        }
        token = token.substr(0, eqPos);
    }

    if (token.size() < pos + 2) {
        return fail("Incomplete SAN move.");
    }

    const std::string destStr = token.substr(token.size() - 2, 2);
    const int to = squareFromString(destStr);
    if (to == -1) {
        return fail("Invalid destination square in SAN move.");
    }

    std::string core = token.substr(pos, token.size() - pos - 2);
    bool wantsCapture = false;
    size_t xPos = core.find('x');
    if (xPos != std::string::npos) {
        wantsCapture = true;
        core.erase(xPos, 1);
        if (core.find('x') != std::string::npos) {
            return fail("Invalid SAN: too many capture markers 'x'.");
        }
    }

    std::string disambiguation = core;

    std::vector<Move> matches;
    for (const Move& m : legal) {
        if (m.piece != piece) continue;
        if (m.to != to) continue;
        if (m.promotion != promotion) continue;
        if (wantsCapture != m.isCapture) continue;

        bool disambOk = true;
        if (!disambiguation.empty()) {
            if (disambiguation.size() == 1) {
                char c = disambiguation[0];
                if (c >= 'a' && c <= 'h') {
                    disambOk = fileMatches(m.from, c);
                } else if (c >= '1' && c <= '8') {
                    disambOk = rankMatches(m.from, c);
                } else {
                    disambOk = false;
                }
            } else if (disambiguation.size() == 2) {
                int fromSq = squareFromString(disambiguation);
                disambOk = (fromSq != -1 && m.from == fromSq);
            } else {
                disambOk = false;
            }
        }

        if (disambOk) {
            matches.push_back(m);
        }
    }

    if (piece == PieceType::Pawn && wantsCapture && disambiguation.size() == 1) {
        const char srcFile = disambiguation[0];
        if (!(srcFile >= 'a' && srcFile <= 'h')) {
            return fail("Invalid SAN pawn capture. Example: exd5.");
        }
    }

    if (matches.empty()) {
        return fail("No legal SAN match for '" + input + "'.");
    }

    if (matches.size() > 1) {
        return fail("Ambiguous SAN move '" + input + "'. Candidates: " + joinMoves(matches) + ".");
    }

    const Move& chosen = matches.front();
    if (requiresMate && !isMateAfterMove(*this, chosen)) {
        return fail("Move does not result in checkmate, but '#' was provided.");
    }
    if (requiresCheck && !isCheckAfterMove(*this, chosen)) {
        return fail("Move does not give check, but '+' was provided.");
    }

    out.move = chosen;
    return out;
}
