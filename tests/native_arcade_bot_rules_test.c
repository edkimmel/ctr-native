#include "platform/native_arcade_bot_rules.h"

#include "platform/native_arcade_link_options.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_identity.h"
#include "platform/native_lockstep_rematch.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SENTINEL_BYTE 0xa5u
#define FIXTURE_SEED UINT64_C(0x4354524e41524331) /* "CTRNARC1" */

/*
 * The full V1 encoding, spelled out by hand from the header's offset table
 * (not produced by the module). Frozen: changing any byte changes the bot
 * rules digest every v1 config carries.
 */
static const uint8_t k_goldenEncoding[NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES] = {
	/* 0: tag "CTRN arcade bot rules v1" */
	0x43, 0x54, 0x52, 0x4e, 0x20, 0x61, 0x72, 0x63, 0x61, 0x64, 0x65, 0x20,
	0x62, 0x6f, 0x74, 0x20, 0x72, 0x75, 0x6c, 0x65, 0x73, 0x20, 0x76, 0x31,
	/* 24: rulesVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 28: profile ARCADE_TWO_CAB */
	0x01, 0x00, 0x00, 0x00,
	/* 32: slot roles CAB1, CAB2, BOT x4, INACTIVE x2 */
	0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00,
	/* 40: humanCount, driverCount, firstBotSlot, botCount */
	0x02, 0x06, 0x02, 0x04,
	/* 44: difficultyCount, difficulty table */
	0x03, 0x50, 0xa0, 0xf0,
	/* 48: difficultyPolicy, botCharacterPolicy, modePolicy */
	0x01, 0x01, 0x01,
	/* 51: aiSetCount, aiSetRacers */
	0x07, 0x04,
	/* 53: the seven 2P AI sets */
	0x06, 0x04, 0x02, 0x03, 0x00, 0x06, 0x03, 0x05, 0x00, 0x06, 0x01, 0x02, 0x00, 0x06,
	0x04, 0x07, 0x01, 0x02, 0x03, 0x05, 0x04, 0x07, 0x03, 0x05, 0x04, 0x07, 0x01, 0x02,
	/* 81: rngDerivationVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 85: seed stream tag MATCH_SETUP 0x4d415443 */
	0x43, 0x54, 0x41, 0x4d,
	/* 89: seedTargetCount, target codes */
	0x05, 0x01, 0x02, 0x03, 0x04, 0x05,
	/* 95: randomNumberMask 0xFFFF */
	0xff, 0xff, 0x00, 0x00,
	/* 99: advRngFallback0 0x30215400 */
	0x00, 0x54, 0x21, 0x30,
	/* 103: advRngFallback1 0x493583fe */
	0xfe, 0x83, 0x35, 0x49,
	/* 107: reservedStreamMask 0x3FF */
	0xff, 0x03, 0x00, 0x00,
};

/*
 * SHA-256 of k_goldenEncoding. Obtained independently of this module: the
 * 111 bytes above were written to a file with the shell's printf and hashed
 * with both `sha256sum` and `certutil -hashfile <file> SHA256` (identical
 * results); the module's own EncodeV1 output was also dumped once by a
 * throwaway program and hashed the same way (identical). The dumps were not
 * committed. Frozen.
 */
static const uint8_t k_goldenDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0x90, 0x22, 0x15, 0x4e, 0xab, 0x79, 0x3f, 0xb2, 0x5d, 0x0a, 0x2d, 0x3b, 0x0c, 0x78, 0x7d, 0x62,
	0xfd, 0xaf, 0x9a, 0xf4, 0x90, 0xb7, 0xe3, 0xf1, 0xd4, 0x8f, 0xbe, 0xc8, 0xd4, 0x06, 0x5e, 0xab,
};

/*
 * The full 1P V1 encoding (ARCADE_ONE_CAB), spelled out by hand from the
 * header's 1P offset table (not produced by the module). Frozen: changing any
 * byte changes the bot rules digest every ONE_CAB config carries.
 */
static const uint8_t k_golden1PEncoding[NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES] = {
	/* 0: tag "CTRN arcade bot rules 1P v1" */
	0x43, 0x54, 0x52, 0x4e, 0x20, 0x61, 0x72, 0x63, 0x61, 0x64, 0x65, 0x20, 0x62, 0x6f,
	0x74, 0x20, 0x72, 0x75, 0x6c, 0x65, 0x73, 0x20, 0x31, 0x50, 0x20, 0x76, 0x31,
	/* 27: rulesVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 31: profile ARCADE_ONE_CAB */
	0x02, 0x00, 0x00, 0x00,
	/* 35: slot roles CAB1, BOT x7 */
	0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	/* 43: humanCount, driverCount, firstBotSlot, botCount */
	0x01, 0x08, 0x01, 0x07,
	/* 47: difficultyCount, difficulty table */
	0x03, 0x50, 0xa0, 0xf0,
	/* 51: difficultyPolicy, botCharacterPolicy (retail 1P), modePolicy */
	0x01, 0x02, 0x01,
	/* 54: candidateCount, candidates in LOAD_Robots1P walk order */
	0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	/* 63: rngDerivationVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 67: seed stream tag MATCH_SETUP 0x4d415443 */
	0x43, 0x54, 0x41, 0x4d,
	/* 71: seedTargetCount, target codes */
	0x05, 0x01, 0x02, 0x03, 0x04, 0x05,
	/* 77: randomNumberMask 0xFFFF */
	0xff, 0xff, 0x00, 0x00,
	/* 81: advRngFallback0 0x30215400 */
	0x00, 0x54, 0x21, 0x30,
	/* 85: advRngFallback1 0x493583fe */
	0xfe, 0x83, 0x35, 0x49,
	/* 89: reservedStreamMask 0x3FF */
	0xff, 0x03, 0x00, 0x00,
};

/*
 * SHA-256 of k_golden1PEncoding,
 * 8d06649af8aa2594fbaca395aba3ebf1cce24689f8c193f5055f4441f1d8b0e3. Obtained
 * independently of this module: the 93 bytes above were written to a file
 * with the shell's printf and hashed with both `sha256sum` and
 * `certutil -hashfile <file> SHA256` (identical results); the module's own
 * Encode1PV1 output was also dumped once by a throwaway program and hashed
 * the same way (identical). The dumps were not committed. At review the
 * digest was cross-checked once more with `sha256sum` over the 93 bytes
 * hand-spelled in a printf format string (identical). Frozen.
 */
static const uint8_t k_golden1PDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0x8d, 0x06, 0x64, 0x9a, 0xf8, 0xaa, 0x25, 0x94, 0xfb, 0xac, 0xa3, 0x95, 0xab, 0xa3, 0xeb, 0xf1,
	0xcc, 0xe2, 0x46, 0x89, 0xf8, 0xc1, 0x93, 0xf5, 0x05, 0x5f, 0x44, 0x41, 0xf1, 0xd8, 0xb0, 0xe3,
};

/*
 * The first five NextU32 draws of the MATCH_SETUP stream (global slot) for
 * masterSeed FIXTURE_SEED, derivation version 1, and their mapping. Obtained
 * independently with a Perl (Digest::SHA, Math::BigInt) reference of the
 * CTRNRNG1 derivation and xoshiro256** (the high 32 bits of each draw):
 * 0xba18b7bd, 0x8daad348, 0x20dc1cc2, 0x1171c7fe, 0x0df7521c. Frozen.
 */
static const uint32_t k_goldenSetupDraws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT] = {
	UINT32_C(0xba18b7bd), UINT32_C(0x8daad348), UINT32_C(0x20dc1cc2), UINT32_C(0x1171c7fe), UINT32_C(0x0df7521c),
};
static const struct NativeArcadeRetailRngSeedsV1 k_goldenSeeds = {
	UINT32_C(0x0000b7bd), UINT32_C(0x8daad348), UINT32_C(0x20dc1cc2), UINT32_C(0x1171c7fe), UINT32_C(0x0df7521c),
};

static void FillCounting(uint8_t *bytes, size_t size, uint8_t start)
{
	for (size_t i = 0; i < size; i++)
	{
		bytes[i] = (uint8_t)(start + i);
	}
}

static int IsAllByte(const void *memory, size_t size, uint8_t value)
{
	const uint8_t *bytes = (const uint8_t *)memory;

	for (size_t i = 0; i < size; i++)
	{
		if (bytes[i] != value)
		{
			return 0;
		}
	}
	return 1;
}

/*
 * Field by field: NativeDeterministicRngStreamV1 has padding after
 * streamIndex, so a whole-struct memcmp would compare indeterminate bytes.
 */
static int StreamsEqual(const struct NativeDeterministicRngStreamV1 *a, const struct NativeDeterministicRngStreamV1 *b)
{
	for (uint32_t i = 0; i < 4u; i++)
	{
		if (a->state[i] != b->state[i])
		{
			return 0;
		}
	}
	return (a->tag == b->tag) && (a->stableSlot == b->stableSlot) && (a->streamIndex == b->streamIndex) &&
	       (a->drawCount == b->drawCount);
}

static int BanksEqual(const struct NativeDeterministicRngBankV1 *a, const struct NativeDeterministicRngBankV1 *b)
{
	if ((a->bankVersion != b->bankVersion) || (a->derivationVersion != b->derivationVersion) ||
	    (a->masterSeed != b->masterSeed))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		if (!StreamsEqual(&a->streams[i], &b->streams[i]))
		{
			return 0;
		}
	}
	return 1;
}

static int SeedsEqual(const struct NativeArcadeRetailRngSeedsV1 *a, const struct NativeArcadeRetailRngSeedsV1 *b)
{
	return (a->randomNumber == b->randomNumber) && (a->advRng0 == b->advRng0) && (a->advRng1 == b->advRng1) &&
	       (a->psxRandSeed == b->psxRandSeed) && (a->audioRNG == b->audioRNG);
}

/* Independent reference for the LOAD_Robots2P rule over the public AI set table. */
static uint32_t ReferenceFirstSetWithout(uint8_t first, uint8_t second)
{
	for (uint32_t set = 0; set < NATIVE_MATCH_SELECT_AI_SET_COUNT; set++)
	{
		int holds = 0;

		for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
		{
			const uint8_t character = NativeMatchSelect_AiSetRacer(set, racer);

			if ((character == first) || (character == second))
			{
				holds = 1;
			}
		}
		if (!holds)
		{
			return set;
		}
	}
	return NATIVE_MATCH_SELECT_AI_SET_COUNT;
}

static int TestTables(void)
{
	uint32_t count = 0;

	CHECK(NativeArcadeBotRules_DifficultyAt(0) == NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY);
	CHECK(NativeArcadeBotRules_DifficultyAt(1) == NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM);
	CHECK(NativeArcadeBotRules_DifficultyAt(2) == NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD);
	CHECK(NativeArcadeBotRules_DifficultyAt(0) == 0x50u);
	CHECK(NativeArcadeBotRules_DifficultyAt(1) == 0xa0u);
	CHECK(NativeArcadeBotRules_DifficultyAt(2) == 0xf0u);
	CHECK(NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY == 0xa0u);
	CHECK(NativeArcadeBotRules_DifficultyAt(NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT) == 0u);
	CHECK(NativeArcadeBotRules_DifficultyAt(UINT32_MAX) == 0u);

	for (uint32_t value = 0; value < 0x200u; value++)
	{
		const int expected = (value == 0x50u) || (value == 0xa0u) || (value == 0xf0u);

		CHECK(NativeArcadeBotRules_IsDifficulty(value) == expected);
		count += (uint32_t)NativeArcadeBotRules_IsDifficulty(value);
	}
	CHECK(count == NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT);
	/* No truncation to a byte: 0x150 is not 0x50. */
	CHECK(NativeArcadeBotRules_IsDifficulty(0x150u) == 0);
	CHECK(NativeArcadeBotRules_IsDifficulty(UINT32_C(0xffffff50)) == 0);
	CHECK(NativeArcadeBotRules_IsDifficulty(UINT32_MAX) == 0);
	return 0;
}

static int TestEncoding(void)
{
	uint8_t buffer[NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES + 16u];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reference[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	struct NativeCodecDigest64 fnvWriter;
	struct NativeCodecDigest64 fnvReference;
	struct NativeMatchConfigV1 twoCab;
	struct NativeSha256 sha;

	CHECK(NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES == 111u);
	CHECK(NativeArcadeBotRules_EncodedSizeV1() == NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES);
	CHECK(sizeof(NATIVE_ARCADE_BOT_RULES_V1_TAG) - 1u == 24u);
	CHECK(memcmp(k_goldenEncoding, NATIVE_ARCADE_BOT_RULES_V1_TAG, 24u) == 0);

	/* The whole encoding, byte for byte, at offset 0. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 1);
	CHECK(NativeCodecWriter_Ok(&writer));
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES);
	CHECK(memcmp(buffer, k_goldenEncoding, sizeof(k_goldenEncoding)) == 0);
	CHECK(IsAllByte(&buffer[NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES], 16u, SENTINEL_BYTE));

	/* Roles and AI sets in the encoding are the live tables, not copies. */
	memset(&twoCab, 0, sizeof(twoCab));
	NativeMatchConfigV1_InitArcadeTwoCab(&twoCab);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(buffer[32u + i] == twoCab.slots[i].role);
	}
	for (uint32_t set = 0; set < NATIVE_MATCH_SELECT_AI_SET_COUNT; set++)
	{
		for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
		{
			CHECK(buffer[53u + (set * NATIVE_MATCH_SELECT_AI_SET_RACERS) + racer] ==
			      NativeMatchSelect_AiSetRacer(set, racer));
		}
	}

	/* At a nonzero offset, after other bytes, with a running digest. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecDigest64_Init(&fnvWriter);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &fnvWriter);
	CHECK(NativeCodecWriter_WriteU32(&writer, UINT32_C(0x01020304)) == 1);
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 1);
	CHECK(NativeCodecWriter_Size(&writer) == 4u + NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES);
	CHECK(memcmp(&buffer[4], k_goldenEncoding, sizeof(k_goldenEncoding)) == 0);
	NativeCodecDigest64_Init(&fnvReference);
	NativeCodecDigest64_Update(&fnvReference, buffer, 4u + NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES);
	CHECK(fnvWriter.value == fnvReference.value);

	/* The golden digest, and SHA-256 of the golden bytes via NativeSha256. */
	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(NativeArcadeBotRules_DigestV1(digest) == 1);
	CHECK(memcmp(digest, k_goldenDigest, sizeof(digest)) == 0);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_goldenEncoding, sizeof(k_goldenEncoding));
	NativeSha256_Final(&sha, reference);
	CHECK(memcmp(reference, k_goldenDigest, sizeof(reference)) == 0);

	/* Deterministic across calls. */
	memset(reference, 0, sizeof(reference));
	CHECK(NativeArcadeBotRules_DigestV1(reference) == 1);
	CHECK(memcmp(reference, digest, sizeof(digest)) == 0);

	CHECK(NativeArcadeBotRules_DigestV1(NULL) == 0);
	return 0;
}

static int TestEncodeTransactional(void)
{
	uint8_t buffer[NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES + 8u];
	struct NativeCodecWriter writer;
	struct NativeCodecWriter before;
	struct NativeCodecDigest64 fnv;

	CHECK(NativeArcadeBotRules_EncodeV1(NULL) == 0);

	/* One byte short of room, from offset 0. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecDigest64_Init(&fnv);
	NativeCodecWriter_Init(&writer, buffer, NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES - 1u, &fnv);
	before = writer;
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(buffer, sizeof(buffer), SENTINEL_BYTE));
	{
		struct NativeCodecDigest64 initial;

		NativeCodecDigest64_Init(&initial);
		CHECK(fnv.value == initial.value);
	}

	/* One byte short of room after earlier bytes. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES + 3u, NULL);
	CHECK(NativeCodecWriter_WriteU32(&writer, 0) == 1);
	before = writer;
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(&buffer[4], sizeof(buffer) - 4u, SENTINEL_BYTE));

	/* Exactly enough room succeeds. */
	NativeCodecWriter_Init(&writer, buffer, NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES, NULL);
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 1);
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES);

	/* A failed writer is refused and left alone. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.failed = 1;
	before = writer;
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(buffer, sizeof(buffer), SENTINEL_BYTE));

	/* An offset beyond capacity is refused. */
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.offset = sizeof(buffer) + 1u;
	before = writer;
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(buffer, sizeof(buffer), SENTINEL_BYTE));
	return 0;
}

static int TestExpectedBots2P(void)
{
	uint32_t pairs = 0;
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
	uint8_t aiSetIndex = 0;

	for (uint32_t a = 0; a < NATIVE_MATCH_SELECT_CHARACTER_COUNT; a++)
	{
		for (uint32_t b = 0; b < NATIVE_MATCH_SELECT_CHARACTER_COUNT; b++)
		{
			const uint8_t human0 = NativeMatchSelect_CharacterAt(a);
			const uint8_t human1 = NativeMatchSelect_CharacterAt(b);

			memset(bots, SENTINEL_BYTE, sizeof(bots));
			aiSetIndex = SENTINEL_BYTE;
			if (a == b)
			{
				CHECK(NativeArcadeBotRules_ExpectedBots2P(human0, human1, bots, &aiSetIndex) == 0);
				CHECK(IsAllByte(bots, sizeof(bots), SENTINEL_BYTE));
				CHECK(aiSetIndex == SENTINEL_BYTE);
				continue;
			}
			{
				const uint32_t set = ReferenceFirstSetWithout(human0, human1);

				CHECK(set < NATIVE_MATCH_SELECT_AI_SET_COUNT);
				CHECK(NativeArcadeBotRules_ExpectedBots2P(human0, human1, bots, &aiSetIndex) == 1);
				CHECK(aiSetIndex == set);
				for (uint32_t racer = 0; racer < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; racer++)
				{
					CHECK(bots[racer] == NativeMatchSelect_AiSetRacer(set, racer));
					CHECK(bots[racer] != human0);
					CHECK(bots[racer] != human1);
				}
			}
			pairs++;
		}
	}
	CHECK(pairs == NATIVE_MATCH_SELECT_CHARACTER_COUNT * (NATIVE_MATCH_SELECT_CHARACTER_COUNT - 1u));

	/* Spot values: humans 0 and 1 get set 0 {6, 4, 2, 3}; humans 6 and 4 get set 4 {1, 2, 3, 5}. */
	CHECK(NativeArcadeBotRules_ExpectedBots2P(0, 1, bots, &aiSetIndex) == 1);
	CHECK((aiSetIndex == 0) && (bots[0] == 6) && (bots[1] == 4) && (bots[2] == 2) && (bots[3] == 3));
	CHECK(NativeArcadeBotRules_ExpectedBots2P(6, 4, bots, &aiSetIndex) == 1);
	CHECK((aiSetIndex == 4) && (bots[0] == 1) && (bots[1] == 2) && (bots[2] == 3) && (bots[3] == 5));

	/* Non-base characters and NULL outputs are rejected with outputs untouched. */
	{
		static const uint8_t nonBase[] = { 8, 14, 15, 16, 0x7f, 0xff };

		for (uint32_t i = 0; i < sizeof(nonBase); i++)
		{
			memset(bots, SENTINEL_BYTE, sizeof(bots));
			aiSetIndex = SENTINEL_BYTE;
			CHECK(NativeArcadeBotRules_ExpectedBots2P(nonBase[i], 0, bots, &aiSetIndex) == 0);
			CHECK(NativeArcadeBotRules_ExpectedBots2P(1, nonBase[i], bots, &aiSetIndex) == 0);
			CHECK(NativeArcadeBotRules_ExpectedBots2P(nonBase[i], nonBase[i], bots, &aiSetIndex) == 0);
			CHECK(IsAllByte(bots, sizeof(bots), SENTINEL_BYTE));
			CHECK(aiSetIndex == SENTINEL_BYTE);
		}
	}
	memset(bots, SENTINEL_BYTE, sizeof(bots));
	aiSetIndex = SENTINEL_BYTE;
	CHECK(NativeArcadeBotRules_ExpectedBots2P(0, 1, NULL, &aiSetIndex) == 0);
	CHECK(aiSetIndex == SENTINEL_BYTE);
	CHECK(NativeArcadeBotRules_ExpectedBots2P(0, 1, bots, NULL) == 0);
	CHECK(IsAllByte(bots, sizeof(bots), SENTINEL_BYTE));
	return 0;
}

static int TestEncoding1P(void)
{
	uint8_t buffer[NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES + 16u];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reference[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	struct NativeCodecDigest64 fnvWriter;
	struct NativeCodecDigest64 fnvReference;
	struct NativeMatchConfigV1 oneCab;
	struct NativeSha256 sha;

	CHECK(NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES == 93u);
	CHECK(NativeArcadeBotRules_EncodedSize1PV1() == NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES);
	CHECK(sizeof(NATIVE_ARCADE_BOT_RULES_1P_V1_TAG) - 1u == 27u);
	CHECK(memcmp(k_golden1PEncoding, NATIVE_ARCADE_BOT_RULES_1P_V1_TAG, 27u) == 0);
	CHECK((NATIVE_ARCADE_BOT_RULES_1P_HUMAN_COUNT == 1u) && (NATIVE_ARCADE_BOT_RULES_1P_DRIVER_COUNT == 8u) &&
	      (NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT == 1u) && (NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT == 7u) &&
	      (NATIVE_ARCADE_BOT_RULES_1P_CANDIDATE_COUNT == 8u) && (NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT == 7u));

	/* The whole encoding, byte for byte, at offset 0. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 1);
	CHECK(NativeCodecWriter_Ok(&writer));
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES);
	CHECK(memcmp(buffer, k_golden1PEncoding, sizeof(k_golden1PEncoding)) == 0);
	CHECK(IsAllByte(&buffer[NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES], 16u, SENTINEL_BYTE));

	/* The roles in the encoding are the live ONE_CAB profile, not a copy. */
	memset(&oneCab, 0, sizeof(oneCab));
	NativeMatchConfigV1_InitArcadeOneCab(&oneCab);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(buffer[35u + i] == oneCab.slots[i].role);
	}
	/* The candidates are the base characters. */
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_1P_CANDIDATE_COUNT; i++)
	{
		CHECK(buffer[55u + i] == NativeMatchSelect_CharacterAt(i));
	}

	/* At a nonzero offset, after other bytes, with a running digest. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecDigest64_Init(&fnvWriter);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &fnvWriter);
	CHECK(NativeCodecWriter_WriteU32(&writer, UINT32_C(0x01020304)) == 1);
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 1);
	CHECK(NativeCodecWriter_Size(&writer) == 4u + NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES);
	CHECK(memcmp(&buffer[4], k_golden1PEncoding, sizeof(k_golden1PEncoding)) == 0);
	NativeCodecDigest64_Init(&fnvReference);
	NativeCodecDigest64_Update(&fnvReference, buffer, 4u + NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES);
	CHECK(fnvWriter.value == fnvReference.value);

	/* The golden digest, and SHA-256 of the golden bytes via NativeSha256. */
	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(NativeArcadeBotRules_Digest1PV1(digest) == 1);
	CHECK(memcmp(digest, k_golden1PDigest, sizeof(digest)) == 0);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_golden1PEncoding, sizeof(k_golden1PEncoding));
	NativeSha256_Final(&sha, reference);
	CHECK(memcmp(reference, k_golden1PDigest, sizeof(reference)) == 0);

	/* Deterministic across calls, and distinct from the TWO_CAB digest. */
	memset(reference, 0, sizeof(reference));
	CHECK(NativeArcadeBotRules_Digest1PV1(reference) == 1);
	CHECK(memcmp(reference, digest, sizeof(digest)) == 0);
	CHECK(memcmp(k_golden1PDigest, k_goldenDigest, sizeof(k_goldenDigest)) != 0);

	CHECK(NativeArcadeBotRules_Digest1PV1(NULL) == 0);
	return 0;
}

static int TestEncode1PTransactional(void)
{
	uint8_t buffer[NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES + 8u];
	struct NativeCodecWriter writer;
	struct NativeCodecWriter before;
	struct NativeCodecDigest64 fnv;

	CHECK(NativeArcadeBotRules_Encode1PV1(NULL) == 0);

	/* One byte short of room, from offset 0. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecDigest64_Init(&fnv);
	NativeCodecWriter_Init(&writer, buffer, NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES - 1u, &fnv);
	before = writer;
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(buffer, sizeof(buffer), SENTINEL_BYTE));
	{
		struct NativeCodecDigest64 initial;

		NativeCodecDigest64_Init(&initial);
		CHECK(fnv.value == initial.value);
	}

	/* One byte short of room after earlier bytes. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES + 3u, NULL);
	CHECK(NativeCodecWriter_WriteU32(&writer, 0) == 1);
	before = writer;
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(&buffer[4], sizeof(buffer) - 4u, SENTINEL_BYTE));

	/* Room for the 1P encoding but not the (longer) V1 one: only 1P fits. */
	NativeCodecWriter_Init(&writer, buffer, NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES, NULL);
	before = writer;
	CHECK(NativeArcadeBotRules_EncodeV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);

	/* Exactly enough room succeeds. */
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 1);
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES);

	/* A failed writer is refused and left alone. */
	memset(buffer, SENTINEL_BYTE, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.failed = 1;
	before = writer;
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(buffer, sizeof(buffer), SENTINEL_BYTE));

	/* An offset beyond capacity is refused. */
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.offset = sizeof(buffer) + 1u;
	before = writer;
	CHECK(NativeArcadeBotRules_Encode1PV1(&writer) == 0);
	CHECK(memcmp(&writer, &before, sizeof(writer)) == 0);
	CHECK(IsAllByte(buffer, sizeof(buffer), SENTINEL_BYTE));
	return 0;
}

static int TestDigestForProfile(void)
{
	static const uint32_t badProfiles[] = { 0u, 3u, 0xffu, UINT32_MAX };
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(NativeArcadeBotRules_DigestForProfileV1(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, digest) == 1);
	CHECK(memcmp(digest, k_goldenDigest, sizeof(digest)) == 0);

	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(NativeArcadeBotRules_DigestForProfileV1(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB, digest) == 1);
	CHECK(memcmp(digest, k_golden1PDigest, sizeof(digest)) == 0);

	for (uint32_t i = 0; i < sizeof(badProfiles) / sizeof(badProfiles[0]); i++)
	{
		memset(digest, SENTINEL_BYTE, sizeof(digest));
		CHECK(NativeArcadeBotRules_DigestForProfileV1(badProfiles[i], digest) == 0);
		CHECK(IsAllByte(digest, sizeof(digest), SENTINEL_BYTE));
	}
	CHECK(NativeArcadeBotRules_DigestForProfileV1(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, NULL) == 0);
	CHECK(NativeArcadeBotRules_DigestForProfileV1(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB, NULL) == 0);
	return 0;
}

/*
 * Independent transcription of LOAD_Robots1P (game/LOAD/LOAD_Assets.c:61-76)
 * on the retail array type: retail declares `s16 characterIDs[8]`
 * (include/regionsEXE.h:2241), here int16_t characterIDs[8]; the loop bound
 * LOAD_CHARACTER_ID_COUNT is 8 too.
 */
static void ReferenceRobots1P(int characterID, int16_t characterIDs[8])
{
	int newCharacterID = 0;

	characterIDs[0] = (int16_t)characterID;

	for (int i = 1; i < 8; i++, newCharacterID++)
	{
		if (newCharacterID == characterID)
		{
			newCharacterID++;
		}

		characterIDs[i] = (int16_t)newCharacterID;
	}
}

static int TestExpectedBots1P(void)
{
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];

	for (uint32_t h = 0; h < NATIVE_MATCH_SELECT_CHARACTER_COUNT; h++)
	{
		const uint8_t human = NativeMatchSelect_CharacterAt(h);
		int16_t characterIDs[8];
		uint32_t seen = 0;

		ReferenceRobots1P(human, characterIDs);
		CHECK(characterIDs[0] == human);
		memset(bots, SENTINEL_BYTE, sizeof(bots));
		CHECK(NativeArcadeBotRules_ExpectedBots1P(human, bots) == 1);
		for (uint32_t bot = 0; bot < NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT; bot++)
		{
			/* Slot FIRST_BOT_SLOT + bot is driver index 1 + bot of the retail array. */
			CHECK((int16_t)bots[bot] == characterIDs[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + bot]);
			CHECK(bots[bot] != human);
			CHECK(bots[bot] < NATIVE_MATCH_SELECT_CHARACTER_COUNT);
			CHECK((bot == 0) || (bots[bot - 1u] < bots[bot]));
			seen |= 1u << bots[bot];
		}
		/* Every base character but the human, once each. */
		CHECK((seen | (1u << human)) == 0xffu);
		CHECK((seen & (1u << human)) == 0u);
	}

	/* Spot values: human 0 -> 1..7; human 7 -> 0..6; human 3 -> 0, 1, 2, 4, 5, 6, 7. */
	CHECK(NativeArcadeBotRules_ExpectedBots1P(0, bots) == 1);
	CHECK((bots[0] == 1) && (bots[1] == 2) && (bots[2] == 3) && (bots[3] == 4) && (bots[4] == 5) && (bots[5] == 6) &&
	      (bots[6] == 7));
	CHECK(NativeArcadeBotRules_ExpectedBots1P(7, bots) == 1);
	CHECK((bots[0] == 0) && (bots[1] == 1) && (bots[2] == 2) && (bots[3] == 3) && (bots[4] == 4) && (bots[5] == 5) &&
	      (bots[6] == 6));
	CHECK(NativeArcadeBotRules_ExpectedBots1P(3, bots) == 1);
	CHECK((bots[0] == 0) && (bots[1] == 1) && (bots[2] == 2) && (bots[3] == 4) && (bots[4] == 5) && (bots[5] == 6) &&
	      (bots[6] == 7));

	/* Non-base characters and a NULL output are rejected with the output untouched. */
	{
		static const uint8_t nonBase[] = { 8, 15, 0xff };

		for (uint32_t i = 0; i < sizeof(nonBase); i++)
		{
			memset(bots, SENTINEL_BYTE, sizeof(bots));
			CHECK(NativeArcadeBotRules_ExpectedBots1P(nonBase[i], bots) == 0);
			CHECK(IsAllByte(bots, sizeof(bots), SENTINEL_BYTE));
		}
	}
	CHECK(NativeArcadeBotRules_ExpectedBots1P(0, NULL) == 0);
	return 0;
}

static int TestMapRetailSeeds(void)
{
	static const uint32_t draws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT] = {
		UINT32_C(0x12345678), UINT32_C(0x9abcdef0), UINT32_C(0x0fedcba9), UINT32_C(0x87654321), UINT32_C(0xdeadbeef),
	};
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct NativeArcadeRetailRngSeedsV1 untouched;
	uint32_t local[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT];

	/* Target order and the 16-bit mask. */
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(draws, &seeds) == 1);
	CHECK(seeds.randomNumber == UINT32_C(0x5678));
	CHECK(seeds.advRng0 == UINT32_C(0x9abcdef0));
	CHECK(seeds.advRng1 == UINT32_C(0x0fedcba9));
	CHECK(seeds.psxRandSeed == UINT32_C(0x87654321));
	CHECK(seeds.audioRNG == UINT32_C(0xdeadbeef));

	memcpy(local, draws, sizeof(local));
	local[0] = UINT32_MAX;
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(local, &seeds) == 1);
	CHECK(seeds.randomNumber == UINT32_C(0xffff));
	local[0] = UINT32_C(0xffff0000);
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(local, &seeds) == 1);
	CHECK(seeds.randomNumber == 0);

	/* Both advRng words 0 -> the retail constants; nothing else changes. */
	memcpy(local, draws, sizeof(local));
	local[1] = 0;
	local[2] = 0;
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(local, &seeds) == 1);
	CHECK(seeds.advRng0 == UINT32_C(0x30215400));
	CHECK(seeds.advRng1 == UINT32_C(0x493583fe));
	CHECK(seeds.advRng0 == NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0);
	CHECK(seeds.advRng1 == NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1);
	CHECK(seeds.randomNumber == UINT32_C(0x5678));
	CHECK(seeds.psxRandSeed == UINT32_C(0x87654321));
	CHECK(seeds.audioRNG == UINT32_C(0xdeadbeef));

	/* Only one word 0: no fallback. */
	local[1] = 0;
	local[2] = 1;
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(local, &seeds) == 1);
	CHECK((seeds.advRng0 == 0) && (seeds.advRng1 == 1));
	local[1] = 1;
	local[2] = 0;
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(local, &seeds) == 1);
	CHECK((seeds.advRng0 == 1) && (seeds.advRng1 == 0));

	/* All zero draws. */
	memset(local, 0, sizeof(local));
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(local, &seeds) == 1);
	CHECK((seeds.randomNumber == 0) && (seeds.advRng0 == NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0) &&
	      (seeds.advRng1 == NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1) && (seeds.psxRandSeed == 0) && (seeds.audioRNG == 0));

	/* NULL arguments leave the output untouched. */
	memset(&untouched, SENTINEL_BYTE, sizeof(untouched));
	seeds = untouched;
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(NULL, &seeds) == 0);
	CHECK(memcmp(&seeds, &untouched, sizeof(seeds)) == 0);
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(draws, NULL) == 0);

	/* The golden draws map to the golden seeds. */
	CHECK(NativeArcadeBotRules_MapRetailSeedsV1(k_goldenSetupDraws, &seeds) == 1);
	CHECK(SeedsEqual(&seeds, &k_goldenSeeds));
	return 0;
}

static int TestDeriveRetailSeeds(void)
{
	struct NativeDeterministicRngBankV1 fresh;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeDeterministicRngBankV1 other;
	struct NativeDeterministicRngBankV1 manual;
	struct NativeDeterministicRngBankV1 before;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct NativeArcadeRetailRngSeedsV1 otherSeeds;
	struct NativeArcadeRetailRngSeedsV1 untouched;
	uint32_t draws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT];

	CHECK(NativeDeterministicRngBankV1_Init(&fresh, FIXTURE_SEED, NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION) == 1);
	bank = fresh;
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&bank, &seeds) == 1);
	CHECK(SeedsEqual(&seeds, &k_goldenSeeds));

	/* MATCH_SETUP (stream 0) advanced by exactly 5; every other stream untouched. */
	CHECK(bank.streams[0].tag == (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP);
	CHECK(bank.streams[0].drawCount == fresh.streams[0].drawCount + NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT);
	CHECK(bank.streams[0].drawCount == 5u);
	CHECK(memcmp(bank.streams[0].state, fresh.streams[0].state, sizeof(fresh.streams[0].state)) != 0);
	for (uint32_t i = 1; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		CHECK(StreamsEqual(&bank.streams[i], &fresh.streams[i]));
	}
	CHECK((bank.bankVersion == fresh.bankVersion) && (bank.derivationVersion == fresh.derivationVersion) &&
	      (bank.masterSeed == fresh.masterSeed));

	/* The same as five manual draws in target order, mapped. */
	manual = fresh;
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT; i++)
	{
		CHECK(NativeDeterministicRngBankV1_NextU32(&manual, (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
		          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &draws[i]) == 1);
		CHECK(draws[i] == k_goldenSetupDraws[i]);
	}
	CHECK(BanksEqual(&manual, &bank));

	/* Two fresh banks give identical results. */
	CHECK(NativeDeterministicRngBankV1_Init(&other, FIXTURE_SEED, NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION) == 1);
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, &otherSeeds) == 1);
	CHECK(SeedsEqual(&seeds, &otherSeeds));
	CHECK(BanksEqual(&other, &bank));

	/* A second derivation continues the stream. */
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, &otherSeeds) == 1);
	CHECK(other.streams[0].drawCount == 10u);
	CHECK(!SeedsEqual(&seeds, &otherSeeds));

	/* A different seed gives different seeds. */
	CHECK(NativeDeterministicRngBankV1_Init(&other, FIXTURE_SEED + 1u, NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION) == 1);
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, &otherSeeds) == 1);
	CHECK(!SeedsEqual(&seeds, &otherSeeds));

	/* Failure is transactional: an invalid bank leaves both outputs untouched. */
	memset(&untouched, SENTINEL_BYTE, sizeof(untouched));
	other = fresh;
	other.bankVersion = 2u;
	before = other;
	seeds = untouched;
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, &seeds) == 0);
	CHECK(BanksEqual(&other, &before));
	CHECK(memcmp(&seeds, &untouched, sizeof(seeds)) == 0);

	other = fresh;
	other.streams[0].tag = (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS;
	before = other;
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, &seeds) == 0);
	CHECK(BanksEqual(&other, &before));
	CHECK(memcmp(&seeds, &untouched, sizeof(seeds)) == 0);

	other = fresh;
	memset(other.streams[0].state, 0, sizeof(other.streams[0].state));
	before = other;
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, &seeds) == 0);
	CHECK(BanksEqual(&other, &before));
	CHECK(memcmp(&seeds, &untouched, sizeof(seeds)) == 0);

	/* NULL output leaves the bank untouched; NULL bank is refused. */
	other = fresh;
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&other, NULL) == 0);
	CHECK(BanksEqual(&other, &fresh));
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(NULL, &seeds) == 0);
	CHECK(memcmp(&seeds, &untouched, sizeof(seeds)) == 0);
	return 0;
}

/*
 * A hand-built TWO_CAB base with v1 bot rules (not the arcade-link fixture:
 * slots 0..5 hold characters 0..5 before resolution), resolved through match
 * select to TINY_TIGER and DINGODILE with their 2P AI set.
 */
static int BuildValidConfig(struct NativeMatchConfigV1 *config, uint8_t botDifficulty)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;

	memset(&base, 0, sizeof(base));
	NativeMatchConfigV1_InitArcadeTwoCab(&base);
	base.trackID = 3;
	base.lapCount = 3;
	base.tickRateNumerator = 30;
	base.tickRateDenominator = 1;
	base.masterSeed = FIXTURE_SEED;
	FillCounting(base.buildIdentity, sizeof(base.buildIdentity), 0x40u);
	FillCounting(base.contentIdentity, sizeof(base.contentIdentity), 0x80u);
	if (!NativeArcadeBotRules_DigestV1(base.botRulesDigest))
	{
		return 0;
	}
	for (uint32_t i = 0; i <= 5u; i++)
	{
		base.slots[i].characterID = (uint8_t)i;
		base.slots[i].difficulty = i >= 2u ? botDifficulty : 0u;
	}

	memset(choices, 0, sizeof(choices));
	choices[0].characterID = 2; /* TINY_TIGER */
	choices[0].trackID = 6;
	choices[0].lapCount = 5;
	choices[0].nonce = UINT64_C(0x0123456789abcdef);
	choices[1].characterID = 5; /* DINGODILE */
	choices[1].trackID = 6;
	choices[1].lapCount = 5;
	choices[1].nonce = UINT64_C(0xfedcba9876543210);
	return NativeMatchSelect_Resolve(&base, 2, choices, &outcome) && NativeMatchSelect_BuildConfig(&base, &outcome, config);
}

/* Rejected by the bot rules while the generic config validator still accepts it. */
static int ExpectRulesReject(const struct NativeMatchConfigV1 *config)
{
	CHECK(NativeMatchConfigV1_Validate(config) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(config) == 0);
	return 0;
}

static int TestValidateConfig(void)
{
	struct NativeMatchConfigV1 valid;
	struct NativeMatchConfigV1 config;
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
	uint8_t aiSetIndex = 0;

	CHECK(BuildValidConfig(&valid, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM) == 1);
	CHECK(valid.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
	CHECK((valid.slots[0].characterID == 2) && (valid.slots[1].characterID == 5));
	CHECK(NativeArcadeBotRules_ExpectedBots2P(2, 5, bots, &aiSetIndex) == 1);
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
	{
		CHECK(valid.slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i].characterID == bots[i]);
		CHECK(valid.slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i].difficulty == 0xa0u);
	}
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&valid) == 1);

	/* Every table difficulty is accepted. */
	for (uint32_t d = 0; d < NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT; d++)
	{
		CHECK(BuildValidConfig(&config, NativeArcadeBotRules_DifficultyAt(d)) == 1);
		CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	}

	CHECK(NativeArcadeBotRules_ValidateConfigV1(NULL) == 0);

	/*
	 * ONE_CAB profile (RS-19): a flipped profile field on a TWO_CAB config is
	 * rejected; a well-formed ONE_CAB config is rejected with the TWO_CAB
	 * digest and accepted with its own Digest1PV1.
	 */
	config = valid;
	config.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB;
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 0);
	memset(&config, 0, sizeof(config));
	NativeMatchConfigV1_InitArcadeOneCab(&config);
	config.trackID = valid.trackID;
	config.lapCount = valid.lapCount;
	config.tickRateNumerator = 30;
	config.tickRateDenominator = 1;
	config.masterSeed = valid.masterSeed;
	memcpy(config.buildIdentity, valid.buildIdentity, sizeof(config.buildIdentity));
	memcpy(config.contentIdentity, valid.contentIdentity, sizeof(config.contentIdentity));
	memcpy(config.botRulesDigest, valid.botRulesDigest, sizeof(config.botRulesDigest));
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config.slots[i].characterID = (uint8_t)i;
		config.slots[i].difficulty = i == 0 ? 0u : 0xa0u;
	}
	CHECK(ExpectRulesReject(&config) == 0);
	CHECK(NativeArcadeBotRules_Digest1PV1(config.botRulesDigest) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);

	/* A TWO_CAB config carrying the 1P digest. */
	config = valid;
	CHECK(NativeArcadeBotRules_Digest1PV1(config.botRulesDigest) == 1);
	CHECK(ExpectRulesReject(&config) == 0);

	/* Mode fields. */
	config = valid;
	config.gameMode1 = 1u;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.gameMode2 = 1u;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.rules = 1u;
	CHECK(ExpectRulesReject(&config) == 0);

	/* A flipped botRulesDigest byte, anywhere. */
	for (uint32_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config = valid;
		config.botRulesDigest[i] ^= 0x01u;
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* Off-table track and laps, including values above a byte. */
	config = valid;
	config.trackID = 13; /* OXIDE_STATION */
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.trackID = 0x103u; /* 3 in the low byte */
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.lapCount = 4;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.lapCount = 0x105u; /* 5 in the low byte */
	CHECK(ExpectRulesReject(&config) == 0);

	/* Equal human characters, and a non-base character in either human slot. */
	config = valid;
	config.slots[1].characterID = config.slots[0].characterID;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.slots[1].characterID = 8;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.slots[0].characterID = 8;
	CHECK(ExpectRulesReject(&config) == 0);

	/* Nonzero human difficulty. */
	config = valid;
	config.slots[0].difficulty = 0xa0u;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.slots[1].difficulty = 1u;
	CHECK(ExpectRulesReject(&config) == 0);

	/* Bot difficulty off the table (all bots equal). */
	config = valid;
	for (uint32_t i = 2; i <= 5u; i++)
	{
		config.slots[i].difficulty = 0x51u;
	}
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	for (uint32_t i = 2; i <= 5u; i++)
	{
		config.slots[i].difficulty = 0u;
	}
	CHECK(ExpectRulesReject(&config) == 0);

	/* Unequal bot difficulties (each table value, in each bot slot). */
	for (uint32_t i = 2; i <= 5u; i++)
	{
		config = valid;
		config.slots[i].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD;
		CHECK(ExpectRulesReject(&config) == 0);
		config = valid;
		config.slots[i].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY;
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* A bot character not matching the rule. */
	for (uint32_t i = 2; i <= 5u; i++)
	{
		config = valid;
		config.slots[i].characterID = (uint8_t)(config.slots[i].characterID == 7u ? 6u : 7u);
		if ((config.slots[i].characterID == config.slots[0].characterID) ||
		    (config.slots[i].characterID == config.slots[1].characterID))
		{
			config.slots[i].characterID = 1u;
		}
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* Swapped bot order. */
	config = valid;
	{
		const uint8_t swap = config.slots[2].characterID;

		config.slots[2].characterID = config.slots[3].characterID;
		config.slots[3].characterID = swap;
	}
	CHECK(config.slots[2].characterID != config.slots[3].characterID);
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	{
		const uint8_t swap = config.slots[4].characterID;

		config.slots[4].characterID = config.slots[5].characterID;
		config.slots[5].characterID = swap;
	}
	CHECK(ExpectRulesReject(&config) == 0);

	/* A config the generic validator rejects is rejected too. */
	config = valid;
	config.slots[6].characterID = 1u;
	CHECK(NativeMatchConfigV1_Validate(&config) == 0);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 0);
	return 0;
}

/*
 * A hand-built ONE_CAB config with the 1P rules: the human in slot 0 at
 * difficulty 0, and the bots of slots 1..7 from the independent LOAD_Robots1P
 * transcription (not the module), all at botDifficulty.
 */
static int BuildValidOneCabConfig(struct NativeMatchConfigV1 *config, uint8_t human, uint8_t botDifficulty)
{
	int16_t characterIDs[8];

	memset(config, 0, sizeof(*config));
	NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = 3;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = FIXTURE_SEED;
	FillCounting(config->buildIdentity, sizeof(config->buildIdentity), 0x40u);
	FillCounting(config->contentIdentity, sizeof(config->contentIdentity), 0x80u);
	if (!NativeArcadeBotRules_Digest1PV1(config->botRulesDigest))
	{
		return 0;
	}
	ReferenceRobots1P(human, characterIDs);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config->slots[i].characterID = (uint8_t)characterIDs[i];
		config->slots[i].difficulty = i == 0 ? 0u : botDifficulty;
	}
	return 1;
}

static int TestValidateConfig1P(void)
{
	struct NativeMatchConfigV1 valid;
	struct NativeMatchConfigV1 config;
	uint8_t twoCabDigest[NATIVE_SHA256_DIGEST_BYTES];

	/* Every base human at every table difficulty. */
	for (uint32_t h = 0; h < NATIVE_MATCH_SELECT_CHARACTER_COUNT; h++)
	{
		for (uint32_t d = 0; d < NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT; d++)
		{
			CHECK(BuildValidOneCabConfig(&config, NativeMatchSelect_CharacterAt(h), NativeArcadeBotRules_DifficultyAt(d)) == 1);
			CHECK(config.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
			CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
		}
	}

	/* The base case for the one-field rejections: human 4 (N_GIN), medium bots. */
	CHECK(BuildValidOneCabConfig(&valid, 4u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM) == 1);
	CHECK((valid.slots[0].characterID == 4u) && (valid.slots[1].characterID == 0u) && (valid.slots[4].characterID == 3u) &&
	      (valid.slots[5].characterID == 5u) && (valid.slots[7].characterID == 7u));
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&valid) == 1);

	/* The TWO_CAB digest on a ONE_CAB config. */
	config = valid;
	CHECK(NativeArcadeBotRules_DigestV1(twoCabDigest) == 1);
	memcpy(config.botRulesDigest, twoCabDigest, sizeof(config.botRulesDigest));
	CHECK(ExpectRulesReject(&config) == 0);

	/* A flipped Digest1PV1 byte, anywhere. */
	for (uint32_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config = valid;
		config.botRulesDigest[i] ^= 0x01u;
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* Mode fields. */
	config = valid;
	config.gameMode1 = 1u;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.gameMode2 = 1u;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.rules = 1u;
	CHECK(ExpectRulesReject(&config) == 0);

	/* Off-table track and laps, including values above a byte. */
	config = valid;
	config.trackID = 13; /* OXIDE_STATION */
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.trackID = 0x103u; /* 3 in the low byte */
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.lapCount = 4;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.lapCount = 0x105u; /* 5 in the low byte */
	CHECK(ExpectRulesReject(&config) == 0);

	/* Nonzero human difficulty. */
	config = valid;
	config.slots[0].difficulty = 0xa0u;
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	config.slots[0].difficulty = 1u;
	CHECK(ExpectRulesReject(&config) == 0);

	/* A non-base human, with or without matching bots. */
	config = valid;
	config.slots[0].characterID = 8;
	CHECK(ExpectRulesReject(&config) == 0);
	CHECK(BuildValidOneCabConfig(&config, 8u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM) == 1);
	CHECK(ExpectRulesReject(&config) == 0);

	/* Bot difficulty off the table (all bots equal). */
	config = valid;
	for (uint32_t i = 1; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config.slots[i].difficulty = 0x51u;
	}
	CHECK(ExpectRulesReject(&config) == 0);
	config = valid;
	for (uint32_t i = 1; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config.slots[i].difficulty = 0u;
	}
	CHECK(ExpectRulesReject(&config) == 0);

	/* Unequal bot difficulties (each other table value, in each bot slot). */
	for (uint32_t i = 1; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config = valid;
		config.slots[i].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD;
		CHECK(ExpectRulesReject(&config) == 0);
		config = valid;
		config.slots[i].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY;
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* A bot character not matching the rule: a repeated base character, or a non-base one. */
	for (uint32_t i = 1; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config = valid;
		config.slots[i].characterID = config.slots[i == 1u ? 2u : 1u].characterID;
		CHECK(ExpectRulesReject(&config) == 0);
		config = valid;
		config.slots[i].characterID = 8u;
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* Two bots swapped: every adjacent pair. */
	for (uint32_t i = 1; i + 1u < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		const uint8_t swap = valid.slots[i].characterID;

		config = valid;
		config.slots[i].characterID = config.slots[i + 1u].characterID;
		config.slots[i + 1u].characterID = swap;
		CHECK(config.slots[i].characterID != config.slots[i + 1u].characterID);
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* A bot equal to the human, in each bot slot. */
	for (uint32_t i = 1; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config = valid;
		config.slots[i].characterID = config.slots[0].characterID;
		CHECK(ExpectRulesReject(&config) == 0);
	}

	/* Another human with the bots of the base case. */
	config = valid;
	config.slots[0].characterID = 5u;
	CHECK(ExpectRulesReject(&config) == 0);

	/* A config the generic validator rejects is rejected too. */
	config = valid;
	config.slots[7].initialLifecycle = NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE;
	CHECK(NativeMatchConfigV1_Validate(&config) == 0);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 0);
	return 0;
}

/* The v1 rules on a two-human config: the humans, the digest, and ExpectedBots2P at difficulty 0xA0. */
static int CheckRulesConfig(const struct NativeMatchConfigV1 *config, uint8_t human0, uint8_t human1)
{
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
	uint8_t aiSetIndex = 0;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	CHECK(NativeArcadeBotRules_ValidateConfigV1(config) == 1);
	CHECK(NativeArcadeBotRules_DigestV1(digest) == 1);
	CHECK(memcmp(config->botRulesDigest, digest, sizeof(digest)) == 0);
	CHECK((config->slots[0].characterID == human0) && (config->slots[1].characterID == human1));
	CHECK((config->slots[0].difficulty == 0u) && (config->slots[1].difficulty == 0u));
	CHECK(NativeArcadeBotRules_ExpectedBots2P(human0, human1, bots, &aiSetIndex) == 1);
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
	{
		CHECK(config->slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i].characterID == bots[i]);
		CHECK(config->slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i].difficulty == 0xa0u);
	}
	return 0;
}

/*
 * End to end on the real arcade-link fixture: every ordered pair of distinct
 * base characters resolved through match select, and a rematch of each
 * result, satisfies the v1 bot rules.
 */
static int TestFixtureThroughMatchSelect(void)
{
	const uint64_t rematchMask = UINT64_C(0x5a5a5a5a5a5a5a5a);
	struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 fixture;
	struct NativeMatchConfigV1 resolved;
	struct NativeMatchConfigV1 rematch;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;
	uint32_t pairs = 0;

	FillCounting(identity.build, sizeof(identity.build), 0x11u);
	FillCounting(identity.content, sizeof(identity.content), 0x91u);
	CHECK(NativeArcadeLinkFixture_Build(&identity, &fixture) == 1);
	CHECK(CheckRulesConfig(&fixture, 0u, 1u) == 0);

	for (uint32_t a = 0; a < NATIVE_MATCH_SELECT_CHARACTER_COUNT; a++)
	{
		for (uint32_t b = 0; b < NATIVE_MATCH_SELECT_CHARACTER_COUNT; b++)
		{
			if (a == b)
			{
				continue;
			}
			memset(choices, 0, sizeof(choices));
			choices[0].characterID = NativeMatchSelect_CharacterAt(a);
			choices[0].trackID = (uint8_t)fixture.trackID;
			choices[0].lapCount = (uint8_t)fixture.lapCount;
			choices[0].nonce = UINT64_C(0x1000000000000001) + a;
			choices[1].characterID = NativeMatchSelect_CharacterAt(b);
			choices[1].trackID = (uint8_t)fixture.trackID;
			choices[1].lapCount = (uint8_t)fixture.lapCount;
			choices[1].nonce = UINT64_C(0x2000000000000002) + ((uint64_t)b << 8);
			CHECK(NativeMatchSelect_Resolve(&fixture, 2u, choices, &outcome) == 1);
			CHECK(outcome.characterReassignedMask == 0u);
			CHECK((outcome.trackDrawn == 0u) && (outcome.lapsDrawn == 0u));
			CHECK(NativeMatchSelect_BuildConfig(&fixture, &outcome, &resolved) == 1);
			CHECK((resolved.trackID == fixture.trackID) && (resolved.lapCount == fixture.lapCount));
			CHECK(CheckRulesConfig(&resolved, choices[0].characterID, choices[1].characterID) == 0);

			CHECK(NativeLockstepRematch_BuildConfig(&resolved, resolved.masterSeed ^ rematchMask, &rematch) == 1);
			CHECK(rematch.masterSeed != resolved.masterSeed);
			CHECK(CheckRulesConfig(&rematch, choices[0].characterID, choices[1].characterID) == 0);
			pairs++;
		}
	}
	CHECK(pairs == 56u);

	/* Differing track and lap votes: both are drawn, and the result still meets the rules. */
	memset(choices, 0, sizeof(choices));
	choices[0].characterID = 7;
	choices[0].trackID = 6;
	choices[0].lapCount = 5;
	choices[0].nonce = UINT64_C(0x0123456789abcdef);
	choices[1].characterID = 4;
	choices[1].trackID = (uint8_t)fixture.trackID;
	choices[1].lapCount = 7;
	choices[1].nonce = UINT64_C(0xfedcba9876543210);
	CHECK(choices[0].trackID != fixture.trackID);
	CHECK(NativeMatchSelect_Resolve(&fixture, 2u, choices, &outcome) == 1);
	CHECK((outcome.trackDrawn == 1u) && (outcome.lapsDrawn == 1u));
	CHECK(NativeMatchSelect_BuildConfig(&fixture, &outcome, &resolved) == 1);
	CHECK((resolved.trackID == 6u) || (resolved.trackID == fixture.trackID));
	CHECK((resolved.lapCount == 5u) || (resolved.lapCount == 7u));
	CHECK(CheckRulesConfig(&resolved, 7u, 4u) == 0);
	CHECK(NativeLockstepRematch_BuildConfig(&resolved, resolved.masterSeed ^ rematchMask, &rematch) == 1);
	CHECK((rematch.trackID == resolved.trackID) && (rematch.lapCount == resolved.lapCount));
	CHECK(CheckRulesConfig(&rematch, 7u, 4u) == 0);
	return 0;
}

int main(void)
{
	CHECK(TestTables() == 0);
	CHECK(TestEncoding() == 0);
	CHECK(TestEncodeTransactional() == 0);
	CHECK(TestExpectedBots2P() == 0);
	CHECK(TestEncoding1P() == 0);
	CHECK(TestEncode1PTransactional() == 0);
	CHECK(TestDigestForProfile() == 0);
	CHECK(TestExpectedBots1P() == 0);
	CHECK(TestMapRetailSeeds() == 0);
	CHECK(TestDeriveRetailSeeds() == 0);
	CHECK(TestValidateConfig() == 0);
	CHECK(TestValidateConfig1P() == 0);
	CHECK(TestFixtureThroughMatchSelect() == 0);
	puts("native_arcade_bot_rules_test: ok");
	return 0;
}
