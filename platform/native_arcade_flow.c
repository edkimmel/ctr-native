#include "platform/native_arcade_flow.h"

#include <stddef.h>
#include <stdint.h>

#include "platform/native_arcade_menu_input.h"

void NativeArcadeFlow_DefaultTimings(struct NativeArcadeFlowTimings *timings)
{
	if (timings == NULL)
	{
		return;
	}
	timings->lobbyRetryPauseTicks = NATIVE_ARCADE_FLOW_DEFAULT_LOBBY_RETRY_PAUSE_TICKS;
	timings->matchFoundHoldTicks = NATIVE_ARCADE_FLOW_DEFAULT_MATCH_FOUND_HOLD_TICKS;
	timings->resultsDwellTicks = NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_DWELL_TICKS;
	timings->resultsIdleTimeoutTicks = NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_IDLE_TIMEOUT_TICKS;
	timings->rematchWaitTimeoutTicks = NATIVE_ARCADE_FLOW_DEFAULT_REMATCH_WAIT_TIMEOUT_TICKS;
	timings->opponentLeftNoticeTicks = NATIVE_ARCADE_FLOW_DEFAULT_OPPONENT_LEFT_NOTICE_TICKS;
	timings->exitHoldTicks = NATIVE_ARCADE_FLOW_DEFAULT_EXIT_HOLD_TICKS;
	timings->selectResultHoldTicks = NATIVE_ARCADE_FLOW_DEFAULT_SELECT_RESULT_HOLD_TICKS;
	timings->launchTimeoutTicks = NATIVE_ARCADE_FLOW_DEFAULT_LAUNCH_TIMEOUT_TICKS;
}

static int NativeArcadeFlow_TimingsValid(const struct NativeArcadeFlowTimings *timings)
{
	if ((timings->lobbyRetryPauseTicks == 0u) || (timings->matchFoundHoldTicks == 0u) ||
		(timings->resultsDwellTicks == 0u) || (timings->resultsIdleTimeoutTicks == 0u) ||
		(timings->rematchWaitTimeoutTicks == 0u) || (timings->opponentLeftNoticeTicks == 0u) ||
		(timings->exitHoldTicks == 0u) || (timings->selectResultHoldTicks == 0u) || (timings->launchTimeoutTicks == 0u))
	{
		return 0;
	}
	if (timings->resultsIdleTimeoutTicks <= timings->resultsDwellTicks)
	{
		return 0;
	}
	return 1;
}

int NativeArcadeFlow_Init(struct NativeArcadeFlow *flow, const struct NativeArcadeFlowTimings *timings)
{
	struct NativeArcadeFlowTimings chosen;

	if (flow == NULL)
	{
		return 0;
	}
	if (timings == NULL)
	{
		NativeArcadeFlow_DefaultTimings(&chosen);
	}
	else
	{
		chosen = *timings;
	}
	if (!NativeArcadeFlow_TimingsValid(&chosen))
	{
		return 0;
	}

	flow->timings = chosen;
	flow->screen = NATIVE_ARCADE_FLOW_SCREEN_OFF;
	flow->endReason = NATIVE_ARCADE_FLOW_END_NONE;
	flow->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_WAITING;
	flow->selectedRow = 0u;
	flow->ticksInScreen = 0u;
	flow->ticksSinceRetry = 0u;
	flow->ticksSinceInput = 0u;
	flow->screenSerial = 0u;
	flow->relinked = 0u;
	flow->ticksSinceRelink = 0u;
	return 1;
}

/* Every transition, including re-entering the same screen, goes through
 * here so the per-screen counters and the serial stay consistent. */
static void NativeArcadeFlow_EnterScreen(struct NativeArcadeFlow *flow, uint32_t screen)
{
	flow->screen = screen;
	flow->ticksInScreen = 0u;
	flow->ticksSinceRetry = 0u;
	flow->ticksSinceInput = 0u;
	flow->relinked = 0u;
	flow->ticksSinceRelink = 0u;
	flow->screenSerial += 1u;
	if (screen == NATIVE_ARCADE_FLOW_SCREEN_RESULTS)
	{
		/* Default focus is REMATCH (UX-7). */
		flow->selectedRow = NATIVE_ARCADE_FLOW_ROW_REMATCH;
	}
	else if (screen == NATIVE_ARCADE_FLOW_SCREEN_OFF)
	{
		flow->endReason = NATIVE_ARCADE_FLOW_END_NONE;
		flow->selectedRow = 0u;
	}
}

enum NativeArcadeFlowAction NativeArcadeFlow_Enter(struct NativeArcadeFlow *flow)
{
	if ((flow == NULL) || (flow->screen != NATIVE_ARCADE_FLOW_SCREEN_OFF))
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	flow->endReason = NATIVE_ARCADE_FLOW_END_NONE;
	flow->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_WAITING;
	return NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY;
}

static int NativeArcadeFlow_ObservationValid(const struct NativeArcadeFlowObservation *observation)
{
	if (observation->lobbyStatus > (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST)
	{
		return 0;
	}
	if ((observation->linkFailure != (uint32_t)NATIVE_ARCADE_FLOW_END_NONE) &&
		(observation->linkFailure != (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT) &&
		(observation->linkFailure != (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC) &&
		(observation->linkFailure != (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR))
	{
		return 0;
	}
	if (observation->raceFinished > 1u)
	{
		return 0;
	}
	if (observation->selectStatus > (uint8_t)NATIVE_ARCADE_FLOW_SELECT_FAILED)
	{
		return 0;
	}
	if (observation->launchStatus > (uint8_t)NATIVE_ARCADE_FLOW_LAUNCH_COMMITTED)
	{
		return 0;
	}
	return 1;
}

/* Automatic retry pause for WAITING and LOST (UX-5): RESTART_LOBBY once
 * lobbyRetryPauseTicks consecutive retryable ticks have passed. */
static enum NativeArcadeFlowAction NativeArcadeFlow_RetryPause(struct NativeArcadeFlow *flow)
{
	if (flow->ticksSinceRetry < UINT32_MAX)
	{
		flow->ticksSinceRetry += 1u;
	}
	if (flow->ticksSinceRetry >= flow->timings.lobbyRetryPauseTicks)
	{
		flow->ticksSinceRetry = 0u;
		return NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY;
	}
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

static enum NativeArcadeFlowAction NativeArcadeFlow_TickLobby(struct NativeArcadeFlow *flow, uint32_t status,
	enum NativeArcadeMenuEvent event)
{
	if (event == NATIVE_ARCADE_MENU_EVENT_BACK)
	{
		flow->endReason = NATIVE_ARCADE_FLOW_END_NONE;
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
		return NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK;
	}
	if (status == NATIVE_ARCADE_FLOW_LOBBY_READY)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (status == NATIVE_ARCADE_FLOW_LOBBY_REJECTED)
	{
		/* Never retried automatically; CONFIRM retries in place. */
		flow->ticksSinceRetry = 0u;
		if (event == NATIVE_ARCADE_MENU_EVENT_CONFIRM)
		{
			return NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY;
		}
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if ((status == NATIVE_ARCADE_FLOW_LOBBY_WAITING) || (status == NATIVE_ARCADE_FLOW_LOBBY_LOST))
	{
		return NativeArcadeFlow_RetryPause(flow);
	}
	/* CONNECTING */
	flow->ticksSinceRetry = 0u;
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

static enum NativeArcadeFlowAction NativeArcadeFlow_TickMatchFound(struct NativeArcadeFlow *flow, uint32_t status)
{
	if (status != NATIVE_ARCADE_FLOW_LOBBY_READY)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
		return NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY;
	}
	if (flow->ticksInScreen >= flow->timings.matchFoundHoldTicks)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_SELECT);
		return NATIVE_ARCADE_FLOW_ACTION_BEGIN_SELECT;
	}
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

/* Every pre-race failure (SELECT, or SELECT_RESULT before or after RELINK)
 * is shown as LINK ERROR on the results screen and closes the link: there is
 * no race report to read, and a lobby left open could still complete a
 * relink handshake in the background while the cabinet sits on RESULTS.
 * RACING -> RESULTS instead keeps the link (and returns NONE) so the caller
 * can read the latched session report. */
static enum NativeArcadeFlowAction NativeArcadeFlow_SelectLinkError(struct NativeArcadeFlow *flow)
{
	flow->endReason = NATIVE_ARCADE_FLOW_END_LINK_ERROR;
	NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	return NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK;
}

/* SELECT: menu events belong to the caller's select session (BACK is
 * ignored, SEL-7), so the flow never reads them here. */
static enum NativeArcadeFlowAction NativeArcadeFlow_TickSelect(struct NativeArcadeFlow *flow, uint32_t status,
	uint32_t selectStatus)
{
	if (status != NATIVE_ARCADE_FLOW_LOBBY_READY)
	{
		return NativeArcadeFlow_SelectLinkError(flow);
	}
	if (selectStatus == (uint32_t)NATIVE_ARCADE_FLOW_SELECT_FAILED)
	{
		return NativeArcadeFlow_SelectLinkError(flow);
	}
	if (selectStatus == (uint32_t)NATIVE_ARCADE_FLOW_SELECT_CONFIRMED)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
	}
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

/* SELECT_RESULT: events are ignored. Phase 1 holds, then RELINK; phase 2
 * waits for the relinked lobby and the launch commit (RL-5), starting on the
 * tick after RELINK. */
static enum NativeArcadeFlowAction NativeArcadeFlow_TickSelectResult(struct NativeArcadeFlow *flow, uint32_t status,
	uint32_t launchStatus)
{
	if (flow->relinked == 0u)
	{
		/* The lobby status is the old link's here and is ignored. */
		if (flow->ticksInScreen >= flow->timings.selectResultHoldTicks)
		{
			flow->relinked = 1u;
			flow->ticksSinceRelink = 0u;
			flow->ticksSinceRetry = 0u;
			return NATIVE_ARCADE_FLOW_ACTION_RELINK;
		}
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}

	if (flow->ticksSinceRelink < UINT32_MAX)
	{
		flow->ticksSinceRelink += 1u;
	}
	/* The launch commit is checked before the timeout on the same tick. */
	if ((status == NATIVE_ARCADE_FLOW_LOBBY_READY) && (launchStatus == (uint32_t)NATIVE_ARCADE_FLOW_LAUNCH_COMMITTED))
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_RACING);
		return NATIVE_ARCADE_FLOW_ACTION_START_RACE;
	}
	if ((status == NATIVE_ARCADE_FLOW_LOBBY_REJECTED) || (flow->ticksSinceRelink >= flow->timings.launchTimeoutTicks))
	{
		return NativeArcadeFlow_SelectLinkError(flow);
	}
	if ((status == NATIVE_ARCADE_FLOW_LOBBY_WAITING) || (status == NATIVE_ARCADE_FLOW_LOBBY_LOST))
	{
		return NativeArcadeFlow_RetryPause(flow);
	}
	/* CONNECTING, or READY with the launch PENDING. */
	flow->ticksSinceRetry = 0u;
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

static enum NativeArcadeFlowAction NativeArcadeFlow_TickRacing(struct NativeArcadeFlow *flow,
	const struct NativeArcadeFlowObservation *observation)
{
	/* A link failure outranks a same-tick finish (UX-6). */
	if (observation->linkFailure != (uint32_t)NATIVE_ARCADE_FLOW_END_NONE)
	{
		flow->endReason = observation->linkFailure;
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (observation->lobbyStatus == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST)
	{
		flow->endReason = NATIVE_ARCADE_FLOW_END_LINK_ERROR;
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (observation->raceFinished != 0u)
	{
		flow->endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

static enum NativeArcadeFlowAction NativeArcadeFlow_TickResults(struct NativeArcadeFlow *flow,
	enum NativeArcadeMenuEvent event)
{
	/* Events inside the dwell are ignored (UX-3). */
	if ((event != NATIVE_ARCADE_MENU_EVENT_NONE) && (flow->ticksInScreen > flow->timings.resultsDwellTicks))
	{
		flow->ticksSinceInput = 0u;
		switch (event)
		{
		case NATIVE_ARCADE_MENU_EVENT_PREV:
			flow->selectedRow = (flow->selectedRow + NATIVE_ARCADE_FLOW_RESULTS_ROW_COUNT - 1u) %
				NATIVE_ARCADE_FLOW_RESULTS_ROW_COUNT;
			return NATIVE_ARCADE_FLOW_ACTION_NONE;
		case NATIVE_ARCADE_MENU_EVENT_NEXT:
			flow->selectedRow = (flow->selectedRow + 1u) % NATIVE_ARCADE_FLOW_RESULTS_ROW_COUNT;
			return NATIVE_ARCADE_FLOW_ACTION_NONE;
		case NATIVE_ARCADE_MENU_EVENT_BACK:
			flow->selectedRow = NATIVE_ARCADE_FLOW_ROW_EXIT;
			return NATIVE_ARCADE_FLOW_ACTION_NONE;
		case NATIVE_ARCADE_MENU_EVENT_CONFIRM:
			if (flow->selectedRow == NATIVE_ARCADE_FLOW_ROW_REMATCH)
			{
				NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
				return NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH;
			}
			NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
			return NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK;
		case NATIVE_ARCADE_MENU_EVENT_NONE:
		default:
			return NATIVE_ARCADE_FLOW_ACTION_NONE;
		}
	}

	/* Idle timeout returns an abandoned cabinet to the title (UX-10). */
	if (flow->ticksSinceInput < UINT32_MAX)
	{
		flow->ticksSinceInput += 1u;
	}
	if (flow->ticksSinceInput >= flow->timings.resultsIdleTimeoutTicks)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
		return NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK;
	}
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

static enum NativeArcadeFlowAction NativeArcadeFlow_TickRematchWait(struct NativeArcadeFlow *flow, uint32_t status,
	enum NativeArcadeMenuEvent event)
{
	if (event == NATIVE_ARCADE_MENU_EVENT_BACK)
	{
		flow->endReason = NATIVE_ARCADE_FLOW_END_NONE;
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
		return NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK;
	}
	if (status == NATIVE_ARCADE_FLOW_LOBBY_READY)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if ((status == NATIVE_ARCADE_FLOW_LOBBY_REJECTED) ||
		(flow->ticksInScreen >= flow->timings.rematchWaitTimeoutTicks))
	{
		/* The other cabinet chose EXIT or never answered (UX-7). */
		flow->endReason = NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT;
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
		return NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK;
	}
	if ((status == NATIVE_ARCADE_FLOW_LOBBY_WAITING) || (status == NATIVE_ARCADE_FLOW_LOBBY_LOST))
	{
		return NativeArcadeFlow_RetryPause(flow);
	}
	/* CONNECTING */
	flow->ticksSinceRetry = 0u;
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

static enum NativeArcadeFlowAction NativeArcadeFlow_TickExit(struct NativeArcadeFlow *flow)
{
	uint32_t hold;

	hold = (flow->endReason == NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT) ? flow->timings.opponentLeftNoticeTicks
																	  : flow->timings.exitHoldTicks;
	if (flow->ticksInScreen >= hold)
	{
		NativeArcadeFlow_EnterScreen(flow, NATIVE_ARCADE_FLOW_SCREEN_OFF);
		return NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE;
	}
	return NATIVE_ARCADE_FLOW_ACTION_NONE;
}

enum NativeArcadeFlowAction NativeArcadeFlow_Tick(struct NativeArcadeFlow *flow,
	const struct NativeArcadeFlowObservation *observation, enum NativeArcadeMenuEvent event)
{
	uint32_t status;

	if ((flow == NULL) || (observation == NULL))
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (!NativeArcadeFlow_ObservationValid(observation))
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if ((uint32_t)event > (uint32_t)NATIVE_ARCADE_MENU_EVENT_BACK)
	{
		event = NATIVE_ARCADE_MENU_EVENT_NONE;
	}
	if (flow->screen == NATIVE_ARCADE_FLOW_SCREEN_OFF)
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}

	status = observation->lobbyStatus;
	flow->lobbyStatus = status;
	if (flow->ticksInScreen < UINT32_MAX)
	{
		flow->ticksInScreen += 1u;
	}

	switch (flow->screen)
	{
	case NATIVE_ARCADE_FLOW_SCREEN_LOBBY:
		return NativeArcadeFlow_TickLobby(flow, status, event);
	case NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND:
		return NativeArcadeFlow_TickMatchFound(flow, status);
	case NATIVE_ARCADE_FLOW_SCREEN_RACING:
		return NativeArcadeFlow_TickRacing(flow, observation);
	case NATIVE_ARCADE_FLOW_SCREEN_RESULTS:
		return NativeArcadeFlow_TickResults(flow, event);
	case NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT:
		return NativeArcadeFlow_TickRematchWait(flow, status, event);
	case NATIVE_ARCADE_FLOW_SCREEN_EXIT:
		return NativeArcadeFlow_TickExit(flow);
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT:
		return NativeArcadeFlow_TickSelect(flow, status, observation->selectStatus);
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT:
		return NativeArcadeFlow_TickSelectResult(flow, status, observation->launchStatus);
	default:
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
}

uint32_t NativeArcadeFlow_Screen(const struct NativeArcadeFlow *flow)
{
	return (flow == NULL) ? (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF : flow->screen;
}

uint32_t NativeArcadeFlow_EndReason(const struct NativeArcadeFlow *flow)
{
	return (flow == NULL) ? (uint32_t)NATIVE_ARCADE_FLOW_END_NONE : flow->endReason;
}

uint32_t NativeArcadeFlow_SelectedRow(const struct NativeArcadeFlow *flow)
{
	return (flow == NULL) ? 0u : flow->selectedRow;
}

uint32_t NativeArcadeFlow_TicksInScreen(const struct NativeArcadeFlow *flow)
{
	return (flow == NULL) ? 0u : flow->ticksInScreen;
}

uint32_t NativeArcadeFlow_LobbyStatus(const struct NativeArcadeFlow *flow)
{
	return (flow == NULL) ? (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_WAITING : flow->lobbyStatus;
}

uint32_t NativeArcadeFlow_ScreenSerial(const struct NativeArcadeFlow *flow)
{
	return (flow == NULL) ? 0u : flow->screenSerial;
}
