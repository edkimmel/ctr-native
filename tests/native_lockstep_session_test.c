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
 * The LR-11 park (docs/LOCKSTEP_RACE_MILESTONE.md LR-11, LR-S5).  The fixtures
 * below drive two sessions in the drive order of LR-2: on race tick k a
 * cabinet records frame k, submits the pad it sampled on tick k and composes
 * and sends the bundle for frame k + D, then takes frame k.  Race tick 0 also
 * composes frames 0 to D - 1, which carry the zero pad and no digest.  B is the
 * receiver, whose last recorded frame is r; A is the peer that leads it.
 */
#define LEAD_FRAMES 64u
#define FLIP_WORLD 3u

static struct NativeLockstepSession g_oracle;
static struct NativeLockstepSession g_scratch;
static uint8_t g_sentA[LEAD_FRAMES][BUNDLE_BYTES];
static uint8_t g_sentB[LEAD_FRAMES][BUNDLE_BYTES];

struct LeadRun
{
	uint32_t delay;
	uint32_t r;       /* B's last recorded frame. */
	uint32_t takenR;  /* 1: B has taken r, so its consumed frame is r + 1; 0: it is r. */
	uint32_t flipFirst; /* A's simulation differs on frames flipFirst..flipLast. */
	uint32_t flipLast;
};

static void InitLeadRun(struct LeadRun *run, uint32_t delay, uint32_t takenR)
{
	run->delay = delay;
	/* Deep enough that every lockstep bundle carries a digest before r. */
	run->r = delay + 3u;
	run->takenR = takenR;
	run->flipFirst = UINT32_MAX;
	run->flipLast = 0u;
}

static uint32_t LeaderWorld(const struct LeadRun *run, uint32_t frame)
{
	return ((frame >= run->flipFirst) && (frame <= run->flipLast)) ? FLIP_WORLD : 0u;
}

/*
 * The longest lead the receiver's window admits.  A at lead L sends frame
 * r + L + D, and B's window is [c, c + 8) with c = r + takenR, so
 * L <= 7 + takenR - D; the drive's own bound is D + 1 (A stalls on its take of
 * r + D + 1).  For D up to 3 the window never binds.
 */
static uint32_t MaxLead(const struct LeadRun *run)
{
	const uint32_t windowLead = (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY - 1u + run->takenR - run->delay;

	return (windowLead < run->delay + 1u) ? windowLead : run->delay + 1u;
}

static int RecordFrame(struct NativeLockstepSession *session, uint32_t frame, uint32_t world)
{
	struct NativeCanonicalStateV4 state;

	CHECK(MakeState(&state, frame, world) == 0);
	/* 1 even when the record latches a parked divergence: that is a latch, not
	 * a failed record. */
	CHECK(NativeLockstepSession_RecordLocalDigests(session, &state) == 1);
	CHECK(session->recordedFrame == frame);
	return 0;
}

/* Submits the pad sampled D frames earlier, then composes frame into sent[frame]. */
static int SendFrame(struct NativeLockstepSession *session, uint32_t slot, uint32_t frame, uint8_t sent[LEAD_FRAMES][BUNDLE_BYTES])
{
	struct NativeCanonicalInputPadV1 pad;
	size_t size = 0;

	CHECK(frame < LEAD_FRAMES);
	if (frame >= session->inputDelay)
	{
		MakePad(&pad, slot, frame - session->inputDelay);
		CHECK(NativeLockstepSession_SubmitLocalInput(session, frame - session->inputDelay, &pad) == 1);
	}
	CHECK(NativeLockstepSession_ComposeBundle(session, frame, sent[frame], BUNDLE_BYTES, &size) == 1);
	CHECK(size == BUNDLE_BYTES);
	return 0;
}

static enum NativeLockstepSessionResult TakeFrame(struct NativeLockstepSession *session, uint32_t frame)
{
	struct NativeLockstepSessionFrameInputs inputs;

	return NativeLockstepSession_TakeFrameInputs(session, frame, &inputs);
}

static uint32_t ParkedCount(const struct NativeLockstepSession *session, uint32_t slot)
{
	uint32_t count = 0;

	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY; i++)
	{
		count += (session->parked[slot][i].present != 0) ? 1u : 0u;
	}
	return count;
}

/*
 * Both cabinets in lockstep through tick r - 1, every bundle delivered at once,
 * then B's tick r, taking r only when run->takenR is set.  Afterwards B has
 * recorded r and consumes r + takenR, A has recorded r - 1 and consumes r, and
 * every digest so far compared clean on arrival.
 */
static int DriveToReceiverRecord(const struct LeadRun *run)
{
	struct NativeMatchConfigV1 config;
	const uint32_t d = run->delay;

	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(OpenPair(&config, &config, d, d) == 0);
	for (uint32_t frame = 0; frame < d; frame++)
	{
		CHECK(SendFrame(&g_a, SLOT_A, frame, g_sentA) == 0);
		CHECK(SendFrame(&g_b, SLOT_B, frame, g_sentB) == 0);
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, g_sentA[frame], BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(NativeLockstepSession_AcceptBundle(&g_a, g_sentB[frame], BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
	}
	for (uint32_t k = 0; k < run->r; k++)
	{
		CHECK(RecordFrame(&g_a, k, LeaderWorld(run, k)) == 0);
		CHECK(SendFrame(&g_a, SLOT_A, k + d, g_sentA) == 0);
		CHECK(RecordFrame(&g_b, k, 0u) == 0);
		CHECK(SendFrame(&g_b, SLOT_B, k + d, g_sentB) == 0);
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, g_sentA[k + d], BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(NativeLockstepSession_AcceptBundle(&g_a, g_sentB[k + d], BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(TakeFrame(&g_a, k) == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(TakeFrame(&g_b, k) == NATIVE_LOCKSTEP_SESSION_OK);
	}
	CHECK(RecordFrame(&g_b, run->r, 0u) == 0);
	CHECK(SendFrame(&g_b, SLOT_B, run->r + d, g_sentB) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_a, g_sentB[run->r + d], BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
	if (run->takenR != 0)
	{
		CHECK(TakeFrame(&g_b, run->r) == NATIVE_LOCKSTEP_SESSION_OK);
	}
	CHECK(g_b.recordedFrame == run->r);
	CHECK(g_b.peers[SLOT_A].consumedFrame == run->r + run->takenR);
	CHECK(ParkedCount(&g_b, SLOT_A) == 0u);
	CHECK(NativeLockstepSession_Mode(&g_a) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	return 0;
}

/*
 * A's ticks first..last.  B has sent frames only up to r + D, so A's take of a
 * later frame stalls; only the last tick may stall.
 */
static int LeaderTicks(const struct LeadRun *run, uint32_t first, uint32_t last)
{
	for (uint32_t k = first; k <= last; k++)
	{
		const enum NativeLockstepSessionResult expected =
		    (k <= run->r + run->delay) ? NATIVE_LOCKSTEP_SESSION_OK : NATIVE_LOCKSTEP_SESSION_STALL;

		CHECK(RecordFrame(&g_a, k, LeaderWorld(run, k)) == 0);
		CHECK(SendFrame(&g_a, SLOT_A, k + run->delay, g_sentA) == 0);
		CHECK((k == last) || (expected == NATIVE_LOCKSTEP_SESSION_OK));
		CHECK(TakeFrame(&g_a, k) == expected);
	}
	return 0;
}

/*
 * A leading B by lead ticks: A's ticks r to r + lead.  Its newest bundle is for
 * frame r + lead + D and carries frame r + lead - 1, so its digests of frames
 * r + 1 to r + lead - 1 reach B before B records them.  Every bundle A sent
 * since the lockstep part is delivered to B and must be accepted as OK: the
 * digests up to r compare clean on arrival and the rest are parked.
 */
static int LeadAndDeliver(const struct LeadRun *run, uint32_t lead)
{
	const uint32_t r = run->r;

	CHECK(LeaderTicks(run, r, r + lead) == 0);
	for (uint32_t frame = r + run->delay; frame <= r + lead + run->delay; frame++)
	{
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, g_sentA[frame], BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
	}
	CHECK(ParkedCount(&g_b, SLOT_A) == lead - 1u);
	for (uint32_t j = 1; j < lead; j++)
	{
		const struct NativeLockstepSessionParkedDigest *entry = &g_b.parked[SLOT_A][(r + j) % NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY];

		CHECK(entry->present == 1u);
		CHECK(entry->frameIndex == r + j);
		CHECK(entry->senderSlot == SLOT_A);
	}
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
	return 0;
}

/*
 * For every D from 1 to 3 a peer leading by 1 to D + 1 ticks parks 0 to D
 * digests, and for every D from 4 to 6, which the session accepts and the
 * drive refuses (LR-3), only the leads the window admits: up to 7 - D parked
 * digests after B's take of r and 6 - D before it, the next lead's bundle
 * being a WINDOW_OVERRUN.  Both at B's consumed frame r and r + 1, every
 * parked digest compares clean at the record of its frame.
 */
static int TestLeadParksAndComparesClean(void)
{
	struct LeadRun run;

	for (uint32_t d = NATIVE_LOCKSTEP_MIN_INPUT_DELAY; d <= NATIVE_LOCKSTEP_MAX_INPUT_DELAY; d++)
	{
		for (uint32_t takenR = 0; takenR <= 1u; takenR++)
		{
			uint32_t maxLead;

			InitLeadRun(&run, d, takenR);
			maxLead = MaxLead(&run);
			if (d <= 3u)
			{
				CHECK(maxLead == d + 1u);
			}
			else
			{
				CHECK(maxLead - 1u == ((takenR != 0) ? 7u - d : 6u - d));
			}

			for (uint32_t lead = 1; lead <= maxLead; lead++)
			{
				const uint32_t r = run.r;
				const uint32_t c = r + takenR;

				CHECK(DriveToReceiverRecord(&run) == 0);
				CHECK(LeadAndDeliver(&run, lead) == 0);
				CHECK(g_b.peers[SLOT_A].consumedFrame == c);

				if ((d > 3u) && (lead == maxLead))
				{
					const struct NativeLockstepFaultReport *fault;
					const uint32_t overrunFrame = r + lead + 1u + d;

					/* The next lead: A's tick r + lead + 1 is one more than
					 * the window admits.  Offered to a copy of B, so the
					 * clean comparisons below still run on B itself. */
					CHECK(lead <= d);
					CHECK(LeaderTicks(&run, r + lead + 1u, r + lead + 1u) == 0);
					CHECK(overrunFrame == c + (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY);
					g_scratch = g_b;
					CHECK(NativeLockstepSession_AcceptBundle(&g_scratch, g_sentA[overrunFrame], BUNDLE_BYTES) ==
					      NATIVE_LOCKSTEP_SESSION_FAULT);
					fault = NativeLockstepSession_FirstFault(&g_scratch);
					CHECK(fault != NULL);
					CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
					CHECK(fault->frameIndex == overrunFrame);
					CHECK(fault->detail == c);
					CHECK(NativeLockstepSession_FirstDivergence(&g_scratch) == NULL);
					CHECK(ParkedCount(&g_scratch, SLOT_A) == lead - 1u);
				}

				if (takenR == 0)
				{
					CHECK(TakeFrame(&g_b, r) == NATIVE_LOCKSTEP_SESSION_OK);
				}
				for (uint32_t k = r + 1u; k < r + lead; k++)
				{
					CHECK(RecordFrame(&g_b, k, 0u) == 0);
					CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
					CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
					/* The entry for k was settled, and only it. */
					CHECK(ParkedCount(&g_b, SLOT_A) == r + lead - 1u - k);
					CHECK(TakeFrame(&g_b, k) == NATIVE_LOCKSTEP_SESSION_OK);
				}
				CHECK(ParkedCount(&g_b, SLOT_A) == 0u);
				CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
				CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
			}
		}
	}
	return 0;
}

/*
 * The same leads with A's digest of one parked frame flipped: B's record of
 * that frame still records it and returns 1, and latches DIVERGED with exactly
 * the report the on-arrival comparison gives.  The oracle is a copy of B that
 * records through the flipped frame before the same bundles arrive, so it
 * compares that digest on arrival.
 */
static int TestLeadParkedDivergence(void)
{
	struct LeadRun run;
	struct NativeCanonicalStateV4 clean;
	struct NativeCanonicalStateV4 perturbed;
	const struct NativeLockstepDivergenceReport *oracle;
	const struct NativeLockstepDivergenceReport *report;
	uint32_t cases = 0;

	for (uint32_t d = NATIVE_LOCKSTEP_MIN_INPUT_DELAY; d <= NATIVE_LOCKSTEP_MAX_INPUT_DELAY; d++)
	{
		for (uint32_t takenR = 0; takenR <= 1u; takenR++)
		{
			InitLeadRun(&run, d, takenR);
			for (uint32_t lead = 2; lead <= MaxLead(&run); lead++)
			{
				for (uint32_t p = 1; p < lead; p++)
				{
					const uint32_t r = run.r;
					const uint32_t flipped = r + p;

					run.flipFirst = flipped;
					run.flipLast = flipped;
					CHECK(DriveToReceiverRecord(&run) == 0);
					CHECK(LeaderTicks(&run, r, r + lead) == 0);

					g_oracle = g_b;
					for (uint32_t k = r + 1u; k <= flipped; k++)
					{
						CHECK(RecordFrame(&g_oracle, k, 0u) == 0);
					}
					for (uint32_t frame = r + d; frame <= r + lead + d; frame++)
					{
						const enum NativeLockstepSessionResult expected =
						    (frame == flipped + d + 1u) ? NATIVE_LOCKSTEP_SESSION_DIVERGENCE : NATIVE_LOCKSTEP_SESSION_OK;

						CHECK(NativeLockstepSession_AcceptBundle(&g_oracle, g_sentA[frame], BUNDLE_BYTES) == expected);
						CHECK(NativeLockstepSession_AcceptBundle(&g_b, g_sentA[frame], BUNDLE_BYTES) ==
						      NATIVE_LOCKSTEP_SESSION_OK);
					}
					oracle = NativeLockstepSession_FirstDivergence(&g_oracle);
					CHECK(oracle != NULL);
					CHECK(ParkedCount(&g_b, SLOT_A) == lead - 1u);
					CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);

					if (takenR == 0)
					{
						CHECK(TakeFrame(&g_b, r) == NATIVE_LOCKSTEP_SESSION_OK);
					}
					for (uint32_t k = r + 1u; k < flipped; k++)
					{
						CHECK(RecordFrame(&g_b, k, 0u) == 0);
						CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_RUNNING);
						CHECK(TakeFrame(&g_b, k) == NATIVE_LOCKSTEP_SESSION_OK);
					}
					/* The record of the flipped frame: recorded, 1, DIVERGED. */
					CHECK(RecordFrame(&g_b, flipped, 0u) == 0);
					CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
					report = NativeLockstepSession_FirstDivergence(&g_b);
					CHECK(report != NULL);
					CHECK(memcmp(report, oracle, sizeof(*report)) == 0);
					CHECK(MakeState(&clean, flipped, 0u) == 0);
					CHECK(MakeState(&perturbed, flipped, FLIP_WORLD) == 0);
					CHECK(report->frameIndex == flipped);
					CHECK(report->senderSlot == SLOT_A);
					CHECK(report->mask == (NATIVE_LOCKSTEP_DIVERGENCE_COMBINED | NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN));
					CHECK(report->canonicalDomainMask == (UINT32_C(1) << DOMAIN_WORLD));
					CHECK(report->localCombinedDigest == clean.combinedDigest);
					CHECK(report->remoteCombinedDigest == perturbed.combinedDigest);
					CHECK(memcmp(report->localDomainDigests, clean.domainDigests, sizeof(report->localDomainDigests)) == 0);
					CHECK(memcmp(report->remoteDomainDigests, perturbed.domainDigests, sizeof(report->remoteDomainDigests)) == 0);
					CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);
					/* Terminal for simulation, exactly as on arrival. */
					CHECK(TakeFrame(&g_b, flipped) == NATIVE_LOCKSTEP_SESSION_REJECTED);
					cases++;
				}
			}
		}
	}
	/* With m parked digests at the longest lead there are m(m + 1)/2 (lead,
	 * flipped frame) pairs.  D 1..3: m = D at both consumed frames, so
	 * 2 * (1 + 3 + 6).  D 4..6: m = 6 - D before the take of r and 7 - D after
	 * it, so (3 + 6) + (1 + 3) + (0 + 1). */
	CHECK(cases == 2u * (1u + 3u + 6u) + (3u + 6u) + (1u + 3u) + (0u + 1u));
	return 0;
}

/*
 * A forged digest for frame r + D + 1, one past the lead bound, is the
 * VERIFY_AHEAD protocol fault, never a divergence.  Its report holds the
 * bundle's frameIndex and senderSlot, and detail is r + 1, the number of
 * frames recorded (0 while nothing is): this replaces the "never simulated"
 * FRAME_UNAVAILABLE case.  D and the consumed frame are pinned: D = 2 at
 * consumed frame r and D = 3 at r + 1 reach the session, and D = 3 at r is a
 * WINDOW_OVERRUN instead.  The bound itself, frame r + D, parks.
 */
static int TestVerifyAheadFault(void)
{
	static const struct
	{
		uint32_t delay;
		uint32_t takenR;
		uint32_t overrun;
	} cases[] = {{2u, 0u, 0u}, {3u, 1u, 0u}, {3u, 0u, 1u}};
	struct NativeMatchConfigV1 config;
	struct NativeCanonicalStateV4 state;
	struct LeadRun run;
	const struct NativeLockstepFaultReport *fault;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint8_t crafted[BUNDLE_BYTES];

	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		digests[i] = UINT64_C(0x1000000000000000) + i;
	}

	/* Nothing recorded: no conforming peer can have sent a digest yet. */
	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepSession_Init(&g_b);
	CHECK(NativeLockstepSession_Open(&g_b, &config, INPUT_DELAY, (uint8_t)SLOT_B) == 1);
	CHECK(g_b.recordedAny == 0u);
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, 5u, 5u - INPUT_DELAY - 1u, 1, digests, UINT64_C(0xfeedfacecafebeef), crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD);
	CHECK(fault->frameIndex == 5u);
	CHECK(fault->senderSlot == SLOT_A);
	CHECK(fault->detail == 0u);
	/* A fault, not a divergence: nothing was compared or reported. */
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(g_b.divergence.mask == 0u);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	CHECK(ParkedCount(&g_b, SLOT_A) == 0u);
	/* The window accepted the record before its digest was classified. */
	CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], 5u) != NULL);

	for (size_t n = 0; n < sizeof(cases) / sizeof(cases[0]); n++)
	{
		const uint32_t d = cases[n].delay;
		uint32_t r;
		uint32_t c;
		uint32_t aheadFrame;

		InitLeadRun(&run, d, cases[n].takenR);
		r = run.r;
		c = r + run.takenR;
		aheadFrame = r + 2u * d + 2u;
		CHECK(DriveToReceiverRecord(&run) == 0);

		/* The bound itself: the digest of r + D, in the bundle for r + 2D + 1. */
		CHECK(MakeState(&state, r + d, 0u) == 0);
		CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, r + 2u * d + 1u, r + d, 1, state.domainDigests, state.combinedDigest, crafted) == 0);
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_OK);
		CHECK(ParkedCount(&g_b, SLOT_A) == 1u);

		/* One past it: the digest of r + D + 1, in the bundle for r + 2D + 2. */
		CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, aheadFrame, r + d + 1u, 1, digests, UINT64_C(0x0badc0de0badc0de), crafted) == 0);
		CHECK((aheadFrame < c + (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY) == (cases[n].overrun == 0u));
		CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
		fault = NativeLockstepSession_FirstFault(&g_b);
		CHECK(fault != NULL);
		CHECK(fault->frameIndex == aheadFrame);
		CHECK(fault->senderSlot == SLOT_A);
		if (cases[n].overrun == 0u)
		{
			CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD);
			CHECK(fault->detail == r + 1u);
			CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], aheadFrame) != NULL);
		}
		else
		{
			CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
			CHECK(fault->detail == c);
			CHECK(NativeLockstepInputWindow_Peek(&g_b.peers[SLOT_A], aheadFrame) == NULL);
		}
		CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
		CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
		CHECK(ParkedCount(&g_b, SLOT_A) == 1u);
	}
	return 0;
}

/*
 * The latches keep their rules with parked entries.  D = 2, B consuming r + 1,
 * A leading by 3 ticks, so frames r + 1 and r + 2 are parked.
 */
static int TestParkedLatchRules(void)
{
	struct LeadRun run;
	struct NativeCanonicalStateV4 state;
	struct NativeLockstepDivergenceReport latchedDivergence;
	struct NativeLockstepFaultReport latchedFault;
	const struct NativeLockstepDivergenceReport *report;
	const struct NativeLockstepFaultReport *fault;
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT] = {0};
	uint8_t crafted[BUNDLE_BYTES];
	const uint32_t lead = 3u;
	uint32_t r;

	/* 1. Both parked frames flipped: the first record latches, once; a later
	 *    record is refused, and a later fault latches separately and leaves
	 *    the divergence byte-identical and the mode DIVERGED. */
	InitLeadRun(&run, INPUT_DELAY, 1u);
	r = run.r;
	run.flipFirst = r + 1u;
	run.flipLast = r + 2u;
	CHECK(DriveToReceiverRecord(&run) == 0);
	CHECK(LeadAndDeliver(&run, lead) == 0);
	CHECK(RecordFrame(&g_b, r + 1u, 0u) == 0);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	CHECK(report->frameIndex == r + 1u);
	latchedDivergence = *report;
	CHECK(TakeFrame(&g_b, r + 1u) == NATIVE_LOCKSTEP_SESSION_REJECTED);
	CHECK(MakeState(&state, r + 2u, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 0);
	CHECK(g_b.recordedFrame == r + 1u);
	CHECK(ParkedCount(&g_b, SLOT_A) == 1u);
	CHECK(memcmp(NativeLockstepSession_FirstDivergence(&g_b), &latchedDivergence, sizeof(latchedDivergence)) == 0);
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, r + 1u + 2u * INPUT_DELAY + 2u, r + 1u + INPUT_DELAY + 1u, 1, digests, 0u, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD);
	CHECK(fault->detail == r + 2u);
	latchedFault = *fault;
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(memcmp(NativeLockstepSession_FirstDivergence(&g_b), &latchedDivergence, sizeof(latchedDivergence)) == 0);
	/* And the fault latch is once-only too. */
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, r + 1u + 2u * INPUT_DELAY + 3u, r + 1u + INPUT_DELAY + 2u, 1, digests, 0u, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	CHECK(memcmp(NativeLockstepSession_FirstFault(&g_b), &latchedFault, sizeof(latchedFault)) == 0);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);

	/* 2. A record that skips a parked frame: r + 1 (clean) is FRAME_UNAVAILABLE,
	 *    and it outranks the real mismatch parked for r + 2, because parked
	 *    frames settle in increasing frame order and the latch is once-only. */
	InitLeadRun(&run, INPUT_DELAY, 1u);
	run.flipFirst = r + 2u;
	run.flipLast = r + 2u;
	CHECK(DriveToReceiverRecord(&run) == 0);
	CHECK(LeadAndDeliver(&run, lead) == 0);
	CHECK(RecordFrame(&g_b, r + 2u, 0u) == 0);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_DIVERGED);
	report = NativeLockstepSession_FirstDivergence(&g_b);
	CHECK(report != NULL);
	CHECK(report->mask == NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE);
	CHECK(report->canonicalDomainMask == 0u);
	CHECK(report->frameIndex == r + 1u);
	CHECK(report->senderSlot == SLOT_A);
	CHECK(report->localCombinedDigest == 0u);
	CHECK(MakeState(&state, r + 1u, 0u) == 0);
	CHECK(report->remoteCombinedDigest == state.combinedDigest);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		CHECK(report->localDomainDigests[i] == 0u);
		CHECK(report->remoteDomainDigests[i] == state.domainDigests[i]);
	}
	/* Both entries were settled by that one record. */
	CHECK(ParkedCount(&g_b, SLOT_A) == 0u);
	CHECK(NativeLockstepSession_FirstFault(&g_b) == NULL);

	/* 3. A fault first: the session is FAULTED, the record of the parked
	 *    mismatch is refused, and the parked digest is never compared. */
	InitLeadRun(&run, INPUT_DELAY, 1u);
	run.flipFirst = r + 1u;
	run.flipLast = r + 1u;
	CHECK(DriveToReceiverRecord(&run) == 0);
	CHECK(LeadAndDeliver(&run, lead) == 0);
	CHECK(CraftBundle(&g_b, (uint8_t)SLOT_A, r + 2u * INPUT_DELAY + 3u, r + INPUT_DELAY + 2u, 1, digests, 0u, crafted) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(&g_b, crafted, sizeof(crafted)) == NATIVE_LOCKSTEP_SESSION_FAULT);
	fault = NativeLockstepSession_FirstFault(&g_b);
	CHECK(fault != NULL);
	CHECK(fault->cause == NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD);
	CHECK(fault->detail == r + 1u);
	latchedFault = *fault;
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	CHECK(MakeState(&state, r + 1u, 0u) == 0);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_b, &state) == 0);
	CHECK(ParkedCount(&g_b, SLOT_A) == lead - 1u);
	CHECK(NativeLockstepSession_FirstDivergence(&g_b) == NULL);
	CHECK(memcmp(NativeLockstepSession_FirstFault(&g_b), &latchedFault, sizeof(latchedFault)) == 0);
	CHECK(NativeLockstepSession_Mode(&g_b) == NATIVE_LOCKSTEP_FAULTED);
	return 0;
}

/*
 * Acceptance 6, as LR-S5 left it: a frame already retired from the D + 2 deep
 * digest history is FRAME_UNAVAILABLE, never a protocol fault, while a frame
 * still inside that depth compares normally.
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
	/* The park holds the conforming lead bound for every delay Open admits. */
	CHECK(NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY == NATIVE_LOCKSTEP_MAX_INPUT_DELAY);
	CHECK(NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD == 15);
	CHECK(TestCleanRun() == 0);
	CHECK(TestDivergence() == 0);
	CHECK(TestVerifyAheadFault() == 0);
	CHECK(TestFrameUnavailableRetired() == 0);
	CHECK(TestLeadParksAndComparesClean() == 0);
	CHECK(TestLeadParkedDivergence() == 0);
	CHECK(TestParkedLatchRules() == 0);
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
