#include "common.h"
#include "MainCanonicalWorldMineRegistry.h"

#include <stdint.h>

CTR_STATIC_ASSERT(OFFSETOF(struct OverlayDATA_231, minePoolTaken) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct OverlayDATA_231, minePoolFree) == 0xc);
CTR_STATIC_ASSERT(OFFSETOF(struct OverlayDATA_231, minePoolItem) == 0x18);
CTR_STATIC_ASSERT(sizeof(((struct OverlayDATA_231 *)0)->minePoolItem) /
	sizeof(((struct OverlayDATA_231 *)0)->minePoolItem[0]) ==
	NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS);
CTR_STATIC_ASSERT(OFFSETOF(struct WeaponSlot231, item) == 0x0);

static uint32_t MainCanonicalWorldMineRegistry_MineCapacity(const struct GameTracker *gGT)
{
	if ((uint32_t)gGT->gameMode1 & CRYSTAL_CHALLENGE) return 40;
	if (((uint32_t)gGT->gameMode1 & ADVENTURE_BOSS) && gGT->levelID == DRAGON_MINES) return 3;
	if (((uint32_t)gGT->gameMode1 & ADVENTURE_BOSS) && gGT->levelID == ROO_TUBES) return 7;
	return 10;
}

static int MainCanonicalWorldMineRegistry_SlotIndex(const struct OverlayDATA_231 *source, uint32_t capacity,
	const struct Item *item, uint32_t *index)
{
	uintptr_t base = (uintptr_t)&source->minePoolItem[0];
	uintptr_t address = (uintptr_t)item;
	uintptr_t span = (uintptr_t)capacity * sizeof(source->minePoolItem[0]);
	uintptr_t delta;

	if (address < base || address - base >= span) return 0;
	delta = address - base;
	if (delta % sizeof(source->minePoolItem[0]) != 0) return 0;
	*index = (uint32_t)(delta / sizeof(source->minePoolItem[0]));
	return 1;
}

static int MainCanonicalWorldMineRegistry_ReadList(const struct OverlayDATA_231 *source, const struct LinkedList *list,
	uint32_t capacity, uint8_t *seen, uint32_t *countOut,
	struct NativeCanonicalWorldMineRegistryV1Facts *facts, int taken)
{
	const struct Item *item, *previous = NULL;
	uint32_t count, index;

	if (list->count < 0 || (uint32_t)list->count > capacity) return 0;
	count = (uint32_t)list->count;
	if (count == 0) {
		if (list->first != NULL || list->last != NULL) return 0;
		*countOut = 0;
		return 1;
	}
	if (list->first == NULL || list->last == NULL) return 0;
	item = list->first;
	for (uint32_t ordinal = 0; ordinal < count; ++ordinal) {
		if (item == NULL || !MainCanonicalWorldMineRegistry_SlotIndex(source, capacity, item, &index) || seen[index]) return 0;
		if (item->prev != previous) return 0;
		seen[index] = 1;
		if (taken) {
			facts->records[index].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN;
			facts->records[index].queueRank = (uint8_t)ordinal;
		}
		previous = item;
		item = item->next;
	}
	if (item != NULL || previous != list->last || list->last->next != NULL) return 0;
	*countOut = count;
	return 1;
}

int MainCanonicalWorldMineRegistry_ExtractV1(const struct GameTracker *gGT,
	const struct OverlayDATA_231 *source,
	struct NativeCanonicalWorldMineRegistryV1 *out)
{
	struct NativeCanonicalWorldMineRegistryV1Facts facts = {0};
	struct NativeCanonicalWorldMineRegistryV1 candidate;
	uint8_t seen[NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS] = {0};
	uint32_t freeCount, takenCount, capacity;

	if (!gGT || !out) return 0;
	switch (gGT->overlayIndex_Threads) {
	case OVERLAY_INDEX_MAIN_MENU:
	case OVERLAY_INDEX_ADV_HUB:
	case OVERLAY_INDEX_PODIUMS:
	case OVERLAY_INDEX_NONE:
		if (!NativeCanonicalWorldMineRegistryV1_FromFacts(&candidate, &facts)) return 0;
		*out = candidate;
		return 1;
	case OVERLAY_INDEX_RACING_OR_BATTLE:
		break;
	default:
		return 0;
	}
	if (!source) return 0;
	capacity = MainCanonicalWorldMineRegistry_MineCapacity(gGT);
	facts.flags = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE;
	facts.activeCapacity = capacity;
	for (uint32_t i = 0; i < capacity; ++i) {
		facts.records[i].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FREE;
		facts.records[i].queueRank = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_NO_RANK;
	}
	if (!MainCanonicalWorldMineRegistry_ReadList(source, &source->minePoolFree, capacity, seen, &freeCount, &facts, 0) ||
		!MainCanonicalWorldMineRegistry_ReadList(source, &source->minePoolTaken, capacity, seen, &takenCount, &facts, 1) ||
		freeCount + takenCount != capacity) return 0;
	for (uint32_t i = 0; i < capacity; ++i) if (!seen[i]) return 0;
	facts.takenCount = takenCount;
	if (!NativeCanonicalWorldMineRegistryV1_FromFacts(&candidate, &facts)) return 0;
	*out = candidate;
	return 1;
}
