#ifndef PLATFORM_NATIVE_CANONICAL_WORLD_MINE_REGISTRY_H
#define PLATFORM_NATIVE_CANONICAL_WORLD_MINE_REGISTRY_H

#include "platform/native_canonical_codec.h"

/*
 * Future-only M2 WORLD mine-registry foundation.  This is deliberately a
 * fixture/fact codec: it has no D231 extraction, game ownership, state,
 * replay, scheduler, or runtime dependency.  A supplied activeCapacity is
 * used (0..50), rather than assuming all 50 slots are live.  Only that active
 * prefix is encoded; the inactive tail is absent from the canonical bytes.
 *
 * A slot index is its stable identity.  A TAKEN slot stores its head-to-tail
 * queue rank.  Native links, addresses, and objects are never represented.
 */
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_VERSION UINT32_C(1)
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE UINT32_C(1)
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS 50u
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FREE UINT8_C(0)
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN UINT8_C(1)
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_NO_RANK UINT8_C(0xff)
#define NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_HEADER_BYTES 16u

struct NativeCanonicalWorldMineRegistryV1Record
{
	uint8_t state;
	uint8_t queueRank;
};

/* This is intentionally a fact-shaped input, not a native-memory adapter. */
struct NativeCanonicalWorldMineRegistryV1Facts
{
	uint32_t flags;
	uint32_t activeCapacity;
	uint32_t takenCount;
	struct NativeCanonicalWorldMineRegistryV1Record records[NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS];
};

struct NativeCanonicalWorldMineRegistryV1
{
	uint32_t version;
	uint32_t flags;
	uint32_t activeCapacity;
	uint32_t takenCount;
	struct NativeCanonicalWorldMineRegistryV1Record records[NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS];
};

void NativeCanonicalWorldMineRegistryV1_Init(struct NativeCanonicalWorldMineRegistryV1 *registry);
int NativeCanonicalWorldMineRegistryV1_FromFacts(struct NativeCanonicalWorldMineRegistryV1 *registry,
	const struct NativeCanonicalWorldMineRegistryV1Facts *facts);
int NativeCanonicalWorldMineRegistryV1_Validate(const struct NativeCanonicalWorldMineRegistryV1 *registry);
size_t NativeCanonicalWorldMineRegistryV1_EncodedSize(const struct NativeCanonicalWorldMineRegistryV1 *registry);
int NativeCanonicalWorldMineRegistryV1_Encode(struct NativeCodecWriter *writer,
	const struct NativeCanonicalWorldMineRegistryV1 *registry);
int NativeCanonicalWorldMineRegistryV1_Decode(struct NativeCodecReader *reader,
	struct NativeCanonicalWorldMineRegistryV1 *registry);
/* FNV-1a 64 over exactly the canonical encoded form; useful diagnostics only. */
int NativeCanonicalWorldMineRegistryV1_Digest(const struct NativeCanonicalWorldMineRegistryV1 *registry,
	uint64_t *digestOut);

#endif
