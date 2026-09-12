#include "platform/native_canonical_fixed_array.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

static struct NativeCanonicalFixedArrayInput Input(uintptr_t base,uint64_t count,size_t elementSize)
{
	struct NativeCanonicalFixedArrayInput input;
	input.base=base;input.count=count;input.elementSize=elementSize;
	return input;
}

static int TestSnapshot(void)
{
	struct NativeCanonicalFixedArrayGeometry geometry,before;
	struct NativeCanonicalFixedArrayInput input=Input(UINT32_C(0x1000),4,8);
	CHECK(NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry));
	CHECK(geometry.base==UINT32_C(0x1000)&&geometry.count==4&&geometry.elementSize==8&&geometry.span==32);
	before=geometry;
	input=Input(0,4,8);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINT32_C(0x1000),0,8);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINT32_C(0x1000),4,0);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINT32_C(0x1000),UINT64_C(0x100000000),1);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINTPTR_MAX-7u,1,8);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINTPTR_MAX-15u,2,8);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINT32_C(0x2000),UINT32_MAX,2);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINT32_C(0x2000),1,SIZE_MAX);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&memcmp(&geometry,&before,sizeof(geometry))==0);
	input=Input(UINTPTR_MAX-1u,1,1);CHECK(NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry)&&geometry.span==1);
	return 0;
}

static int TestIdentities(void)
{
	struct NativeCanonicalFixedArrayGeometry geometry,beforeGeometry;
	struct NativeCanonicalFixedArrayInput input=Input(UINT32_C(0x4000),4,8);
	uint32_t index,before=UINT32_C(0xa5a5a5a5);
	CHECK(NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry));
	CHECK(NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4000),&index)&&index==0);
	CHECK(NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4010),&index)&&index==2);
	CHECK(NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4018),&index)&&index==3);
	CHECK(NativeCanonicalFixedArray_NullableIndex(&geometry,0,&index)&&index==NATIVE_CANONICAL_FIXED_ARRAY_NULL_INDEX);
	index=before;CHECK(!NativeCanonicalFixedArray_Index(&geometry,0,&index)&&index==before);
	index=before;CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x3fff),&index)&&index==before);
	index=before;CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4020),&index)&&index==before);
	index=before;CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x12345678),&index)&&index==before);
	for(uintptr_t element=0;element<4;element++)for(uintptr_t offset=1;offset<8;offset++)
	{
		index=before;CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4000)+element*8u+offset,&index)&&index==before);
	}
	beforeGeometry=geometry;geometry.span=31;index=before;
	CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4000),&index)&&index==before);
	CHECK(!NativeCanonicalFixedArray_NullableIndex(&geometry,0,&index)&&index==before);
	geometry=beforeGeometry;geometry.count=UINT32_MAX;index=before;
	CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x4000),&index)&&index==before);
	return 0;
}

static int TestHighBaseAndElementSizes(void)
{
	struct NativeCanonicalFixedArrayGeometry geometry;
	struct NativeCanonicalFixedArrayInput input;
	uint32_t index,before=UINT32_C(0x5a5a5a5a);
	input=Input(UINTPTR_MAX-64u,2,16);CHECK(NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry));
	CHECK(NativeCanonicalFixedArray_Index(&geometry,UINTPTR_MAX-48u,&index)&&index==1);
	index=before;CHECK(!NativeCanonicalFixedArray_Index(&geometry,UINTPTR_MAX-32u,&index)&&index==before);
	input=Input(UINT32_C(0x8000),3,1);CHECK(NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry));
	CHECK(NativeCanonicalFixedArray_Index(&geometry,UINT32_C(0x8002),&index)&&index==2);
	input=Input(UINT32_C(0x9000),1,SIZE_MAX);CHECK(!NativeCanonicalFixedArray_GeometrySnapshot(&input,&geometry));
	return 0;
}

int main(void)
{
	if(TestSnapshot()!=0||TestIdentities()!=0||TestHighBaseAndElementSizes()!=0)return 1;
	puts("native_canonical_fixed_array_test: passed");
	return 0;
}
