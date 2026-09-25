#include "MAIN/MainArcadeRaceHoldCore.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Stall hold period core (Task 8 race plan LR-9, slice LR-S2
 * (a)). See MAIN/MainArcadeRaceHoldCore.h for the rules.
 */

void MainArcadeRaceHoldCore_Begin(struct MainArcadeRaceHoldCore *core, uint64_t nowUs)
{
	if (core == NULL)
	{
		return;
	}
	memset(core, 0, sizeof(*core));
	core->startUs = nowUs;
	core->lastUs = nowUs;
	core->minPeriodPumps = MAIN_ARCADE_RACE_HOLD_NO_PERIOD;
}

uint32_t MainArcadeRaceHoldCore_PeriodsAt(const struct MainArcadeRaceHoldCore *core, uint64_t nowUs)
{
	uint64_t periods;

	if ((core == NULL) || (nowUs <= core->startUs))
	{
		return 0u;
	}
	periods = (nowUs - core->startUs) / (uint64_t)MAIN_ARCADE_RACE_HOLD_PERIOD_US;
	return (periods >= (uint64_t)UINT32_MAX) ? (UINT32_MAX - 1u) : (uint32_t)periods;
}

uint64_t MainArcadeRaceHoldCore_PeriodsToUs(uint32_t periods)
{
	return (uint64_t)periods * (uint64_t)MAIN_ARCADE_RACE_HOLD_PERIOD_US;
}

uint32_t MainArcadeRaceHoldCore_Pump(struct MainArcadeRaceHoldCore *core, uint64_t nowUs)
{
	uint32_t periods;
	uint32_t flags = 0u;

	if (core == NULL)
	{
		return 0u;
	}
	if (nowUs < core->lastUs)
	{
		nowUs = core->lastUs;
	}
	core->lastUs = nowUs;
	periods = MainArcadeRaceHoldCore_PeriodsAt(core, nowUs);
	if (core->pumps < UINT32_MAX)
	{
		core->pumps++;
	}
	if (periods == core->periods)
	{
		if (core->periodPumps < UINT32_MAX)
		{
			core->periodPumps++;
		}
		return 0u;
	}
	/* The running period ended with periodPumps pumps; any period between it
	 * and this pump's ended with none. */
	if (core->periodPumps < core->minPeriodPumps)
	{
		core->minPeriodPumps = core->periodPumps;
	}
	if ((periods - core->periods) > 1u)
	{
		core->minPeriodPumps = 0u;
	}
	core->periods = periods;
	core->periodPumps = 1u;
	flags |= MAIN_ARCADE_RACE_HOLD_NEW_PERIOD;
	if (periods >= MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS)
	{
		flags |= MAIN_ARCADE_RACE_HOLD_DRAW_BANNER;
		if (core->banners < UINT32_MAX)
		{
			core->banners++;
		}
	}
	return flags;
}
