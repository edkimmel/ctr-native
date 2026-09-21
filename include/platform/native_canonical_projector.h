#ifndef PLATFORM_NATIVE_CANONICAL_PROJECTOR_H
#define PLATFORM_NATIVE_CANONICAL_PROJECTOR_H

#include "platform/native_input.h"
#include "platform/native_canonical_state.h"
#include "platform/native_canonical_state_v3.h"
#include "platform/native_canonical_state_v4.h"
#include "platform/native_match_config.h"

/*
 * Game-owned projection helpers for the audited M2 core domains.  The input
 * snapshot conversion deliberately copies only the nine PSX-shaped ingress
 * bytes; PlatformInputPadSnapshot.reserved is not part of the state contract.
 */
int MainCanonicalState_FreezeInputV1(struct NativeCanonicalInputV1 *input,
                                     const struct PlatformInputPadSnapshot *snapshots, uint32_t count);

/*
 * Projects already-selected, scalar game values.  Callers supply game values
 * field-by-field so this code never discovers native layout or follows game
 * pointers.  The result is committed only after digest computation succeeds.
 */
int MainCanonicalState_ProjectV1(struct NativeCanonicalStateV1 *state, const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
                                 const struct NativeCanonicalControlV1 *control, const struct NativeCanonicalRngV1 *rng,
                                 const struct NativeCanonicalInputV1 *input);

/*
 * Pure stage-A bridge to the sealed v3 state record.  DRIVERS is already a
 * validated 288-byte summary produced by the dormant drivers assembly; this
 * function deliberately accepts neither native driver data nor a detailed
 * stream, and never re-derives that summary.  WORLD and TOPOLOGY remain the
 * canonical empty domains until their independent projectors exist.
 */
int MainCanonicalState_ProjectV3(struct NativeCanonicalStateV3 *state, const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
                                 const struct NativeCanonicalControlV1 *control, const struct NativeCanonicalRngV1 *rng,
                                 const struct NativeCanonicalInputV1 *input, const struct NativeCanonicalDriversV1 *drivers);
/* Runtime-workspace form. The caller owns unpublished staging and scratch;
 * unlike ProjectV3, failure may modify `state`. */
int MainCanonicalState_ProjectV3InPlaceWithScratch(struct NativeCanonicalStateV3 *state,
	const struct NativeIdentityV1 *identity,uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,const struct NativeCanonicalRngV1 *rng,
	const struct NativeCanonicalInputV1 *input,const struct NativeCanonicalDriversV1 *drivers,
	uint8_t *scratch,size_t scratchSize);

/*
 * Dormant V4 projection is deliberately a value-only seam.  The context
 * locks one validated match config and its locally computed SHA-256 digest;
 * it neither owns game data nor selects a control-track policy.
 */
struct MainCanonicalStateV4Context
{
	struct NativeMatchConfigV1 config;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
};

/* Transactional: failure leaves context unchanged. */
int MainCanonicalStateV4Context_Init(struct MainCanonicalStateV4Context *context,
	const struct NativeMatchConfigV1 *config);

/*
 * Transactional explicit-value V4 projector.  Every value is supplied by
 * the caller; this function does not call native extractors or traverse game
 * data.  On failure, state remains unchanged.
 */
int MainCanonicalState_ProjectV4(struct NativeCanonicalStateV4 *state,
	const struct MainCanonicalStateV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeDeterministicRngBankV1 *deterministicRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversV1 *drivers,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology);

/*
 * Runtime-workspace form.  The caller owns unpublished staging and scratch.
 * It performs the same input validation as ProjectV4, then writes the state
 * and its digests in place; unlike ProjectV4, failure may leave `state`
 * modified.  It never materializes a whole NativeCanonicalStateV4 local.
 */
int MainCanonicalState_ProjectV4InPlaceWithScratch(struct NativeCanonicalStateV4 *state,
	const struct MainCanonicalStateV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeDeterministicRngBankV1 *deterministicRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversV1 *drivers,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology,
	uint8_t *scratch, size_t scratchSize);

#endif
