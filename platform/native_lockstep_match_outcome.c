#include "platform/native_lockstep_match_outcome.h"

#include <string.h>

int NativeLockstepMatchOutcome_Init(struct NativeLockstepMatchOutcomeTracker *tracker, uint32_t stallTimeoutFrames)
{
	if (tracker == NULL)
	{
		return 0;
	}
	if (stallTimeoutFrames != 0u)
	{
		if ((stallTimeoutFrames < NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES) ||
		    (stallTimeoutFrames > NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES))
		{
			return 0;
		}
	}

	memset(tracker, 0, sizeof(*tracker));
	tracker->stallTimeoutFrames = (stallTimeoutFrames != 0u) ? stallTimeoutFrames : NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES;
	return 1;
}

enum NativeLockstepSessionResult NativeLockstepMatchOutcome_Poll(struct NativeLockstepMatchOutcomeTracker *tracker,
                                                                  const struct NativeLockstepSession *session,
                                                                  enum NativeLockstepSessionResult lastTakeResult, uint32_t frameIndex)
{
	const struct NativeLockstepDivergenceReport *divergence;
	const struct NativeLockstepFaultReport *fault;

	if ((tracker == NULL) || (session == NULL))
	{
		return lastTakeResult;
	}
	if (tracker->latched)
	{
		return lastTakeResult;
	}

	divergence = NativeLockstepSession_FirstDivergence(session);
	if (divergence != NULL)
	{
		tracker->report.cause = NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED;
		tracker->report.frameIndex = divergence->frameIndex;
		tracker->report.senderSlot = divergence->senderSlot;
		tracker->report.stalledFrameCount = 0u;
		tracker->latched = 1u;
		return lastTakeResult;
	}

	fault = NativeLockstepSession_FirstFault(session);
	if (fault != NULL)
	{
		tracker->report.cause = NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED;
		tracker->report.frameIndex = fault->frameIndex;
		tracker->report.senderSlot = fault->senderSlot;
		tracker->report.stalledFrameCount = 0u;
		tracker->latched = 1u;
		return lastTakeResult;
	}

	if (lastTakeResult == NATIVE_LOCKSTEP_SESSION_STALL)
	{
		tracker->consecutiveStallFrames++;
		if (tracker->consecutiveStallFrames >= tracker->stallTimeoutFrames)
		{
			tracker->report.cause = NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT;
			tracker->report.frameIndex = frameIndex;
			tracker->report.senderSlot = NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT;
			tracker->report.stalledFrameCount = tracker->consecutiveStallFrames;
			tracker->latched = 1u;
		}
	}
	else
	{
		tracker->consecutiveStallFrames = 0u;
	}

	return lastTakeResult;
}

const struct NativeLockstepMatchOutcomeReport *NativeLockstepMatchOutcome_FirstOutcome(
    const struct NativeLockstepMatchOutcomeTracker *tracker)
{
	if ((tracker == NULL) || !tracker->latched)
	{
		return NULL;
	}
	return &tracker->report;
}
