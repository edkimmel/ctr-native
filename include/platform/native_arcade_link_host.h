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

/* LINK only: closes the link and returns the flow to screen OFF, ready for a
 * new Enter. For when the game cannot honour START_RACE yet. */
void NativeArcadeLinkHost_AbortToTitle(void);

/* Closes any open link and returns to mode OFF. Idempotent and safe before
 * any Configure. */
void NativeArcadeLinkHost_Shutdown(void);

#endif
