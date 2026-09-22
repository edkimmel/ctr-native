#include "platform/native_arcade_menu_input.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define UP NATIVE_ARCADE_MENU_BUTTON_UP
#define DOWN NATIVE_ARCADE_MENU_BUTTON_DOWN
#define LEFT NATIVE_ARCADE_MENU_BUTTON_LEFT
#define RIGHT NATIVE_ARCADE_MENU_BUTTON_RIGHT
#define CROSS NATIVE_ARCADE_MENU_BUTTON_CROSS
#define CIRCLE NATIVE_ARCADE_MENU_BUTTON_CIRCLE
#define SQUARE NATIVE_ARCADE_MENU_BUTTON_SQUARE
#define TRIANGLE NATIVE_ARCADE_MENU_BUTTON_TRIANGLE
#define START NATIVE_ARCADE_MENU_BUTTON_START
#define SELECT NATIVE_ARCADE_MENU_BUTTON_SELECT
#define R1 NATIVE_ARCADE_MENU_BUTTON_R1
#define BIT20 (UINT32_C(1) << 20)

#define EV_NONE NATIVE_ARCADE_MENU_EVENT_NONE
#define EV_PREV NATIVE_ARCADE_MENU_EVENT_PREV
#define EV_NEXT NATIVE_ARCADE_MENU_EVENT_NEXT
#define EV_CONFIRM NATIVE_ARCADE_MENU_EVENT_CONFIRM
#define EV_BACK NATIVE_ARCADE_MENU_EVENT_BACK

/* Resets and arms the input with nothing held. */
static int ArmedFresh(struct NativeArcadeMenuInput *input)
{
	NativeArcadeMenuInput_Reset(input);
	CHECK(NativeArcadeMenuInput_IsArmed(input) == 0);
	CHECK(NativeArcadeMenuInput_Update(input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(input) == 1);
	return 0;
}

/* From an armed, idle state, a single press of `buttons` yields `expected`,
 * holding yields NONE, and releasing yields NONE. */
static int ExpectPress(uint32_t buttons, enum NativeArcadeMenuEvent expected)
{
	struct NativeArcadeMenuInput input;

	CHECK(ArmedFresh(&input) == 0);
	CHECK(NativeArcadeMenuInput_Update(&input, buttons) == expected);
	CHECK(NativeArcadeMenuInput_Update(&input, buttons) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	return 0;
}

static int TestFrozenValues(void)
{
	CHECK(UP == UINT32_C(0x001));
	CHECK(DOWN == UINT32_C(0x002));
	CHECK(LEFT == UINT32_C(0x004));
	CHECK(RIGHT == UINT32_C(0x008));
	CHECK(CROSS == UINT32_C(0x010));
	CHECK(CIRCLE == UINT32_C(0x020));
	CHECK(SQUARE == UINT32_C(0x040));
	CHECK(TRIANGLE == UINT32_C(0x080));
	CHECK(START == UINT32_C(0x100));
	CHECK(SELECT == UINT32_C(0x200));
	CHECK(R1 == UINT32_C(0x400));

	CHECK(NATIVE_ARCADE_MENU_PREV_MASK == UINT32_C(0x405));
	CHECK(NATIVE_ARCADE_MENU_NEXT_MASK == UINT32_C(0x02A));
	CHECK(NATIVE_ARCADE_MENU_CONFIRM_MASK == UINT32_C(0x110));
	CHECK(NATIVE_ARCADE_MENU_BACK_MASK == UINT32_C(0x080));
	CHECK(NATIVE_ARCADE_MENU_MAPPED_MASK == UINT32_C(0x5BF));
	CHECK((NATIVE_ARCADE_MENU_MAPPED_MASK & SQUARE) == 0u);
	CHECK((NATIVE_ARCADE_MENU_MAPPED_MASK & SELECT) == 0u);

	CHECK((int)EV_NONE == 0);
	CHECK((int)EV_PREV == 1);
	CHECK((int)EV_NEXT == 2);
	CHECK((int)EV_CONFIRM == 3);
	CHECK((int)EV_BACK == 4);
	return 0;
}

static int TestNullSafetyAndZeroInit(void)
{
	struct NativeArcadeMenuInput zeroed;
	struct NativeArcadeMenuInput reset;

	NativeArcadeMenuInput_Reset(NULL);
	CHECK(NativeArcadeMenuInput_Update(NULL, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(NULL, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(NULL) == 0);

	/* Reset clears a dirty struct completely. */
	memset(&reset, 0xA5, sizeof(reset));
	NativeArcadeMenuInput_Reset(&reset);
	CHECK(reset.previousHeld == 0u);
	CHECK(reset.armed == 0u);
	CHECK(reset.reserved[0] == 0u);
	CHECK(reset.reserved[1] == 0u);
	CHECK(reset.reserved[2] == 0u);

	memset(&zeroed, 0, sizeof(zeroed));
	CHECK(memcmp(&zeroed, &reset, sizeof(zeroed)) == 0);

	/* Same sequence, same results, on both. */
	{
		static const uint32_t sequence[] = {CROSS, CROSS, 0u, CROSS, CROSS, 0u, UP, 0u, DOWN, TRIANGLE};
		size_t i;

		for (i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++)
		{
			CHECK(NativeArcadeMenuInput_Update(&zeroed, sequence[i]) ==
				NativeArcadeMenuInput_Update(&reset, sequence[i]));
			CHECK(NativeArcadeMenuInput_IsArmed(&zeroed) == NativeArcadeMenuInput_IsArmed(&reset));
		}
	}
	return 0;
}

static int TestReleaseToArm(void)
{
	struct NativeArcadeMenuInput input;

	NativeArcadeMenuInput_Reset(&input);
	/* Throttle (Cross) held across the screen change. */
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 0);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 0);
	/* Released: arms, but the arming tick is silent. */
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	/* Fresh press confirms. */
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_CONFIRM);
	/* Held: no repeat, however long. */
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	/* Release then press again confirms again. */
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_CONFIRM);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	return 0;
}

static int TestUnmappedButtons(void)
{
	struct NativeArcadeMenuInput input;
	uint32_t unmapped = SQUARE | SELECT | BIT20;

	/* Brake (Square) and Select held continuously from reset. */
	NativeArcadeMenuInput_Reset(&input);
	CHECK(NativeArcadeMenuInput_Update(&input, SQUARE | SELECT) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	CHECK(NativeArcadeMenuInput_Update(&input, SQUARE | SELECT) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, SQUARE) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, SELECT) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, SQUARE | SELECT) == EV_NONE);
	/* Cross while the brake is still held still confirms. */
	CHECK(NativeArcadeMenuInput_Update(&input, SQUARE | CROSS) == EV_CONFIRM);
	CHECK(NativeArcadeMenuInput_Update(&input, SQUARE) == EV_NONE);

	/* Bit 20 never matters: not for arming, not for events. */
	NativeArcadeMenuInput_Reset(&input);
	CHECK(NativeArcadeMenuInput_Update(&input, BIT20) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, BIT20) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, BIT20 | UP) == EV_PREV);
	CHECK(NativeArcadeMenuInput_Update(&input, UP) == EV_NONE);

	/* Every unmapped bit at once never fires and never blocks arming. */
	NativeArcadeMenuInput_Reset(&input);
	CHECK(NativeArcadeMenuInput_Update(&input, ~NATIVE_ARCADE_MENU_MAPPED_MASK) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	CHECK(NativeArcadeMenuInput_Update(&input, unmapped) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, ~NATIVE_ARCADE_MENU_MAPPED_MASK) == EV_NONE);
	return 0;
}

static int TestEveryMapping(void)
{
	CHECK(ExpectPress(UP, EV_PREV) == 0);
	CHECK(ExpectPress(LEFT, EV_PREV) == 0);
	CHECK(ExpectPress(R1, EV_PREV) == 0);
	CHECK(ExpectPress(DOWN, EV_NEXT) == 0);
	CHECK(ExpectPress(RIGHT, EV_NEXT) == 0);
	CHECK(ExpectPress(CIRCLE, EV_NEXT) == 0);
	CHECK(ExpectPress(CROSS, EV_CONFIRM) == 0);
	CHECK(ExpectPress(START, EV_CONFIRM) == 0);
	CHECK(ExpectPress(TRIANGLE, EV_BACK) == 0);
	CHECK(ExpectPress(SQUARE, EV_NONE) == 0);
	CHECK(ExpectPress(SELECT, EV_NONE) == 0);
	return 0;
}

static int TestAmbiguityAndPriority(void)
{
	CHECK(ExpectPress(CROSS | TRIANGLE, EV_NONE) == 0);
	CHECK(ExpectPress(UP | DOWN, EV_NONE) == 0);
	CHECK(ExpectPress(R1 | CIRCLE, EV_NONE) == 0);
	CHECK(ExpectPress(CROSS | UP, EV_CONFIRM) == 0);
	CHECK(ExpectPress(TRIANGLE | DOWN, EV_BACK) == 0);
	CHECK(ExpectPress(START | TRIANGLE, EV_NONE) == 0);
	CHECK(ExpectPress(CROSS | START, EV_CONFIRM) == 0);
	CHECK(ExpectPress(UP | LEFT | R1, EV_PREV) == 0);
	return 0;
}

static int TestOnlyNewEdgesCount(void)
{
	struct NativeArcadeMenuInput input;

	CHECK(ArmedFresh(&input) == 0);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_CONFIRM);
	/* Cross still held; Up is the only new edge. */
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS | UP) == EV_PREV);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS | UP) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, TRIANGLE) == EV_BACK);
	/* Triangle still held from before does not make Cross ambiguous. */
	CHECK(NativeArcadeMenuInput_Update(&input, TRIANGLE | CROSS) == EV_CONFIRM);
	/* Releasing a button is never an event. */
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	return 0;
}

static int TestResetWhileArmedDisarms(void)
{
	struct NativeArcadeMenuInput input;

	CHECK(ArmedFresh(&input) == 0);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_CONFIRM);
	/* Screen change while the throttle is still held. */
	NativeArcadeMenuInput_Reset(&input);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 0);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS | UP) == EV_NONE);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 0);
	CHECK(NativeArcadeMenuInput_Update(&input, 0u) == EV_NONE);
	CHECK(NativeArcadeMenuInput_IsArmed(&input) == 1);
	CHECK(NativeArcadeMenuInput_Update(&input, CROSS) == EV_CONFIRM);
	return 0;
}

int main(void)
{
	CHECK(TestFrozenValues() == 0);
	CHECK(TestNullSafetyAndZeroInit() == 0);
	CHECK(TestReleaseToArm() == 0);
	CHECK(TestUnmappedButtons() == 0);
	CHECK(TestEveryMapping() == 0);
	CHECK(TestAmbiguityAndPriority() == 0);
	CHECK(TestOnlyNewEdgesCount() == 0);
	CHECK(TestResetWhileArmedDisarms() == 0);
	puts("native_arcade_menu_input_test: ok");
	return 0;
}
