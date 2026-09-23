#include "MAIN/MainArcadeRaceSetupPlan.h"

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MAIN_ARCADE_RACE_SETUP_PLAN_TAG_BYTES (sizeof(MAIN_ARCADE_RACE_SETUP_PLAN_V1_TAG) - 1u)

/* The fixed fields every built plan carries; anything else is refused. */
static int MainArcadeRaceSetupPlan_IsWellFormed(const struct MainArcadeRaceSetupPlan *plan)
{
	return (plan->locked == 1u) && (plan->numPlyrNextGame == MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS) &&
	       (plan->boolDemoMode == 0u) && (plan->gameMode1ClearMask == MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK) &&
	       (plan->gameMode1SetMask == MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK) &&
	       (plan->gameMode2ClearMask == MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK) &&
	       (plan->gameMode2SetMask == MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK) &&
	       (plan->characterWriteMask == MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK) && (plan->reserved[0] == 0u) &&
	       (plan->reserved[1] == 0u) && (plan->characterIDs[6] == 0) && (plan->characterIDs[7] == 0);
}

int MainArcadeRaceSetupPlan_Build(const struct NativeMatchConfigV1 *config, struct MainArcadeRaceSetupPlan *out)
{
	struct MainArcadeRaceSetupPlan candidate;
	uint8_t cab1Slot = 0;
	uint8_t cab2Slot = 0;

	if ((config == NULL) || (out == NULL) || !NativeArcadeBotRules_ValidateConfigV1(config) ||
	    (config->tickRateNumerator != MAIN_ARCADE_RACE_SETUP_TICK_RATE_NUMERATOR) ||
	    (config->tickRateDenominator != MAIN_ARCADE_RACE_SETUP_TICK_RATE_DENOMINATOR) ||
	    !NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot) ||
	    !NativeMatchConfigV1_FindRoleSlot(config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &cab2Slot) ||
	    (cab1Slot != 0u) || (cab2Slot != 1u) || (config->lapCount > (uint32_t)INT8_MAX) ||
	    (config->trackID > (uint32_t)INT32_MAX))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	if (!NativeArcadeBotRules_ExpectedBots2P(config->slots[0].characterID, config->slots[1].characterID,
	        candidate.expectedBots, &candidate.aiSetIndex) ||
	    !NativeMatchConfigV1_Digest(config, candidate.configDigest))
	{
		return 0;
	}
	candidate.locked = 1u;
	candidate.numPlyrNextGame = (uint8_t)MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS;
	candidate.numLaps = (int8_t)config->lapCount;
	candidate.boolDemoMode = 0u;
	candidate.levelID = (int32_t)config->trackID;
	candidate.gameMode1ClearMask = MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK;
	candidate.gameMode1SetMask = MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK;
	candidate.gameMode2ClearMask = MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK;
	candidate.gameMode2SetMask = MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK;
	/* ValidateConfigV1: every bot slot carries the same table difficulty. */
	candidate.arcadeDifficulty = (int32_t)config->slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT].difficulty;
	candidate.characterWriteMask = (uint8_t)MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		if (((MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK >> slot) & 1u) != 0u)
		{
			candidate.characterIDs[slot] = (int16_t)config->slots[slot].characterID;
		}
	}
	candidate.masterSeed = config->masterSeed;
	candidate.rngDerivationVersion = config->rngDerivationVersion;

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
	uint8_t bytes[MAIN_ARCADE_RACE_SETUP_PLAN_V1_ENCODED_BYTES];
	struct NativeCodecWriter writer;
	struct NativeSha256 sha;
	int ok;

	if ((plan == NULL) || (digest == NULL) || !MainArcadeRaceSetupPlan_IsWellFormed(plan))
	{
		return 0;
	}

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	ok = NativeCodecWriter_WriteBytes(&writer, MAIN_ARCADE_RACE_SETUP_PLAN_V1_TAG, MAIN_ARCADE_RACE_SETUP_PLAN_TAG_BYTES) &&
	     NativeCodecWriter_WriteU8(&writer, plan->locked) && NativeCodecWriter_WriteU8(&writer, plan->numPlyrNextGame) &&
	     NativeCodecWriter_WriteU8(&writer, (uint8_t)plan->numLaps) && NativeCodecWriter_WriteU8(&writer, plan->boolDemoMode) &&
	     NativeCodecWriter_WriteU32(&writer, (uint32_t)plan->levelID) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode1ClearMask) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode1SetMask) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode2ClearMask) &&
	     NativeCodecWriter_WriteU32(&writer, plan->gameMode2SetMask) &&
	     NativeCodecWriter_WriteU32(&writer, (uint32_t)plan->arcadeDifficulty) &&
	     NativeCodecWriter_WriteU8(&writer, plan->characterWriteMask) && NativeCodecWriter_WriteU8(&writer, plan->aiSetIndex) &&
	     NativeCodecWriter_WriteBytes(&writer, plan->reserved, sizeof(plan->reserved));
	for (uint32_t slot = 0; ok && (slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT); slot++)
	{
		ok = NativeCodecWriter_WriteU16(&writer, (uint16_t)plan->characterIDs[slot]);
	}
	ok = ok && NativeCodecWriter_WriteBytes(&writer, plan->expectedBots, sizeof(plan->expectedBots)) &&
	     NativeCodecWriter_WriteU64(&writer, plan->masterSeed) &&
	     NativeCodecWriter_WriteU32(&writer, plan->rngDerivationVersion) &&
	     NativeCodecWriter_WriteBytes(&writer, plan->configDigest, sizeof(plan->configDigest));
	if (!ok || (NativeCodecWriter_Size(&writer) != MAIN_ARCADE_RACE_SETUP_PLAN_V1_ENCODED_BYTES))
	{
		return 0;
	}

	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, bytes, sizeof(bytes));
	NativeSha256_Final(&sha, digest);
	return 1;
}
