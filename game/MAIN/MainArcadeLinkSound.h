#ifndef MAIN_ARCADE_LINK_SOUND_H
#define MAIN_ARCADE_LINK_SOUND_H

#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_menu_input.h"

#include <stdint.h>

/*
 * Arcade-link menu sound decision (docs/GAME_LOOP_UI_MILESTONE.md section
 * 3.1, SND-1 to SND-11). The pure part of the arcade-link menu sounds: from
 * this cabinet's host view after one tick, the previous frame's snapshot of
 * the same local fields, and the hook's attract-entry flag, it picks at most
 * one menu cue (navigate, confirm, back, or error) and maps it to the retail
 * menu sound ID. The thin hook (game/MAIN/MainArcadeLink.c) gathers the view,
 * calls MainArcadeLinkSound_Decide, and plays the retail sound; nothing here
 * plays, reads, or writes anything else.
 *
 * Local only (SND-2): the input carries only this cabinet's own view fields
 * (screen, lobby status, end reason, results row, the select status, and the
 * local human's select entry) and the local menu event. A peer's select
 * entry, the peer-locked character mask, and peer cursor movement never
 * reach the decision, so they can never produce a cue.
 *
 * Pure: caller-owned state, no heap use, no I/O, no hidden state, and fully
 * deterministic.
 */

/* Menu cues, the result of MainArcadeLinkSound_Decide. */
#define MAIN_ARCADE_LINK_SOUND_CUE_NONE 0u
#define MAIN_ARCADE_LINK_SOUND_CUE_MOVE 1u
#define MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM 2u
#define MAIN_ARCADE_LINK_SOUND_CUE_BACK 3u
#define MAIN_ARCADE_LINK_SOUND_CUE_ERROR 4u

/*
 * The retail menu sound IDs (SND-1). The retail menus play each with flags 1
 * (no duplicates, recycle the old one); the hook passes the same flags.
 */
/* Cursor moved: game/RECTMENU.c:799, game/230/MM_Characters.c:1156,
 * game/230/MM_TrackSelect.c:635. */
#define MAIN_ARCADE_LINK_SOUND_ID_MOVE 0u
/* Confirm: game/RECTMENU.c:845, game/230/MM_Characters.c:1216,
 * game/230/MM_TrackSelect.c:677, game/230/MM_Battle.c:500. */
#define MAIN_ARCADE_LINK_SOUND_ID_CONFIRM 1u
/* Back: game/RECTMENU.c:817, game/230/MM_Characters.c:1239 and 1246. */
#define MAIN_ARCADE_LINK_SOUND_ID_BACK 2u
/* Error, the "womp" for a locked row: game/RECTMENU.c:866. */
#define MAIN_ARCADE_LINK_SOUND_ID_ERROR 5u

/* Mirrors of the flow values the decision reads
 * (include/platform/native_arcade_flow.h; the host view carries them as
 * uint32_t). MainArcadeLink.c and the unit test static-assert each one. */
#define MAIN_ARCADE_LINK_SOUND_SCREEN_OFF 0u
#define MAIN_ARCADE_LINK_SOUND_SCREEN_LOBBY 1u
#define MAIN_ARCADE_LINK_SOUND_SCREEN_MATCH_FOUND 2u
#define MAIN_ARCADE_LINK_SOUND_SCREEN_RESULTS 4u
#define MAIN_ARCADE_LINK_SOUND_SCREEN_SELECT 7u
#define MAIN_ARCADE_LINK_SOUND_LOBBY_REJECTED 3u
#define MAIN_ARCADE_LINK_SOUND_END_PEER_TIMEOUT 2u
#define MAIN_ARCADE_LINK_SOUND_END_DESYNC 3u
#define MAIN_ARCADE_LINK_SOUND_END_LINK_ERROR 4u
#define MAIN_ARCADE_LINK_SOUND_END_OPPONENT_LEFT 5u

/* This cabinet's own fields of one host view: the only facts a cue may
 * depend on. The select fields are 0 unless selectActive. */
struct MainArcadeLinkSoundLocal
{
	/* enum NativeArcadeFlowScreen */
	uint32_t screen;
	/* enum NativeArcadeFlowLobbyStatus */
	uint32_t lobbyStatus;
	/* enum NativeArcadeFlowEndReason */
	uint32_t endReason;
	/* the focused results row */
	uint32_t selectedRow;
	/* 1 while the host view exposes a select and its local human */
	uint8_t selectActive;
	/* NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_* */
	uint8_t selectStatus;
	/* the local human's cursor or locked values */
	uint8_t characterID;
	uint8_t trackID;
	uint8_t lapCount;
	/* NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_* bits of the local human */
	uint8_t lockMask;
	/* NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_* of the local human */
	uint8_t currentItem;
	uint8_t reserved;
};

/* One frame's decision input. */
struct MainArcadeLinkSoundInput
{
	/* enum NativeArcadeLinkHostMode */
	uint32_t hostMode;
	/* enum NativeArcadeMenuEvent: the host view's localMenuEvent */
	uint8_t localMenuEvent;
	uint8_t reserved[3];
	struct MainArcadeLinkSoundLocal local;
};

/* Caller-owned previous-frame snapshot. A zero-initialized struct behaves
 * exactly like one passed to MainArcadeLinkSound_Reset. */
struct MainArcadeLinkSoundState
{
	/* 1 once previous holds the last decided frame */
	uint8_t valid;
	uint8_t reserved[3];
	struct MainArcadeLinkSoundLocal previous;
};

/* Zeroes the state: the next Decide has no previous view and plays nothing.
 * NULL is a no-op. */
void MainArcadeLinkSound_Reset(struct MainArcadeLinkSoundState *state);

/*
 * Fills *input (always zeroed first) from a host view and returns 1; returns
 * 0, touching nothing, when either pointer is NULL. Only this cabinet's
 * fields are copied: the screen, lobby status, end reason, results row, the
 * local menu event, the select status, and select.humans[select.localHuman]
 * when the select is active and localHuman is in range (otherwise every
 * select field stays 0). No other human's entry and no peer mask is read.
 */
int MainArcadeLinkSound_InputFromHostView(const struct NativeArcadeLinkHostView *view, uint32_t hostMode,
	struct MainArcadeLinkSoundInput *input);

/*
 * Returns one MAIN_ARCADE_LINK_SOUND_CUE_* for this frame and stores the
 * frame's local fields as the next previous view. enterPressed is the hook
 * policy's attract-entry flag for this frame.
 *
 * NULL state returns NONE. NULL input, or any host mode but LINK (OFF, and
 * PREVIEW, which is scripted and has no input), resets the state and returns
 * NONE (SND-10). With no previous view (the first frame after a reset) the
 * frame is stored and NONE returned (SND-10).
 *
 * Otherwise, with "screen changed" meaning the screen differs from the
 * previous frame's, and a "fresh failure entry" meaning a screen change onto
 * RESULTS with end reason PEER_TIMEOUT, DESYNC, or LINK_ERROR, or onto any
 * screen whose failure end reason (those three or OPPONENT_LEFT) differs
 * from the previous frame's, the first local-input cue that applies wins
 * (SND-1):
 * - CONFIRM: enterPressed and the screen went from OFF to another screen
 *   (SND-4).
 * - MOVE: event PREV or NEXT, the screen did not change, and either the
 *   results row changed or, on SELECT with the select active on both
 *   frames, the local cursor value of the previous frame's current item
 *   changed (SND-3).
 * - CONFIRM: event CONFIRM and it took effect: the screen changed (not to a
 *   fresh failure entry), the local lock mask gained a bit, the local
 *   current item advanced, or a LOBBY showing REJECTED stayed on LOBBY (the
 *   flow retries a rejected lobby on CONFIRM) (SND-4).
 * - ERROR: event CONFIRM with no such effect on SELECT, select active,
 *   status PICKING: the retail locked-row womp (SND-5).
 * - BACK: event BACK and it took effect: the screen changed (not to a fresh
 *   failure entry), the results row changed, the local lock mask lost a bit,
 *   or the local current item went back (SND-6).
 * and, only when no local-input cue applies, the first transition cue:
 * - ERROR: a fresh failure entry, or the lobby status changed to REJECTED
 *   (SND-9).
 * - CONFIRM: the screen changed to MATCH_FOUND (SND-9).
 * - CONFIRM: the local lock mask gained a bit without an effective local
 *   CONFIRM (the item timer expired) (SND-7).
 * Everything else is NONE: no-effect input, every other transition, and
 * anything a peer does (SND-2, SND-8).
 */
uint32_t MainArcadeLinkSound_Decide(struct MainArcadeLinkSoundState *state, const struct MainArcadeLinkSoundInput *input,
	uint8_t enterPressed);

/* Maps a cue to its retail sound ID (SND-1): fills *soundID and returns 1 for
 * MOVE, CONFIRM, BACK, and ERROR; returns 0, touching nothing, for NONE, any
 * unknown cue, or NULL. */
int MainArcadeLinkSound_RetailId(uint32_t cue, uint32_t *soundID);

#endif
