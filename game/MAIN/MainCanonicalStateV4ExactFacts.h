#ifndef GAME_MAIN_CANONICAL_STATE_V4_EXACT_FACTS_H
#define GAME_MAIN_CANONICAL_STATE_V4_EXACT_FACTS_H

#include "MAIN/MainCanonicalTopologyFacts.h"
#include "platform/native_canonical_drivers_detailed.h"
#include "platform/native_canonical_projector.h"

/*
 * Source-only candidate seam for the exact, pointer-free facts which feed
 * the V4 DRIVERS, WORLD, and TOPOLOGY domains.  Borrowed topology ranges are
 * consumed during this call only.  This is not a native extraction or a live
 * publication API.
 *
 * `driverScratch` is caller owned, may be overwritten, and must be at least
 * NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES.  Keeping it outside this frame
 * prevents the 4,224-byte normative stream from becoming hidden stack use.
 * On every failure `state` is unchanged.
 */
int MainCanonicalState_ProjectV4FromExactFacts(
	struct NativeCanonicalStateV4 *state,
	const struct MainCanonicalStateV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeDeterministicRngBankV1 *deterministicRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversDetailedV1 *driverFacts,
	const struct NativeCanonicalWorldCountersV1Facts *worldCounterFacts,
	const struct NativeCanonicalWorldMineRegistryV1Facts *mineFacts,
	const struct MainCanonicalTopologyFacts *topologyFacts,
	uint8_t *driverScratch, size_t driverScratchSize);

#endif
