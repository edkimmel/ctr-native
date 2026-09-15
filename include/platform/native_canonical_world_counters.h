#ifndef PLATFORM_NATIVE_CANONICAL_WORLD_COUNTERS_H
#define PLATFORM_NATIVE_CANONICAL_WORLD_COUNTERS_H

#include "platform/native_canonical_codec.h"

/*
 * Future-only M2 WORLD counter foundation.  This fixture/fact codec is
 * deliberately detached from GameTracker and all live game ownership.  The
 * one counter models the later GameTracker.numMissiles source seam.
 *
 * Wire form (12 bytes, little-endian): version u32, flags u32,
 * activeBombMissileCount u32.  When unavailable, flags and count are both
 * exactly zero.  No native layout is serialized.
 */
#define NATIVE_CANONICAL_WORLD_COUNTERS_V1_VERSION UINT32_C(1)
#define NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE UINT32_C(1)
#define NATIVE_CANONICAL_WORLD_COUNTERS_V1_MAX_ACTIVE_BOMB_MISSILE_COUNT 12u
#define NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES 12u

/* Intentional source-only facts, not a GameTracker/native-memory adapter. */
struct NativeCanonicalWorldCountersV1Facts
{
	uint32_t flags;
	uint32_t activeBombMissileCount;
};

struct NativeCanonicalWorldCountersV1
{
	uint32_t version;
	uint32_t flags;
	uint32_t activeBombMissileCount;
};

void NativeCanonicalWorldCountersV1_Init(struct NativeCanonicalWorldCountersV1 *counters);
int NativeCanonicalWorldCountersV1_FromFacts(struct NativeCanonicalWorldCountersV1 *counters,
	const struct NativeCanonicalWorldCountersV1Facts *facts);
int NativeCanonicalWorldCountersV1_Validate(const struct NativeCanonicalWorldCountersV1 *counters);
size_t NativeCanonicalWorldCountersV1_EncodedSize(const struct NativeCanonicalWorldCountersV1 *counters);
int NativeCanonicalWorldCountersV1_Encode(struct NativeCodecWriter *writer,
	const struct NativeCanonicalWorldCountersV1 *counters);
int NativeCanonicalWorldCountersV1_Decode(struct NativeCodecReader *reader,
	struct NativeCanonicalWorldCountersV1 *counters);
/* FNV-1a 64 over precisely the explicit canonical little-endian bytes. */
int NativeCanonicalWorldCountersV1_Digest(const struct NativeCanonicalWorldCountersV1 *counters,
	uint64_t *digestOut);

#endif
