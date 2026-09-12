#include "platform/native_replay_scheduler_seam.h"

#include <string.h>

int NativeReplayScheduler_ModeRequiresCanonicalState(enum NativeReplaySchedulerCanonicalMode mode)
{
	return mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_FUTURE_V2_TEST;
}

int NativeReplayScheduler_CopyCanonicalEndState(int required, uint32_t expectedReplayFrame, const struct NativeCanonicalStateV1 *source,
                                                struct NativeCanonicalStateV1 *destination)
{
	struct NativeCanonicalStateV1 candidate;

	if (required == 0)
	{
		return 1;
	}
	if ((source == NULL) || (destination == NULL) || (source->frameNumber != expectedReplayFrame) || !NativeCanonicalStateV1_Validate(source))
	{
		return 0;
	}

	NativeCanonicalStateV1_Init(&candidate);
	candidate.frameNumber = source->frameNumber;
	memcpy(candidate.identity.build, source->identity.build, NATIVE_IDENTITY_DIGEST_BYTES);
	memcpy(candidate.identity.content, source->identity.content, NATIVE_IDENTITY_DIGEST_BYTES);
	candidate.control = source->control;
	candidate.rng = source->rng;
	candidate.input = source->input;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++) candidate.domainDigests[i] = source->domainDigests[i];
	candidate.combinedDigest = source->combinedDigest;
	/* Validate structural gates first, then recompute so a caller cannot hand
	 * EndFrame a stale/forged diagnostic digest. */
	if (!NativeCanonicalStateV1_ComputeDigests(&candidate))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		if (candidate.domainDigests[i] != source->domainDigests[i]) return 0;
	}
	if (candidate.combinedDigest != source->combinedDigest) return 0;
	*destination = candidate;
	return 1;
}
