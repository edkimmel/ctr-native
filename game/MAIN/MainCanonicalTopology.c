#include "MainCanonicalTopology.h"

#include <limits.h>
#include <string.h>

CTR_STATIC_ASSERT(sizeof(struct QuadBlock) == 0x5c);

static int MainCanonicalTopology_SpanContains(uintptr_t base,size_t span,
	uintptr_t address,size_t size)
{
	uintptr_t delta;
	if(base==0||span==0||address==0||size==0||span>UINTPTR_MAX-base||address<base)return 0;
	delta=address-base;
	return delta<span&&size<=span-delta;
}

static int MainCanonicalTopology_ActivePack(const struct GameTracker *gGT,
	const struct sData *sourceData,uint8_t *indexOut,const struct Mempack **packOut,
	uintptr_t *baseOut,size_t *spanOut)
{
	const struct Mempack *pack;
	uintptr_t base;
	size_t span;
	int packIndex;
	if(!gGT||!sourceData||!indexOut||!packOut||!baseOut||!spanOut||sourceData->gGT!=gGT)return 0;
	if((gGT->gameMode2&LEV_SWAP)!=0)
	{
		packIndex=gGT->activeMempackIndex;
		if((packIndex!=1&&packIndex!=2)||gGT->levID_in_each_mempack[packIndex]!=gGT->levelID)return 0;
	}
	else packIndex=0;
	pack=&sourceData->mempack[packIndex];
	if(pack->packSize<=0)return 0;
	base=(uintptr_t)pack->start;span=(size_t)pack->packSize;
	if(base==0||span>UINTPTR_MAX-base)return 0;
	*indexOut=(uint8_t)packIndex;*packOut=pack;*baseOut=base;*spanOut=span;
	return 1;
}

static int MainCanonicalTopology_Current(struct MainCanonicalTopologySnapshot *candidate,
	const struct GameTracker *gGT,const struct sData *sourceData,uint64_t lifecycleEpoch)
{
	struct NativeCanonicalFixedArrayInput arrayInput;
	const struct Mempack *pack;
	const struct Level *level;
	const struct mesh_info *mesh;
	uintptr_t base;
	size_t span;
	uint8_t packIndex;
	if(!candidate||lifecycleEpoch==0||
		!MainCanonicalTopology_ActivePack(gGT,sourceData,&packIndex,&pack,&base,&span))return 0;
	level=gGT->level1;
	if(!MainCanonicalTopology_SpanContains(base,span,(uintptr_t)level,sizeof(*level)))return 0;
	mesh=level->ptr_mesh_info;
	if(!MainCanonicalTopology_SpanContains(base,span,(uintptr_t)mesh,sizeof(*mesh))||mesh->numQuadBlock<=0)return 0;
	arrayInput.base=(uintptr_t)mesh->ptrQuadBlockArray;
	arrayInput.count=(uint64_t)(uint32_t)mesh->numQuadBlock;
	arrayInput.elementSize=sizeof(struct QuadBlock);
	if(!NativeCanonicalFixedArray_GeometrySnapshot(&arrayInput,&candidate->quadBlocks)||
		!MainCanonicalTopology_SpanContains(base,span,arrayInput.base,candidate->quadBlocks.span))return 0;
	candidate->gGT=gGT;candidate->sourceData=sourceData;candidate->level=level;candidate->mesh=mesh;
	candidate->mempack=pack;candidate->mempackBase=base;candidate->mempackSpan=span;
	candidate->levelID=gGT->levelID;candidate->mempackIndex=packIndex;
	candidate->lifecycleEpoch=lifecycleEpoch;candidate->valid=1;
	return 1;
}

void MainCanonicalTopology_Init(struct MainCanonicalTopologySnapshot *snapshot)
{
	if(!snapshot)return;
	memset(snapshot,0,sizeof(*snapshot));
	snapshot->lifecycleEpoch=1;
}

void MainCanonicalTopology_Invalidate(struct MainCanonicalTopologySnapshot *snapshot)
{
	uint64_t nextEpoch;
	if(!snapshot)return;
	nextEpoch=snapshot->lifecycleEpoch;
	if(nextEpoch==0)nextEpoch=1;
	else if(nextEpoch!=UINT64_MAX)nextEpoch++;
	memset(snapshot,0,sizeof(*snapshot));
	snapshot->lifecycleEpoch=nextEpoch;
}

int MainCanonicalTopology_Capture(struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct MainCanonicalTopologySnapshot candidate;
	if(!snapshot||snapshot->lifecycleEpoch==0||snapshot->lifecycleEpoch==UINT64_MAX)return 0;
	memset(&candidate,0,sizeof(candidate));
	if(!MainCanonicalTopology_Current(&candidate,gGT,sourceData,snapshot->lifecycleEpoch))return 0;
	*snapshot=candidate;
	return 1;
}

int MainCanonicalTopology_Validate(const struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct MainCanonicalTopologySnapshot current;
	if(!snapshot||!snapshot->valid||snapshot->lifecycleEpoch==0)return 0;
	memset(&current,0,sizeof(current));
	if(!MainCanonicalTopology_Current(&current,gGT,sourceData,snapshot->lifecycleEpoch))return 0;
	return current.gGT==snapshot->gGT&&current.sourceData==snapshot->sourceData&&
		current.level==snapshot->level&&current.mesh==snapshot->mesh&&
		current.mempack==snapshot->mempack&&current.mempackBase==snapshot->mempackBase&&
		current.mempackSpan==snapshot->mempackSpan&&current.levelID==snapshot->levelID&&
		current.mempackIndex==snapshot->mempackIndex&&
		current.quadBlocks.base==snapshot->quadBlocks.base&&
		current.quadBlocks.elementSize==snapshot->quadBlocks.elementSize&&
		current.quadBlocks.span==snapshot->quadBlocks.span&&current.quadBlocks.count==snapshot->quadBlocks.count;
}

int MainCanonicalTopology_NullableQuadBlockIndex(const struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct QuadBlock *quadBlock,uint32_t *indexOut)
{
	if(!indexOut||!MainCanonicalTopology_Validate(snapshot,gGT,sourceData))return 0;
	return NativeCanonicalFixedArray_NullableIndex(&snapshot->quadBlocks,(uintptr_t)quadBlock,indexOut);
}
