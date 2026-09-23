#ifndef MAIN_ARCADE_RACE_SETUP_CORE_H
#define MAIN_ARCADE_RACE_SETUP_CORE_H

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRaceSetupFacts.h"
#include "MAIN/MainArcadeRaceSetupPlan.h"
#include "MAIN/MainArcadeRoster.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_canonical_drivers_roster.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stdint.h>

/*
 * Race setup decision core (docs/ROSTER_MILESTONE.md section 3.2, R-5c).
 * Every decision of the live race setup adapter (game/MAIN/MainArcadeRaceSetup.c)
 * lives here as a pure step over a pointer-free view of the live values: the
 * state checks, the Launch preconditions, the verification of the fields the
 * load consumed, which retail fields are written and with what values, the
 * retail RNG seed write order, the fact validation, and the failure codes.
 * The adapter reads the view from the retail globals, calls the step, applies
 * the returned write list in order, and logs; it decides nothing itself.
 *
 * Built as the standalone library ctr_native_arcade_race_setup_core (C17,
 * extensions off), linked into ctr_native and never unity-included. Pure: no
 * retail global, no I/O, no heap, and no static state; all state is the
 * caller-owned struct MainArcadeRaceSetupCore, and the post-drivers step works
 * in a caller-owned scratch so that no large object lives on the stack.
 *
 * State machine (the adapter's contract, MAIN/MainArcadeRaceSetup.h):
 *
 *   IDLE --Arm--> ARMED --Launch--> LAUNCHED --OnFinalizeInitBegin--> SEEDED
 *        --OnDriversInitialized--> VALIDATED
 *
 * and FAILED (fail closed, with a failure code) from any step after Arm.
 * Disarm returns every state to IDLE.
 *
 * Wrong state (STATE): Arm outside IDLE and Launch outside ARMED return 0;
 * while a setup is in flight (ARMED, LAUNCHED, SEEDED, VALIDATED) the misuse
 * also latches FAILED/STATE, and from IDLE or FAILED nothing changes (the
 * outcome log is REFUSED). OnFinalizeInitBegin in SEEDED or VALIDATED, and
 * OnDriversInitialized in LAUNCHED, latch FAILED/STATE. A hook that should act
 * (OnFinalizeInitBegin in LAUNCHED, OnDriversInitialized in SEEDED) without a
 * game tracker latches FAILED/NO_TRACKER. Every other hook call is a no-op:
 * no write, no state change.
 *
 * Writes. Every step fills a MainArcadeRaceSetupCoreOutcome whose ops list
 * is the exact, ordered set of retail writes the adapter performs. There is
 * no levelID target: the load request (REQUEST_LOAD) carries the plan's level
 * and LOAD_LevelFile writes it. The ops are:
 * - Launch: GAME_MODE1, GAME_MODE2, ARCADE_DIFFICULTY, BOOL_DEMO_MODE,
 *   NUM_LAPS, NUM_PLYR_NEXT_GAME, CHARACTER_ID 0..7 (each slot of the plan's
 *   characterWriteMask with the plan's value, every other slot with its
 *   observed value: ARCADE_TWO_CAB writes 0..5 and rewrites 6 and 7 as
 *   observed, ARCADE_ONE_CAB writes all eight), then REQUEST_LOAD. The count
 *   is the same for both profiles (LAUNCH_OP_COUNT, 15).
 * - OnFinalizeInitBegin (LAUNCHED): GAME_MODE1, GAME_MODE2, ARCADE_DIFFICULTY,
 *   BOOL_DEMO_MODE (the re-apply), then TIMER and FRAME_TIMER_CONFETTI (the
 *   pinned boot-relative counters, RS-17, see "Boot-relative counters"
 *   below), then RANDOM_NUMBER, ADV_RNG0, ADV_RNG1, PSX_RAND_SEED, AUDIO_RNG
 *   (the seeds, RS-7, in this order).
 * - Disarm: GAME_MODE1 only when the vibration bits are restored.
 * Every other step writes nothing. LAUNCH_OP_COUNT and BEGIN_OP_COUNT below
 * are those exact counts; a static assert proves each fits MAX_OPS, and a step
 * whose list would still overflow fails closed (OPS) with no op at all, never
 * a truncated list.
 *
 * Launch from inside the attract demo race. Launch may run while the attract
 * demo race still ticks: MainRaceTrack_RequestLoad only queues the load, and
 * the demo race keeps simulating for a few frames until the checkered flag
 * covers the screen and the load starts (MainMain.c, the Loading.stage -4
 * branch). The Launch writes (the mode words, arcadeDifficulty,
 * boolDemoMode 0, numLaps, numPlyrNextGame, and characterIDs) therefore apply
 * to that demo race for those frames. This is safe:
 * - boolDemoMode 0 disables the demo exit: the countdown and the "any button"
 *   exit that would request MAIN_MENU_LEVEL run only while boolDemoMode is set
 *   (MainMain.c, CTR_Main state 3, the boolDemoMode block right after
 *   MainFrame_ResetDB, lines 437-492 today), so the demo cannot queue a
 *   competing load;
 * - every other competing load is caught: a later MainRaceTrack_RequestLoad
 *   replaces the queued level, and the pre-drivers hook then fails closed
 *   with LEVEL_MISMATCH;
 * - nothing written at Launch is trusted at race init: the pre-drivers hook
 *   re-verifies every field the load consumed (levelID, numLaps,
 *   numPlyrCurrGame, and the characterIDs of the plan's characterWriteMask:
 *   0..5 for ARCADE_TWO_CAB, 0..7 for ARCADE_ONE_CAB; LOAD_FIELDS_MISMATCH)
 *   and re-applies
 *   the mode words, arcadeDifficulty, and boolDemoMode after the demo race
 *   ended, so nothing the demo race did in those frames reaches the race;
 * - the demo race's drivers were spawned from its own characterIDs at its own
 *   init, so the new values reach it only through presentation readers
 *   (voice lines, HUD) for those frames;
 * - it has a precedent: the arcade-link return-to-title
 *   (MainArcadeLink.c:239-245) writes boolDemoMode and numPlyrNextGame and
 *   requests a load under the running demo race in the same way.
 * This does not contradict the Disarm rule that gameMode1 must not change
 * under a running race. That rule protects the race this setup launched,
 * whose control state is canonical from its first tick. The attract demo
 * race is not that race: nothing records or compares it, its level is being
 * replaced by the setup's own, and its last frames run before the setup's
 * race exists. The Launch mode write is the one the retail menu itself makes
 * (MM_MenuFlow.c:152, :224) on the way out of the attract loop.
 *
 * Boot-relative counters (RS-17, the R-6c audit). Two cabinets never share a
 * boot history, so every frame or time counter that survives into the race
 * with a boot-relative value was audited: its race readers (simulation,
 * retail RNG, particles that can draw retail RNG), any stored timestamp later
 * compared with it (a reset would make a bogus delta), and the platform.
 * Verdicts: PIN (feeds the simulation or its RNG and is safe to set), LEAVE
 * (presentation or platform only), UNSAFE (feeds the simulation but cannot
 * be reset safely; none found). File:line as of R-6d.
 *
 * - gGT->timer (V1 control "timer"): +1 per MainFrame_GameLogic
 *   (MainFrame.c:184), never reset by a load. Race readers: the exhaust
 *   emitter picks each human's frames by parity in 2P and the bots' by
 *   timer & 3 (Vehicle/VehEmitter.c:1613, :1631, :1636); the terrain emitter
 *   picks its even or odd set (VehEmitter.c:1351); the warp dust spawns on
 *   even frames (VehStuckProc.c:1610), the bubbles every 8 frames
 *   (231/RB_Bubbles.c:50), the flame jet particles by timer
 *   (231/RB_FlameJet.c:304, :310); each of those particles can draw MixRNG
 *   (Particle.c:199-217, the underwater bubble pop: with an odd timer offset
 *   the live roster proof saw randomNumber diverge at race tick 399 on Crash
 *   Cove). Also the
 *   airborne speedometer needle, Driver state (VehPhysGeneral.c:1618, read
 *   by VehStuckProc.c:1265), and the bot wiggle under a cloud (BOTS.c:2520).
 *   Stored timestamp: HOWL_Voiceline.c:280 stores it in a voiceline, never
 *   compared with it again. The rest is presentation or rumble, none treating
 *   0 specially (UI, water, texture cycling, DecalMP.c:110, rumble at
 *   GAMEPAD.c:720, :763, :851, the frozen clock tick sound at
 *   MainFrame.c:232, the invincibility flicker on instFlags at
 *   231/RB_Player.c:247, the shield instance scale by timer % 6 at
 *   231/RB_MaskShieldCloud.c:363-371, the wobble rumble at
 *   VehEmitter.c:1770, the HUD beep at UI/UI_RaceHud.c:379, Display.c:135).
 *   Platform: not read. PIN 0.
 * - gGT->frameTimer_Confetti: +1 per emitted VBlank while not paused (the
 *   VBlank callback, MainDrawCb.c:25), never reset by a load. Race readers:
 *   every particle oscillator's value (Particle.c:247) and each new
 *   oscillator's now-relative phase (Particle.c:1390), so it moves particle
 *   axes, positions included, and with them the particles whose position
 *   decides a MixRNG draw (Particle.c:199-217, :131-137); the end-of-race
 *   confetti (MainFrame_RenderFrame.c:524). Stored timestamps: only the
 *   oscillator phases, whose pool the load rebuilt (LOAD_TenStages.c:202,
 *   :503) and MainInit_FinalizeInit clears again right after the hook
 *   (MainInit.c:469), so no live phase spans the pin. Platform: native VSync
 *   emits the callback on the game thread (native_platform.c,
 *   Native_EmitVBlank) and never reads the field; the platform keeps its own
 *   VBlank count. PIN 0.
 * - sdata->frameCounter (V1 control "frameCounter"): +1 per main-loop frame
 *   (MainMain.c:359). Readers: menu, pause, hub, and profile screens only
 *   (230.c:54, MainFrame_RenderFrame.c:475, MainFreeze.c:57, :135, :179,
 *   SelectProfile.c:1165, :1261, 232/AH_*.c, 233/CS_Garage.c:290); no race
 *   simulation or RNG reader. LEAVE.
 * - gGT->frameTimer_VsyncCallback (V1 control "frameTimer"): +1 per emitted
 *   VBlank (MainDrawCb.c:22). Readers: frameTimer_notPaused (written at
 *   MainFrame_RenderFrame.c:1372, never read), the level audio distortion
 *   (HOWL_LevelAudio.c:52), and the load queue's VRAM release delay, which
 *   compares it with a stored timestamp whose 0 means "none"
 *   (LOAD_File.c:222, LOAD_Queue.c:75-77): a reset would release a pending
 *   VRAM file early or, pinned to 0 with a VRAM file finishing before the
 *   next VBlank, never. No race simulation or RNG reader. LEAVE (and not
 *   safe to reset).
 * - gGT->frameTimer_MainFrame_ResetDB: +1 per frame (MainFrame.c:64). Audio
 *   only, each against a stored timestamp: the crash feedback cooldown
 *   (VehPhysCrash.c:259, :521-527; it gates sounds and voicelines, which
 *   draw audioRNG only), the voiceline cooldown (HOWL_Voiceline.c:179), XA
 *   seeks (HOWL_AudioState.c:297, against the XA_PauseFrame stored at
 *   CDSYS.c:813-909), channel durations (HOWL_Channel.c:95, against the
 *   startFrame stored at HOWL_OtherFX.c:122); plus the options menu voice
 *   preview every 25 frames (HOWL_Settings.c:317, no stored timestamp).
 *   LEAVE (a reset would also make bogus deltas).
 * - gGT->clockFrameStart and clockDurationStall: root-counter snapshots
 *   (MainFrame.c:188, MainFrame_RenderFrame.c:1265, :1333); only their
 *   deltas matter, as the per-tick elapsedTimeMS, which is not
 *   boot-relative. A reset would make a bogus first delta. LEAVE.
 * - gGT->frameTimer_notPaused (never read) and gGT->vSync_between_drawSync
 *   (platform, reset every frame, MainMain.c:513). LEAVE.
 * - Already race- or level-relative, nothing to pin: framesInThisLEV and
 *   msInThisLEV (reset at the end of the load, LOAD_TenStages.c:708-709),
 *   elapsedEventTime (0 while the traffic lights run, MainFrame.c:250),
 *   elapsedTimeMS (per frame), trafficLightsTimer, and
 *   sdata->aiCollisionDelayFrameCount (reset by the bot init, BOTS.c:308,
 *   :2999). The retail RNG states are seeded instead (RS-7).
 *
 * The pins are written only by OnFinalizeInitBegin in LAUNCHED, after the
 * load-field verification and before the seeds, so default boot and every
 * load not launched here never see them. The adapter reads both fields back
 * right after it applied them (MainArcadeRaceSetupCore_RecordPinReadback).
 */

/* Mirrors of retail values the steps compare against; the adapter
 * static-asserts each one. */
#define MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE (-1)        /* the idle Loading.stage (namespace_Main.h) */
#define MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL 39      /* enum LevelID main-menu level (namespace_Level.h) */

#define MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS 16u
/* The exact op count of each writing step (see "Writes" above). */
#define MAIN_ARCADE_RACE_SETUP_CORE_MODE_OP_COUNT 4u /* GAME_MODE1, GAME_MODE2, ARCADE_DIFFICULTY, BOOL_DEMO_MODE */
#define MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT \
	(MAIN_ARCADE_RACE_SETUP_CORE_MODE_OP_COUNT + 2u + MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT + 1u)
#define MAIN_ARCADE_RACE_SETUP_CORE_PIN_OP_COUNT 2u /* TIMER, FRAME_TIMER_CONFETTI */
#define MAIN_ARCADE_RACE_SETUP_CORE_BEGIN_OP_COUNT \
	(MAIN_ARCADE_RACE_SETUP_CORE_MODE_OP_COUNT + MAIN_ARCADE_RACE_SETUP_CORE_PIN_OP_COUNT + \
		NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT)
/* The pinned values of the boot-relative counters (RS-17, "Boot-relative
 * counters" above). 0 is no sentinel for either field. */
#define MAIN_ARCADE_RACE_SETUP_CORE_PIN_TIMER 0
#define MAIN_ARCADE_RACE_SETUP_CORE_PIN_FRAME_TIMER_CONFETTI 0
#define MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES 32u

enum MainArcadeRaceSetupStatus
{
	MAIN_ARCADE_RACE_SETUP_IDLE = 0,
	MAIN_ARCADE_RACE_SETUP_ARMED,
	MAIN_ARCADE_RACE_SETUP_LAUNCHED,
	MAIN_ARCADE_RACE_SETUP_SEEDED,
	MAIN_ARCADE_RACE_SETUP_VALIDATED,
	MAIN_ARCADE_RACE_SETUP_FAILED
};

/* Append-only: the codes are logged and reported. */
enum MainArcadeRaceSetupFailure
{
	MAIN_ARCADE_RACE_SETUP_FAILURE_NONE = 0,
	MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN,                 /* the plan refused the config */
	MAIN_ARCADE_RACE_SETUP_FAILURE_BANK,                 /* the bank could not be derived */
	MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION,         /* a Launch precondition failed */
	MAIN_ARCADE_RACE_SETUP_FAILURE_LEVEL_MISMATCH,       /* the loaded levelID is not the plan's */
	MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH, /* numLaps, numPlyrCurrGame, or an owned characterIDs slot */
	MAIN_ARCADE_RACE_SETUP_FAILURE_SEED,                 /* the retail seeds could not be derived */
	MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS,                /* the live facts could not be read or built */
	MAIN_ARCADE_RACE_SETUP_FAILURE_ROSTER,               /* the roster plan or its validation failed */
	MAIN_ARCADE_RACE_SETUP_FAILURE_BOT_SETUP,            /* MainArcadeBotSetup_Plan refused the facts */
	MAIN_ARCADE_RACE_SETUP_FAILURE_STATE,                /* a hook or call in the wrong state */
	MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER,           /* a hook that should act had no game tracker */
	MAIN_ARCADE_RACE_SETUP_FAILURE_OPS                   /* a write list would overflow MAX_OPS (never a partial list) */
};

/* The retail write targets. The adapter maps each to exactly one retail
 * write; value carries the field's value at its retail width and sign. */
enum MainArcadeRaceSetupCoreTarget
{
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NONE = 0,
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1,         /* the gameMode1 word, as its 32-bit pattern */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE2,         /* the gameMode2 word, as its 32-bit pattern */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ARCADE_DIFFICULTY,  /* int */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_BOOL_DEMO_MODE,     /* char, 0 or 1 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_LAPS,           /* s8 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_PLYR_NEXT_GAME, /* u8 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,       /* characterIDs[index] (s16), index 0..7 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_REQUEST_LOAD,       /* MainRaceTrack_RequestLoad(value): the plan's level */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RANDOM_NUMBER,      /* the 16-bit LCG state */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG0,           /* advRng state0 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG1,           /* advRng state1 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_PSX_RAND_SEED,      /* the PSX BIOS rand seed */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_AUDIO_RNG,          /* the audio RNG */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_TIMER,              /* gGT->timer (int), RS-17 */
	MAIN_ARCADE_RACE_SETUP_CORE_TARGET_FRAME_TIMER_CONFETTI /* gGT->frameTimer_Confetti (int), RS-17 */
};

/* The pinned boot-relative counters (RS-17), each at 32 bits. */
struct MainArcadeRaceSetupPins
{
	int32_t timer;              /* gGT->timer */
	int32_t frameTimerConfetti; /* gGT->frameTimer_Confetti */
};

struct MainArcadeRaceSetupCoreOp
{
	uint8_t target; /* MainArcadeRaceSetupCoreTarget */
	uint8_t index;  /* CHARACTER_ID only: the slot; else 0 */
	uint8_t reserved[6];
	int64_t value;  /* the value, exact: signed fields signed, unsigned fields 0..UINT32_MAX */
};

/* What the adapter logs for a step (it logs nothing for NONE). */
enum MainArcadeRaceSetupCoreLog
{
	MAIN_ARCADE_RACE_SETUP_CORE_LOG_NONE = 0,   /* a no-op */
	MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED,    /* moved to status */
	MAIN_ARCADE_RACE_SETUP_CORE_LOG_FAILED,     /* latched FAILED with failure; detail says why */
	MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED,    /* refused in IDLE or FAILED; nothing changed; detail is the call */
	MAIN_ARCADE_RACE_SETUP_CORE_LOG_ARM_REFUSED /* Arm refused (failure PLAN or BANK); still IDLE */
};

/* Disarm only: what happened to the vibration bits saved at Arm. */
enum MainArcadeRaceSetupCoreVibration
{
	MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_UNTOUCHED = 0, /* Launch never wrote the fields */
	MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_RESTORED,      /* the GAME_MODE1 op restores them */
	MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED   /* not idle on the main-menu level */
};

struct MainArcadeRaceSetupCoreOutcome
{
	uint32_t opCount;
	struct MainArcadeRaceSetupCoreOp ops[MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS];
	uint8_t result;         /* the step's return value */
	uint8_t log;            /* MainArcadeRaceSetupCoreLog */
	uint8_t vibration;      /* MainArcadeRaceSetupCoreVibration */
	uint8_t overflowed;     /* 1 once a push found the list full; the step then fails closed (OPS) */
	uint32_t status;        /* MainArcadeRaceSetupStatus after the step */
	uint32_t failure;       /* MainArcadeRaceSetupFailure after the step */
	const char *detail;     /* fixed text for the log; never NULL */
	int32_t botSetupResult; /* MainArcadeBotSetupResult of the failed bot setup, else OK */
	uint32_t savedVibration;                   /* Disarm: the bits saved at Arm */
	struct NativeArcadeRetailRngSeedsV1 seeds; /* SEEDED: the seeds the ops write */
	struct MainArcadeRaceSetupPins pins;   /* SEEDED: the pinned counters the ops write */
};

/* The live values Launch reads. */
struct MainArcadeRaceSetupCoreLaunchView
{
	int32_t loadingStage;       /* Loading.stage */
	uint32_t onBeginAddBits0;   /* Loading.OnBegin.AddBitsConfig0 */
	uint32_t onBeginRemBits0;   /* Loading.OnBegin.RemBitsConfig0 */
	uint32_t onBeginAddBits8;   /* Loading.OnBegin.AddBitsConfig8 */
	uint32_t onBeginRemBits8;   /* Loading.OnBegin.RemBitsConfig8 */
	uint32_t optionsLoaded;     /* boolHasLoadedOptions */
	struct MainArcadeRaceSetupRetailFields fields; /* the owned fields, gameMode1 included */
};

/* The live values the pre-drivers hook reads. Only trackerPresent is read
 * unless MainArcadeRaceSetupCore_HookReadsView says the hook acts. */
struct MainArcadeRaceSetupCoreBeginView
{
	uint8_t trackerPresent;  /* the hook's game tracker is not NULL */
	uint8_t numPlyrCurrGame; /* u8 */
	uint8_t reserved[2];
	struct MainArcadeRaceSetupRetailFields fields;
};

/* The live values the post-drivers hook reads, with the same rule. */
struct MainArcadeRaceSetupCoreDriversView
{
	uint8_t trackerPresent;   /* the hook's game tracker is not NULL */
	uint8_t rosterInputValid; /* the pre-race roster input extraction succeeded */
	uint8_t reserved[2];
	uint32_t gameMode1;
	uint32_t gameMode2;
	struct MainArcadeRaceSetupLiveSnapshot snapshot;
	struct NativeCanonicalDriversRosterInput rosterInput;
};

/* The live values Disarm reads. */
struct MainArcadeRaceSetupCoreDisarmView
{
	int32_t currentLevel; /* the current level */
	int32_t loadingStage; /* Loading.stage */
	uint32_t gameMode1;
};

enum MainArcadeRaceSetupCoreHook
{
	MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN = 0,
	MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED
};

/*
 * The whole setup state, owned by the caller (the adapter keeps one
 * file-scope static instance, outside every saved-state region, never
 * recorded or canonical, RS-11). bank is the post-Arm bank, then the
 * post-seed bank from SEEDED, then the post-setup bank from VALIDATED. seeds
 * and pins are the retail seeds and pinned counters the SEEDED step produced,
 * and seedReadback and pinReadback what the adapter read back from the retail
 * fields right after it applied them.
 */
struct MainArcadeRaceSetupCore
{
	uint32_t status;         /* MainArcadeRaceSetupStatus */
	uint32_t failure;        /* MainArcadeRaceSetupFailure */
	uint32_t savedVibration; /* gameMode1 & HOST_LOCAL_MASK at Arm */
	uint8_t fieldsWritten;   /* 1 once Launch wrote the live fields */
	uint8_t seedReadbackRecorded; /* 1 once RecordSeedReadback stored seedReadback */
	uint8_t pinReadbackRecorded;  /* 1 once RecordPinReadback stored pinReadback */
	uint8_t reserved;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct NativeArcadeRetailRngSeedsV1 seedReadback;
	struct MainArcadeRaceSetupPins pins;
	struct MainArcadeRaceSetupPins pinReadback;
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;
	struct NativeDeterministicRngBankV1 bank;
	struct MainArcadeBotSetupPlan botSetupPlan;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t racePlanDigest[NATIVE_SHA256_DIGEST_BYTES];
};

/* Workspace of the pre-drivers and post-drivers hooks. */
struct MainArcadeRaceSetupCoreScratch
{
	struct NativeDeterministicRngBankV1 seedBank;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validated;
	struct MainArcadeBotSetupPlan botSetupPlan;
	struct NativeDeterministicRngBankV1 bankAfter;
};

/* IDLE, everything zero. NULL is a no-op. */
void MainArcadeRaceSetupCore_Reset(struct MainArcadeRaceSetupCore *core);

/*
 * The steps. Each one zeroes *outcome, decides, updates *core, fills the
 * outcome, and returns outcome->result. With a NULL core, view, scratch, or
 * outcome it returns 0 and touches nothing.
 *
 * Arm (IDLE only) builds the plan and its digest and derives the bank from
 * the config's masterSeed; it saves liveGameMode1's vibration bits. On failure
 * it stays IDLE with failure PLAN or BANK (log ARM_REFUSED) and returns 0.
 * config may be NULL (PLAN).
 */
int MainArcadeRaceSetupCore_Arm(struct MainArcadeRaceSetupCore *core, const struct NativeMatchConfigV1 *config,
	uint32_t liveGameMode1, struct MainArcadeRaceSetupCoreOutcome *outcome);

/*
 * Launch (ARMED only): the preconditions, in order, each failing closed with
 * PRECONDITION and no op: the loading stage is idle; the four pending OnBegin
 * mode words are 0; the options are loaded; no PAUSE bit in gameMode1. Then
 * MainArcadeRaceSetupPlan_Apply on the view's fields (PLAN on failure) and the
 * Launch ops; LAUNCHED, and returns 1.
 */
int MainArcadeRaceSetupCore_Launch(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreLaunchView *view, struct MainArcadeRaceSetupCoreOutcome *outcome);

/* 1 when the hook acts in the current state and so reads the whole view
 * (FINALIZE_INIT_BEGIN in LAUNCHED, DRIVERS_INITIALIZED in SEEDED), else 0
 * (also for NULL): the adapter then fills only trackerPresent. */
int MainArcadeRaceSetupCore_HookReadsView(const struct MainArcadeRaceSetupCore *core, enum MainArcadeRaceSetupCoreHook hook);

/*
 * The pre-drivers hook. In LAUNCHED with a tracker: verifies levelID
 * (LEVEL_MISMATCH), numLaps and numPlyrCurrGame against the plan's numLaps
 * and numPlyrNextGame (2 ARCADE_TWO_CAB, 1 ARCADE_ONE_CAB;
 * LOAD_FIELDS_MISMATCH), and characterIDs[i] for every bit i of the plan's
 * characterWriteMask (LOAD_FIELDS_MISMATCH: slots 0..5 for ARCADE_TWO_CAB,
 * 0..7 for ARCADE_ONE_CAB); re-applies the plan to the view's fields (PLAN);
 * derives the retail seeds from a copy of the bank in scratch (SEED); only then
 * emits its ops (the mode fields, the pinned counters, the seeds), keeps the
 * post-seed bank, and moves to SEEDED. Every failure writes nothing. Returns 1
 * when it moved to SEEDED.
 */
int MainArcadeRaceSetupCore_OnFinalizeInitBegin(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreBeginView *view, struct MainArcadeRaceSetupCoreScratch *scratch,
	struct MainArcadeRaceSetupCoreOutcome *outcome);

/*
 * The post-drivers hook. In SEEDED with a tracker: the plan's mode bits still
 * hold and no cheat bit is set (FACTS); the roster input was extracted
 * (FACTS); MainArcadeRaceSetupFacts_Build (FACTS); MainArcadeRoster_BuildPlan
 * and _ValidateNativeFacts (ROSTER); MainArcadeBotSetup_Plan on the post-seed
 * bank (BOT_SETUP, with botSetupResult). On success it keeps the bot setup
 * plan, the setup facts, and the post-setup bank (MATCH_SETUP drawn 5 times
 * for the seeds plus once per bot: 9 for ARCADE_TWO_CAB, 12 for
 * ARCADE_ONE_CAB, RS-22), and moves to VALIDATED.
 * It never writes a retail field. Returns 1 when it moved to VALIDATED.
 */
int MainArcadeRaceSetupCore_OnDriversInitialized(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreDriversView *view, struct MainArcadeRaceSetupCoreScratch *scratch,
	struct MainArcadeRaceSetupCoreOutcome *outcome);

/*
 * Any state to IDLE (returns 1). When Launch wrote the fields, the vibration
 * bits saved at Arm are restored (one GAME_MODE1 op: gameMode1 with its
 * HOST_LOCAL bits replaced by the saved ones) only on the main-menu level with
 * the loading stage idle and LOADING clear; otherwise nothing is written
 * (NOT_RESTORED), since gameMode1 is canonical control state and must not
 * change under a running race.
 */
int MainArcadeRaceSetupCore_Disarm(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreDisarmView *view, struct MainArcadeRaceSetupCoreOutcome *outcome);

/*
 * The adapter's readback of the retail seed fields, taken right after it
 * applied the SEEDED ops and before anything else runs: randomNumber, advRng
 * state0 and state1, the PSX BIOS rand seed, and audioRNG, each at 32 bits.
 * Accepted once, only in SEEDED (returns 1); otherwise 0 and nothing changes.
 * It decides nothing: the proof compares it with the produced seeds.
 */
int MainArcadeRaceSetupCore_RecordSeedReadback(struct MainArcadeRaceSetupCore *core,
	const struct NativeArcadeRetailRngSeedsV1 *readback);

/* In SEEDED or VALIDATED, once the readback was recorded: the seeds the
 * SEEDED step produced and the values read back. 0 with both outputs
 * untouched otherwise. */
int MainArcadeRaceSetupCore_SeedReadback(const struct MainArcadeRaceSetupCore *core,
	struct NativeArcadeRetailRngSeedsV1 *produced, struct NativeArcadeRetailRngSeedsV1 *readback);

/* The same for the pinned counters (RS-17): gGT->timer and
 * gGT->frameTimer_Confetti as read back right after the SEEDED writes.
 * Accepted once, only in SEEDED (returns 1); otherwise 0 and nothing changes. */
int MainArcadeRaceSetupCore_RecordPinReadback(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupPins *readback);

/* In SEEDED or VALIDATED, once the pin readback was recorded: the pinned
 * values the SEEDED step produced and the values read back. 0 with both
 * outputs untouched otherwise. */
int MainArcadeRaceSetupCore_PinReadback(const struct MainArcadeRaceSetupCore *core,
	struct MainArcadeRaceSetupPins *produced, struct MainArcadeRaceSetupPins *readback);

/* IDLE for NULL. */
enum MainArcadeRaceSetupStatus MainArcadeRaceSetupCore_Status(const struct MainArcadeRaceSetupCore *core);
/* NONE for NULL; else the latched failure (NONE unless FAILED, or IDLE after a refused Arm). */
enum MainArcadeRaceSetupFailure MainArcadeRaceSetupCore_Failure(const struct MainArcadeRaceSetupCore *core);

/* Only when VALIDATED: the config, race plan, locked bot setup plan, and
 * post-setup bank digests. Returns 0 with every output untouched otherwise. */
int MainArcadeRaceSetupCore_Digests(const struct MainArcadeRaceSetupCore *core,
	uint8_t configDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES], uint8_t racePlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t botSetupPlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES], uint8_t bankDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES]);

/* Only when VALIDATED: the validated per-slot setup facts. 0 with *out untouched otherwise. */
int MainArcadeRaceSetupCore_SlotFacts(const struct MainArcadeRaceSetupCore *core, struct MainArcadeBotSetupSourceFacts *out);

/* The post-setup bank when VALIDATED, else NULL. */
const struct NativeDeterministicRngBankV1 *MainArcadeRaceSetupCore_Bank(const struct MainArcadeRaceSetupCore *core);

/* Fixed names for logs and reports ("IDLE", "PLAN", ...); "UNKNOWN" out of range. */
const char *MainArcadeRaceSetupCore_StatusName(enum MainArcadeRaceSetupStatus status);
const char *MainArcadeRaceSetupCore_FailureName(enum MainArcadeRaceSetupFailure failure);

#endif
