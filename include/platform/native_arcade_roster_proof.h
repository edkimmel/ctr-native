#ifndef PLATFORM_NATIVE_ARCADE_ROSTER_PROOF_H
#define PLATFORM_NATIVE_ARCADE_ROSTER_PROOF_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

struct NativeCanonicalStateV1;

/*
 * Live roster proof, internal builds only (docs/ROSTER_MILESTONE.md section
 * 3.4; R-5b's launcher, R-6's scripted pads, per-tick digests, and the
 * arcade_roster_determinism ctest, tools/arcade-roster-proof-check.ps1;
 * R-6b's race-relative control digest and race tick 0 counters; R-6c's pin
 * readback; OC-3's single-cabinet profile, RS-23 and RS-24 below).
 * Evidence plumbing: host-local options, the fixed proof config, the scripted
 * pad pattern, a game-facing singleton that keeps the per-tick digest lines,
 * and the report writer.
 *
 *   --arcade-roster-proof <log path>        enable the proof; the report is
 *                                           written to this path
 *   --arcade-roster-proof-seed <u64>        decimal, or 0x/0X hex; default 1
 *   --arcade-roster-proof-dwell <ticks>     decimal 0..7200; default 0
 *   --arcade-roster-proof-ticks <N>         decimal 1..3600 (1..6000 with the
 *                                           autopilot); default 900: the
 *                                           race ticks logged before PASS
 *   --arcade-roster-proof-profile <name>    two-cab or one-cab (exactly, in
 *                                           lowercase); default two-cab: the
 *                                           race profile (RS-23)
 *   --arcade-roster-proof-hold              no value: hold the race for
 *                                           HOLD_PERIODS tick periods of wall
 *                                           time at race tick HOLD_TICK
 *                                           (docs/LOCKSTEP_RACE_MILESTONE.md
 *                                           LR-S2 (a)); needs a tick count
 *                                           above HOLD_TICK
 *   --arcade-roster-proof-autopilot         no value: players 0 and 1 run on
 *                                           the steering autopilot instead of
 *                                           the scripted pattern
 *                                           (docs/LOCKSTEP_RACE_MILESTONE.md
 *                                           LR-S2 (b)); two-cab only; allows
 *                                           a tick count up to
 *                                           AUTOPILOT_MAX_TICKS (6000)
 *
 * The report path is opened as given when the report is written: a relative
 * path resolves against the base directory, because main.c changes into
 * NativeAssets_GetBaseDir() before the game starts, not against the shell's
 * working directory. Pass an absolute path to write elsewhere.
 *
 * Parsing is transactional: on any error the caller's options are left
 * untouched. Arguments that are not one of these seven options are ignored,
 * because other host parsers own them. An option whose value is missing (end
 * of argv, a NULL entry, or a next argument starting with '-'), repeated, or
 * malformed is an error, and so is a seed, dwell, tick count, profile, hold,
 * or autopilot without --arcade-roster-proof, a hold with a tick count of
 * HOLD_TICK or less, a tick count above MAX_TICKS without the autopilot, and
 * the autopilot with the one-cab profile. main.c rejects the proof together
 * with any arcade-link or replay option, and with --exit-after-frame (any
 * frame-capture exit option; NativeArcadeRosterProof_NamesExitOption), which
 * would end the run on a frame count instead of the proof result.
 *
 * Dwell and launch window. The game hook waits for the title's menu-ready
 * frame, then the dwell (in proof ticks), then launches from the first
 * launch window it sees: the title menu-ready window, or the attract demo
 * race running (not loading, the demo flag set, off the main-menu level; the
 * title starts it after TITLE_DEMO_IDLE_FRAMES of idle, and the first idle
 * timeout plays the intro cutscene instead). The dwell range covers the
 * whole first demo race, so a run can prove a launch from inside it.
 * Measured on the reference Debug build (proof ticks): menu ready at 732,
 * the intro cutscene loading from 1415, the title back at 4628, the demo
 * race loading from 5541 and running from about 5567 to 7364 (a tick of
 * load-time jitter run to run); so a dwell of about 4840..6630 launches
 * from inside it (5400 does, at tick 6132). When the
 * dwell ends outside both windows (a load, the intro cutscene) the hook waits
 * up to LAUNCH_WAIT_TIMEOUT_TICKS for one.
 *
 * Scripted pads (part of the proof definition; RS-24). While the proof is
 * active the game hook installs the pads of NativeArcadeRosterProof_ScriptedPads
 * for the configured profile through Platform_InputInstallPadSnapshots from
 * process start to exit, so no keyboard, pad, or G29 input reaches the game.
 * In both profiles pads 0 and 1 (retail players 0 and 1) are connected
 * digital pads with centred analog values and pads 2 and 3 are disconnected
 * (no multitap): the same pad layout, so the pre-race frames of the two
 * profiles see the same pads and no pad is ever unplugged. Neutral means no
 * button held. Race tick 0 is the first frame after VALIDATED on which the
 * drivers extraction succeeds (the race order is rebuilt on the first race
 * tick); it is known only once that frame was simulated, so every frame up to
 * and including race tick 0 runs on neutral pads. The frame of race tick
 * n >= 1 runs on the pattern for (profile, n), a pure function of both:
 * - TWO_CAB (players 0 and 1 are CAB1 and CAB2): both players hold CROSS
 *   (accelerate); player 1 (CAB2) also holds RIGHT while (n mod
 *   STEER_PERIOD) is in [STEER_BEGIN, STEER_END), that is [60, 90) of every
 *   120 ticks.
 * - ONE_CAB (player 0 is CAB1, the only human; pad 1 drives no driver):
 *   player 0 holds CROSS, and also RIGHT while (n mod STEER_PERIOD) is in
 *   [STEER_BEGIN, STEER_END); player 1 stays neutral.
 * After the proof reported, the pads are neutral again until the exit.
 *
 * Autopilot (LR-S2 (b), --arcade-roster-proof-autopilot, TWO_CAB only). The
 * frame of race tick n >= 1 runs players 0 and 1 on the steering decision of
 * include/platform/native_arcade_link_autopilot.h (CROSS, and LEFT or RIGHT
 * toward the restart point ahead) instead of the scripted pattern; pads 2
 * and 3 are as above. The game hook forms the decision after race tick
 * n - 1 was simulated, from that tick's kart positions and headings and the
 * level's restart points. Those facts are read internally only and never
 * reach a digest or the report; only the pads they produce reach the game
 * (and so the input digest). The hook logs, to the process log, the race
 * tick at which each of players 0 and 1 first shows the race finished and
 * the race tick at which the race first shows END_OF_RACE; the report format
 * is unchanged.
 *
 * Per-tick digests. From race tick 0, for `ticks` race ticks, the game hook
 * appends one line per tick after its frame was simulated:
 *   tick <n> control <16 hex> rcontrol <16 hex> rng <16 hex> input <16 hex> drivers <64 hex>
 * control, rng, and input are the V1 canonical domain digests of that frame
 * (the live V1 projection, MainCanonicalState_ProjectLive); rcontrol is the
 * race-relative control digest (NativeArcadeRosterProof_RaceControlDigest):
 * the same V1 control encoding and FNV-1a 64 digest with the three
 * boot-relative counters (frameTimer, frameCounter, timer) zeroed, computed
 * locally (no schema change); drivers is the SHA-256 of the canonical
 * encoding (NativeCanonicalDriversDetailedV1_Encode) of the topology-free
 * drivers candidate, a detailed record whose Physics groups are all at their
 * exact zero value (the report header says "drivers digest excludes
 * physics"). The report header also carries four boot-relative counters (the
 * three above and gGT->frameTimer_Confetti) at the launch tick (before the
 * race setup pinned gGT->timer and gGT->frameTimer_Confetti, RS-17) and as
 * race tick 0 saw them. The report ends with "end ticks <count>".
 *
 * Host timing. main.c turns on the host-local fixed VBlank pacing
 * (Platform_SetFixedVBlankPacing, include/platform.h) for the whole proof
 * run: a slow host frame then never emits late VBlanks, so every game tick
 * advances exactly the retail 2 VBlanks and gGT->elapsedTimeMS, and with it
 * the whole race, is independent of host timing. The only other user is a
 * linked arcade race, which the arcade-link host glue runs with fixed pacing
 * from its Launch frame to its Disarm frame (docs/LOCKSTEP_RACE_MILESTONE.md
 * LR-7); the proof excludes the arcade link, so it never switches the
 * proof's pacing. Every other run keeps the default pacing.
 *
 * Hold (LR-S2 (a), --arcade-roster-proof-hold). On the frame logged as race
 * tick HOLD_TICK, the game hook (MainArcadeRosterProof_Frame, which runs
 * after the frame's GameLogic and before its VBlanks, the ones that read the
 * pads for the next tick: the LR-9 hook position) blocks in the stall hold
 * loop (game/MAIN/MainArcadeRaceHold.h) until HOLD_PERIODS full tick periods
 * of wall time have passed. The loop emits no VBlank, so the race must run
 * exactly as without the hold: every tick line equals the unheld run's, and
 * frameTimer (gGT->frameTimer_VsyncCallback) advances by exactly 2 from race
 * tick HOLD_TICK - 1 to HOLD_TICK and not at all inside the hold. The
 * report's "hold" line carries that evidence (FormatReport); the check
 * (tools/arcade-roster-proof-check.ps1, run K) judges it.
 *
 * Process exit codes while the proof is active (enum
 * NativeArcadeRosterProofResult is also the report's result):
 *
 *    0  PASS                 VALIDATED, every requested race tick logged, the
 *                            launch and race tick 0 counters kept, and the
 *                            report was written
 *    1  (startup failure)    main.c's generic failure: an invalid option or
 *                            combination, asset or platform initialisation,
 *                            or a proof that could not be configured; the
 *                            proof never ran and wrote no report. Never a
 *                            proof result.
 *   20  INCOMPLETE           the process ended before the proof reported
 *                            (window closed, host quit, CTR_Main returned):
 *                            the default exit code of every exit path while
 *                            the proof is active; exit code only, no report
 *   21  REPORT_WRITE_FAILED  VALIDATED, but the report could not be written;
 *                            exit code only
 *   22  SETUP_FAILED         the race setup latched FAILED (a refused Launch
 *                            included)
 *   23  ARM_FAILED           MainArcadeRaceSetup_Arm refused the config
 *   24  LAUNCH_FAILED        no launch window within LAUNCH_WAIT_TIMEOUT_TICKS
 *                            after the dwell
 *   25  MENU_READY_TIMEOUT   no menu-ready frame within MENU_READY_TIMEOUT_TICKS
 *   26  VALIDATE_TIMEOUT     not VALIDATED within VALIDATE_TIMEOUT_TICKS of launch
 *   27  SEED_MISMATCH        VALIDATED, but a retail seed field read back
 *                            right after the SEEDED writes differs from the
 *                            seed the setup produced (the adapter's field
 *                            mapping is wrong)
 *   28  EVIDENCE_MISSING     VALIDATED, but the digests, the slot facts, or
 *                            the seed or pin readback could not be read, or
 *                            the launch or race tick 0 counters or a
 *                            requested tick line is missing, or a requested
 *                            hold did not run or lacks its frameTimer values;
 *                            PASS needs all of them
 *   29  RACE_TICK_TIMEOUT    no race tick 0 (the drivers extraction never
 *                            succeeded) within RACE_TICK_TIMEOUT_TICKS of
 *                            VALIDATED
 *   30  DRIVERS_FAILED       the drivers extraction or its canonical encoding
 *                            failed at a logged race tick after tick 0
 *   31  DIGEST_FAILED        the frame's V1 canonical projection (or its input
 *                            freeze) or its race-relative control digest
 *                            failed at a logged race tick, or a tick line
 *                            could not be kept
 *   32  PIN_MISMATCH         VALIDATED, but a pinned boot-relative counter
 *                            (gGT->timer, gGT->frameTimer_Confetti; RS-17)
 *                            read back right after the SEEDED writes differs
 *                            from the value the setup pinned
 *   33  TICK_LOG_TIMEOUT     race tick 0 was reached, but the requested race
 *                            ticks were not all logged within
 *                            ticks + TICK_LOG_SLACK_TICKS proof ticks of it
 *
 * The failure codes start at 20 so that none collides with 1 or with the C
 * runtime's abort() code 3. The table is the same for both profiles: a
 * ONE_CAB proof passes or fails on exactly the evidence a TWO_CAB proof
 * needs (its eight slot lines are then CAB1_HUMAN and seven BOTs).
 *
 * Config, per profile (the same identity, profile, and seed always give the
 * same config):
 * - TWO_CAB (the default): the arcade-link fixture
 *   (NativeArcadeLinkFixture_Build) for the caller's identity, resolved
 *   through match select with two fixed choices: CAB1 picks the fixture's
 *   CAB1 character and CAB2 the fixture's CAB2 character, both vote the
 *   fixture track and lap count, and the nonces are seed (CAB1) and seed XOR
 *   NATIVE_ARCADE_ROSTER_PROOF_NONCE_MIX (CAB2) (NativeMatchSelect_Resolve,
 *   then NativeMatchSelect_BuildConfig). The seed reaches the config only
 *   through the resolved masterSeed.
 * - RS-23 (owner-accepted default), ONE_CAB: match select stays
 *   TWO_CAB-only and is not used. The arcade-link fixture is built for the
 *   identity, and the config takes from it the build and content identity,
 *   trackID, lapCount, the tick rate, the CAB1 character, and the bots'
 *   difficulty (the fixture's first BOT slot).
 *   NativeMatchConfigV1_InitArcadeOneCab gives the ONE_CAB roles and
 *   lifecycles; slot 0 (CAB1_HUMAN) holds the CAB1 character at difficulty 0,
 *   and slots 1..7 (BOT) hold NativeArcadeBotRules_ExpectedBots1P(CAB1
 *   character), in ascending slot order, at the fixture's bot difficulty;
 *   masterSeed is the seed option value itself; botRulesDigest is
 *   NativeArcadeBotRules_Digest1PV1. The result must pass
 *   NativeArcadeBotRules_ValidateConfigV1.
 *
 * Identity: the proof is single-machine. When the build identity is unknown
 * (a dirty tree), main.c uses the fixed proof build identity
 * (NativeArcadeRosterProof_ProofBuildIdentity, SHA-256 of
 * NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG) and logs it, but always the real
 * content identity of the open disc.
 *
 * The singleton is inert until Configure succeeds with the proof enabled.
 * No heap use; the report writer is the only I/O.
 */

#define NATIVE_ARCADE_ROSTER_PROOF_PATH_BYTES 260u /* including the NUL */
#define NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_SEED UINT64_C(1)
#define NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_DWELL 0u
#define NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL 7200u
#define NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_TICKS 900u
#define NATIVE_ARCADE_ROSTER_PROOF_MAX_TICKS 3600u
/* The tick count cap with the autopilot (LR-S2 (b)): room for a 3-lap race. */
#define NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS 6000u
#define NATIVE_ARCADE_ROSTER_PROOF_NONCE_MIX UINT64_C(0x9E3779B97F4A7C15)
#define NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG "CTRN arcade roster proof build v1"
#define NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT NATIVE_MATCH_CONFIG_V1_SLOT_COUNT
#define NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES 24u
/* The race profiles the proof runs (RS-23): the match config's own values. */
#define NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB
#define NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB
#define NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_PROFILE NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB
#define NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE UINT32_MAX
/* The hold (LR-S2 (a)): the race tick it holds on and its length in tick
 * periods of wall time. */
#define NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK 300u
#define NATIVE_ARCADE_ROSTER_PROOF_HOLD_PERIODS 45u

/* Watchdogs and the post-validation wait, in proof ticks (game frames). */
#define NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT_TICKS 3000u
#define NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_WAIT_TIMEOUT_TICKS 3600u
#define NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT_TICKS 1800u
#define NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT_TICKS 1800u
/* After race tick 0: the proof ticks allowed beyond the requested tick count
 * for the tick lines to be logged (normally one line per proof tick). */
#define NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_SLACK_TICKS 600u

/* The scripted pads (see above). Buttons are the PSX pad's active-low word
 * (buttons[0] is its low byte): a held button reads 0. */
#define NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT 4u
#define NATIVE_ARCADE_ROSTER_PROOF_PAD_ID_DIGITAL 0x41u
#define NATIVE_ARCADE_ROSTER_PROOF_PAD_ID_DISCONNECTED 0xFFu
#define NATIVE_ARCADE_ROSTER_PROOF_PAD_STATUS_DISCONNECTED 0xFFu
#define NATIVE_ARCADE_ROSTER_PROOF_PAD_ANALOG_CENTRE 0x80u
#define NATIVE_ARCADE_ROSTER_PROOF_BUTTONS_NONE 0xFFFFu
#define NATIVE_ARCADE_ROSTER_PROOF_BUTTON_RIGHT 0x0020u
#define NATIVE_ARCADE_ROSTER_PROOF_BUTTON_CROSS 0x4000u
#define NATIVE_ARCADE_ROSTER_PROOF_STEER_PERIOD 120u
#define NATIVE_ARCADE_ROSTER_PROOF_STEER_BEGIN 60u
#define NATIVE_ARCADE_ROSTER_PROOF_STEER_END 90u

struct NativeArcadeRosterProofOptions
{
	uint8_t enabled; /* --arcade-roster-proof given */
	uint8_t hold;    /* --arcade-roster-proof-hold given */
	uint8_t autopilot; /* --arcade-roster-proof-autopilot given */
	uint8_t reserved[1];
	uint32_t dwellTicks;
	uint64_t seed;
	uint32_t tickCount; /* race ticks to log */
	uint32_t profile;   /* NATIVE_ARCADE_ROSTER_PROOF_PROFILE_* (--arcade-roster-proof-profile) */
	char logPath[NATIVE_ARCADE_ROSTER_PROOF_PATH_BYTES];
};

/* The proof's result, which is also its process exit code (the table above). */
enum NativeArcadeRosterProofResult
{
	NATIVE_ARCADE_ROSTER_PROOF_PASS = 0,                  /* VALIDATED, report written */
	NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE = 20,           /* exit code only: ended before the proof reported */
	NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED = 21,  /* exit code only: the report could not be written */
	NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED = 22,         /* the race setup latched FAILED */
	NATIVE_ARCADE_ROSTER_PROOF_ARM_FAILED = 23,           /* MainArcadeRaceSetup_Arm refused the config */
	NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED = 24,        /* no launch window within LAUNCH_WAIT_TIMEOUT_TICKS after the dwell */
	NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT = 25,   /* no menu-ready frame within MENU_READY_TIMEOUT_TICKS ticks */
	NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT = 26,     /* not VALIDATED within VALIDATE_TIMEOUT_TICKS ticks of launch */
	NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH = 27,        /* a seeded retail field read back differs from the produced seed */
	NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING = 28,     /* VALIDATED without readable digests, slot facts, or seed readback */
	NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT = 29,    /* no race tick 0 within RACE_TICK_TIMEOUT_TICKS of VALIDATED */
	NATIVE_ARCADE_ROSTER_PROOF_DRIVERS_FAILED = 30,       /* the drivers extraction or encoding failed after race tick 0 */
	NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED = 31,        /* the V1 projection failed at a logged tick, or a line was not kept */
	NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH = 32,         /* a pinned counter read back differs from the pinned value */
	NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_TIMEOUT = 33      /* race tick 0 reached, but not every tick line logged in time */
};

/* One scripted pad, in the shape of the host pad snapshot. */
struct NativeArcadeRosterProofPad
{
	uint8_t status;
	uint8_t id;
	uint8_t buttons[2];
	uint8_t analog[4];
	uint8_t connected;
	uint8_t reserved[3];
};

/* One logged race tick. */
struct NativeArcadeRosterProofTickLine
{
	uint32_t tick;
	uint32_t reserved;
	uint64_t control;     /* V1 CONTROL domain digest */
	uint64_t raceControl; /* race-relative control digest (NativeArcadeRosterProof_RaceControlDigest) */
	uint64_t rng;         /* V1 RNG domain digest */
	uint64_t input;   /* V1 INPUT domain digest */
	uint8_t drivers[NATIVE_SHA256_DIGEST_BYTES];
};

/* Where the proof launched from (the report's "launch window" line). */
enum NativeArcadeRosterProofLaunchWindow
{
	NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE = 0,     /* not launched */
	NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE = 1,    /* the title menu-ready window */
	NATIVE_ARCADE_ROSTER_PROOF_WINDOW_DEMO_RACE = 2 /* inside the attract demo race */
};

/* One validated slot, as the race setup's slot facts report it. */
struct NativeArcadeRosterProofSlotLine
{
	uint8_t present;
	uint8_t role; /* NATIVE_MATCH_SLOT_ROLE_* */
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t spawnOrder;
	uint8_t navPathIndex;
	uint8_t accelerationOrder;
	uint8_t reserved;
};

/* The boot-relative counters of one frame: the V1 control values gGT->timer,
 * sdata->frameCounter, and gGT->frameTimer_VsyncCallback, and
 * gGT->frameTimer_Confetti (pinned by the setup, RS-17; not a V1 control
 * value, so the game hook reads it from gGT on the same frame). */
struct NativeArcadeRosterProofCounters
{
	int32_t timer;
	int32_t frameCounter;
	int32_t frameTimer;
	int32_t frameTimerConfetti;
};

/*
 * The hold's evidence (LR-S2 (a)): what the hold loop measured
 * (MainArcadeRaceHoldResult), an independent measurement of the same hold,
 * and gGT->frameTimer_VsyncCallback at the hold's entry and exit and, from
 * the V1 control values, at race ticks HOLD_TICK - 1 and HOLD_TICK.
 * frameTimerValid has bit 0 set once frameTimerBefore holds tick
 * HOLD_TICK - 1's value and bit 1 once frameTimerAfter holds tick
 * HOLD_TICK's. independentUs is the wall time the proof hook measures around
 * its MainArcadeRaceHold_Run call with C11 timespec_get(TIME_UTC), a clock
 * the hold loop does not use (the loop's own wallUs reads the platform's
 * SDL clock); independentValid is 1 when both reads succeeded and did not
 * go back.
 */
struct NativeArcadeRosterProofHold
{
	uint32_t raceTick; /* the race tick held on */
	uint32_t periods;  /* full tick periods held */
	uint64_t wallUs;   /* measured wall time of the hold (the loop's clock) */
	uint64_t independentUs; /* measured wall time of the hold (timespec_get, the proof hook's) */
	uint64_t expectedUs; /* HOLD_PERIODS tick periods */
	uint32_t pumps;
	uint32_t minPeriodPumps; /* UINT32_MAX when no period ended */
	uint32_t bannersDue;
	uint32_t bannersPresented;
	int32_t vsyncEntry;
	int32_t vsyncExit;
	int32_t frameTimerBefore;
	int32_t frameTimerAfter;
	uint32_t frameTimerValid;
	uint32_t independentValid;
};

/* The boot-relative counters the race setup pins at its seeding point
 * (RS-17): gGT->timer and gGT->frameTimer_Confetti. */
struct NativeArcadeRosterProofPins
{
	int32_t timer;
	int32_t frameTimerConfetti;
};

/*
 * The report the game hook fills. Status and failure are the race setup's
 * own codes and names (the platform module does not include game headers).
 * Ticks are proof ticks, TICK_NONE when never reached. digestsValid and
 * slotsValid say whether the digests and slot lines are filled (only once
 * VALIDATED). seedValid says whether seedStored holds the retail seed fields
 * read back right after the SEEDED writes, and seedMatch whether they equal
 * the seeds the setup produced (NativeArcadeRosterProof_SeedsMatch); pinValid,
 * pinStored, and pinMatch say the same of the pinned counters
 * (NativeArcadeRosterProof_PinsMatch).
 * tickLineCount is the number of tick lines kept, and countersValid says
 * whether raceTickZeroCounters holds race tick 0's counters; launchCountersValid
 * whether launchCounters holds the same counters at the launch tick.
 */
struct NativeArcadeRosterProofReport
{
	uint32_t result;  /* enum NativeArcadeRosterProofResult */
	uint32_t profile; /* NATIVE_ARCADE_ROSTER_PROOF_PROFILE_*: the configured profile */
	uint32_t setupStatus;
	uint32_t setupFailure;
	char setupStatusName[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES];
	char setupFailureName[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES];
	uint64_t seed;
	uint32_t dwellTicks;
	uint32_t menuReadyTick;
	uint32_t demoRaceTick; /* first tick the proof saw the demo race running */
	uint32_t launchTick;
	uint32_t launchWindow; /* enum NativeArcadeRosterProofLaunchWindow */
	uint32_t validatedTick;
	uint32_t raceTickZeroTick; /* the proof tick of race tick 0 */
	uint32_t ticksRequested;   /* --arcade-roster-proof-ticks */
	uint32_t tickLineCount;    /* tick lines kept (NativeArcadeRosterProof_TickCount) */
	uint8_t digestsValid;
	uint8_t slotsValid;
	uint8_t seedValid;
	uint8_t seedMatch;
	uint8_t countersValid;
	uint8_t pinValid;
	uint8_t pinMatch;
	uint8_t launchCountersValid;
	struct NativeArcadeRosterProofCounters launchCounters; /* at the launch tick, before the setup pinned anything */
	struct NativeArcadeRosterProofCounters raceTickZeroCounters;
	struct NativeArcadeRetailRngSeedsV1 seedStored;
	struct NativeArcadeRosterProofPins pinStored;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t racePlanDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t botSetupPlanDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t bankDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeArcadeRosterProofSlotLine slots[NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT];
	uint8_t holdRequested; /* --arcade-roster-proof-hold */
	uint8_t holdDone;      /* the hold ran; hold holds its evidence */
	uint8_t holdReserved[2];
	struct NativeArcadeRosterProofHold hold;
};

/* NULL is a no-op. Otherwise: disabled, seed 1, dwell 0, 900 ticks, profile
 * TWO_CAB, no hold, no autopilot, empty path. */
void NativeArcadeRosterProofOptions_SetDefaults(struct NativeArcadeRosterProofOptions *options);

/* Returns 1 and updates *options on success; 0 with *options untouched otherwise. */
int NativeArcadeRosterProofOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeRosterProofOptions *options);

/* Strict u64: 1..20 decimal digits no greater than UINT64_MAX, or 0x/0X and
 * 1..16 hex digits, with nothing else. 0 with *value untouched otherwise. */
int NativeArcadeRosterProof_ParseSeed(const char *text, uint64_t *value);

/* 1 when argv names a frame-capture exit option (--exit-after-frame, in its
 * separate or its --exit-after-frame=N form), matched by name only; else 0
 * (also for a NULL argv). main.c rejects these with the proof. */
int NativeArcadeRosterProof_NamesExitOption(int argc, char *argv[]);

/* SHA-256 of NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG (no NUL). */
int NativeArcadeRosterProof_ProofBuildIdentity(uint8_t build[NATIVE_IDENTITY_DIGEST_BYTES]);

/*
 * The proof config of a profile (see above; RS-23). Returns 0 with *config
 * untouched on NULL arguments, a profile other than TWO_CAB and ONE_CAB, a
 * fixture the identity cannot build, a failed resolution (TWO_CAB), or a
 * result that fails NativeArcadeBotRules_ValidateConfigV1.
 */
int NativeArcadeRosterProof_BuildConfig(const struct NativeIdentityV1 *identity, uint32_t profile, uint64_t seed,
	struct NativeMatchConfigV1 *config);

/*
 * Configures the singleton. Disabled (or NULL) options leave it inactive and
 * return 1. Enabled options need a non-empty log path and a config
 * BuildConfig can build for *identity and the options' profile and seed; on
 * failure the singleton stays inactive and 0 is returned.
 */
int NativeArcadeRosterProof_Configure(const struct NativeArcadeRosterProofOptions *options,
	const struct NativeIdentityV1 *identity);

/* Back to inactive (and no recorded exit code). */
void NativeArcadeRosterProof_Shutdown(void);

int NativeArcadeRosterProof_Active(void);

/* The game hook records the proof's exit code once, right before it requests
 * the exit; later calls and calls while inactive are ignored. */
void NativeArcadeRosterProof_RecordExitCode(int exitCode);

/*
 * The code every host exit path must use: inactiveExitCode when the proof is
 * inactive (so a default run is unchanged); the recorded code once the proof
 * reported; otherwise NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE, so that closing
 * the window or any other early exit never looks like PASS.
 */
int NativeArcadeRosterProof_ExitCode(int inactiveExitCode);

/*
 * The scripted pads for a frame of a profile (see above; RS-24): raceTick is
 * the race tick the frame will be logged as, or
 * NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE (and 0) for the neutral pads. A pure
 * function of (profile, raceTick); a profile other than TWO_CAB and ONE_CAB
 * gives the neutral pads (the same pad layout, no button held). NULL is a
 * no-op.
 */
void NativeArcadeRosterProof_ScriptedPads(uint32_t profile, uint32_t raceTick,
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT]);

/*
 * Keeps one tick line for the report. Returns 1 only while active, when
 * line->tick is the next tick (the count of lines kept so far), and fewer than
 * the configured tick count are kept; 0 otherwise, with nothing kept.
 */
int NativeArcadeRosterProof_RecordTick(const struct NativeArcadeRosterProofTickLine *line);

/* The number of tick lines kept; 0 when inactive. */
uint32_t NativeArcadeRosterProof_TickCount(void);

/*
 * The race-relative control digest of a V1 state: a copy of the state with
 * control.frameTimer, control.frameCounter, and control.timer (the
 * boot-relative counters) zeroed, digested by NativeCanonicalStateV1_ComputeDigests
 * (the V1 control encoding and FNV-1a 64), and its CONTROL domain digest
 * stored in *digest. The state is not changed. 0 with *digest untouched on
 * NULL arguments or a state the V1 digests reject.
 */
int NativeArcadeRosterProof_RaceControlDigest(const struct NativeCanonicalStateV1 *state, uint64_t *digest);

/* One tick line as text (see above), with its newline, NUL-terminated;
 * *length excludes the NUL. 0 on NULL arguments or a buffer too small. */
int NativeArcadeRosterProof_FormatTickLine(const struct NativeArcadeRosterProofTickLine *line, char *buffer,
	size_t bufferSize, size_t *length);

/* The configured config, profile, dwell, tick count, hold (1 when
 * requested), autopilot (1 when requested), seed, and log path;
 * NULL/0/empty when inactive. */
const struct NativeMatchConfigV1 *NativeArcadeRosterProof_Config(void);
uint32_t NativeArcadeRosterProof_Profile(void);
uint32_t NativeArcadeRosterProof_Dwell(void);
uint32_t NativeArcadeRosterProof_Ticks(void);
uint32_t NativeArcadeRosterProof_Hold(void);
uint32_t NativeArcadeRosterProof_Autopilot(void);
uint64_t NativeArcadeRosterProof_Seed(void);
const char *NativeArcadeRosterProof_LogPath(void);

/*
 * Formats the report as text into buffer (NUL-terminated) and stores its
 * length without the NUL. Returns 0 on NULL arguments or a buffer too small.
 * The format is line based: a header line ("arcade roster proof v9"), the
 * line "drivers digest excludes physics", then "result", "profile" (TWO_CAB
 * or ONE_CAB, the configured profile; UNKNOWN for any other value), "setup
 * status",
 * "setup failure", "seed", "dwell", "ticks" (requested), "menu ready tick",
 * "demo race tick", "launch tick", "launch window" (title, demo race, or
 * none), "launch counters" (timer, frameCounter, frameTimer, and
 * frameTimerConfetti at the launch tick, as signed decimal, or "none"),
 * "validated tick", "race tick 0 tick",
 * "race tick 0 counters" (the same, at race tick 0), the four digests as
 * lowercase hex (or "none"), the "seeded" line (the five retail seed fields
 * and the two pinned counters, timer and frameTimerConfetti as signed
 * decimal, as read back, then "match 1" when every one equals what the setup
 * wrote, else "match 0"; "seeded none" without both readbacks), one
 * "slot" line per slot, and the "hold" line (v9, LR-S2 (a)): "hold none"
 * without a requested hold, "hold missing" when it was requested but did
 * not run, otherwise
 *   hold tick <t> periods <p> wall us <w> independent us <i|none>
 *     expected us <e> pumps <n>
 *     min pumps per period <m|none> banners due <d> presented <b>
 *     vsync entry <x> exit <y> frameTimer before <f|none> after <g|none>
 * on one line (signed decimal for the counters).
 */
int NativeArcadeRosterProof_FormatReport(const struct NativeArcadeRosterProofReport *report, char *buffer,
	size_t bufferSize, size_t *length);

/* 1 when every one of the five seeds equals its readback; 0 otherwise (also
 * for NULL). */
int NativeArcadeRosterProof_SeedsMatch(const struct NativeArcadeRetailRngSeedsV1 *produced,
	const struct NativeArcadeRetailRngSeedsV1 *stored);

/* 1 when both pinned counters equal their readback; 0 otherwise (also for
 * NULL). */
int NativeArcadeRosterProof_PinsMatch(const struct NativeArcadeRosterProofPins *produced,
	const struct NativeArcadeRosterProofPins *stored);

/*
 * The result a finished proof reports: requested unless it is PASS, and PASS
 * only when the digests, the slot facts, the seed and pin readbacks, and the
 * launch and race tick 0 counters are all valid, every requested tick line
 * was kept (tickLineCount == ticksRequested, and at least one), and a
 * requested hold ran with both frameTimer values (else EVIDENCE_MISSING), the
 * seed readback matches (else SEED_MISMATCH), and the
 * pin readback matches (else PIN_MISMATCH).
 */
uint32_t NativeArcadeRosterProof_FinalResult(uint32_t requested, const struct NativeArcadeRosterProofReport *report);

/* Writes the formatted report, then every kept tick line in order, then
 * "end ticks <count>", to the configured log path. 0 when inactive or on any
 * I/O failure. */
int NativeArcadeRosterProof_WriteReport(const struct NativeArcadeRosterProofReport *report);

/* The fixed name of a result ("PASS", "SETUP_FAILED", ...); "UNKNOWN" otherwise. */
const char *NativeArcadeRosterProof_ResultName(uint32_t result);

/* The fixed name of a profile ("TWO_CAB", "ONE_CAB"); "UNKNOWN" otherwise. */
const char *NativeArcadeRosterProof_ProfileName(uint32_t profile);

/* The fixed name of a launch window ("title", "demo race", "none"); "unknown" otherwise. */
const char *NativeArcadeRosterProof_LaunchWindowName(uint32_t window);

#endif
