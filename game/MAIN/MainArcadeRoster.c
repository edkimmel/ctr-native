#include "MAIN/MainArcadeRoster.h"

#include <string.h>

static uint8_t MainArcadeRoster_KindForRole(uint8_t role)
{
	return role == NATIVE_MATCH_SLOT_ROLE_BOT ? NATIVE_CANONICAL_DRIVER_KIND_BOT :
	       role == NATIVE_MATCH_SLOT_ROLE_INACTIVE ? MAIN_ARCADE_ROSTER_SLOT_NONE :
	                                                   NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
}

static int MainArcadeRoster_IsZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;
	for (size_t i = 0; i < size; i++) combined |= bytes[i];
	return combined == 0;
}

int MainArcadeRoster_BuildPlan(const struct NativeMatchConfigV1 *config,
	struct MainArcadeRosterPlan *out)
{
	struct MainArcadeRosterPlan candidate;

	if ((out == NULL) || !NativeMatchConfigV1_Validate(config)) return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.profile = config->profile;
	candidate.locked = 1;
	for (uint8_t slot = 0; slot < MAIN_ARCADE_ROSTER_SLOT_COUNT; slot++)
	{
		const struct NativeMatchConfigSlotV1 *source = &config->slots[slot];
		struct MainArcadeRosterPlanSlot *target = &candidate.slots[slot];
		target->stableSlot = slot;
		target->role = source->role;
		target->initialLifecycle = source->initialLifecycle;
		target->characterID = source->characterID;
		target->difficulty = source->difficulty;
		target->kind = MainArcadeRoster_KindForRole(source->role);
		if (source->role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) continue;
		target->present = 1;
		candidate.presenceMask |= UINT32_C(1) << slot;
		candidate.driverCount++;
		if (source->role == NATIVE_MATCH_SLOT_ROLE_BOT) candidate.botCount++;
		else candidate.humanCount++;
	}
	if (!NativeMatchConfigV1_Digest(config, candidate.matchConfigDigest)) return 0;
	*out = candidate;
	return 1;
}

static int MainArcadeRoster_PlanMatchesConfig(const struct MainArcadeRosterPlan *plan,
	const struct NativeMatchConfigV1 *config)
{
	struct MainArcadeRosterPlan expected;

	if ((plan == NULL) || (plan->locked != 1) ||
	    !MainArcadeRoster_BuildPlan(config, &expected)) return 0;
	return memcmp(plan, &expected, sizeof(expected)) == 0;
}

static int MainArcadeRoster_InactiveFactsAreZero(const struct MainArcadeRosterNativeSlotFacts *slot)
{
	return slot->present == 0 && slot->driverID == 0 && slot->role == 0 &&
	       slot->initialLifecycle == 0 && slot->characterID == 0 &&
	       slot->difficulty == 0 && MainArcadeRoster_IsZero(slot->reserved, sizeof(slot->reserved));
}

int MainArcadeRoster_ValidateNativeFacts(const struct MainArcadeRosterPlan *plan,
	const struct NativeMatchConfigV1 *lockedConfig,
	const struct MainArcadeRosterNativeFacts *facts,
	struct MainArcadeRosterValidated *out)
{
	struct MainArcadeRosterValidated candidate;
	struct NativeCanonicalDriversRosterCandidate roster;
	uint8_t prefix = 0;

	if ((facts == NULL) || (out == NULL) || !MainArcadeRoster_PlanMatchesConfig(plan, lockedConfig) ||
	    (plan->driverCount > MAIN_ARCADE_ROSTER_SLOT_COUNT) ||
	    (facts->numPlyrCurrGame != plan->humanCount) ||
	    (facts->numBotsNextGame != plan->botCount) ||
	    (facts->nativeDriverCount != plan->driverCount) ||
	    !NativeCanonicalDriversRoster_Normalize(&facts->rosterInput, &roster) ||
	    (roster.prelude.presenceMask != plan->presenceMask) ||
	    (roster.prelude.playerCount != plan->humanCount) ||
	    (roster.prelude.activeBotCount != plan->botCount) ||
	    (lockedConfig->lapCount > INT8_MAX) ||
	    (roster.prelude.numLaps != (int8_t)lockedConfig->lapCount))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	candidate.presenceMask = plan->presenceMask;
	candidate.humanCount = plan->humanCount;
	candidate.botCount = plan->botCount;
	candidate.driverCount = plan->driverCount;
	memcpy(candidate.matchConfigDigest, plan->matchConfigDigest, sizeof(candidate.matchConfigDigest));
	candidate.roster = roster;

	for (uint8_t slot = 0; slot < MAIN_ARCADE_ROSTER_SLOT_COUNT; slot++)
	{
		const struct MainArcadeRosterPlanSlot *expected = &plan->slots[slot];
		const struct MainArcadeRosterNativeSlotFacts *observed = &facts->slots[slot];
		if (!expected->present)
		{
			if (!MainArcadeRoster_InactiveFactsAreZero(observed) ||
			    facts->rosterInput.slots[slot].present != 0) return 0;
			continue;
		}
		if ((prefix >= facts->nativeDriverCount) || (facts->nativeDriverSlots[prefix] != slot) ||
		    (observed->present != 1) || (observed->driverID != slot) ||
		    (observed->role != expected->role) ||
		    (observed->initialLifecycle != expected->initialLifecycle) ||
		    (observed->characterID != expected->characterID) ||
		    (observed->difficulty != expected->difficulty) ||
		    !MainArcadeRoster_IsZero(observed->reserved, sizeof(observed->reserved)) ||
		    (roster.kind[slot] != expected->kind)) return 0;
		candidate.slots[slot] = *observed;
		prefix++;
	}
	if (prefix != facts->nativeDriverCount) return 0;
	for (uint8_t index = facts->nativeDriverCount; index < MAIN_ARCADE_ROSTER_SLOT_COUNT; index++)
		if (facts->nativeDriverSlots[index] != MAIN_ARCADE_ROSTER_SLOT_NONE) return 0;

	*out = candidate;
	return 1;
}
