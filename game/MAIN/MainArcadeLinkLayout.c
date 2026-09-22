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

static const char *const MainArcadeLinkLayout_Dots[4] = {"", ".", "..", "..."};

static const char *const MainArcadeLinkLayout_CabinetLine[2] = {
	"THIS CABINET: CAB 1",
	"THIS CABINET: CAB 2",
};

static int MainArcadeLinkLayout_InputValid(const struct MainArcadeLinkLayoutInput *input)
{
	if (input->screen > (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_EXIT) return 0;
	if (input->lobbyStatus > (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_LOST) return 0;
	if (input->endReason > (uint32_t)NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT) return 0;
	if ((input->localCab != 1u) && (input->localCab != 2u)) return 0;
	if (input->rowsEnabled > 1u) return 0;
	if (input->attract > 1u) return 0;
	if ((input->attract == 1u) && (input->screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF)) return 0;
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

static void MainArcadeLinkLayout_AddText(struct MainArcadeLinkLayout *layout, uint32_t font, uint32_t color, int y,
	const char *text, const char *suffix)
{
	struct MainArcadeLinkItem *item;
	size_t length = 0;

	if (layout->count >= MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS) return;
	item = &layout->items[layout->count];
	layout->count++;
	item->kind = (uint8_t)MAIN_ARCADE_LINK_ITEM_TEXT;
	item->font = (uint8_t)font;
	item->flags = (uint16_t)(color | MAIN_ARCADE_LINK_JUSTIFY_CENTER);
	item->x = (int16_t)MAIN_ARCADE_LINK_LAYOUT_CENTER_X;
	item->y = (int16_t)y;
	MainArcadeLinkLayout_Append(item->text, &length, text);
	MainArcadeLinkLayout_Append(item->text, &length, suffix);
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
