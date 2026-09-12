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

struct MainCanonicalRuntimeWorkspace
{
	struct MainCanonicalTopologyContext topologyContext;
	struct MainCanonicalTopologySnapshot topologySnapshot;
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate sourceCandidate;
	struct MainCanonicalDriversDetailedAssembly drivers;
	struct NativeCanonicalStateV3 state;
	uint8_t normativeScratch[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	uint32_t replayFrame;
	uint8_t initialized;
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
 * Release is the explicit handoff completion and retires topology even for a
 * rootless interval. */
const struct NativeCanonicalStateV3 *MainCanonicalRuntime_ViewV3(
	const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request);
int MainCanonicalRuntime_GetSubmissionV3(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	struct NativeReplaySchedulerCanonicalSubmission *submission);
int MainCanonicalRuntime_ReleaseV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request);

#endif
