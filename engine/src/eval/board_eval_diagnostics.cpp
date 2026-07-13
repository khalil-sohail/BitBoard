#include "board.hpp"
#include "eval/eval_terms.hpp"
#include "tuning/generated_tuning_values.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>

namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr int PAWN_IDX = static_cast<int>(PieceType::Pawn);
constexpr int KNIGHT_IDX = static_cast<int>(PieceType::Knight);
constexpr int BISHOP_IDX = static_cast<int>(PieceType::Bishop);
constexpr int ROOK_IDX = static_cast<int>(PieceType::Rook);
constexpr int QUEEN_IDX = static_cast<int>(PieceType::Queen);
constexpr int KING_IDX = static_cast<int>(PieceType::King);
constexpr const auto& EVAL_TUNING = Tuning::Generated::VALUES.evaluation;

} // namespace

#if !defined(NDEBUG) || defined(EVAL_TUNING_DIAGNOSTICS)
void Board::printEvalBreakdown() const {
    bool noWhitePawns = (m_bitboards[WHITE_IDX][PAWN_IDX] == 0);
    bool noBlackPawns = (m_bitboards[BLACK_IDX][PAWN_IDX] == 0);

    int mg = m_mgScore[WHITE_IDX] - m_mgScore[BLACK_IDX];
    int eg = m_egScore[WHITE_IDX] - m_egScore[BLACK_IDX];
    const int phase = m_gamePhase;

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

    std::cout << "=== Eval Breakdown (white-relative) ===\n";
    std::cout << "base.mg=" << mg << " base.eg=" << eg << " phase=" << phase << "/24\n";

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
        std::cout << "insufficient_material_draw=1 final=0\n";
        return;
    }

    auto printTerm = [](const char* label, int mgDelta, int egDelta) {
        std::cout << label << ": mg=" << mgDelta << " eg=" << egDelta << "\n";
    };

    const int whiteBishopsCount = std::popcount(whiteBishops);
    const int blackBishopsCount = std::popcount(blackBishops);
    int bishopPairMg = 0;
    int bishopPairEg = 0;
    if (whiteBishopsCount >= 2) {
        bishopPairMg += EVAL_TUNING.bishopPair.middlegame;
        bishopPairEg += EVAL_TUNING.bishopPair.endgame;
    }
    if (blackBishopsCount >= 2) {
        bishopPairMg -= EVAL_TUNING.bishopPair.middlegame;
        bishopPairEg -= EVAL_TUNING.bishopPair.endgame;
    }
    mg += bishopPairMg;
    eg += bishopPairEg;
    printTerm("bishop_pair", bishopPairMg, bishopPairEg);

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
    const int mobilityMg = whiteMobility.mg - blackMobility.mg;
    const int mobilityEg = whiteMobility.eg - blackMobility.eg;
    mg += mobilityMg;
    eg += mobilityEg;
    printTerm("mobility", mobilityMg, mobilityEg);

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
    const int rookActivityMg = whiteRookActivity.mg - blackRookActivity.mg;
    const int rookActivityEg = whiteRookActivity.eg - blackRookActivity.eg;
    mg += rookActivityMg;
    eg += rookActivityEg;
    printTerm("rook_activity", rookActivityMg, rookActivityEg);

    const int whiteKingSquare = Eval::lsbIndex(m_bitboards[WHITE_IDX][KING_IDX]);
    const int blackKingSquare = Eval::lsbIndex(m_bitboards[BLACK_IDX][KING_IDX]);

    const int whiteConnectedMg = EvalTerms::connectedPawnsBonus(Color::White, whitePawns);
    const int blackConnectedMg = EvalTerms::connectedPawnsBonus(Color::Black, blackPawns);
    const int whiteConnectedEg = EvalTerms::connectedPawnsBonusEg(Color::White, whitePawns);
    const int blackConnectedEg = EvalTerms::connectedPawnsBonusEg(Color::Black, blackPawns);
    const int connectedMg = whiteConnectedMg - blackConnectedMg;
    const int connectedEg = whiteConnectedEg - blackConnectedEg;
    mg += connectedMg;
    eg += connectedEg;
    printTerm("connected_pawns", connectedMg, connectedEg);

    const int whiteCandidateMg = EvalTerms::candidatePawnsBonus(Color::White, whitePawns, blackPawns);
    const int blackCandidateMg = EvalTerms::candidatePawnsBonus(Color::Black, blackPawns, whitePawns);
    const int whiteCandidateEg = EvalTerms::candidatePawnsBonusEg(Color::White, whitePawns, blackPawns);
    const int blackCandidateEg = EvalTerms::candidatePawnsBonusEg(Color::Black, blackPawns, whitePawns);
    const int candidateMg = whiteCandidateMg - blackCandidateMg;
    const int candidateEg = whiteCandidateEg - blackCandidateEg;
    mg += candidateMg;
    eg += candidateEg;
    printTerm("candidate_pawns", candidateMg, candidateEg);

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
    const int kingPressureMg = -(whiteKingPressure - blackKingPressure);
    mg += kingPressureMg;
    printTerm("king_attack_pressure", kingPressureMg, 0);

    const int kingShieldMg = EvalTerms::kingPawnShieldBonus(Color::White, whiteKingSquare, whitePawns)
        - EvalTerms::kingPawnShieldBonus(Color::Black, blackKingSquare, blackPawns);
    mg += kingShieldMg;
    printTerm("king_shield", kingShieldMg, 0);

    const int whitePawnPenalty = EvalTerms::pawnStructurePenalty(whitePawns);
    const int blackPawnPenalty = EvalTerms::pawnStructurePenalty(blackPawns);
    const int pawnStructureMg = -whitePawnPenalty + blackPawnPenalty;
    const int pawnStructureEg = -whitePawnPenalty + blackPawnPenalty;
    mg += pawnStructureMg;
    eg += pawnStructureEg;
    printTerm("pawn_structure", pawnStructureMg, pawnStructureEg);

    const int whiteBackwardMg = EvalTerms::backwardPawnsPenalty(Color::White, whitePawns, blackPawns, allOcc);
    const int blackBackwardMg = EvalTerms::backwardPawnsPenalty(Color::Black, blackPawns, whitePawns, allOcc);
    const int whiteBackwardEg = EvalTerms::backwardPawnsPenaltyEg(Color::White, whitePawns, blackPawns, allOcc);
    const int blackBackwardEg = EvalTerms::backwardPawnsPenaltyEg(Color::Black, blackPawns, whitePawns, allOcc);
    const int backwardMg = -whiteBackwardMg + blackBackwardMg;
    const int backwardEg = -whiteBackwardEg + blackBackwardEg;
    mg += backwardMg;
    eg += backwardEg;
    printTerm("backward_pawns", backwardMg, backwardEg);

    const Eval::TaperTerms whiteIslands = EvalTerms::pawnIslandPenalty(whitePawns);
    const Eval::TaperTerms blackIslands = EvalTerms::pawnIslandPenalty(blackPawns);
    const int islandsMg = -whiteIslands.mg + blackIslands.mg;
    const int islandsEg = -whiteIslands.eg + blackIslands.eg;
    mg += islandsMg;
    eg += islandsEg;
    printTerm("pawn_islands", islandsMg, islandsEg);

    const int passedWhite = EvalTerms::passedPawnCount(Color::White, whitePawns, blackPawns);
    const int passedBlack = EvalTerms::passedPawnCount(Color::Black, blackPawns, whitePawns);
    const int passedCountDelta = passedWhite - passedBlack;
    const int passedCountMg = EVAL_TUNING.pawns.passedCountBonusMg * passedCountDelta;
    const int passedCountEg = EVAL_TUNING.pawns.passedCountBonusEg * passedCountDelta;
    mg += passedCountMg;
    eg += passedCountEg;
    printTerm("passed_count", passedCountMg, passedCountEg);

    const int whitePassed = EvalTerms::passedPawnBonus(Color::White, whitePawns, blackPawns);
    const int blackPassed = EvalTerms::passedPawnBonus(Color::Black, blackPawns, whitePawns);
    const int passedPathMg = whitePassed - blackPassed;
    const int passedPathEg = EVAL_TUNING.pawns.passedEgMultiplier * (whitePassed - blackPassed);
    mg += passedPathMg;
    eg += passedPathEg;
    printTerm("passed_advancement", passedPathMg, passedPathEg);

    const int whiteTrapped = EvalTerms::trappedRookPenalty(Color::White, whiteRooks, m_bitboards[WHITE_IDX][KING_IDX]);
    const int blackTrapped = EvalTerms::trappedRookPenalty(Color::Black, blackRooks, m_bitboards[BLACK_IDX][KING_IDX]);
    const int trappedRookMg = -whiteTrapped + blackTrapped;
    mg += trappedRookMg;
    printTerm("trapped_rook", trappedRookMg, 0);

    const int whiteBadBishop = EvalTerms::badBishopPenalty(whitePawns, whiteBishops, Color::White);
    const int blackBadBishop = EvalTerms::badBishopPenalty(blackPawns, blackBishops, Color::Black);
    const int whiteEarlyQueen = EvalTerms::earlyQueenPenalty(whiteQueens, whiteKnights, whiteBishops, Color::White);
    const int blackEarlyQueen = EvalTerms::earlyQueenPenalty(blackQueens, blackKnights, blackBishops, Color::Black);
    const int developmentMg = -(whiteBadBishop + whiteEarlyQueen) + (blackBadBishop + blackEarlyQueen);
    const int developmentEg = -whiteBadBishop + blackBadBishop;
    mg += developmentMg;
    eg += developmentEg;
    printTerm("development", developmentMg, developmentEg);

    const int whiteUncastled = EvalTerms::uncastledKingPenalty(m_bitboards[WHITE_IDX][KING_IDX], m_castlingRights, Color::White);
    const int blackUncastled = EvalTerms::uncastledKingPenalty(m_bitboards[BLACK_IDX][KING_IDX], m_castlingRights, Color::Black);
    const int uncastledMg = -whiteUncastled + blackUncastled;
    mg += uncastledMg;
    printTerm("uncastled", uncastledMg, 0);

    int mopUpEg = 0;
    const int whiteMaterial = EvalTerms::endgameMaterialValue(whitePawns, whiteKnights, whiteBishops, whiteRooks, whiteQueens);
    const int blackMaterial = EvalTerms::endgameMaterialValue(blackPawns, blackKnights, blackBishops, blackRooks, blackQueens);
    const int materialAdvantage = whiteMaterial - blackMaterial;

    if (clampedPhase <= EVAL_TUNING.endgame.latePhaseMax) {
        if (eg > EVAL_TUNING.endgame.mopUpEgMargin && materialAdvantage >= EVAL_TUNING.endgame.mopUpMaterialMargin) {
            mopUpEg = EvalTerms::mopUpEval(whiteKingSquare, blackKingSquare);
        } else if (eg < -EVAL_TUNING.endgame.mopUpEgMargin && materialAdvantage <= -EVAL_TUNING.endgame.mopUpMaterialMargin) {
            mopUpEg = -EvalTerms::mopUpEval(blackKingSquare, whiteKingSquare);
        }
    }
    eg += mopUpEg;
    printTerm("mop_up", 0, mopUpEg);

    const int tapered = (mg * clampedPhase + eg * (24 - clampedPhase)) / 24;
    std::cout << "tapered_score=" << tapered << "\n";

    int noPawnScaled = tapered;
    if (noPawnScaled > 0 && noWhitePawns) {
        noPawnScaled /= 2;
    } else if (noPawnScaled < 0 && noBlackPawns) {
        noPawnScaled /= 2;
    }
    std::cout << "no_pawn_scaled_score=" << noPawnScaled << "\n";

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

    const int finalScore = (noPawnScaled * lowMaterialScale) / EVAL_TUNING.endgame.taperScale;
    std::cout << "low_material_scale=" << lowMaterialScale << "/" << EVAL_TUNING.endgame.taperScale << "\n";
    std::cout << "final_score=" << finalScore << "\n";
}
#endif
