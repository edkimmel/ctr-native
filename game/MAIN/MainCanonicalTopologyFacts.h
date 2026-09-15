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

/* Converts borrowed fixture facts into the portable V1 summary.  On failure,
 * out is unchanged. */
int MainCanonicalTopologyFacts_ToV1(const struct MainCanonicalTopologyFacts *facts,
	struct NativeCanonicalTopologyV1 *out);

#endif
