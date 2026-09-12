#include "MAIN/MainArcadeBotSetup.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "fail %d: %s\n", __LINE__, #expression); return 1; } } while (0)

struct Fixture
{
	struct NativeMatchConfigV1 config;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeRosterValidated validatedRoster;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct NativeDeterministicRngBankV1 rng;
};

static void InitConfig(struct NativeMatchConfigV1 *config, uint32_t profile, uint64_t seed)
{
	if (profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB)
		NativeMatchConfigV1_InitArcadeTwoCab(config);
	else
		NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = 7;
	config->gameMode1 = 8;
	config->gameMode2 = 9;
	config->rules = 10;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = seed;
	memset(config->buildIdentity, 0x11, sizeof(config->buildIdentity));
	memset(config->contentIdentity, 0x22, sizeof(config->contentIdentity));
	memset(config->botRulesDigest, 0x33, sizeof(config->botRulesDigest));
	for (uint8_t slot = 0; slot < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; slot++)
	{
		if (config->slots[slot].role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) continue;
		config->slots[slot].characterID = (uint8_t)(slot + 1u);
		config->slots[slot].difficulty = (uint8_t)(20u + slot);
	}
}

static void InitRosterFacts(const struct NativeMatchConfigV1 *config,
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
	for (uint8_t slot = 0; slot < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; slot++)
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
			facts->rosterInput.navOrder[slot % 3u][facts->rosterInput.navCount[slot % 3u]++] = slot;
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

static void InitSetupFacts(const struct NativeMatchConfigV1 *config,
	struct MainArcadeBotSetupSourceFacts *facts, int reverse)
{
	memset(facts, 0, sizeof(*facts));
	facts->factCount = MAIN_ARCADE_BOT_SETUP_SLOT_COUNT;
	for (uint8_t index = 0; index < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; index++)
	{
		const uint8_t slot = reverse ? (uint8_t)(MAIN_ARCADE_BOT_SETUP_SLOT_COUNT - 1u - index) : index;
		const struct NativeMatchConfigSlotV1 *source = &config->slots[slot];
		struct MainArcadeBotSetupSourceSlot *fact = &facts->facts[index];
		fact->stableSlot = slot;
		if (source->role == NATIVE_MATCH_SLOT_ROLE_INACTIVE)
		{
			fact->nativeDriverSlot = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			fact->spawnOrder = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			fact->navPathIndex = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			fact->accelerationOrder = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			continue;
		}
		fact->present = 1;
		fact->nativeDriverSlot = slot;
		fact->role = source->role;
		fact->characterID = source->characterID;
		fact->difficulty = source->difficulty;
		fact->spawnOrder = (uint8_t)((slot * 3u) & 7u);
		fact->navPathIndex = (uint8_t)(slot % MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT);
		fact->accelerationOrder = (uint8_t)((slot * 5u + 1u) & 7u);
	}
}

static int InitFixture(struct Fixture *fixture, uint32_t profile, uint64_t seed, int reverse)
{
	InitConfig(&fixture->config, profile, seed);
	InitRosterFacts(&fixture->config, &fixture->rosterFacts);
	InitSetupFacts(&fixture->config, &fixture->setupFacts, reverse);
	return MainArcadeRoster_BuildPlan(&fixture->config, &fixture->rosterPlan) &&
	       MainArcadeRoster_ValidateNativeFacts(&fixture->rosterPlan, &fixture->config,
	           &fixture->rosterFacts, &fixture->validatedRoster) &&
	       NativeDeterministicRngBankV1_Init(&fixture->rng, seed,
	           fixture->config.rngDerivationVersion);
}

static enum MainArcadeBotSetupResult Plan(const struct Fixture *fixture,
	const struct MainArcadeBotSetupSourceFacts *facts,
	const struct NativeDeterministicRngBankV1 *rng,
	struct MainArcadeBotSetupPlan *out,
	struct NativeDeterministicRngBankV1 *rngAfter)
{
	return MainArcadeBotSetup_Plan(&fixture->config, &fixture->rosterPlan,
		&fixture->validatedRoster, facts, rng, out, rngAfter);
}

static int TestProfile(uint32_t profile, uint8_t botCount, uint32_t botMask)
{
	struct Fixture fixture;
	struct MainArcadeBotSetupPlan plan;
	struct NativeDeterministicRngBankV1 after;
	struct NativeDeterministicRngBankV1 expectedAfter;
	uint8_t sequence = 0;

	CHECK(InitFixture(&fixture, profile, UINT64_C(0x0123456789abcdef), 0));
	CHECK(Plan(&fixture, &fixture.setupFacts, &fixture.rng, &plan, &after) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(plan.profile == profile && plan.botCount == botCount && plan.botMask == botMask && plan.locked == 1);
	CHECK(after.streams[0].drawCount == botCount);
	CHECK(memcmp(&fixture.rng, &after, sizeof(after)) != 0);
	expectedAfter = fixture.rng;
	for (uint8_t slot = 0; slot < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; slot++)
	{
		const struct NativeMatchConfigSlotV1 *configSlot = &fixture.config.slots[slot];
		const struct MainArcadeBotSetupAssignment *assignment = &plan.assignments[slot];
		if (configSlot->role != NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			struct MainArcadeBotSetupAssignment zero = {0};
			CHECK(memcmp(assignment, &zero, sizeof(zero)) == 0);
			continue;
		}
		CHECK(assignment->enabled == 1 && assignment->stableSlot == slot && assignment->nativeDriverSlot == slot);
		CHECK(assignment->characterID == configSlot->characterID && assignment->difficulty == configSlot->difficulty);
		CHECK(assignment->spawnOrder == ((slot * 3u) & 7u));
		CHECK(assignment->navPathIndex == slot % MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT);
		CHECK(assignment->accelerationOrder == ((slot * 5u + 1u) & 7u));
		CHECK(assignment->setupSequence == sequence++);
		{
			uint32_t expectedRandom;
			CHECK(NativeDeterministicRngBankV1_NextU32(&expectedAfter,
				NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
				NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
				NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &expectedRandom));
			CHECK(assignment->setupRandom == expectedRandom);
		}
	}
	CHECK(sequence == botCount);
	CHECK(memcmp(&after, &expectedAfter, sizeof(after)) == 0);
	return 0;
}

static int TestOrderInvariance(void)
{
	struct Fixture ascending, descending;
	struct MainArcadeBotSetupPlan first, second;
	struct NativeDeterministicRngBankV1 firstAfter, secondAfter;

	CHECK(InitFixture(&ascending, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 1234, 0));
	CHECK(InitFixture(&descending, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 1234, 1));
	CHECK(Plan(&ascending, &ascending.setupFacts, &ascending.rng, &first, &firstAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(Plan(&descending, &descending.setupFacts, &descending.rng, &second, &secondAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
	CHECK(memcmp(&firstAfter, &secondAfter, sizeof(firstAfter)) == 0);
	return 0;
}

static int TestSeedAndStreamPerturbation(void)
{
	struct Fixture first, changedSeed;
	struct MainArcadeBotSetupPlan baseline, seeded, setupAdvanced, itemsAdvanced;
	struct NativeDeterministicRngBankV1 baselineAfter, seededAfter, setupBank, setupAfter, itemsBank, itemsAfter;
	uint64_t ignored;

	CHECK(InitFixture(&first, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 77, 0));
	CHECK(InitFixture(&changedSeed, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 78, 0));
	CHECK(Plan(&first, &first.setupFacts, &first.rng, &baseline, &baselineAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(Plan(&changedSeed, &changedSeed.setupFacts, &changedSeed.rng, &seeded, &seededAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(baseline.assignments[2].setupRandom != seeded.assignments[2].setupRandom);

	setupBank = first.rng;
	CHECK(NativeDeterministicRngBankV1_NextU64(&setupBank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
		NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &ignored));
	CHECK(Plan(&first, &first.setupFacts, &setupBank, &setupAdvanced, &setupAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(baseline.assignments[2].setupRandom != setupAdvanced.assignments[2].setupRandom);

	itemsBank = first.rng;
	CHECK(NativeDeterministicRngBankV1_NextU64(&itemsBank, NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS,
		NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &ignored));
	CHECK(Plan(&first, &first.setupFacts, &itemsBank, &itemsAdvanced, &itemsAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	for (uint8_t slot = 0; slot < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; slot++)
		CHECK(baseline.assignments[slot].setupRandom == itemsAdvanced.assignments[slot].setupRandom);
	CHECK(memcmp(baseline.rngBeforeDigest, itemsAdvanced.rngBeforeDigest,
	             sizeof(baseline.rngBeforeDigest)) != 0);
	return 0;
}

static int TestSourceRejectionsAndAtomicity(void)
{
	struct Fixture fixture;
	struct MainArcadeBotSetupSourceFacts bad;
	struct MainArcadeBotSetupPlan output, outputBefore;
	struct NativeDeterministicRngBankV1 rngOutput, rngBefore;

	CHECK(InitFixture(&fixture, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 99, 0));
	memset(&output, 0xa5, sizeof(output));
	memset(&rngOutput, 0x5a, sizeof(rngOutput));
	outputBefore = output;
	rngBefore = rngOutput;
#define REJECT(expected, change) do { bad = fixture.setupFacts; change; CHECK(Plan(&fixture, &bad, &fixture.rng, &output, &rngOutput) == (expected)); CHECK(memcmp(&output, &outputBefore, sizeof(output)) == 0); CHECK(memcmp(&rngOutput, &rngBefore, sizeof(rngOutput)) == 0); } while (0)
	REJECT(MAIN_ARCADE_BOT_SETUP_DUPLICATE_SLOT, bad.facts[7].stableSlot = 6);
	REJECT(MAIN_ARCADE_BOT_SETUP_MISSING_SLOT, bad.factCount = 7);
	REJECT(MAIN_ARCADE_BOT_SETUP_MISSING_ACTIVE, bad.facts[2].present = 0);
	REJECT(MAIN_ARCADE_BOT_SETUP_OWNERSHIP, bad.facts[2].role = NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	REJECT(MAIN_ARCADE_BOT_SETUP_COMPACTION, bad.facts[3].nativeDriverSlot = 2);
	REJECT(MAIN_ARCADE_BOT_SETUP_MISMATCH, bad.facts[4].characterID++);
	REJECT(MAIN_ARCADE_BOT_SETUP_MISMATCH, bad.facts[5].difficulty++);
	REJECT(MAIN_ARCADE_BOT_SETUP_RANGE, bad.facts[2].spawnOrder = 8);
	REJECT(MAIN_ARCADE_BOT_SETUP_RANGE, bad.facts[2].navPathIndex = 3);
	REJECT(MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH, bad.facts[2].navPathIndex = 0);
	REJECT(MAIN_ARCADE_BOT_SETUP_RANGE, bad.facts[2].accelerationOrder = 8);
	REJECT(MAIN_ARCADE_BOT_SETUP_RANGE, bad.facts[2].reserved[0] = 1);
	REJECT(MAIN_ARCADE_BOT_SETUP_DUPLICATE_SPAWN, bad.facts[3].spawnOrder = bad.facts[2].spawnOrder);
	REJECT(MAIN_ARCADE_BOT_SETUP_DUPLICATE_ACCELERATION, bad.facts[3].accelerationOrder = bad.facts[2].accelerationOrder);
	REJECT(MAIN_ARCADE_BOT_SETUP_INVALID_INACTIVE, bad.facts[6].present = 1);
	REJECT(MAIN_ARCADE_BOT_SETUP_INVALID_INACTIVE, bad.facts[7].difficulty = 1);
	REJECT(MAIN_ARCADE_BOT_SETUP_FACT_COUNT, bad.factCount = 9);
	REJECT(MAIN_ARCADE_BOT_SETUP_FACT_COUNT, bad.reserved[1] = 1);
#undef REJECT
	return 0;
}

static int TestContractAndRngRejections(void)
{
	struct Fixture fixture;
	struct NativeMatchConfigV1 badConfig;
	struct MainArcadeRosterPlan badPlan;
	struct MainArcadeRosterValidated badRoster;
	struct NativeDeterministicRngBankV1 badRng;
	struct MainArcadeBotSetupPlan output, outputBefore;
	struct NativeDeterministicRngBankV1 rngOutput, rngOutputBefore;

	CHECK(InitFixture(&fixture, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB, 100, 0));
	memset(&output, 0xa5, sizeof(output));
	memset(&rngOutput, 0x5a, sizeof(rngOutput));
	outputBefore = output;
	rngOutputBefore = rngOutput;
#define UNCHANGED() (memcmp(&output, &outputBefore, sizeof(output)) == 0 && memcmp(&rngOutput, &rngOutputBefore, sizeof(rngOutput)) == 0)
	badConfig = fixture.config;
	badConfig.slots[1].role = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	CHECK(MainArcadeBotSetup_Plan(&badConfig, &fixture.rosterPlan, &fixture.validatedRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_CONFIG && UNCHANGED());
	badPlan = fixture.rosterPlan;
	badPlan.slots[1].difficulty++;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &badPlan, &fixture.validatedRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER && UNCHANGED());
	badRoster = fixture.validatedRoster;
	badRoster.slots[1].role = NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &badRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER && UNCHANGED());
	badRoster = fixture.validatedRoster;
	badRoster.reserved = 1;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &badRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER && UNCHANGED());
	/* Missing and duplicate bot ownership cannot be hidden in nav-list data. */
	badRoster = fixture.validatedRoster;
	badRoster.roster.prelude.navListCount[1]--;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &badRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER && UNCHANGED());
	badRoster = fixture.validatedRoster;
	badRoster.roster.prelude.navListOrder[0][0] = 0;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &badRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER && UNCHANGED());
	badRoster = fixture.validatedRoster;
	badRoster.roster.prelude.navListOrder[1][0] = 2;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &badRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER && UNCHANGED());
	/* A complete unique cross-path swap is valid structurally, but no longer
	 * agrees with the per-slot supplied source facts. */
	badRoster = fixture.validatedRoster;
	badRoster.roster.prelude.navListOrder[1][0] = 2;
	badRoster.roster.prelude.navListOrder[2][0] = 1;
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &badRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH && UNCHANGED());
	badRng = fixture.rng;
	badRng.masterSeed++;
	CHECK(Plan(&fixture, &fixture.setupFacts, &badRng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_RNG && UNCHANGED());
	badRng = fixture.rng;
	badRng.streams[0].stableSlot = 0;
	CHECK(Plan(&fixture, &fixture.setupFacts, &badRng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_RNG && UNCHANGED());
	CHECK(MainArcadeBotSetup_Plan(NULL, &fixture.rosterPlan, &fixture.validatedRoster,
		&fixture.setupFacts, &fixture.rng, &output, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ARGUMENT && UNCHANGED());
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &fixture.validatedRoster,
		&fixture.setupFacts, &fixture.rng, NULL, &rngOutput) == MAIN_ARCADE_BOT_SETUP_INVALID_ARGUMENT && UNCHANGED());
	CHECK(MainArcadeBotSetup_Plan(&fixture.config, &fixture.rosterPlan, &fixture.validatedRoster,
		&fixture.setupFacts, &fixture.rng, &output, NULL) == MAIN_ARCADE_BOT_SETUP_INVALID_ARGUMENT && UNCHANGED());
#undef UNCHANGED
	return 0;
}

static int TestAliasedRngCommit(void)
{
	struct Fixture fixture;
	struct MainArcadeBotSetupPlan plan;
	struct NativeDeterministicRngBankV1 expectedAfter;
	struct NativeDeterministicRngBankV1 aliased;

	CHECK(InitFixture(&fixture, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 101, 1));
	CHECK(Plan(&fixture, &fixture.setupFacts, &fixture.rng, &plan, &expectedAfter) == MAIN_ARCADE_BOT_SETUP_OK);
	aliased = fixture.rng;
	CHECK(Plan(&fixture, &fixture.setupFacts, &aliased, &plan, &aliased) == MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(memcmp(&aliased, &expectedAfter, sizeof(aliased)) == 0);
	return 0;
}

int main(void)
{
	if (TestProfile(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 4, UINT32_C(0x3c)) != 0 ||
	    TestProfile(NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB, 7, UINT32_C(0xfe)) != 0 ||
	    TestOrderInvariance() != 0 || TestSeedAndStreamPerturbation() != 0 ||
	    TestSourceRejectionsAndAtomicity() != 0 || TestContractAndRngRejections() != 0 ||
	    TestAliasedRngCommit() != 0)
	{
		return 1;
	}
	puts("main arcade bot setup planner tests passed");
	return 0;
}
