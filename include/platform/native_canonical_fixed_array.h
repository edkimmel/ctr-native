#ifndef PLATFORM_NATIVE_CANONICAL_FIXED_ARRAY_H
#define PLATFORM_NATIVE_CANONICAL_FIXED_ARRAY_H

#include <stddef.h>
#include <stdint.h>

/* Pointer-free, fixed-array identity geometry for future topology mappings.
 * Callers provide integer addresses; this module never dereferences them or
 * forms an end pointer. */
#define NATIVE_CANONICAL_FIXED_ARRAY_NULL_INDEX UINT32_MAX

struct NativeCanonicalFixedArrayInput
{
	uintptr_t base;
	uint64_t count;
	size_t elementSize;
};

struct NativeCanonicalFixedArrayGeometry
{
	uintptr_t base;
	size_t elementSize;
	size_t span;
	uint32_t count;
};

/* A successful snapshot has a nonzero base, count in 1..UINT32_MAX, and a
 * checked count*elementSize extent.  Failure leaves geometryOut unchanged. */
int NativeCanonicalFixedArray_GeometrySnapshot(const struct NativeCanonicalFixedArrayInput *input,
	struct NativeCanonicalFixedArrayGeometry *geometryOut);

/* Exact element-start identities only.  Both calls leave indexOut unchanged
 * on failure.  The nullable form maps candidate address zero to UINT32_MAX. */
int NativeCanonicalFixedArray_Index(const struct NativeCanonicalFixedArrayGeometry *geometry,
	uintptr_t candidateAddress,uint32_t *indexOut);
int NativeCanonicalFixedArray_NullableIndex(const struct NativeCanonicalFixedArrayGeometry *geometry,
	uintptr_t candidateAddress,uint32_t *indexOut);

#endif
