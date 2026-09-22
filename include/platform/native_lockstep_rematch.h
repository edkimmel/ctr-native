#ifndef PLATFORM_NATIVE_LOCKSTEP_REMATCH_H
#define PLATFORM_NATIVE_LOCKSTEP_REMATCH_H

#include "platform/native_match_config.h"

/*
 * Pure config builder for a rematch of the same fixture: same track, mode,
 * rules, lap count, tick rate, build/content identity, bot rules, and
 * per-slot character/difficulty choices as the previous match, but a fresh
 * NativeMatchConfigV1 (not a mutated copy of the previous config's runtime
 * state) with a new masterSeed.
 *
 * A rematch config is only half the story.  Building this config does not
 * create or touch a NativeLockstepSession or a NativeReplaySchedulerV4.  A
 * caller that holds a session left in DIVERGED or FAULTED mode, or a replay
 * scheduler left in MISMATCH or POISON mode, must never resume or reuse it
 * for the rematch: both of those latch-once terminal states exist
 * specifically so a poisoned or diverged run is never silently continued
 * (see platform/native_replay_scheduler_v4.c Poison/Match and
 * include/platform/native_lockstep_session.h DIVERGED/FAULTED).  A rematch
 * means calling NativeLockstepSession_Init then _Open again on a fresh
 * struct NativeLockstepSession with the config this function produces, and
 * opening a brand-new NativeReplaySchedulerV4 recording (Init then
 * OpenRecord) with a fresh output path, never reusing the old scheduler or
 * session struct in place.  This module only produces the config value; it
 * does not open anything itself, and it has no dependency on the session or
 * replay scheduler headers.
 */

/*
 * Builds *next as a fresh rematch config derived from *previous with
 * newMasterSeed.  Returns 1 on success with *next populated.  Returns 0,
 * with *next left completely untouched, if previous is NULL, next is NULL,
 * NativeMatchConfigV1_Validate(previous) fails, newMasterSeed equals
 * previous->masterSeed (a rematch must use a different seed so item, hazard,
 * and bot RNG streams are not replayed bit-identically), or the config this
 * function constructs unexpectedly fails NativeMatchConfigV1_Validate.
 */
int NativeLockstepRematch_BuildConfig(const struct NativeMatchConfigV1 *previous, uint64_t newMasterSeed,
                                       struct NativeMatchConfigV1 *next);

#endif
