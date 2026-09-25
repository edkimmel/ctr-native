#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host_internal.h"
#include "platform/native_arcade_netplay.h"
#include "platform/native_arcade_race_drive.h"
#include "platform/native_canonical_state_v4.h"
#include "platform/native_match_select_rules.h"

#include "native_arcade_link_loopback_test_fixture.h"

#include <platform.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Tests for the arcade-link host glue (platform/native_arcade_link_host.c,
 * docs/GAME_LOOP_UI_MILESTONE.md section 2.6, Task 6b-1). The glue is a
 * process-wide singleton, so every test starts and ends with Shutdown.
 *
 * Link-mode tests open one real loopback socket and point the single
 * candidate at a port nobody listens on, so the lobby stays CONNECTING for
 * the whole default 150-tick attempt budget. Every advance is tick-counted
 * (NativeArcadeLinkHost_Tick call count), never wall-clock, so the test is
 * not flaky by construction.
 *
 * Fixed loopback test ports, in the 48500-48519 band, distinct from every
 * other test file's own bands (tests/native_arcade_netplay_test.c uses
 * 48400-48499, tests/native_lobby_state_test.c 48300-48399).
 */
#define TEST_LINK_LOCAL_PORT 48500u
#define TEST_LINK_DEAD_PEER_PORT 48501u
#define TEST_REPLACE_LOCAL_PORT 48502u
#define TEST_REPLACE_DEAD_PEER_PORT 48503u
#define TEST_REPLACE_SECOND_LOCAL_PORT 48504u
#define TEST_ENTROPY_LOCAL_PORT 48505u
#define TEST_ENTROPY_DEAD_PEER_PORT 48506u
/* A real two-sided select: the host as CAB1 against a test-owned adapter
 * as CAB2 (MS-8). */
#define TEST_SELECT_HOST_PORT 48507u
#define TEST_SELECT_PEER_PORT 48508u
/* The local menu event in LINK mode, against a dead peer. */
#define TEST_MENU_EVENT_LOCAL_PORT 48509u
#define TEST_MENU_EVENT_DEAD_PEER_PORT 48510u
/* The race-launch host API (RL-S6): agreed config, local race failure, and
 * racing query, the host as CAB1 against a test-owned adapter as CAB2. */
#define TEST_RACE_FAILURE_HOST_PORT 48511u
#define TEST_RACE_FAILURE_PEER_PORT 48512u
/* The race pacing switch (LR-7): LINK mode against a dead peer. */
#define TEST_RACE_PACING_LOCAL_PORT 48513u
#define TEST_RACE_PACING_DEAD_PEER_PORT 48514u
/* The race drive glue (LR-S9): the host as CAB1 against a test-owned
 * adapter as CAB2 driven by a test-owned second drive core; two pairs, used
 * in turn. */
#define TEST_DRIVE_HOST_PORT 48515u
#define TEST_DRIVE_PEER_PORT 48516u
#define TEST_DRIVE2_HOST_PORT 48517u
#define TEST_DRIVE2_PEER_PORT 48518u

/* Bounds every loop that waits for the loopback pair; generous, not tuned. */
#define PAIR_BUDGET 4000u

/* The select preview schedule (host: 30 ticks per opponent step, 600-tick
 * countdown). */
#define PREVIEW_STEP_TICKS 30u
#define PREVIEW_ITEM_TICKS 600u

/* Well under the default 150-tick per-candidate attempt budget. */
#define LOBBY_TICKS 5u

#define ACT_NONE ((uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE)

/*
 * The platform's fixed VBlank pacing switch (include/platform.h), stubbed:
 * the host glue's one platform call (docs/LOCKSTEP_RACE_MILESTONE.md LR-7).
 * It keeps the switch as the platform does (off at process start) and counts
 * every call, so a test sees both the state and whether the host touched it.
 */
static int g_pacing;
static uint32_t g_pacingCalls;

void Platform_SetFixedVBlankPacing(int enabled)
{
	g_pacing = (enabled != 0) ? 1 : 0;
	g_pacingCalls++;
}

/* The switch is `pacing` and the host made no call since `calls`. */
static int CheckPacingUntouched(int pacing, uint32_t calls)
{
	CHECK(g_pacing == pacing);
	CHECK(g_pacingCalls == calls);
	return 0;
}

/* No agreed config (RL-S6): 0, and *out untouched. */
static int CheckNoAgreedConfig(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 sentinel;

	memset(&config, 0xA5, sizeof(config));
	memcpy(&sentinel, &config, sizeof(config));
	CHECK(NativeArcadeLinkHost_GetAgreedConfig(&config) == 0);
	CHECK(memcmp(&config, &sentinel, sizeof(config)) == 0);
	CHECK(NativeArcadeLinkHost_GetAgreedConfig(NULL) == 0);
	return 0;
}

/* No agreed match (and so no agreed config): 0, and *out untouched. */
static int CheckNoAgreedMatch(void)
{
	struct NativeArcadeLinkHostMatch match;
	struct NativeArcadeLinkHostMatch sentinel;

	memset(&match, 0xA5, sizeof(match));
	sentinel = match;
	CHECK(NativeArcadeLinkHost_GetAgreedMatch(&match) == 0);
	CHECK(memcmp(&match, &sentinel, sizeof(match)) == 0);
	CHECK(NativeArcadeLinkHost_GetAgreedMatch(NULL) == 0);
	CHECK(CheckNoAgreedConfig() == 0);
	return 0;
}

/* Off RACING (RL-S6): the racing query reads 0 and a local race failure is
 * ignored. */
static int CheckNotRacing(void)
{
	CHECK(NativeArcadeLinkHost_Racing() == 0u);
	CHECK(NativeArcadeLinkHost_ReportRaceFailure() == 0);
	CHECK(NativeArcadeLinkHost_Racing() == 0u);
	return 0;
}

/* No end-of-race record (LR-S6): 0, and *out untouched. */
static int CheckNoRaceEnd(void)
{
	struct NativeArcadeLinkHostRaceEnd raceEnd;
	struct NativeArcadeLinkHostRaceEnd sentinel;

	memset(&raceEnd, 0xA5, sizeof(raceEnd));
	sentinel = raceEnd;
	CHECK(NativeArcadeLinkHost_TakeRaceEnd(&raceEnd) == 0);
	CHECK(memcmp(&raceEnd, &sentinel, sizeof(raceEnd)) == 0);
	CHECK(NativeArcadeLinkHost_TakeRaceEnd(NULL) == 0);
	return 0;
}

/* Exactly one end-of-race record (LR-S6), with this race number, end
 * reason, and foreign-bundle drop count; then none. */
static int CheckRaceEnd(uint32_t raceNumber, uint32_t endReason, uint32_t drops)
{
	struct NativeArcadeLinkHostRaceEnd raceEnd;

	memset(&raceEnd, 0xA5, sizeof(raceEnd));
	CHECK(NativeArcadeLinkHost_TakeRaceEnd(&raceEnd) == 1);
	CHECK(raceEnd.raceNumber == raceNumber);
	CHECK(raceEnd.endReason == endReason);
	CHECK(raceEnd.foreignBundleDrops == drops);
	CHECK(CheckNoRaceEnd() == 0);
	return 0;
}

/* Mode OFF and every call inert. */
static int CheckInert(void)
{
	struct NativeArcadeLinkHostView view;

	CHECK(CheckNoRaceEnd() == 0);
	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(CheckNotRacing() == 0);
	CHECK(NativeArcadeLinkHost_InternalSelectEntropy() == 0u);
	memset(&view, 0xA5, sizeof(view));
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF);
	CHECK(NativeArcadeLinkHost_ScreenActive() == 0);
	CHECK(NativeArcadeLinkHost_Enter() == 0);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == ACT_NONE);
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == ACT_NONE);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 0);
	CHECK(NativeArcadeLinkHost_GetView(NULL) == 0);
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF);
	/* LR-7: no race begins, and the pacing switch is never touched. */
	{
		const int pacing = g_pacing;
		const uint32_t calls = g_pacingCalls;

		CHECK(NativeArcadeLinkHost_RaceBegin() == 0);
		NativeArcadeLinkHost_RaceEnd();
		CHECK(CheckPacingUntouched(pacing, calls) == 0);
	}
	return 0;
}

static int TestInertBeforeConfigure(void)
{
	/* Nothing has been configured yet in this process. */
	CHECK(CheckInert() == 0);
	/* Shutdown before any Configure is safe, twice. */
	NativeArcadeLinkHost_Shutdown();
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

static int TestNullAndDisabled(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;

	CHECK(NativeArcadeLinkHost_Configure(NULL, NULL) == 0);
	CHECK(CheckInert() == 0);

	NativeArcadeLinkLoopback_Identity(&identity);
	CHECK(NativeArcadeLinkHost_Configure(NULL, &identity) == 0);
	CHECK(CheckInert() == 0);

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeLinkHost_Configure(&options, NULL) == 1);
	CHECK(CheckInert() == 0);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(CheckInert() == 0);

	NativeArcadeLinkHost_Shutdown();
	return 0;
}

struct PreviewCase
{
	uint32_t preview;
	uint32_t screen;
	uint32_t lobbyStatus;
	uint32_t endReason;
	uint32_t selectedRow;
	uint8_t rowsEnabled;
	uint8_t attract;
};

static const struct PreviewCase kPreviewCases[] = {
	{NATIVE_ARCADE_LINK_PREVIEW_TITLE, NATIVE_ARCADE_FLOW_SCREEN_OFF, 0u, 0u, 0u, 0u, 1u},
	{NATIVE_ARCADE_LINK_PREVIEW_LOBBY_WAITING, NATIVE_ARCADE_FLOW_SCREEN_LOBBY, NATIVE_ARCADE_FLOW_LOBBY_WAITING, 0u, 0u,
		0u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_LOBBY_CONNECTING, NATIVE_ARCADE_FLOW_SCREEN_LOBBY, NATIVE_ARCADE_FLOW_LOBBY_CONNECTING,
		0u, 0u, 0u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_LOBBY_REJECTED, NATIVE_ARCADE_FLOW_SCREEN_LOBBY, NATIVE_ARCADE_FLOW_LOBBY_REJECTED, 0u,
		0u, 0u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_MATCH_FOUND, NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND, NATIVE_ARCADE_FLOW_LOBBY_READY, 0u,
		0u, 0u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED, NATIVE_ARCADE_FLOW_SCREEN_RESULTS, 0u, NATIVE_ARCADE_FLOW_END_FINISHED,
		NATIVE_ARCADE_FLOW_ROW_REMATCH, 1u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT, NATIVE_ARCADE_FLOW_SCREEN_RESULTS, 0u,
		NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT, NATIVE_ARCADE_FLOW_ROW_REMATCH, 1u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC, NATIVE_ARCADE_FLOW_SCREEN_RESULTS, 0u, NATIVE_ARCADE_FLOW_END_DESYNC,
		NATIVE_ARCADE_FLOW_ROW_REMATCH, 1u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_RESULTS_LINK_ERROR, NATIVE_ARCADE_FLOW_SCREEN_RESULTS, 0u,
		NATIVE_ARCADE_FLOW_END_LINK_ERROR, NATIVE_ARCADE_FLOW_ROW_REMATCH, 1u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT, NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT, NATIVE_ARCADE_FLOW_LOBBY_CONNECTING,
		0u, 0u, 0u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_EXIT, NATIVE_ARCADE_FLOW_SCREEN_EXIT, 0u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u, 0u, 0u},
	{NATIVE_ARCADE_LINK_PREVIEW_EXIT_OPPONENT_LEFT, NATIVE_ARCADE_FLOW_SCREEN_EXIT, 0u,
		NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT, 0u, 0u, 0u},
};

static int CheckSelectZero(const struct NativeArcadeLinkHostSelectView *select)
{
	struct NativeArcadeLinkHostSelectView zero;

	memset(&zero, 0, sizeof(zero));
	CHECK(memcmp(select, &zero, sizeof(zero)) == 0);
	return 0;
}

static int CheckPreviewView(const struct PreviewCase *expected, uint32_t ticks)
{
	struct NativeArcadeLinkHostView view;

	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(CheckNotRacing() == 0);
	CHECK(NativeArcadeLinkHost_InternalSelectEntropy() == 0u);
	memset(&view, 0xA5, sizeof(view));
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.screen == expected->screen);
	CHECK(view.lobbyStatus == expected->lobbyStatus);
	CHECK(view.endReason == expected->endReason);
	CHECK(view.selectedRow == expected->selectedRow);
	CHECK(view.ticksInScreen == ticks);
	CHECK(view.localCab == 1u);
	CHECK(view.rowsEnabled == expected->rowsEnabled);
	CHECK(view.attract == expected->attract);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	/* The original twelve previews carry no select view. */
	CHECK(CheckSelectZero(&view.select) == 0);
	return 0;
}

static int TestPreviews(void)
{
	struct NativeArcadeLinkOptions options;
	uint32_t caseCount = (uint32_t)(sizeof(kPreviewCases) / sizeof(kPreviewCases[0]));
	uint32_t i;
	uint32_t tick;

	CHECK(caseCount == 12u);
	for (i = 0u; i < caseCount; i++)
	{
		const struct PreviewCase *expected = &kPreviewCases[i];

		NativeArcadeLinkOptions_SetDefaults(&options);
		options.preview = expected->preview;
		/* No identity is needed: a preview never opens a socket. */
		CHECK(NativeArcadeLinkHost_Configure(&options, NULL) == 1);
		CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW);
		CHECK(NativeArcadeLinkHost_ScreenActive() == 1);
		CHECK(NativeArcadeLinkHost_Enter() == 0);
		CHECK(NativeArcadeLinkHost_GetView(NULL) == 0);
		CHECK(CheckPreviewView(expected, 0u) == 0);
		for (tick = 1u; tick <= 3u; tick++)
		{
			CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 1u) == ACT_NONE);
			CHECK(CheckPreviewView(expected, tick) == 0);
		}
		/* AbortToTitle is LINK only: the preview is unchanged. */
		NativeArcadeLinkHost_AbortToTitle();
		CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW);
		CHECK(CheckPreviewView(expected, 3u) == 0);
	}
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

static int TestLinkRejectsBadIdentity(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;

	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_LINK_LOCAL_PORT, TEST_LINK_DEAD_PEER_PORT);
	CHECK(NativeArcadeLinkHost_Configure(&options, NULL) == 0);
	CHECK(CheckInert() == 0);

	memset(&identity, 0, sizeof(identity));
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 0);
	CHECK(CheckInert() == 0);

	/* A valid identity with options the adapter rejects (no port). */
	NativeArcadeLinkLoopback_Identity(&identity);
	options.localPort = 0u;
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 0);
	CHECK(CheckInert() == 0);
	return 0;
}

static int CheckLinkView(uint32_t screen, uint32_t lobbyStatus, uint32_t ticksInScreen, uint8_t localCab)
{
	struct NativeArcadeLinkHostView view;

	memset(&view, 0xA5, sizeof(view));
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.screen == screen);
	CHECK(view.lobbyStatus == lobbyStatus);
	CHECK(view.endReason == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	CHECK(view.selectedRow == 0u);
	CHECK(view.ticksInScreen == ticksInScreen);
	CHECK(view.localCab == localCab);
	CHECK(view.rowsEnabled == 0u);
	CHECK(view.attract == ((screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1u : 0u));
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	/* Neither the attract screen nor the lobby selects, and no race is
	 * agreed. */
	CHECK(CheckSelectZero(&view.select) == 0);
	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(CheckNotRacing() == 0);
	return 0;
}

/* From screen OFF: Enter, then LOBBY_TICKS ticks into LOBBY / CONNECTING. */
static int EnterAndConnect(uint8_t localCab)
{
	struct NativeArcadeLinkHostView view;
	uint32_t i;

	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeLinkHost_ScreenActive() == 1);
	/* Only from screen OFF. */
	CHECK(NativeArcadeLinkHost_Enter() == 0);
	for (i = 0u; i < LOBBY_TICKS; i++)
	{
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	}
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(view.lobbyStatus == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_CONNECTING);
	CHECK(view.attract == 0u);
	CHECK(view.localCab == localCab);
	CHECK(view.rowsEnabled == 0u);
	CHECK(CheckSelectZero(&view.select) == 0);
	/* LINK before any race: no agreed match, and not racing. */
	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(CheckNotRacing() == 0);
	CHECK(NativeArcadeLinkHost_ScreenActive() == 1);
	return 0;
}

static int TestLinkLifecycle(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	uint32_t i;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_LINK_LOCAL_PORT, TEST_LINK_DEAD_PEER_PORT);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(NativeArcadeLinkHost_ScreenActive() == 0);
	CHECK(NativeArcadeLinkHost_GetView(NULL) == 0);

	/* Dormant on screen OFF: the attract view, idle ticks counting. */
	CHECK(CheckLinkView(NATIVE_ARCADE_FLOW_SCREEN_OFF, NATIVE_ARCADE_FLOW_LOBBY_WAITING, 0u, 1u) == 0);
	for (i = 1u; i <= 4u; i++)
	{
		CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == ACT_NONE);
		CHECK(CheckLinkView(NATIVE_ARCADE_FLOW_SCREEN_OFF, NATIVE_ARCADE_FLOW_LOBBY_WAITING, i, 1u) == 0);
		CHECK(NativeArcadeLinkHost_ScreenActive() == 0);
	}

	CHECK(EnterAndConnect(1u) == 0);

	/* AbortToTitle: screen OFF, attract again, idle ticks restart at 0. */
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(NativeArcadeLinkHost_ScreenActive() == 0);
	CHECK(CheckLinkView(NATIVE_ARCADE_FLOW_SCREEN_OFF, NATIVE_ARCADE_FLOW_LOBBY_WAITING, 0u, 1u) == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(CheckLinkView(NATIVE_ARCADE_FLOW_SCREEN_OFF, NATIVE_ARCADE_FLOW_LOBBY_WAITING, 1u, 1u) == 0);

	/* Enter works again on the same local port (the link was closed). */
	CHECK(EnterAndConnect(1u) == 0);

	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);

	/* After Shutdown the same port is free again for a fresh Configure. */
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(EnterAndConnect(1u) == 0);
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

static int TestSecondConfigureReplaces(void)
{
	struct NativeArcadeLinkOptions linkOptions;
	struct NativeArcadeLinkOptions previewOptions;
	struct NativeArcadeLinkOptions disabledOptions;
	struct NativeIdentityV1 identity;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&linkOptions, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, TEST_REPLACE_LOCAL_PORT,
		TEST_REPLACE_DEAD_PEER_PORT);
	NativeArcadeLinkOptions_SetDefaults(&previewOptions);
	previewOptions.preview = NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT;
	NativeArcadeLinkOptions_SetDefaults(&disabledOptions);

	/* LINK as CAB2, entered with an open link. */
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, &identity) == 1);
	CHECK(CheckLinkView(NATIVE_ARCADE_FLOW_SCREEN_OFF, NATIVE_ARCADE_FLOW_LOBBY_WAITING, 0u, 2u) == 0);
	CHECK(EnterAndConnect(2u) == 0);

	/* Replaced by a preview: the link is closed and the preview starts fresh. */
	CHECK(NativeArcadeLinkHost_Configure(&previewOptions, NULL) == 1);
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW);
	CHECK(NativeArcadeLinkHost_Enter() == 0);
	CHECK(CheckPreviewView(&kPreviewCases[9], 0u) == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(CheckPreviewView(&kPreviewCases[9], 1u) == 0);

	/* Replaced by LINK on the same port: it was released by the replacement. */
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, &identity) == 1);
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(EnterAndConnect(2u) == 0);

	/* Replaced by LINK as CAB1 on another port while the first is open. */
	NativeArcadeLinkLoopback_LinkOptions(&linkOptions, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_REPLACE_SECOND_LOCAL_PORT,
		TEST_REPLACE_DEAD_PEER_PORT);
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, &identity) == 1);
	CHECK(CheckLinkView(NATIVE_ARCADE_FLOW_SCREEN_OFF, NATIVE_ARCADE_FLOW_LOBBY_WAITING, 0u, 1u) == 0);
	CHECK(EnterAndConnect(1u) == 0);

	/* Replaced by disabled options: OFF, and everything inert. */
	CHECK(NativeArcadeLinkHost_Configure(&disabledOptions, &identity) == 1);
	CHECK(CheckInert() == 0);

	/* A failed Configure also replaces: the previous mode does not survive. */
	CHECK(NativeArcadeLinkHost_Configure(&previewOptions, NULL) == 1);
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, NULL) == 0);
	CHECK(CheckInert() == 0);

	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

/* ---- MS-8: select previews, select entropy, agreed match ---- */

/* The rules module's tables, written out independently here (retail menu
 * order) and cross-checked against the module below. */
static const uint8_t kSelectTracks[16] = {3u, 6u, 4u, 14u, 9u, 2u, 8u, 0u, 5u, 1u, 12u, 10u, 15u, 7u, 11u, 16u};
static const uint8_t kSelectLaps[3] = {3u, 5u, 7u};
/* The first retail 2P AI set holding neither CRASH (0) nor CORTEX (1). */
static const uint8_t kPreviewBots[4] = {6u, 4u, 2u, 3u};

#define PREVIEW_CRASH 0u
#define PREVIEW_CORTEX 1u
#define PREVIEW_CRASH_COVE 3u
#define PREVIEW_TIGER_TEMPLE 4u

static void SetHuman(struct NativeArcadeLinkHostSelectHumanView *human, uint8_t character, uint8_t track, uint8_t laps,
	uint8_t lockMask, uint8_t currentItem)
{
	human->present = 1u;
	human->characterID = character;
	human->trackID = track;
	human->lapCount = laps;
	human->lockMask = lockMask;
	human->currentItem = currentItem;
}

/* The expected screen and select view of one select preview at a tick. */
static void ExpectedSelectPreview(uint32_t preview, uint32_t ticks, uint32_t *screen,
	struct NativeArcadeLinkHostSelectView *expected)
{
	uint32_t step = ticks / PREVIEW_STEP_TICKS;
	struct NativeArcadeLinkHostSelectHumanView *local = &expected->humans[0];
	struct NativeArcadeLinkHostSelectHumanView *opponent = &expected->humans[1];

	memset(expected, 0, sizeof(*expected));
	*screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT;
	expected->active = 1u;
	expected->humanCount = 2u;
	expected->localHuman = 0u;
	expected->status = 0u; /* PICKING */
	expected->ticksLeft = PREVIEW_ITEM_TICKS - (ticks % PREVIEW_ITEM_TICKS);
	switch (preview)
	{
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER:
		SetHuman(local, PREVIEW_CRASH, PREVIEW_CRASH_COVE, 3u, 0u, 0u);
		SetHuman(opponent, (uint8_t)(step % 8u), PREVIEW_CRASH_COVE, 3u, 0u, 0u);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK:
		SetHuman(local, PREVIEW_CRASH, PREVIEW_CRASH_COVE, 3u, 1u, 1u);
		SetHuman(opponent, PREVIEW_CORTEX, kSelectTracks[step % 16u], 3u, 1u, 1u);
		expected->peerLockedCharacterMask = (uint16_t)(1u << PREVIEW_CORTEX);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS:
		SetHuman(local, PREVIEW_CRASH, PREVIEW_CRASH_COVE, 3u, 3u, 2u);
		SetHuman(opponent, PREVIEW_CORTEX, PREVIEW_TIGER_TEMPLE, kSelectLaps[step % 3u], 3u, 2u);
		expected->peerLockedCharacterMask = (uint16_t)(1u << PREVIEW_CORTEX);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT:
		SetHuman(local, PREVIEW_CRASH, PREVIEW_CRASH_COVE, 3u, 7u, 3u);
		SetHuman(opponent, PREVIEW_CORTEX, PREVIEW_TIGER_TEMPLE, kSelectLaps[step % 3u], 3u, 2u);
		expected->status = 1u; /* WAITING */
		expected->ticksLeft = 0u;
		expected->peerLockedCharacterMask = (uint16_t)(1u << PREVIEW_CORTEX);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT:
	default:
		*screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT;
		SetHuman(local, PREVIEW_CRASH, PREVIEW_CRASH_COVE, 3u, 7u, 3u);
		SetHuman(opponent, PREVIEW_CORTEX, PREVIEW_TIGER_TEMPLE, 3u, 7u, 3u);
		expected->status = 3u; /* CONFIRMED */
		expected->ticksLeft = 0u;
		expected->resolved = 1u;
		expected->trackID = PREVIEW_TIGER_TEMPLE;
		expected->trackDrawn = 1u;
		expected->lapCount = 3u;
		expected->lapsDrawn = 0u;
		expected->characterReassignedMask = 0u;
		expected->humanCharacter[0] = PREVIEW_CRASH;
		expected->humanCharacter[1] = PREVIEW_CORTEX;
		expected->botCount = 4u;
		memcpy(expected->botCharacter, kPreviewBots, sizeof(kPreviewBots));
		expected->peerLockedCharacterMask = (uint16_t)(1u << PREVIEW_CORTEX);
		break;
	}
	expected->currentItem = local->currentItem;
}

/* The whole host view of a select preview at a tick, field by field. */
static int CheckSelectPreviewView(uint32_t preview, uint32_t ticks, struct NativeArcadeLinkHostView *out)
{
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkHostSelectView expected;
	uint32_t screen = 0u;
	uint32_t h;
	uint32_t i;

	ExpectedSelectPreview(preview, ticks, &screen, &expected);
	CHECK(CheckNotRacing() == 0);
	memset(&view, 0xA5, sizeof(view));
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.screen == screen);
	CHECK(view.lobbyStatus == 0u);
	CHECK(view.endReason == 0u);
	CHECK(view.selectedRow == 0u);
	CHECK(view.ticksInScreen == ticks);
	CHECK(view.localCab == 1u);
	CHECK(view.rowsEnabled == 0u);
	CHECK(view.attract == 0u);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	CHECK(view.select.active == expected.active);
	CHECK(view.select.humanCount == expected.humanCount);
	CHECK(view.select.localHuman == expected.localHuman);
	CHECK(view.select.currentItem == expected.currentItem);
	CHECK(view.select.ticksLeft == expected.ticksLeft);
	CHECK(view.select.status == expected.status);
	CHECK(view.select.resolved == expected.resolved);
	CHECK(view.select.trackID == expected.trackID);
	CHECK(view.select.lapCount == expected.lapCount);
	CHECK(view.select.trackDrawn == expected.trackDrawn);
	CHECK(view.select.lapsDrawn == expected.lapsDrawn);
	CHECK(view.select.characterReassignedMask == expected.characterReassignedMask);
	CHECK(view.select.botCount == expected.botCount);
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		CHECK(view.select.humanCharacter[i] == expected.humanCharacter[i]);
	}
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS; i++)
	{
		CHECK(view.select.botCharacter[i] == expected.botCharacter[i]);
	}
	CHECK(view.select.peerLockedCharacterMask == expected.peerLockedCharacterMask);
	CHECK(view.select.reserved[0] == 0u);
	CHECK(view.select.reserved[1] == 0u);
	for (h = 0u; h < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; h++)
	{
		CHECK(view.select.humans[h].present == expected.humans[h].present);
		CHECK(view.select.humans[h].characterID == expected.humans[h].characterID);
		CHECK(view.select.humans[h].trackID == expected.humans[h].trackID);
		CHECK(view.select.humans[h].lapCount == expected.humans[h].lapCount);
		CHECK(view.select.humans[h].lockMask == expected.humans[h].lockMask);
		CHECK(view.select.humans[h].currentItem == expected.humans[h].currentItem);
		CHECK(view.select.humans[h].reserved[0] == 0u);
		CHECK(view.select.humans[h].reserved[1] == 0u);
	}
	CHECK(memcmp(&view.select, &expected, sizeof(expected)) == 0);
	if (out != NULL)
	{
		*out = view;
	}
	return 0;
}

/* Configures one select preview and ticks it to tick `ticks`. */
static int ConfigurePreviewAt(uint32_t preview, uint32_t ticks)
{
	struct NativeArcadeLinkOptions options;
	uint32_t tick;

	NativeArcadeLinkOptions_SetDefaults(&options);
	options.preview = preview;
	CHECK(NativeArcadeLinkHost_Configure(&options, NULL) == 1);
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW);
	for (tick = 0u; tick < ticks; tick++)
	{
		CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == ACT_NONE);
	}
	return 0;
}

static int TestSelectPreviews(void)
{
	static const uint32_t previews[5] = {
		NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER, NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK,
		NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS, NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT,
		NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT};
	struct NativeArcadeLinkHostView view;
	uint32_t i;
	uint32_t tick;

	/* The independent tables above are the rules module's, and the result
	 * preview's bots are the rules module's first AI set, so the previews
	 * stay consistent with the rules. */
	for (i = 0u; i < 8u; i++)
	{
		CHECK(NativeMatchSelect_CharacterAt(i) == (uint8_t)i);
	}
	for (i = 0u; i < 16u; i++)
	{
		CHECK(NativeMatchSelect_TrackAt(i) == kSelectTracks[i]);
	}
	for (i = 0u; i < 3u; i++)
	{
		CHECK(NativeMatchSelect_LapOptionAt(i) == kSelectLaps[i]);
	}
	for (i = 0u; i < 4u; i++)
	{
		CHECK(NativeMatchSelect_AiSetRacer(0u, i) == kPreviewBots[i]);
		CHECK(kPreviewBots[i] != PREVIEW_CRASH);
		CHECK(kPreviewBots[i] != PREVIEW_CORTEX);
	}

	/* Every tick of every select preview, through two countdown wraps and
	 * several opponent-cursor wraps; GetAgreedMatch stays 0. */
	for (i = 0u; i < 5u; i++)
	{
		CHECK(ConfigurePreviewAt(previews[i], 0u) == 0);
		CHECK(NativeArcadeLinkHost_ScreenActive() == 1);
		CHECK(NativeArcadeLinkHost_Enter() == 0);
		CHECK(CheckSelectPreviewView(previews[i], 0u, NULL) == 0);
		for (tick = 1u; tick <= 1300u; tick++)
		{
			CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == ACT_NONE);
			CHECK(CheckSelectPreviewView(previews[i], tick, NULL) == 0);
		}
		CHECK(CheckNoAgreedMatch() == 0);
		CHECK(NativeArcadeLinkHost_InternalSelectEntropy() == 0u);
		/* AbortToTitle is LINK only: the preview is unchanged. */
		NativeArcadeLinkHost_AbortToTitle();
		CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW);
		CHECK(CheckSelectPreviewView(previews[i], 1300u, NULL) == 0);
	}

	/* Literal spot checks: the opponent cursor at two tick values each. */
	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER, 0u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER, 0u, &view) == 0);
	CHECK(view.screen == 7u);
	CHECK(view.select.currentItem == 0u);
	CHECK(view.select.ticksLeft == 600u);
	CHECK(view.select.humans[0].characterID == 0u);
	CHECK(view.select.humans[1].characterID == 0u);
	CHECK(view.select.peerLockedCharacterMask == 0u);
	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER, 95u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER, 95u, &view) == 0);
	CHECK(view.select.ticksLeft == 505u);
	CHECK(view.select.humans[1].characterID == 3u);
	CHECK(view.select.humans[1].currentItem == 0u);

	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK, 60u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK, 60u, &view) == 0);
	CHECK(view.select.currentItem == 1u);
	CHECK(view.select.humans[0].lockMask == 1u);
	CHECK(view.select.humans[0].trackID == 3u);
	CHECK(view.select.humans[1].characterID == 1u);
	CHECK(view.select.humans[1].trackID == 4u);
	CHECK(view.select.peerLockedCharacterMask == 0x0002u);
	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK, 480u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK, 480u, &view) == 0);
	CHECK(view.select.humans[1].trackID == 3u);
	CHECK(view.select.ticksLeft == 120u);

	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS, 30u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS, 30u, &view) == 0);
	CHECK(view.select.currentItem == 2u);
	CHECK(view.select.humans[0].lapCount == 3u);
	CHECK(view.select.humans[1].trackID == 4u);
	CHECK(view.select.humans[1].lapCount == 5u);
	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS, 60u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS, 60u, &view) == 0);
	CHECK(view.select.humans[1].lapCount == 7u);

	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT, 0u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT, 0u, &view) == 0);
	CHECK(view.select.currentItem == 3u);
	CHECK(view.select.status == 1u);
	CHECK(view.select.ticksLeft == 0u);
	CHECK(view.select.humans[0].lockMask == 7u);
	CHECK(view.select.humans[1].lapCount == 3u);
	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT, 90u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT, 90u, &view) == 0);
	CHECK(view.select.humans[1].lapCount == 3u);
	CHECK(view.select.humans[1].currentItem == 2u);

	CHECK(ConfigurePreviewAt(NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT, 45u) == 0);
	CHECK(CheckSelectPreviewView(NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT, 45u, &view) == 0);
	CHECK(view.screen == 8u);
	CHECK(view.select.status == 3u);
	CHECK(view.select.resolved == 1u);
	CHECK(view.select.trackID == 4u);
	CHECK(view.select.trackDrawn == 1u);
	CHECK(view.select.lapCount == 3u);
	CHECK(view.select.lapsDrawn == 0u);
	CHECK(view.select.humanCharacter[0] == 0u);
	CHECK(view.select.humanCharacter[1] == 1u);
	CHECK(view.select.botCount == 4u);
	CHECK(view.select.botCharacter[0] == 6u);
	CHECK(view.select.botCharacter[1] == 4u);
	CHECK(view.select.botCharacter[2] == 2u);
	CHECK(view.select.botCharacter[3] == 3u);
	CHECK(view.select.botCharacter[4] == 0u);

	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

static int TestMixSelectEntropy(void)
{
	const uint64_t step = UINT64_C(0x9E3779B97F4A7C15);

	CHECK(NativeArcadeLinkHost_MixSelectEntropy(0u, 0u) == 0u);
	CHECK(NativeArcadeLinkHost_MixSelectEntropy(UINT64_C(0x0123456789ABCDEF), 0u) == UINT64_C(0x0123456789ABCDEF));
	CHECK(NativeArcadeLinkHost_MixSelectEntropy(0u, 1u) == step);
	CHECK(NativeArcadeLinkHost_MixSelectEntropy(0u, 2u) == UINT64_C(0x3C6EF372FE94F82A));
	CHECK(NativeArcadeLinkHost_MixSelectEntropy(UINT64_C(0xFFFFFFFFFFFFFFFF), 1u) == ~step);
	CHECK(NativeArcadeLinkHost_MixSelectEntropy(UINT64_C(0x1122334455667788), 3u) ==
		(UINT64_C(0x1122334455667788) ^ (step * 3u)));
	/* Consecutive epochs never collide for the same entropy. */
	CHECK(NativeArcadeLinkHost_MixSelectEntropy(UINT64_C(42), 7u) != NativeArcadeLinkHost_MixSelectEntropy(UINT64_C(42), 8u));
	return 0;
}

/* Every LINK Configure and every AbortToTitle hands the adapter a new
 * select entropy: the options' entropy mixed with the next host epoch. The
 * epoch is process-local (earlier tests already advanced it), so the checks
 * recover it from the first value and follow it. */
static int TestSelectEntropyEpochs(void)
{
	const uint64_t entropy = UINT64_C(0x1122334455667788);
	const uint64_t step = UINT64_C(0x9E3779B97F4A7C15);
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	uint64_t first;
	uint64_t second;
	uint64_t third;
	uint64_t fourth;
	uint64_t zeroEntropy;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_ENTROPY_LOCAL_PORT, TEST_ENTROPY_DEAD_PEER_PORT);
	options.selectEntropy = entropy;
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	first = NativeArcadeLinkHost_InternalSelectEntropy();
	CHECK((first ^ entropy) != 0u);
	CHECK(first != entropy);

	/* Two consecutive AbortToTitle calls: two new, different values, one
	 * epoch apart. */
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	second = NativeArcadeLinkHost_InternalSelectEntropy();
	NativeArcadeLinkHost_AbortToTitle();
	third = NativeArcadeLinkHost_InternalSelectEntropy();
	CHECK(second != first);
	CHECK(third != second);
	CHECK(third != first);
	CHECK((second ^ entropy) == ((first ^ entropy) + step));
	CHECK((third ^ entropy) == ((second ^ entropy) + step));

	/* Also after an Enter with an open link. */
	CHECK(EnterAndConnect(1u) == 0);
	NativeArcadeLinkHost_AbortToTitle();
	fourth = NativeArcadeLinkHost_InternalSelectEntropy();
	CHECK((fourth ^ entropy) == ((third ^ entropy) + step));

	/* Shutdown does not reset the epoch: a fresh Configure continues it. */
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	options.selectEntropy = 0u;
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	zeroEntropy = NativeArcadeLinkHost_InternalSelectEntropy();
	CHECK(zeroEntropy == ((fourth ^ entropy) + step));
	CHECK(zeroEntropy != 0u);
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_InternalSelectEntropy() == zeroEntropy + step);

	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

static struct NativeArcadeNetplay g_peer;

/* The host view's select outcome fields equal the adapter view's. */
static int CheckSameOutcome(const struct NativeArcadeLinkHostSelectView *host, const struct NativeArcadeNetplaySelectView *peer)
{
	uint32_t i;

	CHECK(host->resolved == peer->resolved);
	CHECK(host->trackID == peer->trackID);
	CHECK(host->lapCount == peer->lapCount);
	CHECK(host->trackDrawn == peer->trackDrawn);
	CHECK(host->lapsDrawn == peer->lapsDrawn);
	CHECK(host->characterReassignedMask == peer->characterReassignedMask);
	CHECK(host->botCount == peer->botCount);
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		CHECK(host->humanCharacter[i] == peer->humanCharacter[i]);
	}
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS; i++)
	{
		CHECK(host->botCharacter[i] == peer->botCharacter[i]);
	}
	return 0;
}

/*
 * LINK end to end over loopback: the host as CAB1 against a test-owned
 * adapter as CAB2 with the same fixture and the production timings. The
 * host's select view follows the session (the opponent's lock and the
 * greyed character arrive, the countdown runs), shows the confirmed
 * outcome on SELECT_RESULT equal to the peer's, and GetAgreedMatch reports
 * the agreed race config once START_RACE is returned, and nothing before.
 */
static int TestLinkSelectAndAgreedMatch(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeNetplayView peerView;
	struct NativeArcadeLinkHostMatch match;
	const struct NativeMatchConfigV1 *agreed;
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t ticksLeft;
	uint32_t tick;
	uint32_t slot;
	int hostStarted = 0;
	int peerStarted = 0;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_SELECT_HOST_PORT, TEST_SELECT_PEER_PORT);
	options.selectEntropy = UINT64_C(0x5EED5EED5EED5EED);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);

	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_SELECT_HOST_PORT, TEST_SELECT_PEER_PORT,
		UINT64_C(0x0DDBA11)) == 1);

	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);

	/* Into SELECT on both sides, then one released tick to arm the menus. */
	for (tick = 0u; tick < PAIR_BUDGET; tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK(hostAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
		CHECK(NativeArcadeNetplay_GetView(&g_peer, &peerView) == 1);
		if (view.screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT)
		{
			CHECK(CheckSelectZero(&view.select) == 0);
		}
		CHECK(CheckNoAgreedMatch() == 0);
		if ((view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) &&
			(peerView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT))
		{
			break;
		}
	}
	CHECK(tick < PAIR_BUDGET);
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);

	/* The local cursors start on the fixture: CRASH, CRASH_COVE, 3 laps. */
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(view.select.active == 1u);
	CHECK(view.select.humanCount == 2u);
	CHECK(view.select.localHuman == 0u);
	CHECK(view.select.currentItem == 0u);
	CHECK(view.select.status == 0u);
	CHECK(view.select.resolved == 0u);
	CHECK(view.select.humans[0].present == 1u);
	CHECK(view.select.humans[0].characterID == 0u);
	CHECK(view.select.humans[0].trackID == 3u);
	CHECK(view.select.humans[0].lapCount == 3u);
	CHECK(view.select.humans[0].lockMask == 0u);
	CHECK(view.select.humans[2].present == 0u);
	CHECK(view.select.humans[3].present == 0u);
	CHECK((view.select.ticksLeft > 0u) && (view.select.ticksLeft <= 600u));
	ticksLeft = view.select.ticksLeft;
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.select.ticksLeft == ticksLeft - 1u);
	/* By now the peer has been heard, on its own fixture character. */
	CHECK(view.select.humans[1].present == 1u);
	CHECK(view.select.humans[1].characterID == 1u);
	CHECK(view.select.humans[1].lockMask == 0u);
	CHECK(view.select.peerLockedCharacterMask == 0u);

	/* The peer locks CORTEX: the host sees the lock and greys CORTEX, but
	 * the peer's CONFIRM never appears as the host's local menu event. */
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, NATIVE_ARCADE_MENU_BUTTON_CROSS, &hostAction, &peerAction);
	CHECK(hostAction == ACT_NONE);
	CHECK(peerAction == ACT_NONE);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(NativeArcadeNetplay_GetView(&g_peer, &peerView) == 1);
	CHECK(peerView.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM);
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
	CHECK(hostAction == ACT_NONE);
	CHECK(peerAction == ACT_NONE);
	CHECK(NativeArcadeLinkLoopback_PressPair(&g_peer, 0u, 0u) == 1);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(view.select.humans[1].lockMask == 1u);
	CHECK(view.select.humans[1].characterID == 1u);
	CHECK(view.select.humans[1].currentItem == 1u);
	CHECK(view.select.peerLockedCharacterMask == 0x0002u);
	CHECK(view.select.currentItem == 0u);

	/* Both confirm the rest on the fixture cursors. The host's own CONFIRM
	 * is its local menu event on the held tick, NONE on the released one. */
	NativeArcadeLinkLoopback_TickPair(&g_peer, NATIVE_ARCADE_MENU_BUTTON_CROSS, NATIVE_ARCADE_MENU_BUTTON_CROSS, &hostAction,
		&peerAction);
	CHECK(hostAction == ACT_NONE);
	CHECK(peerAction == ACT_NONE);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM);
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
	CHECK(hostAction == ACT_NONE);
	CHECK(peerAction == ACT_NONE);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.localMenuEvent == (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(view.select.currentItem == 1u);
	CHECK(view.select.humans[0].lockMask == 1u);
	CHECK(NativeArcadeLinkLoopback_PressPair(&g_peer, NATIVE_ARCADE_MENU_BUTTON_CROSS, NATIVE_ARCADE_MENU_BUTTON_CROSS) == 1);
	CHECK(NativeArcadeLinkLoopback_PressPair(&g_peer, NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == 1);

	/* SELECT_RESULT: the confirmed outcome, equal on both sides. */
	for (tick = 0u; tick < PAIR_BUDGET; tick++)
	{
		CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
		if (view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT)
		{
			break;
		}
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK(hostAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
	}
	CHECK(tick < PAIR_BUDGET);
	CHECK(NativeArcadeNetplay_GetView(&g_peer, &peerView) == 1);
	CHECK(view.select.active == 1u);
	CHECK(view.select.status == 3u);
	CHECK(view.select.currentItem == 3u);
	CHECK(view.select.ticksLeft == 0u);
	CHECK(view.select.resolved == 1u);
	CHECK(view.select.trackID == 3u);
	CHECK(view.select.lapCount == 3u);
	CHECK(view.select.trackDrawn == 0u);
	CHECK(view.select.lapsDrawn == 0u);
	CHECK(view.select.characterReassignedMask == 0u);
	CHECK(view.select.humanCharacter[0] == 0u);
	CHECK(view.select.humanCharacter[1] == 1u);
	CHECK(view.select.botCount == 4u);
	CHECK(view.select.botCharacter[0] == kPreviewBots[0]);
	CHECK(view.select.botCharacter[1] == kPreviewBots[1]);
	CHECK(view.select.botCharacter[2] == kPreviewBots[2]);
	CHECK(view.select.botCharacter[3] == kPreviewBots[3]);
	CHECK(view.select.humans[0].lockMask == 7u);
	CHECK(view.select.humans[1].lockMask == 7u);
	CHECK(peerView.select.resolved == 1u);
	CHECK(CheckSameOutcome(&view.select, &peerView.select) == 0);
	CHECK(CheckNoAgreedMatch() == 0);

	/* On to START_RACE: GetAgreedMatch reports nothing until it. */
	for (tick = 0u; (tick < PAIR_BUDGET) && !(hostStarted && peerStarted); tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		if (!hostStarted)
		{
			if (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)
			{
				hostStarted = 1;
			}
			else
			{
				CHECK(CheckNoAgreedMatch() == 0);
			}
		}
		peerStarted = peerStarted || (peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		CHECK(hostAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE);
		CHECK(peerAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE);
	}
	CHECK(hostStarted && peerStarted);

	agreed = NativeArcadeNetplay_AgreedConfig(&g_peer);
	CHECK(agreed != NULL);
	/* The agreed race meets the v1 bot rules, with every bot at the default difficulty. */
	CHECK(NativeArcadeBotRules_ValidateConfigV1(agreed));
	for (slot = NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT;
	     slot < NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_BOT_COUNT; slot++)
	{
		CHECK(agreed->slots[slot].difficulty == (uint8_t)NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY);
		CHECK(agreed->slots[slot].difficulty == 0xA0u);
	}
	memset(&match, 0xA5, sizeof(match));
	CHECK(NativeArcadeLinkHost_GetAgreedMatch(&match) == 1);
	CHECK(match.trackID == agreed->trackID);
	CHECK(match.trackID == 3u);
	CHECK(match.lapCount == agreed->lapCount);
	CHECK(match.lapCount == 3u);
	CHECK(match.masterSeed == agreed->masterSeed);
	CHECK(match.masterSeed != NATIVE_ARCADE_LINK_FIXTURE_MASTER_SEED);
	CHECK(match.masterSeed != 0u);
	for (slot = 0u; slot < NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS; slot++)
	{
		CHECK(match.slotRole[slot] == agreed->slots[slot].role);
		CHECK(match.slotCharacter[slot] == agreed->slots[slot].characterID);
	}
	CHECK(match.slotRole[0] == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(match.slotCharacter[0] == 0u);
	CHECK(match.slotRole[1] == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	CHECK(match.slotCharacter[1] == 1u);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(CheckSelectZero(&view.select) == 0);
	CHECK(NativeArcadeLinkHost_Racing() == 1u);

	/* The game's abort: back to the attract screen, nothing agreed. */
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(CheckNotRacing() == 0);

	NativeArcadeNetplay_Shutdown(&g_peer);
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

/* The host view's localMenuEvent; 0xFF when GetView fails. */
static uint32_t HostMenuEvent(void)
{
	struct NativeArcadeLinkHostView view;

	memset(&view, 0xA5, sizeof(view));
	if (!NativeArcadeLinkHost_GetView(&view))
	{
		return 0xFFu;
	}
	return view.localMenuEvent;
}

/*
 * LINK: GetView carries the menu event the last NativeArcadeLinkHost_Tick
 * consumed for the local player, NONE before the first tick, on screen OFF,
 * on ticks without a new edge, right after a screen change, and after
 * AbortToTitle. (PREVIEW reports NONE: CheckPreviewView and
 * CheckSelectPreviewView check it on ticks with CROSS held.)
 */
static int TestLinkLocalMenuEvent(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_MENU_EVENT_LOCAL_PORT,
		TEST_MENU_EVENT_DEAD_PEER_PORT);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* Screen OFF: nothing consumed. */
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* LOBBY: the first tick arms, then each rising edge on its own tick. */
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_DOWN, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NEXT);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_DOWN, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_UP, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_PREV);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_START, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* BACK leaves the lobby for EXIT; the next tick re-arms, so a fresh
	 * press there reports NONE. */
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_TRIANGLE, 0u) ==
		(uint32_t)NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_BACK);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_CROSS, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	/* AbortToTitle re-initializes the link: NONE again. */
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(NativeArcadeLinkHost_Tick(NATIVE_ARCADE_MENU_BUTTON_DOWN, 0u) == ACT_NONE);
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NEXT);
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(HostMenuEvent() == (uint32_t)NATIVE_ARCADE_MENU_EVENT_NONE);

	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

/* ---- RL-S6: agreed config, local race failure, racing query ---- */

static uint32_t HostScreen(void)
{
	struct NativeArcadeLinkHostView view;

	memset(&view, 0xA5, sizeof(view));
	if (!NativeArcadeLinkHost_GetView(&view))
	{
		return 0xFFu;
	}
	return view.screen;
}

static uint32_t HostEndReason(void)
{
	struct NativeArcadeLinkHostView view;

	memset(&view, 0xA5, sizeof(view));
	if (!NativeArcadeLinkHost_GetView(&view))
	{
		return 0xFFu;
	}
	return view.endReason;
}

static uint32_t PeerScreen(void)
{
	struct NativeArcadeNetplayView view;

	memset(&view, 0xA5, sizeof(view));
	if (!NativeArcadeNetplay_GetView(&g_peer, &view))
	{
		return 0xFFu;
	}
	return view.screen;
}

/*
 * From the lobby or REMATCH_WAIT: ticks the host and the test-owned peer
 * until each has returned START_RACE (through the select, confirming each
 * item with CROSS on alternate ticks, and the RL-S5 launch agreement).
 * Until the host's START_RACE the host is not racing, ignores a local race
 * failure, and has no agreed config; from it on, the host is racing.
 */
static int DrivePairToRace(void)
{
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t heldHost;
	uint32_t heldPeer;
	uint32_t tick;
	int hostStarted = 0;
	int peerStarted = 0;

	for (tick = 0u; (tick < PAIR_BUDGET) && !(hostStarted && peerStarted); tick++)
	{
		heldHost = ((HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) && ((tick & 1u) == 0u))
			? NATIVE_ARCADE_MENU_BUTTON_CROSS
			: 0u;
		heldPeer = ((PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) && ((tick & 1u) == 0u))
			? NATIVE_ARCADE_MENU_BUTTON_CROSS
			: 0u;
		if (!hostStarted)
		{
			CHECK(CheckNotRacing() == 0);
			CHECK(CheckNoAgreedConfig() == 0);
		}
		NativeArcadeLinkLoopback_TickPair(&g_peer, heldHost, heldPeer, &hostAction, &peerAction);
		CHECK(hostAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK);
		CHECK(hostAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE);
		CHECK(peerAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK);
		CHECK(peerAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE);
		hostStarted = hostStarted || (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		peerStarted = peerStarted || (peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		if (hostStarted)
		{
			CHECK(NativeArcadeLinkHost_Racing() == 1u);
		}
	}
	CHECK(hostStarted && peerStarted);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	return 0;
}

/* The host's agreed config is the peer's, byte for byte (the handshake and
 * the launch commit guarantee both hold it), and agrees with the agreed
 * match; NULL is refused. Copied to *out. */
static int CheckAgreedConfig(struct NativeMatchConfigV1 *out)
{
	struct NativeMatchConfigV1 config;
	struct NativeArcadeLinkHostMatch match;
	const struct NativeMatchConfigV1 *peerAgreed;
	uint32_t slot;

	peerAgreed = NativeArcadeNetplay_AgreedConfig(&g_peer);
	CHECK(peerAgreed != NULL);
	memset(&config, 0xA5, sizeof(config));
	CHECK(NativeArcadeLinkHost_GetAgreedConfig(&config) == 1);
	CHECK(memcmp(&config, peerAgreed, sizeof(config)) == 0);
	CHECK(NativeArcadeLinkHost_GetAgreedConfig(NULL) == 0);
	memset(&match, 0xA5, sizeof(match));
	CHECK(NativeArcadeLinkHost_GetAgreedMatch(&match) == 1);
	CHECK(match.trackID == config.trackID);
	CHECK(match.lapCount == config.lapCount);
	CHECK(match.masterSeed == config.masterSeed);
	for (slot = 0u; slot < NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS; slot++)
	{
		CHECK(match.slotRole[slot] == config.slots[slot].role);
		CHECK(match.slotCharacter[slot] == config.slots[slot].characterID);
	}
	memcpy(out, &config, sizeof(config));
	return 0;
}

/* Ticks the pair `ticks` times with no buttons and no finish; the host stays
 * on RACING and racing, and the peer stays on RACING. (The view's endReason
 * is not checked: on RACING after a rematch it still holds the previous
 * race's reason, which the flow only overwrites when the race ends.) */
static int StayRacing(uint32_t ticks)
{
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t tick;

	for (tick = 0u; tick < ticks; tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK(hostAction == ACT_NONE);
		CHECK(peerAction == ACT_NONE);
		CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(NativeArcadeLinkHost_Racing() == 1u);
		CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	}
	return 0;
}

/*
 * RL-S6 over loopback, three races, the host as CAB1 against a test-owned
 * adapter as CAB2 with the production timings.
 * Race 1: GetAgreedConfig and Racing() are 0 until START_RACE, then the
 * config is the peer's byte for byte; a local race failure reported on
 * RACING together with a same-tick finish ends the host's race as RESULTS
 * LINK ERROR on the next Tick; the peer is not told (it keeps RACING on an
 * open link until it finishes itself); on RESULTS the config stays
 * readable, Racing() is 0, and a report is ignored; REMATCH then works.
 * Race 2 (the rematch): no leak (it keeps RACING); a report followed by
 * AbortToTitle before any Tick. Race 3 (a fresh Enter): no leak again; it
 * finishes as RACE COMPLETE.
 */
static int TestLinkRaceFailureAndRacingQuery(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	struct NativeArcadeNetplayView peerView;
	struct NativeMatchConfigV1 race1;
	struct NativeMatchConfigV1 race2;
	struct NativeMatchConfigV1 race3;
	struct NativeMatchConfigV1 afterFailure;
	struct NativeLockstepPeerLink *peerLink;
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t tick;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_RACE_FAILURE_HOST_PORT,
		TEST_RACE_FAILURE_PEER_PORT);
	options.selectEntropy = UINT64_C(0xFA11FA11FA11FA11);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	/* LINK on screen OFF: not racing, nothing agreed, a report ignored. */
	CHECK(CheckNotRacing() == 0);
	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_RACE_FAILURE_HOST_PORT, TEST_RACE_FAILURE_PEER_PORT,
		UINT64_C(0x0FA11)) == 1);

	/* Race 1. */
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(DrivePairToRace() == 0);
	CHECK(CheckAgreedConfig(&race1) == 0);
	CHECK(StayRacing(5u) == 0);

	/* The report: latched, still RACING until the next Tick; again 1. */
	CHECK(NativeArcadeLinkHost_ReportRaceFailure() == 1);
	CHECK(NativeArcadeLinkHost_Racing() == 1u);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeLinkHost_ReportRaceFailure() == 1);
	/* The next Tick, with a same-tick finish: LINK ERROR outranks it. */
	CHECK(CheckNoRaceEnd() == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(CheckNotRacing() == 0);
	/* LR-S6: the end-of-race record, once, from the first RESULTS tick. */
	CHECK(CheckRaceEnd(1u, NATIVE_ARCADE_FLOW_END_LINK_ERROR, 0u) == 0);
	/* The raced config stays readable on RESULTS, unchanged. */
	memset(&afterFailure, 0xA5, sizeof(afterFailure));
	CHECK(NativeArcadeLinkHost_GetAgreedConfig(&afterFailure) == 1);
	CHECK(memcmp(&afterFailure, &race1, sizeof(race1)) == 0);

	/* The peer is not told: it keeps RACING on an open, running link while
	 * the host sits on RESULTS, and the host stays on LINK ERROR. */
	for (tick = 0u; tick < 60u; tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK(hostAction == ACT_NONE);
		CHECK(peerAction == ACT_NONE);
		CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
		CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
		CHECK(CheckNotRacing() == 0);
		CHECK(CheckNoRaceEnd() == 0);
	}
	CHECK(NativeArcadeNetplay_GetView(&g_peer, &peerView) == 1);
	CHECK(peerView.lobbyStatus == (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY);
	CHECK(peerView.endReason == (uint32_t)NATIVE_ARCADE_FLOW_END_NONE);
	peerLink = NativeArcadeNetplay_Link(&g_peer);
	CHECK(peerLink != NULL);
	CHECK(NativeLockstepPeerLink_Mode(peerLink) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* The peer finishes its own race. */
	CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 1u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
	CHECK(NativeArcadeNetplay_GetView(&g_peer, &peerView) == 1);
	CHECK(peerView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(peerView.endReason == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);

	/* Both past the results dwell with no buttons, then REMATCH on both:
	 * after LINK ERROR the host's results menu works as after a finish. */
	for (tick = 0u; tick <= NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_DWELL_TICKS; tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK(hostAction == ACT_NONE);
		CHECK(peerAction == ACT_NONE);
	}
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	NativeArcadeLinkLoopback_TickPair(&g_peer, NATIVE_ARCADE_MENU_BUTTON_CROSS, NATIVE_ARCADE_MENU_BUTTON_CROSS,
		&hostAction, &peerAction);
	CHECK(hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH);
	CHECK(peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	CHECK(CheckNotRacing() == 0);
	CHECK(CheckNoAgreedConfig() == 0);

	/* Race 2 (the rematch): a new config, and the consumed report does not
	 * leak into it. */
	CHECK(DrivePairToRace() == 0);
	CHECK(CheckAgreedConfig(&race2) == 0);
	CHECK(race2.masterSeed != race1.masterSeed);
	CHECK(StayRacing(30u) == 0);

	/* A report followed by a reset before any Tick: the abort re-initializes
	 * the link, and the latch goes with it. */
	CHECK(NativeArcadeLinkHost_ReportRaceFailure() == 1);
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(CheckNotRacing() == 0);
	/* An aborted race never reached RESULTS: no end-of-race record. */
	CHECK(CheckNoRaceEnd() == 0);
	CHECK(CheckNoAgreedMatch() == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(CheckNotRacing() == 0);
	NativeArcadeNetplay_Shutdown(&g_peer);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_RACE_FAILURE_HOST_PORT, TEST_RACE_FAILURE_PEER_PORT,
		UINT64_C(0x0FA12)) == 1);

	/* Race 3 (a fresh Enter): no leak; it finishes as RACE COMPLETE. */
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(DrivePairToRace() == 0);
	CHECK(CheckAgreedConfig(&race3) == 0);
	CHECK(StayRacing(30u) == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);
	CHECK(CheckNotRacing() == 0);
	/* The abort re-initialized the link, so this is race 1 again. */
	CHECK(CheckRaceEnd(1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);
	memset(&afterFailure, 0xA5, sizeof(afterFailure));
	CHECK(NativeArcadeLinkHost_GetAgreedConfig(&afterFailure) == 1);
	CHECK(memcmp(&afterFailure, &race3, sizeof(race3)) == 0);
	/* A report on RESULTS is ignored and changes nothing on the next Tick. */
	CHECK(NativeArcadeLinkHost_ReportRaceFailure() == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);

	NativeArcadeNetplay_Shutdown(&g_peer);
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckInert() == 0);
	return 0;
}

/* ---- LR-7: the race pacing switch ---- */

static int TestRacePacing(void)
{
	struct NativeArcadeLinkOptions linkOptions;
	struct NativeArcadeLinkOptions previewOptions;
	struct NativeIdentityV1 identity;
	uint32_t calls;

	/* Start from the platform's process-start state whatever ran before:
	 * Shutdown ends any race pacing the host began, then the stub resets. */
	NativeArcadeLinkHost_Shutdown();
	g_pacing = 0;
	g_pacingCalls = 0u;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&linkOptions, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_RACE_PACING_LOCAL_PORT,
		TEST_RACE_PACING_DEAD_PEER_PORT);
	NativeArcadeLinkOptions_SetDefaults(&previewOptions);
	previewOptions.preview = NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT;

	/* PREVIEW: no race begins; nothing is touched. */
	CHECK(NativeArcadeLinkHost_Configure(&previewOptions, NULL) == 1);
	CHECK(NativeArcadeLinkHost_RaceBegin() == 0);
	NativeArcadeLinkHost_RaceEnd();
	/* PREVIEW has no end-of-race record either (LR-S6). */
	CHECK(CheckNoRaceEnd() == 0);
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckPacingUntouched(0, 0u) == 0);

	/* LINK: Configure touches nothing. An Arm or Launch failure at the title
	 * disarms at once without a RaceBegin: RaceEnd leaves the pacing off and
	 * untouched. */
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, &identity) == 1);
	CHECK(CheckPacingUntouched(0, 0u) == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(CheckPacingUntouched(0, 0u) == 0);

	/* A launched race: RaceBegin on the Launch frame turns it on, RaceEnd on
	 * the Disarm frame turns it off, and a second RaceEnd does nothing. */
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(CheckPacingUntouched(1, 1u) == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(CheckPacingUntouched(0, 2u) == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(CheckPacingUntouched(0, 2u) == 0);
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);

	/* The next race, and a failed launch after it: on, off, then untouched. */
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(CheckPacingUntouched(1, 3u) == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(CheckPacingUntouched(0, 4u) == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(CheckPacingUntouched(0, 4u) == 0);

	/* A process exit mid-race: Shutdown turns it off; a second Shutdown and
	 * a RaceEnd after it touch nothing, and OFF begins no race. */
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(CheckPacingUntouched(1, 5u) == 0);
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckPacingUntouched(0, 6u) == 0);
	NativeArcadeLinkHost_Shutdown();
	NativeArcadeLinkHost_RaceEnd();
	CHECK(CheckPacingUntouched(0, 6u) == 0);
	CHECK(CheckInert() == 0);
	CHECK(CheckPacingUntouched(0, 6u) == 0);

	/* A replacing Configure shuts down first, so it too turns it off. */
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, &identity) == 1);
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(CheckPacingUntouched(1, 7u) == 0);
	CHECK(NativeArcadeLinkHost_Configure(&previewOptions, NULL) == 1);
	CHECK(CheckPacingUntouched(0, 8u) == 0);
	CHECK(NativeArcadeLinkHost_RaceBegin() == 0);
	CHECK(CheckPacingUntouched(0, 8u) == 0);
	NativeArcadeLinkHost_Shutdown();

	/* A pacing the host did not turn on (the roster proof's, main.c) is
	 * never touched: not by RaceEnd or Shutdown in OFF, and not by RaceEnd,
	 * Shutdown, or a Configure in LINK without a RaceBegin. */
	Platform_SetFixedVBlankPacing(1);
	calls = g_pacingCalls;
	NativeArcadeLinkHost_RaceEnd();
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckPacingUntouched(1, calls) == 0);
	CHECK(NativeArcadeLinkHost_Configure(&linkOptions, &identity) == 1);
	NativeArcadeLinkHost_RaceEnd();
	NativeArcadeLinkHost_Shutdown();
	CHECK(CheckPacingUntouched(1, calls) == 0);
	Platform_SetFixedVBlankPacing(0);
	CHECK(CheckInert() == 0);
	return 0;
}

/* ---- LR-S9: the race drive glue ---- */

/*
 * The host (CAB1) runs its drive through RaceBegin, RaceStep, RaceHold, and
 * Tick. The test-owned peer adapter (CAB2) runs a second drive core over its
 * own link, with the callbacks the host glue uses: the verbatim send on the
 * adapter's link, RaceService with 0 (poll) and 1 (period service), and
 * OnTakeResult with "latched" read back from pendingLinkFailure. A race tick
 * runs as a host pass does: both adapters' Ticks, then the steps; a hold is
 * resolved by spinning the hold loop's non-new-period iterations. Every
 * committed pad is checked on both sides against the samples of D ticks
 * earlier, normalized; the peer's socket is read directly where the test
 * counts what the host sent.
 */

#define DRIVE_D NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY
#define DRIVE_TICKS 128u
#define DRIVE_HUMANS 2u
#define DRIVE_HOLD_SPINS 20000u
#define DRIVE_SPIN_BUDGET 200000u
#define DRIVE_QUIET_SPINS 3000u
#define DRIVE_RECEIVE_BYTES 512u
/* The linger's resend window of an end tick F > D: frames F - D - 1 to
 * F + D - 1. */
#define DRIVE_LINGER_WINDOW (2u * DRIVE_D + 1u)

#define RACE_GO NATIVE_ARCADE_LINK_HOST_RACE_GO
#define RACE_HOLD NATIVE_ARCADE_LINK_HOST_RACE_HOLD
#define RACE_END NATIVE_ARCADE_LINK_HOST_RACE_END

static struct NativeArcadeRaceDrive g_peerDrive;
static struct NativeArcadeRaceDriveKept g_peerKept;
static struct NativeCanonicalStateV4 g_driveState;
static uint32_t g_driveStateFrame = UINT32_MAX;
static uint32_t g_driveStateKnob;
static int g_driveStateOk;
/* The committed pads of each race tick, as each side got them. */
static struct NativeArcadeLinkHostPad g_hostPads[DRIVE_TICKS][NATIVE_ARCADE_LINK_HOST_RACE_PADS];
static struct NativeCanonicalInputPadV1 g_peerPads[DRIVE_TICKS][NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];
static uint8_t g_hostGo[DRIVE_TICKS];
static uint8_t g_peerGo[DRIVE_TICKS];
/* The next race tick of each side. */
static uint32_t g_hostNext;
static uint32_t g_peerNext;
/* From race tick g_peerKnobFrom on, the peer's state differs (a desync). */
static uint32_t g_peerKnob;
static uint32_t g_peerKnobFrom;
/* The line of the first broken expectation inside a helper that returns a
 * status (CHECK would return 1, which reads as GO). */
static int g_driveBad;

#define DRIVE_EXPECT(expression) do { if (!(expression) && (g_driveBad == 0)) { g_driveBad = __LINE__; } } while (0)

/* The peer drive's callbacks, as the host glue backs its own. */
static int PeerSendBundle(void *context, uint32_t frameIndex, const uint8_t *bytes, size_t size)
{
	(void)context;
	(void)frameIndex;
	return NativeLockstepPeerLink_SendBundleVerbatim(NativeArcadeNetplay_Link(&g_peer), bytes, size);
}

static void PeerPoll(void *context)
{
	(void)context;
	NativeArcadeNetplay_RaceService(&g_peer, 0);
}

static void PeerServicePeriod(void *context)
{
	(void)context;
	NativeArcadeNetplay_RaceService(&g_peer, 1);
}

static int PeerTakeResult(void *context, enum NativeLockstepSessionResult result, uint32_t frameIndex)
{
	(void)context;
	NativeArcadeNetplay_OnTakeResult(&g_peer, result, frameIndex);
	return (g_peer.pendingLinkFailure != NATIVE_ARCADE_FLOW_END_NONE) ? 1 : 0;
}

/* A valid V4 state of frame whose digests are a pure function of the frame
 * and one WORLD counter (the drive test's StateFor). Cached. */
static const struct NativeCanonicalStateV4 *DriveState(uint32_t frame, uint32_t knob)
{
	if ((g_driveStateFrame != frame) || (g_driveStateKnob != knob))
	{
		NativeCanonicalStateV4_Init(&g_driveState);
		g_driveState.frameNumber = frame;
		g_driveState.control.frameCounter = (int32_t)frame;
		g_driveState.worldCounters.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
		g_driveState.worldCounters.activeBombMissileCount = knob;
		g_driveStateOk = NativeCanonicalStateV4_ComputeDigests(&g_driveState);
		g_driveStateFrame = frame;
		g_driveStateKnob = knob;
	}
	return g_driveStateOk ? &g_driveState : NULL;
}

/* The host's raw local sample of race tick k. Even ticks are already
 * normalized (connected, status 0, id 0x41 or 0x73, START released), so
 * their committed pad is the sample byte for byte; odd ticks carry odd
 * status and id bytes, START pressed, and now and then a disconnect. The
 * reserved bytes are never zero, and never reach the pad. */
static void HostSample(uint32_t k, struct NativeArcadeLinkHostPad *pad)
{
	const int raw = (k & 1u) != 0u;
	uint16_t word = (uint16_t)(0xffffu ^ ((k * 0x1235u) & 0xfff7u));

	if (raw)
	{
		word = (uint16_t)(word & ~0x0008u);
	}
	pad->status = raw ? (uint8_t)(0x40u + k) : 0u;
	pad->id = raw ? (uint8_t)(((k & 2u) != 0u) ? 0x12u : 0x73u) : (uint8_t)(((k & 2u) != 0u) ? 0x73u : 0x41u);
	pad->buttons[0] = (uint8_t)(word & 0xffu);
	pad->buttons[1] = (uint8_t)(word >> 8);
	pad->analog[0] = (uint8_t)(k * 7u);
	pad->analog[1] = (uint8_t)(k * 7u + 61u);
	pad->analog[2] = (uint8_t)(k * 7u + 122u);
	pad->analog[3] = (uint8_t)(k * 7u + 183u);
	pad->connected = (raw && ((k % 10u) == 3u)) ? 0u : (uint8_t)(raw ? 2u : 1u);
	pad->reserved[0] = 0xEEu;
	pad->reserved[1] = 0xEFu;
	pad->reserved[2] = 0xF0u;
}

/* The peer's raw local sample of race tick k. */
static void PeerSample(uint32_t k, struct NativeCanonicalInputPadV1 *pad)
{
	const uint16_t word = (uint16_t)(0xffffu ^ ((k * 0x0931u) & 0xffffu));

	pad->status = (uint8_t)(k * 3u);
	pad->id = (uint8_t)(((k % 3u) == 0u) ? 0x73u : 0x41u);
	pad->buttons[0] = (uint8_t)(word & 0xffu);
	pad->buttons[1] = (uint8_t)(word >> 8);
	pad->analog[0] = (uint8_t)(k * 5u + 1u);
	pad->analog[1] = (uint8_t)(k * 5u + 2u);
	pad->analog[2] = (uint8_t)(k * 5u + 3u);
	pad->analog[3] = (uint8_t)(k * 5u + 4u);
	pad->connected = ((k % 17u) == 9u) ? 0u : 1u;
}

/* The host pad as the drive's pad: the first nine bytes. */
static void HostToDrivePad(const struct NativeArcadeLinkHostPad *from, struct NativeCanonicalInputPadV1 *to)
{
	to->status = from->status;
	to->id = from->id;
	to->buttons[0] = from->buttons[0];
	to->buttons[1] = from->buttons[1];
	to->analog[0] = from->analog[0];
	to->analog[1] = from->analog[1];
	to->analog[2] = from->analog[2];
	to->analog[3] = from->analog[3];
	to->connected = from->connected;
}

/* A committed host pad: the drive pad's nine bytes, reserved zeroed. */
static int HostPadIs(const struct NativeArcadeLinkHostPad *pad, const struct NativeCanonicalInputPadV1 *expected)
{
	struct NativeCanonicalInputPadV1 got;

	HostToDrivePad(pad, &got);
	CHECK(memcmp(&got, expected, sizeof(got)) == 0);
	CHECK((pad->reserved[0] == 0u) && (pad->reserved[1] == 0u) && (pad->reserved[2] == 0u));
	return 0;
}

/* Race tick k's committed pads on both sides: [0] the host's sample of
 * k - D, [1] the peer's, each normalized (neutral before D), [2] and [3]
 * disconnected; on an even k - D the host's pad is its sample, byte for
 * byte. */
static int CheckCommitted(uint32_t k)
{
	struct NativeCanonicalInputPadV1 expected[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];
	struct NativeCanonicalInputPadV1 raw;
	struct NativeArcadeLinkHostPad hostSample;
	uint32_t i;

	CHECK(k < DRIVE_TICKS);
	CHECK((g_hostGo[k] != 0u) && (g_peerGo[k] != 0u));
	if (k < DRIVE_D)
	{
		NativeArcadeRaceDrive_NeutralPad(&expected[0]);
		NativeArcadeRaceDrive_NeutralPad(&expected[1]);
	}
	else
	{
		HostSample(k - DRIVE_D, &hostSample);
		HostToDrivePad(&hostSample, &raw);
		NativeArcadeRaceDrive_NormalizePad(&raw, &expected[0]);
		PeerSample(k - DRIVE_D, &raw);
		NativeArcadeRaceDrive_NormalizePad(&raw, &expected[1]);
		if (((k - DRIVE_D) & 1u) == 0u)
		{
			HostToDrivePad(&hostSample, &raw);
			CHECK(memcmp(&raw, &expected[0], sizeof(raw)) == 0);
		}
	}
	NativeArcadeRaceDrive_DisconnectedPad(&expected[2]);
	NativeArcadeRaceDrive_DisconnectedPad(&expected[3]);
	for (i = 0u; i < NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT; i++)
	{
		CHECK(HostPadIs(&g_hostPads[k][i], &expected[i]) == 0);
		CHECK(memcmp(&g_peerPads[k][i], &expected[i], sizeof(expected[i])) == 0);
	}
	return 0;
}

/* Every host pad still holds the 0xA5 fill (padsOut untouched). */
static int HostPadsUntouched(const struct NativeArcadeLinkHostPad pads[NATIVE_ARCADE_LINK_HOST_RACE_PADS])
{
	const uint8_t *bytes = (const uint8_t *)pads;
	size_t i;

	for (i = 0u; i < sizeof(struct NativeArcadeLinkHostPad) * NATIVE_ARCADE_LINK_HOST_RACE_PADS; i++)
	{
		if (bytes[i] != 0xA5u)
		{
			return 0;
		}
	}
	return 1;
}

/* A host status: GO keeps the pads of the host's next race tick; HOLD and
 * END must leave padsOut untouched. */
static uint32_t HostResult(uint32_t status, const struct NativeArcadeLinkHostPad pads[NATIVE_ARCADE_LINK_HOST_RACE_PADS])
{
	if (status == RACE_GO)
	{
		DRIVE_EXPECT(g_hostNext < DRIVE_TICKS);
		if (g_hostNext < DRIVE_TICKS)
		{
			memcpy(g_hostPads[g_hostNext], pads, sizeof(g_hostPads[g_hostNext]));
			g_hostGo[g_hostNext] = 1u;
		}
		g_hostNext += 1u;
	}
	else
	{
		DRIVE_EXPECT((status == RACE_HOLD) || (status == RACE_END));
		DRIVE_EXPECT(HostPadsUntouched(pads));
	}
	return status;
}

/* The host's step of its next race tick. */
static uint32_t HostStep(uint32_t endOfRace)
{
	struct NativeArcadeLinkHostPad sample;
	struct NativeArcadeLinkHostRaceFacts facts;
	struct NativeArcadeLinkHostPad pads[NATIVE_ARCADE_LINK_HOST_RACE_PADS];

	HostSample(g_hostNext, &sample);
	facts.endOfRace = endOfRace;
	facts.finishedHumans = 0u;
	facts.humans = DRIVE_HUMANS;
	memset(pads, 0xA5, sizeof(pads));
	return HostResult(NativeArcadeLinkHost_RaceStep(g_hostNext, DriveState(g_hostNext, 0u), &sample, &facts, pads), pads);
}

/* One host hold iteration. */
static uint32_t HostHold(uint32_t periods, int newPeriod)
{
	struct NativeArcadeLinkHostPad pads[NATIVE_ARCADE_LINK_HOST_RACE_PADS];

	memset(pads, 0xA5, sizeof(pads));
	return HostResult(NativeArcadeLinkHost_RaceHold(periods, newPeriod, pads), pads);
}

/* A peer status: GO keeps the pads of the peer's next race tick. */
static enum NativeArcadeRaceDriveStatus PeerResult(enum NativeArcadeRaceDriveStatus status,
	const struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT])
{
	if (status == NATIVE_ARCADE_RACE_DRIVE_GO)
	{
		DRIVE_EXPECT(g_peerNext < DRIVE_TICKS);
		if (g_peerNext < DRIVE_TICKS)
		{
			memcpy(g_peerPads[g_peerNext], pads, sizeof(g_peerPads[g_peerNext]));
			g_peerGo[g_peerNext] = 1u;
		}
		g_peerNext += 1u;
	}
	return status;
}

static enum NativeArcadeRaceDriveStatus PeerStep(uint32_t endOfRace)
{
	struct NativeCanonicalInputPadV1 sample;
	struct NativeArcadeRaceDriveFacts facts;
	struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];
	const uint32_t knob = ((g_peerKnob != 0u) && (g_peerNext >= g_peerKnobFrom)) ? g_peerKnob : 0u;

	PeerSample(g_peerNext, &sample);
	facts.endOfRace = endOfRace;
	facts.finishedHumans = 0u;
	facts.humans = DRIVE_HUMANS;
	return PeerResult(NativeArcadeRaceDrive_Step(&g_peerDrive, g_peerNext, DriveState(g_peerNext, knob), &sample, &facts, pads), pads);
}

static enum NativeArcadeRaceDriveStatus PeerHold(uint32_t periods, int newPeriod)
{
	struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];

	return PeerResult(NativeArcadeRaceDrive_Hold(&g_peerDrive, periods, newPeriod, pads), pads);
}

/* Spins the host's hold (non-new-period iterations at periods) while it
 * holds; returns the last status. */
static uint32_t HostSpin(uint32_t status, uint32_t periods)
{
	uint32_t spins;

	for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && (status == RACE_HOLD); spins++)
	{
		status = HostHold(periods, 0);
	}
	return status;
}

static enum NativeArcadeRaceDriveStatus PeerSpin(enum NativeArcadeRaceDriveStatus status)
{
	uint32_t spins;

	for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && (status == NATIVE_ARCADE_RACE_DRIVE_HOLD); spins++)
	{
		status = PeerHold(0u, 0);
	}
	return status;
}

/* Every datagram, of any size, the last DrainPeerBundles took. */
static uint32_t g_peerDatagrams;

/* Takes every datagram waiting on the peer's link socket, spinning until at
 * least want bundle-sized ones were taken (with want 0, for a quiet
 * period), then until it reads empty. Returns the bundle-sized datagrams
 * taken, and leaves the count of every datagram taken in g_peerDatagrams;
 * everything taken is discarded. */
static uint32_t DrainPeerBundles(uint32_t want)
{
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(&g_peer);
	uint8_t bytes[DRIVE_RECEIVE_BYTES];
	size_t byteCount = 0u;
	uint32_t taken = 0u;
	uint32_t spins;
	enum NativeUdpTransportReceiveResult result;

	g_peerDatagrams = 0u;
	if (link == NULL)
	{
		return UINT32_MAX;
	}
	for (spins = 0u; spins < DRIVE_SPIN_BUDGET; spins++)
	{
		result = NativeUdpTransport_Receive(&link->transport, bytes, sizeof(bytes), &byteCount, NULL);
		if (result == NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			g_peerDatagrams += 1u;
			taken += (byteCount == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES) ? 1u : 0u;
		}
		else if (result == NATIVE_UDP_TRANSPORT_RECEIVE_TOO_SMALL)
		{
			/* A datagram larger than the buffer is still one received. */
			g_peerDatagrams += 1u;
		}
		else if ((result == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY) && (taken >= want) && ((want > 0u) || (spins >= DRIVE_QUIET_SPINS)))
		{
			break;
		}
	}
	return taken;
}

static int GetDriveState(struct NativeArcadeLinkHostDriveState *state)
{
	memset(state, 0xA5, sizeof(*state));
	CHECK(NativeArcadeLinkHost_GetDriveState(state) == 1);
	return 0;
}

/* The drive is re-initialized: not begun, no end, no race tick. */
static int CheckDriveReset(void)
{
	struct NativeArcadeLinkHostDriveState state;

	CHECK(GetDriveState(&state) == 0);
	CHECK(state.begun == 0u);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE);
	CHECK(state.failureReason == 0u);
	CHECK(state.endTick == NATIVE_ARCADE_LINK_HOST_NO_TICK);
	CHECK(state.graceStartTick == NATIVE_ARCADE_LINK_HOST_NO_TICK);
	CHECK(state.raceTick == NATIVE_ARCADE_LINK_HOST_NO_TICK);
	CHECK(state.heldPeriods == 0u);
	CHECK(state.lingerTicksLeft == 0u);
	CHECK(state.bannerDue == 0u);
	CHECK(state.failureReported == 0u);
	CHECK(state.reserved == 0u);
	return 0;
}

static int BeginPeerDrive(void)
{
	struct NativeArcadeRaceDriveCallbacks callbacks;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.sendBundle = PeerSendBundle;
	callbacks.poll = PeerPoll;
	callbacks.onTakeResult = PeerTakeResult;
	callbacks.servicePeriod = PeerServicePeriod;
	CHECK(NativeArcadeRaceDrive_Begin(&g_peerDrive, NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(&g_peer)), &g_peerKept,
		&callbacks, 0u) == 1);
	return 0;
}

/* The host begins its race drive on RACING (RaceBegin); a new race book. */
static int BeginHostDrive(void)
{
	struct NativeArcadeLinkHostDriveState state;

	g_hostNext = 0u;
	g_peerNext = 0u;
	g_peerKnob = 0u;
	g_peerKnobFrom = 0u;
	g_driveBad = 0;
	memset(g_hostPads, 0, sizeof(g_hostPads));
	memset(g_peerPads, 0, sizeof(g_peerPads));
	memset(g_hostGo, 0, sizeof(g_hostGo));
	memset(g_peerGo, 0, sizeof(g_peerGo));
	CHECK(CheckDriveReset() == 0);
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(g_pacing == 1);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.begun == 1u);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE);
	CHECK(state.raceTick == NATIVE_ARCADE_LINK_HOST_NO_TICK);
	CHECK(state.failureReported == 0u);
	return 0;
}

/* Both drives: the host's, then the peer's. */
static int BeginRaceDrives(void)
{
	CHECK(BeginHostDrive() == 0);
	CHECK(BeginPeerDrive() == 0);
	return 0;
}

/* Configure, a fresh peer, Enter on both, a real launch, and both drives. */
static int StartDriveRace(uint32_t hostPort, uint32_t peerPort, uint64_t entropy)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, hostPort, peerPort);
	options.selectEntropy = entropy;
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, hostPort, peerPort, entropy ^ UINT64_C(0x5A5A)) == 1);
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(DrivePairToRace() == 0);
	CHECK(BeginRaceDrives() == 0);
	return 0;
}

static void StopDriveRace(void)
{
	NativeArcadeNetplay_Shutdown(&g_peer);
	NativeArcadeLinkHost_Shutdown();
	NativeArcadeRaceDrive_Init(&g_peerDrive);
}

/* Both steps of one race tick, as a host pass runs them: both adapters'
 * Ticks, then the host's step and the peer's (the peer's first when
 * peerFirst), each hold spun out. Both must GO with equal, expected pads. */
static int RoundBoth(int peerFirst)
{
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t host;
	enum NativeArcadeRaceDriveStatus peer;
	const uint32_t tick = g_hostNext;
	uint32_t spins;

	CHECK(g_peerNext == tick);
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
	CHECK((hostAction == ACT_NONE) && (peerAction == ACT_NONE));
	if (peerFirst)
	{
		peer = PeerStep(0u);
		host = HostStep(0u);
	}
	else
	{
		host = HostStep(0u);
		peer = PeerStep(0u);
	}
	for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && ((host == RACE_HOLD) || (peer == NATIVE_ARCADE_RACE_DRIVE_HOLD)); spins++)
	{
		if (host == RACE_HOLD)
		{
			host = HostHold(0u, 0);
		}
		if (peer == NATIVE_ARCADE_RACE_DRIVE_HOLD)
		{
			peer = PeerHold(0u, 0);
		}
	}
	CHECK(g_driveBad == 0);
	CHECK(host == RACE_GO);
	CHECK(peer == NATIVE_ARCADE_RACE_DRIVE_GO);
	CHECK(CheckCommitted(tick) == 0);
	return 0;
}

static int RoundsBoth(uint32_t untilTick)
{
	while (g_hostNext < untilTick)
	{
		CHECK(RoundBoth(0) == 0);
	}
	return 0;
}

/* The host's pass alone (its Tick, then its step), with the hold spun out. */
static uint32_t HostPass(void)
{
	DRIVE_EXPECT(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	return HostSpin(HostStep(0u), 0u);
}

/* A stray RaceStep and RaceHold while the finish linger runs (LR-54): both
 * END with padsOut untouched, and the drive is kept as it was: the finish
 * end, the begun flag, and the linger ticks left. */
static int StrayStepAndHoldKeepLinger(uint32_t begun, uint32_t lingerTicksLeft)
{
	struct NativeArcadeLinkHostDriveState state;

	CHECK(HostStep(0u) == RACE_END);
	CHECK(HostHold(1u, 1) == RACE_END);
	CHECK(HostHold(0u, 0) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE);
	CHECK(state.begun == begun);
	CHECK(state.lingerTicksLeft == lingerTicksLeft);
	CHECK(state.failureReported == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	return 0;
}

/*
 * LR-S9 step, hold, and end over a real launch. 40 race ticks commit equal
 * pads on both sides. The peer pauses after race tick 39: the host runs D
 * ticks ahead, then its step holds, and 20 hold periods (resend, service,
 * one counted stall report each, far below the timeout, the banner due from 10)
 * stay HOLD with the flow on RACING. The peer resumes, and the host's hold
 * ends GO. On race tick 60 both see END_OF_RACE and end as END_OF_RACE; the
 * next Ticks move both flows to RESULTS RACE COMPLETE; the host's linger
 * then resends its window of 2D + 1 kept bundles on exactly 15 host ticks
 * (a RaceEnd on the way keeps it: the linger overlaps the return load), then
 * nothing, and the drive is re-initialized. A stray step and hold after the
 * finish END (on RACING, and on RESULTS before and after that RaceEnd) are
 * END, send nothing, and leave the linger running (LR-54).
 */
static int TestDriveStepHoldEnd(void)
{
	struct NativeArcadeLinkHostDriveState state;
	const uint32_t pauseAt = 40u;
	const uint32_t finishAt = 60u;
	uint32_t status;
	uint32_t period;
	uint32_t tick;
	uint32_t spins;
	enum NativeArcadeRaceDriveStatus peer;

	CHECK(StartDriveRace(TEST_DRIVE_HOST_PORT, TEST_DRIVE_PEER_PORT, UINT64_C(0xD21FE00000000001)) == 0);
	CHECK(RoundsBoth(pauseAt) == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.raceTick == pauseAt - 1u);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE);
	CHECK(state.heldPeriods == 0u);

	/* The peer pauses: the host takes D more ticks, then holds. */
	for (tick = 0u; tick < DRIVE_D; tick++)
	{
		CHECK(HostPass() == RACE_GO);
	}
	CHECK(HostPass() == RACE_HOLD);
	CHECK(g_driveBad == 0);
	CHECK(g_hostNext == pauseAt + DRIVE_D);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.raceTick == pauseAt + DRIVE_D);
	CHECK(state.heldPeriods == 0u);
	for (period = 1u; period <= 20u; period++)
	{
		CHECK(HostHold(period, 1) == RACE_HOLD);
		CHECK(HostHold(period, 0) == RACE_HOLD);
		CHECK(GetDriveState(&state) == 0);
		CHECK(state.heldPeriods == period);
		CHECK(state.bannerDue == ((period >= NATIVE_ARCADE_RACE_DRIVE_HOLD_GRACE_PERIODS) ? 1u : 0u));
		CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE);
		CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
		/* One counted stall report per new period, outside the start grace. */
		CHECK(NativeArcadeLinkHost_InternalConsecutiveStalls() == period);
	}
	CHECK(g_driveBad == 0);

	/* The peer resumes: its ticks up to the host's all GO. */
	while (g_peerNext <= pauseAt + DRIVE_D)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 0u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
		CHECK(PeerSpin(PeerStep(0u)) == NATIVE_ARCADE_RACE_DRIVE_GO);
	}
	status = RACE_HOLD;
	for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && (status == RACE_HOLD); spins++)
	{
		status = HostHold(20u, 0);
	}
	CHECK(status == RACE_GO);
	CHECK(g_driveBad == 0);
	CHECK(g_hostNext == pauseAt + DRIVE_D + 1u);
	/* The take that ended the hold was not a stall: the count is cleared. */
	CHECK(NativeArcadeLinkHost_InternalConsecutiveStalls() == 0u);
	for (tick = pauseAt; tick <= pauseAt + DRIVE_D; tick++)
	{
		CHECK(CheckCommitted(tick) == 0);
	}

	/* In step again, up to the finish. */
	CHECK(RoundsBoth(finishAt) == 0);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	{
		uint32_t hostAction = ACT_NONE;
		uint32_t peerAction = ACT_NONE;

		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK((hostAction == ACT_NONE) && (peerAction == ACT_NONE));
	}
	CHECK(HostStep(1u) == RACE_END);
	peer = PeerStep(1u);
	CHECK(peer == NATIVE_ARCADE_RACE_DRIVE_END);
	CHECK(g_driveBad == 0);
	CHECK(NativeArcadeRaceDrive_EndKind(&g_peerDrive) == NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE);
	CHECK(strcmp(NativeArcadeLinkHost_DriveEndKindName(state.endKind), "end of race") == 0);
	CHECK(state.endTick == finishAt);
	CHECK(state.raceTick == finishAt);
	CHECK(state.lingerTicksLeft == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS);
	CHECK(state.failureReported == 0u);
	CHECK(state.begun == 1u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	/* Still RACING until the caller reports the finish on the next Tick. */
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);

	/* The peer's Tick first, so its poll takes what the host sent before. */
	CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 1u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
	CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	(void)DrainPeerBundles(0u);
	/* A stray step and hold after the finish END, still on RACING: END, and
	 * the drive and its linger are kept (the drain below proves nothing was
	 * sent). */
	CHECK(StrayStepAndHoldKeepLinger(1u, NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS) == 0);
	/* A host Tick that does not report the finish leaves the flow on
	 * RACING: the linger waits for RESULTS and sends nothing. */
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(DrainPeerBundles(0u) == 0u);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.lingerTicksLeft == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS);
	/* The host's: RESULTS RACE COMPLETE, and the linger's first tick. */
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);
	CHECK(CheckRaceEnd(1u, NATIVE_ARCADE_FLOW_END_FINISHED, 0u) == 0);
	CHECK(DrainPeerBundles(DRIVE_LINGER_WINDOW) == DRIVE_LINGER_WINDOW);
	for (tick = 2u; tick <= NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS; tick++)
	{
		if (tick == 6u)
		{
			/* The Disarm frame lands inside the linger: the pacing goes off,
			 * the linger goes on. */
			NativeArcadeLinkHost_RaceEnd();
			CHECK(g_pacing == 0);
			CHECK(GetDriveState(&state) == 0);
			CHECK(state.begun == 0u);
			CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE);
			CHECK(state.lingerTicksLeft == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS - 5u);
		}
		if ((tick == 3u) || (tick == 9u))
		{
			/* A stray step and hold on RESULTS, before and after the Disarm:
			 * END with nothing sent, and the linger runs on. */
			CHECK(StrayStepAndHoldKeepLinger((tick < 6u) ? 1u : 0u, NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS - (tick - 1u)) == 0);
			CHECK(DrainPeerBundles(0u) == 0u);
			CHECK(g_peerDatagrams == 0u);
		}
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
		CHECK(DrainPeerBundles(DRIVE_LINGER_WINDOW) == DRIVE_LINGER_WINDOW);
	}
	/* Done: the drive is re-initialized, and nothing more is sent. */
	CHECK(CheckDriveReset() == 0);
	for (tick = 0u; tick < 5u; tick++)
	{
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
		CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	}
	CHECK(DrainPeerBundles(0u) == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);

	StopDriveRace();
	CHECK(CheckInert() == 0);
	return 0;
}

/* Polls the peer's link directly and discards every launch record the host
 * sent (the aux inbox), so none reaches the peer's launch agreement; returns
 * how many were discarded. */
static uint32_t DropHostLaunchRecordsAtPeer(void)
{
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(&g_peer);
	uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0u;
	uint32_t dropped = 0u;
	uint32_t spins;

	if (link == NULL)
	{
		return 0u;
	}
	for (spins = 0u; spins < 50u; spins++)
	{
		NativeLockstepPeerLink_Poll(link);
	}
	while (NativeLockstepPeerLink_TakeAux(link, bytes, sizeof(bytes), &size))
	{
		dropped += 1u;
	}
	return dropped;
}

/* Polls the peer's link until its aux inbox holds want records, then a
 * while longer; returns the count. */
static uint32_t PeerAuxAfterPolls(uint32_t want)
{
	struct NativeLockstepPeerLink *link = NativeArcadeNetplay_Link(&g_peer);
	uint32_t spins;

	if (link == NULL)
	{
		return UINT32_MAX;
	}
	for (spins = 0u; (spins < DRIVE_SPIN_BUDGET) && (NativeLockstepPeerLink_AuxCount(link) < want); spins++)
	{
		NativeLockstepPeerLink_Poll(link);
	}
	for (spins = 0u; spins < DRIVE_QUIET_SPINS; spins++)
	{
		NativeLockstepPeerLink_Poll(link);
	}
	return NativeLockstepPeerLink_AuxCount(link);
}

/*
 * LR-S9 the hold's launch-linger send (LR-9). As in the adapter's case 33c,
 * the host commits and starts its race while every launch record its Tick
 * sends is discarded before the peer's agreement sees it, so the peer is
 * still PENDING on SELECT_RESULT. The host begins its drive, and its race
 * tick 0 holds (the start wait). Each new hold period sends exactly one
 * launch record (the other iterations none); the peer commits and starts
 * its race from them, its drive begins, and the host's hold ends GO with
 * equal pads on both sides. Nothing was reported as a stall (the start
 * grace): the adapter's consecutive stall count stays 0 through the hold.
 */
static int TestDriveHoldLaunchLinger(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	struct NativeArcadeLinkHostDriveState state;
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t heldHost;
	uint32_t heldPeer;
	uint32_t tick;
	uint32_t period;
	uint32_t status;
	uint32_t spins;
	uint32_t dropped = 0u;
	int hostRelinked = 0;
	int peerRelinked = 0;
	int hostStarted = 0;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_DRIVE2_HOST_PORT,
		TEST_DRIVE2_PEER_PORT);
	options.selectEntropy = UINT64_C(0x11F6E11F6E000002);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_DRIVE2_HOST_PORT, TEST_DRIVE2_PEER_PORT,
		UINT64_C(0x11F6E2)) == 1);
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);

	/* Both through the select to their relink. */
	for (tick = 0u; (tick < PAIR_BUDGET) && !(hostRelinked && peerRelinked); tick++)
	{
		heldHost = ((HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) && ((tick & 1u) == 0u))
			? NATIVE_ARCADE_MENU_BUTTON_CROSS
			: 0u;
		heldPeer = ((PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) && ((tick & 1u) == 0u))
			? NATIVE_ARCADE_MENU_BUTTON_CROSS
			: 0u;
		NativeArcadeLinkLoopback_TickPair(&g_peer, heldHost, heldPeer, &hostAction, &peerAction);
		CHECK(hostAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		CHECK(peerAction != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		hostRelinked = hostRelinked || (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RELINK);
		peerRelinked = peerRelinked || (peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RELINK);
	}
	CHECK(hostRelinked && peerRelinked);

	/* The host commits and races; none of its Tick records reach the
	 * peer's agreement. */
	for (tick = 0u; (tick < PAIR_BUDGET) && !hostStarted; tick++)
	{
		hostAction = NativeArcadeLinkHost_Tick(0u, 0u);
		CHECK((hostAction == ACT_NONE) || (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE));
		hostStarted = (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE);
		dropped += DropHostLaunchRecordsAtPeer();
		CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 0u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
		CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
		CHECK(g_peer.launch.acceptedCount == 0u);
	}
	CHECK(hostStarted);
	CHECK(dropped > 0u);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeLaunch_Status(&g_peer.launch) == (uint32_t)NATIVE_ARCADE_LAUNCH_PENDING);
	/* Whatever the host's START_RACE Tick sent goes too. */
	(void)DropHostLaunchRecordsAtPeer();
	CHECK(PeerAuxAfterPolls(0u) == 0u);

	/* The host's race tick 0 holds: the peer has not even committed. */
	CHECK(BeginHostDrive() == 0);
	CHECK(HostSpin(HostStep(0u), 0u) == RACE_HOLD);
	CHECK(PeerAuxAfterPolls(0u) == 0u);
	for (period = 1u; period <= 3u; period++)
	{
		CHECK(HostHold(period, 1) == RACE_HOLD);
		CHECK(HostHold(period, 0) == RACE_HOLD);
		CHECK(HostHold(period, 0) == RACE_HOLD);
		CHECK(PeerAuxAfterPolls(period) == period);
		CHECK(GetDriveState(&state) == 0);
		CHECK(state.heldPeriods == period);
		CHECK(state.raceTick == 0u);
		/* The start grace: no period was reported as a stall. */
		CHECK(NativeArcadeLinkHost_InternalConsecutiveStalls() == 0u);
	}
	CHECK(g_driveBad == 0);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);

	/* The peer commits from the hold's records and starts its race. */
	CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 0u) == NATIVE_ARCADE_FLOW_ACTION_START_RACE);
	CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeLaunch_Status(&g_peer.launch) == (uint32_t)NATIVE_ARCADE_LAUNCH_COMMITTED);
	CHECK(g_peer.launch.acceptedCount >= 1u);
	CHECK(BeginPeerDrive() == 0);
	CHECK(PeerSpin(PeerStep(0u)) == NATIVE_ARCADE_RACE_DRIVE_GO);
	status = RACE_HOLD;
	for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && (status == RACE_HOLD); spins++)
	{
		status = HostHold(3u, 0);
	}
	CHECK(status == RACE_GO);
	CHECK(CheckCommitted(0u) == 0);
	CHECK(RoundsBoth(12u) == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);

	StopDriveRace();
	CHECK(CheckInert() == 0);
	return 0;
}

/*
 * LR-S9 take classification. (a) A desync: from race tick 12 the peer's
 * state differs; the peer leads each tick, so the host compares the peer's
 * digest of 12 on arrival in its step of 13, and ends as the outcome; the
 * next Tick shows RACE OUT OF SYNC, and no local failure was reported. (b) A
 * stall to the timeout: the peer stops; 90 counted hold periods end the
 * host's drive as the outcome, and the next Tick shows OPPONENT
 * DISCONNECTED. (c) A local failure (a race tick out of order) is reported
 * once, whatever follows, and the next Tick shows LINK ERROR.
 */
static int TestDriveTakeClassification(void)
{
	struct NativeArcadeLinkHostDriveState state;
	struct NativeArcadeLinkHostPad sample;
	struct NativeArcadeLinkHostRaceFacts facts;
	struct NativeArcadeLinkHostPad pads[NATIVE_ARCADE_LINK_HOST_RACE_PADS];
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t host = RACE_GO;
	enum NativeArcadeRaceDriveStatus peer;
	uint32_t period;
	uint32_t spins;
	uint32_t tick;

	/* (a) */
	CHECK(StartDriveRace(TEST_DRIVE_HOST_PORT, TEST_DRIVE_PEER_PORT, UINT64_C(0xDE5C000000000003)) == 0);
	CHECK(RoundsBoth(12u) == 0);
	g_peerKnob = 7u;
	g_peerKnobFrom = 12u;
	for (tick = 0u; (tick < 10u) && (host != RACE_END); tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK((hostAction == ACT_NONE) && (peerAction == ACT_NONE));
		CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
		peer = PeerStep(0u);
		host = HostStep(0u);
		for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && ((host == RACE_HOLD) || (peer == NATIVE_ARCADE_RACE_DRIVE_HOLD)); spins++)
		{
			host = (host == RACE_HOLD) ? HostHold(0u, 0) : host;
			peer = (peer == NATIVE_ARCADE_RACE_DRIVE_HOLD) ? PeerHold(0u, 0) : peer;
		}
		CHECK(peer == NATIVE_ARCADE_RACE_DRIVE_GO);
	}
	CHECK(host == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(g_hostNext == 13u);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME);
	CHECK(state.failureReason == 0u);
	CHECK(state.endTick == 13u);
	CHECK(state.failureReported == 0u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	StopDriveRace();

	/* (b) */
	CHECK(StartDriveRace(TEST_DRIVE2_HOST_PORT, TEST_DRIVE2_PEER_PORT, UINT64_C(0x57A1100000000004)) == 0);
	CHECK(RoundsBoth(20u) == 0);
	for (tick = 0u; tick < DRIVE_D; tick++)
	{
		CHECK(HostPass() == RACE_GO);
	}
	CHECK(HostPass() == RACE_HOLD);
	host = RACE_HOLD;
	for (period = 1u; (period <= 200u) && (host == RACE_HOLD); period++)
	{
		host = HostHold(period, 1);
		if (host == RACE_HOLD)
		{
			CHECK(HostHold(period, 0) == RACE_HOLD);
			CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
		}
	}
	CHECK(host == RACE_END);
	CHECK(period - 1u == NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS);
	CHECK(g_driveBad == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME);
	CHECK(state.heldPeriods == NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS);
	CHECK(state.endTick == 20u + DRIVE_D);
	CHECK(state.failureReported == 0u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	StopDriveRace();

	/* (c) */
	CHECK(StartDriveRace(TEST_DRIVE_HOST_PORT, TEST_DRIVE_PEER_PORT, UINT64_C(0x10CA1F0000000005)) == 0);
	CHECK(RoundsBoth(5u) == 0);
	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
	HostSample(g_hostNext, &sample);
	facts.endOfRace = 0u;
	facts.finishedHumans = 0u;
	facts.humans = DRIVE_HUMANS;
	memset(pads, 0xA5, sizeof(pads));
	CHECK(NativeArcadeLinkHost_RaceStep(g_hostNext + 5u, DriveState(g_hostNext + 5u, 0u), &sample, &facts, pads) == RACE_END);
	CHECK(HostPadsUntouched(pads));
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE);
	CHECK(state.failureReason == (uint32_t)NATIVE_ARCADE_RACE_DRIVE_FAILURE_RACE_TICK);
	CHECK(strcmp(NativeArcadeLinkHost_DriveFailureName(state.failureReason), "race tick out of order") == 0);
	CHECK(strcmp(NativeArcadeLinkHost_DriveEndKindName(state.endKind), "local failure") == 0);
	CHECK(state.failureReported == 1u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 1u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 1u);
	/* Once per race: a step and a hold after it are END and report nothing. */
	CHECK(HostStep(0u) == RACE_END);
	CHECK(HostHold(1u, 1) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 1u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 1u);
	StopDriveRace();
	CHECK(CheckInert() == 0);
	return 0;
}

/*
 * LR-S9, the plan's required case: a parked peer digest that mismatches
 * inside RecordLocalDigests. The peer leads by two with a state that differs
 * from race tick 30, so its bundle for frame 33 carries its digest of 30;
 * the host's Tick parks it. The host's step of 30 records 30, the parked
 * digest mismatches inside the record, and the step ends as the outcome on
 * that tick; the next Tick shows RACE OUT OF SYNC. From the host's step on,
 * the peer's socket receives nothing from the host, no datagram of any
 * size: no new bundle, no resend, no hold, and no linger.
 */
static int TestDriveParkedDigestMismatch(void)
{
	struct NativeArcadeLinkHostDriveState state;
	const uint32_t x = 30u;
	uint32_t tick;

	CHECK(StartDriveRace(TEST_DRIVE2_HOST_PORT, TEST_DRIVE2_PEER_PORT, UINT64_C(0x9A2CED0000000006)) == 0);
	CHECK(RoundsBoth(x) == 0);
	g_peerKnob = 5u;
	g_peerKnobFrom = x;
	/* The peer's ticks 30 and 31. */
	for (tick = 0u; tick < 2u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 0u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
		CHECK(PeerSpin(PeerStep(0u)) == NATIVE_ARCADE_RACE_DRIVE_GO);
	}
	CHECK(g_peerNext == x + 2u);
	/* The host's pass: its Tick parks the digest; the peer's socket is
	 * emptied of everything the host sent before. */
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	(void)DrainPeerBundles(0u);
	CHECK(HostStep(0u) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME);
	CHECK(state.endTick == x);
	CHECK(state.raceTick == x);
	CHECK(state.failureReported == 0u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	/* Nothing more: a hold and a step are END. */
	CHECK(HostHold(1u, 1) == RACE_END);
	CHECK(HostStep(0u) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	for (tick = 0u; tick < 20u; tick++)
	{
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	}
	/* Literally nothing: no datagram of any size since the latch. */
	CHECK(DrainPeerBundles(0u) == 0u);
	CHECK(g_peerDatagrams == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	StopDriveRace();
	CHECK(CheckInert() == 0);
	return 0;
}

/* From RESULTS on both (the peer finishing its own race first if it is
 * still RACING): past the dwell, REMATCH on both, and the next launch. */
static int RematchToRace(void)
{
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t tick;

	if (PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 1u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
	}
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	for (tick = 0u; tick <= NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_DWELL_TICKS; tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK((hostAction == ACT_NONE) && (peerAction == ACT_NONE));
	}
	NativeArcadeLinkLoopback_TickPair(&g_peer, NATIVE_ARCADE_MENU_BUTTON_CROSS, NATIVE_ARCADE_MENU_BUTTON_CROSS, &hostAction,
		&peerAction);
	CHECK(hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH);
	CHECK(peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	/* The Tick off RACING and RESULTS re-initialized the drive. */
	CHECK(CheckDriveReset() == 0);
	CHECK(DrivePairToRace() == 0);
	return 0;
}

/* From any host screen, when a rematch is not on offer: the host aborts to
 * the title, a fresh peer, Enter on both, and the next launch. */
static int RepairToRace(uint64_t peerEntropy)
{
	struct NativeIdentityV1 identity;

	NativeArcadeLinkHost_AbortToTitle();
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(CheckDriveReset() == 0);
	NativeArcadeNetplay_Shutdown(&g_peer);
	NativeArcadeLinkLoopback_Identity(&identity);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_DRIVE_HOST_PORT, TEST_DRIVE_PEER_PORT, peerEntropy) == 1);
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(DrivePairToRace() == 0);
	return 0;
}

/* The host's step of its next race tick with END_OF_RACE, after both
 * adapters' Ticks: a finish END on RACING with the full linger ahead. */
static int HostFinishOnRacing(void)
{
	struct NativeArcadeLinkHostDriveState state;
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	const uint32_t tick = g_hostNext;

	NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
	CHECK((hostAction == ACT_NONE) && (peerAction == ACT_NONE));
	CHECK(HostStep(1u) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE);
	CHECK(state.endTick == tick);
	CHECK(state.lingerTicksLeft == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS);
	CHECK(state.begun == 1u);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	return 0;
}

/* A few more host Ticks, and the peer's socket has received nothing at all
 * since the caller last drained it. */
static int HostTicksSendNothing(void)
{
	uint32_t tick;

	for (tick = 0u; tick < 5u; tick++)
	{
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	}
	CHECK(DrainPeerBundles(0u) == 0u);
	CHECK(g_peerDatagrams == 0u);
	return 0;
}

/* RaceStep and RaceHold are END and send nothing; the drive stays (or is
 * made) re-initialized. */
static int CheckDriveRefused(void)
{
	CHECK(HostStep(0u) == RACE_END);
	CHECK(HostHold(1u, 1) == RACE_END);
	CHECK(HostHold(0u, 0) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(CheckDriveReset() == 0);
	return 0;
}

/*
 * LR-S9 the refusals and re-initialization points, and the pad mirror.
 * Outside LINK every drive call is inert. In LINK, before RaceBegin (on the
 * title and on RACING), RaceStep and RaceHold are END with nothing sent and
 * nothing reported. Race A: RaceEnd on RACING re-initializes a running
 * drive; a second RaceBegin over the now used session is refused, ends the
 * drive as a local failure, and reports it once (LINK ERROR); the Tick that
 * leaves RESULTS for REMATCH_WAIT re-initializes the drive. Race B: the
 * caller's own failure report moves the flow to RESULTS under a running
 * drive, and a step or hold there is refused with nothing sent and the
 * drive re-initialized. Race C: RaceEnd after a finish END still on RACING
 * re-initializes the drive, so no linger follows. Race D: RaceEnd on
 * RESULTS re-initializes a LOCAL_FAILURE drive, with no linger. Race E:
 * AbortToTitle re-initializes it. Race F: RaceEnd on RESULTS re-initializes
 * an OUTCOME drive, with no linger. Race G: the linger stops on RESULTS
 * when the session leaves RUNNING, and the drive is re-initialized. Race H:
 * Shutdown does, and a new Configure starts from a re-initialized drive.
 */
static int TestDriveRefusalsAndResets(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeArcadeLinkOptions previewOptions;
	struct NativeIdentityV1 identity;
	struct NativeArcadeLinkHostDriveState state;
	struct NativeArcadeLinkHostDriveState sentinel;
	uint32_t hostAction = ACT_NONE;
	uint32_t peerAction = ACT_NONE;
	uint32_t host = RACE_GO;
	enum NativeArcadeRaceDriveStatus peer;
	uint32_t tick;
	uint32_t spins;

	/* The pad mirror: 12 bytes, laid out as the platform pad snapshot. */
	CHECK(sizeof(struct NativeArcadeLinkHostPad) == 12u);
	CHECK(offsetof(struct NativeArcadeLinkHostPad, status) == 0u);
	CHECK(offsetof(struct NativeArcadeLinkHostPad, id) == 1u);
	CHECK(offsetof(struct NativeArcadeLinkHostPad, buttons) == 2u);
	CHECK(offsetof(struct NativeArcadeLinkHostPad, analog) == 4u);
	CHECK(offsetof(struct NativeArcadeLinkHostPad, connected) == 8u);
	CHECK(offsetof(struct NativeArcadeLinkHostPad, reserved) == 9u);
	CHECK(NATIVE_ARCADE_LINK_HOST_RACE_PADS == NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT);
	CHECK(RACE_GO == (uint32_t)NATIVE_ARCADE_RACE_DRIVE_GO);
	CHECK(RACE_HOLD == (uint32_t)NATIVE_ARCADE_RACE_DRIVE_HOLD);
	CHECK(RACE_END == (uint32_t)NATIVE_ARCADE_RACE_DRIVE_END);
	CHECK(NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE == (uint32_t)NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE);
	CHECK(strcmp(NativeArcadeLinkHost_DriveEndKindName(NATIVE_ARCADE_LINK_HOST_DRIVE_END_FINISH_GRACE), "finish grace") == 0);
	CHECK(strcmp(NativeArcadeLinkHost_DriveEndKindName(99u), "unknown") == 0);
	CHECK(strcmp(NativeArcadeLinkHost_DriveFailureName(0u), "none") == 0);

	/* OFF, and PREVIEW: inert. */
	NativeArcadeLinkHost_Shutdown();
	g_driveBad = 0;
	g_hostNext = 0u;
	CHECK(HostStep(0u) == RACE_END);
	CHECK(HostHold(1u, 1) == RACE_END);
	memset(&state, 0xA5, sizeof(state));
	sentinel = state;
	CHECK(NativeArcadeLinkHost_GetDriveState(&state) == 0);
	CHECK(memcmp(&state, &sentinel, sizeof(state)) == 0);
	CHECK(NativeArcadeLinkHost_GetDriveState(NULL) == 0);
	NativeArcadeLinkOptions_SetDefaults(&previewOptions);
	previewOptions.preview = NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED;
	CHECK(NativeArcadeLinkHost_Configure(&previewOptions, NULL) == 1);
	CHECK(HostStep(0u) == RACE_END);
	CHECK(HostHold(1u, 1) == RACE_END);
	CHECK(NativeArcadeLinkHost_GetDriveState(&state) == 0);
	CHECK(g_driveBad == 0);
	NativeArcadeLinkHost_Shutdown();

	/* LINK on the title: refused; RaceBegin turns the pacing on but begins
	 * no drive off RACING. */
	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_DRIVE_HOST_PORT,
		TEST_DRIVE_PEER_PORT);
	options.selectEntropy = UINT64_C(0x2E5E700000000007);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(CheckDriveRefused() == 0);
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(g_pacing == 1);
	CHECK(CheckDriveReset() == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);

	/* Race A: on RACING before RaceBegin, refused, nothing sent. */
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_DRIVE_HOST_PORT, TEST_DRIVE_PEER_PORT, UINT64_C(0x2E5E7)) == 1);
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(DrivePairToRace() == 0);
	(void)DrainPeerBundles(0u);
	CHECK(CheckDriveRefused() == 0);
	CHECK(DrainPeerBundles(0u) == 0u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	/* RaceEnd on RACING re-initializes a running drive. */
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(3u) == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);
	CHECK(CheckDriveReset() == 0);
	(void)DrainPeerBundles(0u);
	CHECK(CheckDriveRefused() == 0);
	CHECK(DrainPeerBundles(0u) == 0u);
	/* A second RaceBegin over the used session: refused and reported once. */
	CHECK(NativeArcadeLinkHost_RaceBegin() == 1);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.begun == 1u);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE);
	CHECK(state.failureReason == (uint32_t)NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_STARTED);
	CHECK(state.failureReported == 1u);
	CHECK(NativeArcadeLinkHost_InternalLocalRaceFailure() == 1u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 1u);
	g_hostNext = 3u;
	CHECK(HostStep(0u) == RACE_END);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 1u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	/* A local failure's drive stays for the log on RESULTS, through the
	 * dwell, until the Tick that leaves RESULTS for REMATCH_WAIT (checked in
	 * RematchToRace). */
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE);
	CHECK(state.begun == 1u);

	/* Race B: RESULTS under a running drive; a step or hold is refused. */
	CHECK(RematchToRace() == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(3u) == 0);
	CHECK(NativeArcadeLinkHost_ReportRaceFailure() == 1);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.begun == 1u);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE);
	CHECK(state.raceTick == 2u);
	(void)DrainPeerBundles(0u);
	CHECK(CheckDriveRefused() == 0);
	CHECK(DrainPeerBundles(0u) == 0u);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 1u);
	NativeArcadeLinkHost_RaceEnd();

	/* Race C: a finish END while the flow is still on RACING. RaceEnd there
	 * re-initializes the drive (no linger is kept off RESULTS), so the Tick
	 * that reports the finish, and the Ticks after it, send nothing. */
	CHECK(RematchToRace() == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(4u) == 0);
	CHECK(HostFinishOnRacing() == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);
	CHECK(CheckDriveReset() == 0);
	(void)DrainPeerBundles(0u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);
	CHECK(HostTicksSendNothing() == 0);
	CHECK(CheckDriveReset() == 0);

	/* Race D: a LOCAL_FAILURE drive on RESULTS; RaceEnd re-initializes it,
	 * with no linger. */
	CHECK(RematchToRace() == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(3u) == 0);
	g_hostNext += 5u;
	CHECK(HostStep(0u) == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 2u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE);
	CHECK(state.begun == 1u);
	CHECK(state.lingerTicksLeft == 0u);
	(void)DrainPeerBundles(0u);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);
	CHECK(CheckDriveReset() == 0);
	CHECK(HostTicksSendNothing() == 0);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 2u);

	/* Race E: AbortToTitle. */
	CHECK(RematchToRace() == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(3u) == 0);
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(CheckDriveReset() == 0);
	CHECK(CheckDriveRefused() == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);

	/* Race F: an OUTCOME drive on RESULTS (a desync from race tick 12, the
	 * peer leading); RaceEnd re-initializes it, with no linger. A desync
	 * offers no rematch, so the next race is a new pairing. */
	CHECK(RepairToRace(UINT64_C(0x2E5E9)) == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(12u) == 0);
	g_peerKnob = 7u;
	g_peerKnobFrom = 12u;
	host = RACE_GO;
	for (tick = 0u; (tick < 10u) && (host != RACE_END); tick++)
	{
		NativeArcadeLinkLoopback_TickPair(&g_peer, 0u, 0u, &hostAction, &peerAction);
		CHECK((hostAction == ACT_NONE) && (peerAction == ACT_NONE));
		peer = PeerStep(0u);
		host = HostStep(0u);
		for (spins = 0u; (spins < DRIVE_HOLD_SPINS) && ((host == RACE_HOLD) || (peer == NATIVE_ARCADE_RACE_DRIVE_HOLD)); spins++)
		{
			host = (host == RACE_HOLD) ? HostHold(0u, 0) : host;
			peer = (peer == NATIVE_ARCADE_RACE_DRIVE_HOLD) ? PeerHold(0u, 0) : peer;
		}
		CHECK(peer == NATIVE_ARCADE_RACE_DRIVE_GO);
	}
	CHECK(host == RACE_END);
	CHECK(g_driveBad == 0);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME);
	CHECK(state.lingerTicksLeft == 0u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.endKind == NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME);
	CHECK(state.begun == 1u);
	CHECK(state.lingerTicksLeft == 0u);
	(void)DrainPeerBundles(0u);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);
	CHECK(CheckDriveReset() == 0);
	CHECK(HostTicksSendNothing() == 0);

	/* Race G: the linger stops on RESULTS when the session leaves RUNNING.
	 * The host finishes on race tick 8, and its first linger tick on RESULTS
	 * resends its window. The peer, still racing with a state that differs
	 * from race tick 8, steps 8 and 9: its bundle of frame 11 carries its
	 * digest of 8, which the host recorded before its finish, so the host's
	 * next Tick drains it into a DIVERGED session. That Tick's linger tick
	 * stops the linger with 14 ticks left, the flow still on RESULTS, and the
	 * drive is re-initialized; nothing more is sent. */
	CHECK(RepairToRace(UINT64_C(0x2E5EA)) == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(8u) == 0);
	CHECK(HostFinishOnRacing() == 0);
	(void)DrainPeerBundles(0u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(HostEndReason() == (uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED);
	CHECK(DrainPeerBundles(DRIVE_LINGER_WINDOW) == DRIVE_LINGER_WINDOW);
	CHECK(GetDriveState(&state) == 0);
	CHECK(state.lingerTicksLeft == NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS - 1u);
	g_peerKnob = 3u;
	g_peerKnobFrom = 8u;
	for (tick = 0u; tick < 2u; tick++)
	{
		CHECK(NativeArcadeNetplay_Tick(&g_peer, 0u, 0u) == NATIVE_ARCADE_FLOW_ACTION_NONE);
		CHECK(PeerScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(PeerSpin(PeerStep(0u)) == NATIVE_ARCADE_RACE_DRIVE_GO);
	}
	CHECK(g_driveBad == 0);
	CHECK(g_peerNext == 10u);
	(void)DrainPeerBundles(0u);
	CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == ACT_NONE);
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(CheckDriveReset() == 0);
	CHECK(HostTicksSendNothing() == 0);
	NativeArcadeLinkHost_RaceEnd();
	CHECK(g_pacing == 0);
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(HostScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);

	/* Race H: Shutdown, then a new Configure. */
	NativeArcadeNetplay_Shutdown(&g_peer);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_DRIVE_HOST_PORT, TEST_DRIVE_PEER_PORT, UINT64_C(0x2E5E8)) == 1);
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(DrivePairToRace() == 0);
	CHECK(BeginRaceDrives() == 0);
	CHECK(RoundsBoth(3u) == 0);
	NativeArcadeLinkHost_Shutdown();
	CHECK(g_pacing == 0);
	CHECK(NativeArcadeLinkHost_GetDriveState(&state) == 0);
	CHECK(NativeArcadeLinkHost_InternalDriveFailureReports() == 0u);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(CheckDriveReset() == 0);
	CHECK(CheckDriveRefused() == 0);

	StopDriveRace();
	CHECK(CheckInert() == 0);
	return 0;
}

int main(void)
{
	CHECK(TestInertBeforeConfigure() == 0);
	CHECK(TestNullAndDisabled() == 0);
	CHECK(TestPreviews() == 0);
	CHECK(TestLinkRejectsBadIdentity() == 0);
	CHECK(TestLinkLifecycle() == 0);
	CHECK(TestSecondConfigureReplaces() == 0);
	CHECK(TestSelectPreviews() == 0);
	CHECK(TestMixSelectEntropy() == 0);
	CHECK(TestSelectEntropyEpochs() == 0);
	CHECK(TestLinkSelectAndAgreedMatch() == 0);
	CHECK(TestLinkLocalMenuEvent() == 0);
	CHECK(TestLinkRaceFailureAndRacingQuery() == 0);
	CHECK(TestRacePacing() == 0);
	CHECK(TestDriveStepHoldEnd() == 0);
	CHECK(TestDriveHoldLaunchLinger() == 0);
	CHECK(TestDriveTakeClassification() == 0);
	CHECK(TestDriveParkedDigestMismatch() == 0);
	CHECK(TestDriveRefusalsAndResets() == 0);
	puts("native_arcade_link_host_test: passed");
	return 0;
}
