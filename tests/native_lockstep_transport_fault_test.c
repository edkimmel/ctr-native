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

/*
 * The sessions are file scope because each holds a ring per config slot; two
 * of them on the stack would be a needless several tens of kilobytes, the same
 * reasoning tests/native_lockstep_session_test.c gives.
 */
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
 * A hand-built peer record addressed to one receiver, bypassing the sender's
 * own recorded-digest history so a far-future frameIndex can be crafted
 * without first driving a real session that far, the same technique
 * tests/native_lockstep_session_test.c's CraftBundle uses.
 */
static int CraftBundle(const struct NativeLockstepSession *receiver, uint8_t senderSlot, uint32_t frameIndex, uint32_t verifiedFrameIndex,
                       int verifiedPresent, const uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT], uint64_t combinedDigest,
                       uint8_t bytes[BUNDLE_BYTES])
{
	struct NativeLockstepBundleV1 bundle;
	struct NativeCodecWriter writer;

	memset(&bundle, 0, sizeof(bundle));
	bundle.protocolVersion = receiver->protocolVersion;
	memcpy(bundle.matchIdentity, receiver->matchIdentity, sizeof(bundle.matchIdentity));
	bundle.frameIndex = frameIndex;
	bundle.inputDelay = receiver->inputDelay;
	bundle.senderSlot = senderSlot;
	bundle.padCount = 1u;
	bundle.pads[0].slotIndex = senderSlot;
	bundle.pads[0].pad.id = senderSlot;
	bundle.pads[0].pad.connected = 1u;
	bundle.pads[1].slotIndex = NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT;
	if (verifiedPresent != 0)
	{
		bundle.verifiedPresent = 1u;
		bundle.verifiedFrameIndex = verifiedFrameIndex;
		memcpy(bundle.verifiedDomainDigests, domainDigests, sizeof(bundle.verifiedDomainDigests));
		bundle.verifiedCombinedDigest = combinedDigest;
	}

	NativeCodecWriter_Init(&writer, bytes, BUNDLE_BYTES, NULL);
	CHECK(NativeLockstepBundleV1_Encode(&writer, &bundle) == 1);
	CHECK(NativeCodecWriter_Size(&writer) == BUNDLE_BYTES);
	return 0;
}

static int OpenPair(uint32_t delayA, uint32_t delayB)
{
	struct NativeMatchConfigV1 config;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_a);
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_a, &config, delayA, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepSession_Open(&g_b, &config, delayB, (uint8_t)SLOT_B) == 1);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	return 0;
}

/* One driven lockstep frame's outcome, mirroring struct FrameTrace in
 * tests/native_lockstep_session_test.c. */
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
 * One clean lockstep frame driven entirely over the virtual datagram pair:
 * sample and buffer local input, compose both bundles, send both with an
 * immediate DELIVER route at the given virtual step, advance the pair to that
 * step, drain and accept both directions, consume the committed input set,
 * and record the simulated frame's canonical digests on success.  Mirrors
 * RunFrame in tests/native_lockstep_session_test.c but with the datagram pair
 * standing in for the direct peer-to-peer hand-off.
 */
static int RunCleanFrame(struct NativeVirtualDatagramPair *pair, uint32_t frame, uint64_t step, uint32_t worldA, uint32_t worldB,
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
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, frame, &pad) == 1);
	MakePad(&pad, SLOT_B, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, frame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, frame, bytesA, sizeof(bytesA), &sizeA) == 1);
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, frame, bytesB, sizeof(bytesB), &sizeB) == 1);
	CHECK(sizeA == BUNDLE_BYTES && sizeB == BUNDLE_BYTES);

	CHECK(NativeVirtualDatagramPair_Send(pair, SLOT_A, bytesA, sizeA, &immediate));
	CHECK(NativeVirtualDatagramPair_Send(pair, SLOT_B, bytesB, sizeB, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(pair, step));

	CHECK(DrainAndAccept(pair, SLOT_B, &g_b, &trace->fromA, &trace->acceptedByB, &countB) == 0);
	CHECK(DrainAndAccept(pair, SLOT_A, &g_a, &trace->fromB, &trace->acceptedByA, &countA) == 0);
	CHECK(countA == 1 && countB == 1);

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

/*
 * Fault 1 (loss + retransmit): A's bundle for frame 0 is sent once with a DROP
 * route, so it never arrives at all, then composed again -- byte-identical,
 * because ComposeBundle is a pure function of the session state -- and sent
 * again with a DELIVER route so it actually arrives.  B's side of the same
 * frame, and every later frame, is exchanged normally.  Both peers must still
 * reach identical canonical digests for frame 0, read out of the
 * verifiedCombinedDigest / verifiedDomainDigests each side reports for it once
 * the D + 1 verification lag catches up, the same comparison
 * tests/native_lockstep_session_test.c's TestCleanRun makes.
 */
static int TestLossAndRetransmit(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct FrameTrace trace;
	struct NativeCanonicalInputPadV1 pad;
	uint8_t bytesA1[BUNDLE_BYTES];
	uint8_t bytesA2[BUNDLE_BYTES];
	uint8_t bytesB[BUNDLE_BYTES];
	size_t sizeA1 = 0;
	size_t sizeA2 = 0;
	size_t sizeB = 0;
	struct NativeVirtualDatagramRoute drop = { NATIVE_VIRTUAL_DATAGRAM_DROP, 0u, 0u };
	struct NativeVirtualDatagramRoute deliver0 = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	enum NativeLockstepSessionResult acceptedByB = NATIVE_LOCKSTEP_SESSION_REJECTED;
	enum NativeLockstepSessionResult acceptedByA = NATIVE_LOCKSTEP_SESSION_REJECTED;
	int countA = 0;
	int countB = 0;
	const uint32_t targetFrame = 0u;
	const uint32_t detectFrame = targetFrame + INPUT_DELAY + 1u;
	uint64_t step;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(INPUT_DELAY, INPUT_DELAY) == 0);

	MakePad(&pad, SLOT_A, targetFrame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, targetFrame, &pad) == 1);
	MakePad(&pad, SLOT_B, targetFrame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, targetFrame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, targetFrame, bytesA1, sizeof(bytesA1), &sizeA1) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA1, sizeA1, &drop));

	/* The retransmit: composed again, deterministically the same bytes. */
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, targetFrame, bytesA2, sizeof(bytesA2), &sizeA2) == 1);
	CHECK(sizeA2 == sizeA1);
	CHECK(memcmp(bytesA1, bytesA2, sizeA1) == 0);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA2, sizeA2, &deliver0));

	CHECK(NativeLockstepSession_ComposeBundle(&g_b, targetFrame, bytesB, sizeof(bytesB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, bytesB, sizeB, &deliver0));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 0u));

	/* Exactly one record reaches B: the dropped copy created no delivery. */
	CHECK(DrainAndAccept(&pair, SLOT_B, &g_b, NULL, &acceptedByB, &countB) == 0);
	CHECK(countB == 1);
	CHECK(acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(DrainAndAccept(&pair, SLOT_A, &g_a, NULL, &acceptedByA, &countA) == 0);
	CHECK(countA == 1 && acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);

	CHECK(NativeLockstepSession_TakeFrameInputs(&g_a, targetFrame, &trace.inputsA) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, targetFrame, &trace.inputsB) == NATIVE_LOCKSTEP_SESSION_OK);
	{
		struct NativeCanonicalStateV4 state;
		CHECK(MakeState(&state, targetFrame, 0u) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(&g_a, &state) == 1);
		CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	}

	/* Advance cleanly through frames 1..detectFrame so the D + 1 verification
	 * lag reports frame 0's digest from both sides. */
	for (uint32_t frame = 1u; frame <= detectFrame; frame++)
	{
		step = frame;
		CHECK(RunCleanFrame(&pair, frame, step, 0u, 0u, &trace) == 0);
		CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
		if (frame == detectFrame)
		{
			CHECK(trace.fromA.verifiedPresent == 1u);
			CHECK(trace.fromB.verifiedPresent == 1u);
			CHECK(trace.fromA.verifiedFrameIndex == targetFrame);
			CHECK(trace.fromB.verifiedFrameIndex == targetFrame);
			/* Both peers independently computed the same digest for the frame
			 * whose bundle was lost and retransmitted. */
			CHECK(trace.fromA.verifiedCombinedDigest == trace.fromB.verifiedCombinedDigest);
			CHECK(memcmp(trace.fromA.verifiedDomainDigests, trace.fromB.verifiedDomainDigests,
			             sizeof(trace.fromA.verifiedDomainDigests)) == 0);
		}
	}
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	return 0;
}

/*
 * Fault 2 (delay): A's bundle for frame 1 is routed to arrive several virtual
 * steps late, but still inside B's acceptance window
 * [consumedFrame, consumedFrame + NATIVE_LOCKSTEP_RING_CAPACITY - 1].  Before
 * it arrives, TakeFrameInputs for that frame must STALL: not an error, nothing
 * latched, consumedFrame unmoved.  Once the pair reaches the delivery step and
 * the record is received and accepted, TakeFrameInputs must then succeed.
 */
static int TestDelayInsideWindow(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeCanonicalInputPadV1 pad;
	struct NativeLockstepSessionFrameInputs inputs;
	uint8_t bytesA[BUNDLE_BYTES];
	uint8_t bytesB[BUNDLE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	const uint32_t frame = 0u; /* consumedFrame starts at 0 on a fresh session. */
	const uint64_t sendStep = 0u;
	const uint64_t lateStep = 5u; /* Still < capacity 8 frames ahead of consumedFrame 0. */
	struct NativeVirtualDatagramRoute delayed = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, lateStep, 0u };
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, sendStep, 0u };
	enum NativeLockstepSessionResult acceptedByB = NATIVE_LOCKSTEP_SESSION_REJECTED;
	int countB = 0;
	struct NativeCanonicalStateV4 state;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(INPUT_DELAY, INPUT_DELAY) == 0);
	CHECK((uint32_t)(lateStep - sendStep) < NATIVE_LOCKSTEP_RING_CAPACITY);

	MakePad(&pad, SLOT_A, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, frame, &pad) == 1);
	MakePad(&pad, SLOT_B, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, frame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, frame, bytesA, sizeof(bytesA), &sizeA) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA, sizeA, &delayed));
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, frame, bytesB, sizeof(bytesB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, bytesB, sizeB, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, sendStep));

	/* B's bundle arrives immediately, but A's own peer bundle has not, so
	 * neither side may consume the frame yet. */
	CHECK(DrainAndAccept(&pair, SLOT_A, &g_a, NULL, NULL, NULL) == 0);
	CHECK(DrainAndAccept(&pair, SLOT_B, &g_b, NULL, &acceptedByB, &countB) == 0);
	CHECK(countB == 0);
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, frame, &inputs) == NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(g_b.consumedFrame == 0u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	/* Stalling once more before the delayed record arrives is still a clean,
	 * non-latching stall. */
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, frame, &inputs) == NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(g_b.consumedFrame == 0u);

	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, lateStep));
	CHECK(DrainAndAccept(&pair, SLOT_B, &g_b, NULL, &acceptedByB, &countB) == 0);
	CHECK(countB == 1 && acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, frame, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(g_b.consumedFrame == frame + 1u);
	CHECK(MakeState(&state, frame, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	return 0;
}

/*
 * Fault 3 (reorder): the later frame's bundle is sent with a smaller delivery
 * step than the earlier frame's, so it arrives first, even though the earlier
 * frame was composed and sent first.  Both must still be accepted -- the
 * window is indexed by frame, not by arrival order -- and consumed in strict
 * frame order regardless.
 */
static int TestReorder(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeCanonicalInputPadV1 pad;
	struct NativeLockstepSessionFrameInputs inputs;
	uint8_t bytesA2[BUNDLE_BYTES];
	uint8_t bytesA3[BUNDLE_BYTES];
	uint8_t bytesB2[BUNDLE_BYTES];
	uint8_t bytesB3[BUNDLE_BYTES];
	size_t size = 0;
	const uint32_t earlierFrame = 0u; /* consumedFrame starts at 0 on a fresh session. */
	const uint32_t laterFrame = 1u;
	/* laterFrame's bundle is routed to deliver before earlierFrame's. */
	struct NativeVirtualDatagramRoute earlierFrameRoute = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 5u, 0u };
	struct NativeVirtualDatagramRoute laterFrameRoute = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 1u, 0u };
	struct NativeCanonicalStateV4 state;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(INPUT_DELAY, INPUT_DELAY) == 0);

	MakePad(&pad, SLOT_A, earlierFrame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, earlierFrame, &pad) == 1);
	MakePad(&pad, SLOT_B, earlierFrame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, earlierFrame, &pad) == 1);
	MakePad(&pad, SLOT_A, laterFrame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, laterFrame, &pad) == 1);
	MakePad(&pad, SLOT_B, laterFrame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, laterFrame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, earlierFrame, bytesA2, sizeof(bytesA2), &size) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA2, size, &earlierFrameRoute));
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, earlierFrame, bytesB2, sizeof(bytesB2), &size) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, bytesB2, size, &earlierFrameRoute));

	/* Sent after the earlier frame's bundle, but routed to arrive first. */
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, laterFrame, bytesA3, sizeof(bytesA3), &size) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA3, size, &laterFrameRoute));
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, laterFrame, bytesB3, sizeof(bytesB3), &size) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, bytesB3, size, &laterFrameRoute));

	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 1u));
	{
		enum NativeLockstepSessionResult acceptedByA = NATIVE_LOCKSTEP_SESSION_REJECTED;
		enum NativeLockstepSessionResult acceptedByB = NATIVE_LOCKSTEP_SESSION_REJECTED;
		int countA = 0;
		int countB = 0;

		/* Only the later frame's record has arrived so far, and it is
		 * accepted even though it is not the next frame to consume. */
		CHECK(DrainAndAccept(&pair, SLOT_B, &g_b, NULL, &acceptedByB, &countB) == 0);
		CHECK(countB == 1 && acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(DrainAndAccept(&pair, SLOT_A, &g_a, NULL, &acceptedByA, &countA) == 0);
		CHECK(countA == 1 && acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
	}
	/* Consumption is still strictly in frame order: the earlier frame has not
	 * arrived yet, so it stalls even though the later one is already stored. */
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, earlierFrame, &inputs) == NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, laterFrame, &inputs) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(g_b.consumedFrame == earlierFrame);

	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 5u));
	{
		enum NativeLockstepSessionResult acceptedByA = NATIVE_LOCKSTEP_SESSION_REJECTED;
		enum NativeLockstepSessionResult acceptedByB = NATIVE_LOCKSTEP_SESSION_REJECTED;
		int countA = 0;
		int countB = 0;

		CHECK(DrainAndAccept(&pair, SLOT_B, &g_b, NULL, &acceptedByB, &countB) == 0);
		CHECK(countB == 1 && acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(DrainAndAccept(&pair, SLOT_A, &g_a, NULL, &acceptedByA, &countA) == 0);
		CHECK(countA == 1 && acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
	}

	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, earlierFrame, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(MakeState(&state, earlierFrame, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, laterFrame, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(MakeState(&state, laterFrame, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	CHECK(g_b.consumedFrame == laterFrame + 1u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	return 0;
}

/*
 * Fault 4 (duplication): A's bundle for one frame is sent with a DUPLICATE
 * route naming two independently chosen delivery steps, both >= currentStep.
 * Both copies must be received and accepted: the first OK, the second a
 * byte-identical no-op DUPLICATE, and the receiver's window
 * duplicateAcceptCount for that sender increments by exactly one.
 */
static int TestDuplication(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct NativeCanonicalInputPadV1 pad;
	struct NativeLockstepSessionFrameInputs inputs;
	uint8_t bytesA[BUNDLE_BYTES];
	uint8_t bytesB[BUNDLE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	const uint32_t frame = 0u; /* consumedFrame starts at 0 on a fresh session. */
	struct NativeVirtualDatagramRoute duplicate = { NATIVE_VIRTUAL_DATAGRAM_DUPLICATE, 2u, 4u };
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	uint32_t duplicateAcceptBefore;
	struct NativeCanonicalStateV4 state;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(INPUT_DELAY, INPUT_DELAY) == 0);
	duplicateAcceptBefore = g_b.peers[SLOT_A].duplicateAcceptCount;

	MakePad(&pad, SLOT_A, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, frame, &pad) == 1);
	MakePad(&pad, SLOT_B, frame);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_b, frame, &pad) == 1);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, frame, bytesA, sizeof(bytesA), &sizeA) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, bytesA, sizeA, &duplicate));
	CHECK(NativeLockstepSession_ComposeBundle(&g_b, frame, bytesB, sizeof(bytesB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, bytesB, sizeB, &immediate));

	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 4u));
	{
		uint8_t recvBuf1[BUNDLE_BYTES];
		uint8_t recvBuf2[BUNDLE_BYTES];
		size_t recvSize1 = sizeof(recvBuf1);
		size_t recvSize2 = sizeof(recvBuf2);
		struct NativeVirtualDatagramMetadata metadata1;
		struct NativeVirtualDatagramMetadata metadata2;

		/* Both duplicates arrive, in delivery-step order. */
		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, recvBuf1, &recvSize1, &metadata1) ==
			NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(recvSize1 == BUNDLE_BYTES);
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, recvBuf1, recvSize1) == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(g_b.peers[SLOT_A].duplicateAcceptCount == duplicateAcceptBefore);

		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, recvBuf2, &recvSize2, &metadata2) ==
			NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(recvSize2 == BUNDLE_BYTES);
		/* The second delivery is the identical bundle bytes, so it is a
		 * byte-identical no-op DUPLICATE, not a conflicting-input fault. */
		CHECK(memcmp(recvBuf1, recvBuf2, BUNDLE_BYTES) == 0);
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, recvBuf2, recvSize2) == NATIVE_LOCKSTEP_SESSION_DUPLICATE);
		CHECK(g_b.peers[SLOT_A].duplicateAcceptCount == duplicateAcceptBefore + 1u);

		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, recvBuf1, &recvSize1, &metadata1) ==
			NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY);
	}
	CHECK(DrainAndAccept(&pair, SLOT_A, &g_a, NULL, NULL, NULL) == 0);

	CHECK(NativeLockstepSession_TakeFrameInputs(&g_b, frame, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(MakeState(&state, frame, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	return 0;
}

static int StateSame(const void *a, const void *b, size_t bytes)
{
	return memcmp(a, b, bytes) == 0;
}

/*
 * Negative case (window overrun): B is driven ahead on its own by directly
 * crafting an accepted record so far in the future -- as if the sender's
 * simulation had run past the peer window's capacity while its bundles for
 * every intervening frame were withheld from B -- that the frame is
 * >= consumedFrame + NATIVE_LOCKSTEP_RING_CAPACITY.  This models the session
 * and window directly, as TestWindowOverrunLatchesNoDivergence in
 * tests/native_lockstep_session_test.c does, and additionally routes one
 * withheld bundle through the virtual datagram pair (received but never
 * delivered while B's window is still open) to prove the fault is reachable
 * with a real transport in the loop and that nothing about the pair or the
 * window is corrupted by the fault.  Asserts AcceptBundle on the far-future
 * bundle is FAULT, FirstFault reports WINDOW_OVERRUN, FirstDivergence stays
 * NULL, and the window and pair state are otherwise provably unchanged.
 */
static int TestWindowOverrun(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][BUNDLE_BYTES];
	struct FrameTrace trace;
	const struct NativeLockstepFaultReport *fault;
	uint8_t withheld[BUNDLE_BYTES];
	uint8_t overrun[BUNDLE_BYTES];
	size_t withheldSize = 0;
	size_t overrunSize = 0;
	const uint32_t frames = 3u; /* A few clean, fully consumed warm-up frames. */
	/* The withheld bundle: composed and sent, but never delivered to B while
	 * its window is still open -- the sender's next frame after warm-up. */
	const uint32_t withheldFrame = frames;
	/* The window accepts [consumedFrame, consumedFrame + capacity - 1]. */
	const uint32_t overrunFrame = frames + (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY;
	struct NativeLockstepInputWindow windowBefore;
	struct NativeVirtualDatagramPair pairBefore;
	struct NativeVirtualDatagramSlot slotsBefore[QUEUE_CAPACITY];

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	CHECK(OpenPair(INPUT_DELAY, INPUT_DELAY) == 0);

	for (uint32_t frame = 0; frame < frames; frame++)
	{
		CHECK(RunCleanFrame(&pair, frame, frame, 0u, 0u, &trace) == 0);
		CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
	}
	CHECK(g_b.peers[SLOT_A].consumedFrame == frames);
	CHECK(g_b.recordedFrame == frames - 1u);

	/* A's bundle for the very next frame is sent through the real transport,
	 * received at B, but deliberately never AcceptBundle'd: it is the one
	 * bundle a real withholding transport would still be holding back when the
	 * far-future bundle below is finally accepted. */
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, withheldFrame, withheld, sizeof(withheld), &withheldSize) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, withheld, withheldSize,
	                                     &(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, frames, 0u }));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, frames));
	{
		uint8_t receivedWithheld[BUNDLE_BYTES];
		size_t receivedSize = sizeof(receivedWithheld);
		struct NativeVirtualDatagramMetadata metadata;
		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, receivedWithheld, &receivedSize, &metadata) ==
			NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(receivedSize == withheldSize);
		CHECK(memcmp(receivedWithheld, withheld, withheldSize) == 0);
		/* Deliberately withheld from AcceptBundle: B's window for A stays at
		 * consumedFrame == frames, exactly as if this record had never been
		 * delivered by a real, lossier transport. */
	}
	CHECK(g_b.peers[SLOT_A].consumedFrame == frames);
	CHECK(g_b.peers[SLOT_A].occupancyMask == 0u);

	windowBefore = g_b.peers[SLOT_A];
	pairBefore = pair;
	memcpy(slotsBefore, slots, sizeof(slots));

	/*
	 * The sender's simulation is modelled as having run all the way to
	 * overrunFrame without any of its intervening bundles reaching B (every
	 * one of them withheld the same way), so this record is >= consumedFrame +
	 * capacity on B's window for A: a clean WINDOW_OVERRUN fault.  A's own
	 * session has only recorded digests through the warm-up frames, so a real
	 * ComposeBundle this far ahead would itself be refused (the lagging
	 * frame's digest would not exist yet); the far-future record is therefore
	 * hand-built with CraftBundle, exactly as
	 * TestWindowOverrunLatchesNoDivergence does.
	 */
	{
		uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];
		for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
			digests[i] = UINT64_C(0x2000000000000000) + i;
		CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, overrunFrame, overrunFrame - INPUT_DELAY - 1u, 1, digests,
		                  UINT64_C(0x0badc0de0badc0de), overrun) == 0);
		overrunSize = BUNDLE_BYTES;
	}
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, overrun, overrunSize) == NATIVE_LOCKSTEP_SESSION_FAULT);

	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
	CHECK(fault->frameIndex == overrunFrame);
	CHECK(fault->senderSlot == SLOT_A);
	CHECK(fault->detail == g_b.peers[SLOT_A].consumedFrame);
	CHECK(fault->detail == frames);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(g_b.divergence.mask == 0u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);

	/* The ring never grows and never evicts, so the window is provably
	 * unchanged by the rejected record, and the transport it arrived over is
	 * likewise untouched: no memory unsafety, no partial mutation. */
	CHECK(StateSame(&g_b.peers[SLOT_A], &windowBefore, sizeof(windowBefore)));
	CHECK(StateSame(&pair, &pairBefore, sizeof(pair)));
	CHECK(StateSame(slots, slotsBefore, sizeof(slots)));
	CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], overrunFrame) == NULL);
	return 0;
}

int main(void)
{
	CHECK(BUNDLE_BYTES == 128u);
	CHECK(TestLossAndRetransmit() == 0);
	CHECK(TestDelayInsideWindow() == 0);
	CHECK(TestReorder() == 0);
	CHECK(TestDuplication() == 0);
	CHECK(TestWindowOverrun() == 0);
	puts("native_lockstep_transport_fault_test: passed");
	return 0;
}
