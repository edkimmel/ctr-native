#include "MAIN/MainArcadeBotSetup.h"

#include <string.h>

int MainArcadeBotSetupPlan_Digest(const struct MainArcadeBotSetupPlan *plan, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t bytes[4 + 4 + 1 + 1 + 2 + 32 + 32 + 32 + MAIN_ARCADE_BOT_SETUP_SLOT_COUNT * (1+1+1+1+1+1+1+1+1+3+4)];
	struct NativeCodecWriter w; struct NativeSha256 sha;
	if (!plan || !digest || plan->locked != 1 || plan->botCount > MAIN_ARCADE_BOT_SETUP_SLOT_COUNT ||
		!MainArcadeBotSetup_IsZero(plan->reserved,sizeof(plan->reserved))) return 0;
	NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);
	if (!NativeCodecWriter_WriteU32(&w,plan->profile)||!NativeCodecWriter_WriteU32(&w,plan->botMask)||!NativeCodecWriter_WriteU8(&w,plan->botCount)||!NativeCodecWriter_WriteU8(&w,plan->locked)||!NativeCodecWriter_WriteBytes(&w,plan->reserved,2)||!NativeCodecWriter_WriteBytes(&w,plan->matchConfigDigest,32)||!NativeCodecWriter_WriteBytes(&w,plan->rngBeforeDigest,32)||!NativeCodecWriter_WriteBytes(&w,plan->rngAfterDigest,32)) return 0;
	for(uint8_t i=0;i<MAIN_ARCADE_BOT_SETUP_SLOT_COUNT;i++){const struct MainArcadeBotSetupAssignment *a=&plan->assignments[i]; if(!NativeCodecWriter_WriteU8(&w,a->enabled)||!NativeCodecWriter_WriteU8(&w,a->stableSlot)||!NativeCodecWriter_WriteU8(&w,a->nativeDriverSlot)||!NativeCodecWriter_WriteU8(&w,a->characterID)||!NativeCodecWriter_WriteU8(&w,a->difficulty)||!NativeCodecWriter_WriteU8(&w,a->spawnOrder)||!NativeCodecWriter_WriteU8(&w,a->navPathIndex)||!NativeCodecWriter_WriteU8(&w,a->accelerationOrder)||!NativeCodecWriter_WriteU8(&w,a->setupSequence)||!NativeCodecWriter_WriteBytes(&w,a->reserved,3)||!NativeCodecWriter_WriteU32(&w,a->setupRandom))return 0;}
	NativeSha256_Init(&sha); NativeSha256_Update(&sha,bytes,w.offset); NativeSha256_Final(&sha,digest); return 1;
}

static int MainArcadeBotSetup_IsZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;
	for (size_t i = 0; i < size; i++) combined |= bytes[i];
	return combined == 0;
}

static int MainArcadeBotSetup_RosterMatches(
	const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRosterPlan *plan,
	const struct MainArcadeRosterValidated *validated,
	uint8_t navOwnership[MAIN_ARCADE_BOT_SETUP_SLOT_COUNT])
{
	struct MainArcadeRosterPlan expected;
	uint32_t expectedBotMask = 0;
	uint32_t navSeen = 0;

	if ((plan == NULL) || (validated == NULL) ||
	    !MainArcadeRoster_BuildPlan(config, &expected) ||
	    (memcmp(plan, &expected, sizeof(expected)) != 0) ||
	    (validated->presenceMask != plan->presenceMask) ||
	    (validated->humanCount != plan->humanCount) ||
	    (validated->botCount != plan->botCount) ||
	    (validated->driverCount != plan->driverCount) ||
	    (validated->reserved != 0) ||
	    (memcmp(validated->matchConfigDigest, plan->matchConfigDigest,
	            sizeof(plan->matchConfigDigest)) != 0) ||
	    (validated->roster.prelude.presenceMask != plan->presenceMask) ||
	    (validated->roster.prelude.playerCount != plan->humanCount) ||
	    (validated->roster.prelude.activeBotCount != plan->botCount))
	{
		return 0;
	}
	memset(navOwnership, MAIN_ARCADE_BOT_SETUP_SLOT_NONE,
	       MAIN_ARCADE_BOT_SETUP_SLOT_COUNT * sizeof(navOwnership[0]));
	for (uint8_t slot = 0; slot < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; slot++)
	{
		const struct MainArcadeRosterPlanSlot *expectedSlot = &plan->slots[slot];
		const struct MainArcadeRosterNativeSlotFacts *observed = &validated->slots[slot];
		if (!expectedSlot->present)
		{
			static const struct MainArcadeRosterNativeSlotFacts zero = {0};
			if ((memcmp(observed, &zero, sizeof(zero)) != 0) ||
			    (validated->roster.kind[slot] != 0) ||
			    (validated->roster.behaviorID[slot] != 0) ||
			    (validated->roster.threadBehaviorID[slot] != 0)) return 0;
			continue;
		}
		if ((observed->present != 1) || (observed->driverID != slot) ||
		    (observed->role != expectedSlot->role) ||
		    (observed->initialLifecycle != expectedSlot->initialLifecycle) ||
		    (observed->characterID != expectedSlot->characterID) ||
		    (observed->difficulty != expectedSlot->difficulty) ||
		    !MainArcadeBotSetup_IsZero(observed->reserved, sizeof(observed->reserved)) ||
		    (validated->roster.kind[slot] != expectedSlot->kind) ||
		    !NativeCanonicalDriverBehavior_ValidateKind(validated->roster.kind[slot],
		        validated->roster.behaviorID[slot],
		        validated->roster.threadBehaviorID[slot])) return 0;
		if (expectedSlot->role == NATIVE_MATCH_SLOT_ROLE_BOT)
			expectedBotMask |= UINT32_C(1) << slot;
	}
	for (uint8_t path = 0; path < MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT; path++)
	{
		const uint8_t count = validated->roster.prelude.navListCount[path];
		if (count > MAIN_ARCADE_BOT_SETUP_SLOT_COUNT) return 0;
		for (uint8_t index = 0; index < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; index++)
		{
			const uint8_t slot = validated->roster.prelude.navListOrder[path][index];
			if (index >= count)
			{
				if (slot != MAIN_ARCADE_BOT_SETUP_SLOT_NONE) return 0;
				continue;
			}
			if ((slot >= MAIN_ARCADE_BOT_SETUP_SLOT_COUNT) ||
			    ((expectedBotMask & (UINT32_C(1) << slot)) == 0) ||
			    ((navSeen & (UINT32_C(1) << slot)) != 0)) return 0;
			navSeen |= UINT32_C(1) << slot;
			navOwnership[slot] = path;
		}
	}
	return navSeen == expectedBotMask;
}

static int MainArcadeBotSetup_InactiveIsValid(const struct MainArcadeBotSetupSourceSlot *fact)
{
	return (fact->present == 0) && (fact->nativeDriverSlot == MAIN_ARCADE_BOT_SETUP_SLOT_NONE) &&
	       (fact->role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) && (fact->characterID == 0) &&
	       (fact->difficulty == 0) && (fact->spawnOrder == MAIN_ARCADE_BOT_SETUP_SLOT_NONE) &&
	       (fact->navPathIndex == MAIN_ARCADE_BOT_SETUP_SLOT_NONE) &&
	       (fact->accelerationOrder == MAIN_ARCADE_BOT_SETUP_SLOT_NONE) &&
	       MainArcadeBotSetup_IsZero(fact->reserved, sizeof(fact->reserved));
}

enum MainArcadeBotSetupResult MainArcadeBotSetup_Plan(
	const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRosterPlan *rosterPlan,
	const struct MainArcadeRosterValidated *validatedRoster,
	const struct MainArcadeBotSetupSourceFacts *sourceFacts,
	const struct NativeDeterministicRngBankV1 *rngBefore,
	struct MainArcadeBotSetupPlan *out,
	struct NativeDeterministicRngBankV1 *rngAfter)
{
	const struct MainArcadeBotSetupSourceSlot *bySlot[MAIN_ARCADE_BOT_SETUP_SLOT_COUNT] = {0};
	struct NativeDeterministicRngBankV1 stagedRng;
	struct MainArcadeBotSetupPlan candidate;
	uint32_t seenSlots = 0;
	uint32_t seenSpawn = 0;
	uint32_t seenAcceleration = 0;
	uint8_t setupSequence = 0;
	uint8_t navOwnership[MAIN_ARCADE_BOT_SETUP_SLOT_COUNT];

	if ((config == NULL) || (sourceFacts == NULL) || (rngBefore == NULL) ||
	    (out == NULL) || (rngAfter == NULL)) return MAIN_ARCADE_BOT_SETUP_INVALID_ARGUMENT;
	if (!NativeMatchConfigV1_Validate(config)) return MAIN_ARCADE_BOT_SETUP_INVALID_CONFIG;
	if (!MainArcadeBotSetup_RosterMatches(config, rosterPlan, validatedRoster, navOwnership))
		return MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER;
	if (!NativeDeterministicRngBankV1_Validate(rngBefore) ||
	    (rngBefore->masterSeed != config->masterSeed) ||
	    (rngBefore->derivationVersion != config->rngDerivationVersion))
		return MAIN_ARCADE_BOT_SETUP_INVALID_RNG;
	if ((sourceFacts->factCount > MAIN_ARCADE_BOT_SETUP_SLOT_COUNT) ||
	    !MainArcadeBotSetup_IsZero(sourceFacts->reserved, sizeof(sourceFacts->reserved)))
		return MAIN_ARCADE_BOT_SETUP_FACT_COUNT;

	for (uint8_t index = 0; index < sourceFacts->factCount; index++)
	{
		const struct MainArcadeBotSetupSourceSlot *fact = &sourceFacts->facts[index];
		if (fact->stableSlot >= MAIN_ARCADE_BOT_SETUP_SLOT_COUNT)
			return MAIN_ARCADE_BOT_SETUP_RANGE;
		if ((seenSlots & (UINT32_C(1) << fact->stableSlot)) != 0)
			return MAIN_ARCADE_BOT_SETUP_DUPLICATE_SLOT;
		seenSlots |= UINT32_C(1) << fact->stableSlot;
		bySlot[fact->stableSlot] = fact;
	}
	if (seenSlots != UINT32_C(0xff)) return MAIN_ARCADE_BOT_SETUP_MISSING_SLOT;

	memset(&candidate, 0, sizeof(candidate));
	candidate.profile = config->profile;
	candidate.botCount = rosterPlan->botCount;
	candidate.locked = 1;
	memcpy(candidate.matchConfigDigest, rosterPlan->matchConfigDigest,
	       sizeof(candidate.matchConfigDigest));
	stagedRng = *rngBefore;
	if (!NativeDeterministicRngBankV1_Digest(rngBefore, candidate.rngBeforeDigest))
		return MAIN_ARCADE_BOT_SETUP_INVALID_RNG;

	for (uint8_t slot = 0; slot < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; slot++)
	{
		const struct MainArcadeRosterPlanSlot *expected = &rosterPlan->slots[slot];
		const struct MainArcadeBotSetupSourceSlot *fact = bySlot[slot];
		if (!expected->present)
		{
			if (!MainArcadeBotSetup_InactiveIsValid(fact))
				return MAIN_ARCADE_BOT_SETUP_INVALID_INACTIVE;
			continue;
		}
		if (fact->present != 1) return MAIN_ARCADE_BOT_SETUP_MISSING_ACTIVE;
		if (fact->role != expected->role) return MAIN_ARCADE_BOT_SETUP_OWNERSHIP;
		if (fact->nativeDriverSlot != slot) return MAIN_ARCADE_BOT_SETUP_COMPACTION;
		if ((fact->characterID != expected->characterID) ||
		    (fact->difficulty != expected->difficulty)) return MAIN_ARCADE_BOT_SETUP_MISMATCH;
		if ((fact->spawnOrder >= MAIN_ARCADE_BOT_SETUP_SLOT_COUNT) ||
		    (fact->navPathIndex >= MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT) ||
		    (fact->accelerationOrder >= MAIN_ARCADE_BOT_SETUP_SLOT_COUNT) ||
		    !MainArcadeBotSetup_IsZero(fact->reserved, sizeof(fact->reserved)))
			return MAIN_ARCADE_BOT_SETUP_RANGE;
		if ((expected->role == NATIVE_MATCH_SLOT_ROLE_BOT) &&
		    (fact->navPathIndex != navOwnership[slot]))
			return MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH;
		if ((seenSpawn & (UINT32_C(1) << fact->spawnOrder)) != 0)
			return MAIN_ARCADE_BOT_SETUP_DUPLICATE_SPAWN;
		if ((seenAcceleration & (UINT32_C(1) << fact->accelerationOrder)) != 0)
			return MAIN_ARCADE_BOT_SETUP_DUPLICATE_ACCELERATION;
		seenSpawn |= UINT32_C(1) << fact->spawnOrder;
		seenAcceleration |= UINT32_C(1) << fact->accelerationOrder;

		if (expected->role == NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			struct MainArcadeBotSetupAssignment *assignment = &candidate.assignments[slot];
			assignment->enabled = 1;
			assignment->stableSlot = slot;
			assignment->nativeDriverSlot = fact->nativeDriverSlot;
			assignment->characterID = fact->characterID;
			assignment->difficulty = fact->difficulty;
			assignment->spawnOrder = fact->spawnOrder;
			assignment->navPathIndex = fact->navPathIndex;
			assignment->accelerationOrder = fact->accelerationOrder;
			assignment->setupSequence = setupSequence++;
			if (!NativeDeterministicRngBankV1_NextU32(&stagedRng,
			        NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
			        NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
			        NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
			        &assignment->setupRandom)) return MAIN_ARCADE_BOT_SETUP_INVALID_RNG;
			candidate.botMask |= UINT32_C(1) << slot;
		}
	}
	if ((setupSequence != rosterPlan->botCount) ||
	    !NativeDeterministicRngBankV1_Digest(&stagedRng, candidate.rngAfterDigest))
		return MAIN_ARCADE_BOT_SETUP_INVALID_RNG;

	*out = candidate;
	*rngAfter = stagedRng;
	return MAIN_ARCADE_BOT_SETUP_OK;
}
