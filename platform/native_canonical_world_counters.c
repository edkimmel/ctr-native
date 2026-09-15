#include "platform/native_canonical_world_counters.h"

#include <string.h>

void NativeCanonicalWorldCountersV1_Init(struct NativeCanonicalWorldCountersV1 *counters)
{
	if (!counters) return;
	memset(counters, 0, sizeof(*counters));
	counters->version = NATIVE_CANONICAL_WORLD_COUNTERS_V1_VERSION;
}

int NativeCanonicalWorldCountersV1_Validate(const struct NativeCanonicalWorldCountersV1 *counters)
{
	if (!counters || counters->version != NATIVE_CANONICAL_WORLD_COUNTERS_V1_VERSION ||
		(counters->flags & ~NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE) != 0) return 0;
	if (counters->flags == 0) return counters->activeBombMissileCount == 0;
	return counters->activeBombMissileCount <= NATIVE_CANONICAL_WORLD_COUNTERS_V1_MAX_ACTIVE_BOMB_MISSILE_COUNT;
}

int NativeCanonicalWorldCountersV1_FromFacts(struct NativeCanonicalWorldCountersV1 *counters,
	const struct NativeCanonicalWorldCountersV1Facts *facts)
{
	struct NativeCanonicalWorldCountersV1 candidate;
	if (!counters || !facts || (facts->flags & ~NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE) != 0) return 0;
	NativeCanonicalWorldCountersV1_Init(&candidate);
	if (facts->flags == 0) {
		if (facts->activeBombMissileCount != 0) return 0;
	} else {
		candidate.flags = facts->flags;
		candidate.activeBombMissileCount = facts->activeBombMissileCount;
	}
	if (!NativeCanonicalWorldCountersV1_Validate(&candidate)) return 0;
	*counters = candidate;
	return 1;
}

size_t NativeCanonicalWorldCountersV1_EncodedSize(const struct NativeCanonicalWorldCountersV1 *counters)
{
	return NativeCanonicalWorldCountersV1_Validate(counters) ? NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES : 0;
}

int NativeCanonicalWorldCountersV1_Encode(struct NativeCodecWriter *writer,
	const struct NativeCanonicalWorldCountersV1 *counters)
{
	struct NativeCodecWriter candidate;
	if (!writer || !NativeCanonicalWorldCountersV1_Validate(counters) || writer->failed ||
		writer->offset > writer->capacity || NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES > writer->capacity - writer->offset) return 0;
	candidate = *writer;
	if (!NativeCodecWriter_WriteU32(&candidate, counters->version) ||
		!NativeCodecWriter_WriteU32(&candidate, counters->flags) ||
		!NativeCodecWriter_WriteU32(&candidate, counters->activeBombMissileCount)) return 0;
	*writer = candidate;
	return 1;
}

int NativeCanonicalWorldCountersV1_Decode(struct NativeCodecReader *reader,
	struct NativeCanonicalWorldCountersV1 *counters)
{
	struct NativeCodecReader candidateReader;
	struct NativeCanonicalWorldCountersV1 candidate;
	if (!reader || !counters || reader->failed || reader->offset > reader->size ||
		NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES > reader->size - reader->offset) return 0;
	candidateReader = *reader;
	NativeCanonicalWorldCountersV1_Init(&candidate);
	if (!NativeCodecReader_ReadU32(&candidateReader, &candidate.version) ||
		!NativeCodecReader_ReadU32(&candidateReader, &candidate.flags) ||
		!NativeCodecReader_ReadU32(&candidateReader, &candidate.activeBombMissileCount) ||
		!NativeCanonicalWorldCountersV1_Validate(&candidate)) return 0;
	*reader = candidateReader;
	*counters = candidate;
	return 1;
}

int NativeCanonicalWorldCountersV1_Digest(const struct NativeCanonicalWorldCountersV1 *counters,
	uint64_t *digestOut)
{
	uint8_t bytes[NATIVE_CANONICAL_WORLD_COUNTERS_V1_ENCODED_BYTES];
	struct NativeCodecWriter writer;
	struct NativeCodecDigest64 digest;
	if (!digestOut) return 0;
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeCanonicalWorldCountersV1_Encode(&writer, counters)) return 0;
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, writer.offset);
	*digestOut = digest.value;
	return 1;
}
