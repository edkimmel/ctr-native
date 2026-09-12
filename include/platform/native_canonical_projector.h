#ifndef PLATFORM_NATIVE_CANONICAL_PROJECTOR_H
#define PLATFORM_NATIVE_CANONICAL_PROJECTOR_H

#include "platform/native_input.h"
#include "platform/native_canonical_state.h"
#include "platform/native_canonical_state_v3.h"

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

#endif
