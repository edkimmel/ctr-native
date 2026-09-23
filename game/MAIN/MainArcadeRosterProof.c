#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/*
 * Live roster proof hook (docs/ROSTER_MILESTONE.md section 3.4; R-5b's
 * launcher, which R-5c lets launch from inside the attract demo race, and
 * R-6's scripted pads and per-tick digests). Internal native builds only, and
 * dormant unless main.c configured the proof: with the proof inactive every
 * entry point returns as its first statement and touches nothing.
 *
 * The per-tick evidence is local only: the V1 state MainMain.c projects for
 * the proof and the drivers candidate extracted here go into the proof's
 * report and nowhere else (no scheduler, no recording, no saved state).
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
#include "MAIN/MainCanonicalDrivers.h"
#include "platform/native_arcade_roster_proof.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_canonical_drivers_detailed.h"
#include "platform/native_canonical_state.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_sha256.h"

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
};

static struct MainArcadeRosterProofState s_mainArcadeRosterProof = {
	MAIN_ARCADE_ROSTER_PROOF_WAIT_MENU, 0u, 0u,
	NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE,
	NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE,
	0u, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE};

/* The drivers digest workspace: far too large for the game stack, so
 * file-scope static. Local only; only the digest leaves it. */
struct MainArcadeRosterProofDriversScratch
{
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate candidate;
	struct NativeCanonicalDriversDetailedV1 detailed;
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
};

static struct MainArcadeRosterProofDriversScratch s_mainArcadeRosterProofDrivers;

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

/* The pin readback of the setup (RS-17), the same way. */
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
	stored->timer = setupStored.timer;
	stored->frameTimerConfetti = setupStored.frameTimerConfetti;
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
	uint32_t result;
	int exitCode;

	memset(&report, 0, sizeof(report));
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
		break;
	default:
		break;
	}
	state->tick++;
}

/* Installs the scripted pads for raceTick (TICK_NONE: neutral). */
static int MainArcadeRosterProof_InstallPads(uint32_t raceTick)
{
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];

	NativeArcadeRosterProof_ScriptedPads(raceTick, pads);
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
	/* Race tick n >= 1 runs on the pattern for n; every earlier frame, race
	 * tick 0's included, and every frame after the report is neutral. A
	 * failed install is caught by the input digest (the frozen pads). */
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
	if (!NativeArcadeRosterProof_RecordTick(&line))
	{
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED);
		return;
	}
	state->raceTick++;
	if (state->raceTick >= NativeArcadeRosterProof_Ticks())
	{
		MainArcadeRosterProof_Finish((uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS);
	}
}

#endif
