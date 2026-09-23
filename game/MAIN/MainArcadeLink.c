/*
 * Arcade-link live hook (docs/GAME_LOOP_UI_MILESTONE.md sections 2.4-2.6,
 * Tasks 6b-2 to 6b-4): the thin drawer and main-menu-level glue between the
 * retail title screen and the arcade-link host (include/platform/native_arcade_link_host.h,
 * the only arcade-link API game code calls). Native only, and dormant unless
 * an arcade-link host option was given: with the host mode OFF,
 * MainArcadeLink_Frame returns 0 as its first statement and touches nothing.
 *
 * Every decision lives in the pure, unit-tested MAIN/MainArcadeLinkPolicy.c;
 * this file gathers the policy's inputs from the game, applies its outputs,
 * ticks the host, and draws.
 *
 * Unity-included after the 230 overlay sources, because it reads the title
 * state (MM_TITLE_MENU_STATE, MM_TITLE_INTRO_FRAME) and the retail main-menu
 * box (MM_MENU_MAIN).
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "platform/native_arcade_link_host.h"
#include "platform/native_log.h"
#include "MAIN/MainArcadeLinkLayout.h"
#include "MAIN/MainArcadeLinkPolicy.h"
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
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE == PLAYER_BLUE, "MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE must match PLAYER_BLUE");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_PLAYER_RED == PLAYER_RED, "MAIN_ARCADE_LINK_COLOR_PLAYER_RED must match PLAYER_RED");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN == PLAYER_GREEN, "MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN must match PLAYER_GREEN");
_Static_assert((int)MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW == PLAYER_YELLOW, "MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW must match PLAYER_YELLOW");

/* The policy mirrors these retail and host values without including game or
 * host headers; keep the mirrors honest. */
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_MODE_OFF == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF, "MAIN_ARCADE_LINK_POLICY_MODE_OFF must match NATIVE_ARCADE_LINK_HOST_MODE_OFF");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_MODE_LINK == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK, "MAIN_ARCADE_LINK_POLICY_MODE_LINK must match NATIVE_ARCADE_LINK_HOST_MODE_LINK");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_MODE_PREVIEW == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW, "MAIN_ARCADE_LINK_POLICY_MODE_PREVIEW must match NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_TITLE_INTRO == (uint32_t)TITLE_MENU_STATE_INTRO, "MAIN_ARCADE_LINK_POLICY_TITLE_INTRO must match TITLE_MENU_STATE_INTRO");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_TITLE_IN_MENU == (uint32_t)TITLE_MENU_STATE_IN_MENU, "MAIN_ARCADE_LINK_POLICY_TITLE_IN_MENU must match TITLE_MENU_STATE_IN_MENU");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_TITLE_EXITING == (uint32_t)TITLE_MENU_STATE_EXITING, "MAIN_ARCADE_LINK_POLICY_TITLE_EXITING must match TITLE_MENU_STATE_EXITING");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_TITLE_RETURNING == (uint32_t)TITLE_MENU_STATE_RETURNING, "MAIN_ARCADE_LINK_POLICY_TITLE_RETURNING must match TITLE_MENU_STATE_RETURNING");
_Static_assert((int)MAIN_ARCADE_LINK_POLICY_MENU_READY_FRAME == (int)TITLE_INTRO_MENU_READY_FRAME, "MAIN_ARCADE_LINK_POLICY_MENU_READY_FRAME must match TITLE_INTRO_MENU_READY_FRAME");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_UP == (uint32_t)BTN_UP, "MAIN_ARCADE_LINK_POLICY_BTN_UP must match BTN_UP");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_DOWN == (uint32_t)BTN_DOWN, "MAIN_ARCADE_LINK_POLICY_BTN_DOWN must match BTN_DOWN");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_LEFT == (uint32_t)BTN_LEFT, "MAIN_ARCADE_LINK_POLICY_BTN_LEFT must match BTN_LEFT");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_RIGHT == (uint32_t)BTN_RIGHT, "MAIN_ARCADE_LINK_POLICY_BTN_RIGHT must match BTN_RIGHT");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_CROSS_ONE == (uint32_t)BTN_CROSS_one, "MAIN_ARCADE_LINK_POLICY_BTN_CROSS_ONE must match BTN_CROSS_one");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_SQUARE_ONE == (uint32_t)BTN_SQUARE_one, "MAIN_ARCADE_LINK_POLICY_BTN_SQUARE_ONE must match BTN_SQUARE_one");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_CIRCLE == (uint32_t)BTN_CIRCLE, "MAIN_ARCADE_LINK_POLICY_BTN_CIRCLE must match BTN_CIRCLE");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_R1 == (uint32_t)BTN_R1, "MAIN_ARCADE_LINK_POLICY_BTN_R1 must match BTN_R1");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_START == (uint32_t)BTN_START, "MAIN_ARCADE_LINK_POLICY_BTN_START must match BTN_START");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_SELECT == (uint32_t)BTN_SELECT, "MAIN_ARCADE_LINK_POLICY_BTN_SELECT must match BTN_SELECT");
_Static_assert((uint32_t)MAIN_ARCADE_LINK_POLICY_BTN_TRIANGLE == (uint32_t)BTN_TRIANGLE, "MAIN_ARCADE_LINK_POLICY_BTN_TRIANGLE must match BTN_TRIANGLE");

/* The panel style retail menus pass: RECTMENU_DrawFullRect hands
 * RECTMENU_DrawInnerRect the menu's drawStyle, and the retail main menu's
 * drawStyle is 0 (grey frame, translucent dark fill, drop shadow). */
#define MAIN_ARCADE_LINK_PANEL_STYLE 0

/* Local player 0's retail held-button word on the previous frame, tracked on
 * every frame while the host is not OFF so a button already held when the
 * layer takes the frame never counts as a fresh press. */
static uint32_t s_mainArcadeLinkPrevRawHeld;

/* 1 while this module has set INVISIBLE on the retail main-menu box. Retail
 * never sets INVISIBLE on that box, so clearing it restores retail state. */
static uint8_t s_mainArcadeLinkHidMainMenu;

static void MainArcadeLink_HideMainMenu(void)
{
	if ((MM_MENU_MAIN.state & INVISIBLE) == 0)
	{
		MM_MENU_MAIN.state |= INVISIBLE;
		s_mainArcadeLinkHidMainMenu = 1u;
	}
}

/* Gives the retail main-menu box back once the layer no longer owns the
 * frame, or immediately when the host has fallen back to mode OFF. */
static void MainArcadeLink_RestoreMainMenu(void)
{
	if (s_mainArcadeLinkHidMainMenu == 0u)
	{
		return;
	}
	MM_MENU_MAIN.state &= ~INVISIBLE;
	s_mainArcadeLinkHidMainMenu = 0u;
}

/* Clears every player's per-frame taps, which the retail title code reads
 * straight from the pads (MM_ParseCheatCodes reads gamepad[0].buttonsTapped)
 * before any menu input clear could reach them. Held bits are kept: the
 * layer reads them, and the retail demo countdown reads anyoneHeldCurr. */
static void MainArcadeLink_ClearTaps(struct GamepadSystem *gGS)
{
	uint32_t i;

	for (i = 0u; i < (uint32_t)(sizeof(gGS->gamepad) / sizeof(gGS->gamepad[0])); i++)
	{
		gGS->gamepad[i].buttonsTapped = 0;
	}
	gGS->anyoneTapped = 0u;
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
	/* The layout's own field-for-field mapping, the same one
	 * tests/main_arcade_link_view_layout_test.c proves every host view
	 * passes MainArcadeLinkLayout_Build through. */
	if (!MainArcadeLinkLayout_InputFromHostView(&view, &input))
	{
		return;
	}
	if (!MainArcadeLinkLayout_Build(&input, &layout))
	{
		return;
	}
	MainArcadeLink_Draw(gGT, &layout);
}

/* One letter per slot role for the agreed-match log line: 1 and 2 for the
 * cabinets, B for a bot, - for an inactive slot (or anything else). */
static char MainArcadeLink_RoleLetter(uint8_t role)
{
	switch (role)
	{
	case NATIVE_ARCADE_LINK_HOST_ROLE_CAB1:
		return '1';
	case NATIVE_ARCADE_LINK_HOST_ROLE_CAB2:
		return '2';
	case NATIVE_ARCADE_LINK_HOST_ROLE_BOT:
		return 'B';
	default:
		return '-';
	}
}

/* Logs the agreed match the link just started (docs/MATCH_SELECT_MILESTONE.md
 * section 2.7): track, laps, the 64-bit seed as two 32-bit halves, and each
 * slot's character, then the slot roles as letters. */
static void MainArcadeLink_LogAgreedMatch(const struct NativeArcadeLinkHostMatch *match)
{
	char roles[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS + 1u];
	uint32_t slot;

	for (slot = 0u; slot < NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS; slot++)
	{
		roles[slot] = MainArcadeLink_RoleLetter(match->slotRole[slot]);
	}
	roles[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS] = '\0';
	Platform_Log("[CTR Native] arcade link: agreed match track %u laps %u seed 0x%08X%08X slots %u %u %u %u %u %u %u %u (%s)\n",
		(unsigned)match->trackID, (unsigned)match->lapCount, (unsigned)(uint32_t)(match->masterSeed >> 32),
		(unsigned)(uint32_t)(match->masterSeed & 0xFFFFFFFFu), (unsigned)match->slotCharacter[0],
		(unsigned)match->slotCharacter[1], (unsigned)match->slotCharacter[2], (unsigned)match->slotCharacter[3],
		(unsigned)match->slotCharacter[4], (unsigned)match->slotCharacter[5], (unsigned)match->slotCharacter[6],
		(unsigned)match->slotCharacter[7], roles);
}

/* LINK mode: attract entry, one host tick, and the actions the game owns. */
static void MainArcadeLink_LinkTick(struct GameTracker *gGT, const struct MainArcadeLinkPolicyOutput *output)
{
	struct NativeArcadeLinkHostMatch match;
	uint32_t action;

	if (output->enterPressed != 0u)
	{
		(void)NativeArcadeLinkHost_Enter();
	}

	action = NativeArcadeLinkHost_Tick(output->heldButtons, 0u);

	if (action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)
	{
		/* The resolved match first, while the link still holds it. */
		if (NativeArcadeLinkHost_GetAgreedMatch(&match))
		{
			MainArcadeLink_LogAgreedMatch(&match);
		}
		Platform_Log("[CTR Native] arcade link: networked race launch is not wired yet (docs/GAME_LOOP_UI_MILESTONE.md Task 7); returning to title\n");
		NativeArcadeLinkHost_AbortToTitle();
		/* AbortToTitle falls back to mode OFF if the link cannot reopen. The
		 * next frame's OFF early return touches nothing, so give the box back
		 * now or it would stay hidden forever. */
		if (NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)
		{
			MainArcadeLink_RestoreMainMenu();
		}
	}
	else if ((action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE) && (gGT->levelID != MAIN_MENU_LEVEL))
	{
		/* The retail demo-mode exit (MainMain.c): back to the title. */
		gGT->boolDemoMode = 0;
		gGT->numPlyrNextGame = 1;
		sdata->mainMenuState = MAIN_MENU_TITLE;
		MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
	}
}

/* The menu the retail menu system processes this frame: a pending desired
 * menu replaces the active one before RECTMENU_ProcessState runs its funcPtr. */
static const struct RectMenu *MainArcadeLink_MenuThisFrame(void)
{
	if (sdata->ptrDesiredMenu != NULL)
	{
		return sdata->ptrDesiredMenu;
	}
	return sdata->ptrActiveMenu;
}

static void MainArcadeLink_Gather(const struct GameTracker *gGT, const struct GamepadSystem *gGS, struct MainArcadeLinkPolicyInput *input)
{
	input->hostMode = NativeArcadeLinkHost_Mode();
	input->titleState = (uint32_t)MM_TITLE_MENU_STATE;
	input->introFrame = (int32_t)(s16)MM_TITLE_INTRO_FRAME;
	input->rawHeld = (uint32_t)gGS->gamepad[0].buttonsHeldCurrFrame;
	input->prevRawHeld = s_mainArcadeLinkPrevRawHeld;
	input->hostScreenActive = (NativeArcadeLinkHost_ScreenActive() != 0) ? 1u : 0u;
	input->levelIsMainMenu = (gGT->levelID == MAIN_MENU_LEVEL) ? 1u : 0u;
	input->loading = ((sdata->Loading.stage != LOAD_IDLE) || ((gGT->gameMode1 & LOADING) != 0)) ? 1u : 0u;
	input->mainMenuBoxActive = ((MainArcadeLink_MenuThisFrame() == &MM_MENU_MAIN) && (sdata->mainMenuState == MAIN_MENU_TITLE)) ? 1u : 0u;
	input->submenuOpen = ((MM_MENU_MAIN.state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0) ? 1u : 0u;
	input->boxHidden = s_mainArcadeLinkHidMainMenu;
	input->reserved[0] = 0u;
	input->reserved[1] = 0u;
}

int MainArcadeLink_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)
{
	struct MainArcadeLinkPolicyInput input;
	struct MainArcadeLinkPolicyOutput output;

	/* Default behaviour guarantee: no arcade-link option, no side effect. */
	if (NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)
	{
		return 0;
	}
	if ((gGT == NULL) || (gGS == NULL))
	{
		return 0;
	}

	MainArcadeLink_Gather(gGT, gGS, &input);
	s_mainArcadeLinkPrevRawHeld = input.rawHeld;
	if (!MainArcadeLinkPolicy_Decide(&input, &output))
	{
		return 0;
	}

	if (output.restoreBox != 0u)
	{
		MainArcadeLink_RestoreMainMenu();
	}
	if (output.owns == 0u)
	{
		return 0;
	}

	if (output.clearTaps != 0u)
	{
		MainArcadeLink_ClearTaps(gGS);
	}
	if (output.hideBox != 0u)
	{
		MainArcadeLink_HideMainMenu();
	}

	if (input.hostMode == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		MainArcadeLink_LinkTick(gGT, &output);
	}
	else
	{
		/* PREVIEW: a scripted screen; never enters a link. */
		(void)NativeArcadeLinkHost_Tick(output.heldButtons, 0u);
	}

	/* The retail title demo must not fire while an arcade-link screen is up
	 * or a preview is captured; on the LINK attract screen it runs as retail. */
	if (output.resetDemoCountdown != 0u)
	{
		gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
	}

	MainArcadeLink_BuildAndDraw(gGT);
	return 1;
}

#endif
