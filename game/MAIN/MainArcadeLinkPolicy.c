#include "MAIN/MainArcadeLinkPolicy.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Arcade-link live hook decision policy (docs/GAME_LOOP_UI_MILESTONE.md
 * section 2.5, Task 6b-3). See MAIN/MainArcadeLinkPolicy.h for the rules.
 */

uint32_t MainArcadeLinkPolicy_MapHeld(uint32_t rawHeld)
{
	uint32_t logical = 0u;

	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_UP) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_UP;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_DOWN) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_DOWN;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_LEFT) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_LEFT;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_RIGHT) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_RIGHT;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_CROSS_ONE) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_CROSS;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_CIRCLE) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_CIRCLE;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_SQUARE_ONE) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_SQUARE;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_TRIANGLE) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_TRIANGLE;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_START) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_START;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_SELECT) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_SELECT;
	if ((rawHeld & MAIN_ARCADE_LINK_POLICY_BTN_R1) != 0u)
		logical |= NATIVE_ARCADE_MENU_BUTTON_R1;

	return logical;
}

/* The retail main-menu box could be visible or take input on the idle
 * main-menu level: every title state except the intro before the menu-ready
 * frame. submenuOpen deliberately plays no part. */
int MainArcadeLinkPolicy_TitleMenuReady(const struct MainArcadeLinkPolicyInput *input)
{
	if (input == NULL)
	{
		return 0;
	}
	if ((input->levelIsMainMenu == 0u) || (input->loading != 0u) || (input->mainMenuBoxActive == 0u))
	{
		return 0;
	}
	if ((input->titleState == MAIN_ARCADE_LINK_POLICY_TITLE_INTRO) && (input->introFrame < MAIN_ARCADE_LINK_POLICY_MENU_READY_FRAME))
	{
		return 0;
	}
	return 1;
}

int MainArcadeLinkPolicy_Decide(const struct MainArcadeLinkPolicyInput *input, struct MainArcadeLinkPolicyOutput *output)
{
	uint32_t held;
	uint32_t pressed;
	int link;
	int screenActive;
	int owns;
	int enter;

	if ((input == NULL) || (output == NULL))
	{
		return 0;
	}
	memset(output, 0, sizeof(*output));

	if ((input->hostMode != MAIN_ARCADE_LINK_POLICY_MODE_LINK) && (input->hostMode != MAIN_ARCADE_LINK_POLICY_MODE_PREVIEW))
	{
		return 1;
	}
	link = (input->hostMode == MAIN_ARCADE_LINK_POLICY_MODE_LINK) ? 1 : 0;
	screenActive = (input->hostScreenActive != 0u) ? 1 : 0;

	held = MainArcadeLinkPolicy_MapHeld(input->rawHeld);
	pressed = held & ~MainArcadeLinkPolicy_MapHeld(input->prevRawHeld);
	output->heldButtons = held;

	owns = MainArcadeLinkPolicy_TitleMenuReady(input);
	if ((link != 0) && (screenActive != 0))
	{
		owns = 1;
	}
	if (owns == 0)
	{
		output->restoreBox = (input->boxHidden != 0u) ? 1u : 0u;
		return 1;
	}

	enter = (link != 0) && (screenActive == 0) && (input->titleState != MAIN_ARCADE_LINK_POLICY_TITLE_EXITING) &&
	        ((pressed & (NATIVE_ARCADE_MENU_BUTTON_START | NATIVE_ARCADE_MENU_BUTTON_CROSS)) != 0u);

	output->owns = 1u;
	output->hideBox = 1u;
	output->clearTaps = 1u;
	output->enterPressed = (enter != 0) ? 1u : 0u;
	output->resetDemoCountdown = ((link == 0) || (screenActive != 0) || (enter != 0)) ? 1u : 0u;
	return 1;
}
