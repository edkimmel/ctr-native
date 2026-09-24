/*
 * Live race caller (docs/RACE_LAUNCH_MILESTONE.md section 4 RL-8..RL-12,
 * slice RL-S8b): the thin game glue between the arcade-link host's
 * START_RACE and the race setup adapter (MAIN/MainArcadeRaceSetup.h). Native
 * only, and dormant unless the arcade-link host is in LINK mode: otherwise,
 * with no race in progress, MainArcadeRaceLaunch_Frame returns as its first
 * statement and touches nothing.
 *
 * Every decision lives in the pure, unit-tested MAIN/MainArcadeRaceLaunchCore.c
 * (library ctr_native_arcade_race_launch_core, never unity-included); this
 * file samples one frame's facts into the core's input, steps the core, and
 * applies its decisions in the core's order: Arm and Launch (and the result
 * fed back on the same frame), then leaving the title, the host report, the
 * return to the main-menu level, the rehearsal pads, and the Disarm.
 *
 * The host flow is read twice per frame, in two separate calls:
 * MainArcadeLink_Gather reads NativeArcadeLinkHost_Racing before the host
 * tick for the RL-8 policy, and this module reads it again after that tick
 * (MainArcadeRaceLaunch_Frame runs right after MainArcadeLink_Frame), because
 * the tick that returns START_RACE enters RACING first (RL-S7 interpretation
 * (f)). Neither value is fed from the other.
 *
 * The state is file-scope static and host-local: it never enters a saved
 * state, a recording, or canonical state, and nothing here is sent to the
 * peer.
 *
 * Unity-included after the arcade-link hook (whose title launch window it
 * reuses) and the 230 overlay (whose title it leaves), and after
 * MainArcadeRaceSetup.c, which it arms, launches, and disarms.
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_roster_proof.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_match_config.h"
#include "MAIN/MainArcadeLink.h"
#include "MAIN/MainArcadeRaceLaunchCore.h"
#include "MAIN/MainArcadeRaceSetup.h"
#include "MAIN/MainArcadeRaceLaunch.h"

#define MAIN_ARCADE_RACE_LAUNCH_LOG "[CTR Native] arcade link: "

/* The core mirrors these setup values without including the setup header;
 * keep the mirrors honest. */
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_IDLE == (uint32_t)MAIN_ARCADE_RACE_SETUP_IDLE, "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_IDLE must match MAIN_ARCADE_RACE_SETUP_IDLE");
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_ARMED == (uint32_t)MAIN_ARCADE_RACE_SETUP_ARMED, "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_ARMED must match MAIN_ARCADE_RACE_SETUP_ARMED");
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED == (uint32_t)MAIN_ARCADE_RACE_SETUP_LAUNCHED, "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED must match MAIN_ARCADE_RACE_SETUP_LAUNCHED");
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED == (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED, "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED must match MAIN_ARCADE_RACE_SETUP_SEEDED");
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED == (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED, "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED must match MAIN_ARCADE_RACE_SETUP_VALIDATED");
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED == (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILED, "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED must match MAIN_ARCADE_RACE_SETUP_FAILED");
_Static_assert(NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT == PLATFORM_INPUT_PAD_COUNT, "the rehearsal pads cover every host pad");
_Static_assert(MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES == 32u, "the RL-12 line prints 32-byte digests");

struct MainArcadeRaceLaunchState
{
	/* The decision core's state (MAIN/MainArcadeRaceLaunchCore.h). */
	struct MainArcadeRaceLaunchCore core;
	/* The agreed config of the last armAndLaunch frame: the exact bytes the
	 * race setup was armed with. */
	struct NativeMatchConfigV1 config;
	/* The race plan's level of the launched race: the setup's plan takes
	 * its levelID from the config's trackID. */
	int32_t planLevel;
	/* 1 once planLevel holds a launched race's level. */
	uint8_t planLevelValid;
	/* 1 when the arcade-link hook handed START_RACE over this frame. */
	uint8_t startRace;
	/* The host's raceFinished input: 1 from the finish frame until the flow
	 * is first seen off RACING. */
	uint8_t finishedPending;
	uint8_t reserved[1];
};

static struct MainArcadeRaceLaunchState s_mainArcadeRaceLaunch;

void MainArcadeRaceLaunch_StartRace(void)
{
	s_mainArcadeRaceLaunch.startRace = 1u;
}

uint8_t MainArcadeRaceLaunch_RaceFinished(void)
{
	return s_mainArcadeRaceLaunch.finishedPending;
}

/* The core's class of the retail loading stage. */
static uint32_t MainArcadeRaceLaunch_StageClass(void)
{
	if (sdata->Loading.stage == LOAD_IDLE)
	{
		return MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE;
	}
	if (sdata->Loading.stage == LOAD_REQUESTED)
	{
		return MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_REQUESTED;
	}
	return MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_OTHER;
}

/* One frame's facts, after this frame's host tick. Consumes the START_RACE
 * the hook handed over. */
static void MainArcadeRaceLaunch_Gather(const struct GameTracker *gGT, const struct GamepadSystem *gGS,
	struct MainArcadeRaceLaunchCoreInput *input)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;

	memset(input, 0, sizeof(*input));
	input->setupStatus = (uint32_t)MainArcadeRaceSetup_Status();
	input->loadingStage = MainArcadeRaceLaunch_StageClass();
	input->startRace = state->startRace;
	state->startRace = 0u;
	/* After this frame's host tick (RL-S7 interpretation (f)). */
	input->hostRacing = (NativeArcadeLinkHost_Racing() != 0u) ? 1u : 0u;
	input->titleWindowOpen = MainArcadeLink_TitleMenuReady(gGT, gGS) ? 1u : 0u;
	input->onMainMenuLevel = (gGT->levelID == MAIN_MENU_LEVEL) ? 1u : 0u;
	input->onPlanLevel = ((state->planLevelValid != 0u) && ((int32_t)gGT->levelID == state->planLevel)) ? 1u : 0u;
	input->loadingBit = ((gGT->gameMode1 & LOADING) != 0) ? 1u : 0u;
}

/*
 * Leaves the title the way the retail demo route does: the caller's own copy
 * of the steps of MainArcadeRosterProof_LeaveTitle (MainArcadeRosterProof.c),
 * duplicated so the internal proof and its live gate stay untouched;
 * tests/main_arcade_link_hook_isolation_test.cmake pins that the two bodies
 * make the same calls in the same order. Reset the title camera, kill the
 * title thread, hide the main-menu box, and close it: the load is already
 * requested, so RECTMENU_ProcessState does not run again to close it.
 */
static void MainArcadeRaceLaunch_LeaveTitle(void)
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

/* The RL-10 rehearsal pads, the proof's neutral pads: pads 0 and 1 connected
 * neutral digital pads, pads 2 and 3 disconnected. Install writes the pad bus
 * at once, and the host input keeps them until the clear, so no local input
 * reaches the race and no pad reads as unplugged. */
static void MainArcadeRaceLaunch_InstallPads(void)
{
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];

	NativeArcadeRosterProof_ScriptedPads(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, pads);
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
	(void)Platform_InputInstallPadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT);
}

/* Lower-case hex of one setup digest. */
static void MainArcadeRaceLaunch_Hex(char out[2u * MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES + 1u],
	const uint8_t digest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES])
{
	static const char digits[] = "0123456789abcdef";

	for (uint32_t i = 0; i < MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES; i++)
	{
		out[2u * i] = digits[(digest[i] >> 4) & 0xFu];
		out[2u * i + 1u] = digits[digest[i] & 0xFu];
	}
	out[2u * MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES] = '\0';
}

/* RL-12: one line per validated race; the two-process gate compares them. */
static void MainArcadeRaceLaunch_LogDigests(uint32_t raceNumber)
{
	uint8_t digests[4][MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES];
	char hex[4][2u * MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES + 1u];

	if (!MainArcadeRaceSetup_Digests(digests[0], digests[1], digests[2], digests[3]))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u validated; the setup digests are unavailable\n", (unsigned)raceNumber);
		return;
	}
	for (uint32_t i = 0; i < 4u; i++)
	{
		MainArcadeRaceLaunch_Hex(hex[i], digests[i]);
	}
	Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u validated config %s plan %s bots %s bank %s\n", (unsigned)raceNumber, hex[0], hex[1],
		hex[2], hex[3]);
}

/*
 * The armAndLaunch frame: Arm with the agreed config and, if that succeeds,
 * Launch, then the outcome back to the core on this frame with this frame's
 * output. With no agreed config Arm is given none and refuses (ARM).
 */
static void MainArcadeRaceLaunch_ArmAndLaunch(struct MainArcadeRaceLaunchCoreOutput *output)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;
	uint32_t result;
	int haveConfig;

	memset(&state->config, 0, sizeof(state->config));
	haveConfig = NativeArcadeLinkHost_GetAgreedConfig(&state->config);
	if (!haveConfig)
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u has no agreed config\n", (unsigned)output->raceNumber);
	}
	if (!MainArcadeRaceSetup_Arm(haveConfig ? &state->config : NULL))
	{
		result = MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_ARM_FAILED;
	}
	else if (!MainArcadeRaceSetup_Launch())
	{
		result = MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCH_FAILED;
	}
	else
	{
		result = MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCHED;
		state->planLevel = (int32_t)state->config.trackID;
		state->planLevelValid = 1u;
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u launched (track %u laps %u)\n", (unsigned)output->raceNumber,
			(unsigned)state->config.trackID, (unsigned)state->config.lapCount);
	}
	if (!MainArcadeRaceLaunchCore_LaunchResult(&state->core, result, output))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the launch core refused the launch result %u\n", (unsigned)output->raceNumber,
			(unsigned)result);
	}
}

/*
 * The return step (RL-8): back to the main-menu level the way the link's
 * return to title does. The core runs it only while no race-track load is in
 * progress (the stage IDLE or REQUESTED), so the race load's own read of
 * numPlyrNextGame and its stage are never overwritten.
 */
static void MainArcadeRaceLaunch_RequestReturn(struct GameTracker *gGT)
{
	gGT->boolDemoMode = 0;
	gGT->numPlyrNextGame = 1;
	sdata->mainMenuState = MAIN_MENU_TITLE;
	MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
}

/* The core's decisions for this frame, in its order. */
static void MainArcadeRaceLaunch_Apply(struct GameTracker *gGT, const struct MainArcadeRaceLaunchCoreOutput *output)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;

	if (output->leaveTitle != 0u)
	{
		MainArcadeRaceLaunch_LeaveTitle();
	}
	if (output->validated != 0u)
	{
		MainArcadeRaceLaunch_LogDigests(output->raceNumber);
	}
	if (output->raceTickZero != 0u)
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u tick 0 (level %d); rehearsal of %u ticks on neutral pads\n",
			(unsigned)output->raceNumber, (int)gGT->levelID, (unsigned)MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_REHEARSAL_TICKS);
	}
	if (output->reportFailure != 0u)
	{
		/* RL-11: the log names the failure; the peer is not told. */
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u failed locally: %s (setup %s, failure %s)\n", (unsigned)output->raceNumber,
			MainArcadeRaceLaunchCore_FailureName(output->failure), MainArcadeRaceSetup_StatusName(MainArcadeRaceSetup_Status()),
			MainArcadeRaceSetup_FailureName(MainArcadeRaceSetup_Failure()));
		(void)NativeArcadeLinkHost_ReportRaceFailure();
	}
	else if (output->reportFinished != 0u)
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u rehearsal finished\n", (unsigned)output->raceNumber);
		state->finishedPending = 1u;
	}
	if (output->requestReturn != 0u)
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "returning to the main-menu level\n");
		MainArcadeRaceLaunch_RequestReturn(gGT);
	}
	/* RL-10: installed on every frame from the Launch frame until the clear,
	 * which the core never puts on the end frame: it comes on the first
	 * frame after the return step with LOADING set, or on the first idle
	 * main-menu frame, whichever is first. */
	if (output->installPads != 0u)
	{
		MainArcadeRaceLaunch_InstallPads();
	}
	if (output->clearPads != 0u)
	{
		Platform_InputClearInstalledPadSnapshots();
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u rehearsal pads cleared\n", (unsigned)output->raceNumber);
	}
	/* RL-9: once per launched race, on the first idle main-menu frame after
	 * the return (or at once after an Arm or Launch failure at the title). */
	if (output->disarm != 0u)
	{
		MainArcadeRaceSetup_Disarm();
	}
}

void MainArcadeRaceLaunch_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;
	struct MainArcadeRaceLaunchCoreInput input;
	struct MainArcadeRaceLaunchCoreOutput output;

	/* Default behaviour guarantee: no link and no race, no side effect. */
	if ((NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK) && (state->core.phase == MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE))
	{
		return;
	}
	if ((gGT == NULL) || (gGS == NULL))
	{
		return;
	}

	MainArcadeRaceLaunch_Gather(gGT, gGS, &input);
	if (input.hostRacing == 0u)
	{
		/* The flow left RACING: the finish was taken (or no longer can be). */
		state->finishedPending = 0u;
	}
	if (!MainArcadeRaceLaunchCore_Step(&state->core, &input, &output))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "the launch core refused a frame (phase %u, start %u, racing %u)\n", (unsigned)state->core.phase,
			(unsigned)input.startRace, (unsigned)input.hostRacing);
		return;
	}
	if (output.armAndLaunch != 0u)
	{
		MainArcadeRaceLaunch_ArmAndLaunch(&output);
	}
	MainArcadeRaceLaunch_Apply(gGT, &output);
}

#endif
