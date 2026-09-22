#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_flow.h"

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

#define LOOPBACK_IPV4 UINT32_C(0x7F000001)

/* Well under the default 150-tick per-candidate attempt budget. */
#define LOBBY_TICKS 5u

#define ACT_NONE ((uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE)

static void TestIdentity(struct NativeIdentityV1 *identity)
{
	uint32_t i;

	for (i = 0u; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		identity->build[i] = (uint8_t)(i + 1u);
		identity->content[i] = (uint8_t)(0x80u + i);
	}
}

static void LinkOptions(struct NativeArcadeLinkOptions *options, uint8_t role, uint32_t localPort, uint32_t peerPort)
{
	NativeArcadeLinkOptions_SetDefaults(options);
	options->enabled = 1u;
	options->localRole = role;
	options->localPort = (uint16_t)localPort;
	options->peers[0].ipv4 = LOOPBACK_IPV4;
	options->peers[0].port = (uint16_t)peerPort;
	options->peerCount = 1u;
}

/* Mode OFF and every call inert. */
static int CheckInert(void)
{
	struct NativeArcadeLinkHostView view;

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

	TestIdentity(&identity);
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

static int CheckPreviewView(const struct PreviewCase *expected, uint32_t ticks)
{
	struct NativeArcadeLinkHostView view;

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

	LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_LINK_LOCAL_PORT, TEST_LINK_DEAD_PEER_PORT);
	CHECK(NativeArcadeLinkHost_Configure(&options, NULL) == 0);
	CHECK(CheckInert() == 0);

	memset(&identity, 0, sizeof(identity));
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 0);
	CHECK(CheckInert() == 0);

	/* A valid identity with options the adapter rejects (no port). */
	TestIdentity(&identity);
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
	CHECK(NativeArcadeLinkHost_ScreenActive() == 1);
	return 0;
}

static int TestLinkLifecycle(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	uint32_t i;

	TestIdentity(&identity);
	LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_LINK_LOCAL_PORT, TEST_LINK_DEAD_PEER_PORT);
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

	TestIdentity(&identity);
	LinkOptions(&linkOptions, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, TEST_REPLACE_LOCAL_PORT,
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
	LinkOptions(&linkOptions, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_REPLACE_SECOND_LOCAL_PORT,
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

int main(void)
{
	CHECK(TestInertBeforeConfigure() == 0);
	CHECK(TestNullAndDisabled() == 0);
	CHECK(TestPreviews() == 0);
	CHECK(TestLinkRejectsBadIdentity() == 0);
	CHECK(TestLinkLifecycle() == 0);
	CHECK(TestSecondConfigureReplaces() == 0);
	puts("native_arcade_link_host_test: passed");
	return 0;
}
