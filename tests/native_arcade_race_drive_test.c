#include "platform/native_arcade_race_drive.h"

#include "platform/native_arcade_roster_proof.h"
#include "platform/native_lockstep_match_outcome.h"
#include "platform/native_lockstep_session.h"
#include "platform/native_match_config.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * native_arcade_race_drive_unit (docs/LOCKSTEP_RACE_MILESTONE.md LR-S7,
 * LR-S8).
 *
 * LR-S7: the pad normalization of LR-4 and LR-5 (LR-40) over every status
 * and id byte, each with a disconnected and three connected bytes, START
 * pressed and released, every other button bit and analog byte preserved,
 * NULL arguments, and the neutral pad.
 *
 * LR-S8: two drive cores, A (CAB1_HUMAN) and B (CAB2_HUMAN), over real
 * lockstep sessions opened on one config, exchanging bundles in memory.
 * sendBundle enqueues the bytes into the other side's inbox (with switches to
 * drop them, refuse the send, or freeze the inbox), poll drains the inbox
 * into NativeLockstepSession_AcceptBundle, and onTakeResult feeds a real
 * NativeLockstepMatchOutcomeTracker initialized with 90. Every send and
 * resend is checked inside sendBundle against the send order (LR-29: frame f
 * only after the sender recorded f - D, nothing before the first record,
 * nothing while the session is not RUNNING) and for byte identity with the
 * first send of that frame, and every call's set of sent frames is checked
 * against the resend window (LR-3). Hold is driven with the hold loop's
 * periods count, including skipped periods (LR-44). The disconnected pad is
 * checked against the RL-10 source of truth,
 * NativeArcadeRosterProof_ScriptedPads (TWO_CAB) pads 2 and 3 (LR-43).
 */

static int s_failures;

#define CHECK(expression)                                                            \
	do                                                                               \
	{                                                                                \
		if (!(expression))                                                           \
		{                                                                            \
			fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); \
			s_failures++;                                                            \
			return;                                                                  \
		}                                                                            \
	} while (0)

static struct NativeCanonicalInputPadV1 MakePad(uint8_t connected, uint8_t status, uint8_t id, uint16_t buttons, uint8_t a0, uint8_t a1, uint8_t a2, uint8_t a3)
{
	struct NativeCanonicalInputPadV1 pad;

	memset(&pad, 0, sizeof(pad));
	pad.connected = connected;
	pad.status = status;
	pad.id = id;
	pad.buttons[0] = (uint8_t)(buttons & 0xffu);
	pad.buttons[1] = (uint8_t)(buttons >> 8);
	pad.analog[0] = a0;
	pad.analog[1] = a1;
	pad.analog[2] = a2;
	pad.analog[3] = a3;
	return pad;
}

static uint16_t Buttons(const struct NativeCanonicalInputPadV1 *pad)
{
	return (uint16_t)(pad->buttons[0] | (pad->buttons[1] << 8));
}

static int IsNeutral(const struct NativeCanonicalInputPadV1 *pad)
{
	return (pad->connected == 1u) && (pad->status == 0u) && (pad->id == 0x41u) && (pad->buttons[0] == 0xffu) && (pad->buttons[1] == 0xffu) &&
	       (pad->analog[0] == 0x80u) && (pad->analog[1] == 0x80u) && (pad->analog[2] == 0x80u) && (pad->analog[3] == 0x80u);
}

static void TestConstants(void)
{
	CHECK(NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL == 0x41u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG == 0x73u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_START_MASK == 0x0008u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS == 0xffffu);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG == 0x80u);
	CHECK(sizeof(struct NativeCanonicalInputPadV1) == 9u);
}

static void TestNeutralPad(void)
{
	struct NativeCanonicalInputPadV1 pad;

	memset(&pad, 0x5a, sizeof(pad));
	NativeArcadeRaceDrive_NeutralPad(&pad);
	CHECK(IsNeutral(&pad));

	/* NULL is a no-op (it must not crash). */
	NativeArcadeRaceDrive_NeutralPad(NULL);
}

/* Every status byte and every id byte, each with connected 0, 1, 2, and
 * 0xff: connected 0 is the neutral pad whatever the other bytes; any other
 * connected byte keeps the buttons (START released) and analog bytes, and
 * gets connected 1, status 0, and id 0x73 only for 0x73. */
static void TestEveryStatusAndIdByte(void)
{
	static const uint8_t connectedBytes[4] = {0u, 1u, 2u, 0xffu};

	for (unsigned field = 0; field < 2u; field++)
	{
		for (unsigned value = 0; value < 256u; value++)
		{
			for (unsigned c = 0; c < 4u; c++)
			{
				uint8_t status = field == 0u ? (uint8_t)value : (uint8_t)0x5au;
				uint8_t id = field == 1u ? (uint8_t)value : (uint8_t)0x73u;
				struct NativeCanonicalInputPadV1 in = MakePad(connectedBytes[c], status, id, 0xbfb7u, 0x01u, 0x7fu, 0xfeu, 0x42u);
				struct NativeCanonicalInputPadV1 before = in;
				struct NativeCanonicalInputPadV1 out;

				memset(&out, 0xa5, sizeof(out));
				NativeArcadeRaceDrive_NormalizePad(&in, &out);
				CHECK(memcmp(&in, &before, sizeof(in)) == 0);
				if (connectedBytes[c] == 0u)
				{
					CHECK(IsNeutral(&out));
					continue;
				}
				CHECK(out.connected == 1u);
				CHECK(out.status == 0u);
				CHECK(out.id == (id == 0x73u ? 0x73u : 0x41u));
				/* 0xbfb7: CROSS (0x4000) and START (0x0008) pressed; START is released. */
				CHECK(Buttons(&out) == 0xbfbfu);
				CHECK(out.analog[0] == 0x01u && out.analog[1] == 0x7fu && out.analog[2] == 0xfeu && out.analog[3] == 0x42u);
			}
		}
	}
}

/* START is released whether it came pressed or released; every other bit of
 * the word passes through, one bit pressed at a time and all at once. */
static void TestButtons(void)
{
	struct NativeCanonicalInputPadV1 out;
	struct NativeCanonicalInputPadV1 in;

	in = MakePad(1u, 0u, 0x41u, 0xfff7u, 0x80u, 0x80u, 0x80u, 0x80u);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(Buttons(&out) == 0xffffu);

	in = MakePad(1u, 0u, 0x41u, 0xffffu, 0x80u, 0x80u, 0x80u, 0x80u);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(Buttons(&out) == 0xffffu);

	in = MakePad(1u, 0u, 0x41u, 0x0000u, 0x80u, 0x80u, 0x80u, 0x80u);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(Buttons(&out) == 0x0008u);

	for (unsigned bit = 0; bit < 16u; bit++)
	{
		uint16_t pressed = (uint16_t)(0xffffu & ~(1u << bit));
		uint16_t expected = (uint16_t)(pressed | 0x0008u);

		in = MakePad(1u, 0u, 0x73u, pressed, 0x80u, 0x80u, 0x80u, 0x80u);
		NativeArcadeRaceDrive_NormalizePad(&in, &out);
		CHECK(Buttons(&out) == expected);
		CHECK(out.buttons[0] == (uint8_t)(expected & 0xffu) && out.buttons[1] == (uint8_t)(expected >> 8));
	}

	/* Every button word: only bit 0x0008 changes. */
	for (unsigned word = 0; word < 0x10000u; word++)
	{
		in = MakePad(2u, 0x12u, 0x41u, (uint16_t)word, 0x80u, 0x80u, 0x80u, 0x80u);
		NativeArcadeRaceDrive_NormalizePad(&in, &out);
		CHECK(Buttons(&out) == (uint16_t)(word | 0x0008u));
	}
}

static void TestAnalogBytes(void)
{
	struct NativeCanonicalInputPadV1 in;
	struct NativeCanonicalInputPadV1 out;

	for (unsigned axis = 0; axis < 4u; axis++)
	{
		for (unsigned value = 0; value < 256u; value++)
		{
			in = MakePad(1u, 0xffu, 0xffu, 0xffffu, 0x80u, 0x80u, 0x80u, 0x80u);
			in.analog[axis] = (uint8_t)value;
			NativeArcadeRaceDrive_NormalizePad(&in, &out);
			for (unsigned other = 0; other < 4u; other++)
			{
				CHECK(out.analog[other] == (other == axis ? (uint8_t)value : 0x80u));
			}
			CHECK(out.id == 0x41u && out.status == 0u && out.connected == 1u);
		}
	}

	/* A disconnected pad's analog bytes do not survive. */
	in = MakePad(0u, 0u, 0x73u, 0x0000u, 0x00u, 0xffu, 0x00u, 0xffu);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(IsNeutral(&out));
}

/* The status byte of a connected pad never decides disconnection: 0xff with
 * id 0xff (the pad bus's disconnected packet) is still connected. */
static void TestStatusDoesNotDisconnect(void)
{
	struct NativeCanonicalInputPadV1 in = MakePad(1u, 0xffu, 0xffu, 0xbfffu, 0x10u, 0x20u, 0x30u, 0x40u);
	struct NativeCanonicalInputPadV1 out;

	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(out.connected == 1u && out.status == 0u && out.id == 0x41u);
	CHECK(Buttons(&out) == 0xbfffu);
	CHECK(out.analog[0] == 0x10u && out.analog[3] == 0x40u);
}

static void TestInPlace(void)
{
	struct NativeCanonicalInputPadV1 pad = MakePad(0xffu, 0x33u, 0x73u, 0x7ff7u, 0x00u, 0x11u, 0x22u, 0x33u);

	NativeArcadeRaceDrive_NormalizePad(&pad, &pad);
	CHECK(pad.connected == 1u && pad.status == 0u && pad.id == 0x73u);
	CHECK(Buttons(&pad) == 0x7fffu);
	CHECK(pad.analog[0] == 0x00u && pad.analog[1] == 0x11u && pad.analog[2] == 0x22u && pad.analog[3] == 0x33u);

	pad = MakePad(0u, 0x33u, 0x73u, 0x0000u, 0x00u, 0x11u, 0x22u, 0x33u);
	NativeArcadeRaceDrive_NormalizePad(&pad, &pad);
	CHECK(IsNeutral(&pad));
}

/* The all-zero pad (LR-5's frames 0 to D - 1) is disconnected: neutral. */
static void TestZeroPad(void)
{
	struct NativeCanonicalInputPadV1 in;
	struct NativeCanonicalInputPadV1 out;

	memset(&in, 0, sizeof(in));
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(IsNeutral(&out));
}

static void TestNullArguments(void)
{
	struct NativeCanonicalInputPadV1 in = MakePad(1u, 0u, 0x73u, 0x0000u, 0u, 0u, 0u, 0u);
	struct NativeCanonicalInputPadV1 out;
	struct NativeCanonicalInputPadV1 sentinel;

	memset(&out, 0xa5, sizeof(out));
	sentinel = out;
	NativeArcadeRaceDrive_NormalizePad(NULL, &out);
	CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
	NativeArcadeRaceDrive_NormalizePad(&in, NULL);
	NativeArcadeRaceDrive_NormalizePad(NULL, NULL);
	CHECK(in.connected == 1u && in.id == 0x73u && Buttons(&in) == 0x0000u);
}

/* ------------------------------------------------------------------------
 * LR-S8: the drive core.
 * ------------------------------------------------------------------------ */

#define REQUIRE(expression)                                                            \
	do                                                                                 \
	{                                                                                  \
		if (!(expression))                                                             \
		{                                                                              \
			fprintf(stderr, "REQUIRE failed at line %d: %s\n", __LINE__, #expression); \
			return 0;                                                                  \
		}                                                                              \
	} while (0)

#define BUNDLE_BYTES   NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES
#define STALL_TIMEOUT  90u
#define INBOX_CAPACITY 64u
#define SENT_TABLE     64u
#define CALL_SENDS     64u
#define LOG_FRAMES     18100u
#define NO_TICK        NATIVE_ARCADE_RACE_DRIVE_NO_TICK
#define DRIVE_GO       NATIVE_ARCADE_RACE_DRIVE_GO
#define DRIVE_HOLD     NATIVE_ARCADE_RACE_DRIVE_HOLD
#define DRIVE_END      NATIVE_ARCADE_RACE_DRIVE_END

/* The test's end reasons for an OUTCOME end, mapped from the tracker's cause
 * as the adapter maps them (DESYNC, LINK_ERROR, PEER_TIMEOUT). */
enum TestEndReason
{
	REASON_NONE = 0,
	REASON_DESYNC = 1,
	REASON_LINK_ERROR = 2,
	REASON_PEER_TIMEOUT = 3
};

struct Side
{
	const char *name;
	uint8_t slot;
	int active;
	struct NativeLockstepSession session;
	struct NativeArcadeRaceDriveKept kept;
	struct NativeArcadeRaceDrive drive;
	struct NativeLockstepMatchOutcomeTracker tracker;
	struct NativeArcadeRaceDriveCallbacks callbacks;
	struct Side *peer;

	/* What the peer sent, drained by this side's poll. Deduplicated. */
	uint8_t inbox[INBOX_CAPACITY][BUNDLE_BYTES];
	size_t inboxSize[INBOX_CAPACITY];
	uint32_t inboxCount;
	uint32_t inboxOverflow;

	/* Switches. */
	int dropOutgoing;     /* sent, but lost on the way */
	int refuseSends;      /* the link refuses the send */
	int freezeInbox;      /* poll drains nothing */
	int tamperTakeOnPoll; /* poll moves consumedFrame so the take is REJECTED while RUNNING */
	int latchOnOk;        /* onTakeResult returns latched after an OK take */
	uint32_t knob;        /* the WORLD counter of this side's states from knobFromFrame on */
	uint32_t knobFromFrame;

	/* Observations. */
	uint32_t sendCalls;
	uint32_t orderViolations;
	uint32_t identityViolations;
	uint32_t sendSetViolations;
	uint32_t padViolations;    /* a GO's pads wrong, or HOLD or END wrote padsOut */
	uint32_t periodViolations; /* HeldPeriods differs from the hold's periods */
	uint32_t maxSentFrame;
	int sentAny;
	uint32_t callSends[CALL_SENDS];
	uint32_t callSendCount;
	uint32_t sentFrame[SENT_TABLE];
	uint8_t sentPresent[SENT_TABLE];
	uint8_t sentBytes[SENT_TABLE][BUNDLE_BYTES];
	uint32_t pollCalls;
	uint32_t serviceCalls;
	uint32_t acceptResults[8];
	uint32_t takeCalls[8];
	uint32_t takeCallsTotal;
	uint32_t lastTakeResult;
	uint32_t lastTakeFrame;
	uint32_t takeResultWhileEnded;
	uint32_t firstStallPeriod;

	/* The caller's loop. */
	uint32_t nextTick;
	uint32_t holdPeriods; /* the hold loop's periods on the current held tick */
	int held;
	int ended;
	uint32_t endCallSends;
	uint32_t goCount;
	struct NativeCanonicalInputPadV1 committed[LOG_FRAMES][2];
};

/* File scope: each side holds a session and a pad log. */
static struct Side g_a;
static struct Side g_b;
static struct NativeLockstepSession g_spare;
static struct NativeMatchConfigV1 g_config;
static struct NativeCanonicalStateV4 g_state;
static uint32_t g_stateFrame = UINT32_MAX;
static uint32_t g_stateKnob;
static int g_stateOk;

/* The race's game facts, the same on both sides. */
struct RaceFacts
{
	uint32_t humans;
	uint32_t finishTick[4];
	uint32_t endOfRaceTick;
};
static struct RaceFacts g_race;

static void FillConfig(struct NativeMatchConfigV1 *config, int twoCab)
{
	if (twoCab)
	{
		NativeMatchConfigV1_InitArcadeTwoCab(config);
	}
	else
	{
		NativeMatchConfigV1_InitArcadeOneCab(config);
	}
	config->trackID = 3u;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 30;
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

/* A valid V4 state of frame whose digests are a pure function of the frame
 * and one WORLD counter (the session test's MakeState). Cached, because both
 * sides ask for the same frame in turn. */
static const struct NativeCanonicalStateV4 *StateFor(uint32_t frame, uint32_t knob)
{
	if ((g_stateFrame != frame) || (g_stateKnob != knob))
	{
		NativeCanonicalStateV4_Init(&g_state);
		g_state.frameNumber = frame;
		g_state.control.frameCounter = (int32_t)frame;
		g_state.worldCounters.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
		g_state.worldCounters.activeBombMissileCount = knob;
		g_stateOk = NativeCanonicalStateV4_ComputeDigests(&g_state);
		g_stateFrame = frame;
		g_stateKnob = knob;
	}
	return g_stateOk ? &g_state : NULL;
}

static uint32_t KnobFor(const struct Side *s, uint32_t frame)
{
	return ((s->knob != 0u) && (frame >= s->knobFromFrame)) ? s->knob : 0u;
}

/* A raw local sample of slot at frame: every byte varies, including
 * disconnected samples, odd status and id bytes, and START pressed. */
static void Sample(uint8_t slot, uint32_t frame, struct NativeCanonicalInputPadV1 *pad)
{
	const uint32_t h = ((frame + 1u) * UINT32_C(2654435761)) ^ ((uint32_t)(slot + 1u) * UINT32_C(0x9e3779b9));

	memset(pad, 0, sizeof(*pad));
	pad->connected = (uint8_t)(((frame % 11u) == 5u) ? 0u : (1u + (frame % 3u)));
	pad->status = (uint8_t)(h >> 3);
	pad->id = (uint8_t)(((frame % 4u) == 0u) ? 0x73u : (((frame % 4u) == 1u) ? 0xffu : ((h >> 11) & 0xffu)));
	pad->buttons[0] = (uint8_t)(h >> 16);
	pad->buttons[1] = (uint8_t)(h >> 24);
	for (uint32_t axis = 0; axis < 4u; axis++)
	{
		pad->analog[axis] = (uint8_t)(h >> (5u * axis + 1u));
	}
}

/* The committed pad of slot for frame: neutral for frames 0..D-1, else the
 * normalized sample the slot's owner took D frames earlier. */
static void Expected(uint8_t slot, uint32_t frame, uint32_t delay, struct NativeCanonicalInputPadV1 *pad)
{
	struct NativeCanonicalInputPadV1 raw;

	if (frame < delay)
	{
		NativeArcadeRaceDrive_NeutralPad(pad);
		return;
	}
	Sample(slot, frame - delay, &raw);
	NativeArcadeRaceDrive_NormalizePad(&raw, pad);
}

/* The RL-10 source of truth for the disconnected pad (LR-43): pad 2 of
 * NativeArcadeRosterProof_ScriptedPads(TWO_CAB, TICK_NONE), which the
 * rehearsal installs, copied field by field. Pad 3 must be the same. */
static struct NativeCanonicalInputPadV1 g_rl10Disconnected;

static int Rl10PadEquals(const struct NativeArcadeRosterProofPad *proof, const struct NativeCanonicalInputPadV1 *pad)
{
	return (proof->status == pad->status) && (proof->id == pad->id) && (proof->buttons[0] == pad->buttons[0]) && (proof->buttons[1] == pad->buttons[1]) &&
	       (proof->analog[0] == pad->analog[0]) && (proof->analog[1] == pad->analog[1]) && (proof->analog[2] == pad->analog[2]) &&
	       (proof->analog[3] == pad->analog[3]) && (proof->connected == pad->connected);
}

static int LoadRl10Disconnected(void)
{
	struct NativeArcadeRosterProofPad proof[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];

	memset(proof, 0x5a, sizeof(proof));
	NativeArcadeRosterProof_ScriptedPads(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, proof);
	memset(&g_rl10Disconnected, 0, sizeof(g_rl10Disconnected));
	g_rl10Disconnected.status = proof[2].status;
	g_rl10Disconnected.id = proof[2].id;
	memcpy(g_rl10Disconnected.buttons, proof[2].buttons, sizeof(g_rl10Disconnected.buttons));
	memcpy(g_rl10Disconnected.analog, proof[2].analog, sizeof(g_rl10Disconnected.analog));
	g_rl10Disconnected.connected = proof[2].connected;
	return (g_rl10Disconnected.connected == 0u) && Rl10PadEquals(&proof[3], &g_rl10Disconnected);
}

static int IsDisconnected(const struct NativeCanonicalInputPadV1 *pad)
{
	return memcmp(pad, &g_rl10Disconnected, sizeof(*pad)) == 0;
}

/* padsOut untouched: still the 0xa5 fill. */
static int PadsUntouched(const struct NativeCanonicalInputPadV1 pads[4])
{
	const uint8_t *bytes = (const uint8_t *)pads;

	for (size_t i = 0; i < 4u * sizeof(pads[0]); i++)
	{
		if (bytes[i] != 0xa5u)
		{
			return 0;
		}
	}
	return 1;
}

static int IsNormalized(const struct NativeCanonicalInputPadV1 *pad)
{
	return (pad->connected == 1u) && (pad->status == 0u) && ((pad->id == 0x41u) || (pad->id == 0x73u)) && ((pad->buttons[0] & 0x08u) != 0u);
}

static struct NativeArcadeRaceDriveFacts FactsFor(uint32_t frame)
{
	struct NativeArcadeRaceDriveFacts facts;

	memset(&facts, 0, sizeof(facts));
	facts.endOfRace = (frame >= g_race.endOfRaceTick) ? 1u : 0u;
	facts.humans = g_race.humans;
	for (uint32_t i = 0; (i < g_race.humans) && (i < 4u); i++)
	{
		if (frame >= g_race.finishTick[i])
		{
			facts.finishedHumans++;
		}
	}
	return facts;
}

static enum TestEndReason ReasonOf(const struct Side *s)
{
	const struct NativeLockstepMatchOutcomeReport *report = NativeLockstepMatchOutcome_FirstOutcome(&s->tracker);

	if (report == NULL)
	{
		return REASON_NONE;
	}
	switch (report->cause)
	{
	case NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED:
		return REASON_DESYNC;
	case NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED:
		return REASON_LINK_ERROR;
	case NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT:
		return REASON_PEER_TIMEOUT;
	default:
		return REASON_NONE;
	}
}

static void Deliver(struct Side *to, const uint8_t *bytes, size_t size)
{
	if (size > BUNDLE_BYTES)
	{
		size = BUNDLE_BYTES;
	}
	for (uint32_t i = 0; i < to->inboxCount; i++)
	{
		if ((to->inboxSize[i] == size) && (memcmp(to->inbox[i], bytes, size) == 0))
		{
			return;
		}
	}
	if (to->inboxCount >= INBOX_CAPACITY)
	{
		to->inboxOverflow++;
		return;
	}
	memcpy(to->inbox[to->inboxCount], bytes, size);
	to->inboxSize[to->inboxCount] = size;
	to->inboxCount++;
}

static int CbSend(void *context, uint32_t frameIndex, const uint8_t *bytes, size_t size)
{
	struct Side *s = (struct Side *)context;
	struct NativeLockstepBundleV1 bundle;
	struct NativeCodecReader reader;
	uint32_t cause = 0u;
	const uint32_t index = frameIndex % SENT_TABLE;

	s->sendCalls++;
	if (s->callSendCount < CALL_SENDS)
	{
		s->callSends[s->callSendCount] = frameIndex;
	}
	s->callSendCount++;

	/* LR-29 and LR-3 on every send and resend: RUNNING, after the first
	 * record, and frame f only after recording f - D. */
	if ((bytes == NULL) || (size != BUNDLE_BYTES) || (s->session.mode != NATIVE_LOCKSTEP_RUNNING) || (s->session.recordedAny == 0u) ||
	    ((uint64_t)frameIndex > (uint64_t)s->session.recordedFrame + (uint64_t)s->session.inputDelay))
	{
		s->orderViolations++;
		return 0;
	}
	/* The bytes are this side's bundle of frameIndex... */
	NativeCodecReader_Init(&reader, bytes, size);
	if (!NativeLockstepBundleV1_Decode(&reader, s->session.matchIdentity, s->session.protocolVersion, s->session.inputDelay, &bundle, &cause) ||
	    (bundle.frameIndex != frameIndex) || (bundle.senderSlot != s->slot))
	{
		s->identityViolations++;
	}
	/* ...and byte-identical to its first send (composed exactly once). */
	if ((s->sentPresent[index] != 0u) && (s->sentFrame[index] == frameIndex))
	{
		if (memcmp(s->sentBytes[index], bytes, BUNDLE_BYTES) != 0)
		{
			s->identityViolations++;
		}
	}
	else
	{
		s->sentPresent[index] = 1u;
		s->sentFrame[index] = frameIndex;
		memcpy(s->sentBytes[index], bytes, BUNDLE_BYTES);
	}
	if (!s->sentAny || (frameIndex > s->maxSentFrame))
	{
		s->maxSentFrame = frameIndex;
		s->sentAny = 1;
	}
	if (s->refuseSends)
	{
		return 0;
	}
	if (!s->dropOutgoing)
	{
		Deliver(s->peer, bytes, size);
	}
	return 1;
}

static void CbPoll(void *context)
{
	struct Side *s = (struct Side *)context;

	s->pollCalls++;
	if (!s->freezeInbox)
	{
		for (uint32_t i = 0; i < s->inboxCount; i++)
		{
			const enum NativeLockstepSessionResult result = NativeLockstepSession_AcceptBundle(&s->session, s->inbox[i], s->inboxSize[i]);

			s->acceptResults[(uint32_t)result & 7u]++;
		}
		s->inboxCount = 0u;
	}
	if (s->tamperTakeOnPoll)
	{
		s->tamperTakeOnPoll = 0;
		s->session.consumedFrame++;
	}
}

static int CbTakeResult(void *context, enum NativeLockstepSessionResult result, uint32_t frameIndex)
{
	struct Side *s = (struct Side *)context;

	/* OnTakeResult always comes before the drive ends (LR-9). */
	if (NativeArcadeRaceDrive_EndKind(&s->drive) != NATIVE_ARCADE_RACE_DRIVE_END_NONE)
	{
		s->takeResultWhileEnded++;
	}
	s->takeCalls[(uint32_t)result & 7u]++;
	s->takeCallsTotal++;
	s->lastTakeResult = (uint32_t)result;
	s->lastTakeFrame = frameIndex;
	if ((result == NATIVE_LOCKSTEP_SESSION_STALL) && (s->firstStallPeriod == NO_TICK))
	{
		s->firstStallPeriod = NativeArcadeRaceDrive_HeldPeriods(&s->drive);
	}
	(void)NativeLockstepMatchOutcome_Poll(&s->tracker, &s->session, result, frameIndex);
	if (s->latchOnOk && (result == NATIVE_LOCKSTEP_SESSION_OK))
	{
		return 1;
	}
	return NativeLockstepMatchOutcome_FirstOutcome(&s->tracker) != NULL;
}

static void CbService(void *context)
{
	((struct Side *)context)->serviceCalls++;
}

static int SetupSide(struct Side *s, const char *name, uint8_t role, struct Side *peer, uint32_t delay, uint32_t limit)
{
	memset(s, 0, sizeof(*s));
	s->name = name;
	s->peer = peer;
	s->active = 1;
	s->firstStallPeriod = NO_TICK;
	REQUIRE(NativeMatchConfigV1_FindRoleSlot(&g_config, role, &s->slot) == 1);
	s->callbacks.context = s;
	s->callbacks.sendBundle = CbSend;
	s->callbacks.poll = CbPoll;
	s->callbacks.onTakeResult = CbTakeResult;
	s->callbacks.servicePeriod = CbService;
	NativeLockstepSession_Init(&s->session);
	REQUIRE(NativeLockstepSession_Open(&s->session, &g_config, delay, s->slot) == 1);
	REQUIRE(NativeLockstepMatchOutcome_Init(&s->tracker, STALL_TIMEOUT) == 1);
	REQUIRE(NativeArcadeRaceDrive_Begin(&s->drive, &s->session, &s->kept, &s->callbacks, limit) == 1);
	REQUIRE(NativeArcadeRaceDrive_EndKind(&s->drive) == NATIVE_ARCADE_RACE_DRIVE_END_NONE);
	return 1;
}

static int Setup(uint32_t delay, uint32_t limit)
{
	FillConfig(&g_config, 1);
	memset(&g_race, 0, sizeof(g_race));
	g_race.humans = 2u;
	for (uint32_t i = 0; i < 4u; i++)
	{
		g_race.finishTick[i] = NO_TICK;
	}
	g_race.endOfRaceTick = NO_TICK;
	REQUIRE(SetupSide(&g_a, "A", (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &g_b, delay, limit));
	REQUIRE(SetupSide(&g_b, "B", (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &g_a, delay, limit));
	REQUIRE(g_a.slot != g_b.slot);
	return 1;
}

/* The frames sent by the last call must be exactly low..high, each once. */
static void CheckCallSends(struct Side *s, uint32_t low, uint32_t high)
{
	if ((s->callSendCount != high - low + 1u) || (s->callSendCount > CALL_SENDS))
	{
		s->sendSetViolations++;
		return;
	}
	for (uint32_t frame = low; frame <= high; frame++)
	{
		uint32_t seen = 0u;

		for (uint32_t i = 0; i < s->callSendCount; i++)
		{
			seen += (s->callSends[i] == frame) ? 1u : 0u;
		}
		if (seen != 1u)
		{
			s->sendSetViolations++;
		}
	}
}

/* LR-5: pad 0 is CAB1's (A's) committed pad, pad 1 CAB2's (B's), 2 and 3
 * disconnected. */
static void CheckPads(struct Side *s, uint32_t frame, const struct NativeCanonicalInputPadV1 pads[4])
{
	struct NativeCanonicalInputPadV1 expected;
	const uint32_t delay = s->session.inputDelay;

	Expected(g_a.slot, frame, delay, &expected);
	if ((memcmp(&pads[0], &expected, sizeof(expected)) != 0) || !IsNormalized(&pads[0]))
	{
		s->padViolations++;
	}
	Expected(g_b.slot, frame, delay, &expected);
	if ((memcmp(&pads[1], &expected, sizeof(expected)) != 0) || !IsNormalized(&pads[1]))
	{
		s->padViolations++;
	}
	if (!IsDisconnected(&pads[2]) || !IsDisconnected(&pads[3]))
	{
		s->padViolations++;
	}
}

/*
 * One call of the caller's loop: Step on the next race tick, or Hold while
 * held, with the hold loop's periods advanced by advance (0: a retry
 * iteration; 1: the next period; more: a late pump that skipped periods).
 * Checks the frames each call sent: a Step that does not end sends the new
 * bundle(s) and the window, frames max(0, k - D - 1)..k + D (0..D on tick
 * 0); a newPeriod Hold resends that same window once, however many periods
 * it skipped; any other Hold iteration sends nothing. HOLD and END leave
 * padsOut untouched, and HeldPeriods follows the hold's periods.
 */
static enum NativeArcadeRaceDriveStatus ActPeriods(struct Side *s, uint32_t advance)
{
	struct NativeCanonicalInputPadV1 pads[4];
	enum NativeArcadeRaceDriveStatus status;
	const uint32_t k = s->nextTick;
	const uint32_t delay = s->session.inputDelay;
	const uint32_t low = (k > delay) ? (k - delay - 1u) : 0u;
	int wasHeld = s->held;

	memset(pads, 0xa5, sizeof(pads));
	s->callSendCount = 0u;
	if (s->held)
	{
		s->holdPeriods += advance;
		status = NativeArcadeRaceDrive_Hold(&s->drive, s->holdPeriods, advance != 0u, pads);
	}
	else
	{
		struct NativeCanonicalInputPadV1 sample;
		const struct NativeArcadeRaceDriveFacts facts = FactsFor(k);

		Sample(s->slot, k, &sample);
		s->holdPeriods = 0u;
		status = NativeArcadeRaceDrive_Step(&s->drive, k, StateFor(k, KnobFor(s, k)), &sample, &facts, pads);
	}
	if ((status != DRIVE_GO) && !PadsUntouched(pads))
	{
		s->padViolations++;
	}
	if ((status != DRIVE_END) && (NativeArcadeRaceDrive_HeldPeriods(&s->drive) != s->holdPeriods))
	{
		s->periodViolations++;
	}
	if (status == DRIVE_END)
	{
		if (!s->ended)
		{
			s->endCallSends = s->callSendCount;
		}
		else if (s->callSendCount != 0u)
		{
			s->sendSetViolations++;
		}
		s->ended = 1;
		s->held = 0;
		return status;
	}
	if (s->ended)
	{
		/* After END every call must return END. */
		s->sendSetViolations++;
		return status;
	}
	if (wasHeld && (advance == 0u))
	{
		if (s->callSendCount != 0u)
		{
			s->sendSetViolations++;
		}
	}
	else
	{
		CheckCallSends(s, low, k + delay);
		if (!wasHeld && (k > 0u) && ((s->callSendCount == 0u) || (s->callSends[0] != k + delay)))
		{
			/* The new bundle goes first, then the resends. */
			s->sendSetViolations++;
		}
	}
	if (status == DRIVE_HOLD)
	{
		s->held = 1;
		return status;
	}
	CheckPads(s, k, pads);
	if (k < LOG_FRAMES)
	{
		memcpy(s->committed[k], pads, sizeof(s->committed[k]));
	}
	s->goCount++;
	s->nextTick = k + 1u;
	s->held = 0;
	return status;
}

/* One call with newPeriod as given: the next period, or a retry iteration. */
static enum NativeArcadeRaceDriveStatus Act(struct Side *s, int newPeriod)
{
	return ActPeriods(s, newPeriod ? 1u : 0u);
}

/* Rounds of A then B; a side acts while active, not ended, and either held
 * or below target. Stops when neither acts or after maxRounds. */
static void RunUntil(uint32_t target, uint32_t maxRounds)
{
	struct Side *sides[2] = {&g_a, &g_b};

	for (uint32_t round = 0; round < maxRounds; round++)
	{
		int acted = 0;

		for (uint32_t i = 0; i < 2u; i++)
		{
			struct Side *s = sides[i];

			if (s->active && !s->ended && (s->held || (s->nextTick < target)))
			{
				(void)Act(s, 1);
				acted = 1;
			}
		}
		if (!acted)
		{
			return;
		}
	}
}

/* No send-order, identity, window, or pad violation, no late OnTakeResult,
 * and (for a live peer) no inbox overflow. */
static int CheckClean(const struct Side *s)
{
	REQUIRE(s->orderViolations == 0u);
	REQUIRE(s->identityViolations == 0u);
	REQUIRE(s->sendSetViolations == 0u);
	REQUIRE(s->padViolations == 0u);
	REQUIRE(s->periodViolations == 0u);
	REQUIRE(s->takeResultWhileEnded == 0u);
	REQUIRE(s->inboxOverflow == 0u);
	return 1;
}

static int CheckHealthy(const struct Side *s)
{
	REQUIRE(CheckClean(s));
	REQUIRE(NativeLockstepSession_Mode(&s->session) == NATIVE_LOCKSTEP_RUNNING);
	REQUIRE(NativeLockstepSession_FirstFault(&s->session) == NULL);
	REQUIRE(NativeLockstepSession_FirstDivergence(&s->session) == NULL);
	REQUIRE(NativeLockstepMatchOutcome_FirstOutcome(&s->tracker) == NULL);
	REQUIRE(s->acceptResults[NATIVE_LOCKSTEP_SESSION_FAULT] == 0u);
	REQUIRE(s->acceptResults[NATIVE_LOCKSTEP_SESSION_DIVERGENCE] == 0u);
	REQUIRE(!s->ended);
	REQUIRE(NativeArcadeRaceDrive_EndKind(&s->drive) == NATIVE_ARCADE_RACE_DRIVE_END_NONE);
	return 1;
}

/* Both sides committed identical pads for frames 0..count-1. */
static int CheckSamePads(uint32_t count)
{
	for (uint32_t frame = 0; (frame < count) && (frame < LOG_FRAMES); frame++)
	{
		REQUIRE(memcmp(g_a.committed[frame], g_b.committed[frame], sizeof(g_a.committed[frame])) == 0);
	}
	return 1;
}

/* After an end, nothing more is sent, polled, or taken: Step, Hold, and the
 * linger all do nothing. */
static int CheckSilentAfterEnd(struct Side *s)
{
	struct NativeCanonicalInputPadV1 pads[4];
	const uint32_t sends = s->sendCalls;
	const uint32_t polls = s->pollCalls;
	const uint32_t takes = s->takeCallsTotal;
	const uint32_t composed = NativeArcadeRaceDrive_ComposedCount(&s->drive);
	const struct NativeArcadeRaceDriveFacts facts = FactsFor(s->nextTick);
	struct NativeCanonicalInputPadV1 sample;

	REQUIRE(s->ended);
	Sample(s->slot, s->nextTick, &sample);
	memset(pads, 0xa5, sizeof(pads));
	for (uint32_t i = 0; i < 3u; i++)
	{
		const uint32_t held = NativeArcadeRaceDrive_HeldPeriods(&s->drive);

		REQUIRE(NativeArcadeRaceDrive_Step(&s->drive, s->nextTick, StateFor(s->nextTick, 0u), &sample, &facts, pads) == DRIVE_END);
		REQUIRE(NativeArcadeRaceDrive_Step(&s->drive, s->nextTick + 1u, StateFor(s->nextTick + 1u, 0u), &sample, &facts, pads) == DRIVE_END);
		REQUIRE(NativeArcadeRaceDrive_Hold(&s->drive, held + 1u, 1, pads) == DRIVE_END);
		REQUIRE(NativeArcadeRaceDrive_Hold(&s->drive, held, 0, pads) == DRIVE_END);
	}
	REQUIRE(PadsUntouched(pads));
	if (!NativeArcadeRaceDrive_EndIsFinish(&s->drive))
	{
		for (uint32_t i = 0; i < 20u; i++)
		{
			REQUIRE(NativeArcadeRaceDrive_LingerTick(&s->drive, 1) == 0u);
		}
	}
	REQUIRE(s->sendCalls == sends);
	REQUIRE(s->pollCalls == polls);
	REQUIRE(s->takeCallsTotal == takes);
	REQUIRE(NativeArcadeRaceDrive_ComposedCount(&s->drive) == composed);
	return 1;
}

static void TestDriveConstants(void)
{
	struct NativeCanonicalInputPadV1 pad;

	CHECK(NATIVE_ARCADE_RACE_DRIVE_MAX_INPUT_DELAY == 3u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_START_GRACE_PERIODS == 810u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_HOLD_GRACE_PERIODS == 10u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_FINISH_GRACE_TICKS == 900u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS == 15u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT == 18000u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY == 8u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT == 4u);
	/* 2D + 2 frames of the hold's window fit the ring for D <= 3 only. */
	CHECK(2u * NATIVE_ARCADE_RACE_DRIVE_MAX_INPUT_DELAY + 2u <= NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY);

	/* The RL-10 rehearsal's disconnected pad, byte for byte: pads 2 and 3 of
	 * NativeArcadeRosterProof_ScriptedPads(TWO_CAB) (LR-43), on the neutral
	 * tick and on a scripted tick, and the header's named bytes. */
	CHECK(LoadRl10Disconnected());
	memset(&pad, 0x5a, sizeof(pad));
	NativeArcadeRaceDrive_DisconnectedPad(&pad);
	CHECK(IsDisconnected(&pad));
	for (uint32_t tick = 0; tick < 2u; tick++)
	{
		struct NativeArcadeRosterProofPad proof[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];

		NativeArcadeRosterProof_ScriptedPads(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB, (tick == 0u) ? NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE : 100u, proof);
		CHECK(Rl10PadEquals(&proof[2], &pad));
		CHECK(Rl10PadEquals(&proof[3], &pad));
	}
	CHECK(pad.status == NATIVE_ARCADE_RACE_DRIVE_DISCONNECTED_STATUS && pad.id == NATIVE_ARCADE_RACE_DRIVE_DISCONNECTED_ID);
	NativeArcadeRaceDrive_DisconnectedPad(NULL);

	for (uint32_t periods = 0; periods < 10u; periods++)
	{
		CHECK(NativeArcadeRaceDrive_BannerDue(periods) == 0);
	}
	CHECK(NativeArcadeRaceDrive_BannerDue(10u) != 0);
	CHECK(NativeArcadeRaceDrive_BannerDue(11u) != 0);
	CHECK(NativeArcadeRaceDrive_BannerDue(UINT32_MAX) != 0);

	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName(NATIVE_ARCADE_RACE_DRIVE_END_NONE), "none") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName(NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE), "end of race") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName(NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE), "finish grace") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName(NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT), "race tick limit") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName(NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME), "outcome") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName(NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE), "local failure") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_EndKindName((enum NativeArcadeRaceDriveEndKind)99), "unknown") == 0);
	for (uint32_t reason = 0; reason <= (uint32_t)NATIVE_ARCADE_RACE_DRIVE_FAILURE_PERIODS; reason++)
	{
		const char *name = NativeArcadeRaceDrive_FailureName((enum NativeArcadeRaceDriveFailure)reason);

		CHECK(name != NULL);
		CHECK(strcmp(name, "unknown") != 0);
		for (uint32_t other = 0; other < reason; other++)
		{
			CHECK(strcmp(name, NativeArcadeRaceDrive_FailureName((enum NativeArcadeRaceDriveFailure)other)) != 0);
		}
	}
	CHECK(strcmp(NativeArcadeRaceDrive_FailureName((enum NativeArcadeRaceDriveFailure)20), "unknown") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_FailureName(NATIVE_ARCADE_RACE_DRIVE_FAILURE_INPUT_DELAY), "input delay outside 1..3") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_FailureName(NATIVE_ARCADE_RACE_DRIVE_FAILURE_LOCAL_SLOT), "local slot not a cabinet role") == 0);
	CHECK(strcmp(NativeArcadeRaceDrive_FailureName(NATIVE_ARCADE_RACE_DRIVE_FAILURE_PERIODS), "held periods inconsistent") == 0);

	/* NULL drive: END, nothing, and neutral accessors. */
	CHECK(NativeArcadeRaceDrive_Step(NULL, 0u, NULL, NULL, NULL, NULL) == DRIVE_END);
	CHECK(NativeArcadeRaceDrive_Hold(NULL, 1u, 1, NULL) == DRIVE_END);
	CHECK(NativeArcadeRaceDrive_LingerTick(NULL, 1) == 0u);
	CHECK(NativeArcadeRaceDrive_EndKind(NULL) == NATIVE_ARCADE_RACE_DRIVE_END_NONE);
	CHECK(NativeArcadeRaceDrive_EndTick(NULL) == NO_TICK);
	CHECK(NativeArcadeRaceDrive_EndIsFinish(NULL) == 0);
}

/* Begin's refusals end the drive as a named local failure and send nothing
 * (LR-3: D outside 1..3 is refused). */
static int BeginRefused(struct NativeArcadeRaceDrive *drive, struct NativeLockstepSession *session, struct NativeArcadeRaceDriveKept *kept,
                        const struct NativeArcadeRaceDriveCallbacks *callbacks, uint32_t limit, enum NativeArcadeRaceDriveFailure reason)
{
	struct NativeCanonicalInputPadV1 pads[4];
	struct NativeCanonicalInputPadV1 sample;
	const struct NativeArcadeRaceDriveFacts facts = FactsFor(0u);
	const uint32_t sends = g_a.sendCalls;
	const uint32_t polls = g_a.pollCalls;

	Sample(0u, 0u, &sample);
	REQUIRE(NativeArcadeRaceDrive_Begin(drive, session, kept, callbacks, limit) == 0);
	REQUIRE(NativeArcadeRaceDrive_EndKind(drive) == NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE);
	REQUIRE(NativeArcadeRaceDrive_FailureReason(drive) == reason);
	REQUIRE(NativeArcadeRaceDrive_EndIsFinish(drive) == 0);
	REQUIRE(NativeArcadeRaceDrive_Step(drive, 0u, StateFor(0u, 0u), &sample, &facts, pads) == DRIVE_END);
	REQUIRE(NativeArcadeRaceDrive_Hold(drive, 1u, 1, pads) == DRIVE_END);
	REQUIRE(NativeArcadeRaceDrive_LingerTick(drive, 1) == 0u);
	REQUIRE(g_a.sendCalls == sends);
	REQUIRE(g_a.pollCalls == polls);
	return 1;
}

static void TestDriveBegin(void)
{
	static struct NativeArcadeRaceDrive drive;
	static struct NativeArcadeRaceDriveKept kept;
	struct NativeArcadeRaceDriveCallbacks callbacks;
	struct NativeMatchConfigV1 oneCab;

	CHECK(Setup(2u, 0u));
	callbacks = g_a.callbacks;
	CHECK(NativeArcadeRaceDrive_Begin(NULL, &g_a.session, &kept, &callbacks, 0u) == 0);

	/* Missing arguments and callbacks. */
	CHECK(BeginRefused(&drive, NULL, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT));
	CHECK(BeginRefused(&drive, &g_a.session, NULL, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT));
	CHECK(BeginRefused(&drive, &g_a.session, &kept, NULL, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT));
	callbacks.sendBundle = NULL;
	CHECK(BeginRefused(&drive, &g_a.session, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT));
	callbacks = g_a.callbacks;
	callbacks.poll = NULL;
	CHECK(BeginRefused(&drive, &g_a.session, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT));
	callbacks = g_a.callbacks;
	callbacks.onTakeResult = NULL;
	CHECK(BeginRefused(&drive, &g_a.session, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT));
	callbacks = g_a.callbacks;
	callbacks.servicePeriod = NULL;
	CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_a.session, &kept, &callbacks, 0u) == 1);
	callbacks = g_a.callbacks;

	/* The race tick limit: 0 is 18000; the override may only lower it. */
	CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_a.session, &kept, &callbacks, 0u) == 1);
	CHECK(NativeArcadeRaceDrive_RaceTickLimit(&drive) == 18000u);
	CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_a.session, &kept, &callbacks, 18000u) == 1);
	CHECK(NativeArcadeRaceDrive_RaceTickLimit(&drive) == 18000u);
	CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_a.session, &kept, &callbacks, 1u) == 1);
	CHECK(NativeArcadeRaceDrive_RaceTickLimit(&drive) == 1u);
	CHECK(BeginRefused(&drive, &g_a.session, &kept, &callbacks, 18001u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_TICK_LIMIT));
	CHECK(BeginRefused(&drive, &g_a.session, &kept, &callbacks, UINT32_MAX, NATIVE_ARCADE_RACE_DRIVE_FAILURE_TICK_LIMIT));

	/* Begin reinitializes the drive and clears the kept ring. */
	memset(&kept, 0xa5, sizeof(kept));
	CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_a.session, &kept, &callbacks, 0u) == 1);
	for (uint32_t i = 0; i < NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY; i++)
	{
		CHECK(kept.entries[i].present == 0u);
	}
	CHECK(NativeArcadeRaceDrive_EndKind(&drive) == NATIVE_ARCADE_RACE_DRIVE_END_NONE);
	CHECK(NativeArcadeRaceDrive_RaceTick(&drive) == NO_TICK);
	CHECK(NativeArcadeRaceDrive_EndTick(&drive) == NO_TICK);
	CHECK(NativeArcadeRaceDrive_GraceStartTick(&drive) == NO_TICK);

	/* A session that is IDLE, or not RUNNING. */
	NativeLockstepSession_Init(&g_spare);
	CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_MODE));
	CHECK(NativeLockstepSession_Open(&g_spare, &g_config, 2u, g_a.slot) == 1);
	CHECK(NativeLockstepSession_AcceptBundle(&g_spare, g_a.inbox[0], 64u) == NATIVE_LOCKSTEP_SESSION_FAULT);
	CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_MODE));

	/* A session that has already recorded a frame. */
	NativeLockstepSession_Init(&g_spare);
	CHECK(NativeLockstepSession_Open(&g_spare, &g_config, 2u, g_a.slot) == 1);
	CHECK(NativeLockstepSession_RecordLocalDigests(&g_spare, StateFor(0u, 0u)) == 1);
	CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_STARTED));

	/* D 0 (the session refuses it at Open; forced here) is refused. */
	NativeLockstepSession_Init(&g_spare);
	CHECK(NativeLockstepSession_Open(&g_spare, &g_config, 0u, g_a.slot) == 0);
	CHECK(NativeLockstepSession_Open(&g_spare, &g_config, 1u, g_a.slot) == 1);
	g_spare.inputDelay = 0u;
	CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_INPUT_DELAY));

	/* A session whose localSlot is neither role slot (the session only opens
	 * on a human slot; forced here to each bot slot and two past the end). */
	NativeLockstepSession_Init(&g_spare);
	CHECK(NativeLockstepSession_Open(&g_spare, &g_config, 2u, g_b.slot) == 1);
	CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_spare, &kept, &callbacks, 0u) == 1);
	for (uint8_t slot = 0u; slot < 8u; slot++)
	{
		g_spare.localSlot = slot;
		if ((slot == g_a.slot) || (slot == g_b.slot))
		{
			CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_spare, &kept, &callbacks, 0u) == 1);
		}
		else
		{
			CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_LOCAL_SLOT));
		}
	}

	/* D above 3 is refused (LR-3); 1..3 run. The session itself accepts
	 * up to 6. */
	for (uint32_t delay = 1u; delay <= 6u; delay++)
	{
		NativeLockstepSession_Init(&g_spare);
		CHECK(NativeLockstepSession_Open(&g_spare, &g_config, delay, g_a.slot) == 1);
		if (delay <= 3u)
		{
			CHECK(NativeArcadeRaceDrive_Begin(&drive, &g_spare, &kept, &callbacks, 0u) == 1);
		}
		else
		{
			CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_INPUT_DELAY));
		}
	}

	/* A config without a CAB2_HUMAN slot. */
	FillConfig(&oneCab, 0);
	NativeLockstepSession_Init(&g_spare);
	CHECK(NativeLockstepSession_Open(&g_spare, &oneCab, 2u, 0u) == 1);
	CHECK(BeginRefused(&drive, &g_spare, &kept, &callbacks, 0u, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_SLOT));
	CHECK(g_a.sendCalls == 0u);

	/* A drive that was never begun. */
	NativeArcadeRaceDrive_Init(&drive);
	{
		struct NativeCanonicalInputPadV1 pads[4];
		struct NativeCanonicalInputPadV1 sample;
		const struct NativeArcadeRaceDriveFacts facts = FactsFor(0u);

		Sample(0u, 0u, &sample);
		CHECK(NativeArcadeRaceDrive_Step(&drive, 0u, StateFor(0u, 0u), &sample, &facts, pads) == DRIVE_END);
		CHECK(NativeArcadeRaceDrive_FailureReason(&drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NOT_BEGUN);
		NativeArcadeRaceDrive_Init(&drive);
		CHECK(NativeArcadeRaceDrive_Hold(&drive, 1u, 1, pads) == DRIVE_END);
		CHECK(NativeArcadeRaceDrive_FailureReason(&drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NOT_BEGUN);
	}
}

/* Equal inputs (LR-2, LR-3, LR-5) at D = 1, 2, 3: both sides commit identical
 * pads every frame, each equal to the normalized sample its owner submitted
 * D frames earlier, neutral for frames 0..D-1; every bundle is composed once. */
static void TestDriveEqualInputs(void)
{
	for (uint32_t delay = 1u; delay <= 3u; delay++)
	{
		CHECK(Setup(delay, 0u));
		RunUntil(300u, 2000u);
		CHECK(g_a.nextTick == 300u && g_b.nextTick == 300u);
		CHECK(!g_a.held && !g_b.held);
		CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
		CHECK(CheckSamePads(300u));
		for (uint32_t frame = 0; frame < delay; frame++)
		{
			struct NativeCanonicalInputPadV1 neutral;

			NativeArcadeRaceDrive_NeutralPad(&neutral);
			CHECK(memcmp(&g_a.committed[frame][0], &neutral, sizeof(neutral)) == 0);
			CHECK(memcmp(&g_a.committed[frame][1], &neutral, sizeof(neutral)) == 0);
		}
		CHECK(g_a.goCount == 300u && g_b.goCount == 300u);
		CHECK(NativeArcadeRaceDrive_ComposedCount(&g_a.drive) == delay + 300u);
		CHECK(NativeArcadeRaceDrive_ComposedCount(&g_b.drive) == delay + 300u);
		CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_OK] == 300u && g_b.takeCalls[NATIVE_LOCKSTEP_SESSION_OK] == 300u);
		CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 0u && g_b.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 0u);
		CHECK(NativeArcadeRaceDrive_RaceTick(&g_a.drive) == 299u);
		CHECK(NativeArcadeRaceDrive_GraceStartTick(&g_a.drive) == NO_TICK);
	}
}

/* The send order on race tick 0 (LR-29): Begin sends nothing, and tick 0
 * sends frames 0..D, in order, only after recording frame 0. */
static void TestDriveSendOrderAtStart(void)
{
	CHECK(Setup(2u, 0u));
	CHECK(g_a.sendCalls == 0u && g_b.sendCalls == 0u);
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	CHECK(g_a.callSendCount == 3u);
	CHECK(g_a.callSends[0] == 0u && g_a.callSends[1] == 1u && g_a.callSends[2] == 2u);
	CHECK(g_a.session.recordedAny != 0u && g_a.session.recordedFrame == 0u);
	CHECK(NativeArcadeRaceDrive_ComposedCount(&g_a.drive) == 3u);
	CHECK(g_a.takeCallsTotal == 0u);
	CHECK(Act(&g_b, 1) == DRIVE_GO);
	CHECK(Act(&g_a, 1) == DRIVE_GO);
	/* The start grace: no STALL was reported for tick 0's short hold. */
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 0u);
	RunUntil(20u, 200u);
	CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
}

/* Hold iterations that are not a new period poll and retry the take only:
 * no period, no resend, no service, no STALL report. */
static void TestDriveHoldIterations(void)
{
	CHECK(Setup(2u, 0u));
	RunUntil(10u, 100u);
	g_b.active = 0;
	for (uint32_t i = 0; (i < 10u) && !g_a.held; i++)
	{
		CHECK(Act(&g_a, 1) != DRIVE_END);
	}
	CHECK(g_a.held);
	g_a.serviceCalls = 0u; /* the count of this hold only (tick 0's start hold served once) */
	for (uint32_t i = 0; i < 10u; i++)
	{
		const uint32_t polls = g_a.pollCalls;

		CHECK(Act(&g_a, 0) == DRIVE_HOLD);
		CHECK(g_a.pollCalls == polls + 1u);
		CHECK(g_a.callSendCount == 0u);
	}
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 0u);
	CHECK(g_a.serviceCalls == 0u);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 0u);
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 1u);
	CHECK(g_a.serviceCalls == 1u);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 1u);
	CHECK(CheckClean(&g_a));
}

/* Stall and resume (LR-9): a peer frozen for 45 periods; stalls are counted
 * once per full period, never per iteration; the take resumes on a retry,
 * OK resets the count, and the race runs on in step. */
static void TestDriveStallAndResume(void)
{
	CHECK(Setup(2u, 0u));
	RunUntil(50u, 500u);
	g_b.active = 0;
	for (uint32_t i = 0; (i < 10u) && !g_a.held; i++)
	{
		CHECK(Act(&g_a, 1) != DRIVE_END);
	}
	CHECK(g_a.held);
	g_a.serviceCalls = 0u; /* the count of this hold only (tick 0's start hold served once) */
	/* A leads by D + 1 ticks. */
	CHECK(g_a.session.recordedFrame == g_b.session.recordedFrame + 3u);
	for (uint32_t period = 1u; period <= 45u; period++)
	{
		CHECK(Act(&g_a, 1) == DRIVE_HOLD);
		CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == period);
		CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == period);
		CHECK(NativeArcadeRaceDrive_BannerDue(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive)) == (period >= 10u));
		for (uint32_t i = 0; i < 3u; i++)
		{
			CHECK(Act(&g_a, 0) == DRIVE_HOLD);
		}
		CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == period);
	}
	CHECK(g_a.serviceCalls == 45u);
	CHECK(g_a.tracker.consecutiveStallFrames == 45u);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker) == NULL);

	g_b.active = 1;
	CHECK(Act(&g_b, 1) == DRIVE_GO);
	/* A retry iteration (not a new period) resumes. */
	CHECK(Act(&g_a, 0) == DRIVE_GO);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 45u);
	CHECK(g_a.tracker.consecutiveStallFrames == 0u);
	/* The tick after the stall: its sends are checked in Act and CbSend. */
	CHECK(Act(&g_a, 1) != DRIVE_END);
	RunUntil(150u, 1000u);
	CHECK(g_a.nextTick == 150u && g_b.nextTick == 150u);
	CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
	CHECK(CheckSamePads(150u));
}

/* A peer drop (killed): the survivor holds and the stall timeout ends it at
 * exactly 90 counted periods, as the outcome (PEER_TIMEOUT), not a local
 * failure; iterations between periods do not count. */
static void TestDriveStallTimeout(void)
{
	uint32_t period = 0u;
	enum NativeArcadeRaceDriveStatus status = DRIVE_HOLD;
	const struct NativeLockstepMatchOutcomeReport *report;

	CHECK(Setup(2u, 0u));
	RunUntil(40u, 500u);
	g_b.active = 0;
	for (uint32_t i = 0; (i < 10u) && !g_a.held; i++)
	{
		CHECK(Act(&g_a, 1) != DRIVE_END);
	}
	CHECK(g_a.held);
	g_a.serviceCalls = 0u; /* the count of this hold only (tick 0's start hold served once) */
	while ((status == DRIVE_HOLD) && (period < 200u))
	{
		period++;
		status = Act(&g_a, 1);
		for (uint32_t i = 0; (i < 3u) && (status == DRIVE_HOLD); i++)
		{
			CHECK(Act(&g_a, 0) == DRIVE_HOLD);
		}
	}
	CHECK(status == DRIVE_END);
	CHECK(period == STALL_TIMEOUT);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == STALL_TIMEOUT);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(g_a.serviceCalls == STALL_TIMEOUT);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	CHECK(NativeArcadeRaceDrive_EndIsFinish(&g_a.drive) == 0);
	CHECK(NativeArcadeRaceDrive_EndTick(&g_a.drive) == g_a.nextTick);
	report = NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker);
	CHECK(report != NULL);
	CHECK(report->stalledFrameCount == STALL_TIMEOUT);
	CHECK(report->frameIndex == g_a.nextTick);
	CHECK(ReasonOf(&g_a) == REASON_PEER_TIMEOUT);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));
}

/* The start wait (LR-9, LR-12): a peer that never starts. The first 810
 * periods of race tick 0 are not reported, so the wait ends at exactly
 * 810 + 90 periods; the ring window and the service run every period. */
static void TestDriveStartWait(void)
{
	enum NativeArcadeRaceDriveStatus status = DRIVE_HOLD;
	uint32_t period = 0u;

	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	CHECK(NativeArcadeRaceDrive_RaceTick(&g_a.drive) == 0u);
	while ((status == DRIVE_HOLD) && (period < 2000u))
	{
		period++;
		status = Act(&g_a, 1);
		if (period == 810u)
		{
			CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 0u);
		}
		if (period == 811u)
		{
			CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 1u);
		}
	}
	CHECK(status == DRIVE_END);
	CHECK(period == 900u);
	CHECK(g_a.firstStallPeriod == 811u);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(g_a.serviceCalls == 900u);
	CHECK(g_a.sendCalls == 3u + 900u * 3u);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(ReasonOf(&g_a) == REASON_PEER_TIMEOUT);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker) != NULL);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker)->frameIndex == 0u);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));

	/* A peer that starts inside the grace (after 40 counted periods). */
	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	for (period = 1u; period <= 850u; period++)
	{
		CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	}
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 40u);
	g_b.active = 1;
	CHECK(Act(&g_b, 1) == DRIVE_GO);
	CHECK(Act(&g_a, 1) == DRIVE_GO);
	CHECK(g_a.tracker.consecutiveStallFrames == 0u);
	RunUntil(60u, 500u);
	CHECK(g_a.nextTick == 60u && g_b.nextTick == 60u);
	CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
	CHECK(CheckSamePads(60u));
}

/* Holds A on its next tick with B inactive (B stays inactive). */
static int HoldA(uint32_t target)
{
	RunUntil(target, 500u);
	g_b.active = 0;
	for (uint32_t i = 0; (i < 10u) && !g_a.held; i++)
	{
		REQUIRE(Act(&g_a, 1) != DRIVE_END);
	}
	REQUIRE(g_a.held);
	g_a.serviceCalls = 0u; /* the count of this hold only */
	REQUIRE(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 0u);
	return 1;
}

/* Skipped periods (LR-44): a late pump raises newPeriod once for several
 * wall-time periods. Each newly elapsed period is one STALL report, but the
 * window is resent and the service called once per call, and HeldPeriods is
 * the hold's periods. A late period that also resumes reports nothing. */
static void TestDriveSkippedPeriods(void)
{
	const uint32_t window = 2u * 2u + 2u;

	CHECK(Setup(2u, 0u));
	CHECK(HoldA(40u));
	CHECK(ActPeriods(&g_a, 1u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 1u);
	CHECK(g_a.serviceCalls == 1u && g_a.callSendCount == window);
	CHECK(ActPeriods(&g_a, 0u) == DRIVE_HOLD);
	/* Periods 1 -> 5 in one call: 4 reports, one resend, one service. */
	CHECK(ActPeriods(&g_a, 4u) == DRIVE_HOLD);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 5u);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 5u);
	CHECK(g_a.tracker.consecutiveStallFrames == 5u);
	CHECK(g_a.serviceCalls == 2u && g_a.callSendCount == window);
	CHECK(NativeArcadeRaceDrive_BannerDue(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive)) == 0);
	CHECK(ActPeriods(&g_a, 6u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 11u);
	CHECK(NativeArcadeRaceDrive_BannerDue(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive)) != 0);

	/* The peer resumes; a late call over 2 periods takes OK: no report. */
	g_b.active = 1;
	CHECK(Act(&g_b, 1) == DRIVE_GO);
	CHECK(ActPeriods(&g_a, 2u) == DRIVE_GO);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 11u);
	CHECK(g_a.serviceCalls == 4u);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 13u);
	CHECK(g_a.tracker.consecutiveStallFrames == 0u);
	RunUntil(100u, 1000u);
	CHECK(g_a.nextTick == 100u && g_b.nextTick == 100u);
	CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
	CHECK(CheckSamePads(100u));
}

/* The stall timeout with skipped periods lands at exactly 90 counted
 * periods: jumps of 7 reach 84, and the jump to 91 reports 85..90 and stops
 * at the latch; one jump of 1000 reports exactly 90. */
static void TestDriveStallTimeoutSkipped(void)
{
	const struct NativeLockstepMatchOutcomeReport *report;

	CHECK(Setup(2u, 0u));
	CHECK(HoldA(40u));
	for (uint32_t jump = 1u; jump <= 12u; jump++)
	{
		CHECK(ActPeriods(&g_a, 7u) == DRIVE_HOLD);
		CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 7u * jump);
	}
	CHECK(ActPeriods(&g_a, 7u) == DRIVE_END);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 91u);
	CHECK(g_a.serviceCalls == 13u);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	report = NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker);
	CHECK(report != NULL && report->stalledFrameCount == STALL_TIMEOUT && report->frameIndex == g_a.nextTick);
	CHECK(ReasonOf(&g_a) == REASON_PEER_TIMEOUT);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));

	CHECK(Setup(2u, 0u));
	CHECK(HoldA(40u));
	CHECK(ActPeriods(&g_a, 1000u) == DRIVE_END);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 1000u);
	CHECK(g_a.serviceCalls == 1u);
	CHECK(ReasonOf(&g_a) == REASON_PEER_TIMEOUT);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));
}

/* The start wait with skipped periods across the 810 boundary: 805 -> 815
 * reports 811..815 only, and the wait ends at exactly 900 (the jump 895 ->
 * 905 reports 896..900). A jump 0 -> 899 reports 89, and one more ends it. */
static void TestDriveStartWaitSkipped(void)
{
	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	CHECK(ActPeriods(&g_a, 805u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 0u);
	CHECK(ActPeriods(&g_a, 10u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 5u);
	CHECK(g_a.firstStallPeriod == 815u);
	for (uint32_t jump = 1u; jump <= 8u; jump++)
	{
		CHECK(ActPeriods(&g_a, 10u) == DRIVE_HOLD);
	}
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 895u);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 85u);
	CHECK(ActPeriods(&g_a, 10u) == DRIVE_END);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 905u);
	CHECK(g_a.serviceCalls == 11u);
	CHECK(g_a.sendCalls == 3u + 11u * 3u);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker) != NULL);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker)->frameIndex == 0u);
	CHECK(ReasonOf(&g_a) == REASON_PEER_TIMEOUT);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));

	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	CHECK(ActPeriods(&g_a, 810u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 0u);
	CHECK(ActPeriods(&g_a, 89u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 89u);
	CHECK(ActPeriods(&g_a, 1u) == DRIVE_END);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 900u);
	CHECK(CheckClean(&g_a));

	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	CHECK(ActPeriods(&g_a, 899u) == DRIVE_HOLD);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == 89u);
	CHECK(ActPeriods(&g_a, 1u) == DRIVE_END);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_STALL] == STALL_TIMEOUT);
	CHECK(CheckClean(&g_a));
}

/* An onTakeResult that returns latched after an OK take ends as OUTCOME,
 * from Step and from Hold, with padsOut untouched (checked in Act). */
static void TestDriveLatchAfterOk(void)
{
	CHECK(Setup(2u, 0u));
	RunUntil(10u, 100u);
	g_a.latchOnOk = 1;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(g_a.lastTakeResult == NATIVE_LOCKSTEP_SESSION_OK && g_a.lastTakeFrame == 10u);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	CHECK(g_a.goCount == 10u);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));

	for (uint32_t newPeriod = 0; newPeriod < 2u; newPeriod++)
	{
		uint32_t heldTick;

		CHECK(Setup(2u, 0u));
		CHECK(HoldA(10u));
		heldTick = g_a.nextTick;
		CHECK(ActPeriods(&g_a, 2u) == DRIVE_HOLD);
		g_b.active = 1;
		CHECK(Act(&g_b, 1) == DRIVE_GO);
		g_a.latchOnOk = 1;
		CHECK(Act(&g_a, (int)newPeriod) == DRIVE_END);
		CHECK(g_a.lastTakeResult == NATIVE_LOCKSTEP_SESSION_OK && g_a.lastTakeFrame == heldTick);
		CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
		CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
		CHECK(CheckClean(&g_a));
		CHECK(CheckSilentAfterEnd(&g_a));
	}
}

/* A REJECTED take while RUNNING from Hold (a retry iteration and a new
 * period) is a local failure, after OnTakeResult. */
static void TestDriveRejectedFromHold(void)
{
	for (uint32_t newPeriod = 0; newPeriod < 2u; newPeriod++)
	{
		uint32_t heldTick;

		CHECK(Setup(2u, 0u));
		CHECK(HoldA(10u));
		heldTick = g_a.nextTick;
		CHECK(ActPeriods(&g_a, 1u) == DRIVE_HOLD);
		g_a.tamperTakeOnPoll = 1;
		CHECK(Act(&g_a, (int)newPeriod) == DRIVE_END);
		CHECK(NativeLockstepSession_Mode(&g_a.session) == NATIVE_LOCKSTEP_RUNNING);
		CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 1u && g_a.lastTakeFrame == heldTick);
		CHECK(g_a.lastTakeResult == NATIVE_LOCKSTEP_SESSION_REJECTED);
		CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE);
		CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_TAKE);
		CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker) == NULL);
		CHECK(CheckClean(&g_a));
		CHECK(CheckSilentAfterEnd(&g_a));
	}
}

/* A Step that ends early (after its argument checks) resets the held-period
 * count: no stale count from the previous hold survives into the end. */
static void TestDriveHeldPeriodsReset(void)
{
	CHECK(Setup(2u, 0u));
	CHECK(HoldA(20u));
	CHECK(ActPeriods(&g_a, 5u) == DRIVE_HOLD);
	g_b.active = 1;
	CHECK(Act(&g_b, 1) == DRIVE_GO);
	CHECK(Act(&g_a, 0) == DRIVE_GO);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 5u);
	g_a.session.recordedFrame = g_a.nextTick;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_RECORD);
	CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 0u);
}

/* A lead of 1 to D + 1 ticks, either side leading: parked digests, no
 * divergence, no fault. */
static void TestDriveLead(void)
{
	for (uint32_t leaderIndex = 0; leaderIndex < 2u; leaderIndex++)
	{
		for (uint32_t lead = 1u; lead <= 3u; lead++)
		{
			struct Side *leader = (leaderIndex == 0u) ? &g_a : &g_b;
			struct Side *follower = (leaderIndex == 0u) ? &g_b : &g_a;

			CHECK(Setup(2u, 0u));
			RunUntil(20u, 200u);
			follower->active = 0;
			for (uint32_t i = 0; i < lead; i++)
			{
				CHECK(Act(leader, 1) != DRIVE_END);
			}
			CHECK(leader->session.recordedFrame == follower->session.recordedFrame + lead);
			CHECK(leader->held == (lead == 3u));
			follower->active = 1;
			RunUntil(120u, 1000u);
			CHECK(g_a.nextTick == 120u && g_b.nextTick == 120u);
			CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
			CHECK(CheckSamePads(120u));
		}
	}
}

/* The worst-case lead (LR-3): the peer stalled at frame t with frame t + D
 * composed; we take up to t + D and compose t + 2D + 1 before our own take
 * stalls. That stays inside the peer window (no WINDOW_OVERRUN) at D = 2 and
 * D = 3, and the race resumes with no fault on either side. */
static void TestDriveWorstCaseLead(void)
{
	for (uint32_t delay = 2u; delay <= 3u; delay++)
	{
		CHECK(Setup(delay, 0u));
		RunUntil(30u, 300u);
		CHECK(g_a.nextTick == 30u && g_b.nextTick == 30u);
		g_a.dropOutgoing = 1;
		for (uint32_t round = 0; (round < 40u) && !(g_a.held && g_b.held); round++)
		{
			CHECK(Act(&g_a, 1) != DRIVE_END);
			CHECK(Act(&g_b, 1) != DRIVE_END);
		}
		CHECK(g_a.held && g_b.held);
		CHECK(g_b.session.consumedFrame == 30u + delay);
		CHECK(g_a.maxSentFrame == 30u + 3u * delay + 1u);
		CHECK(g_a.maxSentFrame - g_b.session.consumedFrame == 2u * delay + 1u);
		CHECK(g_a.maxSentFrame - g_b.session.consumedFrame < NATIVE_LOCKSTEP_RING_CAPACITY);
		CHECK(g_a.session.recordedFrame == g_b.session.recordedFrame + delay + 1u);
		g_a.dropOutgoing = 0;
		RunUntil(100u, 1000u);
		CHECK(g_a.nextTick == 100u && g_b.nextTick == 100u);
		CHECK(CheckHealthy(&g_a) && CheckHealthy(&g_b));
		CHECK(CheckSamePads(100u));
	}
}

/* A protocol fault drained in the step's poll: the REJECTED take is the
 * outcome (LINK_ERROR), with OnTakeResult called before the end. */
static void TestDriveFaultIsOutcome(void)
{
	uint8_t shortRecord[BUNDLE_BYTES];

	CHECK(Setup(2u, 0u));
	RunUntil(30u, 300u);
	memset(shortRecord, 0x11, sizeof(shortRecord));
	Deliver(&g_a, shortRecord, 64u);
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(g_a.acceptResults[NATIVE_LOCKSTEP_SESSION_FAULT] == 1u);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 1u);
	CHECK(g_a.lastTakeResult == NATIVE_LOCKSTEP_SESSION_REJECTED && g_a.lastTakeFrame == 30u);
	CHECK(ReasonOf(&g_a) == REASON_LINK_ERROR);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));
}

/* A forced desync on either side (frame 40's WORLD digest differs): each
 * side's REJECTED take is the outcome (DESYNC), not a local failure. */
static void TestDriveDesync(void)
{
	for (uint32_t knobOnA = 0; knobOnA < 2u; knobOnA++)
	{
		struct Side *knobbed = knobOnA ? &g_a : &g_b;

		CHECK(Setup(2u, 0u));
		knobbed->knob = 7u;
		knobbed->knobFromFrame = 40u;
		RunUntil(200u, 1000u);
		for (uint32_t i = 0; i < 2u; i++)
		{
			struct Side *s = (i == 0u) ? &g_a : &g_b;
			const struct NativeLockstepMatchOutcomeReport *report = NativeLockstepMatchOutcome_FirstOutcome(&s->tracker);

			CHECK(s->ended);
			CHECK(NativeArcadeRaceDrive_EndKind(&s->drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
			CHECK(NativeArcadeRaceDrive_FailureReason(&s->drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
			CHECK(s->takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 1u);
			CHECK(s->lastTakeResult == NATIVE_LOCKSTEP_SESSION_REJECTED);
			CHECK(s->lastTakeFrame == NativeArcadeRaceDrive_EndTick(&s->drive));
			CHECK(report != NULL && report->frameIndex == 40u);
			CHECK(ReasonOf(s) == REASON_DESYNC);
			CHECK(NativeLockstepSession_FirstDivergence(&s->session) != NULL);
			CHECK(NativeLockstepSession_FirstDivergence(&s->session)->canonicalDomainMask == (UINT32_C(1) << 4));
			CHECK(CheckClean(s));
			CHECK(CheckSilentAfterEnd(s));
		}
	}
}

/* A parked digest that mismatches at the record (LR-9, LR-11): B leads by 2
 * and its digest of frame 40 is parked on A; A's record of 40 latches the
 * divergence, and A ends as the outcome on that tick with nothing composed,
 * sent, or taken, and nothing sent afterwards (no resend, no linger). */
static void TestDriveParkedMismatchAtRecord(void)
{
	const uint32_t x = 40u;
	uint32_t sends;
	uint32_t composed;
	const struct NativeLockstepSessionParkedDigest *parked;

	CHECK(Setup(2u, 0u));
	g_b.knob = 7u;
	g_b.knobFromFrame = x;
	RunUntil(x - 2u, 500u);
	CHECK(g_a.nextTick == x - 2u && g_b.nextTick == x - 2u);
	CHECK(Act(&g_a, 1) == DRIVE_GO);   /* A: tick 38, sends up to frame 40 */
	CHECK(Act(&g_b, 1) == DRIVE_GO);   /* B: 38 */
	CHECK(Act(&g_b, 1) == DRIVE_GO);   /* B: 39 */
	CHECK(Act(&g_b, 1) == DRIVE_GO);   /* B: 40 (its state of 40 differs) */
	CHECK(Act(&g_b, 1) == DRIVE_HOLD); /* B: 41, sends frame 43 carrying 40 */
	CHECK(Act(&g_a, 1) == DRIVE_GO);   /* A: tick 39 parks B's digest of 40 */
	parked = &g_a.session.parked[g_b.slot][x % NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY];
	CHECK(parked->present != 0u && parked->frameIndex == x);
	CHECK(NativeLockstepSession_Mode(&g_a.session) == NATIVE_LOCKSTEP_RUNNING);

	sends = g_a.sendCalls;
	composed = NativeArcadeRaceDrive_ComposedCount(&g_a.drive);
	CHECK(Act(&g_a, 1) == DRIVE_END); /* A: tick 40 */
	CHECK(g_a.endCallSends == 0u);
	CHECK(g_a.sendCalls == sends);
	CHECK(NativeArcadeRaceDrive_ComposedCount(&g_a.drive) == composed);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(NativeArcadeRaceDrive_EndTick(&g_a.drive) == x);
	CHECK(NativeLockstepSession_Mode(&g_a.session) == NATIVE_LOCKSTEP_DIVERGED);
	CHECK(g_a.session.recordedFrame == x);
	CHECK(g_a.session.consumedFrame == x); /* frame 40 was not taken */
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 1u && g_a.lastTakeFrame == x);
	CHECK(ReasonOf(&g_a) == REASON_DESYNC);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker) != NULL);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker)->frameIndex == x);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));
}

/* A divergence latched by an earlier drain (the adapter's Tick) before the
 * step: the record is refused, OnTakeResult(REJECTED) at once, END as the
 * outcome, nothing sent. */
static void TestDriveLatchBeforeStep(void)
{
	uint32_t sends;

	CHECK(Setup(2u, 0u));
	g_b.knob = 7u;
	g_b.knobFromFrame = 30u;
	RunUntil(30u, 500u);
	CHECK(Act(&g_a, 1) == DRIVE_GO); /* A: tick 30 */
	CHECK(Act(&g_b, 1) == DRIVE_GO); /* B: 30 */
	CHECK(Act(&g_b, 1) == DRIVE_GO); /* B: 31, sends frame 33 carrying 30 */
	CbPoll(&g_a);                    /* the adapter's drain */
	CHECK(NativeLockstepSession_Mode(&g_a.session) == NATIVE_LOCKSTEP_DIVERGED);
	sends = g_a.sendCalls;
	CHECK(Act(&g_a, 1) == DRIVE_END); /* A: tick 31 */
	CHECK(g_a.endCallSends == 0u && g_a.sendCalls == sends);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME);
	CHECK(NativeArcadeRaceDrive_EndTick(&g_a.drive) == 31u);
	CHECK(g_a.session.recordedFrame == 30u && g_a.session.consumedFrame == 31u);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 1u && g_a.lastTakeFrame == 31u);
	CHECK(ReasonOf(&g_a) == REASON_DESYNC);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));
}

/* A REJECTED take while the session is still RUNNING is a local failure,
 * after OnTakeResult. */
static void TestDriveRejectedWhileRunning(void)
{
	CHECK(Setup(2u, 0u));
	RunUntil(10u, 100u);
	g_a.tamperTakeOnPoll = 1;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(NativeLockstepSession_Mode(&g_a.session) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(g_a.takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 1u && g_a.lastTakeFrame == 10u);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE);
	CHECK(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_TAKE);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.tracker) == NULL);
	CHECK(CheckClean(&g_a));
	CHECK(CheckSilentAfterEnd(&g_a));
}

/* A local failure on side A at its next step: the reason, nothing sent in
 * that step, and silence after. */
static int ExpectLocalFailure(enum NativeArcadeRaceDriveFailure reason, uint32_t takesExpected)
{
	REQUIRE(g_a.ended);
	REQUIRE(NativeArcadeRaceDrive_EndKind(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE);
	REQUIRE(NativeArcadeRaceDrive_FailureReason(&g_a.drive) == reason);
	REQUIRE(NativeArcadeRaceDrive_EndIsFinish(&g_a.drive) == 0);
	REQUIRE(g_a.takeCallsTotal == takesExpected);
	REQUIRE(CheckClean(&g_a));
	REQUIRE(CheckSilentAfterEnd(&g_a));
	return 1;
}

static void TestDriveLocalFailures(void)
{
	struct NativeCanonicalInputPadV1 pads[4];
	struct NativeCanonicalInputPadV1 sample;
	struct NativeArcadeRaceDriveFacts facts;
	uint32_t takes;

	Sample(0u, 5u, &sample);

	/* A failed record while RUNNING. */
	CHECK(Setup(2u, 0u));
	RunUntil(5u, 100u);
	takes = g_a.takeCallsTotal;
	g_a.session.recordedFrame = 5u;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(g_a.endCallSends == 0u);
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_RECORD, takes));

	/* A refused submit while RUNNING. */
	CHECK(Setup(2u, 0u));
	RunUntil(5u, 100u);
	takes = g_a.takeCallsTotal;
	g_a.session.localInputs[(5u + 2u) % NATIVE_LOCKSTEP_RING_CAPACITY].present = 1u;
	g_a.session.localInputs[(5u + 2u) % NATIVE_LOCKSTEP_RING_CAPACITY].frameIndex = 5u + 2u;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(g_a.endCallSends == 0u);
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_SUBMIT, takes));

	/* A refused compose while RUNNING (frame 7 needs frame 4's digest). */
	CHECK(Setup(2u, 0u));
	RunUntil(5u, 100u);
	takes = g_a.takeCallsTotal;
	g_a.session.localDigests[4u % g_a.session.historyCapacity].present = 0u;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(g_a.endCallSends == 0u);
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_COMPOSE, takes));

	/* A race tick out of order: nothing recorded. */
	CHECK(Setup(2u, 0u));
	RunUntil(5u, 100u);
	takes = g_a.takeCallsTotal;
	facts = FactsFor(6u);
	CHECK(NativeArcadeRaceDrive_Step(&g_a.drive, 6u, StateFor(6u, 0u), &sample, &facts, pads) == DRIVE_END);
	g_a.ended = 1;
	CHECK(g_a.session.recordedFrame == 4u);
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_RACE_TICK, takes));

	/* A state of another frame. */
	CHECK(Setup(2u, 0u));
	RunUntil(5u, 100u);
	takes = g_a.takeCallsTotal;
	facts = FactsFor(5u);
	CHECK(NativeArcadeRaceDrive_Step(&g_a.drive, 5u, StateFor(6u, 0u), &sample, &facts, pads) == DRIVE_END);
	g_a.ended = 1;
	CHECK(g_a.session.recordedFrame == 4u);
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_STATE_FRAME, takes));

	/* Invalid facts: no humans, five humans, more finished than humans. */
	for (uint32_t variant = 0; variant < 3u; variant++)
	{
		CHECK(Setup(2u, 0u));
		RunUntil(5u, 100u);
		takes = g_a.takeCallsTotal;
		facts = FactsFor(5u);
		facts.humans = (variant == 0u) ? 0u : ((variant == 1u) ? 5u : 2u);
		facts.finishedHumans = (variant == 2u) ? 3u : 0u;
		CHECK(NativeArcadeRaceDrive_Step(&g_a.drive, 5u, StateFor(5u, 0u), &sample, &facts, pads) == DRIVE_END);
		g_a.ended = 1;
		CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_FACTS, takes));
	}

	/* NULL state, sample, facts, pads. */
	for (uint32_t variant = 0; variant < 4u; variant++)
	{
		CHECK(Setup(2u, 0u));
		RunUntil(5u, 100u);
		takes = g_a.takeCallsTotal;
		facts = FactsFor(5u);
		CHECK(NativeArcadeRaceDrive_Step(&g_a.drive, 5u, (variant == 0u) ? NULL : StateFor(5u, 0u), (variant == 1u) ? NULL : &sample,
		                                 (variant == 2u) ? NULL : &facts, (variant == 3u) ? NULL : pads) == DRIVE_END);
		g_a.ended = 1;
		CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT, takes));
	}

	/* Step while held; Hold while not held; Hold without pads. */
	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	takes = g_a.takeCallsTotal;
	facts = FactsFor(0u);
	CHECK(NativeArcadeRaceDrive_Step(&g_a.drive, 0u, StateFor(0u, 0u), &sample, &facts, pads) == DRIVE_END);
	g_a.ended = 1;
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEQUENCE, takes));

	CHECK(Setup(2u, 0u));
	RunUntil(3u, 100u);
	takes = g_a.takeCallsTotal;
	CHECK(NativeArcadeRaceDrive_Hold(&g_a.drive, 1u, 1, pads) == DRIVE_END);
	g_a.ended = 1;
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEQUENCE, takes));

	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);
	takes = g_a.takeCallsTotal;
	CHECK(NativeArcadeRaceDrive_Hold(&g_a.drive, 1u, 1, NULL) == DRIVE_END);
	g_a.ended = 1;
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT, takes));

	/* Inconsistent hold periods (LR-44), each refused before the poll, with
	 * nothing sent or taken: periods backwards (with and without newPeriod),
	 * newPeriod with no new period, and a new period without newPeriod. */
	for (uint32_t variant = 0; variant < 4u; variant++)
	{
		static const uint32_t periodsFor[4] = {2u, 2u, 3u, 4u};
		static const int newPeriodFor[4] = {0, 1, 1, 0};
		uint32_t polls;
		uint32_t sends;

		CHECK(Setup(2u, 0u));
		RunUntil(10u, 100u);
		g_b.active = 0;
		for (uint32_t i = 0; (i < 10u) && !g_a.held; i++)
		{
			CHECK(Act(&g_a, 1) != DRIVE_END);
		}
		CHECK(g_a.held);
		CHECK(ActPeriods(&g_a, 3u) == DRIVE_HOLD);
		CHECK(NativeArcadeRaceDrive_HeldPeriods(&g_a.drive) == 3u);
		takes = g_a.takeCallsTotal;
		polls = g_a.pollCalls;
		sends = g_a.sendCalls;
		CHECK(NativeArcadeRaceDrive_Hold(&g_a.drive, periodsFor[variant], newPeriodFor[variant], pads) == DRIVE_END);
		g_a.ended = 1;
		CHECK(g_a.pollCalls == polls && g_a.sendCalls == sends);
		CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_PERIODS, takes));
	}

	/* A committed input set without a role's pad: OnTakeResult(OK) first. */
	CHECK(Setup(2u, 0u));
	RunUntil(3u, 100u);
	takes = g_a.takeCallsTotal;
	g_a.drive.cab2Slot = 5u;
	CHECK(Act(&g_a, 1) == DRIVE_END);
	CHECK(g_a.lastTakeResult == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(ExpectLocalFailure(NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_PAD, takes + 1u));
}

/* Crafted peer bundles (as the session test's CraftBundle): the mapping
 * normalizes whatever the peer sent (LR-5). */
static int CraftFromB(uint32_t frameIndex, const struct NativeCanonicalInputPadV1 *pad, const struct NativeCanonicalStateV4 *verified,
                      uint8_t bytes[BUNDLE_BYTES])
{
	struct NativeLockstepBundleV1 bundle;
	struct NativeCodecWriter writer;

	memset(&bundle, 0, sizeof(bundle));
	bundle.protocolVersion = g_a.session.protocolVersion;
	memcpy(bundle.matchIdentity, g_a.session.matchIdentity, sizeof(bundle.matchIdentity));
	bundle.frameIndex = frameIndex;
	bundle.inputDelay = g_a.session.inputDelay;
	bundle.senderSlot = g_b.slot;
	bundle.padCount = 1u;
	bundle.pads[0].slotIndex = g_b.slot;
	bundle.pads[0].pad = *pad;
	bundle.pads[1].slotIndex = NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT;
	if (verified != NULL)
	{
		bundle.verifiedPresent = 1u;
		bundle.verifiedFrameIndex = verified->frameNumber;
		memcpy(bundle.verifiedDomainDigests, verified->domainDigests, sizeof(bundle.verifiedDomainDigests));
		bundle.verifiedCombinedDigest = verified->combinedDigest;
	}
	NativeCodecWriter_Init(&writer, bytes, BUNDLE_BYTES, NULL);
	REQUIRE(NativeLockstepBundleV1_Encode(&writer, &bundle) == 1);
	REQUIRE(NativeCodecWriter_Size(&writer) == BUNDLE_BYTES);
	return 1;
}

static void TestDrivePadMapping(void)
{
	struct NativeCanonicalInputPadV1 raw[4];
	struct NativeCanonicalInputPadV1 pads[4];
	struct NativeCanonicalInputPadV1 expected;
	uint8_t bytes[BUNDLE_BYTES];

	CHECK(Setup(2u, 0u));
	g_b.active = 0;
	CHECK(Act(&g_a, 1) == DRIVE_HOLD);

	memset(raw, 0, sizeof(raw));
	/* Frame 1: connected 2, odd status, id 0xff, CROSS and START pressed. */
	raw[1].connected = 2u;
	raw[1].status = 0x5au;
	raw[1].id = 0xffu;
	raw[1].buttons[0] = 0xf7u;
	raw[1].buttons[1] = 0xbfu;
	raw[1].analog[0] = 0x01u;
	raw[1].analog[1] = 0x02u;
	raw[1].analog[2] = 0x03u;
	raw[1].analog[3] = 0x04u;
	/* Frame 2: disconnected, with buttons and axes. */
	raw[2].connected = 0u;
	raw[2].id = 0x73u;
	raw[2].analog[0] = 0xffu;
	/* Frame 3: analog id, status 0xff, START pressed. */
	raw[3].connected = 1u;
	raw[3].status = 0xffu;
	raw[3].id = 0x73u;
	raw[3].buttons[0] = 0x00u;
	raw[3].buttons[1] = 0xffu;
	raw[3].analog[0] = 0x80u;
	raw[3].analog[1] = 0x81u;
	raw[3].analog[2] = 0x82u;
	raw[3].analog[3] = 0x83u;
	for (uint32_t frame = 0; frame < 4u; frame++)
	{
		/* Frame 3 carries frame 0's digest, the same as A's. */
		CHECK(CraftFromB(frame, &raw[frame], (frame == 3u) ? StateFor(0u, 0u) : NULL, bytes));
		CHECK(NativeLockstepSession_AcceptBundle(&g_a.session, bytes, BUNDLE_BYTES) == NATIVE_LOCKSTEP_SESSION_OK);
	}

	for (uint32_t frame = 0; frame < 4u; frame++)
	{
		struct NativeCanonicalInputPadV1 sample;
		const struct NativeArcadeRaceDriveFacts facts = FactsFor(frame);

		memset(pads, 0xa5, sizeof(pads));
		if (frame == 0u)
		{
			/* A retry iteration takes too. */
			CHECK(NativeArcadeRaceDrive_Hold(&g_a.drive, 0u, 0, pads) == DRIVE_GO);
		}
		else
		{
			Sample(g_a.slot, frame, &sample);
			CHECK(NativeArcadeRaceDrive_Step(&g_a.drive, frame, StateFor(frame, 0u), &sample, &facts, pads) == DRIVE_GO);
		}
		Expected(g_a.slot, frame, 2u, &expected);
		CHECK(memcmp(&pads[0], &expected, sizeof(expected)) == 0);
		CHECK(IsDisconnected(&pads[2]) && IsDisconnected(&pads[3]));
		NativeArcadeRaceDrive_NormalizePad(&raw[frame], &expected);
		CHECK(memcmp(&pads[1], &expected, sizeof(expected)) == 0);
		CHECK(IsNormalized(&pads[1]));
	}
	/* Spelled out: frame 0 all-zero and frame 2 disconnected are neutral;
	 * frame 1 keeps CROSS and the axes, id 0x41; frame 3 keeps id 0x73. */
	CHECK(pads[1].id == 0x73u && pads[1].status == 0u && pads[1].buttons[0] == 0x08u && pads[1].analog[3] == 0x83u);
	CHECK(g_a.orderViolations == 0u && g_a.identityViolations == 0u);
}

/* One race over two sides to its finish-kind end, with the LR-13/LR-18
 * checks: both sides end on the same tick and kind; the end tick is
 * recorded, nothing is composed, sent, or taken on it; then 15 linger ticks
 * resend the end tick's window, and nothing after. */
static int RunRace(uint32_t limit, uint32_t humans, const uint32_t finishTicks[4], uint32_t endOfRaceTick, enum NativeArcadeRaceDriveEndKind kind,
                   uint32_t endTick, uint32_t graceStart)
{
	REQUIRE(Setup(2u, limit));
	g_race.humans = humans;
	for (uint32_t i = 0; i < 4u; i++)
	{
		g_race.finishTick[i] = finishTicks[i];
	}
	g_race.endOfRaceTick = endOfRaceTick;
	RunUntil(UINT32_MAX, endTick + 100u);
	for (uint32_t i = 0; i < 2u; i++)
	{
		struct Side *s = (i == 0u) ? &g_a : &g_b;

		REQUIRE(s->ended);
		REQUIRE(NativeArcadeRaceDrive_EndKind(&s->drive) == kind);
		REQUIRE(NativeArcadeRaceDrive_EndIsFinish(&s->drive) != 0);
		REQUIRE(NativeArcadeRaceDrive_FailureReason(&s->drive) == NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
		REQUIRE(NativeArcadeRaceDrive_EndTick(&s->drive) == endTick);
		REQUIRE(NativeArcadeRaceDrive_RaceTick(&s->drive) == endTick);
		REQUIRE(NativeArcadeRaceDrive_GraceStartTick(&s->drive) == graceStart);
		REQUIRE(s->endCallSends == 0u);
		REQUIRE(s->session.recordedAny != 0u && s->session.recordedFrame == endTick);
		REQUIRE(s->session.consumedFrame == endTick);
		REQUIRE(s->goCount == endTick);
		REQUIRE(s->lastTakeFrame == endTick - 1u);
		REQUIRE(s->takeCalls[NATIVE_LOCKSTEP_SESSION_REJECTED] == 0u);
		REQUIRE(NativeArcadeRaceDrive_ComposedCount(&s->drive) == 2u + endTick);
		REQUIRE(NativeArcadeRaceDrive_LingerTicksLeft(&s->drive) == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS);
		REQUIRE(NativeLockstepMatchOutcome_FirstOutcome(&s->tracker) == NULL);
		REQUIRE(CheckClean(s));
	}
	REQUIRE(CheckSamePads(endTick));

	/* The linger: frames F - 3..F + 1, verbatim, for 15 host ticks (from
	 * frame 0 when F < 3, an end at or below D + 1). */
	for (uint32_t tick = 1u; tick <= NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS; tick++)
	{
		const uint32_t low = (endTick >= 3u) ? endTick - 3u : 0u;

		g_a.callSendCount = 0u;
		REQUIRE(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == endTick + 2u - low);
		CheckCallSends(&g_a, low, endTick + 1u);
		REQUIRE(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS - tick);
	}
	{
		const uint32_t sends = g_a.sendCalls;

		for (uint32_t tick = 0; tick < 5u; tick++)
		{
			REQUIRE(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 0u);
		}
		REQUIRE(g_a.sendCalls == sends);
	}
	REQUIRE(CheckClean(&g_a));
	REQUIRE(CheckSilentAfterEnd(&g_a));
	return 1;
}

/* The finish grace and the race-length bound (LR-12, LR-13, LR-18). */
static void TestDriveFinish(void)
{
	const uint32_t none[4] = {NO_TICK, NO_TICK, NO_TICK, NO_TICK};
	const uint32_t first100[4] = {100u, NO_TICK, NO_TICK, NO_TICK};
	const uint32_t two[4] = {100u, 200u, NO_TICK, NO_TICK};
	const uint32_t three[4] = {100u, 200u, 300u, NO_TICK};
	const uint32_t second100[4] = {NO_TICK, 100u, NO_TICK, NO_TICK};

	/* 2 humans: the grace starts on the first finish (either human) and ends
	 * at exactly G + 900 as the finish grace. */
	CHECK(RunRace(0u, 2u, first100, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, 1000u, 100u));
	CHECK(RunRace(0u, 2u, second100, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, 1000u, 100u));
	/* 3 humans: all but one (the second finish), not the first. */
	CHECK(RunRace(0u, 3u, two, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, 1100u, 200u));
	/* 4 humans: all but one (the third finish). */
	CHECK(RunRace(0u, 4u, three, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, 1200u, 300u));
	/* 1 human: max(1, 0) = 1, the first finish. */
	CHECK(RunRace(0u, 1u, first100, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, 1000u, 100u));
	/* END_OF_RACE before the grace end is the natural finish. */
	CHECK(RunRace(0u, 2u, first100, 500u, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, 500u, 100u));
	/* END_OF_RACE on the grace-end tick is the natural finish. */
	CHECK(RunRace(0u, 2u, first100, 1000u, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, 1000u, 100u));
	/* END_OF_RACE on the grace start tick G itself: the natural finish, and
	 * G is still latched for the logs. */
	CHECK(RunRace(0u, 2u, first100, 100u, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, 100u, 100u));
	/* END_OF_RACE with no grace. */
	CHECK(RunRace(0u, 2u, none, 77u, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, 77u, NO_TICK));
	/* The tie order with the bound lowered to the grace-end tick: the grace
	 * wins over the bound, END_OF_RACE over both. */
	CHECK(RunRace(1000u, 2u, first100, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, 1000u, 100u));
	CHECK(RunRace(1000u, 2u, first100, 1000u, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, 1000u, 100u));
	/* The lowered bound alone; and END_OF_RACE on the bound's tick. */
	CHECK(RunRace(50u, 2u, none, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT, 50u, NO_TICK));
	CHECK(RunRace(50u, 2u, none, 50u, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, 50u, NO_TICK));
	/* A bound at or below D (2 here, LR-60's lowest caps): it still ends
	 * cleanly as RACE_TICK_LIMIT on both sides on that tick. */
	CHECK(RunRace(1u, 2u, none, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT, 1u, NO_TICK));
	CHECK(RunRace(2u, 2u, none, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT, 2u, NO_TICK));
	/* A grace that would end after the bound: the bound ends it. */
	CHECK(RunRace(600u, 2u, first100, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT, 600u, 100u));
	/* No human finishing runs to the race-length bound: tick 18000 ends. */
	CHECK(RunRace(0u, 2u, none, NO_TICK, NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT, 18000u, NO_TICK));
	CHECK(g_a.committed[17999][0].connected == 1u);
}

/* The linger's stop rules (LR-13, LR-14, LR-46): leaving RESULTS after it
 * was seen, or a session that left RUNNING, stops it for good; off RESULTS
 * before RESULTS was seen waits; a refused send is ignored. And an end on
 * race tick 0 has nothing kept to send. */
static void TestDriveLingerStops(void)
{
	uint8_t shortRecord[BUNDLE_BYTES];
	uint32_t sends;

	/* Off RESULTS before RESULTS is seen (the flow reaches RESULTS on the
	 * host tick after F): no send, no count, no stop; then RESULTS sends. */
	CHECK(Setup(2u, 0u));
	g_race.endOfRaceTick = 50u;
	RunUntil(UINT32_MAX, 200u);
	CHECK(g_a.ended && g_b.ended);
	sends = g_a.sendCalls;
	for (uint32_t tick = 0; tick < 3u; tick++)
	{
		CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 0) == 0u);
		CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS);
	}
	CHECK(g_a.sendCalls == sends);
	g_a.callSendCount = 0u;
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 5u);
	CheckCallSends(&g_a, 47u, 51u);
	CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS - 1u);
	/* Then off RESULTS after it was seen: stopped for good. */
	sends = g_a.sendCalls;
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 0) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 0u);
	CHECK(g_a.sendCalls == sends);
	CHECK(CheckClean(&g_a));

	/* Off RESULTS after two linger ticks: 1, 1, 0 stops; 1 sends nothing. */
	CHECK(Setup(2u, 0u));
	g_race.endOfRaceTick = 50u;
	RunUntil(UINT32_MAX, 200u);
	CHECK(g_a.ended && g_b.ended);
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 5u);
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 5u);
	sends = g_a.sendCalls;
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 0) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == 0u);
	for (uint32_t tick = 0; tick < 3u; tick++)
	{
		CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 0u);
	}
	CHECK(g_a.sendCalls == sends);

	/* The session leaves RUNNING (a fault drained after the end): stopped
	 * for good, even before RESULTS was seen. */
	memset(shortRecord, 0x22, sizeof(shortRecord));
	CHECK(NativeLockstepSession_AcceptBundle(&g_b.session, shortRecord, 64u) == NATIVE_LOCKSTEP_SESSION_FAULT);
	sends = g_b.sendCalls;
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_b.drive, 0) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_b.drive) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_b.drive, 1) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_b.drive) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_b.drive, 1) == 0u);
	CHECK(g_b.sendCalls == sends);

	/* Refused sends (a transient socket error refuses too) are ignored: the
	 * whole window is still offered, the tick counts down, and the next
	 * tick sends again; the linger ends only when the count reaches 0. */
	CHECK(Setup(2u, 0u));
	g_race.endOfRaceTick = 50u;
	RunUntil(UINT32_MAX, 200u);
	g_a.refuseSends = 1;
	for (uint32_t tick = 1u; tick <= 3u; tick++)
	{
		g_a.callSendCount = 0u;
		CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 0u);
		CheckCallSends(&g_a, 47u, 51u);
		CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS - tick);
	}
	g_a.refuseSends = 0;
	for (uint32_t tick = 4u; tick <= NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS; tick++)
	{
		g_a.callSendCount = 0u;
		CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 5u);
		CheckCallSends(&g_a, 47u, 51u);
	}
	CHECK(NativeArcadeRaceDrive_LingerTicksLeft(&g_a.drive) == 0u);
	sends = g_a.sendCalls;
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 0u);
	CHECK(g_a.sendCalls == sends);
	CHECK(CheckClean(&g_a));

	/* An end on race tick 0: nothing composed, nothing to linger with. */
	CHECK(Setup(2u, 0u));
	g_race.endOfRaceTick = 0u;
	RunUntil(UINT32_MAX, 10u);
	CHECK(g_a.ended && g_b.ended);
	CHECK(NativeArcadeRaceDrive_EndTick(&g_a.drive) == 0u);
	CHECK(g_a.sendCalls == 0u && NativeArcadeRaceDrive_ComposedCount(&g_a.drive) == 0u);
	CHECK(NativeArcadeRaceDrive_LingerTick(&g_a.drive, 1) == 0u);
	CHECK(g_a.sendCalls == 0u);
}

int main(void)
{
	if (!LoadRl10Disconnected())
	{
		fprintf(stderr, "the RL-10 disconnected pads 2 and 3 differ or are connected\n");
		s_failures++;
	}
	TestConstants();
	TestNeutralPad();
	TestEveryStatusAndIdByte();
	TestButtons();
	TestAnalogBytes();
	TestStatusDoesNotDisconnect();
	TestInPlace();
	TestZeroPad();
	TestNullArguments();

	TestDriveConstants();
	TestDriveBegin();
	TestDriveEqualInputs();
	TestDriveSendOrderAtStart();
	TestDriveHoldIterations();
	TestDriveStallAndResume();
	TestDriveStallTimeout();
	TestDriveStartWait();
	TestDriveSkippedPeriods();
	TestDriveStallTimeoutSkipped();
	TestDriveStartWaitSkipped();
	TestDriveLatchAfterOk();
	TestDriveRejectedFromHold();
	TestDriveHeldPeriodsReset();
	TestDriveLead();
	TestDriveWorstCaseLead();
	TestDriveFaultIsOutcome();
	TestDriveDesync();
	TestDriveParkedMismatchAtRecord();
	TestDriveLatchBeforeStep();
	TestDriveRejectedWhileRunning();
	TestDriveLocalFailures();
	TestDrivePadMapping();
	TestDriveFinish();
	TestDriveLingerStops();

	if (s_failures != 0)
	{
		fprintf(stderr, "native_arcade_race_drive_unit: %d failure(s)\n", s_failures);
		return 1;
	}
	printf("native_arcade_race_drive_unit: ok\n");
	return 0;
}
