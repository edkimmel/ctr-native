#include "MAIN/MainArcadeLinkPolicy.h"

#include <stddef.h>
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
		(output->tickOnly == 0u) && (output->reserved[0] == 0u);
}

/* A frame left to retail: nothing but the held mapping and a restore. */
static int NotOwned(const struct MainArcadeLinkPolicyOutput *output, uint8_t restore)
{
	return (output->owns == 0u) && (output->hideBox == 0u) && (output->clearTaps == 0u) && (output->enterPressed == 0u) &&
		(output->resetDemoCountdown == 0u) && (output->restoreBox == restore) && (output->tickOnly == 0u) &&
		(output->reserved[0] == 0u);
}

/* A race frame (RL-8): the host tick with the held buttons, nothing else. */
static int TickOnly(const struct MainArcadeLinkPolicyOutput *output, uint32_t held)
{
	return (output->tickOnly == 1u) && (output->heldButtons == held) && (output->owns == 0u) &&
		(output->enterPressed == 0u) && (output->hideBox == 0u) && (output->restoreBox == 0u) &&
		(output->resetDemoCountdown == 0u) && (output->clearTaps == 0u) && (output->reserved[0] == 0u);
}

/* The struct layouts: hostRacing and tickOnly took the first reserved byte
 * of their structs, so every other offset and both sizes are unchanged. */
static int TestLayout(void)
{
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, hostMode) == 0u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, titleState) == 4u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, introFrame) == 8u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, rawHeld) == 12u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, prevRawHeld) == 16u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, hostScreenActive) == 20u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, levelIsMainMenu) == 21u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, loading) == 22u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, mainMenuBoxActive) == 23u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, submenuOpen) == 24u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, boxHidden) == 25u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, hostRacing) == 26u);
	CHECK(offsetof(struct MainArcadeLinkPolicyInput, reserved) == 27u);
	CHECK(sizeof(((struct MainArcadeLinkPolicyInput *)NULL)->reserved) == 1u);
	CHECK(sizeof(struct MainArcadeLinkPolicyInput) == 28u);

	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, heldButtons) == 0u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, owns) == 4u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, enterPressed) == 5u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, hideBox) == 6u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, restoreBox) == 7u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, resetDemoCountdown) == 8u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, clearTaps) == 9u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, tickOnly) == 10u);
	CHECK(offsetof(struct MainArcadeLinkPolicyOutput, reserved) == 11u);
	CHECK(sizeof(((struct MainArcadeLinkPolicyOutput *)NULL)->reserved) == 1u);
	CHECK(sizeof(struct MainArcadeLinkPolicyOutput) == 12u);
	return 0;
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

/* The public menu-ready condition is exactly the title window Decide owns in
 * PREVIEW mode, and ignores the host mode, pads, and box flags. */
static int TestTitleMenuReady(void)
{
	static const uint32_t modes[] = {MODE_OFF, MODE_LINK, MODE_PREVIEW, 7u};
	static const uint32_t states[] = {T_INTRO, T_IN_MENU, T_EXITING, T_RETURNING, 4u};
	static const int32_t frames[] = {-1, 0, READY - 1, READY, READY + 1, SKIP_FRAME};

	CHECK(MainArcadeLinkPolicy_TitleMenuReady(NULL) == 0);
	for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
	{
		for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); s++)
		{
			for (size_t f = 0; f < sizeof(frames) / sizeof(frames[0]); f++)
			{
				for (uint32_t flags = 0; flags < 8u; flags++)
				{
					struct MainArcadeLinkPolicyInput input = TitleInput(MODE_PREVIEW);
					struct MainArcadeLinkPolicyOutput output;
					int ready;

					input.titleState = states[s];
					input.introFrame = frames[f];
					input.levelIsMainMenu = (uint8_t)((flags & 1u) == 0u);
					input.loading = (uint8_t)((flags & 2u) != 0u);
					input.mainMenuBoxActive = (uint8_t)((flags & 4u) == 0u);
					output = Decide(&input);
					input.hostMode = modes[m];
					input.rawHeld = RAW_START;
					input.submenuOpen = 1u;
					input.boxHidden = 1u;
					ready = MainArcadeLinkPolicy_TitleMenuReady(&input);
					CHECK(ready == (int)output.owns);
					CHECK(ready == ((flags == 0u) && ((states[s] != T_INTRO) || (frames[f] >= READY))));
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

/* RL-8: race frames are ticked, not owned. In LINK mode with the flow on
 * RACING, off the idle main-menu level (another level, or any load), the
 * frame is tickOnly: heldButtons as today and every other output 0. */
static int TestRaceFramesTickOnly(void)
{
	struct MainArcadeLinkPolicyInput input;
	struct MainArcadeLinkPolicyOutput output;
	struct MainArcadeLinkPolicyOutput today;
	const uint32_t rawHeld = RAW_START | RAW_CROSS | MAIN_ARCADE_LINK_POLICY_BTN_LEFT | RAW_L1;
	const uint32_t held = NATIVE_ARCADE_MENU_BUTTON_START | NATIVE_ARCADE_MENU_BUTTON_CROSS | NATIVE_ARCADE_MENU_BUTTON_LEFT;

	/* On a race level: the RACING screen counts as active in the host, and a
	 * rising START or CROSS still never enters. */
	input = TitleInput(MODE_LINK);
	input.levelIsMainMenu = 0u;
	input.mainMenuBoxActive = 0u;
	input.hostScreenActive = 1u;
	input.hostRacing = 1u;
	input.rawHeld = rawHeld;
	input.prevRawHeld = 0u;
	output = Decide(&input);
	CHECK(TickOnly(&output, held));
	CHECK(output.heldButtons == MainArcadeLinkPolicy_MapHeld(rawHeld));
	input.hostRacing = 0u;
	today = Decide(&input);
	CHECK(Owned(&today));
	CHECK(today.heldButtons == output.heldButtons);

	/* A box the title frames hid stays hidden: no restore on a race frame. */
	input.hostRacing = 1u;
	input.boxHidden = 1u;
	output = Decide(&input);
	CHECK(TickOnly(&output, held));
	input.hostScreenActive = 0u;
	output = Decide(&input);
	CHECK(TickOnly(&output, held));
	input.hostRacing = 0u;
	today = Decide(&input);
	CHECK(NotOwned(&today, 1u));

	/* The main-menu level while loading (the race-track load staged over
	 * rendered frames): tickOnly, even with the title window's other facts. */
	input = TitleInput(MODE_LINK);
	input.loading = 1u;
	input.hostScreenActive = 1u;
	input.hostRacing = 1u;
	input.rawHeld = rawHeld;
	output = Decide(&input);
	CHECK(TickOnly(&output, held));
	input.hostRacing = 0u;
	today = Decide(&input);
	CHECK(Owned(&today));
	CHECK(today.resetDemoCountdown == 1u);

	/* The idle main-menu level: RACING is owned as today, tickOnly 0. */
	input = TitleInput(MODE_LINK);
	input.hostScreenActive = 1u;
	input.hostRacing = 1u;
	input.rawHeld = rawHeld;
	output = Decide(&input);
	CHECK(Owned(&output));
	CHECK(output.resetDemoCountdown == 1u);
	CHECK(output.enterPressed == 0u);
	CHECK(output.heldButtons == held);
	input.hostRacing = 0u;
	today = Decide(&input);
	CHECK(memcmp(&output, &today, sizeof(output)) == 0);

	/* The full grid: tickOnly exactly when LINK, hostRacing nonzero (2 and
	 * 0xFF count as 1, like the policy's other flags), and not the idle
	 * main-menu level; otherwise the output is byte-for-byte today's. The
	 * previous held word varies between a rising START or CROSS and none. */
	{
		static const uint32_t modes[] = {MODE_OFF, MODE_LINK, MODE_PREVIEW, 3u, 0xFFFFFFFFu};
		static const uint8_t racings[] = {0u, 1u, 2u, 0xFFu};
		static const uint32_t states[] = {T_INTRO, T_IN_MENU, T_EXITING, T_RETURNING};
		/* rawHeld holds START and CROSS: whether either rises against each
		 * previous held word. */
		static const struct
		{
			uint32_t prevRawHeld;
			int rising;
		} prevs[] = {
			{0u, 1},
			{RAW_START, 1},
			{RAW_CROSS, 1},
			{RAW_START | RAW_CROSS, 0},
			{RAW_START | RAW_CROSS | RAW_SQUARE | RAW_L1, 0},
		};

		for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
		{
			for (size_t r = 0; r < sizeof(racings) / sizeof(racings[0]); r++)
			{
				for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); s++)
				{
					for (size_t p = 0; p < sizeof(prevs) / sizeof(prevs[0]); p++)
					{
						for (uint32_t flags = 0; flags < 64u; flags++)
						{
							int expectTick;
							int expectOwns;
							int expectEnter;
							int expectReset;

							input = TitleInput(modes[m]);
							input.titleState = states[s];
							input.introFrame = ((flags & 32u) != 0u) ? 0 : SKIP_FRAME;
							input.levelIsMainMenu = (uint8_t)((flags & 1u) == 0u);
							input.loading = (uint8_t)((flags & 2u) != 0u);
							input.hostScreenActive = (uint8_t)((flags & 4u) != 0u);
							input.boxHidden = (uint8_t)((flags & 8u) != 0u);
							input.mainMenuBoxActive = (uint8_t)((flags & 16u) == 0u);
							input.rawHeld = rawHeld;
							input.prevRawHeld = prevs[p].prevRawHeld;
							input.hostRacing = 0u;
							today = Decide(&input);
							input.hostRacing = racings[r];
							output = Decide(&input);

							expectTick = (modes[m] == MODE_LINK) && (racings[r] != 0u) &&
								((input.levelIsMainMenu == 0u) || (input.loading != 0u));
							/* hostRacing 0 is today's rule: the title window in LINK
							 * and PREVIEW, or a LINK screen, on any level or load. */
							expectOwns = (((modes[m] == MODE_LINK) || (modes[m] == MODE_PREVIEW)) &&
									 (MainArcadeLinkPolicy_TitleMenuReady(&input) != 0)) ||
								((modes[m] == MODE_LINK) && (input.hostScreenActive != 0u));
							/* Attract entry: LINK, owned, no link screen, not EXITING,
							 * and a rising START or CROSS. */
							expectEnter = expectOwns && (modes[m] == MODE_LINK) && (input.hostScreenActive == 0u) &&
								(states[s] != T_EXITING) && (prevs[p].rising != 0);
							/* Demo countdown reset: owned, and PREVIEW, a LINK screen,
							 * or an attract entry. */
							expectReset = expectOwns &&
								((modes[m] == MODE_PREVIEW) || ((modes[m] == MODE_LINK) && (input.hostScreenActive != 0u)) || expectEnter);
							CHECK(today.tickOnly == 0u);
							CHECK((int)today.owns == expectOwns);
							CHECK(today.restoreBox == (uint8_t)((today.owns == 0u) &&
								((modes[m] == MODE_LINK) || (modes[m] == MODE_PREVIEW)) && (input.boxHidden != 0u)));
							CHECK((int)today.enterPressed == expectEnter);
							CHECK((int)today.resetDemoCountdown == expectReset);
							if (expectTick != 0)
							{
								CHECK(TickOnly(&output, today.heldButtons));
								CHECK(output.heldButtons == held);
							}
							else
							{
								CHECK(output.tickOnly == 0u);
								CHECK(memcmp(&output, &today, sizeof(output)) == 0);
							}
						}
					}
				}
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

#define STAGE_IDLE MAIN_ARCADE_LINK_POLICY_STAGE_IDLE
#define STAGE_REQUESTED MAIN_ARCADE_LINK_POLICY_STAGE_REQUESTED
#define STAGE_OTHER MAIN_ARCADE_LINK_POLICY_STAGE_OTHER

/* Race-launch risk 10: the link's return step runs only while no level load
 * is running, and is deferred, not dropped, otherwise. */
static int TestReturnStep(void)
{
	uint8_t pending;
	int frame;

	CHECK(STAGE_IDLE != STAGE_REQUESTED);
	CHECK(STAGE_REQUESTED != STAGE_OTHER);
	CHECK(STAGE_IDLE != STAGE_OTHER);

	/* NULL: nothing runs. */
	CHECK(MainArcadeLinkPolicy_ReturnStep(NULL, 1u, STAGE_IDLE, 0u) == 0);

	/* The action at IDLE or REQUESTED runs now; nothing is owed. */
	pending = 0u;
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 1u, STAGE_IDLE, 0u) == 1);
	CHECK(pending == 0u);
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 1u, STAGE_REQUESTED, 0u) == 1);
	CHECK(pending == 0u);

	/* The action while a load runs: owed, not run. */
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 1u, STAGE_OTHER, 0u) == 0);
	CHECK(pending == 1u);

	/* Still owed for as long as the load runs. */
	for (frame = 0; frame < 1000; frame++)
	{
		CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_OTHER, 0u) == 0);
		CHECK(pending == 1u);
	}
	/* Any unknown stage value counts as OTHER. */
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, 3u, 0u) == 0);
	CHECK(pending == 1u);
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, 0xFFFFFFFFu, 0u) == 0);
	CHECK(pending == 1u);
	/* A second action while owed merges with it. */
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 1u, STAGE_OTHER, 0u) == 0);
	CHECK(pending == 1u);

	/* Then the first IDLE frame runs it once and clears it. */
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_IDLE, 0u) == 1);
	CHECK(pending == 0u);
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_IDLE, 0u) == 0);
	CHECK(pending == 0u);

	/* A REQUESTED frame services an owed step too. */
	pending = 1u;
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_REQUESTED, 0u) == 1);
	CHECK(pending == 0u);

	/* Owed, then already on the main-menu level: cleared with no run, at
	 * every stage. */
	{
		static const uint32_t stages[] = {STAGE_IDLE, STAGE_REQUESTED, STAGE_OTHER};

		for (size_t s = 0; s < sizeof(stages) / sizeof(stages[0]); s++)
		{
			pending = 1u;
			CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, stages[s], 1u) == 0);
			CHECK(pending == 0u);
			CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_IDLE, 0u) == 0);
			CHECK(pending == 0u);

			/* The action on the main-menu level: no run, nothing owed. */
			CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 1u, stages[s], 1u) == 0);
			CHECK(pending == 0u);
		}
	}

	/* No action and nothing owed: never runs, on any stage or level. */
	{
		static const uint32_t stages[] = {STAGE_IDLE, STAGE_REQUESTED, STAGE_OTHER, 3u};

		for (size_t s = 0; s < sizeof(stages) / sizeof(stages[0]); s++)
		{
			for (uint8_t level = 0u; level < 2u; level++)
			{
				pending = 0u;
				CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, stages[s], level) == 0);
				CHECK(pending == 0u);
			}
		}
	}

	/* The flags count any nonzero value as 1. */
	pending = 0u;
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0xFFu, STAGE_OTHER, 0u) == 0);
	CHECK(pending == 1u);
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_OTHER, 2u) == 0);
	CHECK(pending == 0u);
	pending = 7u;
	CHECK(MainArcadeLinkPolicy_ReturnStep(&pending, 0u, STAGE_IDLE, 0u) == 1);
	CHECK(pending == 0u);
	return 0;
}

int main(void)
{
	if (TestLayout() != 0) return 1;
	if (TestNullArguments() != 0) return 1;
	if (TestMapHeld() != 0) return 1;
	if (TestOffInert() != 0) return 1;
	if (TestTitleStates() != 0) return 1;
	if (TestTitleMenuReady() != 0) return 1;
	if (TestNoGapFrame() != 0) return 1;
	if (TestSubmenuOpen() != 0) return 1;
	if (TestOutsideTitleWindow() != 0) return 1;
	if (TestRaceFramesTickOnly() != 0) return 1;
	if (TestLinkEnter() != 0) return 1;
	if (TestPreview() != 0) return 1;
	if (TestRestore() != 0) return 1;
	if (TestHeldReported() != 0) return 1;
	if (TestReturnStep() != 0) return 1;
	printf("main_arcade_link_policy_test: ok\n");
	return 0;
}
