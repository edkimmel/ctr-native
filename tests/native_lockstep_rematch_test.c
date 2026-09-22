#include "platform/native_lockstep_rematch.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SENTINEL_BYTE 0xa5u

static void FillIdentity(struct NativeMatchConfigV1 *config, uint32_t trackID, uint64_t masterSeed)
{
	config->trackID = trackID;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = masterSeed;
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		if (config->slots[i].role != NATIVE_MATCH_SLOT_ROLE_INACTIVE)
		{
			config->slots[i].characterID = (uint8_t)(0x30u + i);
			config->slots[i].difficulty = (uint8_t)(0x40u + i);
		}
	}
}

static void FillSentinel(struct NativeMatchConfigV1 *config)
{
	memset(config, SENTINEL_BYTE, sizeof(*config));
}

static int IsSentinel(const struct NativeMatchConfigV1 *config)
{
	static struct NativeMatchConfigV1 sentinel;
	static int sentinelInit = 0;

	if (!sentinelInit)
	{
		memset(&sentinel, SENTINEL_BYTE, sizeof(sentinel));
		sentinelInit = 1;
	}
	return memcmp(config, &sentinel, sizeof(sentinel)) == 0;
}

static int CheckRematchedIdentity(const struct NativeMatchConfigV1 *previous, const struct NativeMatchConfigV1 *next)
{
	CHECK(next->profile == previous->profile);
	CHECK(next->trackID == previous->trackID);
	CHECK(next->gameMode1 == previous->gameMode1);
	CHECK(next->gameMode2 == previous->gameMode2);
	CHECK(next->rules == previous->rules);
	CHECK(next->lapCount == previous->lapCount);
	CHECK(next->tickRateNumerator == previous->tickRateNumerator);
	CHECK(next->tickRateDenominator == previous->tickRateDenominator);
	CHECK(memcmp(next->buildIdentity, previous->buildIdentity, sizeof(next->buildIdentity)) == 0);
	CHECK(memcmp(next->contentIdentity, previous->contentIdentity, sizeof(next->contentIdentity)) == 0);
	CHECK(memcmp(next->botRulesDigest, previous->botRulesDigest, sizeof(next->botRulesDigest)) == 0);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(next->slots[i].role == previous->slots[i].role);
		CHECK(next->slots[i].initialLifecycle == previous->slots[i].initialLifecycle);
		CHECK(next->slots[i].characterID == previous->slots[i].characterID);
		CHECK(next->slots[i].difficulty == previous->slots[i].difficulty);
	}
	return 0;
}

/*
 * A valid two-cab previous config rematched with a different seed validates,
 * preserves the fixture identity fields and per-slot character/difficulty
 * across every slot (human and bot), sets the new seed, and round-trips
 * through NativeMatchConfigV1_Encode/_Digest.
 */
static int TestTwoCabRematch(void)
{
	struct NativeMatchConfigV1 previous;
	struct NativeMatchConfigV1 next;
	uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	const uint64_t oldSeed = UINT64_C(0x0123456789abcdef);
	const uint64_t newSeed = UINT64_C(0xfedcba9876543210);

	NativeMatchConfigV1_InitArcadeTwoCab(&previous);
	FillIdentity(&previous, UINT32_C(0x01020304), oldSeed);
	CHECK(NativeMatchConfigV1_Validate(&previous) == 1);

	CHECK(NativeLockstepRematch_BuildConfig(&previous, newSeed, &next) == 1);
	CHECK(NativeMatchConfigV1_Validate(&next) == 1);
	CHECK(next.masterSeed == newSeed);
	CHECK(CheckRematchedIdentity(&previous, &next) == 0);

	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(next.slots[i].reserved[0] == 0);
		CHECK(next.slots[i].reserved[1] == 0);
		CHECK(next.slots[i].reserved[2] == 0);
		CHECK(next.slots[i].reserved[3] == 0);
	}

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeMatchConfigV1_Encode(&writer, &next) == 1);
	CHECK(NativeMatchConfigV1_Digest(&next, digest) == 1);
	return 0;
}

/* The same coverage for a valid one-cab previous config. */
static int TestOneCabRematch(void)
{
	struct NativeMatchConfigV1 previous;
	struct NativeMatchConfigV1 next;
	uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	const uint64_t oldSeed = UINT64_C(0x1111222233334444);
	const uint64_t newSeed = UINT64_C(0x5555666677778888);

	NativeMatchConfigV1_InitArcadeOneCab(&previous);
	FillIdentity(&previous, UINT32_C(0x0a0b0c0d), oldSeed);
	CHECK(NativeMatchConfigV1_Validate(&previous) == 1);

	CHECK(NativeLockstepRematch_BuildConfig(&previous, newSeed, &next) == 1);
	CHECK(NativeMatchConfigV1_Validate(&next) == 1);
	CHECK(next.masterSeed == newSeed);
	CHECK(CheckRematchedIdentity(&previous, &next) == 0);

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeMatchConfigV1_Encode(&writer, &next) == 1);
	CHECK(NativeMatchConfigV1_Digest(&next, digest) == 1);
	return 0;
}

/* An unchanged seed is rejected and next is left byte-for-byte untouched. */
static int TestSameSeedRejected(void)
{
	struct NativeMatchConfigV1 previous;
	struct NativeMatchConfigV1 next;

	NativeMatchConfigV1_InitArcadeTwoCab(&previous);
	FillIdentity(&previous, UINT32_C(0x01020304), UINT64_C(0x0123456789abcdef));
	CHECK(NativeMatchConfigV1_Validate(&previous) == 1);

	FillSentinel(&next);
	CHECK(NativeLockstepRematch_BuildConfig(&previous, previous.masterSeed, &next) == 0);
	CHECK(IsSentinel(&next) == 1);
	return 0;
}

/*
 * NULL previous, NULL next, and an invalidated previous config (lapCount
 * left at 0) are all rejected; where next is non-NULL it is left untouched.
 */
static int TestInvalidInputsRejected(void)
{
	struct NativeMatchConfigV1 previous;
	struct NativeMatchConfigV1 invalidPrevious;
	struct NativeMatchConfigV1 next;

	NativeMatchConfigV1_InitArcadeTwoCab(&previous);
	FillIdentity(&previous, UINT32_C(0x01020304), UINT64_C(0x0123456789abcdef));
	CHECK(NativeMatchConfigV1_Validate(&previous) == 1);

	FillSentinel(&next);
	CHECK(NativeLockstepRematch_BuildConfig(NULL, UINT64_C(1), &next) == 0);
	CHECK(IsSentinel(&next) == 1);

	CHECK(NativeLockstepRematch_BuildConfig(&previous, UINT64_C(1), NULL) == 0);

	NativeMatchConfigV1_InitArcadeTwoCab(&invalidPrevious);
	FillIdentity(&invalidPrevious, UINT32_C(0x01020304), UINT64_C(0x0123456789abcdef));
	invalidPrevious.lapCount = 0;
	CHECK(NativeMatchConfigV1_Validate(&invalidPrevious) == 0);

	FillSentinel(&next);
	CHECK(NativeLockstepRematch_BuildConfig(&invalidPrevious, UINT64_C(1), &next) == 0);
	CHECK(IsSentinel(&next) == 1);
	return 0;
}

int main(void)
{
	CHECK(TestTwoCabRematch() == 0);
	CHECK(TestOneCabRematch() == 0);
	CHECK(TestSameSeedRejected() == 0);
	CHECK(TestInvalidInputsRejected() == 0);
	return 0;
}
