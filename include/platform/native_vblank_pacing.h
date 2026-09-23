#ifndef PLATFORM_NATIVE_VBLANK_PACING_H
#define PLATFORM_NATIVE_VBLANK_PACING_H

#include <stdint.h>

/*
 * The pure pacing decision of the native VBlank emitter
 * (platform/native_platform.c, Native_CatchUpDueVBlanks). Every VSync and
 * Platform_WaitUntilVBlank first decides what to do with the VBlanks that
 * fell due while the host was busy, then waits for its own VBlank slot(s).
 *
 * Default pacing (retail-faithful, the only pacing a normal run uses): the
 * late VBlanks are emitted before the wait (CATCH_UP), so a host frame that
 * overran its slot advances the VBlank count, the root counter, and so
 * gGT->elapsedTimeMS by more than the retail 2 VBlanks; a pathological stall
 * (more than catchUpMax due) is dropped and the schedule restarts one period
 * after now (REBASE).
 *
 * Fixed pacing (a host-local switch, Platform_SetFixedVBlankPacing, which only
 * the internal live roster proof turns on): no late VBlank is ever emitted.
 * On time, nothing is emitted before the wait (ON_TIME); late, the due
 * VBlanks are dropped and the schedule is re-anchored at now (REANCHOR), so
 * the wait that follows emits its VBlank at once and the next slot is one
 * period later. Each wait therefore emits exactly the VBlanks it asked for,
 * and every game tick advances exactly the retail 2 VBlanks however slow the
 * host frame was.
 *
 * Pure: no clock, no I/O, no global state.
 */

enum NativeVBlankPacingPlan
{
	NATIVE_VBLANK_PACING_CATCH_UP = 0, /* default: emit the VBlanks already due, then wait */
	NATIVE_VBLANK_PACING_REBASE = 1,   /* default: stall past the cap; drop them, next slot = now + one period */
	NATIVE_VBLANK_PACING_ON_TIME = 2,  /* fixed: nothing due; emit nothing before the wait */
	NATIVE_VBLANK_PACING_REANCHOR = 3  /* fixed: late; drop them, next slot = now (the wait emits at once) */
};

/*
 * The plan for one wait. fixedPacing selects the pacing (nonzero: fixed);
 * now and nextVBlank are performance-counter values (nextVBlank is the slot
 * of the next VBlank to emit); step is the counter ticks per VBlank; and
 * catchUpMax is the most late VBlanks the default pacing emits. A zero step
 * counts as an unbounded number of due VBlanks.
 */
uint32_t NativeVBlankPacing_Plan(int fixedPacing, uint64_t now, uint64_t nextVBlank, uint64_t step, uint32_t catchUpMax);

#endif
