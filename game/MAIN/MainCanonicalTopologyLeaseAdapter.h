#ifndef GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_ADAPTER_H
#define GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_ADAPTER_H

#include "MAIN/MainCanonicalTopologyLeaseAuthority.h"
#include "MAIN/MainCanonicalTopologyFacts.h"

/*
 * Dormant source-only bridge from an active-mempack lease to the portable
 * topology value.  This does not install a lifecycle hook, publish state, or
 * retain any game pointer.  A call acquires a lease, observes the complete
 * topology residency, streams the facts through the bounded reader, then
 * reacquires the observation and commits both pointer-free outputs only when
 * the complete lease/residency identity is still unchanged.  The caller must
 * provide `guard`: it is an audited quiescent/read-only window predicate for
 * this candidate capture.  It is checked repeatedly around native reads, but
 * is deliberately not claimed to be a snapshot or synchronization proof.
 *
 * Both outputs are required and must not overlap.  Failure leaves both
 * outputs unchanged.  The fact reader proves every quad checkpoint and
 * restart edge is either absent or refers to the current restart range.
 */
typedef int (*MainCanonicalTopologyLeaseAdapterGuardFn)(const void *context);

int MainCanonicalTopologyLeaseAdapter_Capture(
	struct NativeCanonicalTopologyV1 *topologyOut,
	struct NativeTopologyResidencySnapshotV1 *residencyOut,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData,
	MainCanonicalTopologyLeaseAdapterGuardFn guard,const void *guardContext);

#endif
