#include "MAIN/MainCanonicalWorldMineRegistry.h"

#define sdata main_canonical_world_mine_registry_test_sdata
#include "common.h"
#undef sdata

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #x); return 1; } } while (0)

struct sData sdata_static;

static int Equal(const struct NativeCanonicalWorldMineRegistryV1 *a,
	const struct NativeCanonicalWorldMineRegistryV1 *b)
{
	return memcmp(a, b, sizeof(*a)) == 0;
}

static void Link(struct OverlayDATA_231 *source, struct LinkedList *list,
	const uint8_t *indices, uint32_t count)
{
	memset(list, 0, sizeof(*list));
	list->count = (s32)count;
	if (!count) return;
	list->first = &source->minePoolItem[indices[0]].item;
	list->last = &source->minePoolItem[indices[count - 1]].item;
	for (uint32_t i = 0; i < count; ++i) {
		struct Item *item = &source->minePoolItem[indices[i]].item;
		item->prev = i ? &source->minePoolItem[indices[i - 1]].item : NULL;
		item->next = i + 1 < count ? &source->minePoolItem[indices[i + 1]].item : NULL;
	}
}

static void Fixture(struct OverlayDATA_231 *source, uint32_t cap, uint32_t taken)
{
	uint8_t freeIndices[50], takenIndices[50];
	memset(source, 0, sizeof(*source));
	for (uint32_t i = 0; i < cap - taken; ++i) freeIndices[i] = (uint8_t)i;
	for (uint32_t i = 0; i < taken; ++i) takenIndices[i] = (uint8_t)(cap - taken + i);
	Link(source, &source->minePoolFree, freeIndices, cap - taken);
	Link(source, &source->minePoolTaken, takenIndices, taken);
}

static void Tracker(struct GameTracker *tracker, uint32_t mode, int level, uint8_t overlay)
{
	memset(tracker, 0, sizeof(*tracker));
	tracker->gameMode1 = (int)mode;
	tracker->levelID = level;
	tracker->overlayIndex_Threads = overlay;
}

static int Baseline(struct NativeCanonicalWorldMineRegistryV1 *out)
{
	struct NativeCanonicalWorldMineRegistryV1Facts facts = {0};
	return NativeCanonicalWorldMineRegistryV1_FromFacts(out, &facts);
}

static int TestCapacitiesAndRanks(void)
{
	const struct { uint32_t mode; int level; uint32_t cap; } modes[] = {
		{ 0, 0, 10 }, { ADVENTURE_BOSS, ROO_TUBES, 7 },
		{ ADVENTURE_BOSS, DRAGON_MINES, 3 },
		{ CRYSTAL_CHALLENGE | ADVENTURE_BOSS, DRAGON_MINES, 40 }
	};
	struct GameTracker tracker; struct OverlayDATA_231 source;
	struct NativeCanonicalWorldMineRegistryV1 out;
	for (uint32_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
		uint32_t taken = modes[m].cap > 2 ? 2 : 1;
		Fixture(&source, modes[m].cap, taken);
		Tracker(&tracker, modes[m].mode, modes[m].level, OVERLAY_INDEX_RACING_OR_BATTLE);
		CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &source, &out));
		CHECK(out.flags == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE &&
			out.activeCapacity == modes[m].cap && out.takenCount == taken);
		for (uint32_t i = 0; i < modes[m].cap - taken; ++i)
			CHECK(out.records[i].state == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FREE && out.records[i].queueRank == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_NO_RANK);
		for (uint32_t i = 0; i < taken; ++i)
			CHECK(out.records[modes[m].cap - taken + i].state == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN && out.records[modes[m].cap - taken + i].queueRank == i);
	}
	return 0;
}

static int TestOrdersAndIgnoredTail(void)
{
	struct GameTracker tracker; struct OverlayDATA_231 a, b;
	struct NativeCanonicalWorldMineRegistryV1 aa, bb;
	const uint8_t freeA[] = { 0, 4, 1, 5, 3, 2, 6 };
	const uint8_t freeB[] = { 6, 2, 3, 5, 1, 4, 0 };
	const uint8_t taken[] = { 9, 7, 8 };
	Fixture(&a, 10, 0); Fixture(&b, 10, 0);
	Link(&a, &a.minePoolFree, freeA, 7); Link(&a, &a.minePoolTaken, taken, 3);
	Link(&b, &b.minePoolFree, freeB, 7); Link(&b, &b.minePoolTaken, taken, 3);
	for (uint32_t i = 10; i < 50; ++i) {
		a.minePoolItem[i].item.next = (struct Item *)(uintptr_t)1;
		a.minePoolItem[i].mineWeapon = (struct MineWeapon *)(uintptr_t)1;
		b.minePoolItem[i] = a.minePoolItem[i];
	}
	Tracker(&tracker, 0, 0, OVERLAY_INDEX_RACING_OR_BATTLE);
	CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &a, &aa));
	CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &b, &bb));
	CHECK(Equal(&aa, &bb));
	CHECK(aa.records[9].queueRank == 0 && aa.records[7].queueRank == 1 && aa.records[8].queueRank == 2);
	Fixture(&a, 10, 0); Fixture(&b, 10, 10);
	CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &a, &aa) && aa.takenCount == 0);
	CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &b, &bb) && bb.takenCount == 10);
	return 0;
}

static int ExpectFail(struct GameTracker *tracker, struct OverlayDATA_231 *source,
	struct NativeCanonicalWorldMineRegistryV1 *out)
{
	struct NativeCanonicalWorldMineRegistryV1 before = *out;
	return !MainCanonicalWorldMineRegistry_ExtractV1(tracker, source, out) && Equal(out, &before);
}

static int TestUnavailableAndFailures(void)
{
	struct GameTracker tracker; struct OverlayDATA_231 source; struct NativeCanonicalWorldMineRegistryV1 out, exact;
	CHECK(Baseline(&out)); exact = out;
	for (uint8_t overlay = OVERLAY_INDEX_MAIN_MENU; overlay <= OVERLAY_INDEX_PODIUMS; ++overlay) {
		if (overlay == OVERLAY_INDEX_RACING_OR_BATTLE) continue;
		Tracker(&tracker, 0, 0, overlay); out.version = 99;
		CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, (const void *)(uintptr_t)1, &out) && Equal(&out, &exact));
	}
	Tracker(&tracker, 0, 0, OVERLAY_INDEX_NONE); out.version = 77;
	CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, NULL, &out) && Equal(&out, &exact));
	Tracker(&tracker, 0, 0, 42); CHECK(ExpectFail(&tracker, NULL, &out));
	Tracker(&tracker, 0, 0, OVERLAY_INDEX_RACING_OR_BATTLE); CHECK(ExpectFail(&tracker, NULL, &out));
	Fixture(&source, 10, 3); CHECK(MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &source, &out));
	CHECK(!MainCanonicalWorldMineRegistry_ExtractV1(NULL, &source, &out) && !MainCanonicalWorldMineRegistry_ExtractV1(&tracker, &source, NULL));
	Fixture(&source, 10, 3); source.minePoolFree.count = 8; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.first = NULL; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.first = (struct Item *)(uintptr_t)1; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.first = (struct Item *)((uintptr_t)&source.minePoolItem[0].item + 1); CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.first = &source.minePoolItem[9].item; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.first->next = NULL; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.last->next = source.minePoolFree.first; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.last = source.minePoolFree.first; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolTaken.first->prev = source.minePoolFree.last; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolTaken.first = source.minePoolFree.first; CHECK(ExpectFail(&tracker, &source, &out));
	Fixture(&source, 10, 3); source.minePoolFree.count = 6; CHECK(ExpectFail(&tracker, &source, &out));
	return 0;
}

int main(void)
{
	if (TestCapacitiesAndRanks() || TestOrdersAndIgnoredTail() || TestUnavailableAndFailures()) return 1;
	puts("main_canonical_world_mine_registry_test: passed");
	return 0;
}
