#ifndef PLATFORM_NATIVE_CANONICAL_STATE_V4_H
#define PLATFORM_NATIVE_CANONICAL_STATE_V4_H

#include "platform/native_canonical_drivers.h"
#include "platform/native_canonical_state.h"
#include "platform/native_canonical_topology.h"
#include "platform/native_canonical_world_counters.h"
#include "platform/native_canonical_world_mine_registry.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_sha256.h"

/* A sibling wire type: V1/V3 bytes and constants remain sealed. */
#define NATIVE_CANONICAL_STATE_V4_MAGIC UINT32_C(0x3456434e) /* "NCV4" */
#define NATIVE_CANONICAL_STATE_V4_SCHEMA_VERSION UINT32_C(5)
#define NATIVE_CANONICAL_REPLAY_V4_FORMAT_VERSION UINT32_C(4)
#define NATIVE_CANONICAL_RNG_V2_VERSION UINT32_C(2)
#define NATIVE_CANONICAL_WORLD_V1_VERSION UINT32_C(1)
#define NATIVE_CANONICAL_WORLD_V1_COUNTERS_COMPONENT UINT32_C(1)
#define NATIVE_CANONICAL_WORLD_V1_MINE_REGISTRY_COMPONENT UINT32_C(2)
#define NATIVE_CANONICAL_WORLD_V1_MINE_SLOT_BYTES 116u

struct NativeCanonicalStateV4 {
	uint32_t schemaVersion, replayFormatVersion, domainCount, frameNumber;
	struct NativeIdentityV1 identity;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 retailRng;
	struct NativeDeterministicRngBankV1 deterministicRng;
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalDriversV1 drivers;
	struct NativeCanonicalWorldCountersV1 worldCounters;
	struct NativeCanonicalWorldMineRegistryV1 mineRegistry;
	struct NativeCanonicalTopologyV1 topology;
	uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT], combinedDigest;
};

void NativeCanonicalStateV4_Init(struct NativeCanonicalStateV4 *state);
int NativeCanonicalStateV4_Validate(const struct NativeCanonicalStateV4 *state);
int NativeCanonicalStateV4_ComputeDigests(struct NativeCanonicalStateV4 *state);
size_t NativeCanonicalStateV4_EncodedSize(void);
int NativeCanonicalStateV4_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalStateV4 *state);
int NativeCanonicalStateV4_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity,
	const uint8_t expectedConfigDigest[NATIVE_SHA256_DIGEST_BYTES], struct NativeCanonicalStateV4 *state);

#endif
