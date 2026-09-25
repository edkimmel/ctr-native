#include "MAIN/MainArcadeRaceHoldCore.h"
#include "platform/native_arcade_race_drive.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define PERIOD ((uint64_t)MAIN_ARCADE_RACE_HOLD_PERIOD_US)
#define START UINT64_C(5000000000)

/* The period: 2 native VBlanks (897619 GPU cycles at 53693175 Hz), rounded. */
static int TestConstants(void)
{
	const uint64_t exactNs = (UINT64_C(2) * UINT64_C(897619) * UINT64_C(1000000000)) / UINT64_C(53693175);

	CHECK(MAIN_ARCADE_RACE_HOLD_PERIOD_US == 33435u);
	CHECK((exactNs + 500u) / 1000u == (uint64_t)MAIN_ARCADE_RACE_HOLD_PERIOD_US);
	CHECK(MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS == 10u);
	CHECK(MainArcadeRaceHoldCore_PeriodsToUs(0u) == 0u);
	CHECK(MainArcadeRaceHoldCore_PeriodsToUs(45u) == UINT64_C(1504575));
	return 0;
}

static int TestBeginAndNull(void)
{
	struct MainArcadeRaceHoldCore core;

	MainArcadeRaceHoldCore_Begin(NULL, START);
	CHECK(MainArcadeRaceHoldCore_Pump(NULL, START) == 0u);
	CHECK(MainArcadeRaceHoldCore_PeriodsAt(NULL, START + (10u * PERIOD)) == 0u);
	MainArcadeRaceHoldCore_Begin(&core, START);
	CHECK(core.startUs == START && core.lastUs == START);
	CHECK(core.periods == 0u && core.pumps == 0u && core.periodPumps == 0u && core.banners == 0u);
	CHECK(core.minPeriodPumps == MAIN_ARCADE_RACE_HOLD_NO_PERIOD);
	CHECK(MainArcadeRaceHoldCore_PeriodsAt(&core, START - 1u) == 0u);
	CHECK(MainArcadeRaceHoldCore_PeriodsAt(&core, START + PERIOD - 1u) == 0u);
	CHECK(MainArcadeRaceHoldCore_PeriodsAt(&core, START + PERIOD) == 1u);
	CHECK(MainArcadeRaceHoldCore_PeriodsAt(&core, START + (45u * PERIOD)) == 45u);
	return 0;
}

/* A 1 ms loop for 45 periods: a new period each ~33 pumps, no banner in the
 * grace, then exactly one per period. */
static int TestSteadyHold(void)
{
	struct MainArcadeRaceHoldCore core;
	uint32_t newPeriods = 0u;
	uint32_t banners = 0u;
	uint32_t firstBannerPeriod = UINT32_MAX;
	uint64_t now = START;

	MainArcadeRaceHoldCore_Begin(&core, START);
	for (;;)
	{
		const uint32_t flags = MainArcadeRaceHoldCore_Pump(&core, now);

		if ((flags & MAIN_ARCADE_RACE_HOLD_NEW_PERIOD) != 0u)
		{
			newPeriods++;
			CHECK(core.periodPumps == 1u);
		}
		if ((flags & MAIN_ARCADE_RACE_HOLD_DRAW_BANNER) != 0u)
		{
			CHECK((flags & MAIN_ARCADE_RACE_HOLD_NEW_PERIOD) != 0u);
			CHECK(core.periods >= MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS);
			if (firstBannerPeriod == UINT32_MAX)
			{
				firstBannerPeriod = core.periods;
			}
			banners++;
		}
		else
		{
			CHECK(((flags & MAIN_ARCADE_RACE_HOLD_NEW_PERIOD) == 0u) || (core.periods < MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS));
		}
		if (core.periods >= 45u)
		{
			break;
		}
		now += 1000u;
	}
	CHECK(core.periods == 45u);
	CHECK(newPeriods == 45u);
	CHECK(firstBannerPeriod == MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS);
	CHECK(banners == 45u - MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS + 1u);
	CHECK(core.banners == banners);
	/* 33.435 ms per period at 1 ms per pump: 33 or 34 pumps each. */
	CHECK(core.minPeriodPumps == 33u);
	CHECK(core.pumps == (uint32_t)(((now - START) / 1000u) + 1u));
	return 0;
}

/* One late pump skips periods: they had no pump (min 0), and only one banner
 * is due for the jump. */
static int TestLatePump(void)
{
	struct MainArcadeRaceHoldCore core;
	uint32_t flags;

	MainArcadeRaceHoldCore_Begin(&core, START);
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START) == 0u);
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START + 1000u) == 0u);
	flags = MainArcadeRaceHoldCore_Pump(&core, START + PERIOD);
	CHECK(flags == MAIN_ARCADE_RACE_HOLD_NEW_PERIOD);
	CHECK(core.periods == 1u && core.minPeriodPumps == 2u);
	/* Period 1 had one pump, then a jump from period 1 to 12. */
	flags = MainArcadeRaceHoldCore_Pump(&core, START + (12u * PERIOD) + 5u);
	CHECK(flags == (MAIN_ARCADE_RACE_HOLD_NEW_PERIOD | MAIN_ARCADE_RACE_HOLD_DRAW_BANNER));
	CHECK(core.periods == 12u && core.minPeriodPumps == 0u && core.banners == 1u);
	CHECK(core.pumps == 4u && core.periodPumps == 1u);
	/* A clock that goes back never moves the periods back. */
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START) == 0u);
	CHECK(core.periods == 12u && core.periodPumps == 2u && core.lastUs == START + (12u * PERIOD) + 5u);
	/* Exactly one period later: one banner. */
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START + (13u * PERIOD)) ==
	      (MAIN_ARCADE_RACE_HOLD_NEW_PERIOD | MAIN_ARCADE_RACE_HOLD_DRAW_BANNER));
	CHECK(core.banners == 2u && core.minPeriodPumps == 0u);
	return 0;
}

/* The grace boundary: the 10th period (index 9) draws nothing, the 11th
 * (index 10) is the first banner; a pump before the start counts as the start. */
static int TestGraceBoundary(void)
{
	struct MainArcadeRaceHoldCore core;

	MainArcadeRaceHoldCore_Begin(&core, START);
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START - 7u) == 0u);
	CHECK(core.lastUs == START && core.periods == 0u);
	for (uint32_t period = 1u; period < MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS; period++)
	{
		CHECK(MainArcadeRaceHoldCore_Pump(&core, START + (period * PERIOD)) == MAIN_ARCADE_RACE_HOLD_NEW_PERIOD);
	}
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START + (10u * PERIOD) - 1u) == 0u);
	CHECK(core.banners == 0u);
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START + (10u * PERIOD)) ==
	      (MAIN_ARCADE_RACE_HOLD_NEW_PERIOD | MAIN_ARCADE_RACE_HOLD_DRAW_BANNER));
	CHECK(core.banners == 1u && core.minPeriodPumps == 1u);
	return 0;
}

/*
 * LR-S11: the hold's banner rule is the drive's BannerDue (LR-44), which the
 * host reports as the drive state's bannerDue. The same grace constant, and
 * for every period count the hold core asks for a banner on the first pump
 * of that period exactly when BannerDue(periods) holds: in a steady hold
 * (every period, 0..2000), after late pumps that skip periods (strides 2,
 * 3, 7, 9, 10, 11, 13, 45 and a jump straight past the grace), and at the
 * top of the range. A pump inside a period never draws.
 */
static int TestBannerDueMatchesDrive(void)
{
	static const uint32_t strides[] = {2u, 3u, 7u, 9u, 10u, 11u, 13u, 45u};
	struct MainArcadeRaceHoldCore core;

	CHECK(MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS == NATIVE_ARCADE_RACE_DRIVE_HOLD_GRACE_PERIODS);
	for (uint32_t periods = 0u; periods <= 2000u; periods++)
	{
		CHECK((NativeArcadeRaceDrive_BannerDue(periods) != 0) == (periods >= MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS));
	}
	CHECK(NativeArcadeRaceDrive_BannerDue(UINT32_MAX) != 0);

	MainArcadeRaceHoldCore_Begin(&core, START);
	for (uint32_t period = 1u; period <= 2000u; period++)
	{
		const uint32_t flags = MainArcadeRaceHoldCore_Pump(&core, START + (period * PERIOD));

		CHECK(core.periods == period);
		CHECK(((flags & MAIN_ARCADE_RACE_HOLD_DRAW_BANNER) != 0u) == (NativeArcadeRaceDrive_BannerDue(core.periods) != 0));
		CHECK((MainArcadeRaceHoldCore_Pump(&core, START + (period * PERIOD) + (PERIOD / 2u)) & MAIN_ARCADE_RACE_HOLD_DRAW_BANNER) == 0u);
	}
	for (uint32_t i = 0u; i < (uint32_t)(sizeof(strides) / sizeof(strides[0])); i++)
	{
		MainArcadeRaceHoldCore_Begin(&core, START);
		for (uint32_t period = strides[i]; period <= 400u; period += strides[i])
		{
			const uint32_t flags = MainArcadeRaceHoldCore_Pump(&core, START + (period * PERIOD) + 1u);

			CHECK(core.periods == period);
			CHECK(((flags & MAIN_ARCADE_RACE_HOLD_DRAW_BANNER) != 0u) == (NativeArcadeRaceDrive_BannerDue(core.periods) != 0));
		}
	}
	MainArcadeRaceHoldCore_Begin(&core, START);
	CHECK(MainArcadeRaceHoldCore_Pump(&core, START + (UINT64_C(0xFFFFFFFF) * PERIOD)) ==
	      (MAIN_ARCADE_RACE_HOLD_NEW_PERIOD | MAIN_ARCADE_RACE_HOLD_DRAW_BANNER));
	CHECK(NativeArcadeRaceDrive_BannerDue(core.periods) != 0);
	return 0;
}

int main(void)
{
	CHECK(TestConstants() == 0);
	CHECK(TestBeginAndNull() == 0);
	CHECK(TestSteadyHold() == 0);
	CHECK(TestLatePump() == 0);
	CHECK(TestGraceBoundary() == 0);
	CHECK(TestBannerDueMatchesDrive() == 0);
	puts("main_arcade_race_hold_core_test: ok");
	return 0;
}
