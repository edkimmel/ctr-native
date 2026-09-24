#ifndef PLATFORM_NATIVE_ARCADE_FLOW_H
#define PLATFORM_NATIVE_ARCADE_FLOW_H

#include <stdint.h>

#include "platform/native_arcade_menu_input.h"

/*
 * Arcade-link screen flow (docs/GAME_LOOP_UI_MILESTONE.md section 2.2, with
 * the UX defaults of section 3, and the match-select phase of
 * docs/MATCH_SELECT_MILESTONE.md section 2.5). A deterministic state machine
 * for the arcade-link screens: OFF, LOBBY, MATCH_FOUND, SELECT,
 * SELECT_RESULT, RACING, RESULTS, REMATCH_WAIT, and EXIT. It is fed once per
 * tick with one navigation event (from the arcade menu input seam) and one
 * observation (the lobby layer's status mapped onto this module's own
 * lobby-status enum, a race-finished flag, an in-race link-failure reason,
 * the caller's select status, and the caller's launch status), and returns
 * at most one action per tick for the caller to execute.
 *
 * Outline:
 * - OFF is inert. NativeArcadeFlow_Enter moves to LOBBY (BEGIN_LOBBY).
 * - LOBBY retries WAITING or LOST indefinitely after lobbyRetryPauseTicks
 *   (UX-5), never retries REJECTED automatically (CONFIRM retries it), moves
 *   to MATCH_FOUND on READY, and BACK closes the link and exits.
 * - MATCH_FOUND holds for matchFoundHoldTicks, then moves to SELECT
 *   (BEGIN_SELECT); losing READY during the hold returns to LOBBY
 *   (RESTART_LOBBY). It never starts a race itself.
 * - SELECT ignores every menu event (the caller routes them to its select
 *   session; BACK is ignored, SEL-7). Checked in order: a lobby status
 *   other than READY, then select status FAILED, each move to RESULTS with
 *   LINK_ERROR and return CLOSE_LINK; select status CONFIRMED moves to
 *   SELECT_RESULT.
 * - SELECT_RESULT ignores every menu event. Phase 1: it shows the resolved
 *   match, ignoring the lobby status, and on the tick ticksInScreen reaches
 *   selectResultHoldTicks returns RELINK (the caller relinks on the
 *   resolved config). Phase 2 starts on the tick after RELINK, because a
 *   READY still observed on the RELINK tick belongs to the old link; checked
 *   in order: READY with launch COMMITTED moves to RACING (START_RACE);
 *   REJECTED, or launchTimeoutTicks ticks since RELINK, moves to RESULTS
 *   with LINK_ERROR and returns CLOSE_LINK; WAITING or LOST returns
 *   RESTART_LOBBY after lobbyRetryPauseTicks; CONNECTING, and READY with
 *   launch PENDING, stay.
 * - Every pre-race LINK_ERROR (from SELECT or SELECT_RESULT) closes the
 *   link: a lobby left open on RESULTS could still complete a relink
 *   handshake in the background, so a later REMATCH could derive from a
 *   config the peer never agreed to.
 * - RACING ignores menu events; a link failure outranks a same-tick finish
 *   (UX-6). Either moves to RESULTS with the matching end reason and returns
 *   NONE: the link stays open so the caller can read the latched session
 *   report.
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
#define NATIVE_ARCADE_FLOW_DEFAULT_SELECT_RESULT_HOLD_TICKS 60u /* SEL-8 */
#define NATIVE_ARCADE_FLOW_DEFAULT_LAUNCH_TIMEOUT_TICKS 300u    /* SEL-9 */

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
	NATIVE_ARCADE_FLOW_SCREEN_EXIT = 6,
	NATIVE_ARCADE_FLOW_SCREEN_SELECT = 7,
	NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT = 8
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
	NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE = 6,
	NATIVE_ARCADE_FLOW_ACTION_BEGIN_SELECT = 7,
	NATIVE_ARCADE_FLOW_ACTION_RELINK = 8
};

/* The caller's select session status, mapped onto this module's own enum. */
enum NativeArcadeFlowSelectStatus
{
	NATIVE_ARCADE_FLOW_SELECT_PENDING = 0,
	NATIVE_ARCADE_FLOW_SELECT_CONFIRMED = 1,
	NATIVE_ARCADE_FLOW_SELECT_FAILED = 2
};

/* The caller's launch agreement status (RL-5, docs/RACE_LAUNCH_MILESTONE.md). */
enum NativeArcadeFlowLaunchStatus
{
	NATIVE_ARCADE_FLOW_LAUNCH_PENDING = 0,
	NATIVE_ARCADE_FLOW_LAUNCH_COMMITTED = 1
};

/* Every timing (all nine) is in 30 Hz game-loop ticks and must be at least
 * 1; resultsIdleTimeoutTicks must exceed resultsDwellTicks. */
struct NativeArcadeFlowTimings
{
	uint32_t lobbyRetryPauseTicks;
	uint32_t matchFoundHoldTicks;
	uint32_t resultsDwellTicks;
	uint32_t resultsIdleTimeoutTicks;
	uint32_t rematchWaitTimeoutTicks;
	uint32_t opponentLeftNoticeTicks;
	uint32_t exitHoldTicks;
	/* SELECT_RESULT hold before RELINK (SEL-8). */
	uint32_t selectResultHoldTicks;
	/* Ticks after RELINK without READY and a launch commit before LINK_ERROR
	 * (SEL-9, RL-5). */
	uint32_t launchTimeoutTicks;
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
	/* enum NativeArcadeFlowSelectStatus; acted on in SELECT only, but
	 * validated on every screen. */
	uint8_t selectStatus;
	/* enum NativeArcadeFlowLaunchStatus; acted on in SELECT_RESULT phase 2
	 * only, but validated on every screen, like selectStatus. */
	uint8_t launchStatus;
	uint8_t reserved[1];
};

/* launchStatus took the first reserved byte: the observation did not grow. */
_Static_assert(sizeof(struct NativeArcadeFlowObservation) == 12u, "NativeArcadeFlowObservation must stay 12 bytes");

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
	/* SELECT_RESULT: 1 once RELINK has been returned (phase 2); cleared on
	 * every screen entry. */
	uint32_t relinked;
	/* SELECT_RESULT phase 2: ticks since the RELINK tick (saturating). */
	uint32_t ticksSinceRelink;
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
 * observation (lobbyStatus, linkFailure, raceFinished, selectStatus, or
 * launchStatus out of range; a launchStatus above COMMITTED is invalid), or
 * screen OFF return NONE and change nothing. Event values above
 * NATIVE_ARCADE_MENU_EVENT_BACK are treated as NONE. */
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
