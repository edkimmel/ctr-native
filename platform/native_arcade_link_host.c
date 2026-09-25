#include "platform/native_arcade_link_host.h"

#include <platform.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host_internal.h"
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

/* The host select view mirrors the adapter's field for field. */
_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS,
	"the host select view holds exactly the adapter's humans");
_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS,
	"the host select view holds exactly the adapter's bots");
_Static_assert(sizeof(struct NativeArcadeLinkHostSelectView) == sizeof(struct NativeArcadeNetplaySelectView),
	"the host select view has the adapter's layout");
_Static_assert(sizeof(struct NativeArcadeLinkHostSelectHumanView) == sizeof(struct NativeArcadeNetplaySelectHumanView),
	"the host select human view has the adapter's layout");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,
	"the agreed match holds exactly the config's slots");

/* Field for field: every host select-view field sits at the adapter's
 * offset. (The agreed match has no adapter counterpart: it is filled from
 * the match config itself.) */
#define NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(hostStruct, netplayStruct, field) \
	_Static_assert(offsetof(struct hostStruct, field) == offsetof(struct netplayStruct, field), \
		#hostStruct "." #field " must sit at its adapter offset")

NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, present);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, characterID);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, trackID);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, lapCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, lockMask);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, currentItem);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, reserved);

NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, active);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humanCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, localHuman);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, currentItem);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, ticksLeft);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, status);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, resolved);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, trackID);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, lapCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, trackDrawn);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, lapsDrawn);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, characterReassignedMask);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, botCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humanCharacter);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, botCharacter);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, peerLockedCharacterMask);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, reserved);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humans);

/* The host names for the select view and agreed-match values equal the
 * module values they mirror. */
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER == (unsigned)NATIVE_MATCH_SELECT_ITEM_CHARACTER,
	"host select item CHARACTER");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK == (unsigned)NATIVE_MATCH_SELECT_ITEM_TRACK,
	"host select item TRACK");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS == (unsigned)NATIVE_MATCH_SELECT_ITEM_LAPS,
	"host select item LAPS");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE == (unsigned)NATIVE_MATCH_SELECT_ITEM_DONE,
	"host select item DONE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER == NATIVE_MATCH_SELECT_LOCK_CHARACTER,
	"host select lock CHARACTER");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK == NATIVE_MATCH_SELECT_LOCK_TRACK, "host select lock TRACK");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS == NATIVE_MATCH_SELECT_LOCK_LAPS, "host select lock LAPS");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING == (unsigned)NATIVE_MATCH_SELECT_STATUS_PICKING,
	"host select status PICKING");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING == (unsigned)NATIVE_MATCH_SELECT_STATUS_WAITING,
	"host select status WAITING");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_RESOLVED == (unsigned)NATIVE_MATCH_SELECT_STATUS_RESOLVED,
	"host select status RESOLVED");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED == (unsigned)NATIVE_MATCH_SELECT_STATUS_CONFIRMED,
	"host select status CONFIRMED");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_FAILED == (unsigned)NATIVE_MATCH_SELECT_STATUS_FAILED,
	"host select status FAILED");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_INACTIVE == (unsigned)NATIVE_MATCH_SLOT_ROLE_INACTIVE,
	"host role INACTIVE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_CAB1 == (unsigned)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, "host role CAB1");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_CAB2 == (unsigned)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, "host role CAB2");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_BOT == (unsigned)NATIVE_MATCH_SLOT_ROLE_BOT, "host role BOT");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_HUMANS == NATIVE_MATCH_SELECT_MAX_HUMANS, "host max humans");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_HUMANS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS,
	"host max humans is the adapter view's capacity");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_HUMANS == NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS,
	"host max humans is the host view's capacity");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_BOTS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS, "host max bots");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_BOTS == NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS,
	"host max bots is the host view's capacity");

/* The select entropy mix constant: 2^64 divided by the golden ratio. */
#define NATIVE_ARCADE_LINK_HOST_ENTROPY_STEP UINT64_C(0x9E3779B97F4A7C15)

/* Select previews: the opponent cursor moves one step every
 * PREVIEW_STEP_TICKS preview ticks, and the local countdown runs over
 * PREVIEW_ITEM_TICKS (the 20 s select item, OD-1), so a capture at a given
 * frame is deterministic. */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_STEP_TICKS 30u
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_ITEM_TICKS 600u
/* The preview picks: the local cabinet (human 0) is CRASH on CRASH_COVE,
 * the opponent (human 1) is CORTEX voting TIGER_TEMPLE; both vote 3 laps. */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_CHARACTER 0u    /* CRASH */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_CHARACTER 1u /* CORTEX */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_TRACK 3u        /* CRASH_COVE */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK 4u     /* TIGER_TEMPLE */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS 3u
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_BOT_COUNT 4u

static uint32_t g_mode = NATIVE_ARCADE_LINK_HOST_MODE_OFF;
static struct NativeArcadeLinkOptions g_options;
static struct NativeArcadeNetplay g_netplay;
static struct NativeArcadeNetplayConfig g_config;
/* Ticks spent on screen OFF in LINK mode; drives the attract blink. */
static uint32_t g_idleTicks;
/* Ticks since PREVIEW mode was configured. */
static uint32_t g_previewTicks;
/* Process-local host epoch: incremented by every LINK Configure and every
 * AbortToTitle, before the adapter is initialized. Shutdown never resets
 * it, so no two initializations in one process share an epoch. */
static uint64_t g_epoch;
/* 1 from a RaceBegin that turned the fixed VBlank pacing on until the
 * RaceEnd or Shutdown that turns it off again (LR-7). Host-local: it never
 * enters a saved state, a recording, or canonical state. */
static uint8_t g_racePacing;

uint64_t NativeArcadeLinkHost_MixSelectEntropy(uint64_t entropy, uint64_t epoch)
{
	return entropy ^ (epoch * NATIVE_ARCADE_LINK_HOST_ENTROPY_STEP);
}

/* A new epoch and the adapter entropy derived from it; called right before
 * every NativeArcadeNetplay_Init. */
static void NativeArcadeLinkHost_NextEpoch(uint64_t entropy)
{
	g_epoch += 1u;
	g_config.selectEntropy = NativeArcadeLinkHost_MixSelectEntropy(entropy, g_epoch);
}

uint64_t NativeArcadeLinkHost_InternalSelectEntropy(void)
{
	return (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) ? g_config.selectEntropy : 0u;
}

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

int NativeArcadeLinkHost_RaceBegin(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	Platform_SetFixedVBlankPacing(1);
	g_racePacing = 1u;
	return 1;
}

void NativeArcadeLinkHost_RaceEnd(void)
{
	if (g_racePacing == 0u)
	{
		return;
	}
	Platform_SetFixedVBlankPacing(0);
	g_racePacing = 0u;
}

void NativeArcadeLinkHost_Shutdown(void)
{
	/* Before a process exit (main.c, and its atexit registration in LINK
	 * mode), a linked race's fixed pacing is turned off. */
	if (g_racePacing != 0u)
	{
		Platform_SetFixedVBlankPacing(0);
		g_racePacing = 0u;
	}
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
	NativeArcadeLinkHost_NextEpoch(options->selectEntropy);
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

/*
 * Synthesizes the select view of one select preview (*view already zeroed):
 * two humans, the local cabinet human 0. The opponent's cursor steps through
 * its current item's table in the rules module's order, one step every
 * PREVIEW_STEP_TICKS, so it visibly moves yet every capture frame is
 * deterministic. The countdown runs while the local human is picking and is
 * 0 once it is done, as the link reports it.
 */
static void NativeArcadeLinkHost_PreviewSelectView(struct NativeArcadeLinkHostView *view)
{
	struct NativeArcadeLinkHostSelectView *sel = &view->select;
	struct NativeArcadeLinkHostSelectHumanView *local = &sel->humans[0];
	struct NativeArcadeLinkHostSelectHumanView *opponent = &sel->humans[1];
	uint32_t step = g_previewTicks / NATIVE_ARCADE_LINK_HOST_PREVIEW_STEP_TICKS;
	uint32_t racer;

	view->screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT;
	sel->active = 1u;
	sel->humanCount = 2u;
	sel->localHuman = 0u;
	sel->status = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING;
	sel->ticksLeft = NATIVE_ARCADE_LINK_HOST_PREVIEW_ITEM_TICKS - (g_previewTicks % NATIVE_ARCADE_LINK_HOST_PREVIEW_ITEM_TICKS);

	/* Both start on the fixture cursors: their own character, CRASH_COVE,
	 * and 3 laps. */
	local->present = 1u;
	local->characterID = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_CHARACTER;
	local->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_TRACK;
	local->lapCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS;
	opponent->present = 1u;
	opponent->characterID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_CHARACTER;
	opponent->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_TRACK;
	opponent->lapCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS;

	switch (g_options.preview)
	{
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER:
		local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER;
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER;
		opponent->characterID = NativeMatchSelect_CharacterAt(step % NATIVE_MATCH_SELECT_CHARACTER_COUNT);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK:
		local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK;
		local->lockMask = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER;
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK;
		opponent->lockMask = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER;
		opponent->trackID = NativeMatchSelect_TrackAt(step % NATIVE_MATCH_SELECT_TRACK_COUNT);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT:
		if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS)
		{
			local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS;
			local->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK);
		}
		else
		{
			local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
			local->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK |
				NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS);
			sel->status = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING;
			sel->ticksLeft = 0u;
		}
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS;
		opponent->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK);
		opponent->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK;
		opponent->lapCount = NativeMatchSelect_LapOptionAt(step % NATIVE_MATCH_SELECT_LAP_OPTION_COUNT);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT:
	default:
		/* Both done; the two track votes differ, so the track is a draw
		 * (here TIGER_TEMPLE), and the laps agree. The bots are the first
		 * retail 2P AI set holding neither CRASH nor CORTEX: set 0. */
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT;
		local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
		local->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK |
			NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS);
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
		opponent->lockMask = local->lockMask;
		opponent->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK;
		sel->status = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED;
		sel->ticksLeft = 0u;
		sel->resolved = 1u;
		sel->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK;
		sel->trackDrawn = 1u;
		sel->lapCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS;
		sel->lapsDrawn = 0u;
		sel->characterReassignedMask = 0u;
		sel->humanCharacter[0] = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_CHARACTER;
		sel->humanCharacter[1] = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_CHARACTER;
		sel->botCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_BOT_COUNT;
		for (racer = 0u; racer < NATIVE_ARCADE_LINK_HOST_PREVIEW_BOT_COUNT; racer++)
		{
			sel->botCharacter[racer] = NativeMatchSelect_AiSetRacer(0u, racer);
		}
		break;
	}
	sel->currentItem = local->currentItem;
	if ((opponent->lockMask & NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER) != 0u)
	{
		sel->peerLockedCharacterMask = (uint16_t)(1u << opponent->characterID);
	}
}

/* Synthesizes the view of one preview screen. *view is already zeroed. A
 * preview consumes no input, so localMenuEvent is always NONE. */
static void NativeArcadeLinkHost_PreviewView(struct NativeArcadeLinkHostView *view)
{
	view->ticksInScreen = g_previewTicks;
	view->localCab = 1u;
	view->localMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE;
	switch (g_options.preview)
	{
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT:
		NativeArcadeLinkHost_PreviewSelectView(view);
		break;
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

/* LINK: the adapter's select view, field for field. */
static void NativeArcadeLinkHost_CopySelectView(const struct NativeArcadeNetplaySelectView *from,
	struct NativeArcadeLinkHostSelectView *to)
{
	uint32_t i;

	to->active = from->active;
	to->humanCount = from->humanCount;
	to->localHuman = from->localHuman;
	to->currentItem = from->currentItem;
	to->ticksLeft = from->ticksLeft;
	to->status = from->status;
	to->resolved = from->resolved;
	to->trackID = from->trackID;
	to->lapCount = from->lapCount;
	to->trackDrawn = from->trackDrawn;
	to->lapsDrawn = from->lapsDrawn;
	to->characterReassignedMask = from->characterReassignedMask;
	to->botCount = from->botCount;
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		to->humanCharacter[i] = from->humanCharacter[i];
	}
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS; i++)
	{
		to->botCharacter[i] = from->botCharacter[i];
	}
	to->peerLockedCharacterMask = from->peerLockedCharacterMask;
	to->reserved[0] = 0u;
	to->reserved[1] = 0u;
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		to->humans[i].present = from->humans[i].present;
		to->humans[i].characterID = from->humans[i].characterID;
		to->humans[i].trackID = from->humans[i].trackID;
		to->humans[i].lapCount = from->humans[i].lapCount;
		to->humans[i].lockMask = from->humans[i].lockMask;
		to->humans[i].currentItem = from->humans[i].currentItem;
		to->humans[i].reserved[0] = 0u;
		to->humans[i].reserved[1] = 0u;
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
	view->localCab = (uint8_t)((g_config.localRole == (uint8_t)NATIVE_ARCADE_LINK_HOST_ROLE_CAB2) ? 2u : 1u);
	view->rowsEnabled = (uint8_t)(((netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS) &&
									  (netplayView.menuArmed != 0u) &&
									  (netplayView.ticksInScreen > g_config.timings.resultsDwellTicks))
			? 1u
			: 0u);
	view->attract = (uint8_t)((netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1u : 0u);
	view->localMenuEvent = netplayView.localMenuEvent;
	NativeArcadeLinkHost_CopySelectView(&netplayView.select, &view->select);
	return 1;
}

int NativeArcadeLinkHost_GetAgreedMatch(struct NativeArcadeLinkHostMatch *out)
{
	const struct NativeMatchConfigV1 *agreed;
	uint32_t slot;

	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	agreed = NativeArcadeNetplay_AgreedConfig(&g_netplay);
	if (agreed == NULL)
	{
		return 0;
	}
	out->trackID = agreed->trackID;
	out->lapCount = agreed->lapCount;
	out->masterSeed = agreed->masterSeed;
	for (slot = 0u; slot < NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS; slot++)
	{
		out->slotRole[slot] = agreed->slots[slot].role;
		out->slotCharacter[slot] = agreed->slots[slot].characterID;
	}
	return 1;
}

int NativeArcadeLinkHost_GetAgreedConfig(struct NativeMatchConfigV1 *out)
{
	const struct NativeMatchConfigV1 *agreed;

	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	agreed = NativeArcadeNetplay_AgreedConfig(&g_netplay);
	if (agreed == NULL)
	{
		return 0;
	}
	/* The exact bytes the link agreed, padding included. */
	memcpy(out, agreed, sizeof(*out));
	return 1;
}

int NativeArcadeLinkHost_ReportRaceFailure(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	return NativeArcadeNetplay_ReportLocalRaceFailure(&g_netplay);
}

int NativeArcadeLinkHost_TakeRaceEnd(struct NativeArcadeLinkHostRaceEnd *out)
{
	struct NativeArcadeNetplayRaceEnd raceEnd;

	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	if (!NativeArcadeNetplay_TakeRaceEnd(&g_netplay, &raceEnd))
	{
		return 0;
	}
	out->raceNumber = raceEnd.raceNumber;
	out->endReason = raceEnd.endReason;
	out->foreignBundleDrops = raceEnd.foreignBundleDrops;
	return 1;
}

uint8_t NativeArcadeLinkHost_Racing(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0u;
	}
	return (uint8_t)((NativeArcadeLinkHost_LinkScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING) ? 1u : 0u);
}

void NativeArcadeLinkHost_AbortToTitle(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return;
	}
	NativeArcadeNetplay_Shutdown(&g_netplay);
	/* Init restarts the adapter's select count, so a new epoch keeps the
	 * next select's nonce from repeating an earlier one. */
	NativeArcadeLinkHost_NextEpoch(g_options.selectEntropy);
	if (!NativeArcadeNetplay_Init(&g_netplay, &g_config))
	{
		/* Defensive only: the stored config was accepted by Configure. */
		NativeArcadeLinkHost_Shutdown();
		return;
	}
	g_idleTicks = 0u;
}
