#include "platform/native_arcade_netplay.h"
#include "platform/native_lockstep_rematch.h"

#include "native_lockstep_peer_link_test_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Tests for the arcade-link host adapter (platform/native_arcade_netplay.c,
 * docs/GAME_LOOP_UI_MILESTONE.md section 2.3, Tasks 4, 4b, and 4c, and the
 * select phase of docs/MATCH_SELECT_MILESTONE.md section 2.6, MS-7: every
 * path to START_RACE now goes MATCH_FOUND -> SELECT -> SELECT_RESULT ->
 * RELINK -> READY, and the race runs on the resolved config; since RL-S5 of
 * docs/RACE_LAUNCH_MILESTONE.md, READY of the relink also needs a launch
 * commit, tested from case 33a on). The socket
 * tests run two adapters in this one process -- A as CAB1_HUMAN and B as
 * CAB2_HUMAN -- over real loopback UDP sockets through the real, unmodified
 * lobby layer,
 * each with exactly one candidate pointing at the other's port, the same
 * "two roles, one process" idiom tests/native_lobby_state_test.c uses.
 *
 * Every advance is tick-counted (NativeArcadeNetplay_Tick call count), never
 * wall-clock, so every bound below is a small fixed number of calls, not a
 * timeout: this test is not flaky by construction, and it is run at least
 * twice in a row as part of verification.
 *
 * Fixed loopback test ports, in the 48400-48499 band (and 48540-48541 for
 * the race hold service), distinct from every other test file's own bands (tests/native_lobby_state_test.c uses
 * 48300-48399; see tests/native_lockstep_peer_link_test.c for the others).
 * Each socket test uses its own pair; a rematch deliberately reopens the
 * same local port, as a cabinet does.
 */
#define TEST_DORMANT_A_PORT 48400u
#define TEST_DORMANT_B_PORT 48401u

#define TEST_RACE_A_PORT 48402u
#define TEST_RACE_B_PORT 48403u

#define TEST_REMATCH_A_PORT 48404u
#define TEST_REMATCH_B_PORT 48405u

#define TEST_ONE_SIDED_A_PORT 48406u
#define TEST_ONE_SIDED_B_PORT 48407u

#define TEST_REJECT_A_PORT 48408u
#define TEST_REJECT_B_PORT 48409u

#define TEST_STALL_A_PORT 48410u
#define TEST_STALL_B_PORT 48411u

#define TEST_ARM_A_PORT 48412u
#define TEST_ARM_B_PORT 48413u

#define TEST_STAGGER_ENTER_A_PORT 48414u
#define TEST_STAGGER_ENTER_B_PORT 48415u

#define TEST_STAGGER_REMATCH_A_PORT 48416u
#define TEST_STAGGER_REMATCH_B_PORT 48417u

#define TEST_FAULT_A_PORT 48418u
#define TEST_FAULT_B_PORT 48419u

#define TEST_DIVERGE_A_PORT 48420u
#define TEST_DIVERGE_B_PORT 48421u

#define TEST_BLOCKED_A_PORT 48422u
#define TEST_BLOCKED_B_PORT 48423u

#define TEST_BACK_A_PORT 48424u
#define TEST_BACK_B_PORT 48425u

#define TEST_BEGIN_FAIL_A_PORT 48426u
#define TEST_BEGIN_FAIL_B_PORT 48427u

/* Task 4c: the race-time hook on a latched fault and a latched divergence. */
#define TEST_HOOK_FAULT_A_PORT 48428u
#define TEST_HOOK_FAULT_B_PORT 48429u

#define TEST_HOOK_DIVERGE_A_PORT 48430u
#define TEST_HOOK_DIVERGE_B_PORT 48431u

/* MS-7: the select phase. */
#define TEST_SELECT_PICKS_A_PORT 48432u
#define TEST_SELECT_PICKS_B_PORT 48433u

#define TEST_SELECT_IDLE_A_PORT 48434u
#define TEST_SELECT_IDLE_B_PORT 48435u

#define TEST_SELECT_SAME_A_PORT 48436u
#define TEST_SELECT_SAME_B_PORT 48437u

#define TEST_SELECT_SILENCE_A_PORT 48438u
#define TEST_SELECT_SILENCE_B_PORT 48439u

#define TEST_SELECT_LAUNCH_A_PORT 48440u
#define TEST_SELECT_LAUNCH_B_PORT 48441u

#define TEST_SELECT_REMATCH_A_PORT 48442u
#define TEST_SELECT_REMATCH_B_PORT 48443u

#define TEST_SELECT_AUX_A_PORT 48444u
#define TEST_SELECT_AUX_B_PORT 48445u

#define TEST_SELECT_BLOCKED_A_PORT 48446u
#define TEST_SELECT_BLOCKED_B_PORT 48447u

/* MS-7b: review follow-ups. */
#define TEST_SELECT_ASYM_REMATCH_A_PORT 48448u
#define TEST_SELECT_ASYM_REMATCH_B_PORT 48449u

#define TEST_SELECT_NO_START_A_PORT 48450u
#define TEST_SELECT_NO_START_B_PORT 48451u

#define TEST_SELECT_BACK_A_PORT 48452u
#define TEST_SELECT_BACK_B_PORT 48453u

#define TEST_SELECT_LINK_LOST_A_PORT 48454u
#define TEST_SELECT_LINK_LOST_B_PORT 48455u
/* MS-8: the flat select view. */
#define TEST_SELECT_VIEW_A_PORT 48456u
#define TEST_SELECT_VIEW_B_PORT 48457u
/* MS-8b: a pre-race LINK ERROR closes the link. */
#define TEST_SELECT_CLOSE_A_PORT 48458u
#define TEST_SELECT_CLOSE_B_PORT 48459u
/* The local menu event in the view: one adapter against a dead peer port,
 * then a real pair on SELECT. */
#define TEST_MENU_EVENT_A_PORT 48460u
#define TEST_MENU_EVENT_DEAD_PORT 48461u
#define TEST_MENU_EVENT_PAIR_A_PORT 48462u
#define TEST_MENU_EVENT_PAIR_B_PORT 48463u
/* RL-S5: the launch agreement (docs/RACE_LAUNCH_MILESTONE.md). */
#define TEST_LAUNCH_SYM_A_PORT 48464u
#define TEST_LAUNCH_SYM_B_PORT 48465u
#define TEST_LAUNCH_ONE_SIDED_A_PORT 48466u
#define TEST_LAUNCH_ONE_SIDED_B_PORT 48467u
#define TEST_LAUNCH_LOST_A_PORT 48468u
#define TEST_LAUNCH_LOST_B_PORT 48469u
#define TEST_LAUNCH_SELECT_STALE_A_PORT 48470u
#define TEST_LAUNCH_SELECT_STALE_B_PORT 48471u
#define TEST_LAUNCH_EDGE_A_PORT 48472u
#define TEST_LAUNCH_EDGE_B_PORT 48473u
#define TEST_LAUNCH_CLOSE_A_PORT 48474u
#define TEST_LAUNCH_CLOSE_B_PORT 48475u
#define TEST_LAUNCH_LINGER_A_PORT 48476u
#define TEST_LAUNCH_LINGER_B_PORT 48477u
#define TEST_LAUNCH_REORDER_A_PORT 48478u
#define TEST_LAUNCH_REORDER_B_PORT 48479u
#define TEST_LAUNCH_STALE_RESTART_A_PORT 48480u
#define TEST_LAUNCH_STALE_RESTART_B_PORT 48481u
#define TEST_LAUNCH_STALE_RELINK_A_PORT 48482u
#define TEST_LAUNCH_STALE_RELINK_B_PORT 48483u
#define TEST_LAUNCH_STALE_REMATCH_A_PORT 48484u
#define TEST_LAUNCH_STALE_REMATCH_B_PORT 48485u
#define TEST_LAUNCH_STALE_TITLE_A_PORT 48486u
#define TEST_LAUNCH_STALE_TITLE_B_PORT 48487u
#define TEST_LAUNCH_FAULT_LINGER_A_PORT 48488u
#define TEST_LAUNCH_FAULT_LINGER_B_PORT 48489u
/* RL-S6: the local race-failure input. */
#define TEST_LOCAL_FAILURE_A_PORT 48490u
#define TEST_LOCAL_FAILURE_B_PORT 48491u
/* LR-S6 (docs/LOCKSTEP_RACE_MILESTONE.md LR-14): stale bundles after a
 * rematch. */
#define TEST_STALE_DESYNC_A_PORT 48492u
#define TEST_STALE_DESYNC_B_PORT 48493u
#define TEST_STALE_PRE_RACE_A_PORT 48494u
#define TEST_STALE_PRE_RACE_B_PORT 48495u
#define TEST_STALE_LINGER_A_PORT 48496u
#define TEST_STALE_LINGER_B_PORT 48497u
#define TEST_STALE_RESTART_A_PORT 48498u
#define TEST_STALE_RESTART_B_PORT 48499u
/* The race hold service (docs/LOCKSTEP_RACE_MILESTONE.md LR-S9): the
 * 48400-48499 band is full, so these take 48540-48541, outside every other
 * test file's band (the link host uses 48500-48519, the view layout
 * 48520-48539). */
#define TEST_RACE_SERVICE_A_PORT 48540u
#define TEST_RACE_SERVICE_B_PORT 48541u

/* Small, fixed, tick-counted budgets and timings: a real loopback handshake
 * completes in a handful of ticks, well inside every one of them. */
#define ATTEMPT_TICKS_PER_CANDIDATE 20u
#define RETRANSMIT_INTERVAL_TICKS 1u
#define LOBBY_RETRY_PAUSE_TICKS 3u
#define MATCH_FOUND_HOLD_TICKS 3u
#define RESULTS_DWELL_TICKS 2u
#define RESULTS_IDLE_TIMEOUT_TICKS 50u
#define REMATCH_WAIT_TIMEOUT_TICKS 60u
#define OPPONENT_LEFT_NOTICE_TICKS 4u
#define EXIT_HOLD_TICKS 3u
#define SELECT_RESULT_HOLD_TICKS 8u
#define LAUNCH_TIMEOUT_TICKS 60u

/* Select session timings: every scripted pick below finishes well inside
 * one item; an idle select auto-picks after three items. */
#define SELECT_ITEM_TICKS 30u
#define SELECT_PEER_SILENCE_TICKS 90u

/* The fixture's cursors: slot characters 0 (CAB1) and 1 (CAB2); its trackID
 * is not a table track, so the cursor starts on the first, CRASH_COVE (3);
 * lapCount 3. */
#define FIXTURE_TRACK_CURSOR 3u
#define FIXTURE_LAP_CURSOR 3u

/* Bounds every outer test-driving loop; generous, not tuned. */
#define DRIVE_BUDGET 2000u
/* Bounds every non-blocking receive spin that waits for a loopback datagram
 * already sent (a count of calls, not a timeout). */
#define RECEIVE_SPIN_BUDGET 2000000u
/* How long a rejected lobby is watched for an automatic restart. */
#define REJECT_WATCH_TICKS 200u

#define BTN_CROSS NATIVE_ARCADE_MENU_BUTTON_CROSS
#define BTN_DOWN NATIVE_ARCADE_MENU_BUTTON_DOWN
#define BTN_UP NATIVE_ARCADE_MENU_BUTTON_UP
#define BTN_TRIANGLE NATIVE_ARCADE_MENU_BUTTON_TRIANGLE

#define ACT_NONE NATIVE_ARCADE_FLOW_ACTION_NONE
#define ACT_BEGIN_LOBBY NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY
#define ACT_RESTART_LOBBY NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY
#define ACT_START_RACE NATIVE_ARCADE_FLOW_ACTION_START_RACE
#define ACT_BEGIN_REMATCH NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH
#define ACT_CLOSE_LINK NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK
#define ACT_RETURN_TO_TITLE NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE
#define ACT_BEGIN_SELECT NATIVE_ARCADE_FLOW_ACTION_BEGIN_SELECT
#define ACT_RELINK NATIVE_ARCADE_FLOW_ACTION_RELINK

/* Adapters are large (each owns a lobby, peer link, and session), so they
 * live in static storage rather than on the stack. */
static struct NativeArcadeNetplay g_a;
static struct NativeArcadeNetplay g_b;
static struct NativeArcadeNetplay g_probe;
static struct NativeArcadeNetplay g_sentinel;

static void SmallTimings(struct NativeArcadeFlowTimings *timings)
{
	timings->lobbyRetryPauseTicks = LOBBY_RETRY_PAUSE_TICKS;
	timings->matchFoundHoldTicks = MATCH_FOUND_HOLD_TICKS;
	timings->resultsDwellTicks = RESULTS_DWELL_TICKS;
	timings->resultsIdleTimeoutTicks = RESULTS_IDLE_TIMEOUT_TICKS;
	timings->rematchWaitTimeoutTicks = REMATCH_WAIT_TIMEOUT_TICKS;
	timings->opponentLeftNoticeTicks = OPPONENT_LEFT_NOTICE_TICKS;
	timings->exitHoldTicks = EXIT_HOLD_TICKS;
	timings->selectResultHoldTicks = SELECT_RESULT_HOLD_TICKS;
	timings->launchTimeoutTicks = LAUNCH_TIMEOUT_TICKS;
}

static void SmallSelectTimings(struct NativeMatchSelectTimings *timings)
{
	timings->characterTicks = SELECT_ITEM_TICKS;
	timings->trackTicks = SELECT_ITEM_TICKS;
	timings->lapTicks = SELECT_ITEM_TICKS;
	timings->peerSilenceTicks = SELECT_PEER_SILENCE_TICKS;
}

static int MakeConfig(struct NativeArcadeNetplayConfig *config, const struct NativeMatchConfigV1 *fixture, uint8_t role,
	uint32_t localPort, uint32_t peerPort)
{
	NativeArcadeNetplay_DefaultConfig(config);
	config->fixture = *fixture;
	config->localRole = role;
	config->localPort = (uint16_t)localPort;
	if (!NativeUdpTransport_MakeAddress(&config->candidates[0], "127.0.0.1", (uint16_t)peerPort))
	{
		return 0;
	}
	config->candidateCount = 1u;
	config->attemptTicksPerCandidate = ATTEMPT_TICKS_PER_CANDIDATE;
	config->retransmitIntervalTicks = RETRANSMIT_INTERVAL_TICKS;
	SmallTimings(&config->timings);
	SmallSelectTimings(&config->selectTimings);
	return 1;
}

/* A is CAB1 on portA pointing at portB; B is CAB2 on portB pointing at
 * portA; both with the given lobby cadence. */
static int InitPairWithCadence(const struct NativeMatchConfigV1 *fixtureA, const struct NativeMatchConfigV1 *fixtureB,
	uint32_t portA, uint32_t portB, uint32_t attemptTicksPerCandidate, uint32_t retransmitIntervalTicks)
{
	struct NativeArcadeNetplayConfig config;

	if (!MakeConfig(&config, fixtureA, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, portA, portB))
	{
		return 0;
	}
	config.attemptTicksPerCandidate = attemptTicksPerCandidate;
	config.retransmitIntervalTicks = retransmitIntervalTicks;
	if (!NativeArcadeNetplay_Init(&g_a, &config))
	{
		return 0;
	}
	if (!MakeConfig(&config, fixtureB, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, portB, portA))
	{
		return 0;
	}
	config.attemptTicksPerCandidate = attemptTicksPerCandidate;
	config.retransmitIntervalTicks = retransmitIntervalTicks;
	if (!NativeArcadeNetplay_Init(&g_b, &config))
	{
		return 0;
	}
	return 1;
}

/* The small test cadence. */
static int InitPair(const struct NativeMatchConfigV1 *fixtureA, const struct NativeMatchConfigV1 *fixtureB, uint32_t portA,
	uint32_t portB)
{
	return InitPairWithCadence(fixtureA, fixtureB, portA, portB, ATTEMPT_TICKS_PER_CANDIDATE, RETRANSMIT_INTERVAL_TICKS);
}

/* The production cadence from NativeArcadeNetplay_DefaultConfig (150-tick
 * attempt budget, HELLO every tick); only the flow timings stay small. */
static int InitPairDefaultCadence(const struct NativeMatchConfigV1 *fixture, uint32_t portA, uint32_t portB)
{
	struct NativeArcadeNetplayConfig defaults;

	NativeArcadeNetplay_DefaultConfig(&defaults);
	return InitPairWithCadence(fixture, fixture, portA, portB, defaults.attemptTicksPerCandidate,
		defaults.retransmitIntervalTicks);
}

static uint32_t ScreenOf(const struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeNetplayView view;

	(void)NativeArcadeNetplay_GetView(netplay, &view);
	return view.screen;
}

static uint32_t LobbyStatusOf(const struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeNetplayView view;

	(void)NativeArcadeNetplay_GetView(netplay, &view);
	return view.lobbyStatus;
}

static uint32_t EndReasonOf(const struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeNetplayView view;

	(void)NativeArcadeNetplay_GetView(netplay, &view);
	return view.endReason;
}

/* The view's localMenuEvent; 0xFF when GetView fails. */
static uint32_t MenuEventOf(const struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeNetplayView view;

	memset(&view, 0xA5, sizeof(view));
	if (!NativeArcadeNetplay_GetView(netplay, &view))
	{
		return 0xFFu;
	}
	return view.localMenuEvent;
}

/* One tick of both adapters, A first, recording both actions. */
static void TickBoth(uint32_t heldA, uint32_t heldB, uint8_t raceFinished, enum NativeArcadeFlowAction *actionA,
	enum NativeArcadeFlowAction *actionB)
{
	*actionA = NativeArcadeNetplay_Tick(&g_a, heldA, raceFinished);
	*actionB = NativeArcadeNetplay_Tick(&g_b, heldB, raceFinished);
}

/* Ticks both with no buttons until each has returned target at least once.
 * Fails on a CLOSE_LINK or RETURN_TO_TITLE, which no happy path produces. */
static int DriveBothUntil(enum NativeArcadeFlowAction target)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	int seenA = 0;
	int seenB = 0;
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && !(seenA && seenB); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		if ((actionA == ACT_CLOSE_LINK) || (actionA == ACT_RETURN_TO_TITLE) || (actionB == ACT_CLOSE_LINK) ||
			(actionB == ACT_RETURN_TO_TITLE))
		{
			return 0;
		}
		seenA = seenA || (actionA == target);
		seenB = seenB || (actionB == target);
	}
	return seenA && seenB;
}

/* Enter and drive two already initialized adapters into RACING. */
static int EnterAndRaceInitialized(void)
{
	if ((NativeArcadeNetplay_Enter(&g_a) != ACT_BEGIN_LOBBY) || (NativeArcadeNetplay_Enter(&g_b) != ACT_BEGIN_LOBBY))
	{
		return 0;
	}
	if (!DriveBothUntil(ACT_START_RACE))
	{
		return 0;
	}
	return (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING) && (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
}

/* Init, Enter, and drive both adapters into RACING on the shared fixture. */
static int EnterAndRace(const struct NativeMatchConfigV1 *fixture, uint32_t portA, uint32_t portB)
{
	return InitPair(fixture, fixture, portA, portB) && EnterAndRaceInitialized();
}

/* Finish the race on both and tick through the results dwell with no
 * buttons, which also arms the menu input. */
static int FinishAndDwell(void)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	struct NativeArcadeNetplayView view;
	uint32_t tick;

	TickBoth(0u, 0u, 1u, &actionA, &actionB);
	if ((actionA != ACT_NONE) || (actionB != ACT_NONE))
	{
		return 0;
	}
	if ((ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_RESULTS) || (ScreenOf(&g_b) != NATIVE_ARCADE_FLOW_SCREEN_RESULTS) ||
		(EndReasonOf(&g_a) != NATIVE_ARCADE_FLOW_END_FINISHED) || (EndReasonOf(&g_b) != NATIVE_ARCADE_FLOW_END_FINISHED))
	{
		return 0;
	}
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		if ((actionA != ACT_NONE) || (actionB != ACT_NONE))
		{
			return 0;
		}
	}
	(void)NativeArcadeNetplay_GetView(&g_a, &view);
	if ((view.menuArmed != 1u) || (view.selectedRow != NATIVE_ARCADE_FLOW_ROW_REMATCH))
	{
		return 0;
	}
	(void)NativeArcadeNetplay_GetView(&g_b, &view);
	return (view.menuArmed == 1u) && (view.selectedRow == NATIVE_ARCADE_FLOW_ROW_REMATCH);
}

static void ShutdownBoth(void)
{
	NativeArcadeNetplay_Shutdown(&g_a);
	NativeArcadeNetplay_Shutdown(&g_b);
}

/* Init must leave *netplay byte-identical on every rejection. */
static int InitRejectsUntouched(const struct NativeArcadeNetplayConfig *config)
{
	memset(&g_probe, 0xA5, sizeof(g_probe));
	memcpy(&g_sentinel, &g_probe, sizeof(g_probe));
	if (NativeArcadeNetplay_Init(&g_probe, config) != 0)
	{
		return 0;
	}
	return memcmp(&g_probe, &g_sentinel, sizeof(g_probe)) == 0;
}

/* The config both cabinets must race on after a two-human select on base
 * with these locked picks (index 0 CAB1, 1 CAB2) and the adapters' nonces
 * (entropy 0, the given select serial), computed here from the rules module
 * directly. */
static int ExpectedResolvedConfig(const struct NativeMatchConfigV1 *base, const uint8_t characters[2],
	const uint8_t tracks[2], const uint8_t laps[2], uint32_t selectSerial, struct NativeMatchSelectOutcome *outcomeOut,
	struct NativeMatchConfigV1 *configOut)
{
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;
	uint32_t h;

	memset(choices, 0, sizeof(choices));
	for (h = 0; h < 2u; h++)
	{
		choices[h].characterID = characters[h];
		choices[h].trackID = tracks[h];
		choices[h].lapCount = laps[h];
		if (!NativeArcadeNetplay_DeriveSelectNonce(0u, (uint8_t)(NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN + h), selectSerial,
				&choices[h].nonce))
		{
			return 0;
		}
	}
	memset(&outcome, 0, sizeof(outcome));
	if (!NativeMatchSelect_Resolve(base, 2u, choices, &outcome) || !NativeMatchSelect_BuildConfig(base, &outcome, configOut))
	{
		return 0;
	}
	if (outcomeOut != NULL)
	{
		*outcomeOut = outcome;
	}
	return 1;
}

/* The bot slots, in slot order, hold the first retail 2P AI set that holds
 * neither human character (the LOAD_Robots2P rule). */
static int BotsAreRetailSet(const struct NativeMatchConfigV1 *config)
{
	uint8_t slotA = 0u;
	uint8_t slotB = 0u;
	uint8_t humanA;
	uint8_t humanB;
	uint32_t set;
	uint32_t racer;
	uint32_t slot;
	uint32_t bot = 0u;
	uint32_t chosen = NATIVE_MATCH_SELECT_AI_SET_COUNT;

	if (!NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &slotA) ||
		!NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slotB))
	{
		return 0;
	}
	humanA = config->slots[slotA].characterID;
	humanB = config->slots[slotB].characterID;
	for (set = 0; (set < NATIVE_MATCH_SELECT_AI_SET_COUNT) && (chosen == NATIVE_MATCH_SELECT_AI_SET_COUNT); set++)
	{
		int clash = 0;

		for (racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
		{
			uint8_t id = NativeMatchSelect_AiSetRacer(set, racer);

			clash = clash || (id == humanA) || (id == humanB);
		}
		if (!clash)
		{
			chosen = set;
		}
	}
	if (chosen == NATIVE_MATCH_SELECT_AI_SET_COUNT)
	{
		return 0;
	}
	for (slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		if (config->slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			if ((bot >= NATIVE_MATCH_SELECT_AI_SET_RACERS) ||
				(config->slots[slot].characterID != NativeMatchSelect_AiSetRacer(chosen, bot)))
			{
				return 0;
			}
			bot += 1u;
		}
	}
	return bot == NATIVE_MATCH_SELECT_AI_SET_RACERS;
}

/* Ticks both with no buttons until each has returned BEGIN_SELECT, then
 * one more released tick so both menus are armed on SELECT. */
static int DriveBothIntoSelect(void)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;

	if (!DriveBothUntil(ACT_BEGIN_SELECT))
	{
		return 0;
	}
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	return (actionA == ACT_NONE) && (actionB == ACT_NONE) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT) &&
		(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT) && (NativeArcadeNetplay_Select(&g_a) != NULL) &&
		(NativeArcadeNetplay_Select(&g_b) != NULL);
}

/* Parallel scripted presses, one per step for each cabinet (0: none): each
 * step is a held tick and a released tick, and every action must be NONE. */
static int PressScripts(const uint32_t *scriptA, const uint32_t *scriptB, uint32_t steps)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t step;

	for (step = 0; step < steps; step++)
	{
		TickBoth(scriptA[step], scriptB[step], 0u, &actionA, &actionB);
		if ((actionA != ACT_NONE) || (actionB != ACT_NONE))
		{
			return 0;
		}
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		if ((actionA != ACT_NONE) || (actionB != ACT_NONE))
		{
			return 0;
		}
	}
	return 1;
}

/* One side's per-tick invariants while driving to a race: RELINK only on
 * SELECT_RESULT, exactly one RELINK before START_RACE, no agreed config
 * before RACING, and the select session visible exactly on the select
 * screens. */
static int CheckSelectPath(const struct NativeArcadeNetplay *netplay, enum NativeArcadeFlowAction action, uint32_t *relinks,
	int *started)
{
	uint32_t screen = ScreenOf(netplay);

	if (*started)
	{
		return 1;
	}
	if (action == ACT_RELINK)
	{
		if (screen != NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT)
		{
			return 0;
		}
		*relinks += 1u;
	}
	if (action == ACT_START_RACE)
	{
		*started = 1;
		return (*relinks == 1u) && (screen == NATIVE_ARCADE_FLOW_SCREEN_RACING) &&
			(NativeArcadeNetplay_AgreedConfig(netplay) != NULL) && (NativeArcadeNetplay_Select(netplay) == NULL);
	}
	if (NativeArcadeNetplay_AgreedConfig(netplay) != NULL)
	{
		return 0;
	}
	if ((screen == NATIVE_ARCADE_FLOW_SCREEN_SELECT) || (screen == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT))
	{
		return NativeArcadeNetplay_Select(netplay) != NULL;
	}
	return NativeArcadeNetplay_Select(netplay) == NULL;
}

/* From the select screens: ticks both with no buttons until each has
 * returned START_RACE, checking CheckSelectPath on every tick. */
static int DriveBothToRaceChecked(void)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t relinksA = 0u;
	uint32_t relinksB = 0u;
	int startedA = 0;
	int startedB = 0;
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && !(startedA && startedB); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		if ((actionA == ACT_CLOSE_LINK) || (actionA == ACT_RETURN_TO_TITLE) || (actionB == ACT_CLOSE_LINK) ||
			(actionB == ACT_RETURN_TO_TITLE))
		{
			return 0;
		}
		if (!CheckSelectPath(&g_a, actionA, &relinksA, &startedA) || !CheckSelectPath(&g_b, actionB, &relinksB, &startedB))
		{
			return 0;
		}
	}
	return startedA && startedB;
}

/* 1. Pure: defaults, Init validation, cause mapping, rematch seed. */
static int TestPure(void)
{
	struct NativeArcadeNetplayConfig config;
	struct NativeArcadeNetplayConfig base;
	struct NativeArcadeNetplayConfig bad;
	struct NativeArcadeFlowTimings timings;
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 zeroFixture;
	struct NativeMatchConfigV1 oneCab;
	struct NativeMatchConfigV1 next;
	struct NativeMatchConfigV1 other;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint64_t seed1 = 0u;
	uint64_t seed2 = 0u;
	uint64_t expected = 0u;
	uint64_t untouched;
	uint32_t i;

	/* DefaultConfig. */
	NativeArcadeNetplay_DefaultConfig(NULL);
	memset(&config, 0x5A, sizeof(config));
	NativeArcadeNetplay_DefaultConfig(&config);
	CHECK(config.inputDelay == 2u);
	CHECK(config.attemptTicksPerCandidate == 150u);
	/* Every tick: the peer-link Retransmit-before-Poll contract (UX-5). */
	CHECK(config.retransmitIntervalTicks == 1u);
	CHECK(config.stallTimeoutTicks == 90u);
	CHECK(config.inputDelay == NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY);
	CHECK(config.attemptTicksPerCandidate == NATIVE_ARCADE_NETPLAY_DEFAULT_ATTEMPT_TICKS_PER_CANDIDATE);
	CHECK(config.retransmitIntervalTicks == NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS);
	CHECK(config.stallTimeoutTicks == NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS);
	CHECK(config.localRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(config.candidateCount == 0u);
	CHECK(config.localPort == 0u);
	CHECK(config.reserved == 0u);
	NativeArcadeFlow_DefaultTimings(&timings);
	CHECK(memcmp(&config.timings, &timings, sizeof(timings)) == 0);
	/* The select session's defaults: 600 ticks per item (OD-1), 90 of
	 * silence (SEL-9); no entropy. */
	CHECK(config.selectTimings.characterTicks == NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS);
	CHECK(config.selectTimings.trackTicks == NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS);
	CHECK(config.selectTimings.lapTicks == NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS);
	CHECK(config.selectTimings.peerSilenceTicks == NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS);
	CHECK(config.selectTimings.characterTicks == 600u);
	CHECK(config.selectTimings.peerSilenceTicks == 90u);
	CHECK(config.selectEntropy == 0u);
	memset(&zeroFixture, 0, sizeof(zeroFixture));
	CHECK(memcmp(&config.fixture, &zeroFixture, sizeof(zeroFixture)) == 0);
	for (i = 0; i < NATIVE_LOBBY_STATE_MAX_CANDIDATES; i++)
	{
		CHECK(config.candidates[i].ipv4 == 0u);
		CHECK(config.candidates[i].port == 0u);
	}
	/* The zero fixture DefaultConfig leaves is not a valid config: the
	 * caller must fill it. */
	CHECK(InitRejectsUntouched(&config));

	/* A valid base. */
	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(MakeConfig(&base, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_DORMANT_A_PORT, TEST_DORMANT_B_PORT));

	/* NULL arguments. */
	CHECK(NativeArcadeNetplay_Init(NULL, &base) == 0);
	CHECK(InitRejectsUntouched(NULL));

	/* Invalid fixture. */
	bad = base;
	bad.fixture.lapCount = 0u;
	CHECK(InitRejectsUntouched(&bad));

	/* Bad roles. */
	bad = base;
	bad.localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT;
	CHECK(InitRejectsUntouched(&bad));
	bad.localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_INACTIVE;
	CHECK(InitRejectsUntouched(&bad));
	bad.localRole = 7u;
	CHECK(InitRejectsUntouched(&bad));

	/* CAB2 with a one-cab fixture: the role is absent from the fixture. */
	NativeMatchConfigV1_InitArcadeOneCab(&oneCab);
	oneCab.trackID = fixture.trackID;
	oneCab.lapCount = fixture.lapCount;
	oneCab.tickRateNumerator = fixture.tickRateNumerator;
	oneCab.tickRateDenominator = fixture.tickRateDenominator;
	oneCab.masterSeed = fixture.masterSeed;
	memcpy(oneCab.buildIdentity, fixture.buildIdentity, sizeof(oneCab.buildIdentity));
	memcpy(oneCab.contentIdentity, fixture.contentIdentity, sizeof(oneCab.contentIdentity));
	memcpy(oneCab.botRulesDigest, fixture.botRulesDigest, sizeof(oneCab.botRulesDigest));
	CHECK(NativeMatchConfigV1_Validate(&oneCab));
	bad = base;
	bad.fixture = oneCab;
	bad.localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	CHECK(InitRejectsUntouched(&bad));
	/* The same one-cab fixture is fine for CAB1. */
	bad.localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);

	/* Too many candidates, or none. */
	bad = base;
	bad.candidateCount = NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u;
	CHECK(InitRejectsUntouched(&bad));
	bad.candidateCount = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad.candidateCount = NATIVE_LOBBY_STATE_MAX_CANDIDATES;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);

	/* Zero local port. */
	bad = base;
	bad.localPort = 0u;
	CHECK(InitRejectsUntouched(&bad));

	/* Zero attempt budget. */
	bad = base;
	bad.attemptTicksPerCandidate = 0u;
	CHECK(InitRejectsUntouched(&bad));

	/* Retransmit interval: only 1 honours the peer-link Retransmit-before-Poll
	 * contract (UX-5), so 0, 2, and the old 15 are all rejected. */
	bad = base;
	bad.retransmitIntervalTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad.retransmitIntervalTicks = 2u;
	CHECK(InitRejectsUntouched(&bad));
	bad.retransmitIntervalTicks = 15u;
	CHECK(InitRejectsUntouched(&bad));
	bad.retransmitIntervalTicks = UINT32_MAX;
	CHECK(InitRejectsUntouched(&bad));
	bad.retransmitIntervalTicks = 1u;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);

	/* Input delay outside [1, 6]. */
	bad = base;
	bad.inputDelay = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad.inputDelay = 7u;
	CHECK(InitRejectsUntouched(&bad));
	bad.inputDelay = (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);
	bad.inputDelay = (uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);

	/* Stall timeout outside [30, 600]. */
	bad = base;
	bad.stallTimeoutTicks = 29u;
	CHECK(InitRejectsUntouched(&bad));
	bad.stallTimeoutTicks = 601u;
	CHECK(InitRejectsUntouched(&bad));
	bad.stallTimeoutTicks = 30u;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);
	bad.stallTimeoutTicks = 600u;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);

	/* Timings the flow rejects. */
	bad = base;
	bad.timings.exitHoldTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.timings.resultsIdleTimeoutTicks = bad.timings.resultsDwellTicks;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.timings.selectResultHoldTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.timings.launchTimeoutTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));

	/* Select timings with any zero field. */
	bad = base;
	bad.selectTimings.characterTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.selectTimings.trackTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.selectTimings.lapTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.selectTimings.peerSilenceTicks = 0u;
	CHECK(InitRejectsUntouched(&bad));
	bad = base;
	bad.selectTimings.characterTicks = 1u;
	bad.selectTimings.trackTicks = 1u;
	bad.selectTimings.lapTicks = 1u;
	bad.selectTimings.peerSilenceTicks = 1u;
	bad.selectEntropy = UINT64_MAX;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);
	CHECK(g_probe.config.selectEntropy == UINT64_MAX);

	/* Success: stored config, fixture proposal, dormant flow, local slot. */
	memset(&g_probe, 0xA5, sizeof(g_probe));
	CHECK(NativeArcadeNetplay_Init(&g_probe, &base) == 1);
	CHECK(g_probe.initialized == 1u);
	CHECK(g_probe.localSlot == 0u);
	CHECK(g_probe.lobbyBegun == 0u);
	CHECK(g_probe.raceArmed == 0u);
	CHECK(g_probe.rematchBlocked == 0u);
	CHECK(g_probe.selectActive == 0u);
	CHECK(g_probe.relinked == 0u);
	CHECK(g_probe.relinkBlocked == 0u);
	CHECK(g_probe.selectSerial == 0u);
	CHECK(g_probe.outcomeValid == 0u);
	CHECK(g_probe.raceConfigValid == 0u);
	CHECK(g_probe.lobbyReadySeen == 0u);
	CHECK(g_probe.lastReadyValid == 0u);
	CHECK(NativeArcadeNetplay_Select(&g_probe) == NULL);
	CHECK(g_probe.matchCount == 0u);
	CHECK(g_probe.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(memcmp(&g_probe.config, &base, sizeof(base)) == 0);
	CHECK(memcmp(&g_probe.currentConfig, &fixture, sizeof(fixture)) == 0);
	CHECK(ScreenOf(&g_probe) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeNetplay_Link(&g_probe) == NULL);
	bad = base;
	bad.localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	CHECK(NativeArcadeNetplay_Init(&g_probe, &bad) == 1);
	CHECK(g_probe.localSlot == 1u);

	/* EndReasonForCause. */
	CHECK(NativeArcadeNetplay_EndReasonForCause(NATIVE_LOCKSTEP_MATCH_OUTCOME_NONE) == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(NativeArcadeNetplay_EndReasonForCause(NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED) ==
		(uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(NativeArcadeNetplay_EndReasonForCause(NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED) ==
		(uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(NativeArcadeNetplay_EndReasonForCause(NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT) ==
		(uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	CHECK(NativeArcadeNetplay_EndReasonForCause(99u) == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(NativeArcadeNetplay_EndReasonForCause(UINT32_MAX) == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);

	/* DeriveRematchSeed: deterministic, nonzero, new, and exactly the first
	 * little-endian 8 bytes of the config digest here. */
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &seed1) == 1);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &seed2) == 1);
	CHECK(seed1 == seed2);
	CHECK(seed1 != 0u);
	CHECK(seed1 != fixture.masterSeed);
	CHECK(NativeMatchConfigV1_Digest(&fixture, digest) == 1);
	for (i = 0; i < 8u; i++)
	{
		expected |= (uint64_t)digest[i] << (8u * i);
	}
	CHECK(seed1 == expected);

	/* NULL-safe and untouched on failure, including a digest failure. */
	untouched = UINT64_C(0x1122334455667788);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(NULL, &untouched) == 0);
	CHECK(untouched == UINT64_C(0x1122334455667788));
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, NULL) == 0);
	other = fixture;
	other.lapCount = 0u;
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&other, &untouched) == 0);
	CHECK(untouched == UINT64_C(0x1122334455667788));

	/* The rematch builder accepts it, and the chain keeps moving. */
	CHECK(NativeLockstepRematch_BuildConfig(&fixture, seed1, &next) == 1);
	CHECK(next.masterSeed == seed1);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&next, &seed2) == 1);
	CHECK(seed2 != seed1);
	CHECK(seed2 != 0u);

	/* A different fixture derives a different seed. */
	other = fixture;
	other.trackID ^= UINT32_C(0xffffffff);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&other, &seed2) == 1);
	CHECK(seed2 != seed1);
	return 0;
}

/* 2. Dormancy: an initialized adapter that was never entered does nothing. */
static int TestDormancy(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayConfig config;
	struct NativeArcadeNetplayView view;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(MakeConfig(&config, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_DORMANT_A_PORT, TEST_DORMANT_B_PORT));
	CHECK(NativeArcadeNetplay_Init(&g_a, &config) == 1);
	memcpy(&g_sentinel, &g_a, sizeof(g_a));

	CHECK(NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 1u) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(memcmp(&g_a, &g_sentinel, sizeof(g_a)) == 0);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_Select(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_Select(NULL) == NULL);

	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, 0u);
	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_OK, 0u);
	CHECK(memcmp(&g_a, &g_sentinel, sizeof(g_a)) == 0);

	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(view.lobbyStatus == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_WAITING);
	CHECK(view.endReason == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(view.matchCount == 0u);
	CHECK(view.localRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(NativeArcadeNetplay_GetView(NULL, &view) == 0);
	CHECK(NativeArcadeNetplay_GetView(&g_a, NULL) == 0);

	/* NULL-safe entry points. */
	CHECK(NativeArcadeNetplay_Tick(NULL, 0u, 0u) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Enter(NULL) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Link(NULL) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(NULL) == NULL);
	NativeArcadeNetplay_OnTakeResult(NULL, NATIVE_LOCKSTEP_SESSION_STALL, 0u);

	/* A zero struct is uninitialized: inert, and Shutdown is safe twice and
	 * leaves it all-zero. */
	memset(&g_b, 0, sizeof(g_b));
	memset(&g_sentinel, 0, sizeof(g_sentinel));
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Tick(&g_b, BTN_CROSS, 0u) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	NativeArcadeNetplay_Shutdown(&g_b);
	NativeArcadeNetplay_Shutdown(&g_b);
	NativeArcadeNetplay_Shutdown(NULL);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	CHECK(memcmp(&g_b, &g_sentinel, sizeof(g_b)) == 0);

	/* Shutdown twice on the dormant adapter, which stays dormant. */
	NativeArcadeNetplay_Shutdown(&g_a);
	NativeArcadeNetplay_Shutdown(&g_a);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	return 0;
}

/* 3. Lobby to race: the lobby agrees on the fixture, an idle select
 * resolves it, and the race runs on the resolved config. */
static int TestLobbyToRace(void)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeArcadeNetplayView view;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &expected));
	CHECK(InitPair(&fixture, &fixture, TEST_RACE_A_PORT, TEST_RACE_B_PORT));
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);

	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	/* Enter only works from OFF. */
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(NativeArcadeNetplay_Link(&g_a) != NULL);
	CHECK(NativeArcadeNetplay_Link(&g_b) != NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);

	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_b)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK(memcmp(agreedB, &expected, sizeof(expected)) == 0);
	CHECK(agreedA->masterSeed != fixture.masterSeed);

	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.matchCount == 1u);
	CHECK(view.localRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(view.endReason == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(NativeArcadeNetplay_GetView(&g_b, &view) == 1);
	CHECK(view.matchCount == 1u);
	CHECK(view.localRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	CHECK(g_a.raceArmed == 1u);
	CHECK(g_b.raceArmed == 1u);

	/* Shutdown closes everything and returns to OFF. */
	ShutdownBoth();
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	ShutdownBoth();
	return 0;
}

/* 4. Both choose REMATCH: a new link on the seed derived from the finished
 * match's config, a new select on it (OD-3), and a new race. */
static int TestBothRematch(void)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 first;
	struct NativeMatchConfigV1 rematchBase;
	struct NativeMatchConfigV1 expected;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	uint64_t expectedSeed = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_REMATCH_A_PORT, TEST_REMATCH_B_PORT));
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);
	first = *NativeArcadeNetplay_AgreedConfig(&g_a);
	CHECK(FinishAndDwell());
	/* RESULTS after a finished race still returns exactly the raced config,
	 * on both sides; it is also the last READY proposal (the relink's). */
	CHECK(g_a.raceConfigValid == 1u);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_b) != NULL);
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &first, sizeof(first)) == 0);
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_b), &first, sizeof(first)) == 0);
	CHECK(g_a.lastReadyValid == 1u);
	CHECK(memcmp(&g_a.lastReadyConfig, &first, sizeof(first)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &first, sizeof(first)) == 0);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&first, &expectedSeed) == 1);

	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(g_a.raceConfigValid == 0u);
	/* A brand-new link is handshaking on the rematch config, which carries
	 * the derived seed. */
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_b)) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(g_a.currentConfig.masterSeed == expectedSeed);
	CHECK(memcmp(&g_a.currentConfig, &g_b.currentConfig, sizeof(g_a.currentConfig)) == 0);
	rematchBase = g_a.currentConfig;
	CHECK(ExpectedResolvedConfig(&rematchBase, characters, tracks, laps, 2u, NULL, &expected));

	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);

	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	/* The rematch select resolved its base: a new seed again. */
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK(agreedA->masterSeed != expectedSeed);
	CHECK(agreedA->masterSeed != first.masterSeed);
	CHECK(agreedA->masterSeed != fixture.masterSeed);
	CHECK(agreedA->trackID == first.trackID);

	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.matchCount == 2u);
	CHECK(NativeArcadeNetplay_GetView(&g_b, &view) == 1);
	CHECK(view.matchCount == 2u);

	ShutdownBoth();
	return 0;
}

/* 5. One-sided rematch: A waits, B exits, A shows OPPONENT LEFT. */
static int TestOneSidedRematch(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;
	uint32_t ticksToCloseA = 0u;
	int closedA = 0;
	int titleA = 0;
	int titleB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_ONE_SIDED_A_PORT, TEST_ONE_SIDED_B_PORT));
	CHECK(FinishAndDwell());

	/* A confirms REMATCH; B moves the focus to EXIT. */
	TickBoth(BTN_CROSS, BTN_DOWN, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(NativeArcadeNetplay_GetView(&g_b, &view) == 1);
	CHECK(view.selectedRow == NATIVE_ARCADE_FLOW_ROW_EXIT);

	/* B confirms EXIT and closes its link. */
	TickBoth(0u, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_CLOSE_LINK);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	/* B left by choice: the race's own FINISHED reason is kept, never
	 * OPPONENT_LEFT. */
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	ticksToCloseA = 1u;

	for (tick = 0; (tick < DRIVE_BUDGET) && !(titleA && titleB); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		/* Nobody answers the rematch. */
		CHECK(actionA != ACT_START_RACE);
		CHECK(ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
		if (!closedA)
		{
			ticksToCloseA += 1u;
		}
		if (actionA == ACT_CLOSE_LINK)
		{
			CHECK(!closedA);
			closedA = 1;
			CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
			CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT);
			CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
		}
		if (actionA == ACT_RETURN_TO_TITLE)
		{
			CHECK(closedA);
			titleA = 1;
		}
		if (actionB == ACT_RETURN_TO_TITLE)
		{
			titleB = 1;
		}
	}
	CHECK(closedA);
	CHECK(titleA);
	CHECK(titleB);
	/* A gave up exactly when the rematch wait ran out. */
	CHECK(ticksToCloseA == REMATCH_WAIT_TIMEOUT_TICKS);

	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	/* OFF is dormant again. */
	CHECK(NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 0u) == ACT_NONE);

	ShutdownBoth();
	return 0;
}

/* 6. Rejection: never auto-restarted; CONFIRM restarts, TRIANGLE leaves. */
static int TestRejection(void)
{
	struct NativeMatchConfigV1 fixtureA;
	struct NativeMatchConfigV1 fixtureB;
	struct NativeArcadeNetplay *rejected;
	struct NativeArcadeNetplay *other;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction actionRejected;
	uint32_t tick;
	int found = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixtureA);
	fixtureB = fixtureA;
	fixtureB.trackID = fixtureA.trackID ^ UINT32_C(0xffffffff);
	CHECK(InitPair(&fixtureA, &fixtureB, TEST_REJECT_A_PORT, TEST_REJECT_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);

	for (tick = 0; (tick < DRIVE_BUDGET) && !found; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		found = (LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED) ||
			(LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED);
	}
	CHECK(found);
	rejected = (LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED) ? &g_a : &g_b;
	other = (rejected == &g_a) ? &g_b : &g_a;
	CHECK(ScreenOf(rejected) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);

	/* No automatic restart of a rejection, however long it is shown. */
	for (tick = 0; tick < REJECT_WATCH_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		actionRejected = (rejected == &g_a) ? actionA : actionB;
		CHECK(actionRejected == ACT_NONE);
		CHECK(ScreenOf(rejected) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
		CHECK(LobbyStatusOf(rejected) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED);
		CHECK(ScreenOf(other) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	}

	/* CONFIRM (the buttons have been released throughout) retries. */
	CHECK(NativeArcadeNetplay_Tick(rejected, BTN_CROSS, 0u) == ACT_RESTART_LOBBY);
	CHECK(ScreenOf(rejected) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(NativeArcadeNetplay_Link(rejected) != NULL);
	(void)NativeArcadeNetplay_Tick(other, 0u, 0u);

	/* Release, then TRIANGLE backs out and closes the link. */
	CHECK(NativeArcadeNetplay_Tick(rejected, 0u, 0u) == ACT_NONE);
	(void)NativeArcadeNetplay_Tick(other, 0u, 0u);
	CHECK(NativeArcadeNetplay_Tick(rejected, BTN_TRIANGLE, 0u) == ACT_CLOSE_LINK);
	CHECK(ScreenOf(rejected) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	CHECK(EndReasonOf(rejected) == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(NativeArcadeNetplay_Link(rejected) == NULL);

	ShutdownBoth();
	return 0;
}

/* 7. Stall timeout through the race-time hook. */
static int TestStallTimeout(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeLockstepMatchOutcomeTracker outcomeBefore;
	const struct NativeLockstepMatchOutcomeReport *report;
	uint8_t slotB = 0u;
	uint32_t i;
	const uint32_t frame = 3u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slotB));
	CHECK(EnterAndRace(&fixture, TEST_STALL_A_PORT, TEST_STALL_B_PORT));
	CHECK(slotB != g_a.localSlot);

	/* 89 stalls: no failure yet. */
	for (i = 0; i < (NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS - 1u); i++)
	{
		NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, frame);
		CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	}
	CHECK(g_a.outcome.consecutiveStallFrames == 89u);

	/* One OK frame resets the count. */
	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_OK, frame);
	CHECK(g_a.outcome.consecutiveStallFrames == 0u);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);

	/* 90 consecutive stalls latch. */
	for (i = 0; i < (NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS - 1u); i++)
	{
		NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, frame + 1u);
		CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	}
	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, frame + 1u);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	report = NativeLockstepMatchOutcome_FirstOutcome(&g_a.outcome);
	CHECK(report != NULL);
	CHECK(report->cause == (uint32_t)NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT);
	CHECK(report->frameIndex == frame + 1u);
	CHECK(report->stalledFrameCount == 90u);

	/* The remote human is dropped; the local one stays active. */
	CHECK(g_a.roster.lifecycle[slotB] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(g_a.roster.lifecycle[g_a.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);

	/* Latched: further results change nothing. */
	outcomeBefore = g_a.outcome;
	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_OK, frame + 2u);
	CHECK(memcmp(&outcomeBefore, &g_a.outcome, sizeof(outcomeBefore)) == 0);

	/* Still RACING until the next Tick, which shows the timeout. */
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	/* The agreed config stays readable on RESULTS. */
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);

	/* B was not touched. */
	CHECK(NativeArcadeNetplay_Tick(&g_b, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(g_b.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);

	/* Off RACING the hook is a no-op. */
	outcomeBefore = g_a.outcome;
	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, frame + 3u);
	CHECK(memcmp(&outcomeBefore, &g_a.outcome, sizeof(outcomeBefore)) == 0);

	ShutdownBoth();
	return 0;
}

/* 8. Release-to-arm across the race end (UX-3): a held throttle never
 * confirms the results screen. */
static int TestReleaseToArmAcrossRaceEnd(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_ARM_A_PORT, TEST_ARM_B_PORT));

	/* CROSS held from mid-race ... */
	for (tick = 0; tick < 3u; tick++)
	{
		TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	/* ... across the finish ... */
	TickBoth(BTN_CROSS, BTN_CROSS, 1u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);

	/* ... and well past the dwell: nothing fires. */
	for (tick = 0; tick < (RESULTS_DWELL_TICKS + 10u); tick++)
	{
		TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.menuArmed == 0u);
	CHECK(view.ticksInScreen > RESULTS_DWELL_TICKS);

	/* One released tick arms; the next press confirms REMATCH. */
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.menuArmed == 1u);
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);

	ShutdownBoth();
	return 0;
}

/* Ticks the adapter until the given screen has been left; returns 0 if the
 * budget ran out. Every action while still on the screen must be NONE, and
 * the tick that leaves it must return leaveAction: NONE out of RACING (the
 * link stays open for the latched report), CLOSE_LINK for a pre-race LINK
 * ERROR out of SELECT (MS-8b). */
static int TickUntilLeft(struct NativeArcadeNetplay *netplay, uint32_t screen, enum NativeArcadeFlowAction leaveAction)
{
	enum NativeArcadeFlowAction action;
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(netplay) == screen); tick++)
	{
		action = NativeArcadeNetplay_Tick(netplay, 0u, 0u);
		if (action != ((ScreenOf(netplay) == screen) ? ACT_NONE : leaveAction))
		{
			return 0;
		}
	}
	return ScreenOf(netplay) != screen;
}

/* 9. B1: staggered Enter with the production cadence. A enters and ticks
 * alone for 10 ticks, so its first HELLO goes nowhere; then B enters. With a
 * sparse HELLO cadence A completes on B's first HELLO and stops sending
 * before B has seen one, and B never completes; with HELLO every tick both
 * reach START_RACE. */
static int TestStaggeredEnter(void)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &expected));
	CHECK(InitPairDefaultCadence(&fixture, TEST_STAGGER_ENTER_A_PORT, TEST_STAGGER_ENTER_B_PORT));
	CHECK(g_a.config.attemptTicksPerCandidate == 150u);
	CHECK(g_a.config.retransmitIntervalTicks == 1u);

	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	for (tick = 0; tick < 10u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	}
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_CONNECTING);

	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);

	ShutdownBoth();
	return 0;
}

/* 10. B1: staggered rematch with the production cadence. A confirms REMATCH;
 * B stays on RESULTS for 20 more ticks and then confirms. Both must agree on
 * the same derived rematch config, select on it, and reach START_RACE again
 * on the same resolved config. */
static int TestStaggeredRematch(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 first;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	uint64_t expectedSeed = 0u;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitPairDefaultCadence(&fixture, TEST_STAGGER_REMATCH_A_PORT, TEST_STAGGER_REMATCH_B_PORT));
	CHECK(EnterAndRaceInitialized());
	CHECK(FinishAndDwell());
	first = *NativeArcadeNetplay_AgreedConfig(&g_a);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&first, &expectedSeed) == 1);

	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);

	for (tick = 0; tick < 20u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	}

	TickBoth(0u, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(actionA == ACT_NONE);
	/* Both propose the same rematch config on the derived seed. */
	CHECK(g_a.currentConfig.masterSeed == expectedSeed);
	CHECK(memcmp(&g_a.currentConfig, &g_b.currentConfig, sizeof(g_a.currentConfig)) == 0);

	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(agreedA->masterSeed != expectedSeed);
	CHECK(agreedA->masterSeed != first.masterSeed);
	CHECK(g_a.matchCount == 2u);
	CHECK(g_b.matchCount == 2u);

	ShutdownBoth();
	return 0;
}

/* 11. S1: a link fault found by the adapter's own lobby poll during RACING.
 * B sends A one cleanly composed bundle with a corrupted body byte and a
 * stale digest (the technique of tests/native_lobby_state_test.c
 * TestPeerLostThenRestartRecovers), over B's real socket. Ticking A alone,
 * its lobby poll faults the session and reads PEER_LOST; Tick then latches
 * the FAULTED outcome, drops B in the roster, and the flow shows LINK ERROR
 * from that cause. */
static int TestInRaceFaultFromPoll(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeUdpTransportAddress addrA;
	struct NativeLockstepPeerLink *linkB;
	struct NativeLockstepSession *sessionB;
	const struct NativeLockstepMatchOutcomeReport *report;
	const struct NativeLockstepFaultReport *fault;
	struct NativeLockstepMatchOutcomeTracker outcomeBefore;
	uint8_t bundleBytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t bundleSize = 0;
	uint8_t slotB = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slotB));
	CHECK(EnterAndRace(&fixture, TEST_FAULT_A_PORT, TEST_FAULT_B_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST_FAULT_A_PORT));

	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK(linkB != NULL);
	sessionB = NativeLockstepPeerLink_Session(linkB);
	CHECK(sessionB != NULL);
	CHECK(NativeLockstepSession_ComposeBundle(sessionB, 0u, bundleBytes, sizeof(bundleBytes), &bundleSize) == 1);
	CHECK(bundleSize == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	bundleBytes[40] ^= 0x01u; /* A pad-region body byte; the trailing digest goes stale. */
	CHECK(NativeUdpTransport_Send(&linkB->transport, &addrA, bundleBytes, bundleSize));

	CHECK(TickUntilLeft(&g_a, NATIVE_ARCADE_FLOW_SCREEN_RACING, ACT_NONE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(NativeLobbyState_Mode(&g_a.lobby) == NATIVE_LOBBY_STATE_PEER_LOST);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	fault = NativeLockstepSession_FirstFault(NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(&g_a)));
	CHECK(fault != NULL);
	CHECK(fault->cause == (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST);

	/* The adapter latched the real cause, not only the flow's LOST fallback. */
	report = NativeLockstepMatchOutcome_FirstOutcome(&g_a.outcome);
	CHECK(report != NULL);
	CHECK(report->cause == (uint32_t)NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED);
	CHECK(report->frameIndex == fault->frameIndex);
	CHECK(report->senderSlot == fault->senderSlot);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_a.roster.lifecycle[slotB] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(g_a.roster.lifecycle[g_a.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);

	/* Off RACING, a further Tick or hook call changes nothing latched. */
	outcomeBefore = g_a.outcome;
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, 1u);
	CHECK(memcmp(&outcomeBefore, &g_a.outcome, sizeof(outcomeBefore)) == 0);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);

	ShutdownBoth();
	return 0;
}

static void MakeTestPad(struct NativeCanonicalInputPadV1 *pad, uint32_t slot, uint32_t frame)
{
	memset(pad, 0, sizeof(*pad));
	pad->status = (uint8_t)(0x40u + slot);
	pad->id = (uint8_t)slot;
	pad->buttons[0] = (uint8_t)(frame & 0xFFu);
	pad->buttons[1] = (uint8_t)((frame >> 8u) & 0xFFu);
	pad->analog[0] = (uint8_t)(0x80u + slot);
	pad->connected = 1u;
}

/* A synthetic simulated frame; worldCount perturbs one domain digest. */
static int MakeWorldState(struct NativeCanonicalStateV4 *state, uint32_t frame, uint32_t worldCount)
{
	NativeCanonicalStateV4_Init(state);
	state->frameNumber = frame;
	state->control.frameCounter = (int32_t)frame;
	state->worldCounters.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
	state->worldCounters.activeBombMissileCount = worldCount;
	return NativeCanonicalStateV4_ComputeDigests(state) == 1;
}

/* One simulated frame for one side: take the committed inputs, and record
 * this side's digests on success. Returns the take result. */
static enum NativeLockstepSessionResult TakeAndRecord(struct NativeLockstepSession *session, uint32_t frame, uint32_t worldCount,
	int *recordOk)
{
	struct NativeLockstepSessionFrameInputs inputs;
	struct NativeCanonicalStateV4 state;
	enum NativeLockstepSessionResult result;

	result = NativeLockstepSession_TakeFrameInputs(session, frame, &inputs);
	*recordOk = 1;
	if (result == NATIVE_LOCKSTEP_SESSION_OK)
	{
		*recordOk = MakeWorldState(&state, frame, worldCount) &&
			(NativeLockstepSession_RecordLocalDigests(session, &state) == 1);
	}
	return result;
}

/* 12. S1: a real divergence found by the adapters' own lobby polls. Both
 * sessions run real frames over the real loopback links (submit, compose
 * and send, Tick to poll, take, record); A's simulation drifts from
 * divergeFrame on. The bundle for divergeFrame + inputDelay + 1 carries the
 * mismatching digests, each side's lobby poll latches DIVERGED, and Tick
 * turns that into DESYNC with the remote human dropped. */
static int TestInRaceDivergenceFromPoll(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeCanonicalInputPadV1 pad;
	struct NativeLockstepPeerLink *linkA;
	struct NativeLockstepPeerLink *linkB;
	struct NativeLockstepSession *sessionA;
	struct NativeLockstepSession *sessionB;
	const struct NativeLockstepMatchOutcomeReport *report;
	const struct NativeLockstepDivergenceReport *divergence;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	const uint32_t divergeFrame = 6u;
	const uint32_t detectFrame = divergeFrame + NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY + 1u;
	const uint32_t perturbedWorld = 3u;
	uint32_t frame;
	uint32_t spin;
	uint32_t leftAtFrame = UINT32_MAX;
	int takenA;
	int takenB;
	int recordOk;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_DIVERGE_A_PORT, TEST_DIVERGE_B_PORT));
	CHECK(g_a.config.inputDelay == NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY);
	linkA = NativeArcadeNetplay_Link(&g_a);
	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK(linkA != NULL);
	CHECK(linkB != NULL);
	sessionA = NativeLockstepPeerLink_Session(linkA);
	sessionB = NativeLockstepPeerLink_Session(linkB);

	for (frame = 0; (frame <= detectFrame) && (leftAtFrame == UINT32_MAX); frame++)
	{
		MakeTestPad(&pad, g_a.localSlot, frame);
		CHECK(NativeLockstepSession_SubmitLocalInput(sessionA, frame, &pad) == 1);
		MakeTestPad(&pad, g_b.localSlot, frame);
		CHECK(NativeLockstepSession_SubmitLocalInput(sessionB, frame, &pad) == 1);
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkA, frame) == 1);
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkB, frame) == 1);

		takenA = 0;
		takenB = 0;
		for (spin = 0; (spin < DRIVE_BUDGET) && !(takenA && takenB); spin++)
		{
			/* Tick polls each lobby, which delivers the other's bundle. */
			TickBoth(0u, 0u, 0u, &actionA, &actionB);
			CHECK(actionA == ACT_NONE);
			CHECK(actionB == ACT_NONE);
			if ((ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_RACING) || (ScreenOf(&g_b) != NATIVE_ARCADE_FLOW_SCREEN_RACING))
			{
				leftAtFrame = frame;
				break;
			}
			if (!takenA)
			{
				takenA = TakeAndRecord(sessionA, frame, (frame >= divergeFrame) ? perturbedWorld : 0u, &recordOk) ==
					NATIVE_LOCKSTEP_SESSION_OK;
				CHECK(recordOk);
			}
			if (!takenB)
			{
				takenB = TakeAndRecord(sessionB, frame, 0u, &recordOk) == NATIVE_LOCKSTEP_SESSION_OK;
				CHECK(recordOk);
			}
		}
		CHECK((leftAtFrame != UINT32_MAX) || (takenA && takenB));
	}
	/* Nothing diverged before the bundle that carries divergeFrame's digests. */
	CHECK(leftAtFrame == detectFrame);

	/* Whichever side noticed first, the other notices within the budget. */
	if (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		CHECK(TickUntilLeft(&g_a, NATIVE_ARCADE_FLOW_SCREEN_RACING, ACT_NONE));
	}
	if (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		CHECK(TickUntilLeft(&g_b, NATIVE_ARCADE_FLOW_SCREEN_RACING, ACT_NONE));
	}

	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(NativeLockstepPeerLink_Mode(linkA) == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED);
	CHECK(NativeLockstepPeerLink_Mode(linkB) == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED);

	divergence = NativeLockstepSession_FirstDivergence(sessionA);
	CHECK(divergence != NULL);
	CHECK(divergence->frameIndex == divergeFrame);
	report = NativeLockstepMatchOutcome_FirstOutcome(&g_a.outcome);
	CHECK(report != NULL);
	CHECK(report->cause == (uint32_t)NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED);
	CHECK(report->frameIndex == divergeFrame);
	CHECK(g_a.roster.lifecycle[g_b.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(g_a.roster.lifecycle[g_a.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);

	divergence = NativeLockstepSession_FirstDivergence(sessionB);
	CHECK(divergence != NULL);
	CHECK(divergence->frameIndex == divergeFrame);
	report = NativeLockstepMatchOutcome_FirstOutcome(&g_b.outcome);
	CHECK(report != NULL);
	CHECK(report->cause == (uint32_t)NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED);
	CHECK(report->frameIndex == divergeFrame);
	CHECK(g_b.roster.lifecycle[g_a.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(g_b.roster.lifecycle[g_b.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);

	ShutdownBoth();
	return 0;
}

/* 13. S2: a rematch whose config cannot be built never reuses the old seed.
 * A's rematch source, the last READY proposal, is corrupted after the race
 * (lapCount 0), so neither the seed nor the config can be built. A opens no
 * lobby at all, refuses every restart (its current config, the old valid
 * config with the old seed, is untouched, and the valid source is put back
 * too), and times out to OPPONENT LEFT; RETURN_TO_TITLE clears the block. */
static int TestRematchBuildFailureBlocks(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 agreed;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;
	/* Ticks spent in REMATCH_WAIT after the BEGIN_REMATCH tick. */
	uint32_t ticksToCloseA = 0u;
	uint32_t restartsA = 0u;
	int closedA = 0;
	int titleA = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_BLOCKED_A_PORT, TEST_BLOCKED_B_PORT));
	CHECK(FinishAndDwell());
	agreed = *NativeArcadeNetplay_AgreedConfig(&g_a);

	CHECK(memcmp(&g_a.lastReadyConfig, &agreed, sizeof(agreed)) == 0);
	g_a.lastReadyConfig.lapCount = 0u;
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(g_a.rematchBlocked == 1u);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	/* The current config was not replaced by a guess either: it is still the
	 * old, valid agreed config with the old seed. */
	CHECK(memcmp(&g_a.currentConfig, &agreed, sizeof(agreed)) == 0);
	CHECK(NativeArcadeNetplay_Link(&g_b) != NULL);
	/* Put the valid rematch source back as well: from here on only the block
	 * itself stops a restart from beginning a lobby on the old seed, so a
	 * Link that stays NULL proves RestartLobby refuses. */
	g_a.lastReadyConfig = agreed;

	for (tick = 0; (tick < DRIVE_BUDGET) && !titleA; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA != ACT_START_RACE);
		CHECK(actionB != ACT_START_RACE);
		CHECK(ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
		CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
		if (!closedA)
		{
			ticksToCloseA += 1u;
			CHECK(g_a.rematchBlocked == 1u);
		}
		if (actionA == ACT_RESTART_LOBBY)
		{
			restartsA += 1u;
			CHECK(g_a.lobbyBegun == 0u);
		}
		if (actionA == ACT_CLOSE_LINK)
		{
			CHECK(!closedA);
			closedA = 1;
			CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
			CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT);
		}
		if (actionA == ACT_RETURN_TO_TITLE)
		{
			CHECK(closedA);
			titleA = 1;
		}
	}
	CHECK(closedA);
	CHECK(titleA);
	/* The refused restarts really ran, and the wait ran its full length. */
	CHECK(restartsA > 0u);
	CHECK(ticksToCloseA == REMATCH_WAIT_TIMEOUT_TICKS);
	CHECK(g_a.rematchBlocked == 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);

	ShutdownBoth();
	return 0;
}

/* 14. S4a: BACK from REMATCH_WAIT closes the link and exits with no reason.
 * B stays on RESULTS, so A's rematch handshake stays unanswered. */
static int TestBackFromRematchWait(void)
{
	struct NativeMatchConfigV1 fixture;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_BACK_A_PORT, TEST_BACK_B_PORT));
	CHECK(FinishAndDwell());

	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(NativeArcadeNetplay_Link(&g_a) != NULL);

	/* Release arms the new screen; TRIANGLE then backs out. */
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	TickBoth(BTN_TRIANGLE, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_CLOSE_LINK);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);

	ShutdownBoth();
	return 0;
}

/* 15. S4b: Begin failure and recovery. A's local port is already bound by a
 * raw transport, so every Begin fails: A reads WAITING and keeps retrying
 * cleanly. Once the blocker is closed, the next retry begins and reaches
 * CONNECTING. */
static int TestBeginFailureRecovers(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayConfig config;
	struct NativeUdpTransport blocker;
	enum NativeArcadeFlowAction action;
	uint32_t tick;
	uint32_t restarts = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(MakeConfig(&config, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_BEGIN_FAIL_A_PORT,
		TEST_BEGIN_FAIL_B_PORT));
	CHECK(NativeArcadeNetplay_Init(&g_a, &config) == 1);

	memset(&blocker, 0, sizeof(blocker));
	CHECK(NativeUdpTransport_GlobalInit() == 1);
	CHECK(NativeUdpTransport_Open(&blocker, (uint16_t)TEST_BEGIN_FAIL_A_PORT) == 1);

	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_WAITING);

	/* Two retry pauses pass; each retry fails cleanly. */
	for (tick = 0; (tick < DRIVE_BUDGET) && (restarts < 2u); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_RESTART_LOBBY));
		if (action == ACT_RESTART_LOBBY)
		{
			restarts += 1u;
		}
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
		CHECK(g_a.lobbyBegun == 0u);
		CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	}
	CHECK(restarts == 2u);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_WAITING);

	/* Free the port: the next retry begins. */
	NativeUdpTransport_Close(&blocker);
	NativeUdpTransport_GlobalShutdown();
	for (tick = 0; (tick < DRIVE_BUDGET) && (g_a.lobbyBegun == 0u); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_RESTART_LOBBY));
		CHECK((g_a.lobbyBegun == 0u) || (action == ACT_RESTART_LOBBY));
	}
	CHECK(g_a.lobbyBegun == 1u);
	CHECK(NativeArcadeNetplay_Link(&g_a) != NULL);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_CONNECTING);

	NativeArcadeNetplay_Shutdown(&g_a);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	return 0;
}

/* Polls a peer link directly (never through an adapter Tick) until it reaches
 * the wanted mode; returns 0 if the budget ran out. */
static int PollLinkUntil(struct NativeLockstepPeerLink *link, enum NativeLockstepPeerLinkMode mode)
{
	uint32_t spin;

	for (spin = 0; (spin < DRIVE_BUDGET) && (NativeLockstepPeerLink_Mode(link) != mode); spin++)
	{
		NativeLockstepPeerLink_Poll(link);
	}
	return NativeLockstepPeerLink_Mode(link) == mode;
}

/* 16. Task 4c: a link fault latched through the race-time hook. B sends A
 * the same corrupted bundle as test 11, but here the race driver's own
 * NativeLockstepPeerLink_Poll receives it before any adapter Tick, so the
 * lobby poll never sees it first: NativeArcadeNetplay_OnTakeResult alone
 * latches FAULTED, drops B, and the next Tick shows LINK ERROR. */
static int TestHookFaultBeforeTick(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeUdpTransportAddress addrA;
	struct NativeLockstepPeerLink *linkA;
	struct NativeLockstepPeerLink *linkB;
	struct NativeLockstepSession *sessionB;
	const struct NativeLockstepMatchOutcomeReport *report;
	const struct NativeLockstepFaultReport *fault;
	uint8_t bundleBytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t bundleSize = 0;
	uint8_t slotB = 0u;
	const uint32_t frame = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slotB));
	CHECK(EnterAndRace(&fixture, TEST_HOOK_FAULT_A_PORT, TEST_HOOK_FAULT_B_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST_HOOK_FAULT_A_PORT));
	CHECK(slotB != g_a.localSlot);

	linkA = NativeArcadeNetplay_Link(&g_a);
	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK(linkA != NULL);
	CHECK(linkB != NULL);
	sessionB = NativeLockstepPeerLink_Session(linkB);
	CHECK(sessionB != NULL);
	CHECK(NativeLockstepSession_ComposeBundle(sessionB, 0u, bundleBytes, sizeof(bundleBytes), &bundleSize) == 1);
	CHECK(bundleSize == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	bundleBytes[40] ^= 0x01u; /* A pad-region body byte; the trailing digest goes stale. */
	CHECK(NativeUdpTransport_Send(&linkB->transport, &addrA, bundleBytes, bundleSize));

	/* The race driver's poll, not the adapter's: no adapter Tick yet. */
	CHECK(PollLinkUntil(linkA, NATIVE_LOCKSTEP_PEER_LINK_FAULTED));
	fault = NativeLockstepSession_FirstFault(NativeLockstepPeerLink_Session(linkA));
	CHECK(fault != NULL);
	CHECK(fault->cause == (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST);
	/* Nothing is latched in the adapter until the hook runs. */
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&g_a.outcome) == NULL);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(g_a.roster.lifecycle[slotB] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);

	NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_OK, frame);

	report = NativeLockstepMatchOutcome_FirstOutcome(&g_a.outcome);
	CHECK(report != NULL);
	CHECK(report->cause == (uint32_t)NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED);
	CHECK(report->frameIndex == fault->frameIndex);
	CHECK(report->senderSlot == fault->senderSlot);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_a.roster.lifecycle[slotB] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(g_a.roster.lifecycle[g_a.localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	/* Still RACING until the next Tick. */
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);

	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);

	ShutdownBoth();
	return 0;
}

/* Hook-path assertions for one side whose link has latched DIVERGED at
 * divergeFrame: nothing latched before the hook, DESYNC after it, then one
 * Tick to RESULTS. */
static int CheckHookDivergence(struct NativeArcadeNetplay *self, uint8_t remoteSlot, uint32_t divergeFrame,
	uint32_t hookFrame)
{
	const struct NativeLockstepMatchOutcomeReport *report;
	const struct NativeLockstepDivergenceReport *divergence;

	divergence = NativeLockstepSession_FirstDivergence(NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(self)));
	CHECK(divergence != NULL);
	CHECK(divergence->frameIndex == divergeFrame);
	CHECK(ScreenOf(self) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeLockstepMatchOutcome_FirstOutcome(&self->outcome) == NULL);
	CHECK(self->pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);

	NativeArcadeNetplay_OnTakeResult(self, NATIVE_LOCKSTEP_SESSION_OK, hookFrame);

	report = NativeLockstepMatchOutcome_FirstOutcome(&self->outcome);
	CHECK(report != NULL);
	CHECK(report->cause == (uint32_t)NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED);
	CHECK(report->frameIndex == divergeFrame);
	CHECK(self->pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(self->roster.lifecycle[remoteSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	CHECK(self->roster.lifecycle[self->localSlot] == (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	CHECK(ScreenOf(self) == NATIVE_ARCADE_FLOW_SCREEN_RACING);

	CHECK(NativeArcadeNetplay_Tick(self, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(self) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(self) == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	return 0;
}

/* 17. Task 4c: a real divergence latched through the race-time hook. The
 * same real-frame race as test 12, but the adapters are never ticked while
 * racing: the race driver polls each link directly and feeds every take
 * result to NativeArcadeNetplay_OnTakeResult. Once a link reads DIVERGED,
 * the hook alone latches DIVERGED on each side, and one Tick shows DESYNC. */
static int TestHookDivergenceBeforeTick(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeCanonicalInputPadV1 pad;
	struct NativeLockstepPeerLink *linkA;
	struct NativeLockstepPeerLink *linkB;
	struct NativeLockstepSession *sessionA;
	struct NativeLockstepSession *sessionB;
	enum NativeLockstepSessionResult result;
	const uint32_t divergeFrame = 6u;
	const uint32_t detectFrame = divergeFrame + NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY + 1u;
	const uint32_t perturbedWorld = 3u;
	uint32_t frame;
	uint32_t spin;
	uint32_t leftAtFrame = UINT32_MAX;
	int takenA;
	int takenB;
	int recordOk;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(EnterAndRace(&fixture, TEST_HOOK_DIVERGE_A_PORT, TEST_HOOK_DIVERGE_B_PORT));
	CHECK(g_a.config.inputDelay == NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY);
	linkA = NativeArcadeNetplay_Link(&g_a);
	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK(linkA != NULL);
	CHECK(linkB != NULL);
	sessionA = NativeLockstepPeerLink_Session(linkA);
	sessionB = NativeLockstepPeerLink_Session(linkB);

	for (frame = 0; (frame <= detectFrame) && (leftAtFrame == UINT32_MAX); frame++)
	{
		MakeTestPad(&pad, g_a.localSlot, frame);
		CHECK(NativeLockstepSession_SubmitLocalInput(sessionA, frame, &pad) == 1);
		MakeTestPad(&pad, g_b.localSlot, frame);
		CHECK(NativeLockstepSession_SubmitLocalInput(sessionB, frame, &pad) == 1);
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkA, frame) == 1);
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkB, frame) == 1);

		takenA = 0;
		takenB = 0;
		for (spin = 0; (spin < DRIVE_BUDGET) && !(takenA && takenB); spin++)
		{
			/* The race driver's poll; no adapter Tick. */
			NativeLockstepPeerLink_Poll(linkA);
			NativeLockstepPeerLink_Poll(linkB);
			if ((NativeLockstepPeerLink_Mode(linkA) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING) ||
				(NativeLockstepPeerLink_Mode(linkB) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING))
			{
				leftAtFrame = frame;
				break;
			}
			if (!takenA)
			{
				result = TakeAndRecord(sessionA, frame, (frame >= divergeFrame) ? perturbedWorld : 0u, &recordOk);
				CHECK(recordOk);
				NativeArcadeNetplay_OnTakeResult(&g_a, result, frame);
				takenA = result == NATIVE_LOCKSTEP_SESSION_OK;
			}
			if (!takenB)
			{
				result = TakeAndRecord(sessionB, frame, 0u, &recordOk);
				CHECK(recordOk);
				NativeArcadeNetplay_OnTakeResult(&g_b, result, frame);
				takenB = result == NATIVE_LOCKSTEP_SESSION_OK;
			}
			/* Healthy frames latch nothing. */
			CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
			CHECK(g_b.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
		}
		CHECK((leftAtFrame != UINT32_MAX) || (takenA && takenB));
	}
	/* Nothing diverged before the bundle that carries divergeFrame's digests. */
	CHECK(leftAtFrame == detectFrame);

	/* Whichever link noticed first, the other notices from its own poll. */
	CHECK(PollLinkUntil(linkA, NATIVE_LOCKSTEP_PEER_LINK_DIVERGED));
	CHECK(PollLinkUntil(linkB, NATIVE_LOCKSTEP_PEER_LINK_DIVERGED));

	CHECK(CheckHookDivergence(&g_a, g_b.localSlot, divergeFrame, detectFrame) == 0);
	CHECK(CheckHookDivergence(&g_b, g_a.localSlot, divergeFrame, detectFrame) == 0);

	ShutdownBoth();
	return 0;
}

/* 18. MS-7: scripted picks. A picks Tiny (2), votes ROO_TUBES (6) and 5
 * laps; B picks Crash (0), votes SLIDE_COLISEUM (16) and 5 laps. Both
 * CONFIRMED -> SELECT_RESULT -> RELINK -> READY -> START_RACE on one
 * byte-identical config: the track is one of the two votes (a seeded draw),
 * 5 laps, the picks in slots 0 and 1, the retail bot set, and a new seed. */
static int TestSelectScriptedPicks(void)
{
	/* A: NEXT, NEXT, CONFIRM (character); NEXT, CONFIRM (track); NEXT,
	 * CONFIRM (laps). B: PREV, CONFIRM; PREV (wraps), CONFIRM; NEXT, CONFIRM. */
	static const uint32_t scriptA[7] = {BTN_DOWN, BTN_DOWN, BTN_CROSS, BTN_DOWN, BTN_CROSS, BTN_DOWN, BTN_CROSS};
	static const uint32_t scriptB[7] = {BTN_UP, BTN_CROSS, BTN_UP, BTN_CROSS, BTN_DOWN, BTN_CROSS, 0u};
	static const uint8_t characters[2] = {2u, 0u};
	static const uint8_t tracks[2] = {6u, 16u};
	static const uint8_t laps[2] = {5u, 5u};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchSelectOutcome expectedOutcome;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	const struct NativeMatchSelectHumanState *human;
	uint64_t nonceA = 0u;
	uint64_t nonceB = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, &expectedOutcome, &expected));
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, 1u, &nonceA));
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, 1u, &nonceB));
	CHECK(nonceA != nonceB);

	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_PICKS_A_PORT, TEST_SELECT_PICKS_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	/* The select base is the lobby-agreed fixture; no agreed race config yet. */
	CHECK(memcmp(&g_a.select.base, &fixture, sizeof(fixture)) == 0);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(g_a.selectSerial == 1u);
	CHECK(g_b.selectSerial == 1u);

	CHECK(PressScripts(scriptA, scriptB, 7u));

	/* Each local human is done with exactly its picks. */
	human = NativeMatchSelectSession_Human(NativeArcadeNetplay_Select(&g_a), 0u);
	CHECK(human != NULL);
	CHECK(human->lockMask == 7u);
	CHECK(human->characterID == 2u);
	CHECK(human->trackID == 6u);
	CHECK(human->lapCount == 5u);
	CHECK(human->nonce == nonceA);
	human = NativeMatchSelectSession_Human(NativeArcadeNetplay_Select(&g_b), 1u);
	CHECK(human != NULL);
	CHECK(human->lockMask == 7u);
	CHECK(human->characterID == 0u);
	CHECK(human->trackID == 16u);
	CHECK(human->lapCount == 5u);
	CHECK(human->nonce == nonceB);

	CHECK(DriveBothToRaceChecked());
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_b)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK((agreedA->trackID == 6u) || (agreedA->trackID == 16u));
	CHECK(agreedA->lapCount == 5u);
	CHECK(agreedA->slots[g_a.localSlot].characterID == 2u);
	CHECK(agreedA->slots[g_b.localSlot].characterID == 0u);
	CHECK(BotsAreRetailSet(agreedA));
	CHECK(agreedA->masterSeed != fixture.masterSeed);
	CHECK(agreedA->masterSeed != 0u);

	/* Both adapters kept the outcome they relinked on: the track came from
	 * a draw between the two votes, the laps did not. */
	CHECK(g_a.outcomeValid == 1u);
	CHECK(g_b.outcomeValid == 1u);
	CHECK(memcmp(&g_a.lastOutcome, &expectedOutcome, sizeof(expectedOutcome)) == 0);
	CHECK(memcmp(&g_b.lastOutcome, &expectedOutcome, sizeof(expectedOutcome)) == 0);
	CHECK(g_a.lastOutcome.trackDrawn == 1u);
	CHECK(g_a.lastOutcome.lapsDrawn == 0u);
	CHECK(g_a.lastOutcome.characterReassignedMask == 0u);

	/* The two adapters used different nonces, and each saw the other's. */
	CHECK(g_a.select.humans[0].nonce == nonceA);
	CHECK(g_a.select.humans[1].nonce == nonceB);
	CHECK(g_b.select.humans[0].nonce == nonceA);
	CHECK(g_b.select.humans[1].nonce == nonceB);
	CHECK(g_a.relinked == 1u);
	CHECK(g_a.relinkBlocked == 0u);

	ShutdownBoth();
	return 0;
}

/* 19. MS-7: both idle. Every item auto-locks on its cursor (SEL-6): Crash
 * and Cortex (the fixture's slot characters), CRASH_COVE (the fixture track
 * is not a table track), 3 laps; nothing is drawn, and the seed is new. */
static int TestSelectIdleAutoPick(void)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	const struct NativeMatchSelectHumanState *human;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &expected));
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_IDLE_A_PORT, TEST_SELECT_IDLE_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());

	/* The initial cursors. */
	human = NativeMatchSelectSession_Human(NativeArcadeNetplay_Select(&g_a), 0u);
	CHECK(human != NULL);
	CHECK(human->characterID == 0u);
	CHECK(human->trackID == FIXTURE_TRACK_CURSOR);
	CHECK(human->lapCount == FIXTURE_LAP_CURSOR);
	CHECK(human->lockMask == 0u);
	human = NativeMatchSelectSession_Human(NativeArcadeNetplay_Select(&g_b), 1u);
	CHECK(human != NULL);
	CHECK(human->characterID == 1u);
	CHECK(human->trackID == FIXTURE_TRACK_CURSOR);
	CHECK(human->lapCount == FIXTURE_LAP_CURSOR);

	CHECK(DriveBothToRaceChecked());
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK(agreedA->slots[g_a.localSlot].characterID == 0u);
	CHECK(agreedA->slots[g_b.localSlot].characterID == 1u);
	CHECK(agreedA->trackID == 3u);
	CHECK(agreedA->lapCount == 3u);
	CHECK(BotsAreRetailSet(agreedA));
	CHECK(agreedA->masterSeed != fixture.masterSeed);
	CHECK(g_a.lastOutcome.trackDrawn == 0u);
	CHECK(g_a.lastOutcome.lapsDrawn == 0u);
	CHECK(g_a.lastOutcome.characterReassignedMask == 0u);

	ShutdownBoth();
	return 0;
}

/* Polls a link directly until an aux record from sender human `sender` with
 * the character locked has arrived, discarding everything taken on the way:
 * the record is then in flight as far as the adapter is concerned. Returns
 * 0 if the budget ran out. */
static int AbsorbLockedCharacterRecord(struct NativeLockstepPeerLink *link, uint8_t sender)
{
	uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0u;
	uint32_t spin;

	for (spin = 0; spin < DRIVE_BUDGET; spin++)
	{
		NativeLockstepPeerLink_Poll(link);
		while (NativeLockstepPeerLink_TakeAux(link, bytes, sizeof(bytes), &size))
		{
			struct NativeCodecReader reader;
			struct NativeMatchSelectMessageV1 message;
			uint32_t cause = 0u;

			NativeCodecReader_Init(&reader, bytes, size);
			if (NativeMatchSelectMessageV1_Decode(&reader, &message, &cause) && (message.senderHuman == sender) &&
				((message.lockMask & NATIVE_MATCH_SELECT_LOCK_CHARACTER) != 0u))
			{
				return 1;
			}
		}
	}
	return 0;
}

/* 20. MS-7: both lock the same character on the same tick. B moves to Crash
 * (0), A's cursor is on Crash; both CONFIRM on one tick. A locks first and
 * sends; that record is still in flight when B's CONFIRM runs (the test
 * absorbs it at B's link before B ticks, as crossing datagrams would be), so
 * B's lock is not refused. Resolution reassigns CAB2 (OD-2): Cortex (1), the
 * lowest free character, identically on both. */
static int TestSelectSameCharacterSameTick(void)
{
	static const uint8_t characters[2] = {0u, 0u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	static const uint32_t moveA[1] = {0u};
	static const uint32_t moveB[1] = {BTN_UP};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchSelectOutcome expectedOutcome;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	const struct NativeMatchSelectHumanState *human;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, &expectedOutcome, &expected));
	CHECK(expectedOutcome.characterReassignedMask == 0x2u);
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_SAME_A_PORT, TEST_SELECT_SAME_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(PressScripts(moveA, moveB, 1u));
	CHECK(NativeMatchSelectSession_Human(&g_b.select, 1u)->characterID == 0u);

	/* The same tick: A confirms and sends; B confirms before seeing it. */
	actionA = NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 0u);
	CHECK(actionA == ACT_NONE);
	CHECK((NativeMatchSelectSession_Human(&g_a.select, 0u)->lockMask & NATIVE_MATCH_SELECT_LOCK_CHARACTER) != 0u);
	CHECK(AbsorbLockedCharacterRecord(NativeArcadeNetplay_Link(&g_b), 0u));
	actionB = NativeArcadeNetplay_Tick(&g_b, BTN_CROSS, 0u);
	CHECK(actionB == ACT_NONE);
	human = NativeMatchSelectSession_Human(&g_b.select, 1u);
	CHECK((human->lockMask & NATIVE_MATCH_SELECT_LOCK_CHARACTER) != 0u);
	CHECK(human->characterID == 0u);

	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(DriveBothToRaceChecked());

	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK(agreedA->slots[g_a.localSlot].characterID == 0u);
	CHECK(agreedA->slots[g_b.localSlot].characterID == 1u);
	CHECK(BotsAreRetailSet(agreedA));
	CHECK(g_a.lastOutcome.characterReassignedMask == 0x2u);
	CHECK(g_b.lastOutcome.characterReassignedMask == 0x2u);

	ShutdownBoth();
	return 0;
}

/* 21. MS-7: B shuts down during SELECT. A's link stays READY (UDP has no
 * disconnect), so A fails by select peer silence (SEL-9) and shows LINK
 * ERROR; it never returns START_RACE and never relinks. */
static int TestSelectPeerSilence(void)
{
	struct NativeMatchConfigV1 fixture;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction action;
	uint32_t tick;
	uint32_t ticks = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_SILENCE_A_PORT, TEST_SELECT_SILENCE_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	for (tick = 0; tick < 5u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	NativeArcadeNetplay_Shutdown(&g_b);

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		/* NONE while selecting; CLOSE_LINK on the LINK ERROR tick (MS-8b). */
		CHECK(action == ((ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT) ? ACT_NONE : ACT_CLOSE_LINK));
		ticks += 1u;
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
	/* B's last record may land on A's first tick after the shutdown or
	 * before it; the silence then runs its full length. */
	CHECK(ticks <= SELECT_PEER_SILENCE_TICKS);
	CHECK(ticks >= SELECT_PEER_SILENCE_TICKS - 2u);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(g_a.relinked == 0u);
	CHECK(g_a.outcomeValid == 0u);
	CHECK(NativeArcadeNetplay_Select(&g_a) == NULL);
	/* No race ran, so RESULTS has no agreed config; the rematch source is the
	 * select base both held at MATCH_FOUND. */
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(g_a.lastReadyValid == 1u);
	CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);

	/* Nothing starts afterwards either. */
	for (tick = 0; tick < 50u; tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK(action != ACT_START_RACE);
		CHECK(action != ACT_RELINK);
	}

	ShutdownBoth();
	return 0;
}

/* From SELECT, idle until both are on SELECT_RESULT, CONFIRMED, and neither
 * has relinked yet. */
static int DriveBothToSelectResult(void)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && !((ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) &&
													 (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT));
		 tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		if ((actionA != ACT_NONE) || (actionB != ACT_NONE))
		{
			return 0;
		}
	}
	return (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) &&
		(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) &&
		(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_CONFIRMED) &&
		(NativeMatchSelectSession_Status(&g_b.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_CONFIRMED) &&
		(g_a.relinked == 0u) && (g_b.relinked == 0u);
}

/* 22. MS-7: B shuts down right after both are CONFIRMED, during A's result
 * hold. A still relinks on the resolved config, nobody answers the relink
 * handshake, and A shows LINK ERROR exactly at the launch timeout, never
 * START_RACE. */
static int TestSelectLaunchTimeout(void)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	enum NativeArcadeFlowAction action;
	uint32_t tick;
	uint32_t ticksSinceRelink = 0u;
	uint32_t restarts = 0u;
	int relinked = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &expected));
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_LAUNCH_A_PORT, TEST_SELECT_LAUNCH_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(DriveBothToSelectResult());
	NativeArcadeNetplay_Shutdown(&g_b);
	/* RELINK builds on the session's own base (what the exchanged digest
	 * covers), never on currentConfig: a perturbed currentConfig must not
	 * change the resolved config. */
	g_a.currentConfig.masterSeed ^= UINT64_C(1);

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK(action != ACT_START_RACE);
		if (relinked)
		{
			ticksSinceRelink += 1u;
			/* CLOSE_LINK on the launch-timeout tick (MS-8b). */
			if (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS)
			{
				CHECK(action == ACT_CLOSE_LINK);
			}
			else
			{
				CHECK((action == ACT_NONE) || (action == ACT_RESTART_LOBBY));
			}
			restarts += (action == ACT_RESTART_LOBBY) ? 1u : 0u;
		}
		else if (action == ACT_RELINK)
		{
			relinked = 1;
			/* The relink is on the resolved config, on a new link. */
			CHECK(g_a.relinked == 1u);
			CHECK(g_a.relinkBlocked == 0u);
			CHECK(g_a.outcomeValid == 1u);
			CHECK(memcmp(&g_a.currentConfig, &expected, sizeof(expected)) == 0);
			CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
		}
		else
		{
			CHECK(action == ACT_NONE);
		}
	}
	CHECK(relinked);
	CHECK(ticksSinceRelink == LAUNCH_TIMEOUT_TICKS);
	/* The 20-tick attempt budget ran out unanswered, and the retries ran. */
	CHECK(restarts > 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_a.raceArmed == 0u);
	CHECK(g_a.matchCount == 0u);
	/* The relink's handshake never completed and no race ran: RESULTS has no
	 * agreed config, although the current config is the unanswered relink
	 * proposal. The rematch source is still the select base. */
	CHECK(memcmp(&g_a.currentConfig, &expected, sizeof(expected)) == 0);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(g_a.lastReadyValid == 1u);
	CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);

	ShutdownBoth();
	return 0;
}

/* 22b. MS-7: a resolved config that cannot be built blocks the relink. A's
 * select session base (what RELINK builds on) is corrupted during the result
 * hold (lapCount 0), so RELINK cannot build: A closes the old link, begins
 * nothing, refuses every restart (its current config is still the valid
 * base, so only the block stops a restart), and shows LINK ERROR at the
 * launch timeout; it never races on the base config, and RESULTS has no
 * agreed config. BEGIN_REMATCH clears the block. */
static int TestSelectRelinkBuildFailureBlocks(void)
{
	struct NativeMatchConfigV1 fixture;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;
	uint32_t ticksSinceRelink = 0u;
	uint32_t restarts = 0u;
	int relinked = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_BLOCKED_A_PORT, TEST_SELECT_BLOCKED_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(DriveBothToSelectResult());
	g_a.select.base.lapCount = 0u;

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA != ACT_START_RACE);
		CHECK(actionB != ACT_START_RACE);
		if (relinked)
		{
			ticksSinceRelink += 1u;
			if (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS)
			{
				CHECK(actionA == ACT_CLOSE_LINK);
			}
			else
			{
				CHECK((actionA == ACT_NONE) || (actionA == ACT_RESTART_LOBBY));
			}
			restarts += (actionA == ACT_RESTART_LOBBY) ? 1u : 0u;
			CHECK(g_a.lobbyBegun == 0u);
			CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
		}
		else if (actionA == ACT_RELINK)
		{
			relinked = 1;
			CHECK(g_a.relinked == 1u);
			CHECK(g_a.relinkBlocked == 1u);
			CHECK(g_a.outcomeValid == 0u);
			CHECK(g_a.lobbyBegun == 0u);
			CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
			/* Not replaced by a guess: still the valid base, so only the
			 * block stops a restart from beginning a lobby on it. */
			CHECK(memcmp(&g_a.currentConfig, &fixture, sizeof(fixture)) == 0);
		}
		else
		{
			CHECK(actionA == ACT_NONE);
		}
	}
	CHECK(relinked);
	CHECK(ticksSinceRelink == LAUNCH_TIMEOUT_TICKS);
	CHECK(restarts > 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_a.relinkBlocked == 1u);
	CHECK(g_a.matchCount == 0u);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);

	/* BEGIN_REMATCH clears the block. */
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	}
	CHECK(NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 0u) == ACT_BEGIN_REMATCH);
	CHECK(g_a.relinkBlocked == 0u);
	CHECK(g_a.relinked == 0u);
	CHECK(g_a.lobbyBegun == 1u);

	ShutdownBoth();
	return 0;
}

/* 23. MS-7, OD-3: a rematch goes back through select. The first match is
 * picked (Coco 3 and Dingodile 5, TIGER_TEMPLE 4, 7 laps); both REMATCH;
 * READY -> MATCH_FOUND -> SELECT, where each cursor starts on that cabinet's
 * previous picks; new picks start a race on a config whose seed differs from
 * the previous match's. */
static int TestSelectRematchThroughSelect(void)
{
	/* First select. A: 3 x NEXT, CONFIRM; 2 x NEXT, CONFIRM; PREV (wraps to
	 * 7), CONFIRM. B: 4 x NEXT, CONFIRM; 2 x NEXT, CONFIRM; PREV, CONFIRM. */
	static const uint32_t firstA[10] = {BTN_DOWN, BTN_DOWN, BTN_DOWN, BTN_CROSS, BTN_DOWN, BTN_DOWN, BTN_CROSS, BTN_UP,
		BTN_CROSS, 0u};
	static const uint32_t firstB[10] = {BTN_DOWN, BTN_DOWN, BTN_DOWN, BTN_DOWN, BTN_CROSS, BTN_DOWN, BTN_DOWN,
		BTN_CROSS, BTN_UP, BTN_CROSS};
	/* Rematch select. A: NEXT, CONFIRM (N_GIN 4); NEXT, CONFIRM (COCO_PARK
	 * 14); NEXT (wraps to 3), CONFIRM. B confirms every cursor as it is. */
	static const uint32_t secondA[6] = {BTN_DOWN, BTN_CROSS, BTN_DOWN, BTN_CROSS, BTN_DOWN, BTN_CROSS};
	static const uint32_t secondB[6] = {BTN_CROSS, BTN_CROSS, BTN_CROSS, 0u, 0u, 0u};
	static const uint8_t firstCharacters[2] = {3u, 5u};
	static const uint8_t firstTracks[2] = {4u, 4u};
	static const uint8_t firstLaps[2] = {7u, 7u};
	static const uint8_t secondCharacters[2] = {4u, 5u};
	static const uint8_t secondTracks[2] = {14u, 4u};
	static const uint8_t secondLaps[2] = {3u, 7u};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 first;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchConfigV1 rematchBase;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	const struct NativeMatchSelectHumanState *human;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint64_t rematchSeed = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, firstCharacters, firstTracks, firstLaps, 1u, NULL, &expected));
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_REMATCH_A_PORT, TEST_SELECT_REMATCH_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(PressScripts(firstA, firstB, 10u));
	CHECK(DriveBothToRaceChecked());
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);
	first = *NativeArcadeNetplay_AgreedConfig(&g_a);
	CHECK(memcmp(&first, &expected, sizeof(expected)) == 0);
	CHECK(first.trackID == 4u);
	CHECK(first.lapCount == 7u);
	CHECK(first.slots[g_a.localSlot].characterID == 3u);
	CHECK(first.slots[g_b.localSlot].characterID == 5u);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&first, &rematchSeed) == 1);

	/* Race, finish, both REMATCH. */
	CHECK(FinishAndDwell());
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(g_a.relinked == 0u);
	CHECK(NativeArcadeNetplay_Select(&g_a) == NULL);
	rematchBase = g_a.currentConfig;
	CHECK(rematchBase.masterSeed == rematchSeed);

	/* READY -> MATCH_FOUND -> SELECT on the rematch config. */
	CHECK(DriveBothIntoSelect());
	CHECK(g_a.selectSerial == 2u);
	CHECK(g_b.selectSerial == 2u);
	CHECK(memcmp(&g_a.select.base, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.select.base, &rematchBase, sizeof(rematchBase)) == 0);
	/* Each cursor starts on that cabinet's previous picks (OD-3). */
	human = NativeMatchSelectSession_Human(NativeArcadeNetplay_Select(&g_a), 0u);
	CHECK(human != NULL);
	CHECK(human->characterID == 3u);
	CHECK(human->trackID == 4u);
	CHECK(human->lapCount == 7u);
	CHECK(human->lockMask == 0u);
	human = NativeMatchSelectSession_Human(NativeArcadeNetplay_Select(&g_b), 1u);
	CHECK(human != NULL);
	CHECK(human->characterID == 5u);
	CHECK(human->trackID == 4u);
	CHECK(human->lapCount == 7u);
	CHECK(human->lockMask == 0u);

	/* New picks. */
	CHECK(ExpectedResolvedConfig(&rematchBase, secondCharacters, secondTracks, secondLaps, 2u, NULL, &expected));
	CHECK(PressScripts(secondA, secondB, 6u));
	CHECK(DriveBothToRaceChecked());
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK(agreedA->slots[g_a.localSlot].characterID == 4u);
	CHECK(agreedA->slots[g_b.localSlot].characterID == 5u);
	CHECK((agreedA->trackID == 14u) || (agreedA->trackID == 4u));
	CHECK((agreedA->lapCount == 3u) || (agreedA->lapCount == 7u));
	CHECK(BotsAreRetailSet(agreedA));
	CHECK(agreedA->masterSeed != first.masterSeed);
	CHECK(agreedA->masterSeed != rematchSeed);
	CHECK(g_a.matchCount == 2u);
	CHECK(g_b.matchCount == 2u);

	ShutdownBoth();
	return 0;
}

/* 24. MS-7: the select nonce (golden values computed independently with
 * .NET System.Security.Cryptography.SHA256 over the documented 39-byte
 * input), and the aux-inbox discard at BEGIN_SELECT: whatever is waiting
 * then is dropped unread, while a datagram arriving after it reaches the
 * session. */
static int TestSelectNonceAndAuxDiscard(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeLockstepPeerLink *linkA;
	struct NativeLockstepPeerLink *linkB;
	uint8_t junk[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction action = ACT_NONE;
	uint64_t nonce = 0u;
	uint64_t other = 0u;
	uint32_t tick;
	uint32_t i;

	/* Golden. */
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdef), 1u, 1u, &nonce) == 1);
	CHECK(nonce == UINT64_C(0x150cf333915b2da1));
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, 1u, 1u, &nonce) == 1);
	CHECK(nonce == UINT64_C(0xebc8503d7e4da731));
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, 2u, 1u, &nonce) == 1);
	CHECK(nonce == UINT64_C(0x67c8684915b85584));
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, 1u, 2u, &nonce) == 1);
	CHECK(nonce == UINT64_C(0x0a1192063a37884d));
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, 2u, 2u, &nonce) == 1);
	CHECK(nonce == UINT64_C(0x7e020a119e4f0815));

	/* Deterministic, and sensitive to every input, including the high
	 * bytes of the entropy and serial. */
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdef), 1u, 1u, &nonce) == 1);
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdef), 1u, 1u, &other) == 1);
	CHECK(nonce == other);
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdee), 1u, 1u, &other) == 1);
	CHECK(nonce != other);
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x8123456789abcdef), 1u, 1u, &other) == 1);
	CHECK(nonce != other);
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdef), 2u, 1u, &other) == 1);
	CHECK(nonce != other);
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdef), 1u, 2u, &other) == 1);
	CHECK(nonce != other);
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(UINT64_C(0x0123456789abcdef), 1u, UINT32_C(0x01000001), &other) == 1);
	CHECK(nonce != other);
	/* NULL output: 0. */
	CHECK(NativeArcadeNetplay_DeriveSelectNonce(0u, 1u, 1u, NULL) == 0);

	/* The aux inbox is empty right after BEGIN_SELECT, even with junk queued
	 * before it. Drive both until A is on MATCH_FOUND, then B alone until its
	 * link runs. */
	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_AUX_A_PORT, TEST_SELECT_AUX_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK((actionB == ACT_NONE) || (actionB == ACT_BEGIN_SELECT));
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	for (tick = 0; (tick < DRIVE_BUDGET) && (LobbyStatusOf(&g_b) != (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY); tick++)
	{
		(void)NativeArcadeNetplay_Tick(&g_b, 0u, 0u);
	}
	linkA = NativeArcadeNetplay_Link(&g_a);
	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK(NativeLockstepPeerLink_Mode(linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	memset(junk, 0xEE, sizeof(junk));
	for (i = 0; i < 5u; i++)
	{
		CHECK(NativeLockstepPeerLink_SendAux(linkB, junk, sizeof(junk)) == 1);
	}
	for (tick = 0; (tick < DRIVE_BUDGET) && (NativeLockstepPeerLink_AuxCount(linkA) < 5u); tick++)
	{
		NativeLockstepPeerLink_Poll(linkA);
	}
	CHECK(NativeLockstepPeerLink_AuxCount(linkA) >= 5u);

	/* A alone, through the rest of its MATCH_FOUND hold. */
	for (tick = 0; (tick < DRIVE_BUDGET) && (action != ACT_BEGIN_SELECT); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_BEGIN_SELECT));
	}
	CHECK(action == ACT_BEGIN_SELECT);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(NativeArcadeNetplay_Link(&g_a) == linkA);
	CHECK(NativeLockstepPeerLink_AuxCount(linkA) == 0u);
	CHECK(NativeArcadeNetplay_Select(&g_a) != NULL);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_PICKING);

	/* One more junk datagram after BEGIN_SELECT does reach the session on
	 * A's next tick and counts as malformed: the counter witnesses every
	 * datagram fed, so exactly 1 (not 6) proves the five queued before
	 * BEGIN_SELECT were discarded unread. */
	CHECK(NativeLockstepPeerLink_SendAux(linkB, junk, sizeof(junk)) == 1);
	for (tick = 0; (tick < DRIVE_BUDGET) && (NativeLockstepPeerLink_AuxCount(linkA) < 1u); tick++)
	{
		NativeLockstepPeerLink_Poll(linkA);
	}
	CHECK(NativeLockstepPeerLink_AuxCount(linkA) == 1u);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(NativeLockstepPeerLink_AuxCount(linkA) == 0u);
	CHECK(NativeMatchSelectSession_DroppedMalformed(&g_a.select) == 1u);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_PICKING);

	/* The select still completes and both race. */
	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), NativeArcadeNetplay_AgreedConfig(&g_b),
			  sizeof(struct NativeMatchConfigV1)) == 0);

	ShutdownBoth();
	return 0;
}

/* Sends the peer on toPort one cleanly composed bundle from from's session
 * with a corrupted body byte and a stale digest, over from's real socket
 * (the technique of TestInRaceFaultFromPoll): the receiver's next lobby poll
 * faults its link (BAD_DIGEST) and its lobby reads PEER_LOST. */
static int SendCorruptBundle(struct NativeArcadeNetplay *from, uint32_t toPort)
{
	struct NativeUdpTransportAddress to;
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(from);
	uint8_t bundleBytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t bundleSize = 0;

	if ((link == NULL) || !NativeUdpTransport_MakeAddress(&to, "127.0.0.1", (uint16_t)toPort))
	{
		return 0;
	}
	if (!NativeLockstepSession_ComposeBundle(NativeLockstepPeerLink_Session(link), 0u, bundleBytes, sizeof(bundleBytes),
			&bundleSize) ||
		(bundleSize != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES))
	{
		return 0;
	}
	bundleBytes[40] ^= 0x01u; /* A pad-region body byte; the trailing digest goes stale. */
	return NativeUdpTransport_Send(&link->transport, &to, bundleBytes, bundleSize) != 0;
}

/* 25. MS-7b: a rematch after an asymmetric pre-race failure. Both reach
 * SELECT on the fixture. A locks all three items and B two; B then locks its
 * last item on a tick of its own, so B has resolved but cannot confirm (A
 * has not ticked since, so no resolved record from A exists). B's old link
 * is then faulted by a corrupted bundle: B shows LINK ERROR still holding the
 * base and never relinks. A, ticked alone, confirms on B's resolved record,
 * relinks on the resolved config, and shows LINK ERROR at the launch
 * timeout. The two current configs now differ (resolved vs base), but the
 * last READY proposal is the base on both, so the two REMATCH proposals are
 * byte-identical: both reach READY, go through SELECT again, and race on one
 * config. Deriving from the current configs would propose two different
 * configs, which the rematch handshake rejects. */
static int TestSelectFailureRematchAgrees(void)
{
	static const uint32_t lockA[3] = {BTN_CROSS, BTN_CROSS, BTN_CROSS};
	static const uint32_t lockB[3] = {BTN_CROSS, BTN_CROSS, 0u};
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	const uint8_t allLocks = (uint8_t)(NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK |
		NATIVE_MATCH_SELECT_LOCK_LAPS);
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 resolved;
	struct NativeMatchConfigV1 rematchBase;
	struct NativeMatchConfigV1 expected;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction action;
	uint64_t rematchSeed = 0u;
	uint32_t tick;
	int relinkedA = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &resolved));
	CHECK(memcmp(&resolved, &fixture, sizeof(fixture)) != 0);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &rematchSeed) == 1);
	CHECK(NativeLockstepRematch_BuildConfig(&fixture, rematchSeed, &rematchBase) == 1);
	CHECK(ExpectedResolvedConfig(&rematchBase, characters, tracks, laps, 2u, NULL, &expected));

	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_ASYM_REMATCH_A_PORT, TEST_SELECT_ASYM_REMATCH_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(PressScripts(lockA, lockB, 3u));
	CHECK(NativeMatchSelectSession_Human(&g_a.select, 0u)->lockMask == allLocks);
	CHECK(NativeMatchSelectSession_Human(&g_b.select, 1u)->lockMask ==
		(uint8_t)(NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK));
	/* B has seen all of A's locks (a few idle ticks at most; B's lap item is
	 * far from its auto-lock). */
	for (tick = 0; (tick < 10u) && (NativeMatchSelectSession_Human(&g_b.select, 0u)->lockMask != allLocks); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(NativeMatchSelectSession_Human(&g_b.select, 0u)->lockMask == allLocks);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);

	/* B locks its last item alone: resolved, not confirmed. */
	CHECK(NativeArcadeNetplay_Tick(&g_b, BTN_CROSS, 0u) == ACT_NONE);
	CHECK(NativeMatchSelectSession_Human(&g_b.select, 1u)->lockMask == allLocks);
	CHECK(NativeMatchSelectSession_Status(&g_b.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_RESOLVED);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);

	/* B's old link faults before A ticks again: B fails on the base. */
	CHECK(SendCorruptBundle(&g_a, TEST_SELECT_ASYM_REMATCH_B_PORT));
	CHECK(TickUntilLeft(&g_b, NATIVE_ARCADE_FLOW_SCREEN_SELECT, ACT_CLOSE_LINK));
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	/* The flow left SELECT on the lost link (PEER_LOST) and closed it. */
	CHECK(LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST);
	CHECK(g_b.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	CHECK(NativeMatchSelectSession_Status(&g_b.select) != (uint32_t)NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	CHECK(g_b.relinked == 0u);
	CHECK(memcmp(&g_b.currentConfig, &fixture, sizeof(fixture)) == 0);

	/* A alone: confirms on B's resolved record, relinks on the resolved
	 * config, and nobody answers. */
	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT); tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK(action != ACT_START_RACE);
		if (action == ACT_RELINK)
		{
			relinkedA = 1;
			CHECK(g_a.relinkBlocked == 0u);
			CHECK(memcmp(&g_a.currentConfig, &resolved, sizeof(resolved)) == 0);
		}
	}
	CHECK(relinkedA);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_a.matchCount == 0u);
	CHECK(g_b.matchCount == 0u);

	/* The asymmetry: different current configs, one shared READY proposal,
	 * and no agreed race config on either side. */
	CHECK(memcmp(&g_a.currentConfig, &resolved, sizeof(resolved)) == 0);
	CHECK(memcmp(&g_a.currentConfig, &g_b.currentConfig, sizeof(g_a.currentConfig)) != 0);
	CHECK(g_a.lastReadyValid == 1u);
	CHECK(g_b.lastReadyValid == 1u);
	CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &fixture, sizeof(fixture)) == 0);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_b) == NULL);

	/* Through the results dwell, then both choose REMATCH. */
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(memcmp(&g_a.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);

	/* READY on the rematch config, MATCH_FOUND, and SELECT again. */
	CHECK(DriveBothIntoSelect());
	CHECK(g_a.selectSerial == 2u);
	CHECK(g_b.selectSerial == 2u);
	CHECK(memcmp(&g_a.lastReadyConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_a.select.base, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.select.base, &rematchBase, sizeof(rematchBase)) == 0);

	/* And on to one race. */
	CHECK(DriveBothToRaceChecked());
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);
	CHECK(g_a.matchCount == 1u);
	CHECK(g_b.matchCount == 1u);

	ShutdownBoth();
	return 0;
}

/* 26. MS-7b: the select session cannot start. A's current config (the
 * select base) is corrupted during MATCH_FOUND (lapCount 0), so the
 * session's Init rejects it at BEGIN_SELECT: selectActive stays 0 and no
 * session is exposed, the next tick reads FAILED and shows LINK ERROR with
 * CLOSE_LINK (MS-8b), and nothing goes out on the aux route: B's link,
 * polled directly, receives only a control datagram A sends between the
 * BEGIN_SELECT tick and the failing tick. */
static int TestSelectCannotStart(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeLockstepPeerLink *linkA;
	struct NativeLockstepPeerLink *linkB;
	uint8_t control[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0u;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction action = ACT_NONE;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_NO_START_A_PORT, TEST_SELECT_NO_START_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK((actionB == ACT_NONE) || (actionB == ACT_BEGIN_SELECT));
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	for (tick = 0; (tick < DRIVE_BUDGET) && (LobbyStatusOf(&g_b) != (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY); tick++)
	{
		(void)NativeArcadeNetplay_Tick(&g_b, 0u, 0u);
	}
	/* B is not ticked again: its aux inbox is only read directly below. */
	linkA = NativeArcadeNetplay_Link(&g_a);
	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK(NativeLockstepPeerLink_Mode(linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	while (NativeLockstepPeerLink_TakeAux(linkB, bytes, sizeof(bytes), &size))
	{
		/* Nothing from A is expected before its select; start empty. */
	}

	g_a.currentConfig.lapCount = 0u;
	for (tick = 0; (tick < DRIVE_BUDGET) && (action != ACT_BEGIN_SELECT); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_BEGIN_SELECT));
	}
	CHECK(action == ACT_BEGIN_SELECT);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(g_a.selectActive == 0u);
	CHECK(g_a.selectSerial == 1u);
	CHECK(NativeArcadeNetplay_Select(&g_a) == NULL);

	/* A control datagram sent now, while the link is still open, must be the
	 * first and only one B receives: A sent nothing on the aux route on the
	 * BEGIN_SELECT tick, and sends nothing on the failing tick below. */
	CHECK(NativeArcadeNetplay_Link(&g_a) == linkA);
	memset(control, 0x5C, sizeof(control));
	CHECK(NativeLockstepPeerLink_SendAux(linkA, control, sizeof(control)) == 1);

	/* The next tick reads FAILED: LINK ERROR on the still healthy link, which
	 * is then closed (MS-8b). */
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_CLOSE_LINK);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(g_a.relinked == 0u);
	CHECK(g_a.outcomeValid == 0u);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	for (tick = 0; tick < 5u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	}

	for (tick = 0; (tick < DRIVE_BUDGET) && (NativeLockstepPeerLink_AuxCount(linkB) < 1u); tick++)
	{
		NativeLockstepPeerLink_Poll(linkB);
	}
	for (tick = 0; tick < 20u; tick++)
	{
		NativeLockstepPeerLink_Poll(linkB);
	}
	CHECK(NativeLockstepPeerLink_AuxCount(linkB) == 1u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(linkB) == 0u);
	CHECK(NativeLockstepPeerLink_TakeAux(linkB, bytes, sizeof(bytes), &size) == 1);
	CHECK(size == sizeof(control));
	CHECK(memcmp(bytes, control, sizeof(control)) == 0);

	ShutdownBoth();
	return 0;
}

/* 27. MS-7b: BACK (TRIANGLE) is ignored on SELECT and on SELECT_RESULT at
 * the adapter level. An armed press and then a held TRIANGLE return NONE
 * (never CLOSE_LINK), keep the screen and the link, and leave the local
 * selection exactly as it was; the select still runs on to START_RACE. */
static int TestSelectBackIgnored(void)
{
	static const uint32_t lockCharacter[1] = {BTN_CROSS};
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchSelectHumanState beforeA;
	struct NativeMatchSelectHumanState beforeB;
	const struct NativeMatchSelectHumanState *human;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &expected));
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_BACK_A_PORT, TEST_SELECT_BACK_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(PressScripts(lockCharacter, lockCharacter, 1u));
	beforeA = *NativeMatchSelectSession_Human(&g_a.select, 0u);
	beforeB = *NativeMatchSelectSession_Human(&g_b.select, 1u);
	CHECK(beforeA.lockMask == (uint8_t)NATIVE_MATCH_SELECT_LOCK_CHARACTER);
	CHECK(beforeB.lockMask == (uint8_t)NATIVE_MATCH_SELECT_LOCK_CHARACTER);

	/* SELECT: an armed TRIANGLE press, then TRIANGLE held. */
	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.menuArmed == 1u);
	CHECK(NativeArcadeNetplay_GetView(&g_b, &view) == 1);
	CHECK(view.menuArmed == 1u);
	for (tick = 0; tick < 4u; tick++)
	{
		TickBoth(BTN_TRIANGLE, BTN_TRIANGLE, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
		CHECK(NativeArcadeNetplay_Link(&g_a) != NULL);
		CHECK(NativeArcadeNetplay_Link(&g_b) != NULL);
		human = NativeMatchSelectSession_Human(&g_a.select, 0u);
		CHECK((human->lockMask == beforeA.lockMask) && (human->currentItem == beforeA.currentItem) &&
			(human->characterID == beforeA.characterID) && (human->trackID == beforeA.trackID) &&
			(human->lapCount == beforeA.lapCount));
		human = NativeMatchSelectSession_Human(&g_b.select, 1u);
		CHECK((human->lockMask == beforeB.lockMask) && (human->currentItem == beforeB.currentItem) &&
			(human->characterID == beforeB.characterID) && (human->trackID == beforeB.trackID) &&
			(human->lapCount == beforeB.lapCount));
	}
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);

	/* SELECT_RESULT, before RELINK: one released tick arms the new screen,
	 * then an armed TRIANGLE press and a held one. */
	CHECK(DriveBothToSelectResult());
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.menuArmed == 1u);
	CHECK(NativeArcadeNetplay_GetView(&g_b, &view) == 1);
	CHECK(view.menuArmed == 1u);
	for (tick = 0; tick < 2u; tick++)
	{
		TickBoth(BTN_TRIANGLE, BTN_TRIANGLE, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
		CHECK(g_a.relinked == 0u);
		CHECK(g_b.relinked == 0u);
		CHECK(NativeArcadeNetplay_Link(&g_a) != NULL);
		CHECK(NativeArcadeNetplay_Link(&g_b) != NULL);
	}

	/* The select runs on to the race as if nothing was pressed. */
	CHECK(DriveBothToRaceChecked());
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);

	ShutdownBoth();
	return 0;
}

/* 28. MS-7b: the old link is lost during SELECT. B sends A a corrupted
 * bundle over its real socket; A's lobby poll faults the link and reads
 * PEER_LOST, and the flow leaves SELECT for LINK ERROR on the lobby status
 * alone (A's select session is still PICKING, not FAILED), closing the
 * link (MS-8b). A never relinks or races, and RESULTS has no agreed
 * config. */
static int TestSelectOldLinkLost(void)
{
	struct NativeMatchConfigV1 fixture;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction action;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_LINK_LOST_A_PORT, TEST_SELECT_LINK_LOST_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	for (tick = 0; tick < 5u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}

	CHECK(SendCorruptBundle(&g_b, TEST_SELECT_LINK_LOST_A_PORT));
	CHECK(TickUntilLeft(&g_a, NATIVE_ARCADE_FLOW_SCREEN_SELECT, ACT_CLOSE_LINK));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	/* The flow left SELECT on the faulted link (PEER_LOST) and closed it. */
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(g_a.relinked == 0u);
	CHECK(g_a.outcomeValid == 0u);
	CHECK(NativeArcadeNetplay_Select(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);

	for (tick = 0; tick < 20u; tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK(action != ACT_START_RACE);
		CHECK(action != ACT_RELINK);
	}

	ShutdownBoth();
	return 0;
}

/* The whole select sub-view is zero. */
static int SelectViewIsZero(const struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeNetplayView view;
	struct NativeArcadeNetplaySelectView zero;

	memset(&view, 0xA5, sizeof(view));
	memset(&zero, 0, sizeof(zero));
	return NativeArcadeNetplay_GetView(netplay, &view) && (memcmp(&view.select, &zero, sizeof(zero)) == 0);
}

/* The select sub-view against the session it is read from, field by field. */
static int CheckSelectViewMatchesSession(const struct NativeArcadeNetplay *netplay, struct NativeArcadeNetplaySelectView *out)
{
	struct NativeArcadeNetplayView view;
	const struct NativeMatchSelectSession *session = NativeArcadeNetplay_Select(netplay);
	const struct NativeMatchSelectOutcome *outcome;
	const struct NativeMatchSelectHumanState *human;
	uint16_t mask = 0u;
	uint32_t h;
	uint32_t c;

	CHECK(session != NULL);
	memset(&view, 0xA5, sizeof(view));
	CHECK(NativeArcadeNetplay_GetView(netplay, &view) == 1);
	CHECK(view.select.active == 1u);
	CHECK(view.select.humanCount == 2u);
	CHECK(view.select.localHuman == (uint8_t)(netplay->config.localRole - 1u));
	CHECK(view.select.currentItem == (uint8_t)NativeMatchSelectSession_CurrentItem(session));
	CHECK(view.select.ticksLeft == NativeMatchSelectSession_TicksLeft(session));
	CHECK(view.select.status == (uint8_t)NativeMatchSelectSession_Status(session));
	outcome = NativeMatchSelectSession_Outcome(session);
	if (outcome == NULL)
	{
		CHECK(view.select.resolved == 0u);
		CHECK(view.select.trackID == 0u);
		CHECK(view.select.lapCount == 0u);
		CHECK(view.select.trackDrawn == 0u);
		CHECK(view.select.lapsDrawn == 0u);
		CHECK(view.select.characterReassignedMask == 0u);
		CHECK(view.select.botCount == 0u);
		for (h = 0u; h < NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS; h++)
		{
			CHECK(view.select.humanCharacter[h] == 0u);
		}
		for (c = 0u; c < NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS; c++)
		{
			CHECK(view.select.botCharacter[c] == 0u);
		}
	}
	else
	{
		CHECK(view.select.resolved == 1u);
		CHECK(view.select.trackID == outcome->trackID);
		CHECK(view.select.lapCount == outcome->lapCount);
		CHECK(view.select.trackDrawn == outcome->trackDrawn);
		CHECK(view.select.lapsDrawn == outcome->lapsDrawn);
		CHECK(view.select.characterReassignedMask == outcome->characterReassignedMask);
		CHECK(view.select.botCount == outcome->botCount);
		CHECK(memcmp(view.select.humanCharacter, outcome->humanCharacter, sizeof(view.select.humanCharacter)) == 0);
		CHECK(memcmp(view.select.botCharacter, outcome->botCharacter, sizeof(view.select.botCharacter)) == 0);
	}
	for (c = 0u; c < 8u; c++)
	{
		if (NativeMatchSelectSession_CharacterLockedByPeer(session, (uint8_t)c))
		{
			mask = (uint16_t)(mask | (1u << c));
		}
	}
	CHECK(view.select.peerLockedCharacterMask == mask);
	CHECK(view.select.reserved[0] == 0u);
	CHECK(view.select.reserved[1] == 0u);
	for (h = 0u; h < NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS; h++)
	{
		human = NativeMatchSelectSession_Human(session, h);
		if ((human == NULL) || (human->seen == 0u))
		{
			CHECK(view.select.humans[h].present == 0u);
			CHECK(view.select.humans[h].characterID == 0u);
			CHECK(view.select.humans[h].trackID == 0u);
			CHECK(view.select.humans[h].lapCount == 0u);
			CHECK(view.select.humans[h].lockMask == 0u);
			CHECK(view.select.humans[h].currentItem == 0u);
		}
		else
		{
			CHECK(view.select.humans[h].present == 1u);
			CHECK(view.select.humans[h].characterID == human->characterID);
			CHECK(view.select.humans[h].trackID == human->trackID);
			CHECK(view.select.humans[h].lapCount == human->lapCount);
			CHECK(view.select.humans[h].lockMask == human->lockMask);
			CHECK(view.select.humans[h].currentItem == human->currentItem);
		}
		CHECK(view.select.humans[h].reserved[0] == 0u);
		CHECK(view.select.humans[h].reserved[1] == 0u);
	}
	if (out != NULL)
	{
		*out = view.select;
	}
	return 0;
}

/*
 * 30. MS-8: the flat select view. Zero outside the select screens; during
 * SELECT it follows the session (the opponent's cursor and locks as they
 * arrive, the countdown, the peer-locked character mask); on SELECT_RESULT
 * the outcome fields are the resolved config the race then runs on.
 */
static int TestSelectView(void)
{
	/* B: NEXT, NEXT (cursor CORTEX -> 3), CONFIRM; A: NEXT (CRASH -> 1),
	 * CONFIRM. Then A: NEXT (track 6), CONFIRM, CONFIRM (laps 3); B: PREV
	 * (track wraps to 16), CONFIRM, CONFIRM (laps 3). */
	static const uint32_t cursorA[2] = {0u, 0u};
	static const uint32_t cursorB[2] = {BTN_DOWN, BTN_DOWN};
	static const uint32_t lockA[2] = {BTN_DOWN, BTN_CROSS};
	static const uint32_t lockB[2] = {BTN_CROSS, 0u};
	static const uint32_t restA[3] = {BTN_DOWN, BTN_CROSS, BTN_CROSS};
	static const uint32_t restB[3] = {BTN_UP, BTN_CROSS, BTN_CROSS};
	static const uint8_t characters[2] = {1u, 3u};
	static const uint8_t tracks[2] = {6u, 16u};
	static const uint8_t laps[2] = {3u, 3u};
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchSelectOutcome expectedOutcome;
	struct NativeArcadeNetplaySelectView viewA;
	struct NativeArcadeNetplaySelectView viewB;
	struct NativeArcadeNetplaySelectView resultA;
	const struct NativeMatchConfigV1 *agreed;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t ticksLeft;
	uint32_t tick;
	uint32_t slot;
	uint32_t bot = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, &expectedOutcome, &expected));

	CHECK(InitPair(&fixture, &fixture, TEST_SELECT_VIEW_A_PORT, TEST_SELECT_VIEW_B_PORT));
	CHECK(SelectViewIsZero(&g_a));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(SelectViewIsZero(&g_a));
	CHECK(DriveBothUntil(ACT_BEGIN_SELECT));
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);

	/* Both start on the fixture cursors; each has heard the other. */
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(CheckSelectViewMatchesSession(&g_b, &viewB) == 0);
	CHECK(viewA.localHuman == 0u);
	CHECK(viewB.localHuman == 1u);
	CHECK(viewA.currentItem == 0u);
	CHECK(viewA.status == (uint8_t)NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(viewA.resolved == 0u);
	CHECK(viewA.humans[0].present == 1u);
	CHECK(viewA.humans[0].characterID == 0u);
	CHECK(viewA.humans[0].trackID == FIXTURE_TRACK_CURSOR);
	CHECK(viewA.humans[0].lapCount == FIXTURE_LAP_CURSOR);
	CHECK(viewA.humans[1].present == 1u);
	CHECK(viewA.humans[1].characterID == 1u);
	CHECK(viewA.humans[1].lockMask == 0u);
	CHECK(viewA.humans[2].present == 0u);
	CHECK(viewA.humans[3].present == 0u);
	CHECK(viewA.peerLockedCharacterMask == 0u);
	CHECK((viewA.ticksLeft > 0u) && (viewA.ticksLeft < SELECT_ITEM_TICKS));

	/* The countdown runs one per tick. */
	ticksLeft = viewA.ticksLeft;
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(viewA.ticksLeft == ticksLeft - 1u);

	/* B's cursor moves: A's view of human 1 follows it step by step. */
	CHECK(PressScripts(cursorA, cursorB, 1u));
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(viewA.humans[1].characterID == 2u);
	CHECK(viewA.humans[1].lockMask == 0u);
	CHECK(PressScripts(&cursorA[1], &cursorB[1], 1u));
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(viewA.humans[1].characterID == 3u);
	CHECK(viewA.peerLockedCharacterMask == 0u);
	CHECK(CheckSelectViewMatchesSession(&g_b, &viewB) == 0);
	CHECK(viewB.humans[1].characterID == 3u);

	/* B locks 3; A moves to 1 and locks it. */
	CHECK(PressScripts(lockA, lockB, 1u));
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(viewA.humans[1].lockMask == 1u);
	CHECK(viewA.humans[1].currentItem == 1u);
	CHECK(viewA.peerLockedCharacterMask == (uint16_t)(1u << 3));
	CHECK(viewA.humans[0].characterID == 1u);
	CHECK(viewA.currentItem == 0u);
	CHECK(CheckSelectViewMatchesSession(&g_b, &viewB) == 0);
	CHECK(viewB.currentItem == 1u);
	CHECK(viewB.peerLockedCharacterMask == 0u);
	CHECK(PressScripts(&lockA[1], &lockB[1], 1u));
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(viewA.currentItem == 1u);
	CHECK(viewA.humans[0].lockMask == 1u);
	CHECK(CheckSelectViewMatchesSession(&g_b, &viewB) == 0);
	CHECK(viewB.peerLockedCharacterMask == (uint16_t)(1u << 1));
	CHECK(viewB.humans[0].characterID == 1u);
	CHECK(viewB.humans[0].lockMask == 1u);

	/* Track and laps on both. */
	CHECK(PressScripts(restA, restB, 1u));
	CHECK(CheckSelectViewMatchesSession(&g_a, &viewA) == 0);
	CHECK(viewA.humans[0].trackID == 6u);
	CHECK(viewA.humans[1].trackID == 16u);
	CHECK(PressScripts(&restA[1], &restB[1], 2u));

	/* Into SELECT_RESULT on both, checking the view on every tick. */
	for (tick = 0u; tick < DRIVE_BUDGET; tick++)
	{
		if ((ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) &&
			(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT))
		{
			break;
		}
		CHECK(CheckSelectViewMatchesSession(&g_a, NULL) == 0);
		CHECK(CheckSelectViewMatchesSession(&g_b, NULL) == 0);
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(tick < DRIVE_BUDGET);
	CHECK(CheckSelectViewMatchesSession(&g_a, &resultA) == 0);
	CHECK(CheckSelectViewMatchesSession(&g_b, &viewB) == 0);
	CHECK(resultA.status == (uint8_t)NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	CHECK(resultA.currentItem == (uint8_t)NATIVE_MATCH_SELECT_ITEM_DONE);
	CHECK(resultA.ticksLeft == 0u);
	CHECK(resultA.resolved == 1u);
	CHECK(resultA.trackID == expectedOutcome.trackID);
	CHECK((resultA.trackID == 6u) || (resultA.trackID == 16u));
	CHECK(resultA.trackDrawn == 1u);
	CHECK(resultA.lapCount == 3u);
	CHECK(resultA.lapsDrawn == 0u);
	CHECK(resultA.characterReassignedMask == 0u);
	CHECK(resultA.humanCharacter[0] == 1u);
	CHECK(resultA.humanCharacter[1] == 3u);
	CHECK(resultA.botCount == 4u);
	CHECK(memcmp(resultA.botCharacter, expectedOutcome.botCharacter, sizeof(resultA.botCharacter)) == 0);
	CHECK(resultA.humans[0].lockMask == 7u);
	CHECK(resultA.humans[1].lockMask == 7u);
	/* Both cabinets show the same outcome. */
	CHECK(viewB.resolved == 1u);
	CHECK(viewB.trackID == resultA.trackID);
	CHECK(viewB.lapCount == resultA.lapCount);
	CHECK(viewB.trackDrawn == resultA.trackDrawn);
	CHECK(viewB.lapsDrawn == resultA.lapsDrawn);
	CHECK(viewB.botCount == resultA.botCount);
	CHECK(memcmp(viewB.humanCharacter, resultA.humanCharacter, sizeof(resultA.humanCharacter)) == 0);
	CHECK(memcmp(viewB.botCharacter, resultA.botCharacter, sizeof(resultA.botCharacter)) == 0);

	/* The view survives RELINK through the rest of SELECT_RESULT, and the
	 * race runs on exactly the config the view described. */
	CHECK(DriveBothToRaceChecked());
	CHECK(SelectViewIsZero(&g_a));
	CHECK(SelectViewIsZero(&g_b));
	agreed = NativeArcadeNetplay_AgreedConfig(&g_a);
	CHECK(agreed != NULL);
	CHECK(memcmp(agreed, &expected, sizeof(expected)) == 0);
	CHECK(agreed->trackID == resultA.trackID);
	CHECK(agreed->lapCount == resultA.lapCount);
	CHECK(agreed->slots[g_a.localSlot].characterID == resultA.humanCharacter[0]);
	CHECK(agreed->slots[g_b.localSlot].characterID == resultA.humanCharacter[1]);
	for (slot = 0u; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		if (agreed->slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			CHECK(bot < resultA.botCount);
			CHECK(agreed->slots[slot].characterID == resultA.botCharacter[bot]);
			bot += 1u;
		}
	}
	CHECK(bot == resultA.botCount);

	ShutdownBoth();
	CHECK(SelectViewIsZero(&g_a));
	return 0;
}

/* TestSelectLinkErrorClosesLink: A ticks alone this long once both are on
 * SELECT_RESULT, which takes it past its hold and RELINK and leaves it a few
 * ticks short of its launch timeout, while B's hold is still running. Ticked
 * together again, A shows LINK ERROR just before B relinks, so B's relink
 * handshake reaches A while A is on RESULTS. With the lobby left open there
 * (before MS-8b), a sweep of this value reproduced the background READY for
 * 60 to 63 ticks: A answered B's handshake, replaced lastReadyConfig with the
 * resolved config behind the results screen, and B started a race alone. 62
 * sits inside that window. */
#define CLOSE_LINK_A_ALONE_TICKS 62u

/*
 * 31. MS-8b: a pre-race LINK ERROR closes the link. Both cabinets confirm
 * the select; B then stops ticking during the result hold, so A relinks on
 * the resolved config and nobody answers. Ticked together again, A shows
 * LINK ERROR at the launch timeout with CLOSE_LINK, so its lobby is closed
 * on RESULTS, and B then relinks on the same resolved config. B's relink
 * handshake toward A is never answered: nothing on A reaches READY or
 * replaces lastReadyConfig, B never races, and B times out to LINK ERROR
 * too. Both then REMATCH from the select base, and both reach READY on
 * byte-identical rematch configs.
 */
static int TestSelectLinkErrorClosesLink(void)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};
	struct NativeArcadeNetplayConfig config;
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 resolved;
	struct NativeMatchConfigV1 rematchBase;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	enum NativeArcadeFlowAction action;
	uint64_t rematchSeed = 0u;
	uint32_t tick;
	uint32_t closeTickA = 0u;
	uint32_t relinkTickB = 0u;
	int closedA = 0;
	int relinkedB = 0;
	int closedB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(ExpectedResolvedConfig(&fixture, characters, tracks, laps, 1u, NULL, &resolved));
	CHECK(memcmp(&resolved, &fixture, sizeof(fixture)) != 0);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &rematchSeed) == 1);
	CHECK(NativeLockstepRematch_BuildConfig(&fixture, rematchSeed, &rematchBase) == 1);

	/* The small cadence, but a results idle timeout long enough that A stays
	 * on RESULTS through B's whole relink attempt. */
	CHECK(MakeConfig(&config, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_SELECT_CLOSE_A_PORT,
		TEST_SELECT_CLOSE_B_PORT));
	config.timings.resultsIdleTimeoutTicks = DRIVE_BUDGET;
	CHECK(NativeArcadeNetplay_Init(&g_a, &config) == 1);
	CHECK(MakeConfig(&config, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, TEST_SELECT_CLOSE_B_PORT,
		TEST_SELECT_CLOSE_A_PORT));
	config.timings.resultsIdleTimeoutTicks = DRIVE_BUDGET;
	CHECK(NativeArcadeNetplay_Init(&g_b, &config) == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(DriveBothToSelectResult());

	/* A alone: its hold, RELINK on the resolved config, and an unanswered
	 * handshake, stopping short of the launch timeout. */
	for (tick = 0; tick < CLOSE_LINK_A_ALONE_TICKS; tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_RELINK) || (action == ACT_RESTART_LOBBY));
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
	CHECK(g_a.relinked == 1u);
	CHECK(memcmp(&g_a.currentConfig, &resolved, sizeof(resolved)) == 0);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
	CHECK(g_b.relinked == 0u);

	/* Both: A's launch timeout shows LINK ERROR and closes the link, then B
	 * relinks on the same resolved config and handshakes toward A. From the
	 * close on, A's lobby stays closed and nothing on either side reaches
	 * READY or replaces lastReadyConfig. */
	for (tick = 0; (tick < DRIVE_BUDGET) && ((ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) ||
												(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT));
		 tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA != ACT_START_RACE);
		CHECK(actionB != ACT_START_RACE);
		if (actionA == ACT_CLOSE_LINK)
		{
			CHECK(!closedA);
			CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
			CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
			closedA = 1;
			closeTickA = tick;
		}
		else if (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS)
		{
			CHECK(actionA == ACT_NONE);
		}
		else
		{
			CHECK((actionA == ACT_NONE) || (actionA == ACT_RESTART_LOBBY));
		}
		if (actionB == ACT_RELINK)
		{
			relinkedB = 1;
			relinkTickB = tick;
			CHECK(memcmp(&g_b.currentConfig, &resolved, sizeof(resolved)) == 0);
			CHECK(g_b.lobbyBegun == 1u);
		}
		if (actionB == ACT_CLOSE_LINK)
		{
			CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
			closedB = 1;
		}
		if (closedA)
		{
			CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
			CHECK(g_a.lobbyBegun == 0u);
			CHECK(g_a.lobbyReadySeen == 0u);
			CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
		}
		CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);
		/* B's relink lobby never reaches READY (before RELINK B still holds
		 * its old, READY link). */
		CHECK((relinkedB == 0) || (g_b.lobbyReadySeen == 0u));
		CHECK(memcmp(&g_b.lastReadyConfig, &fixture, sizeof(fixture)) == 0);
	}
	CHECK(closedA);
	CHECK(relinkedB);
	CHECK(closedB);
	/* The scenario: A had closed its link by the time B began its relink
	 * handshake toward it. */
	CHECK(closeTickA <= relinkTickB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_b.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	CHECK(g_a.matchCount == 0u);
	CHECK(g_b.matchCount == 0u);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_b) == NULL);

	/* Through B's results dwell, then both choose REMATCH: both derive from
	 * the select base, the last READY proposal on both sides. */
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(memcmp(&g_a.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);

	/* READY on the byte-identical rematch configs, then SELECT on them. */
	CHECK(DriveBothIntoSelect());
	CHECK(memcmp(&g_a.lastReadyConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(NativeMatchSelectSession_Base(NativeArcadeNetplay_Select(&g_a)), &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(NativeMatchSelectSession_Base(NativeArcadeNetplay_Select(&g_b)), &rematchBase, sizeof(rematchBase)) == 0);

	ShutdownBoth();
	return 0;
}

/*
 * The view's localMenuEvent (presentation only, for menu sounds): the event
 * the most recent Tick consumed from this adapter's own buttons, and NONE on
 * every tick without a new armed edge, on the first tick after a screen
 * change (release-to-arm), on screen OFF, after Init, Enter, and Shutdown,
 * and whatever the peer presses.
 */
static int TestLocalMenuEvent(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayConfig config;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction action;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint8_t peerCharacter;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(MakeConfig(&config, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_MENU_EVENT_A_PORT,
		TEST_MENU_EVENT_DEAD_PORT));

	/* A fresh Init reports NONE, whatever the struct held. */
	memset(&g_a, 0xA5, sizeof(g_a));
	CHECK(NativeArcadeNetplay_Init(&g_a, &config) == 1);
	CHECK(g_a.lastMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* Screen OFF: no menu event is consumed. */
	CHECK(NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 0u) == ACT_NONE);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* LOBBY, connecting to a dead port. The first tick only arms. */
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	(void)NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(view.menuArmed == 1u);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(view.reserved == 0u);

	/* A rising NEXT is reported on its own tick only: held and released
	 * ticks report NONE. The lobby ignores NEXT, PREV, and CONFIRM while
	 * connecting, so the screen stays LOBBY throughout. */
	(void)NativeArcadeNetplay_Tick(&g_a, BTN_DOWN, 0u);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NEXT);
	(void)NativeArcadeNetplay_Tick(&g_a, BTN_DOWN, 0u);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	(void)NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	(void)NativeArcadeNetplay_Tick(&g_a, BTN_UP, 0u);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_PREV);
	(void)NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 0u);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM);
	(void)NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_LOBBY);

	/* BACK leaves the lobby and is reported on that tick. */
	CHECK(NativeArcadeNetplay_Tick(&g_a, BTN_TRIANGLE, 0u) == ACT_CLOSE_LINK);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_BACK);

	/* The first tick on the new screen re-arms: a fresh CROSS press there
	 * is not an event. */
	(void)NativeArcadeNetplay_Tick(&g_a, BTN_CROSS, 0u);
	CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
	CHECK(view.menuArmed == 0u);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* Through the exit hold to the title. */
	for (tick = 0u; tick < DRIVE_BUDGET; tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
		if (action == ACT_RETURN_TO_TITLE)
		{
			break;
		}
		CHECK(action == ACT_NONE);
	}
	CHECK(tick < DRIVE_BUDGET);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);

	/* A Tick on screen OFF clears an event left by the tick that reached
	 * it, and changes nothing else. */
	g_a.lastMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_BACK;
	memcpy(&g_sentinel, &g_a, sizeof(g_a));
	g_sentinel.lastMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE;
	CHECK(NativeArcadeNetplay_Tick(&g_a, BTN_TRIANGLE, 0u) == ACT_NONE);
	CHECK(memcmp(&g_a, &g_sentinel, sizeof(g_a)) == 0);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* Enter, Shutdown, and Init each clear it. */
	g_a.lastMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM;
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	g_a.lastMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_NEXT;
	NativeArcadeNetplay_Shutdown(&g_a);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	g_a.lastMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_PREV;
	CHECK(NativeArcadeNetplay_Init(&g_a, &config) == 1);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	NativeArcadeNetplay_Shutdown(&g_a);

	/* A real pair on SELECT: each view reports only its own presses. */
	CHECK(InitPair(&fixture, &fixture, TEST_MENU_EVENT_PAIR_A_PORT, TEST_MENU_EVENT_PAIR_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(MenuEventOf(&g_b) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	peerCharacter = NativeMatchSelectSession_Human(&g_b.select, 1u)->characterID;

	/* B steps its cursor: B reports NEXT, A reports NONE. */
	TickBoth(0u, BTN_DOWN, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(MenuEventOf(&g_b) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NEXT);
	CHECK(NativeMatchSelectSession_Human(&g_b.select, 1u)->characterID != peerCharacter);
	peerCharacter = NativeMatchSelectSession_Human(&g_b.select, 1u)->characterID;

	/* A hears B's new cursor within a bounded wait (well inside the select
	 * item and peer-silence timeouts), and reports NONE throughout. */
	for (tick = 0u; tick < 60u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
		CHECK(MenuEventOf(&g_b) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
		CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
		if (view.select.humans[1].characterID == peerCharacter)
		{
			break;
		}
	}
	CHECK(tick < 60u);

	/* A confirms: A reports CONFIRM, B reports NONE; held, both NONE. */
	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM);
	CHECK(MenuEventOf(&g_b) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(MenuEventOf(&g_a) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(MenuEventOf(&g_b) == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	ShutdownBoth();
	return 0;
}

/*
 * RL-S5: the launch agreement (docs/RACE_LAUNCH_MILESTONE.md RL-1, RL-3,
 * RL-4, RL-6, RL-7). START_RACE needs the relink READY plus a launch commit:
 * a launch record from the other role on the digest of this cabinet's own
 * relink proposal, taken while the flow is in SELECT_RESULT phase 2.
 *
 * Most cases run A against a hand-driven test peer: once both cabinets have
 * confirmed the select, B is shut down (its socket closes) and g_peer, a
 * real peer link bound to B's port, proposes A's relink config as CAB2. Its
 * HELLO goes out at Open and it is never polled, so its own handshake never
 * completes: every launch record A sees is one the test chose to send or
 * inject. Records that must land on an exact tick are put straight into A's
 * aux inbox (InjectAux), as the link's Poll would while RUNNING.
 */
#define ROLE_CAB1 ((uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN)
#define ROLE_CAB2 ((uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN)
#define LAUNCH_BYTES NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES
#define LAUNCH_DIGEST_BYTES NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES
#define FLAG_HEARD ((uint8_t)NATIVE_ARCADE_LAUNCH_FLAG_HEARD)
#define LAUNCH_PENDING ((uint32_t)NATIVE_ARCADE_LAUNCH_PENDING)
#define LAUNCH_COMMITTED ((uint32_t)NATIVE_ARCADE_LAUNCH_COMMITTED)
/* Large enough for every datagram these tests receive (284 at most). */
#define RECEIVE_BYTES 512u
/* Ticks a stale record is given to (wrongly) commit before the positive
 * control. */
#define STALE_WATCH_TICKS 5u

static struct NativeLockstepPeerLink g_peer;
/* A bare socket on B's port, for sends while neither B nor the peer holds it. */
static struct NativeUdpTransport g_raw;

static int ComposeLaunchRecord(uint8_t role, const uint8_t *digest, uint8_t flags, uint32_t sequence,
	uint8_t out[LAUNCH_BYTES])
{
	struct NativeArcadeLaunchRecordV1 record;
	struct NativeCodecWriter writer;

	memset(&record, 0, sizeof(record));
	record.senderRole = role;
	record.flags = flags;
	record.sequence = sequence;
	memcpy(record.configDigest, digest, sizeof(record.configDigest));
	NativeCodecWriter_Init(&writer, out, LAUNCH_BYTES, NULL);
	return NativeArcadeLaunchRecordV1_Encode(&writer, &record) && (NativeCodecWriter_Size(&writer) == LAUNCH_BYTES);
}

static int SendTo(struct NativeUdpTransport *via, uint32_t toPort, const uint8_t *bytes, size_t size)
{
	struct NativeUdpTransportAddress to;

	return NativeUdpTransport_MakeAddress(&to, "127.0.0.1", (uint16_t)toPort) &&
		(NativeUdpTransport_Send(via, &to, bytes, size) != 0);
}

/* One CAB2 launch record on digest, from the test peer's socket to toPort. */
static int PeerSendLaunch(uint32_t toPort, const uint8_t *digest, uint8_t flags, uint32_t sequence)
{
	uint8_t bytes[LAUNCH_BYTES];

	return ComposeLaunchRecord(ROLE_CAB2, digest, flags, sequence, bytes) &&
		SendTo(&g_peer.transport, toPort, bytes, sizeof(bytes));
}

/* Appends one datagram to the back of netplay's open aux inbox, as the
 * link's Poll does while RUNNING; 0 when no link is open or it is full. */
static int InjectAux(struct NativeArcadeNetplay *netplay, const uint8_t *bytes)
{
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(netplay);
	uint32_t tail;

	if ((link == NULL) || (link->auxCount >= NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY))
	{
		return 0;
	}
	tail = (link->auxHead + link->auxCount) % NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY;
	memcpy(link->auxBytes[tail], bytes, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES);
	link->auxCount += 1u;
	return 1;
}

static int InjectLaunch(struct NativeArcadeNetplay *netplay, uint8_t role, const uint8_t *digest, uint8_t flags,
	uint32_t sequence)
{
	uint8_t bytes[LAUNCH_BYTES];

	return ComposeLaunchRecord(role, digest, flags, sequence, bytes) && InjectAux(netplay, bytes);
}

/* Makes netplay's agreement an active, COMMITTED one on digest (a stale
 * agreement left behind), for the reset-point cases. */
static int PrimeAgreement(struct NativeArcadeNetplay *netplay, const uint8_t *digest)
{
	uint8_t bytes[LAUNCH_BYTES];
	uint8_t other = (netplay->config.localRole == ROLE_CAB1) ? ROLE_CAB2 : ROLE_CAB1;

	return NativeArcadeLaunch_Begin(&netplay->launch, netplay->config.localRole, digest,
			   NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS) &&
		ComposeLaunchRecord(other, digest, FLAG_HEARD, 1u, bytes) &&
		(NativeArcadeLaunch_Accept(&netplay->launch, bytes, sizeof(bytes)) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED) &&
		(NativeArcadeLaunch_Status(&netplay->launch) == LAUNCH_COMMITTED);
}

/* The agreement is inactive and all zero (NativeArcadeLaunch_Reset). */
static int AgreementIsReset(const struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeLaunchAgreement zero;

	memset(&zero, 0, sizeof(zero));
	return (NativeArcadeLaunch_Active(&netplay->launch) == 0) && (memcmp(&netplay->launch, &zero, sizeof(zero)) == 0);
}

static uint32_t AuxCountOf(struct NativeArcadeNetplay *netplay)
{
	return NativeLockstepPeerLink_AuxCount(NativeArcadeNetplay_Link(netplay));
}

/* Takes every datagram waiting on transport, spinning until at least want of
 * exactly size bytes were taken (datagrams already sent over loopback), then
 * until it reads empty. Everything taken is discarded; returns how many were
 * size bytes. */
static uint32_t DrainSized(struct NativeUdpTransport *transport, size_t size, uint32_t want)
{
	uint8_t bytes[RECEIVE_BYTES];
	size_t byteCount = 0u;
	uint32_t taken = 0u;
	uint32_t spins;
	enum NativeUdpTransportReceiveResult result;

	for (spins = 0u; spins < RECEIVE_SPIN_BUDGET; spins++)
	{
		result = NativeUdpTransport_Receive(transport, bytes, sizeof(bytes), &byteCount, NULL);
		if (result == NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			taken += (byteCount == size) ? 1u : 0u;
		}
		else if ((result == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY) && (taken >= want))
		{
			break;
		}
	}
	return taken;
}

/* Takes every datagram waiting on transport like DrainSized, counting the
 * launch records from CAB1 on digest that carry HEARD. */
static uint32_t DrainHeardRecords(struct NativeUdpTransport *transport, const uint8_t *digest, uint32_t want)
{
	struct NativeArcadeLaunchRecordV1 record;
	struct NativeCodecReader reader;
	uint8_t bytes[RECEIVE_BYTES];
	size_t byteCount = 0u;
	uint32_t heard = 0u;
	uint32_t spins;
	enum NativeUdpTransportReceiveResult result;

	for (spins = 0u; spins < RECEIVE_SPIN_BUDGET; spins++)
	{
		result = NativeUdpTransport_Receive(transport, bytes, sizeof(bytes), &byteCount, NULL);
		if ((result == NATIVE_UDP_TRANSPORT_RECEIVE_OK) && (byteCount == LAUNCH_BYTES))
		{
			NativeCodecReader_Init(&reader, bytes, byteCount);
			if (NativeArcadeLaunchRecordV1_Decode(&reader, &record, NULL) && (record.senderRole == ROLE_CAB1) &&
				((record.flags & FLAG_HEARD) != 0u) &&
				(memcmp(record.configDigest, digest, sizeof(record.configDigest)) == 0))
			{
				heard += 1u;
			}
		}
		else if ((result == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY) && (heard >= want))
		{
			break;
		}
	}
	return heard;
}

/* The drop filter: takes every datagram waiting on to's link socket,
 * spinning until at least dropCount launch-width datagrams were taken, drops
 * those, and sends every other one (a handshake message) again from from's
 * link socket, so to still receives it, in order. */
static int DropLaunchRecordsTo(struct NativeArcadeNetplay *to, uint32_t toPort, struct NativeArcadeNetplay *from,
	uint32_t dropCount)
{
	struct NativeLockstepPeerLink *toLink = NativeArcadeNetplay_Link(to);
	struct NativeLockstepPeerLink *fromLink = NativeArcadeNetplay_Link(from);
	uint8_t kept[4][RECEIVE_BYTES];
	size_t keptSize[4];
	uint8_t bytes[RECEIVE_BYTES];
	size_t byteCount = 0u;
	uint32_t keptCount = 0u;
	uint32_t dropped = 0u;
	uint32_t spins;
	uint32_t i;
	enum NativeUdpTransportReceiveResult result;

	if ((toLink == NULL) || (fromLink == NULL))
	{
		return 0;
	}
	for (spins = 0u; spins < RECEIVE_SPIN_BUDGET; spins++)
	{
		result = NativeUdpTransport_Receive(&toLink->transport, bytes, sizeof(bytes), &byteCount, NULL);
		if (result == NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			if (byteCount == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES)
			{
				dropped += 1u;
			}
			else
			{
				if (keptCount >= 4u)
				{
					return 0;
				}
				memcpy(kept[keptCount], bytes, byteCount);
				keptSize[keptCount] = byteCount;
				keptCount += 1u;
			}
		}
		else if ((result == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY) && (dropped >= dropCount))
		{
			break;
		}
	}
	if (dropped < dropCount)
	{
		return 0;
	}
	for (i = 0u; i < keptCount; i++)
	{
		if (!SendTo(&fromLink->transport, toPort, kept[i], keptSize[i]))
		{
			return 0;
		}
	}
	return 1;
}

/* Ticks A alone to RELINK (NONE before it): the relink is on expected, the
 * agreement is reset, and A's fresh link is HANDSHAKING with an empty inbox. */
static int AloneToRelink(const struct NativeMatchConfigV1 *expected)
{
	enum NativeArcadeFlowAction action = ACT_NONE;
	struct NativeLockstepPeerLink *link;
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && (action != ACT_RELINK); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		if ((action != ACT_NONE) && (action != ACT_RELINK))
		{
			return 0;
		}
	}
	link = NativeArcadeNetplay_Link(&g_a);
	return (action == ACT_RELINK) && (memcmp(&g_a.currentConfig, expected, sizeof(*expected)) == 0) &&
		AgreementIsReset(&g_a) && (link != NULL) &&
		(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING) &&
		(NativeLockstepPeerLink_AuxCount(link) == 0u);
}

/*
 * A is HANDSHAKING in phase 2 on expected. The test peer opens on portB
 * proposing expected as CAB2 (its HELLO goes out at Open). With staleDigest,
 * the peer then sends a stale HEARD launch record on it, queued behind that
 * HELLO: a record still in the transport when A's new link comes up. A's
 * link is polled directly until it is RUNNING (and, with a stale record,
 * until the record is in its inbox), so the record is taken with the
 * handshake completion deterministically; A's next Tick observes the relink
 * READY. On success A is READY in phase 2 with a fresh, active, PENDING
 * agreement on the digest of expected that has accepted nothing, and an
 * empty inbox.
 */
static int PeerUpToReady(uint32_t portA, uint32_t portB, const struct NativeMatchConfigV1 *expected,
	const uint8_t *staleDigest)
{
	struct NativeUdpTransportAddress aAddress;
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(&g_a);
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint32_t spins;

	if ((link == NULL) || !NativeMatchConfigV1_Digest(expected, digest) ||
		!NativeUdpTransport_MakeAddress(&aAddress, "127.0.0.1", (uint16_t)portA) ||
		!NativeLockstepPeerLink_Open(&g_peer, (uint16_t)portB, &aAddress, expected, ROLE_CAB2,
			NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY))
	{
		return 0;
	}
	if ((staleDigest != NULL) && !PeerSendLaunch(portA, staleDigest, FLAG_HEARD, 7u))
	{
		return 0;
	}
	for (spins = 0u; spins < RECEIVE_SPIN_BUDGET; spins++)
	{
		if ((NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING) &&
			((staleDigest == NULL) || (NativeLockstepPeerLink_AuxCount(link) >= 1u)))
		{
			break;
		}
		NativeLockstepPeerLink_Poll(link);
	}
	if ((NativeLockstepPeerLink_Mode(link) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING) ||
		((staleDigest != NULL) && (NativeLockstepPeerLink_AuxCount(link) < 1u)) || NativeArcadeLaunch_Active(&g_a.launch))
	{
		return 0;
	}
	if (NativeArcadeNetplay_Tick(&g_a, 0u, 0u) != ACT_NONE)
	{
		return 0;
	}
	return (LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY) &&
		(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) && (g_a.relinked == 1u) &&
		NativeArcadeLaunch_Active(&g_a.launch) && (NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING) &&
		(g_a.launch.acceptedCount == 0u) && (g_a.launch.localRole == ROLE_CAB1) &&
		(g_a.launch.lingerTicks == NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS) &&
		(memcmp(g_a.launch.configDigest, digest, sizeof(digest)) == 0) && (NativeLockstepPeerLink_AuxCount(link) == 0u);
}

/* From both on SELECT_RESULT phase 1 with the select confirmed: B shuts
 * down, A relinks alone on expected, and the test peer brings A's relink
 * lobby to READY (PeerUpToReady). */
static int RelinkAloneToReady(uint32_t portA, uint32_t portB, const struct NativeMatchConfigV1 *expected,
	const uint8_t *staleDigest)
{
	NativeArcadeNetplay_Shutdown(&g_b);
	return AloneToRelink(expected) && PeerUpToReady(portA, portB, expected, staleDigest);
}

/* A, READY in phase 2 with nothing from the peer, stays PENDING: NONE,
 * nothing accepted, still on SELECT_RESULT. */
static int ExpectPendingTicks(uint32_t ticks)
{
	uint32_t tick;

	for (tick = 0; tick < ticks; tick++)
	{
		if ((NativeArcadeNetplay_Tick(&g_a, 0u, 0u) != ACT_NONE) || !NativeArcadeLaunch_Active(&g_a.launch) ||
			(NativeArcadeLaunch_Status(&g_a.launch) != LAUNCH_PENDING) || (g_a.launch.acceptedCount != 0u) ||
			(ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) ||
			(LobbyStatusOf(&g_a) != (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY))
		{
			return 0;
		}
	}
	return 1;
}

/* The positive control: the test peer sends one PENDING launch record on
 * digest, and A commits and starts the race on expected within a few ticks. */
static int CommitFromPeer(uint32_t portA, const uint8_t *digest, const struct NativeMatchConfigV1 *expected)
{
	enum NativeArcadeFlowAction action = ACT_NONE;
	const struct NativeMatchConfigV1 *agreed;
	uint32_t tick;

	if (!PeerSendLaunch(portA, digest, 0u, 1u))
	{
		return 0;
	}
	for (tick = 0; (tick < 10u) && (action != ACT_START_RACE); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		if ((action != ACT_NONE) && (action != ACT_START_RACE))
		{
			return 0;
		}
	}
	agreed = NativeArcadeNetplay_AgreedConfig(&g_a);
	return (action == ACT_START_RACE) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING) &&
		(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED) && (agreed != NULL) &&
		(memcmp(agreed, expected, sizeof(*expected)) == 0) &&
		(memcmp(&g_a.lastReadyConfig, expected, sizeof(*expected)) == 0);
}

static void ClosePeer(void)
{
	NativeLockstepPeerLink_Close(&g_peer);
}

/* The idle select's resolved config on base for select serial. */
static int IdleResolved(const struct NativeMatchConfigV1 *base, uint32_t selectSerial, struct NativeMatchConfigV1 *out)
{
	static const uint8_t characters[2] = {0u, 1u};
	static const uint8_t tracks[2] = {FIXTURE_TRACK_CURSOR, FIXTURE_TRACK_CURSOR};
	static const uint8_t laps[2] = {FIXTURE_LAP_CURSOR, FIXTURE_LAP_CURSOR};

	return ExpectedResolvedConfig(base, characters, tracks, laps, selectSerial, NULL, out);
}

/* Init, Enter, and an idle select up to SELECT_RESULT phase 1 on both. */
static int PairToSelectResult(const struct NativeMatchConfigV1 *fixture, uint32_t portA, uint32_t portB)
{
	return InitPair(fixture, fixture, portA, portB) && (NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY) &&
		(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY) && DriveBothIntoSelect() && DriveBothToSelectResult();
}

/* Ticks one adapter alone until it returns RELINK; NONE before it. */
static int TickAloneToRelink(struct NativeArcadeNetplay *netplay)
{
	enum NativeArcadeFlowAction action = ACT_NONE;
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && (action != ACT_RELINK); tick++)
	{
		action = NativeArcadeNetplay_Tick(netplay, 0u, 0u);
		if ((action != ACT_NONE) && (action != ACT_RELINK))
		{
			return 0;
		}
	}
	return action == ACT_RELINK;
}

/*
 * 33a. RL-S5 (a): the symmetric launch. Neither cabinet starts on its relink
 * READY alone: each has at least one tick READY with the agreement PENDING
 * (the READY tick itself, whose inbox is discarded), a relink READY never
 * takes lastReadyConfig (RL-6), and each START_RACE comes with a commit on
 * the digest of the resolved config. Both agreed configs are byte-equal, and
 * the linger stops once each side has sent a HEARD record and received one.
 */
static int TestLaunchSymmetric(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeArcadeNetplay *sides[2] = {&g_a, &g_b};
	enum NativeArcadeFlowAction actions[2];
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	int started[2] = {0, 0};
	int readyPending[2] = {0, 0};
	uint32_t sequences[2];
	uint32_t tick;
	uint32_t s;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(InitPair(&fixture, &fixture, TEST_LAUNCH_SYM_A_PORT, TEST_LAUNCH_SYM_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());

	for (tick = 0; (tick < DRIVE_BUDGET) && !(started[0] && started[1]); tick++)
	{
		TickBoth(0u, 0u, 0u, &actions[0], &actions[1]);
		for (s = 0; s < 2u; s++)
		{
			struct NativeArcadeNetplay *side = sides[s];

			CHECK((actions[s] != ACT_CLOSE_LINK) && (actions[s] != ACT_RETURN_TO_TITLE));
			if (started[s])
			{
				continue;
			}
			if (actions[s] == ACT_START_RACE)
			{
				CHECK(readyPending[s]);
				CHECK(ScreenOf(side) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
				CHECK(NativeArcadeLaunch_Status(&side->launch) == LAUNCH_COMMITTED);
				CHECK(side->launch.acceptedCount >= 1u);
				CHECK(memcmp(side->launch.configDigest, digest, sizeof(digest)) == 0);
				/* RL-6: taken on the START_RACE the commit gates. */
				CHECK(memcmp(&side->lastReadyConfig, &expected, sizeof(expected)) == 0);
				started[s] = 1;
				continue;
			}
			CHECK(memcmp(&side->lastReadyConfig, &fixture, sizeof(fixture)) == 0);
			CHECK(NativeArcadeLaunch_Status(&side->launch) == LAUNCH_PENDING);
			/* The relink lobby has been READY (the flow's own lobby status on
			 * the RELINK tick is still the old link's). */
			if ((ScreenOf(side) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) && (side->relinked != 0u) &&
				(side->lobbyReadySeen != 0u))
			{
				CHECK(LobbyStatusOf(side) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
				CHECK(NativeArcadeLaunch_Active(&side->launch));
				CHECK(memcmp(side->launch.configDigest, digest, sizeof(digest)) == 0);
				readyPending[s] = 1;
			}
			else
			{
				/* Only a relink READY begins an agreement. */
				CHECK(!NativeArcadeLaunch_Active(&side->launch));
			}
		}
	}
	CHECK(started[0] && started[1]);
	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK((agreedA != NULL) && (agreedB != NULL));
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(memcmp(agreedA, &expected, sizeof(expected)) == 0);

	/* The linger ends once each has sent and received a HEARD record. */
	for (tick = 0; tick < 10u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actions[0], &actions[1]);
		CHECK((actions[0] == ACT_NONE) && (actions[1] == ACT_NONE));
	}
	for (s = 0; s < 2u; s++)
	{
		CHECK(sides[s]->launch.peerHeard == 1u);
		CHECK(sides[s]->launch.heardSent == 1u);
		CHECK(NativeArcadeLaunch_ShouldSend(&sides[s]->launch) == 0);
		CHECK(sides[s]->launch.mismatchCount == 0u);
		CHECK(sides[s]->launch.selfCount == 0u);
		CHECK(sides[s]->launch.malformedCount == 0u);
		sequences[s] = sides[s]->launch.sequence;
	}
	for (tick = 0; tick < 3u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actions[0], &actions[1]);
	}
	CHECK(g_a.launch.sequence == sequences[0]);
	CHECK(g_b.launch.sequence == sequences[1]);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);

	ShutdownBoth();
	CHECK(AgreementIsReset(&g_a));
	CHECK(AgreementIsReset(&g_b));
	return 0;
}

/*
 * 33b. RL-S5 (b), RL-6: a relink that completes on one side only. A relinks
 * first (its HELLO reaches B's old, already complete link: a no-op), then B;
 * one tick of A sends its HELLO toward B's new link and completes A on B's
 * HELLO. That HELLO is taken out of B's socket, and A, now RUNNING, never
 * sends another, so B's relink never completes. A sits READY and PENDING, B
 * never begins an agreement, and neither launches: both show LINK ERROR
 * with CLOSE_LINK. The relink READY on A took no lastReadyConfig, so both
 * rematch from the select base, agree, and race.
 */
static int TestLaunchOneSidedRelink(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 resolved;
	struct NativeMatchConfigV1 rematchBase;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint64_t rematchSeed = 0u;
	uint32_t tick;
	int closedA = 0;
	int closedB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &resolved));
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &rematchSeed) == 1);
	CHECK(NativeLockstepRematch_BuildConfig(&fixture, rematchSeed, &rematchBase) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_ONE_SIDED_A_PORT, TEST_LAUNCH_ONE_SIDED_B_PORT));

	CHECK(TickAloneToRelink(&g_a));
	CHECK(TickAloneToRelink(&g_b));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(NativeArcadeLaunch_Active(&g_a.launch));
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING);
	CHECK(DrainSized(&NativeArcadeNetplay_Link(&g_b)->transport, NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES, 1u) >= 1u);

	for (tick = 0; (tick < DRIVE_BUDGET) && ((ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) ||
												(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT));
		 tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK((actionA == ACT_NONE) || (actionA == ACT_CLOSE_LINK));
		CHECK((actionB == ACT_NONE) || (actionB == ACT_RESTART_LOBBY) || (actionB == ACT_CLOSE_LINK));
		closedA = closedA || (actionA == ACT_CLOSE_LINK);
		closedB = closedB || (actionB == ACT_CLOSE_LINK);
		if (!closedA)
		{
			/* A's lobby stays READY, its agreement PENDING: no record arrives. */
			CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
			CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING);
			CHECK(g_a.launch.acceptedCount == 0u);
		}
		/* B's relink lobby never reaches READY and never begins an agreement. */
		CHECK(g_b.lobbyReadySeen == 0u);
		CHECK(!NativeArcadeLaunch_Active(&g_b.launch));
	}
	CHECK(closedA && closedB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(AgreementIsReset(&g_a));
	CHECK(AgreementIsReset(&g_b));
	CHECK(g_a.matchCount == 0u);
	CHECK(g_b.matchCount == 0u);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_b) == NULL);
	/* RL-6: both still hold the select base. */
	CHECK(memcmp(&g_a.currentConfig, &resolved, sizeof(resolved)) == 0);
	CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &fixture, sizeof(fixture)) == 0);

	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK((actionA == ACT_NONE) && (actionB == ACT_NONE));
	}
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(memcmp(&g_a.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(DriveBothIntoSelect());
	CHECK(memcmp(&g_a.lastReadyConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &rematchBase, sizeof(rematchBase)) == 0);
	CHECK(DriveBothToRaceChecked());
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), NativeArcadeNetplay_AgreedConfig(&g_b),
			  sizeof(struct NativeMatchConfigV1)) == 0);
	CHECK(g_a.matchCount == 1u);
	CHECK(g_b.matchCount == 1u);

	ShutdownBoth();
	return 0;
}

/*
 * 33c. RL-S5 (c), RL-7: a lost last record. Both relinks complete; A's first
 * launch record is dropped before B's completing poll, and every later one
 * before B's next tick, so B never hears A. B's records reach A, which
 * commits and races alone (its lastReadyConfig becomes the resolved config,
 * RL-6); B stays PENDING and times out to LINK ERROR. The rematch fails as
 * RL-7 traces it: A proposes from the resolved config and B from the select
 * base, the handshake rejects the differing proposals, and both show
 * OPPONENT LEFT, then return to the title.
 */
static int TestLaunchLostLastRecord(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 resolved;
	struct NativeMatchConfigV1 rematchA;
	struct NativeMatchConfigV1 rematchB;
	enum NativeArcadeFlowAction actionA = ACT_NONE;
	enum NativeArcadeFlowAction actionB = ACT_NONE;
	uint64_t seed = 0u;
	uint32_t lastSequenceA;
	uint32_t tick;
	int startedA = 0;
	int closedB = 0;
	int exitedA = 0;
	int exitedB = 0;
	int rejectedA = 0;
	int rejectedB = 0;
	int titleA = 0;
	int titleB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &resolved));
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&resolved, &seed) == 1);
	CHECK(NativeLockstepRematch_BuildConfig(&resolved, seed, &rematchA) == 1);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &seed) == 1);
	CHECK(NativeLockstepRematch_BuildConfig(&fixture, seed, &rematchB) == 1);
	CHECK(memcmp(&rematchA, &rematchB, sizeof(rematchA)) != 0);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_LOST_A_PORT, TEST_LAUNCH_LOST_B_PORT));

	CHECK(TickAloneToRelink(&g_a));
	CHECK(TickAloneToRelink(&g_b));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(g_a.launch.sequence == 1u);
	/* B's socket holds A's HELLO and A's first record: keep the HELLO only. */
	CHECK(DropLaunchRecordsTo(&g_b, TEST_LAUNCH_LOST_B_PORT, &g_a, 1u));
	lastSequenceA = g_a.launch.sequence;

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT); tick++)
	{
		actionB = NativeArcadeNetplay_Tick(&g_b, 0u, 0u);
		CHECK((actionB == ACT_NONE) || (actionB == ACT_CLOSE_LINK));
		closedB = closedB || (actionB == ACT_CLOSE_LINK);
		if (!closedB)
		{
			CHECK(LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
			CHECK(NativeArcadeLaunch_Status(&g_b.launch) == LAUNCH_PENDING);
			CHECK(g_b.launch.acceptedCount == 0u);
		}
		actionA = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		if (actionA == ACT_START_RACE)
		{
			CHECK(!startedA);
			CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
			CHECK(memcmp(&g_a.lastReadyConfig, &resolved, sizeof(resolved)) == 0);
			startedA = 1;
		}
		else
		{
			CHECK(actionA == ACT_NONE);
		}
		/* Every record A sent this tick is dropped before B's next tick. */
		if (g_b.lobbyBegun != 0u)
		{
			CHECK(DropLaunchRecordsTo(&g_b, TEST_LAUNCH_LOST_B_PORT, &g_a, g_a.launch.sequence - lastSequenceA));
		}
		lastSequenceA = g_a.launch.sequence;
	}
	CHECK(startedA);
	CHECK(closedB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(AgreementIsReset(&g_b));
	CHECK(g_a.matchCount == 1u);
	CHECK(g_b.matchCount == 0u);
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &resolved, sizeof(resolved)) == 0);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_b) == NULL);
	/* B never committed, so A never hears HEARD and is still lingering. */
	CHECK(g_a.launch.peerHeard == 0u);
	CHECK(memcmp(&g_a.lastReadyConfig, &resolved, sizeof(resolved)) == 0);
	CHECK(memcmp(&g_b.lastReadyConfig, &fixture, sizeof(fixture)) == 0);

	/* A's rehearsal race finishes; both dwell, then both choose REMATCH. */
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 1u) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK((actionA == ACT_NONE) && (actionB == ACT_NONE));
	}
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(AgreementIsReset(&g_a));
	CHECK(memcmp(&g_a.currentConfig, &rematchA, sizeof(rematchA)) == 0);
	CHECK(memcmp(&g_b.currentConfig, &rematchB, sizeof(rematchB)) == 0);

	for (tick = 0; (tick < DRIVE_BUDGET) && !(titleA && titleB); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
		CHECK(ScreenOf(&g_b) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
		if (!exitedA && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_EXIT))
		{
			CHECK(actionA == ACT_CLOSE_LINK);
			CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT);
			rejectedA = LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED;
			exitedA = 1;
		}
		if (!exitedB && (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_EXIT))
		{
			CHECK(actionB == ACT_CLOSE_LINK);
			CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT);
			rejectedB = LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED;
			exitedB = 1;
		}
		titleA = titleA || (actionA == ACT_RETURN_TO_TITLE);
		titleB = titleB || (actionB == ACT_RETURN_TO_TITLE);
	}
	CHECK(exitedA && exitedB);
	/* The config mismatch rejects the rematch (RL-7); A at least sees it,
	 * since B's HELLO reaches A's open rematch link. */
	CHECK(rejectedA);
	(void)rejectedB;
	CHECK(titleA && titleB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(g_a.matchCount == 1u);
	CHECK(g_b.matchCount == 0u);

	ShutdownBoth();
	return 0;
}

/*
 * 33d. RL-S5 (d): late select records in phase 2 are FOREIGN to the
 * agreement: ignored and counted, never a commit, never a failure. Three of
 * B's confirmed-select records (the linger it sends through its hold) arrive
 * over the socket and two sit in A's inbox; the agreement counts five
 * FOREIGN, stays PENDING, the flow stays on SELECT_RESULT, and the select
 * session, no longer driven, is untouched. A launch record still commits.
 */
static int TestLaunchStaleSelectRecordsIgnored(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchSelectSession selectBefore;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t records[3][NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES];
	size_t size = 0u;
	uint32_t i;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_SELECT_STALE_A_PORT, TEST_LAUNCH_SELECT_STALE_B_PORT));
	for (i = 0; i < 3u; i++)
	{
		CHECK(NativeMatchSelectSession_Compose(&g_b.select, records[i], sizeof(records[i]), &size) == 1);
		CHECK(size == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES);
	}
	CHECK(RelinkAloneToReady(TEST_LAUNCH_SELECT_STALE_A_PORT, TEST_LAUNCH_SELECT_STALE_B_PORT, &expected, NULL));
	memcpy(&selectBefore, &g_a.select, sizeof(selectBefore));

	CHECK(InjectAux(&g_a, records[0]));
	CHECK(InjectAux(&g_a, records[1]));
	for (i = 0; i < 3u; i++)
	{
		CHECK(SendTo(&g_peer.transport, TEST_LAUNCH_SELECT_STALE_A_PORT, records[i], sizeof(records[i])));
	}
	for (tick = 0; (tick < 10u) && (g_a.launch.foreignCount < 5u); tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
		CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING);
	}
	CHECK(g_a.launch.foreignCount == 5u);
	CHECK(g_a.launch.acceptedCount == 0u);
	CHECK(g_a.launch.malformedCount == 0u);
	CHECK(g_a.launch.selfCount == 0u);
	CHECK(g_a.launch.mismatchCount == 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(memcmp(&selectBefore, &g_a.select, sizeof(selectBefore)) == 0);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_CONFIRMED);

	CHECK(CommitFromPeer(TEST_LAUNCH_SELECT_STALE_A_PORT, digest, &expected));
	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33e. RL-S5 (f): a commit at the timeout edge, and a commit on the timeout
 * tick itself. The commit is checked before the timeout (RL-5), so a record
 * read on the tick before the launch timeout starts the race, and so does
 * one read on the very tick ticksSinceRelink reaches launchTimeoutTicks: the
 * race starts, not LINK ERROR. The record is put straight into A's inbox so
 * it is read on exactly that tick.
 */
static int TestLaunchCommitAtTimeoutEdge(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint32_t offset;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	for (offset = 2u; offset >= 1u; offset--)
	{
		CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_EDGE_A_PORT, TEST_LAUNCH_EDGE_B_PORT));
		CHECK(RelinkAloneToReady(TEST_LAUNCH_EDGE_A_PORT, TEST_LAUNCH_EDGE_B_PORT, &expected, NULL));
		while (g_a.flow.ticksSinceRelink + offset < LAUNCH_TIMEOUT_TICKS)
		{
			CHECK(ExpectPendingTicks(1u));
		}
		CHECK(g_a.flow.ticksSinceRelink == LAUNCH_TIMEOUT_TICKS - offset);
		CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 1u));
		/* This tick takes ticksSinceRelink to launchTimeoutTicks - offset + 1:
		 * the tick before the timeout (offset 2) or the timeout tick (1). */
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_START_RACE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
		CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &expected, sizeof(expected)) == 0);
		CHECK(g_a.matchCount == 1u);
		ClosePeer();
		ShutdownBoth();
	}
	return 0;
}

/*
 * 33f. RL-S5 (f): no commit after CLOSE_LINK. With nothing from the peer, A
 * shows LINK ERROR with CLOSE_LINK exactly launchTimeoutTicks after RELINK,
 * and the agreement is reset on that tick. Launch records sent afterwards
 * (PENDING and HEARD, on the relink digest) reach no socket and no
 * agreement: A stays on RESULTS with nothing agreed.
 */
static int TestLaunchNoCommitAfterCloseLink(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	enum NativeArcadeFlowAction action = ACT_NONE;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint32_t ticks = 1u; /* the READY tick */
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_CLOSE_A_PORT, TEST_LAUNCH_CLOSE_B_PORT));
	CHECK(RelinkAloneToReady(TEST_LAUNCH_CLOSE_A_PORT, TEST_LAUNCH_CLOSE_B_PORT, &expected, NULL));
	for (tick = 0; (tick < DRIVE_BUDGET) && (action == ACT_NONE); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		ticks += 1u;
		if (action == ACT_NONE)
		{
			CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING);
		}
	}
	CHECK(action == ACT_CLOSE_LINK);
	CHECK(ticks == LAUNCH_TIMEOUT_TICKS);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(AgreementIsReset(&g_a));
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);

	CHECK(PeerSendLaunch(TEST_LAUNCH_CLOSE_A_PORT, digest, 0u, 50u));
	CHECK(PeerSendLaunch(TEST_LAUNCH_CLOSE_A_PORT, digest, FLAG_HEARD, 51u));
	for (tick = 0; tick < RESULTS_DWELL_TICKS + 10u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
		CHECK(AgreementIsReset(&g_a));
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	}
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	CHECK(g_a.matchCount == 0u);
	CHECK(g_a.raceArmed == 0u);
	CHECK(memcmp(&g_a.lastReadyConfig, &fixture, sizeof(fixture)) == 0);

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33g. RL-S5 (f), RL-4: a lost HEARD runs the linger to the cap. The peer's
 * one record carries no HEARD (its HEARD records are the ones lost), so A
 * commits and races but never sees peerHeard: it sends exactly
 * NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS (300) HEARD records, one per
 * tick from the commit tick on RACING, all received by the peer's socket,
 * and then stops.
 */
static int TestLaunchLostHeardLingerCap(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint32_t sequenceBefore;
	uint32_t sequenceTick;
	uint32_t lingerTicks;
	uint32_t received;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_LINGER_A_PORT, TEST_LAUNCH_LINGER_B_PORT));
	CHECK(RelinkAloneToReady(TEST_LAUNCH_LINGER_A_PORT, TEST_LAUNCH_LINGER_B_PORT, &expected, NULL));
	/* What A sent so far (HELLOs and PENDING records) is not counted. */
	(void)DrainSized(&g_peer.transport, LAUNCH_BYTES, g_a.launch.sequence);

	sequenceBefore = g_a.launch.sequence;
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 1u));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_START_RACE);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	CHECK(g_a.launch.sequence == sequenceBefore + 1u);
	received = DrainHeardRecords(&g_peer.transport, digest, 1u);
	lingerTicks = 1u;
	for (tick = 0; (tick < DRIVE_BUDGET) && NativeArcadeLaunch_ShouldSend(&g_a.launch); tick++)
	{
		sequenceTick = g_a.launch.sequence;
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(g_a.launch.sequence == sequenceTick + 1u);
		received += DrainHeardRecords(&g_peer.transport, digest, 1u);
		lingerTicks += 1u;
	}
	CHECK(lingerTicks == NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS);
	CHECK(g_a.launch.sequence - sequenceBefore == NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS);
	CHECK(g_a.launch.ticksSinceCommit == NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS);
	CHECK(received == NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS);
	CHECK(g_a.launch.peerHeard == 0u);
	CHECK(g_a.launch.heardSent == 1u);

	/* The cap holds: nothing more is sent, and the race goes on. */
	sequenceTick = g_a.launch.sequence;
	for (tick = 0; tick < 20u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	}
	CHECK(g_a.launch.sequence == sequenceTick);
	CHECK(DrainHeardRecords(&g_peer.transport, digest, 0u) == 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33h. RL-S5 (f), RL-3: reordered and duplicated records change nothing.
 * A different digest, an own-role echo, and a malformed record (each
 * duplicated where it can be) are ignored and counted. Then valid records
 * out of sequence order and duplicated: the first commits, sequence is never
 * read, HEARD latches from the one record carrying it, and older records
 * without HEARD after the commit neither unlatch it nor restart the linger;
 * A, having sent its HEARD record and seen one, stops sending.
 */
static int TestLaunchReorderedDuplicated(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t otherDigest[LAUNCH_DIGEST_BYTES];
	uint8_t malformed[LAUNCH_BYTES];
	uint32_t sequence;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	memcpy(otherDigest, digest, sizeof(otherDigest));
	otherDigest[0] ^= 0x01u;
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_REORDER_A_PORT, TEST_LAUNCH_REORDER_B_PORT));
	CHECK(RelinkAloneToReady(TEST_LAUNCH_REORDER_A_PORT, TEST_LAUNCH_REORDER_B_PORT, &expected, NULL));

	CHECK(InjectLaunch(&g_a, ROLE_CAB2, otherDigest, FLAG_HEARD, 3u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, otherDigest, FLAG_HEARD, 3u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB1, digest, FLAG_HEARD, 2u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB1, digest, FLAG_HEARD, 2u));
	CHECK(ComposeLaunchRecord(ROLE_CAB2, digest, FLAG_HEARD, 4u, malformed));
	malformed[20] ^= 0x01u; /* a configDigest byte; the trailer digest goes stale */
	CHECK(InjectAux(&g_a, malformed));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING);
	CHECK(g_a.launch.mismatchCount == 2u);
	CHECK(g_a.launch.selfCount == 2u);
	CHECK(g_a.launch.malformedCount == 1u);
	CHECK(g_a.launch.acceptedCount == 0u);
	CHECK(g_a.launch.peerHeard == 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);

	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 9u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, FLAG_HEARD, 4u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 9u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 1u));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_START_RACE);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	CHECK(g_a.launch.acceptedCount == 4u);
	CHECK(g_a.launch.peerHeard == 1u);
	CHECK(g_a.launch.heardSent == 1u);
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &expected, sizeof(expected)) == 0);

	sequence = g_a.launch.sequence;
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 2u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 2u));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 2u));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(g_a.launch.acceptedCount == 7u);
	CHECK(g_a.launch.peerHeard == 1u);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	CHECK(NativeArcadeLaunch_ShouldSend(&g_a.launch) == 0);
	CHECK(g_a.launch.sequence == sequence);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(g_a.launch.mismatchCount == 2u);
	CHECK(g_a.launch.selfCount == 2u);
	CHECK(g_a.launch.malformedCount == 1u);

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33i. RL-S5 (e): RESTART_LOBBY during phase 2, on the same resolved config
 * (so the same digest). A is READY and PENDING against the test peer. A junk
 * bundle faults A's relink link (lobby LOST), and a stale record on the
 * digest queued right behind it stays in the transport unread (a faulted
 * link polls nothing). A stale record already in the lost link's inbox is
 * taken by the old agreement (still phase 2), but the lobby is LOST, so no
 * race starts. RESTART_LOBBY resets the agreement and reopens the link empty
 * (Close released the old socket and inbox). The peer restarts too: a new
 * HELLO, and a stale record on the same digest queued behind it, still in
 * the transport when A's new link comes up. The new agreement discards it
 * on its READY tick and stays PENDING until a record sent after that READY.
 */
static int TestLaunchStaleAcrossRestartLobby(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeLockstepPeerLink *oldLink;
	struct NativeLockstepPeerLink *link;
	enum NativeArcadeFlowAction action = ACT_NONE;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t junk[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_STALE_RESTART_A_PORT, TEST_LAUNCH_STALE_RESTART_B_PORT));
	CHECK(RelinkAloneToReady(TEST_LAUNCH_STALE_RESTART_A_PORT, TEST_LAUNCH_STALE_RESTART_B_PORT, &expected, NULL));
	oldLink = NativeArcadeNetplay_Link(&g_a);

	memset(junk, 0xEE, sizeof(junk));
	CHECK(SendTo(&g_peer.transport, TEST_LAUNCH_STALE_RESTART_A_PORT, junk, sizeof(junk)));
	CHECK(PeerSendLaunch(TEST_LAUNCH_STALE_RESTART_A_PORT, digest, FLAG_HEARD, 20u));
	for (tick = 0; (tick < 10u) && (LobbyStatusOf(&g_a) != (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST); tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	}
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST);
	CHECK(NativeLockstepPeerLink_Mode(oldLink) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_PENDING);
	CHECK(g_a.launch.acceptedCount == 0u);

	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, FLAG_HEARD, 21u));
	for (tick = 0; (tick < DRIVE_BUDGET) && (action != ACT_RESTART_LOBBY); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_RESTART_LOBBY));
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
		if (action == ACT_NONE)
		{
			/* The old agreement took the inbox record, but LOST never starts. */
			CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
			CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST);
		}
	}
	CHECK(action == ACT_RESTART_LOBBY);
	CHECK(AgreementIsReset(&g_a));
	link = NativeArcadeNetplay_Link(&g_a);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_AuxCount(link) == 0u);
	CHECK(g_a.lobbyReadySeen == 0u);
	CHECK(memcmp(&g_a.currentConfig, &expected, sizeof(expected)) == 0);

	/* The peer restarts: a new HELLO, then the stale record behind it. */
	ClosePeer();
	CHECK(PeerUpToReady(TEST_LAUNCH_STALE_RESTART_A_PORT, TEST_LAUNCH_STALE_RESTART_B_PORT, &expected, digest));
	CHECK(ExpectPendingTicks(STALE_WATCH_TICKS));
	CHECK(CommitFromPeer(TEST_LAUNCH_STALE_RESTART_A_PORT, digest, &expected));

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33j. RL-S5 (e): RELINK. On the tick before RELINK, A is primed with a
 * stale COMMITTED agreement on the upcoming relink digest, a stale record on
 * it sits in the old link's inbox, and another is sent to the old socket.
 * RELINK resets the agreement and opens a fresh link with an empty inbox
 * (the old records went to the select session or were released with the old
 * socket). A stale record queued behind the peer's HELLO on the new link is
 * discarded on the READY tick; the new agreement stays PENDING until a
 * record sent after that READY.
 */
static int TestLaunchStaleAcrossRelink(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t stale[LAUNCH_BYTES];

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_STALE_RELINK_A_PORT, TEST_LAUNCH_STALE_RELINK_B_PORT));
	NativeArcadeNetplay_Shutdown(&g_b);
	while (g_a.flow.ticksInScreen + 1u < SELECT_RESULT_HOLD_TICKS)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	}

	CHECK(PrimeAgreement(&g_a, digest));
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, FLAG_HEARD, 30u));
	CHECK(ComposeLaunchRecord(ROLE_CAB2, digest, FLAG_HEARD, 31u, stale));
	CHECK(NativeUdpTransport_GlobalInit() == 1);
	CHECK(NativeUdpTransport_Open(&g_raw, (uint16_t)TEST_LAUNCH_STALE_RELINK_B_PORT) == 1);
	CHECK(SendTo(&g_raw, TEST_LAUNCH_STALE_RELINK_A_PORT, stale, sizeof(stale)));
	NativeUdpTransport_Close(&g_raw);
	NativeUdpTransport_GlobalShutdown();

	/* One tick: RELINK, which resets the primed agreement. */
	CHECK(AloneToRelink(&expected));
	CHECK(g_a.flow.relinked == 1u);
	CHECK(PeerUpToReady(TEST_LAUNCH_STALE_RELINK_A_PORT, TEST_LAUNCH_STALE_RELINK_B_PORT, &expected, digest));
	CHECK(ExpectPendingTicks(STALE_WATCH_TICKS));
	CHECK(CommitFromPeer(TEST_LAUNCH_STALE_RELINK_A_PORT, digest, &expected));

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33k. RL-S5 (e): BEGIN_REMATCH and BEGIN_SELECT, with stale records on the
 * digest of the next relink (the rematch select's resolved config). After a
 * symmetric first race, A is primed with a stale COMMITTED agreement on that
 * digest and gets a stale record in its inbox and one in its socket, first
 * on RESULTS before BEGIN_REMATCH, then on MATCH_FOUND before BEGIN_SELECT
 * (where the rematch lobby's READY is shown to have begun no agreement).
 * Each resets the agreement (BEGIN_SELECT also empties the inbox; the
 * rematch opened a fresh link). At the next relink, a stale record on the
 * same digest behind the peer's HELLO is discarded on the READY tick, and
 * the agreement stays PENDING until a record sent after that READY.
 */
static int TestLaunchStaleAcrossRematchAndSelect(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 first;
	struct NativeMatchConfigV1 rematchBase;
	struct NativeMatchConfigV1 second;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t stale[LAUNCH_BYTES];
	uint64_t seed = 0u;
	uint32_t tick;
	int selectA = 0;
	int selectB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &first));
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&first, &seed) == 1);
	CHECK(NativeLockstepRematch_BuildConfig(&first, seed, &rematchBase) == 1);
	CHECK(IdleResolved(&rematchBase, 2u, &second));
	CHECK(NativeMatchConfigV1_Digest(&second, digest) == 1);
	CHECK(ComposeLaunchRecord(ROLE_CAB2, digest, FLAG_HEARD, 40u, stale));

	CHECK(InitPair(&fixture, &fixture, TEST_LAUNCH_STALE_REMATCH_A_PORT, TEST_LAUNCH_STALE_REMATCH_B_PORT));
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(DriveBothToRaceChecked());
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &first, sizeof(first)) == 0);
	CHECK(FinishAndDwell());

	/* BEGIN_REMATCH, on RESULTS with the race link still open. */
	CHECK(PrimeAgreement(&g_a, digest));
	CHECK(InjectAux(&g_a, stale));
	CHECK(SendTo(&NativeArcadeNetplay_Link(&g_b)->transport, TEST_LAUNCH_STALE_REMATCH_A_PORT, stale, sizeof(stale)));
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(AgreementIsReset(&g_a));
	CHECK(AgreementIsReset(&g_b));
	CHECK(AuxCountOf(&g_a) == 0u);
	CHECK(memcmp(&g_a.currentConfig, &rematchBase, sizeof(rematchBase)) == 0);

	/* BEGIN_SELECT, from MATCH_FOUND. */
	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK((actionB == ACT_NONE) || (actionB == ACT_BEGIN_SELECT));
		selectB = selectB || (actionB == ACT_BEGIN_SELECT);
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	/* The rematch lobby's READY began no agreement: only a relink READY does. */
	CHECK(g_a.lobbyReadySeen == 1u);
	CHECK(g_a.relinked == 0u);
	CHECK(AgreementIsReset(&g_a));
	CHECK(PrimeAgreement(&g_a, digest));
	CHECK(InjectAux(&g_a, stale));
	CHECK(SendTo(&NativeArcadeNetplay_Link(&g_b)->transport, TEST_LAUNCH_STALE_REMATCH_A_PORT, stale, sizeof(stale)));
	for (tick = 0; (tick < DRIVE_BUDGET) && !(selectA && selectB); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK((actionA == ACT_NONE) || (actionA == ACT_BEGIN_SELECT));
		CHECK((actionB == ACT_NONE) || (actionB == ACT_BEGIN_SELECT));
		if (actionA == ACT_BEGIN_SELECT)
		{
			selectA = 1;
			CHECK(AgreementIsReset(&g_a));
			CHECK(AuxCountOf(&g_a) == 0u);
		}
		selectB = selectB || (actionB == ACT_BEGIN_SELECT);
	}
	CHECK(selectA && selectB);
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK((actionA == ACT_NONE) && (actionB == ACT_NONE));
	CHECK(AgreementIsReset(&g_a));
	CHECK(DriveBothToSelectResult());

	CHECK(RelinkAloneToReady(TEST_LAUNCH_STALE_REMATCH_A_PORT, TEST_LAUNCH_STALE_REMATCH_B_PORT, &second, digest));
	CHECK(ExpectPendingTicks(STALE_WATCH_TICKS));
	CHECK(CommitFromPeer(TEST_LAUNCH_STALE_REMATCH_A_PORT, digest, &second));
	CHECK(g_a.matchCount == 2u);

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33l. RL-S5 (e): CLOSE_LINK, RETURN_TO_TITLE, and Enter, with stale
 * records on the digest of the next relink (the next select's resolved
 * config after a return to the title). A times out to LINK ERROR against
 * the silent peer (CLOSE_LINK resets the agreement), stale records then sent
 * reach no socket, and RESULTS idles out through EXIT to the title. Before
 * every one of those ticks A is primed with a stale COMMITTED agreement on
 * the next relink digest: CLOSE_LINK and RETURN_TO_TITLE each reset it, a
 * screen-OFF tick leaves it untouched, and Enter resets it and opens a fresh
 * link. At the next relink, a stale record on the same digest behind the
 * peer's HELLO is discarded on the READY tick, and the agreement stays
 * PENDING until a record sent after that READY.
 */
static int TestLaunchStaleAcrossCloseTitleEnter(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 first;
	struct NativeMatchConfigV1 next;
	enum NativeArcadeFlowAction action = ACT_NONE;
	uint8_t firstDigest[LAUNCH_DIGEST_BYTES];
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint32_t tick;
	int closes = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &first));
	CHECK(IdleResolved(&fixture, 2u, &next));
	CHECK(memcmp(&first, &next, sizeof(first)) != 0);
	CHECK(NativeMatchConfigV1_Digest(&first, firstDigest) == 1);
	CHECK(NativeMatchConfigV1_Digest(&next, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_STALE_TITLE_A_PORT, TEST_LAUNCH_STALE_TITLE_B_PORT));
	CHECK(RelinkAloneToReady(TEST_LAUNCH_STALE_TITLE_A_PORT, TEST_LAUNCH_STALE_TITLE_B_PORT, &first, NULL));

	/* CLOSE_LINK at the launch timeout. */
	for (tick = 0; (tick < DRIVE_BUDGET) && (action == ACT_NONE); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
	}
	CHECK(action == ACT_CLOSE_LINK);
	CHECK(AgreementIsReset(&g_a));
	CHECK(PeerSendLaunch(TEST_LAUNCH_STALE_TITLE_A_PORT, firstDigest, FLAG_HEARD, 60u));
	CHECK(PeerSendLaunch(TEST_LAUNCH_STALE_TITLE_A_PORT, digest, FLAG_HEARD, 61u));
	ClosePeer();

	/* RESULTS idles out: EXIT (CLOSE_LINK), then RETURN_TO_TITLE. */
	action = ACT_NONE;
	for (tick = 0; (tick < DRIVE_BUDGET) && (action != ACT_RETURN_TO_TITLE); tick++)
	{
		CHECK(PrimeAgreement(&g_a, digest));
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((action == ACT_NONE) || (action == ACT_CLOSE_LINK) || (action == ACT_RETURN_TO_TITLE));
		if (action == ACT_NONE)
		{
			/* Not a reset point: the primed agreement is left as it was. */
			CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
		}
		else
		{
			CHECK(AgreementIsReset(&g_a));
			closes += (action == ACT_CLOSE_LINK) ? 1 : 0;
		}
	}
	CHECK(action == ACT_RETURN_TO_TITLE);
	CHECK(closes == 1);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);

	/* Screen OFF changes nothing; Enter resets. */
	CHECK(PrimeAgreement(&g_a, digest));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(AgreementIsReset(&g_a));
	CHECK(AuxCountOf(&g_a) == 0u);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(DriveBothIntoSelect());
	CHECK(g_a.selectSerial == 2u);
	CHECK(g_b.selectSerial == 2u);
	CHECK(DriveBothToSelectResult());

	CHECK(RelinkAloneToReady(TEST_LAUNCH_STALE_TITLE_A_PORT, TEST_LAUNCH_STALE_TITLE_B_PORT, &next, digest));
	CHECK(ExpectPendingTicks(STALE_WATCH_TICKS));
	CHECK(CommitFromPeer(TEST_LAUNCH_STALE_TITLE_A_PORT, digest, &next));

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * 33m. RL-S5 (f), RL-4: a link fault ends the linger's sends. A commits on a
 * record without HEARD and races, lingering: one HEARD record per tick. A
 * junk bundle from the test peer then faults A's link (lobby PEER_LOST, LINK
 * ERROR on RESULTS, the link kept open). From the tick whose poll faults it
 * on, the agreement is still COMMITTED and still wants to send, but the link
 * is not RUNNING, so no launch record is composed or sent.
 */
static int TestLaunchFaultDuringLinger(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 expected;
	struct NativeLockstepPeerLink *link;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t junk[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint32_t sequenceTick;
	uint32_t sentBeforeFault = 0u;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &expected));
	CHECK(NativeMatchConfigV1_Digest(&expected, digest) == 1);
	CHECK(PairToSelectResult(&fixture, TEST_LAUNCH_FAULT_LINGER_A_PORT, TEST_LAUNCH_FAULT_LINGER_B_PORT));
	CHECK(RelinkAloneToReady(TEST_LAUNCH_FAULT_LINGER_A_PORT, TEST_LAUNCH_FAULT_LINGER_B_PORT, &expected, NULL));
	link = NativeArcadeNetplay_Link(&g_a);
	CHECK(link != NULL);

	/* The commit, then one lingering tick on RACING: two HEARD records. */
	CHECK(InjectLaunch(&g_a, ROLE_CAB2, digest, 0u, 1u));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_START_RACE);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	sequenceTick = g_a.launch.sequence;
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(g_a.launch.sequence == sequenceTick + 1u);
	CHECK(NativeArcadeLaunch_ShouldSend(&g_a.launch) == 1);
	CHECK(DrainHeardRecords(&g_peer.transport, digest, 2u) == 2u);

	/* The fault: until a poll reads the junk bundle, A keeps sending; the
	 * tick whose poll faults the link sends nothing. */
	memset(junk, 0xEE, sizeof(junk));
	CHECK(SendTo(&g_peer.transport, TEST_LAUNCH_FAULT_LINGER_A_PORT, junk, sizeof(junk)));
	for (tick = 0; (tick < 10u) && (NativeLockstepPeerLink_Mode(link) != NATIVE_LOCKSTEP_PEER_LINK_FAULTED); tick++)
	{
		sequenceTick = g_a.launch.sequence;
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
		if (NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED)
		{
			CHECK(g_a.launch.sequence == sequenceTick);
		}
		else
		{
			CHECK(g_a.launch.sequence == sequenceTick + 1u);
			sentBeforeFault += 1u;
		}
	}
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	CHECK(NativeArcadeNetplay_Link(&g_a) == link);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(DrainSized(&g_peer.transport, LAUNCH_BYTES, sentBeforeFault) == sentBeforeFault);

	/* Still COMMITTED and inside the linger, yet nothing more is sent. */
	sequenceTick = g_a.launch.sequence;
	for (tick = 0; tick < 10u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
		CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
		CHECK(NativeArcadeLaunch_ShouldSend(&g_a.launch) == 1);
		CHECK(g_a.launch.sequence == sequenceTick);
	}
	CHECK(g_a.launch.ticksSinceCommit < NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS);
	CHECK(DrainSized(&g_peer.transport, LAUNCH_BYTES, 0u) == 0u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);

	ClosePeer();
	ShutdownBoth();
	return 0;
}

/*
 * White-box, for the reset points of the local race-failure latch: with
 * both latches set off RACING (which the API never does, so nothing consumes
 * them there), ticks both, pressing held until a side has returned target,
 * until each has. The latch survives every tick before target and is 0
 * right after the tick that returns it.
 */
static int LatchClearedBy(enum NativeArcadeFlowAction target, uint32_t held)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	int seenA = 0;
	int seenB = 0;
	uint32_t tick;

	g_a.localRaceFailure = 1u;
	g_b.localRaceFailure = 1u;
	for (tick = 0; (tick < DRIVE_BUDGET) && !(seenA && seenB); tick++)
	{
		TickBoth(seenA ? 0u : held, seenB ? 0u : held, 0u, &actionA, &actionB);
		if (!seenA)
		{
			seenA = (actionA == target);
			CHECK(g_a.localRaceFailure == (seenA ? 0u : 1u));
		}
		if (!seenB)
		{
			seenB = (actionB == target);
			CHECK(g_b.localRaceFailure == (seenB ? 0u : 1u));
		}
	}
	CHECK(seenA && seenB);
	return 0;
}

/*
 * 34. RL-S6, RL-11: the local race-failure input. Refused off RACING
 * (uninitialized, dormant, LOBBY, RESULTS). On RACING it latches, and the
 * next Tick ends the race as RESULTS LINK ERROR without sending anything or
 * touching the link, the launch linger, pendingLinkFailure, or the peer; it
 * outranks a same-tick finish, and a pending link failure outranks it. The
 * latch is consumed by that Tick and cleared at Init, Enter, BEGIN_REMATCH,
 * BEGIN_SELECT, RELINK, START_RACE, CLOSE_LINK, RETURN_TO_TITLE, and
 * Shutdown, so it never reaches a later race.
 */
static int TestLocalRaceFailure(void)
{
	static const uint32_t downScript[1] = {BTN_DOWN};
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayConfig config;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	struct NativeLockstepPeerLink *linkA;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);

	/* Refused: NULL, a zero (uninitialized) struct, a dormant adapter. Init
	 * clears the latch. */
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(NULL) == 0);
	memset(&g_probe, 0, sizeof(g_probe));
	memset(&g_sentinel, 0, sizeof(g_sentinel));
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_probe) == 0);
	CHECK(memcmp(&g_probe, &g_sentinel, sizeof(g_probe)) == 0);
	CHECK(MakeConfig(&config, &fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_LOCAL_FAILURE_A_PORT,
		TEST_LOCAL_FAILURE_B_PORT));
	memset(&g_probe, 0xA5, sizeof(g_probe));
	CHECK(NativeArcadeNetplay_Init(&g_probe, &config) == 1);
	CHECK(g_probe.localRaceFailure == 0u);
	memcpy(&g_sentinel, &g_probe, sizeof(g_probe));
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_probe) == 0);
	CHECK(memcmp(&g_probe, &g_sentinel, sizeof(g_probe)) == 0);
	NativeArcadeNetplay_Shutdown(&g_probe);

	/* Enter clears it; LOBBY refuses it. */
	CHECK(InitPair(&fixture, &fixture, TEST_LOCAL_FAILURE_A_PORT, TEST_LOCAL_FAILURE_B_PORT));
	g_a.localRaceFailure = 1u;
	g_b.localRaceFailure = 1u;
	CHECK(NativeArcadeNetplay_Enter(&g_a) == ACT_BEGIN_LOBBY);
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_BEGIN_LOBBY);
	CHECK(g_a.localRaceFailure == 0u);
	CHECK(g_b.localRaceFailure == 0u);
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_a) == 0);
	CHECK(g_a.localRaceFailure == 0u);

	/* Race 1. */
	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(g_a.localRaceFailure == 0u);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	linkA = NativeArcadeNetplay_Link(&g_a);
	CHECK(linkA != NULL);

	/* A reports on RACING: latched, still RACING until the next Tick. */
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_a) == 1);
	CHECK(g_a.localRaceFailure == 1u);
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_a) == 1);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_NONE);
	CHECK(actionB == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	/* Consumed; pendingLinkFailure keeps its meaning; the link, the race
	 * config, and the committed launch agreement (its linger) are kept. */
	CHECK(g_a.localRaceFailure == 0u);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(NativeArcadeNetplay_Link(&g_a) == linkA);
	CHECK(NativeLockstepPeerLink_Mode(linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(g_a.raceArmed == 1u);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	/* B was not told. */
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	/* RESULTS refuses it. */
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_a) == 0);
	CHECK(g_a.localRaceFailure == 0u);
	for (tick = 0; tick < 10u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	}

	/* B reports with a same-tick finish: LINK ERROR outranks it (UX-6). */
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_b) == 1);
	CHECK(NativeArcadeNetplay_Tick(&g_b, 0u, 1u) == ACT_NONE);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(g_b.localRaceFailure == 0u);

	/* Past the dwell, then BEGIN_REMATCH clears it. */
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(LatchClearedBy(ACT_BEGIN_REMATCH, BTN_CROSS) == 0);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	/* BEGIN_SELECT, RELINK, and START_RACE clear it. */
	CHECK(LatchClearedBy(ACT_BEGIN_SELECT, 0u) == 0);
	CHECK(LatchClearedBy(ACT_RELINK, 0u) == 0);
	CHECK(LatchClearedBy(ACT_START_RACE, 0u) == 0);

	/* Race 2: nothing leaked into it. */
	for (tick = 0; tick < 10u; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	}

	/* A pending link failure outranks the local one, which is still
	 * consumed: 90 stalls latch PEER_TIMEOUT. */
	for (tick = 0; tick < NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS; tick++)
	{
		NativeArcadeNetplay_OnTakeResult(&g_a, NATIVE_LOCKSTEP_SESSION_STALL, 7u);
	}
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_a) == 1);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	CHECK(g_a.localRaceFailure == 0u);
	CHECK(g_a.pendingLinkFailure == (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	/* B finishes normally. */
	CHECK(NativeArcadeNetplay_Tick(&g_b, 0u, 1u) == ACT_NONE);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);

	/* EXIT: CLOSE_LINK, then RETURN_TO_TITLE, each clears it. */
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(PressScripts(downScript, downScript, 1u));
	CHECK(LatchClearedBy(ACT_CLOSE_LINK, BTN_CROSS) == 0);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	CHECK(LatchClearedBy(ACT_RETURN_TO_TITLE, 0u) == 0);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);

	/* Shutdown clears it. */
	g_a.localRaceFailure = 1u;
	g_b.localRaceFailure = 1u;
	ShutdownBoth();
	CHECK(g_a.localRaceFailure == 0u);
	CHECK(g_b.localRaceFailure == 0u);
	return 0;
}

/* ---- LR-S6 (docs/LOCKSTEP_RACE_MILESTONE.md LR-14): stale bundles after a
 * rematch ---- */

/* The old match's bundles B keeps sending: frames 0..D of B's race session
 * (digest-free, so composable at any time while it is RUNNING). Three, well
 * under the peer link's 8-entry early-bundle staging buffer. */
#define STALE_BUNDLE_COUNT (NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY + 1u)

static uint8_t g_stale[STALE_BUNDLE_COUNT][NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
static uint8_t g_staleIdentity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES];

/* A with the production cadence (150-tick attempt budget), the given rematch
 * wait, and the given MATCH_FOUND hold, B likewise; every other timing stays
 * small. */
static int InitStalePairHold(const struct NativeMatchConfigV1 *fixture, uint32_t portA, uint32_t portB, uint32_t rematchWaitTicks,
	uint32_t matchFoundHoldTicks)
{
	struct NativeArcadeNetplayConfig defaults;
	struct NativeArcadeNetplayConfig config;

	NativeArcadeNetplay_DefaultConfig(&defaults);
	if (!MakeConfig(&config, fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, portA, portB))
	{
		return 0;
	}
	config.attemptTicksPerCandidate = defaults.attemptTicksPerCandidate;
	config.timings.rematchWaitTimeoutTicks = rematchWaitTicks;
	config.timings.matchFoundHoldTicks = matchFoundHoldTicks;
	if (!NativeArcadeNetplay_Init(&g_a, &config))
	{
		return 0;
	}
	if (!MakeConfig(&config, fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, portB, portA))
	{
		return 0;
	}
	config.attemptTicksPerCandidate = defaults.attemptTicksPerCandidate;
	config.timings.rematchWaitTimeoutTicks = rematchWaitTicks;
	config.timings.matchFoundHoldTicks = matchFoundHoldTicks;
	return NativeArcadeNetplay_Init(&g_b, &config);
}

/* InitStalePairHold with the small MATCH_FOUND hold. */
static int InitStalePair(const struct NativeMatchConfigV1 *fixture, uint32_t portA, uint32_t portB, uint32_t rematchWaitTicks)
{
	return InitStalePairHold(fixture, portA, portB, rematchWaitTicks, MATCH_FOUND_HOLD_TICKS);
}

/* On RACING: keeps B's frames 0..D, composed from its race session, and
 * that session's match identity. */
static int KeepStaleBundles(void)
{
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(&g_b);
	struct NativeLockstepSession *session;
	size_t size = 0;
	uint32_t frame;

	CHECK(link != NULL);
	session = NativeLockstepPeerLink_Session(link);
	CHECK(NativeLockstepSession_Mode(session) == NATIVE_LOCKSTEP_RUNNING);
	for (frame = 0; frame < STALE_BUNDLE_COUNT; frame++)
	{
		CHECK(NativeLockstepSession_ComposeBundle(session, frame, g_stale[frame], sizeof(g_stale[frame]), &size) == 1);
		CHECK(size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	}
	memcpy(g_staleIdentity, session->matchIdentity, sizeof(g_staleIdentity));
	return 0;
}

/* One kept stale bundle, sent from B's open link's socket (B's cabinet port,
 * so A's sender filter passes it) to A: the resend of a cabinet still held
 * on the old match. */
static int SendStale(uint32_t toPort, uint32_t index)
{
	struct NativeUdpTransportAddress to;
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(&g_b);

	CHECK(link != NULL);
	CHECK(NativeUdpTransport_MakeAddress(&to, "127.0.0.1", (uint16_t)toPort));
	CHECK(NativeUdpTransport_Send(&link->transport, &to, g_stale[index % STALE_BUNDLE_COUNT], NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES));
	return 0;
}

/* netplay's end-of-race record: exactly one, with this race number, end
 * reason, and drop count; a second take finds nothing. */
static int ExpectRaceEnd(struct NativeArcadeNetplay *netplay, uint32_t raceNumber, uint32_t endReason, uint32_t drops)
{
	struct NativeArcadeNetplayRaceEnd raceEnd;
	struct NativeArcadeNetplayRaceEnd sentinel;

	memset(&raceEnd, 0xA5, sizeof(raceEnd));
	CHECK(NativeArcadeNetplay_TakeRaceEnd(netplay, &raceEnd) == 1);
	CHECK(raceEnd.raceNumber == raceNumber);
	CHECK(raceEnd.endReason == endReason);
	CHECK(raceEnd.foreignBundleDrops == drops);
	memset(&raceEnd, 0xA5, sizeof(raceEnd));
	sentinel = raceEnd;
	CHECK(NativeArcadeNetplay_TakeRaceEnd(netplay, &raceEnd) == 0);
	CHECK(memcmp(&raceEnd, &sentinel, sizeof(raceEnd)) == 0);
	return 0;
}

/* No end-of-race record is latched on netplay. */
static int ExpectNoRaceEnd(struct NativeArcadeNetplay *netplay)
{
	struct NativeArcadeNetplayRaceEnd raceEnd;

	CHECK(NativeArcadeNetplay_TakeRaceEnd(netplay, &raceEnd) == 0);
	return 0;
}

/* Tick both through the results dwell with no buttons (arming both menus). */
static int DwellBoth(void)
{
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;

	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	return 0;
}

/* Both have reached READY on the new match and moved on to SELECT: both
 * links RUNNING on one new identity (not the old match's), with no fault or
 * divergence on either session; A dropped exactly `drops` stale records and
 * B none. */
static int ExpectRematchReadyClean(uint32_t drops)
{
	struct NativeLockstepPeerLink *linkA = NativeArcadeNetplay_Link(&g_a);
	struct NativeLockstepPeerLink *linkB = NativeArcadeNetplay_Link(&g_b);
	struct NativeLockstepSession *sessionA;
	struct NativeLockstepSession *sessionB;

	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(linkA != NULL);
	CHECK(linkB != NULL);
	CHECK(NativeLockstepPeerLink_Mode(linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	sessionA = NativeLockstepPeerLink_Session(linkA);
	sessionB = NativeLockstepPeerLink_Session(linkB);
	CHECK(NativeLockstepSession_FirstFault(sessionA) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(sessionA) == NULL);
	CHECK(NativeLockstepSession_FirstFault(sessionB) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(sessionB) == NULL);
	CHECK(memcmp(sessionA->matchIdentity, sessionB->matchIdentity, sizeof(sessionA->matchIdentity)) == 0);
	CHECK(memcmp(sessionA->matchIdentity, g_staleIdentity, sizeof(g_staleIdentity)) != 0);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(linkA) == drops);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(linkB) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(linkA) == 0u);
	return 0;
}

/* TestInRaceDivergenceFromPoll's real race frames, as a helper: both
 * sessions run frames over the real links (submit, compose and send, Tick to
 * poll, take, record), A's simulation drifting from divergeFrame on, until
 * both flows show DESYNC on RESULTS. */
static int RaceUntilDesync(uint32_t divergeFrame)
{
	struct NativeCanonicalInputPadV1 pad;
	struct NativeLockstepPeerLink *linkA = NativeArcadeNetplay_Link(&g_a);
	struct NativeLockstepPeerLink *linkB = NativeArcadeNetplay_Link(&g_b);
	struct NativeLockstepSession *sessionA;
	struct NativeLockstepSession *sessionB;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	const uint32_t detectFrame = divergeFrame + NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY + 1u;
	uint32_t frame;
	uint32_t spin;
	int left = 0;
	int takenA;
	int takenB;
	int recordOk;

	CHECK(linkA != NULL);
	CHECK(linkB != NULL);
	sessionA = NativeLockstepPeerLink_Session(linkA);
	sessionB = NativeLockstepPeerLink_Session(linkB);
	for (frame = 0; (frame <= detectFrame) && !left; frame++)
	{
		MakeTestPad(&pad, g_a.localSlot, frame);
		CHECK(NativeLockstepSession_SubmitLocalInput(sessionA, frame, &pad) == 1);
		MakeTestPad(&pad, g_b.localSlot, frame);
		CHECK(NativeLockstepSession_SubmitLocalInput(sessionB, frame, &pad) == 1);
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkA, frame) == 1);
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkB, frame) == 1);

		takenA = 0;
		takenB = 0;
		for (spin = 0; (spin < DRIVE_BUDGET) && !(takenA && takenB); spin++)
		{
			TickBoth(0u, 0u, 0u, &actionA, &actionB);
			CHECK(actionA == ACT_NONE);
			CHECK(actionB == ACT_NONE);
			if ((ScreenOf(&g_a) != NATIVE_ARCADE_FLOW_SCREEN_RACING) || (ScreenOf(&g_b) != NATIVE_ARCADE_FLOW_SCREEN_RACING))
			{
				left = 1;
				break;
			}
			if (!takenA)
			{
				takenA = TakeAndRecord(sessionA, frame, (frame >= divergeFrame) ? 3u : 0u, &recordOk) == NATIVE_LOCKSTEP_SESSION_OK;
				CHECK(recordOk);
			}
			if (!takenB)
			{
				takenB = TakeAndRecord(sessionB, frame, 0u, &recordOk) == NATIVE_LOCKSTEP_SESSION_OK;
				CHECK(recordOk);
			}
		}
		CHECK(left || (takenA && takenB));
	}
	CHECK(left);
	if (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		CHECK(TickUntilLeft(&g_a, NATIVE_ARCADE_FLOW_SCREEN_RACING, ACT_NONE));
	}
	if (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		CHECK(TickUntilLeft(&g_b, NATIVE_ARCADE_FLOW_SCREEN_RACING, ACT_NONE));
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(EndReasonOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	return 0;
}

/*
 * LR-14, the live gate's race 2 -> race 3 transition: a DESYNC followed by
 * REMATCH. Both show DESYNC; A confirms REMATCH while B stays on RESULTS and
 * keeps resending its old match's bundles, one per tick, to A's new
 * (HANDSHAKING) rematch link, which stages them. B then confirms REMATCH.
 * Both reach READY on the rematch config with no fault: A's link dropped
 * every stale record on replay instead of faulting MATCH_IDENTITY. The drops
 * then reach the next race's end-of-race record on A, although the rematch
 * link was closed at RELINK in between.
 */
static int TestRematchAfterDesyncDropsStaleBundles(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayRaceEnd raceEnd;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t i;

	/* The take refuses NULLs and an uninitialized adapter. */
	CHECK(NativeArcadeNetplay_TakeRaceEnd(NULL, &raceEnd) == 0);
	memset(&g_probe, 0, sizeof(g_probe));
	g_probe.raceEndPending = 1u;
	CHECK(NativeArcadeNetplay_TakeRaceEnd(&g_probe, &raceEnd) == 0);

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitStalePair(&fixture, TEST_STALE_DESYNC_A_PORT, TEST_STALE_DESYNC_B_PORT, REMATCH_WAIT_TIMEOUT_TICKS));
	CHECK(NativeArcadeNetplay_TakeRaceEnd(&g_a, NULL) == 0);
	CHECK(ExpectNoRaceEnd(&g_a) == 0);
	CHECK(EnterAndRaceInitialized());
	CHECK(ExpectNoRaceEnd(&g_a) == 0);
	CHECK(KeepStaleBundles() == 0);
	CHECK(RaceUntilDesync(6u) == 0);
	/* Race 1 ended in DESYNC on both, with nothing dropped yet. */
	CHECK(ExpectRaceEnd(&g_a, 1u, NATIVE_ARCADE_FLOW_END_DESYNC, 0u) == 0);
	CHECK(ExpectRaceEnd(&g_b, 1u, NATIVE_ARCADE_FLOW_END_DESYNC, 0u) == 0);
	CHECK(DwellBoth() == 0);

	/* A confirms REMATCH; B, on RESULTS, keeps sending the old bundles. */
	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_NONE);
	for (i = 0; i < STALE_BUNDLE_COUNT; i++)
	{
		CHECK(SendStale(TEST_STALE_DESYNC_A_PORT, i) == 0);
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
		CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	}

	/* B stops and confirms REMATCH too; both reach READY, then SELECT. */
	TickBoth(0u, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(DriveBothUntil(ACT_BEGIN_SELECT));
	CHECK(ExpectRematchReadyClean(STALE_BUNDLE_COUNT) == 0);
	CHECK(ExpectNoRaceEnd(&g_a) == 0);

	/* On through the select and the relink (which closes A's rematch link)
	 * to race 2, which finishes: its end-of-race record carries the drops. */
	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(NativeArcadeNetplay_Link(&g_a)) == 0u);
	TickBoth(0u, 0u, 1u, &actionA, &actionB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ExpectRaceEnd(&g_a, 2u, NATIVE_ARCADE_FLOW_END_FINISHED, STALE_BUNDLE_COUNT) == 0);
	CHECK(ExpectRaceEnd(&g_b, 2u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);
	/* Once per race: RESULTS ticks latch nothing more. */
	TickBoth(0u, 0u, 1u, &actionA, &actionB);
	CHECK(ExpectNoRaceEnd(&g_a) == 0);

	/* Shutdown drops a latched record. */
	g_a.raceEndPending = 1u;
	ShutdownBoth();
	CHECK(ExpectNoRaceEnd(&g_a) == 0);
	return 0;
}

/*
 * LR-14 after a pre-race failure (RL-11): A's race fails locally, so A is on
 * RESULTS (LINK ERROR) while B is still held on RACING and keeps resending
 * its early bundles. A confirms REMATCH once its dwell ends; B's resends
 * reach A's rematch link. The real 900-period start wait would outlast the
 * 300-tick rematch wait, so the test ends B's stale sending itself after
 * three ticks: B's race fails too, B passes its dwell and confirms REMATCH,
 * well inside A's production 300-tick rematch wait. Both reach READY with no
 * fault, A dropped every stale record, and A's rematch wait had not expired
 * when both were READY.
 */
static int TestRematchAfterPreRaceFailureDropsStaleBundles(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t ticksInRematchWait = 0u;
	uint32_t tick;
	uint32_t i;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitStalePair(&fixture, TEST_STALE_PRE_RACE_A_PORT, TEST_STALE_PRE_RACE_B_PORT,
		NATIVE_ARCADE_FLOW_DEFAULT_REMATCH_WAIT_TIMEOUT_TICKS));
	CHECK(g_a.config.timings.rematchWaitTimeoutTicks == 300u);
	CHECK(EnterAndRaceInitialized());
	CHECK(KeepStaleBundles() == 0);

	/* A's race fails before it starts; B is held on RACING. */
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_a) == 1);
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(EndReasonOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ExpectRaceEnd(&g_a, 1u, NATIVE_ARCADE_FLOW_END_LINK_ERROR, 0u) == 0);
	CHECK(ExpectNoRaceEnd(&g_b) == 0);
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}

	/* A confirms REMATCH; held B keeps resending. */
	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_NONE);
	for (i = 0; i < STALE_BUNDLE_COUNT; i++)
	{
		CHECK(SendStale(TEST_STALE_PRE_RACE_A_PORT, i) == 0);
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
		ticksInRematchWait += 1u;
	}

	/* B's stale sending ends: its race fails too, it passes its dwell, and
	 * it confirms REMATCH. */
	CHECK(NativeArcadeNetplay_ReportLocalRaceFailure(&g_b) == 1);
	TickBoth(0u, 0u, 0u, &actionA, &actionB);
	ticksInRematchWait += 1u;
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ExpectRaceEnd(&g_b, 1u, NATIVE_ARCADE_FLOW_END_LINK_ERROR, 0u) == 0);
	for (tick = 0; tick <= RESULTS_DWELL_TICKS; tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		ticksInRematchWait += 1u;
	}
	TickBoth(0u, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	ticksInRematchWait += 1u;

	/* Until A leaves REMATCH_WAIT: to MATCH_FOUND (READY), not EXIT. */
	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT); tick++)
	{
		CHECK(NativeArcadeNetplay_GetView(&g_a, &view) == 1);
		CHECK(view.ticksInScreen < g_a.config.timings.rematchWaitTimeoutTicks);
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		ticksInRematchWait += 1u;
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(ticksInRematchWait < g_a.config.timings.rematchWaitTimeoutTicks);
	CHECK(DriveBothUntil(ACT_BEGIN_SELECT));
	CHECK(ExpectRematchReadyClean(STALE_BUNDLE_COUNT) == 0);

	ShutdownBoth();
	return 0;
}

/*
 * LR-14 on a clean finish: the finish linger overlaps the return load
 * (LR-13), so B, finished and on RESULTS, still sends its old match's
 * bundles after A has confirmed REMATCH. A's rematch link drops them on
 * replay; both reach READY with no fault. A late copy that arrives after
 * the new link is RUNNING is dropped there too.
 */
static int TestRematchDuringFinishLingerDropsStaleBundles(void)
{
	struct NativeMatchConfigV1 fixture;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;
	uint32_t i;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitStalePair(&fixture, TEST_STALE_LINGER_A_PORT, TEST_STALE_LINGER_B_PORT, REMATCH_WAIT_TIMEOUT_TICKS));
	CHECK(EnterAndRaceInitialized());
	CHECK(KeepStaleBundles() == 0);
	CHECK(FinishAndDwell());
	CHECK(ExpectRaceEnd(&g_a, 1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);
	CHECK(ExpectRaceEnd(&g_b, 1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);

	/* A confirms REMATCH; B lingers on its old, still RUNNING link. */
	TickBoth(BTN_CROSS, 0u, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_NONE);
	for (i = 0; i < STALE_BUNDLE_COUNT; i++)
	{
		CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_b)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
		CHECK(SendStale(TEST_STALE_LINGER_A_PORT, i) == 0);
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
		CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
		CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	}

	TickBoth(0u, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(DriveBothUntil(ACT_BEGIN_SELECT));
	CHECK(ExpectRematchReadyClean(STALE_BUNDLE_COUNT) == 0);

	/* A late copy, from B's port, reaches A's RUNNING rematch link. */
	CHECK(SendStale(TEST_STALE_LINGER_A_PORT, 0u) == 0);
	for (tick = 0; (tick < DRIVE_BUDGET) &&
		(NativeLockstepPeerLink_DroppedForeignBundleCount(NativeArcadeNetplay_Link(&g_a)) < STALE_BUNDLE_COUNT + 1u);
		tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(ExpectRematchReadyClean(STALE_BUNDLE_COUNT + 1u) == 0);

	ShutdownBoth();
	return 0;
}

/* A MATCH_FOUND hold long enough to drop a late copy and fault both rematch
 * links before either moves on to SELECT. */
#define STALE_RESTART_HOLD_TICKS 40u

/*
 * LR-35's restart path: a drop on a rematch link that then goes PEER_LOST
 * and is restarted still reaches the next end-of-race record. After a clean
 * race 1 both confirm REMATCH and hold on MATCH_FOUND with their rematch
 * links RUNNING. A late stale copy from B's port is dropped by A's link
 * (count 1). A corrupted bundle each way then faults both links: each lobby
 * reads PEER_LOST and MATCH_FOUND answers RESTART_LOBBY, which closes that
 * link and opens a new one (whose count starts at 0). Both link again, go
 * through the select, and finish race 2: A's end-of-race record carries the
 * 1, B's carries 0.
 */
static int TestRematchLinkLostKeepsStaleDrop(void)
{
	struct NativeMatchConfigV1 fixture;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	uint32_t tick;
	int restartedA = 0;
	int restartedB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(InitStalePairHold(&fixture, TEST_STALE_RESTART_A_PORT, TEST_STALE_RESTART_B_PORT, REMATCH_WAIT_TIMEOUT_TICKS,
		STALE_RESTART_HOLD_TICKS));
	CHECK(EnterAndRaceInitialized());
	CHECK(KeepStaleBundles() == 0);
	CHECK(FinishAndDwell());
	CHECK(ExpectRaceEnd(&g_a, 1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);
	CHECK(ExpectRaceEnd(&g_b, 1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);

	/* Both confirm REMATCH and reach READY: MATCH_FOUND on both. */
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	for (tick = 0; (tick < DRIVE_BUDGET) && !((ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND) &&
												 (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND));
		tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(NativeArcadeNetplay_Link(&g_a)) == 0u);

	/* A late copy, from B's port, reaches A's RUNNING rematch link. */
	CHECK(SendStale(TEST_STALE_RESTART_A_PORT, 0u) == 0);
	for (tick = 0; (tick < DRIVE_BUDGET) &&
		(NativeLockstepPeerLink_DroppedForeignBundleCount(NativeArcadeNetplay_Link(&g_a)) < 1u);
		tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA == ACT_NONE);
		CHECK(actionB == ACT_NONE);
	}
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(NativeArcadeNetplay_Link(&g_a)) == 1u);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND);

	/* Both rematch links fault; each lobby reads PEER_LOST and restarts. */
	CHECK(SendCorruptBundle(&g_b, TEST_STALE_RESTART_A_PORT));
	CHECK(SendCorruptBundle(&g_a, TEST_STALE_RESTART_B_PORT));
	for (tick = 0; (tick < DRIVE_BUDGET) && !(restartedA && restartedB); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK((actionA == ACT_NONE) || (actionA == ACT_RESTART_LOBBY));
		CHECK((actionB == ACT_NONE) || (actionB == ACT_RESTART_LOBBY));
		restartedA = restartedA || (actionA == ACT_RESTART_LOBBY);
		restartedB = restartedB || (actionB == ACT_RESTART_LOBBY);
	}
	CHECK(restartedA && restartedB);
	/* A's new link starts at 0; the 1 is carried by the adapter. */
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(NativeArcadeNetplay_Link(&g_a)) == 0u);
	CHECK(g_a.foreignDropsSinceRaceEnd == 1u);
	CHECK(ExpectNoRaceEnd(&g_a) == 0);

	/* Both link again and race; race 2's record on A carries the drop. */
	CHECK(DriveBothUntil(ACT_START_RACE));
	TickBoth(0u, 0u, 1u, &actionA, &actionB);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ExpectRaceEnd(&g_a, 2u, NATIVE_ARCADE_FLOW_END_FINISHED, 1u) == 0);
	CHECK(ExpectRaceEnd(&g_b, 2u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);

	ShutdownBoth();
	return 0;
}

/* ---- LR-S9 (docs/LOCKSTEP_RACE_MILESTONE.md LR-9, LR-50): the race hold
 * service ---- */

/* A width none of the peer link's routes uses. */
#define RACE_SERVICE_MARKER_BYTES 7u

static struct NativeArcadeNetplay g_before;
static struct NativeLockstepSession g_scratchSession;

/* Everything of Tick that RaceService must leave alone: the flow (screen,
 * every flow, rematch, and results timer), the menu input, the select
 * session, the outcome tracker and roster, the configs, the race-end record,
 * and every flag and counter outside the lobby, the drop tally, and the
 * launch agreement. */
static int KeptTickState(const struct NativeArcadeNetplay *now, const struct NativeArcadeNetplay *before)
{
	CHECK(memcmp(&now->flow, &before->flow, sizeof(now->flow)) == 0);
	CHECK(memcmp(&now->menuInput, &before->menuInput, sizeof(now->menuInput)) == 0);
	CHECK(memcmp(&now->select, &before->select, sizeof(now->select)) == 0);
	CHECK(memcmp(&now->outcome, &before->outcome, sizeof(now->outcome)) == 0);
	CHECK(memcmp(&now->roster, &before->roster, sizeof(now->roster)) == 0);
	CHECK(memcmp(&now->currentConfig, &before->currentConfig, sizeof(now->currentConfig)) == 0);
	CHECK(memcmp(&now->lastReadyConfig, &before->lastReadyConfig, sizeof(now->lastReadyConfig)) == 0);
	CHECK(memcmp(&now->lastOutcome, &before->lastOutcome, sizeof(now->lastOutcome)) == 0);
	CHECK(memcmp(&now->raceEnd, &before->raceEnd, sizeof(now->raceEnd)) == 0);
	CHECK(now->lastScreenSerial == before->lastScreenSerial);
	CHECK(now->pendingLinkFailure == before->pendingLinkFailure);
	CHECK(now->matchCount == before->matchCount);
	CHECK(now->lobbyBegun == before->lobbyBegun);
	CHECK(now->raceArmed == before->raceArmed);
	CHECK(now->rematchBlocked == before->rematchBlocked);
	CHECK(now->selectActive == before->selectActive);
	CHECK(now->relinked == before->relinked);
	CHECK(now->relinkBlocked == before->relinkBlocked);
	CHECK(now->selectSerial == before->selectSerial);
	CHECK(now->outcomeValid == before->outcomeValid);
	CHECK(now->raceConfigValid == before->raceConfigValid);
	CHECK(now->lobbyReadySeen == before->lobbyReadySeen);
	CHECK(now->lastReadyValid == before->lastReadyValid);
	CHECK(now->lastMenuEvent == before->lastMenuEvent);
	CHECK(now->localRaceFailure == before->localRaceFailure);
	CHECK(now->raceEndPending == before->raceEndPending);
	return 0;
}

/* Sends a marker from sender's socket to receiverPort, then takes every
 * datagram waiting on receiver's socket up to and including the marker.
 * Returns the launch-width datagrams taken before it, or UINT32_MAX when the
 * marker never arrives. Everything sent on this socket pair before the
 * marker has arrived once it has. */
static uint32_t CountLaunchRecordsBeforeMarker(struct NativeUdpTransport *receiver, struct NativeUdpTransport *sender,
	uint32_t receiverPort)
{
	static const uint8_t marker[RACE_SERVICE_MARKER_BYTES] = {0x4Du, 0x41u, 0x52u, 0x4Bu, 0x45u, 0x52u, 0x21u};
	uint8_t bytes[RECEIVE_BYTES];
	size_t byteCount = 0u;
	uint32_t records = 0u;
	uint32_t spins;

	if (!SendTo(sender, receiverPort, marker, sizeof(marker)))
	{
		return UINT32_MAX;
	}
	for (spins = 0u; spins < RECEIVE_SPIN_BUDGET; spins++)
	{
		if (NativeUdpTransport_Receive(receiver, bytes, sizeof(bytes), &byteCount, NULL) != NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			continue;
		}
		if ((byteCount == sizeof(marker)) && (memcmp(bytes, marker, sizeof(marker)) == 0))
		{
			return records;
		}
		records += (byteCount == LAUNCH_BYTES) ? 1u : 0u;
	}
	return UINT32_MAX;
}

/* A clean CAB2 frame-0 bundle of another match: the given config with its
 * master seed changed, at the adapter's input delay. */
static int ComposeOtherMatchBundle(const struct NativeMatchConfigV1 *config, uint8_t *bytes)
{
	struct NativeMatchConfigV1 other = *config;
	uint8_t slot = 0u;
	size_t size = 0u;

	other.masterSeed ^= UINT64_C(0x5A5A5A5A5A5A5A5A);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&other, ROLE_CAB2, &slot));
	NativeLockstepSession_Init(&g_scratchSession);
	CHECK(NativeLockstepSession_Open(&g_scratchSession, &other, NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY, slot));
	CHECK(NativeLockstepSession_ComposeBundle(&g_scratchSession, 0u, bytes, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES, &size));
	CHECK(size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	return 0;
}

/*
 * 36. LR-S9 (LR-9, LR-50): the race hold service over a real pair. As in
 * 33c, A commits and starts its race while every launch record it sends from
 * Tick is dropped before B reads it, so B is still PENDING on SELECT_RESULT
 * and A's linger still wants to send. From then on A only runs RaceService,
 * as the drive's hold would, while B ticks normally.
 * (a) RaceService(A, 0) drains B's frame-0 bundle into A's session and drops
 *     and counts one record of another match, exactly as Tick's step 2 does,
 *     and changes no flow, menu, select, outcome, or race-end state and not
 *     the launch agreement: no launch record reaches B.
 * (b) Each RaceService(A, 1) runs the launch intake, sends one launch record,
 *     and counts one linger tick; RaceService(A, 0) between them counts none.
 *     B commits and starts its race from the first such record, and A hears
 *     B's HEARD record through RaceService; A's linger then stops sending
 *     while still counting one tick per launch period. RaceService on B while
 *     it is still on SELECT_RESULT is a no-op. A's first Tick afterwards
 *     finishes the race, and its end-of-race record carries the one drop,
 *     counted once.
 */
static int TestRaceServiceHold(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 resolved;
	struct NativeLockstepPeerLink *linkA;
	struct NativeLockstepPeerLink *linkB;
	struct NativeLockstepSession *sessionA;
	struct NativeLockstepSessionFrameInputs inputs;
	struct NativeArcadeLaunchAgreement launchBefore;
	enum NativeArcadeFlowAction actionA = ACT_NONE;
	enum NativeArcadeFlowAction actionB = ACT_NONE;
	uint8_t digest[LAUNCH_DIGEST_BYTES];
	uint8_t fixtureDigest[LAUNCH_DIGEST_BYTES];
	uint8_t foreign[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint32_t lastSequenceA;
	uint32_t dropsBefore;
	uint32_t ticksBefore;
	uint32_t launchPeriods = 0u;
	uint32_t spins;
	uint32_t tick;
	uint8_t slotB = 0u;
	int startedA = 0;
	int startedB = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(IdleResolved(&fixture, 1u, &resolved));
	CHECK(NativeMatchConfigV1_Digest(&resolved, digest) == 1);
	CHECK(NativeMatchConfigV1_Digest(&fixture, fixtureDigest) == 1);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&resolved, ROLE_CAB2, &slotB));
	CHECK(PairToSelectResult(&fixture, TEST_RACE_SERVICE_A_PORT, TEST_RACE_SERVICE_B_PORT));

	/* 33c's setup: A commits and races; none of its Tick records reach B. */
	CHECK(TickAloneToRelink(&g_a));
	CHECK(TickAloneToRelink(&g_b));
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(LobbyStatusOf(&g_a) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(DropLaunchRecordsTo(&g_b, TEST_RACE_SERVICE_B_PORT, &g_a, 1u));
	lastSequenceA = g_a.launch.sequence;
	for (tick = 0; (tick < DRIVE_BUDGET) && !startedA; tick++)
	{
		actionB = NativeArcadeNetplay_Tick(&g_b, 0u, 0u);
		CHECK(actionB == ACT_NONE);
		CHECK(LobbyStatusOf(&g_b) == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
		CHECK(NativeArcadeLaunch_Status(&g_b.launch) == LAUNCH_PENDING);
		actionA = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK((actionA == ACT_NONE) || (actionA == ACT_START_RACE));
		startedA = (actionA == ACT_START_RACE);
		CHECK(DropLaunchRecordsTo(&g_b, TEST_RACE_SERVICE_B_PORT, &g_a, g_a.launch.sequence - lastSequenceA));
		lastSequenceA = g_a.launch.sequence;
	}
	CHECK(startedA);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
	CHECK(NativeArcadeLaunch_Status(&g_a.launch) == LAUNCH_COMMITTED);
	CHECK(NativeArcadeLaunch_ShouldSend(&g_a.launch) == 1);
	CHECK(g_a.launch.peerHeard == 0u);
	CHECK(g_b.launch.acceptedCount == 0u);
	linkA = NativeArcadeNetplay_Link(&g_a);
	linkB = NativeArcadeNetplay_Link(&g_b);
	CHECK((linkA != NULL) && (linkB != NULL));
	CHECK(NativeLockstepPeerLink_Mode(linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	sessionA = NativeLockstepPeerLink_Session(linkA);

	/* A non-RACING adapter is a no-op, even with a launch record waiting in
	 * its aux inbox and launchPeriod set (the record carries another digest,
	 * so B's next Tick counts it as MISMATCH and does not commit on it). */
	CHECK(InjectLaunch(&g_b, ROLE_CAB1, fixtureDigest, 0u, 900u));
	memcpy(&g_before, &g_b, sizeof(g_b));
	NativeArcadeNetplay_RaceService(&g_b, 1);
	NativeArcadeNetplay_RaceService(&g_b, 0);
	CHECK(memcmp(&g_b, &g_before, sizeof(g_b)) == 0);

	/* (a) B sends a record of another match, then its own frame 0. */
	dropsBefore = g_a.foreignDropsSinceRaceEnd;
	CHECK(dropsBefore == 0u);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(linkA) == 0u);
	CHECK(ComposeOtherMatchBundle(&resolved, foreign) == 0);
	CHECK(SendTo(&linkB->transport, TEST_RACE_SERVICE_A_PORT, foreign, sizeof(foreign)));
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(linkB, 0u) == 1);
	memcpy(&g_before, &g_a, sizeof(g_a));
	for (spins = 0u; (spins < RECEIVE_SPIN_BUDGET) && (sessionA->peers[slotB].occupancyMask == 0u); spins++)
	{
		NativeArcadeNetplay_RaceService(&g_a, 0);
	}
	CHECK(sessionA->peers[slotB].occupancyMask != 0u);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(linkA) == 1u);
	CHECK(g_a.linkForeignDropsSeen == 1u);
	CHECK(g_a.foreignDropsSinceRaceEnd == dropsBefore + 1u);
	CHECK(NativeLockstepSession_TakeFrameInputs(sessionA, 0u, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_FirstFault(sessionA) == NULL);
	CHECK(KeptTickState(&g_a, &g_before) == 0);
	CHECK(memcmp(&g_a.launch, &g_before.launch, sizeof(g_a.launch)) == 0);
	CHECK(CountLaunchRecordsBeforeMarker(&linkB->transport, &linkA->transport, TEST_RACE_SERVICE_B_PORT) == 0u);

	/* (b) One launch period: one record, one linger tick; B commits on it. */
	memcpy(&launchBefore, &g_a.launch, sizeof(launchBefore));
	NativeArcadeNetplay_RaceService(&g_a, 0);
	NativeArcadeNetplay_RaceService(&g_a, 0);
	CHECK(memcmp(&g_a.launch, &launchBefore, sizeof(launchBefore)) == 0);
	ticksBefore = g_a.launch.ticksSinceCommit;
	NativeArcadeNetplay_RaceService(&g_a, 1);
	launchPeriods += 1u;
	CHECK(g_a.launch.sequence == launchBefore.sequence + 1u);
	CHECK(g_a.launch.ticksSinceCommit == ticksBefore + 1u);
	CHECK(g_a.launch.heardSent == 1u);
	for (spins = 0u; (spins < RECEIVE_SPIN_BUDGET) && (NativeLockstepPeerLink_AuxCount(linkB) < 2u); spins++)
	{
		NativeLockstepPeerLink_Poll(linkB);
	}
	/* The injected MISMATCH record, then A's record. */
	CHECK(NativeLockstepPeerLink_AuxCount(linkB) == 2u);
	CHECK(NativeArcadeNetplay_Tick(&g_b, 0u, 0u) == ACT_START_RACE);
	startedB = 1;
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeLaunch_Status(&g_b.launch) == LAUNCH_COMMITTED);
	CHECK(g_b.launch.acceptedCount == 1u);
	CHECK(g_b.launch.mismatchCount == 1u);
	CHECK(g_b.launch.peerHeard == 1u);
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_b), &resolved, sizeof(resolved)) == 0);
	CHECK(KeptTickState(&g_a, &g_before) == 0);

	/* A hears B's HEARD through RaceService; every launch period counts one
	 * linger tick and sends one record while the linger wants to. */
	for (spins = 0u; (spins < RECEIVE_SPIN_BUDGET) && (g_a.launch.peerHeard == 0u); spins++)
	{
		NativeArcadeNetplay_RaceService(&g_a, 0);
		if (NativeLockstepPeerLink_AuxCount(linkA) == 0u)
		{
			continue;
		}
		memcpy(&launchBefore, &g_a.launch, sizeof(launchBefore));
		NativeArcadeNetplay_RaceService(&g_a, 1);
		launchPeriods += 1u;
		CHECK(g_a.launch.ticksSinceCommit == launchBefore.ticksSinceCommit + 1u);
		CHECK(NativeLockstepPeerLink_AuxCount(linkA) == 0u);
		/* The intake runs before the send: the call that takes B's HEARD record
		 * already sends nothing (heardSent and peerHeard both set). */
		CHECK(g_a.launch.heardSent == 1u);
		CHECK(g_a.launch.sequence == launchBefore.sequence + ((g_a.launch.peerHeard == 0u) ? 1u : 0u));
	}
	CHECK(g_a.launch.peerHeard == 1u);
	CHECK(NativeArcadeLaunch_ShouldSend(&g_a.launch) == 0);
	CHECK(KeptTickState(&g_a, &g_before) == 0);

	/* The linger has stopped sending, and still counts one tick per period. */
	for (tick = 0; tick < 5u; tick++)
	{
		memcpy(&launchBefore, &g_a.launch, sizeof(launchBefore));
		NativeArcadeNetplay_RaceService(&g_a, 1);
		launchPeriods += 1u;
		CHECK(g_a.launch.sequence == launchBefore.sequence);
		CHECK(g_a.launch.ticksSinceCommit == launchBefore.ticksSinceCommit + 1u);
	}
	CHECK(g_a.launch.ticksSinceCommit == g_before.launch.ticksSinceCommit + launchPeriods);
	CHECK(KeptTickState(&g_a, &g_before) == 0);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(g_a.foreignDropsSinceRaceEnd == dropsBefore + 1u);

	/* The flow resumes on A's next Tick; the drop is counted exactly once. */
	CHECK(startedB);
	TickBoth(0u, 0u, 1u, &actionA, &actionB);
	CHECK((actionA == ACT_NONE) && (actionB == ACT_NONE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(ExpectRaceEnd(&g_a, 1u, NATIVE_ARCADE_FLOW_END_FINISHED, dropsBefore + 1u) == 0);
	CHECK(ExpectRaceEnd(&g_b, 1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);

	ShutdownBoth();
	return 0;
}

/* 37. LR-S9 (LR-50): RaceService is a no-op for NULL, an uninitialized
 * adapter, and an initialized one on screen OFF, whatever launchPeriod. */
static int TestRaceServiceNoOps(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayConfig config;

	NativeArcadeNetplay_RaceService(NULL, 0);
	NativeArcadeNetplay_RaceService(NULL, 1);

	memset(&g_probe, 0xA5, sizeof(g_probe));
	g_probe.initialized = 0u;
	memcpy(&g_sentinel, &g_probe, sizeof(g_probe));
	NativeArcadeNetplay_RaceService(&g_probe, 0);
	NativeArcadeNetplay_RaceService(&g_probe, 1);
	CHECK(memcmp(&g_probe, &g_sentinel, sizeof(g_probe)) == 0);

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(MakeConfig(&config, &fixture, ROLE_CAB1, TEST_RACE_SERVICE_A_PORT, TEST_RACE_SERVICE_B_PORT));
	CHECK(NativeArcadeNetplay_Init(&g_probe, &config) == 1);
	CHECK(ScreenOf(&g_probe) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	memcpy(&g_sentinel, &g_probe, sizeof(g_probe));
	NativeArcadeNetplay_RaceService(&g_probe, 0);
	NativeArcadeNetplay_RaceService(&g_probe, 1);
	CHECK(memcmp(&g_probe, &g_sentinel, sizeof(g_probe)) == 0);
	NativeArcadeNetplay_Shutdown(&g_probe);
	return 0;
}

int main(void)
{
	CHECK(TestPure() == 0);
	CHECK(TestDormancy() == 0);
	CHECK(TestLobbyToRace() == 0);
	CHECK(TestBothRematch() == 0);
	CHECK(TestOneSidedRematch() == 0);
	CHECK(TestRejection() == 0);
	CHECK(TestStallTimeout() == 0);
	CHECK(TestReleaseToArmAcrossRaceEnd() == 0);
	CHECK(TestStaggeredEnter() == 0);
	CHECK(TestStaggeredRematch() == 0);
	CHECK(TestInRaceFaultFromPoll() == 0);
	CHECK(TestInRaceDivergenceFromPoll() == 0);
	CHECK(TestRematchBuildFailureBlocks() == 0);
	CHECK(TestBackFromRematchWait() == 0);
	CHECK(TestBeginFailureRecovers() == 0);
	CHECK(TestHookFaultBeforeTick() == 0);
	CHECK(TestHookDivergenceBeforeTick() == 0);
	CHECK(TestSelectScriptedPicks() == 0);
	CHECK(TestSelectIdleAutoPick() == 0);
	CHECK(TestSelectSameCharacterSameTick() == 0);
	CHECK(TestSelectPeerSilence() == 0);
	CHECK(TestSelectLaunchTimeout() == 0);
	CHECK(TestSelectRelinkBuildFailureBlocks() == 0);
	CHECK(TestSelectRematchThroughSelect() == 0);
	CHECK(TestSelectNonceAndAuxDiscard() == 0);
	CHECK(TestSelectFailureRematchAgrees() == 0);
	CHECK(TestSelectCannotStart() == 0);
	CHECK(TestSelectBackIgnored() == 0);
	CHECK(TestSelectOldLinkLost() == 0);
	CHECK(TestSelectView() == 0);
	CHECK(TestSelectLinkErrorClosesLink() == 0);
	CHECK(TestLocalMenuEvent() == 0);
	CHECK(TestLaunchSymmetric() == 0);
	CHECK(TestLaunchOneSidedRelink() == 0);
	CHECK(TestLaunchLostLastRecord() == 0);
	CHECK(TestLaunchStaleSelectRecordsIgnored() == 0);
	CHECK(TestLaunchCommitAtTimeoutEdge() == 0);
	CHECK(TestLaunchNoCommitAfterCloseLink() == 0);
	CHECK(TestLaunchLostHeardLingerCap() == 0);
	CHECK(TestLaunchReorderedDuplicated() == 0);
	CHECK(TestLaunchStaleAcrossRestartLobby() == 0);
	CHECK(TestLaunchStaleAcrossRelink() == 0);
	CHECK(TestLaunchStaleAcrossRematchAndSelect() == 0);
	CHECK(TestLaunchStaleAcrossCloseTitleEnter() == 0);
	CHECK(TestLaunchFaultDuringLinger() == 0);
	CHECK(TestLocalRaceFailure() == 0);
	CHECK(TestRematchAfterDesyncDropsStaleBundles() == 0);
	CHECK(TestRematchAfterPreRaceFailureDropsStaleBundles() == 0);
	CHECK(TestRematchDuringFinishLingerDropsStaleBundles() == 0);
	CHECK(TestRematchLinkLostKeepsStaleDrop() == 0);
	CHECK(TestRaceServiceHold() == 0);
	CHECK(TestRaceServiceNoOps() == 0);
	puts("native_arcade_netplay_test: passed");
	return 0;
}
