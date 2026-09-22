#include "platform/native_lockstep_match_outcome.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define BUNDLE_BYTES NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES
#define INPUT_DELAY 2u
#define SLOT_A 0u /* CAB1_HUMAN in the two-cabinet profile. */
#define SLOT_B 1u /* CAB2_HUMAN. */
#define DOMAIN_WORLD 4u

/* Two sessions on the stack would be a needless several tens of kilobytes. */
static struct NativeLockstepSession g_a;
static struct NativeLockstepSession g_b;

static void FillConfig(struct NativeMatchConfigV1 *config, uint32_t trackID)
{
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = trackID;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
	for (uint8_t i = 0; i <= 5; i++)
	{
		config->slots[i].characterID = i;
		config->slots[i].difficulty = i < 2 ? 2 : 3;
	}
}

static int MakeState(struct NativeCanonicalStateV4 *state, uint32_t frame, uint32_t worldCount)
{
	NativeCanonicalStateV4_Init(state);
	state->frameNumber = frame;
	state->control.frameCounter = (int32_t)frame;
	state->worldCounters.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
	state->worldCounters.activeBombMissileCount = worldCount;
	CHECK(NativeCanonicalStateV4_ComputeDigests(state) == 1);
	return 0;
}

static void MakePad(struct NativeCanonicalInputPadV1 *pad, uint32_t slot, uint32_t frame)
{
	memset(pad, 0, sizeof(*pad));
	pad->status = (uint8_t)(0x40u + slot);
	pad->id = (uint8_t)slot;
	pad->buttons[0] = (uint8_t)(frame & 0xFFu);
	pad->buttons[1] = (uint8_t)((frame >> 8u) & 0xFFu);
	pad->analog[0] = (uint8_t)(0x80u + slot);
	pad->connected = 1u;
}

static int OpenPair(const struct NativeMatchConfigV1 *config)
{
	NativeLockstepSession_Init(&g_a);
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_a, config, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepSession_Open(&g_b, config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	return 0;
}

struct FrameTrace
{
	enum NativeLockstepSessionResult acceptedByA;
	enum NativeLockstepSessionResult acceptedByB;
	enum NativeLockstepSessionResult takenByA;
	enum NativeLockstepSessionResult takenByB;
	struct NativeLockstepSessionFrameInputs inputsA;
	struct NativeLockstepSessionFrameInputs inputsB;
};

/* Same driving style as tests/native_lockstep_session_test.c RunFrame. */
static int RunFrame(uint32_t frame, uint32_t worldA, uint32_t worldB, struct FrameTrace *trace)
{
	struct NativeCanonicalInputPadV1 pad;
	struct NativeCanonicalStateV4 state;
	uint8_t bytesA[BUNDLE_BYTES];
	uint8_t bytesB[BUNDLE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;

	memset(trace, 0, sizeof(*trace));
	MakePad(&pad, SLOT_A, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, frame, &pad) == 1);
	MakePad(&pad, SLOT_B, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, frame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, frame, bytesA, sizeof(bytesA), &sizeA) == 1);
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, frame, bytesB, sizeof(bytesB), &sizeB) == 1);

	trace->acceptedByB = NativeLockstepSession_AcceptBundle(&g_b, bytesA, sizeA);
	trace->acceptedByA = NativeLockstepSession_AcceptBundle(&g_a, bytesB, sizeB);

	trace->takenByA = NativeLockstepSession_TakeFrameInputs(&g_a, frame, &trace->inputsA);
	trace->takenByB = NativeLockstepSession_TakeFrameInputs(&g_b, frame, &trace->inputsB);

	if (trace->takenByA == NATIVE_LOCKSTEP_SESSION_OK)
	{
		CHECK(MakeState(&state, frame, worldA) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(&g_a, &state) == 1);
	}
	if (trace->takenByB == NATIVE_LOCKSTEP_SESSION_OK)
	{
		CHECK(MakeState(&state, frame, worldB) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	}
	return 0;
}

static int TestInitRejects(void)
{
	struct NativeLockstepMatchOutcomeTracker tracker;

	CHECK(NativeLockstepMatchOutcome_Init(NULL, 0u) == 0);
	CHECK(NativeLockstepMatchOutcome_Init(&tracker, NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES - 1u) == 0);
	CHECK(NativeLockstepMatchOutcome_Init(&tracker, NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES + 1u) == 0);

	CHECK(NativeLockstepMatchOutcome_Init(&tracker, 0u) == 1);
	CHECK(tracker.stallTimeoutFrames == NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES);
	CHECK(tracker.consecutiveStallFrames == 0u);
	CHECK(tracker.latched == 0u);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&tracker) == NULL);

	CHECK(NativeLockstepMatchOutcome_Init(&tracker, NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES) == 1);
	CHECK(tracker.stallTimeoutFrames == NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES);

	CHECK(NativeLockstepMatchOutcome_Init(&tracker, NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES) == 1);
	CHECK(tracker.stallTimeoutFrames == NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES);
	return 0;
}

/*
 * Below-timeout stalls never latch, and the reset-then-fresh-run behavior
 * requires a fresh run of stallTimeoutFrames consecutive stalls after an OK
 * result resets the counter.  Reaching the timeout exactly latches
 * STALL_TIMEOUT with the right fields, and the latch is once-only.
 */
static int TestStallTimeoutSequence(void)
{
	struct NativeLockstepMatchOutcomeTracker tracker;
	const struct NativeLockstepMatchOutcomeReport *report;
	const uint32_t timeout = NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES;
	enum NativeLockstepSessionResult result;
	struct NativeLockstepSession dummySession;
	struct NativeMatchConfigV1 config;

	FillConfig(&config, UINT32_C(0x0a0b0c0d));
	NativeLockstepSession_Init(&dummySession);
	CHECK(NativeLockstepSession_Open(&dummySession, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 1);

	CHECK(NativeLockstepMatchOutcome_Init(&tracker, timeout) == 1);

	/* Below the configured timeout: FirstOutcome stays NULL and Poll returns
	 * the passed-through result unchanged. */
	for (uint32_t i = 0; i < timeout - 1u; i++)
	{
		result = NativeLockstepMatchOutcome_Poll(&tracker, &dummySession, NATIVE_LOCKSTEP_SESSION_STALL, i);
		CHECK(result == NATIVE_LOCKSTEP_SESSION_STALL);
		CHECK(NativeLockstepMatchOutcome_FirstOutcome(&tracker) == NULL);
	}
	CHECK(tracker.consecutiveStallFrames == timeout - 1u);

	/* An OK result between two runs of STALL resets the counter. */
	result = NativeLockstepMatchOutcome_Poll(&tracker, &dummySession, NATIVE_LOCKSTEP_SESSION_OK, timeout);
	CHECK(result == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(tracker.consecutiveStallFrames == 0u);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&tracker) == NULL);

	/* A short run of stalls, still below the timeout, does not latch. */
	for (uint32_t i = 0; i < timeout - 1u; i++)
	{
		result = NativeLockstepMatchOutcome_Poll(&tracker, &dummySession, NATIVE_LOCKSTEP_SESSION_STALL, timeout + 1u + i);
		CHECK(result == NATIVE_LOCKSTEP_SESSION_STALL);
		CHECK(NativeLockstepMatchOutcome_FirstOutcome(&tracker) == NULL);
	}
	CHECK(tracker.consecutiveStallFrames == timeout - 1u);

	/* Exactly reaching the timeout, as a fresh run after the reset, latches. */
	{
		const uint32_t timeoutFrame = timeout + 1u + (timeout - 1u);

		result = NativeLockstepMatchOutcome_Poll(&tracker, &dummySession, NATIVE_LOCKSTEP_SESSION_STALL, timeoutFrame);
		CHECK(result == NATIVE_LOCKSTEP_SESSION_STALL);
		CHECK(tracker.consecutiveStallFrames == timeout);
		CHECK(tracker.latched == 1u);

		report = NativeLockstepMatchOutcome_FirstOutcome(&tracker);
		CHECK(report != NULL);
		CHECK(report->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT);
		CHECK(report->frameIndex == timeoutFrame);
		CHECK(report->senderSlot == NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT);
		CHECK(report->stalledFrameCount == timeout);
	}

	/* Latch-once: further polls, even OK or more STALL, do not change it. */
	{
		struct NativeLockstepMatchOutcomeReport latched = *report;

		result = NativeLockstepMatchOutcome_Poll(&tracker, &dummySession, NATIVE_LOCKSTEP_SESSION_OK, 999u);
		CHECK(result == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(memcmp(NativeLockstepMatchOutcome_FirstOutcome(&tracker), &latched, sizeof(latched)) == 0);

		result = NativeLockstepMatchOutcome_Poll(&tracker, &dummySession, NATIVE_LOCKSTEP_SESSION_STALL, 1000u);
		CHECK(result == NATIVE_LOCKSTEP_SESSION_STALL);
		CHECK(memcmp(NativeLockstepMatchOutcome_FirstOutcome(&tracker), &latched, sizeof(latched)) == 0);
		CHECK(tracker.consecutiveStallFrames == timeout);
	}
	return 0;
}

/*
 * A real divergence outranks a simultaneous stall condition, and the latched
 * report copies the frame and slot from NativeLockstepSession_FirstDivergence.
 */
static int TestDivergenceLatches(void)
{
	struct NativeMatchConfigV1 config;
	struct FrameTrace trace;
	struct NativeLockstepMatchOutcomeTracker tracker;
	const struct NativeLockstepMatchOutcomeReport *report;
	const struct NativeLockstepDivergenceReport *divergence;
	const uint32_t divergeFrame = 6u;
	const uint32_t detectFrame = divergeFrame + INPUT_DELAY + 1u;
	const uint32_t perturbedWorld = 3u;

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config) == 0);
	CHECK(NativeLockstepMatchOutcome_Init(&tracker, 0u) == 1);

	for (uint32_t frame = 0; frame < detectFrame; frame++)
	{
		CHECK(RunFrame(frame, frame >= divergeFrame ? perturbedWorld : 0u, 0u, &trace) == 0);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(NativeLockstepMatchOutcome_Poll(&tracker, &g_b, trace.takenByB, frame) == trace.takenByB);
		CHECK(NativeLockstepMatchOutcome_FirstOutcome(&tracker) == NULL);
	}

	CHECK(RunFrame(detectFrame, perturbedWorld, 0u, &trace) == 0);
	CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_REJECTED);
	divergence = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(divergence != NULL);
	CHECK(divergence->frameIndex == divergeFrame);

	CHECK(NativeLockstepMatchOutcome_Poll(&tracker, &g_b, trace.takenByB, detectFrame) == trace.takenByB);
	report = NativeLockstepMatchOutcome_FirstOutcome(&tracker);
	CHECK(report != NULL);
	CHECK(report->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED);
	CHECK(report->frameIndex == divergence->frameIndex);
	CHECK(report->senderSlot == divergence->senderSlot);
	CHECK(report->stalledFrameCount == 0u);

	/* Further polls, even with a stall result, do not change the latch. */
	CHECK(NativeLockstepMatchOutcome_Poll(&tracker, &g_b, NATIVE_LOCKSTEP_SESSION_STALL, detectFrame + 1u) ==
	      NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(memcmp(NativeLockstepMatchOutcome_FirstOutcome(&tracker), report, sizeof(*report)) == 0);
	return 0;
}

/*
 * A real protocol fault latches FAULTED with the fault report frame and slot;
 * a FAULT lastTakeResult or stall state does not also latch STALL_TIMEOUT.
 */
static int TestFaultLatches(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchOutcomeTracker tracker;
	const struct NativeLockstepMatchOutcomeReport *report;
	const struct NativeLockstepFaultReport *fault;
	uint8_t bytes[BUNDLE_BYTES];
	size_t size = 0;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_a);
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepMatchOutcome_Init(&tracker, 0u) == 1);

	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, 0u, bytes, sizeof(bytes), &size) == 1);
	/* A corrupted body byte, so the trailing digest goes stale: a clean
	 * protocol fault, the same technique as native_lockstep_session_test.c. */
	bytes[24] ^= 0x01u;

	CHECK(NativeLockstepSession_AcceptBundle(&g_a, bytes, size) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_a);
	CHECK(fault != NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) == NULL);

	CHECK(NativeLockstepMatchOutcome_Poll(&tracker, &g_a, NATIVE_LOCKSTEP_SESSION_FAULT, 0u) == NATIVE_LOCKSTEP_SESSION_FAULT);
	report = NativeLockstepMatchOutcome_FirstOutcome(&tracker);
	CHECK(report != NULL);
	CHECK(report->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED);
	CHECK(report->frameIndex == fault->frameIndex);
	CHECK(report->senderSlot == fault->senderSlot);
	CHECK(report->stalledFrameCount == 0u);

	/* A stall state afterwards does not also latch STALL_TIMEOUT. */
	CHECK(NativeLockstepMatchOutcome_Poll(&tracker, &g_a, NATIVE_LOCKSTEP_SESSION_STALL, 1u) == NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(memcmp(NativeLockstepMatchOutcome_FirstOutcome(&tracker), report, sizeof(*report)) == 0);
	return 0;
}

static int TestNullIsSafeNoOp(void)
{
	struct NativeLockstepMatchOutcomeTracker tracker;
	struct NativeMatchConfigV1 config;
	struct NativeLockstepSession session;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&session);
	CHECK(NativeLockstepSession_Open(&session, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepMatchOutcome_Init(&tracker, 0u) == 1);

	CHECK(NativeLockstepMatchOutcome_Poll(NULL, &session, NATIVE_LOCKSTEP_SESSION_STALL, 0u) == NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(NativeLockstepMatchOutcome_Poll(&tracker, NULL, NATIVE_LOCKSTEP_SESSION_OK, 0u) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(tracker.consecutiveStallFrames == 0u);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&tracker) == NULL);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(NULL) == NULL);
	return 0;
}

int main(void)
{
	CHECK(TestInitRejects() == 0);
	CHECK(TestStallTimeoutSequence() == 0);
	CHECK(TestDivergenceLatches() == 0);
	CHECK(TestFaultLatches() == 0);
	CHECK(TestNullIsSafeNoOp() == 0);
	return 0;
}
