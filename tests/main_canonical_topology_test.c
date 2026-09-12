#include "common.h"
#include "MAIN/MainCanonicalTopology.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sData sdata_static;
#include "../game/MAIN/MainCanonicalTopology.c"

#define CHECK(expression) do { if(!(expression)) { fprintf(stderr,"fail %d\n",__LINE__); return 0; } } while(0)
#define TOPOLOGY_PACK_CAP (128u * 1024u)

union TopologyPackStorage
{
	void *pointerAlignment;
	double doubleAlignment;
	long long integerAlignment;
	unsigned char bytes[TOPOLOGY_PACK_CAP];
};

struct TopologyFixture
{
	struct GameTracker tracker;
	struct sData source;
	union TopologyPackStorage storage[3];
	struct Level *level[3];
	struct mesh_info *mesh[3];
	struct QuadBlock *quadBlocks[3];
};

static uintptr_t AlignAddress(uintptr_t address,size_t alignment)
{
	return (address+(alignment-1u))&~(uintptr_t)(alignment-1u);
}

static int BuildPack(struct TopologyFixture *fixture,uint8_t packIndex,int32_t levelID,int count)
{
	uintptr_t base,current,end,limit;
	if(!fixture||packIndex>=3||count<=0)return 0;
	memset(fixture->storage[packIndex].bytes,0,sizeof(fixture->storage[packIndex].bytes));
	base=AlignAddress((uintptr_t)fixture->storage[packIndex].bytes,_Alignof(struct Level));
	limit=(uintptr_t)fixture->storage[packIndex].bytes+sizeof(fixture->storage[packIndex].bytes);
	current=base;
	if(current>limit||sizeof(struct Level)>limit-current)return 0;
	fixture->level[packIndex]=(struct Level *)current;
	current=AlignAddress(current+sizeof(struct Level),_Alignof(struct mesh_info));
	if(current>limit||sizeof(struct mesh_info)>limit-current)return 0;
	fixture->mesh[packIndex]=(struct mesh_info *)current;
	current=AlignAddress(current+sizeof(struct mesh_info),_Alignof(struct QuadBlock));
	if(current>limit||(size_t)count>SIZE_MAX/sizeof(struct QuadBlock)||
		(size_t)count*sizeof(struct QuadBlock)>limit-current)return 0;
	fixture->quadBlocks[packIndex]=(struct QuadBlock *)current;
	end=current+(size_t)count*sizeof(struct QuadBlock);
	fixture->level[packIndex]->ptr_mesh_info=fixture->mesh[packIndex];
	fixture->mesh[packIndex]->numQuadBlock=count;
	fixture->mesh[packIndex]->ptrQuadBlockArray=fixture->quadBlocks[packIndex];
	fixture->source.mempack[packIndex].start=(void *)base;
	fixture->source.mempack[packIndex].packSize=(s32)(end-base);
	fixture->tracker.levID_in_each_mempack[packIndex]=(s16)levelID;
	return 1;
}

static struct TopologyFixture *FixtureNew(void)
{
	struct TopologyFixture *fixture=(struct TopologyFixture *)calloc(1,sizeof(*fixture));
	if(!fixture)return NULL;
	fixture->source.gGT=&fixture->tracker;
	fixture->tracker.levelID=7;
	if(!BuildPack(fixture,0,7,4))
	{
		free(fixture);
		return NULL;
	}
	fixture->tracker.level1=fixture->level[0];
	return fixture;
}

static int BasicAndIndices(void)
{
	struct TopologyFixture *fixture=FixtureNew();
	struct MainCanonicalTopologyContext context,beforeContext;
	struct MainCanonicalTopologySnapshot snapshot,beforeSnapshot,mutated;
	uint32_t index,before=UINT32_C(0x9abcdef0);
	CHECK(fixture!=NULL);MainCanonicalTopology_Init(&context);
	CHECK(context.currentEpoch==1&&context.captureActive==0);
	CHECK(!MainCanonicalTopology_Capture(NULL,&snapshot,&fixture->tracker,&fixture->source));
	CHECK(!MainCanonicalTopology_Capture(&context,NULL,&fixture->tracker,&fixture->source));
	/* Non-swap always uses pack 0; stale active metadata is non-authoritative. */
	fixture->tracker.activeMempackIndex=3;
	CHECK(MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source));
	CHECK(snapshot.valid==1&&snapshot.capturedEpoch==1&&snapshot.mempackIndex==0&&
		snapshot.level==fixture->level[0]&&snapshot.mesh==fixture->mesh[0]&&snapshot.quadBlocks.count==4&&
		MainCanonicalTopology_Validate(&context,&snapshot,&fixture->tracker,&fixture->source));
	beforeSnapshot=snapshot;beforeContext=context;
	CHECK(!MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source)&&
		memcmp(&snapshot,&beforeSnapshot,sizeof(snapshot))==0&&memcmp(&context,&beforeContext,sizeof(context))==0);
	CHECK(!MainCanonicalTopology_Validate(NULL,&snapshot,&fixture->tracker,&fixture->source));
	CHECK(!MainCanonicalTopology_Validate(&context,NULL,&fixture->tracker,&fixture->source));
	fixture->source.gGT=NULL;
	CHECK(!MainCanonicalTopology_Validate(&context,&snapshot,&fixture->tracker,&fixture->source));
	fixture->source.gGT=&fixture->tracker;
	mutated=snapshot;mutated.valid=2;
	CHECK(!MainCanonicalTopology_Validate(&context,&mutated,&fixture->tracker,&fixture->source));
	mutated=snapshot;mutated.capturedEpoch++;
	CHECK(!MainCanonicalTopology_Validate(&context,&mutated,&fixture->tracker,&fixture->source));
	CHECK(MainCanonicalTopology_NullableQuadBlockIndex(&context,&snapshot,&fixture->tracker,&fixture->source,NULL,&index)&&
		index==NATIVE_CANONICAL_FIXED_ARRAY_NULL_INDEX);
	index=before;CHECK(!MainCanonicalTopology_NullableQuadBlockIndex(NULL,&snapshot,&fixture->tracker,&fixture->source,NULL,&index)&&index==before);
	for(uint32_t element=0;element<4;element++)
	{
		uintptr_t address=snapshot.quadBlocks.base+(size_t)element*sizeof(struct QuadBlock);
		CHECK(MainCanonicalTopology_NullableQuadBlockIndex(&context,&snapshot,&fixture->tracker,&fixture->source,
			(const struct QuadBlock *)address,&index)&&index==element);
		for(size_t offset=1;offset<sizeof(struct QuadBlock);offset++)
		{
			index=before;
			CHECK(!MainCanonicalTopology_NullableQuadBlockIndex(&context,&snapshot,&fixture->tracker,&fixture->source,
				(const struct QuadBlock *)(address+offset),&index)&&index==before);
		}
	}
	index=before;CHECK(!MainCanonicalTopology_NullableQuadBlockIndex(&context,&snapshot,&fixture->tracker,&fixture->source,
		(const struct QuadBlock *)(snapshot.quadBlocks.base-1u),&index)&&index==before);
	index=before;CHECK(!MainCanonicalTopology_NullableQuadBlockIndex(&context,&snapshot,&fixture->tracker,&fixture->source,
		(const struct QuadBlock *)(snapshot.quadBlocks.base+snapshot.quadBlocks.span),&index)&&index==before);
	index=before;CHECK(!MainCanonicalTopology_NullableQuadBlockIndex(&context,&snapshot,&fixture->tracker,&fixture->source,
		(const struct QuadBlock *)(uintptr_t)1,&index)&&index==before);
	free(fixture);
	return 1;
}

static int CaptureFailuresAndAlignment(void)
{
	struct TopologyFixture *fixture=FixtureNew();
	struct MainCanonicalTopologyContext context,beforeContext;
	struct MainCanonicalTopologySnapshot snapshot,beforeSnapshot;
	s32 originalPackSize;
	CHECK(fixture!=NULL);MainCanonicalTopology_Init(&context);
	/* Retire once, then each malformed capture must leave both outputs alone. */
	CHECK(MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source));
	MainCanonicalTopology_Invalidate(&context);beforeSnapshot=snapshot;beforeContext=context;
	#define FAIL_CAPTURE(change) do { change; CHECK(!MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source)); CHECK(memcmp(&snapshot,&beforeSnapshot,sizeof(snapshot))==0&&memcmp(&context,&beforeContext,sizeof(context))==0); } while(0)
	FAIL_CAPTURE(fixture->source.gGT=NULL);fixture->source.gGT=&fixture->tracker;
	FAIL_CAPTURE(fixture->tracker.level1=(struct Level *)(uintptr_t)1);fixture->tracker.level1=fixture->level[0];
	FAIL_CAPTURE(fixture->tracker.level1=(struct Level *)((uintptr_t)fixture->level[0]+1u));fixture->tracker.level1=fixture->level[0];
	FAIL_CAPTURE(fixture->level[0]->ptr_mesh_info=(struct mesh_info *)(uintptr_t)1);fixture->level[0]->ptr_mesh_info=fixture->mesh[0];
	FAIL_CAPTURE(fixture->level[0]->ptr_mesh_info=(struct mesh_info *)((uintptr_t)fixture->mesh[0]+1u));fixture->level[0]->ptr_mesh_info=fixture->mesh[0];
	FAIL_CAPTURE(fixture->mesh[0]->numQuadBlock=0);fixture->mesh[0]->numQuadBlock=4;
	FAIL_CAPTURE(fixture->mesh[0]->numQuadBlock=-1);fixture->mesh[0]->numQuadBlock=4;
	FAIL_CAPTURE(fixture->mesh[0]->numQuadBlock=INT_MAX);fixture->mesh[0]->numQuadBlock=4;
	FAIL_CAPTURE(fixture->mesh[0]->ptrQuadBlockArray=(struct QuadBlock *)(uintptr_t)1);fixture->mesh[0]->ptrQuadBlockArray=fixture->quadBlocks[0];
	FAIL_CAPTURE(fixture->mesh[0]->ptrQuadBlockArray=(struct QuadBlock *)((uintptr_t)fixture->quadBlocks[0]+1u));fixture->mesh[0]->ptrQuadBlockArray=fixture->quadBlocks[0];
	FAIL_CAPTURE(fixture->mesh[0]->ptrQuadBlockArray=fixture->quadBlocks[0]+4);fixture->mesh[0]->ptrQuadBlockArray=fixture->quadBlocks[0];
	originalPackSize=fixture->source.mempack[0].packSize;
	FAIL_CAPTURE(fixture->source.mempack[0].packSize=0);fixture->source.mempack[0].packSize=originalPackSize;
	FAIL_CAPTURE(fixture->source.mempack[0].packSize=-1);fixture->source.mempack[0].packSize=originalPackSize;
	/* One byte short proves the complete QuadBlock array, not just its base, fits. */
	FAIL_CAPTURE(fixture->source.mempack[0].packSize=originalPackSize-1);fixture->source.mempack[0].packSize=originalPackSize;
	FAIL_CAPTURE(fixture->source.mempack[0].start=(void *)(UINTPTR_MAX-4u);fixture->source.mempack[0].packSize=8);fixture->source.mempack[0].start=fixture->storage[0].bytes;fixture->source.mempack[0].packSize=originalPackSize;
	#undef FAIL_CAPTURE
	free(fixture);
	return 1;
}

static int ActivePackEpochAndReuse(void)
{
	struct TopologyFixture *fixture=FixtureNew();
	struct MainCanonicalTopologyContext context,maxContext;
	struct MainCanonicalTopologySnapshot snapshot,stale,beforeSnapshot;
	uint64_t epoch;
	CHECK(fixture!=NULL&&BuildPack(fixture,1,12,3)&&BuildPack(fixture,2,13,2));
	fixture->tracker.gameMode2|=LEV_SWAP;fixture->tracker.activeMempackIndex=1;
	fixture->tracker.levelID=12;fixture->tracker.level1=fixture->level[1];fixture->tracker.level2=fixture->level[2];
	/* Neither the inactive pack nor level2 can become authority. */
	fixture->source.PtrMempack=(struct Mempack *)(uintptr_t)1;
	fixture->source.mempack[2].start=(void *)(uintptr_t)1;
	MainCanonicalTopology_Init(&context);
	CHECK(MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source)&&snapshot.mempackIndex==1);
	fixture->tracker.level2=(struct Level *)(uintptr_t)1;
	CHECK(MainCanonicalTopology_Validate(&context,&snapshot,&fixture->tracker,&fixture->source));
	beforeSnapshot=snapshot;CHECK(!MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source)&&memcmp(&snapshot,&beforeSnapshot,sizeof(snapshot))==0);
	stale=snapshot;epoch=context.currentEpoch;MainCanonicalTopology_Invalidate(&context);
	CHECK(context.currentEpoch==epoch+1&&context.captureActive==0&&
		!MainCanonicalTopology_Validate(&context,&stale,&fixture->tracker,&fixture->source));
	/* Rebuild the identical storage and capture a new generation: stale copies
	 * still fail even though all addresses have been reused. */
	fixture->tracker.level2=fixture->level[2];fixture->source.mempack[2].start=fixture->storage[2].bytes;
	CHECK(BuildPack(fixture,1,12,3));fixture->tracker.level1=fixture->level[1];
	CHECK(MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source)&&
		snapshot.capturedEpoch==context.currentEpoch&&!MainCanonicalTopology_Validate(&context,&stale,&fixture->tracker,&fixture->source));
	MainCanonicalTopology_Invalidate(&context);
	fixture->tracker.activeMempackIndex=2;fixture->tracker.levelID=13;fixture->tracker.level1=fixture->level[2];
	CHECK(MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source)&&snapshot.mempackIndex==2);
	MainCanonicalTopology_Invalidate(&context);fixture->tracker.activeMempackIndex=0;
	CHECK(!MainCanonicalTopology_Capture(&context,&snapshot,&fixture->tracker,&fixture->source));
	MainCanonicalTopology_Init(&maxContext);maxContext.currentEpoch=0;
	CHECK(!MainCanonicalTopology_Capture(&maxContext,&snapshot,&fixture->tracker,&fixture->source));
	maxContext.currentEpoch=UINT64_MAX;MainCanonicalTopology_Invalidate(&maxContext);
	CHECK(maxContext.currentEpoch==UINT64_MAX&&!MainCanonicalTopology_Capture(&maxContext,&snapshot,&fixture->tracker,&fixture->source));
	free(fixture);
	return 1;
}

int main(void)
{
	CHECK(BasicAndIndices());
	CHECK(CaptureFailuresAndAlignment());
	CHECK(ActivePackEpochAndReuse());
	puts("main_canonical_topology_test: passed");
	return 0;
}
