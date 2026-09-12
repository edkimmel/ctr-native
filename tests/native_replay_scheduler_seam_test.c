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
	CHECK(NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_FUTURE_V2_TEST));
	return 0;
}

static int TestAtomicCopyGate(void)
{
	struct NativeCanonicalStateV1 source;
	struct NativeCanonicalStateV1 destination;
	struct NativeCanonicalStateV1 before;

	MakeState(&source);
	memset(&destination, 0xa5, sizeof(destination)); before = destination;
	CHECK(NativeReplayScheduler_CopyCanonicalEndState(0, NULL, NULL));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	CHECK(!NativeReplayScheduler_CopyCanonicalEndState(1, NULL, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.domainDigests[0] ^= 1;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndState(1, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.domainDigests[0] ^= 1;
	CHECK(NativeReplayScheduler_CopyCanonicalEndState(1, &source, &destination));
	CHECK(destination.frameNumber == source.frameNumber && destination.combinedDigest == source.combinedDigest &&
	      memcmp(destination.identity.build, source.identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
	source.control.gameMode1++;
	CHECK(destination.control.gameMode1 != source.control.gameMode1);
	return 0;
}

int main(void)
{
	if ((TestRequirementModes() != 0) || (TestAtomicCopyGate() != 0)) return 1;
	puts("native_replay_scheduler_seam_test: passed");
	return 0;
}
