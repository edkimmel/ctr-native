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

static int MainCanonicalTopology_Aligned(uintptr_t address,size_t alignment)
{
	return address!=0&&alignment!=0&&(address%alignment)==0;
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
	if(!MainCanonicalTopology_Aligned((uintptr_t)level,_Alignof(struct Level))||
		!MainCanonicalTopology_SpanContains(base,span,(uintptr_t)level,sizeof(*level)))return 0;
	mesh=level->ptr_mesh_info;
	if(!MainCanonicalTopology_Aligned((uintptr_t)mesh,_Alignof(struct mesh_info))||
		!MainCanonicalTopology_SpanContains(base,span,(uintptr_t)mesh,sizeof(*mesh))||mesh->numQuadBlock<=0)return 0;
	arrayInput.base=(uintptr_t)mesh->ptrQuadBlockArray;
	arrayInput.count=(uint64_t)(uint32_t)mesh->numQuadBlock;
	arrayInput.elementSize=sizeof(struct QuadBlock);
	if(!MainCanonicalTopology_Aligned(arrayInput.base,_Alignof(struct QuadBlock))||
		!NativeCanonicalFixedArray_GeometrySnapshot(&arrayInput,&candidate->quadBlocks)||
		!MainCanonicalTopology_SpanContains(base,span,arrayInput.base,candidate->quadBlocks.span))return 0;
	candidate->gGT=gGT;candidate->sourceData=sourceData;candidate->level=level;candidate->mesh=mesh;
	candidate->mempack=pack;candidate->mempackBase=base;candidate->mempackSpan=span;
	candidate->levelID=gGT->levelID;candidate->mempackIndex=packIndex;
	candidate->capturedEpoch=lifecycleEpoch;candidate->valid=1;
	return 1;
}

void MainCanonicalTopology_Init(struct MainCanonicalTopologyContext *context)
{
	if(!context)return;
	memset(context,0,sizeof(*context));
	context->currentEpoch=1;
}

void MainCanonicalTopology_Invalidate(struct MainCanonicalTopologyContext *context)
{
	uint64_t nextEpoch;
	if(!context)return;
	nextEpoch=context->currentEpoch;
	if(nextEpoch!=0&&nextEpoch!=UINT64_MAX)nextEpoch++;
	context->currentEpoch=nextEpoch;
	context->captureActive=0;
}

int MainCanonicalTopology_Capture(struct MainCanonicalTopologyContext *context,
	struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct MainCanonicalTopologySnapshot candidate;
	if(!context||!snapshot||context->currentEpoch==0||context->currentEpoch==UINT64_MAX||context->captureActive!=0)return 0;
	memset(&candidate,0,sizeof(candidate));
	if(!MainCanonicalTopology_Current(&candidate,gGT,sourceData,context->currentEpoch))return 0;
	*snapshot=candidate;
	context->captureActive=1;
	return 1;
}

int MainCanonicalTopology_Validate(const struct MainCanonicalTopologyContext *context,
	const struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData)
{
	struct MainCanonicalTopologySnapshot current;
	if(!context||!snapshot||context->captureActive!=1||context->currentEpoch==0||
		context->currentEpoch==UINT64_MAX||snapshot->valid!=1||
		snapshot->capturedEpoch!=context->currentEpoch)return 0;
	memset(&current,0,sizeof(current));
	if(!MainCanonicalTopology_Current(&current,gGT,sourceData,context->currentEpoch))return 0;
	return current.gGT==snapshot->gGT&&current.sourceData==snapshot->sourceData&&
		current.level==snapshot->level&&current.mesh==snapshot->mesh&&
		current.mempack==snapshot->mempack&&current.mempackBase==snapshot->mempackBase&&
		current.mempackSpan==snapshot->mempackSpan&&current.levelID==snapshot->levelID&&
		current.mempackIndex==snapshot->mempackIndex&&
		current.quadBlocks.base==snapshot->quadBlocks.base&&
		current.quadBlocks.elementSize==snapshot->quadBlocks.elementSize&&
		current.quadBlocks.span==snapshot->quadBlocks.span&&current.quadBlocks.count==snapshot->quadBlocks.count;
}

int MainCanonicalTopology_NullableQuadBlockIndex(const struct MainCanonicalTopologyContext *context,
	const struct MainCanonicalTopologySnapshot *snapshot,
	const struct GameTracker *gGT,const struct sData *sourceData,
	const struct QuadBlock *quadBlock,uint32_t *indexOut)
{
	if(!indexOut||!MainCanonicalTopology_Validate(context,snapshot,gGT,sourceData))return 0;
	return NativeCanonicalFixedArray_NullableIndex(&snapshot->quadBlocks,(uintptr_t)quadBlock,indexOut);
}
