#ifndef MAIN_ARCADE_LINK_LAYOUT_H
#define MAIN_ARCADE_LINK_LAYOUT_H

#include "platform/native_arcade_flow.h"

#include <stdint.h>

/*
 * Arcade-link screen layout builder (docs/GAME_LOOP_UI_MILESTONE.md section
 * 2.4). Turns the arcade flow's view (screen, lobby status, end reason,
 * focused row, ticks in screen, local cabinet) into a fixed-capacity draw
 * list in the 512 x 216 retail screen space. It draws nothing and reads no
 * game state; the drawer walks the list in order.
 *
 * Item order is the retail menu draw order: every TEXT item top to bottom,
 * then the focused-row HIGHLIGHT box (results screen only, and only while
 * the menu accepts input), then exactly one PANEL (the menu frame).
 *
 * Pure: caller-owned output, no heap use, no I/O, no mutable state, and
 * fully deterministic. Unused bytes of every item and every unused item are
 * zero, so two builds of the same input compare equal byte for byte.
 */

/* Mirrors of retail values from include/namespace_Decal.h; Task 6 static-asserts they match. */
#define MAIN_ARCADE_LINK_FONT_BIG 1u
#define MAIN_ARCADE_LINK_FONT_SMALL 2u
#define MAIN_ARCADE_LINK_COLOR_ORANGE 0u
#define MAIN_ARCADE_LINK_COLOR_RED 3u
#define MAIN_ARCADE_LINK_COLOR_WHITE 4u
#define MAIN_ARCADE_LINK_COLOR_GRAY 23u
#define MAIN_ARCADE_LINK_JUSTIFY_CENTER 0x8000u

#define MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS 10u
#define MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES 40u
#define MAIN_ARCADE_LINK_DOT_STEP_TICKS 15u

enum MainArcadeLinkItemKind
{
	MAIN_ARCADE_LINK_ITEM_TEXT = 1,
	MAIN_ARCADE_LINK_ITEM_HIGHLIGHT = 2,
	MAIN_ARCADE_LINK_ITEM_PANEL = 3
};

struct MainArcadeLinkItem
{
	/* enum MainArcadeLinkItemKind */
	uint8_t kind;
	/* TEXT only: MAIN_ARCADE_LINK_FONT_* */
	uint8_t font;
	/* TEXT only: colour | MAIN_ARCADE_LINK_JUSTIFY_CENTER */
	uint16_t flags;
	/* TEXT: x,y anchor (w,h zero); HIGHLIGHT/PANEL: rectangle */
	int16_t x, y, w, h;
	/* TEXT only, NUL-terminated ASCII upper case */
	char text[MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES];
};

struct MainArcadeLinkLayout
{
	uint32_t count;
	struct MainArcadeLinkItem items[MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS];
};

struct MainArcadeLinkLayoutInput
{
	/* enum NativeArcadeFlowScreen */
	uint32_t screen;
	/* enum NativeArcadeFlowLobbyStatus */
	uint32_t lobbyStatus;
	/* enum NativeArcadeFlowEndReason */
	uint32_t endReason;
	/* NATIVE_ARCADE_FLOW_ROW_*; any value other than ROW_EXIT means
	 * ROW_REMATCH */
	uint32_t selectedRow;
	uint32_t ticksInScreen;
	/* 1 or 2 */
	uint8_t localCab;
	/* 0 or 1: the menu is accepting input */
	uint8_t rowsEnabled;
	/* 0 or 1: the title-screen attract layout; valid only with screen OFF */
	uint8_t attract;
	uint8_t reserved;
};

/* Builds the draw list for one tick. Returns 1 on success. Returns 0 with
 * *out untouched on NULL arguments or invalid input (screen above EXIT,
 * lobbyStatus above LOST, endReason above OPPONENT_LEFT, localCab not 1 or 2,
 * rowsEnabled or attract above 1, or attract 1 with a screen other than
 * OFF). Screens OFF (without attract) and RACING give count 0. */
int MainArcadeLinkLayout_Build(const struct MainArcadeLinkLayoutInput *input, struct MainArcadeLinkLayout *out);

#endif
