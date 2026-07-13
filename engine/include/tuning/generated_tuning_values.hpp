// Generated from the canonical tuning profile.
// Do not edit manually.
// Regenerate with tools/tuning/generate_tuning_header.py.

#pragma once

#include "tuning/engine_tuning.hpp"

#include <cstddef>
#include <string_view>

namespace Tuning::Generated {

inline constexpr std::string_view PROFILE_ID = "builtin-default-v1";
inline constexpr std::string_view PROFILE_HASH = "sha256:55a1ac92352bd018460f115cb5061c76140f1eed453afc8a229ed3fa84145718";
inline constexpr int PROFILE_SCHEMA_VERSION = 1;
inline constexpr int REGISTRY_VERSION = 1;
inline constexpr std::string_view MODEL_VERSION = "phase-2-typed-model-v1";
inline constexpr std::string_view SOURCE_BASELINE = "builtin-default-v1";
inline constexpr std::size_t GENERATED_PROFILE_ENTRY_COUNT = 76;
inline constexpr std::size_t GENERATED_GROUPED_ARRAY_TABLE_COUNT = 19;

inline constexpr PieceSquareTable MIDDLEGAME_PIECE_SQUARE_TABLE = {
    {
        {{0, 0, 0, 0, 0, 0, 0, 0, 98, 134, 61, 95, 68, 126, 34, -11, -6, 7, 26, 31, 65, 56, 25, -20, -14, 13, 6, 21, 23, 12, 17, -23, -27, -2, -5, 12, 17, 6, 10, -25, -26, -4, -4, -10, 3, 3, 33, -12, -35, -1, -20, -23, -15, 24, 38, -22, 0, 0, 0, 0, 0, 0, 0, 0}},
        {{-167, -89, -34, -49, 61, -97, -15, -107, -73, -41, 72, 36, 23, 62, 7, -17, -47, 60, 37, 65, 84, 129, 73, 44, -9, 17, 19, 53, 37, 69, 18, 22, -13, 4, 16, 13, 28, 19, 21, -8, -23, -9, 12, 10, 19, 17, 25, -16, -29, -53, -12, -3, -1, 18, -14, -19, -105, -21, -58, -33, -17, -28, -19, -23}},
        {{-29, 4, -82, -37, -25, -42, 7, -8, -26, 16, -18, -13, 30, 59, 18, -47, -16, 37, 43, 40, 35, 50, 37, -2, -4, 5, 19, 50, 37, 37, 7, -2, -6, 13, 13, 26, 34, 12, 10, 4, 0, 15, 15, 15, 14, 27, 18, 10, 4, 15, 16, 0, 7, 21, 33, 1, -33, -3, -14, -21, -13, -12, -39, -21}},
        {{32, 42, 32, 51, 63, 9, 31, 43, 27, 32, 58, 62, 80, 67, 26, 44, -5, 19, 26, 36, 17, 45, 61, 16, -24, -11, 7, 26, 24, 35, -8, -20, -36, -26, -12, -1, 9, -7, 6, -23, -45, -25, -16, -17, 3, 0, -5, -33, -44, -16, -20, -9, -1, 11, -6, -71, -19, -13, 1, 17, 16, 7, -37, -26}},
        {{-28, 0, 29, 12, 59, 44, 43, 45, -24, -39, -5, 1, -16, 57, 28, 54, -13, -17, 7, 8, 29, 56, 47, 57, -27, -27, -16, -16, -1, 17, -2, 1, -9, -26, -9, -10, -2, -4, 3, -3, -14, 2, -11, -2, -5, 2, 14, 5, -35, -8, 11, 2, 8, 15, -3, 1, -1, -18, -9, 10, -15, -25, -31, -50}},
        {{-65, 23, 16, -15, -56, -34, 2, 13, 29, -1, -20, -7, -8, -4, -38, -29, -9, 24, 2, -16, -20, 6, 22, -22, -17, -20, -12, -27, -30, -25, -14, -36, -49, -1, -27, -39, -46, -44, -33, -51, -14, -14, -22, -46, -44, -30, -15, -27, 1, 7, -8, -64, -43, -16, 9, 8, -15, 36, 12, -54, 8, -28, 24, 14}},
    }
};

inline constexpr PieceSquareTable ENDGAME_PIECE_SQUARE_TABLE = {
    {
        {{0, 0, 0, 0, 0, 0, 0, 0, 178, 173, 158, 134, 147, 132, 165, 187, 94, 100, 85, 67, 56, 53, 82, 84, 32, 24, 13, 5, -2, 4, 17, 17, 13, 9, -3, -7, -7, -8, 3, -1, 4, 7, -6, 1, 0, -5, -1, -8, 13, 8, 8, 10, 13, 0, 2, -7, 0, 0, 0, 0, 0, 0, 0, 0}},
        {{-58, -38, -13, -28, -31, -27, -63, -99, -25, -8, -25, -2, -9, -25, -24, -52, -24, -20, 10, 9, -1, -9, -19, -41, -17, 3, 22, 22, 22, 11, 8, -18, -18, -6, 16, 25, 16, 17, 4, -18, -23, -3, -1, 15, 10, -3, -20, -22, -42, -20, -10, -5, -2, -20, -23, -44, -29, -51, -23, -15, -22, -18, -50, -64}},
        {{-14, -21, -11, -8, -7, -9, -17, -24, -8, -4, 7, -12, -3, -13, -4, -14, 2, -8, 0, -1, -2, 6, 0, 4, -3, 9, 12, 9, 14, 10, 3, 2, -6, 3, 13, 19, 7, 10, -3, -9, -12, -3, 8, 10, 13, 3, -7, -15, -14, -18, -7, -1, 4, -9, -15, -27, -23, -9, -23, -5, -9, -16, -5, -17}},
        {{13, 10, 18, 15, 12, 12, 8, 5, 11, 13, 13, 11, -3, 3, 8, 3, 7, 7, 7, 5, 4, -3, -5, -3, 4, 3, 13, 1, 2, 1, -1, 2, 3, 5, 8, 4, -5, -6, -8, -11, -4, 0, -5, -1, -7, -12, -8, -16, -6, -6, 0, 2, -9, -9, -11, -3, -9, 2, 3, -1, -5, -13, 4, -20}},
        {{-9, 22, 22, 27, 27, 19, 10, 20, -17, 20, 32, 41, 58, 25, 30, 0, -20, 6, 9, 49, 47, 35, 19, 9, 3, 22, 24, 45, 57, 40, 57, 36, -18, 28, 19, 47, 31, 34, 39, 23, -16, -27, 15, 6, 9, 17, 10, 5, -22, -23, -30, -16, -16, -23, -36, -32, -33, -28, -22, -43, -5, -32, -20, -41}},
        {{-74, -35, -18, -18, -11, 15, 4, -17, -12, 17, 14, 17, 17, 38, 23, 11, 10, 17, 23, 15, 20, 45, 44, 13, -8, 22, 24, 27, 26, 33, 26, 3, -18, -4, 21, 24, 27, 23, 9, -11, -19, -3, 11, 21, 23, 16, 7, -9, -27, -11, 4, 13, 14, 4, -5, -17, -53, -34, -21, -11, -28, -14, -24, -43}},
    }
};

inline constexpr EngineTuning VALUES = {
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
            .base = RationalValue<int>{3, 4},
        },
        .quiescence = {
            .deltaMarginCp = 260,
        },
        .singularExtension = {
            .marginCp = 100,
        },
        .moveOrdering = {
            .transpositionMoveScore = 1000000,
            .quiet = {
                .firstKillerScore = 900000,
                .secondKillerScore = 850000,
                .counterMoveScore = 50000,
            },
            .capture = {
                .winningCaptureBaseScore = 800000,
                .losingCaptureBaseScore = -100000,
                .seeScoreMultiplier = 10,
            },
            .promotionBaseScore = 700,
            .historyLimit = 16384,
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
            .incrementContribution = RationalValue<int>{1, 1},
            .instabilityThresholdCp = 50,
            .instabilityMultiplier = RationalValue<int>{13, 10},
            .maximumClockFraction = RationalValue<int>{1, 4},
        },
        .stopPolicy = {
            .stableSoftStopFraction = RationalValue<int>{1, 2},
            .unstableSoftStopFraction = RationalValue<int>{4, 5},
            .hardStopFraction = RationalValue<int>{3, 4},
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
        .selectionTopN = 4U,
        .seed = 1592594996U,
    },
};

static_assert(PROFILE_SCHEMA_VERSION == 1);
static_assert(!PROFILE_ID.empty());
static_assert(!PROFILE_HASH.empty());
static_assert(MIDDLEGAME_PIECE_SQUARE_TABLE.size() == kPieceTypeCount);
static_assert(MIDDLEGAME_PIECE_SQUARE_TABLE[0].size() == kBoardSquareCount);
static_assert(ENDGAME_PIECE_SQUARE_TABLE.size() == kPieceTypeCount);
static_assert(ENDGAME_PIECE_SQUARE_TABLE[0].size() == kBoardSquareCount);

} // namespace Tuning::Generated
