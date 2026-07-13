#include "board.hpp"
#include "eval/evaluation_trace.hpp"
#include "eval/eval_masks.hpp"
#include "eval/eval_tables.hpp"
#include "eval/eval_terms.hpp"
#include "tuning/active_tuning_values.hpp"
#include "tuning/tuning_metadata.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <string>

namespace {

constexpr int W = static_cast<int>(Color::White);
constexpr int B = static_cast<int>(Color::Black);
constexpr int P = static_cast<int>(PieceType::Pawn);
constexpr int N = static_cast<int>(PieceType::Knight);
constexpr int BI = static_cast<int>(PieceType::Bishop);
constexpr int R = static_cast<int>(PieceType::Rook);
constexpr int Q = static_cast<int>(PieceType::Queen);
constexpr int K = static_cast<int>(PieceType::King);
constexpr const auto& T = Tuning::Generated::VALUES.evaluation;
constexpr std::array<const char*, 6> PIECES{"pawn", "knight", "bishop", "rook", "queen", "king"};

Eval::FeatureValue& feature(Eval::EvaluationFeatureTrace& trace, const char* name) {
    return trace.features.at(name);
}

void coefficient(Eval::EvaluationFeatureTrace& trace, const char* name, const std::string& index,
                 int value, int mgContribution = 0, int egContribution = 0) {
    auto& f = feature(trace, name);
    if (value != 0) f.coefficients[index] += value;
    f.middlegameContribution += mgContribution;
    f.endgameContribution += egContribution;
}

void term(Eval::EvaluationFeatureTrace& trace, const char* name, int mg, int eg) {
    trace.terms.push_back({name, mg, eg});
}

int relativeRank(Color color, int square) {
    const int rank = square >> 3;
    return color == Color::White ? rank + 1 : 8 - rank;
}

uint64_t pawnAttacks(Color color, uint64_t pawns) {
    return color == Color::White
        ? ((pawns & ~Board::FILE_A) << 7) | ((pawns & ~Board::FILE_H) << 9)
        : ((pawns & ~Board::FILE_A) >> 9) | ((pawns & ~Board::FILE_H) >> 7);
}

bool adjacentSupport(Color color, uint64_t pawns, int square) {
    const int rank = square >> 3;
    const int file = Eval::fileOf(square);
    for (int df : {-1, 1}) {
        const int f = file + df;
        if (f < 0 || f > 7) continue;
        if (color == Color::White) {
            for (int r = 0; r <= rank; ++r) if (pawns & Eval::squareMask(r * 8 + f)) return true;
        } else {
            for (int r = 7; r >= rank; --r) if (pawns & Eval::squareMask(r * 8 + f)) return true;
        }
    }
    return false;
}

std::array<int, 9> connectedCoefficients(Color color, uint64_t pawns) {
    std::array<int, 9> result{};
    const uint64_t phalanx = ((pawns & ~Board::FILE_A) >> 1) | ((pawns & ~Board::FILE_H) << 1);
    const uint64_t support = color == Color::White
        ? ((pawns & ~Board::FILE_A) << 7) | ((pawns & ~Board::FILE_H) << 9)
        : ((pawns & ~Board::FILE_A) >> 9) | ((pawns & ~Board::FILE_H) >> 7);
    uint64_t active = pawns & (phalanx | support);
    while (active) {
        const int sq = Eval::lsbIndex(active); active &= active - 1;
        ++result[static_cast<size_t>(std::clamp(relativeRank(color, sq), 0, 8))];
    }
    return result;
}

std::array<int, 9> candidateCoefficients(Color color, uint64_t own, uint64_t enemy) {
    std::array<int, 9> result{};
    uint64_t pawns = own;
    const int ci = color == Color::White ? W : B;
    while (pawns) {
        const int sq = Eval::lsbIndex(pawns); pawns &= pawns - 1;
        const uint64_t front = EvalMask::MASKS.PASSED_PAWN_MASKS[ci][sq];
        if (!(enemy & front)) continue;
        const uint64_t file = EvalMask::MASKS.FILE_MASKS[Eval::fileOf(sq)];
        if (enemy & front & file) continue;
        const int adjacent = std::popcount(enemy & front & ~file);
        if (adjacent > 1 || (!adjacentSupport(color, own, sq) && adjacent > 0)) continue;
        ++result[static_cast<size_t>(relativeRank(color, sq))];
    }
    return result;
}

std::array<int, 9> backwardCoefficients(Color color, uint64_t own, uint64_t enemy, uint64_t all) {
    std::array<int, 9> result{};
    const Color enemyColor = color == Color::White ? Color::Black : Color::White;
    const uint64_t pressure = pawnAttacks(enemyColor, enemy);
    uint64_t pawns = own;
    while (pawns) {
        const int sq = Eval::lsbIndex(pawns); pawns &= pawns - 1;
        if (adjacentSupport(color, own, sq)) continue;
        const int advance = color == Color::White ? sq + 8 : sq - 8;
        if (advance < 0 || advance >= 64) continue;
        const uint64_t mask = Eval::squareMask(advance);
        if (!(all & mask) && !(pressure & mask)) continue;
        ++result[static_cast<size_t>(relativeRank(color, sq))];
    }
    return result;
}

int pawnIslands(uint64_t pawns) {
    unsigned files = 0;
    for (int f = 0; f < 8; ++f) if (pawns & EvalMask::MASKS.FILE_MASKS[f]) files |= 1U << f;
    int count = 0; bool previous = false;
    for (int f = 0; f < 8; ++f) { const bool present = files & (1U << f); if (present && !previous) ++count; previous = present; }
    return count;
}

std::pair<int, int> pawnStructureCounts(uint64_t pawns) {
    int doubled = 0, isolated = 0;
    for (int f = 0; f < 8; ++f) {
        const uint64_t onFile = pawns & EvalMask::MASKS.FILE_MASKS[f];
        const int count = std::popcount(onFile);
        if (!count) continue;
        doubled += std::max(0, count - 1);
        uint64_t adjacent = 0;
        if (f) adjacent |= EvalMask::MASKS.FILE_MASKS[f - 1];
        if (f < 7) adjacent |= EvalMask::MASKS.FILE_MASKS[f + 1];
        if (!(pawns & adjacent)) isolated += count;
    }
    return {doubled, isolated};
}

std::array<int, 6> mobilityCoefficients(uint64_t knights, uint64_t bishops, uint64_t rooks,
                                       uint64_t queens, uint64_t own, uint64_t all) {
    std::array<int, 6> result{};
    auto consume = [&](uint64_t bb, int piece) {
        while (bb) {
            const int sq = Eval::lsbIndex(bb); bb &= bb - 1;
            uint64_t attacks = 0;
            if (piece == N) attacks = EvalTerms::knightAttacks(sq);
            else if (piece == BI) attacks = EvalTerms::bishopAttacks(sq, all);
            else if (piece == R) attacks = EvalTerms::rookAttacks(sq, all);
            else attacks = EvalTerms::bishopAttacks(sq, all) | EvalTerms::rookAttacks(sq, all);
            result[piece] += std::popcount(attacks & ~own);
        }
    };
    consume(knights, N); consume(bishops, BI); consume(rooks, R); consume(queens, Q);
    return result;
}

std::array<int, 3> rookCoefficients(Color color, uint64_t rooks, uint64_t ownPawns, uint64_t enemyPawns) {
    std::array<int, 3> result{};
    while (rooks) {
        const int sq = Eval::lsbIndex(rooks); rooks &= rooks - 1;
        const uint64_t file = EvalMask::MASKS.FILE_MASKS[Eval::fileOf(sq)];
        if (!(ownPawns & file) && !(enemyPawns & file)) ++result[0];
        else if (!(ownPawns & file) && (enemyPawns & file)) ++result[1];
        const uint64_t mask = Eval::squareMask(sq);
        if ((color == Color::White && (mask & Board::RANK_7)) || (color == Color::Black && (mask & Board::RANK_2))) ++result[2];
    }
    return result;
}

int kingAttackers(Color color, int kingSquare, uint64_t knights, uint64_t bishops, uint64_t rooks,
                  uint64_t queens, uint64_t all) {
    const uint64_t zone = EvalMask::MASKS.KING_SHIELD_MASKS[color == Color::White ? W : B][kingSquare];
    int count = 0;
    auto hits = [&](uint64_t bb, int type) {
        while (bb) {
            const int sq = Eval::lsbIndex(bb); bb &= bb - 1;
            uint64_t attacks = type == N ? EvalTerms::knightAttacks(sq)
                : type == BI ? EvalTerms::bishopAttacks(sq, all)
                : type == R ? EvalTerms::rookAttacks(sq, all)
                : EvalTerms::bishopAttacks(sq, all) | EvalTerms::rookAttacks(sq, all);
            if (attacks & zone) ++count;
        }
    };
    hits(knights, N); hits(bishops, BI); hits(rooks, R); hits(queens, Q);
    return std::min(count, 8);
}

int shieldPawns(Color color, int kingSquare, uint64_t pawns) {
    return std::popcount(EvalMask::MASKS.KING_SHIELD_MASKS[color == Color::White ? W : B][kingSquare] & pawns);
}

std::pair<int, int> passedRankUnits(Color color, uint64_t own, uint64_t enemy) {
    int unblocked = 0, blocked = 0;
    uint64_t pawns = own;
    const int ci = color == Color::White ? W : B;
    while (pawns) {
        const int sq = Eval::lsbIndex(pawns); pawns &= pawns - 1;
        if (enemy & EvalMask::MASKS.PASSED_PAWN_MASKS[ci][sq]) continue;
        const int units = relativeRank(color, sq) * relativeRank(color, sq);
        const int forward = color == Color::White ? sq + 8 : sq - 8;
        if (forward >= 0 && forward < 64 && (enemy & Eval::squareMask(forward))) blocked += units;
        else unblocked += units;
    }
    return {unblocked, blocked};
}

void initializeFeatures(Eval::EvaluationFeatureTrace& trace) {
    for (const auto& metadata : Tuning::TUNING_FIELDS) {
        if (!metadata.stableName.starts_with("evaluation.")) continue;
        Eval::FeatureValue value;
        if (metadata.id == Tuning::TuningFieldId::EvaluationEndgameLatePhaseMax ||
            metadata.id == Tuning::TuningFieldId::EvaluationEndgameMopUpEgMargin ||
            metadata.id == Tuning::TuningFieldId::EvaluationEndgameMopUpMaterialMargin ||
            metadata.id == Tuning::TuningFieldId::EvaluationKingShieldMaxPawns ||
            metadata.id == Tuning::TuningFieldId::EvaluationPawnsPassedBlockedDivisor)
            value.classification = "piecewise linear";
        if (metadata.id == Tuning::TuningFieldId::EvaluationTaperScale ||
            metadata.stableName.starts_with("evaluation.endgame.scale"))
            value.classification = "conditional linear";
        if (metadata.id == Tuning::TuningFieldId::EvaluationEndgameMopUpWeights)
            value.classification = "non-linear";
        trace.features.emplace(std::string(metadata.stableName), std::move(value));
    }
}

} // namespace

Eval::EvaluationFeatureTrace Board::traceEvaluation() const {
    Eval::EvaluationFeatureTrace out;
    initializeFeatures(out);
    const uint64_t wp = m_bitboards[W][P], bp = m_bitboards[B][P];
    const uint64_t wn = m_bitboards[W][N], bn = m_bitboards[B][N];
    const uint64_t wb = m_bitboards[W][BI], bb = m_bitboards[B][BI];
    const uint64_t wr = m_bitboards[W][R], br = m_bitboards[B][R];
    const uint64_t wq = m_bitboards[W][Q], bq = m_bitboards[B][Q];
    const uint64_t wk = m_bitboards[W][K], bk = m_bitboards[B][K];
    const uint64_t wo = occupancy(Color::White), bo = occupancy(Color::Black), all = wo | bo;

    int mg = 0, eg = 0, phase = 0;
    for (int c = W; c <= B; ++c) for (int p = P; p <= K; ++p) {
        uint64_t pieces = m_bitboards[c][p];
        const int sign = c == W ? 1 : -1;
        while (pieces) {
            const int square = Eval::lsbIndex(pieces); pieces &= pieces - 1;
            const int pstSquare = c == W ? square : Eval::mirrorSquare(square);
            const auto delta = EvalTables::pieceScoreDelta(static_cast<Color>(c), static_cast<PieceType>(p), square);
            mg += sign * delta.mg; eg += sign * delta.eg; phase += delta.phase;
            coefficient(out, "evaluation.material.mg", PIECES[p], sign, sign * T.material.middlegame[p], 0);
            coefficient(out, "evaluation.material.eg", PIECES[p], sign, 0, sign * T.material.endgame[p]);
            coefficient(out, "evaluation.pst.mgPesto", std::string(PIECES[p]) + "." + Board::squareToString(pstSquare), sign,
                        sign * Tuning::Generated::MIDDLEGAME_PIECE_SQUARE_TABLE[p][pstSquare], 0);
            coefficient(out, "evaluation.pst.egPesto", std::string(PIECES[p]) + "." + Board::squareToString(pstSquare), sign,
                        0, sign * Tuning::Generated::ENDGAME_PIECE_SQUARE_TABLE[p][pstSquare]);
            coefficient(out, "evaluation.phase.increments", PIECES[p], 1);
        }
    }
    out.baseMiddlegameScore = mg; out.baseEndgameScore = eg; out.phase = phase;
    out.clampedPhase = std::clamp(phase, 0, 24);
    term(out, "material_and_pst", mg, eg);

    out.insufficientMaterialDraw = EvalTerms::hasInsufficientMaterialDraw(wp, bp, wn, bn, wb, bb, wr, br, wq, bq);
    if (out.insufficientMaterialDraw) {
        out.middlegameScore = mg; out.endgameScore = eg; out.lowMaterialScale = T.endgame.taperScale;
        out.finalScore = 0; out.sideToMoveScore = 0;
        return out;
    }

    const int bishopCoeff = (std::popcount(wb) >= 2 ? 1 : 0) - (std::popcount(bb) >= 2 ? 1 : 0);
    const int bishopMg = bishopCoeff * T.bishopPair.middlegame, bishopEg = bishopCoeff * T.bishopPair.endgame;
    coefficient(out, "evaluation.bishopPair.mg", "scalar", bishopCoeff, bishopMg, 0);
    coefficient(out, "evaluation.bishopPair.eg", "scalar", bishopCoeff, 0, bishopEg);
    mg += bishopMg; eg += bishopEg; term(out, "bishop_pair", bishopMg, bishopEg);

    const auto wm = mobilityCoefficients(wn, wb, wr, wq, wo, all), bm = mobilityCoefficients(bn, bb, br, bq, bo, all);
    int mobilityMg = 0, mobilityEg = 0;
    for (int p = 0; p < 6; ++p) { const int c = wm[p] - bm[p]; mobilityMg += c*T.mobility.middlegame[p]; mobilityEg += c*T.mobility.endgame[p]; coefficient(out,"evaluation.mobility.mg",PIECES[p],c,c*T.mobility.middlegame[p],0); coefficient(out,"evaluation.mobility.eg",PIECES[p],c,0,c*T.mobility.endgame[p]); }
    mg += mobilityMg; eg += mobilityEg; term(out,"mobility",mobilityMg,mobilityEg);

    const auto wra=rookCoefficients(Color::White,wr,wp,bp), bra=rookCoefficients(Color::Black,br,bp,wp);
    const auto ram=T.rookActivity.middlegameArray(), rae=T.rookActivity.endgameArray();
    int rookMg=0,rookEg=0; constexpr std::array<const char*,3> rookNames{"open","semiOpen","seventhRank"};
    for(int i=0;i<3;++i){int c=wra[i]-bra[i];rookMg+=c*ram[i];rookEg+=c*rae[i];coefficient(out,"evaluation.rookActivity.mg",rookNames[i],c,c*ram[i],0);coefficient(out,"evaluation.rookActivity.eg",rookNames[i],c,0,c*rae[i]);}
    mg+=rookMg;eg+=rookEg;term(out,"rook_activity",rookMg,rookEg);

    auto applyRank=[&](const char* mgName,const char* egName,const auto& wc,const auto& bc,const auto& mgWeights,const auto& egWeights,int sign){int dm=0,de=0;for(int i=0;i<9;++i){int c=sign*(wc[i]-bc[i]);dm+=c*mgWeights[i];de+=c*egWeights[i];coefficient(out,mgName,std::to_string(i),c,c*mgWeights[i],0);coefficient(out,egName,std::to_string(i),c,0,c*egWeights[i]);}mg+=dm;eg+=de;term(out,mgName,dm,de);};
    applyRank("evaluation.pawns.connectedByRank.mg","evaluation.pawns.connectedByRank.eg",connectedCoefficients(Color::White,wp),connectedCoefficients(Color::Black,bp),T.pawns.connectedMgByRank,T.pawns.connectedEgByRank,1);
    applyRank("evaluation.pawns.candidateByRank.mg","evaluation.pawns.candidateByRank.eg",candidateCoefficients(Color::White,wp,bp),candidateCoefficients(Color::Black,bp,wp),T.pawns.candidateMgByRank,T.pawns.candidateEgByRank,1);

    const int wks=Eval::lsbIndex(wk), bks=Eval::lsbIndex(bk);
    const int wa=kingAttackers(Color::White,wks,bn,bb,br,bq,all), ba=kingAttackers(Color::Black,bks,wn,wb,wr,wq,all);
    int pressure=0; coefficient(out,"evaluation.king.attackPressure",std::to_string(wa),-1,-T.kingSafety.attackPressure[wa],0); coefficient(out,"evaluation.king.attackPressure",std::to_string(ba),1,T.kingSafety.attackPressure[ba],0); pressure=-T.kingSafety.attackPressure[wa]+T.kingSafety.attackPressure[ba];mg+=pressure;term(out,"king_pressure",pressure,0);
    const int wsp=shieldPawns(Color::White,wks,wp),bsp=shieldPawns(Color::Black,bks,bp);const int shieldCoeff=std::min(wsp,T.kingSafety.shieldMaxPawns)-std::min(bsp,T.kingSafety.shieldMaxPawns);const int shield=shieldCoeff*T.kingSafety.shieldPerPawnBonus;coefficient(out,"evaluation.king.shieldPerPawnBonus","scalar",shieldCoeff,shield,0);coefficient(out,"evaluation.king.shieldMaxPawns","whiteRaw",wsp);coefficient(out,"evaluation.king.shieldMaxPawns","blackRaw",-bsp);mg+=shield;term(out,"king_shield",shield,0);

    const auto [wd,wi]=pawnStructureCounts(wp);const auto [bd,bii]=pawnStructureCounts(bp);const int dc=bd-wd,ic=bii-wi;const int structure=dc*T.pawns.doubledPenalty+ic*T.pawns.isolatedPenalty;coefficient(out,"evaluation.pawns.doubledPenalty","scalar",dc,dc*T.pawns.doubledPenalty,dc*T.pawns.doubledPenalty);coefficient(out,"evaluation.pawns.isolatedPenalty","scalar",ic,ic*T.pawns.isolatedPenalty,ic*T.pawns.isolatedPenalty);mg+=structure;eg+=structure;term(out,"pawn_structure",structure,structure);
    applyRank("evaluation.pawns.backwardByRank.mg","evaluation.pawns.backwardByRank.eg",backwardCoefficients(Color::White,wp,bp,all),backwardCoefficients(Color::Black,bp,wp,all),T.pawns.backwardMgByRank,T.pawns.backwardEgByRank,-1);
    const int islandCoeff=std::max(0,pawnIslands(bp)-1)-std::max(0,pawnIslands(wp)-1);const int islandMg=islandCoeff*T.pawns.islandPenaltyMg,islandEg=islandCoeff*T.pawns.islandPenaltyEg;coefficient(out,"evaluation.pawns.islandPenalty.mg","scalar",islandCoeff,islandMg,0);coefficient(out,"evaluation.pawns.islandPenalty.eg","scalar",islandCoeff,0,islandEg);mg+=islandMg;eg+=islandEg;term(out,"pawn_islands",islandMg,islandEg);

    const int passedDelta=EvalTerms::passedPawnCount(Color::White,wp,bp)-EvalTerms::passedPawnCount(Color::Black,bp,wp);const int pcm=passedDelta*T.pawns.passedCountBonusMg,pce=passedDelta*T.pawns.passedCountBonusEg;coefficient(out,"evaluation.pawns.passedCountBonus.mg","scalar",passedDelta,pcm,0);coefficient(out,"evaluation.pawns.passedCountBonus.eg","scalar",passedDelta,0,pce);mg+=pcm;eg+=pce;term(out,"passed_count",pcm,pce);
    const int wpassed=EvalTerms::passedPawnBonus(Color::White,wp,bp),bpassed=EvalTerms::passedPawnBonus(Color::Black,bp,wp),passed=wpassed-bpassed;const auto wu=passedRankUnits(Color::White,wp,bp),bu=passedRankUnits(Color::Black,bp,wp);coefficient(out,"evaluation.pawns.passedRankSquareMultiplier","unblocked",wu.first-bu.first,passed,0);coefficient(out,"evaluation.pawns.passedRankSquareMultiplier","blocked",wu.second-bu.second);coefficient(out,"evaluation.pawns.passedBlockedDivisor","blockedUnits",wu.second-bu.second);coefficient(out,"evaluation.pawns.passedEgMultiplier","scalar",passed,0,passed*T.pawns.passedEgMultiplier);mg+=passed;eg+=passed*T.pawns.passedEgMultiplier;term(out,"passed_advancement",passed,passed*T.pawns.passedEgMultiplier);

    const int trapped=(EvalTerms::trappedRookPenalty(Color::Black,br,bk)-EvalTerms::trappedRookPenalty(Color::White,wr,wk));const int trappedCoeff=T.piecePlacement.trappedRookPenalty?trapped/T.piecePlacement.trappedRookPenalty:0;coefficient(out,"evaluation.rook.trappedPenalty","scalar",trappedCoeff,trapped,0);mg+=trapped;term(out,"trapped_rook",trapped,0);
    auto badCounts=[&](Color color,uint64_t pawns,uint64_t bishops){int heavy=0,light=0;if(color==Color::White){heavy+=!!((bishops&512ULL)&&(pawns&262144ULL));heavy+=!!((bishops&16384ULL)&&(pawns&2097152ULL));light+=!!((bishops&4ULL)&&(pawns&2048ULL));light+=!!((bishops&32ULL)&&(pawns&4096ULL));}else{heavy+=!!((bishops&562949953421312ULL)&&(pawns&4398046511104ULL));heavy+=!!((bishops&18014398509481984ULL)&&(pawns&35184372088832ULL));light+=!!((bishops&288230376151711744ULL)&&(pawns&2251799813685248ULL));light+=!!((bishops&2305843009213693952ULL)&&(pawns&4503599627370496ULL));}return std::pair{heavy,light};};const auto wbad=badCounts(Color::White,wp,wb),bbad=badCounts(Color::Black,bp,bb);const int hc=bbad.first-wbad.first,lc=bbad.second-wbad.second;int devMg=hc*T.piecePlacement.badBishopHeavyPenalty+lc*T.piecePlacement.badBishopLightPenalty,devEg=devMg;coefficient(out,"evaluation.bishop.badHeavyPenalty","scalar",hc,hc*T.piecePlacement.badBishopHeavyPenalty,hc*T.piecePlacement.badBishopHeavyPenalty);coefficient(out,"evaluation.bishop.badLightPenalty","scalar",lc,lc*T.piecePlacement.badBishopLightPenalty,lc*T.piecePlacement.badBishopLightPenalty);
    const int weq=EvalTerms::earlyQueenPenalty(wq,wn,wb,Color::White),beq=EvalTerms::earlyQueenPenalty(bq,bn,bb,Color::Black),eq=beq-weq,eqc=T.piecePlacement.earlyQueenUndevelopedMinorPenalty?eq/T.piecePlacement.earlyQueenUndevelopedMinorPenalty:0;coefficient(out,"evaluation.queen.earlyUndevelopedMinorPenalty","scalar",eqc,eq,0);devMg+=eq;mg+=devMg;eg+=devEg;term(out,"development",devMg,devEg);
    const int wuc=EvalTerms::uncastledKingPenalty(wk,m_castlingRights,Color::White),buc=EvalTerms::uncastledKingPenalty(bk,m_castlingRights,Color::Black),uc=buc-wuc;int centerCoeff=0,lostCoeff=0;auto unc=[&](Color c,int sq){int file=Eval::fileOf(sq);if(file<2||file>5)return;centerCoeff+=c==Color::White?-1:1;const bool rights=c==Color::White?(m_castlingRights&0b1100):(m_castlingRights&0b0011);if(!rights)lostCoeff+=c==Color::White?-1:1;};unc(Color::White,wks);unc(Color::Black,bks);coefficient(out,"evaluation.king.uncastledCenterPenalty","scalar",centerCoeff,centerCoeff*T.kingSafety.uncastledCenterPenalty,0);coefficient(out,"evaluation.king.uncastledLostRightsPenalty","scalar",lostCoeff,lostCoeff*T.kingSafety.uncastledLostRightsPenalty,0);mg+=uc;term(out,"uncastled",uc,0);

    const int wmat=EvalTerms::endgameMaterialValue(wp,wn,wb,wr,wq),bmat=EvalTerms::endgameMaterialValue(bp,bn,bb,br,bq),adv=wmat-bmat;int mop=0;if(out.clampedPhase<=T.endgame.latePhaseMax){if(eg>T.endgame.mopUpEgMargin&&adv>=T.endgame.mopUpMaterialMargin)mop=EvalTerms::mopUpEval(wks,bks);else if(eg<-T.endgame.mopUpEgMargin&&adv<=-T.endgame.mopUpMaterialMargin)mop=-EvalTerms::mopUpEval(bks,wks);}feature(out,"evaluation.endgame.latePhaseMax").coefficients["phase"]=out.clampedPhase;feature(out,"evaluation.endgame.mopUpEgMargin").coefficients["endgameScore"]=eg;feature(out,"evaluation.endgame.mopUpMaterialMargin").coefficients["materialAdvantage"]=adv;auto geometry=[](int winning,int losing){const int lf=Eval::fileOf(losing),lr=losing>>3,wf=Eval::fileOf(winning),wrank=winning>>3;return std::array<int,4>{std::abs(lf-3)+std::abs(lr-3),std::min({lf,7-lf,lr,7-lr}),std::min({lf+lr,lf+7-lr,7-lf+lr,14-lf-lr}),std::abs(wf-lf)+std::abs(wrank-lr)};};const auto wg=geometry(wks,bks),bg=geometry(bks,wks);auto& mf=feature(out,"evaluation.endgame.mopUpWeights");constexpr std::array<const char*,4> gn{"centerDistance","edgeDistance","cornerDistance","kingDistance"};for(int i=0;i<4;++i){if(wg[i])mf.coefficients[std::string("whiteWins.")+gn[i]]=wg[i];if(bg[i])mf.coefficients[std::string("blackWins.")+gn[i]]=bg[i];}mf.coefficients["activeSign"]=mop>0?1:mop<0?-1:0;mf.endgameContribution=mop;eg+=mop;term(out,"mop_up",0,mop);

    out.middlegameScore=mg;out.endgameScore=eg;out.taperedScore=(mg*out.clampedPhase+eg*(24-out.clampedPhase))/24;out.noPawnScaledScore=out.taperedScore;if(out.noPawnScaledScore>0&&!wp)out.noPawnScaledScore/=2;else if(out.noPawnScaledScore<0&&!bp)out.noPawnScaledScore/=2;out.lowMaterialScale=EvalTerms::lowMaterialScaleFactor(out.clampedPhase,wp,bp,wn,bn,wb,bb,wr,br,wq,bq);feature(out,"evaluation.taper.scale").coefficients["denominator"]=1;feature(out,"evaluation.endgame.scaleMinorOnlyClearEdge").coefficients["selected"]=(out.lowMaterialScale==T.endgame.scaleMinorOnlyClearEdge);feature(out,"evaluation.endgame.scaleMinorOnlyNearEqual").coefficients["selected"]=(out.lowMaterialScale==T.endgame.scaleMinorOnlyNearEqual);feature(out,"evaluation.endgame.scaleOppositeBishopsLowPawns").coefficients["selected"]=(out.lowMaterialScale==T.endgame.scaleOppositeBishopsLowPawns);feature(out,"evaluation.endgame.scaleOppositeBishopsMinPawns").coefficients["selected"]=(out.lowMaterialScale==T.endgame.scaleOppositeBishopsMinPawns);out.finalScore=(out.noPawnScaledScore*out.lowMaterialScale)/T.endgame.taperScale;out.sideToMoveScore=m_sideToMove==Color::White?out.finalScore:-out.finalScore;
    return out;
}
