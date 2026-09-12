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

static int MakeStateV3(struct NativeCanonicalStateV3 *state)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	NativeCanonicalStateV3_Init(state);
	state->frameNumber = 77u;
	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		state->identity.build[i] = (uint8_t)i;
		state->identity.content[i] = (uint8_t)(0x80u + i);
	}
	state->control.gameMode1 = 1;
	state->rng.advRng1 = 2;
	state->input.pads[0].connected = 1;
	stream[NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES] = 1;
	if (!NativeCanonicalDriversV1_FromNormativeStream(&state->drivers, 1u, stream, sizeof(stream))) return 0;
	return NativeCanonicalStateV3_ComputeDigests(state);
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
	CHECK(!NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V3));
	CHECK(NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V3));
	CHECK(NativeReplayScheduler_ModeRequiresCanonicalState(NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V3));
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
	char *recordV3[] = { "ctr_native", "--record-v3" };
	char *replayV3[] = { "ctr_native", "--replay-v3", "input.v3.ctrreplay" };
	char *v3Toggle[] = { "ctr_native", "--record-v3", "--toggle" };
	char *v3Detailed[] = { "ctr_native", "--record-v3", "--detailed" };
	char *v3Bypass[] = { "ctr_native", "--replay-v3", "x", "--replay-bypass-header" };
	char *v3Conflict[] = { "ctr_native", "--record-v3", "--record-v2" };

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
	CHECK(Parse(2, recordV3, &args) && args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V3);
	CHECK(Parse(3, replayV3, &args) && args.selector == NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V3 && strcmp(args.replayPath, "input.v3.ctrreplay") == 0);
	CHECK(!Parse(3, v3Toggle, &args));
	CHECK(!Parse(3, v3Detailed, &args));
	CHECK(!Parse(4, v3Bypass, &args));
	CHECK(!Parse(3, v3Conflict, &args));
	return 0;
}

static int TestAtomicV3CopyGate(void)
{
	struct NativeCanonicalStateV3 source, destination, before;
	struct NativeIdentityV1 wrong;
	CHECK(MakeStateV3(&source));
	memset(&destination, 0xa5, sizeof(destination)); before = destination;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndStateV3(77u, NULL, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	wrong = source.identity; wrong.build[0] ^= 1u;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndStateV3(77u, &wrong, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.frameNumber = 76u;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndStateV3(77u, &source.identity, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.frameNumber = 77u;
	source.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] ^= 1u;
	CHECK(!NativeReplayScheduler_CopyCanonicalEndStateV3(77u, &source.identity, &source, &destination));
	CHECK(memcmp(&destination, &before, sizeof(destination)) == 0);
	source.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] ^= 1u;
	CHECK(NativeReplayScheduler_CopyCanonicalEndStateV3(77u, &source.identity, &source, &destination));
	CHECK(destination.frameNumber == 77u && destination.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] == source.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u]);
	source.drivers.fullStreamDigest ^= 1u;
	CHECK(destination.drivers.fullStreamDigest != source.drivers.fullStreamDigest);
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
	CHECK(NativeReplayScheduler_V3RecordMayFinalize(0, 0, 1u, 1));
	CHECK(!NativeReplayScheduler_V3RecordMayFinalize(1, 0, 1u, 1));
	CHECK(!NativeReplayScheduler_V3RecordMayFinalize(0, 1, 1u, 1));
	CHECK(!NativeReplayScheduler_V3RecordMayFinalize(0, 0, 0u, 1));
	CHECK(!NativeReplayScheduler_V3RecordMayFinalize(0, 0, 2u, 1));
	CHECK(!NativeReplayScheduler_V3RecordMayFinalize(0, 0, 1u, 0));
	return 0;
}

static int TestV3MismatchReport(void)
{
	struct NativeReplayV3Frame expected;
	struct NativeCanonicalStateV3 live;
	struct NativeReplayV2FrameObservation liveEnd;
	struct NativeReplaySchedulerV3MismatchReport report, before;
	uint16_t packets[2] = { 3u, 9u };

	CHECK(MakeStateV3(&live));
	memset(&expected, 0, sizeof(expected));
	expected.padCount = NATIVE_REPLAY_V2_PAD_COUNT;
	expected.vsyncTotal = 4u;
	expected.vsyncPacketCount = 2u;
	expected.vsyncPackets[0] = 2u;
	expected.vsyncPackets[1] = 8u;
	expected.canonical = live;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_PAD_COUNT; i++)
	{
		expected.pads[i].status = live.input.pads[i].status;
		expected.pads[i].id = live.input.pads[i].id;
		expected.pads[i].buttons[0] = live.input.pads[i].buttons[0];
		expected.pads[i].buttons[1] = live.input.pads[i].buttons[1];
		memcpy(expected.pads[i].analog, live.input.pads[i].analog, sizeof(expected.pads[i].analog));
		expected.pads[i].connected = live.input.pads[i].connected;
	}
	memset(&liveEnd, 0, sizeof(liveEnd));
	expected.end = liveEnd;
	/* Perturb independent components; the report must retain every one. */
	liveEnd.timer = 1;
	live.input.pads[2].connected ^= 1u;
	live.domainDigests[NATIVE_CANONICAL_DOMAIN_INPUT - 1u] ^= 1u;
	live.combinedDigest ^= 1u;
	live.drivers.rosterMetaDigest ^= 1u;
	live.drivers.slots[0].slotDigest ^= 1u;
	live.drivers.slots[0].metaRaceDigest ^= 1u;
	live.drivers.slots[0].physicsDynamicsDigest ^= 1u;
	live.drivers.slots[0].behaviorBotDigest ^= 1u;
	live.drivers.fullStreamDigest ^= 1u;
	CHECK(NativeReplayScheduler_BuildV3MismatchReport(&expected, &liveEnd, 5u, 2u, packets, 1, &live, &report));
	CHECK(report.observationMismatch && report.vsyncTotalMismatch && !report.vsyncPacketCountMismatch && report.vsyncFirstPacketMismatch);
	CHECK(report.padMask == (UINT32_C(1) << 2u));
	CHECK(report.firstDomainID == NATIVE_CANONICAL_DOMAIN_INPUT && report.expectedDomainDigest != report.liveDomainDigest);
	CHECK(report.combinedMismatch && report.expectedCombinedDigest != report.liveCombinedDigest);
	CHECK(report.drivers.headerMask == 0u && report.drivers.rosterMask == 1u && report.drivers.slotMask == 1u &&
	      report.drivers.metaRaceMask == 1u && report.drivers.physicsDynamicsMask == 1u &&
	      report.drivers.behaviorBotMask == 1u && report.drivers.fullStreamMask == 1u);
	/* Invalid/forged summary is a retained header diagnostic, rather than a
	 * silent generic divergence.  Failed builder calls preserve outputs. */
	live.drivers.version = 1u;
	CHECK(NativeReplayScheduler_BuildV3MismatchReport(&expected, &expected.end, 4u, 2u, expected.vsyncPackets, 0, &live, &report));
	CHECK(report.drivers.headerMask == UINT32_MAX);
	before = report;
	CHECK(!NativeReplayScheduler_BuildV3MismatchReport(NULL, &expected.end, 4u, 2u, expected.vsyncPackets, 0, &live, &report));
	CHECK(memcmp(&report, &before, sizeof(report)) == 0);
	return 0;
}

int main(void)
{
	if ((TestRequirementModes() != 0) || (TestCliMatrix() != 0) || (TestAtomicCopyGate() != 0) || (TestAtomicV3CopyGate() != 0) ||
	    (TestV2LifecycleGates() != 0) || (TestV3MismatchReport() != 0)) return 1;
	puts("native_replay_scheduler_seam_test: passed");
	return 0;
}
