/*
 * Stall hold loop (Task 8 race plan LR-9, slice LR-S2 (a); the plan is
 * linked from docs/GAME_LOOP_UI_MILESTONE.md).
 * See MAIN/MainArcadeRaceHold.h. Native only.
 *
 * The loop's only calls are the host event pump, the caller's step, the
 * host-local clock and wait, and the banner present. It names no VSync, no
 * VBlank wait, no pad or input call, and no game global: it must not
 * advance, read into, or write the simulation, and the banner is drawn by
 * the platform over the presented image only, never through the game's
 * ordering table or primitive memory (tests/main_arcade_race_hold_isolation_test.cmake).
 * The mode only decides whether step 3 (the banner) runs (LR-S10 part 2),
 * and the glyph table only which present draws it (LR-S11: the race
 * caller's table, the game font; NULL, the roster proof's, the block font).
 * The table is handed on, never read here.
 *
 * Unity-included (game/game_unity.h); the period core is a linked library.
 */
#if defined(CTR_NATIVE)

#include <platform.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "MAIN/MainArcadeRaceHold.h"
#include "MAIN/MainArcadeRaceHoldCore.h"

void MainArcadeRaceHold_RunMode(MainArcadeRaceHoldStepFn step, void *context, uint32_t mode, const struct NativeHoldBannerGlyphs *glyphs,
                                struct MainArcadeRaceHoldResult *result)
{
	struct MainArcadeRaceHoldCore core;
	uint32_t bannersDue = 0u;
	uint32_t bannersPresented = 0u;
	uint64_t endUs;

	MainArcadeRaceHoldCore_Begin(&core, Platform_HostClockUs());
	for (;;)
	{
		uint32_t flags;

		/* 1. Keep the window responsive; a quit exits from here. */
		Platform_PollHostEvents();
		flags = MainArcadeRaceHoldCore_Pump(&core, Platform_HostClockUs());
		/* 2. The caller decides whether the hold goes on. */
		if ((step == NULL) || (step(context, core.periods, (flags & MAIN_ARCADE_RACE_HOLD_NEW_PERIOD) != 0u) == 0))
		{
			break;
		}
		/* 3. After the grace, once per period, unless the mode has no banner:
		 * the banner over the displayed frame. */
		if ((mode != MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER) && ((flags & MAIN_ARCADE_RACE_HOLD_DRAW_BANNER) != 0u))
		{
			int presented;

			bannersDue++;
			if (glyphs != NULL)
			{
				presented = Platform_PresentVRAMDisplayBannerGlyphs(MAIN_ARCADE_RACE_HOLD_BANNER_TEXT, glyphs);
			}
			else
			{
				presented = Platform_PresentVRAMDisplayBanner(MAIN_ARCADE_RACE_HOLD_BANNER_TEXT);
			}
			if (presented != 0)
			{
				bannersPresented++;
			}
		}
		/* 4. About 1 ms, emitting no VBlank. */
		Platform_HostWaitMs(MAIN_ARCADE_RACE_HOLD_WAIT_MS);
	}
	endUs = Platform_HostClockUs();

	if (result != NULL)
	{
		memset(result, 0, sizeof(*result));
		result->wallUs = (endUs > core.startUs) ? (endUs - core.startUs) : 0u;
		result->periods = core.periods;
		result->pumps = core.pumps;
		result->minPeriodPumps = core.minPeriodPumps;
		result->bannersDue = bannersDue;
		result->bannersPresented = bannersPresented;
	}
}

void MainArcadeRaceHold_Run(MainArcadeRaceHoldStepFn step, void *context, struct MainArcadeRaceHoldResult *result)
{
	MainArcadeRaceHold_RunMode(step, context, MAIN_ARCADE_RACE_HOLD_MODE_BANNER, NULL, result);
}

#endif
