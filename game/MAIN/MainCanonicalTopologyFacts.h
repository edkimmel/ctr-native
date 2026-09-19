#ifndef GAME_MAIN_CANONICAL_TOPOLOGY_FACTS_H
#define GAME_MAIN_CANONICAL_TOPOLOGY_FACTS_H

#include "platform/native_canonical_topology.h"

#include <stddef.h>
#include <stdint.h>

/* Fixture-facing, pointer-free elements.  Ranges borrow their item storage for
 * this call only; the adapter neither retains nor dereferences game state. */
struct MainCanonicalTopologyQuadCheckpointFact
{
	uint8_t restartIndex;
};

struct MainCanonicalTopologyRestartFact
{
	int16_t pos[3];
	uint16_t distance;
	uint8_t edgeIndex[4]; /* forward, left, backward, right */
};

struct MainCanonicalTopologyNavFrameFact
{
	int16_t pos[3];
	uint8_t rot[4];
	int16_t distanceXYZ;
	int16_t distanceXZ;
	int16_t flags;
	int16_t pathChangeOpcode;
	uint8_t goBackCount;
	uint8_t specialBits;
};

struct MainCanonicalTopologyQuadCheckpointFacts
{
	const struct MainCanonicalTopologyQuadCheckpointFact *items;
	uint32_t count;
};

struct MainCanonicalTopologyRestartFacts
{
	const struct MainCanonicalTopologyRestartFact *items;
	uint32_t count;
};

struct MainCanonicalTopologyNavFrameFacts
{
	const struct MainCanonicalTopologyNavFrameFact *items;
	uint32_t count;
};

struct MainCanonicalTopologyNavPathFacts
{
	int32_t firstNodeY;
	int16_t rampPhys1[16];
	int16_t rampPhys2[16];
	struct MainCanonicalTopologyNavFrameFacts frames;
};

struct MainCanonicalTopologyFacts
{
	uint32_t flags;
	int32_t levelID;
	struct MainCanonicalTopologyQuadCheckpointFacts quadCheckpoints;
	struct MainCanonicalTopologyRestartFacts restarts;
	struct MainCanonicalTopologyNavPathFacts navPaths[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
};

/* A bounded, indexed view of topology facts.  The callbacks are invoked at
 * most once for each index, in canonical stream order.  They must copy one
 * value into out and return nonzero; a failed callback leaves the V1 output
 * unchanged.  The reader never retains callback output or game pointers.
 * Reader metadata, context, and every callback-provided fact must remain
 * stable for the full call; this function supplies output transactionality,
 * not a live snapshot or lifetime guarantee. */
typedef int (*MainCanonicalTopologyReadQuadCheckpointFn)(const void *context,
	uint32_t index,struct MainCanonicalTopologyQuadCheckpointFact *out);
typedef int (*MainCanonicalTopologyReadRestartFn)(const void *context,
	uint32_t index,struct MainCanonicalTopologyRestartFact *out);
typedef int (*MainCanonicalTopologyReadNavFrameFn)(const void *context,
	uint32_t path,uint32_t index,struct MainCanonicalTopologyNavFrameFact *out);

struct MainCanonicalTopologyNavPathReader
{
	int32_t firstNodeY;
	int16_t rampPhys1[16];
	int16_t rampPhys2[16];
	uint32_t frameCount;
};

struct MainCanonicalTopologyFactReader
{
	uint32_t flags;
	int32_t levelID;
	uint32_t quadCount;
	uint32_t restartCount;
	struct MainCanonicalTopologyNavPathReader navPaths[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
	const void *context;
	MainCanonicalTopologyReadQuadCheckpointFn readQuadCheckpoint;
	MainCanonicalTopologyReadRestartFn readRestart;
	MainCanonicalTopologyReadNavFrameFn readNavFrame;
};

/* Builds V1 from bounded indexed callbacks without materializing the quad,
 * restart, or nav-frame ranges.  On failure, out is unchanged. */
int MainCanonicalTopologyFactReader_ToV1(const struct MainCanonicalTopologyFactReader *reader,
	struct NativeCanonicalTopologyV1 *out);

/* Converts borrowed fixture facts into the portable V1 summary.  On failure,
 * out is unchanged.  This is a range-backed wrapper around the reader API. */
int MainCanonicalTopologyFacts_ToV1(const struct MainCanonicalTopologyFacts *facts,
	struct NativeCanonicalTopologyV1 *out);

#endif
