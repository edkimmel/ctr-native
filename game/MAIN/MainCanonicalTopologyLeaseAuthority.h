#ifndef GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_H
#define GAME_MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_H

#include <common.h>
#include "platform/native_topology_residency.h"

/*
 * Dormant ownership authority for a future active-mempack topology reader.
 * This module owns no hook and is not live authority until the call sites in
 * docs/TOPOLOGY_LEASE_AUTHORITY.md have been deliberately installed.
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

/* Init and Retire never wrap epochs.  Zero and UINT64_MAX are terminal
 * fail-closed states.  Inactive hub preload deliberately has no retire API. */
void MainCanonicalTopologyLeaseAuthority_Init(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseInitReason reason);
void MainCanonicalTopologyLeaseAuthority_Retire(struct MainCanonicalTopologyLeaseAuthority *authority,
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

/* This deliberately named post-init reader is the sole API that observes
 * NavHeader.last.  It proves each range before dereference and requires last
 * to equal the one-past end of the in-header frame array.  Failure leaves the
 * supplied pointer-free observation unchanged. */
int MainCanonicalTopologyLease_ObservePostInit(struct NativeTopologyResidencyObservedV1 *observedOut,
	const struct MainCanonicalTopologyLease *lease,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData);

#endif
