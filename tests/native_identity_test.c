#include "platform/native_canonical_state.h"
#include "platform/native_disc_image.h"
#include "platform/native_identity.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utime.h>

#if defined(_WIN32)
#include <direct.h>
#define NATIVE_IDENTITY_MKDIR(path) _mkdir(path)
#define NATIVE_IDENTITY_UTIME(path, times) _utime(path, times)
typedef struct _utimbuf NativeIdentityUtime;
#else
#include <unistd.h>
#define NATIVE_IDENTITY_MKDIR(path) mkdir(path, 0700)
#define NATIVE_IDENTITY_UTIME(path, times) utime(path, times)
typedef struct utimbuf NativeIdentityUtime;
#endif

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                         \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

#define NATIVE_IDENTITY_FIXTURE_A "native_identity_fixture_a"
#define NATIVE_IDENTITY_FIXTURE_B "native_identity_fixture_b"
#define NATIVE_IDENTITY_ASSETS_A  NATIVE_IDENTITY_FIXTURE_A "/assets"
#define NATIVE_IDENTITY_ASSETS_B  NATIVE_IDENTITY_FIXTURE_B "/assets"
#define NATIVE_IDENTITY_FILE_A    NATIVE_IDENTITY_ASSETS_A "/ctr-u.bin"
#define NATIVE_IDENTITY_FILE_B    NATIVE_IDENTITY_ASSETS_B "/ctr-u.bin"

static int WriteFixture(const char *path, int changedByte)
{
	uint8_t sector[2352];
	FILE *file = fopen(path, "wb");

	if (file == NULL)
	{
		return 0;
	}

	for (uint32_t lba = 0; lba <= 16; lba++)
	{
		memset(sector, 0, sizeof(sector));
		sector[0] = 0;
		memset(&sector[1], 0xff, 10);
		sector[11] = 0;
		sector[15] = 2;
		if ((lba == 0) && (changedByte != 0))
		{
			sector[24] = 0x5a;
		}
		if (lba == 16)
		{
			uint8_t *payload = &sector[24];

			payload[0] = 1;
			memcpy(&payload[1], "CD001", 5);
			payload[6] = 1;
			payload[156] = 34;
			payload[158] = 17;
			payload[166] = 1;
			payload[181] = 2;
			payload[188] = 1;
		}
		if (fwrite(sector, 1, sizeof(sector), file) != sizeof(sector))
		{
			fclose(file);
			return 0;
		}
	}

	return fclose(file) == 0;
}

static void CleanupFixtures(void)
{
	(void)remove(NATIVE_IDENTITY_FILE_A);
	(void)remove(NATIVE_IDENTITY_FILE_B);
	(void)rmdir(NATIVE_IDENTITY_ASSETS_A);
	(void)rmdir(NATIVE_IDENTITY_ASSETS_B);
	(void)rmdir(NATIVE_IDENTITY_FIXTURE_A);
	(void)rmdir(NATIVE_IDENTITY_FIXTURE_B);
}

static int PrepareFixtures(void)
{
	CleanupFixtures();
	if ((NATIVE_IDENTITY_MKDIR(NATIVE_IDENTITY_FIXTURE_A) != 0) || (NATIVE_IDENTITY_MKDIR(NATIVE_IDENTITY_ASSETS_A) != 0) ||
	    (NATIVE_IDENTITY_MKDIR(NATIVE_IDENTITY_FIXTURE_B) != 0) || (NATIVE_IDENTITY_MKDIR(NATIVE_IDENTITY_ASSETS_B) != 0))
	{
		return 0;
	}

	return WriteFixture(NATIVE_IDENTITY_FILE_A, 0) && WriteFixture(NATIVE_IDENTITY_FILE_B, 0);
}

static int StatesEqual(const struct NativeCanonicalStateV1 *left, const struct NativeCanonicalStateV1 *right)
{
	return (left->frameNumber == right->frameNumber) &&
	       (memcmp(left->identity.build, right->identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(left->identity.content, right->identity.content, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(left->domainDigests, right->domainDigests, sizeof(left->domainDigests)) == 0) &&
	       (left->combinedDigest == right->combinedDigest);
}

static int TestIdentityProvider(void)
{
	static const uint8_t expectedBuild[NATIVE_IDENTITY_DIGEST_BYTES] = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	};
	struct NativeIdentityV1 first;
	struct NativeIdentityV1 second;
	struct NativeIdentityV1 changed;
	struct NativeIdentityV1 untouched;
	uint8_t contentFirst[NATIVE_IDENTITY_DIGEST_BYTES];
	uint8_t contentSecond[NATIVE_IDENTITY_DIGEST_BYTES];
	NativeIdentityUtime times;

	CHECK(PrepareFixtures());
	CHECK(NativeIdentity_BuildKnown());
	CHECK(NativeDiscImage_Init(NATIVE_IDENTITY_ASSETS_A));
	CHECK(!NativeDiscImage_ContentIdentityReady());
	CHECK(NativeIdentity_Get(&first));
	CHECK(NativeDiscImage_ContentIdentityReady());
	CHECK(memcmp(first.build, expectedBuild, sizeof(expectedBuild)) == 0);
	CHECK(NativeDiscImage_GetContentIdentity(contentFirst));

	first.content[0] ^= 1;
	CHECK(NativeIdentity_Get(&second));
	CHECK(memcmp(second.content, contentFirst, sizeof(contentFirst)) == 0);

	times.actime = 1;
	times.modtime = 2;
	CHECK(NATIVE_IDENTITY_UTIME(NATIVE_IDENTITY_FILE_B, &times) == 0);
	CHECK(NativeDiscImage_Init(NATIVE_IDENTITY_ASSETS_B));
	CHECK(!NativeDiscImage_ContentIdentityReady());
	CHECK(NativeDiscImage_GetContentIdentity(contentSecond));
	CHECK(NativeDiscImage_ContentIdentityReady());
	CHECK(memcmp(contentFirst, contentSecond, sizeof(contentFirst)) == 0);

	CHECK(NativeDiscImage_Init(NATIVE_IDENTITY_ASSETS_A));
	CHECK(!NativeDiscImage_ContentIdentityReady());
	CHECK(WriteFixture(NATIVE_IDENTITY_FILE_B, 1));
	CHECK(NativeDiscImage_Init(NATIVE_IDENTITY_ASSETS_B));
	CHECK(NativeIdentity_Get(&changed));
	CHECK(memcmp(changed.content, contentFirst, sizeof(changed.content)) != 0);

	memset(&untouched, 0xa5, sizeof(untouched));
	CHECK(!NativeDiscImage_Init("native_identity_missing/assets"));
	CHECK(!NativeIdentity_Get(&untouched));
	for (uint32_t i = 0; i < sizeof(untouched); i++)
	{
		CHECK(((const uint8_t *)&untouched)[i] == 0xa5);
	}

	CleanupFixtures();
	return 0;
}

static int TestCanonicalIdentityIntegration(void)
{
	uint8_t bytes[292];
	struct NativeIdentityV1 identity;
	struct NativeCanonicalStateV1 state;
	struct NativeCanonicalStateV1 decoded;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	CHECK(PrepareFixtures());
	CHECK(NativeDiscImage_Init(NATIVE_IDENTITY_ASSETS_A));
	CHECK(NativeIdentity_Get(&identity));
	NativeCanonicalStateV1_Init(&state);
	state.frameNumber = 77;
	state.identity = identity;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&state));
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeCanonicalStateV1_Encode(&writer, &state));
	NativeCanonicalStateV1_Init(&decoded);
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCanonicalStateV1_Decode(&reader, &identity, &decoded));
	CHECK(StatesEqual(&state, &decoded));
	CleanupFixtures();
	return 0;
}

int main(void)
{
	if ((TestIdentityProvider() != 0) || (TestCanonicalIdentityIntegration() != 0))
	{
		CleanupFixtures();
		return 1;
	}

	puts("native_identity_test: passed");
	return 0;
}
