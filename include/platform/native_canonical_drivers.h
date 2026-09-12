#ifndef PLATFORM_NATIVE_CANONICAL_DRIVERS_H
#define PLATFORM_NATIVE_CANONICAL_DRIVERS_H

#include "platform/native_canonical_codec.h"

#include <stddef.h>
#include <stdint.h>

/* Pointer-free synthetic normative stream contract.  Live Driver projection is
 * deliberately a later audit: callers supply only explicit bytes here. */
#define NATIVE_CANONICAL_DRIVERS_VERSION 1u
#define NATIVE_CANONICAL_DRIVERS_SLOT_COUNT 8u
#define NATIVE_CANONICAL_DRIVERS_GROUP_COUNT 3u
#define NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES 64u
#define NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES 100u
#define NATIVE_CANONICAL_DRIVERS_PHYSICS_DYNAMICS_BYTES 264u
#define NATIVE_CANONICAL_DRIVERS_BEHAVIOR_BOT_BYTES 156u
#define NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES 520u
#define NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES 4224u
#define NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES 288u

struct NativeCanonicalDriversSlotSummary
{
	uint64_t slotDigest;
	uint64_t metaRaceDigest;
	uint64_t physicsDynamicsDigest;
	uint64_t behaviorBotDigest;
};

struct NativeCanonicalDriversV1
{
	uint32_t version;
	uint32_t slotCount;
	uint32_t presenceMask;
	uint32_t groupCount;
	uint64_t rosterMetaDigest;
	struct NativeCanonicalDriversSlotSummary slots[NATIVE_CANONICAL_DRIVERS_SLOT_COUNT];
	uint64_t fullStreamDigest;
};

struct NativeCanonicalDriversCompareMask
{
	uint32_t headerMask;
	uint32_t rosterMask;
	uint32_t slotMask;
	uint32_t metaRaceMask;
	uint32_t physicsDynamicsMask;
	uint32_t behaviorBotMask;
	uint32_t fullStreamMask;
};

void NativeCanonicalDriversV1_Init(struct NativeCanonicalDriversV1 *drivers);
/* Derives every summary value from a 64 + eight*520 normative byte stream.
 * Absent slots must contain the canonical all-zero 520-byte slot stream. */
int NativeCanonicalDriversV1_FromNormativeStream(struct NativeCanonicalDriversV1 *drivers, uint32_t presenceMask,
                                                 const uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]);
int NativeCanonicalDriversV1_Validate(const struct NativeCanonicalDriversV1 *drivers);
size_t NativeCanonicalDriversV1_EncodedSize(void);
int NativeCanonicalDriversV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalDriversV1 *drivers);
int NativeCanonicalDriversV1_Decode(struct NativeCodecReader *reader, struct NativeCanonicalDriversV1 *drivers);
/* Returns a fieldwise diagnostic mask; zero means exact summary equality. */
uint32_t NativeCanonicalDriversV1_Compare(const struct NativeCanonicalDriversV1 *expected,
                                          const struct NativeCanonicalDriversV1 *actual,
                                          struct NativeCanonicalDriversCompareMask *masks);

#endif
