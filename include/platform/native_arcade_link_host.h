#ifndef PLATFORM_NATIVE_ARCADE_LINK_HOST_H
#define PLATFORM_NATIVE_ARCADE_LINK_HOST_H

#include <stdint.h>

#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_identity.h"

/*
 * Arcade-link host glue (docs/GAME_LOOP_UI_MILESTONE.md section 2.6).
 *
 * A process-wide singleton, like platform/native_platform.c, that owns the
 * arcade-link host adapter (link mode) or a scripted screen preview (preview
 * mode) and gives game code a small API over it: configure it once from the
 * parsed host options, enter the link from the title screen, tick it once per
 * game-loop tick, read a flat view for the screen drawer, and shut it down.
 *
 * Everything here is dormant unless NativeArcadeLinkHost_Configure is given
 * options with the link enabled or a preview requested. With no Configure
 * call, or with default options, the mode is OFF and every call below is
 * inert: no socket opens, nothing ticks, and GetView reports nothing.
 * Preview mode never opens a socket either.
 *
 * This header is safe for game code: it names only this module's own types,
 * the host options, the menu-input button bits, and the identity struct.
 * Screen, lobby-status, end-reason, and action values use the enums of
 * include/platform/native_arcade_flow.h, carried here as uint32_t.
 *
 * No heap use and no wall clock: every duration is counted in caller ticks.
 */

enum NativeArcadeLinkHostMode
{
	NATIVE_ARCADE_LINK_HOST_MODE_OFF = 0,
	NATIVE_ARCADE_LINK_HOST_MODE_LINK = 1,
	NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW = 2
};

/* The select view's per-human and bot capacities, and the match slot count. */
#define NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS 4u
#define NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS 8u
#define NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS 8u

/* One human on the select screens (docs/MATCH_SELECT_MILESTONE.md section
 * 2.7): the local human's own state, or a peer's latest state. Everything is
 * 0 while present is 0. */
struct NativeArcadeLinkHostSelectHumanView
{
	/* 1 for the local human always; 1 for a peer once it was heard */
	uint8_t present;
	/* cursor or locked value (a base character ID, 0..7) */
	uint8_t characterID;
	/* cursor or locked value (a levelID): this human's vote */
	uint8_t trackID;
	/* cursor or locked value (3, 5, or 7): this human's vote */
	uint8_t lapCount;
	/* bit 0 character, bit 1 track, bit 2 laps locked */
	uint8_t lockMask;
	/* 0 character, 1 track, 2 laps, 3 done */
	uint8_t currentItem;
	uint8_t reserved[2];
};

/* The select phase, flat. Everything is 0 unless active. */
struct NativeArcadeLinkHostSelectView
{
	/* 1 on the SELECT and SELECT_RESULT screens while a select exists */
	uint8_t active;
	uint8_t humanCount;
	/* the local human's index (cabinet - 1) */
	uint8_t localHuman;
	/* the local human's current item: 0 character, 1 track, 2 laps, 3 done */
	uint8_t currentItem;
	/* ticks until the local current item auto-locks; 0 once done */
	uint32_t ticksLeft;
	/* 0 picking, 1 waiting, 2 resolved, 3 confirmed, 4 failed */
	uint8_t status;
	/* 1 when the outcome fields below are valid */
	uint8_t resolved;
	uint8_t trackID;
	uint8_t lapCount;
	/* 1 if the track came from a tie draw */
	uint8_t trackDrawn;
	/* 1 if the lap count came from a tie draw */
	uint8_t lapsDrawn;
	/* bit h: human h was reassigned a character */
	uint8_t characterReassignedMask;
	uint8_t botCount;
	/* the first humanCount used, the rest 0 */
	uint8_t humanCharacter[NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS];
	/* the first botCount used, the rest 0 */
	uint8_t botCharacter[NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS];
	/* bit c: a peer has locked base character c (greyed on the character
	 * screen; it cannot be confirmed) */
	uint16_t peerLockedCharacterMask;
	uint8_t reserved[2];
	/* indexed by human; entries at or above humanCount stay 0 */
	struct NativeArcadeLinkHostSelectHumanView humans[NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS];
};

/* Everything a screen drawer needs. Fields not meaningful for the current
 * screen are 0. */
struct NativeArcadeLinkHostView
{
	/* enum NativeArcadeFlowScreen */
	uint32_t screen;
	/* enum NativeArcadeFlowLobbyStatus */
	uint32_t lobbyStatus;
	/* enum NativeArcadeFlowEndReason */
	uint32_t endReason;
	uint32_t selectedRow;
	/* Ticks in the current screen; on screen OFF, the idle (attract) ticks. */
	uint32_t ticksInScreen;
	/* 1 or 2 */
	uint8_t localCab;
	/* 1 only on RESULTS once the menu accepts input */
	uint8_t rowsEnabled;
	/* 1 when the screen is OFF: the title attract layout applies */
	uint8_t attract;
	uint8_t reserved;
	/* The select screens (docs/MATCH_SELECT_MILESTONE.md section 2.7). */
	struct NativeArcadeLinkHostSelectView select;
};

/* The agreed match, for logging. slotRole uses the match-config slot roles
 * (0 inactive, 1 cabinet 1, 2 cabinet 2, 3 bot). */
struct NativeArcadeLinkHostMatch
{
	uint32_t trackID;
	uint32_t lapCount;
	uint64_t masterSeed;
	uint8_t slotRole[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS];
	uint8_t slotCharacter[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS];
};

/*
 * Always shuts down first, so a second call replaces the first. NULL options
 * leave the mode OFF and return 0. Options with neither the link enabled nor
 * a preview leave the mode OFF and return 1. A preview (without the link)
 * selects PREVIEW mode and returns 1; identity is not needed. An enabled link
 * needs a non-NULL identity that builds the fixed fixture, and a valid port
 * and peer list; it selects LINK mode, dormant on screen OFF with no socket
 * open, and returns 1. On any failure the mode is OFF and 0 is returned.
 */
int NativeArcadeLinkHost_Configure(const struct NativeArcadeLinkOptions *options,
	const struct NativeIdentityV1 *identity);

/* enum NativeArcadeLinkHostMode */
uint32_t NativeArcadeLinkHost_Mode(void);

/* LINK: 1 while the flow is on any screen other than OFF. PREVIEW: 1. OFF: 0. */
int NativeArcadeLinkHost_ScreenActive(void);

/* LINK only: from screen OFF, enters the lobby and returns 1. Otherwise 0. */
int NativeArcadeLinkHost_Enter(void);

/* One game-loop tick. heldMenuButtons uses the NATIVE_ARCADE_MENU_BUTTON_*
 * bits; raceFinished is nonzero once the local race has finished. Returns an
 * enum NativeArcadeFlowAction: START_RACE and RETURN_TO_TITLE are the
 * caller's cue; every other action has already been executed. OFF and
 * PREVIEW return NONE. */
uint32_t NativeArcadeLinkHost_Tick(uint32_t heldMenuButtons, uint8_t raceFinished);

/* Fills *view and returns 1; returns 0 on NULL or in mode OFF. */
int NativeArcadeLinkHost_GetView(struct NativeArcadeLinkHostView *view);

/* LINK only: when the link has an agreed race config (on RACING, and on
 * RESULTS after a race was started), fills *out from it and returns 1.
 * Otherwise (NULL, OFF, PREVIEW, or no agreed config) returns 0 with *out
 * untouched. */
int NativeArcadeLinkHost_GetAgreedMatch(struct NativeArcadeLinkHostMatch *out);

/* LINK only: closes the link and returns the flow to screen OFF, ready for a
 * new Enter. For when the game cannot honour START_RACE yet. The link is
 * re-initialized with a new select entropy (see
 * NativeArcadeLinkHost_MixSelectEntropy). */
void NativeArcadeLinkHost_AbortToTitle(void);

/* Pure: the select entropy handed to the link for one host epoch,
 * entropy ^ (epoch * 0x9E3779B97F4A7C15), modulo 2^64. The host keeps a
 * process-local epoch counter that every LINK Configure and every
 * AbortToTitle increments before it initializes the link, so the select
 * nonces keep varying across the re-initialization AbortToTitle performs,
 * even though the link restarts its own select count there. */
uint64_t NativeArcadeLinkHost_MixSelectEntropy(uint64_t entropy, uint64_t epoch);

/* Closes any open link and returns to mode OFF. Idempotent and safe before
 * any Configure. */
void NativeArcadeLinkHost_Shutdown(void);

#endif
