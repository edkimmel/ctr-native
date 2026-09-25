/*
 * Unit test of the root-counter wrap (docs/LOCKSTEP_RACE_MILESTONE.md section
 * 2.2 and LR-8, slice LR-S3 (b)). It compiles and runs the REAL retail
 * game/Timer.c (Timer_GetTime_Total and Timer_GetTime_Elapsed), included
 * below through the real game headers, the way the unity build includes it,
 * with a stubbed sdata and root counter (GetRCnt and the other RCnt calls
 * Timer.c names). It does not re-derive Timer.c's arithmetic.
 *
 * The wrap relies on signed 32-bit overflow in (units * 1000), which C17
 * leaves undefined. Only the reference toolchain's behaviour (MSVC x86, the
 * build that ships and that this test runs under in the fast suite) is
 * evidence; another compiler or optimisation level may do anything there.
 *
 * Per race tick the test does what the game does: 2 emitted VBlanks, each
 * adding 263 root-counter units to sdata->rcntTotalUnits and resetting the
 * counter (platform/native_libapi.c:15-23, game/MAIN/MainDrawCb.c:32-33, so
 * GetRCnt reads 0 between VBlanks), then GameLogic's elapsed-time lines,
 * copied from game/MAIN/MainFrame.c:188-203 (the scale by 32/100, the
 * clamps, and the override to 32 after a paused frame; MainFrame.c cannot be
 * compiled standalone). Race tick 0 is the first GameLogic after the load,
 * whose previous frame reads as paused: the load stage sets
 * gameMode1_prevFrame to 1 (game/LOAD/LOAD_TenStages.c:499).
 *
 * It shows:
 * - the crossing: without the pin, the only race ticks whose elapsedTimeMS is
 *   not 32 are ticks where units * 1000 crosses a multiple of 2^32 (zero, as
 *   a wrapped signed int, from below), and there 2 VBlanks give a delta of
 *   99 ms and elapsedTimeMS 31; that tick moves with the boot-relative phase;
 * - the pin (LR-8): with rcntTotalUnits pinned to 0 and clockFrameStart to
 *   -200 at race init, race tick 0 (no VBlank since the pin) computes a
 *   200 ms delta, 64 after the scale and clamp, which the after-load
 *   override turns into 32 (as it does without the pin), and the crossing
 *   falls on the same race tick for every boot-relative phase.
 */
#include <common.h>

#include <stdint.h>
#include <stdio.h>

/* Timer.c is included below, after the stubs, like the unity build does. */
struct sData sdata_static;

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

enum
{
	RCNT_UNITS_PER_VBLANK = 263,    /* CTR_NATIVE_RCNT1_TICKS_PER_VBLANK */
	VBLANKS_PER_TICK = 2,           /* RenderVSYNC after RenderSubmit (plan 2.1) */
	PIN_RCNT_TOTAL_UNITS = 0,       /* MAIN_ARCADE_RACE_SETUP_CORE_PIN_RCNT_TOTAL_UNITS */
	PIN_CLOCK_FRAME_START = -200,   /* MAIN_ARCADE_RACE_SETUP_CORE_PIN_CLOCK_FRAME_START */
	RACE_TICKS = 20000,             /* about 2.4 crossings at 2 VBlanks per tick */
	/* The VBlanks between the pin and race tick 0's GameLogic, as observed on
	 * both cabinets of arcade_link_launch (LR-S3 (b)): race ticks 0, 1, and 2
	 * logged rcntTotalUnits 0, 526, and 1052 and clockFrameStart 0, 100, and
	 * 200 (after GameLogic, before that pass's RenderVSYNC). */
	OBSERVED_PIN_TO_TICK0_VBLANKS = 0
};

/* The root counter since its last reset (the stub's only state). */
static int s_rootCounter;

int GetRCnt(int spec)
{
	(void)spec;
	return s_rootCounter;
}

int SetRCnt(int spec, unsigned short target, int mode)
{
	(void)spec;
	(void)target;
	(void)mode;
	return 1;
}

int StartRCnt(int spec)
{
	(void)spec;
	return 1;
}

int StopRCnt(int spec)
{
	(void)spec;
	return 0;
}

#include "Timer.c"

/* One emitted VBlank: the counter advances, then the VBlank callback adds it
 * to rcntTotalUnits and resets it (MainDrawCb.c:32-33). */
static void EmitVBlank(void)
{
	s_rootCounter += RCNT_UNITS_PER_VBLANK;
	sdata->rcntTotalUnits += GetRCnt(0);
	s_rootCounter = 0;
}

/* MainFrame.c:188-203, the elapsed-time lines of MainFrame_GameLogic:
 * Timer_GetTime_Elapsed on clockFrameStart, times 32 over 100 (the retail
 * (x << 5) / 100, written as a multiply so the copy has no shift of a
 * negative value), the two clamps, then 32 when the previous frame was
 * paused (prevFramePaused: gameMode1_prevFrame & PAUSE_ALL). *scaled gets
 * the clamped value before that override. */
static int GameLogicElapsed(int *clockFrameStart, int prevFramePaused, int *scaled)
{
	int elapsed = Timer_GetTime_Elapsed(*clockFrameStart, clockFrameStart);
	int elapsedTimeMS;

	elapsed = (elapsed * 32) / 100;
	elapsedTimeMS = elapsed;
	if (elapsed < 0)
	{
		elapsedTimeMS = 0x20;
	}
	if (0x40 < elapsedTimeMS)
	{
		elapsedTimeMS = 0x40;
	}
	*scaled = elapsedTimeMS;
	if (prevFramePaused)
	{
		elapsedTimeMS = 0x20;
	}
	return elapsedTimeMS;
}

/* 1 when units * 1000 passed a multiple of 2^32 between two readings less
 * than 2^31 / 1000 units apart, computed in well-defined unsigned arithmetic
 * independent of Timer.c: the product modulo 2^32 went down, so the 32-bit
 * product crossed zero from below. */
static int CrossesZero(uint32_t unitsBefore, uint32_t unitsAfter)
{
	return ((uint32_t)(unitsAfter * 1000u) < (uint32_t)(unitsBefore * 1000u)) ? 1 : 0;
}

struct RaceResult
{
	int tick0;              /* elapsedTimeMS of race tick 0 */
	int tick0Scaled;        /* race tick 0's scaled, clamped value before the after-load override */
	uint32_t firstOdd;      /* the first race tick >= 1 whose elapsedTimeMS is not 32 (RACE_TICKS: none) */
	int firstOddValue;      /* its elapsedTimeMS */
	uint32_t secondOdd;     /* the second one (RACE_TICKS: none) */
	uint32_t oddCount;      /* race ticks >= 1 whose elapsedTimeMS is not 32 */
	uint32_t zeroCrossings; /* race ticks >= 1 where units * 1000 crossed zero from below */
	int oddOnlyAtCrossing;  /* every odd tick is a crossing tick with elapsedTimeMS 31 */
};

/*
 * One race from a boot-relative phase: rcntTotalUnits and clockFrameStart as
 * the boot history left them (bootUnits; the previous GameLogic's snapshot
 * taken bootGap VBlanks before race init), then, with pin set, the LR-8 pin at
 * race init, then loadVBlanks VBlanks before race tick 0 (the first GameLogic
 * after the load, prevFrame paused) and 2 per race tick.
 */
static int RunRace(uint32_t bootUnits, int bootGap, int pin, int loadVBlanks, struct RaceResult *result)
{
	int clockFrameStart;
	int scaled;

	s_rootCounter = 0;
	sdata->rcntTotalUnits = (int)bootUnits;
	clockFrameStart = Timer_GetTime_Total();
	for (int vblank = 0; vblank < bootGap; vblank++)
	{
		EmitVBlank();
	}
	if (pin)
	{
		sdata->rcntTotalUnits = PIN_RCNT_TOTAL_UNITS;
		clockFrameStart = PIN_CLOCK_FRAME_START;
	}
	for (int vblank = 0; vblank < loadVBlanks; vblank++)
	{
		EmitVBlank();
	}
	result->tick0 = GameLogicElapsed(&clockFrameStart, 1, &result->tick0Scaled);
	result->firstOdd = RACE_TICKS;
	result->firstOddValue = 0;
	result->secondOdd = RACE_TICKS;
	result->oddCount = 0;
	result->zeroCrossings = 0;
	result->oddOnlyAtCrossing = 1;
	for (uint32_t tick = 1; tick < RACE_TICKS; tick++)
	{
		const uint32_t before = (uint32_t)sdata->rcntTotalUnits;
		int elapsedTimeMS;
		int crossing;

		for (int vblank = 0; vblank < VBLANKS_PER_TICK; vblank++)
		{
			EmitVBlank();
		}
		elapsedTimeMS = GameLogicElapsed(&clockFrameStart, 0, &scaled);
		crossing = CrossesZero(before, (uint32_t)sdata->rcntTotalUnits);
		result->zeroCrossings += (uint32_t)crossing;
		if (elapsedTimeMS != 32)
		{
			if (result->oddCount == 0u)
			{
				result->firstOdd = tick;
				result->firstOddValue = elapsedTimeMS;
			}
			else if (result->oddCount == 1u)
			{
				result->secondOdd = tick;
			}
			result->oddCount++;
			if (!crossing || (elapsedTimeMS != 31))
			{
				result->oddOnlyAtCrossing = 0;
			}
		}
	}
	return 0;
}

/* The pinned race's first zero crossing in exact arithmetic, independent of
 * Timer.c: the first race tick t whose rcntTotalUnits, loadVBlanks + 2 * t
 * VBlanks after the pin, has units * 1000 >= 2^32. */
static uint32_t ExpectedPinnedCrossing(int loadVBlanks)
{
	uint32_t tick = 0;

	while ((((uint64_t)loadVBlanks + (uint64_t)tick * VBLANKS_PER_TICK) * RCNT_UNITS_PER_VBLANK * 1000u) < (UINT64_C(1) << 32))
	{
		tick++;
	}
	return tick;
}

int main(void)
{
	/* Boot-relative phases before race init: rcntTotalUnits, and the VBlanks
	 * since the last GameLogic's snapshot (a load's worth, or a few). */
	static const uint32_t bootUnits[] = {0u, 263u * 2377u, 263u * 5402u + 17u, 1234567u, 2147483000u, 4294000000u, 3000000001u};
	static const int bootGaps[] = {60, 2, 1500};
	struct RaceResult pinned;
	struct RaceResult reference;
	struct RaceResult unpinned;
	uint32_t expectedPinnedCrossing = 0;
	uint32_t unpinnedFirst[2] = {0u, 0u};

	/* The pinned race's zero crossing in exact arithmetic, with the observed
	 * VBlanks between the pin and race tick 0 (none): race tick 8,166. */
	expectedPinnedCrossing = ExpectedPinnedCrossing(OBSERVED_PIN_TO_TICK0_VBLANKS);
	CHECK(expectedPinnedCrossing == 8166u);

	/* 1. The crossing, without the pin: two boot phases, two race ticks. */
	CHECK(RunRace(263u * 2377u, 60, 0, 0, &unpinned) == 0);
	printf("unpinned, boot units %u: race tick 0 elapsedTimeMS %d (scaled %d); "
	       "non-32 race ticks %u (elapsedTimeMS %d) and %u; %u non-32 ticks, %u zero crossings\n",
		263u * 2377u, unpinned.tick0, unpinned.tick0Scaled, (unsigned)unpinned.firstOdd, unpinned.firstOddValue,
		(unsigned)unpinned.secondOdd, (unsigned)unpinned.oddCount, (unsigned)unpinned.zeroCrossings);
	CHECK(unpinned.tick0 == 32);
	CHECK(unpinned.oddOnlyAtCrossing == 1);
	CHECK(unpinned.oddCount >= 1u && unpinned.firstOddValue == 31);
	unpinnedFirst[0] = unpinned.firstOdd;
	CHECK(RunRace(263u * 5402u + 17u, 60, 0, 0, &unpinned) == 0);
	printf("unpinned, boot units %u: race tick 0 elapsedTimeMS %d (scaled %d); "
	       "non-32 race ticks %u (elapsedTimeMS %d) and %u; %u non-32 ticks, %u zero crossings\n",
		263u * 5402u + 17u, unpinned.tick0, unpinned.tick0Scaled, (unsigned)unpinned.firstOdd, unpinned.firstOddValue,
		(unsigned)unpinned.secondOdd, (unsigned)unpinned.oddCount, (unsigned)unpinned.zeroCrossings);
	CHECK(unpinned.tick0 == 32);
	CHECK(unpinned.oddOnlyAtCrossing == 1);
	CHECK(unpinned.oddCount >= 1u && unpinned.firstOddValue == 31);
	unpinnedFirst[1] = unpinned.firstOdd;
	CHECK(unpinnedFirst[0] != unpinnedFirst[1]);
	/* Every unpinned phase: a non-32 tick only ever at a crossing, as 31. */
	for (uint32_t i = 0; i < (uint32_t)(sizeof(bootUnits) / sizeof(bootUnits[0])); i++)
	{
		CHECK(RunRace(bootUnits[i], 60, 0, 0, &unpinned) == 0);
		CHECK(unpinned.tick0 == 32);
		CHECK(unpinned.oddOnlyAtCrossing == 1);
		CHECK(unpinned.zeroCrossings >= 2u);
	}

	/* 2. The pin: race tick 0 computes 200 ms (64 scaled; 32 after the
	 * after-load override) and the crossing lands on the same race tick,
	 * whatever the boot-relative phase before the pin. */
	CHECK(RunRace(bootUnits[0], bootGaps[0], 1, OBSERVED_PIN_TO_TICK0_VBLANKS, &reference) == 0);
	printf("pinned (%d VBlanks from the pin to race tick 0): race tick 0 elapsedTimeMS %d (scaled %d); non-32 race ticks %u (elapsedTimeMS %d) and %u; %u non-32 ticks, %u zero crossings in %u ticks\n",
		(int)OBSERVED_PIN_TO_TICK0_VBLANKS, reference.tick0, reference.tick0Scaled, (unsigned)reference.firstOdd,
		reference.firstOddValue, (unsigned)reference.secondOdd, (unsigned)reference.oddCount, (unsigned)reference.zeroCrossings,
		(unsigned)RACE_TICKS);
	CHECK(reference.tick0 == 32 && reference.tick0Scaled == 64);
	CHECK(reference.firstOdd == expectedPinnedCrossing && reference.firstOddValue == 31);
	CHECK(reference.oddOnlyAtCrossing == 1);
	for (uint32_t i = 0; i < (uint32_t)(sizeof(bootUnits) / sizeof(bootUnits[0])); i++)
	{
		for (uint32_t gap = 0; gap < (uint32_t)(sizeof(bootGaps) / sizeof(bootGaps[0])); gap++)
		{
			CHECK(RunRace(bootUnits[i], bootGaps[gap], 1, OBSERVED_PIN_TO_TICK0_VBLANKS, &pinned) == 0);
			CHECK(pinned.tick0 == 32 && pinned.tick0Scaled == 64);
			CHECK(pinned.firstOdd == reference.firstOdd && pinned.firstOddValue == reference.firstOddValue);
			CHECK(pinned.secondOdd == reference.secondOdd);
			CHECK(pinned.oddCount == reference.oddCount && pinned.zeroCrossings == reference.zeroCrossings);
		}
	}
	/* The assumption behind race tick 8,166: no VBlank between the pin and
	 * race tick 0 (observed). N VBlanks there lengthen the first delta (still
	 * 64 scaled, 32 stored) and shift the crossing: it lands on the race tick
	 * ExpectedPinnedCrossing(N), still the same for every boot phase, but not
	 * on 8,166 once N >= 1 (N = 1 and 2 give 8,165, N = 3 and 4 give 8,164).
	 * Two cabinets agree on the crossing tick only if they agree on N. */
	CHECK(ExpectedPinnedCrossing(1) == 8165u && ExpectedPinnedCrossing(2) == 8165u);
	CHECK(ExpectedPinnedCrossing(3) == 8164u && ExpectedPinnedCrossing(4) == 8164u);
	for (int loadVBlanks = 1; loadVBlanks <= 4; loadVBlanks++)
	{
		struct RaceResult shifted;

		CHECK(RunRace(bootUnits[0], bootGaps[0], 1, loadVBlanks, &shifted) == 0);
		printf("pinned (%d VBlanks from the pin to race tick 0): race tick 0 elapsedTimeMS %d (scaled %d); first non-32 race tick %u (elapsedTimeMS %d)\n",
			loadVBlanks, shifted.tick0, shifted.tick0Scaled, (unsigned)shifted.firstOdd, shifted.firstOddValue);
		CHECK(shifted.tick0 == 32 && shifted.tick0Scaled == 64);
		CHECK(shifted.firstOdd == ExpectedPinnedCrossing(loadVBlanks) && shifted.firstOddValue == 31);
		CHECK(shifted.firstOdd != reference.firstOdd);
		CHECK(shifted.oddOnlyAtCrossing == 1);
		for (uint32_t i = 1; i < (uint32_t)(sizeof(bootUnits) / sizeof(bootUnits[0])); i++)
		{
			CHECK(RunRace(bootUnits[i], bootGaps[2], 1, loadVBlanks, &pinned) == 0);
			CHECK(pinned.firstOdd == shifted.firstOdd && pinned.secondOdd == shifted.secondOdd);
		}
	}

	/* The pinned values themselves: no VBlank since the pin is a 200 ms
	 * delta, (200 * 32) / 100 = 64 (MainFrame.c:188-199). */
	s_rootCounter = 0;
	sdata->rcntTotalUnits = PIN_RCNT_TOTAL_UNITS;
	CHECK(Timer_GetTime_Total() == 0);
	CHECK(Timer_GetTime_Elapsed(PIN_CLOCK_FRAME_START, NULL) == 200);

	puts("game_timer_wrap_test: ok");
	return 0;
}
