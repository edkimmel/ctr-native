#include "MAIN/MainCanonicalTopologyLeaseAdapter.h"

#include <string.h>

struct MainCanonicalTopologyLeaseAdapterCapture
{
	const struct MainCanonicalTopologyLeaseAuthority *authority;
	const struct GameTracker *gGT;
	const struct sData *sourceData;
	const struct MainCanonicalTopologyLease *lease;
	const struct NativeTopologyResidencyObservedV1 *observed;
	int32_t levelID;
	MainCanonicalTopologyLeaseAdapterGuardFn guard;
	const void *guardContext;
};

struct MainCanonicalTopologyLeaseAdapterReader
{
	struct MainCanonicalTopologyLeaseAdapterCapture capture;
	const struct QuadBlock *quads;
	const struct CheckpointNode *restarts;
	const struct NavFrame *frames[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
	uint32_t quadCount;
	uint32_t restartCount;
	uint32_t navPointCounts[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
};

static int SameObserved(const struct NativeTopologyResidencyObservedV1 *a,
	const struct NativeTopologyResidencyObservedV1 *b)
{
	return a&&b&&memcmp(a,b,sizeof(*a))==0;
}

static int Overlap(const void *a,size_t aSize,const void *b,size_t bSize)
{
	uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
	return aa<bb?bb-aa<aSize:aa-bb<bSize;
}

/* This is intentionally a candidate guard only.  The guard is an audited
 * caller-owned read-only/quiescent window, while lease plus observed equality
 * reject the lifecycle/root changes this source-only seam can detect. */
static int BeforeNativeRead(const struct MainCanonicalTopologyLeaseAdapterCapture *capture)
{
	struct NativeTopologyResidencyObservedV1 current;
	if(!capture||!capture->authority||!capture->gGT||!capture->sourceData||
		!capture->lease||!capture->observed||!capture->guard||
		!capture->guard(capture->guardContext)||
		!MainCanonicalTopologyLease_Validate(capture->lease,capture->authority,
			capture->gGT,capture->sourceData)||
		!MainCanonicalTopologyLease_ObservePostInit(&current,capture->lease,
			capture->authority,capture->gGT,capture->sourceData)||
		!SameObserved(capture->observed,&current)||
		capture->gGT->levelID!=capture->levelID)return 0;
	return 1;
}

static int ReadQuad(const void *context,uint32_t index,
	struct MainCanonicalTopologyQuadCheckpointFact *out)
{
	const struct MainCanonicalTopologyLeaseAdapterReader *reader=context;
	struct QuadBlock source;
	if(!reader||!out||index>=reader->quadCount||!reader->quads||
		!BeforeNativeRead(&reader->capture))return 0;
	source=reader->quads[index];out->restartIndex=source.checkpointIndex;
	return 1;
}

static int ReadRestart(const void *context,uint32_t index,
	struct MainCanonicalTopologyRestartFact *out)
{
	const struct MainCanonicalTopologyLeaseAdapterReader *reader=context;
	struct CheckpointNode source;
	if(!reader||!out||index>=reader->restartCount||!reader->restarts||
		!BeforeNativeRead(&reader->capture))return 0;
	source=reader->restarts[index];
	out->pos[0]=source.pos.x;out->pos[1]=source.pos.y;out->pos[2]=source.pos.z;
	out->distance=source.distToFinish;
	out->edgeIndex[0]=source.nextIndex_forward;out->edgeIndex[1]=source.nextIndex_left;
	out->edgeIndex[2]=source.nextIndex_backward;out->edgeIndex[3]=source.nextIndex_right;
	return 1;
}

static int ReadNav(const void *context,uint32_t path,uint32_t index,
	struct MainCanonicalTopologyNavFrameFact *out)
{
	const struct MainCanonicalTopologyLeaseAdapterReader *reader=context;
	struct NavFrame source;
	if(!reader||!out||path>=NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT||
		index>=reader->navPointCounts[path]||!reader->frames[path]||
		!BeforeNativeRead(&reader->capture))return 0;
	source=reader->frames[path][index];
	out->pos[0]=source.pos.x;out->pos[1]=source.pos.y;out->pos[2]=source.pos.z;
	memcpy(out->rot,source.rot,sizeof(out->rot));
	out->distanceXYZ=source.distToNextNavXYZ;out->distanceXZ=source.distToNextNavXZ;
	out->flags=source.flags;out->pathChangeOpcode=source.pathChangeOpcode;
	out->goBackCount=source.goBackCount;out->specialBits=source.specialBits;
	return 1;
}

static int AnyNav(const struct NativeTopologyResidencyObservedV1 *observed)
{
	for(uint32_t path=0;path<NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT;path++)
		if(observed->nav[path].headerAddress)return 1;
	return 0;
}

static int BuildReader(struct MainCanonicalTopologyFactReader *reader,
	struct MainCanonicalTopologyLeaseAdapterReader *readerContext,
	const struct MainCanonicalTopologyLeaseAdapterCapture *capture)
{
	const struct Level *level;
	const struct mesh_info *mesh;
	struct NavHeader *const *navTable;
	int quadCount,restartCount;
	if(!reader||!readerContext||!capture||!capture->observed)return 0;
	memset(reader,0,sizeof(*reader));memset(readerContext,0,sizeof(*readerContext));
	readerContext->capture=*capture;
	if(!BeforeNativeRead(capture))return 0;
	level=capture->gGT->level1;
	if((uint32_t)(uintptr_t)level!=capture->observed->levelAddress)return 0;
	if(!BeforeNativeRead(capture))return 0;
	mesh=level->ptr_mesh_info;
	if((uint32_t)(uintptr_t)mesh!=capture->observed->meshAddress)return 0;
	if(!BeforeNativeRead(capture))return 0;
	quadCount=mesh->numQuadBlock;
	if(quadCount<0||(uint32_t)quadCount!=capture->observed->quadCount)return 0;
	readerContext->quadCount=(uint32_t)quadCount;
	if(readerContext->quadCount)
	{
		if(!BeforeNativeRead(capture))return 0;
		readerContext->quads=mesh->ptrQuadBlockArray;
		if((uint32_t)(uintptr_t)readerContext->quads!=capture->observed->quadAddress)return 0;
	}
	if(!BeforeNativeRead(capture))return 0;
	restartCount=level->cnt_restart_points;
	if(restartCount<0||(uint32_t)restartCount!=capture->observed->restartCount)return 0;
	readerContext->restartCount=(uint32_t)restartCount;
	if(readerContext->restartCount)
	{
		if(!BeforeNativeRead(capture))return 0;
		readerContext->restarts=level->ptr_restart_points;
		if((uint32_t)(uintptr_t)readerContext->restarts!=capture->observed->restartAddress)return 0;
	}
	if(AnyNav(capture->observed))
	{
		if(!BeforeNativeRead(capture))return 0;
		navTable=level->LevNavTable;
		if((uint32_t)(uintptr_t)navTable!=capture->observed->navTableAddress)return 0;
		for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
		{
			const struct NavHeader *header;
			struct NavHeader source;
			if(capture->observed->nav[path].headerAddress==0)continue;
			if(!BeforeNativeRead(capture))return 0;
			header=navTable[path];
			if((uint32_t)(uintptr_t)header!=capture->observed->nav[path].headerAddress)return 0;
			if(!BeforeNativeRead(capture))return 0;
			source=*header;
			if(source.magicNumber!=capture->observed->nav[path].magic||source.numPoints<0||
				(uint32_t)source.numPoints!=capture->observed->nav[path].pointCount)return 0;
			reader->navPaths[path].firstNodeY=source.posY_firstNode;
			memcpy(reader->navPaths[path].rampPhys1,source.rampPhys1,sizeof(source.rampPhys1));
			memcpy(reader->navPaths[path].rampPhys2,source.rampPhys2,sizeof(source.rampPhys2));
			reader->navPaths[path].frameCount=(uint32_t)source.numPoints;
			readerContext->navPointCounts[path]=(uint32_t)source.numPoints;
			if(readerContext->navPointCounts[path])
			{
				readerContext->frames[path]=(const struct NavFrame *)((const uint8_t *)header+sizeof(*header));
				if((uint32_t)(uintptr_t)readerContext->frames[path]!=capture->observed->nav[path].frameAddress)return 0;
			}
		}
	}
	reader->flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;reader->levelID=capture->levelID;
	reader->quadCount=readerContext->quadCount;reader->restartCount=readerContext->restartCount;
	reader->context=readerContext;reader->readQuadCheckpoint=ReadQuad;
	reader->readRestart=ReadRestart;reader->readNavFrame=ReadNav;
	return 1;
}

int MainCanonicalTopologyLeaseAdapter_Capture(
	struct NativeCanonicalTopologyV1 *topologyOut,
	struct NativeTopologyResidencySnapshotV1 *residencyOut,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData,
	MainCanonicalTopologyLeaseAdapterGuardFn guard,const void *guardContext)
{
	struct MainCanonicalTopologyLease lease;
	struct NativeTopologyResidencyObservedV1 observed,reobserved;
	struct NativeTopologyResidencySnapshotV1 residencyCandidate;
	struct NativeCanonicalTopologyV1 topologyCandidate;
	struct MainCanonicalTopologyFactReader reader;
	struct MainCanonicalTopologyLeaseAdapterReader readerContext;
	struct MainCanonicalTopologyLeaseAdapterCapture capture;
	if(!topologyOut||!residencyOut||!guard||Overlap(topologyOut,sizeof(*topologyOut),residencyOut,sizeof(*residencyOut)))return 0;
	memset(&lease,0,sizeof(lease));memset(&observed,0,sizeof(observed));
	if(!guard(guardContext)||!MainCanonicalTopologyLease_Acquire(&lease,authority,gGT,sourceData)||
		!guard(guardContext)||!MainCanonicalTopologyLease_Validate(&lease,authority,gGT,sourceData)||
		!MainCanonicalTopologyLease_ObservePostInit(&observed,&lease,authority,gGT,sourceData)||
		!guard(guardContext)||!MainCanonicalTopologyLease_Validate(&lease,authority,gGT,sourceData)||
		!MainCanonicalTopologyLease_ObservePostInit(&reobserved,&lease,authority,gGT,sourceData)||
		!SameObserved(&observed,&reobserved))return 0;
	/* Bootstrap has an already revalidated witness; every subsequent game read
	 * goes through BeforeNativeRead, which also compares this captured ID. */
	memset(&capture,0,sizeof(capture));capture.authority=authority;capture.gGT=gGT;
	capture.sourceData=sourceData;capture.lease=&lease;capture.observed=&observed;
	capture.levelID=gGT->levelID;capture.guard=guard;capture.guardContext=guardContext;
	if(!NativeTopologyResidencyV1_Capture(&residencyCandidate,&lease.residency,&observed)||
		!BuildReader(&reader,&readerContext,&capture)||
		!MainCanonicalTopologyFactReader_ToV1(&reader,&topologyCandidate)||
		!BeforeNativeRead(&capture)||
		!NativeTopologyResidencyV1_Validate(&residencyCandidate,&lease.residency,&observed))return 0;
	*topologyOut=topologyCandidate;*residencyOut=residencyCandidate;
	return 1;
}
