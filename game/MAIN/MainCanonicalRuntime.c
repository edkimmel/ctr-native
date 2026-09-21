#include "MainCanonicalRuntime.h"

#include <string.h>

static struct MainCanonicalRuntimeWorkspace s_mainCanonicalRuntimeWorkspace;

#ifdef MAIN_CANONICAL_RUNTIME_TESTING
static enum MainCanonicalRuntimeFailureReason s_mainCanonicalRuntimeForcedFailure;
void MainCanonicalRuntime_TestForceFailure(enum MainCanonicalRuntimeFailureReason reason)
{
	s_mainCanonicalRuntimeForcedFailure=reason;
}
#define MAIN_CANONICAL_RUNTIME_FORCED(reason) (s_mainCanonicalRuntimeForcedFailure==(reason))
#else
#define MAIN_CANONICAL_RUNTIME_FORCED(reason) 0
#endif

CTR_STATIC_ASSERT(sizeof(struct MainCanonicalRuntimeWorkspace) <= MAIN_CANONICAL_RUNTIME_WORKSPACE_MAX_BYTES);

static int MainCanonicalRuntime_RequestMatches(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request)
{
	return (workspace != NULL) && (request != NULL) && (workspace->prepared != 0) &&
		(workspace->preparedKind == NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3) &&
		(request->requiredKind == workspace->preparedRequest.v3.requiredKind) &&
		(request->replayFrame == workspace->preparedRequest.v3.replayFrame) &&
		(request->restoredThisFrame == workspace->preparedRequest.v3.restoredThisFrame) &&
		(memcmp(request->identity.build, workspace->preparedRequest.v3.identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
		(memcmp(request->identity.content, workspace->preparedRequest.v3.identity.content, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
}

static int MainCanonicalRuntime_V4RequestMatches(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request)
{
	return (workspace != NULL) && (request != NULL) && (workspace->prepared != 0) &&
		(workspace->preparedKind == NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V4) &&
		(request->replayFrame == workspace->preparedRequest.v4.replayFrame) &&
		(request->restoredThisFrame == workspace->preparedRequest.v4.restoredThisFrame) &&
		(memcmp(request->identity.build, workspace->preparedRequest.v4.identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
		(memcmp(request->identity.content, workspace->preparedRequest.v4.identity.content, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
		(memcmp(&request->config, &workspace->preparedRequest.v4.config, sizeof(request->config)) == 0) &&
		(memcmp(request->configDigest, workspace->preparedRequest.v4.configDigest, NATIVE_SHA256_DIGEST_BYTES) == 0);
}

static void MainCanonicalRuntime_Poison(struct MainCanonicalRuntimeWorkspace *workspace,
	enum MainCanonicalRuntimeFailureReason reason)
{
	if (workspace != NULL)
	{
		if (workspace->failureReason == MAIN_CANONICAL_RUNTIME_FAILURE_NONE) workspace->failureReason = reason;
		workspace->preparedKind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_NONE;
		workspace->frameActive = 0;
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
	memset(&workspace->stateCandidate, 0, sizeof(workspace->stateCandidate));
	memset(&workspace->state, 0, sizeof(workspace->state));
	memset(workspace->normativeScratch, 0, sizeof(workspace->normativeScratch));
	memset(&workspace->preparedRequest, 0, sizeof(workspace->preparedRequest));
	memset(&workspace->stageCounts, 0, sizeof(workspace->stageCounts));
	workspace->failureReason = MAIN_CANONICAL_RUNTIME_FAILURE_NONE;
	workspace->preparedKind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_NONE;
	workspace->frameActive = 0;
	workspace->prepared = 0;
	workspace->poisoned = 0;
}

enum MainCanonicalRuntimeFailureReason MainCanonicalRuntime_FailureReason(
	const struct MainCanonicalRuntimeWorkspace *workspace)
{
	return workspace ? workspace->failureReason : MAIN_CANONICAL_RUNTIME_FAILURE_NULL_ARGUMENT;
}

int MainCanonicalRuntime_BeginFrame(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if ((workspace == NULL) || (workspace->initialized == 0) || (workspace->poisoned != 0) ||
		(workspace->prepared != 0) || (workspace->frameActive != 0)) return 0;
	workspace->frameActive = 1;
	return 1;
}

int MainCanonicalRuntime_InvalidateTopology(struct MainCanonicalRuntimeWorkspace *workspace)
{
	if ((workspace == NULL) || (workspace->initialized == 0) || (workspace->poisoned != 0) ||
		(workspace->prepared != 0) || (workspace->frameActive != 0)) return 0;
	MainCanonicalTopology_Invalidate(&workspace->topologyContext);
	memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
	return workspace->topologyContext.currentEpoch!=0&&workspace->topologyContext.currentEpoch!=UINT64_MAX;
}

int MainCanonicalRuntime_CaptureTopology(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	if ((workspace == NULL) || (workspace->initialized == 0) || (workspace->poisoned != 0) ||
		(workspace->prepared != 0) || (workspace->frameActive != 0)) return 0;
	return MainCanonicalTopology_Capture(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData);
}

int MainCanonicalRuntime_PrepareV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct NativeCanonicalControlV1 *control,const struct NativeCanonicalRngV1 *rng,
	const struct NativeCanonicalInputV1 *input)
{
	uint32_t presenceMask;
	if (workspace == NULL) return 0;
	if ((request == NULL) || (gGT == NULL) || (sourceData == NULL) || (control == NULL) || (rng == NULL) || (input == NULL))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_NULL_ARGUMENT);
		return 0;
	}
	if ((workspace->initialized == 0) || (workspace->poisoned != 0) || (workspace->prepared != 0) || (workspace->frameActive != 1))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_FRAME_STATE);
		return 0;
	}
	if ((request->requiredKind != NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3) ||
		(request->restoredThisFrame != 0 && request->restoredThisFrame != 1))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_REQUEST); return 0;
	}
	if ((sourceData->gGT != gGT) || (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_INPUT); return 0;
	}
	if (request->restoredThisFrame != 0)
	{
		MainCanonicalTopology_Invalidate(&workspace->topologyContext);
		memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
		if (workspace->topologyContext.currentEpoch==0||workspace->topologyContext.currentEpoch==UINT64_MAX)
		{
			MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_RESTORE_EPOCH); return 0;
		}
	}
	workspace->stageCounts.rosterPreflight++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_ROSTER_PREFLIGHT) ||
		!MainCanonicalDrivers_ExtractRosterPrelude(gGT,sourceData,&workspace->sourceCandidate.roster))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_ROSTER_PREFLIGHT); return 0;
	}
	presenceMask=workspace->sourceCandidate.roster.prelude.presenceMask;
	if (presenceMask != 0)
	{
		if (workspace->topologyContext.captureActive == 0)
		{
			if (!MainCanonicalTopology_Capture(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData))
			{
				MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_CAPTURE); return 0;
			}
		}
		else if (!MainCanonicalTopology_Validate(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData))
		{
			/* A stale available capture is a lifecycle error, never a hidden
			 * recapture against potentially reused native memory. */
			MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_VALIDATE); return 0;
		}
	}
	else if (workspace->topologyContext.captureActive != 0)
	{
		/* A menu/rootless frame retires the prior level generation. */
		MainCanonicalTopology_Invalidate(&workspace->topologyContext);
		memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
		if (workspace->topologyContext.currentEpoch==0||workspace->topologyContext.currentEpoch==UINT64_MAX)
		{
			MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_EPOCH); return 0;
		}
	}
	workspace->stageCounts.sourceExtract++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_SOURCE_EXTRACT) ||
		!MainCanonicalDrivers_ExtractCompleteFromPreludeInPlace(gGT,sourceData,
		&workspace->topologyContext,&workspace->topologySnapshot,&workspace->sourceCandidate))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_SOURCE_EXTRACT); return 0;
	}
	workspace->stageCounts.assembly++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_ASSEMBLY) ||
		!MainCanonicalDrivers_AssembleDetailedWithScratch(&workspace->sourceCandidate,workspace->normativeScratch,
		sizeof(workspace->normativeScratch),&workspace->drivers))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_ASSEMBLY); return 0;
	}
	workspace->stageCounts.project++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT) ||
		!MainCanonicalState_ProjectV3InPlaceWithScratch(&workspace->stateCandidate.v3,&request->identity,
			request->replayFrame,control,rng,input,&workspace->drivers.summary,
			workspace->normativeScratch,sizeof(workspace->normativeScratch)))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT); return 0;
	}
	workspace->state.v3=workspace->stateCandidate.v3;
	workspace->preparedRequest.v3=*request;
	workspace->preparedKind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3;
	workspace->prepared = 1;
	return 1;
}

int MainCanonicalRuntime_PrepareV4(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct NativeCanonicalControlV1 *control,const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology)
{
	uint32_t presenceMask;
	if (workspace == NULL) return 0;
	if ((request == NULL) || (gGT == NULL) || (sourceData == NULL) || (control == NULL) ||
		(retailRng == NULL) || (input == NULL) || (worldCounters == NULL) ||
		(mineRegistry == NULL) || (topology == NULL))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_NULL_ARGUMENT);
		return 0;
	}
	if ((workspace->initialized == 0) || (workspace->poisoned != 0) || (workspace->prepared != 0) || (workspace->frameActive != 1))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_FRAME_STATE);
		return 0;
	}
	/* The V4 config digest and identity binding are re-derived here rather than
	 * trusted from the request; a mismatch is a request error, not a native one. */
	if ((request->restoredThisFrame != 0 && request->restoredThisFrame != 1) ||
		!MainCanonicalStateV4Context_Init(&workspace->preparedRequest.v4Context,&request->config) ||
		(memcmp(workspace->preparedRequest.v4Context.configDigest,request->configDigest,NATIVE_SHA256_DIGEST_BYTES) != 0) ||
		(memcmp(request->identity.build,request->config.buildIdentity,NATIVE_IDENTITY_DIGEST_BYTES) != 0) ||
		(memcmp(request->identity.content,request->config.contentIdentity,NATIVE_IDENTITY_DIGEST_BYTES) != 0))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_REQUEST); return 0;
	}
	if ((sourceData->gGT != gGT) || (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_INPUT); return 0;
	}
	if (request->restoredThisFrame != 0)
	{
		MainCanonicalTopology_Invalidate(&workspace->topologyContext);
		memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
		if (workspace->topologyContext.currentEpoch==0||workspace->topologyContext.currentEpoch==UINT64_MAX)
		{
			MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_RESTORE_EPOCH); return 0;
		}
	}
	workspace->stageCounts.rosterPreflight++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_ROSTER_PREFLIGHT) ||
		!MainCanonicalDrivers_ExtractRosterPrelude(gGT,sourceData,&workspace->sourceCandidate.roster))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_ROSTER_PREFLIGHT); return 0;
	}
	presenceMask=workspace->sourceCandidate.roster.prelude.presenceMask;
	if (presenceMask != 0)
	{
		if (workspace->topologyContext.captureActive == 0)
		{
			if (!MainCanonicalTopology_Capture(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData))
			{
				MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_CAPTURE); return 0;
			}
		}
		else if (!MainCanonicalTopology_Validate(&workspace->topologyContext,&workspace->topologySnapshot,gGT,sourceData))
		{
			MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_VALIDATE); return 0;
		}
	}
	else if (workspace->topologyContext.captureActive != 0)
	{
		MainCanonicalTopology_Invalidate(&workspace->topologyContext);
		memset(&workspace->topologySnapshot, 0, sizeof(workspace->topologySnapshot));
		if (workspace->topologyContext.currentEpoch==0||workspace->topologyContext.currentEpoch==UINT64_MAX)
		{
			MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_TOPOLOGY_EPOCH); return 0;
		}
	}
	workspace->stageCounts.sourceExtract++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_SOURCE_EXTRACT) ||
		!MainCanonicalDrivers_ExtractCompleteFromPreludeInPlace(gGT,sourceData,
		&workspace->topologyContext,&workspace->topologySnapshot,&workspace->sourceCandidate))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_SOURCE_EXTRACT); return 0;
	}
	workspace->stageCounts.assembly++;
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_ASSEMBLY) ||
		!MainCanonicalDrivers_AssembleDetailedWithScratch(&workspace->sourceCandidate,workspace->normativeScratch,
		sizeof(workspace->normativeScratch),&workspace->drivers))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_ASSEMBLY); return 0;
	}
	workspace->stageCounts.project++;
	/* WORLD and TOPOLOGY are the gated domains supplied by the caller: the
	 * isolated mine/counter extractors, D231, NavHeader.last, and the topology
	 * lease stay untouched here, and the projector revalidates every value.
	 * The bounded in-place projector keeps the whole-state value in the
	 * workspace and uses the reserved staging region for context, RNG, and the
	 * per-domain payload. */
	if (MAIN_CANONICAL_RUNTIME_FORCED(MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT) ||
		!NativeDeterministicRngBankV1_InitInPlace(&workspace->v4Scratch.deterministicRng,request->config.masterSeed,
			request->config.rngDerivationVersion) ||
		!MainCanonicalState_ProjectV4InPlaceWithScratch(&workspace->stateCandidate.v4,&workspace->preparedRequest.v4Context,&request->identity,
			request->replayFrame,control,retailRng,&workspace->v4Scratch.deterministicRng,input,&workspace->drivers.summary,
			worldCounters,mineRegistry,topology,workspace->v4Scratch.projectorScratch,sizeof(workspace->v4Scratch.projectorScratch)))
	{
		MainCanonicalRuntime_Poison(workspace, MAIN_CANONICAL_RUNTIME_FAILURE_PROJECT); return 0;
	}
	workspace->state.v4=workspace->stateCandidate.v4;
	workspace->preparedRequest.v4=*request;
	workspace->preparedKind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V4;
	workspace->prepared = 1;
	return 1;
}

const struct NativeCanonicalStateV3 *MainCanonicalRuntime_ViewV3(
	const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request)
{
	return MainCanonicalRuntime_RequestMatches(workspace,request) ? &workspace->state.v3 : NULL;
}

int MainCanonicalRuntime_GetSubmissionV3(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request,
	struct NativeReplaySchedulerCanonicalSubmission *submission)
{
	struct NativeReplaySchedulerCanonicalSubmission candidate;
	if ((submission == NULL) || !MainCanonicalRuntime_RequestMatches(workspace,request)) return 0;
	candidate.kind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3;
	candidate.state.v3 = &workspace->state.v3;
	*submission = candidate;
	return 1;
}

int MainCanonicalRuntime_ReleaseV3(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct NativeReplaySchedulerCanonicalRequest *request)
{
	if (!MainCanonicalRuntime_RequestMatches(workspace,request)) return 0;
	memset(&workspace->preparedRequest, 0, sizeof(workspace->preparedRequest));
	memset(&workspace->state, 0, sizeof(workspace->state));
	workspace->preparedKind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_NONE;
	workspace->frameActive = 0;
	workspace->prepared = 0;
	return 1;
}

const struct NativeCanonicalStateV4 *MainCanonicalRuntime_ViewV4(
	const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request)
{
	return MainCanonicalRuntime_V4RequestMatches(workspace,request) ? &workspace->state.v4 : NULL;
}

int MainCanonicalRuntime_GetSubmissionV4(const struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request,
	struct NativeReplaySchedulerCanonicalSubmission *submission)
{
	struct NativeReplaySchedulerCanonicalSubmission candidate;
	if ((submission == NULL) || !MainCanonicalRuntime_V4RequestMatches(workspace,request)) return 0;
	candidate.kind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V4;
	candidate.state.v4 = &workspace->state.v4;
	*submission = candidate;
	return 1;
}

int MainCanonicalRuntime_ReleaseV4(struct MainCanonicalRuntimeWorkspace *workspace,
	const struct MainCanonicalRuntimeV4Request *request)
{
	if (!MainCanonicalRuntime_V4RequestMatches(workspace,request)) return 0;
	memset(&workspace->preparedRequest, 0, sizeof(workspace->preparedRequest));
	memset(&workspace->state, 0, sizeof(workspace->state));
	workspace->preparedKind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_NONE;
	workspace->frameActive = 0;
	workspace->prepared = 0;
	return 1;
}
