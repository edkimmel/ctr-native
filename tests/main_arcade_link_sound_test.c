#include "MAIN/MainArcadeLinkSound.h"
#include "platform/native_arcade_flow.h"

#include <stdio.h>
#include <string.h>

/* Unit test for the arcade-link menu sound decision
 * (docs/GAME_LOOP_UI_MILESTONE.md section 3.1, SND-1..SND-11). */

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "fail %d: %s\n", __LINE__, #expression); return 1; } } while (0)

/* The mirrors match the flow values they copy (MainArcadeLink.c asserts the
 * same). */
_Static_assert(MAIN_ARCADE_LINK_SOUND_SCREEN_OFF == (unsigned)NATIVE_ARCADE_FLOW_SCREEN_OFF, "screen OFF");
_Static_assert(MAIN_ARCADE_LINK_SOUND_SCREEN_LOBBY == (unsigned)NATIVE_ARCADE_FLOW_SCREEN_LOBBY, "screen LOBBY");
_Static_assert(MAIN_ARCADE_LINK_SOUND_SCREEN_MATCH_FOUND == (unsigned)NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND, "screen MATCH_FOUND");
_Static_assert(MAIN_ARCADE_LINK_SOUND_SCREEN_RESULTS == (unsigned)NATIVE_ARCADE_FLOW_SCREEN_RESULTS, "screen RESULTS");
_Static_assert(MAIN_ARCADE_LINK_SOUND_SCREEN_SELECT == (unsigned)NATIVE_ARCADE_FLOW_SCREEN_SELECT, "screen SELECT");
_Static_assert(MAIN_ARCADE_LINK_SOUND_LOBBY_REJECTED == (unsigned)NATIVE_ARCADE_FLOW_LOBBY_REJECTED, "lobby REJECTED");
_Static_assert(MAIN_ARCADE_LINK_SOUND_END_PEER_TIMEOUT == (unsigned)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT, "end PEER_TIMEOUT");
_Static_assert(MAIN_ARCADE_LINK_SOUND_END_DESYNC == (unsigned)NATIVE_ARCADE_FLOW_END_DESYNC, "end DESYNC");
_Static_assert(MAIN_ARCADE_LINK_SOUND_END_LINK_ERROR == (unsigned)NATIVE_ARCADE_FLOW_END_LINK_ERROR, "end LINK_ERROR");
_Static_assert(MAIN_ARCADE_LINK_SOUND_END_OPPONENT_LEFT == (unsigned)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT, "end OPPONENT_LEFT");

#define NONE MAIN_ARCADE_LINK_SOUND_CUE_NONE
#define MOVE MAIN_ARCADE_LINK_SOUND_CUE_MOVE
#define CONFIRM MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM
#define BACK MAIN_ARCADE_LINK_SOUND_CUE_BACK
#define ERROR MAIN_ARCADE_LINK_SOUND_CUE_ERROR
/* Returned by Pair when the priming frame itself produced a cue. */
#define PRIME_FAILED 0xDEADu

#define EV_NONE ((uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE)
#define EV_PREV ((uint8_t)NATIVE_ARCADE_MENU_EVENT_PREV)
#define EV_NEXT ((uint8_t)NATIVE_ARCADE_MENU_EVENT_NEXT)
#define EV_CONFIRM ((uint8_t)NATIVE_ARCADE_MENU_EVENT_CONFIRM)
#define EV_BACK ((uint8_t)NATIVE_ARCADE_MENU_EVENT_BACK)

#define S_OFF ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF)
#define S_LOBBY ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_LOBBY)
#define S_MATCH_FOUND ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND)
#define S_RACING ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING)
#define S_RESULTS ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS)
#define S_REMATCH_WAIT ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT)
#define S_EXIT ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_EXIT)
#define S_SELECT ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT)
#define S_SELECT_RESULT ((uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT)

#define L_WAITING ((uint32_t)NATIVE_ARCADE_FLOW_LOBBY_WAITING)
#define L_CONNECTING ((uint32_t)NATIVE_ARCADE_FLOW_LOBBY_CONNECTING)
#define L_READY ((uint32_t)NATIVE_ARCADE_FLOW_LOBBY_READY)
#define L_REJECTED ((uint32_t)NATIVE_ARCADE_FLOW_LOBBY_REJECTED)
#define L_LOST ((uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST)

#define E_NONE ((uint32_t)NATIVE_ARCADE_FLOW_END_NONE)
#define E_FINISHED ((uint32_t)NATIVE_ARCADE_FLOW_END_FINISHED)
#define E_PEER_TIMEOUT ((uint32_t)NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT)
#define E_DESYNC ((uint32_t)NATIVE_ARCADE_FLOW_END_DESYNC)
#define E_LINK_ERROR ((uint32_t)NATIVE_ARCADE_FLOW_END_LINK_ERROR)
#define E_OPPONENT_LEFT ((uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT)

#define ITEM_CHARACTER ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER)
#define ITEM_TRACK ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK)
#define ITEM_LAPS ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS)
#define ITEM_DONE ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE)
#define LOCK_CHARACTER ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER)
#define LOCK_TRACK ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK)
#define LOCK_LAPS ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS)
#define ST_PICKING ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING)
#define ST_WAITING ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING)
#define ST_CONFIRMED ((uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED)

/* A LINK frame on one screen with no event and no select. */
static struct MainArcadeLinkSoundInput Frame(uint32_t screen, uint32_t lobbyStatus, uint32_t endReason)
{
	struct MainArcadeLinkSoundInput input;

	memset(&input, 0, sizeof(input));
	input.hostMode = (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK;
	input.local.screen = screen;
	input.local.lobbyStatus = lobbyStatus;
	input.local.endReason = endReason;
	return input;
}

/* A LINK SELECT frame: the local human picking the given item. */
static struct MainArcadeLinkSoundInput SelectFrame(uint8_t item, uint8_t lockMask, uint8_t status)
{
	struct MainArcadeLinkSoundInput input = Frame(S_SELECT, L_READY, E_NONE);

	input.local.selectActive = 1u;
	input.local.selectStatus = status;
	input.local.characterID = 0u;
	input.local.trackID = 3u;
	input.local.lapCount = 3u;
	input.local.lockMask = lockMask;
	input.local.currentItem = item;
	return input;
}

static struct MainArcadeLinkSoundInput WithEvent(struct MainArcadeLinkSoundInput input, uint8_t event)
{
	input.localMenuEvent = event;
	return input;
}

/* The cue for cur after prev, from a fresh state primed with prev (the
 * priming frame must itself be silent). */
static uint32_t Pair(struct MainArcadeLinkSoundInput prev, struct MainArcadeLinkSoundInput cur, uint8_t enterPressed)
{
	struct MainArcadeLinkSoundState state;

	MainArcadeLinkSound_Reset(&state);
	if (MainArcadeLinkSound_Decide(&state, &prev, 0u) != NONE)
	{
		return PRIME_FAILED;
	}
	return MainArcadeLinkSound_Decide(&state, &cur, enterPressed);
}

static int AllZero(const void *bytes, size_t size)
{
	const unsigned char *p = (const unsigned char *)bytes;

	for (size_t i = 0; i < size; i++)
	{
		if (p[i] != 0u)
		{
			return 0;
		}
	}
	return 1;
}

/* SND-1: the cue -> retail sound ID mapping. */
static int TestRetailIds(void)
{
	uint32_t id = 77u;

	CHECK(MainArcadeLinkSound_RetailId(MOVE, &id) == 1 && id == 0u);
	CHECK(MainArcadeLinkSound_RetailId(CONFIRM, &id) == 1 && id == 1u);
	CHECK(MainArcadeLinkSound_RetailId(BACK, &id) == 1 && id == 2u);
	CHECK(MainArcadeLinkSound_RetailId(ERROR, &id) == 1 && id == 5u);
	CHECK(MAIN_ARCADE_LINK_SOUND_ID_MOVE == 0u && MAIN_ARCADE_LINK_SOUND_ID_CONFIRM == 1u);
	CHECK(MAIN_ARCADE_LINK_SOUND_ID_BACK == 2u && MAIN_ARCADE_LINK_SOUND_ID_ERROR == 5u);

	id = 77u;
	CHECK(MainArcadeLinkSound_RetailId(NONE, &id) == 0 && id == 77u);
	CHECK(MainArcadeLinkSound_RetailId(5u, &id) == 0 && id == 77u);
	CHECK(MainArcadeLinkSound_RetailId(0xFFFFFFFFu, &id) == 0 && id == 77u);
	CHECK(MainArcadeLinkSound_RetailId(MOVE, NULL) == 0);
	return 0;
}

/* Reset, NULL handling, and SND-10 (first frame, PREVIEW, OFF). */
static int TestResetAndSilentModes(void)
{
	struct MainArcadeLinkSoundState state;
	struct MainArcadeLinkSoundState zero;
	struct MainArcadeLinkSoundInput prev = Frame(S_RESULTS, L_READY, E_FINISHED);
	struct MainArcadeLinkSoundInput cur = WithEvent(prev, EV_NEXT);
	struct MainArcadeLinkSoundInput preview;

	cur.local.selectedRow = 1u;

	memset(&zero, 0, sizeof(zero));
	memset(&state, 0xA5, sizeof(state));
	MainArcadeLinkSound_Reset(&state);
	CHECK(memcmp(&state, &zero, sizeof(state)) == 0);
	MainArcadeLinkSound_Reset(NULL);

	/* NULL state: NONE. NULL input: NONE and a reset. */
	CHECK(MainArcadeLinkSound_Decide(NULL, &cur, 1u) == NONE);
	CHECK(MainArcadeLinkSound_Decide(&state, &prev, 0u) == NONE);
	CHECK(state.valid == 1u);
	CHECK(MainArcadeLinkSound_Decide(&state, NULL, 0u) == NONE);
	CHECK(AllZero(&state, sizeof(state)));

	/* The first frame after a reset is silent, whatever it shows. */
	MainArcadeLinkSound_Reset(&state);
	CHECK(MainArcadeLinkSound_Decide(&state, &cur, 1u) == NONE);
	CHECK(state.valid == 1u);
	CHECK(memcmp(&state.previous, &cur.local, sizeof(cur.local)) == 0);
	{
		struct MainArcadeLinkSoundInput failure = Frame(S_RESULTS, L_WAITING, E_LINK_ERROR);
		struct MainArcadeLinkSoundInput rejected = Frame(S_LOBBY, L_REJECTED, E_NONE);

		MainArcadeLinkSound_Reset(&state);
		CHECK(MainArcadeLinkSound_Decide(&state, &failure, 0u) == NONE);
		MainArcadeLinkSound_Reset(&state);
		CHECK(MainArcadeLinkSound_Decide(&state, &rejected, 0u) == NONE);
		/* An attract entry on the first frame is silent too. */
		MainArcadeLinkSound_Reset(&state);
		failure = Frame(S_LOBBY, L_CONNECTING, E_NONE);
		CHECK(MainArcadeLinkSound_Decide(&state, &failure, 1u) == NONE);
	}

	/* A zero-initialized state behaves like a reset one. */
	memset(&state, 0, sizeof(state));
	CHECK(MainArcadeLinkSound_Decide(&state, &cur, 0u) == NONE);

	/* PREVIEW: silent, and the snapshot is dropped, so the next LINK frame
	 * is a first frame too. */
	MainArcadeLinkSound_Reset(&state);
	CHECK(MainArcadeLinkSound_Decide(&state, &prev, 0u) == NONE);
	preview = cur;
	preview.hostMode = (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW;
	CHECK(MainArcadeLinkSound_Decide(&state, &preview, 1u) == NONE);
	CHECK(AllZero(&state, sizeof(state)));
	CHECK(MainArcadeLinkSound_Decide(&state, &cur, 0u) == NONE);

	/* Every scripted preview screen change is silent. */
	{
		struct MainArcadeLinkSoundInput a = Frame(S_RESULTS, L_WAITING, E_FINISHED);
		struct MainArcadeLinkSoundInput b = Frame(S_RESULTS, L_WAITING, E_DESYNC);

		a.hostMode = (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW;
		b.hostMode = (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW;
		b.local.screen = S_SELECT;
		MainArcadeLinkSound_Reset(&state);
		CHECK(MainArcadeLinkSound_Decide(&state, &a, 0u) == NONE);
		CHECK(MainArcadeLinkSound_Decide(&state, &b, 0u) == NONE);
		b.local.screen = S_RESULTS;
		CHECK(MainArcadeLinkSound_Decide(&state, &b, 0u) == NONE);
	}

	/* OFF (and any unknown mode): silent and reset. */
	MainArcadeLinkSound_Reset(&state);
	CHECK(MainArcadeLinkSound_Decide(&state, &prev, 0u) == NONE);
	preview = cur;
	preview.hostMode = (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF;
	CHECK(MainArcadeLinkSound_Decide(&state, &preview, 1u) == NONE);
	CHECK(AllZero(&state, sizeof(state)));
	CHECK(MainArcadeLinkSound_Decide(&state, &prev, 0u) == NONE);
	preview.hostMode = 9u;
	CHECK(MainArcadeLinkSound_Decide(&state, &preview, 1u) == NONE);
	CHECK(AllZero(&state, sizeof(state)));
	return 0;
}

/* SND-3: MOVE only when the local focus changed. */
static int TestMove(void)
{
	struct MainArcadeLinkSoundInput prev = Frame(S_RESULTS, L_READY, E_FINISHED);
	struct MainArcadeLinkSoundInput cur;

	/* Results rows. */
	cur = WithEvent(prev, EV_NEXT);
	cur.local.selectedRow = 1u;
	CHECK(Pair(prev, cur, 0u) == MOVE);
	prev.local.selectedRow = 1u;
	cur = WithEvent(prev, EV_PREV);
	cur.local.selectedRow = 0u;
	CHECK(Pair(prev, cur, 0u) == MOVE);

	/* No row change (the results dwell) -> silent. */
	cur = WithEvent(prev, EV_NEXT);
	CHECK(Pair(prev, cur, 0u) == NONE);
	cur = WithEvent(prev, EV_PREV);
	CHECK(Pair(prev, cur, 0u) == NONE);

	/* PREV/NEXT on screens without focus -> silent. */
	prev = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_PREV), 0u) == NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_NEXT), 0u) == NONE);
	prev = Frame(S_MATCH_FOUND, L_READY, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_NEXT), 0u) == NONE);
	prev = Frame(S_SELECT_RESULT, L_READY, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_NEXT), 0u) == NONE);

	/* A row change with no local event -> silent. */
	prev = Frame(S_RESULTS, L_READY, E_FINISHED);
	cur = prev;
	cur.local.selectedRow = 1u;
	CHECK(Pair(prev, cur, 0u) == NONE);

	/* SELECT: the local cursor of the current item. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(prev, EV_NEXT);
	cur.local.characterID = 1u;
	CHECK(Pair(prev, cur, 0u) == MOVE);
	cur = WithEvent(prev, EV_PREV);
	cur.local.characterID = 7u;
	CHECK(Pair(prev, cur, 0u) == MOVE);
	prev = SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING);
	cur = WithEvent(prev, EV_NEXT);
	cur.local.trackID = 4u;
	CHECK(Pair(prev, cur, 0u) == MOVE);
	prev = SelectFrame(ITEM_LAPS, LOCK_CHARACTER | LOCK_TRACK, ST_PICKING);
	cur = WithEvent(prev, EV_PREV);
	cur.local.lapCount = 7u;
	CHECK(Pair(prev, cur, 0u) == MOVE);

	/* SELECT with no cursor change -> silent: nothing moved, a value of
	 * another item changed, or the local human is done. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	CHECK(Pair(prev, WithEvent(prev, EV_NEXT), 0u) == NONE);
	cur = WithEvent(prev, EV_NEXT);
	cur.local.trackID = 9u;
	CHECK(Pair(prev, cur, 0u) == NONE);
	prev = SelectFrame(ITEM_DONE, LOCK_CHARACTER | LOCK_TRACK | LOCK_LAPS, ST_WAITING);
	CHECK(Pair(prev, WithEvent(prev, EV_NEXT), 0u) == NONE);

	/* A cursor change on the frame the screen changes is not a move. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(prev, EV_NEXT);
	cur.local.characterID = 1u;
	cur.local.screen = S_SELECT_RESULT;
	CHECK(Pair(prev, cur, 0u) == NONE);
	return 0;
}

/* SND-4: CONFIRM when a local confirm took effect, and on attract entry. */
static int TestConfirm(void)
{
	struct MainArcadeLinkSoundInput prev;
	struct MainArcadeLinkSoundInput cur;

	/* Attract entry: enterPressed took screen OFF into the lobby. */
	prev = Frame(S_OFF, L_WAITING, E_NONE);
	cur = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	CHECK(Pair(prev, cur, 1u) == CONFIRM);
	/* The same entry without enterPressed is not a local cue (and entering
	 * the lobby is no other cue). */
	CHECK(Pair(prev, cur, 0u) == NONE);
	/* enterPressed that did not take effect -> silent. */
	CHECK(Pair(prev, prev, 1u) == NONE);

	/* RESULTS: confirming REMATCH or EXIT. */
	prev = Frame(S_RESULTS, L_READY, E_FINISHED);
	cur = WithEvent(Frame(S_REMATCH_WAIT, L_WAITING, E_FINISHED), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	prev.local.selectedRow = 1u;
	cur = WithEvent(Frame(S_EXIT, L_WAITING, E_FINISHED), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* Confirming EXIT on a LINK ERROR results screen carries the end reason
	 * onto EXIT: still the confirm, not an error. */
	prev = Frame(S_RESULTS, L_WAITING, E_LINK_ERROR);
	prev.local.selectedRow = 1u;
	cur = WithEvent(Frame(S_EXIT, L_WAITING, E_LINK_ERROR), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	prev.local.selectedRow = 0u;
	cur = WithEvent(Frame(S_REMATCH_WAIT, L_WAITING, E_LINK_ERROR), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);

	/* SELECT: the local lock gained a bit and the item advanced. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	prev = SelectFrame(ITEM_LAPS, LOCK_CHARACTER | LOCK_TRACK, ST_PICKING);
	cur = WithEvent(SelectFrame(ITEM_DONE, LOCK_CHARACTER | LOCK_TRACK | LOCK_LAPS, ST_WAITING), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* The last lock resolved the select on the same tick: SELECT_RESULT. */
	cur.local.screen = S_SELECT_RESULT;
	cur.local.selectStatus = ST_CONFIRMED;
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* Either signal alone counts. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(SelectFrame(ITEM_CHARACTER, LOCK_CHARACTER, ST_PICKING), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	cur = WithEvent(SelectFrame(ITEM_TRACK, 0u, ST_PICKING), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);

	/* LOBBY: a confirm on a lobby observed REJECTED retries it. */
	prev = Frame(S_LOBBY, L_REJECTED, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == CONFIRM);
	/* The retry's next frame (REJECTED -> CONNECTING) is silent. */
	CHECK(Pair(prev, Frame(S_LOBBY, L_CONNECTING, E_NONE), 0u) == NONE);
	return 0;
}

/* SND-5: ERROR for a refused pick on SELECT; other no-effect confirms are
 * silent. */
static int TestRefusedConfirm(void)
{
	struct MainArcadeLinkSoundInput prev;
	struct MainArcadeLinkSoundInput cur;

	/* The cursor character is peer-locked: the select refused the pick. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(prev, EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == ERROR);

	/* No-effect confirms elsewhere are silent. */
	prev = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	prev = Frame(S_LOBBY, L_WAITING, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	prev = Frame(S_EXIT, L_WAITING, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	/* RESULTS rows not yet enabled (the dwell): nothing changed. */
	prev = Frame(S_RESULTS, L_READY, E_FINISHED);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	prev = Frame(S_MATCH_FOUND, L_READY, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	prev = Frame(S_REMATCH_WAIT, L_CONNECTING, E_FINISHED);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	/* SELECT but done and waiting for the peer: silent. */
	prev = SelectFrame(ITEM_DONE, LOCK_CHARACTER | LOCK_TRACK | LOCK_LAPS, ST_WAITING);
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	/* SELECT_RESULT ignores events: silent. */
	prev = SelectFrame(ITEM_DONE, LOCK_CHARACTER | LOCK_TRACK | LOCK_LAPS, ST_CONFIRMED);
	prev.local.screen = S_SELECT_RESULT;
	CHECK(Pair(prev, WithEvent(prev, EV_CONFIRM), 0u) == NONE);
	return 0;
}

/* SND-6: BACK when a local back took effect. */
static int TestBack(void)
{
	struct MainArcadeLinkSoundInput prev;
	struct MainArcadeLinkSoundInput cur;

	prev = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	cur = WithEvent(Frame(S_EXIT, L_CONNECTING, E_NONE), EV_BACK);
	CHECK(Pair(prev, cur, 0u) == BACK);
	prev = Frame(S_REMATCH_WAIT, L_CONNECTING, E_FINISHED);
	cur = WithEvent(Frame(S_EXIT, L_CONNECTING, E_NONE), EV_BACK);
	CHECK(Pair(prev, cur, 0u) == BACK);

	/* RESULTS: BACK jumps the focus to EXIT. Already there -> silent. */
	prev = Frame(S_RESULTS, L_READY, E_FINISHED);
	cur = WithEvent(prev, EV_BACK);
	cur.local.selectedRow = 1u;
	CHECK(Pair(prev, cur, 0u) == BACK);
	prev.local.selectedRow = 1u;
	CHECK(Pair(prev, WithEvent(prev, EV_BACK), 0u) == NONE);

	/* SELECT ignores BACK (SEL-7): silent. */
	prev = SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING);
	CHECK(Pair(prev, WithEvent(prev, EV_BACK), 0u) == NONE);
	/* Were a lock or item ever taken back, it would be the back cue. */
	cur = WithEvent(SelectFrame(ITEM_TRACK, 0u, ST_PICKING), EV_BACK);
	CHECK(Pair(prev, cur, 0u) == BACK);
	cur = WithEvent(SelectFrame(ITEM_CHARACTER, LOCK_CHARACTER, ST_PICKING), EV_BACK);
	CHECK(Pair(prev, cur, 0u) == BACK);

	/* No effect elsewhere -> silent. */
	prev = Frame(S_MATCH_FOUND, L_READY, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_BACK), 0u) == NONE);
	prev = Frame(S_EXIT, L_WAITING, E_NONE);
	CHECK(Pair(prev, WithEvent(prev, EV_BACK), 0u) == NONE);
	return 0;
}

/* SND-7: the item timer's auto-lock plays CONFIRM. */
static int TestAutoLock(void)
{
	struct MainArcadeLinkSoundInput prev;
	struct MainArcadeLinkSoundInput cur;

	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* SEL-6: the auto-lock skipped a peer-held cursor character. */
	cur.local.characterID = 2u;
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* The last item's auto-lock that resolves on the same tick. */
	prev = SelectFrame(ITEM_LAPS, LOCK_CHARACTER | LOCK_TRACK, ST_PICKING);
	cur = SelectFrame(ITEM_DONE, LOCK_CHARACTER | LOCK_TRACK | LOCK_LAPS, ST_CONFIRMED);
	cur.local.screen = S_SELECT_RESULT;
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* A refused confirm on the tick the timer auto-locks elsewhere: the
	 * lock took effect, so it is the confirm, not the womp. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING), EV_CONFIRM);
	cur.local.characterID = 2u;
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* The first select frame (BEGIN_SELECT) gains nothing: silent. */
	prev = Frame(S_MATCH_FOUND, L_READY, E_NONE);
	cur = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	CHECK(Pair(prev, cur, 0u) == NONE);
	/* A lock seen without a select on the previous frame: silent. */
	prev = Frame(S_SELECT, L_READY, E_NONE);
	cur = SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING);
	CHECK(Pair(prev, cur, 0u) == NONE);
	return 0;
}

/* SND-2 and SND-8: the peer never produces a cue; only the local human's
 * entry reaches the decision. */
static int TestLocalOnly(void)
{
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkHostView peerMoved;
	struct MainArcadeLinkSoundInput a;
	struct MainArcadeLinkSoundInput b;
	struct MainArcadeLinkSoundState state;
	uint32_t local;

	for (local = 0u; local < 2u; local++)
	{
		const uint32_t peer = 1u - local;

		memset(&view, 0, sizeof(view));
		view.screen = S_SELECT;
		view.lobbyStatus = L_READY;
		view.endReason = E_NONE;
		view.selectedRow = 0u;
		view.localCab = (uint8_t)(local + 1u);
		view.select.active = 1u;
		view.select.humanCount = 2u;
		view.select.localHuman = (uint8_t)local;
		view.select.currentItem = ITEM_CHARACTER;
		view.select.status = ST_PICKING;
		view.select.humans[local].present = 1u;
		view.select.humans[local].characterID = 4u;
		view.select.humans[local].trackID = 6u;
		view.select.humans[local].lapCount = 5u;
		view.select.humans[local].lockMask = 0u;
		view.select.humans[local].currentItem = ITEM_CHARACTER;
		view.select.humans[peer].present = 1u;
		view.select.humans[peer].characterID = 1u;
		view.select.humans[peer].currentItem = ITEM_CHARACTER;

		CHECK(MainArcadeLinkSound_InputFromHostView(&view, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &a) == 1);
		CHECK(a.hostMode == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK);
		CHECK(a.local.screen == S_SELECT && a.local.lobbyStatus == L_READY && a.local.selectActive == 1u);
		CHECK(a.local.selectStatus == ST_PICKING);
		CHECK(a.local.characterID == 4u && a.local.trackID == 6u && a.local.lapCount == 5u);
		CHECK(a.local.lockMask == 0u && a.local.currentItem == ITEM_CHARACTER);

		/* The peer moves its cursor, locks a character (so the peer mask
		 * changes), advances, and resolves: the input is unchanged. */
		peerMoved = view;
		peerMoved.select.humans[peer].characterID = 2u;
		peerMoved.select.humans[peer].trackID = 9u;
		peerMoved.select.humans[peer].lapCount = 7u;
		peerMoved.select.humans[peer].lockMask = LOCK_CHARACTER | LOCK_TRACK;
		peerMoved.select.humans[peer].currentItem = ITEM_LAPS;
		peerMoved.select.peerLockedCharacterMask = (uint16_t)(1u << 2);
		peerMoved.select.humans[2].characterID = 3u;
		peerMoved.select.humans[3].lockMask = 7u;
		CHECK(MainArcadeLinkSound_InputFromHostView(&peerMoved, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &b) == 1);
		CHECK(memcmp(&a, &b, sizeof(a)) == 0);

		MainArcadeLinkSound_Reset(&state);
		CHECK(MainArcadeLinkSound_Decide(&state, &a, 0u) == NONE);
		CHECK(MainArcadeLinkSound_Decide(&state, &b, 0u) == NONE);
		/* peerLockedCharacterMask alone. */
		peerMoved = view;
		peerMoved.select.peerLockedCharacterMask = 0xFFu;
		CHECK(MainArcadeLinkSound_InputFromHostView(&peerMoved, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &b) == 1);
		CHECK(MainArcadeLinkSound_Decide(&state, &b, 0u) == NONE);
		/* The peer's lock (SND-8). */
		peerMoved = view;
		peerMoved.select.humans[peer].lockMask = LOCK_CHARACTER;
		peerMoved.select.humans[peer].currentItem = ITEM_TRACK;
		CHECK(MainArcadeLinkSound_InputFromHostView(&peerMoved, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &b) == 1);
		CHECK(MainArcadeLinkSound_Decide(&state, &b, 0u) == NONE);

		/* The local human's own move does reach it. */
		peerMoved = view;
		peerMoved.localMenuEvent = EV_NEXT;
		peerMoved.select.humans[local].characterID = 5u;
		CHECK(MainArcadeLinkSound_InputFromHostView(&peerMoved, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &b) == 1);
		CHECK(b.localMenuEvent == EV_NEXT);
		CHECK(MainArcadeLinkSound_Decide(&state, &b, 0u) == MOVE);
	}

	/* The local human's fields stay 0 with no active select, or with an
	 * out-of-range local index. */
	memset(&view, 0, sizeof(view));
	view.screen = S_RESULTS;
	view.selectedRow = 1u;
	view.select.humans[0].characterID = 3u;
	view.select.humans[0].lockMask = 1u;
	CHECK(MainArcadeLinkSound_InputFromHostView(&view, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &a) == 1);
	CHECK(a.local.selectedRow == 1u && a.local.selectActive == 0u && a.local.characterID == 0u);
	CHECK(a.local.lockMask == 0u);
	view.select.active = 1u;
	view.select.localHuman = (uint8_t)NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS;
	CHECK(MainArcadeLinkSound_InputFromHostView(&view, (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, &a) == 1);
	CHECK(a.local.selectActive == 0u && a.local.characterID == 0u);

	/* NULL handling: nothing is touched. */
	memset(&a, 0x5A, sizeof(a));
	memcpy(&b, &a, sizeof(a));
	CHECK(MainArcadeLinkSound_InputFromHostView(NULL, 1u, &a) == 0);
	CHECK(memcmp(&a, &b, sizeof(a)) == 0);
	CHECK(MainArcadeLinkSound_InputFromHostView(&view, 1u, NULL) == 0);
	return 0;
}

/* SND-9: MATCH_FOUND and link failures; everything else peer-driven is
 * silent. */
static int TestTransitions(void)
{
	struct MainArcadeLinkSoundInput prev;
	struct MainArcadeLinkSoundInput cur;

	/* Entering MATCH_FOUND, from the lobby or a rematch wait. */
	CHECK(Pair(Frame(S_LOBBY, L_CONNECTING, E_NONE), Frame(S_MATCH_FOUND, L_READY, E_NONE), 0u) == CONFIRM);
	CHECK(Pair(Frame(S_REMATCH_WAIT, L_CONNECTING, E_FINISHED), Frame(S_MATCH_FOUND, L_READY, E_FINISHED), 0u) ==
		CONFIRM);
	/* A rematch after a LINK ERROR carries the old reason: still CONFIRM. */
	CHECK(Pair(Frame(S_REMATCH_WAIT, L_CONNECTING, E_LINK_ERROR), Frame(S_MATCH_FOUND, L_READY, E_LINK_ERROR), 0u) ==
		CONFIRM);

	/* Link failures entering RESULTS. */
	prev = SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING);
	CHECK(Pair(prev, Frame(S_RESULTS, L_LOST, E_LINK_ERROR), 0u) == ERROR);
	CHECK(Pair(Frame(S_SELECT_RESULT, L_CONNECTING, E_NONE), Frame(S_RESULTS, L_REJECTED, E_LINK_ERROR), 0u) == ERROR);
	CHECK(Pair(Frame(S_RACING, L_READY, E_NONE), Frame(S_RESULTS, L_READY, E_PEER_TIMEOUT), 0u) == ERROR);
	CHECK(Pair(Frame(S_RACING, L_READY, E_NONE), Frame(S_RESULTS, L_LOST, E_DESYNC), 0u) == ERROR);
	CHECK(Pair(Frame(S_RACING, L_READY, E_NONE), Frame(S_RESULTS, L_LOST, E_LINK_ERROR), 0u) == ERROR);
	/* A rematch that failed again reports LINK ERROR anew on RESULTS. */
	CHECK(Pair(Frame(S_SELECT, L_READY, E_LINK_ERROR), Frame(S_RESULTS, L_WAITING, E_LINK_ERROR), 0u) == ERROR);
	/* The opponent left the rematch. */
	CHECK(Pair(Frame(S_REMATCH_WAIT, L_CONNECTING, E_FINISHED), Frame(S_EXIT, L_CONNECTING, E_OPPONENT_LEFT), 0u) ==
		ERROR);
	CHECK(Pair(Frame(S_REMATCH_WAIT, L_REJECTED, E_LINK_ERROR), Frame(S_EXIT, L_REJECTED, E_OPPONENT_LEFT), 0u) ==
		ERROR);
	/* The lobby status changed to REJECTED. */
	CHECK(Pair(Frame(S_LOBBY, L_CONNECTING, E_NONE), Frame(S_LOBBY, L_REJECTED, E_NONE), 0u) == ERROR);
	CHECK(Pair(Frame(S_LOBBY, L_WAITING, E_NONE), Frame(S_LOBBY, L_REJECTED, E_NONE), 0u) == ERROR);

	/* Silent transitions. */
	CHECK(Pair(Frame(S_LOBBY, L_REJECTED, E_NONE), Frame(S_LOBBY, L_REJECTED, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_LOBBY, L_CONNECTING, E_NONE), Frame(S_LOBBY, L_LOST, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_LOBBY, L_LOST, E_NONE), Frame(S_LOBBY, L_CONNECTING, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_LOBBY, L_CONNECTING, E_NONE), Frame(S_LOBBY, L_WAITING, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_MATCH_FOUND, L_READY, E_NONE), Frame(S_LOBBY, L_LOST, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_MATCH_FOUND, L_READY, E_NONE), SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING), 0u) == NONE);
	CHECK(Pair(SelectFrame(ITEM_DONE, 7u, ST_WAITING), SelectFrame(ITEM_DONE, 7u, ST_CONFIRMED), 0u) == NONE);
	prev = SelectFrame(ITEM_DONE, 7u, ST_CONFIRMED);
	cur = prev;
	cur.local.screen = S_SELECT_RESULT;
	CHECK(Pair(prev, cur, 0u) == NONE);
	CHECK(Pair(Frame(S_SELECT_RESULT, L_READY, E_NONE), Frame(S_RACING, L_READY, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_RACING, L_READY, E_NONE), Frame(S_RESULTS, L_READY, E_FINISHED), 0u) == NONE);
	/* RESULTS idle timeout to EXIT keeps the old failure reason: silent. */
	CHECK(Pair(Frame(S_RESULTS, L_WAITING, E_LINK_ERROR), Frame(S_EXIT, L_WAITING, E_LINK_ERROR), 0u) == NONE);
	CHECK(Pair(Frame(S_RESULTS, L_WAITING, E_FINISHED), Frame(S_EXIT, L_WAITING, E_FINISHED), 0u) == NONE);
	CHECK(Pair(Frame(S_EXIT, L_WAITING, E_OPPONENT_LEFT), Frame(S_OFF, L_WAITING, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_EXIT, L_WAITING, E_NONE), Frame(S_OFF, L_WAITING, E_NONE), 0u) == NONE);
	CHECK(Pair(Frame(S_OFF, L_WAITING, E_NONE), Frame(S_OFF, L_WAITING, E_NONE), 0u) == NONE);
	return 0;
}

/* SND-1: at most one cue, and a local-input cue beats a transition cue. */
static int TestPriority(void)
{
	struct MainArcadeLinkSoundInput prev;
	struct MainArcadeLinkSoundInput cur;

	/* A local lock on the tick the lobby reports REJECTED: CONFIRM. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING), EV_CONFIRM);
	cur.local.lobbyStatus = L_REJECTED;
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* A local back out of the lobby on the tick it became REJECTED: BACK. */
	prev = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	cur = WithEvent(Frame(S_EXIT, L_REJECTED, E_NONE), EV_BACK);
	CHECK(Pair(prev, cur, 0u) == BACK);
	/* A confirm on the tick the lobby turned REJECTED retries it: CONFIRM. */
	cur = WithEvent(Frame(S_LOBBY, L_REJECTED, E_NONE), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* A local cursor move on the tick the item auto-locks: MOVE. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING), EV_NEXT);
	cur.local.characterID = 1u;
	CHECK(Pair(prev, cur, 0u) == MOVE);
	/* A confirm in the lobby on the tick the peer is found: CONFIRM. */
	prev = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	cur = WithEvent(Frame(S_MATCH_FOUND, L_READY, E_NONE), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == CONFIRM);
	/* An ignored input on the tick of a link failure: the failure's ERROR,
	 * since the input took no effect. */
	prev = Frame(S_SELECT_RESULT, L_CONNECTING, E_NONE);
	cur = WithEvent(Frame(S_RESULTS, L_REJECTED, E_LINK_ERROR), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == ERROR);
	cur.localMenuEvent = EV_BACK;
	CHECK(Pair(prev, cur, 0u) == ERROR);
	cur.localMenuEvent = EV_NEXT;
	CHECK(Pair(prev, cur, 0u) == ERROR);
	prev = Frame(S_REMATCH_WAIT, L_CONNECTING, E_FINISHED);
	cur = WithEvent(Frame(S_EXIT, L_REJECTED, E_OPPONENT_LEFT), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == ERROR);
	/* A refused pick on the tick the link fails: the failure. */
	prev = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	cur = WithEvent(Frame(S_RESULTS, L_LOST, E_LINK_ERROR), EV_CONFIRM);
	CHECK(Pair(prev, cur, 0u) == ERROR);
	return 0;
}

/* The snapshot follows every decided frame, and the decision is a pure
 * function of the sequence. */
static int TestSequence(void)
{
	struct MainArcadeLinkSoundInput frames[12];
	uint32_t first[12];
	struct MainArcadeLinkSoundState state;
	uint32_t run;
	uint32_t i;
	const uint32_t expected[12] = {NONE, CONFIRM, NONE, CONFIRM, NONE, MOVE, ERROR, CONFIRM, CONFIRM, CONFIRM, NONE, NONE};

	frames[0] = Frame(S_OFF, L_WAITING, E_NONE);
	frames[1] = Frame(S_LOBBY, L_CONNECTING, E_NONE); /* entered with enterPressed */
	frames[2] = Frame(S_LOBBY, L_CONNECTING, E_NONE);
	frames[3] = Frame(S_MATCH_FOUND, L_READY, E_NONE);
	frames[4] = SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING);
	frames[5] = WithEvent(SelectFrame(ITEM_CHARACTER, 0u, ST_PICKING), EV_NEXT);
	frames[5].local.characterID = 1u;
	frames[6] = WithEvent(frames[5], EV_CONFIRM); /* peer-locked: refused */
	frames[7] = WithEvent(SelectFrame(ITEM_TRACK, LOCK_CHARACTER, ST_PICKING), EV_CONFIRM);
	frames[7].local.characterID = 2u;
	frames[8] = SelectFrame(ITEM_LAPS, LOCK_CHARACTER | LOCK_TRACK, ST_PICKING); /* auto-lock */
	frames[8].local.characterID = 2u;
	frames[9] = WithEvent(SelectFrame(ITEM_DONE, 7u, ST_WAITING), EV_CONFIRM);
	frames[9].local.characterID = 2u;
	frames[10] = SelectFrame(ITEM_DONE, 7u, ST_WAITING);
	frames[10].local.characterID = 2u;
	frames[11] = frames[10];

	for (run = 0u; run < 2u; run++)
	{
		MainArcadeLinkSound_Reset(&state);
		for (i = 0u; i < 12u; i++)
		{
			const uint32_t cue = MainArcadeLinkSound_Decide(&state, &frames[i], (i == 1u) ? 1u : 0u);

			if (run == 0u)
			{
				first[i] = cue;
				if (cue != expected[i])
				{
					fprintf(stderr, "sequence frame %u: cue %u, expected %u\n", (unsigned)i, (unsigned)cue,
						(unsigned)expected[i]);
					return 1;
				}
			}
			else
			{
				CHECK(cue == first[i]);
			}
			CHECK(state.valid == 1u);
			CHECK(memcmp(&state.previous, &frames[i].local, sizeof(frames[i].local)) == 0);
		}
	}
	return 0;
}

int main(void)
{
	if (TestRetailIds() != 0)
		return 1;
	if (TestResetAndSilentModes() != 0)
		return 1;
	if (TestMove() != 0)
		return 1;
	if (TestConfirm() != 0)
		return 1;
	if (TestRefusedConfirm() != 0)
		return 1;
	if (TestBack() != 0)
		return 1;
	if (TestAutoLock() != 0)
		return 1;
	if (TestLocalOnly() != 0)
		return 1;
	if (TestTransitions() != 0)
		return 1;
	if (TestPriority() != 0)
		return 1;
	if (TestSequence() != 0)
		return 1;
	printf("main_arcade_link_sound_test: ok\n");
	return 0;
}
