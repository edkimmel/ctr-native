#include "platform/native_sha256.h"

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

static int HashMatches(const void *bytes, size_t size, const uint8_t expected[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t actual[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeSha256 sha;

	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, bytes, size);
	NativeSha256_Final(&sha, actual);
	return memcmp(actual, expected, sizeof(actual)) == 0;
}

static int TestStandardVectors(void)
{
	static const uint8_t empty[] = {
		0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
		0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
	};
	static const uint8_t abc[] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
	};
	static const uint8_t longVector[] = {
		0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
		0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
	};

	CHECK(HashMatches("", 0, empty));
	CHECK(HashMatches("abc", 3, abc));
	CHECK(HashMatches("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, longVector));
	return 0;
}

static int TestBinaryAndChunkBoundaries(void)
{
	static const uint8_t binaryExpected[] = {
		0x40, 0xaf, 0xf2, 0xe9, 0xd2, 0xd8, 0x92, 0x2e, 0x47, 0xaf, 0xd4, 0x64, 0x8e, 0x69, 0x67, 0x49,
		0x71, 0x58, 0x78, 0x5f, 0xbd, 0x1d, 0xa8, 0x70, 0xe7, 0x11, 0x02, 0x66, 0xbf, 0x94, 0x48, 0x80,
	};
	static const uint8_t boundaryExpected[] = {
		0x4b, 0xfd, 0x2c, 0x8b, 0x6f, 0x1e, 0xec, 0x7a, 0x2a, 0xfe, 0xb4, 0x8b, 0x93, 0x4e, 0xe4, 0xb2,
		0x69, 0x41, 0x82, 0x02, 0x7e, 0x6d, 0x0f, 0xc0, 0x75, 0x07, 0x4f, 0x2f, 0xab, 0xb3, 0x17, 0x81,
	};
	uint8_t binary[256];
	uint8_t boundary[65];
	uint8_t actual[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeSha256 sha;

	for (uint32_t i = 0; i < sizeof(binary); i++)
	{
		binary[i] = (uint8_t)i;
	}
	for (uint32_t i = 0; i < sizeof(boundary); i++)
	{
		boundary[i] = (uint8_t)i;
	}

	CHECK(HashMatches(binary, sizeof(binary), binaryExpected));
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, boundary, 55);
	NativeSha256_Update(&sha, &boundary[55], 1);
	NativeSha256_Update(&sha, &boundary[56], 7);
	NativeSha256_Update(&sha, &boundary[63], 1);
	NativeSha256_Update(&sha, &boundary[64], 1);
	NativeSha256_Final(&sha, actual);
	CHECK(memcmp(actual, boundaryExpected, sizeof(actual)) == 0);
	return 0;
}

int main(void)
{
	if ((TestStandardVectors() != 0) || (TestBinaryAndChunkBoundaries() != 0))
	{
		return 1;
	}

	puts("native_sha256_test: passed");
	return 0;
}
