#include "platform/native_canonical_world_mine_registry.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int Equal(const struct NativeCanonicalWorldMineRegistryV1 *a, const struct NativeCanonicalWorldMineRegistryV1 *b)
{ return memcmp(a, b, sizeof(*a)) == 0; }

static void Facts(struct NativeCanonicalWorldMineRegistryV1Facts *facts)
{
	memset(facts, 0, sizeof(*facts)); facts->flags = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE;
	facts->activeCapacity = 10; facts->takenCount = 3;
	for (uint32_t i = 0; i < facts->activeCapacity; ++i) { facts->records[i].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FREE; facts->records[i].queueRank = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_NO_RANK; }
	/* Fixture queue head -> tail: stable slots 7, 1, 9. */
	facts->records[7].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN; facts->records[7].queueRank = 0;
	facts->records[1].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN; facts->records[1].queueRank = 1;
	facts->records[9].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN; facts->records[9].queueRank = 2;
}

static int TestLayoutAndPointerIndependence(void)
{
	struct NativeCanonicalWorldMineRegistryV1Facts facts, relocated; struct NativeCanonicalWorldMineRegistryV1 a, b, decoded;
	uint8_t bytes[116]; struct NativeCodecWriter writer; struct NativeCodecReader reader; uint64_t da, db;
	Facts(&facts); relocated = facts; relocated.records[49].state = 0xa5; relocated.records[49].queueRank = 0x5a; /* Inactive fixture tail/base storage is ignored. */
	CHECK(NativeCanonicalWorldMineRegistryV1_FromFacts(&a, &facts)); CHECK(NativeCanonicalWorldMineRegistryV1_FromFacts(&b, &relocated));
	CHECK(Equal(&a, &b) && NativeCanonicalWorldMineRegistryV1_EncodedSize(&a) == 36 && NativeCanonicalWorldMineRegistryV1_Digest(&a, &da) && NativeCanonicalWorldMineRegistryV1_Digest(&b, &db) && da == db);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL); CHECK(NativeCanonicalWorldMineRegistryV1_Encode(&writer, &a) && writer.offset == 36);
	CHECK(bytes[0] == 1 && bytes[4] == 1 && bytes[8] == 10 && bytes[12] == 3); /* Explicit little-endian header. */
	CHECK(bytes[16 + 2 * 7] == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN && bytes[17 + 2 * 7] == 0);
	NativeCodecReader_Init(&reader, bytes, writer.offset); CHECK(NativeCanonicalWorldMineRegistryV1_Decode(&reader, &decoded) && reader.offset == writer.offset && Equal(&a, &decoded));
	return 0;
}

static int TestTransitionsOverflowAndBounds(void)
{
	struct NativeCanonicalWorldMineRegistryV1Facts facts; struct NativeCanonicalWorldMineRegistryV1 out, before;
	Facts(&facts); CHECK(NativeCanonicalWorldMineRegistryV1_FromFacts(&out, &facts));
	/* Remove rank 1 and append free slot 4: queue becomes 7,9,4. */
	facts.records[1].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FREE; facts.records[1].queueRank = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_NO_RANK;
	facts.records[9].queueRank = 1; facts.records[4].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN; facts.records[4].queueRank = 2;
	CHECK(NativeCanonicalWorldMineRegistryV1_FromFacts(&out, &facts));
	before = out; facts.records[2].state = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN; facts.records[2].queueRank = 3; facts.takenCount = 3;
	CHECK(!NativeCanonicalWorldMineRegistryV1_FromFacts(&out, &facts) && Equal(&out, &before));
	Facts(&facts); facts.activeCapacity = 51; CHECK(!NativeCanonicalWorldMineRegistryV1_FromFacts(&out, &facts) && Equal(&out, &before));
	Facts(&facts); facts.records[7].queueRank = 3; CHECK(!NativeCanonicalWorldMineRegistryV1_FromFacts(&out, &facts) && Equal(&out, &before));
	return 0;
}

static int TestMalformedAndAtomicity(void)
{
	struct NativeCanonicalWorldMineRegistryV1Facts facts; struct NativeCanonicalWorldMineRegistryV1 value, out, before, invalid;
	uint8_t bytes[116], bad[116], buffer[116]; struct NativeCodecWriter writer, writerBefore; struct NativeCodecReader reader, readerBefore; struct NativeCodecDigest64 digest, digestBefore;
	Facts(&facts); CHECK(NativeCanonicalWorldMineRegistryV1_FromFacts(&value, &facts));
	memset(buffer, 0xa5, sizeof(buffer)); NativeCodecDigest64_Init(&digest); digestBefore = digest; NativeCodecWriter_Init(&writer, buffer, 35, &digest); writerBefore = writer;
	CHECK(!NativeCanonicalWorldMineRegistryV1_Encode(&writer, &value) && memcmp(&writer, &writerBefore, sizeof(writer)) == 0 && memcmp(&digest, &digestBefore, sizeof(digest)) == 0);
	for (size_t i = 0; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0xa5);
	invalid = value; invalid.records[0].queueRank = 0; NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL); writerBefore = writer; CHECK(!NativeCanonicalWorldMineRegistryV1_Encode(&writer, &invalid) && memcmp(&writer, &writerBefore, sizeof(writer)) == 0);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL); CHECK(NativeCanonicalWorldMineRegistryV1_Encode(&writer, &value));
	before = value; before.takenCount = 99; out = before; NativeCodecReader_Init(&reader, bytes, 35); readerBefore = reader; CHECK(!NativeCanonicalWorldMineRegistryV1_Decode(&reader, &out) && memcmp(&reader, &readerBefore, sizeof(reader)) == 0 && Equal(&out, &before));
	memcpy(bad, bytes, writer.offset); bad[17 + 2 * 7] = 1; out = before; NativeCodecReader_Init(&reader, bad, writer.offset); readerBefore = reader; CHECK(!NativeCanonicalWorldMineRegistryV1_Decode(&reader, &out) && memcmp(&reader, &readerBefore, sizeof(reader)) == 0 && Equal(&out, &before));
	return 0;
}

static int TestUnavailableExact(void)
{
	struct NativeCanonicalWorldMineRegistryV1Facts facts = {0}; struct NativeCanonicalWorldMineRegistryV1 a, b; uint8_t bytes[16]; struct NativeCodecWriter writer;
	CHECK(NativeCanonicalWorldMineRegistryV1_FromFacts(&a, &facts)); NativeCanonicalWorldMineRegistryV1_Init(&b); CHECK(Equal(&a, &b) && NativeCanonicalWorldMineRegistryV1_EncodedSize(&a) == 16);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL); CHECK(NativeCanonicalWorldMineRegistryV1_Encode(&writer, &a) && writer.offset == 16); for (size_t i = 4; i < sizeof(bytes); ++i) CHECK(bytes[i] == 0);
	facts.activeCapacity = 1; CHECK(!NativeCanonicalWorldMineRegistryV1_FromFacts(&a, &facts)); return 0;
}

int main(void)
{
	if (TestLayoutAndPointerIndependence() || TestTransitionsOverflowAndBounds() || TestMalformedAndAtomicity() || TestUnavailableExact()) return 1;
	puts("native_canonical_world_mine_registry_v1_test: passed"); return 0;
}
