#ifndef MAIN_CANONICAL_RUNTIME_H
#define MAIN_CANONICAL_RUNTIME_H

#include "MainCanonicalDrivers.h"
#include "platform/native_canonical_projector.h"
#include "platform/native_replay_scheduler.h"

/* This static, non-checkpointed coordinator is the only owner of the full
 * DRIVERS staging values.  No MainMain, load, scheduler, or replay path calls
 * it.  Its one live caller is the race digest module, MainArcadeRaceDigest
 * (the Task 8 race plan's LR-10), which projects V4 once per race tick
 * through BeginFrame, PrepareV4, ViewV4, and ReleaseV4. */
#define MAIN_CANONICAL_RUNTIME_PREPARE_STACK_BUDGET_BYTES 1536u
#define MAIN_CANONICAL_RUNTIME_WORKSPACE_MAX_BYTES 16384u

enum MainCanonicalRuntimeFailureReason
{
	MAIN_CANONICAL_RUNTIME_FAILURE_NONE=0,
	MAIN_CANONICAL_RUNTIME_FAILURE_NULL_ARGUMENT=1,
	MAIN_CANONICAL_RUNTIME_FAILURE_FRAME_STATE=2,
	MAIN_CANONICAL_RUNTIME_FAILURE_REQUEST=3,
	MAIN_CANONICAL_RUNTIME_FAILURE_INPUT=4,
	MAIN_CANONICAL_RUNTIME_FAILURE_RESTORE_EPOCH=5,
	MAIN_CANONICAL_RUNTIME_FAILURE_ROSTER_PREFLIGHT=6,
	MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_CAPTURE=7,
	MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_VALIDATE=8,
	MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_EPOCH=9,
	MAIN_CANONICAL_RUNTIME_FAILURE_SOURCE_EXTRACT=10,
	MAIN_CANONICAL_RUNTIME_FAILURE_ASSEMBLY=11,
	MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT=12
};

struct MainCanonicalRuntimeStageCounts
{
	uint32_t rosterPreflight;
	uint32_t sourceExtract;
	uint32_t assembly;
	uint32_t project;
};

/* A runtime-owned V4 transaction token.  This intentionally does not name any
 * file-session/scheduler type.  The caller supplies the validated match config
 * and its locally computed digest; the projector remains the sole validator. */
struct MainCanonicalRuntimeV4Request
{
	struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 config;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint32_t replayFrame;
	int restoredThisFrame;
	/* The deterministic bank to project (LR-10, LR-19).  NULL: PrepareV4
	 * derives a fresh bank from config.masterSeed, as before.  Non-NULL (the
	 * post-setup bank of a linked race): PrepareV4 copies it into its staging
	 * instead, and the projector still rejects a bank whose masterSeed or
	 * derivation version differs from the config.  The pointer is part of the
	 * transaction token, so View and Release must present the same one. */
	const struct NativeDeterministicRngBankV1 *bank;
};

struct MainCanonicalRuntimeWorkspace
{
	struct MainCanonicalTopologyContext topologyContext;
	struct MainCanonicalTopologySnapshot topologySnapshot;
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate sourceCandidate;
	struct MainCanonicalDriversDetailedAssembly drivers;
	/* A frame prepares exactly one schema generation; the sealed V1/V3 bytes
	 * and the sibling V4 bytes never coexist in one prepared state, so they
	 * share storage to remain inside the fixed workspace ceiling. */
	union
	{
		struct NativeCanonicalStateV3 v3;
		struct NativeCanonicalStateV4 v4;
	} stateCandidate;
	union
	{
		struct NativeCanonicalStateV3 v3;
		struct NativeCanonicalStateV4 v4;
	} state;
	/* Bounded V4 staging reuses the drivers assembly scratch for the derived
	 * RNG bank and one 600-byte domain payload: assembly has completed before
	 * the projector runs.  The projector context is validated before the source
	 * stages and must survive them, so it lives in the request slot instead. */
	union
	{
		uint8_t normativeScratch[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
		struct
		{
			struct NativeDeterministicRngBankV1 deterministicRng;
			uint8_t projectorScratch[NATIVE_CANONICAL_STATE_V4_MAX_DOMAIN_BYTES];
		} v4Scratch;
	};
	union
	{
		struct NativeReplaySchedulerCanonicalRequest v3;
		struct MainCanonicalRuntimeV4Request v4;
		struct MainCanonicalStateV4Context v4Context;
	} preparedRequest;
	struct MainCanonicalRuntimeStageCounts stageCounts;
	enum MainCanonicalRuntimeFailureReason failureReason;
	enum NativeReplaySchedulerCanonicalKind preparedKind;
	uint8_t initialized;
	uint8_t frameActive;
	uint8_t prepared;
	uint8_t poisoned;
};

/* Returns the game-owned BSS workspace; it has no checkpoint serialization. */
struct MainCanonicalRuntimeWorkspace *MainCanonicalRuntime_Global(void);
size_t MainCanonicalRuntime_WorkspaceSize(void);

/* Init is intentionally idempotent: a second call never resets topology's
 * epoch authority. Reset is the explicit new-generation operation. */
void MainCanonicalRuntime_Init(struct MainCanonicalRuntimeWorkspace *workspace);
void MainCanonicalRuntime_Reset(struct MainCanonicalRuntimeWorkspace *workspace);
enum MainCanonicalRuntimeFailureReason MainCanonicalRuntime_FailureReason(
	const struct MainCanonicalRuntimeWorkspace *workspace);

/* A prepared state must be released before another lifecycle mutation. */
int MainCanonicalRuntime_BeginFrame(struct MainCanonicalRuntimeWorkspace *workspace);
int MainCanonicalRuntime_InvalidateTopology(struct MainCanonicalRuntimeWorkspace *workspace);
int MainCanonicalRuntime_CaptureTopology(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct GameTracker *gGT,const struct sData *sourceData);

/* Builds the complete local V3 value once. Source projection and topology are
 * strictly game-owned; the scheduler request is only a typed frame/identity
 * precondition. Any failure latches poisoned until Reset. */
int MainCanonicalRuntime_PrepareV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct NativeCanonicalControlV1 *control,const struct NativeCanonicalRngV1 *rng,
	const struct NativeCanonicalInputV1 *input);

/* Views/submissions are unavailable until a matching prepared request exists.
 * Release is the explicit handoff completion. It preserves a live topology
 * capture for reuse by later frames; restore, rootless transition, explicit
 * invalidation, and Reset are the generation-retirement boundaries. */
const struct NativeCanonicalStateV3 *MainCanonicalRuntime_ViewV3(
	const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request);
int MainCanonicalRuntime_GetSubmissionV3(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	struct NativeReplaySchedulerCanonicalSubmission *submission);
int MainCanonicalRuntime_ReleaseV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request);

/* V4 mirrors the V3 lifecycle but consumes a runtime-owned request.  WORLD and
 * TOPOLOGY are caller-supplied canonical domain values: this coordinator never
 * runs the mine/counter extractors, reads D231, or acquires, activates,
 * captures, or publishes a topology lease.  It does read NavHeader.last, once
 * per bot, check-only: the drivers extraction's MainCanonicalDrivers_BotNavIndex
 * proves each bot's botNavFrame lies in its own path's frame array.  That read
 * is never written and never used for the lease (LR-17, ruled (a)); the lease's
 * MainCanonicalTopologyLease_ObservePostInit is the only other reader. */
int MainCanonicalRuntime_PrepareV4(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct NativeCanonicalControlV1 *control,const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology);
const struct NativeCanonicalStateV4 *MainCanonicalRuntime_ViewV4(
	const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request);
int MainCanonicalRuntime_GetSubmissionV4(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request,
	struct NativeReplaySchedulerCanonicalSubmission *submission);
int MainCanonicalRuntime_ReleaseV4(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request);

#ifdef MAIN_CANONICAL_RUNTIME_TESTING
void MainCanonicalRuntime_TestForceFailure(enum MainCanonicalRuntimeFailureReason reason);
#endif

#endif
