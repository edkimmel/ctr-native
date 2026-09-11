#ifndef PLATFORM_NATIVE_CANONICAL_STATE_H
#define PLATFORM_NATIVE_CANONICAL_STATE_H

#include "platform/native_canonical_codec.h"
#include "platform/native_identity.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Caller-projected canonical values for the first M2 domains.  These types
 * intentionally have no SDL, game, checkpoint, native-state, pointer, or
 * native-layout dependency.  Only the field-wise encoding below is persisted.
 */

#define NATIVE_CANONICAL_STATE_V1_MAGIC UINT32_C(0x3156434e) /* Little-endian "NCV1". */
#define NATIVE_CANONICAL_INPUT_PAD_COUNT 4u
struct NativeCanonicalControlV1
{
	int32_t frameTimer;
	int32_t frameCounter;
	int32_t timer;
	int32_t framesInThisLEV;
	int32_t elapsedTimeMS;
	int32_t msInThisLEV;
	int32_t elapsedEventTime;
	int32_t mainGameState;
	int32_t loadingStage;
	int32_t levelID;
};

struct NativeCanonicalRngV1
{
	uint32_t mixRandomNumber;
	uint32_t deadcoed0;
	uint32_t deadcoed1;
	uint32_t advRng0;
	uint32_t advRng1;
	uint32_t psxRngSeed;
};

struct NativeCanonicalInputPadV1
{
	uint8_t status;
	uint8_t id;
	uint8_t buttons[2];
	uint8_t analog[4];
	uint8_t connected;
};

struct NativeCanonicalInputV1
{
	uint32_t padCount;
	struct NativeCanonicalInputPadV1 pads[NATIVE_CANONICAL_INPUT_PAD_COUNT];
};

struct NativeCanonicalStateV1
{
	uint32_t schemaVersion;
	uint32_t replayFormatVersion;
	uint32_t domainCount;
	uint32_t frameNumber;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 rng;
	struct NativeCanonicalInputV1 input;
	uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint64_t combinedDigest;
};

/*
 * Wire order is magic, schema version, replay format version, domain count,
 * frame number, 32-byte build identity, 32-byte content identity, then each ordered domain as (u32 id, u32 payload size,
 * payload bytes, u64 payload digest), followed by the combined digest.
 * Future domains use a zero payload size and the FNV-1a-64 offset digest.
 * The combined digest is FNV-1a 64 over ordered (u32 domain id, u64 digest)
 * pairs, all encoded little-endian.
 */
void NativeCanonicalStateV1_Init(struct NativeCanonicalStateV1 *state);
int NativeCanonicalStateV1_Validate(const struct NativeCanonicalStateV1 *state);
int NativeCanonicalStateV1_ComputeDigests(struct NativeCanonicalStateV1 *state);
size_t NativeCanonicalStateV1_EncodedSize(void);

/*
 * Encode/decode exactly one state record.  Both operations are transactional:
 * validation, bounds, schema, domain order, count, payload digest, and
 * combined digest failures leave the caller's state and codec offset unchanged.
 */
int NativeCanonicalStateV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalStateV1 *state);
int NativeCanonicalStateV1_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity,
                                  struct NativeCanonicalStateV1 *state);

#endif
