#include "board.hpp"
#include "move/move_parse_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace MoveParseHelpers {

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

} // namespace MoveParseHelpers

ParseResult Board::parseMove(const std::string& input) const {
    ParseResult out;
    auto fail = [&](const std::string& msg) {
        out.error = msg;
        std::cout << "info string PARSE ERROR: " << msg << " (input: " << input << ")\n";
        return out;
    };

    std::string token = MoveParseHelpers::trim(input);
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
        if (requiresMate && !MoveParseHelpers::isMateAfterMove(*this, matches.front())) {
            return fail("Move does not result in checkmate, but '#' was provided.");
        }
        if (requiresCheck && !MoveParseHelpers::isCheckAfterMove(*this, matches.front())) {
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
        if (requiresMate && !MoveParseHelpers::isMateAfterMove(*this, matches.front())) {
            return fail("Move does not result in checkmate, but '#' was provided.");
        }
        if (requiresCheck && !MoveParseHelpers::isCheckAfterMove(*this, matches.front())) {
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
                PieceType p = MoveParseHelpers::pieceFromLetter(promoChar);
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
            if (requiresMate && !MoveParseHelpers::isMateAfterMove(*this, chosen)) {
                return fail("Move does not result in checkmate, but '#' was provided.");
            }
            if (requiresCheck && !MoveParseHelpers::isCheckAfterMove(*this, chosen)) {
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
    if (!token.empty() && MoveParseHelpers::isPieceLetter(token[0])) {
        piece = MoveParseHelpers::pieceFromLetter(token[0]);
        pos = 1;
    }

    size_t eqPos = token.find('=');
    std::optional<PieceType> promotion;
    if (eqPos != std::string::npos) {
        if (eqPos + 1 >= token.size()) {
            return fail("Invalid SAN promotion syntax. Use e8=Q or exd8=Q.");
        }
        char promoChar = static_cast<char>(std::toupper(static_cast<unsigned char>(token[eqPos + 1])));
        PieceType p = MoveParseHelpers::pieceFromLetter(promoChar);
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
                    disambOk = MoveParseHelpers::fileMatches(m.from, c);
                } else if (c >= '1' && c <= '8') {
                    disambOk = MoveParseHelpers::rankMatches(m.from, c);
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
        return fail("Ambiguous SAN move '" + input + "'. Candidates: " + MoveParseHelpers::joinMoves(matches) + ".");
    }

    const Move& chosen = matches.front();
    if (requiresMate && !MoveParseHelpers::isMateAfterMove(*this, chosen)) {
        return fail("Move does not result in checkmate, but '#' was provided.");
    }
    if (requiresCheck && !MoveParseHelpers::isCheckAfterMove(*this, chosen)) {
        return fail("Move does not give check, but '+' was provided.");
    }

    out.move = chosen;
    return out;
}