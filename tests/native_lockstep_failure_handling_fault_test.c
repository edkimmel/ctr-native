#include "platform/native_lockstep_match_outcome.h"
#include "platform/native_lockstep_match_roster.h"
#include "platform/native_lockstep_rematch.h"
#include "platform/native_lockstep_session.h"
#include "platform/native_virtual_datagram.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define BUNDLE_BYTES NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES
#define INPUT_DELAY 2u
#define SLOT_A 0u /* CAB1_HUMAN in the two-cabinet profile; also datagram endpoint 0. */
#define SLOT_B 1u /* CAB2_HUMAN; also datagram endpoint 1. */
#define QUEUE_CAPACITY 8u

/* One distinct trackID per scenario, the same convention this milestone's
 * fault-injection tests use. */
#define TRACK_ID_SCENARIO_1 UINT32_C(0x01020304)
#define TRACK_ID_SCENARIO_2 UINT32_C(0x0a0b0c0d)
#define TRACK_ID_SCENARIO_3_A UINT32_C(0x11223344)
#define TRACK_ID_SCENARIO_3_B UINT32_C(0x55667788)

/*
 * The sessions are file scope because each holds a ring per config slot; two
 * of them on the stack would be a needless several tens of kilobytes, the
 * same reasoning tests/native_lockstep_session_test.c gives.  g_a/g_b carry
 * scenarios 1 and 2, so g_b is left latched DIVERGED at the end of scenario
 * 2 for scenario 4 to prove reopen-refusal on.  g_a3/g_b3 are scenario 3's
 * own pair, so its match-identity fault does not re-Init g_b out from under
 * scenario 2's latch before scenario 4 runs.  g_a2/g_b2 are the brand-new
 * pair scenario 4 opens on its rematch config.
 */
static struct NativeLockstepSession g_a;
static struct NativeLockstepSession g_b;
static struct NativeLockstepSession g_a3;
static struct NativeLockstepSession g_b3;
static struct NativeLockstepSession g_a2;
static struct NativeLockstepSession g_b2;

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

/*
 * A valid canonical state whose digests are a pure function of the frame and
 * of one WORLD counter, the same fixture tests/native_lockstep_session_test.c
 * uses.
 */
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

static int DecodeBundle(const struct NativeLockstepSession *receiver, const uint8_t *bytes, struct NativeLockstepBundleV1 *bundle)
{
	struct NativeCodecReader reader;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_NONE;

	NativeCodecReader_Init(&reader, bytes, BUNDLE_BYTES);
	CHECK(NativeLockstepBundleV1_Decode(&reader, receiver->matchIdentity, receiver->protocolVersion, receiver->inputDelay, bundle,
	                                    &cause) == 1);
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_NONE);
	return 0;
}

/*
 * Opens the file-scope g_a/g_b pair on the same freshly built config, whose
 * trackID is the caller's own scenario constant.  *configOut is populated so
 * the caller can also initialize an outcome tracker/roster from the identical
 * config.
 */
static int OpenPair(uint32_t trackID, uint32_t delayA, uint32_t delayB, struct NativeMatchConfigV1 *configOut)
{
	FillConfig(configOut, trackID);
	NativeLockstepSession_Init(&g_a);
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_a, configOut, delayA, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepSession_Open(&g_b, configOut, delayB, (uint8_t)SLOT_B) == 1);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	return 0;
}

/* One driven lockstep frame's outcome, mirroring struct FrameTrace in
 * tests/native_lockstep_transport_fault_test.c. */
struct FrameTrace
{
	struct NativeLockstepBundleV1 fromA; /* B's decode of A's bundle. */
	struct NativeLockstepBundleV1 fromB; /* A's decode of B's bundle. */
	enum NativeLockstepSessionResult acceptedByA;
	enum NativeLockstepSessionResult acceptedByB;
	enum NativeLockstepSessionResult takenByA;
	enum NativeLockstepSessionResult takenByB;
	struct NativeLockstepSessionFrameInputs inputsA;
	struct NativeLockstepSessionFrameInputs inputsB;
};

/*
 * Drains every ready record addressed to destination and hands each straight
 * to receiver's AcceptBundle, decoding the last one accepted into *decodedOut
 * when it is non-NULL.  Returns the number of records drained; a caller that
 * expects exactly one checks the count itself.  DROP creates no queued
 * delivery at all, so a frame routed that way drains zero records here.
 */
static int DrainAndAccept(struct NativeVirtualDatagramPair *pair, uint32_t destination, struct NativeLockstepSession *receiver,
                          struct NativeLockstepBundleV1 *decodedOut, enum NativeLockstepSessionResult *lastResultOut, int *countOut)
{
	uint8_t bytes[BUNDLE_BYTES];
	size_t size;
	struct NativeVirtualDatagramMetadata metadata;
	enum NativeVirtualDatagramReceiveResult received;
	int count = 0;

	for (;;)
	{
		size = sizeof(bytes);
		received = NativeVirtualDatagramPair_Receive(pair, destination, bytes, &size, &metadata);
		if (received == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY)
			break;
		CHECK(received == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(size == BUNDLE_BYTES);
		count++;
		if (lastResultOut)
			*lastResultOut = NativeLockstepSession_AcceptBundle(receiver, bytes, size);
		else
			CHECK(NativeLockstepSession_AcceptBundle(receiver, bytes, size) != NATIVE_LOCKSTEP_SESSION_REJECTED);
		if (decodedOut)
			CHECK(DecodeBundle(receiver, bytes, decodedOut) == 0);
	}
	if (countOut)
		*countOut = count;
	return 0;
}

/*
 * One clean lockstep frame driven entirely over the virtual datagram pair,
 * parameterized on the session pair so scenario 4 can drive a brand-new
 * sessionA/sessionB without disturbing g_a/g_b: sample and buffer local
 * input, compose both bundles, send both with an immediate DELIVER route at
 * the given virtual step, advance the pair to that step, drain and accept
 * both directions, consume the committed input set, and record the
 * simulated frame's canonical digests on success.  Mirrors RunCleanFrame in
 * tests/native_lockstep_transport_fault_test.c.
 */
static int RunCleanFrame(struct NativeLockstepSession *sessionA, struct NativeLockstepSession *sessionB,
                         struct NativeVirtualDatagramPair *pair, uint32_t frame, uint64_t step, uint32_t worldA, uint32_t worldB,
                         struct FrameTrace *trace)
{
	struct NativeCanonicalInputPadV1 pad;
	struct NativeCanonicalStateV4 state;
	uint8_t bytesA[BUNDLE_BYTES];
	uint8_t bytesB[BUNDLE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, step, 0u };
	int countA = 0;
	int countB = 0;

	memset(trace, 0, sizeof(*trace));
	MakePad(&pad, SLOT_A, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(sessionA, frame, &pad) == 1);
	MakePad(&pad, SLOT_B, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(sessionB, frame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(sessionA, frame, bytesA, sizeof(bytesA), &sizeA) == 1);
	CHECK(NativeLockstepSession_ComposeBundle(sessionB, frame, bytesB, sizeof(bytesB), &sizeB) == 1);
	CHECK(sizeA == BUNDLE_BYTES && sizeB == BUNDLE_BYTES);

	CHECK(NativeVirtualDatagramPair_Send(pair, SLOT_A, bytesA, sizeA, &immediate));
	CHECK(NativeVirtualDatagramPair_Send(pair, SLOT_B, bytesB, sizeB, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(pair, step));

	CHECK(DrainAndAccept(pair, SLOT_B, sessionB, &trace->fromA, &trace->acceptedByB, &countB) == 0);
	CHECK(DrainAndAccept(pair, SLOT_A, sessionA, &trace->fromB, &trace->acceptedByA, &countA) == 0);
	CHECK(countA == 1 && countB == 1);

	trace->takenByA = NativeLockstepSession_TakeFrameInputs(sessionA, frame, &trace->inputsA);
	trace->takenByB = NativeLockstepSession_TakeFrameInputs(sessionB, frame, &trace->inputsB);

	if (trace->takenByA == NATIVE_LOCKSTEP_SESSION_OK)
	{
		CHECK(MakeState(&state, frame, worldA) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(sessionA, &state) == 1);
	}
	if (trace->takenByB == NATIVE_LOCKSTEP_SESSION_OK)
	{
		CHECK(MakeState(&state, frame, worldB) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(sessionB, &state) == 1);
	}
	return 0;
}

/*
 * Scenario 1 (stall timeout): A never receives anything from B at all, so
 * every TakeFrameInputs call on A stalls forever.  The outcome tracker turns
 * that indefinitely retried stall into a terminated STALL_TIMEOUT outcome
 * after exactly stallTimeoutFrames consecutive stalled polls, and the roster
 * then drops the (unreachable) remote peer -- all without the session's own
 * mode ever leaving RUNNING, because a stall is not itself a session-level
 * terminal condition.
 */
static int TestStallTimeoutDropsPeer(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchOutcomeTracker trackerA;
	struct NativeLockstepMatchRoster rosterA;
	struct NativeLockstepSessionFrameInputs inputs;
	const struct NativeLockstepMatchOutcomeReport *outcome;
	enum NativeLockstepSessionResult taken;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(TRACK_ID_SCENARIO_1, INPUT_DELAY, INPUT_DELAY, &config) == 0);
	CHECK(NativeLockstepMatchOutcome_Init(&trackerA, NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES) == 1);
	CHECK(NativeLockstepMatchRoster_Init(&rosterA, &config) == 1);

	for (uint32_t i = 1u; i <= NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES; i++)
	{
		taken = NativeLockstepSession_TakeFrameInputs(&g_a, 0u, &inputs);
		CHECK(taken == NATIVE_LOCKSTEP_SESSION_STALL);
		CHECK(NativeLockstepMatchOutcome_Poll(&trackerA, &g_a, taken, 0u) == taken);
		outcome = NativeLockstepMatchOutcome_FirstOutcome(&trackerA);
		if (i < NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES)
		{
			CHECK(outcome == NULL);
		}
		else
		{
			CHECK(outcome != NULL);
			CHECK(outcome->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT);
			CHECK(outcome->frameIndex == 0u);
			CHECK(outcome->senderSlot == NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT);
			CHECK(outcome->stalledFrameCount == NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES);
		}
		CHECK(g_a.consumedFrame == 0u);
		CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	}

	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&rosterA, (uint8_t)SLOT_A, NativeLockstepMatchOutcome_FirstOutcome(&trackerA)) == 1);
	CHECK(rosterA.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(rosterA.lifecycle[SLOT_A] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	return 0;
}

/*
 * Scenario 2 (divergence): a real divergence is forced over the virtual
 * datagram pair exactly the way tests/native_lockstep_session_test.c's
 * TestDivergence forces it directly, then B's outcome tracker turns the
 * session's own latched divergence report into a DIVERGED outcome and the
 * roster drops A.
 */
static int TestDivergenceDropsPeer(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeMatchConfigV1 config;
	struct FrameTrace trace;
	struct NativeLockstepMatchOutcomeTracker trackerB;
	struct NativeLockstepMatchRoster rosterB;
	const struct NativeLockstepMatchOutcomeReport *outcome;
	const uint32_t divergeFrame = 6u;
	const uint32_t detectFrame = divergeFrame + INPUT_DELAY + 1u;
	const uint32_t perturbedWorld = 3u;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(TRACK_ID_SCENARIO_2, INPUT_DELAY, INPUT_DELAY, &config) == 0);

	for (uint32_t frame = 0; frame <= detectFrame; frame++)
	{
		uint64_t step = frame;
		/* Side A's simulation starts drifting at divergeFrame; side B never
		 * drifts. */
		CHECK(RunCleanFrame(&g_a, &g_b, &pair, frame, step, frame >= divergeFrame ? perturbedWorld : 0u, 0u, &trace) == 0);
		if (frame < detectFrame)
		{
			CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
			CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
			CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
			CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
			CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
		}
		else
		{
			CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
			CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
			/* DIVERGED is terminal. */
			CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_REJECTED);
			CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_REJECTED);
		}
	}
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);

	CHECK(NativeLockstepMatchOutcome_Init(&trackerB, 0u) == 1);
	CHECK(NativeLockstepMatchOutcome_Poll(&trackerB, &g_b, NATIVE_LOCKSTEP_SESSION_DIVERGENCE, detectFrame) ==
	      NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	outcome = NativeLockstepMatchOutcome_FirstOutcome(&trackerB);
	CHECK(outcome != NULL);
	CHECK(outcome->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED);
	/* The frame that actually diverged, not the frame that noticed it. */
	CHECK(outcome->frameIndex == divergeFrame);
	CHECK(outcome->senderSlot == SLOT_A);
	CHECK(outcome->stalledFrameCount == 0u);

	CHECK(NativeLockstepMatchRoster_Init(&rosterB, &config) == 1);
	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&rosterB, (uint8_t)SLOT_B, outcome) == 1);
	CHECK(rosterB.lifecycle[SLOT_A] == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(rosterB.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	return 0;
}

/*
 * Scenario 3 (protocol fault): two sessions opened on two configs whose
 * trackID differs give them different matchIdentity, exactly the setup
 * tests/native_lockstep_session_test.c's TestMatchIdentityMismatch uses, so
 * a single bundle from A is a clean MATCH_IDENTITY fault on B, detected on
 * decode before any window logic runs.  B's outcome tracker turns that
 * latched fault into a FAULTED outcome and the roster drops A.
 */
static int TestFaultDropsPeer(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeMatchConfigV1 configA;
	struct NativeMatchConfigV1 configB;
	struct NativeLockstepMatchOutcomeTracker trackerB;
	struct NativeLockstepMatchRoster rosterB;
	const struct NativeLockstepMatchOutcomeReport *outcome;
	const struct NativeLockstepFaultReport *fault;
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	uint8_t bytesA[BUNDLE_BYTES];
	size_t sizeA = 0;
	enum NativeLockstepSessionResult acceptedByB = NATIVE_LOCKSTEP_SESSION_REJECTED;
	int countB = 0;

	FillConfig(&configA, TRACK_ID_SCENARIO_3_A);
	FillConfig(&configB, TRACK_ID_SCENARIO_3_B);
	/* g_a3/g_b3, not g_a/g_b: scenario 2 left g_b latched DIVERGED and
	 * scenario 4 still needs to observe that, so this scenario's Init calls
	 * must not land on the same file-scope pair. */
	NativeLockstepSession_Init(&g_a3);
	NativeLockstepSession_Init(&g_b3);
	CHECK(NativeLockstepSession_Open(&g_a3, &configA, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepSession_Open(&g_b3, &configB, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	CHECK(memcmp(g_a3.matchIdentity, g_b3.matchIdentity, sizeof(g_a3.matchIdentity)) != 0);

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(NativeLockstepSession_ComposeBundle(&g_a3, 0u, bytesA, sizeof(bytesA), &sizeA) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA, sizeA, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 0u));
	CHECK(DrainAndAccept(&pair, SLOT_B, &g_b3, NULL, &acceptedByB, &countB) == 0);
	CHECK(countB == 1);
	CHECK(acceptedByB == NATIVE_LOCKSTEP_SESSION_FAULT);

	fault = NativeLockstepSession_FirstFault(&g_b3);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY);

	CHECK(NativeLockstepMatchOutcome_Init(&trackerB, 0u) == 1);
	CHECK(NativeLockstepMatchOutcome_Poll(&trackerB, &g_b3, NATIVE_LOCKSTEP_SESSION_FAULT, 0u) == NATIVE_LOCKSTEP_SESSION_FAULT);
	outcome = NativeLockstepMatchOutcome_FirstOutcome(&trackerB);
	CHECK(outcome != NULL);
	CHECK(outcome->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED);
	/* Nothing read out of an undecodable record is trusted, so both are 0u. */
	CHECK(outcome->frameIndex == fault->frameIndex);
	CHECK(outcome->senderSlot == fault->senderSlot);
	CHECK(outcome->frameIndex == 0u);
	CHECK(outcome->senderSlot == 0u);
	CHECK(outcome->stalledFrameCount == 0u);

	CHECK(NativeLockstepMatchRoster_Init(&rosterB, &configB) == 1);
	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&rosterB, (uint8_t)SLOT_B, outcome) == 1);
	CHECK(rosterB.lifecycle[SLOT_A] == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	return 0;
}

/*
 * Scenario 4 (rematch is genuinely clean): a rematch config built from
 * scenario 2's now-DIVERGED config never carries over any terminal state.
 * The old, still-DIVERGED g_b must refuse to reopen even on the rematch
 * config, and a brand-new session/tracker/roster triple opened on that same
 * config behaves as if nothing had ever happened.
 */
static int TestRematchStartsCleanSession(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeMatchConfigV1 previous;
	struct NativeMatchConfigV1 next;
	struct NativeLockstepMatchOutcomeTracker trackerA2;
	struct NativeLockstepMatchOutcomeTracker trackerB2;
	struct NativeLockstepMatchRoster rosterA2;
	struct NativeLockstepMatchRoster rosterB2;
	struct FrameTrace trace;

	FillConfig(&previous, TRACK_ID_SCENARIO_2);
	CHECK(NativeLockstepRematch_BuildConfig(&previous, previous.masterSeed + 1u, &next) == 1);
	CHECK(NativeMatchConfigV1_Validate(&next) == 1);
	CHECK(next.trackID == previous.trackID);
	CHECK(next.masterSeed != previous.masterSeed);

	/* The old session from scenario 2 is still latched DIVERGED and must
	 * refuse to be reopened, even on a fresh config. */
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(NativeLockstepSession_Open(&g_b, &next, INPUT_DELAY, (uint8_t)SLOT_B) == 0);

	NativeLockstepSession_Init(&g_a2);
	NativeLockstepSession_Init(&g_b2);
	CHECK(NativeLockstepSession_Open(&g_a2, &next, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepSession_Open(&g_b2, &next, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	CHECK(NativeLockstepSession_Mode(&g_a2) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b2) == NATIVE_LOCKSTEP_RUNNING);

	CHECK(NativeLockstepMatchOutcome_Init(&trackerA2, 0u) == 1);
	CHECK(NativeLockstepMatchOutcome_Init(&trackerB2, 0u) == 1);
	CHECK(NativeLockstepMatchRoster_Init(&rosterA2, &next) == 1);
	CHECK(NativeLockstepMatchRoster_Init(&rosterB2, &next) == 1);

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	for (uint32_t frame = 0; frame < 4u; frame++)
	{
		uint64_t step = frame;
		CHECK(RunCleanFrame(&g_a2, &g_b2, &pair, frame, step, 0u, 0u, &trace) == 0);
		CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
	}

	CHECK(NativeLockstepSession_Mode(&g_a2) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b2) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a2) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b2) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_a2) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b2) == NULL);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&trackerA2) == NULL);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&trackerB2) == NULL);
	CHECK(rosterA2.lifecycle[SLOT_A] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	CHECK(rosterA2.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	CHECK(rosterB2.lifecycle[SLOT_A] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	CHECK(rosterB2.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	return 0;
}

int main(void)
{
	CHECK(BUNDLE_BYTES == 128u);
	CHECK(TestStallTimeoutDropsPeer() == 0);
	CHECK(TestDivergenceDropsPeer() == 0);
	CHECK(TestFaultDropsPeer() == 0);
	CHECK(TestRematchStartsCleanSession() == 0);
	puts("native_lockstep_failure_handling_fault_test: passed");
	return 0;
}
