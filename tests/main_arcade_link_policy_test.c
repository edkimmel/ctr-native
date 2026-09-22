#include "MAIN/MainArcadeLinkPolicy.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "fail %d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define MODE_OFF MAIN_ARCADE_LINK_POLICY_MODE_OFF
#define MODE_LINK MAIN_ARCADE_LINK_POLICY_MODE_LINK
#define MODE_PREVIEW MAIN_ARCADE_LINK_POLICY_MODE_PREVIEW

#define T_INTRO MAIN_ARCADE_LINK_POLICY_TITLE_INTRO
#define T_IN_MENU MAIN_ARCADE_LINK_POLICY_TITLE_IN_MENU
#define T_EXITING MAIN_ARCADE_LINK_POLICY_TITLE_EXITING
#define T_RETURNING MAIN_ARCADE_LINK_POLICY_TITLE_RETURNING

#define READY MAIN_ARCADE_LINK_POLICY_MENU_READY_FRAME
#define SKIP_FRAME 1000

#define RAW_CROSS MAIN_ARCADE_LINK_POLICY_BTN_CROSS_ONE
#define RAW_START MAIN_ARCADE_LINK_POLICY_BTN_START
#define RAW_SQUARE MAIN_ARCADE_LINK_POLICY_BTN_SQUARE_ONE
#define RAW_TRIANGLE MAIN_ARCADE_LINK_POLICY_BTN_TRIANGLE
#define RAW_CIRCLE MAIN_ARCADE_LINK_POLICY_BTN_CIRCLE
/* Retail L1 (0x800): the cheat-code modifier; never mapped. */
#define RAW_L1 0x800u

/* The idle main-menu level with the retail main-menu box active and the
 * title in IN_MENU: the plainest owned frame. */
static struct MainArcadeLinkPolicyInput TitleInput(uint32_t mode)
{
	struct MainArcadeLinkPolicyInput input;

	memset(&input, 0, sizeof(input));
	input.hostMode = mode;
	input.titleState = T_IN_MENU;
	input.introFrame = SKIP_FRAME;
	input.hostScreenActive = (mode == MODE_PREVIEW) ? 1u : 0u;
	input.levelIsMainMenu = 1u;
	input.loading = 0u;
	input.mainMenuBoxActive = 1u;
	return input;
}

static struct MainArcadeLinkPolicyOutput Decide(const struct MainArcadeLinkPolicyInput *input)
{
	struct MainArcadeLinkPolicyOutput output;

	memset(&output, 0xA5, sizeof(output));
	if (!MainArcadeLinkPolicy_Decide(input, &output))
	{
		memset(&output, 0xEE, sizeof(output));
	}
	return output;
}

static int AllZero(const void *bytes, size_t size)
{
	const unsigned char *p = (const unsigned char *)bytes;
	for (size_t i = 0; i < size; i++)
		if (p[i] != 0u) return 0;
	return 1;
}

/* An owned frame: hidden, taps cleared, nothing to restore. */
static int Owned(const struct MainArcadeLinkPolicyOutput *output)
{
	return (output->owns == 1u) && (output->hideBox == 1u) && (output->clearTaps == 1u) && (output->restoreBox == 0u) &&
		(output->reserved[0] == 0u) && (output->reserved[1] == 0u);
}

/* A frame left to retail: nothing but the held mapping and a restore. */
static int NotOwned(const struct MainArcadeLinkPolicyOutput *output, uint8_t restore)
{
	return (output->owns == 0u) && (output->hideBox == 0u) && (output->clearTaps == 0u) && (output->enterPressed == 0u) &&
		(output->resetDemoCountdown == 0u) && (output->restoreBox == restore) && (output->reserved[0] == 0u) &&
		(output->reserved[1] == 0u);
}

static int TestNullArguments(void)
{
	struct MainArcadeLinkPolicyInput input = TitleInput(MODE_LINK);
	struct MainArcadeLinkPolicyOutput output;

	memset(&output, 0x5A, sizeof(output));
	CHECK(MainArcadeLinkPolicy_Decide(NULL, &output) == 0);
	CHECK(output.owns == 0x5Au);
	CHECK(MainArcadeLinkPolicy_Decide(&input, NULL) == 0);
	CHECK(MainArcadeLinkPolicy_Decide(NULL, NULL) == 0);
	return 0;
}

static int TestMapHeld(void)
{
	CHECK(MainArcadeLinkPolicy_MapHeld(0u) == 0u);
	CHECK(MainArcadeLinkPolicy_MapHeld(MAIN_ARCADE_LINK_POLICY_BTN_UP) == NATIVE_ARCADE_MENU_BUTTON_UP);
	CHECK(MainArcadeLinkPolicy_MapHeld(MAIN_ARCADE_LINK_POLICY_BTN_DOWN) == NATIVE_ARCADE_MENU_BUTTON_DOWN);
	CHECK(MainArcadeLinkPolicy_MapHeld(MAIN_ARCADE_LINK_POLICY_BTN_LEFT) == NATIVE_ARCADE_MENU_BUTTON_LEFT);
	CHECK(MainArcadeLinkPolicy_MapHeld(MAIN_ARCADE_LINK_POLICY_BTN_RIGHT) == NATIVE_ARCADE_MENU_BUTTON_RIGHT);
	CHECK(MainArcadeLinkPolicy_MapHeld(RAW_CROSS) == NATIVE_ARCADE_MENU_BUTTON_CROSS);
	CHECK(MainArcadeLinkPolicy_MapHeld(RAW_CIRCLE) == NATIVE_ARCADE_MENU_BUTTON_CIRCLE);
	CHECK(MainArcadeLinkPolicy_MapHeld(RAW_SQUARE) == NATIVE_ARCADE_MENU_BUTTON_SQUARE);
	CHECK(MainArcadeLinkPolicy_MapHeld(RAW_TRIANGLE) == NATIVE_ARCADE_MENU_BUTTON_TRIANGLE);
	CHECK(MainArcadeLinkPolicy_MapHeld(RAW_START) == NATIVE_ARCADE_MENU_BUTTON_START);
	CHECK(MainArcadeLinkPolicy_MapHeld(MAIN_ARCADE_LINK_POLICY_BTN_SELECT) == NATIVE_ARCADE_MENU_BUTTON_SELECT);
	CHECK(MainArcadeLinkPolicy_MapHeld(MAIN_ARCADE_LINK_POLICY_BTN_R1) == NATIVE_ARCADE_MENU_BUTTON_R1);
	/* Every unmapped retail bit (L1, L2, R2, L3, R3, the raw second Cross
	 * and Square bits, and everything above Triangle) maps to nothing. */
	CHECK(MainArcadeLinkPolicy_MapHeld(RAW_L1) == 0u);
	CHECK(MainArcadeLinkPolicy_MapHeld(0x80u | 0x200u | 0x10000u | 0x20000u) == 0u);
	CHECK(MainArcadeLinkPolicy_MapHeld(0x4000u | 0x8000u | 0x100u) == 0u);
	CHECK(MainArcadeLinkPolicy_MapHeld(0xFFF80000u) == 0u);
	CHECK(MainArcadeLinkPolicy_MapHeld(0xFFFFFFFFu) == 0x7FFu);
	return 0;
}

/* Mode OFF (and any unknown mode) is inert: every output 0, whatever else. */
static int TestOffInert(void)
{
	static const uint32_t modes[] = {MODE_OFF, 3u, 0xFFFFFFFFu};

	for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
	{
		struct MainArcadeLinkPolicyInput input = TitleInput(modes[m]);
		struct MainArcadeLinkPolicyOutput output;

		input.hostScreenActive = 1u;
		input.boxHidden = 1u;
		input.submenuOpen = 1u;
		input.rawHeld = RAW_START | RAW_CROSS;
		input.prevRawHeld = 0u;
		output = Decide(&input);
		CHECK(AllZero(&output, sizeof(output)));

		input.titleState = T_INTRO;
		input.introFrame = 0;
		input.levelIsMainMenu = 0u;
		output = Decide(&input);
		CHECK(AllZero(&output, sizeof(output)));
	}
	return 0;
}

/* Every title state and the intro-frame boundary, in both active modes. */
static int TestTitleStates(void)
{
	static const uint32_t modes[] = {MODE_LINK, MODE_PREVIEW};

	for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
	{
		struct MainArcadeLinkPolicyInput input = TitleInput(modes[m]);
		struct MainArcadeLinkPolicyOutput output;

		/* The intro before the menu-ready frame is retail: the box is
		 * input-less and undrawn, and the intro skip must keep working. */
		input.titleState = T_INTRO;
		input.introFrame = 0;
		output = Decide(&input);
		CHECK(NotOwned(&output, 0u));
		input.introFrame = -1;
		output = Decide(&input);
		CHECK(NotOwned(&output, 0u));
		input.introFrame = READY - 1;
		output = Decide(&input);
		CHECK(NotOwned(&output, 0u));

		/* From the menu-ready frame the box slides in, still in INTRO. */
		input.introFrame = READY;
		output = Decide(&input);
		CHECK(Owned(&output));
		input.introFrame = READY + 1;
		output = Decide(&input);
		CHECK(Owned(&output));
		/* The retail intro skip jumps straight to frame 1000. */
		input.introFrame = SKIP_FRAME;
		output = Decide(&input);
		CHECK(Owned(&output));

		/* IN_MENU, EXITING, RETURNING, and any other state: owned, whatever
		 * the intro frame says. */
		{
			static const uint32_t states[] = {T_IN_MENU, T_EXITING, T_RETURNING, 4u, 0xFFFFFFFFu};
			static const int32_t frames[] = {0, READY - 1, READY, SKIP_FRAME, -1};

			for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); s++)
			{
				for (size_t f = 0; f < sizeof(frames) / sizeof(frames[0]); f++)
				{
					input.titleState = states[s];
					input.introFrame = frames[f];
					output = Decide(&input);
					CHECK(Owned(&output));
				}
			}
		}
	}
	return 0;
}

/* The retail title code turns INTRO into IN_MENU (and RETURNING into
 * IN_MENU) inside the box's funcPtr, after the hook has decided. Every frame
 * of both transitions must be owned, so no frame is left in between. */
static int TestNoGapFrame(void)
{
	struct MainArcadeLinkPolicyInput input = TitleInput(MODE_LINK);
	struct MainArcadeLinkPolicyOutput output;
	int32_t frame;

	/* Intro running up to the menu-ready frame, the 12-frame slide-in, then
	 * IN_MENU: the first owned frame is exactly the menu-ready frame, and
	 * nothing after it is ever released. */
	input.titleState = T_INTRO;
	for (frame = READY - 3; frame <= READY + 12; frame++)
	{
		input.introFrame = frame;
		output = Decide(&input);
		CHECK((output.owns != 0u) == (frame >= READY));
	}
	input.titleState = T_IN_MENU;
	output = Decide(&input);
	CHECK(Owned(&output));

	/* The skip path: intro frame jumps from early in the intro to 1000. */
	input.titleState = T_INTRO;
	input.introFrame = 40;
	output = Decide(&input);
	CHECK(NotOwned(&output, 0u));
	input.introFrame = SKIP_FRAME;
	output = Decide(&input);
	CHECK(Owned(&output));
	input.titleState = T_IN_MENU;
	output = Decide(&input);
	CHECK(Owned(&output));

	/* RETURNING slides in and becomes IN_MENU. */
	input.titleState = T_RETURNING;
	output = Decide(&input);
	CHECK(Owned(&output));
	input.titleState = T_IN_MENU;
	output = Decide(&input);
	CHECK(Owned(&output));
	return 0;
}

/* An open submenu never relaxes ownership: identical output either way. */
static int TestSubmenuOpen(void)
{
	static const uint32_t modes[] = {MODE_LINK, MODE_PREVIEW};
	static const uint32_t states[] = {T_INTRO, T_IN_MENU, T_EXITING, T_RETURNING};

	for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
	{
		for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); s++)
		{
			struct MainArcadeLinkPolicyInput input = TitleInput(modes[m]);
			struct MainArcadeLinkPolicyOutput closed;
			struct MainArcadeLinkPolicyOutput open;

			input.titleState = states[s];
			input.introFrame = SKIP_FRAME;
			input.rawHeld = RAW_CROSS;
			input.submenuOpen = 0u;
			closed = Decide(&input);
			input.submenuOpen = 1u;
			open = Decide(&input);
			CHECK(Owned(&open));
			CHECK(memcmp(&closed, &open, sizeof(open)) == 0);

			input.boxHidden = 1u;
			open = Decide(&input);
			CHECK(Owned(&open));
		}
	}
	return 0;
}

/* Off the idle main-menu level, or without the retail box, the title window
 * is closed: PREVIEW releases the frame; LINK owns it only with a link screen. */
static int TestOutsideTitleWindow(void)
{
	static const uint32_t modes[] = {MODE_LINK, MODE_PREVIEW};

	for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
	{
		for (int which = 0; which < 3; which++)
		{
			struct MainArcadeLinkPolicyInput input = TitleInput(modes[m]);
			struct MainArcadeLinkPolicyOutput output;

			if (which == 0) input.levelIsMainMenu = 0u;
			if (which == 1) input.loading = 1u;
			if (which == 2) input.mainMenuBoxActive = 0u;

			/* PREVIEW's host always reports a screen: it must not own. */
			input.hostScreenActive = (modes[m] == MODE_PREVIEW) ? 1u : 0u;
			output = Decide(&input);
			CHECK(NotOwned(&output, 0u));

			input.hostScreenActive = 1u;
			output = Decide(&input);
			if (modes[m] == MODE_LINK)
			{
				CHECK(Owned(&output));
				CHECK(output.enterPressed == 0u);
				CHECK(output.resetDemoCountdown == 1u);
			}
			else
			{
				CHECK(NotOwned(&output, 0u));
			}
		}
	}
	return 0;
}

/* LINK attract entry: a rising START or CROSS only. */
static int TestLinkEnter(void)
{
	struct MainArcadeLinkPolicyInput input = TitleInput(MODE_LINK);
	struct MainArcadeLinkPolicyOutput output;

	/* Idle attract screen: owned, the demo countdown runs as retail. */
	output = Decide(&input);
	CHECK(Owned(&output));
	CHECK(output.enterPressed == 0u);
	CHECK(output.resetDemoCountdown == 0u);
	CHECK(output.heldButtons == 0u);

	/* Rising START, rising CROSS. */
	input.rawHeld = RAW_START;
	output = Decide(&input);
	CHECK(output.enterPressed == 1u);
	CHECK(output.resetDemoCountdown == 1u);
	CHECK(output.heldButtons == NATIVE_ARCADE_MENU_BUTTON_START);
	input.rawHeld = RAW_CROSS;
	output = Decide(&input);
	CHECK(output.enterPressed == 1u);

	/* Held, not rising: no entry. */
	input.rawHeld = RAW_CROSS;
	input.prevRawHeld = RAW_CROSS;
	output = Decide(&input);
	CHECK(Owned(&output));
	CHECK(output.enterPressed == 0u);
	CHECK(output.resetDemoCountdown == 0u);
	CHECK(output.heldButtons == NATIVE_ARCADE_MENU_BUTTON_CROSS);

	/* One held, the other rising: entry. */
	input.rawHeld = RAW_CROSS | RAW_START;
	input.prevRawHeld = RAW_CROSS;
	output = Decide(&input);
	CHECK(output.enterPressed == 1u);

	/* Only the raw second Cross bit changed: not a mapped press. */
	input.rawHeld = RAW_CROSS | 0x4000u;
	input.prevRawHeld = RAW_CROSS;
	output = Decide(&input);
	CHECK(output.enterPressed == 0u);

	/* Other buttons never enter. */
	input.prevRawHeld = 0u;
	input.rawHeld = RAW_SQUARE | RAW_TRIANGLE | RAW_CIRCLE | RAW_L1 | MAIN_ARCADE_LINK_POLICY_BTN_SELECT;
	output = Decide(&input);
	CHECK(output.enterPressed == 0u);

	/* During the slide-in the attract screen is up and entry works. */
	input.titleState = T_INTRO;
	input.introFrame = READY;
	input.rawHeld = RAW_START;
	output = Decide(&input);
	CHECK(Owned(&output));
	CHECK(output.enterPressed == 1u);
	input.titleState = T_RETURNING;
	output = Decide(&input);
	CHECK(output.enterPressed == 1u);

	/* EXITING (the demo is on its way): owned, but no entry. */
	input.titleState = T_EXITING;
	output = Decide(&input);
	CHECK(Owned(&output));
	CHECK(output.enterPressed == 0u);
	CHECK(output.resetDemoCountdown == 0u);

	/* A link screen is already up: no entry, countdown held. */
	input.titleState = T_IN_MENU;
	input.hostScreenActive = 1u;
	output = Decide(&input);
	CHECK(Owned(&output));
	CHECK(output.enterPressed == 0u);
	CHECK(output.resetDemoCountdown == 1u);

	/* Not owned (intro before the menu-ready frame): no entry. */
	input.hostScreenActive = 0u;
	input.titleState = T_INTRO;
	input.introFrame = READY - 1;
	output = Decide(&input);
	CHECK(NotOwned(&output, 0u));
	CHECK(output.heldButtons == NATIVE_ARCADE_MENU_BUTTON_START);
	return 0;
}

/* PREVIEW never enters and holds the demo countdown on every owned frame. */
static int TestPreview(void)
{
	struct MainArcadeLinkPolicyInput input = TitleInput(MODE_PREVIEW);
	struct MainArcadeLinkPolicyOutput output;
	static const uint32_t states[] = {T_IN_MENU, T_EXITING, T_RETURNING};

	for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); s++)
	{
		input.titleState = states[s];
		input.rawHeld = RAW_START | RAW_CROSS;
		input.prevRawHeld = 0u;
		output = Decide(&input);
		CHECK(Owned(&output));
		CHECK(output.enterPressed == 0u);
		CHECK(output.resetDemoCountdown == 1u);
		CHECK(output.heldButtons == (NATIVE_ARCADE_MENU_BUTTON_START | NATIVE_ARCADE_MENU_BUTTON_CROSS));

		input.hostScreenActive = 0u;
		output = Decide(&input);
		CHECK(Owned(&output));
		CHECK(output.enterPressed == 0u);
		CHECK(output.resetDemoCountdown == 1u);
		input.hostScreenActive = 1u;
	}

	/* Boot: not the title window yet, so nothing is owned. */
	input.titleState = T_INTRO;
	input.introFrame = 0;
	output = Decide(&input);
	CHECK(NotOwned(&output, 0u));
	return 0;
}

/* Restore once the layer lets go, and only if it had hidden the box. */
static int TestRestore(void)
{
	static const uint32_t modes[] = {MODE_LINK, MODE_PREVIEW};

	for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
	{
		struct MainArcadeLinkPolicyInput input = TitleInput(modes[m]);
		struct MainArcadeLinkPolicyOutput output;

		input.boxHidden = 1u;
		output = Decide(&input);
		CHECK(Owned(&output));

		/* Leaving the main-menu level. */
		input.levelIsMainMenu = 0u;
		input.hostScreenActive = (modes[m] == MODE_PREVIEW) ? 1u : 0u;
		output = Decide(&input);
		CHECK(NotOwned(&output, 1u));
		input.boxHidden = 0u;
		output = Decide(&input);
		CHECK(NotOwned(&output, 0u));

		/* Loading, the box closed, and a fresh intro all restore. */
		input = TitleInput(modes[m]);
		input.boxHidden = 1u;
		input.loading = 1u;
		output = Decide(&input);
		CHECK(NotOwned(&output, 1u));
		input.loading = 0u;
		input.mainMenuBoxActive = 0u;
		output = Decide(&input);
		CHECK(NotOwned(&output, 1u));
		input.mainMenuBoxActive = 1u;
		input.titleState = T_INTRO;
		input.introFrame = 0;
		output = Decide(&input);
		CHECK(NotOwned(&output, 1u));
	}
	return 0;
}

/* heldButtons is reported on owned and released frames alike. */
static int TestHeldReported(void)
{
	struct MainArcadeLinkPolicyInput input = TitleInput(MODE_LINK);
	struct MainArcadeLinkPolicyOutput output;

	input.rawHeld = MAIN_ARCADE_LINK_POLICY_BTN_UP | RAW_TRIANGLE | MAIN_ARCADE_LINK_POLICY_BTN_R1;
	output = Decide(&input);
	CHECK(output.heldButtons ==
		(NATIVE_ARCADE_MENU_BUTTON_UP | NATIVE_ARCADE_MENU_BUTTON_TRIANGLE | NATIVE_ARCADE_MENU_BUTTON_R1));
	input.levelIsMainMenu = 0u;
	output = Decide(&input);
	CHECK(output.owns == 0u);
	CHECK(output.heldButtons ==
		(NATIVE_ARCADE_MENU_BUTTON_UP | NATIVE_ARCADE_MENU_BUTTON_TRIANGLE | NATIVE_ARCADE_MENU_BUTTON_R1));
	return 0;
}

int main(void)
{
	if (TestNullArguments() != 0) return 1;
	if (TestMapHeld() != 0) return 1;
	if (TestOffInert() != 0) return 1;
	if (TestTitleStates() != 0) return 1;
	if (TestNoGapFrame() != 0) return 1;
	if (TestSubmenuOpen() != 0) return 1;
	if (TestOutsideTitleWindow() != 0) return 1;
	if (TestLinkEnter() != 0) return 1;
	if (TestPreview() != 0) return 1;
	if (TestRestore() != 0) return 1;
	if (TestHeldReported() != 0) return 1;
	printf("main_arcade_link_policy_test: ok\n");
	return 0;
}
