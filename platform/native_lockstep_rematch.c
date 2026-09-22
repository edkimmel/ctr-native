#include "platform/native_lockstep_rematch.h"

#include <string.h>

int NativeLockstepRematch_BuildConfig(const struct NativeMatchConfigV1 *previous, uint64_t newMasterSeed,
                                       struct NativeMatchConfigV1 *next)
{
	struct NativeMatchConfigV1 built;

	if ((previous == NULL) || (next == NULL) || !NativeMatchConfigV1_Validate(previous) ||
	    (newMasterSeed == previous->masterSeed))
	{
		return 0;
	}

	if (previous->profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB)
	{
		NativeMatchConfigV1_InitArcadeTwoCab(&built);
	}
	else
	{
		NativeMatchConfigV1_InitArcadeOneCab(&built);
	}

	built.trackID = previous->trackID;
	built.gameMode1 = previous->gameMode1;
	built.gameMode2 = previous->gameMode2;
	built.rules = previous->rules;
	built.lapCount = previous->lapCount;
	built.tickRateNumerator = previous->tickRateNumerator;
	built.tickRateDenominator = previous->tickRateDenominator;
	built.masterSeed = newMasterSeed;
	memcpy(built.buildIdentity, previous->buildIdentity, sizeof(built.buildIdentity));
	memcpy(built.contentIdentity, previous->contentIdentity, sizeof(built.contentIdentity));
	memcpy(built.botRulesDigest, previous->botRulesDigest, sizeof(built.botRulesDigest));

	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		built.slots[i].characterID = previous->slots[i].characterID;
		built.slots[i].difficulty = previous->slots[i].difficulty;
	}

	if (!NativeMatchConfigV1_Validate(&built))
	{
		return 0;
	}

	*next = built;
	return 1;
}
