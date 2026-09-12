#include "platform/native_replay_scheduler_seam.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                         \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static void MakeState(struct NativeCanonicalStateV1 *state)
{
	NativeCanonicalStateV1_Init(state);
	state->frameNumber = 77u;
	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		state->identity.build[i] = (uint8_t)i;
		state->identity.content[i] = (uint8_t)(0x80u + i);
	}
	state->control.gameMode1 = 1;
	state->rng.advRng1 = 2;
	state->input.pads[0].connected = 1;
	(void)NativeCanonicalStateV1_ComputeDigests(state);
}

static int TestRequirementModes(void)
{
	CHECK(!NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_NONE));
	CHECK(!NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V1));
	CHECK(!NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V1));
	CHECK(!NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V1));
	CHECK(!NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V2));
	CHECK(NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V2));
	CHECK(NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V2));
	return 0;
}

static int Parse(int argc, char **argv, struct NativeReplaySchedulerArgs *args)
{
	memset(args, 0xa5, sizeof(*args));
	return NativeReplayScheduler_ParseArgs(argc, argv, args);
}

static int TestCliMatrix(void)
{
	struct NativeReplaySchedulerArgs args;
	char *normal[] = { "ctr_native" };
	char *record[] = { "ctr_native", "--record" };
	char *recordV2[] = { "ctr_native", "--record-v2", "--toggle" };
	char *replay[] = { "ctr_native", "--replay", "input.ctrreplay", "--replay-bypass-header" };
	char *replayV2[] = { "ctr_native", "--replay-v2", "input.v2.ctrreplay" };
	char *conflict[] = { "ctr_native", "--record", "--replay-v2", "x" };
	char *missing[] = { "ctr_native", "--replay-v2" };
	char *missingOption[] = { "ctr_native", "--replay", "--toggle" };
	char *v2Detailed[] = { "ctr_native", "--record-v2", "--detailed" };
	char *replayToggle[] = { "ctr_native", "--replay-v2", "x", "--toggle" };
	char *v2Bypass[] = { "ctr_native", "--replay-v2", "x", "--replay-bypass-header" };

	CHECK(Parse(1, normal, &args));
	CHECK(args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_NONE && args.replayPath == NULL);
	CHECK(Parse(2, record, &args));
	CHECK(args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V1 && !args.toggle && !args.detailed);
	CHECK(Parse(3, recordV2, &args));
	CHECK(args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2 && args.toggle);
	CHECK(Parse(4, replay, &args));
	CHECK(args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1 && strcmp(args.replayPath, "input.ctrreplay") == 0 && args.bypassHeaderIdentity);
	CHECK(Parse(3, replayV2, &args));
	CHECK(args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V2 && strcmp(args.replayPath, "input.v2.ctrreplay") == 0);
	CHECK(!Parse(4, conflict, &args));
	CHECK(!Parse(2, missing, &args));
	CHECK(!Parse(3, missingOption, &args));
	CHECK(!Parse(3, v2Detailed, &args));
	CHECK(!Parse(4, replayToggle, &args));
	CHECK(!Parse(4, v2Bypass, &args));
	return 0;
}

static int TestAtomicCopyGate(void)
{
	struct NativeCanonicalStateV1 source;
	struct NativeCanonicalStateV1 destination;
	struct NativeCanonicalStateV1 before;

	MakeState(&source);
	memset(&destination, 0xa5, sizeof(destination)); before = destination;
	CHECK(NativeReplayScheduler_CopyCanonicalEndState(0, 0u, NULL, NULL));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	CHECK(!NativeReplayScheduler_CopyCanonicalEndState(1, 77u, NULL, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.domainDigests[0] ^= 1;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndState(1, 77u, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.domainDigests[0] ^= 1;
	source.frameNumber = 76u;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndState(1, 77u, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.frameNumber = 78u;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndState(1, 77u, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.frameNumber = 77u;
	CHECK(NativeReplayScheduler_CopyCanonicalEndState(1, 77u, &source, &destination));
	CHECK(destination.frameNumber == source.frameNumber && destination.combinedDigest == source.combinedDigest &&
	      memcmp(destination.identity.build, source.identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
	source.control.gameMode1++;
	CHECK(destination.control.gameMode1 != source.control.gameMode1);
	return 0;
}

static int TestV2LifecycleGates(void)
{
	uint16_t consumed[2] = { 0xa5a5u, 0x5a5au };
	uint16_t before[2];

	memcpy(before, consumed, sizeof(before));
	/* Playback's normal nonzero packet is retained for EndFrame comparison. */
	CHECK(NativeReplayScheduler_CopyConsumedV2VSyncPacket(consumed, 2u, 0u, 30u));
	CHECK(consumed[0] == 30u && consumed[1] == before[1]);
	before[0] = consumed[0];
	CHECK(!NativeReplayScheduler_CopyConsumedV2VSyncPacket(consumed, 2u, 2u, 1u));
	CHECK(!NativeReplayScheduler_CopyConsumedV2VSyncPacket(consumed, 2u, 1u, 0u));
	CHECK(memcmp(consumed, before, sizeof(before)) == 0);

	/* Frame zero's begin observation is deferred until checkpoint restore. */
	CHECK(!NativeReplayScheduler_V2BeginObservationNeedsValidation(0, 1));
	CHECK(!NativeReplayScheduler_V2BeginObservationNeedsValidation(1, 0));
	CHECK(NativeReplayScheduler_V2BeginObservationNeedsValidation(1, 1));

	/* Every fatal record path poisons finalization, even after frames exist. */
	CHECK(NativeReplayScheduler_V2RecordMayFinalize(0, 1));
	CHECK(!NativeReplayScheduler_V2RecordMayFinalize(1, 1));
	CHECK(!NativeReplayScheduler_V2RecordMayFinalize(0, 0));
	return 0;
}

int main(void)
{
	if ((TestRequirementModes() != 0) || (TestCliMatrix() != 0) || (TestAtomicCopyGate() != 0) || (TestV2LifecycleGates() != 0)) return 1;
	puts("native_replay_scheduler_seam_test: passed");
	return 0;
}
