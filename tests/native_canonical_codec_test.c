#include "platform/native_canonical_codec.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                         \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static int TestGoldenVectorAndRoundTrip(void)
{
	static const uint8_t expected[] = {
		0x7f, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0xff, 0x00,
		0x80, 0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xde, 0xad, 0xbe, 0xef,
	};
	uint8_t bytes[sizeof(expected)] = {0};
	uint8_t span[4] = {0};
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	int8_t s8;
	int16_t s16;
	int32_t s32;
	int64_t s64;
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(NativeCodecWriter_WriteU8(&writer, UINT8_C(0x7f)));
	CHECK(NativeCodecWriter_WriteU16(&writer, UINT16_C(0x1234)));
	CHECK(NativeCodecWriter_WriteU32(&writer, UINT32_C(0x89abcdef)));
	CHECK(NativeCodecWriter_WriteU64(&writer, UINT64_C(0x0123456789abcdef)));
	CHECK(NativeCodecWriter_WriteS8(&writer, -1));
	CHECK(NativeCodecWriter_WriteS16(&writer, INT16_MIN));
	CHECK(NativeCodecWriter_WriteS32(&writer, -2));
	CHECK(NativeCodecWriter_WriteS64(&writer, INT64_MIN));
	CHECK(NativeCodecWriter_WriteBytes(&writer, "\xde\xad\xbe\xef", sizeof(span)));
	CHECK(NativeCodecWriter_Ok(&writer));
	CHECK(NativeCodecWriter_Size(&writer) == sizeof(expected));
	CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
	CHECK(digest.value == UINT64_C(0x63883eda174d0b36));

	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCodecReader_ReadU8(&reader, &u8) && (u8 == UINT8_C(0x7f)));
	CHECK(NativeCodecReader_ReadU16(&reader, &u16) && (u16 == UINT16_C(0x1234)));
	CHECK(NativeCodecReader_ReadU32(&reader, &u32) && (u32 == UINT32_C(0x89abcdef)));
	CHECK(NativeCodecReader_ReadU64(&reader, &u64) && (u64 == UINT64_C(0x0123456789abcdef)));
	CHECK(NativeCodecReader_ReadS8(&reader, &s8) && (s8 == -1));
	CHECK(NativeCodecReader_ReadS16(&reader, &s16) && (s16 == INT16_MIN));
	CHECK(NativeCodecReader_ReadS32(&reader, &s32) && (s32 == -2));
	CHECK(NativeCodecReader_ReadS64(&reader, &s64) && (s64 == INT64_MIN));
	CHECK(NativeCodecReader_ReadBytes(&reader, span, sizeof(span)));
	CHECK(memcmp(span, "\xde\xad\xbe\xef", sizeof(span)) == 0);
	CHECK(NativeCodecReader_Ok(&reader));
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	return 0;
}

static int TestBoundsAndStickyFailure(void)
{
	uint8_t writeBytes[3] = {0xcc, 0xcc, 0xcc};
	uint8_t readBytes[] = {0x34, 0x12, 0xaa};
	uint16_t u16 = 0;
	uint8_t u8 = 0x5a;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	NativeCodecWriter_Init(&writer, writeBytes, sizeof(writeBytes), NULL);
	CHECK(NativeCodecWriter_WriteU16(&writer, UINT16_C(0x1234)));
	CHECK(!NativeCodecWriter_WriteU16(&writer, UINT16_C(0xabcd)));
	CHECK(!NativeCodecWriter_Ok(&writer));
	CHECK(NativeCodecWriter_Size(&writer) == 2);
	CHECK((writeBytes[0] == 0x34) && (writeBytes[1] == 0x12) && (writeBytes[2] == 0xcc));
	CHECK(!NativeCodecWriter_WriteU8(&writer, UINT8_C(0xff)));
	CHECK(writeBytes[2] == 0xcc);

	NativeCodecReader_Init(&reader, readBytes, sizeof(readBytes));
	CHECK(NativeCodecReader_ReadU16(&reader, &u16) && (u16 == UINT16_C(0x1234)));
	CHECK(!NativeCodecReader_ReadU16(&reader, &u16));
	CHECK(!NativeCodecReader_Ok(&reader));
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	CHECK(!NativeCodecReader_ReadU8(&reader, &u8));
	CHECK(u8 == 0x5a);

	NativeCodecWriter_Init(&writer, NULL, 1, NULL);
	CHECK(!NativeCodecWriter_Ok(&writer));
	NativeCodecReader_Init(&reader, NULL, 1);
	CHECK(!NativeCodecReader_Ok(&reader));
	return 0;
}

static int TestNullArgumentsAndForgedOffsets(void)
{
	uint8_t bytes[8] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
	uint8_t source[8] = {0};
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(!NativeCodecWriter_WriteBytes(&writer, NULL, 1));
	CHECK(!NativeCodecWriter_Ok(&writer));
	CHECK(NativeCodecWriter_Size(&writer) == 0);
	CHECK(digest.value == UINT64_C(0xcbf29ce484222325));
	CHECK(bytes[0] == 0xcc);

	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadBytes(&reader, NULL, 1));
	CHECK(!NativeCodecReader_Ok(&reader));
	CHECK(reader.offset == 0);

	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadU8(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadU16(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadU32(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadU64(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadS8(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadS16(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadS32(&reader, NULL));
	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadS64(&reader, NULL));

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	writer.offset = writer.capacity + 1;
	CHECK(!NativeCodecWriter_WriteU8(&writer, 0));
	CHECK(!NativeCodecWriter_Ok(&writer));
	CHECK(bytes[0] == 0xcc);

	NativeCodecReader_Init(&reader, source, sizeof(source));
	reader.offset = reader.size + 1;
	CHECK(!NativeCodecReader_ReadU8(&reader, &source[0]));
	CHECK(!NativeCodecReader_Ok(&reader));
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	return 0;
}

static int TestOversizedRequestsAndDigestFailureStability(void)
{
	uint8_t bytes[2] = {0xcc, 0xcc};
	uint8_t source[2] = {0};
	uint8_t output[2] = {0xcc, 0xcc};
	uint64_t digestBeforeFailure;
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(NativeCodecWriter_WriteU8(&writer, UINT8_C(0x42)));
	digestBeforeFailure = digest.value;
	CHECK(!NativeCodecWriter_WriteBytes(&writer, source, SIZE_MAX));
	CHECK(!NativeCodecWriter_Ok(&writer));
	CHECK(NativeCodecWriter_Size(&writer) == 1);
	CHECK(bytes[0] == 0x42);
	CHECK(bytes[1] == 0xcc);
	CHECK(digest.value == digestBeforeFailure);
	CHECK(!NativeCodecWriter_WriteU8(&writer, UINT8_C(0x99)));
	CHECK(digest.value == digestBeforeFailure);

	NativeCodecReader_Init(&reader, source, sizeof(source));
	CHECK(!NativeCodecReader_ReadBytes(&reader, output, SIZE_MAX));
	CHECK(!NativeCodecReader_Ok(&reader));
	CHECK(reader.offset == 0);
	CHECK((output[0] == 0xcc) && (output[1] == 0xcc));
	return 0;
}

static int TestSignedValues(void)
{
	static const uint8_t expected[] = {
		0x01, 0x7f, 0xff,
		0x01, 0x00, 0xff, 0x7f, 0xff, 0xff,
		0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};
	uint8_t bytes[sizeof(expected)] = {0};
	int8_t s8;
	int16_t s16;
	int32_t s32;
	int64_t s64;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeCodecWriter_WriteS8(&writer, 1));
	CHECK(NativeCodecWriter_WriteS8(&writer, INT8_MAX));
	CHECK(NativeCodecWriter_WriteS8(&writer, -1));
	CHECK(NativeCodecWriter_WriteS16(&writer, 1));
	CHECK(NativeCodecWriter_WriteS16(&writer, INT16_MAX));
	CHECK(NativeCodecWriter_WriteS16(&writer, -1));
	CHECK(NativeCodecWriter_WriteS32(&writer, 1));
	CHECK(NativeCodecWriter_WriteS32(&writer, INT32_MAX));
	CHECK(NativeCodecWriter_WriteS32(&writer, -1));
	CHECK(NativeCodecWriter_WriteS64(&writer, 1));
	CHECK(NativeCodecWriter_WriteS64(&writer, INT64_MAX));
	CHECK(NativeCodecWriter_WriteS64(&writer, -1));
	CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);

	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCodecReader_ReadS8(&reader, &s8) && (s8 == 1));
	CHECK(NativeCodecReader_ReadS8(&reader, &s8) && (s8 == INT8_MAX));
	CHECK(NativeCodecReader_ReadS8(&reader, &s8) && (s8 == -1));
	CHECK(NativeCodecReader_ReadS16(&reader, &s16) && (s16 == 1));
	CHECK(NativeCodecReader_ReadS16(&reader, &s16) && (s16 == INT16_MAX));
	CHECK(NativeCodecReader_ReadS16(&reader, &s16) && (s16 == -1));
	CHECK(NativeCodecReader_ReadS32(&reader, &s32) && (s32 == 1));
	CHECK(NativeCodecReader_ReadS32(&reader, &s32) && (s32 == INT32_MAX));
	CHECK(NativeCodecReader_ReadS32(&reader, &s32) && (s32 == -1));
	CHECK(NativeCodecReader_ReadS64(&reader, &s64) && (s64 == 1));
	CHECK(NativeCodecReader_ReadS64(&reader, &s64) && (s64 == INT64_MAX));
	CHECK(NativeCodecReader_ReadS64(&reader, &s64) && (s64 == -1));
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	return 0;
}

static int TestDigestAndSpan(void)
{
	uint8_t bytes[3] = {0};
	uint8_t copied[3] = {0};
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, "hel", 3);
	NativeCodecDigest64_Update(&digest, "lo", 2);
	CHECK(digest.value == UINT64_C(0xa430d84680aabd0b));
	CHECK(strcmp(NATIVE_CODEC_DIGEST64_ALGORITHM_NAME, "FNV-1a 64") == 0);
	NativeCodecDigest64_Update(&digest, NULL, 1);
	CHECK(digest.value == UINT64_C(0xa430d84680aabd0b));

	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(NativeCodecWriter_WriteBytes(&writer, "abc", sizeof(bytes)));
	CHECK(NativeCodecWriter_WriteBytes(&writer, NULL, 0));
	CHECK(digest.value == UINT64_C(0xe71fa2190541574b));
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCodecReader_ReadBytes(&reader, copied, sizeof(copied)));
	CHECK(memcmp(copied, "abc", sizeof(copied)) == 0);
	return 0;
}

static int TestDomainAndVersionInvariants(void)
{
	static const uint32_t expected[] = {
		NATIVE_CANONICAL_DOMAIN_CONTROL,
		NATIVE_CANONICAL_DOMAIN_RNG,
		NATIVE_CANONICAL_DOMAIN_INPUT,
		NATIVE_CANONICAL_DOMAIN_DRIVERS,
		NATIVE_CANONICAL_DOMAIN_WORLD,
		NATIVE_CANONICAL_DOMAIN_TOPOLOGY,
	};

	CHECK(NATIVE_CANONICAL_STATE_SCHEMA_VERSION == UINT32_C(1));
	CHECK(NATIVE_CANONICAL_REPLAY_FORMAT_VERSION == UINT32_C(2));
	CHECK(NATIVE_CANONICAL_DOMAIN_COUNT == (sizeof(expected) / sizeof(expected[0])));
	CHECK(memcmp(NativeCanonicalDomainOrder, expected, sizeof(expected)) == 0);
	return 0;
}

int main(void)
{
	if (TestGoldenVectorAndRoundTrip() != 0 || TestBoundsAndStickyFailure() != 0 || TestNullArgumentsAndForgedOffsets() != 0 ||
	    TestOversizedRequestsAndDigestFailureStability() != 0 || TestSignedValues() != 0 || TestDigestAndSpan() != 0 ||
	    TestDomainAndVersionInvariants() != 0)
	{
		return 1;
	}

	puts("native_canonical_codec_test: passed");
	return 0;
}
