#include "platform/native_canonical_codec.h"

#include <limits.h>
#include <string.h>

#define NATIVE_CODEC_FNV1A64_OFFSET UINT64_C(0xcbf29ce484222325)
#define NATIVE_CODEC_FNV1A64_PRIME  UINT64_C(0x00000100000001b3)

const uint32_t NativeCanonicalDomainOrder[NATIVE_CANONICAL_DOMAIN_COUNT] = {
	NATIVE_CANONICAL_DOMAIN_CONTROL,
	NATIVE_CANONICAL_DOMAIN_RNG,
	NATIVE_CANONICAL_DOMAIN_INPUT,
	NATIVE_CANONICAL_DOMAIN_DRIVERS,
	NATIVE_CANONICAL_DOMAIN_WORLD,
	NATIVE_CANONICAL_DOMAIN_TOPOLOGY,
};

static int NativeCodecWriter_CanWrite(struct NativeCodecWriter *writer, size_t size)
{
	if ((writer == NULL) || (writer->failed != 0) || (writer->offset > writer->capacity) || ((writer->data == NULL) && (writer->capacity != 0)) ||
	    (size > writer->capacity - writer->offset))
	{
		if (writer != NULL)
		{
			writer->failed = 1;
		}
		return 0;
	}

	return 1;
}

static int NativeCodecReader_CanRead(struct NativeCodecReader *reader, size_t size)
{
	if ((reader == NULL) || (reader->failed != 0) || (reader->offset > reader->size) || ((reader->data == NULL) && (reader->size != 0)) ||
	    (size > reader->size - reader->offset))
	{
		if (reader != NULL)
		{
			reader->failed = 1;
		}
		return 0;
	}

	return 1;
}

static int8_t NativeCodec_S8FromBits(uint8_t value)
{
	if (value <= INT8_MAX)
	{
		return (int8_t)value;
	}

	return (int8_t)(INT8_MIN + (int8_t)(value - UINT8_C(0x80)));
}

static int16_t NativeCodec_S16FromBits(uint16_t value)
{
	if (value <= INT16_MAX)
	{
		return (int16_t)value;
	}

	return (int16_t)(INT16_MIN + (int16_t)(value - UINT16_C(0x8000)));
}

static int32_t NativeCodec_S32FromBits(uint32_t value)
{
	if (value <= INT32_MAX)
	{
		return (int32_t)value;
	}

	return INT32_MIN + (int32_t)(value - UINT32_C(0x80000000));
}

static int64_t NativeCodec_S64FromBits(uint64_t value)
{
	if (value <= INT64_MAX)
	{
		return (int64_t)value;
	}

	return INT64_MIN + (int64_t)(value - UINT64_C(0x8000000000000000));
}

void NativeCodecDigest64_Init(struct NativeCodecDigest64 *digest)
{
	if (digest != NULL)
	{
		digest->value = NATIVE_CODEC_FNV1A64_OFFSET;
	}
}

void NativeCodecDigest64_Update(struct NativeCodecDigest64 *digest, const void *bytes, size_t size)
{
	const uint8_t *data = (const uint8_t *)bytes;

	if ((digest == NULL) || ((data == NULL) && (size != 0)))
	{
		return;
	}

	for (size_t i = 0; i < size; i++)
	{
		digest->value ^= data[i];
		digest->value *= NATIVE_CODEC_FNV1A64_PRIME;
	}
}

void NativeCodecWriter_Init(struct NativeCodecWriter *writer, void *data, size_t capacity, struct NativeCodecDigest64 *digest)
{
	if (writer == NULL)
	{
		return;
	}

	writer->data = (uint8_t *)data;
	writer->capacity = capacity;
	writer->offset = 0;
	writer->failed = ((data == NULL) && (capacity != 0)) ? 1 : 0;
	writer->digest = digest;
}

int NativeCodecWriter_Ok(const struct NativeCodecWriter *writer)
{
	return (writer != NULL) && (writer->failed == 0);
}

size_t NativeCodecWriter_Size(const struct NativeCodecWriter *writer)
{
	return writer != NULL ? writer->offset : 0;
}

int NativeCodecWriter_WriteBytes(struct NativeCodecWriter *writer, const void *bytes, size_t size)
{
	if ((bytes == NULL) && (size != 0))
	{
		if (writer != NULL)
		{
			writer->failed = 1;
		}
		return 0;
	}
	if (!NativeCodecWriter_CanWrite(writer, size))
	{
		return 0;
	}

	if (size != 0)
	{
		memcpy(&writer->data[writer->offset], bytes, size);
		NativeCodecDigest64_Update(writer->digest, bytes, size);
		writer->offset += size;
	}
	return 1;
}

int NativeCodecWriter_WriteU8(struct NativeCodecWriter *writer, uint8_t value)
{
	return NativeCodecWriter_WriteBytes(writer, &value, sizeof(value));
}

int NativeCodecWriter_WriteU16(struct NativeCodecWriter *writer, uint16_t value)
{
	const uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};

	return NativeCodecWriter_WriteBytes(writer, bytes, sizeof(bytes));
}

int NativeCodecWriter_WriteU32(struct NativeCodecWriter *writer, uint32_t value)
{
	const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};

	return NativeCodecWriter_WriteBytes(writer, bytes, sizeof(bytes));
}

int NativeCodecWriter_WriteU64(struct NativeCodecWriter *writer, uint64_t value)
{
	const uint8_t bytes[8] = {
		(uint8_t)value,
		(uint8_t)(value >> 8),
		(uint8_t)(value >> 16),
		(uint8_t)(value >> 24),
		(uint8_t)(value >> 32),
		(uint8_t)(value >> 40),
		(uint8_t)(value >> 48),
		(uint8_t)(value >> 56),
	};

	return NativeCodecWriter_WriteBytes(writer, bytes, sizeof(bytes));
}

int NativeCodecWriter_WriteS8(struct NativeCodecWriter *writer, int8_t value)
{
	return NativeCodecWriter_WriteU8(writer, (uint8_t)value);
}

int NativeCodecWriter_WriteS16(struct NativeCodecWriter *writer, int16_t value)
{
	return NativeCodecWriter_WriteU16(writer, (uint16_t)value);
}

int NativeCodecWriter_WriteS32(struct NativeCodecWriter *writer, int32_t value)
{
	return NativeCodecWriter_WriteU32(writer, (uint32_t)value);
}

int NativeCodecWriter_WriteS64(struct NativeCodecWriter *writer, int64_t value)
{
	return NativeCodecWriter_WriteU64(writer, (uint64_t)value);
}

void NativeCodecReader_Init(struct NativeCodecReader *reader, const void *data, size_t size)
{
	if (reader == NULL)
	{
		return;
	}

	reader->data = (const uint8_t *)data;
	reader->size = size;
	reader->offset = 0;
	reader->failed = ((data == NULL) && (size != 0)) ? 1 : 0;
}

int NativeCodecReader_Ok(const struct NativeCodecReader *reader)
{
	return (reader != NULL) && (reader->failed == 0);
}

size_t NativeCodecReader_Remaining(const struct NativeCodecReader *reader)
{
	if ((reader == NULL) || (reader->failed != 0) || (reader->offset > reader->size))
	{
		return 0;
	}

	return reader->size - reader->offset;
}

int NativeCodecReader_ReadBytes(struct NativeCodecReader *reader, void *bytes, size_t size)
{
	if ((bytes == NULL) && (size != 0))
	{
		if (reader != NULL)
		{
			reader->failed = 1;
		}
		return 0;
	}
	if (!NativeCodecReader_CanRead(reader, size))
	{
		return 0;
	}

	if (size != 0)
	{
		memcpy(bytes, &reader->data[reader->offset], size);
		reader->offset += size;
	}
	return 1;
}

int NativeCodecReader_ReadU8(struct NativeCodecReader *reader, uint8_t *value)
{
	return NativeCodecReader_ReadBytes(reader, value, sizeof(*value));
}

int NativeCodecReader_ReadU16(struct NativeCodecReader *reader, uint16_t *value)
{
	uint8_t bytes[2];

	if ((value == NULL) || !NativeCodecReader_ReadBytes(reader, bytes, sizeof(bytes)))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
	return 1;
}

int NativeCodecReader_ReadU32(struct NativeCodecReader *reader, uint32_t *value)
{
	uint8_t bytes[4];

	if ((value == NULL) || !NativeCodecReader_ReadBytes(reader, bytes, sizeof(bytes)))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return 1;
}

int NativeCodecReader_ReadU64(struct NativeCodecReader *reader, uint64_t *value)
{
	uint8_t bytes[8];

	if ((value == NULL) || !NativeCodecReader_ReadBytes(reader, bytes, sizeof(bytes)))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8) | ((uint64_t)bytes[2] << 16) | ((uint64_t)bytes[3] << 24) |
	         ((uint64_t)bytes[4] << 32) | ((uint64_t)bytes[5] << 40) | ((uint64_t)bytes[6] << 48) | ((uint64_t)bytes[7] << 56);
	return 1;
}

int NativeCodecReader_ReadS8(struct NativeCodecReader *reader, int8_t *value)
{
	uint8_t bits;

	if ((value == NULL) || !NativeCodecReader_ReadU8(reader, &bits))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = NativeCodec_S8FromBits(bits);
	return 1;
}

int NativeCodecReader_ReadS16(struct NativeCodecReader *reader, int16_t *value)
{
	uint16_t bits;

	if ((value == NULL) || !NativeCodecReader_ReadU16(reader, &bits))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = NativeCodec_S16FromBits(bits);
	return 1;
}

int NativeCodecReader_ReadS32(struct NativeCodecReader *reader, int32_t *value)
{
	uint32_t bits;

	if ((value == NULL) || !NativeCodecReader_ReadU32(reader, &bits))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = NativeCodec_S32FromBits(bits);
	return 1;
}

int NativeCodecReader_ReadS64(struct NativeCodecReader *reader, int64_t *value)
{
	uint64_t bits;

	if ((value == NULL) || !NativeCodecReader_ReadU64(reader, &bits))
	{
		if ((value == NULL) && (reader != NULL))
		{
			reader->failed = 1;
		}
		return 0;
	}
	*value = NativeCodec_S64FromBits(bits);
	return 1;
}
