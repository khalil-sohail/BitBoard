#include "eval/eval_terms.hpp"
#include "eval/eval_masks.hpp"
#include "tuning/active_tuning_values.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace EvalTerms {
namespace {

constexpr int WHITE_IDX = static_cast<int>(Color::White);
constexpr int BLACK_IDX = static_cast<int>(Color::Black);
constexpr const auto& KING_SAFETY = Tuning::Generated::VALUES.evaluation.kingSafety;

} // namespace

int kingAttackPressure(
    Color color,
    int kingSquare,
    uint64_t enemyKnights,
    uint64_t enemyBishops,
    uint64_t enemyRooks,
    uint64_t enemyQueens,
    uint64_t allOcc
) {
    const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;
    const uint64_t kingZone = EvalMask::MASKS.KING_SHIELD_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(kingSquare)];

    if (kingZone == 0ULL) {
        return 0;
    }

    int attackers = 0;

    uint64_t bb = enemyKnights;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);
        if ((knightAttacks(square) & kingZone) != 0ULL) {
            ++attackers;
        }
    }

    bb = enemyBishops;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);
        if ((bishopAttacks(square, allOcc) & kingZone) != 0ULL) {
            ++attackers;
        }
    }

    bb = enemyRooks;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);
        if ((rookAttacks(square, allOcc) & kingZone) != 0ULL) {
            ++attackers;
        }
    }

    bb = enemyQueens;
    while (bb != 0ULL) {
        const int square = Eval::lsbIndex(bb);
        bb &= (bb - 1);
        const uint64_t attacks = bishopAttacks(square, allOcc) | rookAttacks(square, allOcc);
        if ((attacks & kingZone) != 0ULL) {
            ++attackers;
        }
    }

    const size_t idx = std::min(static_cast<size_t>(attackers), KING_SAFETY.attackPressure.size() - 1);
    return KING_SAFETY.attackPressure[idx];
}

int kingPawnShieldBonus(Color color, int kingSquare, uint64_t ownPawns) {
    const int colorIdx = (color == Color::White) ? WHITE_IDX : BLACK_IDX;
    const uint64_t shield = EvalMask::MASKS.KING_SHIELD_MASKS[static_cast<size_t>(colorIdx)][static_cast<size_t>(kingSquare)] & ownPawns;
    const int count = std::popcount(shield);
    return std::min(count, KING_SAFETY.shieldMaxPawns) * KING_SAFETY.shieldPerPawnBonus;
}

} // namespace EvalTerms
