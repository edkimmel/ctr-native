#include "MAIN/MainCanonicalTopologyLeaseAuthority.h"

#include <limits.h>
#include <string.h>

static int ValidInitReason(enum MainCanonicalTopologyLeaseInitReason reason)
{
	return reason==MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_COLD_BOOT||reason==MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST;
}

static int ValidRetireReason(enum MainCanonicalTopologyLeaseRetireReason reason)
{
	return reason==MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD||
		reason==MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_HUB_SWAP||
		reason==MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_CHECKPOINT_RESTORE||
		reason==MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET;
}

static int ValidAuthority(const struct MainCanonicalTopologyLeaseAuthority *authority)
{
	if(!authority||authority->tag!=MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_TAG||
		authority->initialized!=1||!ValidInitReason((enum MainCanonicalTopologyLeaseInitReason)authority->initReason)||
		authority->epoch==0||authority->epoch==UINT64_MAX)return 0;
	if(authority->retired==0)return authority->retireReason==0;
	return authority->retired==1&&ValidRetireReason((enum MainCanonicalTopologyLeaseRetireReason)authority->retireReason);
}

static int ToAddress32(const void *pointer,uint32_t *out)
{
	uintptr_t value;
	if(!pointer||!out)return 0;
	value=(uintptr_t)pointer;
	if(value>UINT32_MAX)return 0;
	*out=(uint32_t)value;
	return 1;
}

static int SpanContains(uint32_t base,uint32_t span,uint32_t address,uint32_t bytes)
{
	uint64_t end=(uint64_t)base+(uint64_t)span;
	uint64_t addressEnd=(uint64_t)address+(uint64_t)bytes;
	return base!=0&&span!=0&&address!=0&&bytes!=0&&end<=UINT64_C(0x100000000)&&address>=base&&addressEnd<=end;
}

static int Aligned(uint32_t address,uint32_t alignment)
{
	return address!=0&&alignment!=0&&(address%alignment)==0;
}

static int CurrentLease(struct MainCanonicalTopologyLease *candidate,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	const struct Mempack *pack;
	uint32_t start,firstFree,lastFree,end;
	int packIndex;
	if(!candidate||!ValidAuthority(authority)||authority->retired!=0||!gGT||!sourceData||
		sourceData->gGT!=gGT||sourceData->Loading.stage!=LOAD_IDLE||sourceData->load_inProgress!=0)return 0;
	if((gGT->gameMode2&LEV_SWAP)!=0)
	{
		packIndex=gGT->activeMempackIndex;
		if((packIndex!=1&&packIndex!=2)||gGT->levID_in_each_mempack[packIndex]!=gGT->levelID)return 0;
	}
	else packIndex=0;
	pack=&sourceData->mempack[packIndex];
	if(pack->packSize<=0||!ToAddress32(pack->start,&start)||!ToAddress32(pack->firstFreeByte,&firstFree)||
		!ToAddress32(pack->lastFreeByte,&lastFree)||!ToAddress32(pack->endOfAllocator,&end)||
		start>firstFree||firstFree>lastFree||lastFree>end||
		(uint64_t)end-(uint64_t)start!=(uint32_t)pack->packSize||firstFree==start)return 0;
	memset(candidate,0,sizeof(*candidate));
	candidate->residency.base=start;
	candidate->residency.span=firstFree-start;
	candidate->residency.epoch=authority->epoch;
	candidate->gGT=gGT;candidate->sourceData=sourceData;candidate->mempack=pack;
	candidate->mempackIndex=(uint8_t)packIndex;candidate->valid=1;
	return 1;
}

void MainCanonicalTopologyLeaseAuthority_Init(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseInitReason reason)
{
	if(!authority||!ValidInitReason(reason))return;
	if(authority->tag!=MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_TAG||authority->initialized!=1)
	{
		memset(authority,0,sizeof(*authority));
		authority->tag=MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_TAG;
		authority->epoch=1;authority->initialized=1;authority->initReason=(uint8_t)reason;
		return;
	}
	/* Initialization is construction, not a lifecycle transition. */
}

void MainCanonicalTopologyLeaseAuthority_Retire(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseRetireReason reason)
{
	if(!ValidRetireReason(reason)||!ValidAuthority(authority)||authority->retired!=0)return;
	authority->epoch++;
	authority->retired=1;authority->retireReason=(uint8_t)reason;
}

void MainCanonicalTopologyLeaseAuthority_ActivatePostInit(struct MainCanonicalTopologyLeaseAuthority *authority,
	enum MainCanonicalTopologyLeaseRetireReason reason)
{
	if(!ValidRetireReason(reason)||!ValidAuthority(authority)||authority->retired!=1||
		authority->retireReason!=(uint8_t)reason)return;
	authority->retired=0;
	authority->retireReason=0;
}

int MainCanonicalTopologyLease_Acquire(struct MainCanonicalTopologyLease *leaseOut,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct MainCanonicalTopologyLease candidate;
	if(!leaseOut)return 0;
	memset(&candidate,0,sizeof(candidate));
	if(!CurrentLease(&candidate,authority,gGT,sourceData))return 0;
	*leaseOut=candidate;
	return 1;
}

int MainCanonicalTopologyLease_Validate(const struct MainCanonicalTopologyLease *lease,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct MainCanonicalTopologyLease current;
	if(!lease||lease->valid!=1)return 0;
	memset(&current,0,sizeof(current));
	if(!CurrentLease(&current,authority,gGT,sourceData))return 0;
	return current.residency.base==lease->residency.base&&current.residency.span==lease->residency.span&&
		current.residency.epoch==lease->residency.epoch&&current.gGT==lease->gGT&&
		current.sourceData==lease->sourceData&&current.mempack==lease->mempack&&
		current.mempackIndex==lease->mempackIndex;
}

static int AddressOf(const void *pointer,uint32_t alignment,uint32_t base,uint32_t span,uint32_t bytes,uint32_t *out)
{
	uint32_t address;
	if(!ToAddress32(pointer,&address)||!Aligned(address,alignment)||!SpanContains(base,span,address,bytes))return 0;
	*out=address;
	return 1;
}

int MainCanonicalTopologyLease_ObservePostInit(struct NativeTopologyResidencyObservedV1 *observedOut,
	const struct MainCanonicalTopologyLease *lease,
	const struct MainCanonicalTopologyLeaseAuthority *authority,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct NativeTopologyResidencyObservedV1 candidate;
	struct NativeTopologyResidencySnapshotV1 checked;
	const struct Level *level;
	const struct mesh_info *mesh;
	const struct NavHeader *header;
	struct NavHeader *const *navTable;
	uint32_t base,span,levelAddress,meshAddress,navTableAddress;
	uint32_t valueAddress,frameAddress,frameSpan;
	int count;
	if(!observedOut||!MainCanonicalTopologyLease_Validate(lease,authority,gGT,sourceData))return 0;
	base=lease->residency.base;span=lease->residency.span;
	memset(&candidate,0,sizeof(candidate));
	level=gGT->level1;
	if(!AddressOf(level,4u,base,span,NATIVE_TOPOLOGY_RESIDENCY_LEVEL_PREFIX_BYTES,&levelAddress))return 0;
	/* level has now been proved resident through every field read below. */
	mesh=level->ptr_mesh_info;
	if(!AddressOf(mesh,4u,base,span,NATIVE_TOPOLOGY_RESIDENCY_MESH_BYTES,&meshAddress)||mesh->numQuadBlock<0||
		(uint32_t)mesh->numQuadBlock>NATIVE_TOPOLOGY_RESIDENCY_MAX_QUAD_COUNT)return 0;
	candidate.levelAddress=levelAddress;candidate.meshAddress=meshAddress;candidate.quadCount=(uint32_t)mesh->numQuadBlock;
	if(candidate.quadCount!=0&&!AddressOf(mesh->ptrQuadBlockArray,4u,base,span,
		candidate.quadCount*NATIVE_TOPOLOGY_RESIDENCY_QUAD_BYTES,&candidate.quadAddress))return 0;
	count=level->cnt_restart_points;
	if(count<0||(uint32_t)count>NATIVE_TOPOLOGY_RESIDENCY_MAX_RESTART_COUNT)return 0;
	candidate.restartCount=(uint32_t)count;
	if(candidate.restartCount!=0&&!AddressOf(level->ptr_restart_points,2u,base,span,
		candidate.restartCount*NATIVE_TOPOLOGY_RESIDENCY_RESTART_BYTES,&candidate.restartAddress))return 0;
	navTable=level->LevNavTable;
	if(!AddressOf(navTable,4u,base,span,NATIVE_TOPOLOGY_RESIDENCY_NAV_TABLE_BYTES,&navTableAddress))return 0;
	for(uint32_t path=0;path<NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT;path++)
	{
		header=navTable[path];
		if(!header)continue;
		if(!AddressOf(header,4u,base,span,NATIVE_TOPOLOGY_RESIDENCY_NAV_HEADER_BYTES,&valueAddress))return 0;
		if(header->magicNumber!=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC||header->numPoints<0||
			(uint32_t)header->numPoints>NATIVE_TOPOLOGY_RESIDENCY_MAX_NAV_POINT_COUNT)return 0;
		if(valueAddress>UINT32_MAX-NATIVE_TOPOLOGY_RESIDENCY_NAV_HEADER_BYTES)return 0;
		candidate.nav[path].headerAddress=valueAddress;
		candidate.nav[path].magic=header->magicNumber;
		candidate.nav[path].pointCount=(uint16_t)header->numPoints;
		if(header->numPoints==0)
		{
			if(header->last!=(struct NavFrame *)((uintptr_t)header+sizeof(*header)))return 0;
			continue;
		}
		frameAddress=valueAddress+NATIVE_TOPOLOGY_RESIDENCY_NAV_HEADER_BYTES;
		frameSpan=(uint32_t)header->numPoints*NATIVE_TOPOLOGY_RESIDENCY_NAV_FRAME_BYTES;
		if(!Aligned(frameAddress,2u)||!SpanContains(base,span,frameAddress,frameSpan)||
			header->last!=(struct NavFrame *)((uintptr_t)frameAddress+frameSpan))return 0;
		candidate.nav[path].frameAddress=frameAddress;
	}
	for(uint32_t path=0;path<NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT;path++)
		if(candidate.nav[path].headerAddress!=0){candidate.navTableAddress=navTableAddress;break;}
	if(!NativeTopologyResidencyV1_Capture(&checked,&lease->residency,&candidate))return 0;
	*observedOut=candidate;
	return 1;
}
