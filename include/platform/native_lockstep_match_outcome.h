#ifndef PLATFORM_NATIVE_LOCKSTEP_MATCH_OUTCOME_H
#define PLATFORM_NATIVE_LOCKSTEP_MATCH_OUTCOME_H

#include "platform/native_lockstep_session.h"

#include <stdint.h>

/*
 * Transport-agnostic, allocation-free policy layer on top of
 * struct NativeLockstepSession.  The session itself reports a stall
 * (NATIVE_LOCKSTEP_SESSION_STALL) but deliberately never decides how long to
 * wait: the caller must not advance the simulation and should service the
 * transport and retry, indefinitely.  This module closes that gap, turning an
 * indefinitely retried stall into a terminated match outcome after a bounded
 * number of consecutive stalled frames, and mirrors the session's own
 * DIVERGED-outranks-FAULTED priority for the two other terminal conditions it
 * already latches.
 *
 * It reads the session only through its public const-or-NULL accessors
 * (NativeLockstepSession_FirstDivergence, NativeLockstepSession_FirstFault);
 * it never reaches into struct NativeLockstepSession internals and never
 * mutates the session.
 */

/*
 * Default stall timeout: 180 consumption frames, 3 s at 60 Hz.  This is the
 * milestone's deliberate, documented policy decision: a two-cabinet wired-LAN
 * hiccup should recover well inside 3 s.
 */
#define NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES 180u
/*
 * Minimum configurable stall timeout: 30 consumption frames, 0.5 s at 60 Hz.
 * Below this a transient network hiccup could not plausibly recover in time.
 */
#define NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES 30u
/*
 * Maximum configurable stall timeout: 600 consumption frames, 10 s at 60 Hz.
 * This is a hard ceiling so a truly dead peer does not stall the cabinet
 * forever.
 */
#define NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES 600u

enum NativeLockstepMatchOutcomeCause
{
	NATIVE_LOCKSTEP_MATCH_OUTCOME_NONE = 0,
	NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED = 1,
	NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED = 2,
	NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT = 3
};

/*
 * senderSlot for a STALL_TIMEOUT report: a stall timeout does not name a
 * specific peer slot, because the session does not expose which remote
 * window is empty.  In the two-cabinet profile there is exactly one remote
 * human slot, so a caller can resolve this unambiguously itself if needed.
 */
#define NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT UINT32_MAX

/*
 * cause is an enum NativeLockstepMatchOutcomeCause value.  frameIndex is the
 * diverged/faulted frame for those two causes, or the consumption frame that
 * was still stalled at timeout for STALL_TIMEOUT.  senderSlot is copied from
 * the corresponding session report, or
 * NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT for STALL_TIMEOUT.
 * stalledFrameCount is the consecutive stalled poll count at latch time,
 * zero for DIVERGED/FAULTED.
 */
struct NativeLockstepMatchOutcomeReport
{
	uint32_t cause;
	uint32_t frameIndex;
	uint32_t senderSlot;
	uint32_t stalledFrameCount;
};

struct NativeLockstepMatchOutcomeTracker
{
	uint32_t stallTimeoutFrames;
	uint32_t consecutiveStallFrames;
	uint8_t latched;
	struct NativeLockstepMatchOutcomeReport report;
};

/*
 * Zeroes *tracker and sets stallTimeoutFrames to
 * NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES when the argument is 0,
 * otherwise requires it in [NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES,
 * NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES].  Returns 0 with *tracker
 * untouched on a NULL tracker or an out-of-range nonzero value.
 */
int NativeLockstepMatchOutcome_Init(struct NativeLockstepMatchOutcomeTracker *tracker, uint32_t stallTimeoutFrames);

/*
 * Call once per simulation tick, right after a
 * NativeLockstepSession_TakeFrameInputs call, passing its result and the
 * frameIndex that was attempted.  Returns lastTakeResult unchanged: this is a
 * policy hook, not a result transform, the caller still sees the session's
 * own result.
 *
 * Behavior, in order, and it is a no-op in every branch once tracker->latched
 * is already true:
 *   1. If NativeLockstepSession_FirstDivergence(session) is non-NULL, latch
 *      cause = DIVERGED, frameIndex/senderSlot copied from the divergence
 *      report, stalledFrameCount = 0.  (Mirrors the session's own priority:
 *      DIVERGED outranks FAULTED.)
 *   2. Else if NativeLockstepSession_FirstFault(session) is non-NULL, latch
 *      cause = FAULTED, frameIndex/senderSlot copied from the fault report,
 *      stalledFrameCount = 0.
 *   3. Else if lastTakeResult == NATIVE_LOCKSTEP_SESSION_STALL:
 *      tracker->consecutiveStallFrames++; if that now reaches
 *      tracker->stallTimeoutFrames, latch cause = STALL_TIMEOUT, frameIndex =
 *      frameIndex (the argument), senderSlot =
 *      NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT, stalledFrameCount =
 *      tracker->consecutiveStallFrames.
 *   4. Else (OK, REJECTED, or any other non-stall result):
 *      tracker->consecutiveStallFrames = 0.
 *
 * A NULL tracker or NULL session is a no-op that just returns lastTakeResult.
 */
enum NativeLockstepSessionResult NativeLockstepMatchOutcome_Poll(struct NativeLockstepMatchOutcomeTracker *tracker,
                                                                  const struct NativeLockstepSession *session,
                                                                  enum NativeLockstepSessionResult lastTakeResult, uint32_t frameIndex);

/* &tracker->report once latched, NULL otherwise, and NULL on a NULL tracker. */
const struct NativeLockstepMatchOutcomeReport *NativeLockstepMatchOutcome_FirstOutcome(
    const struct NativeLockstepMatchOutcomeTracker *tracker);

#endif
