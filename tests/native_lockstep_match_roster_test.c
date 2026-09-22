#include "platform/native_lockstep_match_roster.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SLOT_A 0u /* CAB1_HUMAN. */
#define SLOT_B 1u /* CAB2_HUMAN. */

static void FillConfig(struct NativeMatchConfigV1 *config, uint32_t trackID)
{
	config->trackID = trackID;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
}

/*
 * Init from a valid two-cab config: role[] matches the config slots,
 * lifecycle[] is ACTIVE for slots 0-5 (CAB1_HUMAN, CAB2_HUMAN, then four BOT
 * slots) and INACTIVE for slots 6-7.
 */
static int TestInitTwoCab(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchRoster roster;

	NativeMatchConfigV1_InitArcadeTwoCab(&config);
	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(NativeMatchConfigV1_Validate(&config) == 1);

	CHECK(NativeLockstepMatchRoster_Init(&roster, &config) == 1);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(roster.role[i] == config.slots[i].role);
	}
	CHECK(roster.role[0] == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(roster.role[1] == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	for (uint32_t i = 2; i <= 5; i++)
	{
		CHECK(roster.role[i] == NATIVE_MATCH_SLOT_ROLE_BOT);
	}
	for (uint32_t i = 0; i <= 5; i++)
	{
		CHECK(roster.lifecycle[i] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	}
	for (uint32_t i = 6; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(roster.role[i] == NATIVE_MATCH_SLOT_ROLE_INACTIVE);
		CHECK(roster.lifecycle[i] == NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE);
	}
	return 0;
}

/*
 * Init from a valid one-cab config: ACTIVE for slot 0 and every bot slot 1-7.
 */
static int TestInitOneCab(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchRoster roster;

	NativeMatchConfigV1_InitArcadeOneCab(&config);
	FillConfig(&config, UINT32_C(0x05060708));
	CHECK(NativeMatchConfigV1_Validate(&config) == 1);

	CHECK(NativeLockstepMatchRoster_Init(&roster, &config) == 1);
	CHECK(roster.role[0] == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(roster.lifecycle[0] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	for (uint32_t i = 1; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(roster.role[i] == NATIVE_MATCH_SLOT_ROLE_BOT);
		CHECK(roster.lifecycle[i] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	}
	return 0;
}

/* Init rejects a NULL roster, a NULL config, and an invalidated config. */
static int TestInitRejects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 invalid;
	struct NativeLockstepMatchRoster roster;

	NativeMatchConfigV1_InitArcadeTwoCab(&config);
	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(NativeMatchConfigV1_Validate(&config) == 1);

	CHECK(NativeLockstepMatchRoster_Init(NULL, &config) == 0);
	CHECK(NativeLockstepMatchRoster_Init(&roster, NULL) == 0);

	invalid = config;
	memset(invalid.buildIdentity, 0, sizeof(invalid.buildIdentity));
	CHECK(NativeMatchConfigV1_Validate(&invalid) == 0);
	CHECK(NativeLockstepMatchRoster_Init(&roster, &invalid) == 0);
	return 0;
}

/*
 * DropSlot on a human slot transitions ACTIVE to DISCONNECTED and returns
 * nonzero.  A second DropSlot on the same slot returns whatever
 * NativeMatchSlotLifecycle_Transition actually returns for a DISCONNECTED to
 * DISCONNECTED request, and lifecycle[slot] stays DISCONNECTED either way.
 */
static int TestDropSlotHuman(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchRoster roster;
	int secondResult;

	NativeMatchConfigV1_InitArcadeTwoCab(&config);
	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(NativeLockstepMatchRoster_Init(&roster, &config) == 1);

	CHECK(NativeLockstepMatchRoster_DropSlot(&roster, (uint8_t)SLOT_B) != 0);
	CHECK(roster.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);

	secondResult = NativeLockstepMatchRoster_DropSlot(&roster, (uint8_t)SLOT_B);
	CHECK(secondResult == NativeMatchSlotLifecycle_CanTransition(NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED,
	                                                             NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED));
	CHECK(roster.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	return 0;
}

/*
 * DropSlot on a bot slot, an inactive slot, an out-of-range slot, and a NULL
 * roster all return 0 and change nothing.
 */
static int TestDropSlotRefusals(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchRoster roster;
	struct NativeLockstepMatchRoster before;

	NativeMatchConfigV1_InitArcadeTwoCab(&config);
	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(NativeLockstepMatchRoster_Init(&roster, &config) == 1);
	before = roster;

	CHECK(NativeLockstepMatchRoster_DropSlot(&roster, 2u) == 0); /* bot */
	CHECK(memcmp(&roster, &before, sizeof(roster)) == 0);

	CHECK(NativeLockstepMatchRoster_DropSlot(&roster, 6u) == 0); /* inactive */
	CHECK(memcmp(&roster, &before, sizeof(roster)) == 0);

	CHECK(NativeLockstepMatchRoster_DropSlot(&roster, (uint8_t)NATIVE_MATCH_CONFIG_V1_SLOT_COUNT) == 0); /* out of range */
	CHECK(memcmp(&roster, &before, sizeof(roster)) == 0);

	CHECK(NativeLockstepMatchRoster_DropSlot(NULL, (uint8_t)SLOT_A) == 0);
	return 0;
}

/* ApplyOutcome with outcome equal to NULL, or with cause equal to NONE, is a no-op returning 0. */
static int TestApplyOutcomeNoOp(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchRoster roster;
	struct NativeLockstepMatchRoster before;
	struct NativeLockstepMatchOutcomeReport outcome;

	NativeMatchConfigV1_InitArcadeTwoCab(&config);
	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(NativeLockstepMatchRoster_Init(&roster, &config) == 1);
	before = roster;

	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&roster, (uint8_t)SLOT_A, NULL) == 0);
	CHECK(memcmp(&roster, &before, sizeof(roster)) == 0);

	memset(&outcome, 0, sizeof(outcome));
	outcome.cause = NATIVE_LOCKSTEP_MATCH_OUTCOME_NONE;
	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&roster, (uint8_t)SLOT_A, &outcome) == 0);
	CHECK(memcmp(&roster, &before, sizeof(roster)) == 0);
	return 0;
}

/*
 * ApplyOutcome with a latched DIVERGED, FAULTED, or STALL_TIMEOUT outcome on
 * a two-cab roster with localSlot 0: slot 1, the remote human, transitions to
 * DISCONNECTED; slots 2-5, the bots, and slot 0, the local human, are
 * unchanged; the return value is 1, one slot transitioned.  Calling
 * ApplyOutcome a second time, with the remote already DISCONNECTED, returns
 * 0, nothing new transitioned, and does not corrupt lifecycle[].
 */
static int TestApplyOutcomeDropsRemoteOnly(uint32_t cause)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepMatchRoster roster;
	struct NativeLockstepMatchOutcomeReport outcome;
	struct NativeLockstepMatchRoster afterFirst;

	NativeMatchConfigV1_InitArcadeTwoCab(&config);
	FillConfig(&config, UINT32_C(0x01020304));
	CHECK(NativeLockstepMatchRoster_Init(&roster, &config) == 1);

	memset(&outcome, 0, sizeof(outcome));
	outcome.cause = cause;
	outcome.frameIndex = 42u;
	outcome.senderSlot = NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT;
	outcome.stalledFrameCount = 7u;

	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&roster, (uint8_t)SLOT_A, &outcome) == 1);
	CHECK(roster.lifecycle[SLOT_A] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	CHECK(roster.lifecycle[SLOT_B] == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED);
	for (uint32_t i = 2; i <= 5; i++)
	{
		CHECK(roster.lifecycle[i] == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
	}
	afterFirst = roster;

	CHECK(NativeLockstepMatchRoster_ApplyOutcome(&roster, (uint8_t)SLOT_A, &outcome) == 0);
	CHECK(memcmp(&roster, &afterFirst, sizeof(roster)) == 0);
	return 0;
}

int main(void)
{
	CHECK(TestInitTwoCab() == 0);
	CHECK(TestInitOneCab() == 0);
	CHECK(TestInitRejects() == 0);
	CHECK(TestDropSlotHuman() == 0);
	CHECK(TestDropSlotRefusals() == 0);
	CHECK(TestApplyOutcomeNoOp() == 0);
	CHECK(TestApplyOutcomeDropsRemoteOnly(NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED) == 0);
	CHECK(TestApplyOutcomeDropsRemoteOnly(NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED) == 0);
	CHECK(TestApplyOutcomeDropsRemoteOnly(NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT) == 0);
	return 0;
}
