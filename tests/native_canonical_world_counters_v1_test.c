#include "platform/native_canonical_world_counters.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int Equal(const struct NativeCanonicalWorldCountersV1 *a, const struct NativeCanonicalWorldCountersV1 *b)
{ return memcmp(a, b, sizeof(*a)) == 0; }

static int FromCount(struct NativeCanonicalWorldCountersV1 *out, uint32_t count)
{
	struct NativeCanonicalWorldCountersV1Facts facts = {
		NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE, count
	};
	return NativeCanonicalWorldCountersV1_FromFacts(out, &facts);
}

static int TestGoldenLayoutDigestAndBoundaries(void)
{
	struct NativeCanonicalWorldCountersV1 value, decoded;
	uint8_t bytes[NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES];
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	uint64_t digest;
	CHECK(FromCount(&value, 7));
	CHECK(NativeCanonicalWorldCountersV1_EncodedSize(&value) == sizeof(bytes));
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeCanonicalWorldCountersV1_Encode(&writer, &value) && writer.offset == sizeof(bytes));
	CHECK(memcmp(bytes, (const uint8_t[]){1, 0, 0, 0, 1, 0, 0, 0, 7, 0, 0, 0}, sizeof(bytes)) == 0);
	CHECK(NativeCanonicalWorldCountersV1_Digest(&value, &digest) && digest == UINT64_C(0x9e2ef029ca904052));
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCanonicalWorldCountersV1_Decode(&reader, &decoded) && reader.offset == sizeof(bytes) && Equal(&value, &decoded));
	for (uint32_t count = 0; count <= NATIVE_CANONICAL_WORLD_COUNTERS_V1_MAX_ACTIVE_BOMB_MISSILE_COUNT; ++count) {
		CHECK(FromCount(&value, count));
		NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
		CHECK(NativeCanonicalWorldCountersV1_Encode(&writer, &value) && bytes[8] == count && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 0);
	}
	CHECK(!FromCount(&value, NATIVE_CANONICAL_WORLD_COUNTERS_V1_MAX_ACTIVE_BOMB_MISSILE_COUNT + 1));
	return 0;
}

static int TestMutationsMalformedAndTransactional(void)
{
	struct NativeCanonicalWorldCountersV1 value, out, before, invalid;
	struct NativeCanonicalWorldCountersV1Facts facts;
	uint8_t bytes[NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES], buffer[sizeof(bytes)], bad[sizeof(bytes)];
	struct NativeCodecWriter writer, writerBefore;
	struct NativeCodecReader reader, readerBefore;
	struct NativeCodecDigest64 digest, digestBefore;
	CHECK(FromCount(&value, 6));
	memset(buffer, 0xa5, sizeof(buffer)); NativeCodecDigest64_Init(&digest); digestBefore = digest;
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer) - 1, &digest); writerBefore = writer;
	CHECK(!NativeCanonicalWorldCountersV1_Encode(&writer, &value) && memcmp(&writer, &writerBefore, sizeof(writer)) == 0 && memcmp(&digest, &digestBefore, sizeof(digest)) == 0);
	for (size_t i = 0; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0xa5);
	invalid = value; invalid.version = 2;
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL); writerBefore = writer;
	CHECK(!NativeCanonicalWorldCountersV1_Encode(&writer, &invalid) && memcmp(&writer, &writerBefore, sizeof(writer)) == 0);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL); CHECK(NativeCanonicalWorldCountersV1_Encode(&writer, &value));
	before = value; before.activeBombMissileCount = 99; out = before;
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes) - 1); readerBefore = reader;
	CHECK(!NativeCanonicalWorldCountersV1_Decode(&reader, &out) && memcmp(&reader, &readerBefore, sizeof(reader)) == 0 && Equal(&out, &before));
	for (size_t mutation = 0; mutation < 4; ++mutation) {
		memcpy(bad, bytes, sizeof(bad));
		if (mutation == 0) bad[0] = 2;       /* Version */
		if (mutation == 1) bad[4] = 2;       /* Reserved flag */
		if (mutation == 2) bad[8] = 13;      /* Counter range */
		if (mutation == 3) bad[4] = 0;       /* Unavailable must be exactly zero. */
		out = before; NativeCodecReader_Init(&reader, bad, sizeof(bad)); readerBefore = reader;
		CHECK(!NativeCanonicalWorldCountersV1_Decode(&reader, &out) && memcmp(&reader, &readerBefore, sizeof(reader)) == 0 && Equal(&out, &before));
	}
	facts.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
	facts.activeBombMissileCount = 13;
	out = value;
	CHECK(!NativeCanonicalWorldCountersV1_FromFacts(&out, &facts) && Equal(&out, &value));
	facts.flags = 2;
	facts.activeBombMissileCount = 0;
	CHECK(!NativeCanonicalWorldCountersV1_FromFacts(&out, &facts) && Equal(&out, &value));
	return 0;
}

static int TestUnavailableExact(void)
{
	struct NativeCanonicalWorldCountersV1Facts facts = {0}, differentlyInitializedFacts;
	struct NativeCanonicalWorldCountersV1 value, exact;
	uint8_t bytes[NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES];
	struct NativeCodecWriter writer;
	CHECK(NativeCanonicalWorldCountersV1_FromFacts(&value, &facts));
	NativeCanonicalWorldCountersV1_Init(&exact);
	CHECK(Equal(&value, &exact));
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeCanonicalWorldCountersV1_Encode(&writer, &value));
	CHECK(memcmp(bytes, (const uint8_t[]){1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, sizeof(bytes)) == 0);
	/* Fieldwise validation must not depend on an unavailable fact object's padding. */
	memset(&differentlyInitializedFacts, 0xa5, sizeof(differentlyInitializedFacts));
	differentlyInitializedFacts.flags = 0;
	differentlyInitializedFacts.activeBombMissileCount = 0;
	CHECK(NativeCanonicalWorldCountersV1_FromFacts(&value, &differentlyInitializedFacts));
	facts.activeBombMissileCount = 1;
	CHECK(!NativeCanonicalWorldCountersV1_FromFacts(&value, &facts));
	differentlyInitializedFacts.activeBombMissileCount = 1;
	CHECK(!NativeCanonicalWorldCountersV1_FromFacts(&value, &differentlyInitializedFacts));
	return 0;
}

int main(void)
{
	if (TestGoldenLayoutDigestAndBoundaries() || TestMutationsMalformedAndTransactional() || TestUnavailableExact()) return 1;
	puts("native_canonical_world_counters_v1_test: passed");
	return 0;
}
