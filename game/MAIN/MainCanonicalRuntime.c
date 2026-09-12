#include "MainCanonicalRuntime.h"

#include <string.h>

static struct MainCanonicalRuntimeWorkspace s_mainCanonicalRuntimeWorkspace;

CTR_STATIC_ASSERT(sizeof(struct MainCanonicalRuntimeWorkspace) <= MAIN_CANONICAL_RUNTIME_WORKSPACE_MAX_BYTES);

static int MainCanonicalRuntime_RequestMatches(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request)
{
	return (workspace != NULL) && (request != NULL) && (workspace->prepared != 0) &&
		(request->requiredKind == NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3) &&
		(request->replayFrame == workspace->replayFrame) &&
		(memcmp(request->identity.build, workspace->state.identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
		(memcmp(request->identity.content, workspace->state.identity.content, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
}

static int MainCanonicalRuntime_HasRoots(const struct GameTracker *gGT,const struct sData *sourceData)
{
	if ((gGT == NULL) || (sourceData == NULL) || (sourceData->gGT != gGT)) return -1;
	for (uint32_t slot = 0; slot < 8u; slot++) if (gGT->drivers[slot] != NULL) return 1;
	return 0;
}

static void MainCanonicalRuntime_Poison(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if (workspace != NULL)
	{
		workspace->prepared = 0;
		workspace->poisoned = 1;
	}
}

struct MainCanonicalRuntimeWorkspace *MainCanonicalRuntime_Global(void) { return &s_mainCanonicalRuntimeWorkspace; }
size_t MainCanonicalRuntime_WorkspaceSize(void) { return sizeof(struct MainCanonicalRuntimeWorkspace); }

void MainCanonicalRuntime_Init(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if ((workspace == NULL) || (workspace->initialized != 0)) return;
	memset(workspace, 0, sizeof(*workspace));
	MainCanonicalTopology_Init(&workspace->topologyContext);
	workspace->initialized = 1;
}

void MainCanonicalRuntime_Reset(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if (workspace == NULL) return;
	if (workspace->initialized == 0) { MainCanonicalRuntime_Init(workspace); return; }
	MainCanonicalTopology_Invalidate(&workspace->topologyContext);
	memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
	memset(&workspace->sourceCandidate, 0, sizeof(workspace->sourceCandidate));
	memset(&workspace->drivers, 0, sizeof(workspace->drivers));
	memset(&workspace->state, 0, sizeof(workspace->state));
	memset(workspace->normativeScratch, 0, sizeof(workspace->normativeScratch));
	workspace->replayFrame = 0;
	workspace->prepared = 0;
	workspace->poisoned = 0;
}

int MainCanonicalRuntime_BeginFrame(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if ((workspace == NULL) || (workspace->initialized == 0) || (workspace->poisoned != 0) || (workspace->prepared != 0)) return 0;
	workspace->prepared = 0;
	return 1;
}

int MainCanonicalRuntime_InvalidateTopology(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if ((workspace == NULL) || (workspace->initialized == 0) || (workspace->poisoned != 0) || (workspace->prepared != 0)) return 0;
	MainCanonicalTopology_Invalidate(&workspace->topologyContext);
	memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
	return 1;
}

int MainCanonicalRuntime_CaptureTopology(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	if ((workspace == NULL) || (workspace->initialized == 0) || (workspace->poisoned != 0) || (workspace->prepared != 0)) return 0;
	return MainCanonicalTopology_Capture(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData);
}

int MainCanonicalRuntime_PrepareV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct NativeCanonicalControlV1 *control,const struct NativeCanonicalRngV1 *rng,
	const struct NativeCanonicalInputV1 *input)
{
	int roots;
	if ((workspace == NULL) || (request == NULL) || (gGT == NULL) || (sourceData == NULL) || (control == NULL) || (rng == NULL) || (input == NULL) ||
		(workspace->initialized == 0) || (workspace->poisoned != 0) || (workspace->prepared != 0) ||
		(request->requiredKind != NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3))
	{
		MainCanonicalRuntime_Poison(workspace);
		return 0;
	}
	roots = MainCanonicalRuntime_HasRoots(gGT,sourceData);
	if (roots < 0) { MainCanonicalRuntime_Poison(workspace); return 0; }
	if (roots != 0)
	{
		if (workspace->topologyContext.captureActive == 0)
		{
			if (!MainCanonicalTopology_Capture(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData))
			{
				MainCanonicalRuntime_Poison(workspace); return 0;
			}
		}
		else if (!MainCanonicalTopology_Validate(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData))
		{
			/* A stale available capture is a lifecycle error, never a hidden
			 * recapture against potentially reused native memory. */
			MainCanonicalRuntime_Poison(workspace); return 0;
		}
	}
	else if (workspace->topologyContext.captureActive != 0)
	{
		/* A menu/rootless frame retires the prior level generation. */
		MainCanonicalTopology_Invalidate(&workspace->topologyContext);
		memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
	}
	if (!MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMetaPhysics(gGT,sourceData,
		&workspace->topologyContext,&workspace->topologySnapshot,&workspace->sourceCandidate) ||
		!MainCanonicalDrivers_AssembleDetailedWithScratch(&workspace->sourceCandidate,workspace->normativeScratch,
			sizeof(workspace->normativeScratch),&workspace->drivers) ||
		!MainCanonicalState_ProjectV3(&workspace->state,&request->identity,request->replayFrame,control,rng,input,&workspace->drivers.summary))
	{
		MainCanonicalRuntime_Poison(workspace); return 0;
	}
	workspace->replayFrame = request->replayFrame;
	workspace->prepared = 1;
	return 1;
}

const struct NativeCanonicalStateV3 *MainCanonicalRuntime_ViewV3(
	const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request)
{
	return MainCanonicalRuntime_RequestMatches(workspace,request) ? &workspace->state : NULL;
}

int MainCanonicalRuntime_GetSubmissionV3(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	struct NativeReplaySchedulerCanonicalSubmission *submission)
{
	struct NativeReplaySchedulerCanonicalSubmission candidate;
	if ((submission == NULL) || !MainCanonicalRuntime_RequestMatches(workspace,request)) return 0;
	candidate.kind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3;
	candidate.state.v3 = &workspace->state;
	*submission = candidate;
	return 1;
}

int MainCanonicalRuntime_ReleaseV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request)
{
	if (!MainCanonicalRuntime_RequestMatches(workspace,request)) return 0;
	workspace->prepared = 0;
	MainCanonicalTopology_Invalidate(&workspace->topologyContext);
	memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
	return 1;
}
