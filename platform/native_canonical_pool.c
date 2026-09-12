#include "platform/native_canonical_pool.h"

#include <limits.h>

static int GeometryValid(const struct NativeCanonicalPoolGeometry *geometry)
{
	return geometry!=NULL && geometry->base!=0 && (geometry->base%NATIVE_CANONICAL_POOL_ALIGNMENT)==0 && geometry->maxItems!=0 && geometry->stride>=sizeof(struct NativeCanonicalPoolItem) &&
		geometry->stride==((geometry->itemSize/NATIVE_CANONICAL_POOL_ALIGNMENT)*NATIVE_CANONICAL_POOL_ALIGNMENT) &&
		(size_t)geometry->maxItems<=SIZE_MAX/geometry->stride && geometry->span!=0 &&
		geometry->span==(size_t)geometry->maxItems*geometry->stride && geometry->span<=UINTPTR_MAX-geometry->base;
}

int NativeCanonicalPool_GeometrySnapshot(const struct NativeCanonicalPoolInput *input,
	struct NativeCanonicalPoolGeometry *geometryOut)
{
	struct NativeCanonicalPoolGeometry candidate;
	uintptr_t base;
	size_t stride,span,maxItems;
	if(input==NULL||geometryOut==NULL||input->base==NULL||input->maxItems<=0||input->poolSize<=0)return 0;
	base=(uintptr_t)input->base;
	if((base%NATIVE_CANONICAL_POOL_ALIGNMENT)!=0)return 0;
	stride=((size_t)input->itemSize/NATIVE_CANONICAL_POOL_ALIGNMENT)*NATIVE_CANONICAL_POOL_ALIGNMENT;
	if(stride<sizeof(struct NativeCanonicalPoolItem))return 0;
	maxItems=(size_t)input->maxItems;
	if(maxItems>SIZE_MAX/stride)return 0;
	span=maxItems*stride;
	if(span>(size_t)INT32_MAX||(size_t)input->poolSize!=span||span>UINTPTR_MAX-base)return 0;
	candidate.base=base;
	candidate.itemSize=(size_t)input->itemSize;
	candidate.stride=stride;
	candidate.span=span;
	candidate.maxItems=(uint32_t)input->maxItems;
	*geometryOut=candidate;
	return 1;
}

int NativeCanonicalPool_SlotIndex(const struct NativeCanonicalPoolGeometry *geometry,
	const void *slot, uint32_t *slotIndexOut)
{
	uintptr_t address,delta;
	uint32_t index;
	if(!GeometryValid(geometry)||slot==NULL||slotIndexOut==NULL)return 0;
	address=(uintptr_t)slot;
	if(address<geometry->base)return 0;
	delta=address-geometry->base;
	if(delta>=geometry->span||(delta%geometry->stride)!=0)return 0;
	index=(uint32_t)(delta/geometry->stride);
	if(index>=geometry->maxItems)return 0;
	*slotIndexOut=index;
	return 1;
}

int NativeCanonicalPool_ValidateList(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *list)
{
	const struct NativeCanonicalPoolItem *current,*previous,*last;
	if(!GeometryValid(geometry)||list==NULL||list->count<0||(uint32_t)list->count>geometry->maxItems)return 0;
	if(list->count==0)return list->first==NULL&&list->last==NULL;
	if(list->first==NULL||list->last==NULL)return 0;
	current=list->first;previous=NULL;last=NULL;
	for(uint32_t index=0;index<(uint32_t)list->count;index++)
	{
		uint32_t slotIndex;
		if(!NativeCanonicalPool_SlotIndex(geometry,current,&slotIndex)||current->prev!=previous)return 0;
		last=current;
		previous=current;
		current=current->next;
	}
	return current==NULL&&last==list->last;
}

static int ListContains(const struct NativeCanonicalPoolList *list,const void *slot)
{
	const struct NativeCanonicalPoolItem *current=list->first;
	int found=0;
	for(uint32_t index=0;index<(uint32_t)list->count;index++)
	{
		if(current==slot)found=1;
		current=current->next;
	}
	return found;
}

static int ListsDisjoint(const struct NativeCanonicalPoolList *left,const struct NativeCanonicalPoolList *right)
{
	const struct NativeCanonicalPoolItem *leftCurrent=left->first;
	for(uint32_t leftIndex=0;leftIndex<(uint32_t)left->count;leftIndex++)
	{
		const struct NativeCanonicalPoolItem *rightCurrent=right->first;
		for(uint32_t rightIndex=0;rightIndex<(uint32_t)right->count;rightIndex++)
		{
			if(leftCurrent==rightCurrent)return 0;
			rightCurrent=rightCurrent->next;
		}
		leftCurrent=leftCurrent->next;
	}
	return 1;
}

int NativeCanonicalPool_AllocatedFromFree(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *freeList, const void *slot, uint32_t *slotIndexOut)
{
	uint32_t candidate;
	if(slotIndexOut==NULL||!NativeCanonicalPool_SlotIndex(geometry,slot,&candidate)||!NativeCanonicalPool_ValidateList(geometry,freeList)||ListContains(freeList,slot))return 0;
	*slotIndexOut=candidate;
	return 1;
}

int NativeCanonicalPool_AllocatedInTaken(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *takenList, const struct NativeCanonicalPoolList *freeList,
	enum NativeCanonicalPoolFreeListMode freeListMode, const void *slot, uint32_t *slotIndexOut)
{
	uint32_t candidate;
	if(slotIndexOut==NULL||!NativeCanonicalPool_SlotIndex(geometry,slot,&candidate)||!NativeCanonicalPool_ValidateList(geometry,takenList))return 0;
	if(freeListMode==NATIVE_CANONICAL_POOL_FREE_LIST_IGNORE)
	{
		/* Thread/stack callers intentionally ignore stale taken/free metadata. */
	}
	else if(freeListMode==NATIVE_CANONICAL_POOL_FREE_LIST_OPTIONAL)
	{
		if(freeList!=NULL&&(!NativeCanonicalPool_ValidateList(geometry,freeList)||!ListsDisjoint(takenList,freeList)))return 0;
	}
	else if(freeListMode==NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED)
	{
		if(freeList==NULL||!NativeCanonicalPool_ValidateList(geometry,freeList)||!ListsDisjoint(takenList,freeList))return 0;
	}
	else return 0;
	if(!ListContains(takenList,slot))return 0;
	*slotIndexOut=candidate;
	return 1;
}

int NativeCanonicalPool_PayloadStartIndex(const struct NativeCanonicalPoolGeometry *geometry,
	const void *payloadStart, size_t payloadOffset, size_t payloadSize, uint32_t *slotIndexOut)
{
	uintptr_t address,delta,slotDelta;
	uint32_t candidate;
	if(!GeometryValid(geometry)||payloadStart==NULL||slotIndexOut==NULL||payloadOffset>geometry->stride||payloadSize>geometry->stride-payloadOffset)return 0;
	address=(uintptr_t)payloadStart;
	if(address<geometry->base)return 0;
	delta=address-geometry->base;
	if(delta<payloadOffset)return 0;
	slotDelta=delta-payloadOffset;
	if(slotDelta>=geometry->span||(slotDelta%geometry->stride)!=0)return 0;
	candidate=(uint32_t)(slotDelta/geometry->stride);
	if(candidate>=geometry->maxItems)return 0;
	*slotIndexOut=candidate;
	return 1;
}
