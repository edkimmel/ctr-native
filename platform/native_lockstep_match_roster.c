#include "platform/native_lockstep_match_roster.h"

#include <string.h>

int NativeLockstepMatchRoster_Init(struct NativeLockstepMatchRoster *roster, const struct NativeMatchConfigV1 *config)
{
	if ((roster == NULL) || (config == NULL) || !NativeMatchConfigV1_Validate(config))
	{
		return 0;
	}

	memset(roster, 0, sizeof(*roster));
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		roster->role[i] = config->slots[i].role;
		roster->lifecycle[i] =
		    (config->slots[i].role != NATIVE_MATCH_SLOT_ROLE_INACTIVE) ? NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE : NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE;
	}
	return 1;
}

int NativeLockstepMatchRoster_DropSlot(struct NativeLockstepMatchRoster *roster, uint8_t slot)
{
	if ((roster == NULL) || (slot >= NATIVE_MATCH_CONFIG_V1_SLOT_COUNT))
	{
		return 0;
	}
	if ((roster->role[slot] != NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (roster->role[slot] != NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
	{
		return 0;
	}
	return NativeMatchSlotLifecycle_Transition(roster->role[slot], &roster->lifecycle[slot], NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
}

int NativeLockstepMatchRoster_ApplyOutcome(struct NativeLockstepMatchRoster *roster, uint8_t localSlot,
                                           const struct NativeLockstepMatchOutcomeReport *outcome)
{
	int transitioned = 0;

	if ((roster == NULL) || (outcome == NULL) || (outcome->cause == NATIVE_LOCKSTEP_MATCH_OUTCOME_NONE))
	{
		return 0;
	}

	for (uint32_t slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		uint8_t before;

		if (slot == localSlot)
		{
			continue;
		}
		if ((roster->role[slot] != NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (roster->role[slot] != NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
		{
			continue;
		}
		/*
		 * DropSlot's own return mirrors NativeMatchSlotLifecycle_Transition,
		 * which allows a harmless DISCONNECTED-to-DISCONNECTED (or
		 * FINISHED-to-FINISHED) self-transition, so "successfully
		 * transitioned" here means the lifecycle value actually moved, not
		 * merely that the call was accepted.
		 */
		before = roster->lifecycle[slot];
		if ((NativeLockstepMatchRoster_DropSlot(roster, (uint8_t)slot) != 0) && (roster->lifecycle[slot] != before))
		{
			transitioned++;
		}
	}
	return transitioned;
}
