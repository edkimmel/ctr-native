#ifndef PLATFORM_NATIVE_ARCADE_RACE_DRIVE_H
#define PLATFORM_NATIVE_ARCADE_RACE_DRIVE_H

/*
 * Linked-race drive core (docs/LOCKSTEP_RACE_MILESTONE.md LR-1). A pure core
 * over caller-owned values: no socket, clock, SDL, heap, stdio, or game
 * dependency. Slice LR-S7 added the pad normalization (LR-4, LR-5, LR-40);
 * slice LR-S8 grows it into the per-tick drive core (LR-2, LR-3, LR-5, LR-9,
 * LR-12, LR-13, LR-18; the API is recorded in LR-41..LR-48).
 *
 * The drive runs over a caller-owned lockstep session (the peer link's
 * session in LR-S9) and a caller-owned kept-bundle ring, and does its I/O
 * only through caller-supplied callbacks: the verbatim bundle send, the link
 * poll, the adapter's OnTakeResult, and the hold's once-per-period service.
 * It holds no pointer to anything else and reads no clock: the hold's wall
 * time arrives as the caller's newPeriod flag.
 */

#include "platform/native_canonical_state.h"
#include "platform/native_lockstep_session.h"

#include <stddef.h>
#include <stdint.h>

/* The only pad ids a normalized pad carries. */
#define NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL      0x41u
#define NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG       0x73u

/* START in the 16-bit PSX button word, buttons[0] | (buttons[1] << 8). The
 * word is active-low: a set bit is a released button. */
#define NATIVE_ARCADE_RACE_DRIVE_START_MASK          0x0008u

/* The neutral connected pad: status 0, digital id, nothing pressed, every
 * analog axis centred. */
#define NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS     0xffffu
#define NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG      0x80u

/* The disconnected pad of retail pads 2 and 3 (LR-5), byte for byte the
 * RL-10 rehearsal's TWO_CAB install (NativeArcadeRosterProof_ScriptedPads):
 * status 0xff, id 0xff, buttons 0xff 0xff, analog 0x80 x4, connected 0. */
#define NATIVE_ARCADE_RACE_DRIVE_DISCONNECTED_STATUS 0xffu
#define NATIVE_ARCADE_RACE_DRIVE_DISCONNECTED_ID     0xffu

/* The four retail pads a GO maps: CAB1_HUMAN, CAB2_HUMAN, and two
 * disconnected pads. */
#define NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT           4u

/* LR-3: the drive refuses a session whose D is above 3 (the 2D + 1 lead must
 * stay below the peer window of 8). */
#define NATIVE_ARCADE_RACE_DRIVE_MAX_INPUT_DELAY     3u
/* LR-9, LR-12: the first 810 held periods of race tick 0 do not count toward
 * the stall timeout, so the whole start wait is 810 + 90 periods. */
#define NATIVE_ARCADE_RACE_DRIVE_START_GRACE_PERIODS 810u
/* LR-9: the banner is due from this many held periods on. */
#define NATIVE_ARCADE_RACE_DRIVE_HOLD_GRACE_PERIODS  10u
/* LR-18: the finish grace ends on race tick G + 900. */
#define NATIVE_ARCADE_RACE_DRIVE_FINISH_GRACE_TICKS  900u
/* LR-13: host ticks of resending after a finish-kind end. */
#define NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS 15u
/* LR-12, LR-13: the race-length bound. Race tick 18000 itself ends (LR-42). */
#define NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT     18000u
/* The kept-bundle ring, indexed frame % capacity (LR-3). */
#define NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY       NATIVE_LOCKSTEP_RING_CAPACITY
/* A race tick or frame that does not exist (yet). */
#define NATIVE_ARCADE_RACE_DRIVE_NO_TICK             UINT32_MAX

/* What a Step or Hold tells the caller. */
enum NativeArcadeRaceDriveStatus
{
	NATIVE_ARCADE_RACE_DRIVE_GO = 1,   /* padsOut holds the committed pads of the race tick; install them */
	NATIVE_ARCADE_RACE_DRIVE_HOLD = 2, /* the take stalled; call Hold until GO or END */
	NATIVE_ARCADE_RACE_DRIVE_END = 3   /* the drive has ended; see the end kind */
};

/* How the drive ended (LR-12, LR-13, LR-18). Append-only. */
enum NativeArcadeRaceDriveEndKind
{
	NATIVE_ARCADE_RACE_DRIVE_END_NONE = 0,
	NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE = 1,         /* END_OF_RACE seen: the natural finish */
	NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE = 2,    /* race tick G + 900 */
	NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT = 3, /* the race-length bound */
	NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME = 4,         /* OnTakeResult latched (or will latch) the adapter's outcome */
	NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE = 5    /* a local drive failure (LR-12); see the failure reason */
};

/* Why a LOCAL_FAILURE end happened. Append-only. */
enum NativeArcadeRaceDriveFailure
{
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE = 0,
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT = 1,        /* a NULL argument or callback */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_MODE = 2,    /* Begin: the session is not RUNNING */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_STARTED = 3, /* Begin: the session has recorded or consumed a frame */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_INPUT_DELAY = 4,     /* Begin: D above 3 (LR-3) */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_SLOT = 5,       /* Begin: no CAB1_HUMAN or CAB2_HUMAN slot */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_TICK_LIMIT = 6,      /* Begin: a race tick limit above 18000 */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_NOT_BEGUN = 7,       /* Step or Hold before a successful Begin */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEQUENCE = 8,        /* Step while held, or Hold while not held */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_RACE_TICK = 9,       /* raceTick is not the next race tick */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_STATE_FRAME = 10,    /* state->frameNumber is not raceTick */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_FACTS = 11,          /* humans outside 1..4, or finished above humans */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_RECORD = 12,         /* RecordLocalDigests failed while RUNNING */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_SUBMIT = 13,         /* SubmitLocalInput failed while RUNNING */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_COMPOSE = 14,        /* ComposeBundle failed while RUNNING */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEND_ORDER = 15,     /* a compose the LR-29 send order forbids (unreachable) */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_TAKE = 16,           /* a non-OK, non-STALL take while RUNNING */
	NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_PAD = 17        /* the committed inputs lack a role's pad */
};

/*
 * One kept bundle: the encoded bytes of frame frameIndex, composed exactly
 * once; every later send of that frame is a byte-identical copy.
 */
struct NativeArcadeRaceDriveKeptBundle
{
	uint32_t frameIndex;
	uint8_t present;
	uint8_t bytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
};

/* The caller-owned kept-bundle ring (LR-3), indexed frameIndex % capacity.
 * Begin clears it. */
struct NativeArcadeRaceDriveKept
{
	struct NativeArcadeRaceDriveKeptBundle entries[NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY];
};

/*
 * The drive's I/O (LR-41). context is passed back verbatim.
 *
 * sendBundle: sends size (always NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
 *   bytes verbatim, the bundle for frameIndex; returns 1 if sent, 0 if
 *   refused (the link or the session is not RUNNING). Required.
 * poll: drains the link into the session (AcceptBundle). Required.
 * onTakeResult: the adapter's OnTakeResult for a take of frameIndex; returns
 *   nonzero once the adapter's outcome is latched. Required.
 * servicePeriod: the hold's once-per-period host work (the adapter's launch
 *   intake and launch linger, LR-9). Optional (NULL: nothing).
 */
struct NativeArcadeRaceDriveCallbacks
{
	void *context;
	int (*sendBundle)(void *context, uint32_t frameIndex, const uint8_t *bytes, size_t size);
	void (*poll)(void *context);
	int (*onTakeResult)(void *context, enum NativeLockstepSessionResult result, uint32_t frameIndex);
	void (*servicePeriod)(void *context);
};

/* The game facts of one race tick, read by the game-side caller (LR-1,
 * LR-18). Pointer-free. */
struct NativeArcadeRaceDriveFacts
{
	uint32_t endOfRace;      /* nonzero: the hook sees END_OF_RACE */
	uint32_t finishedHumans; /* humans (slots 0..humans-1) with ACTION_RACE_FINISHED */
	uint32_t humans;         /* numPlyrCurrGame, 1..4 */
};

/* The drive's state. Caller-owned, host-local (LR-15): never in a
 * checkpoint, in replay, or in canonical state. Read it through the
 * accessors. */
struct NativeArcadeRaceDrive
{
	struct NativeLockstepSession *session;
	struct NativeArcadeRaceDriveKept *kept;
	struct NativeArcadeRaceDriveCallbacks callbacks;
	uint32_t phase;
	uint32_t inputDelay;
	uint32_t raceTickLimit;
	uint32_t nextTick;
	uint32_t raceTick;
	uint32_t heldPeriods;
	uint32_t graceStartTick;
	uint32_t endTick;
	uint32_t endKind;
	uint32_t failure;
	uint32_t lingerTicksLeft;
	uint32_t composedCount;
	uint8_t cab1Slot;
	uint8_t cab2Slot;
	uint8_t reserved[2];
};

/* Writes the neutral connected pad (connected 1, status 0, id 0x41, buttons
 * 0xff 0xff, analog 0x80 x4) to *out. NULL out: no-op. */
void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out);

/*
 * Normalizes one pad (LR-4, LR-40). A pad whose connected byte is 0 becomes
 * the neutral connected pad. Any other connected byte counts as connected;
 * such a pad keeps its button bits and analog bytes, except that START is
 * released, and gets connected 1, status 0 (its own status byte is
 * overwritten, never used to decide disconnection), and id 0x73 if its id
 * is 0x73, else 0x41. in and out may be the same pad. NULL in or out: no-op,
 * out untouched.
 */
void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out);

/* Writes the disconnected pad of retail pads 2 and 3 (LR-5, LR-43). NULL
 * out: no-op. */
void NativeArcadeRaceDrive_DisconnectedPad(struct NativeCanonicalInputPadV1 *out);

/* Zeroes the drive: not begun, end kind NONE. NULL: no-op. */
void NativeArcadeRaceDrive_Init(struct NativeArcadeRaceDrive *drive);

/*
 * Begins a race's drive over session (RUNNING, nothing recorded or
 * consumed), kept (cleared here), and callbacks (sendBundle, poll, and
 * onTakeResult required). D is the session's inputDelay; the CAB1_HUMAN and
 * CAB2_HUMAN slots come from the session's config
 * (NativeMatchConfigV1_FindRoleSlot). raceTickLimit 0 is the default 18000; a
 * nonzero value is the internal override (LR-S10) and may only lower it
 * (1..18000). Always reinitializes the whole drive first. Returns 1 when the
 * drive runs; 0 on a refusal, which ends the drive as LOCAL_FAILURE with the
 * reason (D above 3 is FAILURE_INPUT_DELAY) and sends nothing. NULL drive:
 * returns 0.
 */
int NativeArcadeRaceDrive_Begin(struct NativeArcadeRaceDrive *drive, struct NativeLockstepSession *session, struct NativeArcadeRaceDriveKept *kept,
                                const struct NativeArcadeRaceDriveCallbacks *callbacks, uint32_t raceTickLimit);

/*
 * One race tick k (LR-2, LR-9, LR-13, LR-18). raceTick must be the next race
 * tick (0 first, then one more after each GO) and state->frameNumber must
 * equal it; localSample is the raw local sample (normalized here). In order:
 *
 *   1. RecordLocalDigests(state). If the session is not RUNNING afterwards (a
 *      parked digest diverged inside the record, or an earlier drain latched
 *      it), onTakeResult(REJECTED, k) at once and END as OUTCOME. A failed
 *      record while RUNNING is a local failure.
 *   2. The end checks, first match wins: facts->endOfRace (END_OF_RACE); the
 *      finish grace, which starts on the first tick G with finishedHumans >=
 *      max(1, humans - 1) and ends on tick G + 900 (FINISH_GRACE); raceTick
 *      >= the race tick limit (RACE_TICK_LIMIT). A finish-kind END composes,
 *      sends, and takes nothing, and arms the 15-tick linger.
 *   3. Submits the normalized sample for frame k (consumed at k + D).
 *   4. Composes into the kept ring and sends: on tick 0 frames 0..D, else
 *      frame k + D. No bundle for frame f before frame f - D is recorded,
 *      and none before the first record (LR-29).
 *   5. Resends, verbatim from the ring, frames max(0, k - D - 1)..k + D - 1
 *      not sent in step 4.
 *   6. poll.
 *   7. Takes frame k. OK: onTakeResult(OK, k), then GO with padsOut mapped
 *      (LR-5). STALL: HOLD (no onTakeResult: stalls count per full period in
 *      Hold). Anything else: onTakeResult(result, k) first, then END as
 *      OUTCOME if it latched or the session is not RUNNING, else END as a
 *      local failure. A latched onTakeResult always ends as OUTCOME.
 *
 * padsOut is written only on GO: [0] the CAB1_HUMAN pad, [1] the CAB2_HUMAN
 * pad, each normalized again, [2] and [3] disconnected. NULL drive: END.
 * After END every call returns END and does nothing.
 */
enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Step(struct NativeArcadeRaceDrive *drive, uint32_t raceTick, const struct NativeCanonicalStateV4 *state,
                                                            const struct NativeCanonicalInputPadV1 *localSample, const struct NativeArcadeRaceDriveFacts *facts,
                                                            struct NativeCanonicalInputPadV1 padsOut[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT]);

/*
 * One hold-loop iteration while held on race tick k (LR-9), about 1 ms
 * apart. newPeriod is nonzero on the first iteration of each full tick period
 * held (MainArcadeRaceHoldStepFn's flag). Every call polls. On newPeriod
 * only: counts the period, resends the ring window max(0, k - D - 1)..k + D
 * once, calls servicePeriod, takes, and on a stall calls
 * onTakeResult(STALL, k), except in the start grace (race tick 0 and held
 * periods <= 810). Other iterations only retry the take. A non-STALL take is
 * classified as in Step. Returns GO (padsOut mapped), HOLD, or END.
 */
enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Hold(struct NativeArcadeRaceDrive *drive, int newPeriod,
                                                            struct NativeCanonicalInputPadV1 padsOut[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT]);

/*
 * The finish linger (LR-13), once per host tick after a finish-kind END.
 * While linger ticks remain, onResults is nonzero, and the session is
 * RUNNING, it resends the kept bundles of the end tick's window, frames
 * max(0, F - D - 1)..F + D - 1, once, and counts one tick down. It stops for
 * good when onResults is 0, the session is not RUNNING, a send is refused,
 * or the count reaches 0. Never sends after an OUTCOME or LOCAL_FAILURE end.
 * Returns the bundles sent by this call.
 */
uint32_t NativeArcadeRaceDrive_LingerTick(struct NativeArcadeRaceDrive *drive, int onResults);

/* LR-9's hold grace: nonzero when periods >= HOLD_GRACE_PERIODS. */
int NativeArcadeRaceDrive_BannerDue(uint32_t periods);

/* Accessors. Each returns 0, NONE, or NO_TICK for a NULL drive. */
enum NativeArcadeRaceDriveEndKind NativeArcadeRaceDrive_EndKind(const struct NativeArcadeRaceDrive *drive);
enum NativeArcadeRaceDriveFailure NativeArcadeRaceDrive_FailureReason(const struct NativeArcadeRaceDrive *drive);
/* Nonzero for END_OF_RACE, FINISH_GRACE, and RACE_TICK_LIMIT: the caller
 * reports the race finished. */
int NativeArcadeRaceDrive_EndIsFinish(const struct NativeArcadeRaceDrive *drive);
/* The race tick of the last Step that passed its argument checks (the tick
 * held on, or ended on); NO_TICK before that. */
uint32_t NativeArcadeRaceDrive_RaceTick(const struct NativeArcadeRaceDrive *drive);
/* The race tick the drive ended on; NO_TICK while it runs. */
uint32_t NativeArcadeRaceDrive_EndTick(const struct NativeArcadeRaceDrive *drive);
/* The finish grace's start tick G; NO_TICK before it starts. */
uint32_t NativeArcadeRaceDrive_GraceStartTick(const struct NativeArcadeRaceDrive *drive);
/* Full periods counted in the current hold; every Step resets it to 0. */
uint32_t NativeArcadeRaceDrive_HeldPeriods(const struct NativeArcadeRaceDrive *drive);
uint32_t NativeArcadeRaceDrive_LingerTicksLeft(const struct NativeArcadeRaceDrive *drive);
/* The race tick limit in force (18000 unless lowered). */
uint32_t NativeArcadeRaceDrive_RaceTickLimit(const struct NativeArcadeRaceDrive *drive);
/* Bundles composed into the kept ring so far (each frame exactly once). */
uint32_t NativeArcadeRaceDrive_ComposedCount(const struct NativeArcadeRaceDrive *drive);

/* Fixed log names: "none", "end of race", "finish grace", "race tick
 * limit", "outcome", "local failure"; "unknown" otherwise. */
const char *NativeArcadeRaceDrive_EndKindName(enum NativeArcadeRaceDriveEndKind kind);
/* Fixed log names of the failure reasons; "unknown" otherwise. */
const char *NativeArcadeRaceDrive_FailureName(enum NativeArcadeRaceDriveFailure failure);

#endif
