#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

#include "platform/native_canonical_state.h"
#include "platform/native_disc_image.h"
#include "platform/native_identity.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utime.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
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
			fprintf(stderr, "%s:%d: check failed: %s\\n", __FILE__, __LINE__, #expression);                                         \
			return 0;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

#define NATIVE_IDENTITY_PATH_CAP 1024

struct NativeIdentityFixture
{
	char root[NATIVE_IDENTITY_PATH_CAP];
	char directoryA[NATIVE_IDENTITY_PATH_CAP];
	char directoryB[NATIVE_IDENTITY_PATH_CAP];
	char assetsA[NATIVE_IDENTITY_PATH_CAP];
	char assetsB[NATIVE_IDENTITY_PATH_CAP];
	char fileA[NATIVE_IDENTITY_PATH_CAP];
	char fileB[NATIVE_IDENTITY_PATH_CAP];
	int rootOwned;
	int directoryAOwned;
	int directoryBOwned;
	int assetsAOwned;
	int assetsBOwned;
	int fileAOwned;
	int fileBOwned;
};

static int NativeIdentityPathJoin(char *output, size_t outputSize, const char *left, const char *right)
{
	int written = snprintf(output, outputSize, "%s/%s", left, right);

	return (written >= 0) && ((size_t)written < outputSize);
}

static int NativeIdentityNewRoot(struct NativeIdentityFixture *fixture)
{
#if defined(_WIN32)
	char tempPath[NATIVE_IDENTITY_PATH_CAP];
	DWORD written = GetTempPathA(sizeof(tempPath), tempPath);

	if ((written == 0) || (written >= sizeof(tempPath)) || !GetTempFileNameA(tempPath, "nid", 0, fixture->root) ||
	    (remove(fixture->root) != 0) || (NATIVE_IDENTITY_MKDIR(fixture->root) != 0))
	{
		return 0;
	}
#else
	char pattern[] = "/tmp/ctr-native-identity-XXXXXX";
	char *root = mkdtemp(pattern);

	if ((root == NULL) || (snprintf(fixture->root, sizeof(fixture->root), "%s", root) < 0) ||
	    (strlen(root) >= sizeof(fixture->root)))
	{
		return 0;
	}
#endif

	fixture->rootOwned = 1;
	return 1;
}

static int NativeIdentityFixtureCleanup(struct NativeIdentityFixture *fixture)
{
	int success = 1;

	/* The tested file remains retained after hashing. Release it before removal. */
	NativeDiscImage_Shutdown();
	if (fixture->fileBOwned && (remove(fixture->fileB) != 0))
	{
		success = 0;
	}
	if (fixture->fileAOwned && (remove(fixture->fileA) != 0))
	{
		success = 0;
	}
	fixture->fileBOwned = 0;
	fixture->fileAOwned = 0;
	if (fixture->assetsBOwned && (rmdir(fixture->assetsB) != 0))
	{
		success = 0;
	}
	if (fixture->assetsAOwned && (rmdir(fixture->assetsA) != 0))
	{
		success = 0;
	}
	fixture->assetsBOwned = 0;
	fixture->assetsAOwned = 0;
	if (fixture->directoryBOwned && (rmdir(fixture->directoryB) != 0))
	{
		success = 0;
	}
	if (fixture->directoryAOwned && (rmdir(fixture->directoryA) != 0))
	{
		success = 0;
	}
	fixture->directoryBOwned = 0;
	fixture->directoryAOwned = 0;
	if (fixture->rootOwned && (rmdir(fixture->root) != 0))
	{
		success = 0;
	}
	fixture->rootOwned = 0;
	return success;
}

static int NativeIdentityWriteFixture(const char *path, int changedByte)
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
			(void)fclose(file);
			return 0;
		}
	}

	return fclose(file) == 0;
}

static int NativeIdentityFixturePrepare(struct NativeIdentityFixture *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	NativeDiscImage_Shutdown();
	if (!NativeIdentityNewRoot(fixture) ||
	    !NativeIdentityPathJoin(fixture->directoryA, sizeof(fixture->directoryA), fixture->root, "a") ||
	    !NativeIdentityPathJoin(fixture->directoryB, sizeof(fixture->directoryB), fixture->root, "b") ||
	    !NativeIdentityPathJoin(fixture->assetsA, sizeof(fixture->assetsA), fixture->directoryA, "assets") ||
	    !NativeIdentityPathJoin(fixture->assetsB, sizeof(fixture->assetsB), fixture->directoryB, "assets") ||
	    !NativeIdentityPathJoin(fixture->fileA, sizeof(fixture->fileA), fixture->assetsA, "ctr-u.bin") ||
	    !NativeIdentityPathJoin(fixture->fileB, sizeof(fixture->fileB), fixture->assetsB, "ctr-u.bin"))
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	if (NATIVE_IDENTITY_MKDIR(fixture->directoryA) != 0)
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	fixture->directoryAOwned = 1;
	if (NATIVE_IDENTITY_MKDIR(fixture->directoryB) != 0)
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	fixture->directoryBOwned = 1;
	if (NATIVE_IDENTITY_MKDIR(fixture->assetsA) != 0)
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	fixture->assetsAOwned = 1;
	if (NATIVE_IDENTITY_MKDIR(fixture->assetsB) != 0)
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	fixture->assetsBOwned = 1;
	if (!NativeIdentityWriteFixture(fixture->fileA, 0))
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	fixture->fileAOwned = 1;
	if (!NativeIdentityWriteFixture(fixture->fileB, 0))
	{
		(void)NativeIdentityFixtureCleanup(fixture);
		return 0;
	}
	fixture->fileBOwned = 1;
	return 1;
}

static int NativeIdentityStatesEqual(const struct NativeCanonicalStateV1 *left, const struct NativeCanonicalStateV1 *right)
{
	return (left->frameNumber == right->frameNumber) &&
	       (memcmp(left->identity.build, right->identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(left->identity.content, right->identity.content, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(left->domainDigests, right->domainDigests, sizeof(left->domainDigests)) == 0) &&
	       (left->combinedDigest == right->combinedDigest);
}

static int NativeIdentityTestProvider(struct NativeIdentityFixture *fixture)
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

	CHECK(NativeIdentityFixturePrepare(fixture));
	CHECK(NativeIdentity_BuildKnown());
	CHECK(NativeDiscImage_Init(fixture->assetsA));
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
	CHECK(NATIVE_IDENTITY_UTIME(fixture->fileB, &times) == 0);
	CHECK(NativeDiscImage_Init(fixture->assetsB));
	CHECK(!NativeDiscImage_ContentIdentityReady());
	CHECK(NativeDiscImage_GetContentIdentity(contentSecond));
	CHECK(NativeDiscImage_ContentIdentityReady());
	CHECK(memcmp(contentFirst, contentSecond, sizeof(contentFirst)) == 0);

	CHECK(NativeDiscImage_Init(fixture->assetsA));
	CHECK(!NativeDiscImage_ContentIdentityReady());
	CHECK(NativeIdentityWriteFixture(fixture->fileB, 1));
	CHECK(NativeDiscImage_Init(fixture->assetsB));
	CHECK(NativeIdentity_Get(&changed));
	CHECK(memcmp(changed.content, contentFirst, sizeof(changed.content)) != 0);

	memset(&untouched, 0xa5, sizeof(untouched));
	CHECK(!NativeDiscImage_Init("native_identity_missing/assets"));
	CHECK(!NativeIdentity_Get(&untouched));
	for (uint32_t i = 0; i < sizeof(untouched); i++)
	{
		CHECK(((const uint8_t *)&untouched)[i] == 0xa5);
	}

	NativeDiscImage_Shutdown();
	NativeDiscImage_Shutdown();
	CHECK(!NativeDiscImage_ContentIdentityReady());
	CHECK(!NativeDiscImage_GetContentIdentity(contentSecond));
	return 1;
}

static int NativeIdentityTestCanonicalIntegration(struct NativeIdentityFixture *fixture)
{
	uint8_t bytes[296];
	struct NativeIdentityV1 identity;
	struct NativeCanonicalStateV1 state;
	struct NativeCanonicalStateV1 decoded;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	CHECK(NativeIdentityFixturePrepare(fixture));
	CHECK(NativeDiscImage_Init(fixture->assetsA));
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
	CHECK(NativeIdentityStatesEqual(&state, &decoded));
	return 1;
}

int main(void)
{
	struct NativeIdentityFixture provider = {0};
	struct NativeIdentityFixture canonical = {0};
	int success = NativeIdentityTestProvider(&provider);

	if (!NativeIdentityFixtureCleanup(&provider))
	{
		success = 0;
	}
	if (success && !NativeIdentityTestCanonicalIntegration(&canonical))
	{
		success = 0;
	}
	if (!NativeIdentityFixtureCleanup(&canonical))
	{
		success = 0;
	}
	NativeDiscImage_Shutdown();
	if (!success)
	{
		return 1;
	}

	puts("native_identity_test: passed");
	return 0;
}
