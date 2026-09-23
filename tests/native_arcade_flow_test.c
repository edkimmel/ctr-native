#include "platform/native_arcade_flow.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define EV_NONE NATIVE_ARCADE_MENU_EVENT_NONE
#define EV_PREV NATIVE_ARCADE_MENU_EVENT_PREV
#define EV_NEXT NATIVE_ARCADE_MENU_EVENT_NEXT
#define EV_CONFIRM NATIVE_ARCADE_MENU_EVENT_CONFIRM
#define EV_BACK NATIVE_ARCADE_MENU_EVENT_BACK

#define SC_OFF NATIVE_ARCADE_FLOW_SCREEN_OFF
#define SC_LOBBY NATIVE_ARCADE_FLOW_SCREEN_LOBBY
#define SC_MATCH_FOUND NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND
#define SC_RACING NATIVE_ARCADE_FLOW_SCREEN_RACING
#define SC_RESULTS NATIVE_ARCADE_FLOW_SCREEN_RESULTS
#define SC_REMATCH_WAIT NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT
#define SC_EXIT NATIVE_ARCADE_FLOW_SCREEN_EXIT
#define SC_SELECT NATIVE_ARCADE_FLOW_SCREEN_SELECT
#define SC_SELECT_RESULT NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT

#define SS_PENDING NATIVE_ARCADE_FLOW_SELECT_PENDING
#define SS_CONFIRMED NATIVE_ARCADE_FLOW_SELECT_CONFIRMED
#define SS_FAILED NATIVE_ARCADE_FLOW_SELECT_FAILED

#define LS_WAITING NATIVE_ARCADE_FLOW_LOBBY_WAITING
#define LS_CONNECTING NATIVE_ARCADE_FLOW_LOBBY_CONNECTING
#define LS_READY NATIVE_ARCADE_FLOW_LOBBY_READY
#define LS_REJECTED NATIVE_ARCADE_FLOW_LOBBY_REJECTED
#define LS_LOST NATIVE_ARCADE_FLOW_LOBBY_LOST

#define END_NONE NATIVE_ARCADE_FLOW_END_NONE
#define END_FINISHED NATIVE_ARCADE_FLOW_END_FINISHED
#define END_PEER_TIMEOUT NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT
#define END_DESYNC NATIVE_ARCADE_FLOW_END_DESYNC
#define END_LINK_ERROR NATIVE_ARCADE_FLOW_END_LINK_ERROR
#define END_OPPONENT_LEFT NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT

#define ACT_NONE NATIVE_ARCADE_FLOW_ACTION_NONE
#define ACT_BEGIN_LOBBY NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY
#define ACT_RESTART_LOBBY NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY
#define ACT_START_RACE NATIVE_ARCADE_FLOW_ACTION_START_RACE
#define ACT_BEGIN_REMATCH NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH
#define ACT_CLOSE_LINK NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK
#define ACT_RETURN_TO_TITLE NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE
#define ACT_BEGIN_SELECT NATIVE_ARCADE_FLOW_ACTION_BEGIN_SELECT
#define ACT_RELINK NATIVE_ARCADE_FLOW_ACTION_RELINK

#define ROW_REMATCH NATIVE_ARCADE_FLOW_ROW_REMATCH
#define ROW_EXIT NATIVE_ARCADE_FLOW_ROW_EXIT

static struct NativeArcadeFlowObservation Obs(uint32_t status, uint32_t failure, uint8_t finished)
{
	struct NativeArcadeFlowObservation observation;

	memset(&observation, 0, sizeof(observation));
	observation.lobbyStatus = status;
	observation.linkFailure = failure;
	observation.raceFinished = finished;
	return observation;
}

static enum NativeArcadeFlowAction Step(struct NativeArcadeFlow *flow, uint32_t status, uint32_t failure,
	uint8_t finished, enum NativeArcadeMenuEvent event)
{
	struct NativeArcadeFlowObservation observation = Obs(status, failure, finished);

	return NativeArcadeFlow_Tick(flow, &observation, event);
}

/* One tick with a select status (no failure, not finished). */
static enum NativeArcadeFlowAction StepSel(struct NativeArcadeFlow *flow, uint32_t status, uint8_t selectStatus,
	enum NativeArcadeMenuEvent event)
{
	struct NativeArcadeFlowObservation observation = Obs(status, END_NONE, 0u);

	observation.selectStatus = selectStatus;
	return NativeArcadeFlow_Tick(flow, &observation, event);
}

/* Runs `count` ticks of the same lobby status, select status, and event;
 * every tick must return NONE and keep the screen. */
static int RunQuietSel(struct NativeArcadeFlow *flow, uint32_t count, uint32_t status, uint8_t selectStatus,
	enum NativeArcadeMenuEvent event)
{
	uint32_t screen = NativeArcadeFlow_Screen(flow);
	uint32_t i;

	for (i = 0; i < count; i++)
	{
		CHECK(StepSel(flow, status, selectStatus, event) == ACT_NONE);
		CHECK(NativeArcadeFlow_Screen(flow) == screen);
	}
	return 0;
}

/* Runs `count` ticks of the same lobby status and event (select PENDING);
 * every tick must return NONE and keep the screen. */
static int RunQuiet(struct NativeArcadeFlow *flow, uint32_t count, uint32_t status, enum NativeArcadeMenuEvent event)
{
	return RunQuietSel(flow, count, status, SS_PENDING, event);
}

/* Default timings, entered: screen LOBBY. */
static int ToLobby(struct NativeArcadeFlow *flow)
{
	CHECK(NativeArcadeFlow_Init(flow, NULL) == 1);
	CHECK(NativeArcadeFlow_Enter(flow) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_LOBBY);
	return 0;
}

static int ToMatchFound(struct NativeArcadeFlow *flow)
{
	CHECK(ToLobby(flow) == 0);
	CHECK(Step(flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_MATCH_FOUND);
	return 0;
}

/* MATCH_FOUND's 45-tick hold, then BEGIN_SELECT: screen SELECT. */
static int ToSelect(struct NativeArcadeFlow *flow)
{
	CHECK(ToMatchFound(flow) == 0);
	CHECK(RunQuiet(flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(Step(flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_SELECT);
	return 0;
}

/* SELECT confirmed: screen SELECT_RESULT, phase 1. */
static int ToSelectResult(struct NativeArcadeFlow *flow)
{
	CHECK(ToSelect(flow) == 0);
	CHECK(StepSel(flow, LS_READY, SS_CONFIRMED, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_SELECT_RESULT);
	CHECK(flow->relinked == 0u);
	return 0;
}

/* SELECT_RESULT after its 60-tick hold: RELINK returned, phase 2. */
static int ToRelinked(struct NativeArcadeFlow *flow)
{
	CHECK(ToSelectResult(flow) == 0);
	CHECK(RunQuiet(flow, 59u, LS_READY, EV_NONE) == 0);
	CHECK(Step(flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RELINK);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_SELECT_RESULT);
	CHECK(flow->relinked == 1u);
	CHECK(flow->ticksSinceRelink == 0u);
	return 0;
}

/* Through select, the relink, and READY on the tick after RELINK. */
static int ToRacing(struct NativeArcadeFlow *flow)
{
	CHECK(ToRelinked(flow) == 0);
	CHECK(Step(flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_RACING);
	return 0;
}

/* Results screen reached by the given failure reason (or a finish). */
static int ToResults(struct NativeArcadeFlow *flow, uint32_t failure)
{
	CHECK(ToRacing(flow) == 0);
	CHECK(Step(flow, LS_READY, failure, (failure == END_NONE) ? 1u : 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(flow) == ((failure == END_NONE) ? (uint32_t)END_FINISHED : failure));
	return 0;
}

/* Results with the dwell (30 ticks) already elapsed. */
static int ToResultsArmed(struct NativeArcadeFlow *flow)
{
	CHECK(ToResults(flow, END_NONE) == 0);
	CHECK(RunQuiet(flow, 30u, LS_READY, EV_NONE) == 0);
	return 0;
}

static int ToRematchWait(struct NativeArcadeFlow *flow)
{
	CHECK(ToResultsArmed(flow) == 0);
	CHECK(Step(flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_BEGIN_REMATCH);
	CHECK(NativeArcadeFlow_Screen(flow) == SC_REMATCH_WAIT);
	return 0;
}

static int TestDefaultsAndInit(void)
{
	struct NativeArcadeFlowTimings timings;
	struct NativeArcadeFlowTimings bad;
	struct NativeArcadeFlow flow;
	struct NativeArcadeFlow sentinel;
	uint32_t *fields[9];
	size_t i;

	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_SELECT_RESULT_HOLD_TICKS == 60u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_LAUNCH_TIMEOUT_TICKS == 300u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_LOBBY_RETRY_PAUSE_TICKS == 30u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_MATCH_FOUND_HOLD_TICKS == 45u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_DWELL_TICKS == 30u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_IDLE_TIMEOUT_TICKS == 900u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_REMATCH_WAIT_TIMEOUT_TICKS == 300u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_OPPONENT_LEFT_NOTICE_TICKS == 90u);
	CHECK(NATIVE_ARCADE_FLOW_DEFAULT_EXIT_HOLD_TICKS == 60u);
	CHECK(ROW_REMATCH == 0u);
	CHECK(ROW_EXIT == 1u);
	CHECK(NATIVE_ARCADE_FLOW_RESULTS_ROW_COUNT == 2u);

	CHECK((int)SC_OFF == 0 && (int)SC_LOBBY == 1 && (int)SC_MATCH_FOUND == 2 && (int)SC_RACING == 3);
	CHECK((int)SC_RESULTS == 4 && (int)SC_REMATCH_WAIT == 5 && (int)SC_EXIT == 6);
	CHECK((int)LS_WAITING == 0 && (int)LS_CONNECTING == 1 && (int)LS_READY == 2);
	CHECK((int)LS_REJECTED == 3 && (int)LS_LOST == 4);
	CHECK((int)END_NONE == 0 && (int)END_FINISHED == 1 && (int)END_PEER_TIMEOUT == 2);
	CHECK((int)END_DESYNC == 3 && (int)END_LINK_ERROR == 4 && (int)END_OPPONENT_LEFT == 5);
	CHECK((int)ACT_NONE == 0 && (int)ACT_BEGIN_LOBBY == 1 && (int)ACT_RESTART_LOBBY == 2);
	CHECK((int)ACT_START_RACE == 3 && (int)ACT_BEGIN_REMATCH == 4 && (int)ACT_CLOSE_LINK == 5);
	CHECK((int)ACT_RETURN_TO_TITLE == 6);
	/* Appended, never renumbered. */
	CHECK((int)SC_SELECT == 7 && (int)SC_SELECT_RESULT == 8);
	CHECK((int)ACT_BEGIN_SELECT == 7 && (int)ACT_RELINK == 8);
	CHECK((int)SS_PENDING == 0 && (int)SS_CONFIRMED == 1 && (int)SS_FAILED == 2);
	/* selectStatus took one reserved byte: the observation did not grow. */
	CHECK(sizeof(struct NativeArcadeFlowObservation) == 12u);
	CHECK(offsetof(struct NativeArcadeFlowObservation, raceFinished) == 8u);
	CHECK(offsetof(struct NativeArcadeFlowObservation, selectStatus) == 9u);
	CHECK(offsetof(struct NativeArcadeFlowObservation, reserved) == 10u);

	memset(&timings, 0xA5, sizeof(timings));
	NativeArcadeFlow_DefaultTimings(&timings);
	CHECK(timings.lobbyRetryPauseTicks == 30u);
	CHECK(timings.matchFoundHoldTicks == 45u);
	CHECK(timings.resultsDwellTicks == 30u);
	CHECK(timings.resultsIdleTimeoutTicks == 900u);
	CHECK(timings.rematchWaitTimeoutTicks == 300u);
	CHECK(timings.opponentLeftNoticeTicks == 90u);
	CHECK(timings.exitHoldTicks == 60u);
	CHECK(timings.selectResultHoldTicks == 60u);
	CHECK(timings.launchTimeoutTicks == 300u);

	/* NULL timings means the defaults; the rest of the struct is zeroed. */
	memset(&flow, 0xA5, sizeof(flow));
	CHECK(NativeArcadeFlow_Init(&flow, NULL) == 1);
	CHECK(memcmp(&flow.timings, &timings, sizeof(timings)) == 0);
	CHECK(flow.screen == SC_OFF);
	CHECK(flow.endReason == END_NONE);
	CHECK(flow.lobbyStatus == LS_WAITING);
	CHECK(flow.selectedRow == 0u);
	CHECK(flow.ticksInScreen == 0u);
	CHECK(flow.ticksSinceRetry == 0u);
	CHECK(flow.ticksSinceInput == 0u);
	CHECK(flow.screenSerial == 0u);
	CHECK(flow.relinked == 0u);
	CHECK(flow.ticksSinceRelink == 0u);

	/* Explicit defaults give the same struct. */
	memset(&sentinel, 0x5A, sizeof(sentinel));
	CHECK(NativeArcadeFlow_Init(&sentinel, &timings) == 1);
	CHECK(memcmp(&sentinel, &flow, sizeof(flow)) == 0);

	/* Rejections leave the struct untouched. */
	CHECK(NativeArcadeFlow_Init(NULL, NULL) == 0);
	CHECK(NativeArcadeFlow_Init(NULL, &timings) == 0);

	fields[0] = &bad.lobbyRetryPauseTicks;
	fields[1] = &bad.matchFoundHoldTicks;
	fields[2] = &bad.resultsDwellTicks;
	fields[3] = &bad.resultsIdleTimeoutTicks;
	fields[4] = &bad.rematchWaitTimeoutTicks;
	fields[5] = &bad.opponentLeftNoticeTicks;
	fields[6] = &bad.exitHoldTicks;
	fields[7] = &bad.selectResultHoldTicks;
	fields[8] = &bad.launchTimeoutTicks;
	for (i = 0; i < 9u; i++)
	{
		bad = timings;
		*fields[i] = 0u;
		memset(&flow, 0xA5, sizeof(flow));
		memset(&sentinel, 0xA5, sizeof(sentinel));
		CHECK(NativeArcadeFlow_Init(&flow, &bad) == 0);
		CHECK(memcmp(&flow, &sentinel, sizeof(flow)) == 0);
	}

	/* Idle must exceed dwell. */
	bad = timings;
	bad.resultsIdleTimeoutTicks = bad.resultsDwellTicks;
	memset(&flow, 0xA5, sizeof(flow));
	memset(&sentinel, 0xA5, sizeof(sentinel));
	CHECK(NativeArcadeFlow_Init(&flow, &bad) == 0);
	CHECK(memcmp(&flow, &sentinel, sizeof(flow)) == 0);

	bad.resultsIdleTimeoutTicks = bad.resultsDwellTicks - 1u;
	CHECK(NativeArcadeFlow_Init(&flow, &bad) == 0);
	CHECK(memcmp(&flow, &sentinel, sizeof(flow)) == 0);

	bad.resultsIdleTimeoutTicks = bad.resultsDwellTicks + 1u;
	CHECK(NativeArcadeFlow_Init(&flow, &bad) == 1);
	CHECK(flow.timings.resultsIdleTimeoutTicks == 31u);

	/* A rejection also leaves an already-initialized flow untouched. */
	CHECK(ToLobby(&flow) == 0);
	memcpy(&sentinel, &flow, sizeof(flow));
	bad = timings;
	bad.exitHoldTicks = 0u;
	CHECK(NativeArcadeFlow_Init(&flow, &bad) == 0);
	CHECK(memcmp(&flow, &sentinel, sizeof(flow)) == 0);
	return 0;
}

static int TestNullSafetyOffAndEnter(void)
{
	struct NativeArcadeFlow flow;
	struct NativeArcadeFlow snapshot;
	struct NativeArcadeFlowObservation observation = Obs(LS_READY, END_NONE, 0u);

	NativeArcadeFlow_DefaultTimings(NULL);
	CHECK(NativeArcadeFlow_Enter(NULL) == ACT_NONE);
	CHECK(NativeArcadeFlow_Tick(NULL, &observation, EV_CONFIRM) == ACT_NONE);
	CHECK(NativeArcadeFlow_Tick(NULL, NULL, EV_CONFIRM) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(NULL) == SC_OFF);
	CHECK(NativeArcadeFlow_EndReason(NULL) == END_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(NULL) == 0u);
	CHECK(NativeArcadeFlow_TicksInScreen(NULL) == 0u);
	CHECK(NativeArcadeFlow_LobbyStatus(NULL) == LS_WAITING);
	CHECK(NativeArcadeFlow_ScreenSerial(NULL) == 0u);

	/* NULL observation changes nothing. */
	CHECK(ToLobby(&flow) == 0);
	memcpy(&snapshot, &flow, sizeof(flow));
	CHECK(NativeArcadeFlow_Tick(&flow, NULL, EV_BACK) == ACT_NONE);
	CHECK(memcmp(&flow, &snapshot, sizeof(flow)) == 0);

	/* OFF ignores Tick completely: no counter moves. */
	CHECK(NativeArcadeFlow_Init(&flow, NULL) == 1);
	memcpy(&snapshot, &flow, sizeof(flow));
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_NONE);
	CHECK(Step(&flow, LS_LOST, END_DESYNC, 1u, EV_BACK) == ACT_NONE);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(memcmp(&flow, &snapshot, sizeof(flow)) == 0);

	/* Enter only from OFF. */
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	CHECK(NativeArcadeFlow_LobbyStatus(&flow) == LS_WAITING);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 1u);
	memcpy(&snapshot, &flow, sizeof(flow));
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_NONE);
	CHECK(memcmp(&flow, &snapshot, sizeof(flow)) == 0);

	/* The serial increments on every entry, including re-entry. */
	CHECK(Step(&flow, LS_CONNECTING, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 1u);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 1u);
	CHECK(NativeArcadeFlow_LobbyStatus(&flow) == LS_CONNECTING);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_MATCH_FOUND);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 2u);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 3u);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 4u);

	/* Event values above BACK are treated as NONE. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, (enum NativeArcadeMenuEvent)5) == ACT_NONE);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, (enum NativeArcadeMenuEvent)99) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	CHECK(ToResultsArmed(&flow) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, (enum NativeArcadeMenuEvent)7) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(flow.ticksSinceInput == 31u);
	return 0;
}

static int ExpectInvalidIgnored(struct NativeArcadeFlow *flow)
{
	struct NativeArcadeFlow snapshot;

	memcpy(&snapshot, flow, sizeof(*flow));
	CHECK(Step(flow, 5u, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(flow, 5u, END_NONE, 0u, EV_BACK) == ACT_NONE);
	CHECK(Step(flow, LS_READY, END_FINISHED, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(flow, LS_READY, END_OPPONENT_LEFT, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(flow, LS_READY, 6u, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(flow, LS_READY, END_NONE, 2u, EV_NONE) == ACT_NONE);
	CHECK(Step(flow, LS_LOST, END_NONE, 2u, EV_BACK) == ACT_NONE);
	/* A select status above FAILED is invalid on every screen, including
	 * one that would otherwise leave SELECT or start a race. */
	CHECK(StepSel(flow, LS_READY, 3u, EV_NONE) == ACT_NONE);
	CHECK(StepSel(flow, LS_READY, 255u, EV_CONFIRM) == ACT_NONE);
	CHECK(StepSel(flow, LS_LOST, 3u, EV_BACK) == ACT_NONE);
	CHECK(memcmp(flow, &snapshot, sizeof(*flow)) == 0);
	return 0;
}

static int TestInvalidObservations(void)
{
	struct NativeArcadeFlow flow;

	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_WAITING, EV_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);

	CHECK(ToRacing(&flow) == 0);
	CHECK(RunQuiet(&flow, 3u, LS_READY, EV_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);

	CHECK(ToResults(&flow, END_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);

	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);

	CHECK(ToSelect(&flow) == 0);
	CHECK(RunQuiet(&flow, 5u, LS_READY, EV_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);

	CHECK(ToSelectResult(&flow) == 0);
	CHECK(RunQuiet(&flow, 59u, LS_READY, EV_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);

	CHECK(ToRelinked(&flow) == 0);
	CHECK(RunQuiet(&flow, 299u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(ExpectInvalidIgnored(&flow) == 0);
	/* Still exactly on the launch-timeout tick afterwards. */
	CHECK(Step(&flow, LS_CONNECTING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);
	return 0;
}

static int TestLobby(void)
{
	struct NativeArcadeFlow flow;
	uint32_t statuses[5];
	size_t i;

	/* CONNECTING stays indefinitely. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 500u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 500u);

	/* WAITING: RESTART_LOBBY exactly on the 30th tick, and 30 ticks later,
	 * without a screen entry. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 1u);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 60u);

	/* A CONNECTING tick in between resets the pause. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 20u, LS_WAITING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 1u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);

	/* LOST behaves like WAITING, and the two share the pause. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(RunQuiet(&flow, 15u, LS_WAITING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 14u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);

	/* Non-BACK events do not disturb the WAITING pause. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_CONFIRM) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NEXT) == ACT_RESTART_LOBBY);

	/* REJECTED never restarts automatically; CONFIRM restarts in place. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 1000u, LS_REJECTED, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 5u, LS_REJECTED, EV_PREV) == 0);
	CHECK(RunQuiet(&flow, 5u, LS_REJECTED, EV_NEXT) == 0);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, EV_CONFIRM) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 1u);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 1011u);
	CHECK(NativeArcadeFlow_LobbyStatus(&flow) == LS_REJECTED);

	/* REJECTED resets the retry pause. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 20u, LS_WAITING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 1u, LS_REJECTED, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);

	/* READY enters MATCH_FOUND with no action. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(RunQuiet(&flow, 3u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_MATCH_FOUND);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 2u);
	CHECK(NativeArcadeFlow_LobbyStatus(&flow) == LS_READY);

	/* BACK exits with CLOSE_LINK from every status, READY included. */
	statuses[0] = LS_WAITING;
	statuses[1] = LS_CONNECTING;
	statuses[2] = LS_READY;
	statuses[3] = LS_REJECTED;
	statuses[4] = LS_LOST;
	for (i = 0; i < 5u; i++)
	{
		CHECK(ToLobby(&flow) == 0);
		CHECK(RunQuiet(&flow, 3u, statuses[i] == LS_READY ? LS_CONNECTING : statuses[i], EV_NONE) == 0);
		CHECK(Step(&flow, statuses[i], END_NONE, 0u, EV_BACK) == ACT_CLOSE_LINK);
		CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
		CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
		CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
		CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 2u);
	}
	return 0;
}

static int TestMatchFound(void)
{
	struct NativeArcadeFlow flow;

	/* BEGIN_SELECT (never START_RACE) exactly on tick 45, not 44. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 44u);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 3u);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);

	/* The select status is not read on MATCH_FOUND: CONFIRMED or FAILED
	 * during the hold changes nothing. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuietSel(&flow, 22u, LS_READY, SS_CONFIRMED, EV_NONE) == 0);
	CHECK(RunQuietSel(&flow, 22u, LS_READY, SS_FAILED, EV_NONE) == 0);
	CHECK(StepSel(&flow, LS_READY, SS_FAILED, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT);

	/* Events are ignored, BACK included. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_BACK) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_PREV) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_NEXT) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_BACK) == ACT_BEGIN_SELECT);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT);

	/* LOST during the hold returns to LOBBY with RESTART_LOBBY. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 20u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 3u);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);

	/* Leaving READY on the would-be start tick still returns to LOBBY. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	return 0;
}

static int TestSelect(void)
{
	struct NativeArcadeFlow flow;
	uint32_t statuses[4];
	uint8_t selects[3];
	uint32_t serial;
	size_t i;
	size_t j;

	/* PENDING with READY stays indefinitely: the flow has no select
	 * timeout of its own (the select session bounds the phase). */
	CHECK(ToSelect(&flow) == 0);
	CHECK(RunQuiet(&flow, 2000u, LS_READY, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 2000u);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);

	/* Every menu event is ignored, BACK included (SEL-7). */
	CHECK(ToSelect(&flow) == 0);
	serial = NativeArcadeFlow_ScreenSerial(&flow);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_BACK) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_PREV) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_NEXT) == 0);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == serial);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == 0u);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);

	/* Race-side observation fields are not read in SELECT. */
	CHECK(ToSelect(&flow) == 0);
	CHECK(Step(&flow, LS_READY, END_DESYNC, 1u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT);

	/* A lobby status other than READY is LINK ERROR and closes the link,
	 * checked before the select status: whatever the select status, and
	 * whatever the event. */
	statuses[0] = LS_WAITING;
	statuses[1] = LS_CONNECTING;
	statuses[2] = LS_REJECTED;
	statuses[3] = LS_LOST;
	selects[0] = SS_PENDING;
	selects[1] = SS_CONFIRMED;
	selects[2] = SS_FAILED;
	for (i = 0; i < 4u; i++)
	{
		for (j = 0; j < 3u; j++)
		{
			CHECK(ToSelect(&flow) == 0);
			CHECK(RunQuiet(&flow, 7u, LS_READY, EV_NONE) == 0);
			serial = NativeArcadeFlow_ScreenSerial(&flow);
			CHECK(StepSel(&flow, statuses[i], selects[j], EV_BACK) == ACT_CLOSE_LINK);
			CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
			CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);
			CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);
			CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
			CHECK(NativeArcadeFlow_ScreenSerial(&flow) == serial + 1u);
		}
	}

	/* FAILED with READY is LINK ERROR and closes the link, on the very first
	 * select tick too. */
	CHECK(ToSelect(&flow) == 0);
	CHECK(StepSel(&flow, LS_READY, SS_FAILED, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);

	/* The results screen after a select failure is the ordinary one: after
	 * the dwell, CONFIRM on REMATCH begins a rematch. */
	CHECK(RunQuiet(&flow, 30u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_BEGIN_REMATCH);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_REMATCH_WAIT);

	/* CONFIRMED with READY moves to SELECT_RESULT with no action, even with
	 * BACK held on that tick. */
	CHECK(ToSelect(&flow) == 0);
	CHECK(RunQuiet(&flow, 12u, LS_READY, EV_NONE) == 0);
	serial = NativeArcadeFlow_ScreenSerial(&flow);
	CHECK(StepSel(&flow, LS_READY, SS_CONFIRMED, EV_BACK) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == serial + 1u);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	CHECK(flow.relinked == 0u);
	return 0;
}

static int TestSelectResult(void)
{
	struct NativeArcadeFlow flow;
	uint32_t statuses[5];
	size_t i;
	uint32_t tick;

	statuses[0] = LS_WAITING;
	statuses[1] = LS_CONNECTING;
	statuses[2] = LS_READY;
	statuses[3] = LS_REJECTED;
	statuses[4] = LS_LOST;

	/* Phase 1: RELINK exactly on tick 60, not 59, whatever the (old) lobby
	 * status, select status, or event. */
	for (i = 0; i < 5u; i++)
	{
		CHECK(ToSelectResult(&flow) == 0);
		CHECK(RunQuietSel(&flow, 20u, statuses[i], SS_FAILED, EV_BACK) == 0);
		CHECK(RunQuietSel(&flow, 20u, statuses[i], SS_PENDING, EV_CONFIRM) == 0);
		CHECK(RunQuietSel(&flow, 19u, statuses[i], SS_CONFIRMED, EV_NEXT) == 0);
		CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 59u);
		CHECK(flow.relinked == 0u);
		CHECK(StepSel(&flow, statuses[i], SS_FAILED, EV_BACK) == ACT_RELINK);
		CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
		CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 60u);
		CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
		CHECK(flow.relinked == 1u);
		CHECK(flow.ticksSinceRelink == 0u);
	}

	/* The READY observed on the RELINK tick is the old link's: it returns
	 * RELINK, not START_RACE, and is not remembered. */
	CHECK(ToSelectResult(&flow) == 0);
	CHECK(RunQuiet(&flow, 59u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RELINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
	CHECK(Step(&flow, LS_CONNECTING, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
	CHECK(flow.ticksSinceRelink == 1u);
	/* Phase 2 READY: RACING with START_RACE. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RACING);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(flow.relinked == 0u);

	/* READY on the first tick after RELINK starts at once, whatever the
	 * select status and event. */
	CHECK(ToRelinked(&flow) == 0);
	CHECK(StepSel(&flow, LS_READY, SS_FAILED, EV_BACK) == ACT_START_RACE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RACING);

	/* REJECTED after RELINK is LINK ERROR. */
	CHECK(ToRelinked(&flow) == 0);
	CHECK(RunQuiet(&flow, 4u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);

	/* Launch timeout exactly on the 300th tick after RELINK. */
	CHECK(ToRelinked(&flow) == 0);
	CHECK(RunQuiet(&flow, 299u, LS_CONNECTING, EV_BACK) == 0);
	CHECK(flow.ticksSinceRelink == 299u);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 359u);
	CHECK(Step(&flow, LS_CONNECTING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);

	/* READY outranks the timeout on the 300th tick. */
	CHECK(ToRelinked(&flow) == 0);
	CHECK(RunQuiet(&flow, 299u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RACING);

	/* WAITING: RESTART_LOBBY at ticks 30, 60, ... 270 after RELINK without a
	 * screen entry; the timeout outranks the retry on tick 300. */
	CHECK(ToRelinked(&flow) == 0);
	for (tick = 1u; tick < 300u; tick++)
	{
		enum NativeArcadeFlowAction action = Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE);

		CHECK(action == (((tick % 30u) == 0u) ? ACT_RESTART_LOBBY : ACT_NONE));
		CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
		CHECK(flow.relinked == 1u);
	}
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);

	/* LOST shares the pause with WAITING; CONNECTING resets it. */
	CHECK(ToRelinked(&flow) == 0);
	CHECK(RunQuiet(&flow, 15u, LS_WAITING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 14u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(RunQuiet(&flow, 20u, LS_LOST, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 1u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);

	/* The phase-1 retry counter does not leak into phase 2: the pause
	 * starts on the tick after RELINK. */
	CHECK(ToSelectResult(&flow) == 0);
	CHECK(RunQuiet(&flow, 59u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RELINK);
	CHECK(RunQuiet(&flow, 29u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	return 0;
}

static int TestRacing(void)
{
	struct NativeArcadeFlow flow;
	uint32_t failures[3];
	size_t i;

	failures[0] = END_PEER_TIMEOUT;
	failures[1] = END_DESYNC;
	failures[2] = END_LINK_ERROR;

	/* Each link failure maps to RESULTS with that reason and returns NONE:
	 * unlike a pre-race LINK ERROR, the in-race path keeps the link open so
	 * the caller can read the latched session report. */
	for (i = 0; i < 3u; i++)
	{
		CHECK(ToRacing(&flow) == 0);
		CHECK(RunQuiet(&flow, 100u, LS_READY, EV_NONE) == 0);
		CHECK(Step(&flow, LS_READY, failures[i], 0u, EV_NONE) == ACT_NONE);
		CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
		CHECK(NativeArcadeFlow_EndReason(&flow) == failures[i]);
		CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);
		CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);

		/* A failure outranks a same-tick finish (UX-6). */
		CHECK(ToRacing(&flow) == 0);
		CHECK(Step(&flow, LS_READY, failures[i], 1u, EV_NONE) == ACT_NONE);
		CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
		CHECK(NativeArcadeFlow_EndReason(&flow) == failures[i]);

		/* ...and a LOST status. */
		CHECK(ToRacing(&flow) == 0);
		CHECK(Step(&flow, LS_LOST, failures[i], 1u, EV_NONE) == ACT_NONE);
		CHECK(NativeArcadeFlow_EndReason(&flow) == failures[i]);
	}

	/* raceFinished gives FINISHED. */
	CHECK(ToRacing(&flow) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 1u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);

	/* LOST without a failure gives LINK_ERROR, even with a finish. */
	CHECK(ToRacing(&flow) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);
	CHECK(ToRacing(&flow) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 1u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);

	/* Menu events and non-LOST statuses do nothing in a race. */
	CHECK(ToRacing(&flow) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_READY, EV_BACK) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_READY, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_READY, EV_PREV) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_READY, EV_NEXT) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_WAITING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 50u, LS_REJECTED, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 350u);
	return 0;
}

static int TestResults(void)
{
	struct NativeArcadeFlow flow;
	uint32_t serial;

	/* Default focus REMATCH; CONFIRM on tick 30 ignored, tick 31 accepted. */
	CHECK(ToResults(&flow, END_NONE) == 0);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);
	CHECK(RunQuiet(&flow, 29u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 30u);
	serial = NativeArcadeFlow_ScreenSerial(&flow);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_BEGIN_REMATCH);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_REMATCH_WAIT);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == serial + 1u);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);

	/* Every event is ignored during the dwell. */
	CHECK(ToResults(&flow, END_NONE) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_BACK) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_NEXT) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_READY, EV_PREV) == 0);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);

	/* PREV/NEXT toggle and wrap. */
	CHECK(ToResultsArmed(&flow) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NEXT) == ACT_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_EXIT);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NEXT) == ACT_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_PREV) == ACT_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_EXIT);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_PREV) == ACT_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_REMATCH);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);

	/* BACK moves focus to EXIT without leaving, from either row. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_BACK) == ACT_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_EXIT);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_BACK) == ACT_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_EXIT);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);

	/* CONFIRM on EXIT closes the link with the end reason unchanged. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);

	CHECK(ToResults(&flow, END_DESYNC) == 0);
	CHECK(RunQuiet(&flow, 30u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_BACK) == ACT_NONE);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_CONFIRM) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_DESYNC);

	/* Lobby status is ignored on this screen. */
	CHECK(ToResults(&flow, END_PEER_TIMEOUT) == 0);
	CHECK(RunQuiet(&flow, 20u, LS_LOST, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 20u, LS_REJECTED, EV_NONE) == 0);
	CHECK(RunQuiet(&flow, 20u, LS_WAITING, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_PEER_TIMEOUT);

	/* Idle timeout on exactly the 900th tick without an accepted event;
	 * events inside the dwell are not accepted and count as idle. */
	CHECK(ToResults(&flow, END_NONE) == 0);
	CHECK(RunQuiet(&flow, 30u, LS_READY, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 869u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);

	/* An accepted event resets the idle count. */
	CHECK(ToResults(&flow, END_LINK_ERROR) == 0);
	CHECK(RunQuiet(&flow, 500u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NEXT) == ACT_NONE);
	CHECK(RunQuiet(&flow, 899u, LS_READY, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_EXIT);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);
	return 0;
}

static int TestRematchWait(void)
{
	struct NativeArcadeFlow flow;
	uint32_t i;

	/* READY gives MATCH_FOUND, then select again (OD-3), never a race
	 * straight away. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_MATCH_FOUND);
	CHECK(RunQuiet(&flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT);

	/* REJECTED: the other cabinet left. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 5u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_OPPONENT_LEFT);

	/* Timeout exactly at tick 300. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 299u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_CONNECTING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_OPPONENT_LEFT);

	/* BACK cancels with END_NONE. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 5u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_BACK) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_EXIT);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);

	/* Other events are ignored. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_CONNECTING, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_CONNECTING, EV_PREV) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_CONNECTING, EV_NEXT) == 0);

	/* WAITING retry pause gives RESTART_LOBBY at tick 30 and every 30
	 * ticks after; the timeout outranks the retry on tick 300. */
	CHECK(ToRematchWait(&flow) == 0);
	for (i = 1u; i < 300u; i++)
	{
		enum NativeArcadeFlowAction action = Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE);

		CHECK(action == (((i % 30u) == 0u) ? ACT_RESTART_LOBBY : ACT_NONE));
		CHECK(NativeArcadeFlow_Screen(&flow) == SC_REMATCH_WAIT);
	}
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_OPPONENT_LEFT);

	/* LOST uses the same pause. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_REMATCH_WAIT);
	return 0;
}

static int TestExit(void)
{
	struct NativeArcadeFlow flow;

	/* OPPONENT_LEFT holds 90 ticks. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(RunQuiet(&flow, 30u, LS_REJECTED, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 30u, LS_REJECTED, EV_BACK) == 0);
	CHECK(RunQuiet(&flow, 29u, LS_READY, EV_NEXT) == 0);
	CHECK(Step(&flow, LS_REJECTED, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == 0u);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);

	/* END_NONE (lobby BACK) holds 60 ticks. */
	CHECK(ToLobby(&flow) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_BACK) == ACT_CLOSE_LINK);
	CHECK(RunQuiet(&flow, 59u, LS_WAITING, EV_BACK) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);

	/* FINISHED with focus on EXIT holds 60 ticks and clears the row. */
	CHECK(ToResultsArmed(&flow) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NEXT) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == ROW_EXIT);
	CHECK(RunQuiet(&flow, 59u, LS_LOST, EV_CONFIRM) == 0);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	CHECK(NativeArcadeFlow_SelectedRow(&flow) == 0u);

	/* Failure reasons hold 60 ticks too. */
	CHECK(ToResults(&flow, END_PEER_TIMEOUT) == 0);
	CHECK(RunQuiet(&flow, 899u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(RunQuiet(&flow, 59u, LS_LOST, EV_NONE) == 0);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);

	/* Back on OFF, the flow is inert and can be entered again. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_LOBBY);
	return 0;
}

struct FlowScript
{
	uint32_t count;
	uint32_t status;
	uint32_t failure;
	uint8_t finished;
	enum NativeArcadeMenuEvent event;
	uint8_t selectStatus;
};

struct FlowActionAt
{
	uint32_t tick;
	enum NativeArcadeFlowAction action;
};

static int TestHappyPath(void)
{
	static const struct FlowScript script[] = {
		{5u, LS_CONNECTING, END_NONE, 0u, EV_NONE, SS_PENDING},   /* ticks 1-5 */
		{1u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},        /* 6: MATCH_FOUND */
		{45u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},       /* 7-51: BEGIN_SELECT on 51 */
		{10u, LS_READY, END_NONE, 0u, EV_NEXT, SS_PENDING},       /* 52-61: selecting */
		{1u, LS_READY, END_NONE, 0u, EV_NONE, SS_CONFIRMED},      /* 62: SELECT_RESULT */
		{60u, LS_READY, END_NONE, 0u, EV_NONE, SS_CONFIRMED},     /* 63-122: RELINK on 122 */
		{5u, LS_CONNECTING, END_NONE, 0u, EV_NONE, SS_CONFIRMED}, /* 123-127: relinking */
		{1u, LS_READY, END_NONE, 0u, EV_NONE, SS_CONFIRMED},      /* 128: START_RACE */
		{100u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},      /* 129-228: racing */
		{1u, LS_READY, END_NONE, 1u, EV_NONE, SS_PENDING},        /* 229: RESULTS */
		{30u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},       /* 230-259: dwell */
		{1u, LS_READY, END_NONE, 0u, EV_CONFIRM, SS_PENDING},     /* 260: BEGIN_REMATCH */
		{10u, LS_CONNECTING, END_NONE, 0u, EV_NONE, SS_PENDING},  /* 261-270 */
		{1u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},        /* 271: MATCH_FOUND */
		{45u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},       /* 272-316: BEGIN_SELECT on 316 */
		{1u, LS_READY, END_NONE, 0u, EV_NONE, SS_CONFIRMED},      /* 317: SELECT_RESULT */
		{60u, LS_READY, END_NONE, 0u, EV_BACK, SS_CONFIRMED},     /* 318-377: RELINK on 377 */
		{1u, LS_READY, END_NONE, 0u, EV_NONE, SS_CONFIRMED},      /* 378: START_RACE */
		{10u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},       /* 379-388 */
		{1u, LS_READY, END_NONE, 1u, EV_NONE, SS_PENDING},        /* 389: RESULTS */
		{30u, LS_READY, END_NONE, 0u, EV_NONE, SS_PENDING},       /* 390-419: dwell */
		{1u, LS_READY, END_NONE, 0u, EV_NEXT, SS_PENDING},        /* 420: focus EXIT */
		{1u, LS_READY, END_NONE, 0u, EV_CONFIRM, SS_PENDING},     /* 421: CLOSE_LINK */
		{60u, LS_WAITING, END_NONE, 0u, EV_NONE, SS_PENDING},     /* 422-481: RETURN_TO_TITLE on 481 */
		{10u, LS_READY, END_NONE, 0u, EV_CONFIRM, SS_PENDING},    /* 482-491: OFF, inert */
	};
	static const struct FlowActionAt expected[] = {
		{0u, ACT_BEGIN_LOBBY},
		{51u, ACT_BEGIN_SELECT},
		{122u, ACT_RELINK},
		{128u, ACT_START_RACE},
		{260u, ACT_BEGIN_REMATCH},
		{316u, ACT_BEGIN_SELECT},
		{377u, ACT_RELINK},
		{378u, ACT_START_RACE},
		{421u, ACT_CLOSE_LINK},
		{481u, ACT_RETURN_TO_TITLE},
	};
	static const uint32_t expectedScreens[] = {SC_LOBBY, SC_SELECT, SC_SELECT_RESULT, SC_RACING, SC_REMATCH_WAIT,
		SC_SELECT, SC_SELECT_RESULT, SC_RACING, SC_EXIT, SC_OFF};
	struct FlowActionAt seen[16];
	uint32_t seenScreens[16];
	size_t seenCount = 0;
	struct NativeArcadeFlow flow;
	uint32_t tick = 0;
	size_t i;
	uint32_t j;

	CHECK(NativeArcadeFlow_Init(&flow, NULL) == 1);
	seen[seenCount].tick = 0u;
	seen[seenCount].action = NativeArcadeFlow_Enter(&flow);
	seenScreens[seenCount] = NativeArcadeFlow_Screen(&flow);
	seenCount++;

	for (i = 0; i < sizeof(script) / sizeof(script[0]); i++)
	{
		for (j = 0; j < script[i].count; j++)
		{
			struct NativeArcadeFlowObservation observation =
				Obs(script[i].status, script[i].failure, script[i].finished);
			enum NativeArcadeFlowAction action;

			tick++;
			observation.selectStatus = script[i].selectStatus;
			action = NativeArcadeFlow_Tick(&flow, &observation, script[i].event);
			if (action != ACT_NONE)
			{
				CHECK(seenCount < 16u);
				seen[seenCount].tick = tick;
				seen[seenCount].action = action;
				seenScreens[seenCount] = NativeArcadeFlow_Screen(&flow);
				seenCount++;
			}
			if (tick == 6u || tick == 271u)
			{
				CHECK(NativeArcadeFlow_Screen(&flow) == SC_MATCH_FOUND);
			}
			if (tick == 62u || tick == 317u)
			{
				CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
			}
			if (tick == 229u || tick == 389u)
			{
				CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
				CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);
			}
		}
	}

	CHECK(tick == 491u);
	CHECK(seenCount == sizeof(expected) / sizeof(expected[0]));
	for (i = 0; i < seenCount; i++)
	{
		CHECK(seen[i].tick == expected[i].tick);
		CHECK(seen[i].action == expected[i].action);
		CHECK(seenScreens[i] == expectedScreens[i]);
	}
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	/* LOBBY, MATCH_FOUND, SELECT, SELECT_RESULT, RACING, RESULTS,
	 * REMATCH_WAIT, MATCH_FOUND, SELECT, SELECT_RESULT, RACING, RESULTS, EXIT,
	 * OFF. */
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 14u);
	return 0;
}

static int TestCustomTimings(void)
{
	struct NativeArcadeFlowTimings timings;
	struct NativeArcadeFlow flow;

	timings.lobbyRetryPauseTicks = 2u;
	timings.matchFoundHoldTicks = 3u;
	timings.resultsDwellTicks = 2u;
	timings.resultsIdleTimeoutTicks = 5u;
	timings.rematchWaitTimeoutTicks = 4u;
	timings.opponentLeftNoticeTicks = 3u;
	timings.exitHoldTicks = 2u;
	timings.selectResultHoldTicks = 2u;
	timings.launchTimeoutTicks = 3u;
	CHECK(NativeArcadeFlow_Init(&flow, &timings) == 1);
	CHECK(memcmp(&flow.timings, &timings, sizeof(timings)) == 0);

	/* Lobby retry every 2 ticks. */
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_BEGIN_LOBBY);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);

	/* Match-found hold of 3, then select. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(StepSel(&flow, LS_READY, SS_CONFIRMED, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);

	/* Result hold of 2: RELINK on tick 2. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RELINK);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);

	/* Dwell of 2: CONFIRM on tick 2 ignored, tick 3 accepted. */
	CHECK(Step(&flow, LS_READY, END_NONE, 1u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_CONFIRM) == ACT_BEGIN_REMATCH);

	/* Rematch timeout of 4, then the opponent-left notice of 3. */
	CHECK(RunQuiet(&flow, 3u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_CONNECTING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_OPPONENT_LEFT);
	CHECK(RunQuiet(&flow, 2u, LS_WAITING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);

	/* Idle timeout of 5, then the exit hold of 2. */
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_BEGIN_LOBBY);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(RunQuiet(&flow, 2u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(StepSel(&flow, LS_READY, SS_CONFIRMED, EV_NONE) == ACT_NONE);
	CHECK(RunQuiet(&flow, 1u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RELINK);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);
	CHECK(Step(&flow, LS_READY, END_DESYNC, 0u, EV_NONE) == ACT_NONE);
	CHECK(RunQuiet(&flow, 4u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_DESYNC);
	CHECK(RunQuiet(&flow, 1u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);

	/* Launch timeout of 3: LINK ERROR on the third tick after RELINK, with
	 * the retry pause of 2 firing once before it. */
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_BEGIN_LOBBY);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(RunQuiet(&flow, 2u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_BEGIN_SELECT);
	CHECK(StepSel(&flow, LS_READY, SS_CONFIRMED, EV_NONE) == ACT_NONE);
	CHECK(RunQuiet(&flow, 1u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RELINK);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_SELECT_RESULT);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_LINK_ERROR);
	return 0;
}

int main(void)
{
	CHECK(TestDefaultsAndInit() == 0);
	CHECK(TestNullSafetyOffAndEnter() == 0);
	CHECK(TestInvalidObservations() == 0);
	CHECK(TestLobby() == 0);
	CHECK(TestMatchFound() == 0);
	CHECK(TestSelect() == 0);
	CHECK(TestSelectResult() == 0);
	CHECK(TestRacing() == 0);
	CHECK(TestResults() == 0);
	CHECK(TestRematchWait() == 0);
	CHECK(TestExit() == 0);
	CHECK(TestHappyPath() == 0);
	CHECK(TestCustomTimings() == 0);
	puts("native_arcade_flow_test: ok");
	return 0;
}
