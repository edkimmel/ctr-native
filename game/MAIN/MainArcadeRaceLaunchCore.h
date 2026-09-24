#ifndef MAIN_ARCADE_RACE_LAUNCH_CORE_H
#define MAIN_ARCADE_RACE_LAUNCH_CORE_H

#include <stdint.h>

/*
 * Race launch decision core (docs/RACE_LAUNCH_MILESTONE.md section 4,
 * RL-8..RL-11, slice RL-S7). The per-frame decisions of the live race caller
 * (RL-S8b) for one networked race after another: the launch from the title
 * window, the bounded waits, race tick 0, the launch rehearsal, the return to
 * the main-menu level, the rehearsal pad install and clear, the Disarm point,
 * and the failure mapping. The caller samples one frame's facts into
 * struct MainArcadeRaceLaunchCoreInput, calls MainArcadeRaceLaunchCore_Step,
 * and applies the returned decisions; it decides nothing itself.
 *
 * Built as the standalone library ctr_native_arcade_race_launch_core (C17,
 * extensions off), never unity-included. Pure: it reads no game global, calls
 * no game, platform, host, or setup function, uses no heap and no I/O, and
 * keeps no static state; all state is the caller-owned
 * struct MainArcadeRaceLaunchCore. Zero-initialize it, or call
 * MainArcadeRaceLaunchCore_Init (the same thing), once before the first step;
 * there is nothing to shut down.
 *
 * Frames. Every count is in calls of MainArcadeRaceLaunchCore_Step (one per
 * rendered frame). The frame a wait begins on is its frame 0; the wait's
 * count grows by one on each later step.
 *
 * Launch and the Arm/Launch feedback (two calls on one frame). START_RACE
 * starts a race only from the idle core and only when the host is RACING on
 * that frame. The core then waits for the title window, checking it from the
 * START_RACE frame on. On the frame the window is open, Step sets
 * armAndLaunch and nothing else. The caller then calls MainArcadeRaceSetup_Arm
 * with the agreed config and, if that succeeds, MainArcadeRaceSetup_Launch,
 * and passes the outcome to MainArcadeRaceLaunchCore_LaunchResult on the same
 * frame with the same output struct, which adds that frame's remaining
 * decisions:
 * - LAUNCHED: leaveTitle and installPads (the caller leaves the title, then
 *   installs the rehearsal pads). The pads are installed from this Launch
 *   frame on, on every frame, until the clear.
 * - ARM_FAILED or LAUNCH_FAILED: reportFailure (ARM or LAUNCH) and disarm on
 *   that frame (the RL-9 exception: a failed Launch wrote no field), and the
 *   core is idle again. No return load is requested: nothing left the title
 *   (LeaveTitle never ran and no race load was requested), so the cabinet is
 *   still on the idle main-menu level the window required. No pads were
 *   installed, so none are cleared.
 * While a launch result is due, Step refuses (returns 0 with a zeroed output)
 * and the state is unchanged.
 *
 * Bounded waits (RL-8), each an RL-11 failure on expiry:
 * - the title window: frame 0 is the START_RACE frame; an open window is
 *   taken on any frame up to and including frame
 *   LAUNCH_WINDOW_TIMEOUT_TICKS, and a window still closed on that frame is
 *   WINDOW_TIMEOUT;
 * - VALIDATED: frame 0 is the Launch frame; setup status VALIDATED is taken
 *   on frames 1 to LAUNCH_VALIDATE_TIMEOUT_TICKS, and anything else on that
 *   last frame is VALIDATE_TIMEOUT;
 * - race tick 0: frame 0 is the frame the core first sees VALIDATED; race
 *   tick 0 is the first later frame (1 to LAUNCH_VALIDATE_TIMEOUT_TICKS) that
 *   is on the plan's level with the loading stage IDLE and the LOADING bit
 *   clear; none by that last frame is RACE_TICK_TIMEOUT. VALIDATED on the
 *   main-menu level is never race tick 0.
 * Before Launch (the window wait) a failure only reports: nothing was armed
 * and nothing left the title, so there is no return, clear, or Disarm.
 *
 * Rehearsal (RL-10). Frame 0 is race tick 0; on frame LAUNCH_REHEARSAL_TICKS
 * the core reports the race finished (reportFinished, the host's raceFinished
 * input).
 *
 * Setup failure (RL-11). From the frame after Launch until the rehearsal
 * ends (the finish frame included, checked before the finish), a setup status
 * the race cannot be in is SETUP_FAILED: FAILED at any point; IDLE or ARMED
 * before VALIDATED; anything but VALIDATED from VALIDATED on. After the race
 * has ended (finished, failed, or aborted) the setup status is ignored, so a
 * later status change is never reported: the expected FAILED/LEVEL_MISMATCH
 * of an abort whose return load replaced the queued race level stays local.
 *
 * Flow leaving RACING. The host being off RACING is checked first on every
 * frame of a race. Before Launch it ends the race quietly (nothing armed, no
 * report, no return, no Disarm). From Launch on it ends the race without a
 * report (the host no longer takes one) and runs the end steps below. A
 * failure is never reported for a race the flow has left, and a race reports
 * at most one of finished and failure.
 *
 * End steps (RL-8, RL-10), for the finish frame, every RL-11 failure frame
 * after Launch, and the first frame seen off RACING after Launch (the end
 * frame):
 * - requestReturn (the caller sets boolDemoMode 0, numPlyrNextGame 1, and
 *   mainMenuState MAIN_MENU_TITLE, then requests the main-menu level load):
 *   on the end frame when the loading stage is IDLE or REQUESTED, otherwise
 *   (a race-track load is running) on the first later frame with the stage
 *   IDLE. Exactly once.
 * - clearPads: never on the end frame and never before the return step's
 *   frame. On the first frame after the return step's frame with the LOADING
 *   bit set or that is an idle main-menu frame, whichever comes first.
 *   Exactly once. installPads stays set on every frame before the clear
 *   (the end frame included) and is never set after it. Counting from the
 *   return step rather than the end frame is how a return deferred behind a
 *   race-track load keeps the neutral pads installed while the race level
 *   runs GameLogic between that load and the return load.
 * - disarm (RL-9): on the first idle main-menu frame (the main-menu level,
 *   the loading stage IDLE, the LOADING bit clear) after the return step's
 *   frame; aborts included. Exactly once per launched race, and never off the
 *   idle main-menu level. The clear lands on that frame at the latest. The
 *   race is over (the core idle again) on the Disarm frame.
 *
 * Two races in a row. The race number counts accepted START_RACE events
 * (1 for the first). A START_RACE that arrives while an ended race still
 * waits for its Disarm is held: the host must be RACING on that frame, and
 * the held race's window wait counts from it. It is dropped quietly when the
 * host leaves RACING, and reports WINDOW_TIMEOUT if it is still held on frame
 * LAUNCH_WINDOW_TIMEOUT_TICKS (checked before that frame's Disarm). On the
 * Disarm frame the held race takes over the window wait, which is checked
 * from the next frame, so armAndLaunch always follows the previous race's
 * Disarm on an earlier frame. START_RACE during a race in progress is
 * ignored.
 *
 * The caller's order on one frame: Step; if armAndLaunch, Arm, Launch, and
 * LaunchResult; then leaveTitle, reportFailure or reportFinished,
 * requestReturn, installPads or clearPads, and disarm, as set.
 */

/* RL-8 and RL-10 bounds, in core steps (frames). */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_WINDOW_TIMEOUT_TICKS   900u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS 1800u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_REHEARSAL_TICKS        150u

/* Mirrors of enum MainArcadeRaceSetupStatus (game/MAIN/MainArcadeRaceSetupCore.h). */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_IDLE                    0u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_ARMED                   1u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED                2u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED                  3u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED               4u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED                  5u

/* The loading stage, classified by the caller: IDLE is retail LOAD_IDLE,
 * REQUESTED is LOAD_REQUESTED, OTHER is any other stage (a load running). */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE                    0u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_REQUESTED               1u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_OTHER                   2u

/* The outcome of the caller's Arm and Launch on an armAndLaunch frame. */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCHED               1u /* Arm and Launch both returned 1 */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_ARM_FAILED             2u /* Arm returned 0 (Launch not called) */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCH_FAILED          3u /* Arm returned 1, Launch returned 0 */

/* The RL-11 local failure reasons. Append-only: the codes are logged. */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_NONE                  0u
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_ARM                   1u /* Arm failed at the title */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_LAUNCH                2u /* Launch failed at the title */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT        3u /* no title window in time */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_VALIDATE_TIMEOUT      4u /* no VALIDATED in time after Launch */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_RACE_TICK_TIMEOUT     5u /* no race tick 0 in time after VALIDATED */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED          6u /* the setup status failed the race */

/* The core's phases (struct MainArcadeRaceLaunchCore.phase). */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE                    0u /* no race */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_WINDOW             1u /* START_RACE seen; waiting for the title window */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT           2u /* armAndLaunch emitted; LaunchResult due this frame */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_VALIDATED          3u /* launched; waiting for VALIDATED */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_RACE_TICK          4u /* validated; waiting for race tick 0 */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_REHEARSAL               5u /* race tick 0 seen; counting to the finish */
#define MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_ENDED                   6u /* ended; return, clear, and Disarm pending */

/* Caller-owned state. The fields are the core's; the caller only zeroes it. */
struct MainArcadeRaceLaunchCore
{
	uint32_t phase;          /* MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_* */
	uint32_t startedRaces;   /* accepted START_RACE events, held ones included */
	uint32_t raceNumber;     /* the current race's number (0 while idle) */
	uint32_t waitTicks;      /* frames since the current wait's frame 0 */
	uint32_t heldRaceNumber; /* the held race's number (valid while held is 1) */
	uint32_t heldTicks;      /* frames since the held START_RACE */
	uint8_t padsInstalled;   /* 1 from the Launch frame until the clear */
	uint8_t returnPending;   /* 1 while the return step waits for the stage IDLE */
	uint8_t returnDone;      /* 1 once the return step has run (read from the next frame on) */
	uint8_t held;            /* 1 while a START_RACE waits for the Disarm */
};

/* One frame's facts. Every uint8_t flag is 0 or 1 (nonzero counts as 1). */
struct MainArcadeRaceLaunchCoreInput
{
	/* MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_*: MainArcadeRaceSetup_Status() */
	uint32_t setupStatus;
	/* MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_*: the class of Loading.stage */
	uint32_t loadingStage;
	/* 1: the host's tick returned START_RACE this frame */
	uint8_t startRace;
	/* 1: the host flow is on RACING (NativeArcadeLinkHost_Racing) */
	uint8_t hostRacing;
	/* 1: the title launch window is open (MainArcadeLink_TitleMenuReady) */
	uint8_t titleWindowOpen;
	/* 1: the current level is the main-menu level */
	uint8_t onMainMenuLevel;
	/* 1: the current level is the race plan's level */
	uint8_t onPlanLevel;
	/* 1: the LOADING bit of gameMode1 is set */
	uint8_t loadingBit;
	uint8_t reserved[2];
};

/* One frame's decisions. Every uint8_t field is 0 or 1. */
struct MainArcadeRaceLaunchCoreOutput
{
	/* MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_*: the reason, when reportFailure */
	uint32_t failure;
	/* The race this frame's armAndLaunch, validated, raceTickZero,
	 * reportFinished, or reportFailure concerns; otherwise the current race
	 * (0 while idle). */
	uint32_t raceNumber;
	/* Arm the setup with the agreed config and Launch it, then call
	 * MainArcadeRaceLaunchCore_LaunchResult on this frame. */
	uint8_t armAndLaunch;
	/* Leave the title (the caller's copy of the roster proof's LeaveTitle). */
	uint8_t leaveTitle;
	/* Install the neutral rehearsal pads this frame. */
	uint8_t installPads;
	/* Clear the installed pads (Platform_InputClearInstalledPadSnapshots). */
	uint8_t clearPads;
	/* Report the race finished to the host (its raceFinished input). */
	uint8_t reportFinished;
	/* Report a local race failure to the host and log failure. */
	uint8_t reportFailure;
	/* numPlyrNextGame 1 and the main-menu level load (the return step). */
	uint8_t requestReturn;
	/* Disarm the setup. */
	uint8_t disarm;
	/* Event: the first frame the setup is seen VALIDATED (the RL-12 log). */
	uint8_t validated;
	/* Event: race tick 0. */
	uint8_t raceTickZero;
	uint8_t reserved[2];
};

/* Zeroes *core (idle, no race started). Does nothing for NULL. */
void MainArcadeRaceLaunchCore_Init(struct MainArcadeRaceLaunchCore *core);

/*
 * One frame. Fills *output (always zeroed first) and returns 1. Returns 0
 * with *output zeroed (when output is not NULL) and *core unchanged when a
 * pointer is NULL, when setupStatus or loadingStage is out of range, or when
 * a launch result is due (see LaunchResult).
 */
int MainArcadeRaceLaunchCore_Step(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                  struct MainArcadeRaceLaunchCoreOutput *output);

/*
 * The outcome of the caller's Arm and Launch on the frame Step set
 * armAndLaunch (MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_*). Adds that frame's
 * remaining decisions to *output, which must be the struct that frame's Step
 * filled (its other fields are kept), and returns 1. Returns 0, touching
 * nothing, when a pointer is NULL, when no launch result is due, or when
 * result is not a RESULT_* value.
 */
int MainArcadeRaceLaunchCore_LaunchResult(struct MainArcadeRaceLaunchCore *core, uint32_t result, struct MainArcadeRaceLaunchCoreOutput *output);

/* A stable name for a MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_* code ("UNKNOWN"
 * for any other value). */
const char *MainArcadeRaceLaunchCore_FailureName(uint32_t failure);

#endif
