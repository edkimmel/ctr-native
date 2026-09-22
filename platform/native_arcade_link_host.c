#include "platform/native_arcade_link_host.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_arcade_netplay.h"
#include "platform/native_identity.h"

/*
 * Arcade-link host glue (docs/GAME_LOOP_UI_MILESTONE.md section 2.6). Host
 * glue, like platform/native_platform.c, so the singleton state lives in
 * file-scope statics. The adapter is large (it owns a lobby, peer link, and
 * session), so it lives in static storage rather than on any stack.
 */

static uint32_t g_mode = NATIVE_ARCADE_LINK_HOST_MODE_OFF;
static struct NativeArcadeLinkOptions g_options;
static struct NativeArcadeNetplay g_netplay;
static struct NativeArcadeNetplayConfig g_config;
/* Ticks spent on screen OFF in LINK mode; drives the attract blink. */
static uint32_t g_idleTicks;
/* Ticks since PREVIEW mode was configured. */
static uint32_t g_previewTicks;

static uint32_t NativeArcadeLinkHost_SaturatingIncrement(uint32_t value)
{
	return (value == UINT32_MAX) ? value : (value + 1u);
}

static uint32_t NativeArcadeLinkHost_LinkScreen(void)
{
	struct NativeArcadeNetplayView view;

	if (!NativeArcadeNetplay_GetView(&g_netplay, &view))
	{
		return NATIVE_ARCADE_FLOW_SCREEN_OFF;
	}
	return view.screen;
}

void NativeArcadeLinkHost_Shutdown(void)
{
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		NativeArcadeNetplay_Shutdown(&g_netplay);
	}
	memset(&g_options, 0, sizeof(g_options));
	memset(&g_netplay, 0, sizeof(g_netplay));
	memset(&g_config, 0, sizeof(g_config));
	g_idleTicks = 0u;
	g_previewTicks = 0u;
	g_mode = NATIVE_ARCADE_LINK_HOST_MODE_OFF;
}

int NativeArcadeLinkHost_Configure(const struct NativeArcadeLinkOptions *options,
	const struct NativeIdentityV1 *identity)
{
	struct NativeMatchConfigV1 fixture;
	uint32_t i;

	NativeArcadeLinkHost_Shutdown();
	if (options == NULL)
	{
		return 0;
	}
	if (options->enabled == 0u)
	{
		if (options->preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE)
		{
			return 1;
		}
		g_options = *options;
		g_previewTicks = 0u;
		g_mode = NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW;
		return 1;
	}

	if (identity == NULL)
	{
		return 0;
	}
	if (!NativeArcadeLinkFixture_Build(identity, &fixture))
	{
		return 0;
	}
	/* Bounds the copy below; Init applies the adapter's own candidate rules. */
	if ((options->peerCount > NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS) ||
		(options->peerCount > (uint32_t)(sizeof(g_config.candidates) / sizeof(g_config.candidates[0]))))
	{
		return 0;
	}
	NativeArcadeNetplay_DefaultConfig(&g_config);
	g_config.fixture = fixture;
	for (i = 0u; i < options->peerCount; i++)
	{
		g_config.candidates[i].ipv4 = options->peers[i].ipv4;
		g_config.candidates[i].port = options->peers[i].port;
	}
	g_config.candidateCount = options->peerCount;
	g_config.localPort = options->localPort;
	g_config.localRole = options->localRole;
	if (!NativeArcadeNetplay_Init(&g_netplay, &g_config))
	{
		memset(&g_config, 0, sizeof(g_config));
		return 0;
	}
	g_options = *options;
	g_idleTicks = 0u;
	g_mode = NATIVE_ARCADE_LINK_HOST_MODE_LINK;
	return 1;
}

uint32_t NativeArcadeLinkHost_Mode(void)
{
	return g_mode;
}

int NativeArcadeLinkHost_ScreenActive(void)
{
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return (NativeArcadeLinkHost_LinkScreen() != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1 : 0;
	}
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW)
	{
		return 1;
	}
	return 0;
}

int NativeArcadeLinkHost_Enter(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	return (NativeArcadeNetplay_Enter(&g_netplay) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY) ? 1 : 0;
}

uint32_t NativeArcadeLinkHost_Tick(uint32_t heldMenuButtons, uint8_t raceFinished)
{
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW)
	{
		g_previewTicks = NativeArcadeLinkHost_SaturatingIncrement(g_previewTicks);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (NativeArcadeLinkHost_LinkScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF)
	{
		g_idleTicks = NativeArcadeLinkHost_SaturatingIncrement(g_idleTicks);
	}
	else
	{
		g_idleTicks = 0u;
	}
	return (uint32_t)NativeArcadeNetplay_Tick(&g_netplay, heldMenuButtons, raceFinished);
}

/* Synthesizes the view of one preview screen. *view is already zeroed. */
static void NativeArcadeLinkHost_PreviewView(struct NativeArcadeLinkHostView *view)
{
	view->ticksInScreen = g_previewTicks;
	view->localCab = 1u;
	switch (g_options.preview)
	{
	case NATIVE_ARCADE_LINK_PREVIEW_LOBBY_WAITING:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_LOBBY;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_WAITING;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_LOBBY_CONNECTING:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_LOBBY;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_CONNECTING;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_LOBBY_REJECTED:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_LOBBY;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_REJECTED;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_MATCH_FOUND:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_READY;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED:
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT:
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC:
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_LINK_ERROR:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_RESULTS;
		if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED)
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
		}
		else if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT)
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT;
		}
		else if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC)
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_DESYNC;
		}
		else
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_LINK_ERROR;
		}
		view->selectedRow = NATIVE_ARCADE_FLOW_ROW_REMATCH;
		view->rowsEnabled = 1u;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_CONNECTING;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_EXIT:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_EXIT;
		view->endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_EXIT_OPPONENT_LEFT:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_EXIT;
		view->endReason = NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_TITLE:
	default:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_OFF;
		view->attract = 1u;
		break;
	}
}

int NativeArcadeLinkHost_GetView(struct NativeArcadeLinkHostView *view)
{
	struct NativeArcadeNetplayView netplayView;

	if ((view == NULL) || (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_OFF))
	{
		return 0;
	}
	memset(view, 0, sizeof(*view));
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW)
	{
		NativeArcadeLinkHost_PreviewView(view);
		return 1;
	}
	if (!NativeArcadeNetplay_GetView(&g_netplay, &netplayView))
	{
		return 0;
	}
	view->screen = netplayView.screen;
	view->lobbyStatus = netplayView.lobbyStatus;
	view->endReason = netplayView.endReason;
	view->selectedRow = netplayView.selectedRow;
	view->ticksInScreen = (netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? g_idleTicks
																						 : netplayView.ticksInScreen;
	view->localCab = (uint8_t)((g_config.localRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) ? 2u : 1u);
	view->rowsEnabled = (uint8_t)(((netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS) &&
									  (netplayView.menuArmed != 0u) &&
									  (netplayView.ticksInScreen > g_config.timings.resultsDwellTicks))
			? 1u
			: 0u);
	view->attract = (uint8_t)((netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1u : 0u);
	return 1;
}

void NativeArcadeLinkHost_AbortToTitle(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return;
	}
	NativeArcadeNetplay_Shutdown(&g_netplay);
	if (!NativeArcadeNetplay_Init(&g_netplay, &g_config))
	{
		/* Defensive only: the stored config was accepted by Configure. */
		NativeArcadeLinkHost_Shutdown();
		return;
	}
	g_idleTicks = 0u;
}
