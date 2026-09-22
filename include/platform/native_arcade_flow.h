#ifndef PLATFORM_NATIVE_ARCADE_FLOW_H
#define PLATFORM_NATIVE_ARCADE_FLOW_H

#include <stdint.h>

#include "platform/native_arcade_menu_input.h"

/*
 * Arcade-link screen flow (docs/GAME_LOOP_UI_MILESTONE.md section 2.2, with
 * the UX defaults of section 3). A deterministic state machine for the
 * arcade-link screens: OFF, LOBBY, MATCH_FOUND, RACING, RESULTS,
 * REMATCH_WAIT, and EXIT. It is fed once per tick with one navigation event
 * (from the arcade menu input seam) and one observation (the lobby layer's
 * status mapped onto this module's own lobby-status enum, a race-finished
 * flag, and an in-race link-failure reason), and returns at most one action
 * per tick for the caller to execute.
 *
 * Outline:
 * - OFF is inert. NativeArcadeFlow_Enter moves to LOBBY (BEGIN_LOBBY).
 * - LOBBY retries WAITING or LOST indefinitely after lobbyRetryPauseTicks
 *   (UX-5), never retries REJECTED automatically (CONFIRM retries it), moves
 *   to MATCH_FOUND on READY, and BACK closes the link and exits.
 * - MATCH_FOUND holds for matchFoundHoldTicks, then START_RACE; losing READY
 *   during the hold returns to LOBBY (RESTART_LOBBY).
 * - RACING ignores menu events; a link failure outranks a same-tick finish
 *   (UX-6). Either moves to RESULTS with the matching end reason.
 * - RESULTS has rows REMATCH (default focus, UX-7) and EXIT, ignores events
 *   for resultsDwellTicks (UX-3), and exits after resultsIdleTimeoutTicks
 *   without an accepted event (UX-10).
 * - REMATCH_WAIT moves to MATCH_FOUND on READY, or exits with reason
 *   OPPONENT_LEFT on REJECTED or after rematchWaitTimeoutTicks (UX-7).
 * - EXIT holds for exitHoldTicks (opponentLeftNoticeTicks for OPPONENT_LEFT),
 *   then returns to OFF with RETURN_TO_TITLE.
 *
 * Pure: caller-owned state, no heap use, no I/O, no hidden state, fully
 * deterministic, and at most one action per tick.
 */

/* Default timings, in 30 Hz game-loop ticks. Values are frozen. */
#define NATIVE_ARCADE_FLOW_DEFAULT_LOBBY_RETRY_PAUSE_TICKS 30u
#define NATIVE_ARCADE_FLOW_DEFAULT_MATCH_FOUND_HOLD_TICKS 45u
#define NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_DWELL_TICKS 30u
#define NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_IDLE_TIMEOUT_TICKS 900u
#define NATIVE_ARCADE_FLOW_DEFAULT_REMATCH_WAIT_TIMEOUT_TICKS 300u
#define NATIVE_ARCADE_FLOW_DEFAULT_OPPONENT_LEFT_NOTICE_TICKS 90u
#define NATIVE_ARCADE_FLOW_DEFAULT_EXIT_HOLD_TICKS 60u

/* Results screen rows. */
#define NATIVE_ARCADE_FLOW_ROW_REMATCH 0u
#define NATIVE_ARCADE_FLOW_ROW_EXIT 1u
#define NATIVE_ARCADE_FLOW_RESULTS_ROW_COUNT 2u

enum NativeArcadeFlowScreen
{
	NATIVE_ARCADE_FLOW_SCREEN_OFF = 0,
	NATIVE_ARCADE_FLOW_SCREEN_LOBBY = 1,
	NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND = 2,
	NATIVE_ARCADE_FLOW_SCREEN_RACING = 3,
	NATIVE_ARCADE_FLOW_SCREEN_RESULTS = 4,
	NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT = 5,
	NATIVE_ARCADE_FLOW_SCREEN_EXIT = 6
};

/* Mirrors the lobby layer's modes without naming them. */
enum NativeArcadeFlowLobbyStatus
{
	NATIVE_ARCADE_FLOW_LOBBY_WAITING = 0,
	NATIVE_ARCADE_FLOW_LOBBY_CONNECTING = 1,
	NATIVE_ARCADE_FLOW_LOBBY_READY = 2,
	NATIVE_ARCADE_FLOW_LOBBY_REJECTED = 3,
	NATIVE_ARCADE_FLOW_LOBBY_LOST = 4
};

enum NativeArcadeFlowEndReason
{
	NATIVE_ARCADE_FLOW_END_NONE = 0,
	NATIVE_ARCADE_FLOW_END_FINISHED = 1,
	NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT = 2,
	NATIVE_ARCADE_FLOW_END_DESYNC = 3,
	NATIVE_ARCADE_FLOW_END_LINK_ERROR = 4,
	NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT = 5
};

enum NativeArcadeFlowAction
{
	NATIVE_ARCADE_FLOW_ACTION_NONE = 0,
	NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY = 1,
	NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY = 2,
	NATIVE_ARCADE_FLOW_ACTION_START_RACE = 3,
	NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH = 4,
	NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK = 5,
	NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE = 6
};

/* Every timing is in 30 Hz game-loop ticks and must be at least 1;
 * resultsIdleTimeoutTicks must exceed resultsDwellTicks. */
struct NativeArcadeFlowTimings
{
	uint32_t lobbyRetryPauseTicks;
	uint32_t matchFoundHoldTicks;
	uint32_t resultsDwellTicks;
	uint32_t resultsIdleTimeoutTicks;
	uint32_t rematchWaitTimeoutTicks;
	uint32_t opponentLeftNoticeTicks;
	uint32_t exitHoldTicks;
};

struct NativeArcadeFlowObservation
{
	/* enum NativeArcadeFlowLobbyStatus */
	uint32_t lobbyStatus;
	/* NATIVE_ARCADE_FLOW_END_NONE, _PEER_TIMEOUT, _DESYNC, or _LINK_ERROR
	 * only. */
	uint32_t linkFailure;
	/* 0 or 1 */
	uint8_t raceFinished;
	uint8_t reserved[3];
};

struct NativeArcadeFlow
{
	struct NativeArcadeFlowTimings timings;
	/* enum NativeArcadeFlowScreen */
	uint32_t screen;
	/* enum NativeArcadeFlowEndReason */
	uint32_t endReason;
	/* Last observed enum NativeArcadeFlowLobbyStatus. */
	uint32_t lobbyStatus;
	/* Focused results row (NATIVE_ARCADE_FLOW_ROW_*). */
	uint32_t selectedRow;
	/* Ticks since the current screen was entered (saturating). */
	uint32_t ticksInScreen;
	/* Ticks toward the next automatic lobby retry. */
	uint32_t ticksSinceRetry;
	/* Results ticks without an accepted event. */
	uint32_t ticksSinceInput;
	/* Incremented on every screen entry, including re-entry (wraps). */
	uint32_t screenSerial;
};

/* Fills in the frozen default timings. NULL is a no-op. */
void NativeArcadeFlow_DefaultTimings(struct NativeArcadeFlowTimings *timings);

/* Initializes the flow on screen OFF. NULL timings means the defaults.
 * Returns 1 on success; returns 0 with *flow untouched when flow is NULL or
 * the timings are invalid. */
int NativeArcadeFlow_Init(struct NativeArcadeFlow *flow, const struct NativeArcadeFlowTimings *timings);

/* From OFF only: moves to LOBBY and returns BEGIN_LOBBY. Otherwise (or for
 * NULL) returns NONE and changes nothing. */
enum NativeArcadeFlowAction NativeArcadeFlow_Enter(struct NativeArcadeFlow *flow);

/* Advances one tick. Returns at most one action. NULL arguments, an invalid
 * observation, or screen OFF return NONE and change nothing. Event values
 * above NATIVE_ARCADE_MENU_EVENT_BACK are treated as NONE. */
enum NativeArcadeFlowAction NativeArcadeFlow_Tick(struct NativeArcadeFlow *flow,
	const struct NativeArcadeFlowObservation *observation, enum NativeArcadeMenuEvent event);

/* Accessors. NULL gives OFF, END_NONE, 0, 0, WAITING, and 0 respectively. */
uint32_t NativeArcadeFlow_Screen(const struct NativeArcadeFlow *flow);
uint32_t NativeArcadeFlow_EndReason(const struct NativeArcadeFlow *flow);
uint32_t NativeArcadeFlow_SelectedRow(const struct NativeArcadeFlow *flow);
uint32_t NativeArcadeFlow_TicksInScreen(const struct NativeArcadeFlow *flow);
uint32_t NativeArcadeFlow_LobbyStatus(const struct NativeArcadeFlow *flow);
uint32_t NativeArcadeFlow_ScreenSerial(const struct NativeArcadeFlow *flow);

#endif
