#ifndef MAIN_ARCADE_LINK_LAYOUT_H
#define MAIN_ARCADE_LINK_LAYOUT_H

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host.h"

#include <stdint.h>

/*
 * Arcade-link screen layout builder (docs/GAME_LOOP_UI_MILESTONE.md section
 * 2.4). Turns the arcade flow's view (screen, lobby status, end reason,
 * focused row, ticks in screen, local cabinet) into a fixed-capacity draw
 * list in the 512 x 216 retail screen space. It draws nothing and reads no
 * game state; the drawer walks the list in order.
 *
 * Item order is the retail menu draw order: every TEXT item top to bottom,
 * then the focused-row HIGHLIGHT box (the results screen while the menu
 * accepts input, and the local cursor on the select item screens), then
 * exactly one PANEL (the menu frame).
 *
 * The match-select screens (docs/MATCH_SELECT_MILESTONE.md section 2.8)
 * read the layout-owned select fields below, which
 * MainArcadeLinkLayout_InputFromHostView fills field for field from the
 * host's view. The layout owns its own name tables and select-order lists
 * (characters 0..7; the 16 base multiplayer tracks in retail menu order;
 * laps 3, 5, 7) and never names the selection modules. From the host glue
 * header it uses only the plain view types and value names; it calls no
 * host function and does not link the host library.
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
#define MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE 24u
#define MAIN_ARCADE_LINK_COLOR_PLAYER_RED 25u
#define MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN 26u
#define MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW 27u
#define MAIN_ARCADE_LINK_JUSTIFY_CENTER 0x8000u

#define MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS 32u
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

/* The select screens' capacities and values. Each is static-asserted below,
 * next to MainArcadeLinkLayout_InputFromHostView, against the host name it
 * mirrors (NATIVE_ARCADE_LINK_HOST_*). */
#define MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS 4u
#define MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS 8u
#define MAIN_ARCADE_LINK_SELECT_ITEM_CHARACTER 0u
#define MAIN_ARCADE_LINK_SELECT_ITEM_TRACK 1u
#define MAIN_ARCADE_LINK_SELECT_ITEM_LAPS 2u
#define MAIN_ARCADE_LINK_SELECT_ITEM_DONE 3u
#define MAIN_ARCADE_LINK_SELECT_LOCK_CHARACTER 0x1u
#define MAIN_ARCADE_LINK_SELECT_LOCK_TRACK 0x2u
#define MAIN_ARCADE_LINK_SELECT_LOCK_LAPS 0x4u
#define MAIN_ARCADE_LINK_SELECT_STATUS_FAILED 4u
/* Game-loop ticks per second: the countdown shows ceil(ticksLeft / 30). */
#define MAIN_ARCADE_LINK_SELECT_TICKS_PER_SECOND 30u

/* One human on the select screens: the local human's own state, or a
 * peer's latest state. Ignored while present is 0. */
struct MainArcadeLinkLayoutSelectHuman
{
	/* 0 or 1 */
	uint8_t present;
	/* cursor or locked value: a base character, 0..7 */
	uint8_t characterID;
	/* cursor or locked vote: a base multiplayer track levelID */
	uint8_t trackID;
	/* cursor or locked vote: 3, 5, or 7 */
	uint8_t lapCount;
	/* MAIN_ARCADE_LINK_SELECT_LOCK_* bits */
	uint8_t lockMask;
	/* MAIN_ARCADE_LINK_SELECT_ITEM_* */
	uint8_t currentItem;
	uint8_t reserved[2];
};

/* The select phase, read only on the SELECT and SELECT_RESULT screens. */
struct MainArcadeLinkLayoutSelect
{
	/* 1 while a select exists */
	uint8_t active;
	/* 1..4 */
	uint8_t humanCount;
	/* the local human's index, below humanCount */
	uint8_t localHuman;
	/* the local human's MAIN_ARCADE_LINK_SELECT_ITEM_*: picks the screen */
	uint8_t currentItem;
	/* ticks until the local current item auto-locks */
	uint32_t ticksLeft;
	/* 0..MAIN_ARCADE_LINK_SELECT_STATUS_FAILED */
	uint8_t status;
	/* 1 when the outcome fields are valid */
	uint8_t resolved;
	/* outcome: the track levelID and the lap count */
	uint8_t trackID;
	uint8_t lapCount;
	/* outcome: 1 when the track or the lap count came from a tie draw */
	uint8_t trackDrawn;
	uint8_t lapsDrawn;
	/* outcome: bit h set when human h was reassigned a character */
	uint8_t characterReassignedMask;
	/* outcome: bots in botCharacter, at most 8 - humanCount */
	uint8_t botCount;
	/* outcome: the first humanCount entries are used */
	uint8_t humanCharacter[MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS];
	/* outcome: the first botCount entries are used */
	uint8_t botCharacter[MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS];
	/* bit c: a peer has locked base character c (drawn GRAY) */
	uint16_t peerLockedCharacterMask;
	uint8_t reserved[2];
	/* indexed by human; entries at or above humanCount are ignored */
	struct MainArcadeLinkLayoutSelectHuman humans[MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS];
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
	/* SELECT and SELECT_RESULT only; ignored on every other screen */
	struct MainArcadeLinkLayoutSelect select;
};

/* Builds the draw list for one tick. Returns 1 on success. Returns 0 with
 * *out untouched on NULL arguments or invalid input (screen above
 * SELECT_RESULT, lobbyStatus above LOST, endReason above OPPONENT_LEFT,
 * localCab not 1 or 2, rowsEnabled or attract above 1, or attract 1 with a
 * screen other than OFF). On SELECT and SELECT_RESULT the select fields must
 * also be valid: active 1; humanCount 1..4; localHuman below humanCount and
 * present; currentItem, status, resolved, and the drawn flags in range
 * (SELECT_RESULT needs resolved 1); peerLockedCharacterMask within the 8
 * base characters; every present human below humanCount with present 0 or
 * 1, a character, track, and lap count the name tables know, lockMask within
 * the three lock bits, and currentItem in range; and, when resolved, a known
 * track and lap count, a reassignment mask within humanCount, known human
 * characters, and botCount at most 8 - humanCount with known bot
 * characters. Screens OFF (without attract) and RACING give count 0. */
int MainArcadeLinkLayout_Build(const struct MainArcadeLinkLayoutInput *input, struct MainArcadeLinkLayout *out);

/* The layout's select fields mirror the host's select view; keep the
 * capacities and values in step so the field-for-field copy below is
 * exact. */
_Static_assert(MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS == NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS, "MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS must match NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS");
_Static_assert(MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS == NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS, "MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS must match NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS");
_Static_assert(MAIN_ARCADE_LINK_SELECT_ITEM_CHARACTER == NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER, "MAIN_ARCADE_LINK_SELECT_ITEM_CHARACTER must match NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER");
_Static_assert(MAIN_ARCADE_LINK_SELECT_ITEM_TRACK == NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK, "MAIN_ARCADE_LINK_SELECT_ITEM_TRACK must match NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK");
_Static_assert(MAIN_ARCADE_LINK_SELECT_ITEM_LAPS == NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS, "MAIN_ARCADE_LINK_SELECT_ITEM_LAPS must match NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS");
_Static_assert(MAIN_ARCADE_LINK_SELECT_ITEM_DONE == NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE, "MAIN_ARCADE_LINK_SELECT_ITEM_DONE must match NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE");
_Static_assert(MAIN_ARCADE_LINK_SELECT_LOCK_CHARACTER == NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER, "MAIN_ARCADE_LINK_SELECT_LOCK_CHARACTER must match NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER");
_Static_assert(MAIN_ARCADE_LINK_SELECT_LOCK_TRACK == NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK, "MAIN_ARCADE_LINK_SELECT_LOCK_TRACK must match NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK");
_Static_assert(MAIN_ARCADE_LINK_SELECT_LOCK_LAPS == NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS, "MAIN_ARCADE_LINK_SELECT_LOCK_LAPS must match NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS");
_Static_assert(MAIN_ARCADE_LINK_SELECT_STATUS_FAILED == NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_FAILED, "MAIN_ARCADE_LINK_SELECT_STATUS_FAILED must match NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_FAILED");

/* Fills *input from the host's view, field for field: the screen fields and
 * every select field, with every reserved byte zero. The result is exactly
 * what the drawer hands MainArcadeLinkLayout_Build. Returns 1 on success;
 * returns 0 with *input untouched when either argument is NULL. Pure: it
 * validates nothing (MainArcadeLinkLayout_Build does). */
int MainArcadeLinkLayout_InputFromHostView(const struct NativeArcadeLinkHostView *view, struct MainArcadeLinkLayoutInput *input);

#endif
