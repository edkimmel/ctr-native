#include "common.h"
#include "MainCanonicalWorldCounters.h"

int MainCanonicalWorldCounters_ExtractV1(const struct GameTracker *gGT,
	struct NativeCanonicalWorldCountersV1 *out)
{
	struct NativeCanonicalWorldCountersV1Facts facts;
	struct NativeCanonicalWorldCountersV1 candidate;

	if (!gGT || !out) return 0;

	facts.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
	facts.activeBombMissileCount = gGT->numMissiles;
	if (!NativeCanonicalWorldCountersV1_FromFacts(&candidate, &facts)) return 0;

	*out = candidate;
	return 1;
}
