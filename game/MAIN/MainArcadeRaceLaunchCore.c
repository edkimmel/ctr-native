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

/* The end frame of a launched race: the return step now when no race-track
 * load is running, else deferred to the first frame with the stage IDLE. */
static void MainArcadeRaceLaunchCore_End(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                         struct MainArcadeRaceLaunchCoreOutput *output)
{
	core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_ENDED;
	core->waitTicks = 0u;
	if ((input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE) || (input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_REQUESTED))
	{
		output->requestReturn = 1u;
		core->returnPending = 0u;
		core->returnDone = 1u;
	}
	else
	{
		core->returnPending = 1u;
		core->returnDone = 0u;
	}
}

/* An RL-11 failure of a launched race. */
static void MainArcadeRaceLaunchCore_Fail(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                          struct MainArcadeRaceLaunchCoreOutput *output, uint32_t failure)
{
	output->reportFailure = 1u;
	output->failure = failure;
	MainArcadeRaceLaunchCore_End(core, input, output);
}

/* The window wait: launch on an open window, else fail on the bound. Nothing
 * was armed yet, so a failure only reports and the core is idle again. */
static void MainArcadeRaceLaunchCore_TryWindow(struct MainArcadeRaceLaunchCore *core, const struct MainArcadeRaceLaunchCoreInput *input,
                                               struct MainArcadeRaceLaunchCoreOutput *output)
{
	output->raceNumber = core->raceNumber;
	if (input->titleWindowOpen != 0u)
	{
		output->armAndLaunch = 1u;
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT;
		return;
	}
	if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_WINDOW_TIMEOUT_TICKS)
	{
		output->reportFailure = 1u;
		output->failure = MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT;
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE;
		core->raceNumber = 0u;
		core->waitTicks = 0u;
	}
}

/* An ended race: the held START_RACE, then the deferred return step, the
 * pad clear, and the Disarm. */
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
				output->reportFailure = 1u;
				output->failure = MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT;
				output->raceNumber = core->heldRaceNumber;
				core->held = 0u;
			}
		}
	}
	else if ((input->startRace != 0u) && (input->hostRacing != 0u))
	{
		core->startedRaces++;
		core->heldRaceNumber = core->startedRaces;
		core->heldTicks = 0u;
		core->held = 1u;
	}

	if (core->returnDone == 0u)
	{
		if ((core->returnPending != 0u) && (input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE))
		{
			output->requestReturn = 1u;
			core->returnPending = 0u;
			core->returnDone = 1u;
		}
		return;
	}

	idleMainMenu = MainArcadeRaceLaunchCore_IdleMainMenu(input);
	if ((core->padsInstalled != 0u) && ((input->loadingBit != 0u) || (idleMainMenu != 0)))
	{
		output->clearPads = 1u;
		core->padsInstalled = 0u;
	}
	if (idleMainMenu == 0)
	{
		return;
	}
	output->disarm = 1u;
	core->returnDone = 0u;
	core->returnPending = 0u;
	core->waitTicks = 0u;
	if (core->held != 0u)
	{
		/* The held race takes over its window wait; the window is checked
		 * from the next frame, after this Disarm. */
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_WINDOW;
		core->raceNumber = core->heldRaceNumber;
		core->waitTicks = core->heldTicks;
		core->held = 0u;
	}
	else
	{
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE;
		core->raceNumber = 0u;
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

	status = input->setupStatus;
	output->raceNumber = core->raceNumber;
	switch (core->phase)
	{
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE:
	{
		if ((input->startRace != 0u) && (input->hostRacing != 0u))
		{
			core->startedRaces++;
			core->raceNumber = core->startedRaces;
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
			/* Quiet abort: nothing armed, nothing to return from. */
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
			MainArcadeRaceLaunchCore_End(core, input, output);
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
			MainArcadeRaceLaunchCore_Fail(core, input, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED);
		}
		else if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS)
		{
			MainArcadeRaceLaunchCore_Fail(core, input, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_VALIDATE_TIMEOUT);
		}
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_RACE_TICK:
	{
		if (input->hostRacing == 0u)
		{
			MainArcadeRaceLaunchCore_End(core, input, output);
			break;
		}
		core->waitTicks++;
		if (status != MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED)
		{
			MainArcadeRaceLaunchCore_Fail(core, input, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED);
		}
		else if ((input->onPlanLevel != 0u) && (input->loadingStage == MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE) && (input->loadingBit == 0u))
		{
			output->raceTickZero = 1u;
			core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_REHEARSAL;
			core->waitTicks = 0u;
		}
		else if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS)
		{
			MainArcadeRaceLaunchCore_Fail(core, input, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_RACE_TICK_TIMEOUT);
		}
		break;
	}
	case MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_REHEARSAL:
	{
		if (input->hostRacing == 0u)
		{
			MainArcadeRaceLaunchCore_End(core, input, output);
			break;
		}
		core->waitTicks++;
		if (status != MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED)
		{
			MainArcadeRaceLaunchCore_Fail(core, input, output, MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED);
		}
		else if (core->waitTicks >= MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_REHEARSAL_TICKS)
		{
			output->reportFinished = 1u;
			MainArcadeRaceLaunchCore_End(core, input, output);
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
	return 1;
}

int MainArcadeRaceLaunchCore_LaunchResult(struct MainArcadeRaceLaunchCore *core, uint32_t result, struct MainArcadeRaceLaunchCoreOutput *output)
{
	if ((core == NULL) || (output == NULL) || (core->phase != MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT))
	{
		return 0;
	}
	if (result == MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCHED)
	{
		output->leaveTitle = 1u;
		output->installPads = 1u;
		core->padsInstalled = 1u;
		core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_VALIDATED;
		core->waitTicks = 0u;
		return 1;
	}
	if ((result != MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_ARM_FAILED) && (result != MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCH_FAILED))
	{
		return 0;
	}
	/* The RL-9 exception: Disarm at once at the title. Nothing left the
	 * title and no pads were installed, so no return and no clear. */
	output->reportFailure = 1u;
	output->failure =
	    (result == MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_ARM_FAILED) ? MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_ARM : MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_LAUNCH;
	output->raceNumber = core->raceNumber;
	output->disarm = 1u;
	core->phase = MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE;
	core->raceNumber = 0u;
	core->waitTicks = 0u;
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
