#include "MAIN/MainArcadeRaceLaunchCore.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Race launch decision core (docs/RACE_LAUNCH_MILESTONE.md section 4,
 * RL-8..RL-11, slice RL-S7). See MAIN/MainArcadeRaceLaunchCore.h for the rules.
 */

/* The main-menu level with no load in progress and the LOADING bit clear. */
static int MainArcadeRaceLaunchCore_IdleMainMenu(const struct MainArcadeRaceLaunchCoreInput *input)
{
	return (input->onMainMenuLevel != 0u) && (input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE) && (input->loadingBit == 0u);
}

/* The race is over: the core is idle again, or a held START_RACE takes over
 * its window wait, which is checked from the next frame. */
static void MainArcadeRaceLaunchCore_Over(struct MainArcadeRaceLaunchCore *core)
{
	core->returnPending = 0u;
	core->raceNumber = 0u;
	if (core->held != 0u)
	{
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_WINDOW;
		core->waitTicks = core->heldTicks;
		core->held = 0u;
	}
	else
	{
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE;
		core->waitTicks = 0u;
	}
}

/* A return step owed by an ended race: on this frame when no race-track load
 * is running (the stage IDLE or REQUESTED), else deferred to the first frame
 * with the stage IDLE. A race with no pads to clear and no Disarm due is over
 * on its return step's frame. */
static void MainArcadeRaceLaunchCore_Return(struct MainArcadeRaceLaunchCore *core, uint32_t loadingStage, struct MainArcadeRaceLaunchCoreOutput *output)
{
	if ((loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE) || (loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_REQUESTED))
	{
		output->requestReturn = 1u;
		core->returnPending = 0u;
		if ((core->padsInstalled == 0u) && (core->disarmPending == 0u))
		{
			MainArcadeRaceLaunchCore_Over(core);
		}
	}
	else
	{
		core->returnPending = 1u;
	}
}

/* The end frame of a race: the return step, then the clear and the Disarm on
 * later frames (MainArcadeRaceLaunchCore_StepEnded). */
static void MainArcadeRaceLaunchCore_End(struct MainArcadeRaceLaunchCore *core, uint32_t loadingStage, struct MainArcadeRaceLaunchCoreOutput *output)
{
	core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_ENDED;
	core->waitTicks = 0u;
	core->returnPending = 0u;
	MainArcadeRaceLaunchCore_Return(core, loadingStage, output);
}

/* An RL-11 failure: report it and end the race. */
static void MainArcadeRaceLaunchCore_Fail(struct MainArcadeRaceLaunchCore *core, uint32_t loadingStage, struct MainArcadeRaceLaunchCoreOutput *output,
                                          uint32_t failure)
{
	output->reportFailure = 1u;
	output->failure = failure;
	MainArcadeRaceLaunchCore_End(core, loadingStage, output);
}

/* The window wait: launch on an open window (the launch number is taken
 * here), else fail on the bound. Nothing is armed or installed before the
 * launch, so a WINDOW_TIMEOUT owes the return step only. */
static void MainArcadeRaceLaunchCore_TryWindow(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                               struct MainArcadeRaceLaunchCoreOutput *output)
{
	if (input->titleWindowOpen != 0u)
	{
		core->launches++;
		core->raceNumber = core->launches;
		core->launchStage = (uint8_t)input->loadingStage;
		output->raceNumber = core->raceNumber;
		output->armAndLaunch = 1u;
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT;
		return;
	}
	if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_WINDOW_TIMEOUT_TICKS)
	{
		output->raceNumber = 0u;
		MainArcadeRaceLaunchCore_Fail(core, input->loadingStage, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT);
	}
}

/* An ended race: the held START_RACE, then the (deferred) return step, the
 * pad clear, and the Disarm. Nothing else runs on a return step's frame. */
static void MainArcadeRaceLaunchCore_StepEnded(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                               struct MainArcadeRaceLaunchCoreOutput *output)
{
	int idleMainMenu;

	if (core->held != 0u)
	{
		if (input->hostRacing == 0u)
		{
			core->held = 0u;
		}
		else
		{
			core->heldTicks++;
			if (core->heldTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_WINDOW_TIMEOUT_TICKS)
			{
				/* The held race never launched, so it has no number. Its
				 * return step merges with one already pending. */
				output->reportFailure = 1u;
				output->failure = MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT;
				output->raceNumber = 0u;
				core->held = 0u;
				if (core->returnPending == 0u)
				{
					MainArcadeRaceLaunchCore_Return(core, input->loadingStage, output);
				}
			}
		}
	}
	else if (input->startRace != 0u)
	{
		core->heldTicks = 0u;
		core->held = 1u;
	}

	if (output->requestReturn != 0u)
	{
		return;
	}
	if (core->returnPending != 0u)
	{
		if (input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE)
		{
			MainArcadeRaceLaunchCore_Return(core, input->loadingStage, output);
		}
		return;
	}

	idleMainMenu = MainArcadeRaceLaunchCore_IdleMainMenu(input);
	if ((core->padsInstalled != 0u) && ((input->loadingBit != 0u) || (idleMainMenu != 0)))
	{
		output->clearPads = 1u;
		core->padsInstalled = 0u;
	}
	if (core->disarmPending != 0u)
	{
		if (idleMainMenu == 0)
		{
			return;
		}
		output->disarm = 1u;
		core->disarmPending = 0u;
	}
	if (core->padsInstalled == 0u)
	{
		MainArcadeRaceLaunchCore_Over(core);
	}
}

void MainArcadeRaceLaunchCore_Init(struct MainArcadeRaceLaunchCore *core)
{
	if (core != NULL)
	{
		memset(core, 0, sizeof(*core));
	}
}

int MainArcadeRaceLaunchCore_Step(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                  struct MainArcadeRaceLaunchCoreOutput *output)
{
	uint32_t status;

	if (output != NULL)
	{
		memset(output, 0, sizeof(*output));
	}
	if ((core == NULL) || (input == NULL) || (output == NULL))
	{
		return 0;
	}
	if ((core->phase == MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT) || (core->phase > MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_ENDED) ||
	    (input->setupStatus > MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED) || (input->loadingStage > MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_OTHER))
	{
		return 0;
	}
	if ((input->startRace > 1u) || (input->hostRacing > 1u) || (input->titleWindowOpen > 1u) || (input->onMainMenuLevel > 1u) || (input->onPlanLevel > 1u) ||
	    (input->loadingBit > 1u))
	{
		return 0;
	}
	/* START_RACE enters RACING on the same host tick, so hostRacing sampled
	 * after that tick is 1; anything else is a caller error. */
	if ((input->startRace != 0u) && (input->hostRacing == 0u))
	{
		return 0;
	}

	/* The finish latch: the flow off RACING or a new START_RACE clears it
	 * before this frame's decisions. */
	if ((input->hostRacing == 0u) || (input->startRace != 0u))
	{
		core->finishedPending = 0u;
	}

	status = input->setupStatus;
	output->raceNumber = core->raceNumber;
	switch (core->phase)
	{
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE:
	{
		if (input->startRace != 0u)
		{
			core->raceNumber = 0u;
			core->waitTicks = 0u;
			core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_WINDOW;
			MainArcadeRaceLaunchCore_TryWindow(core, input, output);
		}
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_WINDOW:
	{
		if (input->hostRacing == 0u)
		{
			/* Quiet abort, checked before the bound: nothing armed, nothing
			 * to return from. */
			core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE;
			core->raceNumber = 0u;
			core->waitTicks = 0u;
			break;
		}
		core->waitTicks++;
		MainArcadeRaceLaunchCore_TryWindow(core, input, output);
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_VALIDATED:
	{
		if (input->hostRacing == 0u)
		{
			MainArcadeRaceLaunchCore_End(core, input->loadingStage, output);
			break;
		}
		core->waitTicks++;
		if (status == MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED)
		{
			output->validated = 1u;
			core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_RACE_TICK;
			core->waitTicks = 0u;
		}
		else if ((status != MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED) && (status != MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED))
		{
			MainArcadeRaceLaunchCore_Fail(core, input->loadingStage, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED);
		}
		else if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS)
		{
			MainArcadeRaceLaunchCore_Fail(core, input->loadingStage, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_VALIDATE_TIMEOUT);
		}
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_RACE_TICK:
	{
		if (input->hostRacing == 0u)
		{
			MainArcadeRaceLaunchCore_End(core, input->loadingStage, output);
			break;
		}
		core->waitTicks++;
		if (status != MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED)
		{
			MainArcadeRaceLaunchCore_Fail(core, input->loadingStage, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED);
		}
		else if ((input->onPlanLevel != 0u) && (input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE) && (input->loadingBit == 0u))
		{
			output->raceTickZero = 1u;
			core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_REHEARSAL;
			core->waitTicks = 0u;
		}
		else if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS)
		{
			MainArcadeRaceLaunchCore_Fail(core, input->loadingStage, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_RACE_TICK_TIMEOUT);
		}
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_REHEARSAL:
	{
		if (input->hostRacing == 0u)
		{
			MainArcadeRaceLaunchCore_End(core, input->loadingStage, output);
			break;
		}
		core->waitTicks++;
		if (status != MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED)
		{
			MainArcadeRaceLaunchCore_Fail(core, input->loadingStage, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED);
		}
		else if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_REHEARSAL_TICKS)
		{
			output->reportFinished = 1u;
			core->finishedPending = 1u;
			MainArcadeRaceLaunchCore_End(core, input->loadingStage, output);
		}
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_ENDED:
	{
		MainArcadeRaceLaunchCore_StepEnded(core, input, output);
		break;
	}
	default:
	{
		break;
	}
	}

	output->installPads = core->padsInstalled;
	output->raceFinishedInput = core->finishedPending;
	return 1;
}

int MainArcadeRaceLaunchCore_LaunchResult(struct MainArcadeRaceLaunchCore *core, uint32_t result, struct MainArcadeRaceLaunchCoreOutput *output)
{
	uint32_t failure;

	if ((core == NULL) || (output == NULL) || (core->phase != MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT))
	{
		return 0;
	}
	if (result == MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCHED)
	{
		output->leaveTitle = 1u;
		output->installPads = 1u;
		core->padsInstalled = 1u;
		core->disarmPending = 1u;
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_VALIDATED;
		core->waitTicks = 0u;
		return 1;
	}
	if (result == MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_ARM_FAILED)
	{
		failure = MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_ARM;
	}
	else if (result == MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCH_FAILED)
	{
		failure = MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_LAUNCH;
	}
	else
	{
		return 0;
	}
	/* The RL-9 exception: Disarm at once at the title. No pads were
	 * installed, so no clear. The return step as for every RL-11 failure,
	 * against the loading stage of this (the armAndLaunch) frame. */
	output->raceNumber = core->raceNumber;
	output->disarm = 1u;
	core->disarmPending = 0u;
	MainArcadeRaceLaunchCore_Fail(core, core->launchStage, output, failure);
	return 1;
}

const char *MainArcadeRaceLaunchCore_FailureName(uint32_t failure)
{
	switch (failure)
	{
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_NONE:
		return "NONE";
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_ARM:
		return "ARM";
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_LAUNCH:
		return "LAUNCH";
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT:
		return "WINDOW_TIMEOUT";
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_VALIDATE_TIMEOUT:
		return "VALIDATE_TIMEOUT";
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_RACE_TICK_TIMEOUT:
		return "RACE_TICK_TIMEOUT";
	case MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED:
		return "SETUP_FAILED";
	default:
		return "UNKNOWN";
	}
}
