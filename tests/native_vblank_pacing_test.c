#include "platform/native_vblank_pacing.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/* The pacer's catch-up cap (NATIVE_VSYNC_CATCHUP_MAX in platform/native_platform.c). */
#define TEST_CATCH_UP_MAX 8u
/* Counter ticks per VBlank in the model below (any positive value works). */
#define TEST_STEP UINT64_C(1000)

static int TestDefaultPlan(void)
{
	const uint64_t next = UINT64_C(50000);

	/* On time: the catch-up loop runs and finds nothing due. */
	CHECK(NativeVBlankPacing_Plan(0, next - 1u, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_CATCH_UP);
	CHECK(NativeVBlankPacing_Plan(0, 0u, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_CATCH_UP);
	/* Late by up to the cap: the late VBlanks are replayed. */
	CHECK(NativeVBlankPacing_Plan(0, next, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_CATCH_UP);
	CHECK(NativeVBlankPacing_Plan(0, next + (7u * TEST_STEP), next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_CATCH_UP);
	CHECK(NativeVBlankPacing_Plan(0, next + (8u * TEST_STEP) - 1u, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_CATCH_UP);
	/* Past the cap: a pathological stall is rebased. */
	CHECK(NativeVBlankPacing_Plan(0, next + (8u * TEST_STEP), next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_REBASE);
	CHECK(NativeVBlankPacing_Plan(0, UINT64_MAX, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_REBASE);
	/* A zero step counts as unbounded when late, and is harmless on time. */
	CHECK(NativeVBlankPacing_Plan(0, next, next, 0u, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_REBASE);
	CHECK(NativeVBlankPacing_Plan(0, next - 1u, next, 0u, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_CATCH_UP);
	return 0;
}

static int TestFixedPlan(void)
{
	const uint64_t next = UINT64_C(50000);

	CHECK(NativeVBlankPacing_Plan(1, next - 1u, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_ON_TIME);
	CHECK(NativeVBlankPacing_Plan(1, 0u, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_ON_TIME);
	CHECK(NativeVBlankPacing_Plan(1, next, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_REANCHOR);
	CHECK(NativeVBlankPacing_Plan(-7, next, next, TEST_STEP, TEST_CATCH_UP_MAX) == NATIVE_VBLANK_PACING_REANCHOR);
	CHECK(NativeVBlankPacing_Plan(1, UINT64_MAX, next, 0u, 0u) == NATIVE_VBLANK_PACING_REANCHOR);
	/* Fixed pacing never catches up and never rebases, at any lateness. */
	for (uint64_t late = 0; late < (20u * TEST_STEP); late += 37u)
	{
		const uint32_t plan = NativeVBlankPacing_Plan(1, next + late, next, TEST_STEP, TEST_CATCH_UP_MAX);

		CHECK(plan == NATIVE_VBLANK_PACING_REANCHOR);
	}
	CHECK(NATIVE_VBLANK_PACING_CATCH_UP == 0 && NATIVE_VBLANK_PACING_REBASE == 1 && NATIVE_VBLANK_PACING_ON_TIME == 2 &&
	      NATIVE_VBLANK_PACING_REANCHOR == 3);
	return 0;
}

/*
 * A model of the native emitter (platform/native_platform.c:
 * Native_CatchUpDueVBlanks, Native_WaitAndEmitVBlank, VSync) on a fake clock,
 * driven by the retail frame loop (MainFrame_RenderFrame.c, RenderVSYNC: VSync(0)
 * until two VBlanks have passed since the flip). Emitting takes no time here.
 */
struct Pacer
{
	int fixed;
	uint64_t now;
	uint64_t next;
	uint32_t vblanks;
	int32_t vsyncTillFlip;
};

static void Pacer_Emit(struct Pacer *pacer)
{
	pacer->vblanks++;
	pacer->vsyncTillFlip--;
}

static void Pacer_CatchUp(struct Pacer *pacer)
{
	uint32_t emitted = 0;

	switch (NativeVBlankPacing_Plan(pacer->fixed, pacer->now, pacer->next, TEST_STEP, TEST_CATCH_UP_MAX))
	{
	case NATIVE_VBLANK_PACING_REBASE:
		pacer->next = pacer->now + TEST_STEP;
		return;
	case NATIVE_VBLANK_PACING_REANCHOR:
		pacer->next = pacer->now;
		return;
	case NATIVE_VBLANK_PACING_ON_TIME:
		return;
	default:
		break;
	}
	while (pacer->now >= pacer->next)
	{
		Pacer_Emit(pacer);
		emitted++;
		if (emitted >= TEST_CATCH_UP_MAX)
		{
			pacer->next = pacer->now + TEST_STEP;
			break;
		}
		pacer->next += TEST_STEP;
	}
}

static void Pacer_VSync0(struct Pacer *pacer)
{
	Pacer_CatchUp(pacer);
	if (pacer->now < pacer->next)
	{
		pacer->now = pacer->next; /* the wait */
	}
	Pacer_Emit(pacer);
	pacer->next += TEST_STEP;
}

/* One game tick taking `work` counter ticks of host time; returns its VBlanks. */
static uint32_t Pacer_Tick(struct Pacer *pacer, uint64_t work)
{
	const uint32_t before = pacer->vblanks;

	pacer->now += work;
	while (pacer->vsyncTillFlip >= 1)
	{
		Pacer_VSync0(pacer);
	}
	pacer->vsyncTillFlip = 2; /* the flip */
	return pacer->vblanks - before;
}

/* An injected host stall: frames of 0.6 VBlank of work, one of 3.5 VBlanks,
 * one pathological one of 40 VBlanks. */
static int TestInjectedStall(void)
{
	static const uint64_t work[] = {600u, 600u, 600u, 3500u, 600u, 600u, 40000u, 600u, 1900u, 2100u, 600u};
	struct Pacer pacers[2];
	uint32_t defaultOver = 0;

	for (int fixed = 0; fixed < 2; fixed++)
	{
		struct Pacer *pacer = &pacers[fixed];

		pacer->fixed = fixed;
		pacer->now = UINT64_C(1000000);
		pacer->next = pacer->now + TEST_STEP;
		pacer->vblanks = 0;
		pacer->vsyncTillFlip = 2;
		for (uint32_t frame = 0; frame < (uint32_t)(sizeof(work) / sizeof(work[0])); frame++)
		{
			const uint32_t vblanks = Pacer_Tick(pacer, work[frame]);

			if (fixed)
			{
				/* Fixed pacing: every tick is exactly the retail 2 VBlanks. */
				CHECK(vblanks == 2u);
			}
			else if (vblanks > 2u)
			{
				defaultOver++;
			}
		}
	}
	/* The default pacing replays the late VBlanks of the 3.5-VBlank stall
	 * and the 2.1-VBlank frame (3 and more VBlanks in one tick); the
	 * pathological stall is rebased, not replayed. */
	CHECK(defaultOver >= 2u);
	CHECK(pacers[1].vblanks == 2u * (uint32_t)(sizeof(work) / sizeof(work[0])));
	CHECK(pacers[0].vblanks > pacers[1].vblanks);
	return 0;
}

int main(void)
{
	CHECK(TestDefaultPlan() == 0);
	CHECK(TestFixedPlan() == 0);
	CHECK(TestInjectedStall() == 0);
	puts("native_vblank_pacing_test: ok");
	return 0;
}
