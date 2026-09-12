#ifndef MAIN_CANONICAL_TOPOLOGY_H
#define MAIN_CANONICAL_TOPOLOGY_H

#include <common.h>
#include "platform/native_canonical_fixed_array.h"

/* Dormant topology ownership snapshot.  It is an internal lifetime guard for
 * future projectors, not a canonical value or a publisher payload. */
struct MainCanonicalTopologySnapshot
{
	const struct GameTracker *gGT;
	const struct sData *sourceData;
	const struct Level *level;
	const struct mesh_info *mesh;
	const struct Mempack *mempack;
	uintptr_t mempackBase;
	size_t mempackSpan;
	int32_t levelID;
	uint8_t mempackIndex;
	struct NativeCanonicalFixedArrayGeometry quadBlocks;
	uint64_t lifecycleEpoch;
	uint8_t valid;
};

/* Init starts a fresh lifecycle generation.  Invalidate permanently retires
 * the current capture and increments the generation without wrapping. */
void MainCanonicalTopology_Init(struct MainCanonicalTopologySnapshot *snapshot);
void MainCanonicalTopology_Invalidate(struct MainCanonicalTopologySnapshot *snapshot);

/* Capture and validate require sourceData->gGT == gGT.  Both establish the
 * active pack and prove all native spans before dereferencing level, mesh, or
 * QuadBlock storage.  Capture leaves snapshot unchanged on failure. */
int MainCanonicalTopology_Capture(struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData);
int MainCanonicalTopology_Validate(const struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData);

/* Validates the current snapshot before normalizing a QuadBlock pointer.  A
 * null pointer maps to UINT32_MAX; failure leaves indexOut unchanged. */
int MainCanonicalTopology_NullableQuadBlockIndex(const struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct QuadBlock *quadBlock,uint32_t *indexOut);

#endif
