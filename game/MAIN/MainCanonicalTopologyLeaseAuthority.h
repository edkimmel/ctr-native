#ifndef GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_H
#define GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_H

#include <common.h>
#include "platform/native_topology_residency.h"

/*
 * Ownership authority for a future active-mempack topology reader. This
 * module owns no lifecycle hook. A private retire-only owner may invalidate
 * it at audited mutation boundaries, but it remains non-live: no game path
 * acquires, observes, captures, activates, or publishes a topology lease.
 */
enum MainCanonicalTopologyLeaseInitReason
{
	MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_COLD_BOOT = 1,
	MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST = 2
};

#define MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_TAG UINT32_C(0x31414c54) /* TLA1 */

enum MainCanonicalTopologyLeaseRetireReason
{
	MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD = 1,
	MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_HUB_SWAP = 2,
	MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_CHECKPOINT_RESTORE = 3,
	MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET = 4
};

struct MainCanonicalTopologyLeaseAuthority
{
	uint32_t tag;
	uint64_t epoch;
	uint8_t initialized;
	uint8_t retired;
	uint8_t initReason;
	uint8_t retireReason;
};

struct MainCanonicalTopologyLease
{
	struct NativeTopologyResidencyLeaseV1 residency;
	const struct GameTracker *gGT;
	const struct sData *sourceData;
	const struct Mempack *mempack;
	uint8_t mempackIndex;
	uint8_t valid;
};

/* Init constructs an authority once.  Retire and Activate are the matching
 * lifecycle transition: active -> retired(reason) increments the generation
 * exactly once, then retired(same reason) -> active at post-init does not
 * increment it.
 * Zero and UINT64_MAX are terminal fail-closed states.  Inactive hub preload
 * deliberately has no retire API. */
void MainCanonicalTopologyLeaseAuthority_Init(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseInitReason reason);
void MainCanonicalTopologyLeaseAuthority_Retire(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseRetireReason reason);
void MainCanonicalTopologyLeaseAuthority_ActivatePostInit(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseRetireReason reason);

/* Acquire only while the source reports an idle load stage and no in-progress
 * load.  The resident lease is [mempack.start, mempack.firstFreeByte), never
 * allocator capacity.  Failure leaves leaseOut unchanged. */
int MainCanonicalTopologyLease_Acquire(struct MainCanonicalTopologyLease *leaseOut,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData);
int MainCanonicalTopologyLease_Validate(const struct MainCanonicalTopologyLease *lease,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData);

/* This deliberately named post-init reader is the lease's only API that
 * observes NavHeader.last.  It proves each range before dereference and
 * requires last to equal the one-past end of the in-header frame array.
 * Failure leaves the supplied pointer-free observation unchanged.  One other
 * first-party reader exists, outside the lease: MainCanonicalDrivers_BotNavIndex
 * (MainCanonicalDrivers.c) reads a bot path's last check-only, to prove the
 * bot's botNavFrame lies in that path's frame array.  The owner ruled that
 * read allowed on the live path (the Task 8 race plan's LR-17, ruled (a)); it
 * is never written and never used to acquire, activate, capture, or publish
 * the lease.  tests/main_arcade_race_digest_isolation_test.cmake pins
 * these two as the only readers. */
int MainCanonicalTopologyLease_ObservePostInit(struct NativeTopologyResidencyObservedV1 *observedOut,
	const struct MainCanonicalTopologyLease *lease,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData);

#endif
