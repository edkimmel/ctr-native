#include "platform/native_canonical_fixed_array.h"

#include <limits.h>

static int GeometryValid(const struct NativeCanonicalFixedArrayGeometry *geometry)
{
	return geometry!=NULL&&geometry->base!=0&&geometry->count!=0&&geometry->elementSize!=0&&
		(size_t)geometry->count<=SIZE_MAX/geometry->elementSize&&geometry->span!=0&&
		geometry->span==(size_t)geometry->count*geometry->elementSize&&
		geometry->span<=UINTPTR_MAX-geometry->base;
}

int NativeCanonicalFixedArray_GeometrySnapshot(const struct NativeCanonicalFixedArrayInput *input,
	struct NativeCanonicalFixedArrayGeometry *geometryOut)
{
	struct NativeCanonicalFixedArrayGeometry candidate;
	size_t count,span;
	if(input==NULL||geometryOut==NULL||input->base==0||input->count==0||
		input->count>UINT32_MAX||input->elementSize==0)return 0;
	count=(size_t)input->count;
	if(count>SIZE_MAX/input->elementSize)return 0;
	span=count*input->elementSize;
	if(span==0||span>UINTPTR_MAX-input->base)return 0;
	candidate.base=input->base;
	candidate.count=(uint32_t)input->count;
	candidate.elementSize=input->elementSize;
	candidate.span=span;
	*geometryOut=candidate;
	return 1;
}

int NativeCanonicalFixedArray_Index(const struct NativeCanonicalFixedArrayGeometry *geometry,
	uintptr_t candidateAddress,uint32_t *indexOut)
{
	uintptr_t delta;
	uint32_t index;
	if(!GeometryValid(geometry)||candidateAddress==0||indexOut==NULL||candidateAddress<geometry->base)return 0;
	delta=candidateAddress-geometry->base;
	if(delta>=geometry->span||(delta%geometry->elementSize)!=0)return 0;
	index=(uint32_t)(delta/geometry->elementSize);
	if(index>=geometry->count)return 0;
	*indexOut=index;
	return 1;
}

int NativeCanonicalFixedArray_NullableIndex(const struct NativeCanonicalFixedArrayGeometry *geometry,
	uintptr_t candidateAddress,uint32_t *indexOut)
{
	if(indexOut==NULL)return 0;
	if(candidateAddress==0)
	{
		if(!GeometryValid(geometry))return 0;
		*indexOut=NATIVE_CANONICAL_FIXED_ARRAY_NULL_INDEX;
		return 1;
	}
	return NativeCanonicalFixedArray_Index(geometry,candidateAddress,indexOut);
}
