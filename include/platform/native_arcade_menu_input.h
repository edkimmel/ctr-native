#ifndef PLATFORM_NATIVE_ARCADE_MENU_INPUT_H
#define PLATFORM_NATIVE_ARCADE_MENU_INPUT_H

#include <stdint.h>

/*
 * Arcade menu input (docs/GAME_LOOP_UI_MILESTONE.md section 2.1, UX-1 to
 * UX-4): converts one local player's held-button word, expressed in this
 * module's own logical button bits, into at most one navigation event per
 * tick. The caller translates its pad bits into these logical bits, so this
 * module is independent of both the raw pad format and any engine header.
 *
 * Rules:
 * - Release-to-arm: after a reset nothing fires until every mapped button
 *   has been seen released at least once. The arming tick itself never
 *   produces an event.
 * - Rising edge only: holding a button never repeats, and there is no
 *   auto-repeat (every menu here has at most two rows).
 * - Ambiguous same-tick edges (confirm with back, or previous with next)
 *   produce no event.
 * - Confirm is Cross or Start. On the G29 the throttle pedal is also Cross;
 *   release-to-arm keeps a throttle held across a screen change harmless.
 * - Back is Triangle only. Square is excluded because it is the G29 brake
 *   pedal: a foot resting on the brake must never leave a screen.
 * - The paddles navigate: the left paddle (R1) with D-pad up/left selects
 *   the previous row, the right paddle (Circle) with D-pad down/right the
 *   next row. Steering is not an input here, so an off-centre wheel cannot
 *   drift the focus.
 * - Unmapped bits (Square, Select, and every bit from 11 up) are ignored
 *   completely: they never produce an event and never block arming.
 *
 * Caller-owned state, no heap use, no hidden state. A zero-initialized
 * struct behaves exactly like one passed to NativeArcadeMenuInput_Reset.
 */

/* Logical button bits. Values are frozen. */
#define NATIVE_ARCADE_MENU_BUTTON_UP (UINT32_C(1) << 0)
#define NATIVE_ARCADE_MENU_BUTTON_DOWN (UINT32_C(1) << 1)
#define NATIVE_ARCADE_MENU_BUTTON_LEFT (UINT32_C(1) << 2)
#define NATIVE_ARCADE_MENU_BUTTON_RIGHT (UINT32_C(1) << 3)
#define NATIVE_ARCADE_MENU_BUTTON_CROSS (UINT32_C(1) << 4)
#define NATIVE_ARCADE_MENU_BUTTON_CIRCLE (UINT32_C(1) << 5)
#define NATIVE_ARCADE_MENU_BUTTON_SQUARE (UINT32_C(1) << 6)
#define NATIVE_ARCADE_MENU_BUTTON_TRIANGLE (UINT32_C(1) << 7)
#define NATIVE_ARCADE_MENU_BUTTON_START (UINT32_C(1) << 8)
#define NATIVE_ARCADE_MENU_BUTTON_SELECT (UINT32_C(1) << 9)
#define NATIVE_ARCADE_MENU_BUTTON_R1 (UINT32_C(1) << 10)

/* Previous row: D-pad up/left or the left paddle (R1). */
#define NATIVE_ARCADE_MENU_PREV_MASK \
	(NATIVE_ARCADE_MENU_BUTTON_UP | NATIVE_ARCADE_MENU_BUTTON_LEFT | NATIVE_ARCADE_MENU_BUTTON_R1)
/* Next row: D-pad down/right or the right paddle (Circle). */
#define NATIVE_ARCADE_MENU_NEXT_MASK \
	(NATIVE_ARCADE_MENU_BUTTON_DOWN | NATIVE_ARCADE_MENU_BUTTON_RIGHT | NATIVE_ARCADE_MENU_BUTTON_CIRCLE)
/* Confirm: Cross (also the G29 throttle) or Start. */
#define NATIVE_ARCADE_MENU_CONFIRM_MASK (NATIVE_ARCADE_MENU_BUTTON_CROSS | NATIVE_ARCADE_MENU_BUTTON_START)
/* Back: Triangle only; Square is the G29 brake and is excluded. */
#define NATIVE_ARCADE_MENU_BACK_MASK (NATIVE_ARCADE_MENU_BUTTON_TRIANGLE)
/* Every bit this module reacts to; all other bits are ignored. */
#define NATIVE_ARCADE_MENU_MAPPED_MASK \
	(NATIVE_ARCADE_MENU_PREV_MASK | NATIVE_ARCADE_MENU_NEXT_MASK | NATIVE_ARCADE_MENU_CONFIRM_MASK | \
		NATIVE_ARCADE_MENU_BACK_MASK)

enum NativeArcadeMenuEvent
{
	NATIVE_ARCADE_MENU_EVENT_NONE = 0,
	NATIVE_ARCADE_MENU_EVENT_PREV = 1,
	NATIVE_ARCADE_MENU_EVENT_NEXT = 2,
	NATIVE_ARCADE_MENU_EVENT_CONFIRM = 3,
	NATIVE_ARCADE_MENU_EVENT_BACK = 4
};

struct NativeArcadeMenuInput
{
	/* Mapped buttons held on the previous update. */
	uint32_t previousHeld;
	/* Nonzero once every mapped button has been seen released. */
	uint8_t armed;
	uint8_t reserved[3];
};

/* Zeroes the state (disarmed, nothing held). NULL is a no-op. */
void NativeArcadeMenuInput_Reset(struct NativeArcadeMenuInput *input);

/* Feeds one tick's held-button word; returns at most one event. NULL input
 * returns NATIVE_ARCADE_MENU_EVENT_NONE. */
enum NativeArcadeMenuEvent NativeArcadeMenuInput_Update(struct NativeArcadeMenuInput *input, uint32_t heldButtons);

/* 1 once armed, else 0. NULL gives 0. */
int NativeArcadeMenuInput_IsArmed(const struct NativeArcadeMenuInput *input);

#endif
