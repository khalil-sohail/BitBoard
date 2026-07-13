#pragma once

// Candidate builds may select an isolated generated header at compile time.
// The default remains the canonical builtin header.
#ifndef BITBOARD_TUNING_HEADER
#define BITBOARD_TUNING_HEADER "tuning/generated_tuning_values.hpp"
#endif

#include BITBOARD_TUNING_HEADER
