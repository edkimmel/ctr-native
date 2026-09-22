#include "platform/native_arcade_netplay.h"
#include "platform/native_lockstep_rematch.h"

#include "native_lockstep_peer_link_test_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Tests for the arcade-link host adapter (platform/native_arcade_netplay.c,
 * docs/GAME_LOOP_UI_MILESTONE.md section 2.3, Task 4). The socket tests run
 * two adapters in this one process -- A as CAB1_HUMAN and B as CAB2_HUMAN --
 * over real loopback UDP sockets through the real, unmodified lobby layer,
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

/* Bounds every outer test-driving loop; generous, not tuned. */
#define DRIVE_BUDGET 2000u
/* How long a rejected lobby is watched for an automatic restart. */
#define REJECT_WATCH_TICKS 200u

#define BTN_CROSS NATIVE_ARCADE_MENU_BUTTON_CROSS
#define BTN_DOWN NATIVE_ARCADE_MENU_BUTTON_DOWN
#define BTN_TRIANGLE NATIVE_ARCADE_MENU_BUTTON_TRIANGLE

#define ACT_NONE NATIVE_ARCADE_FLOW_ACTION_NONE
#define ACT_BEGIN_LOBBY NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY
#define ACT_RESTART_LOBBY NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY
#define ACT_START_RACE NATIVE_ARCADE_FLOW_ACTION_START_RACE
#define ACT_BEGIN_REMATCH NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH
#define ACT_CLOSE_LINK NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK
#define ACT_RETURN_TO_TITLE NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE

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
	return 1;
}

/* A is CAB1 on portA pointing at portB; B is CAB2 on portB pointing at
 * portA. */
static int InitPair(const struct NativeMatchConfigV1 *fixtureA, const struct NativeMatchConfigV1 *fixtureB, uint32_t portA,
	uint32_t portB)
{
	struct NativeArcadeNetplayConfig config;

	if (!MakeConfig(&config, fixtureA, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, portA, portB) ||
		!NativeArcadeNetplay_Init(&g_a, &config))
	{
		return 0;
	}
	if (!MakeConfig(&config, fixtureB, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, portB, portA) ||
		!NativeArcadeNetplay_Init(&g_b, &config))
	{
		return 0;
	}
	return 1;
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

/* Init, Enter, and drive both adapters into RACING on the shared fixture. */
static int EnterAndRace(const struct NativeMatchConfigV1 *fixture, uint32_t portA, uint32_t portB)
{
	if (!InitPair(fixture, fixture, portA, portB))
	{
		return 0;
	}
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
	CHECK(config.retransmitIntervalTicks == 15u);
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

	/* Too many candidates. */
	bad = base;
	bad.candidateCount = NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u;
	CHECK(InitRejectsUntouched(&bad));

	/* Zero attempt budget. */
	bad = base;
	bad.attemptTicksPerCandidate = 0u;
	CHECK(InitRejectsUntouched(&bad));

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

	/* Success: stored config, fixture proposal, dormant flow, local slot. */
	memset(&g_probe, 0xA5, sizeof(g_probe));
	CHECK(NativeArcadeNetplay_Init(&g_probe, &base) == 1);
	CHECK(g_probe.initialized == 1u);
	CHECK(g_probe.localSlot == 0u);
	CHECK(g_probe.lobbyBegun == 0u);
	CHECK(g_probe.raceArmed == 0u);
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

	/* A zero struct is uninitialized: inert, and Shutdown is safe twice. */
	memset(&g_b, 0, sizeof(g_b));
	CHECK(NativeArcadeNetplay_Enter(&g_b) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Tick(&g_b, BTN_CROSS, 0u) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);
	NativeArcadeNetplay_Shutdown(&g_b);
	NativeArcadeNetplay_Shutdown(&g_b);
	NativeArcadeNetplay_Shutdown(NULL);
	CHECK(NativeArcadeNetplay_Link(&g_b) == NULL);

	/* Shutdown twice on the dormant adapter, which stays dormant. */
	NativeArcadeNetplay_Shutdown(&g_a);
	NativeArcadeNetplay_Shutdown(&g_a);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeNetplay_Tick(&g_a, 0u, 0u) == ACT_NONE);
	CHECK(NativeArcadeNetplay_Link(&g_a) == NULL);
	return 0;
}

/* 3. Lobby to race on the fixture. */
static int TestLobbyToRace(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayView view;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
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
	CHECK(memcmp(agreedA, &fixture, sizeof(fixture)) == 0);
	CHECK(memcmp(agreedB, &fixture, sizeof(fixture)) == 0);

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

/* 4. Both choose REMATCH: a new link on the derived seed, and a new race. */
static int TestBothRematch(void)
{
	struct NativeMatchConfigV1 fixture;
	struct NativeArcadeNetplayView view;
	enum NativeArcadeFlowAction actionA;
	enum NativeArcadeFlowAction actionB;
	const struct NativeMatchConfigV1 *agreedA;
	const struct NativeMatchConfigV1 *agreedB;
	uint64_t expectedSeed = 0u;

	NativeLockstepPeerLinkFixture_BuildConfig(&fixture);
	CHECK(NativeArcadeNetplay_DeriveRematchSeed(&fixture, &expectedSeed) == 1);
	CHECK(EnterAndRace(&fixture, TEST_REMATCH_A_PORT, TEST_REMATCH_B_PORT));
	CHECK(FinishAndDwell());
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) != NULL);

	TickBoth(BTN_CROSS, BTN_CROSS, 0u, &actionA, &actionB);
	CHECK(actionA == ACT_BEGIN_REMATCH);
	CHECK(actionB == ACT_BEGIN_REMATCH);
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(NativeArcadeNetplay_AgreedConfig(&g_a) == NULL);
	/* A brand-new link is handshaking on the rematch config. */
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_a)) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_Mode(NativeArcadeNetplay_Link(&g_b)) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	CHECK(DriveBothUntil(ACT_START_RACE));
	CHECK(ScreenOf(&g_a) == NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(ScreenOf(&g_b) == NATIVE_ARCADE_FLOW_SCREEN_RACING);

	agreedA = NativeArcadeNetplay_AgreedConfig(&g_a);
	agreedB = NativeArcadeNetplay_AgreedConfig(&g_b);
	CHECK(agreedA != NULL);
	CHECK(agreedB != NULL);
	CHECK(memcmp(agreedA, agreedB, sizeof(*agreedA)) == 0);
	CHECK(agreedA->masterSeed == expectedSeed);
	CHECK(agreedA->masterSeed != fixture.masterSeed);
	CHECK(agreedA->trackID == fixture.trackID);

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
	puts("native_arcade_netplay_test: passed");
	return 0;
}
