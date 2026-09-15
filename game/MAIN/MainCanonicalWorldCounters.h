#ifndef GAME_MAIN_CANONICAL_WORLD_COUNTERS_H
#define GAME_MAIN_CANONICAL_WORLD_COUNTERS_H

#include "platform/native_canonical_world_counters.h"

struct GameTracker;

/* Reads the game-owned missile count into the portable M2 WORLD counters
 * value.  A null game root is a source failure, not an unavailable snapshot. */
int MainCanonicalWorldCounters_ExtractV1(const struct GameTracker *gGT,
	struct NativeCanonicalWorldCountersV1 *out);

#endif
