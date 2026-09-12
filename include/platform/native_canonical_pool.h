#ifndef PLATFORM_NATIVE_CANONICAL_POOL_H
#define PLATFORM_NATIVE_CANONICAL_POOL_H

#include <stddef.h>
#include <stdint.h>

/* Pointer-free validation view of a JitPool.  This is deliberately not a
 * game adapter: callers snapshot native fields into these explicit values. */
#define NATIVE_CANONICAL_POOL_ALIGNMENT 4u

struct NativeCanonicalPoolItem
{
	const struct NativeCanonicalPoolItem *next;
	const struct NativeCanonicalPoolItem *prev;
};

struct NativeCanonicalPoolList
{
	const struct NativeCanonicalPoolItem *first;
	const struct NativeCanonicalPoolItem *last;
	int32_t count;
};

struct NativeCanonicalPoolInput
{
	const void *base;
	int32_t maxItems;
	uint32_t itemSize;
	int32_t poolSize;
};

struct NativeCanonicalPoolGeometry
{
	uintptr_t base;
	size_t itemSize;
	size_t stride;
	size_t span;
	uint32_t maxItems;
};

enum NativeCanonicalPoolFreeListMode
{
	NATIVE_CANONICAL_POOL_FREE_LIST_IGNORE = 0,
	NATIVE_CANONICAL_POOL_FREE_LIST_OPTIONAL = 1,
	NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED = 2
};

/* Snapshots only coherent JitPool geometry. On failure geometryOut is not
 * modified. The runtime stride is itemSize rounded down to 4-byte alignment. */
int NativeCanonicalPool_GeometrySnapshot(const struct NativeCanonicalPoolInput *input,
	struct NativeCanonicalPoolGeometry *geometryOut);

/* All success+out queries leave their output unchanged on failure. */
int NativeCanonicalPool_SlotIndex(const struct NativeCanonicalPoolGeometry *geometry,
	const void *slot, uint32_t *slotIndexOut);
int NativeCanonicalPool_ValidateList(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *list);

/* Thread/stack-style ownership: a valid free list is authoritative; taken
 * list contents are intentionally irrelevant. */
int NativeCanonicalPool_AllocatedFromFree(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *freeList, const void *slot, uint32_t *slotIndexOut);

/* Instance/rain-style ownership: the slot must be in a valid taken list.
 * OPTIONAL validates and rejects free/taken overlap when a free list is
 * supplied; REQUIRED requires one; IGNORE does not inspect it. */
int NativeCanonicalPool_AllocatedInTaken(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *takenList, const struct NativeCanonicalPoolList *freeList,
	enum NativeCanonicalPoolFreeListMode freeListMode, const void *slot, uint32_t *slotIndexOut);

/* Validates that payloadStart is the exact payloadOffset inside one pool slot,
 * and that payloadSize fits the item's declared payload extent. */
int NativeCanonicalPool_PayloadStartIndex(const struct NativeCanonicalPoolGeometry *geometry,
	const void *payloadStart, size_t payloadOffset, size_t payloadSize, uint32_t *slotIndexOut);

#endif
