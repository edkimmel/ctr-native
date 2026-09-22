#include "platform/native_arcade_menu_input.h"

#include <stddef.h>
#include <stdint.h>

void NativeArcadeMenuInput_Reset(struct NativeArcadeMenuInput *input)
{
	if (input == NULL)
	{
		return;
	}
	input->previousHeld = 0u;
	input->armed = 0u;
	input->reserved[0] = 0u;
	input->reserved[1] = 0u;
	input->reserved[2] = 0u;
}

enum NativeArcadeMenuEvent NativeArcadeMenuInput_Update(struct NativeArcadeMenuInput *input, uint32_t heldButtons)
{
	uint32_t held;
	uint32_t rising;
	uint32_t confirm;
	uint32_t back;
	uint32_t prev;
	uint32_t next;

	if (input == NULL)
	{
		return NATIVE_ARCADE_MENU_EVENT_NONE;
	}

	/* Unmapped bits (Square, Select, bits 11 and up) never matter. */
	held = heldButtons & NATIVE_ARCADE_MENU_MAPPED_MASK;

	if (input->armed == 0u)
	{
		/* Release-to-arm: the arming tick itself never produces an event. */
		if (held == 0u)
		{
			input->armed = 1u;
		}
		input->previousHeld = held;
		return NATIVE_ARCADE_MENU_EVENT_NONE;
	}

	rising = held & ~input->previousHeld;
	input->previousHeld = held;

	confirm = rising & NATIVE_ARCADE_MENU_CONFIRM_MASK;
	back = rising & NATIVE_ARCADE_MENU_BACK_MASK;
	prev = rising & NATIVE_ARCADE_MENU_PREV_MASK;
	next = rising & NATIVE_ARCADE_MENU_NEXT_MASK;

	if ((confirm != 0u) && (back != 0u))
	{
		return NATIVE_ARCADE_MENU_EVENT_NONE;
	}
	if (back != 0u)
	{
		return NATIVE_ARCADE_MENU_EVENT_BACK;
	}
	if (confirm != 0u)
	{
		return NATIVE_ARCADE_MENU_EVENT_CONFIRM;
	}
	if ((prev != 0u) && (next != 0u))
	{
		return NATIVE_ARCADE_MENU_EVENT_NONE;
	}
	if (prev != 0u)
	{
		return NATIVE_ARCADE_MENU_EVENT_PREV;
	}
	if (next != 0u)
	{
		return NATIVE_ARCADE_MENU_EVENT_NEXT;
	}
	return NATIVE_ARCADE_MENU_EVENT_NONE;
}

int NativeArcadeMenuInput_IsArmed(const struct NativeArcadeMenuInput *input)
{
	if (input == NULL)
	{
		return 0;
	}
	return (input->armed != 0u) ? 1 : 0;
}
