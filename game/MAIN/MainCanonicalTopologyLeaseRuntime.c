#include "MAIN/MainCanonicalTopologyLeaseRuntime.h"

#include "platform/native_topology_lease_runtime.h"

#include <string.h>

struct MainCanonicalTopologyLeaseRuntime
{
	struct MainCanonicalTopologyLeaseRuntimeEvidence evidence;
	uint8_t ready;
	uint8_t coldLoadPending;
};

static struct MainCanonicalTopologyLeaseRuntime s_runtime;

static int RecordMutation(enum MainCanonicalTopologyLifecycleMutationReason reason)
{
	struct MainCanonicalTopologyLifecycleEventV1 event;

	event.kind = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION;
	event.reason = (uint32_t)reason;
	/* This is an ordering-only source provenance value, never a game tick. */
	event.opaqueSourceStep = (uint64_t)s_runtime.evidence.events.count + 1u;
	return MainCanonicalTopologyLifecycleEventsV1_Append(&s_runtime.evidence.events, &event, 1u);
}

static void RetireAtMutation(enum MainCanonicalTopologyLeaseRetireReason retireReason,
	enum MainCanonicalTopologyLifecycleMutationReason eventReason)
{
	if (s_runtime.ready == 0)
		return;

	/* Retirement comes first so a recorder-capacity failure remains fail-closed. */
	MainCanonicalTopologyLeaseAuthority_Retire(&s_runtime.evidence.authority, retireReason);
	(void)RecordMutation(eventReason);
}

void MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero(void)
{
	struct MainCanonicalTopologyLifecycleEventV1 constructed;

	if (s_runtime.ready != 0)
	{
		/* StateZero must never revive, replace, or erase a prior authority. */
		return;
	}

	memset(&s_runtime, 0, sizeof(s_runtime));
	MainCanonicalTopologyLeaseAuthority_Init(&s_runtime.evidence.authority,
		MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_COLD_BOOT);
	MainCanonicalTopologyLifecycleEventsV1_Init(&s_runtime.evidence.events);
	s_runtime.evidence.version = MAIN_CANONICAL_TOPOLOGY_LEASE_RUNTIME_EVIDENCE_VERSION;
	constructed.kind = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_CONSTRUCTED;
	constructed.reason = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_COLD_BOOT;
	constructed.opaqueSourceStep = 1u;
	if (!MainCanonicalTopologyLifecycleEventsV1_Append(&s_runtime.evidence.events, &constructed, 1u))
		return;
	s_runtime.ready = 1;
}

void MainCanonicalTopologyLeaseRuntime_BeforeGameTrackerZero(void)
{
	/* A later StateZero can destroy the source while a future owner is active. */
	RetireAtMutation(MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET,
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_GAME_TRACKER_ZERO);
}

void MainCanonicalTopologyLeaseRuntime_BeforeArenaReset(void)
{
	/* Only the actual MEMPACK arena wipe authorizes cold-load coalescing. */
	if (s_runtime.ready != 0)
		s_runtime.coldLoadPending = 1;
	RetireAtMutation(MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET,
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_ARENA_RESET);
}

void MainCanonicalTopologyLeaseRuntime_BeforeFullLoad(void)
{
	if (s_runtime.ready == 0)
		return;

	/*
	 * StateZero's immediately preceding arena reset has already retired this
	 * cold generation. The first full-load entry (the direct StateZero path at
	 * boot) is still recorded as a real mutation boundary, but must not
	 * manufacture a second retirement epoch.
	 */
	if (s_runtime.coldLoadPending != 0)
	{
		s_runtime.coldLoadPending = 0;
		s_runtime.evidence.coldLoadCoalescedCount++;
		(void)RecordMutation(MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD);
		return;
	}
	s_runtime.coldLoadPending = 0;

	RetireAtMutation(MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD,
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD);
}

void MainCanonicalTopologyLeaseRuntime_BeforeHubSwap(void)
{
	RetireAtMutation(MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_HUB_SWAP,
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_HUB_SWAP);
}

void NativeTopologyLeaseRuntime_BeforeCheckpointRestore(void)
{
	RetireAtMutation(MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_CHECKPOINT_RESTORE,
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_CHECKPOINT_RESTORE);
}

int MainCanonicalTopologyLeaseRuntime_CopyEvidence(struct MainCanonicalTopologyLeaseRuntimeEvidence *out)
{
	if (!out || s_runtime.ready == 0 ||
		s_runtime.evidence.version != MAIN_CANONICAL_TOPOLOGY_LEASE_RUNTIME_EVIDENCE_VERSION ||
		!MainCanonicalTopologyLifecycleEventsV1_Validate(&s_runtime.evidence.events))
		return 0;
	*out = s_runtime.evidence;
	return 1;
}
