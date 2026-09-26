#include "MAIN/MainArcadeLinkLayout.h"
#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_link_host_internal.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_netplay.h"

#include "native_arcade_link_loopback_test_fixture.h"

#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Cross-seam test (MS-10b): every view the arcade-link host glue hands the
 * drawer passes the screen layout. The host's select view
 * (platform/native_arcade_link_host.c) and the layout's strict validation
 * (game/MAIN/MainArcadeLinkLayout.c) are each unit-tested alone; here the
 * two meet on the drawer's exact path (MainArcadeLink_BuildAndDraw):
 * NativeArcadeLinkHost_GetView, MainArcadeLinkLayout_InputFromHostView,
 * MainArcadeLinkLayout_Build. Drift between them would show in the game as
 * a blank screen with the retail menu box hidden.
 *
 * Covered: every tick 0..PREVIEW_LAST_TICK of each of the 21 previews (the
 * four solo previews of SOLO-S3 among them; among the ticks 0, 29, 30, and
 * 1320: the first frame, both sides of an
 * opponent-cursor step, and past two countdown wraps), and every tick of a
 * live loopback LINK run, the host as cabinet 1 against a test-owned
 * adapter as cabinet 2, from the attract screen through Enter, the lobby,
 * MATCH_FOUND, SELECT on each of the four select items, and SELECT_RESULT
 * to the START_RACE tick (RACING, an empty draw list). The host exposes no
 * select-timing option, so the live run uses the production timings; its
 * character item is left to auto-lock, which walks its whole countdown.
 * Also every tick of a solo run (docs/SOLO_CAB_MILESTONE.md SOLO-S2): the
 * LOBBY's solo offer, the one-human SELECT, SELECT_RESULT, the
 * START_SOLO_RACE tick, and the solo RESULTS screen.
 *
 * Known exception, by design and not constructed here: on the single tick
 * where the link's select session fails to start (the session rejects the
 * human count or the local human, docs/MATCH_SELECT_MILESTONE.md section
 * 2.6), the host reports screen SELECT with select.active 0, Build rejects
 * that input, and the drawer draws nothing for that one tick; the flow
 * reads the select as FAILED on its next tick and leaves SELECT for the
 * link-error RESULTS screen.
 *
 * Link ports: the fixed loopback band 48520-48539, distinct from every
 * other test file's own band (tests/native_arcade_link_host_test.c uses
 * 48500-48519).
 */
#define TEST_VIEW_HOST_PORT 48520u
#define TEST_VIEW_PEER_PORT 48521u
/* The solo run: nobody listens on the peer port. */
#define TEST_SOLO_HOST_PORT 48522u
#define TEST_SOLO_DEAD_PEER_PORT 48523u

#define PREVIEW_FIRST ((uint32_t)NATIVE_ARCADE_LINK_PREVIEW_TITLE)
#define PREVIEW_LAST ((uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_SOLO_ERROR)
#define PREVIEW_LAST_TICK 1320u

/* Bounds every loop that waits for the loopback pair; generous, not tuned. */
#define PAIR_BUDGET 4000u
/* Released ticks the host spends DONE while the peer is still voting. */
#define DONE_WAIT_TICKS 10u

#define BUTTON_CROSS ((uint32_t)NATIVE_ARCADE_MENU_BUTTON_CROSS)
#define ACT_START_RACE ((uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)
#define ACT_RETURN_TO_TITLE ((uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE)

/* The platform's fixed VBlank pacing switch (include/platform.h), which the
 * host glue links against (docs/LOCKSTEP_RACE_MILESTONE.md LR-7). This test
 * begins no race, so the host never calls it; a stub keeps the link whole.
 * tests/native_arcade_link_host_test.c tests the switch itself. */
void Platform_SetFixedVBlankPacing(int enabled)
{
	(void)enabled;
}

static int IsSelectScreen(uint32_t screen)
{
	return (screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) ||
	       (screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT);
}

/*
 * The drawer's path for the host's current view. The mapping runs twice,
 * into an input pre-filled with 0x00 and one pre-filled with 0xFF; the two
 * results must be byte-identical (the input has no padding, so the mapping
 * must have written every byte) and both must build. Screens that draw
 * (all but RACING, and OFF without the attract layout) must give a nonempty
 * draw list. Fills *out with the view.
 */
static int CheckViewBuilds(struct NativeArcadeLinkHostView *out)
{
	struct NativeArcadeLinkHostView view;
	struct MainArcadeLinkLayoutInput zeroFilled;
	struct MainArcadeLinkLayoutInput oneFilled;
	struct MainArcadeLinkLayout layout;
	struct MainArcadeLinkLayout layoutFromOnes;
	int draws;
	uint32_t item;
	uint32_t panels;

	memset(&view, 0xA5, sizeof(view));
	CHECK(NativeArcadeLinkHost_GetView(&view) == 1);
	memset(&zeroFilled, 0x00, sizeof(zeroFilled));
	memset(&oneFilled, 0xFF, sizeof(oneFilled));
	CHECK(MainArcadeLinkLayout_InputFromHostView(&view, &zeroFilled) == 1);
	CHECK(MainArcadeLinkLayout_InputFromHostView(&view, &oneFilled) == 1);
	CHECK(MainArcadeLinkLayout_Build(&zeroFilled, &layout) == 1);
	CHECK(MainArcadeLinkLayout_Build(&oneFilled, &layoutFromOnes) == 1);
	CHECK(memcmp(&zeroFilled, &oneFilled, sizeof(zeroFilled)) == 0);
	CHECK(memcmp(&layout, &layoutFromOnes, sizeof(layout)) == 0);

	CHECK(layout.count <= MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS);
	draws = (view.screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING) &&
	        ((view.screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) || (view.attract == 1u));
	if (draws)
	{
		CHECK(layout.count > 0u);
		/* Exactly one panel, and it is the last item. */
		panels = 0u;
		for (item = 0u; item < layout.count; item++)
		{
			if (layout.items[item].kind == (uint8_t)MAIN_ARCADE_LINK_ITEM_PANEL)
			{
				panels++;
			}
		}
		CHECK(panels == 1u);
		CHECK(layout.items[layout.count - 1u].kind == (uint8_t)MAIN_ARCADE_LINK_ITEM_PANEL);
	}
	else
	{
		CHECK(layout.count == 0u);
	}
	/* A select screen passes only with a live select behind it. */
	if (IsSelectScreen(view.screen))
	{
		CHECK(view.select.active == 1u);
	}
	*out = view;
	return 0;
}

/* (a) Every tick 0..PREVIEW_LAST_TICK of each of the 21 previews. */
static int TestEveryPreviewBuilds(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeArcadeLinkHostView view;
	uint32_t selectScreens = 0u;
	uint32_t soloViews = 0u;
	uint32_t preview;
	uint32_t tick;

	CHECK(PREVIEW_LAST - PREVIEW_FIRST + 1u == 21u);
	for (preview = PREVIEW_FIRST; preview <= PREVIEW_LAST; preview++)
	{
		NativeArcadeLinkOptions_SetDefaults(&options);
		options.preview = preview;
		CHECK(NativeArcadeLinkHost_Configure(&options, NULL) == 1);
		CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW);
		for (tick = 0u; tick <= PREVIEW_LAST_TICK; tick++)
		{
			if (tick != 0u)
			{
				CHECK(NativeArcadeLinkHost_Tick(BUTTON_CROSS, 0u) == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE);
			}
			if (CheckViewBuilds(&view) != 0)
			{
				fprintf(stderr, "preview %u, tick %u\n", (unsigned)preview, (unsigned)tick);
				return 1;
			}
			CHECK(view.ticksInScreen == tick);
		}
		if (IsSelectScreen(view.screen))
		{
			selectScreens++;
		}
		if ((view.solo != 0u) || (view.soloOffered != 0u))
		{
			soloViews++;
		}
	}
	/* The five select previews and select-solo reached the select
	 * validation; the four solo previews reached the solo validation. */
	CHECK(selectScreens == 6u);
	CHECK(soloViews == 4u);
	NativeArcadeLinkHost_Shutdown();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF);
	return 0;
}

/* (b) The live loopback run. */

static struct NativeArcadeNetplay g_peer;

/* Ticks every build check of the live run saw, by host screen and, on
 * SELECT, by the local current item. */
#define SCREEN_COUNT ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT + 1u)
struct LiveCoverage
{
	uint32_t ticks;
	uint32_t screens[SCREEN_COUNT];
	uint32_t selectItem[4];
	uint32_t hostStarted;
};

static struct LiveCoverage g_coverage;

/* The build check on the host's current view, counted. */
static int LiveCheck(struct NativeArcadeLinkHostView *view)
{
	struct NativeArcadeLinkHostView failed;

	if (CheckViewBuilds(view) != 0)
	{
		memset(&failed, 0, sizeof(failed));
		(void)NativeArcadeLinkHost_GetView(&failed);
		fprintf(stderr, "live check %u, screen %u, select item %u\n", (unsigned)g_coverage.ticks, (unsigned)failed.screen,
			(unsigned)failed.select.currentItem);
		return 1;
	}
	g_coverage.ticks++;
	CHECK(view->screen < SCREEN_COUNT);
	g_coverage.screens[view->screen]++;
	if (view->screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT)
	{
		CHECK(view->select.currentItem < 4u);
		g_coverage.selectItem[view->select.currentItem]++;
	}
	return 0;
}

/* One pair tick, then the build check on the host's new view. Fails on
 * RETURN_TO_TITLE from either side, or a second START_RACE. */
static int LiveStep(uint32_t heldHost, uint32_t heldPeer, struct NativeArcadeLinkHostView *view)
{
	uint32_t hostAction;
	uint32_t peerAction;

	NativeArcadeLinkLoopback_TickPair(&g_peer, heldHost, heldPeer, &hostAction, &peerAction);
	CHECK(hostAction != ACT_RETURN_TO_TITLE);
	CHECK(peerAction != ACT_RETURN_TO_TITLE);
	if (hostAction == ACT_START_RACE)
	{
		CHECK(g_coverage.hostStarted == 0u);
		g_coverage.hostStarted = 1u;
	}
	return LiveCheck(view);
}

/* A press: a held tick, then a released tick, each checked. */
static int LivePress(uint32_t heldHost, uint32_t heldPeer, struct NativeArcadeLinkHostView *view)
{
	CHECK(LiveStep(heldHost, heldPeer, view) == 0);
	CHECK(LiveStep(0u, 0u, view) == 0);
	return 0;
}

static int PeerView(struct NativeArcadeNetplayView *peerView)
{
	CHECK(NativeArcadeNetplay_GetView(&g_peer, peerView) == 1);
	return 0;
}

static int TestLiveLinkBuilds(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeNetplayView peerView;
	uint32_t tick;
	uint32_t item;

	memset(&g_coverage, 0, sizeof(g_coverage));
	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_VIEW_HOST_PORT,
		TEST_VIEW_PEER_PORT);
	options.selectEntropy = UINT64_C(0x0123456789ABCDEF);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeLinkLoopback_PeerInit(&g_peer, &identity, TEST_VIEW_HOST_PORT, TEST_VIEW_PEER_PORT,
		UINT64_C(0xFEDCBA9876543210)) == 1);

	/* The attract screen before Enter. */
	CHECK(LiveCheck(&view) == 0);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(view.attract == 1u);

	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(NativeArcadeNetplay_Enter(&g_peer) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY);
	CHECK(LiveCheck(&view) == 0);

	/* Into SELECT on both sides, then one released tick to arm the menus. */
	for (tick = 0u; tick < PAIR_BUDGET; tick++)
	{
		CHECK(LiveStep(0u, 0u, &view) == 0);
		CHECK(PeerView(&peerView) == 0);
		if ((view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) &&
			(peerView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT))
		{
			break;
		}
	}
	CHECK(tick < PAIR_BUDGET);
	CHECK(LiveStep(0u, 0u, &view) == 0);

	/* The peer locks its character first, so the host shows it greyed. */
	CHECK(LivePress(0u, BUTTON_CROSS, &view) == 0);
	CHECK(LivePress(0u, 0u, &view) == 0);
	CHECK(view.select.peerLockedCharacterMask != 0u);

	/* The host's character item auto-locks: its whole countdown. */
	for (tick = 0u; (tick < PAIR_BUDGET) && (view.select.currentItem == 0u); tick++)
	{
		CHECK(LiveStep(0u, 0u, &view) == 0);
	}
	CHECK(tick < PAIR_BUDGET);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(view.select.currentItem == 1u);

	/* The host confirms track and laps, then waits DONE while the peer is
	 * still voting laps. */
	CHECK(PeerView(&peerView) == 0);
	CHECK(peerView.select.currentItem < 3u);
	CHECK(LivePress(BUTTON_CROSS, 0u, &view) == 0);
	CHECK(view.select.currentItem == 2u);
	CHECK(LivePress(BUTTON_CROSS, 0u, &view) == 0);
	CHECK(view.select.currentItem == 3u);
	for (tick = 0u; tick < DONE_WAIT_TICKS; tick++)
	{
		CHECK(LiveStep(0u, 0u, &view) == 0);
	}
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	CHECK(view.select.currentItem == 3u);

	/* The peer confirms whatever it has left. */
	for (item = 0u; item < 3u; item++)
	{
		CHECK(PeerView(&peerView) == 0);
		if (peerView.select.currentItem >= 3u)
		{
			break;
		}
		CHECK(LivePress(0u, BUTTON_CROSS, &view) == 0);
	}
	CHECK(PeerView(&peerView) == 0);
	CHECK((peerView.select.currentItem >= 3u) || (peerView.screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT));

	/* SELECT_RESULT, then on to START_RACE. */
	for (tick = 0u; (tick < PAIR_BUDGET) && (g_coverage.hostStarted == 0u); tick++)
	{
		CHECK(LiveStep(0u, 0u, &view) == 0);
	}
	CHECK(g_coverage.hostStarted == 1u);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);

	/* Coverage: the lobby, each select item, the result screen, and the
	 * START_RACE tick (RACING, an empty draw list) were all checked. */
	printf("main_arcade_link_view_layout_test: live run checked %u ticks; by screen off %u lobby %u match-found %u "
	       "racing %u select %u select-result %u; select items character %u track %u laps %u done %u\n",
		(unsigned)g_coverage.ticks, (unsigned)g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_OFF],
		(unsigned)g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_LOBBY],
		(unsigned)g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND],
		(unsigned)g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_RACING],
		(unsigned)g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_SELECT],
		(unsigned)g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT], (unsigned)g_coverage.selectItem[0],
		(unsigned)g_coverage.selectItem[1], (unsigned)g_coverage.selectItem[2], (unsigned)g_coverage.selectItem[3]);
	for (item = 0u; item < 4u; item++)
	{
		if (g_coverage.selectItem[item] == 0u)
		{
			fprintf(stderr, "no live tick checked on select item %u\n", (unsigned)item);
			return 1;
		}
	}
	/* The attract screen before Enter, the lobby on the Enter tick (a
	 * loopback pair is matched on the first tick after it), and the match
	 * found screen. */
	CHECK(g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_OFF] == 1u);
	CHECK(g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_LOBBY] > 0u);
	CHECK(g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND] > 0u);
	/* The character countdown ran out on its own. */
	CHECK(g_coverage.selectItem[0] >= 500u);
	CHECK(g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT] > 0u);
	/* Exactly the START_RACE tick. */
	CHECK(g_coverage.screens[NATIVE_ARCADE_FLOW_SCREEN_RACING] == 1u);

	NativeArcadeLinkHost_AbortToTitle();
	CHECK(CheckViewBuilds(&view) == 0);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);

	NativeArcadeNetplay_Shutdown(&g_peer);
	NativeArcadeLinkHost_Shutdown();
	CHECK(NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF);
	return 0;
}

/*
 * (c) The solo live run (docs/SOLO_CAB_MILESTONE.md SOLO-S2): the host with
 * solo enabled by its test-only setter, against a dead peer port. Every
 * tick from Enter through the solo offer in the LOBBY, the one-human SELECT
 * on each item, SELECT_RESULT, the START_SOLO_RACE tick, and the solo
 * RESULTS screen passes the build check. The body runs with the gate on; the
 * wrapper below turns it off whatever the body returns.
 */
static int RunSoloLiveBuilds(void)
{
	struct NativeArcadeLinkOptions options;
	struct NativeIdentityV1 identity;
	struct NativeArcadeLinkHostView view;
	uint32_t soloTicks = 0u;
	uint32_t offeredTicks = 0u;
	uint32_t selectItems[4] = {0u, 0u, 0u, 0u};
	uint32_t action = (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE;
	uint32_t tick;
	uint32_t item;

	NativeArcadeLinkLoopback_Identity(&identity);
	NativeArcadeLinkLoopback_LinkOptions(&options, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, TEST_SOLO_HOST_PORT,
		TEST_SOLO_DEAD_PEER_PORT);
	CHECK(NativeArcadeLinkHost_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeLinkHost_Enter() == 1);
	CHECK(CheckViewBuilds(&view) == 0);

	/* The LOBBY until the offer stands, then one tick of it shown. */
	for (tick = 0u; (tick < PAIR_BUDGET) && (view.soloOffered == 0u); tick++)
	{
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE);
		CHECK(CheckViewBuilds(&view) == 0);
		CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	}
	CHECK(view.soloOffered == 1u);
	offeredTicks++;
	CHECK(NativeArcadeLinkHost_Tick(BUTTON_CROSS, 0u) == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_BEGIN_SOLO_SELECT);

	/* Solo SELECT (arm, then confirm each item), SELECT_RESULT, and the
	 * START_SOLO_RACE tick. */
	for (tick = 0u; (tick < PAIR_BUDGET) && (action != (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_SOLO_RACE); tick++)
	{
		CHECK(CheckViewBuilds(&view) == 0);
		CHECK(view.solo == 1u);
		soloTicks++;
		if (view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT)
		{
			CHECK(view.select.currentItem < 4u);
			selectItems[view.select.currentItem]++;
		}
		action = NativeArcadeLinkHost_Tick(((tick % 2u) == 1u) ? BUTTON_CROSS : 0u, 0u);
	}
	CHECK(action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_SOLO_RACE);
	CHECK(CheckViewBuilds(&view) == 0);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING);
	CHECK(view.solo == 1u);
	for (item = 0u; item < 3u; item++)
	{
		CHECK(selectItems[item] > 0u);
	}

	/* The finish: solo RESULTS, its dwell and its rows. */
	CHECK(NativeArcadeLinkHost_Tick(0u, 1u) == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE);
	for (tick = 0u; tick < 200u; tick++)
	{
		CHECK(CheckViewBuilds(&view) == 0);
		CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
		CHECK(view.solo == 1u);
		soloTicks++;
		CHECK(NativeArcadeLinkHost_Tick(0u, 0u) == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE);
	}
	CHECK(view.rowsEnabled == 1u);
	printf("main_arcade_link_view_layout_test: solo run checked %u solo ticks, %u offered\n", (unsigned)soloTicks,
		(unsigned)offeredTicks);

	NativeArcadeLinkHost_AbortToTitle();
	CHECK(CheckViewBuilds(&view) == 0);
	CHECK(view.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF);
	NativeArcadeLinkHost_Shutdown();
	return 0;
}

static int TestSoloLiveBuilds(void)
{
	int failed;

	NativeArcadeLinkHost_InternalSetSoloEnabled(1u);
	failed = RunSoloLiveBuilds();
	/* Unconditional: a failed CHECK must not leave the gate on for later tests. */
	NativeArcadeLinkHost_InternalSetSoloEnabled(0u);
	return failed;
}

int main(void)
{
	CHECK(TestEveryPreviewBuilds() == 0);
	CHECK(TestLiveLinkBuilds() == 0);
	CHECK(TestSoloLiveBuilds() == 0);
	puts("main_arcade_link_view_layout_test: passed");
	return 0;
}
