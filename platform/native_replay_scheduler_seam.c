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
		else if (NativeReplaySchedulerArg_Equals(arg, "--record-v3"))
		{
			candidate.selector = NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V3;
			selectorCount++;
		}
		else if (NativeReplaySchedulerArg_Equals(arg, "--replay") || NativeReplaySchedulerArg_Equals(arg, "--replay-v2") ||
		         NativeReplaySchedulerArg_Equals(arg, "--replay-v3"))
		{
			if ((i + 1 >= argc) || NativeReplaySchedulerArg_IsOption(argv[i + 1])) return 0;
			candidate.selector = NativeReplaySchedulerArg_Equals(arg, "--replay") ? NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1 :
			                     NativeReplaySchedulerArg_Equals(arg, "--replay-v2") ? NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V2 :
			                                                                        NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V3;
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
	/* V3 is bootstrap-only and deliberately has no toggle/detailed or legacy
	 * compatibility bypass path. */
	if (((candidate.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V3) ||
	     (candidate.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V3)) &&
	    (candidate.toggle || candidate.detailed || candidate.bypassHeaderIdentity)) return 0;
	if (candidate.bypassHeaderIdentity && (candidate.selector != NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1)) return 0;
	*args = candidate;
	return 1;
}

int NativeReplayScheduler_ModeRequiresCanonicalState(enum NativeReplaySchedulerCanonicalMode mode)
{
	return (mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V2) ||
	       (mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V2) ||
	       (mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V3) ||
	       (mode == NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V3);
}

int NativeReplayScheduler_CopyCanonicalEndStateV3(uint32_t expectedReplayFrame, const struct NativeIdentityV1 *expectedIdentity,
	                                               const struct NativeCanonicalStateV3 *source, struct NativeCanonicalStateV3 *destination)
{
	struct NativeCanonicalStateV3 candidate;

	if ((expectedIdentity == NULL) || (source == NULL) || (destination == NULL) || (source->frameNumber != expectedReplayFrame) ||
	    !NativeCanonicalStateV3_Validate(source) ||
	    (memcmp(source->identity.build, expectedIdentity->build, NATIVE_IDENTITY_DIGEST_BYTES) != 0) ||
	    (memcmp(source->identity.content, expectedIdentity->content, NATIVE_IDENTITY_DIGEST_BYTES) != 0)) return 0;
	candidate = *source;
	if (!NativeCanonicalStateV3_ComputeDigests(&candidate) ||
	    (memcmp(candidate.domainDigests, source->domainDigests, sizeof(candidate.domainDigests)) != 0) ||
	    (candidate.combinedDigest != source->combinedDigest)) return 0;
	*destination = candidate;
	return 1;
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

int NativeReplayScheduler_CopyConsumedV2VSyncPacket(uint16_t *packets, uint32_t capacity, uint32_t index, uint16_t packet)
{
	if ((packets == NULL) || (index >= capacity) || (packet == 0)) return 0;
	packets[index] = packet;
	return 1;
}

int NativeReplayScheduler_V2BeginObservationNeedsValidation(int playbackV2, int pending)
{
	return (playbackV2 != 0) && (pending != 0);
}

int NativeReplayScheduler_V2RecordMayFinalize(int poisoned, int checkpointClosed)
{
	return (poisoned == 0) && (checkpointClosed != 0);
}

int NativeReplayScheduler_V3RecordMayFinalize(int poisoned, int beginOpen, uint32_t checkpointCount, int checkpointClosed)
{
	return (poisoned == 0) && (beginOpen == 0) && (checkpointCount == 1u) && (checkpointClosed != 0);
}

void NativeReplaySchedulerV3Lifecycle_Init(struct NativeReplaySchedulerV3Lifecycle *lifecycle)
{
	if (lifecycle != NULL) memset(lifecycle, 0, sizeof(*lifecycle));
}

void NativeReplaySchedulerV3Lifecycle_Abort(struct NativeReplaySchedulerV3Lifecycle *lifecycle)
{
	if (lifecycle != NULL) lifecycle->poisoned = 1;
}

int NativeReplaySchedulerV3Lifecycle_BeginFrame(struct NativeReplaySchedulerV3Lifecycle *lifecycle)
{
	if ((lifecycle == NULL) || (lifecycle->poisoned != 0) || (lifecycle->beginOpen != 0))
	{
		NativeReplaySchedulerV3Lifecycle_Abort(lifecycle);
		return 0;
	}
	lifecycle->beginOpen = 1;
	return 1;
}

int NativeReplaySchedulerV3Lifecycle_Submit(struct NativeReplaySchedulerV3Lifecycle *lifecycle,
	                                          enum NativeReplaySchedulerCanonicalKind requiredKind,
	                                          enum NativeReplaySchedulerCanonicalKind submittedKind)
{
	if ((lifecycle == NULL) || (lifecycle->beginOpen == 0) || (requiredKind != NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3) ||
	    (submittedKind != NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3))
	{
		NativeReplaySchedulerV3Lifecycle_Abort(lifecycle);
		return 0;
	}
	return 1;
}

int NativeReplaySchedulerV3Lifecycle_EndFrame(struct NativeReplaySchedulerV3Lifecycle *lifecycle)
{
	if ((lifecycle == NULL) || (lifecycle->poisoned != 0) || (lifecycle->beginOpen == 0))
	{
		NativeReplaySchedulerV3Lifecycle_Abort(lifecycle);
		return 0;
	}
	lifecycle->beginOpen = 0;
	return 1;
}

int NativeReplaySchedulerV3Lifecycle_CheckpointClosed(struct NativeReplaySchedulerV3Lifecycle *lifecycle, int success)
{
	if ((lifecycle == NULL) || (success == 0) || (lifecycle->checkpointCount == UINT32_MAX))
	{
		NativeReplaySchedulerV3Lifecycle_Abort(lifecycle);
		return 0;
	}
	lifecycle->checkpointCount++;
	lifecycle->checkpointClosed = 1;
	return 1;
}

int NativeReplaySchedulerV3Lifecycle_MayFinalize(const struct NativeReplaySchedulerV3Lifecycle *lifecycle)
{
	return (lifecycle != NULL) && NativeReplayScheduler_V3RecordMayFinalize(lifecycle->poisoned, lifecycle->beginOpen,
	                                                                          lifecycle->checkpointCount, lifecycle->checkpointClosed);
}

int NativeReplayScheduler_BuildV3MismatchReport(const struct NativeReplayV3Frame *expected,
	                                              const struct NativeReplayV2FrameObservation *liveEnd,
	                                              uint32_t liveVsyncTotal, uint32_t liveVsyncPacketCount,
	                                              const uint16_t *liveVsyncPackets, int playbackVsyncMismatch,
	                                              const struct NativeCanonicalStateV3 *liveCanonical,
	                                              struct NativeReplaySchedulerV3MismatchReport *report)
{
	struct NativeReplaySchedulerV3MismatchReport candidate;
	uint32_t packetCount;

	if ((expected == NULL) || (liveEnd == NULL) || (liveCanonical == NULL) || (report == NULL) ||
	    (expected->padCount != NATIVE_REPLAY_V2_PAD_COUNT) ||
	    (liveCanonical->input.padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT) ||
	    ((liveVsyncPacketCount != 0u) && (liveVsyncPackets == NULL))) return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.observationMismatch = (expected->end.frameTimer != liveEnd->frameTimer) ||
	                                (expected->end.frameCounter != liveEnd->frameCounter) ||
	                                (expected->end.timer != liveEnd->timer) ||
	                                (expected->end.framesInThisLEV != liveEnd->framesInThisLEV) ||
	                                (expected->end.elapsedTimeMS != liveEnd->elapsedTimeMS) ||
	                                (expected->end.msInThisLEV != liveEnd->msInThisLEV) ||
	                                (expected->end.elapsedEventTime != liveEnd->elapsedEventTime) ||
	                                (expected->end.mainGameState != liveEnd->mainGameState) ||
	                                (expected->end.loadingStage != liveEnd->loadingStage) ||
	                                (expected->end.levelID != liveEnd->levelID) ||
	                                (expected->end.mixRandomNumber != liveEnd->mixRandomNumber) ||
	                                (expected->end.audioRNG != liveEnd->audioRNG) ||
	                                (expected->end.deadcoed0 != liveEnd->deadcoed0) ||
	                                (expected->end.deadcoed1 != liveEnd->deadcoed1) ||
	                                (expected->end.advRng0 != liveEnd->advRng0) ||
	                                (expected->end.advRng1 != liveEnd->advRng1);
	candidate.vsyncTotalMismatch = (playbackVsyncMismatch != 0) || (expected->vsyncTotal != liveVsyncTotal);
	candidate.vsyncPacketCountMismatch = expected->vsyncPacketCount != liveVsyncPacketCount;
	packetCount = expected->vsyncPacketCount < liveVsyncPacketCount ? expected->vsyncPacketCount : liveVsyncPacketCount;
	if (packetCount > NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS) packetCount = NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS;
	for (uint32_t index = 0; index < packetCount; index++)
		if (expected->vsyncPackets[index] != liveVsyncPackets[index]) { candidate.vsyncFirstPacketMismatch = 1; break; }
	for (uint32_t index = 0; index < NATIVE_REPLAY_V2_PAD_COUNT; index++)
	{
		const struct NativeReplayV2Pad *recorded = &expected->pads[index];
		const struct NativeCanonicalInputPadV1 *live = &liveCanonical->input.pads[index];
		if ((recorded->status != live->status) || (recorded->id != live->id) ||
		    (recorded->buttons[0] != live->buttons[0]) || (recorded->buttons[1] != live->buttons[1]) ||
		    (memcmp(recorded->analog, live->analog, sizeof(recorded->analog)) != 0) || (recorded->connected != live->connected))
			candidate.padMask |= UINT32_C(1) << index;
	}
	for (uint32_t index = 0; index < NATIVE_CANONICAL_DOMAIN_COUNT; index++)
		if (expected->canonical.domainDigests[index] != liveCanonical->domainDigests[index])
		{
			candidate.firstDomainID = index + 1u;
			candidate.expectedDomainDigest = expected->canonical.domainDigests[index];
			candidate.liveDomainDigest = liveCanonical->domainDigests[index];
			break;
		}
	candidate.combinedMismatch = expected->canonical.combinedDigest != liveCanonical->combinedDigest;
	candidate.expectedCombinedDigest = expected->canonical.combinedDigest;
	candidate.liveCombinedDigest = liveCanonical->combinedDigest;
	(void)NativeCanonicalDriversV1_Compare(&expected->canonical.drivers, &liveCanonical->drivers, &candidate.drivers);
	*report = candidate;
	return 1;
}
