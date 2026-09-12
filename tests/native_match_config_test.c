#include "platform/native_match_config.h"

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

static void FillValid(struct NativeMatchConfigV1 *config)
{
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = UINT32_C(0x01020304);
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 7;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
	for (uint8_t i = 0; i <= 5; i++)
	{
		config->slots[i].characterID = i;
		config->slots[i].difficulty = i < 2 ? 2 : 3;
	}
}

static int Encode(const struct NativeMatchConfigV1 *config, uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES, NULL);
	return NativeMatchConfigV1_Encode(&writer, config) && (NativeCodecWriter_Size(&writer) == NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES);
}

static int TestGoldenBytesRoundTripAndDigest(void)
{
	static const uint8_t expected[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES] = {
		0x4e, 0x4d, 0x43, 0x31, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x04, 0x03, 0x02, 0x01, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0xcc, 0xbb, 0xaa, 0x99,
		0x07, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xef, 0xcd, 0xab, 0x89,
		0x67, 0x45, 0x23, 0x01, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00,
		0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
		0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
		0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
		0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
		0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x01, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x01, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
		0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	};
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 decoded;
	uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecReader reader;

	FillValid(&config);
	CHECK(NativeMatchConfigV1_EncodedSize() == sizeof(expected));
	CHECK(Encode(&config, bytes));
	CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
	memset(&decoded, 0xcc, sizeof(decoded));
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeMatchConfigV1_Decode(&reader, &decoded));
	CHECK(reader.offset == sizeof(bytes));
	CHECK(memcmp(&decoded, &config, sizeof(config)) == 0);
	CHECK(NativeMatchConfigV1_Digest(&config, digest));
	/* SHA-256 over the complete 256-byte golden record. */
	CHECK(strcmp(NATIVE_MATCH_CONFIG_V1_DIGEST_ALGORITHM_NAME, "SHA-256") == 0);
	CHECK(memcmp(digest, "\x39\xb1\x98\xb2\x83\xbd\x3a\x37\x50\x11\xbf\xca\xe7\x54\xe1\x43"
	                     "\x25\x9d\x12\x9d\xda\x2e\xeb\x8a\x67\xfd\x63\x6d\xa4\x5a\x3b\xac", sizeof(digest)) == 0);
	return 0;
}

static int TestValidationBoundariesAndReserved(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 before;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	FillValid(&config);
	CHECK(NativeMatchConfigV1_Validate(&config));
	config.lapCount = UINT32_MAX;
	config.tickRateNumerator = UINT32_MAX;
	config.tickRateDenominator = UINT32_MAX;
	config.masterSeed = 0;
	CHECK(NativeMatchConfigV1_Validate(&config));

#define REJECT_ZERO(field)                                                                                                                  \
	do                                                                                                                                  \
	{                                                                                                                                   \
		FillValid(&config);                                                                                                             \
		config.field = 0;                                                                                                               \
		CHECK(!NativeMatchConfigV1_Validate(&config));                                                                                  \
	} while (0)
	REJECT_ZERO(lapCount);
	REJECT_ZERO(tickRateNumerator);
	REJECT_ZERO(tickRateDenominator);
	REJECT_ZERO(rngDerivationVersion);
	REJECT_ZERO(canonicalSchemaVersion);
	REJECT_ZERO(replayFormatVersion);
	REJECT_ZERO(protocolVersion);
#undef REJECT_ZERO

	FillValid(&config);
	config.configurationVersion++;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.profile++;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.rngDerivationVersion++;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.canonicalSchemaVersion++;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.replayFormatVersion++;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.protocolVersion++;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	memset(config.buildIdentity, 0, sizeof(config.buildIdentity));
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	memset(config.contentIdentity, 0, sizeof(config.contentIdentity));
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	memset(config.botRulesDigest, 0, sizeof(config.botRulesDigest));
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	for (size_t i = 0; i < sizeof(config.reserved); i++)
	{
		config.reserved[i] = 1;
		CHECK(!NativeMatchConfigV1_Validate(&config));
		config.reserved[i] = 0;
	}
	for (size_t slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		for (size_t i = 0; i < sizeof(config.slots[slot].reserved); i++)
		{
			config.slots[slot].reserved[i] = 1;
			CHECK(!NativeMatchConfigV1_Validate(&config));
			config.slots[slot].reserved[i] = 0;
		}
	}
	FillValid(&config);
	config.slots[6].characterID = 1;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.slots[7].difficulty = 1;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.slots[0].role = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	CHECK(!NativeMatchConfigV1_Validate(&config));
	FillValid(&config);
	config.slots[2].initialLifecycle = NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED;
	CHECK(!NativeMatchConfigV1_Validate(&config));

	FillValid(&config);
	memset(digest, 0xcc, sizeof(digest));
	before = config;
	config.reserved[0] = 1;
	CHECK(!NativeMatchConfigV1_Digest(&config, digest));
	for (size_t i = 0; i < sizeof(digest); i++) CHECK(digest[i] == 0xcc);
	config = before;
	CHECK(!NativeMatchConfigV1_Validate(NULL));
	CHECK(!NativeMatchConfigV1_Digest(NULL, digest));
	CHECK(!NativeMatchConfigV1_Digest(&config, NULL));
	return 0;
}

static int TestDecodeFailuresAreTransactional(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 output;
	struct NativeMatchConfigV1 before;
	uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES + 1];
	struct NativeCodecReader reader;

	FillValid(&config);
	CHECK(Encode(&config, bytes));
	memset(&output, 0x5a, sizeof(output));
	before = output;
	for (size_t size = 0; size < NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES; size++)
	{
		NativeCodecReader_Init(&reader, bytes, size);
		CHECK(!NativeMatchConfigV1_Decode(&reader, &output));
		CHECK(reader.offset == 0);
		CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	}
	bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES] = 0;
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(!NativeMatchConfigV1_Decode(&reader, &output));
	CHECK(reader.offset == 0);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);

#define REJECT_BYTE(index, value)                                                                                                          \
	do                                                                                                                                  \
	{                                                                                                                                   \
		CHECK(Encode(&config, bytes));                                                                                                  \
		bytes[(index)] = (value);                                                                                                       \
		NativeCodecReader_Init(&reader, bytes, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES);                                                   \
		CHECK(!NativeMatchConfigV1_Decode(&reader, &output));                                                                           \
		CHECK(reader.offset == 0);                                                                                                      \
		CHECK(memcmp(&output, &before, sizeof(output)) == 0);                                                                            \
	} while (0)
	REJECT_BYTE(0, 0);
	REJECT_BYTE(4, 0xff);
	REJECT_BYTE(8, 2);
	REJECT_BYTE(12, 2);
	REJECT_BYTE(228, 1);
	REJECT_BYTE(136, 1);
#undef REJECT_BYTE

	NativeCodecReader_Init(&reader, bytes, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES);
	CHECK(!NativeMatchConfigV1_Decode(&reader, NULL));
	CHECK(reader.offset == 0);
	CHECK(!NativeMatchConfigV1_Decode(NULL, &output));
	return 0;
}

static int TestDecodeFromNonzeroOffset(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 output;
	uint8_t framed[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES + 3] = {0xaa, 0xbb, 0xcc};
	struct NativeCodecReader reader;

	FillValid(&config);
	CHECK(Encode(&config, &framed[3]));
	NativeCodecReader_Init(&reader, framed, sizeof(framed));
	reader.offset = 3;
	CHECK(NativeMatchConfigV1_Decode(&reader, &output));
	CHECK(reader.offset == sizeof(framed));
	CHECK(memcmp(&output, &config, sizeof(config)) == 0);
	return 0;
}

static int TestEncodeFailuresAreTransactional(void)
{
	struct NativeMatchConfigV1 config;
	uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES + 4];
	uint8_t before[sizeof(bytes)];
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	uint64_t digestBefore;

	FillValid(&config);
	memset(bytes, 0xcc, sizeof(bytes));
	memcpy(before, bytes, sizeof(bytes));
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES - 1, &digest);
	digestBefore = digest.value;
	CHECK(!NativeMatchConfigV1_Encode(&writer, &config));
	CHECK(writer.offset == 0 && writer.failed == 0);
	CHECK(digest.value == digestBefore);
	CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(NativeCodecWriter_WriteU32(&writer, UINT32_C(0x78563412)));
	memcpy(before, bytes, sizeof(bytes));
	digestBefore = digest.value;
	config.reserved[0] = 1;
	CHECK(!NativeMatchConfigV1_Encode(&writer, &config));
	CHECK(writer.offset == 4 && writer.failed == 0);
	CHECK(digest.value == digestBefore);
	CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
	CHECK(!NativeMatchConfigV1_Encode(NULL, &config));
	CHECK(!NativeMatchConfigV1_Encode(&writer, NULL));
	return 0;
}

static int ExpectedTransition(uint8_t role, uint8_t current, uint8_t next)
{
	if (role == NATIVE_MATCH_SLOT_ROLE_INACTIVE)
	{
		return current == NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE && next == current;
	}
	if (role == NATIVE_MATCH_SLOT_ROLE_BOT)
	{
		return (current == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE &&
		        (next == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE || next == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED)) ||
		       (current == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED && next == current);
	}
	if (role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN || role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN)
	{
		return (current == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE && next >= NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE &&
		        next <= NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED) ||
		       (current == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED &&
		        (next == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED || next == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED)) ||
		       (current == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED && next == current);
	}
	return 0;
}

static int TestLifecycleTransitions(void)
{
	for (uint8_t role = 0; role <= 4; role++)
	{
		for (uint8_t current = 0; current <= 4; current++)
		{
			for (uint8_t next = 0; next <= 4; next++)
			{
				uint8_t lifecycle = current;
				const int expected = ExpectedTransition(role, current, next);
				CHECK(NativeMatchSlotLifecycle_CanTransition(role, current, next) == expected);
				CHECK(NativeMatchSlotLifecycle_Transition(role, &lifecycle, next) == expected);
				CHECK(lifecycle == (expected ? next : current));
			}
		}
	}
	CHECK(!NativeMatchSlotLifecycle_Transition(NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, NULL,
	                                          NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED));
	return 0;
}

static int TestCanonicalOwnershipIsLocalRoleInvariant(void)
{
	struct NativeMatchConfigV1 first;
	struct NativeMatchConfigV1 second;
	uint8_t before[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t after[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t slot = 0xff;

	/* Join order is absent from construction: every peer gets this exact map. */
	FillValid(&first);
	FillValid(&second);
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
	CHECK(first.slots[0].role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(first.slots[1].role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	for (uint8_t i = 2; i <= 5; i++) CHECK(first.slots[i].role == NATIVE_MATCH_SLOT_ROLE_BOT);
	for (uint8_t i = 6; i <= 7; i++) CHECK(first.slots[i].role == NATIVE_MATCH_SLOT_ROLE_INACTIVE);

	CHECK(Encode(&first, before));
	/* Looking up the opposite local role changes routing only, never bytes. */
	CHECK(NativeMatchConfigV1_FindRoleSlot(&first, NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slot) && slot == 1);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&first, NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &slot) && slot == 0);
	CHECK(Encode(&first, after));
	CHECK(memcmp(before, after, sizeof(before)) == 0);
	slot = 0x5a;
	CHECK(!NativeMatchConfigV1_FindRoleSlot(&first, NATIVE_MATCH_SLOT_ROLE_BOT, &slot) && slot == 0x5a);
	CHECK(!NativeMatchConfigV1_FindRoleSlot(NULL, NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &slot));
	CHECK(!NativeMatchConfigV1_FindRoleSlot(&first, NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, NULL));
	return 0;
}

int main(void)
{
	if (TestGoldenBytesRoundTripAndDigest() != 0 || TestValidationBoundariesAndReserved() != 0 ||
	    TestDecodeFailuresAreTransactional() != 0 || TestDecodeFromNonzeroOffset() != 0 || TestEncodeFailuresAreTransactional() != 0 ||
	    TestLifecycleTransitions() != 0 || TestCanonicalOwnershipIsLocalRoleInvariant() != 0)
	{
		return 1;
	}

	puts("native_match_config_test: passed");
	return 0;
}
