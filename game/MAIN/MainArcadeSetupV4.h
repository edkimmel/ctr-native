#ifndef MAIN_ARCADE_SETUP_V4_H
#define MAIN_ARCADE_SETUP_V4_H

#include "MAIN/MainArcadeBotSetup.h"
#include "platform/native_canonical_projector.h"

/*
 * Dormant one-way composition boundary: pointer-free M3 roster/setup facts
 * are frozen, revalidated on every use, and then passed to the value-only V4
 * projector.  This type is not a native-state snapshot and has no live owner.
 */
struct MainArcadeSetupV4Context
{
	struct MainCanonicalStateV4Context projector;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct NativeDeterministicRngBankV1 rngBefore;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validatedRoster;
	struct MainArcadeBotSetupPlan setupPlan;
	struct NativeDeterministicRngBankV1 rngAfter;
	uint32_t driversPresenceMask;
	uint64_t driversRosterMetaDigest;
};

/* Transactional.  It accepts only pointer-free facts; no extractor is called. */
int MainArcadeSetupV4Context_Init(struct MainArcadeSetupV4Context *context,
	const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRosterNativeFacts *rosterFacts,
	const struct MainArcadeBotSetupSourceFacts *setupFacts,
	const struct NativeDeterministicRngBankV1 *rngBefore);

/* Transactional V4 projection after strict M3 setup/context revalidation. */
int MainArcadeSetupV4_Project(struct NativeCanonicalStateV4 *state,
	const struct MainArcadeSetupV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversV1 *drivers,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology);

#endif
