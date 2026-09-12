#ifndef MAIN_CANONICAL_RUNTIME_H
#define MAIN_CANONICAL_RUNTIME_H

#include "MainCanonicalDrivers.h"
#include "platform/native_canonical_projector.h"
#include "platform/native_replay_scheduler.h"

/* This static, non-checkpointed coordinator is the only future owner of the
 * full DRIVERS staging values.  It is deliberately dormant: no MainMain,
 * load, scheduler, or replay path calls it in this slice. */
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

struct MainCanonicalRuntimeWorkspace
{
	struct MainCanonicalTopologyContext topologyContext;
	struct MainCanonicalTopologySnapshot topologySnapshot;
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate sourceCandidate;
	struct MainCanonicalDriversDetailedAssembly drivers;
	struct NativeCanonicalStateV3 stateCandidate;
	struct NativeCanonicalStateV3 state;
	uint8_t normativeScratch[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	struct NativeReplaySchedulerCanonicalRequest preparedRequest;
	struct MainCanonicalRuntimeStageCounts stageCounts;
	enum MainCanonicalRuntimeFailureReason failureReason;
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

#ifdef MAIN_CANONICAL_RUNTIME_TESTING
void MainCanonicalRuntime_TestForceFailure(enum MainCanonicalRuntimeFailureReason reason);
#endif

#endif
