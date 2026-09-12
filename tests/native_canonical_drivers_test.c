#include "platform/native_canonical_drivers.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

static int TestEmptyGoldenAndRoundTrip(void)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];
	uint8_t before[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];
	struct NativeCanonicalDriversV1 drivers, decoded, untouched;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&drivers, 0, stream));
	CHECK(drivers.rosterMetaDigest == UINT64_C(0xb9b23f3a46fd0825));
	CHECK(drivers.fullStreamDigest == UINT64_C(0x3937141309162d25));
	CHECK(drivers.slots[0].slotDigest == UINT64_C(0x7270ce3a3ef261c5));
	CHECK(drivers.slots[0].metaRaceDigest == UINT64_C(0x1fc05eb337858375));
	CHECK(drivers.slots[0].physicsDynamicsDigest == UINT64_C(0x92184cc8715e4dc5));
	CHECK(drivers.slots[0].behaviorBotDigest == UINT64_C(0xeb4f61260020abd5));
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeCanonicalDriversV1_Encode(&writer, &drivers));
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES);
	CHECK(bytes[0] == 1 && bytes[4] == 8 && bytes[8] == 0 && bytes[12] == 3);
	CHECK(bytes[16] == 0x25 && bytes[17] == 0x08 && bytes[18] == 0xfd && bytes[19] == 0x46);
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCanonicalDriversV1_Decode(&reader, &decoded));
	CHECK(reader.offset == sizeof(bytes));
	CHECK(memcmp(&drivers, &decoded, sizeof(drivers)) == 0);

	memcpy(before, bytes, sizeof(bytes)); bytes[0] = 2;
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes)); memset(&untouched, 0xa5, sizeof(untouched));
	CHECK(!NativeCanonicalDriversV1_Decode(&reader, &untouched));
	CHECK(reader.offset == 0 && ((const unsigned char *)&untouched)[0] == 0xa5);
	memcpy(bytes, before, sizeof(bytes));
	return 0;
}

static int TestNormativeMutationsAndComparison(void)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	struct NativeCanonicalDriversV1 base, changed;
	struct NativeCanonicalDriversCompareMask masks;
	const uint32_t slot = 3;
	const size_t offset = NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES + slot * NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES;

	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES; i++) stream[i] = (uint8_t)i;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES; i++) stream[offset + i] = (uint8_t)(0x80u + i);
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&base, UINT32_C(1) << slot, stream));
	stream[offset + 99] ^= 1; CHECK(NativeCanonicalDriversV1_FromNormativeStream(&changed, UINT32_C(1) << slot, stream));
	CHECK(NativeCanonicalDriversV1_Compare(&base, &changed, &masks) != 0);
	CHECK((masks.slotMask & (UINT32_C(1) << slot)) != 0 && (masks.metaRaceMask & (UINT32_C(1) << slot)) != 0);
	stream[offset + 99] ^= 1; stream[offset + NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES] ^= 1;
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&changed, UINT32_C(1) << slot, stream));
	(void)NativeCanonicalDriversV1_Compare(&base, &changed, &masks);
	CHECK((masks.physicsDynamicsMask & (UINT32_C(1) << slot)) != 0 && (masks.metaRaceMask & (UINT32_C(1) << slot)) == 0);
	stream[offset + NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES] ^= 1; stream[offset + 364] ^= 1;
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&changed, UINT32_C(1) << slot, stream));
	(void)NativeCanonicalDriversV1_Compare(&base, &changed, &masks);
	CHECK((masks.behaviorBotMask & (UINT32_C(1) << slot)) != 0);
	stream[offset + 364] ^= 1; stream[offset] = 1;
	CHECK(!NativeCanonicalDriversV1_FromNormativeStream(&changed, 0, stream));
	return 0;
}

int main(void)
{
	if ((TestEmptyGoldenAndRoundTrip() != 0) || (TestNormativeMutationsAndComparison() != 0)) return 1;
	puts("native_canonical_drivers_test: passed");
	return 0;
}
