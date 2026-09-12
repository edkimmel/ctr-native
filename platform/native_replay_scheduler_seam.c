#include "platform/native_replay_scheduler_seam.h"

#include <string.h>

static int NativeReplaySchedulerArg_Equals(const char *left, const char *right)
{
	return (left != NULL) && (right != NULL) && (strcmp(left, right) == 0);
}

static int NativeReplaySchedulerArg_IsOption(const char *arg)
{
	return (arg != NULL) && (arg[0] == '-') && (arg[1] == '-');
}

int NativeReplayScheduler_ParseArgs(int argc, char **argv, struct NativeReplaySchedulerArgs *args)
{
	struct NativeReplaySchedulerArgs candidate;
	int selectorCount = 0;

	if ((argc < 0) || (argv == NULL) || (args == NULL)) return 0;
	memset(&candidate, 0, sizeof(candidate));
	for (int i = 1; i < argc; i++)
	{
		const char *arg = argv[i];
		if (NativeReplaySchedulerArg_Equals(arg, "--record"))
		{
			candidate.selector = NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V1;
			selectorCount++;
		}
		else if (NativeReplaySchedulerArg_Equals(arg, "--record-v2"))
		{
			candidate.selector = NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2;
			selectorCount++;
		}
		else if (NativeReplaySchedulerArg_Equals(arg, "--replay") || NativeReplaySchedulerArg_Equals(arg, "--replay-v2"))
		{
			if ((i + 1 >= argc) || NativeReplaySchedulerArg_IsOption(argv[i + 1])) return 0;
			candidate.selector = NativeReplaySchedulerArg_Equals(arg, "--replay") ? NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1 :
			                                                                NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V2;
			candidate.replayPath = argv[++i];
			selectorCount++;
		}
		else if (NativeReplaySchedulerArg_Equals(arg, "--toggle")) candidate.toggle = 1;
		else if (NativeReplaySchedulerArg_Equals(arg, "--detailed")) candidate.detailed = 1;
		else if (NativeReplaySchedulerArg_Equals(arg, "--replay-bypass-header")) candidate.bypassHeaderIdentity = 1;
	}
	if (selectorCount > 1) return 0;
	if ((candidate.toggle || candidate.detailed) &&
	    (candidate.selector != NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V1) &&
	    (candidate.selector != NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2)) return 0;
	if ((candidate.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2) && candidate.detailed) return 0;
	if (candidate.bypassHeaderIdentity && (candidate.selector != NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1)) return 0;
	*args = candidate;
	return 1;
}

int NativeReplayScheduler_ModeRequiresCanonicalState(enum NativeReplaySchedulerCanonicalMode mode)
{
	return (mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V2) ||
	       (mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V2);
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
