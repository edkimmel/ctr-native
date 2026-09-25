/*
 * Internal arcade-link autopilot glue (docs/RACE_LAUNCH_MILESTONE.md RL-15,
 * slice RL-S10): drives one link cabinet through START, the select items,
 * race 1, REMATCH, race 2, REMATCH, race 3, and EXIT (since LR-S13 part B
 * the LR-16 scenario of the linked-race plan, LR-75) for the
 * two-process live gate (tools/arcade-link-launch-check.ps1, ctest
 * arcade_link_launch), then writes the report and exits with the result
 * code.
 *
 * Every decision is the pure platform/native_arcade_link_autopilot.c's; this
 * file reads the host view, the hook's enter window (the arcade-link
 * policy's own rule), the agreed match, and the race caller's RL-12
 * evidence, and hands the decision back to the arcade-link hook, which feeds
 * it to NativeArcadeLinkHost_Enter and NativeArcadeLinkHost_Tick. It never
 * touches a pad: the race caller owns the installed pads (RL-10, LR-16).
 *
 * Native only; the autopilot exists only in internal builds (main.c rejects
 * the option elsewhere), so without CTR_INTERNAL the two frame entries are
 * empty. The state is file-scope static and host-local: it never enters a
 * saved state, a recording, or canonical state, and nothing here is sent to
 * the peer.
 *
 * Unity-included after the roster proof launcher; the hook calls it through
 * MAIN/MainArcadeLinkAutopilot.h.
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_autopilot.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_log.h"
#include "MAIN/MainArcadeLinkPolicy.h"
#include "MAIN/MainArcadeRaceLaunch.h"
#include "MAIN/MainArcadeLinkAutopilot.h"

#if defined(CTR_INTERNAL)

#define MAIN_ARCADE_LINK_AUTOPILOT_LOG "[CTR Native] arcade link autopilot: "

_Static_assert(NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES == MAIN_ARCADE_RACE_LAUNCH_DIGEST_BYTES, "the report copies the race caller's RL-12 digests");
_Static_assert(NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT == 4u, "the race caller keeps four RL-12 digests");

struct MainArcadeLinkAutopilotState
{
	/* 1 once configured from --arcade-link-autopilot */
	uint8_t active;
	/* 1 once the report was written and the exit requested */
	uint8_t finished;
	uint8_t reserved[2];
	/* The pure run state (include/platform/native_arcade_link_autopilot.h). */
	struct NativeArcadeLinkAutopilot autopilot;
	char reportPath[NATIVE_ARCADE_LINK_AUTOPILOT_PATH_BYTES];
};

static struct MainArcadeLinkAutopilotState s_mainArcadeLinkAutopilot;

void MainArcadeLinkAutopilot_Configure(const struct NativeArcadeLinkAutopilotOptions *options)
{
	struct MainArcadeLinkAutopilotState *state = &s_mainArcadeLinkAutopilot;

	memset(state, 0, sizeof(*state));
	if ((options == NULL) || (options->enabled == 0u))
	{
		return;
	}
	NativeArcadeLinkAutopilot_Init(&state->autopilot);
	/* The report records the race tick cap main.c handed the link host. */
	state->autopilot.raceTickLimit = options->raceTickLimit;
	/* The fault injections' race ticks (LR-73), for FaultAt and the report
	 * (LR-75). */
	state->autopilot.freezeTick = options->freezeTick;
	state->autopilot.desyncTick = options->desyncTick;
	memcpy(state->reportPath, options->reportPath, sizeof(state->reportPath));
	state->reportPath[sizeof(state->reportPath) - 1u] = '\0';
	state->active = 1u;
}

uint8_t MainArcadeLinkAutopilot_Active(void)
{
	return s_mainArcadeLinkAutopilot.active;
}

uint32_t MainArcadeLinkAutopilot_Fault(uint32_t raceTick)
{
	if (s_mainArcadeLinkAutopilot.active == 0u)
	{
		return NATIVE_ARCADE_LINK_AUTOPILOT_FAULT_NONE;
	}
	return NativeArcadeLinkAutopilot_FaultAt(&s_mainArcadeLinkAutopilot.autopilot, raceTick);
}

/*
 * The hook's enter window for this frame: the arcade-link policy's own
 * enterPressed for this frame's facts with a fresh START (held now, not last
 * frame), so the autopilot enters exactly where a player's START would. Also
 * returns that decision's demo-countdown reset.
 */
static uint8_t MainArcadeLinkAutopilot_EnterReady(const struct MainArcadeLinkPolicyInput *input, uint8_t *resetDemoCountdown)
{
	struct MainArcadeLinkPolicyInput probe = *input;
	struct MainArcadeLinkPolicyOutput probeOutput;

	*resetDemoCountdown = 0u;
	probe.rawHeld = MAIN_ARCADE_LINK_POLICY_BTN_START;
	probe.prevRawHeld = 0u;
	if (!MainArcadeLinkPolicy_Decide(&probe, &probeOutput))
	{
		return 0u;
	}
	*resetDemoCountdown = probeOutput.resetDemoCountdown;
	return probeOutput.enterPressed;
}

void MainArcadeLinkAutopilot_Input(const struct MainArcadeLinkPolicyInput *input, struct MainArcadeLinkPolicyOutput *output)
{
	struct MainArcadeLinkAutopilotState *state = &s_mainArcadeLinkAutopilot;
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkAutopilotOutput decision;
	uint8_t enterReady;
	uint8_t resetDemoCountdown = 0u;

	if (state->active == 0u)
	{
		return;
	}
	if ((input == NULL) || (output == NULL))
	{
		return;
	}
	/* On the owned LINK frames (the only frames this entry runs on) the
	 * autopilot owns the link host's local menu input: no local pad or
	 * keyboard input reaches it there. Race and tick-only frames pass pad
	 * input unchanged; RACING ignores menu events. */
	output->heldButtons = 0u;
	output->enterPressed = 0u;
	if (!NativeArcadeLinkHost_GetView(&view))
	{
		return;
	}
	enterReady = MainArcadeLinkAutopilot_EnterReady(input, &resetDemoCountdown);
	if (!NativeArcadeLinkAutopilot_Decide(&state->autopilot, &view, enterReady, &decision))
	{
		return;
	}
	output->heldButtons = decision.heldButtons;
	if ((decision.enter != 0u) && (enterReady != 0u))
	{
		output->enterPressed = 1u;
		if (resetDemoCountdown != 0u)
		{
			output->resetDemoCountdown = 1u;
		}
		Platform_Log(MAIN_ARCADE_LINK_AUTOPILOT_LOG "START on the attract screen\n");
	}
}

/* Writes the report and requests the exit with the run's result code. */
static void MainArcadeLinkAutopilot_Finish(void)
{
	struct MainArcadeLinkAutopilotState *state = &s_mainArcadeLinkAutopilot;
	const struct NativeArcadeLinkAutopilot *autopilot = &state->autopilot;
	int exitCode = (int)autopilot->result;

	state->finished = 1u;
	if (!NativeArcadeLinkAutopilot_WriteReport(state->reportPath, autopilot))
	{
		Platform_Log(MAIN_ARCADE_LINK_AUTOPILOT_LOG "could not write the report to %s\n", state->reportPath);
		if (exitCode == (int)NATIVE_ARCADE_LINK_AUTOPILOT_PASS)
		{
			exitCode = (int)NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_WRITE_FAILED;
		}
	}
	Platform_Log(MAIN_ARCADE_LINK_AUTOPILOT_LOG "%s after %u ticks (%u races started, %u validated, %u ended; last screen %s end reason %s); exit code %d\n",
		NativeArcadeLinkAutopilot_ResultName(autopilot->result), (unsigned)autopilot->ticks, (unsigned)autopilot->racesStarted,
		(unsigned)autopilot->racesValidated, (unsigned)autopilot->racesEnded, NativeArcadeLinkAutopilot_ScreenName(autopilot->lastScreen),
		NativeArcadeLinkAutopilot_EndReasonName(autopilot->lastEndReason), exitCode);
	Platform_RequestExit(exitCode);
}

void MainArcadeLinkAutopilot_AfterTick(uint32_t action)
{
	struct MainArcadeLinkAutopilotState *state = &s_mainArcadeLinkAutopilot;
	struct NativeArcadeLinkHostMatch match;
	struct NativeArcadeLinkHostView view;
	uint8_t digests[NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT * NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES];
	uint32_t raceNumber = 0u;
	uint32_t validatedRaces;

	if ((state->active == 0u) || (state->finished != 0u))
	{
		return;
	}

	/* The agreed match of a START_RACE, while the link still holds it. */
	if (action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)
	{
		if (NativeArcadeLinkHost_GetAgreedMatch(&match))
		{
			if (NativeArcadeLinkAutopilot_RecordMatch(&state->autopilot, &match))
			{
				Platform_Log(MAIN_ARCADE_LINK_AUTOPILOT_LOG "race %u started\n", (unsigned)state->autopilot.racesStarted);
			}
		}
		else
		{
			(void)NativeArcadeLinkAutopilot_RecordMatch(&state->autopilot, NULL);
		}
	}

	/* The race caller's RL-12 evidence (read only). */
	validatedRaces = MainArcadeRaceLaunch_ValidatedRaces();
	if (validatedRaces != state->autopilot.racesValidated)
	{
		const int haveDigests = MainArcadeRaceLaunch_LastValidated(&raceNumber, digests);

		if (NativeArcadeLinkAutopilot_RecordValidated(&state->autopilot, validatedRaces, raceNumber, haveDigests ? digests : NULL))
		{
			Platform_Log(MAIN_ARCADE_LINK_AUTOPILOT_LOG "race %u validated (launch %u)\n", (unsigned)state->autopilot.racesValidated,
				(unsigned)raceNumber);
		}
	}

	if (NativeArcadeLinkHost_GetView(&view))
	{
		(void)NativeArcadeLinkAutopilot_Observe(&state->autopilot, &view, action);
	}
	else
	{
		(void)NativeArcadeLinkAutopilot_Observe(&state->autopilot, NULL, action);
	}
	if (state->autopilot.done != 0u)
	{
		MainArcadeLinkAutopilot_Finish();
	}
}

#else

/* Without CTR_INTERNAL there is no autopilot option: both entries are inert. */
void MainArcadeLinkAutopilot_Input(const struct MainArcadeLinkPolicyInput *input, struct MainArcadeLinkPolicyOutput *output)
{
	(void)input;
	(void)output;
}

void MainArcadeLinkAutopilot_AfterTick(uint32_t action)
{
	(void)action;
}

#endif

#endif
