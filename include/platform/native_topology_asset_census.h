#ifndef PLATFORM_NATIVE_TOPOLOGY_ASSET_CENSUS_H
#define PLATFORM_NATIVE_TOPOLOGY_ASSET_CENSUS_H

#include <stddef.h>
#include <stdint.h>
#include "platform/native_canonical_topology.h"

#define NATIVE_TOPOLOGY_ASSET_CENSUS_VERSION 1u

/* The census reports only derived facts.  It neither retains nor emits asset
 * bytes.  `stored` means the BIG entry exists; `selectorAddressable` means the
 * retail LOD1 LEV/PTR selector pair was valid and decoded. */
struct NativeTopologyAssetCensusRecord
{
	uint32_t levelID;
	uint8_t stored;
	uint8_t selectorAddressable;
	uint8_t restartAvailable;
	uint8_t navAvailableMask;
	struct NativeCanonicalTopologyV1 topology;
};

struct NativeTopologyAssetCensus
{
	uint32_t version;
	uint32_t corpusCount;
	uint32_t storedCount;
	uint32_t selectorAddressableCount;
	uint32_t excludedStoredEntryCount;
	uint64_t corpusDigest;
	struct NativeTopologyAssetCensusRecord records[25];
};

/* Parse a raw MODE2/2352 ISO image. expectedSha256 must be exactly 32 bytes;
 * the caller supplies it explicitly, so a retail image is never accepted by
 * path convention or cabinet configuration. */
int NativeTopologyAssetCensus_FromRawMode2(const uint8_t *raw, size_t rawSize,
	const uint8_t expectedSha256[32], struct NativeTopologyAssetCensus *out);

/* Canonical JSON contains facts/digests only. Returns false without changing
 * output when the buffer is too small. */
int NativeTopologyAssetCensus_ToJson(const struct NativeTopologyAssetCensus *census,
	char *out, size_t outSize, size_t *written);

#endif
