#ifndef GAME_MAIN_ARCADE_RACE_HOLD_CORE_H
#define GAME_MAIN_ARCADE_RACE_HOLD_CORE_H

#include <stdint.h>

/*
 * Stall hold period core (Task 8 race plan LR-9, slice LR-S2
 * (a)): the pure wall-time bookkeeping of the MainArcadeRaceHold loop. It
 * counts the full tick periods held, the host event pumps of each period,
 * and decides when the loop redraws the displayed frame with the banner. No
 * clock of its own: the loop hands it the host time of every pump.
 *
 * A tick period is 2 VBlank periods of the native pacer
 * (platform/native_platform.c: 897619 GPU cycles per VBlank at 53693175 Hz),
 * 33435.1 us, rounded to MAIN_ARCADE_RACE_HOLD_PERIOD_US. Period p of a hold
 * is the wall time [p, p + 1) * PERIOD_US after Begin; "periods" is the
 * number of full periods held so far.
 *
 * Presentation (LR-9): for the first MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS
 * (holdGraceTicks) periods the last frame simply stays on screen; after
 * that, the loop redraws once per period: on the first pump of every period
 * p >= GRACE (once, however many periods one late pump skipped).
 *
 * Pump evidence: a pump belongs to the period its time falls in. When a
 * period ends, its pump count enters minPeriodPumps; a period that a late
 * pump skipped entirely had no pump, so minPeriodPumps becomes 0. A running
 * (unfinished) period never enters it.
 */

#define MAIN_ARCADE_RACE_HOLD_PERIOD_US 33435u
#define MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS 10u
#define MAIN_ARCADE_RACE_HOLD_NO_PERIOD UINT32_MAX /* minPeriodPumps before the first period ended */

/* The flags MainArcadeRaceHoldCore_Pump returns. */
#define MAIN_ARCADE_RACE_HOLD_NEW_PERIOD 1u  /* this pump is the first of a new period */
#define MAIN_ARCADE_RACE_HOLD_DRAW_BANNER 2u /* redraw the displayed frame with the banner now */

struct MainArcadeRaceHoldCore
{
	uint64_t startUs;        /* host time at Begin */
	uint64_t lastUs;         /* host time of the last pump (startUs before the first) */
	uint32_t periods;        /* full periods held at the last pump */
	uint32_t pumps;          /* pumps so far */
	uint32_t periodPumps;    /* pumps in the running period */
	uint32_t minPeriodPumps; /* fewest pumps of any ended period; NO_PERIOD before the first */
	uint32_t banners;        /* DRAW_BANNER decisions so far */
	uint32_t reserved;
};

/* Starts a hold at host time nowUs. NULL is a no-op. */
void MainArcadeRaceHoldCore_Begin(struct MainArcadeRaceHoldCore *core, uint64_t nowUs);

/*
 * Records one host event pump at host time nowUs and returns its flags
 * (NEW_PERIOD, DRAW_BANNER). A time before startUs counts as startUs, and a
 * time before the last pump as the last pump's (the periods never go back).
 * NULL returns 0.
 */
uint32_t MainArcadeRaceHoldCore_Pump(struct MainArcadeRaceHoldCore *core, uint64_t nowUs);

/* The full periods held at host time nowUs (0 for a time before startUs); 0 for NULL. */
uint32_t MainArcadeRaceHoldCore_PeriodsAt(const struct MainArcadeRaceHoldCore *core, uint64_t nowUs);

/* The wall time of periods full periods, in microseconds. */
uint64_t MainArcadeRaceHoldCore_PeriodsToUs(uint32_t periods);

#endif
