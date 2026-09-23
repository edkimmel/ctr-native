#include "platform/native_arcade_bot_rules.h"

#include "platform/native_canonical_codec.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The encoding size, field by field, in the order of the header's table. */
_Static_assert((sizeof(NATIVE_ARCADE_BOT_RULES_V1_TAG) - 1u) /* tag */
		+ 4u /* rulesVersion */
		+ 4u /* profile */
		+ NATIVE_MATCH_CONFIG_V1_SLOT_COUNT /* slot roles */
		+ 4u /* humanCount, driverCount, firstBotSlot, botCount */
		+ 1u + NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT /* difficultyCount, difficulty table */
		+ 3u /* difficultyPolicy, botCharacterPolicy, modePolicy */
		+ 2u + (NATIVE_MATCH_SELECT_AI_SET_COUNT * NATIVE_MATCH_SELECT_AI_SET_RACERS) /* aiSetCount, aiSetRacers, sets */
		+ 4u /* rngDerivationVersion */
		+ 4u /* seed stream tag */
		+ 1u + NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT /* seedTargetCount, target codes */
		+ 4u /* randomNumberMask */
		+ 4u + 4u /* advRngFallback0, advRngFallback1 */
		+ 4u /* reservedStreamMask */
		== NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES,
	"NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES must equal the V1 field list");
_Static_assert(sizeof(NATIVE_ARCADE_BOT_RULES_V1_TAG) - 1u == 24u, "the V1 tag is 24 ASCII bytes");
_Static_assert(NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_BOT_COUNT == NATIVE_ARCADE_BOT_RULES_DRIVER_COUNT,
	"the bots fill the slots after the humans");
_Static_assert(NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT == NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT, "the humans hold slots 0 and 1");
_Static_assert(NATIVE_ARCADE_BOT_RULES_BOT_COUNT == NATIVE_MATCH_SELECT_AI_SET_RACERS, "one bot per 2P AI set racer");
_Static_assert(NATIVE_ARCADE_BOT_RULES_DRIVER_COUNT <= NATIVE_MATCH_CONFIG_V1_SLOT_COUNT, "every driver has a config slot");
/* The digest encodes the RNG derivation version; the config validator checks its own copy of it. */
_Static_assert(NATIVE_MATCH_CONFIG_V1_RNG_DERIVATION_VERSION == NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION,
	"the config and the bank must agree on the RNG derivation version");

/*
 * The difficulty table stays a simple one-line initializer so the isolation
 * test can read it and compare it with the D230 cupDifficulty speed
 * initializer (game/230/D230.c).
 */
static const uint8_t k_arcadeBotRulesDifficulty[NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT] = { 0x50, 0xA0, 0xF0 };

#define NATIVE_ARCADE_BOT_RULES_DIFFICULTY_POLICY_GLOBAL 1u
#define NATIVE_ARCADE_BOT_RULES_BOT_CHARACTER_POLICY_RETAIL_2P_AI_SET 1u
#define NATIVE_ARCADE_BOT_RULES_MODE_POLICY_ARCADE_SINGLE_RACE 1u

static const char k_arcadeBotRulesTag[] = NATIVE_ARCADE_BOT_RULES_V1_TAG;

uint8_t NativeArcadeBotRules_DifficultyAt(uint32_t index)
{
	return index < NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT ? k_arcadeBotRulesDifficulty[index] : 0u;
}

int NativeArcadeBotRules_IsDifficulty(uint32_t value)
{
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT; i++)
	{
		if (k_arcadeBotRulesDifficulty[i] == value)
		{
			return 1;
		}
	}
	return 0;
}

size_t NativeArcadeBotRules_EncodedSizeV1(void)
{
	return NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES;
}

int NativeArcadeBotRules_EncodeV1(struct NativeCodecWriter *writer)
{
	struct NativeMatchConfigV1 twoCab;
	struct NativeCodecWriter encoded;
	const size_t start = writer != NULL ? writer->offset : 0u;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES > writer->capacity - writer->offset))
	{
		return 0;
	}

	/* The slot roles come from the profile initializer, not a copy of it. */
	NativeMatchConfigV1_InitArcadeTwoCab(&twoCab);

	encoded = *writer;
	if (!NativeCodecWriter_WriteBytes(&encoded, k_arcadeBotRulesTag, sizeof(k_arcadeBotRulesTag) - 1u) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_ARCADE_BOT_RULES_V1_VERSION) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		if (!NativeCodecWriter_WriteU8(&encoded, twoCab.slots[i].role))
		{
			return 0;
		}
	}
	if (!NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_DRIVER_COUNT) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_BOT_COUNT) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT) ||
	    !NativeCodecWriter_WriteBytes(&encoded, k_arcadeBotRulesDifficulty, sizeof(k_arcadeBotRulesDifficulty)) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_DIFFICULTY_POLICY_GLOBAL) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_BOT_CHARACTER_POLICY_RETAIL_2P_AI_SET) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_MODE_POLICY_ARCADE_SINGLE_RACE) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_MATCH_SELECT_AI_SET_COUNT) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_MATCH_SELECT_AI_SET_RACERS))
	{
		return 0;
	}
	for (uint32_t set = 0; set < NATIVE_MATCH_SELECT_AI_SET_COUNT; set++)
	{
		for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
		{
			if (!NativeCodecWriter_WriteU8(&encoded, NativeMatchSelect_AiSetRacer(set, racer)))
			{
				return 0;
			}
		}
	}
	if (!NativeCodecWriter_WriteU32(&encoded, NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION) ||
	    !NativeCodecWriter_WriteU32(&encoded, (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_SEED_RANDOM_NUMBER) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_SEED_ADV_RNG0) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_SEED_ADV_RNG1) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_SEED_PSX_RAND) ||
	    !NativeCodecWriter_WriteU8(&encoded, (uint8_t)NATIVE_ARCADE_BOT_RULES_SEED_AUDIO_RNG) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_ARCADE_BOT_RULES_RANDOM_NUMBER_MASK) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_ARCADE_BOT_RULES_RESERVED_STREAM_MASK) ||
	    (encoded.offset - start != NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES))
	{
		return 0;
	}
	*writer = encoded;
	return 1;
}

int NativeArcadeBotRules_DigestV1(uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t bytes[NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES];
	struct NativeCodecWriter writer;
	struct NativeSha256 sha;

	if (digest == NULL)
	{
		return 0;
	}
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeArcadeBotRules_EncodeV1(&writer) || (NativeCodecWriter_Size(&writer) != sizeof(bytes)))
	{
		return 0;
	}
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, bytes, sizeof(bytes));
	NativeSha256_Final(&sha, digest);
	return 1;
}

static int NativeArcadeBotRules_AiSetHolds(uint32_t setIndex, uint8_t characterID)
{
	for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
	{
		if (NativeMatchSelect_AiSetRacer(setIndex, racer) == characterID)
		{
			return 1;
		}
	}
	return 0;
}

int NativeArcadeBotRules_ExpectedBots2P(uint8_t human0, uint8_t human1, uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT],
	uint8_t *aiSetIndex)
{
	uint32_t index = 0;

	if ((bots == NULL) || (aiSetIndex == NULL) || (human0 == human1) ||
	    !NativeMatchSelect_CharacterIndex(human0, &index) || !NativeMatchSelect_CharacterIndex(human1, &index))
	{
		return 0;
	}

	/* The LOAD_Robots2P rule: the first set holding neither human. */
	for (uint32_t set = 0; set < NATIVE_MATCH_SELECT_AI_SET_COUNT; set++)
	{
		if (!NativeArcadeBotRules_AiSetHolds(set, human0) && !NativeArcadeBotRules_AiSetHolds(set, human1))
		{
			for (uint32_t racer = 0; racer < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; racer++)
			{
				bots[racer] = NativeMatchSelect_AiSetRacer(set, racer);
			}
			*aiSetIndex = (uint8_t)set;
			return 1;
		}
	}
	return 0;
}

int NativeArcadeBotRules_MapRetailSeedsV1(const uint32_t draws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT],
	struct NativeArcadeRetailRngSeedsV1 *out)
{
	struct NativeArcadeRetailRngSeedsV1 seeds;

	if ((draws == NULL) || (out == NULL))
	{
		return 0;
	}

	seeds.randomNumber = draws[NATIVE_ARCADE_BOT_RULES_SEED_RANDOM_NUMBER - 1] & NATIVE_ARCADE_BOT_RULES_RANDOM_NUMBER_MASK;
	seeds.advRng0 = draws[NATIVE_ARCADE_BOT_RULES_SEED_ADV_RNG0 - 1];
	seeds.advRng1 = draws[NATIVE_ARCADE_BOT_RULES_SEED_ADV_RNG1 - 1];
	/* BOTS_Adv_AdjustDifficulty's own rule (game/BOTS.c:310-313). */
	if ((seeds.advRng0 == 0) && (seeds.advRng1 == 0))
	{
		seeds.advRng0 = NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0;
		seeds.advRng1 = NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1;
	}
	seeds.psxRandSeed = draws[NATIVE_ARCADE_BOT_RULES_SEED_PSX_RAND - 1];
	seeds.audioRNG = draws[NATIVE_ARCADE_BOT_RULES_SEED_AUDIO_RNG - 1];
	*out = seeds;
	return 1;
}

int NativeArcadeBotRules_DeriveRetailSeedsV1(struct NativeDeterministicRngBankV1 *bank,
	struct NativeArcadeRetailRngSeedsV1 *out)
{
	struct NativeDeterministicRngBankV1 staged;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	uint32_t draws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT];

	if ((bank == NULL) || (out == NULL) || !NativeDeterministicRngBankV1_Validate(bank))
	{
		return 0;
	}

	staged = *bank;
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT; i++)
	{
		if (!NativeDeterministicRngBankV1_NextU32(&staged, (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
		        NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &draws[i]))
		{
			return 0;
		}
	}
	if (!NativeArcadeBotRules_MapRetailSeedsV1(draws, &seeds))
	{
		return 0;
	}
	*bank = staged;
	*out = seeds;
	return 1;
}

int NativeArcadeBotRules_ValidateConfigV1(const struct NativeMatchConfigV1 *config)
{
	uint8_t rulesDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t expectedBots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
	uint8_t aiSetIndex = 0;
	uint8_t cab1Slot = 0;
	uint8_t cab2Slot = 0;
	uint8_t botDifficulty = 0;
	uint32_t botCount = 0;
	uint32_t index = 0;

	if ((config == NULL) || !NativeMatchConfigV1_Validate(config) ||
	    (config->profile != NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB) || (config->gameMode1 != 0) ||
	    (config->gameMode2 != 0) || (config->rules != 0))
	{
		return 0;
	}
	if (!NativeArcadeBotRules_DigestV1(rulesDigest) ||
	    (memcmp(rulesDigest, config->botRulesDigest, sizeof(rulesDigest)) != 0))
	{
		return 0;
	}
	/* Both are uint32_t in the config: a value above 0xff is never a table value. */
	if ((config->trackID > 0xffu) || (config->lapCount > 0xffu) ||
	    !NativeMatchSelect_TrackIndex((uint8_t)config->trackID, &index) ||
	    !NativeMatchSelect_LapOptionIndex((uint8_t)config->lapCount, &index))
	{
		return 0;
	}

	if (!NativeMatchConfigV1_FindRoleSlot(config, NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot) ||
	    !NativeMatchConfigV1_FindRoleSlot(config, NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &cab2Slot) ||
	    (config->slots[cab1Slot].difficulty != 0) || (config->slots[cab2Slot].difficulty != 0) ||
	    !NativeArcadeBotRules_ExpectedBots2P(config->slots[cab1Slot].characterID, config->slots[cab2Slot].characterID,
	        expectedBots, &aiSetIndex))
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		const struct NativeMatchConfigSlotV1 *slot = &config->slots[i];

		if (slot->role != NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			continue;
		}
		if (botCount >= NATIVE_ARCADE_BOT_RULES_BOT_COUNT)
		{
			return 0;
		}
		if (botCount == 0)
		{
			botDifficulty = slot->difficulty;
		}
		if ((slot->characterID != expectedBots[botCount]) || (slot->difficulty != botDifficulty))
		{
			return 0;
		}
		botCount++;
	}
	if ((botCount != NATIVE_ARCADE_BOT_RULES_BOT_COUNT) || !NativeArcadeBotRules_IsDifficulty(botDifficulty))
	{
		return 0;
	}
	return 1;
}
