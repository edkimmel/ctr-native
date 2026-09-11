#ifndef PLATFORM_NATIVE_CANONICAL_CODEC_H
#define PLATFORM_NATIVE_CANONICAL_CODEC_H

#include <stddef.h>
#include <stdint.h>

/*
 * Explicit binary primitives for canonical state and replay v2.  This API
 * never serializes native objects: callers write each schema field directly.
 */

#define NATIVE_CANONICAL_STATE_SCHEMA_VERSION UINT32_C(1)
#define NATIVE_CANONICAL_REPLAY_FORMAT_VERSION UINT32_C(2)

#define NATIVE_CANONICAL_DOMAIN_COUNT 6u

enum NativeCanonicalDomainID
{
	NATIVE_CANONICAL_DOMAIN_CONTROL = 1,
	NATIVE_CANONICAL_DOMAIN_RNG,
	NATIVE_CANONICAL_DOMAIN_INPUT,
	NATIVE_CANONICAL_DOMAIN_DRIVERS,
	NATIVE_CANONICAL_DOMAIN_WORLD,
	NATIVE_CANONICAL_DOMAIN_TOPOLOGY
};

extern const uint32_t NativeCanonicalDomainOrder[NATIVE_CANONICAL_DOMAIN_COUNT];

struct NativeCodecDigest64
{
	uint64_t value;
};

struct NativeCodecWriter
{
	uint8_t *data;
	size_t capacity;
	size_t offset;
	int failed;
	struct NativeCodecDigest64 *digest;
};

struct NativeCodecReader
{
	const uint8_t *data;
	size_t size;
	size_t offset;
	int failed;
};

void NativeCodecDigest64_Init(struct NativeCodecDigest64 *digest);
void NativeCodecDigest64_Update(struct NativeCodecDigest64 *digest, const void *bytes, size_t size);

/* A non-NULL digest is updated only for bytes successfully written. */
void NativeCodecWriter_Init(struct NativeCodecWriter *writer, void *data, size_t capacity, struct NativeCodecDigest64 *digest);
int NativeCodecWriter_Ok(const struct NativeCodecWriter *writer);
size_t NativeCodecWriter_Size(const struct NativeCodecWriter *writer);
int NativeCodecWriter_WriteBytes(struct NativeCodecWriter *writer, const void *bytes, size_t size);
int NativeCodecWriter_WriteU8(struct NativeCodecWriter *writer, uint8_t value);
int NativeCodecWriter_WriteU16(struct NativeCodecWriter *writer, uint16_t value);
int NativeCodecWriter_WriteU32(struct NativeCodecWriter *writer, uint32_t value);
int NativeCodecWriter_WriteU64(struct NativeCodecWriter *writer, uint64_t value);
int NativeCodecWriter_WriteS8(struct NativeCodecWriter *writer, int8_t value);
int NativeCodecWriter_WriteS16(struct NativeCodecWriter *writer, int16_t value);
int NativeCodecWriter_WriteS32(struct NativeCodecWriter *writer, int32_t value);
int NativeCodecWriter_WriteS64(struct NativeCodecWriter *writer, int64_t value);

void NativeCodecReader_Init(struct NativeCodecReader *reader, const void *data, size_t size);
int NativeCodecReader_Ok(const struct NativeCodecReader *reader);
size_t NativeCodecReader_Remaining(const struct NativeCodecReader *reader);
int NativeCodecReader_ReadBytes(struct NativeCodecReader *reader, void *bytes, size_t size);
int NativeCodecReader_ReadU8(struct NativeCodecReader *reader, uint8_t *value);
int NativeCodecReader_ReadU16(struct NativeCodecReader *reader, uint16_t *value);
int NativeCodecReader_ReadU32(struct NativeCodecReader *reader, uint32_t *value);
int NativeCodecReader_ReadU64(struct NativeCodecReader *reader, uint64_t *value);
int NativeCodecReader_ReadS8(struct NativeCodecReader *reader, int8_t *value);
int NativeCodecReader_ReadS16(struct NativeCodecReader *reader, int16_t *value);
int NativeCodecReader_ReadS32(struct NativeCodecReader *reader, int32_t *value);
int NativeCodecReader_ReadS64(struct NativeCodecReader *reader, int64_t *value);

#endif
