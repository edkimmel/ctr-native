#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/*
 * Live roster proof hook (docs/ROSTER_MILESTONE.md section 3.4; R-5b's
 * launcher, which R-5c lets launch from inside the attract demo race; R-6's
 * scripted pads and per-tick digests; and R-6b's race-relative control
 * digest, race tick 0 counters, and tick-log watchdog; OC-3's ONE_CAB
 * profile: the configured profile's scripted pads and the report's profile
 * line, nothing else is profile-specific). Internal native
 * builds only, and dormant unless main.c configured the proof (which also
 * turns on the fixed VBlank pacing): with the proof inactive every
 * entry point returns as its first statement and touches nothing.
 *
 * The per-tick evidence is local only: the V1 state MainMain.c projects for
 * the proof, the drivers candidate extracted here, and the live V4 state the
 * race digest projects each race tick (MAIN/MainArcadeRaceDigest.h, Task 8
 * race plan LR-10, slice LR-S4) go into the proof's report and nowhere else
 * (no scheduler, no recording, no saved state).
 *
 * LR-S2 (a)'s hold (--arcade-roster-proof-hold): on the frame logged as race
 * tick HOLD_TICK, MainArcadeRosterProof_Frame (after GameLogic, before the
 * frame's VBlanks) blocks in the stall hold loop, MainArcadeRaceHold_Run,
 * for HOLD_PERIODS tick periods of wall time, and keeps the loop's
 * measurements and the VSync counter around it as the report's hold
 * evidence.
 *
 * LR-S2 (b)'s autopilot (--arcade-roster-proof-autopilot): after each logged
 * race tick, MainArcadeRosterProof_EndFrame reads the steering facts of
 * players 0 and 1 (kart position and heading, and the level's restart
 * points, gGT->level1->ptr_restart_points) and keeps the pure steering
 * decision (include/platform/native_arcade_link_autopilot.h) as the buttons
 * BeginFrame installs for the next race tick. The facts are read here only,
 * internal-only and non-canonical: no restart point value enters a digest, a
 * tick line, or the report. Only the installed pads reach the game. The
 * finish evidence (each player's finish tick and the END_OF_RACE tick) goes
 * to the process log.
 *
 * Unity-included after the 230 overlay sources and the arcade-link hook,
 * because it reads and closes the retail title (MM_Title_*, MM_MENU_MAIN)
 * and asks the arcade-link hook for its menu-ready condition; and after
 * MainArcadeRaceSetup, which it arms and launches.
 */

#include <common.h>

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeLink.h"
#include "MAIN/MainArcadeRaceDigest.h"
#include "MAIN/MainArcadeRaceHold.h"
#include "MAIN/MainArcadeRaceHoldCore.h"
#include "MAIN/MainArcadeRaceSetup.h"
#include "MAIN/MainArcadeRosterProof.h"
#include "MAIN/MainCanonicalDrivers.h"
#include "platform/native_arcade_link_autopilot.h"
#include "platform/native_arcade_roster_proof.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_canonical_drivers_detailed.h"
#include "platform/native_canonical_state.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_sha256.h"

#include <time.h>

#define MAIN_ARCADE_ROSTER_PROOF_LOG "[CTR Native] arcade roster proof: "

_Static_assert(NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT == MAIN_ARCADE_BOT_SETUP_SLOT_COUNT,
	"the proof report holds exactly the bot setup's slots");
_Static_assert(MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES == NATIVE_SHA256_DIGEST_BYTES,
	"the proof report digests are the race setup's digests");
_Static_assert(NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT == PLATFORM_INPUT_PAD_COUNT,
	"the scripted pads cover every host pad");
_Static_assert(NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT == NATIVE_CANONICAL_INPUT_PAD_COUNT,
	"the scripted pads are the canonical input pads");

enum MainArcadeRosterProofPhase
{
	MAIN_ARCADE_ROSTER_PROOF_WAIT_MENU = 0, /* waiting for the menu-ready frame */
	MAIN_ARCADE_ROSTER_PROOF_DWELL,         /* menu ready; counting the dwell */
	MAIN_ARCADE_ROSTER_PROOF_WAIT_WINDOW,   /* dwell over; waiting for a launch window */
	MAIN_ARCADE_ROSTER_PROOF_RUNNING,       /* launched; polling the setup */
	MAIN_ARCADE_ROSTER_PROOF_VALIDATED,     /* validated; waiting for race tick 0, then logging the ticks */
	MAIN_ARCADE_ROSTER_PROOF_DONE           /* report written, exit requested */
};

struct MainArcadeRosterProofState
{
	uint32_t phase;
	uint32_t tick; /* proof ticks: calls since the proof became active */
	uint32_t dwellLeft;
	uint32_t menuReadyTick;
	uint32_t demoRaceTick;
	uint32_t waitStartTick;
	uint32_t launchTick;
	uint32_t launchWindow;
	uint32_t validatedTick;
	uint32_t frameTick;        /* the proof tick of the current frame */
	uint32_t raceTick;         /* the next race tick to log; TICK_NONE before race tick 0 */
	uint32_t raceTickZeroTick; /* the proof tick of race tick 0 */
	uint32_t countersValid;    /* 1 once raceTickZeroCounters holds race tick 0's counters */
	struct NativeArcadeRosterProofCounters raceTickZeroCounters;
	uint32_t launchCountersValid; /* 1 once launchCounters holds the launch tick's counters */
	struct NativeArcadeRosterProofCounters launchCounters;
	uint32_t holdDone; /* 1 once the LR-S2 (a) hold ran */
	struct NativeArcadeRosterProofHold hold;
};

static struct MainArcadeRosterProofState s_mainArcadeRosterProof = {
	MAIN_ARCADE_ROSTER_PROOF_WAIT_MENU, 0u, 0u,
	NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE,
	NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE,
	0u, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, 0u, {0, 0, 0}, 0u, {0, 0, 0}, 0u, {0}};

/* The drivers digest workspace: far too large for the game stack, so
 * file-scope static. Local only; only the digest leaves it. */
struct MainArcadeRosterProofDriversScratch
{
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate candidate;
	struct NativeCanonicalDriversDetailedV1 detailed;
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
};

static struct MainArcadeRosterProofDriversScratch s_mainArcadeRosterProofDrivers;

/* LR-S2 (b): the autopilot drives players 0 and 1 (pads 0 and 1). */
#define MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS 2u
/* The kart aims this many restart points beyond its target. */
#define MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_LOOKAHEAD 1u
/* posCurr is in 1/256 world units (VehLap.c reads it the same way). */
#define MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_WORLD_SHIFT 8
/* A restart point link at or above the count (0xFF: none) is no link. */
#define MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_POINTS 0xFF

/* The autopilot's state: local only, never in a digest or the report. */
struct MainArcadeRosterProofAutopilotState
{
	uint32_t started;           /* 1 once the ticks below were initialized */
	uint32_t endOfRaceTick;     /* the first race tick showing END_OF_RACE; TICK_NONE before */
	uint32_t held[MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS];        /* buttons for the next race tick */
	uint32_t targetValid[MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS]; /* 1 once target holds a restart point */
	uint32_t target[MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS];      /* the restart point the kart heads for */
	uint32_t finishTick[MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS];  /* the first race tick showing the player finished */
};

static struct MainArcadeRosterProofAutopilotState s_mainArcadeRosterProofAutopilot;

static void MainArcadeRosterProof_CopyName(char out[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES], const char *name)
{
	size_t i;

	memset(out, 0, NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES);
	for (i = 0; (i + 1u < NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES) && (name[i] != '\0'); i++)
	{
		out[i] = name[i];
	}
}

/* The seed readback of the setup: 1 with *stored filled and *match set when
 * the setup can report it, else 0. */
static int MainArcadeRosterProof_ReadSeeds(struct NativeArcadeRetailRngSeedsV1 *stored, uint8_t *match)
{
	struct NativeArcadeRetailRngSeedsV1 produced;

	if (!MainArcadeRaceSetup_SeedReadback(&produced, stored))
	{
		return 0;
	}
	*match = NativeArcadeRosterProof_SeedsMatch(&produced, stored) ? 1u : 0u;
	return 1;
}

/* The pin readback of the setup (RS-17, LR-8), the same way. */
static int MainArcadeRosterProof_ReadPins(struct NativeArcadeRosterProofPins *stored, uint8_t *match)
{
	struct MainArcadeRaceSetupPins setupProduced;
	struct MainArcadeRaceSetupPins setupStored;
	struct NativeArcadeRosterProofPins produced;

	if (!MainArcadeRaceSetup_PinReadback(&setupProduced, &setupStored))
	{
		return 0;
	}
	produced.timer = setupProduced.timer;
	produced.frameTimerConfetti = setupProduced.frameTimerConfetti;
	produced.rcntTotalUnits = setupProduced.rcntTotalUnits;
	produced.clockFrameStart = setupProduced.clockFrameStart;
	stored->timer = setupStored.timer;
	stored->frameTimerConfetti = setupStored.frameTimerConfetti;
	stored->rcntTotalUnits = setupStored.rcntTotalUnits;
	stored->clockFrameStart = setupStored.clockFrameStart;
	*match = NativeArcadeRosterProof_PinsMatch(&produced, stored) ? 1u : 0u;
	return 1;
}

/* Writes the report for `requested` (PASS only with valid evidence,
 * NativeArcadeRosterProof_FinalResult), logs it, records the exit code, and
 * requests the exit. */
static void MainArcadeRosterProof_Finish(uint32_t requested)
{
	struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	struct NativeArcadeRosterProofReport report;
	struct MainArcadeBotSetupSourceFacts facts;
	const enum MainArcadeRaceSetupStatus status = MainArcadeRaceSetup_Status();
	const enum MainArcadeRaceSetupFailure failure = MainArcadeRaceSetup_Failure();
	const struct NativeMatchConfigV1 *config = NativeArcadeRosterProof_Config();
	uint32_t result;
	int exitCode;

	memset(&report, 0, sizeof(report));
	/* The profile of the configured config, the one that ran. */
	report.profile = (config != NULL) ? config->profile : 0u;
	report.setupStatus = (uint32_t)status;
	report.setupFailure = (uint32_t)failure;
	MainArcadeRosterProof_CopyName(report.setupStatusName, MainArcadeRaceSetup_StatusName(status));
	MainArcadeRosterProof_CopyName(report.setupFailureName, MainArcadeRaceSetup_FailureName(failure));
	report.seed = NativeArcadeRosterProof_Seed();
	report.dwellTicks = NativeArcadeRosterProof_Dwell();
	report.menuReadyTick = state->menuReadyTick;
	report.demoRaceTick = state->demoRaceTick;
	report.launchTick = state->launchTick;
	report.launchWindow = state->launchWindow;
	report.validatedTick = state->validatedTick;
	report.raceTickZeroTick = state->raceTickZeroTick;
	report.ticksRequested = NativeArcadeRosterProof_Ticks();
	report.tickLineCount = NativeArcadeRosterProof_TickCount();
	report.countersValid = (state->countersValid != 0u) ? 1u : 0u;
	report.raceTickZeroCounters = state->raceTickZeroCounters;
	report.launchCountersValid = (state->launchCountersValid != 0u) ? 1u : 0u;
	report.launchCounters = state->launchCounters;
	report.digestsValid = MainArcadeRaceSetup_Digests(report.configDigest, report.racePlanDigest,
		report.botSetupPlanDigest, report.bankDigest) ? 1u : 0u;
	if (MainArcadeRaceSetup_SlotFacts(&facts))
	{
		for (uint32_t i = 0; (i < facts.factCount) && (i < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT); i++)
		{
			const struct MainArcadeBotSetupSourceSlot *fact = &facts.facts[i];
			struct NativeArcadeRosterProofSlotLine *line;

			if (fact->stableSlot >= NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT)
			{
				continue;
			}
			line = &report.slots[fact->stableSlot];
			line->present = fact->present;
			line->role = fact->role;
			line->characterID = fact->characterID;
			line->difficulty = fact->difficulty;
			line->spawnOrder = fact->spawnOrder;
			line->navPathIndex = fact->navPathIndex;
			line->accelerationOrder = fact->accelerationOrder;
		}
		report.slotsValid = 1u;
	}
	report.seedValid = MainArcadeRosterProof_ReadSeeds(&report.seedStored, &report.seedMatch) ? 1u : 0u;
	report.pinValid = MainArcadeRosterProof_ReadPins(&report.pinStored, &report.pinMatch) ? 1u : 0u;
	report.holdRequested = (NativeArcadeRosterProof_Hold() != 0u) ? 1u : 0u;
	report.holdDone = (state->holdDone != 0u) ? 1u : 0u;
	report.hold = state->hold;
	result = NativeArcadeRosterProof_FinalResult(requested, &report);
	report.result = result;
	exitCode = (int)result;

	if (!NativeArcadeRosterProof_WriteReport(&report))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "could not write the report to %s\n", NativeArcadeRosterProof_LogPath());
		if (exitCode == (int)NATIVE_ARCADE_ROSTER_PROOF_PASS)
		{
			exitCode = (int)NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED;
		}
	}
	Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "%s at tick %u (setup %s, failure %s, %u race ticks logged); exit code %d\n",
		NativeArcadeRosterProof_ResultName(result), (unsigned)state->tick, MainArcadeRaceSetup_StatusName(status),
		MainArcadeRaceSetup_FailureName(failure), (unsigned)NativeArcadeRosterProof_TickCount(), exitCode);
	state->phase = MAIN_ARCADE_ROSTER_PROOF_DONE;
	NativeArcadeRosterProof_RecordExitCode(exitCode);
	Platform_RequestExit(exitCode);
}

/*
 * The attract demo race is running: off the main-menu level with the demo
 * flag set (MM_Title.c, the MM_EXIT_ROUTE_DEMO case), fully loaded (no load
 * stage, no LOADING bit), and not a cutscene or menu level. The first idle
 * timeout of a boot plays the intro cutscene instead, which is not a window.
 */
static int MainArcadeRosterProof_DemoRaceRunning(const struct GameTracker *gGT)
{
	return (gGT->levelID != MAIN_MENU_LEVEL) && (gGT->boolDemoMode != 0) && (sdata->Loading.stage == LOAD_IDLE) &&
	       ((gGT->gameMode1 & (LOADING | GAME_CUTSCENE | MAIN_MENU)) == 0);
}

/*
 * Leaves the title the way the retail demo route does (MM_Title.c, the
 * MM_EXIT_ROUTE_DEMO case and its shared exit at LAB_800abfc0): reset the
 * title camera, kill the title thread, and hide the main-menu box. The
 * retail route runs inside the box's funcPtr, so RECTMENU_ProcessState
 * closes the hidden box at its end; here the load is already requested, so
 * RECTMENU_ProcessState does not run again until the race is loaded, and
 * the hook performs that close step itself.
 */
static void MainArcadeRosterProof_LeaveTitle(void)
{
	MM_Title_CameraReset();
	MM_Title_KillThread();
	RECTMENU_Hide(&MM_MENU_MAIN);
	if (sdata->ptrDesiredMenu == &MM_MENU_MAIN)
	{
		sdata->ptrDesiredMenu = NULL;
	}
	if (sdata->ptrActiveMenu == &MM_MENU_MAIN)
	{
		sdata->ptrActiveMenu = NULL;
	}
}

/*
 * Launches from the first launch window: the title menu-ready window (then
 * the title is left as the retail demo route leaves it), or the running demo
 * race (then nothing else is closed: the race keeps running until the
 * requested load starts, as it does under the arcade-link screens). Returns 1
 * when it launched or finished; 0 when no window is open yet.
 */
static int MainArcadeRosterProof_TryLaunch(struct GameTracker *gGT, struct GamepadSystem *gGS)
{
	struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	uint32_t window;

	if (MainArcadeLink_TitleMenuReady(gGT, gGS))
	{
		window = NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE;
	}
	else if (MainArcadeRosterProof_DemoRaceRunning(gGT))
	{
		window = NATIVE_ARCADE_ROSTER_PROOF_WINDOW_DEMO_RACE;
	}
	else
	{
		return 0;
	}
	/* The boot-relative counters as this boot history left them, before the
	 * setup pins gGT->timer and gGT->frameTimer_Confetti at race init (RS-17
	 * evidence). */
	state->launchCounters.timer = (int32_t)gGT->timer;
	state->launchCounters.frameCounter = (int32_t)sdata->frameCounter;
	state->launchCounters.frameTimer = (int32_t)gGT->frameTimer_VsyncCallback;
	state->launchCounters.frameTimerConfetti = (int32_t)gGT->frameTimer_Confetti;
	state->launchCountersValid = 1u;
	if (!MainArcadeRaceSetup_Arm(NativeArcadeRosterProof_Config()))
	{
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_ARM_FAILED);
		return 1;
	}
	state->launchTick = state->tick;
	state->launchWindow = window;
	if (!MainArcadeRaceSetup_Launch())
	{
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
		return 1;
	}
	if (window == NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE)
	{
		MainArcadeRosterProof_LeaveTitle();
	}
	Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "launched %s at tick %u from the %s (menu ready at tick %u, dwell %u)\n",
		NativeArcadeRosterProof_ProfileName(NativeArcadeRosterProof_Profile()), (unsigned)state->tick,
		NativeArcadeRosterProof_LaunchWindowName(window), (unsigned)state->menuReadyTick,
		(unsigned)NativeArcadeRosterProof_Dwell());
	state->phase = MAIN_ARCADE_ROSTER_PROOF_RUNNING;
	return 1;
}

/* The hold's step: held until HOLD_PERIODS full tick periods passed. */
static int MainArcadeRosterProof_HoldStep(void *context, uint32_t periods, int newPeriod)
{
	(void)context;
	(void)newPeriod;
	return periods < NATIVE_ARCADE_ROSTER_PROOF_HOLD_PERIODS;
}

/* A C11 timespec_get(TIME_UTC) read in microseconds: the hold's independent
 * clock (not the platform clock the hold loop uses). Returns 0 on failure. */
static int MainArcadeRosterProof_IndependentUs(uint64_t *us)
{
	struct timespec now;

	if ((timespec_get(&now, TIME_UTC) != TIME_UTC) || (now.tv_sec < 0) || (now.tv_nsec < 0))
	{
		return 0;
	}
	*us = ((uint64_t)now.tv_sec * UINT64_C(1000000)) + ((uint64_t)now.tv_nsec / UINT64_C(1000));
	return 1;
}

/*
 * LR-S2 (a): holds this frame, after its GameLogic and before its VBlanks,
 * in the stall hold loop, and keeps the evidence. The VSync counter is read
 * around the hold (the loop must not move it), and so is an independent
 * clock (the loop measures itself on the platform clock); the hold loop
 * itself never sees game state.
 */
static void MainArcadeRosterProof_Hold(const struct GameTracker *gGT)
{
	struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	struct MainArcadeRaceHoldResult result;
	uint64_t independentBeginUs = 0u;
	uint64_t independentEndUs = 0u;
	int independentBegun;

	state->hold.raceTick = state->raceTick;
	state->hold.expectedUs = MainArcadeRaceHoldCore_PeriodsToUs(NATIVE_ARCADE_ROSTER_PROOF_HOLD_PERIODS);
	state->hold.vsyncEntry = (int32_t)gGT->frameTimer_VsyncCallback;
	Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "holding at race tick %u for %u tick periods (tick %u)\n",
		(unsigned)state->raceTick, (unsigned)NATIVE_ARCADE_ROSTER_PROOF_HOLD_PERIODS, (unsigned)state->tick);
	independentBegun = MainArcadeRosterProof_IndependentUs(&independentBeginUs);
	MainArcadeRaceHold_Run(MainArcadeRosterProof_HoldStep, NULL, &result);
	if (independentBegun && MainArcadeRosterProof_IndependentUs(&independentEndUs) && (independentEndUs >= independentBeginUs))
	{
		state->hold.independentUs = independentEndUs - independentBeginUs;
		state->hold.independentValid = 1u;
	}
	state->hold.vsyncExit = (int32_t)gGT->frameTimer_VsyncCallback;
	state->hold.periods = result.periods;
	state->hold.wallUs = result.wallUs;
	state->hold.pumps = result.pumps;
	state->hold.minPeriodPumps = result.minPeriodPumps;
	state->hold.bannersDue = result.bannersDue;
	state->hold.bannersPresented = result.bannersPresented;
	state->holdDone = 1u;
	Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "hold ended after %u periods, %llu us (independent %llu us, valid %u; expected %llu us), "
		"%u pumps, min %u per period, %u of %u banners presented, frameTimer entry %ld exit %ld\n",
		(unsigned)result.periods, (unsigned long long)result.wallUs, (unsigned long long)state->hold.independentUs,
		(unsigned)state->hold.independentValid, (unsigned long long)state->hold.expectedUs,
		(unsigned)result.pumps, (unsigned)result.minPeriodPumps, (unsigned)result.bannersPresented,
		(unsigned)result.bannersDue, (long)state->hold.vsyncEntry, (long)state->hold.vsyncExit);
}

void MainArcadeRosterProof_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)
{
	struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	enum MainArcadeRaceSetupStatus status;

	/* Default behaviour guarantee: no proof option, no side effect. */
	if (!NativeArcadeRosterProof_Active())
	{
		return;
	}
	if ((gGT == NULL) || (gGS == NULL) || (state->phase == MAIN_ARCADE_ROSTER_PROOF_DONE))
	{
		return;
	}
	state->frameTick = state->tick;

	/* Evidence: the first tick the attract demo race is seen running before launch. */
	if ((state->demoRaceTick == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) && (state->phase < MAIN_ARCADE_ROSTER_PROOF_RUNNING) &&
	    MainArcadeRosterProof_DemoRaceRunning(gGT))
	{
		state->demoRaceTick = state->tick;
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "demo race running at tick %u (level %d)\n", (unsigned)state->tick,
			(int)gGT->levelID);
	}

	switch (state->phase)
	{
	case MAIN_ARCADE_ROSTER_PROOF_WAIT_MENU:
		if (MainArcadeLink_TitleMenuReady(gGT, gGS))
		{
			state->menuReadyTick = state->tick;
			state->dwellLeft = NativeArcadeRosterProof_Dwell();
			state->phase = MAIN_ARCADE_ROSTER_PROOF_DWELL;
			Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "menu ready at tick %u; dwell %u\n", (unsigned)state->tick,
				(unsigned)state->dwellLeft);
		}
		else if (state->tick >= NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT_TICKS)
		{
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT);
			return;
		}
		if (state->phase != MAIN_ARCADE_ROSTER_PROOF_DWELL)
		{
			break;
		}
		/* fall through: a zero dwell launches on the menu-ready frame itself */
	case MAIN_ARCADE_ROSTER_PROOF_DWELL:
		if (state->dwellLeft != 0u)
		{
			state->dwellLeft--;
			break;
		}
		state->waitStartTick = state->tick;
		state->phase = MAIN_ARCADE_ROSTER_PROOF_WAIT_WINDOW;
		if (MainArcadeRosterProof_TryLaunch(gGT, gGS))
		{
			break;
		}
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "dwell over at tick %u outside a launch window; waiting\n", (unsigned)state->tick);
		break;
	case MAIN_ARCADE_ROSTER_PROOF_WAIT_WINDOW:
		if (MainArcadeRosterProof_TryLaunch(gGT, gGS))
		{
			break;
		}
		if ((state->tick - state->waitStartTick) >= NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_WAIT_TIMEOUT_TICKS)
		{
			Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "no launch window within %u ticks of the dwell\n",
				(unsigned)NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_WAIT_TIMEOUT_TICKS);
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED);
			return;
		}
		break;
	case MAIN_ARCADE_ROSTER_PROOF_RUNNING:
		status = MainArcadeRaceSetup_Status();
		if (status == MAIN_ARCADE_RACE_SETUP_FAILED)
		{
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
			return;
		}
		if (status == MAIN_ARCADE_RACE_SETUP_VALIDATED)
		{
			struct NativeArcadeRetailRngSeedsV1 stored;
			struct NativeArcadeRosterProofPins pinsStored;
			uint8_t match = 0u;
			uint8_t pinMatch = 0u;

			/* Pin the adapter's field mapping: the seeds and the pinned
			 * counters as stored must be what the setup produced. */
			if (!MainArcadeRosterProof_ReadSeeds(&stored, &match) || !MainArcadeRosterProof_ReadPins(&pinsStored, &pinMatch))
			{
				MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
				return;
			}
			if (match == 0u)
			{
				MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH);
				return;
			}
			if (pinMatch == 0u)
			{
				MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH);
				return;
			}
			state->validatedTick = state->tick;
			state->phase = MAIN_ARCADE_ROSTER_PROOF_VALIDATED;
			Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "validated at tick %u\n", (unsigned)state->tick);
		}
		else if ((state->tick - state->launchTick) >= NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT_TICKS)
		{
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT);
			return;
		}
		break;
	case MAIN_ARCADE_ROSTER_PROOF_VALIDATED:
		if (MainArcadeRaceSetup_Status() != MAIN_ARCADE_RACE_SETUP_VALIDATED)
		{
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
			return;
		}
		/* MainArcadeRosterProof_EndFrame logs the ticks and reports PASS. */
		if ((state->raceTick == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) &&
		    ((state->tick - state->validatedTick) >= NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT_TICKS))
		{
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT);
			return;
		}
		/* After race tick 0: every requested tick must be logged in time. */
		if ((state->raceTick != NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) &&
		    ((state->tick - state->raceTickZeroTick) >=
		     (NativeArcadeRosterProof_Ticks() + NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_SLACK_TICKS)))
		{
			Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "only %u of %u race ticks logged %u ticks after race tick 0\n",
				(unsigned)NativeArcadeRosterProof_TickCount(), (unsigned)NativeArcadeRosterProof_Ticks(),
				(unsigned)(state->tick - state->raceTickZeroTick));
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_TIMEOUT);
			return;
		}
		/* LR-S2 (a): the hold, on the frame that will be logged as HOLD_TICK. */
		if ((NativeArcadeRosterProof_Hold() != 0u) && (state->holdDone == 0u) &&
		    (state->raceTick == NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK))
		{
			MainArcadeRosterProof_Hold(gGT);
		}
		break;
	default:
		break;
	}
	state->tick++;
}

/* Installs the configured profile's scripted pads for raceTick (TICK_NONE:
 * neutral). With the autopilot option, the autopilot's buttons replace pads
 * 0 and 1 on race ticks. */
static int MainArcadeRosterProof_InstallPads(uint32_t raceTick)
{
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];

	NativeArcadeRosterProof_ScriptedPads(NativeArcadeRosterProof_Profile(), raceTick, pads);
	/* LR-S2 (b): players 0 and 1 run on the autopilot's buttons instead of
	 * the pattern (active low: a held button clears its bit). */
	if ((raceTick != NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) && (NativeArcadeRosterProof_Autopilot() != 0u))
	{
		for (uint32_t pad = 0; pad < MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS; pad++)
		{
			const uint32_t word = NATIVE_ARCADE_ROSTER_PROOF_BUTTONS_NONE & ~s_mainArcadeRosterProofAutopilot.held[pad];

			pads[pad].buttons[0] = (uint8_t)(word & 0xFFu);
			pads[pad].buttons[1] = (uint8_t)(word >> 8);
		}
	}
	memset(snapshots, 0, sizeof(snapshots));
	for (uint32_t pad = 0; pad < NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT; pad++)
	{
		snapshots[pad].status = pads[pad].status;
		snapshots[pad].id = pads[pad].id;
		snapshots[pad].buttons[0] = pads[pad].buttons[0];
		snapshots[pad].buttons[1] = pads[pad].buttons[1];
		for (uint32_t axis = 0; axis < 4u; axis++)
		{
			snapshots[pad].analog[axis] = pads[pad].analog[axis];
		}
		snapshots[pad].connected = pads[pad].connected;
	}
	return Platform_InputInstallPadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT;
}

int MainArcadeRosterProof_Start(void)
{
	/* Default behaviour guarantee: no proof option, no side effect. */
	if (!NativeArcadeRosterProof_Active())
	{
		return 1;
	}
	return MainArcadeRosterProof_InstallPads(NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE);
}

int MainArcadeRosterProof_BeginFrame(void)
{
	const struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;

	/* Default behaviour guarantee: no proof option, no side effect. */
	if (!NativeArcadeRosterProof_Active())
	{
		return 0;
	}
	/* Race tick n >= 1 runs on the pattern for n (with the autopilot option,
	 * pads 0 and 1 run on the autopilot's buttons instead); every earlier
	 * frame, race tick 0's included, and every frame after the report is
	 * neutral. A failed install is caught by the input digest (the frozen
	 * pads). */
	(void)MainArcadeRosterProof_InstallPads(
		(state->phase == MAIN_ARCADE_ROSTER_PROOF_VALIDATED) ? state->raceTick : NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE);
	return 1;
}

/* The index of a V1 domain in domainDigests; the domain count if absent. */
static uint32_t MainArcadeRosterProof_DomainIndex(uint32_t domain)
{
	uint32_t index = 0;

	while ((index < NATIVE_CANONICAL_DOMAIN_COUNT) && (NativeCanonicalDomainOrder[index] != domain))
	{
		index++;
	}
	return index;
}

/*
 * The drivers digest of the extracted candidate: a detailed record with every
 * group of the candidate and each Physics group at its exact zero value (the
 * detailed validator accepts it: null-free zero quad indices, terrain 0, no
 * step flags), encoded by the canonical encoder and hashed with SHA-256.
 */
static int MainArcadeRosterProof_DriversDigest(uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	struct MainArcadeRosterProofDriversScratch *scratch = &s_mainArcadeRosterProofDrivers;
	const struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate *candidate = &scratch->candidate;
	struct NativeCodecWriter writer;
	struct NativeSha256 sha;

	NativeCanonicalDriversDetailedV1_Init(&scratch->detailed);
	scratch->detailed.prelude = candidate->roster.prelude;
	for (uint32_t slot = 0; slot < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; slot++)
	{
		struct NativeCanonicalDriverSlotV1 *target = &scratch->detailed.slots[slot];

		target->meta = candidate->meta[slot];
		target->race = candidate->race[slot];
		target->dynamics = candidate->dynamics[slot];
		target->active = candidate->active[slot];
		target->bot = candidate->bot[slot];
		target->pendingDamage = candidate->pendingDamage[slot];
		/* target->physics stays at the zero value Init gave it. */
	}
	NativeCodecWriter_Init(&writer, scratch->stream, sizeof(scratch->stream), NULL);
	if (!NativeCanonicalDriversDetailedV1_Encode(&writer, &scratch->detailed) ||
	    (NativeCodecWriter_Size(&writer) != sizeof(scratch->stream)))
	{
		return 0;
	}
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, scratch->stream, sizeof(scratch->stream));
	NativeSha256_Final(&sha, digest);
	return 1;
}

/*
 * LR-S4: the live V4 state of race tick state->raceTick, projected through
 * the race digest (MAIN/MainArcadeRaceDigest.h) from this frame's game state,
 * the frozen pads of the frame's V1 state (the pads GameLogic read), the
 * setup's post-setup bank, and the racing overlay's mine pool. Race tick 0
 * resets the runtime and takes the race-relative base. Fills the line's v4
 * fields; 0 (logged) on a projection failure.
 */
static int MainArcadeRosterProof_ProjectV4(const struct GameTracker *gGT, const struct NativeCanonicalStateV1 *frameState,
	struct NativeArcadeRosterProofTickLine *line)
{
	const struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	struct MainArcadeRaceDigestSources sources;
	struct MainArcadeRaceDigestTick tick;

	sources.gGT = gGT;
	sources.sourceData = sdata;
	sources.mineSource = &D231;
	sources.config = NativeArcadeRosterProof_Config();
	sources.bank = MainArcadeRaceSetup_Bank();
	sources.input = &frameState->input;
	if (!MainArcadeRaceDigest_Project(state->raceTick, &sources, &tick))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "the V4 projection failed at race tick %u (%s, runtime reason %u)\n",
			(unsigned)state->raceTick, MainArcadeRaceDigest_FailureName(MainArcadeRaceDigest_Failure()),
			(unsigned)MainArcadeRaceDigest_RuntimeFailure());
		return 0;
	}
	/* The domain digests by name, whatever the canonical domain order. */
	if (!NativeArcadeRosterProof_SetTickLineV4(line, tick.combinedDigest, tick.domainDigests))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "the V4 domain digests of race tick %u do not map to the tick line\n",
			(unsigned)state->raceTick);
		return 0;
	}
	return 1;
}

/* The restart point after index (forward); index itself when it has no valid link. */
static uint32_t MainArcadeRosterProof_AutopilotNext(const struct Level *level, uint32_t count, uint32_t index)
{
	const uint32_t next = level->ptr_restart_points[index].nextIndex_forward;

	return (next < count) ? next : index;
}

/* The restart point nearest to the kart (x, y, z in world units). */
static uint32_t MainArcadeRosterProof_AutopilotNearest(const struct Level *level, uint32_t count, int32_t x, int32_t y, int32_t z)
{
	uint32_t nearest = 0u;
	int64_t nearestDistance = INT64_MAX;

	for (uint32_t index = 0; index < count; index++)
	{
		const int64_t dx = (int64_t)level->ptr_restart_points[index].pos.x - x;
		const int64_t dy = (int64_t)level->ptr_restart_points[index].pos.y - y;
		const int64_t dz = (int64_t)level->ptr_restart_points[index].pos.z - z;
		const int64_t distance = (dx * dx) + (dy * dy) + (dz * dz);

		if (distance < nearestDistance)
		{
			nearest = index;
			nearestDistance = distance;
		}
	}
	return nearest;
}

/* One player's steering buttons for the next race tick, from this tick's facts. */
static uint32_t MainArcadeRosterProof_AutopilotSteer(const struct Level *level, uint32_t count, const struct Driver *driver,
	uint32_t player)
{
	struct MainArcadeRosterProofAutopilotState *autopilot = &s_mainArcadeRosterProofAutopilot;
	struct NativeArcadeLinkAutopilotPassFacts pass;
	struct NativeArcadeLinkAutopilotSteerFacts steer;
	const int32_t kartX = (int32_t)CTR_MipsSra(driver->posCurr.x, MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_WORLD_SHIFT);
	const int32_t kartY = (int32_t)CTR_MipsSra(driver->posCurr.y, MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_WORLD_SHIFT);
	const int32_t kartZ = (int32_t)CTR_MipsSra(driver->posCurr.z, MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_WORLD_SHIFT);
	uint32_t target;
	uint32_t aim;

	if ((autopilot->targetValid[player] == 0u) || (autopilot->target[player] >= count))
	{
		autopilot->target[player] = MainArcadeRosterProof_AutopilotNearest(level, count, kartX, kartY, kartZ);
		autopilot->targetValid[player] = 1u;
	}
	target = autopilot->target[player];
	/* Move the target on past every restart point the kart has passed. */
	for (uint32_t guard = 0; guard < count; guard++)
	{
		const uint32_t previous = level->ptr_restart_points[target].nextIndex_backward;
		const uint32_t from = (previous < count) ? previous : target;

		pass.kartX = kartX;
		pass.kartZ = kartZ;
		pass.pointX = level->ptr_restart_points[target].pos.x;
		pass.pointZ = level->ptr_restart_points[target].pos.z;
		pass.previousX = level->ptr_restart_points[from].pos.x;
		pass.previousZ = level->ptr_restart_points[from].pos.z;
		if (!NativeArcadeLinkAutopilot_Passed(&pass))
		{
			break;
		}
		target = MainArcadeRosterProof_AutopilotNext(level, count, target);
	}
	autopilot->target[player] = target;
	aim = target;
	for (uint32_t step = 0; step < MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_LOOKAHEAD; step++)
	{
		aim = MainArcadeRosterProof_AutopilotNext(level, count, aim);
	}
	steer.kartX = kartX;
	steer.kartZ = kartZ;
	steer.heading = (int32_t)driver->angle;
	steer.aimX = level->ptr_restart_points[aim].pos.x;
	steer.aimZ = level->ptr_restart_points[aim].pos.z;
	return NativeArcadeLinkAutopilot_Steer(&steer);
}

/* A race tick for the log: the tick, or -1 when never reached. */
static long MainArcadeRosterProof_AutopilotLogTick(uint32_t tick)
{
	return (tick == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) ? -1L : (long)tick;
}

/*
 * LR-S2 (b): after race tick raceTick was simulated, notes the finish
 * evidence and forms the autopilot's buttons for the next race tick. Reads
 * only; nothing here enters a digest, a tick line, or the report.
 */
static void MainArcadeRosterProof_AutopilotStep(const struct GameTracker *gGT, uint32_t raceTick)
{
	struct MainArcadeRosterProofAutopilotState *autopilot = &s_mainArcadeRosterProofAutopilot;
	const struct Level *level = gGT->level1;
	uint32_t count = 0u;

	if (autopilot->started == 0u)
	{
		autopilot->endOfRaceTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
		for (uint32_t player = 0; player < MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS; player++)
		{
			autopilot->finishTick[player] = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
		}
		autopilot->started = 1u;
	}
	if ((autopilot->endOfRaceTick == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) && ((gGT->gameMode1 & END_OF_RACE) != 0))
	{
		autopilot->endOfRaceTick = raceTick;
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "autopilot: END_OF_RACE at race tick %u\n", (unsigned)raceTick);
	}
	if ((level != NULL) && (level->ptr_restart_points != NULL) && (level->cnt_restart_points > 0) &&
	    (level->cnt_restart_points < MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_POINTS))
	{
		count = (uint32_t)level->cnt_restart_points;
	}
	for (uint32_t player = 0; player < MAIN_ARCADE_ROSTER_PROOF_AUTOPILOT_PLAYERS; player++)
	{
		const struct Driver *driver = gGT->drivers[player];

		autopilot->held[player] = NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS;
		if (driver == NULL)
		{
			continue;
		}
		if ((autopilot->finishTick[player] == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) &&
		    ((driver->actionsFlagSet & ACTION_RACE_FINISHED) != 0))
		{
			autopilot->finishTick[player] = raceTick;
			Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "autopilot: player %u finished at race tick %u\n", (unsigned)player,
				(unsigned)raceTick);
		}
		if (count != 0u)
		{
			autopilot->held[player] = MainArcadeRosterProof_AutopilotSteer(level, count, driver, player);
		}
	}
	if ((raceTick + 1u) >= NativeArcadeRosterProof_Ticks())
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "autopilot: seed 0x%08X%08X END_OF_RACE tick %ld player 0 finish tick %ld player 1 finish tick %ld (-1: never; %u race ticks)\n",
			(unsigned)(uint32_t)(NativeArcadeRosterProof_Seed() >> 32), (unsigned)(uint32_t)(NativeArcadeRosterProof_Seed() & 0xFFFFFFFFu),
			MainArcadeRosterProof_AutopilotLogTick(autopilot->endOfRaceTick), MainArcadeRosterProof_AutopilotLogTick(autopilot->finishTick[0]),
			MainArcadeRosterProof_AutopilotLogTick(autopilot->finishTick[1]), (unsigned)(raceTick + 1u));
	}
}

void MainArcadeRosterProof_EndFrame(struct GameTracker *gGT, const struct NativeCanonicalStateV1 *frameState)
{
	struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	struct NativeArcadeRosterProofTickLine line;
	uint32_t controlIndex;
	uint32_t rngIndex;
	uint32_t inputIndex;
	int extracted;

	/* Default behaviour guarantee: no proof option, no side effect. */
	if (!NativeArcadeRosterProof_Active())
	{
		return;
	}
	if ((gGT == NULL) || (state->phase != MAIN_ARCADE_ROSTER_PROOF_VALIDATED))
	{
		return;
	}

	extracted = MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta(gGT, sdata,
		&s_mainArcadeRosterProofDrivers.candidate);
	if (state->raceTick == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE)
	{
		if (!extracted)
		{
			return; /* the race order is not rebuilt yet */
		}
		state->raceTick = 0u;
		state->raceTickZeroTick = state->frameTick;
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "race tick 0 at tick %u; logging %u race ticks\n", (unsigned)state->frameTick,
			(unsigned)NativeArcadeRosterProof_Ticks());
	}
	else if (!extracted)
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "the drivers extraction failed at race tick %u\n", (unsigned)state->raceTick);
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_DRIVERS_FAILED);
		return;
	}

	controlIndex = MainArcadeRosterProof_DomainIndex(NATIVE_CANONICAL_DOMAIN_CONTROL);
	rngIndex = MainArcadeRosterProof_DomainIndex(NATIVE_CANONICAL_DOMAIN_RNG);
	inputIndex = MainArcadeRosterProof_DomainIndex(NATIVE_CANONICAL_DOMAIN_INPUT);
	memset(&line, 0, sizeof(line));
	line.tick = state->raceTick;
	if (!MainArcadeRosterProof_DriversDigest(line.drivers))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "the drivers record did not encode at race tick %u\n", (unsigned)state->raceTick);
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_DRIVERS_FAILED);
		return;
	}
	if ((frameState == NULL) || (controlIndex >= NATIVE_CANONICAL_DOMAIN_COUNT) || (rngIndex >= NATIVE_CANONICAL_DOMAIN_COUNT) ||
	    (inputIndex >= NATIVE_CANONICAL_DOMAIN_COUNT))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "no V1 canonical state at race tick %u\n", (unsigned)state->raceTick);
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED);
		return;
	}
	line.control = frameState->domainDigests[controlIndex];
	line.rng = frameState->domainDigests[rngIndex];
	line.input = frameState->domainDigests[inputIndex];
	/* The race-relative control digest: the boot-relative counters zeroed. */
	if (!NativeArcadeRosterProof_RaceControlDigest(frameState, &line.raceControl))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "no race-relative control digest at race tick %u\n", (unsigned)state->raceTick);
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED);
		return;
	}
	if (!MainArcadeRosterProof_ProjectV4(gGT, frameState, &line))
	{
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_V4_FAILED);
		return;
	}
	if (state->raceTick == 0u)
	{
		/* The boot-relative counters as race tick 0 saw them (RS-12 evidence). */
		state->raceTickZeroCounters.timer = frameState->control.timer;
		state->raceTickZeroCounters.frameCounter = frameState->control.frameCounter;
		state->raceTickZeroCounters.frameTimer = frameState->control.frameTimer;
		/* frameTimer_Confetti is not a V1 control value, so it is read from
		 * gGT here. It is the same frame the control values come from: their
		 * snapshot is taken at CTR_Main's per-frame frame-info read
		 * (MainMain.c), on the game thread, shortly before this hook. Its
		 * writers are the VBlank callback (MainDrawCb.c), which runs on the
		 * game thread whenever VSync or Platform_WaitUntilVBlank emits a
		 * VBlank, and the RS-17 pin (MainArcadeRaceSetup.c) at race init.
		 * None of them runs between that snapshot and this hook: the
		 * projector and this hook call neither VSync,
		 * Platform_WaitUntilVBlank, nor the race setup. */
		state->raceTickZeroCounters.frameTimerConfetti = (int32_t)gGT->frameTimer_Confetti;
		state->countersValid = 1u;
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "race tick 0 counters: timer %ld frameCounter %ld frameTimer %ld frameTimerConfetti %ld\n",
			(long)frameState->control.timer, (long)frameState->control.frameCounter, (long)frameState->control.frameTimer,
			(long)state->raceTickZeroCounters.frameTimerConfetti);
	}
	/* LR-8 evidence, log only: the elapsedTimeMS the first three race
	 * GameLogic passes computed (MainFrame.c:188-203), from the same V1
	 * control snapshot as the digests (timer is race tick + 1 after the
	 * RS-17 pin). Observed 32 at race ticks 0..2 (race tick 0: the after-load
	 * override, gameMode1_prevFrame 1, MainFrame.c:200-203). With them, the
	 * root-counter phase the LR-8 pin set, as read here: sdata->rcntTotalUnits
	 * after this frame's RenderVSYNC, and gGT->clockFrameStart as this
	 * GameLogic left it. */
	if (state->raceTick < 3u)
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "race tick %u elapsedTimeMS %ld (timer %ld) rcntTotalUnits %ld clockFrameStart %ld\n",
			(unsigned)state->raceTick, (long)frameState->control.elapsedTimeMS, (long)frameState->control.timer,
			(long)sdata->rcntTotalUnits, (long)gGT->clockFrameStart);
	}
	/* The hold's frameTimer evidence: race ticks HOLD_TICK - 1 and HOLD_TICK. */
	if ((NativeArcadeRosterProof_Hold() != 0u) && (state->raceTick == (NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK - 1u)))
	{
		state->hold.frameTimerBefore = frameState->control.frameTimer;
		state->hold.frameTimerValid |= 1u;
	}
	if ((NativeArcadeRosterProof_Hold() != 0u) && (state->raceTick == NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK))
	{
		state->hold.frameTimerAfter = frameState->control.frameTimer;
		state->hold.frameTimerValid |= 2u;
	}
	if (!NativeArcadeRosterProof_RecordTick(&line))
	{
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED);
		return;
	}
	/* LR-S2 (b): after the tick line, so nothing it reads can reach it. */
	if (NativeArcadeRosterProof_Autopilot() != 0u)
	{
		MainArcadeRosterProof_AutopilotStep(gGT, state->raceTick);
	}
	state->raceTick++;
	if (state->raceTick >= NativeArcadeRosterProof_Ticks())
	{
		/* The proof race's end frame for the race digest (LR-10): it
		 * clears the runtime's per-level state for the next race. */
		if (!MainArcadeRaceDigest_EndRace())
		{
			Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "the V4 end frame failed after race tick %u (%s)\n",
				(unsigned)(state->raceTick - 1u), MainArcadeRaceDigest_FailureName(MainArcadeRaceDigest_Failure()));
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_V4_FAILED);
			return;
		}
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS);
	}
}

#endif
