#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/*
 * Live roster proof hook (docs/ROSTER_MILESTONE.md section 3.4; R-5b's
 * minimal launcher, which R-5c lets launch from inside the attract demo race
 * and R-6 extends with scripted pads, per-tick digests, and a ctest).
 * Internal native builds only, and dormant unless main.c configured the
 * proof: with the proof inactive MainArcadeRosterProof_Frame returns as its
 * first statement and touches nothing.
 *
 * Unity-included after the 230 overlay sources and the arcade-link hook,
 * because it reads and closes the retail title (MM_Title_*, MM_MENU_MAIN)
 * and asks the arcade-link hook for its menu-ready condition; and after
 * MainArcadeRaceSetup, which it arms and launches.
 */

#include <common.h>

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeLink.h"
#include "MAIN/MainArcadeRaceSetup.h"
#include "MAIN/MainArcadeRosterProof.h"
#include "platform/native_arcade_roster_proof.h"
#include "platform/native_log.h"

#define MAIN_ARCADE_ROSTER_PROOF_LOG "[CTR Native] arcade roster proof: "

_Static_assert(NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT == MAIN_ARCADE_BOT_SETUP_SLOT_COUNT,
	"the proof report holds exactly the bot setup's slots");
_Static_assert(MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES == NATIVE_SHA256_DIGEST_BYTES,
	"the proof report digests are the race setup's digests");

enum MainArcadeRosterProofPhase
{
	MAIN_ARCADE_ROSTER_PROOF_WAIT_MENU = 0, /* waiting for the menu-ready frame */
	MAIN_ARCADE_ROSTER_PROOF_DWELL,         /* menu ready; counting the dwell */
	MAIN_ARCADE_ROSTER_PROOF_WAIT_WINDOW,   /* dwell over; waiting for a launch window */
	MAIN_ARCADE_ROSTER_PROOF_RUNNING,       /* launched; polling the setup */
	MAIN_ARCADE_ROSTER_PROOF_VALIDATED,     /* validated; waiting the post-validation ticks */
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
};

static struct MainArcadeRosterProofState s_mainArcadeRosterProof = {
	MAIN_ARCADE_ROSTER_PROOF_WAIT_MENU, 0u, 0u,
	NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE,
	NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE};

static void MainArcadeRosterProof_CopyName(char out[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES], const char *name)
{
	size_t i;

	memset(out, 0, NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES);
	for (i = 0; (i + 1u < NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES) && (name[i] != '\0'); i++)
	{
		out[i] = name[i];
	}
}

/* Writes the report for `result`, logs it, records the exit code, and
 * requests the exit. */
static void MainArcadeRosterProof_Finish(uint32_t result)
{
	struct MainArcadeRosterProofState *state = &s_mainArcadeRosterProof;
	struct NativeArcadeRosterProofReport report;
	struct MainArcadeBotSetupSourceFacts facts;
	const enum MainArcadeRaceSetupStatus status = MainArcadeRaceSetup_Status();
	const enum MainArcadeRaceSetupFailure failure = MainArcadeRaceSetup_Failure();
	int exitCode = (int)result;

	memset(&report, 0, sizeof(report));
	report.result = result;
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

	if (!NativeArcadeRosterProof_WriteReport(&report))
	{
		Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "could not write the report to %s\n", NativeArcadeRosterProof_LogPath());
		if (exitCode == (int)NATIVE_ARCADE_ROSTER_PROOF_PASS)
		{
			exitCode = (int)NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED;
		}
	}
	Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "%s at tick %u (setup %s, failure %s); exit code %d\n",
		NativeArcadeRosterProof_ResultName(result), (unsigned)state->tick, MainArcadeRaceSetup_StatusName(status),
		MainArcadeRaceSetup_FailureName(failure), exitCode);
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
	Platform_Log(MAIN_ARCADE_ROSTER_PROOF_LOG "launched at tick %u from the %s (menu ready at tick %u, dwell %u)\n",
		(unsigned)state->tick, NativeArcadeRosterProof_LaunchWindowName(window), (unsigned)state->menuReadyTick,
		(unsigned)NativeArcadeRosterProof_Dwell());
	state->phase = MAIN_ARCADE_ROSTER_PROOF_RUNNING;
	return 1;
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
		if ((state->tick - state->validatedTick) >= NATIVE_ARCADE_ROSTER_PROOF_POST_VALIDATED_TICKS)
		{
			MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS);
			return;
		}
		break;
	default:
		break;
	}
	state->tick++;
}

#endif
