#include "platform/native_canonical_world_mine_registry.h"

#include <string.h>

static int AvailableValid(const struct NativeCanonicalWorldMineRegistryV1 *r)
{
	uint8_t seen[NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS] = {0};
	uint32_t count = 0;
	if (r->activeCapacity > NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS || r->takenCount > r->activeCapacity) return 0;
	for (uint32_t i = 0; i < r->activeCapacity; ++i) {
		const struct NativeCanonicalWorldMineRegistryV1Record *record = &r->records[i];
		if (record->state == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FREE) {
			if (record->queueRank != NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_NO_RANK) return 0;
		} else if (record->state == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_TAKEN) {
			if (record->queueRank >= r->takenCount || seen[record->queueRank] != 0) return 0;
			seen[record->queueRank] = 1; ++count;
		} else return 0;
	}
	for (uint32_t i = 0; i < r->takenCount; ++i) if (seen[i] == 0) return 0;
	if (count != r->takenCount) return 0;
	/* The inactive tail has no canonical representation, including in memory. */
	for (uint32_t i = r->activeCapacity; i < NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS; ++i)
		if (r->records[i].state != 0 || r->records[i].queueRank != 0) return 0;
	return 1;
}

void NativeCanonicalWorldMineRegistryV1_Init(struct NativeCanonicalWorldMineRegistryV1 *r)
{
	if (!r) return;
	memset(r, 0, sizeof(*r));
	r->version = NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_VERSION;
}

int NativeCanonicalWorldMineRegistryV1_Validate(const struct NativeCanonicalWorldMineRegistryV1 *r)
{
	if (!r || r->version != NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_VERSION ||
		(r->flags & ~NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE) != 0) return 0;
	if (r->flags == 0) {
		struct NativeCanonicalWorldMineRegistryV1 exact;
		NativeCanonicalWorldMineRegistryV1_Init(&exact);
		return memcmp(r, &exact, sizeof(exact)) == 0;
	}
	return AvailableValid(r);
}

int NativeCanonicalWorldMineRegistryV1_FromFacts(struct NativeCanonicalWorldMineRegistryV1 *r,
	const struct NativeCanonicalWorldMineRegistryV1Facts *facts)
{
	struct NativeCanonicalWorldMineRegistryV1 candidate;
	if (!r || !facts || (facts->flags & ~NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE) != 0) return 0;
	NativeCanonicalWorldMineRegistryV1_Init(&candidate);
	if (facts->flags == 0) {
		struct NativeCanonicalWorldMineRegistryV1Facts exact = {0};
		return memcmp(facts, &exact, sizeof(exact)) == 0 ? ((*r = candidate), 1) : 0;
	}
	if (facts->activeCapacity > NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS) return 0;
	candidate.flags = facts->flags; candidate.activeCapacity = facts->activeCapacity; candidate.takenCount = facts->takenCount;
	/* Tail fixture storage is intentionally ignored; it is not canonical state. */
	memcpy(candidate.records, facts->records, (size_t)candidate.activeCapacity * sizeof(candidate.records[0]));
	if (!NativeCanonicalWorldMineRegistryV1_Validate(&candidate)) return 0;
	*r = candidate;
	return 1;
}

size_t NativeCanonicalWorldMineRegistryV1_EncodedSize(const struct NativeCanonicalWorldMineRegistryV1 *r)
{
	if (!NativeCanonicalWorldMineRegistryV1_Validate(r)) return 0;
	return NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_HEADER_BYTES + (r->flags ? (size_t)r->activeCapacity * 2u : 0u);
}

int NativeCanonicalWorldMineRegistryV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalWorldMineRegistryV1 *r)
{
	struct NativeCodecWriter w; size_t size = NativeCanonicalWorldMineRegistryV1_EncodedSize(r);
	if (!writer || size == 0 || writer->failed || writer->offset > writer->capacity || size > writer->capacity - writer->offset) return 0;
	w = *writer;
	if (!NativeCodecWriter_WriteU32(&w, r->version) || !NativeCodecWriter_WriteU32(&w, r->flags) ||
		!NativeCodecWriter_WriteU32(&w, r->activeCapacity) || !NativeCodecWriter_WriteU32(&w, r->takenCount)) return 0;
	for (uint32_t i = 0; i < r->activeCapacity; ++i)
		if (!NativeCodecWriter_WriteU8(&w, r->records[i].state) || !NativeCodecWriter_WriteU8(&w, r->records[i].queueRank)) return 0;
	*writer = w; return 1;
}

int NativeCanonicalWorldMineRegistryV1_Decode(struct NativeCodecReader *reader, struct NativeCanonicalWorldMineRegistryV1 *r)
{
	struct NativeCodecReader rd; struct NativeCanonicalWorldMineRegistryV1 candidate; size_t size;
	if (!reader || !r || reader->failed || reader->offset > reader->size || NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_HEADER_BYTES > reader->size - reader->offset) return 0;
	rd = *reader; NativeCanonicalWorldMineRegistryV1_Init(&candidate);
	if (!NativeCodecReader_ReadU32(&rd, &candidate.version) || !NativeCodecReader_ReadU32(&rd, &candidate.flags) ||
		!NativeCodecReader_ReadU32(&rd, &candidate.activeCapacity) || !NativeCodecReader_ReadU32(&rd, &candidate.takenCount)) return 0;
	if (candidate.flags == NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_FLAG_AVAILABLE && candidate.activeCapacity <= NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS)
		for (uint32_t i = 0; i < candidate.activeCapacity; ++i)
			if (!NativeCodecReader_ReadU8(&rd, &candidate.records[i].state) || !NativeCodecReader_ReadU8(&rd, &candidate.records[i].queueRank)) return 0;
	size = NativeCanonicalWorldMineRegistryV1_EncodedSize(&candidate);
	if (size == 0 || !NativeCanonicalWorldMineRegistryV1_Validate(&candidate)) return 0;
	*reader = rd; *r = candidate; return 1;
}

int NativeCanonicalWorldMineRegistryV1_Digest(const struct NativeCanonicalWorldMineRegistryV1 *r, uint64_t *digestOut)
{
	uint8_t bytes[NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_HEADER_BYTES + 2u * NATIVE_CANONICAL_WORLD_MINE_REGISTRY_V1_MAX_RECORDS];
	struct NativeCodecWriter writer; struct NativeCodecDigest64 digest;
	if (!digestOut) return 0;
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeCanonicalWorldMineRegistryV1_Encode(&writer, r)) return 0;
	NativeCodecDigest64_Init(&digest); NativeCodecDigest64_Update(&digest, bytes, writer.offset); *digestOut = digest.value;
	return 1;
}
