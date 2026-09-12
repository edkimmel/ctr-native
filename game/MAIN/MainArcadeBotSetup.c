#include "MAIN/MainArcadeBotSetup.h"

#include <string.h>

static int MainArcadeBotSetup_IsZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;
	for (size_t i = 0; i < size; i++) combined |= bytes[i];
	return combined == 0;
}

static int MainArcadeBotSetup_RosterMatches(
	const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRosterPlan *plan,
	const struct MainArcadeRosterValidated *validated)
{
	struct MainArcadeRosterPlan expected;

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
	}
	return 1;
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

	if ((config == NULL) || (sourceFacts == NULL) || (rngBefore == NULL) ||
	    (out == NULL) || (rngAfter == NULL)) return MAIN_ARCADE_BOT_SETUP_INVALID_ARGUMENT;
	if (!NativeMatchConfigV1_Validate(config)) return MAIN_ARCADE_BOT_SETUP_INVALID_CONFIG;
	if (!MainArcadeBotSetup_RosterMatches(config, rosterPlan, validatedRoster))
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
