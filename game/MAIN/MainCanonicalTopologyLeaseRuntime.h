#ifndef GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_RUNTIME_H
#define GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_RUNTIME_H

#include "MAIN/MainCanonicalTopologyLeaseAuthority.h"
#include "MAIN/MainCanonicalTopologyLifecycleEvents.h"

/*
 * Private, process-local retirement owner for the future topology lease.
 * It is intentionally not part of any checkpoint, replay, canonical-state,
 * topology publication, or network payload.  It never acquires, captures, or
 * activates a lease.  These hooks exist only at audited mutation boundaries
 * and leave the authority fail-closed once one has been retired.
 */
#define MAIN_CANONICAL_TOPOLOGY_LEASE_RUNTIME_EVIDENCE_VERSION UINT32_C(1)

struct MainCanonicalTopologyLeaseRuntimeEvidence
{
	uint32_t version;
	uint32_t coldLoadCoalescedCount;
	struct MainCanonicalTopologyLeaseAuthority authority;
	struct MainCanonicalTopologyLifecycleEventsV1 events;
};

/* StateZero brackets gGT clearing so an existing owner is retired before its
 * source state is destroyed. Construction happens only on first boot. */
void MainCanonicalTopologyLeaseRuntime_BeforeGameTrackerZero(void);
void MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero(void);
void MainCanonicalTopologyLeaseRuntime_BeforeArenaReset(void);
void MainCanonicalTopologyLeaseRuntime_BeforeFullLoad(void);
void MainCanonicalTopologyLeaseRuntime_BeforeHubSwap(void);

/* Test-only-style diagnostic copy; it exposes no game pointers or lease. */
int MainCanonicalTopologyLeaseRuntime_CopyEvidence(struct MainCanonicalTopologyLeaseRuntimeEvidence *out);

#endif
