#include "eval/eval_terms.hpp"
#include "eval/eval_masks.hpp"
#include "tuning/generated_tuning_values.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace EvalTerms {
namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr const auto& EVAL_TUNING = Tuning::Generated::VALUES.evaluation;

int connectedPawnsBonusByRank(Color color, uint64_t ownPawns, const std::array<int, 9>& bonusByRank) {
    const uint64_t phalanx = ((ownPawns & ~Board::FILE_A) >> 1) | ((ownPawns & ~Board::FILE_H) << 1);
    uint64_t support = 0ULL;

    if (color == Color::White) {
        support = ((ownPawns & ~Board::FILE_A) << 7) | ((ownPawns & ~Board::FILE_H) << 9);
    } else {
        support = ((ownPawns & ~Board::FILE_A) >> 9) | ((ownPawns & ~Board::FILE_H) >> 7);
    }

    uint64_t connected = ownPawns & (phalanx | support);
    int bonus = 0;

    while (connected != 0ULL) {
        const int square = Eval::lsbIndex(connected);
        connected &= (connected - 1);

        const int rank = square >> 3;
        const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
        bonus += bonusByRank[static_cast<size_t>(std::clamp(relativeRank, 0, 8))];
    }

    return bonus;
}

uint64_t pawnAttacks(Color color, uint64_t pawns) {
    if (color == Color::White) {
        return ((pawns & ~Board::FILE_A) << 7) | ((pawns & ~Board::FILE_H) << 9);
    }
    return ((pawns & ~Board::FILE_A) >> 9) | ((pawns & ~Board::FILE_H) >> 7);
}

bool hasAdjacentPawnSupport(Color color, uint64_t ownPawns, int square) {
    const int rank = square >> 3;
    const int file = Eval::fileOf(square);

    for (int df = -1; df <= 1; df += 2) {
        const int adjFile = file + df;
        if (adjFile < 0 || adjFile > 7) {
            continue;
        }

        if (color == Color::White) {
            for (int r = 0; r <= rank; ++r) {
                if ((ownPawns & Eval::squareMask(r * 8 + adjFile)) != 0ULL) {
                    return true;
                }
            }
        } else {
            for (int r = 7; r >= rank; --r) {
                if ((ownPawns & Eval::squareMask(r * 8 + adjFile)) != 0ULL) {
                    return true;
                }
            }
        }
    }

    return false;
}

int candidatePawnsBonusByRank(Color color, uint64_t ownPawns, uint64_t enemyPawns, const std::array<int, 9>& bonusByRank) {
    int bonus = 0;
    uint64_t pawns = ownPawns;
    const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;

    while (pawns != 0ULL) {
        const int square = Eval::lsbIndex(pawns);
        pawns &= (pawns - 1);

        const uint64_t frontMask = EvalMask::MASKS.PASSED_PAWN_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(square)];
        if ((enemyPawns & frontMask) == 0ULL) {
            continue;
        }

        const uint64_t fileMask = EvalMask::MASKS.FILE_MASKS[static_cast<size_t>(Eval::fileOf(square))];
        const uint64_t sameFileFront = frontMask & fileMask;
        if ((enemyPawns & sameFileFront) != 0ULL) {
            continue;
        }

        const uint64_t adjacentFront = frontMask & ~fileMask;
        const int enemyAdjacent = std::popcount(enemyPawns & adjacentFront);
        if (enemyAdjacent > 1) {
            continue;
        }

        const bool hasSupport = hasAdjacentPawnSupport(color, ownPawns, square);
        if (!hasSupport && enemyAdjacent > 0) {
            continue;
        }

        const int rank = square >> 3;
        const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
        bonus += bonusByRank[static_cast<size_t>(std::clamp(relativeRank, 0, 8))];
    }

    return bonus;
}

int backwardPawnsPenaltyByRank(
    Color color,
    uint64_t ownPawns,
    uint64_t enemyPawns,
    uint64_t allOcc,
    const std::array<int, 9>& penaltyByRank
) {
    int penalty = 0;
    uint64_t pawns = ownPawns;
    const Color enemyColor = (color == Color::White) ? Color::Black : Color::White;
    const uint64_t enemyPawnAttackMap = pawnAttacks(enemyColor, enemyPawns);

    while (pawns != 0ULL) {
        const int square = Eval::lsbIndex(pawns);
        pawns &= (pawns - 1);

        if (hasAdjacentPawnSupport(color, ownPawns, square)) {
            continue;
        }

        const int advanceSquare = (color == Color::White) ? square + 8 : square - 8;
        if (advanceSquare < 0 || advanceSquare >= 64) {
            continue;
        }

        const uint64_t advanceMask = Eval::squareMask(advanceSquare);
        const bool blocked = (allOcc & advanceMask) != 0ULL;
        const bool pressured = (enemyPawnAttackMap & advanceMask) != 0ULL;
        if (!blocked && !pressured) {
            continue;
        }

        const int rank = square >> 3;
        const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
        penalty += penaltyByRank[static_cast<size_t>(std::clamp(relativeRank, 0, 8))];
    }

    return penalty;
}

int pawnIslands(uint64_t ownPawns) {
    unsigned int occupiedFiles = 0;
    for (int file = 0; file < 8; ++file) {
        if ((ownPawns & EvalMask::MASKS.FILE_MASKS[static_cast<size_t>(file)]) != 0ULL) {
            occupiedFiles |= (1U << file);
        }
    }

    int islands = 0;
    bool inIsland = false;
    for (int file = 0; file < 8; ++file) {
        const bool hasPawn = (occupiedFiles & (1U << file)) != 0U;
        if (hasPawn && !inIsland) {
            ++islands;
        }
        inIsland = hasPawn;
    }

    return islands;
}

} // namespace

int passedPawnCount(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
    int count = 0;
    uint64_t pawns = ownPawns;
    const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;

    while (pawns != 0ULL) {
        const int square = Eval::lsbIndex(pawns);
        pawns &= (pawns - 1);

        if ((enemyPawns & EvalMask::MASKS.PASSED_PAWN_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(square)]) == 0ULL) {
            ++count;
        }
    }

    return count;
}

int connectedPawnsBonus(Color color, uint64_t ownPawns) {
    return connectedPawnsBonusByRank(color, ownPawns, EVAL_TUNING.pawns.connectedMgByRank);
}

int connectedPawnsBonusEg(Color color, uint64_t ownPawns) {
    return connectedPawnsBonusByRank(color, ownPawns, EVAL_TUNING.pawns.connectedEgByRank);
}

int candidatePawnsBonus(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
    return candidatePawnsBonusByRank(color, ownPawns, enemyPawns, EVAL_TUNING.pawns.candidateMgByRank);
}

int candidatePawnsBonusEg(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
    return candidatePawnsBonusByRank(color, ownPawns, enemyPawns, EVAL_TUNING.pawns.candidateEgByRank);
}

int backwardPawnsPenalty(Color color, uint64_t ownPawns, uint64_t enemyPawns, uint64_t allOcc) {
    return backwardPawnsPenaltyByRank(color, ownPawns, enemyPawns, allOcc, EVAL_TUNING.pawns.backwardMgByRank);
}

int backwardPawnsPenaltyEg(Color color, uint64_t ownPawns, uint64_t enemyPawns, uint64_t allOcc) {
    return backwardPawnsPenaltyByRank(color, ownPawns, enemyPawns, allOcc, EVAL_TUNING.pawns.backwardEgByRank);
}

Eval::TaperTerms pawnIslandPenalty(uint64_t ownPawns) {
    const int islands = pawnIslands(ownPawns);
    const int extraIslands = std::max(0, islands - 1);
    return {
        extraIslands * EVAL_TUNING.pawns.islandPenaltyMg,
        extraIslands * EVAL_TUNING.pawns.islandPenaltyEg
    };
}

int pawnStructurePenalty(uint64_t ownPawns) {
    int penalty = 0;

    for (int file = 0; file < 8; ++file) {
        const uint64_t filePawns = ownPawns & EvalMask::MASKS.FILE_MASKS[static_cast<size_t>(file)];
        if (filePawns == 0ULL) {
            continue;
        }

        const int count = std::popcount(filePawns);
        if (count > 1) {
            penalty += (count - 1) * EVAL_TUNING.pawns.doubledPenalty;
        }

        uint64_t adjMask = 0ULL;
        if (file > 0) {
            adjMask |= EvalMask::MASKS.FILE_MASKS[static_cast<size_t>(file - 1)];
        }
        if (file < 7) {
            adjMask |= EvalMask::MASKS.FILE_MASKS[static_cast<size_t>(file + 1)];
        }

        if ((ownPawns & adjMask) == 0ULL) {
            penalty += count * EVAL_TUNING.pawns.isolatedPenalty;
        }
    }

    return penalty;
}

int passedPawnBonus(Color color, uint64_t ownPawns, uint64_t enemyPawns) {
    int bonus = 0;
    uint64_t pawns = ownPawns;

    while (pawns != 0ULL) {
        const int square = Eval::lsbIndex(pawns);
        pawns &= (pawns - 1);

        const int sq = square;
        const int file = Eval::fileOf(sq);
        const int rank = sq >> 3;

        uint64_t mask = 0ULL;
        const int startFile = std::max(0, file - 1);
        const int endFile = std::min(7, file + 1);

        if (color == Color::White) {
            for (int targetRank = rank + 1; targetRank < 8; ++targetRank) {
                for (int targetFile = startFile; targetFile <= endFile; ++targetFile) {
                    mask |= (1ULL << (targetRank * 8 + targetFile));
                }
            }
        } else {
            for (int targetRank = rank - 1; targetRank >= 0; --targetRank) {
                for (int targetFile = startFile; targetFile <= endFile; ++targetFile) {
                    mask |= (1ULL << (targetRank * 8 + targetFile));
                }
            }
        }

        if ((mask & enemyPawns) == 0ULL) {
            const int relativeRank = (color == Color::White) ? (rank + 1) : (8 - rank);
            int pawnBonus = (relativeRank * relativeRank) * EVAL_TUNING.pawns.passedRankSquareMultiplier;
            uint64_t forwardSq = (color == Color::White) ? (1ULL << (sq + 8)) : (1ULL << (sq - 8));
            uint64_t oppOcc = enemyPawns;

            if (forwardSq & oppOcc) {
                pawnBonus /= EVAL_TUNING.pawns.passedBlockedDivisor;
            }
            bonus += pawnBonus;
        }
    }

    return bonus;
}

} // namespace EvalTerms
