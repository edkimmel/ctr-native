#ifndef PLATFORM_NATIVE_LOCKSTEP_MATCH_ROSTER_H
#define PLATFORM_NATIVE_LOCKSTEP_MATCH_ROSTER_H

#include "platform/native_lockstep_match_outcome.h"
#include "platform/native_match_config.h"

#include <stdint.h>

/*
 * Caller-owned per-slot lifecycle tracking, entirely separate from
 * struct NativeMatchConfigV1: slot->initialLifecycle is pinned by
 * NativeMatchConfigV1_Validate to the role-fixed initial value and is part of
 * the 256-byte encoded/digested config, so this roster tracks current
 * lifecycle in its own array and never writes into struct NativeMatchConfigV1
 * itself.  This is peer-drop policy only: it never touches canonical state,
 * replay, or the topology lease, and it never mutates the match config.
 */
struct NativeLockstepMatchRoster
{
	uint8_t role[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT];
	uint8_t lifecycle[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT];
};

/*
 * Requires NativeMatchConfigV1_Validate(config); copies config->slots[i].role
 * into roster->role[i] and sets roster->lifecycle[i] =
 * NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE for every non-INACTIVE role and
 * NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE otherwise, the same mapping
 * NativeMatchConfigV1_Validate itself uses.  Returns 0 with *roster untouched
 * on a NULL argument or a config that fails validation.
 */
int NativeLockstepMatchRoster_Init(struct NativeLockstepMatchRoster *roster, const struct NativeMatchConfigV1 *config);

/*
 * slot must be < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT and roster->role[slot]
 * must be CAB1_HUMAN or CAB2_HUMAN, because bots are never networked and are
 * never dropped.  Calls NativeMatchSlotLifecycle_Transition(roster->role[slot],
 * &roster->lifecycle[slot], NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED) and
 * returns its result.  Returns 0, and changes nothing, for an out-of-range
 * slot, a non-human role, or a transition the lifecycle state machine itself
 * refuses (for example a slot already FINISHED).
 */
int NativeLockstepMatchRoster_DropSlot(struct NativeLockstepMatchRoster *roster, uint8_t slot);

/*
 * If outcome is NULL or outcome->cause equals
 * NATIVE_LOCKSTEP_MATCH_OUTCOME_NONE, returns 0 and changes nothing.  Any
 * latched, non-NONE cause (DIVERGED, FAULTED, or STALL_TIMEOUT) is
 * match-ending regardless of which side reported it, so this calls
 * NativeLockstepMatchRoster_DropSlot for every slot whose role is CAB1_HUMAN
 * or CAB2_HUMAN and whose index is not localSlot: the local human is never
 * dropped by its own outcome report.  Returns the count of slots successfully
 * transitioned, 0 if none (for example the remote slot was already
 * DISCONNECTED or FINISHED).
 */
int NativeLockstepMatchRoster_ApplyOutcome(struct NativeLockstepMatchRoster *roster, uint8_t localSlot,
                                           const struct NativeLockstepMatchOutcomeReport *outcome);

#endif
