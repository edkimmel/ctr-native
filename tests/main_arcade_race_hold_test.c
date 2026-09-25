/*
 * Unit test of the stall hold loop itself, MainArcadeRaceHold_Run and (since
 * LR-S10 part 2) MainArcadeRaceHold_RunMode
 * (game/MAIN/MainArcadeRaceHold.c; docs/LOCKSTEP_RACE_MILESTONE.md LR-9,
 * slice LR-S2 (a)), over stubbed platform calls: a simulated host clock that
 * only the host wait advances, a counting event pump, and a banner present
 * whose result the test chooses. It checks the LR-9 order of the host work,
 * that a step returning 0 ends the loop before that iteration's banner and
 * wait, that a NULL step stops after one pump, the banners due against the
 * banners presented, and the result the loop fills.
 */
#include <platform.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MAIN/MainArcadeRaceHold.h"
#include "MAIN/MainArcadeRaceHoldCore.h"
#include "platform/native_hold_banner.h"

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define START_US UINT64_C(7000000000)
#define TRACE_MAX 64u

/* The stubs' state. */
static uint64_t s_nowUs;
static uint32_t s_pumps;
static uint32_t s_clockReads;
static uint32_t s_waits;
static uint32_t s_waitMsTotal;
static uint32_t s_bannerCalls;      /* both presents */
static uint32_t s_blockBannerCalls; /* Platform_PresentVRAMDisplayBanner */
static uint32_t s_glyphBannerCalls; /* Platform_PresentVRAMDisplayBannerGlyphs (LR-S11) */
static const struct NativeHoldBannerGlyphs *s_expectedGlyphs;
static int s_glyphsPassedOk; /* every glyph present got s_expectedGlyphs */
static uint32_t s_bannerPresented;
static int s_bannerTextOk;
static int s_bannerReturn; /* 1: present every banner, 0: none, 2: every other one */
static char s_trace[TRACE_MAX + 1u];
static uint32_t s_traceLength;
static char s_lastEvent; /* the previous host call: 0, P, S, B, or W */
static int s_orderOk;    /* every call followed the LR-9 order P S [B] W */

/* Records one host call c; allowedPrevious lists the calls that may come
 * right before it ('0': none yet). */
static void Event(char c, const char *allowedPrevious)
{
	const char previous = (s_lastEvent == '\0') ? '0' : s_lastEvent;

	if (strchr(allowedPrevious, previous) == NULL)
	{
		s_orderOk = 0;
	}
	s_lastEvent = c;
	if (s_traceLength < TRACE_MAX)
	{
		s_trace[s_traceLength++] = c;
		s_trace[s_traceLength] = '\0';
	}
}

static void StubReset(int bannerReturn)
{
	s_nowUs = START_US;
	s_pumps = 0u;
	s_clockReads = 0u;
	s_waits = 0u;
	s_waitMsTotal = 0u;
	s_bannerCalls = 0u;
	s_blockBannerCalls = 0u;
	s_glyphBannerCalls = 0u;
	s_expectedGlyphs = NULL;
	s_glyphsPassedOk = 1;
	s_bannerPresented = 0u;
	s_bannerTextOk = 1;
	s_bannerReturn = bannerReturn;
	s_trace[0] = '\0';
	s_traceLength = 0u;
	s_lastEvent = 0;
	s_orderOk = 1;
}

void Platform_PollHostEvents(void)
{
	s_pumps++;
	Event('P', "0W");
}

unsigned long long Platform_HostClockUs(void)
{
	s_clockReads++;
	return (unsigned long long)s_nowUs;
}

void Platform_HostWaitMs(unsigned int milliseconds)
{
	s_waits++;
	s_waitMsTotal += milliseconds;
	s_nowUs += (uint64_t)milliseconds * 1000u;
	Event('W', "SB");
}

static int BannerStub(const char *text);

int Platform_PresentVRAMDisplayBanner(const char *text)
{
	s_blockBannerCalls++;
	return BannerStub(text);
}

int Platform_PresentVRAMDisplayBannerGlyphs(const char *text, const struct NativeHoldBannerGlyphs *glyphs)
{
	s_glyphBannerCalls++;
	if (glyphs != s_expectedGlyphs)
	{
		s_glyphsPassedOk = 0;
	}
	return BannerStub(text);
}

static int BannerStub(const char *text)
{
	int presented;

	s_bannerCalls++;
	if ((text == NULL) || (strcmp(text, MAIN_ARCADE_RACE_HOLD_BANNER_TEXT) != 0))
	{
		s_bannerTextOk = 0;
	}
	Event('B', "S");
	presented = (s_bannerReturn == 2) ? (int)(s_bannerCalls & 1u) : s_bannerReturn;
	if (presented != 0)
	{
		s_bannerPresented++;
	}
	return presented;
}

/* A step: held while periods < limit; records what it saw. */
struct StepState
{
	uint32_t limit;
	uint32_t calls;
	uint32_t newPeriods;
	uint32_t lastPeriods;
	uint32_t bannerCallsAtFirstBannerPeriod; /* s_bannerCalls when periods first reached the grace */
	int periodsWentBack;
};

static int Step(void *context, uint32_t periods, int newPeriod)
{
	struct StepState *state = (struct StepState *)context;

	Event('S', "P");
	if (state->calls > 0u && periods < state->lastPeriods)
	{
		state->periodsWentBack = 1;
	}
	if ((periods == MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS) && newPeriod)
	{
		state->bannerCallsAtFirstBannerPeriod = s_bannerCalls;
	}
	state->calls++;
	state->lastPeriods = periods;
	if (newPeriod)
	{
		state->newPeriods++;
	}
	return periods < state->limit;
}

/* A NULL step ends the hold after exactly one pump: no step, banner, or wait. */
static int TestNullStep(void)
{
	struct MainArcadeRaceHoldResult result;

	StubReset(1);
	memset(&result, 0xA5, sizeof(result));
	MainArcadeRaceHold_Run(NULL, NULL, &result);
	CHECK(strcmp(s_trace, "P") == 0);
	CHECK(s_pumps == 1u && s_waits == 0u && s_bannerCalls == 0u);
	CHECK(result.pumps == 1u);
	CHECK(result.periods == 0u);
	CHECK(result.wallUs == 0u);
	CHECK(result.minPeriodPumps == MAIN_ARCADE_RACE_HOLD_NO_PERIOD);
	CHECK(result.bannersDue == 0u && result.bannersPresented == 0u);
	CHECK(result.reserved == 0u);

	/* A NULL result is allowed. */
	StubReset(1);
	MainArcadeRaceHold_Run(NULL, NULL, NULL);
	CHECK(s_pumps == 1u && s_waits == 0u);
	return 0;
}

/* The LR-9 order of each iteration: pump, step, then (after the grace) the
 * banner, then the wait; a step that returns 0 ends the loop at once. */
static int TestOrderAndEarlyEnd(void)
{
	struct StepState step;
	struct MainArcadeRaceHoldResult result;

	/* The step ends it at once: one pump and one step, no wait. */
	StubReset(1);
	memset(&step, 0, sizeof(step));
	step.limit = 0u; /* periods < 0 is never true: stop at the first step */
	MainArcadeRaceHold_Run(Step, &step, &result);
	CHECK(strcmp(s_trace, "PS") == 0);
	CHECK(result.pumps == 1u && result.periods == 0u && result.wallUs == 0u);

	/* Held 1 period: pump, step, wait, until the step ends it on a pump. */
	StubReset(1);
	memset(&step, 0, sizeof(step));
	step.limit = 1u;
	MainArcadeRaceHold_Run(Step, &step, &result);
	CHECK(strncmp(s_trace, "PSWPSWPSW", 9u) == 0);
	CHECK(s_orderOk == 1);
	CHECK(s_lastEvent == 'S'); /* the final step ended it: no wait after */
	CHECK(s_bannerCalls == 0u);
	CHECK(result.periods == 1u);
	CHECK(result.pumps == s_pumps && result.pumps == s_waits + 1u);
	return 0;
}

/* A step that ends the hold on the pump that reaches the grace: that pump is
 * due a banner, but the loop ends before it, so none is due or drawn. */
static int TestStepEndsBeforeGraceBanner(void)
{
	struct StepState step;
	struct MainArcadeRaceHoldResult result;

	StubReset(1);
	memset(&step, 0, sizeof(step));
	step.limit = MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS;
	MainArcadeRaceHold_Run(Step, &step, &result);
	CHECK(result.periods == MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS);
	CHECK(s_bannerCalls == 0u);
	CHECK(result.bannersDue == 0u && result.bannersPresented == 0u);
	CHECK(s_lastEvent == 'S');
	CHECK(step.newPeriods == MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS);
	CHECK(s_orderOk == 1);
	return 0;
}

/* The proof's hold: 45 periods at a 1 ms wait. The banner is due once per
 * period from period 10 on (35 times), and the result carries what the stubs
 * saw. */
static int TestFullHold(int bannerReturn, uint32_t expectedPresented)
{
	struct StepState step;
	struct MainArcadeRaceHoldResult result;
	const uint64_t expectedUs = MainArcadeRaceHoldCore_PeriodsToUs(45u);

	StubReset(bannerReturn);
	memset(&step, 0, sizeof(step));
	memset(&result, 0xA5, sizeof(result));
	step.limit = 45u;
	MainArcadeRaceHold_Run(Step, &step, &result);

	CHECK(result.periods == 45u);
	CHECK(step.periodsWentBack == 0);
	CHECK(step.newPeriods == 45u);
	CHECK(step.calls == s_pumps);
	CHECK(step.bannerCallsAtFirstBannerPeriod == 0u); /* no banner inside the grace */
	CHECK(s_bannerTextOk == 1);
	CHECK(s_orderOk == 1); /* every banner right after a step, before the wait */
	CHECK(s_lastEvent == 'S'); /* the final step ended it: no banner, no wait after */
	CHECK(s_bannerCalls == 45u - MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS);
	CHECK(result.bannersDue == s_bannerCalls);
	CHECK(result.bannersPresented == s_bannerPresented);
	CHECK(result.bannersPresented == expectedPresented);
	CHECK(result.pumps == s_pumps);
	CHECK(s_waits == s_pumps - 1u);
	CHECK(s_waitMsTotal == s_waits * MAIN_ARCADE_RACE_HOLD_WAIT_MS);
	CHECK(result.wallUs == s_nowUs - START_US);
	CHECK(result.wallUs >= expectedUs);
	CHECK(result.wallUs < expectedUs + 1000u);
	/* 33435 us periods at 1 ms per pump: 33 or 34 pumps in every ended period. */
	CHECK(result.minPeriodPumps == 33u);
	CHECK(result.reserved == 0u);
	/* The loop reads the clock once to begin, once per pump, once to end. */
	CHECK(s_clockReads == s_pumps + 2u);
	return 0;
}

/* The race caller's hold (LR-S10 part 2): MainArcadeRaceHold_RunMode without
 * the banner holds exactly as Run does (same periods, pumps, waits, and the
 * LR-9 order of the other host work), but no banner is due or presented,
 * even across the grace; with the banner mode (or any other nonzero mode)
 * it is Run. */
static int TestModes(void)
{
	static const uint32_t bannerModes[3] = {MAIN_ARCADE_RACE_HOLD_MODE_BANNER, 2u, 0xFFFFFFFFu};
	struct StepState step;
	struct MainArcadeRaceHoldResult result;
	struct MainArcadeRaceHoldResult reference;
	uint32_t referencePumps;

	CHECK(MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER == 0u && MAIN_ARCADE_RACE_HOLD_MODE_BANNER == 1u);
	/* The reference: Run, 45 periods, every banner presented. */
	StubReset(1);
	memset(&step, 0, sizeof(step));
	step.limit = 45u;
	MainArcadeRaceHold_Run(Step, &step, &reference);
	referencePumps = s_pumps;
	CHECK(reference.bannersDue == 35u && reference.bannersPresented == 35u);

	/* Without the banner. */
	StubReset(1);
	memset(&step, 0, sizeof(step));
	memset(&result, 0xA5, sizeof(result));
	step.limit = 45u;
	MainArcadeRaceHold_RunMode(Step, &step, MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER, NULL, &result);
	CHECK(s_bannerCalls == 0u && s_bannerPresented == 0u);
	CHECK(result.bannersDue == 0u && result.bannersPresented == 0u);
	CHECK(strchr(s_trace, 'B') == NULL);
	CHECK(s_orderOk == 1 && s_lastEvent == 'S');
	CHECK(result.periods == 45u && step.newPeriods == 45u && step.calls == s_pumps);
	CHECK(s_pumps == referencePumps && result.pumps == reference.pumps && result.wallUs == reference.wallUs);
	CHECK(result.minPeriodPumps == reference.minPeriodPumps && result.reserved == 0u);
	CHECK(s_waits == s_pumps - 1u);

	/* With the banner mode, or any other nonzero mode: Run's result. */
	for (uint32_t i = 0; i < 3u; i++)
	{
		StubReset(1);
		memset(&step, 0, sizeof(step));
		step.limit = 45u;
		MainArcadeRaceHold_RunMode(Step, &step, bannerModes[i], NULL, &result);
		CHECK(memcmp(&result, &reference, sizeof(result)) == 0);
		CHECK(s_bannerCalls == 35u && s_bannerTextOk == 1 && s_orderOk == 1);
	}

	/* A NULL step and a NULL result, without the banner: one pump. */
	StubReset(1);
	MainArcadeRaceHold_RunMode(NULL, NULL, MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER, NULL, NULL);
	CHECK(s_pumps == 1u && s_waits == 0u && s_bannerCalls == 0u);
	return 0;
}

/* The glyph table (LR-S11): with a table every due banner goes to the glyph
 * present with that exact table, never to the block present, and the hold
 * is otherwise Run's; without one (Run, the roster proof) every banner goes
 * to the block present; without the banner neither is called. The loop
 * never reads the table: a table of 0xA5 bytes passes through unchanged. */
static int TestGlyphs(void)
{
	static struct NativeHoldBannerGlyphs glyphs;
	static struct NativeHoldBannerGlyphs untouched;
	const struct NativeHoldBannerGlyphs *const table = &glyphs;
	struct StepState step;
	struct MainArcadeRaceHoldResult result;
	struct MainArcadeRaceHoldResult reference;

	memset(&glyphs, 0xA5, sizeof(glyphs));
	memset(&untouched, 0xA5, sizeof(untouched));
	/* Run: the block present only. */
	StubReset(1);
	memset(&step, 0, sizeof(step));
	step.limit = 45u;
	MainArcadeRaceHold_Run(Step, &step, &reference);
	CHECK(s_blockBannerCalls == 35u && s_glyphBannerCalls == 0u);

	/* RunMode with the banner and a table: the glyph present only. */
	StubReset(1);
	s_expectedGlyphs = table;
	memset(&step, 0, sizeof(step));
	step.limit = 45u;
	MainArcadeRaceHold_RunMode(Step, &step, MAIN_ARCADE_RACE_HOLD_MODE_BANNER, table, &result);
	CHECK(s_glyphBannerCalls == 35u && s_blockBannerCalls == 0u && s_glyphsPassedOk == 1);
	CHECK(s_bannerTextOk == 1 && s_orderOk == 1 && s_lastEvent == 'S');
	CHECK(memcmp(&result, &reference, sizeof(result)) == 0);

	/* Half presented: the counts follow the glyph present's answers. */
	StubReset(2);
	s_expectedGlyphs = table;
	memset(&step, 0, sizeof(step));
	step.limit = 45u;
	MainArcadeRaceHold_RunMode(Step, &step, MAIN_ARCADE_RACE_HOLD_MODE_BANNER, table, &result);
	CHECK(result.bannersDue == 35u && result.bannersPresented == 18u && s_glyphBannerCalls == 35u);

	/* A table without the banner: no present at all. */
	StubReset(1);
	s_expectedGlyphs = table;
	memset(&step, 0, sizeof(step));
	step.limit = 45u;
	MainArcadeRaceHold_RunMode(Step, &step, MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER, table, &result);
	CHECK(s_bannerCalls == 0u && result.bannersDue == 0u && result.bannersPresented == 0u);
	CHECK(memcmp(&glyphs, &untouched, sizeof(glyphs)) == 0);
	return 0;
}

int main(void)
{
	if (TestNullStep() != 0)
	{
		return 1;
	}
	if (TestOrderAndEarlyEnd() != 0)
	{
		return 1;
	}
	if (TestStepEndsBeforeGraceBanner() != 0)
	{
		return 1;
	}
	if (TestFullHold(1, 35u) != 0)
	{
		return 1;
	}
	if (TestFullHold(0, 0u) != 0)
	{
		return 1;
	}
	if (TestFullHold(2, 18u) != 0)
	{
		return 1;
	}
	if (TestModes() != 0)
	{
		return 1;
	}
	if (TestGlyphs() != 0)
	{
		return 1;
	}
	printf("main_arcade_race_hold_test: all tests passed\n");
	return 0;
}
