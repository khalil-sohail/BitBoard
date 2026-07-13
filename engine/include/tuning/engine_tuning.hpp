#ifndef TUNING_ENGINE_TUNING_HPP
#define TUNING_ENGINE_TUNING_HPP

#include "tuning/evaluation_tuning.hpp"
#include "tuning/opening_tuning.hpp"
#include "tuning/search_tuning.hpp"
#include "tuning/time_tuning.hpp"

namespace Tuning {

struct EngineTuning {
    EvaluationTuning evaluation;
    SearchTuning search;
    TimeTuning time;
    OpeningTuning opening;
};

inline constexpr EngineTuning BUILTIN_DEFAULT_V1_MODEL = {
    .evaluation = {
        .material = {
            .middlegame = {150, 250, 284, 490, 839, 0},
            .endgame = {160, 200, 269, 650, 1150, 0},
        },
        .phase = {
            .increments = {0, 1, 1, 2, 4, 0},
        },
        .mobility = {
            .middlegame = {0, 3, 10, 8, 5, 0},
            .endgame = {0, 10, 10, 8, 8, 0},
        },
        .rookActivity = {
            .openFileMg = 40,
            .openFileEg = 40,
            .semiOpenFileMg = 30,
            .semiOpenFileEg = 0,
            .seventhRankMg = 50,
            .seventhRankEg = 50,
        },
        .bishopPair = {
            .middlegame = 70,
            .endgame = 80,
        },
        .pawns = {
            .connectedMgByRank = {0, 0, 0, 21, 60, 60, 60, 0, 0},
            .connectedEgByRank = {0, 0, 0, 0, 80, 80, 80, 80, 0},
            .candidateMgByRank = {0, 0, 0, 40, 40, 40, 40, 0, 0},
            .candidateEgByRank = {0, 0, 0, 50, 50, 50, 50, 0, 0},
            .backwardMgByRank = {0, 0, 40, 20, 10, 40, 0, 0, 0},
            .backwardEgByRank = {0, 0, 30, 30, 30, 0, 0, 0, 0},
            .doubledPenalty = 30,
            .isolatedPenalty = 30,
            .islandPenaltyMg = 20,
            .islandPenaltyEg = 20,
            .passedCountBonusMg = 0,
            .passedCountBonusEg = 6,
            .passedEgMultiplier = 4,
            .passedRankSquareMultiplier = 5,
            .passedBlockedDivisor = 2,
        },
        .kingSafety = {
            .attackPressure = {0, 40, 165, 400, 400, 400, 160, 200, 240},
            .shieldMaxPawns = 3,
            .shieldPerPawnBonus = 27,
            .uncastledCenterPenalty = 50,
            .uncastledLostRightsPenalty = 66,
        },
        .piecePlacement = {
            .badBishopHeavyPenalty = 100,
            .badBishopLightPenalty = 60,
            .earlyQueenUndevelopedMinorPenalty = 50,
            .trappedRookPenalty = 10,
        },
        .endgame = {
            .taperScale = 128,
            .latePhaseMax = 8,
            .mopUpEgMargin = 220,
            .mopUpMaterialMargin = 350,
            .scaleOppositeBishopsMinPawns = 56,
            .scaleOppositeBishopsLowPawns = 72,
            .scaleMinorOnlyNearEqual = 64,
            .scaleMinorOnlyClearEdge = 80,
            .mopUpWeights = {15, 10, 20, 7, 10, 25, 12},
        },
        .pieceSquare = {
            .middlegameRepresented = true,
            .endgameRepresented = true,
        },
    },
    .search = {
        .aspiration = {
            .windowCp = 75,
        },
        .nullMove = {
            .reduction = 2,
        },
        .futility = {
            .reverseMarginPerDepthCp = 80,
            .forwardMarginPerDepthCp = 100,
        },
        .lateMoveReduction = {
            .base = {3, 4},
        },
        .quiescence = {
            .deltaMarginCp = 260,
        },
        .singularExtension = {
            .marginCp = 100,
        },
        .moveOrdering = {
            .transpositionMoveScore = 1'000'000,
            .quiet = {
                .firstKillerScore = 900'000,
                .secondKillerScore = 850'000,
                .counterMoveScore = 50'000,
            },
            .capture = {
                .winningCaptureBaseScore = 800'000,
                .losingCaptureBaseScore = -100'000,
                .seeScoreMultiplier = 10,
            },
            .promotionBaseScore = 700,
            .historyLimit = 16'384,
            .mvvLva = {{
                {{105, 205, 305, 405, 505, 605}},
                {{104, 204, 304, 404, 504, 604}},
                {{103, 203, 303, 403, 503, 603}},
                {{102, 202, 302, 402, 502, 602}},
                {{101, 201, 301, 401, 501, 601}},
                {{100, 200, 300, 400, 500, 600}},
            }},
            .seePieceValues = {100, 320, 330, 500, 900, 0},
        },
    },
    .time = {
        .allocation = {
            .safetyReserveMs = 30,
            .minimumMoveTimeMs = 10,
            .expectedMovesBase = 40,
            .expectedMovesFloor = 20,
            .incrementContribution = {1, 1},
            .instabilityThresholdCp = 50,
            .instabilityMultiplier = {13, 10},
            .maximumClockFraction = {1, 4},
        },
        .stopPolicy = {
            .stableSoftStopFraction = {1, 2},
            .unstableSoftStopFraction = {4, 5},
            .hardStopFraction = {3, 4},
            .criticalLowTimeThresholdMs = 40,
            .criticalLowTimeReserveMs = 5,
        },
        .polling = {
            .nodeMask = 8191ULL,
        },
    },
    .opening = {
        .enabled = true,
        .depthPlies = 30,
        .selectionMode = BookSelectionMode::Weighted,
        .selectionTopN = 4,
        .seed = 1592594996U,
    },
};

} // namespace Tuning

#endif
