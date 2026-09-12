#ifndef PLATFORM_NATIVE_CANONICAL_STATE_V3_H
#define PLATFORM_NATIVE_CANONICAL_STATE_V3_H

#include "platform/native_canonical_drivers.h"
#include "platform/native_canonical_state.h"

/* v3 is intentionally a new wire type. NativeCanonicalStateV1 remains the
 * sealed schema-2/v2 compatibility codec. */
#define NATIVE_CANONICAL_STATE_V3_MAGIC UINT32_C(0x3356434e) /* Little-endian "NCV3". */
#define NATIVE_CANONICAL_STATE_V3_SCHEMA_VERSION UINT32_C(3)
#define NATIVE_CANONICAL_REPLAY_V3_FORMAT_VERSION UINT32_C(3)

struct NativeCanonicalStateV3
{
	uint32_t schemaVersion;
	uint32_t replayFormatVersion;
	uint32_t domainCount;
	uint32_t frameNumber;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 rng;
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalDriversV1 drivers;
	uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint64_t combinedDigest;
};

void NativeCanonicalStateV3_Init(struct NativeCanonicalStateV3 *state);
int NativeCanonicalStateV3_Validate(const struct NativeCanonicalStateV3 *state);
int NativeCanonicalStateV3_ComputeDigests(struct NativeCanonicalStateV3 *state);
size_t NativeCanonicalStateV3_EncodedSize(void);
int NativeCanonicalStateV3_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalStateV3 *state);
int NativeCanonicalStateV3_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity,
                                  struct NativeCanonicalStateV3 *state);

#endif
