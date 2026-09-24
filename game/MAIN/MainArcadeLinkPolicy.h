#ifndef MAIN_ARCADE_LINK_POLICY_H
#define MAIN_ARCADE_LINK_POLICY_H

#include "platform/native_arcade_menu_input.h"

#include <stdint.h>

/*
 * Arcade-link live hook decision policy (docs/GAME_LOOP_UI_MILESTONE.md
 * section 2.5, Task 6b-3). The pure part of game/MAIN/MainArcadeLink.c: from
 * one frame's facts (host mode, title state, main-menu box, raw pad words) it
 * decides whether the arcade-link layer owns the frame's menu layer and what
 * the thin hook must do about it. It reads no game state and calls nothing;
 * the hook gathers the inputs, calls MainArcadeLinkPolicy_Decide, and applies
 * the outputs.
 *
 * Ownership. In LINK and PREVIEW mode the layer owns the frame whenever the
 * retail main-menu box could be visible or take input on the idle main-menu
 * level: the box is the menu the retail menu system is about to process, the
 * main-menu state is the title, and the title is not in its intro before the
 * retail menu-ready frame. That covers the intro slide-in (the title state is
 * still INTRO once the intro frame reaches the menu-ready frame, or is skipped
 * past it), IN_MENU, EXITING, RETURNING, and any other title state. An open
 * submenu never relaxes ownership, so the retail menu hierarchy stays
 * unreachable and hidden. Before the menu-ready frame the box is input-less
 * and undrawn in retail, so the layer leaves the frame alone and the retail
 * intro skip keeps working. In LINK mode the layer also owns every frame on
 * which a link screen is active, on any level; PREVIEW mode never owns
 * outside the title window.
 *
 * Race frames are ticked, not owned (docs/RACE_LAUNCH_MILESTONE.md RL-8). In
 * LINK mode, with the host flow on RACING and the frame not on the idle
 * main-menu level (another level, or a load in progress), the layer does not
 * own the frame: it only ticks the host with the held buttons. Nothing else
 * is touched, so the retail race keeps its pad taps, the box is neither
 * hidden nor restored, and the demo countdown is left alone. On the idle
 * main-menu level RACING is owned by the rules above.
 *
 * Pure: caller-owned output, no heap use, no I/O, no mutable state, and
 * fully deterministic.
 */

/* Mirrors of the arcade-link host modes (include/platform/native_arcade_link_host.h). */
#define MAIN_ARCADE_LINK_POLICY_MODE_OFF 0u
#define MAIN_ARCADE_LINK_POLICY_MODE_LINK 1u
#define MAIN_ARCADE_LINK_POLICY_MODE_PREVIEW 2u

/* Mirrors of the retail title states (enum TitleMenuState, include/ovr_230.h). */
#define MAIN_ARCADE_LINK_POLICY_TITLE_INTRO 0u
#define MAIN_ARCADE_LINK_POLICY_TITLE_IN_MENU 1u
#define MAIN_ARCADE_LINK_POLICY_TITLE_EXITING 2u
#define MAIN_ARCADE_LINK_POLICY_TITLE_RETURNING 3u

/* Mirror of TITLE_INTRO_MENU_READY_FRAME (include/ovr_230.h): the intro frame
 * from which the retail title code makes the main-menu box interactive and
 * slides it in. */
#define MAIN_ARCADE_LINK_POLICY_MENU_READY_FRAME 230

/* Mirrors of the retail held-button bits (enum Buttons, include/namespace_Gamepad.h).
 * Only the first Cross and Square bits are mapped, the bits the retail menus read. */
#define MAIN_ARCADE_LINK_POLICY_BTN_UP 0x1u
#define MAIN_ARCADE_LINK_POLICY_BTN_DOWN 0x2u
#define MAIN_ARCADE_LINK_POLICY_BTN_LEFT 0x4u
#define MAIN_ARCADE_LINK_POLICY_BTN_RIGHT 0x8u
#define MAIN_ARCADE_LINK_POLICY_BTN_CROSS_ONE 0x10u
#define MAIN_ARCADE_LINK_POLICY_BTN_SQUARE_ONE 0x20u
#define MAIN_ARCADE_LINK_POLICY_BTN_CIRCLE 0x40u
#define MAIN_ARCADE_LINK_POLICY_BTN_R1 0x400u
#define MAIN_ARCADE_LINK_POLICY_BTN_START 0x1000u
#define MAIN_ARCADE_LINK_POLICY_BTN_SELECT 0x2000u
#define MAIN_ARCADE_LINK_POLICY_BTN_TRIANGLE 0x40000u

struct MainArcadeLinkPolicyInput
{
	/* MAIN_ARCADE_LINK_POLICY_MODE_* */
	uint32_t hostMode;
	/* MAIN_ARCADE_LINK_POLICY_TITLE_* (any other value counts as past the intro) */
	uint32_t titleState;
	/* The retail intro frame, already narrowed the way the title code reads it */
	int32_t introFrame;
	/* Local player 0's retail held-button word this frame and last frame */
	uint32_t rawHeld;
	uint32_t prevRawHeld;
	/* 0 or 1: the host reports a link screen other than OFF (LINK), or 1 (PREVIEW) */
	uint8_t hostScreenActive;
	/* 0 or 1: the current level is the main-menu level */
	uint8_t levelIsMainMenu;
	/* 0 or 1: a level load is in progress */
	uint8_t loading;
	/* 0 or 1: the retail main-menu box is the menu the retail menu system
	 * processes this frame, and the main-menu state is the title */
	uint8_t mainMenuBoxActive;
	/* 0 or 1: the main-menu box has a submenu open (never relaxes ownership) */
	uint8_t submenuOpen;
	/* 0 or 1: the hook has hidden the retail main-menu box */
	uint8_t boxHidden;
	/* 0 or 1: the host flow is on RACING (LINK only; the host's racing query,
	 * read before this frame's host tick) */
	uint8_t hostRacing;
	uint8_t reserved[1];
};

struct MainArcadeLinkPolicyOutput
{
	/* NATIVE_ARCADE_MENU_BUTTON_* bits held by local player 0 this frame */
	uint32_t heldButtons;
	/* 1: the layer owns this frame's menu layer (tick, draw, and return 1) */
	uint8_t owns;
	/* 1: LINK only; a rising START or CROSS on the attract screen enters the lobby */
	uint8_t enterPressed;
	/* 1: hide the retail main-menu box (set whenever the layer owns the frame) */
	uint8_t hideBox;
	/* 1: give the hidden retail main-menu box back (the layer no longer owns the frame) */
	uint8_t restoreBox;
	/* 1: reset the retail title demo countdown */
	uint8_t resetDemoCountdown;
	/* 1: clear every player's per-frame taps before the retail menu code runs */
	uint8_t clearTaps;
	/* 1: LINK only; a race frame: tick the host with heldButtons, touch nothing
	 * else, and return 0 (every other output is 0) */
	uint8_t tickOnly;
	uint8_t reserved[1];
};

/* Maps a retail held-button word to NATIVE_ARCADE_MENU_BUTTON_* bits. */
uint32_t MainArcadeLinkPolicy_MapHeld(uint32_t rawHeld);

/*
 * The menu-ready condition: 1 when the frame is in the title window described
 * above (the idle main-menu level, the retail main-menu box is the menu the
 * retail menu system processes, and the title is not in its intro before the
 * menu-ready frame), else 0 (also for NULL). Only the title fields of *input
 * are read; the host mode, pad words, and box flags play no part. Decide
 * uses exactly this rule for the title window, and the internal roster proof
 * (game/MAIN/MainArcadeRosterProof.c) waits for it.
 */
int MainArcadeLinkPolicy_TitleMenuReady(const struct MainArcadeLinkPolicyInput *input);

/*
 * Fills *output (always zeroed first) and returns 1; returns 0, touching
 * nothing, when either pointer is NULL. With host mode OFF, or any unknown
 * mode, every output is 0.
 *
 * Otherwise heldButtons is the mapped rawHeld. Then, first:
 * - tickOnly: LINK, hostRacing (any nonzero value, like the other flags),
 *   and not the idle main-menu level (levelIsMainMenu 0 or loading
 *   nonzero). Then every other output (owns, enterPressed, hideBox,
 *   restoreBox, resetDemoCountdown, clearTaps) is 0, whatever boxHidden
 *   says. Never in PREVIEW, and never without hostRacing.
 * Otherwise tickOnly is 0 and:
 * - owns: the title window (see above), or in LINK mode an active link screen.
 * - hideBox and clearTaps: equal to owns.
 * - restoreBox: not owns, and boxHidden.
 * - enterPressed: LINK, owns, no link screen active, the title is not EXITING
 *   (the demo is already on its way), and START or CROSS was pressed this
 *   frame (held now, not held last frame).
 * - resetDemoCountdown: owns, and PREVIEW (a capture holds steady), an active
 *   link screen, or enterPressed. On the LINK attract screen the countdown
 *   runs as retail.
 */
int MainArcadeLinkPolicy_Decide(const struct MainArcadeLinkPolicyInput *input, struct MainArcadeLinkPolicyOutput *output);

#endif
