/*
 * Live race caller (docs/RACE_LAUNCH_MILESTONE.md section 4 RL-8..RL-12,
 * slice RL-S8b; the race drive of the Task 8 race plan, slice LR-S10): the
 * thin game glue between the arcade-link host's START_RACE and the race
 * setup adapter (MAIN/MainArcadeRaceSetup.h), and from race tick 0 on
 * between each race tick and the host's race drive. Native only, and dormant
 * unless the arcade-link host is in LINK mode: otherwise, with no race in
 * progress, MainArcadeRaceLaunch_Frame returns as its first statement and
 * touches nothing.
 *
 * Every decision lives in the pure, unit-tested MAIN/MainArcadeRaceLaunchCore.c
 * (library ctr_native_arcade_race_launch_core, never unity-included); this
 * file samples one frame's facts into the core's input, steps the core, and
 * applies its decisions in the core's order: Arm and Launch (then, only for
 * a launched race, the host's NativeArcadeLinkHost_RaceBegin, which turns
 * fixed VBlank pacing on, LR-7, and begins the race drive; and the result fed
 * back on the same frame), the drive tick (below), then leaving the title,
 * the host report, the end of the race's digest, the return to the
 * main-menu level, the pads, and the Disarm (followed by the host's
 * NativeArcadeLinkHost_RaceEnd, which turns that pacing off again). Last,
 * log only, the elapsedTimeMS of the race's first three GameLogic passes
 * (LR-8 evidence, MainArcadeRaceLaunch_LogElapsed).
 *
 * The drive tick (the Task 8 race plan LR-1, LR-4, LR-9, LR-16, LR-18), on
 * every frame the core sets driveStep (each race tick from race tick 0 on,
 * after that tick's GameLogic and before the VBlanks that read the pads for
 * the next one):
 *   1. the tick's facts: END_OF_RACE, the human count, and the finished
 *      humans (the core's count over the eight drivers' flag words; read
 *      only, LR-18);
 *   2. the pads that tick's GameLogic read, frozen;
 *   3. the tick's V4 state, projected through MAIN/MainArcadeRaceDigest.h
 *      (ProjectState) into a file-scope static; a failed projection is a
 *      local drive failure, reported here, and the host's drive is not
 *      stepped; in internal builds, while the arcade-link autopilot runs,
 *      the projected state's digest line and the autopilot's fault
 *      injection (a freeze in the hold loop without the banner, or a flipped
 *      bit in the state's CONTROL domain digest; LR-73, LR-74);
 *   4. the local pad sample (Platform_InputSampleLocalPad), converted to the
 *      host's pad; in internal builds, while the arcade-link autopilot runs,
 *      the autopilot's steering pad instead (LR-16);
 *   5. the host's race step for the tick; on a hold, the banner's game-font
 *      glyphs read once (read only, LR-S11) and the blocking hold loop
 *      (MAIN/MainArcadeRaceHold.h, with the WAITING FOR OPPONENT banner),
 *      whose step forwards its arguments to the host's hold until it stops
 *      holding;
 *   6. the result back to the core (MainArcadeRaceLaunchCore_DriveResult):
 *      GO, and the committed pads are installed by this frame's Apply; or the
 *      drive's end: a finish kind is the core's finish report, a local
 *      failure is DRIVE_FAILED (the host already reported it), and an outcome
 *      the link latched ends the race with no report. One log line per end.
 * The caller writes no game state but the installed pads (and the title
 * leave and the return step it always had): the drive tick only reads.
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
 * peer except through the host's race drive.
 *
 * Unity-included after the arcade-link hook (whose title launch window it
 * reuses) and the 230 overlay (whose title it leaves), and after
 * MainArcadeRaceSetup.c, which it arms, launches, and disarms, the race
 * digest, and the hold loop.
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include <stddef.h>

#include "platform/native_arcade_link_autopilot.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_roster_proof.h"
#include "platform/native_canonical_projector.h"
#include "platform/native_hold_banner.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_match_config.h"
#include "MAIN/MainArcadeLink.h"
#include "MAIN/MainArcadeLinkAutopilot.h"
#include "MAIN/MainArcadeRaceDigest.h"
#include "MAIN/MainArcadeRaceHold.h"
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
/* The core's finished-human count mirrors the retail finished bit (LR-59). */
_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_ACTION_RACE_FINISHED == (uint32_t)ACTION_RACE_FINISHED, "MAIN_ARCADE_RACE_LAUNCH_CORE_ACTION_RACE_FINISHED must match ACTION_RACE_FINISHED");
/* ... over every driver slot. */
_Static_assert(sizeof(((struct GameTracker *)0)->drivers) / sizeof(((struct GameTracker *)0)->drivers[0]) == MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS, "the finished-human count covers every driver slot");
_Static_assert(NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT == PLATFORM_INPUT_PAD_COUNT, "the neutral pads cover every host pad");
_Static_assert(MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES == 32u, "the RL-12 line prints 32-byte digests");
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_DIGEST_BYTES == MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES, "the RL-12 evidence copies the setup digests");
/* The host's pad mirrors the platform input layer's pad snapshot field for
 * field and byte for byte (include/platform/native_arcade_link_host.h); the
 * two conversions below copy field by field. */
_Static_assert(NATIVE_ARCADE_LINK_HOST_RACE_PADS == PLATFORM_INPUT_PAD_COUNT, "the committed pads cover every host pad");
_Static_assert(sizeof(struct NativeArcadeLinkHostPad) == sizeof(struct PlatformInputPadSnapshot), "the host pad mirrors the pad snapshot's size");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, status) == offsetof(struct PlatformInputPadSnapshot, status), "the host pad mirrors status");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, id) == offsetof(struct PlatformInputPadSnapshot, id), "the host pad mirrors id");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, buttons) == offsetof(struct PlatformInputPadSnapshot, buttons), "the host pad mirrors buttons");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, analog) == offsetof(struct PlatformInputPadSnapshot, analog), "the host pad mirrors analog");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, connected) == offsetof(struct PlatformInputPadSnapshot, connected), "the host pad mirrors connected");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, reserved) == offsetof(struct PlatformInputPadSnapshot, reserved), "the host pad mirrors reserved");

struct MainArcadeRaceLaunchState
{
	/* The decision core's state (MAIN/MainArcadeRaceLaunchCore.h). */
	struct MainArcadeRaceLaunchCore core;
	/* The agreed config of the last armAndLaunch frame: the exact bytes the
	 * race setup was armed with (and the digest's config for the race). */
	struct NativeMatchConfigV1 config;
	/* The committed pads of the last GO, installed by that frame's Apply. */
	struct NativeArcadeLinkHostPad committed[NATIVE_ARCADE_LINK_HOST_RACE_PADS];
	/* The host's answer to the last hold step. */
	uint32_t holdStatus;
	/* The race plan's level of the launched race: the setup's plan takes
	 * its levelID from the config's trackID. */
	int32_t planLevel;
	/* The launch number whose pad install failure was logged (0: none), so
	 * a failing install logs once per race. */
	uint32_t padFailureRace;
	/* 1 once planLevel holds a launched race's level, until the Disarm. */
	uint8_t planLevelValid;
	/* 1 when the arcade-link hook handed START_RACE over this frame. */
	uint8_t startRace;
	/* The host's raceFinished input: the core's raceFinishedInput of its
	 * last accepted Step (the core owns the finish latch). */
	uint8_t raceFinishedInput;
	/* 1 once a refused Step was logged, until the next accepted Step, so a
	 * core that keeps refusing logs once. */
	uint8_t refusedLogged;
	/* RL-15 evidence, read only by the internal autopilot: the races that
	 * logged the RL-12 line, and the last one's launch number and digests. */
	uint32_t validatedRaces;
	uint32_t validatedRace;
	uint8_t validatedDigests[4u * MAIN_ARCADE_RACE_LAUNCH_DIGEST_BYTES];
	/* LR-8 evidence, log only: the launch number whose first race ticks
	 * were logged, and a bit per race tick 0..2 already logged. */
	uint32_t elapsedRace;
	uint8_t elapsedLogged;
};

static struct MainArcadeRaceLaunchState s_mainArcadeRaceLaunch;

/* The V4 state of the current race tick, projected for the host's race
 * drive (LR-58): too large for the stack, and host-local like the rest. */
static struct NativeCanonicalStateV4 s_mainArcadeRaceLaunchTickState;

void MainArcadeRaceLaunch_StartRace(void)
{
	s_mainArcadeRaceLaunch.startRace = 1u;
}

uint8_t MainArcadeRaceLaunch_RaceFinished(void)
{
	return s_mainArcadeRaceLaunch.raceFinishedInput;
}

uint32_t MainArcadeRaceLaunch_ValidatedRaces(void)
{
	return s_mainArcadeRaceLaunch.validatedRaces;
}

int MainArcadeRaceLaunch_LastValidated(uint32_t *raceNumber, uint8_t digests[4u * MAIN_ARCADE_RACE_LAUNCH_DIGEST_BYTES])
{
	const struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;

	if ((raceNumber == NULL) || (digests == NULL) || (state->validatedRaces == 0u))
	{
		return 0;
	}
	*raceNumber = state->validatedRace;
	memcpy(digests, state->validatedDigests, sizeof(state->validatedDigests));
	return 1;
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

/* The RL-10 neutral pads, the proof's: pads 0 and 1 connected neutral
 * digital pads, pads 2 and 3 disconnected. Install writes the pad bus at
 * once, and the host input keeps them until the clear, so no local input
 * reaches the race and no pad reads as unplugged. From the Launch frame to
 * race tick 0 and from the drive's end frame to the clear. A failed install
 * is logged once per race. */
static void MainArcadeRaceLaunch_InstallPads(uint32_t raceNumber)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;
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
	if ((Platform_InputInstallPadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT) != PLATFORM_INPUT_PAD_COUNT) && (state->padFailureRace != raceNumber))
	{
		state->padFailureRace = raceNumber;
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the neutral pads could not be installed\n", (unsigned)raceNumber);
	}
}

/* The committed pads of a GO (LR-4): the one conversion from the host's pads
 * to the pad snapshots, field by field, and their install for the next race
 * tick's GameLogic. A failed install is logged once per race. */
static void MainArcadeRaceLaunch_InstallCommitted(uint32_t raceNumber, uint32_t raceTick)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];

	memset(snapshots, 0, sizeof(snapshots));
	for (uint32_t pad = 0; pad < NATIVE_ARCADE_LINK_HOST_RACE_PADS; pad++)
	{
		const struct NativeArcadeLinkHostPad *committed = &state->committed[pad];

		snapshots[pad].status = committed->status;
		snapshots[pad].id = committed->id;
		snapshots[pad].buttons[0] = committed->buttons[0];
		snapshots[pad].buttons[1] = committed->buttons[1];
		for (uint32_t axis = 0; axis < 4u; axis++)
		{
			snapshots[pad].analog[axis] = committed->analog[axis];
		}
		snapshots[pad].connected = committed->connected;
	}
	if ((Platform_InputInstallPadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT) != PLATFORM_INPUT_PAD_COUNT) && (state->padFailureRace != raceNumber))
	{
		state->padFailureRace = raceNumber;
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the committed pads of race tick %u could not be installed\n", (unsigned)raceNumber,
			(unsigned)raceTick);
	}
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
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: validated without setup digests\n", (unsigned)raceNumber);
		return;
	}
	for (uint32_t i = 0; i < 4u; i++)
	{
		MainArcadeRaceLaunch_Hex(hex[i], digests[i]);
	}
	Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u validated config %s plan %s bots %s bank %s\n", (unsigned)raceNumber, hex[0], hex[1],
		hex[2], hex[3]);
	/* The same digests, kept for the RL-15 gate's report. */
	for (uint32_t i = 0; i < 4u; i++)
	{
		memcpy(&s_mainArcadeRaceLaunch.validatedDigests[i * MAIN_ARCADE_RACE_LAUNCH_DIGEST_BYTES], digests[i], MAIN_ARCADE_RACE_LAUNCH_DIGEST_BYTES);
	}
	s_mainArcadeRaceLaunch.validatedRace = raceNumber;
	s_mainArcadeRaceLaunch.validatedRaces++;
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
		/* A copy of the race setup plan's level rule,
		 * `candidate.levelID = (int32_t)config->trackID;` in the setup's plan
		 * module (which this file may not name or call; see
		 * tests/main_arcade_race_setup_plan_isolation_test.cmake rule 7). The
		 * two must change together: tests/main_arcade_link_hook_isolation_test.cmake
		 * (16h) requires both lines. */
		state->planLevel = (int32_t)state->config.trackID;
		state->planLevelValid = 1u;
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u launched (track %u laps %u)\n", (unsigned)output->raceNumber,
			(unsigned)state->config.trackID, (unsigned)state->config.lapCount);
		/* LR-7: fixed VBlank pacing from the Launch frame, before the
		 * race-track load (and so before the setup's race-init pins), to the
		 * Disarm frame. Only a launched race turns it on. */
		if (!NativeArcadeLinkHost_RaceBegin())
		{
			Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the host did not begin the race pacing\n", (unsigned)output->raceNumber);
		}
	}
	if (!MainArcadeRaceLaunchCore_LaunchResult(&state->core, result, output))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the launch core refused the launch result %u\n", (unsigned)output->raceNumber,
			(unsigned)result);
	}
}

/* The drive tick's facts (LR-18): END_OF_RACE, the human count, and the
 * humans finished, over the eight drivers' flag words (0 for an empty slot).
 * Reads only. */
static void MainArcadeRaceLaunch_Facts(const struct GameTracker *gGT, struct NativeArcadeLinkHostRaceFacts *facts)
{
	uint32_t flagWords[MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS];

	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS; slot++)
	{
		const struct Driver *driver = gGT->drivers[slot];

		flagWords[slot] = (driver != NULL) ? (uint32_t)driver->actionsFlagSet : 0u;
	}
	memset(facts, 0, sizeof(*facts));
	facts->endOfRace = ((gGT->gameMode1 & END_OF_RACE) != 0) ? 1u : 0u;
	facts->humans = (uint32_t)gGT->numPlyrCurrGame;
	facts->finishedHumans = MainArcadeRaceLaunchCore_FinishedHumans(flagWords, facts->humans);
}

#if defined(CTR_INTERNAL)

/*
 * LR-16: the arcade-link autopilot's steering pad, for the two-process live
 * gate. The pure decision is include/platform/native_arcade_link_autopilot.h's
 * (Steering); this reads the facts it needs, the way the roster proof's
 * autopilot does (MainArcadeRosterProof.c): the local kart (driver 0 on cab
 * 1, driver 1 on cab 2) and the level's restart points. It aims at the
 * restart point one past its target, the target being the nearest point at
 * race tick 0, moved on past every point the kart has passed. Reads only;
 * the target is host-local and reset at every race tick 0.
 */

/* The kart aims this many restart points beyond its target. */
#define MAIN_ARCADE_RACE_LAUNCH_STEER_LOOKAHEAD 1u
/* posCurr is in 1/256 world units (VehLap.c reads it the same way). */
#define MAIN_ARCADE_RACE_LAUNCH_STEER_WORLD_SHIFT 8
/* A restart point link at or above the count (0xFF: none) is no link. */
#define MAIN_ARCADE_RACE_LAUNCH_STEER_MAX_POINTS 0xFF
/* The steering pad: a connected digital pad, analog centred. */
#define MAIN_ARCADE_RACE_LAUNCH_STEER_PAD_ID 0x41u
#define MAIN_ARCADE_RACE_LAUNCH_STEER_ANALOG 0x80u
/* A pad word with no button held (the pad is active low). */
#define MAIN_ARCADE_RACE_LAUNCH_STEER_NONE_HELD 0xFFFFu

struct MainArcadeRaceLaunchSteer
{
	/* the restart point the kart heads for */
	uint32_t target;
	/* 1 once target holds a restart point of this race */
	uint8_t targetValid;
	uint8_t reserved[3];
};

static struct MainArcadeRaceLaunchSteer s_mainArcadeRaceLaunchSteer;

/* The restart point after index (forward); index itself when it has no valid link. */
static uint32_t MainArcadeRaceLaunch_SteerNext(const struct Level *level, uint32_t count, uint32_t index)
{
	const uint32_t next = level->ptr_restart_points[index].nextIndex_forward;

	return (next < count) ? next : index;
}

/* The restart point nearest to the kart (x, y, z in world units). */
static uint32_t MainArcadeRaceLaunch_SteerNearest(const struct Level *level, uint32_t count, int32_t x, int32_t y, int32_t z)
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

/* The steering buttons (active high) for the kart's next race tick. */
static uint32_t MainArcadeRaceLaunch_SteerButtons(const struct Level *level, uint32_t count, const struct Driver *driver)
{
	struct MainArcadeRaceLaunchSteer *steerState = &s_mainArcadeRaceLaunchSteer;
	struct NativeArcadeLinkAutopilotPassFacts pass;
	struct NativeArcadeLinkAutopilotSteerFacts steer;
	const int32_t kartX = (int32_t)CTR_MipsSra(driver->posCurr.x, MAIN_ARCADE_RACE_LAUNCH_STEER_WORLD_SHIFT);
	const int32_t kartY = (int32_t)CTR_MipsSra(driver->posCurr.y, MAIN_ARCADE_RACE_LAUNCH_STEER_WORLD_SHIFT);
	const int32_t kartZ = (int32_t)CTR_MipsSra(driver->posCurr.z, MAIN_ARCADE_RACE_LAUNCH_STEER_WORLD_SHIFT);
	uint32_t target;
	uint32_t aim;

	if ((steerState->targetValid == 0u) || (steerState->target >= count))
	{
		steerState->target = MainArcadeRaceLaunch_SteerNearest(level, count, kartX, kartY, kartZ);
		steerState->targetValid = 1u;
	}
	target = steerState->target;
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
		target = MainArcadeRaceLaunch_SteerNext(level, count, target);
	}
	steerState->target = target;
	aim = target;
	for (uint32_t step = 0; step < MAIN_ARCADE_RACE_LAUNCH_STEER_LOOKAHEAD; step++)
	{
		aim = MainArcadeRaceLaunch_SteerNext(level, count, aim);
	}
	steer.kartX = kartX;
	steer.kartZ = kartZ;
	steer.heading = (int32_t)driver->angle;
	steer.aimX = level->ptr_restart_points[aim].pos.x;
	steer.aimZ = level->ptr_restart_points[aim].pos.z;
	return NativeArcadeLinkAutopilot_Steer(&steer);
}

/* While the arcade-link autopilot runs: replaces *sample with the steering
 * pad for the tick after raceTick and returns 1. Otherwise returns 0 and
 * leaves *sample alone. */
static int MainArcadeRaceLaunch_AutopilotSample(const struct GameTracker *gGT, uint32_t raceTick, struct NativeArcadeLinkHostPad *sample)
{
	struct NativeArcadeLinkHostView view;
	const struct Level *level = gGT->level1;
	const struct Driver *driver = NULL;
	uint32_t count = 0u;
	uint32_t held = NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS;
	uint32_t word;

	if (MainArcadeLinkAutopilot_Active() == 0u)
	{
		return 0;
	}
	if (raceTick == 0u)
	{
		memset(&s_mainArcadeRaceLaunchSteer, 0, sizeof(s_mainArcadeRaceLaunchSteer));
	}
	if (NativeArcadeLinkHost_GetView(&view) && ((view.localCab == 1u) || (view.localCab == 2u)))
	{
		driver = gGT->drivers[view.localCab - 1u];
	}
	if ((level != NULL) && (level->ptr_restart_points != NULL) && (level->cnt_restart_points > 0) &&
	    (level->cnt_restart_points < MAIN_ARCADE_RACE_LAUNCH_STEER_MAX_POINTS))
	{
		count = (uint32_t)level->cnt_restart_points;
	}
	if ((driver != NULL) && (count != 0u))
	{
		held = MainArcadeRaceLaunch_SteerButtons(level, count, driver);
	}
	/* Active low: a held button clears its bit. */
	word = MAIN_ARCADE_RACE_LAUNCH_STEER_NONE_HELD & ~held;
	memset(sample, 0, sizeof(*sample));
	sample->status = 0u;
	sample->id = MAIN_ARCADE_RACE_LAUNCH_STEER_PAD_ID;
	sample->buttons[0] = (uint8_t)(word & 0xFFu);
	sample->buttons[1] = (uint8_t)((word >> 8) & 0xFFu);
	for (uint32_t axis = 0; axis < 4u; axis++)
	{
		sample->analog[axis] = MAIN_ARCADE_RACE_LAUNCH_STEER_ANALOG;
	}
	sample->connected = 1u;
	return 1;
}

/* The desync injection flips one of the projected state's domain digests. */
_Static_assert(NATIVE_ARCADE_LINK_AUTOPILOT_CONTROL_DIGEST < sizeof(s_mainArcadeRaceLaunchTickState.domainDigests) / sizeof(s_mainArcadeRaceLaunchTickState.domainDigests[0]), "the desync injection flips a domain digest");

/* The freeze injection's hold step (LR-73): the cabinet freezes itself, for
 * FREEZE_PERIODS tick periods. It names no link host or peer call, so the
 * frozen cabinet sends and takes nothing; the hold loop only pumps the
 * window and waits. */
static int MainArcadeRaceLaunch_FreezeStep(void *context, uint32_t periods, int newPeriod)
{
	(void)context;
	(void)newPeriod;
	return (periods < NATIVE_ARCADE_LINK_AUTOPILOT_FREEZE_PERIODS) ? 1 : 0;
}

/*
 * LR-73, LR-74: while the arcade-link autopilot runs, once per drive tick
 * right after the projection: the tick's digest line from the projected
 * state as projected (its domain digests named and ordered by the roster
 * proof's tick line text, NativeArcadeRosterProof_FormatV4Digests), then the
 * tick's fault injection. A freeze blocks here in the hold loop, without the
 * banner. A desync XORs 1 into the CONTROL
 * domain digest of the projected state, after its line, so only the copy the
 * host's race step records and sends differs (the combined digest is left
 * alone, and the next tick's projection overwrites the whole state). No game
 * state is written. Returns at once while the autopilot is inactive.
 */
static void MainArcadeRaceLaunch_AutopilotTick(const struct MainArcadeRaceLaunchCoreOutput *output)
{
	struct MainArcadeRaceHoldResult freeze;
	char digests[NATIVE_ARCADE_ROSTER_PROOF_V4_DIGESTS_BYTES];
	size_t length = 0u;
	uint32_t fault;

	if (MainArcadeLinkAutopilot_Active() == 0u)
	{
		return;
	}
	if (NativeArcadeRosterProof_FormatV4Digests(s_mainArcadeRaceLaunchTickState.combinedDigest, s_mainArcadeRaceLaunchTickState.domainDigests,
	        digests, sizeof(digests), &length))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u race tick %u digests%s\n", (unsigned)output->raceNumber, (unsigned)output->raceTick,
			digests);
	}
	fault = MainArcadeLinkAutopilot_Fault(output->raceTick);
	if (fault == NATIVE_ARCADE_LINK_AUTOPILOT_FAULT_FREEZE)
	{
		memset(&freeze, 0, sizeof(freeze));
		MainArcadeRaceHold_RunMode(MainArcadeRaceLaunch_FreezeStep, NULL, MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER, NULL, &freeze);
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u race tick %u froze %u tick periods (%llu us)\n", (unsigned)output->raceNumber,
			(unsigned)output->raceTick, (unsigned)freeze.periods, (unsigned long long)freeze.wallUs);
	}
	else if (fault == NATIVE_ARCADE_LINK_AUTOPILOT_FAULT_DESYNC)
	{
		s_mainArcadeRaceLaunchTickState.domainDigests[NATIVE_ARCADE_LINK_AUTOPILOT_CONTROL_DIGEST] ^= 1u;
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u race tick %u: autopilot desync injection flipped bit 0 of the CONTROL domain digest\n",
			(unsigned)output->raceNumber, (unsigned)output->raceTick);
	}
}

#else

/* Without CTR_INTERNAL there is no autopilot: the local pad is the sample. */
static int MainArcadeRaceLaunch_AutopilotSample(const struct GameTracker *gGT, uint32_t raceTick, struct NativeArcadeLinkHostPad *sample)
{
	(void)gGT;
	(void)raceTick;
	(void)sample;
	return 0;
}

/* Without CTR_INTERNAL there is no autopilot: no digest line and no fault. */
static void MainArcadeRaceLaunch_AutopilotTick(const struct MainArcadeRaceLaunchCoreOutput *output)
{
	(void)output;
}

#endif

/* The local pad sample of the drive tick (LR-4, LR-37): the cabinet's own
 * pad, converted to the host's pad field by field (the drive normalizes it);
 * in internal builds the autopilot's steering pad replaces it while the
 * autopilot runs (LR-16). */
static void MainArcadeRaceLaunch_Sample(const struct GameTracker *gGT, uint32_t raceTick, struct NativeArcadeLinkHostPad *sample)
{
	struct PlatformInputPadSnapshot local;

	memset(&local, 0, sizeof(local));
	(void)Platform_InputSampleLocalPad(&local);
	memset(sample, 0, sizeof(*sample));
	sample->status = local.status;
	sample->id = local.id;
	sample->buttons[0] = local.buttons[0];
	sample->buttons[1] = local.buttons[1];
	for (uint32_t axis = 0; axis < 4u; axis++)
	{
		sample->analog[axis] = local.analog[axis];
	}
	sample->connected = local.connected;
	(void)MainArcadeRaceLaunch_AutopilotSample(gGT, raceTick, sample);
}

/* The hold loop's step (LR-9): forwards its arguments unchanged to the
 * host's hold and holds on while the host says HOLD. */
static int MainArcadeRaceLaunch_HoldStep(void *context, uint32_t periods, int newPeriod)
{
	struct MainArcadeRaceLaunchState *state = (struct MainArcadeRaceLaunchState *)context;

	state->holdStatus = NativeArcadeLinkHost_RaceHold(periods, newPeriod, state->committed);
	return (state->holdStatus == NATIVE_ARCADE_LINK_HOST_RACE_HOLD) ? 1 : 0;
}

/*
 * The hold banner's game-font glyphs (LR-S11, LR-72): the arcade-link
 * layout's banner font and colour (FONT_SMALL, WHITE; MainArcadeLinkLayout.c),
 * one entry per character of the banner text, looked up as
 * DecalFont_DrawLineStrlen does: the pen advance (the punctuation width for
 * ':' and '.'), the icon ID from font_characterIconID for 0x21..0xFF (none,
 * 0xFF, is a blank that only advances), the font's icon group (none when
 * NULL, as DecalFont's native guard), and the icon's texture words and
 * corners. Read only: no game state is written, nothing is drawn, and no
 * ordering table or primitive memory is touched; the platform decodes the
 * texels itself and falls back to its block font for any entry it refuses.
 * A button, indent, or kana character is left MISSING (the banner has none).
 */
static void MainArcadeRaceLaunch_BannerGlyphs(const struct GameTracker *gGT, struct NativeHoldBannerGlyphs *glyphs)
{
	const char *text = MAIN_ARCADE_RACE_HOLD_BANNER_TEXT;
	const size_t length = strlen(text);
	const int groupID = (int)data.font_IconGroupID[FONT_SMALL];
	const struct IconGroup *group = NULL;

	memset(glyphs, 0, sizeof(*glyphs));
	if (length > (size_t)NATIVE_HOLD_BANNER_MAX_CHARS)
	{
		return;
	}
	glyphs->count = (uint32_t)length;
	glyphs->color = data.ptrColor[WHITE][0];
	if ((groupID >= 0) && ((size_t)groupID < (sizeof(gGT->iconGroup) / sizeof(gGT->iconGroup[0]))))
	{
		group = gGT->iconGroup[groupID];
	}
	for (size_t i = 0; i < length; i++)
	{
		struct NativeHoldBannerGlyph *glyph = &glyphs->glyphs[i];
		const uint32_t c = (uint32_t)(unsigned char)text[i];
		uint32_t iconID = 0xFFu;
		const struct Icon *icon;

		glyph->advance = ((c == ':') || (c == '.')) ? data.font_puncPixWidth[FONT_SMALL] : data.font_charPixWidth[FONT_SMALL];
		if ((c < 3u) || (c == '@') || (c == '[') || (c == '^') || (c == '*'))
		{
			continue;
		}
		if ((c - 0x21u) < 0xDFu)
		{
			iconID = (uint32_t)data.font_characterIconID[c - 0x21u];
		}
		if (iconID == 0xFFu)
		{
			glyph->kind = NATIVE_HOLD_BANNER_GLYPH_BLANK;
			continue;
		}
		if ((iconID > 0x7Fu) || (group == NULL) || ((int)iconID >= (int)group->numIcons))
		{
			continue;
		}
		icon = (ICONGROUP_GETICONS(group))[iconID];
		if (icon == NULL)
		{
			continue;
		}
		glyph->tpage = icon->texLayout.tpage;
		glyph->clut = icon->texLayout.clut;
		glyph->u = icon->texLayout.u0;
		glyph->v = icon->texLayout.v0;
		glyph->width = (int16_t)((int)icon->texLayout.u1 - (int)icon->texLayout.u0);
		glyph->height = (int16_t)((int)icon->texLayout.v2 - (int)icon->texLayout.v0);
		glyph->kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
	}
}

/* The drive tick's result back to the core. A refusal is only logged: it
 * would leave the result due and every later Step refused (a wedged race),
 * but it cannot happen here. The core refuses only a NULL, a result outside
 * GO..OUTCOME, or no result due; this is called exactly once per driveStep
 * frame (the Step that set driveStep left the result due, in the drive
 * phase, and nothing touches the core in between), always with one of the
 * four results. main_arcade_race_launch_core_unit
 * (TestDriveResultCallerPattern) pins that the core never refuses it in
 * this pattern. */
static void MainArcadeRaceLaunch_DriveResult(struct MainArcadeRaceLaunchCoreOutput *output, uint32_t result)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;

	if (!MainArcadeRaceLaunchCore_DriveResult(&state->core, result, output))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the launch core refused the drive result %u at race tick %u\n",
			(unsigned)output->raceNumber, (unsigned)result, (unsigned)output->raceTick);
	}
}

/* A local drive failure the host's drive did not see (the pads, the
 * projection, or a drive that ended without a kind): reported here, once, as
 * an RL-11 local failure; Apply does not report DRIVE_FAILED again. */
static void MainArcadeRaceLaunch_DriveFailed(struct MainArcadeRaceLaunchCoreOutput *output)
{
	(void)NativeArcadeLinkHost_ReportRaceFailure();
	MainArcadeRaceLaunch_DriveResult(output, MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_FAILED);
}

/* The drive's end: its kind to the core's result, with one log line. */
static void MainArcadeRaceLaunch_DriveEnd(struct MainArcadeRaceLaunchCoreOutput *output)
{
	struct NativeArcadeLinkHostDriveState drive;

	memset(&drive, 0, sizeof(drive));
	if (!NativeArcadeLinkHost_GetDriveState(&drive))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u drive end: no drive state at race tick %u\n", (unsigned)output->raceNumber,
			(unsigned)output->raceTick);
		MainArcadeRaceLaunch_DriveFailed(output);
		return;
	}
	switch (drive.endKind)
	{
	case NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE:
	case NATIVE_ARCADE_LINK_HOST_DRIVE_END_FINISH_GRACE:
	case NATIVE_ARCADE_LINK_HOST_DRIVE_END_RACE_TICK_LIMIT:
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u drive end: %s at race tick %u\n", (unsigned)output->raceNumber,
			NativeArcadeLinkHost_DriveEndKindName(drive.endKind), (unsigned)drive.endTick);
		MainArcadeRaceLaunch_DriveResult(output, MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_FINISHED);
		break;
	case NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME:
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u drive end: %s at race tick %u\n", (unsigned)output->raceNumber,
			NativeArcadeLinkHost_DriveEndKindName(drive.endKind), (unsigned)drive.endTick);
		MainArcadeRaceLaunch_DriveResult(output, MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_OUTCOME);
		break;
	case NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE:
		/* The host's drive reported it already (once per race). */
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u drive end: %s (%s) at race tick %u\n", (unsigned)output->raceNumber,
			NativeArcadeLinkHost_DriveEndKindName(drive.endKind), NativeArcadeLinkHost_DriveFailureName(drive.failureReason),
			(unsigned)drive.endTick);
		MainArcadeRaceLaunch_DriveResult(output, MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_FAILED);
		break;
	default:
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u drive end: %s at race tick %u (the drive did not run)\n", (unsigned)output->raceNumber,
			NativeArcadeLinkHost_DriveEndKindName(drive.endKind), (unsigned)output->raceTick);
		MainArcadeRaceLaunch_DriveFailed(output);
		break;
	}
}

/*
 * One drive tick (see the top of this file), on a driveStep frame: facts,
 * the frozen pads, the projected state, the sample, the host's race step
 * (and hold), and exactly one result back to the core.
 */
static void MainArcadeRaceLaunch_Drive(const struct GameTracker *gGT, struct MainArcadeRaceLaunchCoreOutput *output)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;
	struct NativeArcadeLinkHostRaceFacts facts;
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct NativeCanonicalInputV1 frozen;
	struct MainArcadeRaceDigestSources sources;
	struct MainArcadeRaceDigestTick tick;
	struct NativeArcadeLinkHostPad sample;
	struct MainArcadeRaceHoldResult hold;
	struct NativeHoldBannerGlyphs glyphs;
	uint32_t status;

	MainArcadeRaceLaunch_Facts(gGT, &facts);
	/* The pads this tick's GameLogic read (installed pads stay until the
	 * next install, and no VBlank ran since). */
	memset(snapshots, 0, sizeof(snapshots));
	memset(&frozen, 0, sizeof(frozen));
	if ((Platform_InputCapturePadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT) != PLATFORM_INPUT_PAD_COUNT) ||
	    !MainCanonicalState_FreezeInputV1(&frozen, snapshots, PLATFORM_INPUT_PAD_COUNT))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the pads of race tick %u could not be frozen\n", (unsigned)output->raceNumber,
			(unsigned)output->raceTick);
		MainArcadeRaceLaunch_DriveFailed(output);
		return;
	}
	sources.gGT = gGT;
	sources.sourceData = sdata;
	sources.mineSource = &D231;
	sources.config = &state->config;
	sources.bank = MainArcadeRaceSetup_Bank();
	sources.input = &frozen;
	if (!MainArcadeRaceDigest_ProjectState(output->raceTick, &sources, &tick, &s_mainArcadeRaceLaunchTickState))
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the V4 projection failed at race tick %u (%s, runtime reason %u)\n",
			(unsigned)output->raceNumber, (unsigned)output->raceTick, MainArcadeRaceDigest_FailureName(MainArcadeRaceDigest_Failure()),
			(unsigned)MainArcadeRaceDigest_RuntimeFailure());
		MainArcadeRaceLaunch_DriveFailed(output);
		return;
	}
	/* LR-73, LR-74: the autopilot's digest line and fault injection. */
	MainArcadeRaceLaunch_AutopilotTick(output);
	MainArcadeRaceLaunch_Sample(gGT, output->raceTick, &sample);
	status = NativeArcadeLinkHost_RaceStep(output->raceTick, &s_mainArcadeRaceLaunchTickState, &sample, &facts, state->committed);
	if (status == NATIVE_ARCADE_LINK_HOST_RACE_HOLD)
	{
		/* LR-9: block here, before the VBlanks, until the host commits the
		 * tick or ends the drive; after the grace, with the banner in the
		 * game font (LR-S11). */
		MainArcadeRaceLaunch_BannerGlyphs(gGT, &glyphs);
		memset(&hold, 0, sizeof(hold));
		state->holdStatus = NATIVE_ARCADE_LINK_HOST_RACE_HOLD;
		MainArcadeRaceHold_RunMode(MainArcadeRaceLaunch_HoldStep, state, MAIN_ARCADE_RACE_HOLD_MODE_BANNER, &glyphs, &hold);
		status = state->holdStatus;
		if (hold.periods != 0u)
		{
			Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u race tick %u held %u tick periods (%llu us)\n", (unsigned)output->raceNumber,
				(unsigned)output->raceTick, (unsigned)hold.periods, (unsigned long long)hold.wallUs);
		}
	}
	if (status == NATIVE_ARCADE_LINK_HOST_RACE_GO)
	{
		MainArcadeRaceLaunch_DriveResult(output, MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_GO);
		return;
	}
	MainArcadeRaceLaunch_DriveEnd(output);
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
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u tick 0 (level %d)\n", (unsigned)output->raceNumber, (int)gGT->levelID);
	}
	if (output->reportFailure != 0u)
	{
		/* RL-11: the log names the failure; the peer is not told. A
		 * DRIVE_FAILED was reported by the drive tick already. */
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u failed locally: %s (setup %s, failure %s)\n", (unsigned)output->raceNumber,
			MainArcadeRaceLaunchCore_FailureName(output->failure), MainArcadeRaceSetup_StatusName(MainArcadeRaceSetup_Status()),
			MainArcadeRaceSetup_FailureName(MainArcadeRaceSetup_Failure()));
		if (output->failure != MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_DRIVE_FAILED)
		{
			(void)NativeArcadeLinkHost_ReportRaceFailure();
		}
	}
	else if (output->reportFinished != 0u)
	{
		/* The host takes it through the core's raceFinishedInput latch. */
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u finished\n", (unsigned)output->raceNumber);
	}
	/* The race's digest ends with its drive: the next race loads a level
	 * that can reuse addresses. */
	if (output->driveEnded != 0u)
	{
		if (!MainArcadeRaceDigest_EndRace())
		{
			Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u: the race digest did not end (%s)\n", (unsigned)output->raceNumber,
				MainArcadeRaceDigest_FailureName(MainArcadeRaceDigest_Failure()));
		}
	}
	if (output->requestReturn != 0u)
	{
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "returning to the main-menu level\n");
		MainArcadeRaceLaunch_RequestReturn(gGT);
	}
	/* RL-10 and LR-4: the committed pads of a GO for the next race tick;
	 * otherwise the neutral pads on every frame from the Launch frame until
	 * the clear, which the core never puts on the end frame: it comes on the
	 * first frame after the return step with LOADING set, or on the first
	 * idle main-menu frame, whichever is first. */
	if (output->installCommitted != 0u)
	{
		MainArcadeRaceLaunch_InstallCommitted(output->raceNumber, output->raceTick);
	}
	else if (output->installPads != 0u)
	{
		MainArcadeRaceLaunch_InstallPads(output->raceNumber);
	}
	if (output->clearPads != 0u)
	{
		Platform_InputClearInstalledPadSnapshots();
		Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u race pads cleared\n", (unsigned)output->raceNumber);
	}
	/* RL-9: once per launched race, on the first idle main-menu frame after
	 * the return (or at once after an Arm or Launch failure at the title). */
	if (output->disarm != 0u)
	{
		MainArcadeRaceSetup_Disarm();
		/* LR-7: the race pacing ends on the Disarm frame; after an Arm or
		 * Launch failure no race began, and this does nothing. */
		NativeArcadeLinkHost_RaceEnd();
		state->planLevel = 0;
		state->planLevelValid = 0u;
	}
}

/*
 * LR-8 evidence, log only: the elapsedTimeMS of a launched race's first three
 * GameLogic passes (LR-8's race ticks 0..2), read after MainFrame_GameLogic
 * computed it (MainFrame.c:188-203; this runs from RenderFrame, after
 * GameLogic). The setup's RS-17 pin sets gGT->timer to 0 at race init and
 * each GameLogic adds 1, so LR-8 race tick n is the frame whose timer is
 * n + 1 once the setup is VALIDATED. LR-8 race tick n + 1 is the launch
 * core's race tick n (LR-2). One line per race tick, logged once, with the
 * root-counter phase the LR-8 pin set (sdata->rcntTotalUnits and
 * gGT->clockFrameStart as this GameLogic left them, before this pass's
 * RenderVSYNC); the two-process gate (tools/arcade-link-launch-check.ps1)
 * requires the three lines of every race and compares them across the
 * cabinets. Reads only: nothing here feeds the core, the host, or the game.
 */
static void MainArcadeRaceLaunch_LogElapsed(const struct GameTracker *gGT, uint32_t raceNumber)
{
	struct MainArcadeRaceLaunchState *state = &s_mainArcadeRaceLaunch;
	uint32_t tick;

	if ((state->planLevelValid == 0u) || ((int32_t)gGT->levelID != state->planLevel) ||
	    (MainArcadeRaceSetup_Status() != MAIN_ARCADE_RACE_SETUP_VALIDATED) || (gGT->timer < 1) || (gGT->timer > 3))
	{
		return;
	}
	if (state->elapsedRace != raceNumber)
	{
		state->elapsedRace = raceNumber;
		state->elapsedLogged = 0u;
	}
	tick = (uint32_t)gGT->timer - 1u;
	if (((state->elapsedLogged >> tick) & 1u) != 0u)
	{
		return;
	}
	state->elapsedLogged = (uint8_t)(state->elapsedLogged | (1u << tick));
	Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "race %u LR-8 race tick %u elapsedTimeMS %ld (timer %ld) rcntTotalUnits %ld clockFrameStart %ld\n",
		(unsigned)raceNumber, (unsigned)tick, (long)gGT->elapsedTimeMS, (long)gGT->timer, (long)sdata->rcntTotalUnits,
		(long)gGT->clockFrameStart);
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
	if (!MainArcadeRaceLaunchCore_Step(&state->core, &input, &output))
	{
		/* A refused Step changes nothing, so the finish latch is kept too. */
		if (state->refusedLogged == 0u)
		{
			state->refusedLogged = 1u;
			Platform_Log(MAIN_ARCADE_RACE_LAUNCH_LOG "the launch core refused a frame (phase %u, start %u, racing %u)\n", (unsigned)state->core.phase,
				(unsigned)input.startRace, (unsigned)input.hostRacing);
		}
		return;
	}
	state->refusedLogged = 0u;
	if (output.armAndLaunch != 0u)
	{
		MainArcadeRaceLaunch_ArmAndLaunch(&output);
	}
	if (output.driveStep != 0u)
	{
		MainArcadeRaceLaunch_Drive(gGT, &output);
	}
	/* The host's raceFinished input for its next Tick: the core's finish
	 * latch, held from the finish frame (a FINISHED drive result, so copied
	 * after the drive tick) until the core first sees the flow off RACING
	 * (or a new START_RACE). */
	state->raceFinishedInput = output.raceFinishedInput;
	MainArcadeRaceLaunch_Apply(gGT, &output);
	MainArcadeRaceLaunch_LogElapsed(gGT, output.raceNumber);
}

#endif
