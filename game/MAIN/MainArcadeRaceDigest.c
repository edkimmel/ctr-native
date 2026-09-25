/*
 * Live V4 race digest (Task 8 race plan LR-10, slice LR-S4; the plan is
 * linked from docs/GAME_LOOP_UI_MILESTONE.md). See MAIN/MainArcadeRaceDigest.h.
 * Native only.
 *
 * Read-only: every game value comes in through const pointers, and the only
 * writes are this file's static state and the V4 runtime coordinator's
 * workspace. Its callers are the internal roster proof (LR-S4) and, from
 * LR-S10, the race caller (tests/main_arcade_race_digest_isolation_test.cmake).
 * The race caller projects through MainArcadeRaceDigest_ProjectState, the
 * only way the module copies the whole state out (LR-58).
 *
 * Unity-included after the MainCanonical* sources, whose runtime and world
 * extractors it calls, and MainArcadeRaceSetup, whose post-setup bank its
 * callers pass in; before the roster proof and the race caller.
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "MAIN/MainArcadeRaceDigest.h"
#include "MAIN/MainCanonicalRuntime.h"
#include "MAIN/MainCanonicalWorldCounters.h"
#include "MAIN/MainCanonicalWorldMineRegistry.h"
#include "platform/native_canonical_topology.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_match_config.h"
#include "platform/native_perf.h"

#include <string.h>

_Static_assert(sizeof(((const struct GameTracker *)0)->frameTimer_VsyncCallback) == sizeof(uint32_t),
	"the race-relative frameTimer is a 32-bit counter");
_Static_assert(sizeof(((const struct sData *)0)->frameCounter) == sizeof(uint32_t), "the race-relative frameCounter is a 32-bit counter");

/*
 * The whole digest state: file-scope static, outside every saved-state
 * region, never recorded or canonical. The runtime request, the bank copy,
 * and the domain values are too large for comfort on the game stack. The
 * runtime's own workspace is its file-scope global (MainCanonicalRuntime_Global).
 */
struct MainArcadeRaceDigestState
{
	struct MainCanonicalRuntimeV4Request request;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 retailRng;
	struct NativeCanonicalWorldCountersV1 worldCounters;
	struct NativeCanonicalWorldMineRegistryV1 mineRegistry;
	struct NativeCanonicalTopologyV1 topology;
	struct NativeCanonicalTopologyV1 unavailable;
	/* ProjectState's copy of the view, taken before Release zeroes it and
	 * handed to the caller only once the tick succeeded (LR-58). */
	struct NativeCanonicalStateV4 stateScratch;
	struct MainArcadeRaceDigestBase base;
	uint32_t nextTick;
	uint32_t runtimeFailure;
	enum MainArcadeRaceDigestFailure failure;
	uint8_t raceActive;
	uint8_t failed;
};

static struct MainArcadeRaceDigestState s_mainArcadeRaceDigest;

/* The same 32 bits as a signed value, without an implementation-defined
 * out-of-range conversion. */
static int32_t MainArcadeRaceDigest_Signed(uint32_t value)
{
	if (value <= (uint32_t)INT32_MAX)
	{
		return (int32_t)value;
	}
	return -(int32_t)(UINT32_MAX - value) - 1;
}

/* Latches failure (keeping the first one) and returns 0. */
static int MainArcadeRaceDigest_Fail(enum MainArcadeRaceDigestFailure failure)
{
	struct MainArcadeRaceDigestState *state = &s_mainArcadeRaceDigest;

	if (state->failed == 0u)
	{
		state->failure = failure;
		state->runtimeFailure = (uint32_t)MainCanonicalRuntime_FailureReason(MainCanonicalRuntime_Global());
		state->failed = 1u;
	}
	return 0;
}

int MainArcadeRaceDigest_CaptureBase(const struct GameTracker *gGT, const struct sData *sourceData,
	struct MainArcadeRaceDigestBase *base)
{
	if ((gGT == NULL) || (sourceData == NULL) || (base == NULL))
	{
		return 0;
	}
	base->frameTimer = (uint32_t)gGT->frameTimer_VsyncCallback;
	base->frameCounter = (uint32_t)sourceData->frameCounter;
	return 1;
}

int MainArcadeRaceDigest_ProjectControl(const struct GameTracker *gGT, const struct sData *sourceData,
	const struct MainArcadeRaceDigestBase *base, struct NativeCanonicalControlV1 *out)
{
	struct NativeCanonicalControlV1 control;

	if ((gGT == NULL) || (sourceData == NULL) || (base == NULL) || (out == NULL))
	{
		return 0;
	}
	/* The main loop's live V1 control values, with the two boot-relative
	 * counters race-relative (LR-10). timer is race-relative through its
	 * RS-17 pin; every other value is level- or race-relative. */
	control.frameTimer = MainArcadeRaceDigest_Signed((uint32_t)gGT->frameTimer_VsyncCallback - base->frameTimer);
	control.frameCounter = MainArcadeRaceDigest_Signed((uint32_t)sourceData->frameCounter - base->frameCounter);
	control.timer = (int32_t)gGT->timer;
	control.framesInThisLEV = (int32_t)gGT->framesInThisLEV;
	control.elapsedTimeMS = (int32_t)gGT->elapsedTimeMS;
	control.msInThisLEV = (int32_t)gGT->msInThisLEV;
	control.elapsedEventTime = (int32_t)gGT->elapsedEventTime;
	control.mainGameState = (int32_t)sourceData->mainGameState;
	control.loadingStage = (int32_t)sourceData->Loading.stage;
	control.levelID = (int32_t)gGT->levelID;
	control.gameMode1 = (int32_t)gGT->gameMode1;
	control.gameMode2 = (int32_t)gGT->gameMode2;
	*out = control;
	return 1;
}

int MainArcadeRaceDigest_ProjectRetailRng(const struct GameTracker *gGT, const struct sData *sourceData,
	struct NativeCanonicalRngV1 *out)
{
	struct NativeCanonicalRngV1 rng;

	if ((gGT == NULL) || (sourceData == NULL) || (out == NULL))
	{
		return 0;
	}
	/* The main loop's live V1 RNG values; audioRNG and the PSX rand are not
	 * authoritative (section 4.1 of the plan). */
	rng.mixRandomNumber = (uint32_t)sourceData->randomNumber;
	rng.deadcoed0 = (uint32_t)gGT->deadcoed_struct.state0;
	rng.deadcoed1 = (uint32_t)gGT->deadcoed_struct.state1;
	rng.advRng0 = (uint32_t)sourceData->advRng.state0;
	rng.advRng1 = (uint32_t)sourceData->advRng.state1;
	*out = rng;
	return 1;
}

/* Race tick 0: the runtime reset, the race-relative base, and the request's
 * config, identity, and config digest. */
static int MainArcadeRaceDigest_StartRace(const struct MainArcadeRaceDigestSources *sources)
{
	struct MainArcadeRaceDigestState *state = &s_mainArcadeRaceDigest;

	if (!MainArcadeRaceDigest_CaptureBase(sources->gGT, sources->sourceData, &state->base))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_ARGUMENT);
	}
	state->request.config = *sources->config;
	memcpy(state->request.identity.build, sources->config->buildIdentity, sizeof(state->request.identity.build));
	memcpy(state->request.identity.content, sources->config->contentIdentity, sizeof(state->request.identity.content));
	if (!NativeMatchConfigV1_Digest(&state->request.config, state->request.configDigest))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_CONFIG);
	}
	return 1;
}

/* One race tick's lifecycle (see the header). With wantState nonzero the
 * view is also copied whole for *stateOut, which is then required
 * (ProjectState, LR-58); Project passes 0 and NULL. */
static int MainArcadeRaceDigest_ProjectTick(uint32_t raceTick, const struct MainArcadeRaceDigestSources *sources,
	struct MainArcadeRaceDigestTick *out, int wantState, struct NativeCanonicalStateV4 *stateOut)
{
	struct MainArcadeRaceDigestState *state = &s_mainArcadeRaceDigest;
	struct MainCanonicalRuntimeWorkspace *workspace = MainCanonicalRuntime_Global();
	const struct NativeCanonicalStateV4 *view;
	struct MainArcadeRaceDigestTick tick;
	int topologyUnavailable;

	if (raceTick == 0u)
	{
		/* A new race: nothing of an earlier race, a poisoned workspace
		 * included, carries into it. Reset also initializes on first use. */
		MainCanonicalRuntime_Reset(workspace);
		memset(state, 0, sizeof(*state));
		state->raceActive = 1u;
	}
	else if (state->failed != 0u)
	{
		return 0;
	}
	else if ((state->raceActive == 0u) || (raceTick != state->nextTick))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE);
	}
	if ((sources == NULL) || (out == NULL) || ((wantState != 0) && (stateOut == NULL)) || (sources->gGT == NULL) ||
	    (sources->sourceData == NULL) || (sources->mineSource == NULL) || (sources->config == NULL) || (sources->bank == NULL) ||
	    (sources->input == NULL))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_ARGUMENT);
	}
	if (raceTick == 0u)
	{
		if (!MainArcadeRaceDigest_StartRace(sources))
		{
			return 0;
		}
	}
	else if (memcmp(sources->config, &state->request.config, sizeof(state->request.config)) != 0)
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_CONFIG);
	}

	/* The request: this tick's frame, and a copy of the caller's bank (LR-10,
	 * LR-19), which PrepareV4 projects instead of a fresh derivation. */
	state->bank = *sources->bank;
	state->request.bank = &state->bank;
	state->request.replayFrame = raceTick;
	state->request.restoredThisFrame = 0;

	(void)MainArcadeRaceDigest_ProjectControl(sources->gGT, sources->sourceData, &state->base, &state->control);
	(void)MainArcadeRaceDigest_ProjectRetailRng(sources->gGT, sources->sourceData, &state->retailRng);
	if (!MainCanonicalWorldCounters_ExtractV1(sources->gGT, &state->worldCounters) ||
	    !MainCanonicalWorldMineRegistry_ExtractV1(sources->gGT, sources->mineSource, &state->mineRegistry))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_WORLD);
	}
	/* TOPOLOGY is not compared in Task 8: the unavailable summary, every tick. */
	NativeCanonicalTopologyV1_Init(&state->topology);
	NativeCanonicalTopologyV1_Init(&state->unavailable);

	if (!MainCanonicalRuntime_BeginFrame(workspace))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_BEGIN_FRAME);
	}
	if (!MainCanonicalRuntime_PrepareV4(workspace, &state->request, sources->gGT, sources->sourceData, &state->control,
		    &state->retailRng, sources->input, &state->worldCounters, &state->mineRegistry, &state->topology))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_PREPARE);
	}
	view = MainCanonicalRuntime_ViewV4(workspace, &state->request);
	if ((view == NULL) || (view->frameNumber != raceTick))
	{
		/* End the frame before latching, so no failure leaves the workspace
		 * prepared or frame-active: Release the prepared state, or Reset when
		 * there is no view (Release refuses the same request View refused). */
		if ((view == NULL) || !MainCanonicalRuntime_ReleaseV4(workspace, &state->request))
		{
			MainCanonicalRuntime_Reset(workspace);
		}
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_VIEW);
	}
	/* Copied out before Release, which zeroes the view. */
	memset(&tick, 0, sizeof(tick));
	tick.frameNumber = view->frameNumber;
	tick.combinedDigest = view->combinedDigest;
	memcpy(tick.domainDigests, view->domainDigests, sizeof(tick.domainDigests));
	if (wantState != 0)
	{
		state->stateScratch = *view;
	}
	topologyUnavailable = (memcmp(&view->topology, &state->unavailable, sizeof(state->unavailable)) == 0);
	if (!MainCanonicalRuntime_ReleaseV4(workspace, &state->request))
	{
		/* A refused Release leaves the state prepared: Reset ends it. */
		MainCanonicalRuntime_Reset(workspace);
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_RELEASE);
	}
	if (!topologyUnavailable)
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_TOPOLOGY);
	}
	*out = tick;
	if (wantState != 0)
	{
		*stateOut = state->stateScratch;
	}
	state->nextTick = raceTick + 1u;
	return 1;
}

int MainArcadeRaceDigest_Project(uint32_t raceTick, const struct MainArcadeRaceDigestSources *sources,
	struct MainArcadeRaceDigestTick *out)
{
	int projected;

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST);
	projected = MainArcadeRaceDigest_ProjectTick(raceTick, sources, out, 0, NULL);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST);
	return projected;
}

int MainArcadeRaceDigest_ProjectState(uint32_t raceTick, const struct MainArcadeRaceDigestSources *sources,
	struct MainArcadeRaceDigestTick *out, struct NativeCanonicalStateV4 *stateOut)
{
	int projected;

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST);
	projected = MainArcadeRaceDigest_ProjectTick(raceTick, sources, out, 1, stateOut);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST);
	return projected;
}

int MainArcadeRaceDigest_EndRace(void)
{
	struct MainArcadeRaceDigestState *state = &s_mainArcadeRaceDigest;

	if ((state->raceActive == 0u) || (state->failed != 0u))
	{
		state->raceActive = 0u;
		return 0;
	}
	state->raceActive = 0u;
	if (!MainCanonicalRuntime_InvalidateTopology(MainCanonicalRuntime_Global()))
	{
		return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_END);
	}
	return 1;
}

enum MainArcadeRaceDigestFailure MainArcadeRaceDigest_Failure(void)
{
	return (s_mainArcadeRaceDigest.failed != 0u) ? s_mainArcadeRaceDigest.failure : MAIN_ARCADE_RACE_DIGEST_FAILURE_NONE;
}

const char *MainArcadeRaceDigest_FailureName(enum MainArcadeRaceDigestFailure failure)
{
	switch (failure)
	{
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_NONE:
		return "NONE";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_ARGUMENT:
		return "ARGUMENT";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE:
		return "SEQUENCE";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_CONFIG:
		return "CONFIG";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_WORLD:
		return "WORLD";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_BEGIN_FRAME:
		return "BEGIN_FRAME";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_PREPARE:
		return "PREPARE";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_VIEW:
		return "VIEW";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_TOPOLOGY:
		return "TOPOLOGY";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_RELEASE:
		return "RELEASE";
	case MAIN_ARCADE_RACE_DIGEST_FAILURE_END:
		return "END";
	default:
		return "UNKNOWN";
	}
}

uint32_t MainArcadeRaceDigest_RuntimeFailure(void)
{
	return (s_mainArcadeRaceDigest.failed != 0u) ? s_mainArcadeRaceDigest.runtimeFailure : 0u;
}

#endif
