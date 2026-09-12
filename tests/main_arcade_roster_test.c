#include "MAIN/MainArcadeRoster.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "fail %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static void InitConfig(struct NativeMatchConfigV1 *config, uint32_t profile)
{
	if (profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB)
		NativeMatchConfigV1_InitArcadeTwoCab(config);
	else
		NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = 3;
	config->gameMode1 = 4;
	config->gameMode2 = 5;
	config->rules = 6;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	memset(config->buildIdentity, 0x11, sizeof(config->buildIdentity));
	memset(config->contentIdentity, 0x22, sizeof(config->contentIdentity));
	memset(config->botRulesDigest, 0x33, sizeof(config->botRulesDigest));
	for (uint8_t slot = 0; slot < MAIN_ARCADE_ROSTER_SLOT_COUNT; slot++)
	{
		if (config->slots[slot].role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) continue;
		config->slots[slot].characterID = (uint8_t)(slot + 1u);
		config->slots[slot].difficulty = (uint8_t)(10u + slot);
	}
}

static void InitFacts(const struct NativeMatchConfigV1 *config,
	struct MainArcadeRosterNativeFacts *facts)
{
	uint8_t nativeIndex = 0;
	memset(facts, 0, sizeof(*facts));
	memset(facts->rosterInput.raceOrder, MAIN_ARCADE_ROSTER_SLOT_NONE,
	       sizeof(facts->rosterInput.raceOrder));
	memset(facts->rosterInput.winnerDriverIDs, MAIN_ARCADE_ROSTER_SLOT_NONE,
	       sizeof(facts->rosterInput.winnerDriverIDs));
	memset(facts->rosterInput.ranks, MAIN_ARCADE_ROSTER_SLOT_NONE,
	       sizeof(facts->rosterInput.ranks));
	memset(facts->rosterInput.navOrder, MAIN_ARCADE_ROSTER_SLOT_NONE,
	       sizeof(facts->rosterInput.navOrder));
	memset(facts->nativeDriverSlots, MAIN_ARCADE_ROSTER_SLOT_NONE,
	       sizeof(facts->nativeDriverSlots));
	facts->rosterInput.numLaps = (int8_t)config->lapCount;
	for (uint8_t slot = 0; slot < MAIN_ARCADE_ROSTER_SLOT_COUNT; slot++)
	{
		const struct NativeMatchConfigSlotV1 *source = &config->slots[slot];
		struct NativeCanonicalDriversRosterSlot *rosterSlot = &facts->rosterInput.slots[slot];
		struct MainArcadeRosterNativeSlotFacts *nativeSlot = &facts->slots[slot];
		uint8_t kind;
		if (source->role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) continue;
		kind = source->role == NATIVE_MATCH_SLOT_ROLE_BOT ?
		       NATIVE_CANONICAL_DRIVER_KIND_BOT : NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
		rosterSlot->present = 1;
		rosterSlot->driverID = slot;
		rosterSlot->kind = kind;
		rosterSlot->behaviorID = kind == NATIVE_CANONICAL_DRIVER_KIND_BOT ? 1 : 0;
		rosterSlot->threadBehaviorID = kind == NATIVE_CANONICAL_DRIVER_KIND_BOT ?
			NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE : NATIVE_CANONICAL_DRIVER_THREAD_NULL;
		facts->rosterInput.raceOrder[nativeIndex] = slot;
		facts->nativeDriverSlots[nativeIndex] = slot;
		nativeSlot->present = 1;
		nativeSlot->driverID = slot;
		nativeSlot->role = source->role;
		nativeSlot->initialLifecycle = source->initialLifecycle;
		nativeSlot->characterID = source->characterID;
		nativeSlot->difficulty = source->difficulty;
		if (kind == NATIVE_CANONICAL_DRIVER_KIND_BOT)
		{
			facts->rosterInput.navOrder[0][facts->rosterInput.navCount[0]++] = slot;
			facts->numBotsNextGame++;
		}
		else
		{
			facts->rosterInput.ranks[facts->numPlyrCurrGame] = facts->numPlyrCurrGame;
			facts->numPlyrCurrGame++;
		}
		nativeIndex++;
	}
	facts->nativeDriverCount = nativeIndex;
	facts->rosterInput.raceOrderCount = nativeIndex;
	facts->rosterInput.playerCount = facts->numPlyrCurrGame;
}

static int Validate(const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRosterPlan *plan,
	const struct MainArcadeRosterNativeFacts *facts,
	struct MainArcadeRosterValidated *out)
{
	return MainArcadeRoster_ValidateNativeFacts(plan, config, facts, out);
}

static int TestProfile(uint32_t profile, uint8_t humans, uint8_t bots, uint32_t mask)
{
	struct NativeMatchConfigV1 config;
	struct MainArcadeRosterPlan plan;
	struct MainArcadeRosterNativeFacts facts;
	struct MainArcadeRosterValidated validated;

	InitConfig(&config, profile);
	InitFacts(&config, &facts);
	CHECK(MainArcadeRoster_BuildPlan(&config, &plan));
	CHECK(plan.locked == 1 && plan.humanCount == humans && plan.botCount == bots);
	CHECK(plan.driverCount == (uint8_t)(humans + bots) && plan.presenceMask == mask);
	CHECK(Validate(&config, &plan, &facts, &validated));
	CHECK(validated.presenceMask == mask && validated.humanCount == humans && validated.botCount == bots);
	CHECK(memcmp(validated.matchConfigDigest, plan.matchConfigDigest, sizeof(plan.matchConfigDigest)) == 0);
	return 0;
}

static int TestRosterRejections(void)
{
	struct NativeMatchConfigV1 config;
	struct MainArcadeRosterPlan plan;
	struct MainArcadeRosterNativeFacts good, bad;
	struct MainArcadeRosterValidated output, before;

	InitConfig(&config, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
	InitFacts(&config, &good);
	CHECK(MainArcadeRoster_BuildPlan(&config, &plan));
	CHECK(Validate(&config, &plan, &good, &output));
	before = output;

#define REJECT(change) do { bad = good; change; CHECK(!Validate(&config, &plan, &bad, &output)); CHECK(memcmp(&output, &before, sizeof(output)) == 0); } while (0)
	REJECT(bad.rosterInput.slots[1].driverID = 0);                 /* duplicate ID */
	REJECT(bad.rosterInput.slots[5].present = 0);                 /* missing slot */
	REJECT(bad.slots[3].driverID = 4);                            /* wrong stable slot */
	REJECT(bad.slots[1].role = NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN); /* wrong role */
	REJECT(bad.slots[2].characterID++);                           /* character mismatch */
	REJECT(bad.slots[4].difficulty++);                            /* difficulty mismatch */
	REJECT(bad.numBotsNextGame--);                               /* bot-count mismatch */
	REJECT(bad.numPlyrCurrGame++);                               /* human-count mismatch */
	REJECT(bad.slots[6].characterID = 1);                         /* inactive payload */
	REJECT(bad.nativeDriverSlots[2] = 3; bad.nativeDriverSlots[3] = 2); /* reorder */
	REJECT(bad.nativeDriverSlots[6] = 0);                         /* non-empty compact tail */
	REJECT(bad.rosterInput.slots[2].kind = NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
	       bad.rosterInput.slots[2].threadBehaviorID = NATIVE_CANONICAL_DRIVER_THREAD_NULL);
	REJECT(bad.slots[0].initialLifecycle = NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED);
#undef REJECT
	return 0;
}

static int TestLockAndOutputAtomicity(void)
{
	struct NativeMatchConfigV1 config, changed;
	struct MainArcadeRosterPlan plan, changedPlan, planBefore;
	struct MainArcadeRosterNativeFacts facts;
	struct MainArcadeRosterValidated output, before;

	InitConfig(&config, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
	InitFacts(&config, &facts);
	memset(&plan, 0xa5, sizeof(plan));
	planBefore = plan;
	changed = config;
	changed.slots[1].role = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	CHECK(!MainArcadeRoster_BuildPlan(&changed, &plan));
	CHECK(memcmp(&plan, &planBefore, sizeof(plan)) == 0);
	CHECK(MainArcadeRoster_BuildPlan(&config, &plan));
	CHECK(Validate(&config, &plan, &facts, &output));
	before = output;

	changed = config;
	changed.slots[3].characterID++;
	CHECK(!Validate(&changed, &plan, &facts, &output)); /* post-lock config mutation */
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	changed = config;
	changed.slots[3].difficulty++;
	CHECK(!Validate(&changed, &plan, &facts, &output));
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	changedPlan = plan;
	changedPlan.slots[3].role = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	CHECK(!Validate(&config, &changedPlan, &facts, &output)); /* locked plan tamper */
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	CHECK(!MainArcadeRoster_BuildPlan(NULL, &plan));
	CHECK(!MainArcadeRoster_BuildPlan(&config, NULL));
	CHECK(!MainArcadeRoster_ValidateNativeFacts(NULL, &config, &facts, &output));
	CHECK(!MainArcadeRoster_ValidateNativeFacts(&plan, &config, NULL, &output));
	CHECK(!MainArcadeRoster_ValidateNativeFacts(&plan, &config, &facts, NULL));
	return 0;
}

int main(void)
{
	if (TestProfile(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 2, 4, UINT32_C(0x3f)) != 0 ||
	    TestProfile(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB, 1, 7, UINT32_C(0xff)) != 0 ||
	    TestRosterRejections() != 0 || TestLockAndOutputAtomicity() != 0)
		return 1;
	puts("main arcade roster planner tests passed");
	return 0;
}
