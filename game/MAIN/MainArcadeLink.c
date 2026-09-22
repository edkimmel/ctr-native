/*
 * Arcade-link live hook (docs/GAME_LOOP_UI_MILESTONE.md sections 2.4-2.6,
 * Task 6b-2): the thin drawer and main-menu-level glue between the retail
 * title screen and the arcade-link host (include/platform/native_arcade_link_host.h,
 * the only arcade-link API game code calls). Native only, and dormant unless
 * an arcade-link host option was given: with the host mode OFF,
 * MainArcadeLink_Frame returns 0 as its first statement and touches nothing.
 *
 * Unity-included after the 230 overlay sources, because it reads the title
 * state (MM_TITLE_MENU_STATE) and the retail main-menu box (MM_MENU_MAIN).
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_menu_input.h"
#include "MAIN/MainArcadeLinkLayout.h"
#include "MAIN/MainArcadeLink.h"

/* The layout builder mirrors these retail values without including game
 * headers; keep the mirrors honest. */
_Static_assert((int)MAIN_ARCADE_LINK_FONT_BIG == FONT_BIG, "MAIN_ARCADE_LINK_FONT_BIG must match FONT_BIG");
_Static_assert((int)MAIN_ARCADE_LINK_FONT_SMALL == FONT_SMALL, "MAIN_ARCADE_LINK_FONT_SMALL must match FONT_SMALL");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_ORANGE == ORANGE, "MAIN_ARCADE_LINK_COLOR_ORANGE must match ORANGE");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_RED == RED, "MAIN_ARCADE_LINK_COLOR_RED must match RED");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_WHITE == WHITE, "MAIN_ARCADE_LINK_COLOR_WHITE must match WHITE");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_GRAY == GRAY, "MAIN_ARCADE_LINK_COLOR_GRAY must match GRAY");
_Static_assert((int)MAIN_ARCADE_LINK_JUSTIFY_CENTER == JUSTIFY_CENTER, "MAIN_ARCADE_LINK_JUSTIFY_CENTER must match JUSTIFY_CENTER");

/* The panel style retail menus pass: RECTMENU_DrawFullRect hands
 * RECTMENU_DrawInnerRect the menu's drawStyle, and the retail main menu's
 * drawStyle is 0 (grey frame, translucent dark fill, drop shadow). */
#define MAIN_ARCADE_LINK_PANEL_STYLE 0

/* Local player 0's mapped held buttons on the previous frame, tracked on
 * every frame while the host is not OFF so a button already held when the
 * layer takes the frame never counts as a fresh press. */
static uint32_t s_mainArcadeLinkPrevHeld;

/* 1 while this module has set INVISIBLE on the retail main-menu box. Retail
 * never sets INVISIBLE on that box, so clearing it restores retail state. */
static uint8_t s_mainArcadeLinkHidMainMenu;

/* Retail BTN_* held bits (include/namespace_Gamepad.h) to the arcade menu
 * input's logical bits. Only the first Cross and Square bits are used, the
 * same bits the retail menus read. */
static uint32_t MainArcadeLink_MapHeld(u32 held)
{
	uint32_t logical = 0u;

	if ((held & BTN_UP) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_UP;
	if ((held & BTN_DOWN) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_DOWN;
	if ((held & BTN_LEFT) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_LEFT;
	if ((held & BTN_RIGHT) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_RIGHT;
	if ((held & BTN_CROSS_one) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_CROSS;
	if ((held & BTN_CIRCLE) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_CIRCLE;
	if ((held & BTN_SQUARE_one) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_SQUARE;
	if ((held & BTN_TRIANGLE) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_TRIANGLE;
	if ((held & BTN_START) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_START;
	if ((held & BTN_SELECT) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_SELECT;
	if ((held & BTN_R1) != 0)
		logical |= NATIVE_ARCADE_MENU_BUTTON_R1;

	return logical;
}

/* The main-menu level is idle, the title intro has finished, and the retail
 * top-level main menu is the active box with no submenu open. */
static int MainArcadeLink_TitleMenuShowing(const struct GameTracker *gGT)
{
	return (gGT->levelID == MAIN_MENU_LEVEL) && (sdata->Loading.stage == LOAD_IDLE) && ((gGT->gameMode1 & LOADING) == 0) &&
	       (MM_TITLE_MENU_STATE == TITLE_MENU_STATE_IN_MENU) && (sdata->mainMenuState == MAIN_MENU_TITLE) && (sdata->ptrActiveMenu == &MM_MENU_MAIN) &&
	       ((MM_MENU_MAIN.state & DRAW_NEXT_MENU_IN_HIERARCHY) == 0);
}

static void MainArcadeLink_HideMainMenu(void)
{
	if ((MM_MENU_MAIN.state & INVISIBLE) == 0)
	{
		MM_MENU_MAIN.state |= INVISIBLE;
		s_mainArcadeLinkHidMainMenu = 1u;
	}
}

/* Gives the retail main-menu box back once the layer no longer owns the
 * frame. While the title slides out on the main-menu level (the demo
 * countdown fired from the attract screen) the box stays hidden, so it does
 * not flash in only to slide away; it is restored once the level changes or
 * the title is anywhere but EXITING. */
static void MainArcadeLink_RestoreMainMenu(const struct GameTracker *gGT)
{
	if (s_mainArcadeLinkHidMainMenu == 0u)
	{
		return;
	}
	if ((gGT->levelID == MAIN_MENU_LEVEL) && (MM_TITLE_MENU_STATE == TITLE_MENU_STATE_EXITING))
	{
		return;
	}
	MM_MENU_MAIN.state &= ~INVISIBLE;
	s_mainArcadeLinkHidMainMenu = 0u;
}

/* Walks the draw list in order with the retail primitives: the text lines,
 * then the RECTMENU_DrawSelf row highlight, then the menu panel, exactly the
 * order RECTMENU_DrawSelf and RECTMENU_DrawFullRect draw a retail menu. */
static void MainArcadeLink_Draw(struct GameTracker *gGT, struct MainArcadeLinkLayout *layout)
{
	uint32_t i;

	for (i = 0u; i < layout->count; i++)
	{
		struct MainArcadeLinkItem *item = &layout->items[i];
		RECT rect;

		rect.x = item->x;
		rect.y = item->y;
		rect.w = item->w;
		rect.h = item->h;

		switch (item->kind)
		{
		case MAIN_ARCADE_LINK_ITEM_TEXT:
			DecalFont_DrawLine(item->text, item->x, item->y, (s16)item->font, (s16)item->flags);
			break;
		case MAIN_ARCADE_LINK_ITEM_HIGHLIGHT:
			CTR_Box_DrawClearBox(&rect, &sdata->menuRowHighlight_Normal, 1, gGT->backBuffer->otMem.uiOT, &gGT->backBuffer->primMem);
			break;
		case MAIN_ARCADE_LINK_ITEM_PANEL:
			RECTMENU_DrawInnerRect(&rect, MAIN_ARCADE_LINK_PANEL_STYLE, gGT->backBuffer->otMem.uiOT);
			break;
		default:
			break;
		}
	}
}

static void MainArcadeLink_BuildAndDraw(struct GameTracker *gGT)
{
	struct NativeArcadeLinkHostView view;
	struct MainArcadeLinkLayoutInput input;
	struct MainArcadeLinkLayout layout;

	if (!NativeArcadeLinkHost_GetView(&view))
	{
		return;
	}

	input.screen = view.screen;
	input.lobbyStatus = view.lobbyStatus;
	input.endReason = view.endReason;
	input.selectedRow = view.selectedRow;
	input.ticksInScreen = view.ticksInScreen;
	input.localCab = view.localCab;
	input.rowsEnabled = view.rowsEnabled;
	input.attract = view.attract;
	input.reserved = 0u;

	if (!MainArcadeLinkLayout_Build(&input, &layout))
	{
		return;
	}
	MainArcadeLink_Draw(gGT, &layout);
}

/* LINK mode: attract entry, one host tick, and the actions the game owns. */
static void MainArcadeLink_LinkTick(struct GameTracker *gGT, uint32_t held, uint32_t pressed)
{
	uint32_t action;

	if ((NativeArcadeLinkHost_ScreenActive() == 0) && ((pressed & (NATIVE_ARCADE_MENU_BUTTON_START | NATIVE_ARCADE_MENU_BUTTON_CROSS)) != 0u))
	{
		(void)NativeArcadeLinkHost_Enter();
	}

	action = NativeArcadeLinkHost_Tick(held, 0u);

	if (action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)
	{
		printf("[CTR Native] arcade link: networked race launch is not wired yet (docs/GAME_LOOP_UI_MILESTONE.md Task 7); returning to title\n");
		fflush(stdout);
		NativeArcadeLinkHost_AbortToTitle();
	}
	else if ((action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE) && (gGT->levelID != MAIN_MENU_LEVEL))
	{
		/* The retail demo-mode exit (MainMain.c): back to the title. */
		gGT->numPlyrNextGame = 1;
		sdata->mainMenuState = MAIN_MENU_TITLE;
		MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
	}
}

int MainArcadeLink_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)
{
	uint32_t held;
	uint32_t pressed;

	/* Default behaviour guarantee: no arcade-link option, no side effect. */
	if (NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)
	{
		return 0;
	}
	if ((gGT == NULL) || (gGS == NULL))
	{
		return 0;
	}

	held = MainArcadeLink_MapHeld((u32)gGS->gamepad[0].buttonsHeldCurrFrame);
	pressed = held & ~s_mainArcadeLinkPrevHeld;
	s_mainArcadeLinkPrevHeld = held;

	if (!MainArcadeLink_TitleMenuShowing(gGT) && (NativeArcadeLinkHost_ScreenActive() == 0))
	{
		MainArcadeLink_RestoreMainMenu(gGT);
		return 0;
	}

	if (NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		MainArcadeLink_LinkTick(gGT, held, pressed);
	}
	else
	{
		/* PREVIEW: a scripted screen; never enters a link. */
		(void)NativeArcadeLinkHost_Tick(held, 0u);
	}

	/* The retail title demo countdown must not fire while an arcade-link
	 * screen is up; on the attract screen (screen OFF) it runs as retail. */
	if (NativeArcadeLinkHost_ScreenActive() != 0)
	{
		gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
	}

	MainArcadeLink_HideMainMenu();
	MainArcadeLink_BuildAndDraw(gGT);
	return 1;
}

#endif
