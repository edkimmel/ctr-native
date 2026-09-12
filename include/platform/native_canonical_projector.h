#ifndef PLATFORM_NATIVE_CANONICAL_PROJECTOR_H
#define PLATFORM_NATIVE_CANONICAL_PROJECTOR_H

#include "platform/native_input.h"
#include "platform/native_canonical_state.h"

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
int MainCanonicalState_ProjectV1(struct NativeCanonicalStateV1 *state, const struct NativeIdentityV1 *identity, uint32_t frameNumber,
                                 const struct NativeCanonicalControlV1 *control, const struct NativeCanonicalRngV1 *rng,
                                 const struct NativeCanonicalInputV1 *input);

#endif
