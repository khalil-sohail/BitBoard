#include "board.hpp"
#include "eval/eval_terms.hpp"
#include "eval/eval_weights.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);
constexpr int KING_IDX = static_cast<int>(PieceType::King);

} // namespace

[[nodiscard]] int Board::applyBonusTermsAndTaper(int mg, int eg, int phase, bool noWhitePawns, bool noBlackPawns) const {
    const uint64_t whitePawns = m_bitboards[WHITE_IDX][PAWN_IDX];
    const uint64_t blackPawns = m_bitboards[BLACK_IDX][PAWN_IDX];
    const uint64_t whiteKnights = m_bitboards[WHITE_IDX][KNIGHT_IDX];
    const uint64_t blackKnights = m_bitboards[BLACK_IDX][KNIGHT_IDX];
    const uint64_t whiteBishops = m_bitboards[WHITE_IDX][BISHOP_IDX];
    const uint64_t blackBishops = m_bitboards[BLACK_IDX][BISHOP_IDX];
    const uint64_t whiteRooks = m_bitboards[WHITE_IDX][ROOK_IDX];
    const uint64_t blackRooks = m_bitboards[BLACK_IDX][ROOK_IDX];
    const uint64_t whiteQueens = m_bitboards[WHITE_IDX][QUEEN_IDX];
    const uint64_t blackQueens = m_bitboards[BLACK_IDX][QUEEN_IDX];

    if (EvalTerms::hasInsufficientMaterialDraw(
        whitePawns,
        blackPawns,
        whiteKnights,
        blackKnights,
        whiteBishops,
        blackBishops,
        whiteRooks,
        blackRooks,
        whiteQueens,
        blackQueens
    )) {
        return 0;
    }

    const int whiteBishopsCount = std::popcount(whiteBishops);
    const int blackBishopsCount = std::popcount(blackBishops);

    if (whiteBishopsCount >= 2) {
        mg += EvalWeights::BISHOP_PAIR_BONUS_MG;
        eg += EvalWeights::BISHOP_PAIR_BONUS_EG;
    }
    if (blackBishopsCount >= 2) {
        mg -= EvalWeights::BISHOP_PAIR_BONUS_MG;
        eg -= EvalWeights::BISHOP_PAIR_BONUS_EG;
    }

    const uint64_t whiteOcc = occupancy(Color::White);
    const uint64_t blackOcc = occupancy(Color::Black);
    const uint64_t allOcc = whiteOcc | blackOcc;
    const int clampedPhase = std::clamp(phase, 0, 24);

    const Eval::TaperTerms whiteMobility = EvalTerms::mobilityTermsForSide(
        whiteKnights,
        whiteBishops,
        whiteRooks,
        whiteQueens,
        whiteOcc,
        allOcc
    );
    const Eval::TaperTerms blackMobility = EvalTerms::mobilityTermsForSide(
        blackKnights,
        blackBishops,
        blackRooks,
        blackQueens,
        blackOcc,
        allOcc
    );

    mg += whiteMobility.mg - blackMobility.mg;
    eg += whiteMobility.eg - blackMobility.eg;

    const Eval::TaperTerms whiteRookActivity = EvalTerms::rookActivityTermsForSide(
        Color::White,
        whiteRooks,
        whitePawns,
        blackPawns
    );
    const Eval::TaperTerms blackRookActivity = EvalTerms::rookActivityTermsForSide(
        Color::Black,
        blackRooks,
        blackPawns,
        whitePawns
    );

    mg += whiteRookActivity.mg - blackRookActivity.mg;
    eg += whiteRookActivity.eg - blackRookActivity.eg;

    const int whiteKingSquare = Eval::lsbIndex(m_bitboards[WHITE_IDX][KING_IDX]);
    const int blackKingSquare = Eval::lsbIndex(m_bitboards[BLACK_IDX][KING_IDX]);

    const int whiteConnectedMg = EvalTerms::connectedPawnsBonus(Color::White, whitePawns);
    const int blackConnectedMg = EvalTerms::connectedPawnsBonus(Color::Black, blackPawns);
    const int whiteConnectedEg = EvalTerms::connectedPawnsBonusEg(Color::White, whitePawns);
    const int blackConnectedEg = EvalTerms::connectedPawnsBonusEg(Color::Black, blackPawns);

    mg += whiteConnectedMg - blackConnectedMg;
    eg += whiteConnectedEg - blackConnectedEg;

    const int whiteCandidateMg = EvalTerms::candidatePawnsBonus(Color::White, whitePawns, blackPawns);
    const int blackCandidateMg = EvalTerms::candidatePawnsBonus(Color::Black, blackPawns, whitePawns);
    const int whiteCandidateEg = EvalTerms::candidatePawnsBonusEg(Color::White, whitePawns, blackPawns);
    const int blackCandidateEg = EvalTerms::candidatePawnsBonusEg(Color::Black, blackPawns, whitePawns);

    mg += whiteCandidateMg - blackCandidateMg;
    eg += whiteCandidateEg - blackCandidateEg;

    const int whiteKingPressure = EvalTerms::kingAttackPressure(
        Color::White,
        whiteKingSquare,
        blackKnights,
        blackBishops,
        blackRooks,
        blackQueens,
        allOcc
    );
    const int blackKingPressure = EvalTerms::kingAttackPressure(
        Color::Black,
        blackKingSquare,
        whiteKnights,
        whiteBishops,
        whiteRooks,
        whiteQueens,
        allOcc
    );

    mg -= (whiteKingPressure - blackKingPressure);

    mg += EvalTerms::kingPawnShieldBonus(Color::White, whiteKingSquare, whitePawns);
    mg -= EvalTerms::kingPawnShieldBonus(Color::Black, blackKingSquare, blackPawns);

    const int whitePawnPenalty = EvalTerms::pawnStructurePenalty(whitePawns);
    const int blackPawnPenalty = EvalTerms::pawnStructurePenalty(blackPawns);
    mg -= whitePawnPenalty;
    eg -= whitePawnPenalty;
    mg += blackPawnPenalty;
    eg += blackPawnPenalty;

    const int whiteBackwardMg = EvalTerms::backwardPawnsPenalty(Color::White, whitePawns, blackPawns, allOcc);
    const int blackBackwardMg = EvalTerms::backwardPawnsPenalty(Color::Black, blackPawns, whitePawns, allOcc);
    const int whiteBackwardEg = EvalTerms::backwardPawnsPenaltyEg(Color::White, whitePawns, blackPawns, allOcc);
    const int blackBackwardEg = EvalTerms::backwardPawnsPenaltyEg(Color::Black, blackPawns, whitePawns, allOcc);
    mg -= whiteBackwardMg;
    mg += blackBackwardMg;
    eg -= whiteBackwardEg;
    eg += blackBackwardEg;

    const Eval::TaperTerms whiteIslands = EvalTerms::pawnIslandPenalty(whitePawns);
    const Eval::TaperTerms blackIslands = EvalTerms::pawnIslandPenalty(blackPawns);
    mg -= whiteIslands.mg;
    mg += blackIslands.mg;
    eg -= whiteIslands.eg;
    eg += blackIslands.eg;

    const int passedWhite = EvalTerms::passedPawnCount(Color::White, whitePawns, blackPawns);
    const int passedBlack = EvalTerms::passedPawnCount(Color::Black, blackPawns, whitePawns);
    mg += EvalWeights::PASSED_PAWN_COUNT_BONUS_MG * (passedWhite - passedBlack);
    eg += EvalWeights::PASSED_PAWN_COUNT_BONUS_EG * (passedWhite - passedBlack);

    const int whitePassed = EvalTerms::passedPawnBonus(Color::White, whitePawns, blackPawns);
    const int blackPassed = EvalTerms::passedPawnBonus(Color::Black, blackPawns, whitePawns);
    mg += whitePassed;
    mg -= blackPassed;
    eg += EvalWeights::PASSED_PAWN_EG_MULTIPLIER * whitePassed;
    eg -= EvalWeights::PASSED_PAWN_EG_MULTIPLIER * blackPassed;

    const int whiteTrapped = EvalTerms::trappedRookPenalty(Color::White, whiteRooks, m_bitboards[WHITE_IDX][KING_IDX]);
    const int blackTrapped = EvalTerms::trappedRookPenalty(Color::Black, blackRooks, m_bitboards[BLACK_IDX][KING_IDX]);
    mg -= whiteTrapped;
    mg += blackTrapped;

    const int whiteBadBishop = EvalTerms::badBishopPenalty(whitePawns, whiteBishops, Color::White);
    const int blackBadBishop = EvalTerms::badBishopPenalty(blackPawns, blackBishops, Color::Black);
    const int whiteEarlyQueen = EvalTerms::earlyQueenPenalty(
        whiteQueens,
        whiteKnights,
        whiteBishops,
        Color::White
    );
    const int blackEarlyQueen = EvalTerms::earlyQueenPenalty(
        blackQueens,
        blackKnights,
        blackBishops,
        Color::Black
    );

    mg -= (whiteBadBishop + whiteEarlyQueen);
    mg += (blackBadBishop + blackEarlyQueen);
    eg -= whiteBadBishop;
    eg += blackBadBishop;

    const int whiteUncastled = EvalTerms::uncastledKingPenalty(m_bitboards[WHITE_IDX][KING_IDX], m_castlingRights, Color::White);
    const int blackUncastled = EvalTerms::uncastledKingPenalty(m_bitboards[BLACK_IDX][KING_IDX], m_castlingRights, Color::Black);
    mg -= whiteUncastled;
    mg += blackUncastled;

    const int whiteMaterial = EvalTerms::endgameMaterialValue(whitePawns, whiteKnights, whiteBishops, whiteRooks, whiteQueens);
    const int blackMaterial = EvalTerms::endgameMaterialValue(blackPawns, blackKnights, blackBishops, blackRooks, blackQueens);
    const int materialAdvantage = whiteMaterial - blackMaterial;

    if (clampedPhase <= EvalWeights::LATE_ENDGAME_PHASE_MAX) {
        if (eg > EvalWeights::MOP_UP_EG_MARGIN && materialAdvantage >= EvalWeights::MOP_UP_MATERIAL_MARGIN) {
            eg += EvalTerms::mopUpEval(whiteKingSquare, blackKingSquare);
        } else if (eg < -EvalWeights::MOP_UP_EG_MARGIN && materialAdvantage <= -EvalWeights::MOP_UP_MATERIAL_MARGIN) {
            eg -= EvalTerms::mopUpEval(blackKingSquare, whiteKingSquare);
        }
    }

    int score = (mg * clampedPhase + eg * (24 - clampedPhase)) / 24;

    if (score > 0 && noWhitePawns) {
        score /= 2;
    } else if (score < 0 && noBlackPawns) {
        score /= 2;
    }

    const int lowMaterialScale = EvalTerms::lowMaterialScaleFactor(
        clampedPhase,
        whitePawns,
        blackPawns,
        whiteKnights,
        blackKnights,
        whiteBishops,
        blackBishops,
        whiteRooks,
        blackRooks,
        whiteQueens,
        blackQueens
    );

    score = (score * lowMaterialScale) / EvalWeights::TAPER_SCALE;

    return score;
}
