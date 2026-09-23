#include "MAIN/MainArcadeRaceSetupPlan.h"

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MAIN_ARCADE_RACE_SETUP_PLAN_TAG_BYTES (sizeof(MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG) - 1u)

/* RS-21: the V2 encoding is the tag, then every plan field (the offset table
 * in the header); the tag keeps the v1 length. */
_Static_assert(MAIN_ARCADE_RACE_SETUP_PLAN_TAG_BYTES == 30u, "the v2 tag keeps the v1 tag length");
_Static_assert(MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES ==
		MAIN_ARCADE_RACE_SETUP_PLAN_TAG_BYTES + 4u * 1u /* locked, numPlyrNextGame, numLaps, boolDemoMode */ +
			4u /* profile */ + 6u * 4u /* levelID, the four mode masks, arcadeDifficulty */ +
			4u * 1u /* characterWriteMask, aiSetIndex, firstBotSlot, botCount */ + 2u /* reserved */ +
			MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT * 2u + NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT + 8u /* masterSeed */ +
			4u /* rngDerivationVersion */ + NATIVE_SHA256_DIGEST_BYTES,
	"PLAN_V2_ENCODED_BYTES must be the sum of the v2 fields");
_Static_assert(MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES == 135u, "the v2 encoding is 135 bytes");

/* RS-22: each profile's write mask covers exactly its humans and its bots,
 * whose slots and counts are the bot rules' own. */
_Static_assert(NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT == MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_TWO_CAB,
	"TWO_CAB: the bots follow the humans");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_TWO_CAB ==
		(1u << (NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_BOT_COUNT)) - 1u,
	"TWO_CAB: the write mask is the humans and the bots");
_Static_assert(NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT == MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_ONE_CAB,
	"ONE_CAB: the bots follow the human");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_ONE_CAB ==
		(1u << (NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT)) - 1u,
	"ONE_CAB: the write mask is the human and the bots");
_Static_assert(NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT ==
		MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT,
	"ONE_CAB: the race uses every characterIDs slot");
_Static_assert((NATIVE_ARCADE_BOT_RULES_BOT_COUNT <= NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT) &&
		(NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT <= NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT),
	"expectedBots holds either profile's bots");
_Static_assert(MAIN_ARCADE_RACE_SETUP_AI_SET_NONE >= MAIN_ARCADE_RACE_SETUP_AI_SET_COUNT, "AI_SET_NONE is no AI set index");

/* The fixed shape of a profile's plan (RS-22). */
struct MainArcadeRaceSetupPlanShape
{
	uint8_t numPlyrNextGame;
	uint8_t characterWriteMask;
	uint8_t firstBotSlot;
	uint8_t botCount;
};

/* 1 with *shape filled for ARCADE_TWO_CAB and ARCADE_ONE_CAB; 0 for any other profile. */
static int MainArcadeRaceSetupPlan_Shape(uint32_t profile, struct MainArcadeRaceSetupPlanShape *shape)
{
	if (profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB)
	{
		shape->numPlyrNextGame = (uint8_t)MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_TWO_CAB;
		shape->characterWriteMask = (uint8_t)MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_TWO_CAB;
		shape->firstBotSlot = (uint8_t)NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT;
		shape->botCount = (uint8_t)NATIVE_ARCADE_BOT_RULES_BOT_COUNT;
		return 1;
	}
	if (profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB)
	{
		shape->numPlyrNextGame = (uint8_t)MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_ONE_CAB;
		shape->characterWriteMask = (uint8_t)MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_ONE_CAB;
		shape->firstBotSlot = (uint8_t)NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT;
		shape->botCount = (uint8_t)NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT;
		return 1;
	}
	return 0;
}

/* The fixed fields every built plan carries, per profile; anything else is refused. */
static int MainArcadeRaceSetupPlan_IsWellFormed(const struct MainArcadeRaceSetupPlan *plan)
{
	struct MainArcadeRaceSetupPlanShape shape;
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT];
	uint8_t aiSetIndex = 0;

	if (!MainArcadeRaceSetupPlan_Shape(plan->profile, &shape))
	{
		return 0;
	}
	if (!((plan->locked == 1u) && (plan->numPlyrNextGame == shape.numPlyrNextGame) && (plan->boolDemoMode == 0u) &&
	        (plan->gameMode1ClearMask == MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK) &&
	        (plan->gameMode1SetMask == MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK) &&
	        (plan->gameMode2ClearMask == MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK) &&
	        (plan->gameMode2SetMask == MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK) &&
	        (plan->characterWriteMask == shape.characterWriteMask) && (plan->firstBotSlot == shape.firstBotSlot) &&
	        (plan->botCount == shape.botCount) && (plan->reserved[0] == 0u) && (plan->reserved[1] == 0u) &&
	        (plan->arcadeDifficulty >= 0) && NativeArcadeBotRules_IsDifficulty((uint32_t)plan->arcadeDifficulty)))
	{
		return 0;
	}
	/* The bots are the bot rules' result for the humans: TWO_CAB, the
	 * LOAD_Robots2P bots of humans 0 and 1 and their retail 2P AI set; ONE_CAB,
	 * the LOAD_Robots1P bots of human 0 and no AI set. bots stays 0 past the
	 * profile's botCount. */
	memset(bots, 0, sizeof(bots));
	if ((plan->characterIDs[0] < 0) || (plan->characterIDs[0] > (int16_t)UINT8_MAX))
	{
		return 0;
	}
	if (plan->profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB)
	{
		if ((plan->characterIDs[1] < 0) || (plan->characterIDs[1] > (int16_t)UINT8_MAX) ||
		    !NativeArcadeBotRules_ExpectedBots2P((uint8_t)plan->characterIDs[0], (uint8_t)plan->characterIDs[1], bots,
		        &aiSetIndex))
		{
			return 0;
		}
	}
	else
	{
		if (!NativeArcadeBotRules_ExpectedBots1P((uint8_t)plan->characterIDs[0], bots))
		{
			return 0;
		}
		aiSetIndex = (uint8_t)MAIN_ARCADE_RACE_SETUP_AI_SET_NONE;
	}
	if (plan->aiSetIndex != aiSetIndex)
	{
		return 0;
	}
	/* The slots the plan does not own carry 0. */
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		if (((((uint32_t)plan->characterWriteMask >> slot) & 1u) == 0u) && (plan->characterIDs[slot] != 0))
		{
			return 0;
		}
	}
	/* expectedBots is exactly the humans' bots, the unused tail 0, and the bot
	 * slots carry exactly the expected bots. */
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT; i++)
	{
		if ((plan->expectedBots[i] != bots[i]) ||
		    ((i < plan->botCount) && (plan->characterIDs[plan->firstBotSlot + i] != (int16_t)plan->expectedBots[i])))
		{
			return 0;
		}
	}
	return 1;
}

int MainArcadeRaceSetupPlan_Build(const struct NativeMatchConfigV1 *config, struct MainArcadeRaceSetupPlan *out)
{
	struct MainArcadeRaceSetupPlan candidate;
	struct MainArcadeRaceSetupPlanShape shape;
	uint8_t cab1Slot = 0;
	uint8_t cab2Slot = 0;

	if ((config == NULL) || (out == NULL) || !NativeArcadeBotRules_ValidateConfigV1(config) ||
	    !MainArcadeRaceSetupPlan_Shape(config->profile, &shape) ||
	    (config->tickRateNumerator != MAIN_ARCADE_RACE_SETUP_TICK_RATE_NUMERATOR) ||
	    (config->tickRateDenominator != MAIN_ARCADE_RACE_SETUP_TICK_RATE_DENOMINATOR) ||
	    !NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot) ||
	    (cab1Slot != 0u) || (config->lapCount > (uint32_t)INT8_MAX) || (config->trackID > (uint32_t)INT32_MAX))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	if (config->profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB)
	{
		/* CAB1 and CAB2 are retail players 0 and 1; the bots follow LOAD_Robots2P. */
		if (!NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &cab2Slot) ||
		    (cab2Slot != 1u) ||
		    !NativeArcadeBotRules_ExpectedBots2P(config->slots[0].characterID, config->slots[1].characterID,
		        candidate.expectedBots, &candidate.aiSetIndex))
		{
			return 0;
		}
	}
	else
	{
		/* ONE_CAB: CAB1 is retail player 0, there is no CAB2, and the bots are
		 * the LOAD_Robots1P result; 1P uses no AI set. */
		if (NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &cab2Slot) ||
		    !NativeArcadeBotRules_ExpectedBots1P(config->slots[0].characterID, candidate.expectedBots))
		{
			return 0;
		}
		candidate.aiSetIndex = (uint8_t)MAIN_ARCADE_RACE_SETUP_AI_SET_NONE;
	}
	if (!NativeMatchConfigV1_Digest(config, candidate.configDigest))
	{
		return 0;
	}
	candidate.locked = 1u;
	candidate.numPlyrNextGame = shape.numPlyrNextGame;
	candidate.numLaps = (int8_t)config->lapCount;
	candidate.boolDemoMode = 0u;
	candidate.profile = config->profile;
	candidate.levelID = (int32_t)config->trackID;
	candidate.gameMode1ClearMask = MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK;
	candidate.gameMode1SetMask = MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK;
	candidate.gameMode2ClearMask = MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK;
	candidate.gameMode2SetMask = MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK;
	/* ValidateConfigV1: every bot slot carries the same table difficulty. */
	candidate.arcadeDifficulty = (int32_t)config->slots[shape.firstBotSlot].difficulty;
	candidate.characterWriteMask = shape.characterWriteMask;
	candidate.firstBotSlot = shape.firstBotSlot;
	candidate.botCount = shape.botCount;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		if ((((uint32_t)shape.characterWriteMask >> slot) & 1u) != 0u)
		{
			candidate.characterIDs[slot] = (int16_t)config->slots[slot].characterID;
		}
	}
	candidate.masterSeed = config->masterSeed;
	candidate.rngDerivationVersion = config->rngDerivationVersion;
	/* ValidateConfigV1 fixes the bot slots to the expected bots, so this holds;
	 * checked anyway, so Build never returns a plan Apply would refuse. */
	if (!MainArcadeRaceSetupPlan_IsWellFormed(&candidate))
	{
		return 0;
	}

	*out = candidate;
	return 1;
}

int MainArcadeRaceSetupPlan_Apply(const struct MainArcadeRaceSetupPlan *plan,
	const struct MainArcadeRaceSetupRetailFields *before, struct MainArcadeRaceSetupRetailFields *after)
{
	struct MainArcadeRaceSetupRetailFields candidate;

	if ((plan == NULL) || (before == NULL) || (after == NULL) || !MainArcadeRaceSetupPlan_IsWellFormed(plan))
	{
		return 0;
	}

	candidate = *before;
	candidate.levelID = plan->levelID;
	candidate.numLaps = plan->numLaps;
	candidate.numPlyrNextGame = plan->numPlyrNextGame;
	candidate.gameMode1 = (before->gameMode1 & ~plan->gameMode1ClearMask) | plan->gameMode1SetMask;
	candidate.gameMode2 = (before->gameMode2 & ~plan->gameMode2ClearMask) | plan->gameMode2SetMask;
	candidate.arcadeDifficulty = plan->arcadeDifficulty;
	candidate.boolDemoMode = plan->boolDemoMode;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		if (((plan->characterWriteMask >> slot) & 1u) != 0u)
		{
			candidate.characterIDs[slot] = plan->characterIDs[slot];
		}
	}

	*after = candidate;
	return 1;
}

int MainArcadeRaceSetupPlan_Digest(const struct MainArcadeRaceSetupPlan *plan, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t bytes[MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES];
	struct NativeCodecWriter writer;
	struct NativeSha256 sha;
	int ok;

	if ((plan == NULL) || (digest == NULL) || !MainArcadeRaceSetupPlan_IsWellFormed(plan))
	{
		return 0;
	}

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	ok = NativeCodecWriter_WriteBytes(&writer, MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG, MAIN_ARCADE_RACE_SETUP_PLAN_TAG_BYTES) &&
	     NativeCodecWriter_WriteU8(&writer, plan->locked) && NativeCodecWriter_WriteU8(&writer, plan->numPlyrNextGame) &&
	     NativeCodecWriter_WriteU8(&writer, (uint8_t)plan->numLaps) && NativeCodecWriter_WriteU8(&writer, plan->boolDemoMode) &&
	     NativeCodecWriter_WriteU32(&writer, plan->profile) && NativeCodecWriter_WriteU32(&writer, (uint32_t)plan->levelID) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode1ClearMask) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode1SetMask) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode2ClearMask) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode2SetMask) &&
	     NativeCodecWriter_WriteU32(&writer, (uint32_t)plan->arcadeDifficulty) &&
	     NativeCodecWriter_WriteU8(&writer, plan->characterWriteMask) && NativeCodecWriter_WriteU8(&writer, plan->aiSetIndex) &&
	     NativeCodecWriter_WriteU8(&writer, plan->firstBotSlot) && NativeCodecWriter_WriteU8(&writer, plan->botCount) &&
	     NativeCodecWriter_WriteBytes(&writer, plan->reserved, sizeof(plan->reserved));
	for (uint32_t slot = 0; ok && (slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT); slot++)
	{
		ok = NativeCodecWriter_WriteU16(&writer, (uint16_t)plan->characterIDs[slot]);
	}
	ok = ok && NativeCodecWriter_WriteBytes(&writer, plan->expectedBots, sizeof(plan->expectedBots)) &&
	     NativeCodecWriter_WriteU64(&writer, plan->masterSeed) &&
	     NativeCodecWriter_WriteU32(&writer, plan->rngDerivationVersion) &&
	     NativeCodecWriter_WriteBytes(&writer, plan->configDigest, sizeof(plan->configDigest));
	if (!ok || (NativeCodecWriter_Size(&writer) != MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES))
	{
		return 0;
	}

	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, bytes, sizeof(bytes));
	NativeSha256_Final(&sha, digest);
	return 1;
}
