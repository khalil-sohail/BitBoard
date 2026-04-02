#ifndef EVAL_TERMS_HPP
#define EVAL_TERMS_HPP

#include "board.hpp"
#include "eval/eval_types.hpp"

#include <cstdint>

namespace EvalTerms {

int passedPawnCount(Color color, uint64_t ownPawns, uint64_t enemyPawns);

uint64_t knightAttacks(int square);
uint64_t bishopAttacks(int square, uint64_t occupied);
uint64_t rookAttacks(int square, uint64_t occupied);

Eval::TaperTerms mobilityTermsForSide(
    uint64_t ownKnights,
    uint64_t ownBishops,
    uint64_t ownRooks,
    uint64_t ownQueens,
    uint64_t ownOccupancy,
    uint64_t allOccupancy
);

Eval::TaperTerms rookActivityTermsForSide(Color color, uint64_t rooks, uint64_t ownPawns, uint64_t enemyPawns);

int connectedPawnsBonus(Color color, uint64_t ownPawns);
int connectedPawnsBonusEg(Color color, uint64_t ownPawns);

int candidatePawnsBonus(Color color, uint64_t ownPawns, uint64_t enemyPawns);
int candidatePawnsBonusEg(Color color, uint64_t ownPawns, uint64_t enemyPawns);

int backwardPawnsPenalty(Color color, uint64_t ownPawns, uint64_t enemyPawns, uint64_t allOcc);
int backwardPawnsPenaltyEg(Color color, uint64_t ownPawns, uint64_t enemyPawns, uint64_t allOcc);

Eval::TaperTerms pawnIslandPenalty(uint64_t ownPawns);

int kingAttackPressure(
    Color color,
    int kingSquare,
    uint64_t enemyKnights,
    uint64_t enemyBishops,
    uint64_t enemyRooks,
    uint64_t enemyQueens,
    uint64_t allOcc
);

int kingPawnShieldBonus(Color color, int kingSquare, uint64_t ownPawns);

int endgameMaterialValue(uint64_t pawns, uint64_t knights, uint64_t bishops, uint64_t rooks, uint64_t queens);

bool hasInsufficientMaterialDraw(
    uint64_t whitePawns,
    uint64_t blackPawns,
    uint64_t whiteKnights,
    uint64_t blackKnights,
    uint64_t whiteBishops,
    uint64_t blackBishops,
    uint64_t whiteRooks,
    uint64_t blackRooks,
    uint64_t whiteQueens,
    uint64_t blackQueens
);

bool areOppositeColoredSingleBishops(uint64_t whiteBishops, uint64_t blackBishops);

int lowMaterialScaleFactor(
    int phase,
    uint64_t whitePawns,
    uint64_t blackPawns,
    uint64_t whiteKnights,
    uint64_t blackKnights,
    uint64_t whiteBishops,
    uint64_t blackBishops,
    uint64_t whiteRooks,
    uint64_t blackRooks,
    uint64_t whiteQueens,
    uint64_t blackQueens
);

int pawnStructurePenalty(uint64_t ownPawns);
int mopUpEval(int winningKingSq, int losingKingSq);
int passedPawnBonus(Color color, uint64_t ownPawns, uint64_t enemyPawns);

int trappedRookPenalty(Color color, uint64_t rooks, uint64_t king);
int badBishopPenalty(uint64_t ownPawns, uint64_t ownBishops, Color color);
int earlyQueenPenalty(uint64_t ownQueen, uint64_t ownKnights, uint64_t ownBishops, Color color);
int uncastledKingPenalty(uint64_t king, uint8_t castlingRights, Color color);

} // namespace EvalTerms

#endif
