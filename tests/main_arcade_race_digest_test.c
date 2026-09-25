/*
 * main_arcade_race_digest_unit: the live V4 race digest
 * (game/MAIN/MainArcadeRaceDigest.{c,h}, Task 8 race plan LR-10, slice
 * LR-S4) over the real V4 runtime, drivers extraction, projector, and world
 * extractors, on a rootless game fixture (no driver roots, so the drivers
 * domain is the empty roster and no topology context is captured).
 *
 * It covers the race-relative control projection (including the unsigned
 * 32-bit wrap), the unavailable TOPOLOGY summary on every tick (its domain
 * digest pinned to the constant tools/arcade-roster-proof-check.ps1
 * requires), the bank carried through the request, the per-tick lifecycle
 * and sequence rules, a clean race 2 after a race 1 poisoned by a forced
 * runtime failure, and the NativePerf scope.
 */
#include "common.h"
#include "MAIN/MainCanonicalDrivers.h"
#include "functions.h"
#include "platform/native_perf.h"
#include <limits.h>
#include <stdio.h>
struct sData sdata_static;
struct Data data;
#define DRIVER_STUB(name) void name(struct Thread*t,struct Driver*d){(void)t;(void)d;}
DRIVER_STUB(VehPhysProc_Driving_Init) DRIVER_STUB(VehStuckProc_RevEngine_Init) DRIVER_STUB(VehPhysProc_FreezeEndEvent_Init) DRIVER_STUB(VehStuckProc_Warp_Init) DRIVER_STUB(VehStuckProc_RIP_Init) DRIVER_STUB(VehStuckProc_Tumble_Init) DRIVER_STUB(VehStuckProc_PlantEaten_Init) DRIVER_STUB(VehPhysProc_SpinFirst_Init) DRIVER_STUB(VehPhysProc_PowerSlide_InitSetUpdate) DRIVER_STUB(VehPhysProc_SpinFirst_InitSetUpdate)
DRIVER_STUB(VehPhysProc_Driving_Update) DRIVER_STUB(VehPhysProc_Driving_PhysLinear) DRIVER_STUB(VehPhysProc_Driving_Audio) DRIVER_STUB(VehPhysGeneral_PhysAngular) DRIVER_STUB(VehPhysForce_OnApplyForces) DRIVER_STUB(COLL_MOVED_PlayerSearch) DRIVER_STUB(VehPhysForce_CollideDrivers) DRIVER_STUB(COLL_FIXED_PlayerSearch) DRIVER_STUB(VehPhysGeneral_JumpAndFriction) DRIVER_STUB(VehPhysForce_TranslateMatrix) DRIVER_STUB(VehFrameProc_Driving) DRIVER_STUB(VehEmitter_DriverMain)
DRIVER_STUB(VehPhysProc_FreezeEndEvent_PhysLinear) DRIVER_STUB(VehPhysProc_FreezeVShift_Update) DRIVER_STUB(VehPhysProc_FreezeVShift_ReverseOneFrame) DRIVER_STUB(VehPhysProc_PowerSlide_PhysLinear) DRIVER_STUB(VehPhysProc_PowerSlide_Update) DRIVER_STUB(VehPhysProc_PowerSlide_PhysAngular) DRIVER_STUB(VehPhysProc_SlamWall_Update) DRIVER_STUB(VehPhysProc_SlamWall_PhysLinear) DRIVER_STUB(VehPhysProc_SlamWall_PhysAngular) DRIVER_STUB(VehPhysProc_SlamWall_Animate) DRIVER_STUB(VehPhysProc_SpinFirst_PhysLinear) DRIVER_STUB(VehPhysProc_SpinFirst_PhysAngular) DRIVER_STUB(VehFrameProc_Spinning) DRIVER_STUB(VehPhysProc_SpinFirst_Update) DRIVER_STUB(VehPhysProc_SpinLast_Update) DRIVER_STUB(VehPhysProc_SpinLast_PhysLinear) DRIVER_STUB(VehPhysProc_SpinLast_PhysAngular) DRIVER_STUB(VehFrameProc_LastSpin) DRIVER_STUB(VehPhysProc_SpinStop_Update) DRIVER_STUB(VehPhysProc_SpinStop_PhysLinear) DRIVER_STUB(VehPhysProc_SpinStop_PhysAngular) DRIVER_STUB(VehPhysProc_SpinStop_Animate)
DRIVER_STUB(VehStuckProc_MaskGrab_Update) DRIVER_STUB(VehStuckProc_MaskGrab_PhysLinear) DRIVER_STUB(VehStuckProc_MaskGrab_Animate) DRIVER_STUB(VehStuckProc_PlantEaten_Update) DRIVER_STUB(VehStuckProc_PlantEaten_PhysLinear) DRIVER_STUB(VehStuckProc_PlantEaten_Animate) DRIVER_STUB(VehStuckProc_RevEngine_Update) DRIVER_STUB(VehStuckProc_RevEngine_PhysLinear) DRIVER_STUB(VehStuckProc_RevEngine_Animate) DRIVER_STUB(VehStuckProc_Tumble_Update) DRIVER_STUB(VehStuckProc_Tumble_PhysLinear) DRIVER_STUB(VehStuckProc_Tumble_PhysAngular) DRIVER_STUB(VehStuckProc_Tumble_Animate) DRIVER_STUB(VehStuckProc_Warp_PhysAngular)
void VehBirth_NullThread(struct Thread*t){(void)t;} void BOTS_ThTick_Drive(struct Thread*t){(void)t;} void BOTS_ThTick_RevEngine(struct Thread*t){(void)t;}
void RB_RainCloud_ThTick(struct Thread*t){(void)t;} void RB_RainCloud_FadeAway(struct Thread*t){(void)t;} void RB_MaskWeapon_ThTick(struct Thread*t){(void)t;}
#include "../game/MAIN/MainCanonicalTopology.c"
#include "../game/MAIN/MainCanonicalDrivers.c"
#include "../game/MAIN/MainCanonicalState.c"
#include "../game/MAIN/MainCanonicalStateV4.c"
#define MAIN_CANONICAL_RUNTIME_TESTING 1
#include "../game/MAIN/MainCanonicalRuntime.c"
#include "../game/MAIN/MainCanonicalWorldCounters.c"
#include "../game/MAIN/MainCanonicalWorldMineRegistry.c"
#include "../game/MAIN/MainArcadeRaceDigest.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "main_arcade_race_digest_test: failed at line %d: %s\n", __LINE__, #x); return 0; } } while (0)

/* The TOPOLOGY domain digest of the unavailable summary
 * (NativeCanonicalTopologyV1_Init), as V4 encodes and digests it. The roster
 * proof check (tools/arcade-roster-proof-check.ps1, $unavailableTopologyDigest)
 * requires this value on every tick line; main_arcade_race_digest_isolation
 * pins the two copies equal. */
#define UNAVAILABLE_TOPOLOGY_DIGEST UINT64_C(0xd75d92ae427cd357)

/* NativePerf is an internal library; the digest's one scope is counted here. */
static int s_perfBegin;
static int s_perfEnd;
static int s_perfOther;
static int s_perfOpen;
void NativePerf_BeginScope(enum NativePerfBucket bucket)
{
	if (bucket != NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST) { s_perfOther++; return; }
	s_perfBegin++;
	s_perfOpen++;
}
void NativePerf_EndScope(enum NativePerfBucket bucket)
{
	if (bucket != NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST) { s_perfOther++; return; }
	s_perfEnd++;
	s_perfOpen--;
}

struct Fixture
{
	struct GameTracker tracker;
	struct OverlayDATA_231 mines;
	struct NativeMatchConfigV1 config;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeCanonicalInputV1 input;
	struct MainArcadeRaceDigestSources sources;
};

static struct Fixture s_fixture;

/* A rootless race frame: boot-relative counters at an arbitrary phase, a
 * validated two-cab config, and a post-setup bank (drawn from once). */
static int FixtureInit(struct Fixture *f, int32_t frameTimer, int32_t frameCounter)
{
	uint32_t drawn = 0u;

	memset(f, 0, sizeof(*f));
	memset(&sdata_static, 0, sizeof(sdata_static));
	memset(&data, 0, sizeof(data));
	sdata_static.gGT = &f->tracker;
	f->tracker.frameTimer_VsyncCallback = frameTimer;
	sdata_static.frameCounter = frameCounter;
	f->tracker.timer = 1;
	f->tracker.elapsedTimeMS = 32;
	f->tracker.levelID = 3;
	f->tracker.gameMode1 = 0x2000;
	f->tracker.numMissiles = 2u;
	f->tracker.overlayIndex_Threads = (u8)OVERLAY_INDEX_MAIN_MENU;
	sdata_static.randomNumber = 0x1234;
	sdata_static.advRng.state0 = 0x11111111;
	sdata_static.advRng.state1 = 0x22222222;
	NativeMatchConfigV1_InitArcadeTwoCab(&f->config);
	f->config.trackID = 7;
	f->config.lapCount = 3;
	f->config.tickRateNumerator = 30;
	f->config.tickRateDenominator = 1;
	f->config.masterSeed = UINT64_C(0x5EED);
	for (uint32_t index = 0; index < 32u; index++)
	{
		f->config.buildIdentity[index] = (uint8_t)(0x10u + index);
		f->config.contentIdentity[index] = (uint8_t)(0x80u + index);
		f->config.botRulesDigest[index] = (uint8_t)(0x40u + index);
	}
	for (uint32_t index = 0; index < 6u; index++)
	{
		f->config.slots[index].characterID = (uint8_t)index;
		f->config.slots[index].difficulty = 2;
	}
	if (!NativeMatchConfigV1_Validate(&f->config) ||
	    !NativeDeterministicRngBankV1_Init(&f->bank, f->config.masterSeed, f->config.rngDerivationVersion) ||
	    !NativeDeterministicRngBankV1_NextU32(&f->bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
		    NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &drawn))
	{
		return 0;
	}
	f->input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT;
	f->input.pads[0].connected = 1u;
	f->input.pads[0].id = 0x41u;
	f->input.pads[0].buttons[0] = 0xFFu;
	f->input.pads[0].buttons[1] = 0xBFu;
	f->sources.gGT = &f->tracker;
	f->sources.sourceData = &sdata_static;
	f->sources.mineSource = &f->mines;
	f->sources.config = &f->config;
	f->sources.bank = &f->bank;
	f->sources.input = &f->input;
	return 1;
}

/* One simulated race tick: 2 VBlanks and 1 frame, as fixed pacing gives. */
static void FixtureAdvance(struct Fixture *f)
{
	f->tracker.frameTimer_VsyncCallback += 2;
	sdata_static.frameCounter += 1;
	f->tracker.timer += 1;
	f->input.pads[0].buttons[1] ^= 0x40u;
}

static uint32_t DomainIndex(uint32_t domain)
{
	uint32_t index = 0;

	while ((index < NATIVE_CANONICAL_DOMAIN_COUNT) && (NativeCanonicalDomainOrder[index] != domain))
	{
		index++;
	}
	return index;
}

/*
 * The expected state of race tick k, built without the digest module: the
 * control values by hand (frameTimer 2k and frameCounter k race-relative),
 * the retail RNG, the fixture's bank, the frozen pads, the runtime's drivers
 * summary, the extractors' world values, and the unavailable topology.
 */
static int Expected(const struct Fixture *f, uint32_t k, struct NativeCanonicalStateV4 *out)
{
	struct MainCanonicalStateV4Context context;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 rng;
	struct NativeCanonicalWorldCountersV1 counters;
	struct NativeCanonicalWorldMineRegistryV1 mines;
	struct NativeCanonicalTopologyV1 topology;

	memset(&control, 0, sizeof(control));
	control.frameTimer = (int32_t)(2u * k);
	control.frameCounter = (int32_t)k;
	control.timer = f->tracker.timer;
	control.elapsedTimeMS = 32;
	control.levelID = 3;
	control.gameMode1 = 0x2000;
	rng.mixRandomNumber = 0x1234u;
	rng.deadcoed0 = 0u;
	rng.deadcoed1 = 0u;
	rng.advRng0 = 0x11111111u;
	rng.advRng1 = 0x22222222u;
	memcpy(identity.build, f->config.buildIdentity, sizeof(identity.build));
	memcpy(identity.content, f->config.contentIdentity, sizeof(identity.content));
	NativeCanonicalTopologyV1_Init(&topology);
	return MainCanonicalStateV4Context_Init(&context, &f->config) &&
	       MainCanonicalWorldCounters_ExtractV1(&f->tracker, &counters) && (counters.activeBombMissileCount == 2u) &&
	       MainCanonicalWorldMineRegistry_ExtractV1(&f->tracker, &f->mines, &mines) &&
	       MainCanonicalState_ProjectV4(out, &context, &identity, k, &control, &rng, &f->bank, &f->input,
		       &MainCanonicalRuntime_Global()->drivers.summary, &counters, &mines, &topology);
}

static int SameDigests(const struct MainArcadeRaceDigestTick *tick, const struct NativeCanonicalStateV4 *state)
{
	return (tick->frameNumber == state->frameNumber) && (tick->combinedDigest == state->combinedDigest) &&
	       (memcmp(tick->domainDigests, state->domainDigests, sizeof(tick->domainDigests)) == 0);
}

/* The race-relative control projection, the wrap included. */
static int TestControlProjection(void)
{
	struct Fixture *f = &s_fixture;
	struct MainArcadeRaceDigestBase base;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalControlV1 before;
	struct NativeCanonicalRngV1 rng;

	CHECK(FixtureInit(f, 2315, 733));
	CHECK(MainArcadeRaceDigest_CaptureBase(&f->tracker, &sdata_static, &base));
	CHECK(base.frameTimer == 2315u && base.frameCounter == 733u);
	CHECK(MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, &base, &control));
	CHECK(control.frameTimer == 0 && control.frameCounter == 0 && control.timer == 1 && control.elapsedTimeMS == 32 &&
	      control.levelID == 3 && control.gameMode1 == 0x2000);
	for (uint32_t k = 1; k <= 5u; k++)
	{
		FixtureAdvance(f);
		CHECK(MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, &base, &control));
		CHECK(control.frameTimer == (int32_t)(2u * k) && control.frameCounter == (int32_t)k && control.timer == (int32_t)(1u + k));
	}
	/* Unsigned 32-bit differences: across the wrap of either counter. */
	base.frameTimer = 0xFFFFFFFEu;
	base.frameCounter = 0x7FFFFFFFu;
	f->tracker.frameTimer_VsyncCallback = 2;
	sdata_static.frameCounter = INT_MIN;
	CHECK(MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, &base, &control));
	CHECK(control.frameTimer == 4 && control.frameCounter == 1);
	/* A counter behind its base is the same 32 bits, negative. */
	base.frameTimer = 10u;
	base.frameCounter = 0u;
	f->tracker.frameTimer_VsyncCallback = 9;
	sdata_static.frameCounter = INT_MAX;
	CHECK(MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, &base, &control));
	CHECK(control.frameTimer == -1 && control.frameCounter == INT_MAX);
	base.frameTimer = 0u;
	f->tracker.frameTimer_VsyncCallback = INT_MIN;
	CHECK(MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, &base, &control) && control.frameTimer == INT_MIN);
	/* NULL arguments leave the output alone. */
	before = control;
	CHECK(!MainArcadeRaceDigest_ProjectControl(NULL, &sdata_static, &base, &control));
	CHECK(!MainArcadeRaceDigest_ProjectControl(&f->tracker, NULL, &base, &control));
	CHECK(!MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, NULL, &control));
	CHECK(!MainArcadeRaceDigest_ProjectControl(&f->tracker, &sdata_static, &base, NULL));
	CHECK(memcmp(&control, &before, sizeof(control)) == 0);
	CHECK(!MainArcadeRaceDigest_CaptureBase(NULL, &sdata_static, &base) && !MainArcadeRaceDigest_CaptureBase(&f->tracker, &sdata_static, NULL));
	CHECK(MainArcadeRaceDigest_ProjectRetailRng(&f->tracker, &sdata_static, &rng));
	CHECK(rng.mixRandomNumber == 0x1234u && rng.advRng0 == 0x11111111u && rng.advRng1 == 0x22222222u);
	CHECK(!MainArcadeRaceDigest_ProjectRetailRng(&f->tracker, &sdata_static, NULL));
	return 1;
}

/* A clean race: every tick equals the independent expectation; the topology
 * digest is the unavailable summary's; the bank is the caller's. */
static int TestRace(void)
{
	struct Fixture *f = &s_fixture;
	struct MainArcadeRaceDigestTick tick;
	struct MainArcadeRaceDigestTick first[4];
	struct NativeCanonicalStateV4 expected;
	struct NativeDeterministicRngBankV1 fresh;
	const uint32_t topologyIndex = DomainIndex(NATIVE_CANONICAL_DOMAIN_TOPOLOGY);
	const uint32_t rngIndex = DomainIndex(NATIVE_CANONICAL_DOMAIN_RNG);
	const int perfBegin = s_perfBegin;

	CHECK(topologyIndex < NATIVE_CANONICAL_DOMAIN_COUNT && rngIndex < NATIVE_CANONICAL_DOMAIN_COUNT);
	CHECK(FixtureInit(f, 2315, 733));
	for (uint32_t k = 0; k < 4u; k++)
	{
		memset(&tick, 0xA5, sizeof(tick));
		CHECK(MainArcadeRaceDigest_Project(k, &f->sources, &tick));
		CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_NONE);
		CHECK(Expected(f, k, &expected));
		CHECK(SameDigests(&tick, &expected));
		CHECK(tick.frameNumber == k && tick.reserved == 0u);
		if (tick.domainDigests[topologyIndex] != UNAVAILABLE_TOPOLOGY_DIGEST)
		{
			fprintf(stderr, "main_arcade_race_digest_test: the unavailable topology digest is %08lx%08lx\n",
				(unsigned long)(uint32_t)(tick.domainDigests[topologyIndex] >> 32),
				(unsigned long)(uint32_t)(tick.domainDigests[topologyIndex] & 0xFFFFFFFFu));
		}
		CHECK(tick.domainDigests[topologyIndex] == UNAVAILABLE_TOPOLOGY_DIGEST);
		first[k] = tick;
		/* The lifecycle ran to Release: nothing is prepared or active. */
		CHECK(!MainCanonicalRuntime_Global()->prepared && !MainCanonicalRuntime_Global()->frameActive &&
		      !MainCanonicalRuntime_Global()->poisoned);
		FixtureAdvance(f);
	}
	CHECK(s_perfBegin == perfBegin + 4 && s_perfEnd == s_perfBegin && s_perfOpen == 0 && s_perfOther == 0);
	/* The bank is the caller's post-setup bank, not a fresh derivation. */
	CHECK(NativeDeterministicRngBankV1_Init(&fresh, f->config.masterSeed, f->config.rngDerivationVersion));
	CHECK(memcmp(&fresh, &f->bank, sizeof(fresh)) != 0);
	CHECK(MainArcadeRaceDigest_EndRace() == 1);
	CHECK(!MainArcadeRaceDigest_EndRace());

	/* The boot history does not matter: a race from another counter phase
	 * (an odd frameTimer offset, as run E of the roster proof has) gives the
	 * same digests on every tick, because the two counters are race-relative. */
	CHECK(FixtureInit(f, 2315 + 37 * 2 + 1, 733 + 37));
	for (uint32_t k = 0; k < 4u; k++)
	{
		CHECK(MainArcadeRaceDigest_Project(k, &f->sources, &tick));
		CHECK(memcmp(&tick, &first[k], sizeof(tick)) == 0);
		FixtureAdvance(f);
	}
	/* A pacing fault shows: one extra VBlank changes the control digest only. */
	f->tracker.frameTimer_VsyncCallback += 1;
	CHECK(MainArcadeRaceDigest_Project(4u, &f->sources, &tick));
	CHECK(Expected(f, 4u, &expected) && !SameDigests(&tick, &expected));
	CHECK(tick.domainDigests[rngIndex] == expected.domainDigests[rngIndex]);
	CHECK(tick.domainDigests[DomainIndex(NATIVE_CANONICAL_DOMAIN_CONTROL)] != expected.domainDigests[DomainIndex(NATIVE_CANONICAL_DOMAIN_CONTROL)]);
	CHECK(MainArcadeRaceDigest_EndRace() == 1);

	/* Another bank (another seed's config and bank) moves the RNG domain. */
	CHECK(FixtureInit(f, 2315, 733));
	f->config.masterSeed = UINT64_C(0x5EEE);
	CHECK(NativeDeterministicRngBankV1_Init(&f->bank, f->config.masterSeed, f->config.rngDerivationVersion));
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick));
	CHECK(tick.domainDigests[rngIndex] != first[0].domainDigests[rngIndex]);
	CHECK(tick.domainDigests[topologyIndex] == UNAVAILABLE_TOPOLOGY_DIGEST);
	CHECK(MainArcadeRaceDigest_EndRace() == 1);
	return 1;
}

/* The sequence rules, the failure latch, and each local failure. */
static int TestFailures(void)
{
	struct Fixture *f = &s_fixture;
	struct MainArcadeRaceDigestTick tick;
	struct MainArcadeRaceDigestTick before;
	struct MainArcadeRaceDigestSources sources;
	struct NativeDeterministicRngBankV1 foreign;

	/* No race: only race tick 0 starts one. */
	CHECK(FixtureInit(f, 100, 50));
	CHECK(!MainArcadeRaceDigest_EndRace());
	memset(&tick, 0x5A, sizeof(tick));
	before = tick;
	CHECK(!MainArcadeRaceDigest_Project(5u, &f->sources, &tick) && memcmp(&tick, &before, sizeof(tick)) == 0);
	CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE);
	CHECK(strcmp(MainArcadeRaceDigest_FailureName(MainArcadeRaceDigest_Failure()), "SEQUENCE") == 0);

	/* A skipped tick latches SEQUENCE; the latch holds for the next tick too. */
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick));
	CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_NONE);
	FixtureAdvance(f);
	CHECK(!MainArcadeRaceDigest_Project(2u, &f->sources, &tick));
	CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE);
	CHECK(!MainArcadeRaceDigest_Project(1u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE);
	CHECK(!MainArcadeRaceDigest_EndRace());
	/* A repeated tick is a sequence failure too. */
	CHECK(FixtureInit(f, 100, 50));
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick) && MainArcadeRaceDigest_Project(1u, &f->sources, &tick));
	CHECK(!MainArcadeRaceDigest_Project(1u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE);

	/* Every source is required. */
	for (uint32_t missing = 0; missing < 7u; missing++)
	{
		CHECK(FixtureInit(f, 100, 50));
		sources = f->sources;
		switch (missing)
		{
		case 0: sources.gGT = NULL; break;
		case 1: sources.sourceData = NULL; break;
		case 2: sources.mineSource = NULL; break;
		case 3: sources.config = NULL; break;
		case 4: sources.bank = NULL; break;
		case 5: sources.input = NULL; break;
		default: break;
		}
		if (missing < 6u)
		{
			CHECK(!MainArcadeRaceDigest_Project(0u, &sources, &tick));
		}
		else
		{
			CHECK(!MainArcadeRaceDigest_Project(0u, &f->sources, NULL));
		}
		CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_ARGUMENT);
	}
	CHECK(!MainArcadeRaceDigest_Project(0u, NULL, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_ARGUMENT);

	/* The config may not change during the race. */
	CHECK(FixtureInit(f, 100, 50));
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick));
	f->config.trackID++;
	CHECK(!MainArcadeRaceDigest_Project(1u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_CONFIG);

	/* A world extractor that refuses the game state (an unknown overlay). */
	CHECK(FixtureInit(f, 100, 50));
	f->tracker.overlayIndex_Threads = 7u;
	CHECK(!MainArcadeRaceDigest_Project(0u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_WORLD);

	/* A bank that is not the config's (masterSeed): the projector refuses it. */
	CHECK(FixtureInit(f, 100, 50));
	foreign = f->bank;
	foreign.masterSeed ^= UINT64_C(1);
	f->sources.bank = &foreign;
	CHECK(!MainArcadeRaceDigest_Project(0u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_PREPARE);
	CHECK(MainArcadeRaceDigest_RuntimeFailure() == (uint32_t)MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT);

	/* A runtime that refuses the lifecycle (a frame already active). */
	CHECK(FixtureInit(f, 100, 50));
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick));
	CHECK(MainCanonicalRuntime_BeginFrame(MainCanonicalRuntime_Global()));
	FixtureAdvance(f);
	CHECK(!MainArcadeRaceDigest_Project(1u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_BEGIN_FRAME);

	CHECK(strcmp(MainArcadeRaceDigest_FailureName(MAIN_ARCADE_RACE_DIGEST_FAILURE_END), "END") == 0);
	CHECK(strcmp(MainArcadeRaceDigest_FailureName((enum MainArcadeRaceDigestFailure)99), "UNKNOWN") == 0);
	CHECK(s_perfEnd == s_perfBegin && s_perfOpen == 0 && s_perfOther == 0);
	return 1;
}

/* Race 1 is poisoned by a forced runtime failure; race 2 is clean, and its
 * ticks equal a never-poisoned race's. */
static int TestPoisonedRaceThenCleanRace(void)
{
	struct Fixture *f = &s_fixture;
	struct MainArcadeRaceDigestTick clean[3];
	struct MainArcadeRaceDigestTick tick;

	CHECK(FixtureInit(f, 4000, 1999));
	for (uint32_t k = 0; k < 3u; k++)
	{
		CHECK(MainArcadeRaceDigest_Project(k, &f->sources, &clean[k]));
		FixtureAdvance(f);
	}
	CHECK(MainArcadeRaceDigest_EndRace() == 1);

	/* Race 1: tick 1 fails inside PrepareV4 and poisons the workspace. */
	CHECK(FixtureInit(f, 4000, 1999));
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick) && memcmp(&tick, &clean[0], sizeof(tick)) == 0);
	FixtureAdvance(f);
	MainCanonicalRuntime_TestForceFailure(MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT);
	CHECK(!MainArcadeRaceDigest_Project(1u, &f->sources, &tick));
	MainCanonicalRuntime_TestForceFailure(MAIN_CANONICAL_RUNTIME_FAILURE_NONE);
	CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_PREPARE);
	CHECK(MainArcadeRaceDigest_RuntimeFailure() == (uint32_t)MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT);
	CHECK(MainCanonicalRuntime_Global()->poisoned);
	/* The race stays failed: the next tick fails without touching the runtime. */
	FixtureAdvance(f);
	CHECK(!MainArcadeRaceDigest_Project(2u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_PREPARE);
	CHECK(!MainArcadeRaceDigest_EndRace());
	CHECK(MainCanonicalRuntime_Global()->poisoned);

	/* Race 2: race tick 0 resets the runtime, and every tick is clean. */
	CHECK(FixtureInit(f, 4000, 1999));
	for (uint32_t k = 0; k < 3u; k++)
	{
		CHECK(MainArcadeRaceDigest_Project(k, &f->sources, &tick));
		CHECK(MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_NONE && MainArcadeRaceDigest_RuntimeFailure() == 0u);
		CHECK(memcmp(&tick, &clean[k], sizeof(tick)) == 0);
		FixtureAdvance(f);
	}
	CHECK(!MainCanonicalRuntime_Global()->poisoned);
	CHECK(MainArcadeRaceDigest_EndRace() == 1);
	return 1;
}

/* EndRace invalidates the runtime's topology context: a new epoch. */
static int TestEndRace(void)
{
	struct Fixture *f = &s_fixture;
	struct MainArcadeRaceDigestTick tick;
	uint64_t epoch;

	CHECK(FixtureInit(f, 10, 20));
	CHECK(MainArcadeRaceDigest_Project(0u, &f->sources, &tick));
	epoch = MainCanonicalRuntime_Global()->topologyContext.currentEpoch;
	CHECK(MainArcadeRaceDigest_EndRace() == 1);
	CHECK(MainCanonicalRuntime_Global()->topologyContext.currentEpoch == epoch + 1u);
	CHECK(!MainCanonicalRuntime_Global()->topologyContext.captureActive);
	/* After the end frame the race is over: only race tick 0 starts one. */
	CHECK(!MainArcadeRaceDigest_Project(1u, &f->sources, &tick) && MainArcadeRaceDigest_Failure() == MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE);
	return 1;
}

int main(void)
{
	if (!TestControlProjection() || !TestRace() || !TestFailures() || !TestPoisonedRaceThenCleanRace() || !TestEndRace())
	{
		return 1;
	}
	puts("main_arcade_race_digest_test: passed");
	return 0;
}
