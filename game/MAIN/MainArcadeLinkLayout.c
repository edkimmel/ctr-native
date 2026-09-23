#include "MAIN/MainArcadeLinkLayout.h"

#include <stddef.h>
#include <string.h>

/* Geometry in the 512 x 216 retail screen space (section 2.4). */
#define MAIN_ARCADE_LINK_LAYOUT_CENTER_X 256
#define MAIN_ARCADE_LINK_LAYOUT_TITLE_Y 40
#define MAIN_ARCADE_LINK_LAYOUT_BODY1_Y 90
#define MAIN_ARCADE_LINK_LAYOUT_BODY2_Y 110
#define MAIN_ARCADE_LINK_LAYOUT_ROW_REMATCH_Y 120
#define MAIN_ARCADE_LINK_LAYOUT_ROW_EXIT_Y 145
#define MAIN_ARCADE_LINK_LAYOUT_FOOTER_Y 186
#define MAIN_ARCADE_LINK_LAYOUT_PANEL_X 56
#define MAIN_ARCADE_LINK_LAYOUT_PANEL_Y 28
#define MAIN_ARCADE_LINK_LAYOUT_PANEL_W 400
#define MAIN_ARCADE_LINK_LAYOUT_PANEL_H 176
#define MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_X 136
#define MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_ABOVE_ROW 3
#define MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_W 240
#define MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_H 21

/* Select screen geometry (docs/MATCH_SELECT_MILESTONE.md section 2.8): the
 * panel keeps the lobby panel's height and widens to 4..508, so two columns
 * of names fit with a player-marker slot on each side of every name. */
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_X 4
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_W 504
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_TIME_Y 60
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W 252
/* The highlight and the marker slots are inset this far from a cell's
 * edges; the highlight spans the rest of the cell. */
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_INSET 4
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_SMALL_ABOVE_ROW 2
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_SMALL_HIGHLIGHT_H 12
/* Retail per-character advances (the font_charPixWidth data of
 * game/zGlobal_DATA.c; tests/main_arcade_link_layout_isolation_test.cmake
 * checks them): markers are packed against a cell's inset edges, one advance
 * per glyph, so they never reach a centred name. */
#define MAIN_ARCADE_LINK_LAYOUT_BIG_ADVANCE 17
#define MAIN_ARCADE_LINK_LAYOUT_SMALL_ADVANCE 13
/* Opponent footer entries (two or three opponents) are this far apart. */
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_FOOTER_STEP 160
/* The waiting screen: the local picks, then one line per opponent. */
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_Y 68
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_STEP 12
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_PEER_Y 116
#define MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_PEER_STEP 14
/* The result screen. */
#define MAIN_ARCADE_LINK_LAYOUT_RESULT_TRACK_Y 66
#define MAIN_ARCADE_LINK_LAYOUT_RESULT_LAPS_Y 80
#define MAIN_ARCADE_LINK_LAYOUT_RESULT_HUMAN_Y 100
#define MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_STEP 12
#define MAIN_ARCADE_LINK_LAYOUT_RESULT_CPU_GAP 6
/* A bot line holds at most this many characters (468 px of FONT_SMALL). */
#define MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_CHARS 36u

#define MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT 8u
#define MAIN_ARCADE_LINK_LAYOUT_TRACK_COUNT 16u
#define MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT 3u
#define MAIN_ARCADE_LINK_LAYOUT_LEVEL_NAME_COUNT 17u

static const char *const MainArcadeLinkLayout_Dots[4] = {"", ".", "..", "..."};

static const char *const MainArcadeLinkLayout_CabinetLine[2] = {
	"THIS CABINET: CAB 1",
	"THIS CABINET: CAB 2",
};

/*
 * Select-order lists: the order the select screens list each item in, and
 * the order a cursor steps through. They equal the selection rules' tables
 * (the unit test hard-codes them): the base characters by ID; the levelIDs
 * of the game/230/D230.c arcadeTracks rows with unlock 0xFFFF, in retail
 * menu order (tests/main_arcade_link_layout_isolation_test.cmake parses both
 * sides); and the retail lap rows.
 */
static const uint8_t MainArcadeLinkLayout_CharacterOrder[MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT] = {
	0, 1, 2, 3, 4, 5, 6, 7};
static const uint8_t MainArcadeLinkLayout_TrackOrder[MAIN_ARCADE_LINK_LAYOUT_TRACK_COUNT] = {
	3, 6, 4, 14, 9, 2, 8, 0, 5, 1, 12, 10, 15, 7, 11, 16};
static const uint8_t MainArcadeLinkLayout_LapOrder[MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT] = {3, 5, 7};

/* Names by character ID (enum Characters 0..7). ASCII upper case; every
 * glyph is in the retail font map (letters, space, '.', and '\''). */
static const char *const MainArcadeLinkLayout_CharacterNames[MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT] = {
	"CRASH", "CORTEX", "TINY", "COCO", "N. GIN", "DINGODILE", "POLAR", "PURA"};

/* Names by levelID 0..16; NULL for OXIDE_STATION (13), never offered in
 * multiplayer. */
static const char *const MainArcadeLinkLayout_LevelNames[MAIN_ARCADE_LINK_LAYOUT_LEVEL_NAME_COUNT] = {
	"DINGO CANYON", "DRAGON MINES", "BLIZZARD BLUFF", "CRASH COVE", "TIGER TEMPLE", "PAPU'S PYRAMID",
	"ROO'S TUBES", "HOT AIR SKYWAY", "SEWER SPEEDWAY", "MYSTERY CAVES", "CORTEX CASTLE", "N. GIN LABS",
	"POLAR PASS", NULL, "COCO PARK", "TINY ARENA", "SLIDE COLISEUM"};

static const char *const MainArcadeLinkLayout_LapNames[MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT] = {
	"3 LAPS", "5 LAPS", "7 LAPS"};

/* The retail multiplayer colour of human 0..3 (P1..P4). */
static const uint32_t MainArcadeLinkLayout_PlayerColors[MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS] = {
	MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE, MAIN_ARCADE_LINK_COLOR_PLAYER_RED, MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN,
	MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW};

static const char *const MainArcadeLinkLayout_PlayerLabels[MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS] = {
	"P1", "P2", "P3", "P4"};
static const char *const MainArcadeLinkLayout_PlayerDigits[MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS] = {
	"1", "2", "3", "4"};

/* An opponent's progress by its current item, and while not yet heard. */
static const char *const MainArcadeLinkLayout_ProgressLong[4] = {
	"CHOOSING CHARACTER", "VOTING TRACK", "VOTING LAPS", "READY"};
static const char *const MainArcadeLinkLayout_ProgressShort[4] = {"CHARACTER", "TRACK", "LAPS", "READY"};
#define MAIN_ARCADE_LINK_LAYOUT_PROGRESS_LONG_ABSENT "CONNECTING"
#define MAIN_ARCADE_LINK_LAYOUT_PROGRESS_SHORT_ABSENT "JOINING"

static const char *MainArcadeLinkLayout_CharacterName(uint32_t characterID)
{
	return (characterID < MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT) ? MainArcadeLinkLayout_CharacterNames[characterID]
	                                                               : NULL;
}

static const char *MainArcadeLinkLayout_TrackName(uint32_t trackID)
{
	return (trackID < MAIN_ARCADE_LINK_LAYOUT_LEVEL_NAME_COUNT) ? MainArcadeLinkLayout_LevelNames[trackID] : NULL;
}

/* The lap count's list index, or MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT
 * when the count is not an option. */
static uint32_t MainArcadeLinkLayout_LapIndex(uint32_t lapCount)
{
	uint32_t i;

	for (i = 0u; i < MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT; i++)
	{
		if (MainArcadeLinkLayout_LapOrder[i] == lapCount) return i;
	}
	return MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT;
}

static int MainArcadeLinkLayout_ChoiceKnown(uint32_t characterID, uint32_t trackID, uint32_t lapCount)
{
	return (MainArcadeLinkLayout_CharacterName(characterID) != NULL) &&
	       (MainArcadeLinkLayout_TrackName(trackID) != NULL) &&
	       (MainArcadeLinkLayout_LapIndex(lapCount) < MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT);
}

static int MainArcadeLinkLayout_OutcomeValid(const struct MainArcadeLinkLayoutSelect *sel)
{
	uint32_t i;

	if (MainArcadeLinkLayout_TrackName(sel->trackID) == NULL) return 0;
	if (MainArcadeLinkLayout_LapIndex(sel->lapCount) >= MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT) return 0;
	if ((sel->trackDrawn > 1u) || (sel->lapsDrawn > 1u)) return 0;
	if ((sel->characterReassignedMask >> sel->humanCount) != 0u) return 0;
	if ((uint32_t)sel->botCount > (MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS - (uint32_t)sel->humanCount)) return 0;
	for (i = 0u; i < sel->humanCount; i++)
	{
		if (MainArcadeLinkLayout_CharacterName(sel->humanCharacter[i]) == NULL) return 0;
	}
	for (i = 0u; i < sel->botCount; i++)
	{
		if (MainArcadeLinkLayout_CharacterName(sel->botCharacter[i]) == NULL) return 0;
	}
	return 1;
}

static int MainArcadeLinkLayout_SelectValid(const struct MainArcadeLinkLayoutInput *input)
{
	const struct MainArcadeLinkLayoutSelect *sel = &input->select;
	uint32_t h;

	if (sel->active != 1u) return 0;
	if ((sel->humanCount < 1u) || (sel->humanCount > MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS)) return 0;
	if (sel->localHuman >= sel->humanCount) return 0;
	if (sel->currentItem > MAIN_ARCADE_LINK_SELECT_ITEM_DONE) return 0;
	if (sel->status > MAIN_ARCADE_LINK_SELECT_STATUS_FAILED) return 0;
	if (sel->resolved > 1u) return 0;
	if ((input->screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) && (sel->resolved != 1u)) return 0;
	if ((sel->peerLockedCharacterMask >> MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT) != 0u) return 0;
	for (h = 0u; h < sel->humanCount; h++)
	{
		const struct MainArcadeLinkLayoutSelectHuman *human = &sel->humans[h];

		if (human->present > 1u) return 0;
		if (human->present == 0u) continue;
		if (!MainArcadeLinkLayout_ChoiceKnown(human->characterID, human->trackID, human->lapCount)) return 0;
		if ((human->lockMask & ~(MAIN_ARCADE_LINK_SELECT_LOCK_CHARACTER | MAIN_ARCADE_LINK_SELECT_LOCK_TRACK |
									MAIN_ARCADE_LINK_SELECT_LOCK_LAPS)) != 0u)
			return 0;
		if (human->currentItem > MAIN_ARCADE_LINK_SELECT_ITEM_DONE) return 0;
	}
	if (sel->humans[sel->localHuman].present != 1u) return 0;
	if ((sel->resolved == 1u) && !MainArcadeLinkLayout_OutcomeValid(sel)) return 0;
	return 1;
}

static int MainArcadeLinkLayout_InputValid(const struct MainArcadeLinkLayoutInput *input)
{
	if (input->screen > (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT) return 0;
	if (input->lobbyStatus > (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST) return 0;
	if (input->endReason > (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT) return 0;
	if ((input->localCab != 1u) && (input->localCab != 2u)) return 0;
	if (input->rowsEnabled > 1u) return 0;
	if (input->attract > 1u) return 0;
	if ((input->attract == 1u) && (input->screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF)) return 0;
	if (((input->screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT) ||
			(input->screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT)) &&
		!MainArcadeLinkLayout_SelectValid(input))
		return 0;
	return 1;
}

/* Appends src to dest[*length], never writing past the last byte before the
 * terminating NUL. The destination is zero-filled by the caller. */
static void MainArcadeLinkLayout_Append(char *dest, size_t *length, const char *src)
{
	size_t i = 0;
	while ((src[i] != '\0') && (*length < (size_t)MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES - 1u))
	{
		dest[*length] = src[i];
		(*length)++;
		i++;
	}
	dest[*length] = '\0';
}

/* Appends value in decimal, as MainArcadeLinkLayout_Append does. */
static void MainArcadeLinkLayout_AppendUnsigned(char *dest, size_t *length, uint32_t value)
{
	char digits[11];
	size_t count = 0;
	char one[2];

	do
	{
		digits[count] = (char)('0' + (char)(value % 10u));
		count++;
		value /= 10u;
	} while (value != 0u);
	one[1] = '\0';
	while (count > 0u)
	{
		count--;
		one[0] = digits[count];
		MainArcadeLinkLayout_Append(dest, length, one);
	}
}

/* A TEXT item centred on x. */
static void MainArcadeLinkLayout_AddTextAt(struct MainArcadeLinkLayout *layout, uint32_t font, uint32_t color, int x,
	int y, const char *text, const char *suffix)
{
	struct MainArcadeLinkItem *item;
	size_t length = 0;

	if (layout->count >= MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS) return;
	item = &layout->items[layout->count];
	layout->count++;
	item->kind = (uint8_t)MAIN_ARCADE_LINK_ITEM_TEXT;
	item->font = (uint8_t)font;
	item->flags = (uint16_t)(color | MAIN_ARCADE_LINK_JUSTIFY_CENTER);
	item->x = (int16_t)x;
	item->y = (int16_t)y;
	MainArcadeLinkLayout_Append(item->text, &length, text);
	MainArcadeLinkLayout_Append(item->text, &length, suffix);
}

static void MainArcadeLinkLayout_AddText(struct MainArcadeLinkLayout *layout, uint32_t font, uint32_t color, int y,
	const char *text, const char *suffix)
{
	MainArcadeLinkLayout_AddTextAt(layout, font, color, MAIN_ARCADE_LINK_LAYOUT_CENTER_X, y, text, suffix);
}

static void MainArcadeLinkLayout_AddRect(struct MainArcadeLinkLayout *layout, uint32_t kind, int x, int y, int w,
	int h)
{
	struct MainArcadeLinkItem *item;

	if (layout->count >= MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS) return;
	item = &layout->items[layout->count];
	layout->count++;
	item->kind = (uint8_t)kind;
	item->x = (int16_t)x;
	item->y = (int16_t)y;
	item->w = (int16_t)w;
	item->h = (int16_t)h;
}

static void MainArcadeLinkLayout_Title(struct MainArcadeLinkLayout *layout, uint32_t color, const char *text)
{
	MainArcadeLinkLayout_AddText(
		layout, MAIN_ARCADE_LINK_FONT_BIG, color, MAIN_ARCADE_LINK_LAYOUT_TITLE_Y, text, "");
}

static void MainArcadeLinkLayout_Body(
	struct MainArcadeLinkLayout *layout, int y, uint32_t color, const char *text, const char *suffix)
{
	MainArcadeLinkLayout_AddText(layout, MAIN_ARCADE_LINK_FONT_SMALL, color, y, text, suffix);
}

static void MainArcadeLinkLayout_Footer(struct MainArcadeLinkLayout *layout, const char *text)
{
	MainArcadeLinkLayout_AddText(layout, MAIN_ARCADE_LINK_FONT_SMALL, MAIN_ARCADE_LINK_COLOR_ORANGE,
		MAIN_ARCADE_LINK_LAYOUT_FOOTER_Y, text, "");
}

static void MainArcadeLinkLayout_Panel(struct MainArcadeLinkLayout *layout)
{
	MainArcadeLinkLayout_AddRect(layout, MAIN_ARCADE_LINK_ITEM_PANEL, MAIN_ARCADE_LINK_LAYOUT_PANEL_X,
		MAIN_ARCADE_LINK_LAYOUT_PANEL_Y, MAIN_ARCADE_LINK_LAYOUT_PANEL_W, MAIN_ARCADE_LINK_LAYOUT_PANEL_H);
}

static void MainArcadeLinkLayout_Attract(
	struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutInput *input)
{
	const char *cabinet = MainArcadeLinkLayout_CabinetLine[input->localCab - 1u];

	MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "ARCADE LINK");
	if (((input->ticksInScreen / MAIN_ARCADE_LINK_DOT_STEP_TICKS) % 2u) == 0u)
	{
		MainArcadeLinkLayout_Body(
			layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, MAIN_ARCADE_LINK_COLOR_WHITE, "PRESS START", "");
	}
	MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY2_Y, MAIN_ARCADE_LINK_COLOR_WHITE, cabinet, "");
	MainArcadeLinkLayout_Panel(layout);
}

static void MainArcadeLinkLayout_Lobby(
	struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutInput *input, const char *dots)
{
	const char *cabinet = MainArcadeLinkLayout_CabinetLine[input->localCab - 1u];
	const uint32_t white = MAIN_ARCADE_LINK_COLOR_WHITE;

	MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "ARCADE LINK");
	switch (input->lobbyStatus)
	{
	case NATIVE_ARCADE_FLOW_LOBBY_CONNECTING:
	{
		MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, white, "CONNECTING", dots);
		MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY2_Y, white, cabinet, "");
		MainArcadeLinkLayout_Footer(layout, "TRIANGLE: BACK");
		break;
	}
	case NATIVE_ARCADE_FLOW_LOBBY_READY:
	{
		MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, white, "OPPONENT FOUND", "");
		MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY2_Y, white, cabinet, "");
		MainArcadeLinkLayout_Footer(layout, "TRIANGLE: BACK");
		break;
	}
	case NATIVE_ARCADE_FLOW_LOBBY_REJECTED:
	{
		MainArcadeLinkLayout_Body(
			layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, MAIN_ARCADE_LINK_COLOR_RED, "LINK REFUSED", "");
		MainArcadeLinkLayout_Body(
			layout, MAIN_ARCADE_LINK_LAYOUT_BODY2_Y, MAIN_ARCADE_LINK_COLOR_RED, "SETTINGS DO NOT MATCH", "");
		MainArcadeLinkLayout_Footer(layout, "CROSS: RETRY  TRIANGLE: BACK");
		break;
	}
	default:
	{
		/* WAITING or LOST */
		MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, white, "WAITING FOR OPPONENT", dots);
		MainArcadeLinkLayout_Body(layout, MAIN_ARCADE_LINK_LAYOUT_BODY2_Y, white, cabinet, "");
		MainArcadeLinkLayout_Footer(layout, "TRIANGLE: BACK");
		break;
	}
	}
	MainArcadeLinkLayout_Panel(layout);
}

static void MainArcadeLinkLayout_Results(
	struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutInput *input)
{
	const uint32_t rowColor =
		input->rowsEnabled != 0u ? MAIN_ARCADE_LINK_COLOR_ORANGE : MAIN_ARCADE_LINK_COLOR_GRAY;
	const int focusedY = input->selectedRow == NATIVE_ARCADE_FLOW_ROW_EXIT ? MAIN_ARCADE_LINK_LAYOUT_ROW_EXIT_Y :
	                                                                       MAIN_ARCADE_LINK_LAYOUT_ROW_REMATCH_Y;

	switch (input->endReason)
	{
	case NATIVE_ARCADE_FLOW_END_FINISHED:
	{
		MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "RACE COMPLETE");
		break;
	}
	case NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT:
	{
		MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_RED, "OPPONENT DISCONNECTED");
		break;
	}
	case NATIVE_ARCADE_FLOW_END_DESYNC:
	{
		MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_RED, "RACE OUT OF SYNC");
		break;
	}
	case NATIVE_ARCADE_FLOW_END_LINK_ERROR:
	{
		MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_RED, "LINK ERROR");
		break;
	}
	default:
	{
		MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "RESULTS");
		break;
	}
	}
	MainArcadeLinkLayout_AddText(
		layout, MAIN_ARCADE_LINK_FONT_BIG, rowColor, MAIN_ARCADE_LINK_LAYOUT_ROW_REMATCH_Y, "REMATCH", "");
	MainArcadeLinkLayout_AddText(
		layout, MAIN_ARCADE_LINK_FONT_BIG, rowColor, MAIN_ARCADE_LINK_LAYOUT_ROW_EXIT_Y, "EXIT", "");
	MainArcadeLinkLayout_Footer(layout, "CROSS: SELECT");
	if (input->rowsEnabled != 0u)
	{
		MainArcadeLinkLayout_AddRect(layout, MAIN_ARCADE_LINK_ITEM_HIGHLIGHT, MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_X,
			focusedY - MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_ABOVE_ROW, MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_W,
			MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_H);
	}
	MainArcadeLinkLayout_Panel(layout);
}

/* One select item screen's list: how many entries, the grid (filled column
 * by column in list order), and the row font. */
struct MainArcadeLinkLayoutGrid
{
	uint32_t count;
	uint32_t rows;
	/* the left edge of column 0; columns are SELECT_CELL_W wide */
	int cellX;
	/* the top row's y, and the row pitch */
	int rowY;
	int pitch;
	/* the font of the names and of the markers, and its per-glyph advance */
	uint32_t font;
	int advance;
	/* the cursor highlight: this far above the row's y, and this tall */
	int highlightAbove;
	int highlightH;
};

/* Indexed by MAIN_ARCADE_LINK_SELECT_ITEM_CHARACTER, _TRACK, _LAPS. */
static const struct MainArcadeLinkLayoutGrid MainArcadeLinkLayout_Grids[3] = {
	/* 8 characters, 2 columns x 4 rows, FONT_BIG */
	{MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT, 4u, MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_X, 78, 24,
		MAIN_ARCADE_LINK_FONT_BIG, MAIN_ARCADE_LINK_LAYOUT_BIG_ADVANCE, MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_ABOVE_ROW,
		MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_H},
	/* 16 tracks, 2 columns x 8 rows, FONT_SMALL */
	{MAIN_ARCADE_LINK_LAYOUT_TRACK_COUNT, 8u, MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_X, 76, 13,
		MAIN_ARCADE_LINK_FONT_SMALL, MAIN_ARCADE_LINK_LAYOUT_SMALL_ADVANCE,
		MAIN_ARCADE_LINK_LAYOUT_SELECT_SMALL_ABOVE_ROW, MAIN_ARCADE_LINK_LAYOUT_SELECT_SMALL_HIGHLIGHT_H},
	/* 3 lap counts, 1 column centred on the screen, FONT_BIG */
	{MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT, 3u,
		MAIN_ARCADE_LINK_LAYOUT_CENTER_X - (MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W / 2), 86, 25,
		MAIN_ARCADE_LINK_FONT_BIG, MAIN_ARCADE_LINK_LAYOUT_BIG_ADVANCE, MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_ABOVE_ROW,
		MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_H},
};

static const char *const MainArcadeLinkLayout_ItemTitles[3] = {"SELECT CHARACTER", "VOTE TRACK", "VOTE LAPS"};

static const uint8_t MainArcadeLinkLayout_ItemLocks[3] = {
	MAIN_ARCADE_LINK_SELECT_LOCK_CHARACTER, MAIN_ARCADE_LINK_SELECT_LOCK_TRACK, MAIN_ARCADE_LINK_SELECT_LOCK_LAPS};

static void MainArcadeLinkLayout_SelectPanel(struct MainArcadeLinkLayout *layout)
{
	MainArcadeLinkLayout_AddRect(layout, MAIN_ARCADE_LINK_ITEM_PANEL, MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_X,
		MAIN_ARCADE_LINK_LAYOUT_PANEL_Y, MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_W, MAIN_ARCADE_LINK_LAYOUT_PANEL_H);
}

/* The list index of a human's value for an item (CHARACTER, TRACK, or LAPS).
 * Validation guarantees every value is in its list. */
static uint32_t MainArcadeLinkLayout_ItemIndex(uint32_t item, const struct MainArcadeLinkLayoutSelectHuman *human)
{
	const uint8_t *order = MainArcadeLinkLayout_CharacterOrder;
	uint32_t count = MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT;
	uint32_t value = human->characterID;
	uint32_t i;

	if (item == MAIN_ARCADE_LINK_SELECT_ITEM_TRACK)
	{
		order = MainArcadeLinkLayout_TrackOrder;
		count = MAIN_ARCADE_LINK_LAYOUT_TRACK_COUNT;
		value = human->trackID;
	}
	else if (item == MAIN_ARCADE_LINK_SELECT_ITEM_LAPS)
	{
		order = MainArcadeLinkLayout_LapOrder;
		count = MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT;
		value = human->lapCount;
	}
	for (i = 0u; i < count; i++)
	{
		if (order[i] == value) return i;
	}
	return 0u;
}

static const char *MainArcadeLinkLayout_ItemName(uint32_t item, uint32_t index)
{
	if (item == MAIN_ARCADE_LINK_SELECT_ITEM_TRACK)
		return MainArcadeLinkLayout_TrackName(MainArcadeLinkLayout_TrackOrder[index]);
	if (item == MAIN_ARCADE_LINK_SELECT_ITEM_LAPS) return MainArcadeLinkLayout_LapNames[index];
	return MainArcadeLinkLayout_CharacterName(MainArcadeLinkLayout_CharacterOrder[index]);
}

/* A list entry's colour: a character a peer has locked is GRAY (taken); a
 * track or lap count that some human has locked as a vote is WHITE; every
 * other entry is ORANGE, like a retail menu row. */
static uint32_t MainArcadeLinkLayout_ItemColor(
	const struct MainArcadeLinkLayoutSelect *sel, uint32_t item, uint32_t index)
{
	uint32_t h;

	if (item == MAIN_ARCADE_LINK_SELECT_ITEM_CHARACTER)
	{
		return (((uint32_t)sel->peerLockedCharacterMask >> MainArcadeLinkLayout_CharacterOrder[index]) & 1u) != 0u
		           ? MAIN_ARCADE_LINK_COLOR_GRAY
		           : MAIN_ARCADE_LINK_COLOR_ORANGE;
	}
	for (h = 0u; h < sel->humanCount; h++)
	{
		const struct MainArcadeLinkLayoutSelectHuman *human = &sel->humans[h];

		if ((human->present == 1u) && ((human->lockMask & MainArcadeLinkLayout_ItemLocks[item]) != 0u) &&
			(MainArcadeLinkLayout_ItemIndex(item, human) == index))
			return MAIN_ARCADE_LINK_COLOR_WHITE;
	}
	return MAIN_ARCADE_LINK_COLOR_ORANGE;
}

/* The centre of human h's marker in a cell whose left edge is x0, for a font
 * with the given advance. With one or two humans the marker is "P<n>" (two
 * glyphs): P1 against the cell's left inset edge and P2 against its right.
 * With three or four the marker is the digit alone (one glyph), paired on
 * each side: 1 then 3 from the left edge, 2 then 4 up to the right edge. A
 * centred marker of width w starts w / 2 (rounded down) left of its centre,
 * so a one-glyph marker ends advance - advance / 2 right of it. */
static int MainArcadeLinkLayout_MarkerX(int x0, uint32_t human, uint32_t humanCount, int advance)
{
	const int left = x0 + MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_INSET;
	const int right = x0 + MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W - MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_INSET;
	const int glyphRight = advance - (advance / 2);

	if (humanCount <= 2u)
	{
		return (human == 0u) ? (left + advance) : (right - advance);
	}
	switch (human)
	{
	case 0u:
		return left + (advance / 2);
	case 1u:
		return right - advance - glyphRight;
	case 2u:
		return left + advance + (advance / 2);
	default:
		return right - glyphRight;
	}
}

/* "TIME <s>", s = ceil(ticksLeft / 30). */
static void MainArcadeLinkLayout_Countdown(struct MainArcadeLinkLayout *layout, uint32_t ticksLeft)
{
	char text[MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES];
	size_t length = 0;
	uint32_t seconds = ticksLeft / MAIN_ARCADE_LINK_SELECT_TICKS_PER_SECOND;

	if ((ticksLeft % MAIN_ARCADE_LINK_SELECT_TICKS_PER_SECOND) != 0u) seconds++;
	memset(text, 0, sizeof(text));
	MainArcadeLinkLayout_Append(text, &length, "TIME ");
	MainArcadeLinkLayout_AppendUnsigned(text, &length, seconds);
	MainArcadeLinkLayout_AddText(layout, MAIN_ARCADE_LINK_FONT_SMALL, MAIN_ARCADE_LINK_COLOR_WHITE,
		MAIN_ARCADE_LINK_LAYOUT_SELECT_TIME_Y, text, "");
}

/* Each opponent's progress on the footer line, in its player colour: one
 * opponent as "P2: VOTING TRACK", two or three as short entries ("P2
 * TRACK") SELECT_FOOTER_STEP apart, centred as a group. */
static void MainArcadeLinkLayout_SelectFooter(
	struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutSelect *sel)
{
	const int opponents = (int)sel->humanCount - 1;
	int k = 0;
	uint32_t h;

	for (h = 0u; h < sel->humanCount; h++)
	{
		const struct MainArcadeLinkLayoutSelectHuman *human = &sel->humans[h];
		char text[MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES];
		size_t length = 0;
		int x = MAIN_ARCADE_LINK_LAYOUT_CENTER_X;

		if (h == sel->localHuman) continue;
		memset(text, 0, sizeof(text));
		MainArcadeLinkLayout_Append(text, &length, MainArcadeLinkLayout_PlayerLabels[h]);
		if (opponents == 1)
		{
			MainArcadeLinkLayout_Append(text, &length, ": ");
			MainArcadeLinkLayout_Append(text, &length,
				(human->present == 1u) ? MainArcadeLinkLayout_ProgressLong[human->currentItem]
									   : MAIN_ARCADE_LINK_LAYOUT_PROGRESS_LONG_ABSENT);
		}
		else
		{
			MainArcadeLinkLayout_Append(text, &length, " ");
			MainArcadeLinkLayout_Append(text, &length,
				(human->present == 1u) ? MainArcadeLinkLayout_ProgressShort[human->currentItem]
									   : MAIN_ARCADE_LINK_LAYOUT_PROGRESS_SHORT_ABSENT);
			x += (((2 * k) - (opponents - 1)) * MAIN_ARCADE_LINK_LAYOUT_SELECT_FOOTER_STEP) / 2;
		}
		MainArcadeLinkLayout_AddTextAt(layout, MAIN_ARCADE_LINK_FONT_SMALL, MainArcadeLinkLayout_PlayerColors[h], x,
			MAIN_ARCADE_LINK_LAYOUT_FOOTER_Y, text, "");
		k++;
	}
}

/*
 * SELECT with the local human on CHARACTER, TRACK, or LAPS: the title, the
 * countdown, every list entry in its grid cell, a marker for each present
 * human at or past this item on the cell of its cursor (or locked value),
 * the opponents' progress, then the retail row highlight on the local
 * cursor's cell and the panel.
 */
static void MainArcadeLinkLayout_SelectItem(struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutSelect *sel)
{
	const uint32_t item = sel->currentItem;
	const struct MainArcadeLinkLayoutGrid *grid = &MainArcadeLinkLayout_Grids[item];
	uint32_t index;
	uint32_t h;
	int x0;
	int y;

	MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_ORANGE, MainArcadeLinkLayout_ItemTitles[item]);
	MainArcadeLinkLayout_Countdown(layout, sel->ticksLeft);
	for (index = 0u; index < grid->count; index++)
	{
		x0 = grid->cellX + ((int)(index / grid->rows) * MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W);
		y = grid->rowY + ((int)(index % grid->rows) * grid->pitch);
		MainArcadeLinkLayout_AddTextAt(layout, grid->font, MainArcadeLinkLayout_ItemColor(sel, item, index),
			x0 + (MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W / 2), y, MainArcadeLinkLayout_ItemName(item, index), "");
	}
	for (h = 0u; h < sel->humanCount; h++)
	{
		const struct MainArcadeLinkLayoutSelectHuman *human = &sel->humans[h];

		if ((human->present != 1u) || (human->currentItem < item)) continue;
		index = MainArcadeLinkLayout_ItemIndex(item, human);
		x0 = grid->cellX + ((int)(index / grid->rows) * MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W);
		y = grid->rowY + ((int)(index % grid->rows) * grid->pitch);
		MainArcadeLinkLayout_AddTextAt(layout, grid->font, MainArcadeLinkLayout_PlayerColors[h],
			MainArcadeLinkLayout_MarkerX(x0, h, sel->humanCount, grid->advance), y,
			(sel->humanCount <= 2u) ? MainArcadeLinkLayout_PlayerLabels[h] : MainArcadeLinkLayout_PlayerDigits[h], "");
	}
	MainArcadeLinkLayout_SelectFooter(layout, sel);
	index = MainArcadeLinkLayout_ItemIndex(item, &sel->humans[sel->localHuman]);
	x0 = grid->cellX + ((int)(index / grid->rows) * MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W);
	y = grid->rowY + ((int)(index % grid->rows) * grid->pitch);
	MainArcadeLinkLayout_AddRect(layout, MAIN_ARCADE_LINK_ITEM_HIGHLIGHT, x0 + MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_INSET,
		y - grid->highlightAbove, MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W - (2 * MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_INSET),
		grid->highlightH);
	MainArcadeLinkLayout_SelectPanel(layout);
}

/*
 * SELECT with the local human DONE: a waiting title with the dot animation,
 * the local picks, and each opponent's progress with its live cursor.
 */
static void MainArcadeLinkLayout_SelectWait(
	struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutSelect *sel, const char *dots)
{
	const struct MainArcadeLinkLayoutSelectHuman *local = &sel->humans[sel->localHuman];
	const uint32_t white = MAIN_ARCADE_LINK_COLOR_WHITE;
	const uint32_t small = MAIN_ARCADE_LINK_FONT_SMALL;
	char text[MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES];
	size_t length = 0;
	int k = 0;
	int y = MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_Y;
	uint32_t h;

	memset(text, 0, sizeof(text));
	MainArcadeLinkLayout_Append(text, &length, "WAITING FOR ");
	MainArcadeLinkLayout_Append(text, &length,
		(sel->humanCount == 2u) ? MainArcadeLinkLayout_PlayerLabels[1u - sel->localHuman] : "PLAYERS");
	MainArcadeLinkLayout_AddText(
		layout, MAIN_ARCADE_LINK_FONT_BIG, MAIN_ARCADE_LINK_COLOR_ORANGE, MAIN_ARCADE_LINK_LAYOUT_TITLE_Y, text, dots);

	MainArcadeLinkLayout_AddText(
		layout, small, white, y, "YOUR CHARACTER: ", MainArcadeLinkLayout_CharacterName(local->characterID));
	y += MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_STEP;
	MainArcadeLinkLayout_AddText(layout, small, white, y, "YOUR TRACK VOTE: ", MainArcadeLinkLayout_TrackName(local->trackID));
	y += MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_STEP;
	MainArcadeLinkLayout_AddText(layout, small, white, y, "YOUR LAP VOTE: ",
		MainArcadeLinkLayout_LapNames[MainArcadeLinkLayout_LapIndex(local->lapCount)]);

	for (h = 0u; h < sel->humanCount; h++)
	{
		const struct MainArcadeLinkLayoutSelectHuman *human = &sel->humans[h];

		if (h == sel->localHuman) continue;
		memset(text, 0, sizeof(text));
		length = 0;
		MainArcadeLinkLayout_Append(text, &length, MainArcadeLinkLayout_PlayerLabels[h]);
		MainArcadeLinkLayout_Append(text, &length, ": ");
		if (human->present != 1u)
		{
			MainArcadeLinkLayout_Append(text, &length, MAIN_ARCADE_LINK_LAYOUT_PROGRESS_LONG_ABSENT);
		}
		else
		{
			MainArcadeLinkLayout_Append(text, &length, MainArcadeLinkLayout_ProgressLong[human->currentItem]);
			if (human->currentItem < MAIN_ARCADE_LINK_SELECT_ITEM_DONE)
			{
				MainArcadeLinkLayout_Append(text, &length, "  ");
				MainArcadeLinkLayout_Append(text, &length,
					MainArcadeLinkLayout_ItemName(
						human->currentItem, MainArcadeLinkLayout_ItemIndex(human->currentItem, human)));
			}
		}
		MainArcadeLinkLayout_AddText(layout, small, MainArcadeLinkLayout_PlayerColors[h],
			MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_PEER_Y + (k * MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_PEER_STEP), text, "");
		k++;
	}
	MainArcadeLinkLayout_SelectPanel(layout);
}

/*
 * SELECT_RESULT: the agreed track and lap count (" - RANDOM" when drawn),
 * each human's character in its player colour (" - REASSIGNED" when its
 * pick was taken), the bots on wrapped "CPU" lines, and GET READY.
 */
static void MainArcadeLinkLayout_SelectResult(
	struct MainArcadeLinkLayout *layout, const struct MainArcadeLinkLayoutSelect *sel)
{
	const uint32_t white = MAIN_ARCADE_LINK_COLOR_WHITE;
	const uint32_t small = MAIN_ARCADE_LINK_FONT_SMALL;
	char text[MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES];
	size_t length = 0;
	uint32_t h;
	uint32_t b;
	int y;

	MainArcadeLinkLayout_Title(layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "MATCH SET");

	memset(text, 0, sizeof(text));
	MainArcadeLinkLayout_Append(text, &length, "TRACK ");
	MainArcadeLinkLayout_Append(text, &length, MainArcadeLinkLayout_TrackName(sel->trackID));
	MainArcadeLinkLayout_AddText(
		layout, small, white, MAIN_ARCADE_LINK_LAYOUT_RESULT_TRACK_Y, text, (sel->trackDrawn == 1u) ? " - RANDOM" : "");

	memset(text, 0, sizeof(text));
	length = 0;
	MainArcadeLinkLayout_Append(text, &length, "LAPS ");
	MainArcadeLinkLayout_AppendUnsigned(text, &length, sel->lapCount);
	MainArcadeLinkLayout_AddText(
		layout, small, white, MAIN_ARCADE_LINK_LAYOUT_RESULT_LAPS_Y, text, (sel->lapsDrawn == 1u) ? " - RANDOM" : "");

	y = MAIN_ARCADE_LINK_LAYOUT_RESULT_HUMAN_Y;
	for (h = 0u; h < sel->humanCount; h++)
	{
		memset(text, 0, sizeof(text));
		length = 0;
		MainArcadeLinkLayout_Append(text, &length, MainArcadeLinkLayout_PlayerLabels[h]);
		MainArcadeLinkLayout_Append(text, &length, " ");
		MainArcadeLinkLayout_Append(text, &length, MainArcadeLinkLayout_CharacterName(sel->humanCharacter[h]));
		MainArcadeLinkLayout_AddText(layout, small, MainArcadeLinkLayout_PlayerColors[h], y, text,
			(((uint32_t)sel->characterReassignedMask >> h) & 1u) != 0u ? " - REASSIGNED" : "");
		y += MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_STEP;
	}

	/* The bots: "CPU " and the names joined by ", ", wrapped onto a new line
	 * whenever the next name would pass RESULT_LINE_CHARS. */
	y += MAIN_ARCADE_LINK_LAYOUT_RESULT_CPU_GAP;
	if (sel->botCount > 0u)
	{
		memset(text, 0, sizeof(text));
		length = 0;
		MainArcadeLinkLayout_Append(text, &length, "CPU ");
		for (b = 0u; b < sel->botCount; b++)
		{
			const char *name = MainArcadeLinkLayout_CharacterName(sel->botCharacter[b]);

			if (b != 0u)
			{
				if (length + 2u + strlen(name) > MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_CHARS)
				{
					MainArcadeLinkLayout_AddText(layout, small, white, y, text, "");
					y += MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_STEP;
					memset(text, 0, sizeof(text));
					length = 0;
				}
				else
				{
					MainArcadeLinkLayout_Append(text, &length, ", ");
				}
			}
			MainArcadeLinkLayout_Append(text, &length, name);
		}
		MainArcadeLinkLayout_AddText(layout, small, white, y, text, "");
	}

	MainArcadeLinkLayout_Footer(layout, "GET READY");
	MainArcadeLinkLayout_SelectPanel(layout);
}

int MainArcadeLinkLayout_Build(const struct MainArcadeLinkLayoutInput *input, struct MainArcadeLinkLayout *out)
{
	struct MainArcadeLinkLayout layout;
	const char *dots;

	if ((input == NULL) || (out == NULL)) return 0;
	if (!MainArcadeLinkLayout_InputValid(input)) return 0;

	memset(&layout, 0, sizeof(layout));
	dots = MainArcadeLinkLayout_Dots[(input->ticksInScreen / MAIN_ARCADE_LINK_DOT_STEP_TICKS) % 4u];

	switch (input->screen)
	{
	case NATIVE_ARCADE_FLOW_SCREEN_OFF:
	{
		if (input->attract == 1u) MainArcadeLinkLayout_Attract(&layout, input);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_LOBBY:
	{
		MainArcadeLinkLayout_Lobby(&layout, input, dots);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND:
	{
		MainArcadeLinkLayout_Title(&layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "ARCADE LINK");
		MainArcadeLinkLayout_Body(
			&layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, MAIN_ARCADE_LINK_COLOR_WHITE, "OPPONENT FOUND", "");
		MainArcadeLinkLayout_Body(
			&layout, MAIN_ARCADE_LINK_LAYOUT_BODY2_Y, MAIN_ARCADE_LINK_COLOR_WHITE, "GET READY", "");
		MainArcadeLinkLayout_Panel(&layout);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_RESULTS:
	{
		MainArcadeLinkLayout_Results(&layout, input);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT:
	{
		MainArcadeLinkLayout_Title(&layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "REMATCH");
		MainArcadeLinkLayout_Body(
			&layout, MAIN_ARCADE_LINK_LAYOUT_BODY1_Y, MAIN_ARCADE_LINK_COLOR_WHITE, "WAITING FOR OPPONENT", dots);
		MainArcadeLinkLayout_Footer(&layout, "TRIANGLE: CANCEL");
		MainArcadeLinkLayout_Panel(&layout);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_EXIT:
	{
		if (input->endReason == (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT)
			MainArcadeLinkLayout_Title(&layout, MAIN_ARCADE_LINK_COLOR_RED, "OPPONENT LEFT");
		else
			MainArcadeLinkLayout_Title(&layout, MAIN_ARCADE_LINK_COLOR_ORANGE, "THANKS FOR PLAYING");
		MainArcadeLinkLayout_Panel(&layout);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT:
	{
		if (input->select.currentItem == MAIN_ARCADE_LINK_SELECT_ITEM_DONE)
			MainArcadeLinkLayout_SelectWait(&layout, &input->select, dots);
		else
			MainArcadeLinkLayout_SelectItem(&layout, &input->select);
		break;
	}
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT:
	{
		MainArcadeLinkLayout_SelectResult(&layout, &input->select);
		break;
	}
	default:
	{
		/* RACING: the race HUD owns the screen. */
		break;
	}
	}

	memcpy(out, &layout, sizeof(layout));
	return 1;
}

#undef MAIN_ARCADE_LINK_LAYOUT_CENTER_X
#undef MAIN_ARCADE_LINK_LAYOUT_TITLE_Y
#undef MAIN_ARCADE_LINK_LAYOUT_BODY1_Y
#undef MAIN_ARCADE_LINK_LAYOUT_BODY2_Y
#undef MAIN_ARCADE_LINK_LAYOUT_ROW_REMATCH_Y
#undef MAIN_ARCADE_LINK_LAYOUT_ROW_EXIT_Y
#undef MAIN_ARCADE_LINK_LAYOUT_FOOTER_Y
#undef MAIN_ARCADE_LINK_LAYOUT_PANEL_X
#undef MAIN_ARCADE_LINK_LAYOUT_PANEL_Y
#undef MAIN_ARCADE_LINK_LAYOUT_PANEL_W
#undef MAIN_ARCADE_LINK_LAYOUT_PANEL_H
#undef MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_X
#undef MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_ABOVE_ROW
#undef MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_W
#undef MAIN_ARCADE_LINK_LAYOUT_HIGHLIGHT_H
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_X
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_PANEL_W
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_TIME_Y
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_W
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_CELL_INSET
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_SMALL_ABOVE_ROW
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_SMALL_HIGHLIGHT_H
#undef MAIN_ARCADE_LINK_LAYOUT_BIG_ADVANCE
#undef MAIN_ARCADE_LINK_LAYOUT_SMALL_ADVANCE
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_FOOTER_STEP
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_Y
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_OWN_STEP
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_PEER_Y
#undef MAIN_ARCADE_LINK_LAYOUT_SELECT_WAIT_PEER_STEP
#undef MAIN_ARCADE_LINK_LAYOUT_RESULT_TRACK_Y
#undef MAIN_ARCADE_LINK_LAYOUT_RESULT_LAPS_Y
#undef MAIN_ARCADE_LINK_LAYOUT_RESULT_HUMAN_Y
#undef MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_STEP
#undef MAIN_ARCADE_LINK_LAYOUT_RESULT_CPU_GAP
#undef MAIN_ARCADE_LINK_LAYOUT_RESULT_LINE_CHARS
#undef MAIN_ARCADE_LINK_LAYOUT_CHARACTER_COUNT
#undef MAIN_ARCADE_LINK_LAYOUT_TRACK_COUNT
#undef MAIN_ARCADE_LINK_LAYOUT_LAP_OPTION_COUNT
#undef MAIN_ARCADE_LINK_LAYOUT_LEVEL_NAME_COUNT
#undef MAIN_ARCADE_LINK_LAYOUT_PROGRESS_LONG_ABSENT
#undef MAIN_ARCADE_LINK_LAYOUT_PROGRESS_SHORT_ABSENT
