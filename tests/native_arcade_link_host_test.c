#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host_internal.h"
#include "platform/native_arcade_netplay.h"
#include "platform/native_match_select_rules.h"

#include "native_arcade_link_loopback_test_fixture.h"

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

/* Bounds every loop that waits for the loopback pair; generous, not tuned. */
#define PAIR_BUDGET 4000u

/* The select preview schedule (host: 30 ticks per opponent step, 600-tick
 * countdown). */
#define PREVIEW_STEP_TICKS 30u
#define PREVIEW_ITEM_TICKS 600u

/* Well under the default 150-tick per-candidate attempt budget. */
#define LOBBY_TICKS 5u

#define ACT_NONE ((uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE)

/* No agreed match: 0, and *out untouched. */
static int CheckNoAgreedMatch(void)
{
	struct NativeArcadeLinkHostMatch match;
	struct NativeArcadeLinkHostMatch sentinel;

	memset(&match, 0xA5, sizeof(match));
	sentinel = match;
	CHECK(NativeArcadeLinkHost_GetAgreedMatch(&match) == 0);
	CHECK(memcmp(&match, &sentinel, sizeof(match)) == 0);
	CHECK(NativeArcadeLinkHost_GetAgreedMatch(NULL) == 0);
	return 0;
}

/* Mode OFF and every call inert. */
static int CheckInert(void)
{
	struct NativeArcadeLinkHostView view;

	CHECK(CheckNoAgreedMatch() == 0);
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
	CHECK(view.reserved == 0u);
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
	CHECK(view.reserved == 0u);
	/* Neither the attract screen nor the lobby selects, and no race is
	 * agreed. */
	CHECK(CheckSelectZero(&view.select) == 0);
	CHECK(CheckNoAgreedMatch() == 0);
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
	/* LINK before any race: no agreed match. */
	CHECK(CheckNoAgreedMatch() == 0);
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
	CHECK(view.reserved == 0u);

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

	/* The peer locks CORTEX: the host sees the lock and greys CORTEX. */
	CHECK(NativeArcadeLinkLoopback_PressPair(&g_peer, 0u, NATIVE_ARCADE_MENU_BUTTON_CROSS) == 1);
	CHECK(NativeArcadeLinkLoopback_PressPair(&g_peer, 0u, 0u) == 1);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	CHECK(view.select.humans[1].lockMask == 1u);
	CHECK(view.select.humans[1].characterID == 1u);
	CHECK(view.select.humans[1].currentItem == 1u);
	CHECK(view.select.peerLockedCharacterMask == 0x0002u);
	CHECK(view.select.currentItem == 0u);

	/* Both confirm the rest on the fixture cursors. */
	CHECK(NativeArcadeLinkLoopback_PressPair(&g_peer, NATIVE_ARCADE_MENU_BUTTON_CROSS, NATIVE_ARCADE_MENU_BUTTON_CROSS) == 1);
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
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

	/* The game's abort: back to the attract screen, nothing agreed. */
	NativeArcadeLinkHost_AbortToTitle();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
	CHECK(CheckNoAgreedMatch() == 0);

	NativeArcadeNetplay_Shutdown(&g_peer);
	NativeArcadeLinkHost_Shutdown();
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
	puts("native_arcade_link_host_test: passed");
	return 0;
}
