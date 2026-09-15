#include "MAIN/MainCanonicalWorldCounters.h"

/* common.h carries the native GP backing pointer definition.  The extractor
 * owns its real definition; give this test TU its own unused spelling while
 * supplying the backing storage required by that object file. */
#define sdata main_canonical_world_counters_test_sdata
#include "common.h"
#undef sdata

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #x); return 1; } } while (0)

struct sData sdata_static;

static int Equal(const struct NativeCanonicalWorldCountersV1 *a,
	const struct NativeCanonicalWorldCountersV1 *b)
{
	return memcmp(a, b, sizeof(*a)) == 0;
}

static int IndependentFromFacts(struct NativeCanonicalWorldCountersV1 *out,
	uint32_t missileCount)
{
	const struct NativeCanonicalWorldCountersV1Facts facts = {
		NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE, missileCount
	};
	return NativeCanonicalWorldCountersV1_FromFacts(out, &facts);
}

static int TestEveryValidCount(void)
{
	struct GameTracker tracker;
	struct NativeCanonicalWorldCountersV1 actual, expected;

	memset(&tracker, 0, sizeof(tracker));
	for (uint32_t count = 0;
		count <= NATIVE_CANONICAL_WORLD_COUNTERS_V1_MAX_ACTIVE_BOMB_MISSILE_COUNT; ++count) {
		tracker.numMissiles = count;
		CHECK(IndependentFromFacts(&expected, count));
		CHECK(MainCanonicalWorldCounters_ExtractV1(&tracker, &actual));
		CHECK(Equal(&actual, &expected));
		CHECK(actual.flags == NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE);
	}
	return 0;
}

static int TestUnrelatedFieldsAreIgnored(void)
{
	struct GameTracker tracker;
	struct Driver firstDriver, secondDriver;
	struct NativeCanonicalWorldCountersV1 baseline, changed;

	memset(&tracker, 0, sizeof(tracker));
	memset(&firstDriver, 0, sizeof(firstDriver));
	memset(&secondDriver, 0, sizeof(secondDriver));
	tracker.numMissiles = 0;
	CHECK(MainCanonicalWorldCounters_ExtractV1(&tracker, &baseline));
	tracker.gameMode1 = -99;
	tracker.currLEV = 771;
	tracker.drivers[0] = &firstDriver;
	tracker.drivers[7] = &secondDriver;
	CHECK(MainCanonicalWorldCounters_ExtractV1(&tracker, &changed));
	CHECK(Equal(&baseline, &changed));
	return 0;
}

static int TestFailuresAreAtomic(void)
{
	struct GameTracker tracker;
	struct NativeCanonicalWorldCountersV1 out, before;
	const uint32_t invalidCounts[] = { 13u, UINT32_C(0x80000000), UINT32_MAX };

	memset(&tracker, 0, sizeof(tracker));
	CHECK(IndependentFromFacts(&out, 6));
	before = out;
	CHECK(!MainCanonicalWorldCounters_ExtractV1(NULL, &out));
	CHECK(Equal(&out, &before));
	CHECK(!MainCanonicalWorldCounters_ExtractV1(&tracker, NULL));
	CHECK(Equal(&out, &before));
	for (size_t i = 0; i < sizeof(invalidCounts) / sizeof(invalidCounts[0]); ++i) {
		tracker.numMissiles = invalidCounts[i];
		CHECK(!MainCanonicalWorldCounters_ExtractV1(&tracker, &out));
		CHECK(Equal(&out, &before));
	}
	return 0;
}

int main(void)
{
	if (TestEveryValidCount() || TestUnrelatedFieldsAreIgnored() || TestFailuresAreAtomic()) return 1;
	puts("main_canonical_world_counters_test: passed");
	return 0;
}
