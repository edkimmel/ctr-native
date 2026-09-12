#include "platform/native_canonical_drivers.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

/* This is a literal, independent wire golden for an all-zero normative stream
 * with no present slots.  It intentionally does not derive expected bytes
 * through any codec helper. */
static const uint8_t EmptySummaryGolden[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES] = {
	0x02,0x00,0x00,0x00, 0x08,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x03,0x00,0x00,0x00,
	0x25,0x08,0xfd,0x46,0x3a,0x3f,0xb2,0xb9,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0xc5,0x61,0xf2,0x3e,0x3a,0xce,0x70,0x72, 0x75,0x83,0x85,0x37,0xb3,0x5e,0xc0,0x1f, 0xc5,0x4d,0x5e,0x71,0xc8,0x4c,0x18,0x92, 0xd5,0xab,0x20,0x00,0x26,0x61,0x4f,0xeb,
	0x25,0x2d,0x16,0x09,0x13,0x14,0x37,0x39
};

static int DriversEqual(const struct NativeCanonicalDriversV1 *left, const struct NativeCanonicalDriversV1 *right)
{
	if ((left->version != right->version) || (left->slotCount != right->slotCount) ||
	    (left->presenceMask != right->presenceMask) || (left->groupCount != right->groupCount) ||
	    (left->rosterMetaDigest != right->rosterMetaDigest) || (left->fullStreamDigest != right->fullStreamDigest)) return 0;
	for (uint32_t slot = 0; slot < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; slot++)
		if ((left->slots[slot].slotDigest != right->slots[slot].slotDigest) ||
		    (left->slots[slot].metaRaceDigest != right->slots[slot].metaRaceDigest) ||
		    (left->slots[slot].physicsDynamicsDigest != right->slots[slot].physicsDynamicsDigest) ||
		    (left->slots[slot].behaviorBotDigest != right->slots[slot].behaviorBotDigest)) return 0;
	return 1;
}

static int TestEmptyGoldenAndTransactions(void)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	uint8_t longStream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES + 1] = {0};
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];
	struct NativeCanonicalDriversV1 drivers, decoded, untouched, before;
	struct NativeCodecWriter writer, shortWriter;
	struct NativeCodecReader reader, shortReader;

	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&drivers, 0, stream, sizeof(stream)));
	CHECK(drivers.rosterMetaDigest == UINT64_C(0xb9b23f3a46fd0825));
	CHECK(drivers.fullStreamDigest == UINT64_C(0x3937141309162d25));
	CHECK(drivers.slots[0].slotDigest == UINT64_C(0x7270ce3a3ef261c5));
	CHECK(drivers.slots[0].metaRaceDigest == UINT64_C(0x1fc05eb337858375));
	CHECK(drivers.slots[0].physicsDynamicsDigest == UINT64_C(0x92184cc8715e4dc5));
	CHECK(drivers.slots[0].behaviorBotDigest == UINT64_C(0xeb4f61260020abd5));
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeCanonicalDriversV1_Encode(&writer, &drivers));
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES);
	CHECK(memcmp(bytes, EmptySummaryGolden, sizeof(bytes)) == 0);
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCanonicalDriversV1_Decode(&reader, &decoded));
	CHECK(reader.offset == sizeof(bytes) && DriversEqual(&drivers, &decoded));
	/* Persisted summary v1 is not silently accepted as v2. */
	bytes[0]=1;NativeCanonicalDriversV1_Init(&untouched);untouched.fullStreamDigest=UINT64_C(0x4444444444444444);before=untouched;
	NativeCodecReader_Init(&reader,bytes,sizeof(bytes));CHECK(!NativeCanonicalDriversV1_Decode(&reader,&untouched));
	CHECK(reader.offset==0&&reader.failed==0&&DriversEqual(&untouched,&before));bytes[0]=2;

	NativeCodecWriter_Init(&shortWriter, bytes, sizeof(bytes) - 1, NULL);
	CHECK(!NativeCanonicalDriversV1_Encode(&shortWriter, &drivers));
	CHECK(shortWriter.offset == 0 && shortWriter.failed == 0);
	NativeCodecReader_Init(&shortReader, bytes, sizeof(bytes) - 1);
	NativeCanonicalDriversV1_Init(&untouched); untouched.fullStreamDigest = UINT64_C(0x1111111111111111); before = untouched;
	CHECK(!NativeCanonicalDriversV1_Decode(&shortReader, &untouched));
	CHECK(shortReader.offset == 0 && shortReader.failed == 0 && DriversEqual(&untouched, &before));

	NativeCanonicalDriversV1_Init(&untouched); untouched.fullStreamDigest = UINT64_C(0x2222222222222222); before = untouched;
	CHECK(!NativeCanonicalDriversV1_FromNormativeStream(&untouched, 0, stream, sizeof(stream) - 1));
	CHECK(DriversEqual(&untouched, &before));
	CHECK(!NativeCanonicalDriversV1_FromNormativeStream(&untouched, 0, longStream, sizeof(longStream)));
	CHECK(DriversEqual(&untouched, &before));
	CHECK(!NativeCanonicalDriversV1_FromNormativeStream(&untouched, 0, NULL, sizeof(stream)));
	CHECK(DriversEqual(&untouched, &before));
	return 0;
}

static int TestExhaustivePresentMutations(void)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	uint8_t changedStream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	struct NativeCanonicalDriversV1 base, changed;
	struct NativeCanonicalDriversCompareMask masks;

	for (size_t i = 0; i < sizeof(stream); i++) stream[i] = (uint8_t)(i * 37u + 11u);
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&base, UINT32_C(0xff), stream, sizeof(stream)));
	for (size_t byte = 0; byte < sizeof(stream); byte++)
	{
		uint32_t expectedSlot = 0, expectedMeta = 0, expectedPhysics = 0, expectedBehavior = 0;
		memcpy(changedStream, stream, sizeof(stream)); changedStream[byte] ^= UINT8_C(0x80);
		CHECK(NativeCanonicalDriversV1_FromNormativeStream(&changed, UINT32_C(0xff), changedStream, sizeof(changedStream)));
		CHECK(NativeCanonicalDriversV1_Compare(&base, &changed, &masks) == (byte < NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES ? UINT32_C(0x0a) : UINT32_C(0x0c)));
		CHECK(masks.headerMask == 0 && masks.fullStreamMask == 1);
		if (byte < NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES)
		{
			CHECK(masks.rosterMask == 1 && masks.slotMask == 0 && masks.metaRaceMask == 0 && masks.physicsDynamicsMask == 0 && masks.behaviorBotMask == 0);
			continue;
		}
		{
			const size_t slotByte = byte - NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES;
			const uint32_t slot = (uint32_t)(slotByte / NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES);
			const size_t groupByte = slotByte % NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES;
			expectedSlot = UINT32_C(1) << slot;
			if (groupByte < NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES) expectedMeta = expectedSlot;
			else if (groupByte < NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES + NATIVE_CANONICAL_DRIVERS_PHYSICS_DYNAMICS_BYTES) expectedPhysics = expectedSlot;
			else expectedBehavior = expectedSlot;
		}
		CHECK(masks.rosterMask == 0 && masks.slotMask == expectedSlot && masks.metaRaceMask == expectedMeta &&
		      masks.physicsDynamicsMask == expectedPhysics && masks.behaviorBotMask == expectedBehavior);
	}
	return 0;
}

static int TestAbsentMutationRejectsTransactionally(void)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	struct NativeCanonicalDriversV1 untouched, before;

	NativeCanonicalDriversV1_Init(&untouched); untouched.fullStreamDigest = UINT64_C(0x3333333333333333); before = untouched;
	for (size_t byte = NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES; byte < sizeof(stream); byte++)
	{
		stream[byte] = 1;
		CHECK(!NativeCanonicalDriversV1_FromNormativeStream(&untouched, 0, stream, sizeof(stream)));
		CHECK(DriversEqual(&untouched, &before));
		stream[byte] = 0;
	}
	return 0;
}

int main(void)
{
	if ((TestEmptyGoldenAndTransactions() != 0) || (TestExhaustivePresentMutations() != 0) ||
	    (TestAbsentMutationRejectsTransactionally() != 0)) return 1;
	puts("native_canonical_drivers_test: passed");
	return 0;
}
