#include "platform/native_canonical_pool.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

struct TestSlot
{
	struct NativeCanonicalPoolItem item;
	uint8_t payload[16];
};

struct TestPool
{
	struct TestSlot slots[4];
};

static struct NativeCanonicalPoolInput Input(const struct TestPool *pool)
{
	struct NativeCanonicalPoolInput input;
	input.base=pool->slots;
	input.maxItems=4;
	input.itemSize=(uint32_t)sizeof(pool->slots[0]);
	input.poolSize=(int32_t)sizeof(pool->slots);
	return input;
}

static void ClearLinks(struct TestPool *pool)
{
	for(uint32_t i=0;i<4;i++) { pool->slots[i].item.next=NULL; pool->slots[i].item.prev=NULL; }
}

static struct NativeCanonicalPoolList List(struct TestPool *pool,const uint8_t *indices,uint32_t count)
{
	struct NativeCanonicalPoolList list;
	ClearLinks(pool);
	list.count=(int32_t)count;
	list.first=count==0?NULL:&pool->slots[indices[0]].item;
	list.last=count==0?NULL:&pool->slots[indices[count-1]].item;
	for(uint32_t i=0;i<count;i++)
	{
		struct NativeCanonicalPoolItem *item=&pool->slots[indices[i]].item;
		item->prev=i==0?NULL:&pool->slots[indices[i-1]].item;
		item->next=i+1==count?NULL:&pool->slots[indices[i+1]].item;
	}
	return list;
}

static int TestGeometryAndSlots(void)
{
	struct TestPool pool={{0}};
	struct NativeCanonicalPoolInput input=Input(&pool);
	struct NativeCanonicalPoolGeometry geometry,before;
	uint32_t index,beforeIndex;
	CHECK(NativeCanonicalPool_GeometrySnapshot(&input,&geometry));
	CHECK(geometry.base==(uintptr_t)pool.slots&&geometry.maxItems==4&&geometry.stride==sizeof(struct TestSlot)&&geometry.span==sizeof(pool.slots)&&geometry.allocationSpan==sizeof(pool.slots));
	for(uint32_t i=0;i<4;i++){index=UINT32_MAX;CHECK(NativeCanonicalPool_SlotIndex(&geometry,&pool.slots[i],&index)&&index==i);}
	before=geometry;input.base=NULL;CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);input=Input(&pool);
	input.base=(const void *)((uintptr_t)pool.slots+1);CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));input=Input(&pool);
	input.maxItems=0;CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));input=Input(&pool);
	input.itemSize=(uint32_t)(sizeof(struct NativeCanonicalPoolItem)-1);input.poolSize=4*(int32_t)input.itemSize;CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));input=Input(&pool);
	input.poolSize--;CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));input=Input(&pool);
	input.poolSize=-1;CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));input=Input(&pool);
	input.maxItems=INT32_MAX;input.itemSize=(uint32_t)sizeof(struct NativeCanonicalPoolItem);input.poolSize=INT32_MAX;CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));input=Input(&pool);
	{ union { uint32_t align; uint8_t bytes[100]; } raw={{0}}; struct NativeCanonicalPoolInput nonaligned={raw.bytes,4,25,100};
		CHECK(NativeCanonicalPool_GeometrySnapshot(&nonaligned,&geometry)&&geometry.stride==24&&geometry.span==96&&geometry.allocationSpan==100);
		nonaligned.poolSize=96;CHECK(!NativeCanonicalPool_GeometrySnapshot(&nonaligned,&geometry)); }
	input=Input(&pool);CHECK(NativeCanonicalPool_GeometrySnapshot(&input,&geometry));
	input.base=(const void *)(UINTPTR_MAX-(uintptr_t)geometry.allocationSpan+1);CHECK(!NativeCanonicalPool_GeometrySnapshot(&input,&geometry));
	beforeIndex=UINT32_C(0xa5a5a5a5);
	CHECK(!NativeCanonicalPool_SlotIndex(&geometry,(const void *)((uintptr_t)pool.slots+1),&beforeIndex)&&beforeIndex==UINT32_C(0xa5a5a5a5));
	CHECK(!NativeCanonicalPool_SlotIndex(&geometry,(const void *)(geometry.base-1),&beforeIndex)&&beforeIndex==UINT32_C(0xa5a5a5a5));
	CHECK(!NativeCanonicalPool_SlotIndex(&geometry,(const void *)(geometry.base+geometry.span),&beforeIndex)&&beforeIndex==UINT32_C(0xa5a5a5a5));
	geometry.span=SIZE_MAX;CHECK(!NativeCanonicalPool_SlotIndex(&geometry,pool.slots,&beforeIndex)&&beforeIndex==UINT32_C(0xa5a5a5a5));
	return 0;
}

static int TestListsAndOwnership(void)
{
	struct TestPool pool={{0}};
	struct NativeCanonicalPoolInput input=Input(&pool);
	struct NativeCanonicalPoolGeometry geometry;
	const uint8_t freeIndices[]={1,2},takenIndices[]={0},twoTakenIndices[]={0,1};
	struct NativeCanonicalPoolList freeList,takenList,empty;
	uint32_t index,before=UINT32_C(0xa5a5a5a5);
	CHECK(NativeCanonicalPool_GeometrySnapshot(&input,&geometry));
	freeList=List(&pool,freeIndices,2);CHECK(NativeCanonicalPool_ValidateList(&geometry,&freeList));
	CHECK(NativeCanonicalPool_AllocatedFromFree(&geometry,&freeList,&pool.slots[0],&index)&&index==0);
	index=before;CHECK(!NativeCanonicalPool_AllocatedFromFree(&geometry,&freeList,&pool.slots[1],&index)&&index==before);
	/* The target is valid, but a poison link after it still rejects. */
	takenList=List(&pool,twoTakenIndices,2);pool.slots[1].item.next=(const struct NativeCanonicalPoolItem *)(uintptr_t)UINT32_C(0x12345678);
	index=before;CHECK(!NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,NULL,NATIVE_CANONICAL_POOL_FREE_LIST_IGNORE,&pool.slots[0],&index)&&index==before);
	freeList=List(&pool,freeIndices,2);freeList.first=(const struct NativeCanonicalPoolItem *)(uintptr_t)UINT32_C(0x12345678);CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);pool.slots[2].item.prev=NULL;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);pool.slots[2].item.next=&pool.slots[1].item;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);freeList.last=&pool.slots[3].item;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);freeList.count=-1;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);freeList.count=5;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);freeList.count=0;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	empty=List(&pool,NULL,0);CHECK(NativeCanonicalPool_ValidateList(&geometry,&empty));empty.first=&pool.slots[0].item;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&empty));
	freeList=List(&pool,freeIndices,2);freeList.first=NULL;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);freeList.last=NULL;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);pool.slots[1].item.prev=&pool.slots[0].item;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);pool.slots[1].item.next=NULL;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	freeList=List(&pool,freeIndices,2);pool.slots[1].item.next=(const struct NativeCanonicalPoolItem *)(uintptr_t)1;CHECK(!NativeCanonicalPool_ValidateList(&geometry,&freeList));
	takenList=List(&pool,takenIndices,1);empty=List(&pool,NULL,0);
	CHECK(NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,&empty,NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED,&pool.slots[0],&index)&&index==0);
	CHECK(NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,NULL,NATIVE_CANONICAL_POOL_FREE_LIST_OPTIONAL,&pool.slots[0],&index)&&index==0);
	index=before;CHECK(!NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,NULL,NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED,&pool.slots[0],&index)&&index==before);
	/* A singleton can coherently appear on both lists; the ownership query
	 * rejects that double-list even though neither list itself is malformed. */
	takenList=List(&pool,takenIndices,1);freeList=takenList;
	index=before;CHECK(!NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,&freeList,NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED,&pool.slots[0],&index)&&index==before);
	takenList=List(&pool,takenIndices,1);freeList=takenList;
	index=before;CHECK(!NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,&freeList,NATIVE_CANONICAL_POOL_FREE_LIST_OPTIONAL,&pool.slots[0],&index)&&index==before);
	takenList=List(&pool,takenIndices,1);freeList=List(&pool,freeIndices,2);freeList.first=(const struct NativeCanonicalPoolItem *)(uintptr_t)1;
	index=before;CHECK(!NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,&freeList,NATIVE_CANONICAL_POOL_FREE_LIST_OPTIONAL,&pool.slots[0],&index)&&index==before);
	index=before;CHECK(!NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,NULL,(enum NativeCanonicalPoolFreeListMode)99,&pool.slots[0],&index)&&index==before);
	/* IGNORE deliberately tolerates unrelated free metadata for taken-only callers. */
	takenList=List(&pool,takenIndices,1);freeList.first=(const struct NativeCanonicalPoolItem *)(uintptr_t)UINT32_C(0x12345678);
	CHECK(NativeCanonicalPool_AllocatedInTaken(&geometry,&takenList,&freeList,NATIVE_CANONICAL_POOL_FREE_LIST_IGNORE,&pool.slots[0],&index)&&index==0);
	return 0;
}

static int TestPayload(void)
{
	struct TestPool pool={{0}};
	struct NativeCanonicalPoolGeometry geometry;
	struct NativeCanonicalPoolInput input=Input(&pool);
	uintptr_t payload;
	uint32_t index,before=UINT32_C(0xa5a5a5a5);
	CHECK(NativeCanonicalPool_GeometrySnapshot(&input,&geometry));
	payload=geometry.base+geometry.stride+sizeof(struct NativeCanonicalPoolItem);
	CHECK(NativeCanonicalPool_PayloadStartIndex(&geometry,(const void *)payload,sizeof(struct NativeCanonicalPoolItem),geometry.stride-sizeof(struct NativeCanonicalPoolItem),&index)&&index==1);
	index=before;CHECK(!NativeCanonicalPool_PayloadStartIndex(&geometry,(const void *)(payload-1),sizeof(struct NativeCanonicalPoolItem),1,&index)&&index==before);
	CHECK(!NativeCanonicalPool_PayloadStartIndex(&geometry,(const void *)payload,sizeof(struct NativeCanonicalPoolItem),geometry.stride-sizeof(struct NativeCanonicalPoolItem)+1,&index)&&index==before);
	CHECK(!NativeCanonicalPool_PayloadStartIndex(&geometry,(const void *)(geometry.base+geometry.stride),geometry.stride,0,&index)&&index==before);
	CHECK(!NativeCanonicalPool_PayloadStartIndex(&geometry,(const void *)(geometry.base+geometry.span),0,1,&index)&&index==before);
	return 0;
}

int main(void)
{
	if(TestGeometryAndSlots()!=0||TestListsAndOwnership()!=0||TestPayload()!=0)return 1;
	puts("native_canonical_pool_test: passed");
	return 0;
}
