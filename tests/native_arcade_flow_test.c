#include "platform/native_arcade_flow.h"

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

/* Runs `count` ticks of the same lobby status and event; every tick must
 * return NONE and keep the screen. */
static int RunQuiet(struct NativeArcadeFlow *flow, uint32_t count, uint32_t status, enum NativeArcadeMenuEvent event)
{
	uint32_t screen = NativeArcadeFlow_Screen(flow);
	uint32_t i;

	for (i = 0; i < count; i++)
	{
		CHECK(Step(flow, status, END_NONE, 0u, event) == ACT_NONE);
		CHECK(NativeArcadeFlow_Screen(flow) == screen);
	}
	return 0;
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

static int ToRacing(struct NativeArcadeFlow *flow)
{
	CHECK(ToMatchFound(flow) == 0);
	CHECK(RunQuiet(flow, 44u, LS_READY, EV_NONE) == 0);
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
	uint32_t *fields[7];
	size_t i;

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

	memset(&timings, 0xA5, sizeof(timings));
	NativeArcadeFlow_DefaultTimings(&timings);
	CHECK(timings.lobbyRetryPauseTicks == 30u);
	CHECK(timings.matchFoundHoldTicks == 45u);
	CHECK(timings.resultsDwellTicks == 30u);
	CHECK(timings.resultsIdleTimeoutTicks == 900u);
	CHECK(timings.rematchWaitTimeoutTicks == 300u);
	CHECK(timings.opponentLeftNoticeTicks == 90u);
	CHECK(timings.exitHoldTicks == 60u);

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
	for (i = 0; i < 7u; i++)
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

	/* START_RACE exactly on tick 45, not 44. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 44u);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RACING);
	CHECK(NativeArcadeFlow_TicksInScreen(&flow) == 0u);
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 3u);

	/* Events are ignored, BACK included. */
	CHECK(ToMatchFound(&flow) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_BACK) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_CONFIRM) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_PREV) == 0);
	CHECK(RunQuiet(&flow, 11u, LS_READY, EV_NEXT) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_BACK) == ACT_START_RACE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_RACING);

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

static int TestRacing(void)
{
	struct NativeArcadeFlow flow;
	uint32_t failures[3];
	size_t i;

	failures[0] = END_PEER_TIMEOUT;
	failures[1] = END_DESYNC;
	failures[2] = END_LINK_ERROR;

	/* Each link failure maps to RESULTS with that reason. */
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

	/* READY gives MATCH_FOUND, then a race as usual. */
	CHECK(ToRematchWait(&flow) == 0);
	CHECK(RunQuiet(&flow, 10u, LS_CONNECTING, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_MATCH_FOUND);
	CHECK(RunQuiet(&flow, 44u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);

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
};

struct FlowActionAt
{
	uint32_t tick;
	enum NativeArcadeFlowAction action;
};

static int TestHappyPath(void)
{
	static const struct FlowScript script[] = {
		{5u, LS_CONNECTING, END_NONE, 0u, EV_NONE},  /* ticks 1-5 */
		{1u, LS_READY, END_NONE, 0u, EV_NONE},       /* 6: MATCH_FOUND */
		{45u, LS_READY, END_NONE, 0u, EV_NONE},      /* 7-51: START_RACE on 51 */
		{100u, LS_READY, END_NONE, 0u, EV_NONE},     /* 52-151: racing */
		{1u, LS_READY, END_NONE, 1u, EV_NONE},       /* 152: RESULTS */
		{30u, LS_READY, END_NONE, 0u, EV_NONE},      /* 153-182: dwell */
		{1u, LS_READY, END_NONE, 0u, EV_CONFIRM},    /* 183: BEGIN_REMATCH */
		{10u, LS_CONNECTING, END_NONE, 0u, EV_NONE}, /* 184-193 */
		{1u, LS_READY, END_NONE, 0u, EV_NONE},       /* 194: MATCH_FOUND */
		{45u, LS_READY, END_NONE, 0u, EV_NONE},      /* 195-239: START_RACE on 239 */
		{10u, LS_READY, END_NONE, 0u, EV_NONE},      /* 240-249 */
		{1u, LS_READY, END_NONE, 1u, EV_NONE},       /* 250: RESULTS */
		{30u, LS_READY, END_NONE, 0u, EV_NONE},      /* 251-280: dwell */
		{1u, LS_READY, END_NONE, 0u, EV_NEXT},       /* 281: focus EXIT */
		{1u, LS_READY, END_NONE, 0u, EV_CONFIRM},    /* 282: CLOSE_LINK */
		{60u, LS_WAITING, END_NONE, 0u, EV_NONE},    /* 283-342: RETURN_TO_TITLE on 342 */
		{10u, LS_READY, END_NONE, 0u, EV_CONFIRM},   /* 343-352: OFF, inert */
	};
	static const struct FlowActionAt expected[] = {
		{0u, ACT_BEGIN_LOBBY},
		{51u, ACT_START_RACE},
		{183u, ACT_BEGIN_REMATCH},
		{239u, ACT_START_RACE},
		{282u, ACT_CLOSE_LINK},
		{342u, ACT_RETURN_TO_TITLE},
	};
	static const uint32_t expectedScreens[] = {SC_LOBBY, SC_RACING, SC_REMATCH_WAIT, SC_RACING, SC_EXIT, SC_OFF};
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
			enum NativeArcadeFlowAction action;

			tick++;
			action = Step(&flow, script[i].status, script[i].failure, script[i].finished, script[i].event);
			if (action != ACT_NONE)
			{
				CHECK(seenCount < 16u);
				seen[seenCount].tick = tick;
				seen[seenCount].action = action;
				seenScreens[seenCount] = NativeArcadeFlow_Screen(&flow);
				seenCount++;
			}
			if (tick == 6u || tick == 194u)
			{
				CHECK(NativeArcadeFlow_Screen(&flow) == SC_MATCH_FOUND);
			}
			if (tick == 152u || tick == 250u)
			{
				CHECK(NativeArcadeFlow_Screen(&flow) == SC_RESULTS);
				CHECK(NativeArcadeFlow_EndReason(&flow) == END_FINISHED);
			}
		}
	}

	CHECK(tick == 352u);
	CHECK(seenCount == sizeof(expected) / sizeof(expected[0]));
	for (i = 0; i < seenCount; i++)
	{
		CHECK(seen[i].tick == expected[i].tick);
		CHECK(seen[i].action == expected[i].action);
		CHECK(seenScreens[i] == expectedScreens[i]);
	}
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_NONE);
	/* LOBBY, MATCH_FOUND, RACING, RESULTS, REMATCH_WAIT, MATCH_FOUND, RACING,
	 * RESULTS, EXIT, OFF. */
	CHECK(NativeArcadeFlow_ScreenSerial(&flow) == 10u);
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
	CHECK(NativeArcadeFlow_Init(&flow, &timings) == 1);
	CHECK(memcmp(&flow.timings, &timings, sizeof(timings)) == 0);

	/* Lobby retry every 2 ticks. */
	CHECK(NativeArcadeFlow_Enter(&flow) == ACT_BEGIN_LOBBY);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_WAITING, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_LOST, END_NONE, 0u, EV_NONE) == ACT_RESTART_LOBBY);

	/* Match-found hold of 3. */
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_NONE);
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
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_START_RACE);
	CHECK(Step(&flow, LS_READY, END_DESYNC, 0u, EV_NONE) == ACT_NONE);
	CHECK(RunQuiet(&flow, 4u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_CLOSE_LINK);
	CHECK(NativeArcadeFlow_EndReason(&flow) == END_DESYNC);
	CHECK(RunQuiet(&flow, 1u, LS_READY, EV_NONE) == 0);
	CHECK(Step(&flow, LS_READY, END_NONE, 0u, EV_NONE) == ACT_RETURN_TO_TITLE);
	CHECK(NativeArcadeFlow_Screen(&flow) == SC_OFF);
	return 0;
}

int main(void)
{
	CHECK(TestDefaultsAndInit() == 0);
	CHECK(TestNullSafetyOffAndEnter() == 0);
	CHECK(TestInvalidObservations() == 0);
	CHECK(TestLobby() == 0);
	CHECK(TestMatchFound() == 0);
	CHECK(TestRacing() == 0);
	CHECK(TestResults() == 0);
	CHECK(TestRematchWait() == 0);
	CHECK(TestExit() == 0);
	CHECK(TestHappyPath() == 0);
	CHECK(TestCustomTimings() == 0);
	puts("native_arcade_flow_test: ok");
	return 0;
}
