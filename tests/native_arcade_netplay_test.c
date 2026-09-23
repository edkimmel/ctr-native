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
 * RELINK -> READY, and the race runs on the resolved config). The socket
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
 * Fixed loopback test ports, in the 48400-48499 band, distinct from every
 * other test file's own bands (tests/native_lobby_state_test.c uses
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
	CHECK(FinishAndDwell());
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);
	first = *NativeArcadeNetplay_AgreedConfig(&g_a);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&first, &expectedSeed) == 1);

	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
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

/* Ticks each adapter until the given screen has been left; returns 0 if the
 * budget ran out. Every action on the way must be NONE. */
static int TickUntilLeft(struct NativeArcadeNetplay *netplay, uint32_t screen)
{
	uint32_t tick;

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(netplay) == screen); tick++)
	{
		if (NativeArcadeNetplay_Tick(netplay, 0u, 0u) != ACT_NONE)
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

	CHECK(TickUntilLeft(&g_a, NATIVE_ARCADE_FLOW_SCREEN_RACING));
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
		CHECK(TickUntilLeft(&g_a, NATIVE_ARCADE_FLOW_SCREEN_RACING));
	}
	if (ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		CHECK(TickUntilLeft(&g_b, NATIVE_ARCADE_FLOW_SCREEN_RACING));
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
 * A's current config is corrupted after the race (lapCount 0), so neither
 * the seed nor the config can be built. A opens no lobby at all, refuses
 * every restart (even once the old valid config is put back), and times out
 * to OPPONENT LEFT; RETURN_TO_TITLE clears the block. */
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

	g_a.currentConfig.lapCount = 0u;
	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(g_a.rematchBlocked == 1u);
	CHECK(g_a.lobbyBegun == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	/* The corrupted config was not replaced by a guess either. */
	CHECK(g_a.currentConfig.lapCount == 0u);
	CHECK(NativeArcadeNetplay_Link(&g_b) != NULL);
	/* Put the old, valid agreed config (with the old seed) back: from here on
	 * only the block itself stops a restart from beginning a lobby on the old
	 * seed, so a Link that stays NULL proves RestartLobby refuses. */
	g_a.currentConfig = agreed;

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
		CHECK(action == ACT_NONE);
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
	CHECK(g_a.relinked == 0u);
	CHECK(g_a.outcomeValid == 0u);
	CHECK(NativeArcadeNetplay_Select(&g_a) == NULL);
	/* RESULTS shows the select base: nothing was resolved. */
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &fixture, sizeof(fixture)) == 0);

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

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT); tick++)
	{
		action = NativeArcadeNetplay_Tick(&g_a, 0u, 0u);
		CHECK(action != ACT_START_RACE);
		if (relinked)
		{
			ticksSinceRelink += 1u;
			CHECK((action == ACT_NONE) || (action == ACT_RESTART_LOBBY));
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
	/* RESULTS shows the resolved config the relink proposed. */
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), &expected, sizeof(expected)) == 0);

	ShutdownBoth();
	return 0;
}

/* 22b. MS-7: a resolved config that cannot be built blocks the relink. A's
 * current config is corrupted during the result hold (lapCount 0), so
 * RELINK cannot build: A closes the old link, begins nothing, refuses every
 * restart (even once the valid base is put back), and shows LINK ERROR at
 * the launch timeout; it never races on the base config. BEGIN_REMATCH
 * clears the block. */
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
	g_a.currentConfig.lapCount = 0u;

	for (tick = 0; (tick < DRIVE_BUDGET) && (ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT); tick++)
	{
		TickBoth(0u, 0u, 0u, &actionA, &actionB);
		CHECK(actionA != ACT_START_RACE);
		CHECK(actionB != ACT_START_RACE);
		if (relinked)
		{
			ticksSinceRelink += 1u;
			CHECK((actionA == ACT_NONE) || (actionA == ACT_RESTART_LOBBY));
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
			/* Not replaced by a guess. */
			CHECK(g_a.currentConfig.lapCount == 0u);
			/* The valid base back: only the block stops a restart now. */
			g_a.currentConfig = fixture;
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
 * input), and the stale-datagram guard at BEGIN_SELECT. */
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
	/* The junk was discarded unread, never fed to the session. */
	CHECK(NativeMatchSelectSession_DroppedMalformed(&g_a.select) == 0u);
	CHECK(NativeMatchSelectSession_Status(&g_a.select) == (uint32_t)NATIVE_MATCH_SELECT_STATUS_PICKING);

	/* The select still completes and both race. */
	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(memcmp(NativeArcadeNetplay_AgreedConfig(&g_a), NativeArcadeNetplay_AgreedConfig(&g_b),
			  sizeof(struct NativeMatchConfigV1)) == 0);

	ShutdownBoth();
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
	puts("native_arcade_netplay_test: passed");
	return 0;
}
