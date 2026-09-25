#ifndef GAME_MAIN_ARCADE_RACE_HOLD_H
#define GAME_MAIN_ARCADE_RACE_HOLD_H

#include <stdint.h>

/*
 * Stall hold loop (Task 8 race plan LR-9, slice LR-S2 (a); the plan is
 * linked from docs/GAME_LOOP_UI_MILESTONE.md).
 * Native only: game/MAIN/MainArcadeRaceHold.c is compiled only with
 * CTR_NATIVE. A caller that must not advance the simulation (a peer input take
 * that stalls; for LR-S2 the internal roster proof's hold) calls
 * MainArcadeRaceHold_Run at its hook, before the VBlanks that would read the
 * pads for the next tick. The loop never calls VSync and never emits a
 * VBlank, so no VSync callback, sound update, pad poll, or audio step runs
 * while held; it reads no pad and writes no game state. Each iteration does
 * exactly this host work:
 *
 *   1. pumps host events (Platform_PollHostEvents), so the window stays
 *      responsive and a quit still exits;
 *   2. asks the caller's step whether the hold goes on (LR-S9 plugs RaceHold
 *      in here; the roster proof counts 45 periods);
 *   3. from the 11th tick period on (holdGraceTicks = 10), on the first
 *      iteration of each period, redraws the displayed frame with the
 *      WAITING FOR OPPONENT banner (Platform_PresentVRAMDisplayBanner, a
 *      host overlay on the presented image only);
 *   4. sleeps about 1 ms through the host-local wait (Platform_HostWaitMs),
 *      which emits no VBlank.
 *
 * The period bookkeeping is the pure MAIN/MainArcadeRaceHoldCore.h's.
 */

#define MAIN_ARCADE_RACE_HOLD_BANNER_TEXT "WAITING FOR OPPONENT"
#define MAIN_ARCADE_RACE_HOLD_WAIT_MS 1u

/*
 * The caller's step, once per iteration after the pump: periods is the
 * number of full tick periods held so far, newPeriod is nonzero on the first
 * iteration of each period after the first. Returns nonzero while the hold
 * must go on, 0 to end it.
 */
typedef int (*MainArcadeRaceHoldStepFn)(void *context, uint32_t periods, int newPeriod);

/* What one hold did, for the caller's evidence. */
struct MainArcadeRaceHoldResult
{
	uint64_t wallUs;           /* host time from the hold's start to its end */
	uint32_t periods;          /* full tick periods held at the end */
	uint32_t pumps;            /* host event pumps (loop iterations) */
	uint32_t minPeriodPumps;   /* fewest pumps of any ended period (MAIN_ARCADE_RACE_HOLD_NO_PERIOD: none ended) */
	uint32_t bannersDue;       /* banner redraws the grace and period rules asked for while held */
	uint32_t bannersPresented; /* of those, the ones the platform presented */
	uint32_t reserved;
};

/*
 * Runs the hold until step returns 0 (a NULL step ends it at once, after
 * one pump). The result, when not NULL, is filled on return.
 */
void MainArcadeRaceHold_Run(MainArcadeRaceHoldStepFn step, void *context, struct MainArcadeRaceHoldResult *result);

#endif
