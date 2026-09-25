#ifndef MAIN_ARCADE_RACE_DIGEST_H
#define MAIN_ARCADE_RACE_DIGEST_H

#include <stdint.h>

#include "platform/native_canonical_state.h"

/*
 * Live V4 race digest (Task 8 race plan LR-10, slice LR-S4; the plan is
 * linked from docs/GAME_LOOP_UI_MILESTONE.md). Native only:
 * game/MAIN/MainArcadeRaceDigest.c is compiled only with CTR_NATIVE.
 *
 * Once per race tick k the caller hands MainArcadeRaceDigest_Project the
 * tick's const game state and the match facts, and gets back the V4
 * canonical state's frame number, combined digest, and domain digests. The
 * module is read-only: it takes const game state only and writes nothing
 * but its own file-scope state and the runtime's workspace. It is the V4
 * runtime coordinator's one live caller, and runs the runtime's per-tick
 * lifecycle, every step on every tick: BeginFrame, PrepareV4, ViewV4 (the
 * digests are copied out of the view), and ReleaseV4.
 *
 * The domains (LR-10):
 * - CONTROL: the retail control values, with frameTimer
 *   (gGT->frameTimer_VsyncCallback) and frameCounter projected
 *   race-relative: the value minus its value at race tick 0, in unsigned
 *   32-bit arithmetic (MainArcadeRaceDigest_ProjectControl). With fixed
 *   pacing they read 2k and k on race tick k.
 * - RNG: the retail RNG values, and a copy of the caller's bank (the
 *   post-setup bank, MainArcadeRaceSetup_Bank()), carried through the
 *   runtime request instead of re-derived; the projector still checks its
 *   masterSeed and derivation version against the config.
 * - INPUT: the caller's frozen pads, the four GameLogic k read.
 * - DRIVERS: the runtime's complete extraction (Physics included).
 * - WORLD: MainCanonicalWorldCounters_ExtractV1 and
 *   MainCanonicalWorldMineRegistry_ExtractV1.
 * - TOPOLOGY: the unavailable summary (NativeCanonicalTopologyV1_Init) on
 *   every tick; the module checks that the view carries exactly it.
 * - Identity and config: the config's build and content identity and its
 *   digest; frameNumber = k.
 *
 * Race tick 0 starts a race: it resets the runtime (so a workspace poisoned
 * in an earlier race never carries into this one) and captures the
 * race-relative base. Every later call must be the next race tick. Any
 * failure is latched until the next race tick 0, and every call after it
 * fails; the caller treats any failure as a local drive failure.
 * MainArcadeRaceDigest_EndRace, on the race's end frame, invalidates the
 * runtime's topology context, because the next race loads a level that can
 * reuse addresses.
 *
 * Nothing here is checkpointed, recorded, or replayed. The module names no
 * topology lease, topology fact reader, or nav path structure: the drivers
 * extraction's one nav read is inside the runtime's drivers extraction
 * (LR-17, ruled (a); tests/main_arcade_race_digest_isolation_test.cmake).
 */

struct GameTracker;
struct sData;
struct OverlayDATA_231;
struct NativeMatchConfigV1;
struct NativeDeterministicRngBankV1;

enum MainArcadeRaceDigestFailure
{
	MAIN_ARCADE_RACE_DIGEST_FAILURE_NONE = 0,
	MAIN_ARCADE_RACE_DIGEST_FAILURE_ARGUMENT = 1,    /* a NULL source or output */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_SEQUENCE = 2,    /* not race tick 0, and not the tick after the last one */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_CONFIG = 3,      /* the config does not digest, or changed during the race */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_WORLD = 4,       /* a world extractor refused the game state */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_BEGIN_FRAME = 5, /* the runtime refused BeginFrame */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_PREPARE = 6,     /* the runtime refused PrepareV4 (see the runtime reason) */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_VIEW = 7,        /* the runtime gave no view */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_TOPOLOGY = 8,    /* the view's topology is not the unavailable summary */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_RELEASE = 9,     /* the runtime refused ReleaseV4 */
	MAIN_ARCADE_RACE_DIGEST_FAILURE_END = 10         /* EndRace could not invalidate the topology context */
};

/* The game state and match facts of one race tick. Every pointer is required. */
struct MainArcadeRaceDigestSources
{
	const struct GameTracker *gGT;
	const struct sData *sourceData;
	const struct OverlayDATA_231 *mineSource;        /* the racing overlay's mine pool (D231) */
	const struct NativeMatchConfigV1 *config;        /* the agreed config; constant for the race */
	const struct NativeDeterministicRngBankV1 *bank; /* the post-setup bank (MainArcadeRaceSetup_Bank()) */
	const struct NativeCanonicalInputV1 *input;      /* the four pads GameLogic of this tick read, frozen */
};

/* The race-relative base: the two boot-relative counters at race tick 0. */
struct MainArcadeRaceDigestBase
{
	uint32_t frameTimer;   /* gGT->frameTimer_VsyncCallback */
	uint32_t frameCounter; /* sdata->frameCounter */
};

/* One projected race tick: the V4 state's frame number and digests, in
 * NativeCanonicalDomainOrder. */
struct MainArcadeRaceDigestTick
{
	uint32_t frameNumber;
	uint32_t reserved;
	uint64_t combinedDigest;
	uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
};

/*
 * Projects race tick raceTick (see above). Returns 1 with *out filled; 0 on
 * any failure, with *out untouched and the failure latched until the next
 * race tick 0. The whole call is one NativePerf scope
 * (NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST).
 */
int MainArcadeRaceDigest_Project(uint32_t raceTick, const struct MainArcadeRaceDigestSources *sources,
	struct MainArcadeRaceDigestTick *out);

/* The race's end frame: invalidates the runtime's topology context. 1 on
 * success; 0 without a race in progress, after a latched failure (the next
 * race tick 0 resets the runtime instead), or when the runtime refuses. */
int MainArcadeRaceDigest_EndRace(void);

/* The latched failure (NONE when the last call succeeded), its fixed name
 * ("NONE", "ARGUMENT", ...; "UNKNOWN" out of range), and the runtime's own
 * failure reason (its MainCanonicalRuntimeFailureReason value; 0 is none). */
enum MainArcadeRaceDigestFailure MainArcadeRaceDigest_Failure(void);
const char *MainArcadeRaceDigest_FailureName(enum MainArcadeRaceDigestFailure failure);
uint32_t MainArcadeRaceDigest_RuntimeFailure(void);

/* The race-relative base of the given state (its counters now). 0 on NULL. */
int MainArcadeRaceDigest_CaptureBase(const struct GameTracker *gGT, const struct sData *sourceData,
	struct MainArcadeRaceDigestBase *base);

/* The CONTROL values of the given state, frameTimer and frameCounter
 * race-relative to base (unsigned 32-bit differences, stored as the same
 * 32 bits); every other value as read. 0 on NULL with *out untouched. */
int MainArcadeRaceDigest_ProjectControl(const struct GameTracker *gGT, const struct sData *sourceData,
	const struct MainArcadeRaceDigestBase *base, struct NativeCanonicalControlV1 *out);

/* The retail RNG values of the given state. 0 on NULL with *out untouched. */
int MainArcadeRaceDigest_ProjectRetailRng(const struct GameTracker *gGT, const struct sData *sourceData,
	struct NativeCanonicalRngV1 *out);

#endif
