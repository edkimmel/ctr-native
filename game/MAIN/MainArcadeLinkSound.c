#include "MAIN/MainArcadeLinkSound.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Arcade-link menu sound decision (docs/GAME_LOOP_UI_MILESTONE.md section
 * 3.1). See MAIN/MainArcadeLinkSound.h for the rules.
 */

void MainArcadeLinkSound_Reset(struct MainArcadeLinkSoundState *state)
{
	if (state == NULL)
	{
		return;
	}
	memset(state, 0, sizeof(*state));
}

int MainArcadeLinkSound_InputFromHostView(const struct NativeArcadeLinkHostView *view, uint32_t hostMode, struct MainArcadeLinkSoundInput *input)
{
	const struct NativeArcadeLinkHostSelectHumanView *local;

	if ((view == NULL) || (input == NULL))
	{
		return 0;
	}

	memset(input, 0, sizeof(*input));
	input->hostMode = hostMode;
	input->localMenuEvent = view->localMenuEvent;
	input->local.screen = view->screen;
	input->local.lobbyStatus = view->lobbyStatus;
	input->local.endReason = view->endReason;
	input->local.selectedRow = view->selectedRow;

	/* Only the local human's own entry; every other entry and the peer
	 * mask stay out of the decision (SND-2). */
	if ((view->select.active != 0u) && (view->select.localHuman < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS))
	{
		local = &view->select.humans[view->select.localHuman];
		input->local.selectActive = 1u;
		input->local.selectStatus = view->select.status;
		input->local.characterID = local->characterID;
		input->local.trackID = local->trackID;
		input->local.lapCount = local->lapCount;
		input->local.lockMask = local->lockMask;
		input->local.currentItem = local->currentItem;
	}
	return 1;
}

static int MainArcadeLinkSound_IsFailure(uint32_t endReason)
{
	return (endReason == MAIN_ARCADE_LINK_SOUND_END_PEER_TIMEOUT) || (endReason == MAIN_ARCADE_LINK_SOUND_END_DESYNC) ||
	       (endReason == MAIN_ARCADE_LINK_SOUND_END_LINK_ERROR) || (endReason == MAIN_ARCADE_LINK_SOUND_END_OPPONENT_LEFT);
}

/* A screen entry that reports a new link failure. Every RESULTS entry sets
 * its end reason afresh; other screens can carry an old one (RESULTS to
 * EXIT or REMATCH_WAIT, and on through a rematch), which counts only when it
 * changed. */
static int MainArcadeLinkSound_FreshFailure(const struct MainArcadeLinkSoundLocal *prev, const struct MainArcadeLinkSoundLocal *cur)
{
	if ((cur->screen == prev->screen) || !MainArcadeLinkSound_IsFailure(cur->endReason))
	{
		return 0;
	}
	return (cur->screen == MAIN_ARCADE_LINK_SOUND_SCREEN_RESULTS) || (cur->endReason != prev->endReason);
}

/* 1 when the local cursor value of item differs between the two frames;
 * 0 for DONE or an unknown item, which has no cursor. */
static int MainArcadeLinkSound_CursorChanged(const struct MainArcadeLinkSoundLocal *prev, const struct MainArcadeLinkSoundLocal *cur, uint8_t item)
{
	switch (item)
	{
	case NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER:
		return prev->characterID != cur->characterID;
	case NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK:
		return prev->trackID != cur->trackID;
	case NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS:
		return prev->lapCount != cur->lapCount;
	default:
		return 0;
	}
}

/* The facts of one frame pair that the rules read. */
struct MainArcadeLinkSoundFacts
{
	int screenChanged;
	int freshFailure;
	int rowChanged;
	int selectBoth;
	int lockGained;
	int lockLost;
	int itemAdvanced;
	int itemBack;
};

static void MainArcadeLinkSound_Facts(const struct MainArcadeLinkSoundLocal *prev, const struct MainArcadeLinkSoundLocal *cur,
                                      struct MainArcadeLinkSoundFacts *facts)
{
	facts->screenChanged = (cur->screen != prev->screen);
	facts->freshFailure = MainArcadeLinkSound_FreshFailure(prev, cur);
	facts->rowChanged = !facts->screenChanged && (cur->selectedRow != prev->selectedRow);
	facts->selectBoth = (prev->selectActive != 0u) && (cur->selectActive != 0u);
	facts->lockGained = facts->selectBoth && (((uint32_t)cur->lockMask & ~(uint32_t)prev->lockMask) != 0u);
	facts->lockLost = facts->selectBoth && (((uint32_t)prev->lockMask & ~(uint32_t)cur->lockMask) != 0u);
	facts->itemAdvanced = facts->selectBoth && (cur->currentItem > prev->currentItem);
	facts->itemBack = facts->selectBoth && (cur->currentItem < prev->currentItem);
}

/* SND-3 to SND-6 and the attract entry of SND-4: cues from this cabinet's
 * own input, judged by the effect it had. */
static uint32_t MainArcadeLinkSound_InputCue(const struct MainArcadeLinkSoundLocal *prev, const struct MainArcadeLinkSoundLocal *cur,
                                             const struct MainArcadeLinkSoundFacts *facts, uint8_t event, uint8_t enterPressed)
{
	const int effectiveScreenChange = facts->screenChanged && !facts->freshFailure;

	if ((enterPressed != 0u) && (prev->screen == MAIN_ARCADE_LINK_SOUND_SCREEN_OFF) && (cur->screen != MAIN_ARCADE_LINK_SOUND_SCREEN_OFF))
	{
		return MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM;
	}

	switch (event)
	{
	case NATIVE_ARCADE_MENU_EVENT_PREV:
	case NATIVE_ARCADE_MENU_EVENT_NEXT:
		if (facts->rowChanged)
		{
			return MAIN_ARCADE_LINK_SOUND_CUE_MOVE;
		}
		if (!facts->screenChanged && facts->selectBoth && (cur->screen == MAIN_ARCADE_LINK_SOUND_SCREEN_SELECT) &&
		    MainArcadeLinkSound_CursorChanged(prev, cur, prev->currentItem))
		{
			return MAIN_ARCADE_LINK_SOUND_CUE_MOVE;
		}
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	case NATIVE_ARCADE_MENU_EVENT_CONFIRM:
		if (effectiveScreenChange || facts->lockGained || facts->itemAdvanced)
		{
			return MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM;
		}
		/* The flow retries a lobby it observed REJECTED on CONFIRM; the
		 * status shown this frame is the one it acted on. */
		if (!facts->screenChanged && (cur->screen == MAIN_ARCADE_LINK_SOUND_SCREEN_LOBBY) && (cur->lobbyStatus == MAIN_ARCADE_LINK_SOUND_LOBBY_REJECTED))
		{
			return MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM;
		}
		/* A pick the select refused (the cursor character is held by a
		 * peer): the retail locked-row womp. */
		if (!facts->screenChanged && (cur->screen == MAIN_ARCADE_LINK_SOUND_SCREEN_SELECT) && (cur->selectActive != 0u) &&
		    (cur->selectStatus == NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING))
		{
			return MAIN_ARCADE_LINK_SOUND_CUE_ERROR;
		}
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	case NATIVE_ARCADE_MENU_EVENT_BACK:
		if (effectiveScreenChange || facts->rowChanged || facts->lockLost || facts->itemBack)
		{
			return MAIN_ARCADE_LINK_SOUND_CUE_BACK;
		}
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	default:
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	}
}

/* SND-7 and SND-9: cues from transitions, only when no input cue applies. */
static uint32_t MainArcadeLinkSound_TransitionCue(const struct MainArcadeLinkSoundLocal *prev, const struct MainArcadeLinkSoundLocal *cur,
                                                  const struct MainArcadeLinkSoundFacts *facts)
{
	if (facts->freshFailure)
	{
		return MAIN_ARCADE_LINK_SOUND_CUE_ERROR;
	}
	if ((cur->lobbyStatus == MAIN_ARCADE_LINK_SOUND_LOBBY_REJECTED) && (prev->lobbyStatus != MAIN_ARCADE_LINK_SOUND_LOBBY_REJECTED))
	{
		return MAIN_ARCADE_LINK_SOUND_CUE_ERROR;
	}
	if (facts->screenChanged && (cur->screen == MAIN_ARCADE_LINK_SOUND_SCREEN_MATCH_FOUND))
	{
		return MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM;
	}
	/* An effective local CONFIRM that gained the lock was an input cue
	 * already, so a gain seen here is the item timer's auto-lock. */
	if (facts->lockGained)
	{
		return MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM;
	}
	return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
}

uint32_t MainArcadeLinkSound_Decide(struct MainArcadeLinkSoundState *state, const struct MainArcadeLinkSoundInput *input, uint8_t enterPressed)
{
	struct MainArcadeLinkSoundFacts facts;
	uint32_t cue;

	if (state == NULL)
	{
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	}
	if ((input == NULL) || (input->hostMode != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		MainArcadeLinkSound_Reset(state);
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	}
	if (state->valid == 0u)
	{
		state->previous = input->local;
		state->valid = 1u;
		return MAIN_ARCADE_LINK_SOUND_CUE_NONE;
	}

	MainArcadeLinkSound_Facts(&state->previous, &input->local, &facts);
	cue = MainArcadeLinkSound_InputCue(&state->previous, &input->local, &facts, input->localMenuEvent, enterPressed);
	if (cue == MAIN_ARCADE_LINK_SOUND_CUE_NONE)
	{
		cue = MainArcadeLinkSound_TransitionCue(&state->previous, &input->local, &facts);
	}
	state->previous = input->local;
	return cue;
}

int MainArcadeLinkSound_RetailId(uint32_t cue, uint32_t *soundID)
{
	uint32_t id;

	if (soundID == NULL)
	{
		return 0;
	}
	switch (cue)
	{
	case MAIN_ARCADE_LINK_SOUND_CUE_MOVE:
		id = MAIN_ARCADE_LINK_SOUND_ID_MOVE;
		break;
	case MAIN_ARCADE_LINK_SOUND_CUE_CONFIRM:
		id = MAIN_ARCADE_LINK_SOUND_ID_CONFIRM;
		break;
	case MAIN_ARCADE_LINK_SOUND_CUE_BACK:
		id = MAIN_ARCADE_LINK_SOUND_ID_BACK;
		break;
	case MAIN_ARCADE_LINK_SOUND_CUE_ERROR:
		id = MAIN_ARCADE_LINK_SOUND_ID_ERROR;
		break;
	default:
		return 0;
	}
	*soundID = id;
	return 1;
}
