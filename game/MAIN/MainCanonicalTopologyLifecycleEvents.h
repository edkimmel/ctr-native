#ifndef GAME_MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_H
#define GAME_MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_H

#include <stdint.h>

/*
 * Value-only lifecycle observation log. The private retirement owner may
 * append audited source-boundary observations, but this module itself does
 * not own lifecycle state, read game state, activate or capture topology,
 * publish a value, or establish any network or simulation-time boundary.
 *
 * The owner records source observations in their encountered order. In
 * particular, this module deliberately neither
 * coalesces nor rejects an ARENA_RESET followed by FULL_LOAD; the cold-load
 * coalescing policy remains an integration decision to be proved at real
 * source call sites. opaqueSourceStep is caller-provided provenance only. It
 * is not interpreted as a frame, VBlank, tick, clock, or generation value.
 */

#define MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_VERSION UINT32_C(1)
#define MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY UINT32_C(16)

enum MainCanonicalTopologyLifecycleEventKind
{
	/* Observation after cold game-state construction, not an authority call. */
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_CONSTRUCTED = 1,
	/* Observation of a source mutation boundary. */
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION = 2,
	/* Observation of a source post-initialization point. */
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT = 3
};

enum MainCanonicalTopologyLifecycleMutationReason
{
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_NONE = 0,
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_COLD_BOOT = 1,
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_TEST = 2,
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD = 3,
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_HUB_SWAP = 4,
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_CHECKPOINT_RESTORE = 5,
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_ARENA_RESET = 6,
	/* StateZero destroys the game tracker before the later physical arena wipe. */
	MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_GAME_TRACKER_ZERO = 7
};

struct MainCanonicalTopologyLifecycleEventV1
{
	uint32_t kind;
	uint32_t reason;
	uint64_t opaqueSourceStep;
};

struct MainCanonicalTopologyLifecycleEventsV1
{
	uint32_t version;
	uint32_t count;
	struct MainCanonicalTopologyLifecycleEventV1 events[MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY];
};

/* Initializes a caller-owned recorder with no observations. */
void MainCanonicalTopologyLifecycleEventsV1_Init(struct MainCanonicalTopologyLifecycleEventsV1 *recorder);

/* Checks the complete, fixed-capacity value shape, including the zero tail. */
int MainCanonicalTopologyLifecycleEventsV1_Validate(const struct MainCanonicalTopologyLifecycleEventsV1 *recorder);

/*
 * Appends one or more caller-owned event values in supplied order. The entire
 * batch is accepted or rejected: malformed input, an invalid existing log, or
 * insufficient capacity leaves recorder byte-for-byte unchanged. The source
 * array is read during this call only and no pointer is retained.
 */
int MainCanonicalTopologyLifecycleEventsV1_Append(
	struct MainCanonicalTopologyLifecycleEventsV1 *recorder,
	const struct MainCanonicalTopologyLifecycleEventV1 *events, uint32_t count);

#endif
