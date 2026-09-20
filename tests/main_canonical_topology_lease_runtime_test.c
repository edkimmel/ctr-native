#include "MAIN/MainCanonicalTopologyLeaseRuntime.h"
#include "platform/native_topology_lease_runtime.h"

#include <stdio.h>
#include <string.h>

/* Keep the game-layout global and both game-owned sources in this one test
 * translation unit, as the authority fixture does. */
struct sData sdata_static;
#include "../game/MAIN/MainCanonicalTopologyLeaseAuthority.c"
#include "../game/MAIN/MainCanonicalTopologyLeaseRuntime.c"

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                          \
	{                                                                                                                                           \
		if (!(expression))                                                                                                                        \
		{                                                                                                                                         \
			fprintf(stderr, "%s:%d: check failed: %s\\n", __FILE__, __LINE__, #expression);                                                   \
			return 1;                                                                                                                               \
		}                                                                                                                                         \
	} while (0)

static int CheckMutation(const struct MainCanonicalTopologyLeaseRuntimeEvidence *evidence, uint32_t index, uint32_t reason)
{
	CHECK(index < evidence->events.count);
	CHECK(evidence->events.events[index].kind == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION);
	CHECK(evidence->events.events[index].reason == reason);
	return 1;
}

static int TestFullLoadWithoutArenaResetIsNormal(void)
{
	struct MainCanonicalTopologyLeaseRuntimeEvidence evidence;

	memset(&evidence, 0xa5, sizeof(evidence));
	CHECK(!MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));

	MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.version == MAIN_CANONICAL_TOPOLOGY_LEASE_RUNTIME_EVIDENCE_VERSION);
	CHECK(evidence.authority.epoch == 1u && evidence.authority.retired == 0u);
	CHECK(evidence.events.count == 1u);
	CHECK(evidence.events.events[0].kind == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_CONSTRUCTED);
	CHECK(evidence.events.events[0].reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_COLD_BOOT);

	/* A direct full-load call without the real arena-wipe hook is never cold
	 * coalesced, even immediately after construction. */
	MainCanonicalTopologyLeaseRuntime_BeforeFullLoad();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == 2u && evidence.authority.retired == 1u);
	CHECK(evidence.authority.retireReason == MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
	CHECK(evidence.coldLoadCoalescedCount == 0u);
	CHECK(evidence.events.count == 2u && CheckMutation(&evidence, 1u, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD));
	return 1;
}

static int TestColdLoadCoalescesOnlyAfterArenaReset(void)
{
	struct MainCanonicalTopologyLeaseRuntimeEvidence evidence;
	struct MainCanonicalTopologyLeaseRuntimeEvidence before;

	/* A unit fixture may reset the process-local owner between independent
	 * source-order scenarios; production StateZero never takes this path. */
	memset(&s_runtime, 0, sizeof(s_runtime));
	MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero();
	MainCanonicalTopologyLeaseRuntime_BeforeArenaReset();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == 2u && evidence.authority.retired == 1u);
	CHECK(evidence.authority.retireReason == MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(evidence.events.count == 2u && CheckMutation(&evidence, 1u, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_ARENA_RESET));

	MainCanonicalTopologyLeaseRuntime_BeforeFullLoad();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == 2u && evidence.authority.retired == 1u);
	CHECK(evidence.authority.retireReason == MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(evidence.coldLoadCoalescedCount == 1u);
	CHECK(evidence.events.count == 3u && CheckMutation(&evidence, 2u, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD));

	/* All later destructive boundaries remain recorded but cannot revive or
	 * advance the already-retired cold generation. */
	MainCanonicalTopologyLeaseRuntime_BeforeHubSwap();
	NativeTopologyLeaseRuntime_BeforeCheckpointRestore();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == 2u && evidence.authority.retired == 1u);
	CHECK(evidence.events.count == 5u);
	CHECK(CheckMutation(&evidence, 3u, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_HUB_SWAP));
	CHECK(CheckMutation(&evidence, 4u, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_CHECKPOINT_RESTORE));

	before = evidence;
	MainCanonicalTopologyLeaseRuntime_BeforeFullLoad();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == before.authority.epoch);
	CHECK(evidence.coldLoadCoalescedCount == before.coldLoadCoalescedCount);
	CHECK(evidence.events.count == before.events.count + 1u);
	CHECK(CheckMutation(&evidence, before.events.count, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD));
	return 1;
}

static int TestSecondStateZeroPreservesRetiredOwner(void)
{
	struct MainCanonicalTopologyLeaseRuntimeEvidence evidence, before;

	memset(&s_runtime, 0, sizeof(s_runtime));
	MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == 1u && evidence.authority.retired == 0u);
	CHECK(evidence.coldLoadCoalescedCount == 0u && evidence.events.count == 1u);

	/* This models a later StateZero: retire before gGT can be overwritten,
	 * then verify the after-zero hook preserves rather than reinitializes it. */
	MainCanonicalTopologyLeaseRuntime_BeforeGameTrackerZero();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == 2u && evidence.authority.retired == 1u);
	CHECK(evidence.authority.retireReason == MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(evidence.events.count == 2u && CheckMutation(&evidence, 1u, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_GAME_TRACKER_ZERO));
	before = evidence;
	MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(memcmp(&evidence, &before, sizeof(evidence)) == 0);
	MainCanonicalTopologyLeaseRuntime_BeforeFullLoad();
	CHECK(MainCanonicalTopologyLeaseRuntime_CopyEvidence(&evidence));
	CHECK(evidence.authority.epoch == before.authority.epoch && evidence.authority.retired == 1u);
	CHECK(evidence.coldLoadCoalescedCount == before.coldLoadCoalescedCount + 1u);
	CHECK(evidence.events.count == before.events.count + 1u);
	CHECK(CheckMutation(&evidence, before.events.count, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD));
	return 1;
}

int main(void)
{
	if (!TestFullLoadWithoutArenaResetIsNormal()) return 1;
	if (!TestColdLoadCoalescesOnlyAfterArenaReset()) return 1;
	if (!TestSecondStateZeroPreservesRetiredOwner()) return 1;
	puts("main_canonical_topology_lease_runtime_test: passed");
	return 0;
}
