#include "platform/native_lockstep_session.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define BUNDLE_BYTES NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES
#define INPUT_DELAY 2u
#define SLOT_A 0u /* CAB1_HUMAN in the two-cabinet profile. */
#define SLOT_B 1u /* CAB2_HUMAN. */
/* NativeCanonicalDomainOrder is CONTROL, RNG, INPUT, DRIVERS, WORLD, TOPOLOGY. */
#define DOMAIN_CONTROL 0u
#define DOMAIN_WORLD 4u
/* Any cause value the module never writes, so "untouched" is observable. */
#define CAUSE_SENTINEL UINT32_C(0xA5A5A5A5)

/*
 * The sessions are file scope because each holds a ring per config slot; two of
 * them on the stack would be a needless several tens of kilobytes.
 */
static struct NativeLockstepSession g_a;
static struct NativeLockstepSession g_b;
static uint8_t g_pattern[sizeof(struct NativeLockstepSession)];
/*
 * The exact encoded bytes of the bundle A sent on the most recent driven frame.
 * A delaying or duplicating transport is modelled by re-delivering that record
 * verbatim some frames later, which is the only way to obtain a record whose
 * verified frame the receiver's history has already retired.
 */
static uint8_t g_lastFromA[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];

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
 * A valid canonical state whose digests are a pure function of the frame and of
 * one WORLD counter, so perturbing that counter moves exactly the WORLD domain
 * digest and the combined digest and nothing else.  That is what a real
 * simulation divergence looks like at this seam.
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

/*
 * Frame N consumes the pad its owner sampled at N - D.  The first D frames
 * consume the neutral zero pad nobody ever submitted.  The pad is nine uint8_t
 * fields, so it has no interior padding and memcmp is exact.
 */
static int CheckDelayedPad(const struct NativeCanonicalInputPadV1 *pad, uint32_t slot, uint32_t frame)
{
	struct NativeCanonicalInputPadV1 expected;

	memset(&expected, 0, sizeof(expected));
	if (frame >= INPUT_DELAY)
	{
		MakePad(&expected, slot, frame - INPUT_DELAY);
	}
	CHECK(memcmp(pad, &expected, sizeof(expected)) == 0);
	return 0;
}

static int DecodeBundle(const struct NativeLockstepSession *receiver, const uint8_t *bytes, struct NativeLockstepBundleV1 *bundle)
{
	struct NativeCodecReader reader;
	uint32_t cause = CAUSE_SENTINEL;

	NativeCodecReader_Init(&reader, bytes, BUNDLE_BYTES);
	CHECK(NativeLockstepBundleV1_Decode(&reader, receiver->matchIdentity, receiver->protocolVersion, receiver->inputDelay, bundle,
	                                    &cause) == 1);
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_NONE);
	return 0;
}

/*
 * A hand-built peer record addressed to one receiver.  On the wire the verified
 * digests are just u64s, so this is how a second, different divergence or an
 * out-of-depth verified frame is delivered to a session without re-running a
 * whole second peer.  The codec still enforces the lag invariant, so
 * verifiedFrameIndex + D + 1 must equal frameIndex.
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

static int OpenPair(const struct NativeMatchConfigV1 *configA, const struct NativeMatchConfigV1 *configB, uint32_t delayA,
                    uint32_t delayB)
{
	NativeLockstepSession_Init(&g_a);
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_IDLE);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_IDLE);
	CHECK(NativeLockstepSession_Open(&g_a, configA, delayA, (uint8_t)SLOT_A) == 1);
	CHECK(NativeLockstepSession_Open(&g_b, configB, delayB, (uint8_t)SLOT_B) == 1);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	return 0;
}

/* One driven lockstep frame: what each peer sent and how the other reacted. */
struct FrameTrace
{
	struct NativeLockstepBundleV1 fromA;
	struct NativeLockstepBundleV1 fromB;
	enum NativeLockstepSessionResult acceptedByA; /* A's verdict on B's record. */
	enum NativeLockstepSessionResult acceptedByB;
	enum NativeLockstepSessionResult takenByA;
	enum NativeLockstepSessionResult takenByB;
	struct NativeLockstepSessionFrameInputs inputsA;
	struct NativeLockstepSessionFrameInputs inputsB;
};

/*
 * One lockstep frame for both peers, in the order a cabinet runs it: sample and
 * buffer the local input, compose the bundle and hand it straight to the other
 * session, consume the committed input set, then record the simulated frame's
 * canonical digests.  Composition is driven D frames of wall time later than a
 * latency-optimal cabinet would schedule it, which changes nothing observable:
 * the wire content, the delay line, and the D + 1 verification lag are
 * identical, and every frame index here is the frame its bundle is for.
 *
 * Both peers must be RUNNING on entry.  A frame that latches a terminal state
 * leaves Take rejected and records no digests, so the caller stops driving.
 */
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
	CHECK(sizeA == BUNDLE_BYTES);
	CHECK(sizeB == BUNDLE_BYTES);
	memcpy(g_lastFromA, bytesA, sizeof(g_lastFromA));
	CHECK(DecodeBundle(&g_b, bytesA, &trace->fromA) == 0);
	CHECK(DecodeBundle(&g_a, bytesB, &trace->fromB) == 0);
	CHECK(trace->fromA.frameIndex == frame);
	CHECK(trace->fromA.senderSlot == (uint8_t)SLOT_A);
	CHECK(trace->fromB.senderSlot == (uint8_t)SLOT_B);

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

/*
 * Acceptance 1: two in-process sessions in the two-cabinet profile run long
 * enough to exercise the D + 1 verification lag and both stay RUNNING with no
 * divergence and no fault.
 * Acceptance 2: verifiedPresent is 0 for exactly the first D + 1 frames and
 * verifiedFrameIndex is frameIndex - D - 1 thereafter.
 */
static int TestCleanRun(void)
{
	struct NativeMatchConfigV1 config;
	struct FrameTrace trace;
	const uint32_t frames = 12u;
	uint32_t unverifiedFrames = 0;

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config, &config, INPUT_DELAY, INPUT_DELAY) == 0);
	/* Each cabinet owns one human slot and has exactly one lockstep peer: the
	 * other cabinet's human slot.  Bot slots are not peers. */
	CHECK(g_a.peerCount == 1u);
	CHECK(g_b.peerCount == 1u);
	CHECK(g_a.peerActive[SLOT_B] == 1u);
	CHECK(g_a.peerActive[SLOT_A] == 0u);
	CHECK(g_a.peerActive[2] == 0u);
	CHECK(g_b.peerActive[SLOT_A] == 1u);
	CHECK(g_b.peerActive[SLOT_B] == 0u);
	/* Both cabinets derive the same identity, and it truncates the same digest. */
	CHECK(memcmp(g_a.matchIdentity, g_b.matchIdentity, sizeof(g_a.matchIdentity)) == 0);
	CHECK(memcmp(g_a.matchIdentity, g_a.configDigest, sizeof(g_a.matchIdentity)) == 0);
	CHECK(g_a.historyCapacity == INPUT_DELAY + 2u);

	for (uint32_t frame = 0; frame < frames; frame++)
	{
		CHECK(RunFrame(frame, 0u, 0u, &trace) == 0);
		CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);

		if (frame <= INPUT_DELAY)
		{
			/* No digest exists yet for the lagging frame. */
			CHECK(trace.fromA.verifiedPresent == 0u);
			CHECK(trace.fromB.verifiedPresent == 0u);
			CHECK(trace.fromA.verifiedFrameIndex == 0u);
			CHECK(trace.fromA.verifiedCombinedDigest == 0u);
			CHECK(trace.fromB.verifiedCombinedDigest == 0u);
			unverifiedFrames++;
		}
		else
		{
			CHECK(trace.fromA.verifiedPresent == 1u);
			CHECK(trace.fromB.verifiedPresent == 1u);
			CHECK(trace.fromA.verifiedFrameIndex == frame - INPUT_DELAY - 1u);
			CHECK(trace.fromB.verifiedFrameIndex == frame - INPUT_DELAY - 1u);
			CHECK(trace.fromA.verifiedCombinedDigest != 0u);
			/* Identical simulations, so the two sides agree byte for byte. */
			CHECK(trace.fromA.verifiedCombinedDigest == trace.fromB.verifiedCombinedDigest);
			CHECK(memcmp(trace.fromA.verifiedDomainDigests, trace.fromB.verifiedDomainDigests,
			             sizeof(trace.fromA.verifiedDomainDigests)) == 0);
		}

		/* The committed input set is the local pad then the peer's, both
		 * delayed by exactly D frames. */
		CHECK(trace.inputsA.frameIndex == frame);
		CHECK(trace.inputsB.frameIndex == frame);
		CHECK(trace.inputsA.padCount == 2u);
		CHECK(trace.inputsB.padCount == 2u);
		CHECK(trace.inputsA.pads[0].slotIndex == (uint8_t)SLOT_A);
		CHECK(trace.inputsA.pads[1].slotIndex == (uint8_t)SLOT_B);
		CHECK(trace.inputsB.pads[0].slotIndex == (uint8_t)SLOT_B);
		CHECK(trace.inputsB.pads[1].slotIndex == (uint8_t)SLOT_A);
		CHECK(trace.inputsA.pads[2].slotIndex == NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT);
		CHECK(CheckDelayedPad(&trace.inputsA.pads[0].pad, SLOT_A, frame) == 0);
		CHECK(CheckDelayedPad(&trace.inputsA.pads[1].pad, SLOT_B, frame) == 0);
		CHECK(CheckDelayedPad(&trace.inputsB.pads[0].pad, SLOT_B, frame) == 0);
		CHECK(CheckDelayedPad(&trace.inputsB.pads[1].pad, SLOT_A, frame) == 0);
		/* Both sides consumed the identical committed set. */
		CHECK(memcmp(&trace.inputsA.pads[0].pad, &trace.inputsB.pads[1].pad, sizeof(trace.inputsA.pads[0].pad)) == 0);
	}

	CHECK(unverifiedFrames == INPUT_DELAY + 1u);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	CHECK(g_a.consumedFrame == frames);
	CHECK(g_b.consumedFrame == frames);
	CHECK(g_a.peers[SLOT_B].occupancyMask == 0u);
	CHECK(g_a.peers[SLOT_B].staleDropCount == 0u);
	CHECK(g_a.peers[SLOT_B].duplicateAcceptCount == 0u);
	return 0;
}

/*
 * Acceptance 3: one domain digest perturbed on one side only produces exactly
 * one report on the other, naming the diverged frame and domain and retaining
 * both sides' digests.
 * Acceptance 4: the latch is once-only.
 * Acceptance 5: a protocol fault arriving afterwards leaves it intact.
 */
static int TestDivergence(void)
{
	struct NativeMatchConfigV1 config;
	struct FrameTrace trace;
	struct NativeCanonicalStateV4 clean;
	struct NativeCanonicalStateV4 perturbed;
	struct NativeLockstepDivergenceReport latched;
	const struct NativeLockstepDivergenceReport *report;
	const struct NativeLockstepFaultReport *fault;
	uint64_t secondDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint8_t crafted[BUNDLE_BYTES];
	const uint32_t divergeFrame = 6u;
	const uint32_t detectFrame = divergeFrame + INPUT_DELAY + 1u;
	const uint32_t perturbedWorld = 3u;

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config, &config, INPUT_DELAY, INPUT_DELAY) == 0);

	/* The perturbation moves exactly the WORLD domain and the combined digest. */
	CHECK(MakeState(&clean, divergeFrame, 0u) == 0);
	CHECK(MakeState(&perturbed, divergeFrame, perturbedWorld) == 0);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		CHECK((clean.domainDigests[i] != perturbed.domainDigests[i]) == (i == DOMAIN_WORLD));
	}
	CHECK(clean.combinedDigest != perturbed.combinedDigest);

	for (uint32_t frame = 0; frame < detectFrame; frame++)
	{
		/* Side A's simulation starts drifting at divergeFrame. */
		CHECK(RunFrame(frame, frame >= divergeFrame ? perturbedWorld : 0u, 0u, &trace) == 0);
		CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
		/* Nothing is detectable before frame F + D + 1. */
		CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	}

	/* Frame F + D + 1 carries F's digest, so both sides see it at once. */
	CHECK(RunFrame(detectFrame, perturbedWorld, 0u, &trace) == 0);
	CHECK(trace.fromA.verifiedFrameIndex == divergeFrame);
	CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	CHECK(trace.acceptedByA == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	/* DIVERGED is terminal for simulation. */
	CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);

	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	/* The frame that diverged, not the frame that noticed it. */
	CHECK(report->frameIndex == divergeFrame);
	CHECK(report->frameIndex != detectFrame);
	CHECK(report->senderSlot == SLOT_A);
	CHECK(report->mask == (NATIVE_LOCKSTEP_DIVERGENCE_COMBINED | NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN));
	CHECK((report->mask & NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE) == 0u);
	CHECK(report->canonicalDomainMask == (UINT32_C(1) << DOMAIN_WORLD));
	/* Both sides' digests are retained, because the peer's state cannot be
	 * re-read afterwards. */
	CHECK(report->localCombinedDigest == clean.combinedDigest);
	CHECK(report->remoteCombinedDigest == perturbed.combinedDigest);
	CHECK(memcmp(report->localDomainDigests, clean.domainDigests, sizeof(report->localDomainDigests)) == 0);
	CHECK(memcmp(report->remoteDomainDigests, perturbed.domainDigests, sizeof(report->remoteDomainDigests)) == 0);
	/* The mirror-image report is latched on the other cabinet. */
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) != NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a)->frameIndex == divergeFrame);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a)->senderSlot == SLOT_B);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a)->localCombinedDigest == perturbed.combinedDigest);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a)->remoteCombinedDigest == clean.combinedDigest);

	/* A second, different divergence on a later frame changes nothing. */
	latched = *report;
	CHECK(MakeState(&clean, divergeFrame + 1u, 0u) == 0);
	memcpy(secondDigests, clean.domainDigests, sizeof(secondDigests));
	secondDigests[DOMAIN_CONTROL] ^= UINT64_MAX;
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, detectFrame + 1u, divergeFrame + 1u, 1, secondDigests, clean.combinedDigest ^ UINT64_MAX,
	                  crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	CHECK(memcmp(report, &latched, sizeof(latched)) == 0);
	CHECK(report->canonicalDomainMask == (UINT32_C(1) << DOMAIN_WORLD));
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);

	/* A protocol fault arriving after the divergence latches separately and
	 * leaves the divergence report byte-identical and the mode DIVERGED. */
	crafted[24] ^= 0x01u; /* A body byte, so the trailing digest goes stale. */
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_BAD_DIGEST);
	/* Nothing read out of an undecodable record is trusted. */
	CHECK(fault->frameIndex == 0u);
	CHECK(fault->senderSlot == 0u);
	CHECK(fault->detail == 0u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	CHECK(memcmp(report, &latched, sizeof(latched)) == 0);
	return 0;
}

/*
 * Acceptance 6: a peer digest for a frame the local side never simulated is a
 * FRAME_UNAVAILABLE divergence and never a protocol fault.
 */
static int TestFrameUnavailableNeverSimulated(void)
{
	struct NativeMatchConfigV1 config;
	const struct NativeLockstepDivergenceReport *report;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint8_t crafted[BUNDLE_BYTES];
	const uint32_t frameIndex = 5u;
	const uint32_t verifiedFrameIndex = frameIndex - INPUT_DELAY - 1u;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	CHECK(g_b.recordedAny == 0u);

	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		digests[i] = UINT64_C(0x1000000000000000) + i;
	}
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, frameIndex, verifiedFrameIndex, 1, digests, UINT64_C(0xfeedfacecafebeef), crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);

	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	/* Exactly FRAME_UNAVAILABLE: there was nothing to compare, so neither the
	 * combined nor the domain bits are set and the local digests stay zero. */
	CHECK(report->mask == NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE);
	CHECK(report->canonicalDomainMask == 0u);
	CHECK(report->frameIndex == verifiedFrameIndex);
	CHECK(report->senderSlot == SLOT_A);
	CHECK(report->localCombinedDigest == 0u);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		CHECK(report->localDomainDigests[i] == 0u);
		CHECK(report->remoteDomainDigests[i] == digests[i]);
	}
	CHECK(report->remoteCombinedDigest == UINT64_C(0xfeedfacecafebeef));
	/* It is a divergence, not a fault, so no NATIVE_LOCKSTEP_FAULT_* value. */
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	CHECK(g_b.faulted == 0u);
	CHECK(g_b.fault.cause == NATIVE_LOCKSTEP_FAULT_NONE);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	/* The record itself was well formed, so it still reached the peer window. */
	CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], frameIndex) != NULL);
	return 0;
}

/*
 * The other half of acceptance 6: a frame already retired from the D + 2 deep
 * digest history is equally unavailable, while a frame still inside that depth
 * compares normally.
 */
static int TestFrameUnavailableRetired(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeCanonicalStateV4 state;
	const struct NativeLockstepDivergenceReport *report;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint8_t crafted[BUNDLE_BYTES];
	const uint32_t recordedFrames = 6u; /* Frames 0..5. */

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	for (uint32_t frame = 0; frame < recordedFrames; frame++)
	{
		CHECK(MakeState(&state, frame, 0u) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	}
	CHECK(g_b.recordedFrame == recordedFrames - 1u);
	/* Recording backwards would retire a frame the lag still needs. */
	CHECK(MakeState(&state, recordedFrames - 1u, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 0);

	/* Frame 4 is still inside the D + 2 = 4 deep history, and the digests
	 * agree, so this record is not a divergence at all. */
	CHECK(MakeState(&state, 4u, 0u) == 0);
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, 4u + INPUT_DELAY + 1u, 4u, 1, state.domainDigests, state.combinedDigest, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);

	/* Frame 1 has been retired: recordedFrame 5 minus depth 4 leaves [2, 5]. */
	CHECK(MakeState(&state, 1u, 0u) == 0);
	memcpy(digests, state.domainDigests, sizeof(digests));
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, 1u + INPUT_DELAY + 1u, 1u, 1, digests, state.combinedDigest, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	CHECK(report->mask == NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE);
	CHECK(report->canonicalDomainMask == 0u);
	CHECK(report->frameIndex == 1u);
	CHECK(report->localCombinedDigest == 0u);
	/* The digests matched; only the depth made the frame incomparable. */
	CHECK(report->remoteCombinedDigest == state.combinedDigest);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	return 0;
}

/*
 * Regression for the deleted per-peer high-water mark: an ACCEPTED record
 * carrying a lower verifiedFrameIndex than one already compared must still be
 * digest-compared, and a divergence on it must latch at that earlier frame,
 * not at the later one processed first.  Two different frameIndex values are
 * both legitimately ACCEPTED by the window out of order here, exactly as a
 * reordering transport could deliver them; only their verifiedFrameIndex
 * ordering is reversed relative to the order they are offered.  A high-water
 * mark keyed on "largest verifiedFrameIndex seen" would have treated the
 * second record's lower verifiedFrameIndex as already covered and silently
 * discarded its only comparison.
 */
static int TestOutOfOrderLowerVerifiedFrameStillCompared(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeCanonicalStateV4 state;
	struct NativeCanonicalStateV4 cleanFrame1;
	struct NativeCanonicalStateV4 cleanFrame4;
	struct NativeCanonicalStateV4 perturbedFrame1;
	const struct NativeLockstepDivergenceReport *report;
	uint8_t higher[BUNDLE_BYTES];
	uint8_t lower[BUNDLE_BYTES];
	const uint32_t recordedFrames = 5u; /* Frames 0..4. */

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	for (uint32_t frame = 0; frame < recordedFrames; frame++)
	{
		CHECK(MakeState(&state, frame, 0u) == 0);
		CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	}
	CHECK(g_b.recordedFrame == recordedFrames - 1u);
	CHECK(g_b.historyCapacity == INPUT_DELAY + 2u);
	/* Frame 1 is still inside the D + 2 = 4 deep history: recordedFrame 4 minus
	 * frame 1 is 3, below historyCapacity, so it has not been retired. */
	CHECK((g_b.recordedFrame - 1u) < g_b.historyCapacity);

	CHECK(MakeState(&cleanFrame4, 4u, 0u) == 0);
	CHECK(MakeState(&cleanFrame1, 1u, 0u) == 0);
	CHECK(MakeState(&perturbedFrame1, 1u, 3u) == 0);
	CHECK(cleanFrame1.combinedDigest != perturbedFrame1.combinedDigest);

	/* Offered first: verifiedFrameIndex 4, agrees with the local history. */
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, 4u + INPUT_DELAY + 1u, 4u, 1, cleanFrame4.domainDigests, cleanFrame4.combinedDigest,
	                  higher) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, higher, sizeof(higher)) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);

	/* Offered second, a different frameIndex so the window's occupancy-slot
	 * dedup does not apply: verifiedFrameIndex 1, below the one just compared,
	 * and its digest disagrees with the local history. */
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, 1u + INPUT_DELAY + 1u, 1u, 1, perturbedFrame1.domainDigests, perturbedFrame1.combinedDigest,
	                  lower) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, lower, sizeof(lower)) == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);

	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	/* The frame that actually diverged is 1, not 4: the earlier frame, not the
	 * one compared first. */
	CHECK(report->frameIndex == 1u);
	CHECK(report->frameIndex != 4u);
	CHECK(report->senderSlot == SLOT_A);
	CHECK(report->mask == (NATIVE_LOCKSTEP_DIVERGENCE_COMBINED | NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN));
	CHECK((report->mask & NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE) == 0u);
	CHECK(report->canonicalDomainMask == (UINT32_C(1) << DOMAIN_WORLD));
	CHECK(report->localCombinedDigest == cleanFrame1.combinedDigest);
	CHECK(report->remoteCombinedDigest == perturbedFrame1.combinedDigest);
	CHECK(memcmp(report->localDomainDigests, cleanFrame1.domainDigests, sizeof(report->localDomainDigests)) == 0);
	CHECK(memcmp(report->remoteDomainDigests, perturbedFrame1.domainDigests, sizeof(report->remoteDomainDigests)) == 0);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	return 0;
}

/*
 * A record the peer window does not accept is never digest-verified, so a
 * delaying or duplicating transport cannot kill the match: the re-delivery of an
 * already consumed frame is a STALE drop and an in-window re-delivery is a
 * DUPLICATE no-op, and neither latches anything even though the receiver's
 * D + 2 deep history has already retired the frame the record verifies.
 */
static int TestLateRedeliveryIsNotADivergence(void)
{
	struct NativeMatchConfigV1 config;
	struct FrameTrace trace;
	struct NativeLockstepBundleV1 bundle;
	uint8_t saved[BUNDLE_BYTES];
	uint8_t bytes[BUNDLE_BYTES];
	size_t size = 0;
	const uint32_t captureFrame = 6u;
	const uint32_t lastFrame = captureFrame + 2u; /* Two frames later, so the copy is stale. */

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config, &config, INPUT_DELAY, INPUT_DELAY) == 0);
	for (uint32_t frame = 0; frame <= lastFrame; frame++)
	{
		CHECK(RunFrame(frame, 0u, 0u, &trace) == 0);
		CHECK(trace.acceptedByB == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
		if (frame == captureFrame)
		{
			memcpy(saved, g_lastFromA, sizeof(saved));
		}
	}
	CHECK(g_b.peers[SLOT_A].consumedFrame == lastFrame + 1u);
	CHECK(g_b.recordedFrame == lastFrame);

	/* The copy verifies a frame the receiver's history has since retired, so
	 * digest-comparing it would report FRAME_UNAVAILABLE. */
	CHECK(DecodeBundle(&g_b, saved, &bundle) == 0);
	CHECK(bundle.frameIndex == captureFrame);
	CHECK(bundle.verifiedPresent == 1u);
	CHECK(bundle.verifiedFrameIndex == captureFrame - INPUT_DELAY - 1u);
	CHECK((g_b.recordedFrame - bundle.verifiedFrameIndex) >= g_b.historyCapacity);

	/* Below the window: a drop, not an error, and not a divergence. */
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, saved, sizeof(saved)) == NATIVE_LOCKSTEP_SESSION_STALE);
	CHECK(g_b.peers[SLOT_A].staleDropCount == 1u);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	CHECK(g_b.faulted == 0u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	/* A third, still later re-delivery of the same record behaves identically. */
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, saved, sizeof(saved)) == NATIVE_LOCKSTEP_SESSION_STALE);
	CHECK(g_b.peers[SLOT_A].staleDropCount == 2u);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);

	/* The in-window half: the same bytes twice for a frame not yet consumed. */
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, lastFrame + 1u, bytes, sizeof(bytes), &size) == 1);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, bytes, size) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, bytes, size) == NATIVE_LOCKSTEP_SESSION_DUPLICATE);
	CHECK(g_b.peers[SLOT_A].duplicateAcceptCount == 1u);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	return 0;
}

/*
 * A record the window faults on is not taken into the match, so it is not
 * digest-verified either: a peer that has run far enough ahead to overrun the
 * ring is a clean WINDOW_OVERRUN fault with no divergence latched, even though
 * the far-future record verifies a frame the local side never simulated.
 */
static int TestWindowOverrunLatchesNoDivergence(void)
{
	struct NativeMatchConfigV1 config;
	struct FrameTrace trace;
	const struct NativeLockstepFaultReport *fault;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint8_t crafted[BUNDLE_BYTES];
	const uint32_t frames = 6u;
	/* The window accepts [consumedFrame, consumedFrame + capacity - 1]. */
	const uint32_t overrunFrame = frames + (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY;

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config, &config, INPUT_DELAY, INPUT_DELAY) == 0);
	for (uint32_t frame = 0; frame < frames; frame++)
	{
		CHECK(RunFrame(frame, 0u, 0u, &trace) == 0);
		CHECK(trace.takenByB == NATIVE_LOCKSTEP_SESSION_OK);
	}
	CHECK(g_b.peers[SLOT_A].consumedFrame == frames);
	CHECK(g_b.recordedFrame == frames - 1u);

	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		digests[i] = UINT64_C(0x2000000000000000) + i;
	}
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, overrunFrame, overrunFrame - INPUT_DELAY - 1u, 1, digests, UINT64_C(0x0badc0de0badc0de),
	                  crafted) == 0);
	/* The verified frame is ahead of everything the local side has simulated. */
	CHECK((overrunFrame - INPUT_DELAY - 1u) > g_b.recordedFrame);

	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
	CHECK(fault->frameIndex == overrunFrame);
	CHECK(fault->senderSlot == SLOT_A);
	/* detail is the peer window's consumedFrame, per the header contract. */
	CHECK(fault->detail == g_b.peers[SLOT_A].consumedFrame);
	CHECK(fault->detail == frames);
	/* A clean fault: nothing about the digests was compared or reported. */
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(g_b.divergence.mask == 0u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	/* The ring never grows and never evicts, so nothing was stored either. */
	CHECK(g_b.peers[SLOT_A].occupancyMask == 0u);
	CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], overrunFrame) == NULL);
	return 0;
}

/*
 * A buffered local pad is the pad already put on the wire, so a submission never
 * replaces one: the re-submission, the modular collision, and the already
 * consumed consumption frame are all refused with the delay line untouched.
 */
static int TestSubmitLocalInputRefusesReplacement(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeCanonicalInputPadV1 first;
	struct NativeCanonicalInputPadV1 second;
	struct NativeCanonicalStateV4 state;
	struct NativeLockstepBundleV1 bundle;
	struct FrameTrace trace;
	uint8_t bytes[BUNDLE_BYTES];
	size_t size = 0;
	const uint32_t sampleFrame = 1u;
	const uint32_t consumeFrame = sampleFrame + INPUT_DELAY;
	const uint32_t slot = consumeFrame % (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_a);
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	MakePad(&first, SLOT_A, sampleFrame);
	MakePad(&second, SLOT_A, sampleFrame + 100u);
	CHECK(memcmp(&first, &second, sizeof(first)) != 0);

	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, sampleFrame, &first) == 1);
	CHECK(g_a.localInputs[slot].frameIndex == consumeFrame);
	/* The same sample frame again, and a different sample frame whose
	 * consumption frame collides modulo the ring, are both refused. */
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, sampleFrame, &second) == 0);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, sampleFrame + (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY, &second) == 0);
	CHECK(g_a.localInputs[slot].present == 1u);
	CHECK(g_a.localInputs[slot].frameIndex == consumeFrame);
	CHECK(memcmp(&g_a.localInputs[slot].pad, &first, sizeof(first)) == 0);
	/* Not a blanket refusal: a free consumption frame still buffers. */
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, sampleFrame + 1u, &second) == 1);

	/* And the pad the peer is sent is the pad that was kept. */
	CHECK(MakeState(&state, 0u, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_a, &state) == 1);
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, consumeFrame, bytes, sizeof(bytes), &size) == 1);
	CHECK(DecodeBundle(&g_a, bytes, &bundle) == 0);
	CHECK(bundle.frameIndex == consumeFrame);
	CHECK(memcmp(&bundle.pads[0].pad, &first, sizeof(first)) == 0);

	/* A consumption frame the simulation has already passed is refused too. */
	CHECK(OpenPair(&config, &config, INPUT_DELAY, INPUT_DELAY) == 0);
	for (uint32_t frame = 0; frame < consumeFrame + 1u; frame++)
	{
		CHECK(RunFrame(frame, 0u, 0u, &trace) == 0);
		CHECK(trace.takenByA == NATIVE_LOCKSTEP_SESSION_OK);
	}
	CHECK(g_a.consumedFrame == consumeFrame + 1u);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, sampleFrame, &second) == 0);
	CHECK(g_a.localInputs[slot].frameIndex == consumeFrame);
	CHECK(CheckDelayedPad(&g_a.localInputs[slot].pad, SLOT_A, consumeFrame) == 0);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_a) == NULL);
	return 0;
}

/*
 * Acceptance 7: a mismatched inputDelay on the very first bundle is a clean
 * protocol fault with no divergence.  D is configured, not negotiated, so a
 * cabinet misconfiguration surfaces here rather than as a renegotiation.
 */
static int TestInputDelayMismatch(void)
{
	struct NativeMatchConfigV1 config;
	const struct NativeLockstepFaultReport *fault;
	uint8_t bytes[BUNDLE_BYTES];
	size_t size = 0;

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config, &config, INPUT_DELAY + 1u, INPUT_DELAY) == 0);
	CHECK(memcmp(g_a.matchIdentity, g_b.matchIdentity, sizeof(g_a.matchIdentity)) == 0);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, 0u, bytes, sizeof(bytes), &size) == 1);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, bytes, size) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_INPUT_DELAY);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	/* The record never reached the peer window. */
	CHECK(g_b.peers[SLOT_A].occupancyMask == 0u);
	CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], 0u) == NULL);
	return 0;
}

/*
 * Acceptance 8: a mismatched match identity on the very first bundle is a clean
 * protocol fault with no divergence.  The fault latch is also once-only, and a
 * later divergence raises the mode without disturbing it.
 */
static int TestMatchIdentityMismatch(void)
{
	struct NativeMatchConfigV1 configA;
	struct NativeMatchConfigV1 configB;
	struct NativeCanonicalStateV4 state;
	struct NativeLockstepFaultReport latched;
	const struct NativeLockstepFaultReport *fault;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint8_t bytes[BUNDLE_BYTES];
	uint8_t crafted[BUNDLE_BYTES];
	size_t size = 0;

	FillConfig(&configA, UINT32_C(0x01020304));
	FillConfig(&configB, UINT32_C(0x0a0b0c0d));
	CHECK(OpenPair(&configA, &configB, INPUT_DELAY, INPUT_DELAY) == 0);
	/* Different configurations really do give different per-frame identities. */
	CHECK(memcmp(g_a.matchIdentity, g_b.matchIdentity, sizeof(g_a.matchIdentity)) != 0);

	CHECK(NativeLockstepSession_ComposeBundle(&g_a, 0u, bytes, sizeof(bytes), &size) == 1);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, bytes, size) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	CHECK(g_b.peers[SLOT_A].occupancyMask == 0u);
	latched = *fault;

	/* A second, different fault does not change the report. */
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, bytes, BUNDLE_BYTES - 1u) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(memcmp(fault, &latched, sizeof(latched)) == 0);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);

	/*
	 * And the mirror of acceptance 5: a divergence arriving after a fault
	 * latches separately, leaves the fault report byte-identical, and raises
	 * the mode, because DIVERGED outranks FAULTED.  RecordLocalDigests is
	 * refused in a terminal mode, so the history is seeded first.
	 */
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &configB, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	CHECK(MakeState(&state, 4u, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 1);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, bytes, size) == NATIVE_LOCKSTEP_SESSION_FAULT);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	latched = *fault;
	CHECK(latched.cause == NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY);

	memcpy(digests, state.domainDigests, sizeof(digests));
	digests[DOMAIN_CONTROL] ^= UINT64_MAX;
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, 4u + INPUT_DELAY + 1u, 4u, 1, digests, state.combinedDigest ^ UINT64_MAX, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_DIVERGENCE);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) != NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b)->frameIndex == 4u);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b)->canonicalDomainMask == (UINT32_C(1) << DOMAIN_CONTROL));
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(memcmp(fault, &latched, sizeof(latched)) == 0);
	return 0;
}

/* An unexpected sender slot is a clean fault that never reaches a window. */
static int TestUnknownSenderSlot(void)
{
	struct NativeMatchConfigV1 config;
	const struct NativeLockstepFaultReport *fault;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT] = {0};
	uint8_t crafted[BUNDLE_BYTES];

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);

	/* Slot 2 is a bot: bot inputs are not on the wire, so it is not a peer. */
	CHECK(CraftBundle(&g_b, 2u, 3u, 0u, 0, digests, 0u, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_BAD_SLOT);
	CHECK(fault->frameIndex == 3u);
	CHECK(fault->senderSlot == 2u);
	CHECK(fault->detail == SLOT_B);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	for (uint32_t slot = 0; slot < NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY; slot++)
	{
		CHECK(g_b.peers[slot].occupancyMask == 0u);
	}
	return 0;
}

/* Open validates D against both fixed rings and refuses a non-human slot. */
static int TestOpenRejects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 invalid;

	FillConfig(&config, UINT32_C(0x01020304));
	memset(&invalid, 0, sizeof(invalid));

	/* A session that is not IDLE is refused before anything is touched. */
	memset(g_pattern, 0x5Au, sizeof(g_pattern));
	memset(&g_a, 0x5Au, sizeof(g_a));
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 0);
	CHECK(memcmp(&g_a, g_pattern, sizeof(g_a)) == 0);

	NativeLockstepSession_Init(&g_a);
	memcpy(g_pattern, &g_a, sizeof(g_pattern));
	CHECK(NativeLockstepSession_Open(NULL, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, NULL, INPUT_DELAY, (uint8_t)SLOT_A) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &invalid, INPUT_DELAY, (uint8_t)SLOT_A) == 0);
	/* D outside [MIN, MAX], including values past the fixed ring capacity. */
	CHECK(NativeLockstepSession_Open(&g_a, &config, 0u, (uint8_t)SLOT_A) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1u, (uint8_t)SLOT_A) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, NATIVE_LOCKSTEP_RING_CAPACITY, (uint8_t)SLOT_A) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY, (uint8_t)SLOT_A) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, UINT32_MAX, (uint8_t)SLOT_A) == 0);
	/* Bot, inactive, and out-of-range slots are not lockstep peers. */
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, 2u) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, 6u) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, 8u) == 0);
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, 255u) == 0);
	CHECK(memcmp(&g_a, g_pattern, sizeof(g_a)) == 0);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_IDLE);

	/* Every in-range D opens, and the history depth follows it exactly. */
	for (uint32_t delay = NATIVE_LOCKSTEP_MIN_INPUT_DELAY; delay <= NATIVE_LOCKSTEP_MAX_INPUT_DELAY; delay++)
	{
		NativeLockstepSession_Init(&g_a);
		CHECK(NativeLockstepSession_Open(&g_a, &config, delay, (uint8_t)SLOT_A) == 1);
		CHECK(g_a.inputDelay == delay);
		CHECK(g_a.historyCapacity == delay + 2u);
		CHECK(g_a.historyCapacity <= NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY);
		CHECK(g_a.peers[SLOT_B].inputDelay == delay);
		/* Reopening an open session is refused. */
		CHECK(NativeLockstepSession_Open(&g_a, &config, delay, (uint8_t)SLOT_A) == 0);
	}
	/*
	 * The ring-capacity and history-depth clauses of Open are unreachable at
	 * runtime while the capacities are 8 and the maximum delay is 6: the range
	 * check rejects every such delay first.  The ring-capacity relationship is
	 * also enforced at compile time by a _Static_assert in both
	 * platform/native_lockstep_session.c and
	 * platform/native_lockstep_input_window.c; the history-depth relationship
	 * has no _Static_assert because NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY
	 * is defined as exactly NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 2, so an
	 * assertion against that expression could not fail.  Both are asserted
	 * here as constant expressions rather than faked at runtime.
	 */
	CHECK(NATIVE_LOCKSTEP_RING_CAPACITY >= NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1);
	CHECK(NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY >= NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 2u);
	CHECK(NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT);

	/* The whole surface is inert on a NULL or still IDLE session. */
	NativeLockstepSession_Init(&g_a);
	NativeLockstepSession_Init(NULL);
	CHECK(NativeLockstepSession_Mode(NULL) == NATIVE_LOCKSTEP_IDLE);
	CHECK(NativeLockstepSession_FirstDivergence(NULL) == NULL);
	CHECK(NativeLockstepSession_FirstFault(NULL) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_a) == NULL);
	CHECK(NativeLockstepSession_AcceptBundle(&g_a, g_pattern, BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(NativeLockstepSession_AcceptBundle(NULL, g_pattern, BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(NativeLockstepSession_FirstFault(&g_a) == NULL);
	CHECK(memcmp(&g_a, g_pattern, sizeof(g_a)) == 0);
	return 0;
}

/* Caller misuse of the running surface, all non-mutating. */
static int TestRunningRejects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeCanonicalInputPadV1 pad;
	struct NativeCanonicalStateV4 state;
	struct NativeLockstepSessionFrameInputs inputs;
	uint8_t bytes[BUNDLE_BYTES];
	/* Any width the module never writes, so "untouched" is observable. */
	size_t size = SIZE_MAX;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_a);
	CHECK(NativeLockstepSession_Open(&g_a, &config, INPUT_DELAY, (uint8_t)SLOT_A) == 1);
	MakePad(&pad, SLOT_A, 0u);
	memset(&state, 0, sizeof(state));

	CHECK(NativeLockstepSession_SubmitLocalInput(NULL, 0u, &pad) == 0);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, 0u, NULL) == 0);
	/* The delay would push the consumption frame past the frame range. */
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, UINT32_MAX, &pad) == 0);
	CHECK(NativeLockstepSession_SubmitLocalInput(&g_a, 0u, &pad) == 1);

	CHECK(NativeLockstepSession_RecordLocalDigests(NULL, &state) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_a, NULL) == 0);
	/* A zeroed state does not validate, so it is not a digest source. */
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_a, &state) == 0);
	CHECK(g_a.recordedAny == 0u);

	CHECK(NativeLockstepSession_ComposeBundle(NULL, 0u, bytes, sizeof(bytes), &size) == 0);
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, 0u, NULL, sizeof(bytes), &size) == 0);
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, 0u, bytes, sizeof(bytes), NULL) == 0);
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, 0u, bytes, BUNDLE_BYTES - 1u, &size) == 0);
	CHECK(size == SIZE_MAX);
	/* Past the first D + 1 frames the lagging digest must already exist. */
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, INPUT_DELAY + 1u, bytes, sizeof(bytes), &size) == 0);
	CHECK(NativeLockstepSession_ComposeBundle(&g_a, INPUT_DELAY, bytes, sizeof(bytes), &size) == 1);
	CHECK(size == BUNDLE_BYTES);

	CHECK(NativeLockstepSession_TakeFrameInputs(NULL, 0u, &inputs) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_a, 0u, NULL) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	/* Only consumedFrame may be taken. */
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_a, 1u, &inputs) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	/* The peer frame has not arrived: a stall, not an error, and nothing moves. */
	CHECK(NativeLockstepSession_TakeFrameInputs(&g_a, 0u, &inputs) == NATIVE_LOCKSTEP_SESSION_STALL);
	CHECK(g_a.consumedFrame == 0u);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_a) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_a) == NULL);
	return 0;
}

int main(void)
{
	CHECK(BUNDLE_BYTES == 128u);
	CHECK(NATIVE_CANONICAL_DOMAIN_COUNT == 6u);
	CHECK(TestCleanRun() == 0);
	CHECK(TestDivergence() == 0);
	CHECK(TestFrameUnavailableNeverSimulated() == 0);
	CHECK(TestFrameUnavailableRetired() == 0);
	CHECK(TestOutOfOrderLowerVerifiedFrameStillCompared() == 0);
	CHECK(TestLateRedeliveryIsNotADivergence() == 0);
	CHECK(TestWindowOverrunLatchesNoDivergence() == 0);
	CHECK(TestSubmitLocalInputRefusesReplacement() == 0);
	CHECK(TestInputDelayMismatch() == 0);
	CHECK(TestMatchIdentityMismatch() == 0);
	CHECK(TestUnknownSenderSlot() == 0);
	CHECK(TestOpenRejects() == 0);
	CHECK(TestRunningRejects() == 0);
	return 0;
}
