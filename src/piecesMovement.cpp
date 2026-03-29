#include "board.hpp"

#include <algorithm>
#include <cctype>
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

uint64_t squareMask(int square) {
    return 1ULL << square;
}

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
    m_hashHistory.push_back(computePolyglotHash());

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

    const uint64_t fromMask = squareMask(move.from);
    const uint64_t toMask = squareMask(move.to);
    const int pieceIdx = static_cast<int>(move.piece);

    removePieceEval(usColor, move.piece, move.from);

    m_bitboards[us][pieceIdx] &= ~fromMask;

    for (int p = 0; p < static_cast<int>(PieceType::Count); ++p) {
        if (m_bitboards[them][p] & toMask) {
            removePieceEval(themColor, static_cast<PieceType>(p), move.to);
            m_bitboards[them][p] &= ~toMask;
        }
    }

    if (move.isEnPassant) {
        const int capSq = (m_sideToMove == Color::White) ? move.to - 8 : move.to + 8;
        removePieceEval(themColor, PieceType::Pawn, capSq);
        m_bitboards[them][PAWN_IDX] &= ~squareMask(capSq);
    }

    if (move.promotion.has_value()) {
        addPieceEval(usColor, *move.promotion, move.to);
        m_bitboards[us][static_cast<int>(*move.promotion)] |= toMask;
    } else {
        addPieceEval(usColor, move.piece, move.to);
        m_bitboards[us][pieceIdx] |= toMask;
    }

    if (move.isKingSideCastle) {
        if (m_sideToMove == Color::White) {
            removePieceEval(usColor, PieceType::Rook, 7);
            addPieceEval(usColor, PieceType::Rook, 5);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(7);
            m_bitboards[us][ROOK_IDX] |= squareMask(5);
        } else {
            removePieceEval(usColor, PieceType::Rook, 63);
            addPieceEval(usColor, PieceType::Rook, 61);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(63);
            m_bitboards[us][ROOK_IDX] |= squareMask(61);
        }
    }
    if (move.isQueenSideCastle) {
        if (m_sideToMove == Color::White) {
            removePieceEval(usColor, PieceType::Rook, 0);
            addPieceEval(usColor, PieceType::Rook, 3);
            m_bitboards[us][ROOK_IDX] &= ~squareMask(0);
            m_bitboards[us][ROOK_IDX] |= squareMask(3);
        } else {
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
}

void Board::makeNullMove() {
    m_hashHistory.push_back(computePolyglotHash());

    m_undoStack.push_back({
        .bitboards = m_bitboards,
        .sideToMove = m_sideToMove,
        .castlingRights = m_castlingRights,
        .enPassantSquare = m_enPassantSquare,
        .mgScore = m_mgScore,
        .egScore = m_egScore,
        .gamePhase = m_gamePhase,
    });

    m_enPassantSquare = -1;
    m_sideToMove = (m_sideToMove == Color::White) ? Color::Black : Color::White;
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

    if (!m_hashHistory.empty()) {
        m_hashHistory.pop_back();
    }

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
    std::string token = trim(input);
    if (token.empty()) {
        out.error = "Empty input. Enter a move like e2e4, Nf3, or O-O.";
        return out;
    }

    std::vector<Move> legal = generateLegalMoves();
    if (legal.empty()) {
        out.error = "No legal moves available in this position.";
        return out;
    }

    bool requiresCheck = false;
    bool requiresMate = false;
    if (!token.empty() && (token.back() == '+' || token.back() == '#')) {
        requiresMate = token.back() == '#';
        requiresCheck = token.back() == '+';
        token.pop_back();
        if (!token.empty() && (token.back() == '+' || token.back() == '#')) {
            out.error = "Invalid SAN suffix. Use at most one of '+' or '#'.";
            return out;
        }
    }

    if (token == "O-O" || token == "0-0" || token == "o-o") {
        std::vector<Move> matches;
        for (const Move& m : legal) {
            if (m.isKingSideCastle) matches.push_back(m);
        }
        if (matches.empty()) {
            out.error = "Kingside castling is not legal in this position.";
            return out;
        }
        if (requiresMate && !isMateAfterMove(*this, matches.front())) {
            out.error = "Move does not result in checkmate, but '#' was provided.";
            return out;
        }
        if (requiresCheck && !isCheckAfterMove(*this, matches.front())) {
            out.error = "Move does not give check, but '+' was provided.";
            return out;
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
            out.error = "Queenside castling is not legal in this position.";
            return out;
        }
        if (requiresMate && !isMateAfterMove(*this, matches.front())) {
            out.error = "Move does not result in checkmate, but '#' was provided.";
            return out;
        }
        if (requiresCheck && !isCheckAfterMove(*this, matches.front())) {
            out.error = "Move does not give check, but '+' was provided.";
            return out;
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
                out.error = "Invalid coordinate move. Expected format like e2e4.";
                return out;
            }

            std::optional<PieceType> promotion;
            if (token.size() > 4) {
                char promoChar = static_cast<char>(std::toupper(static_cast<unsigned char>(token[4])));
                PieceType p = pieceFromLetter(promoChar);
                if (p == PieceType::Pawn || p == PieceType::King) {
                    out.error = "Invalid promotion piece. Use one of Q, R, B, N.";
                    return out;
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
                out.error = "Illegal move for current position: " + token + ".";
                return out;
            }

            const Move& chosen = matches.front();
            if (requiresMate && !isMateAfterMove(*this, chosen)) {
                out.error = "Move does not result in checkmate, but '#' was provided.";
                return out;
            }
            if (requiresCheck && !isCheckAfterMove(*this, chosen)) {
                out.error = "Move does not give check, but '+' was provided.";
                return out;
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
            out.error = "Invalid SAN promotion syntax. Use e8=Q or exd8=Q.";
            return out;
        }
        char promoChar = static_cast<char>(std::toupper(static_cast<unsigned char>(token[eqPos + 1])));
        PieceType p = pieceFromLetter(promoChar);
        if (p == PieceType::Pawn || p == PieceType::King) {
            out.error = "Invalid promotion piece in SAN. Use Q, R, B, or N.";
            return out;
        }
        promotion = p;
        if (piece != PieceType::Pawn) {
            out.error = "Only pawns may promote in SAN notation.";
            return out;
        }
        token = token.substr(0, eqPos);
    }

    if (token.size() < pos + 2) {
        out.error = "Incomplete SAN move.";
        return out;
    }

    const std::string destStr = token.substr(token.size() - 2, 2);
    const int to = squareFromString(destStr);
    if (to == -1) {
        out.error = "Invalid destination square in SAN move.";
        return out;
    }

    std::string core = token.substr(pos, token.size() - pos - 2);
    bool wantsCapture = false;
    size_t xPos = core.find('x');
    if (xPos != std::string::npos) {
        wantsCapture = true;
        core.erase(xPos, 1);
        if (core.find('x') != std::string::npos) {
            out.error = "Invalid SAN: too many capture markers 'x'.";
            return out;
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
            out.error = "Invalid SAN pawn capture. Example: exd5.";
            return out;
        }
    }

    if (matches.empty()) {
        out.error = "No legal SAN match for '" + input + "'.";
        return out;
    }

    if (matches.size() > 1) {
        out.error = "Ambiguous SAN move '" + input + "'. Candidates: " + joinMoves(matches) + ".";
        return out;
    }

    const Move& chosen = matches.front();
    if (requiresMate && !isMateAfterMove(*this, chosen)) {
        out.error = "Move does not result in checkmate, but '#' was provided.";
        return out;
    }
    if (requiresCheck && !isCheckAfterMove(*this, chosen)) {
        out.error = "Move does not give check, but '+' was provided.";
        return out;
    }

    out.move = chosen;
    return out;
}
