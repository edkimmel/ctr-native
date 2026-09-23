#include "MAIN/MainArcadeRaceSetupFacts.h"

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRoster.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_rules.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SENTINEL_BYTE 0xa5u
#define SLOTS MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT

/* A retail race as the snapshot and the roster input would observe it. */
struct Race
{
	struct MainArcadeRaceSetupLiveSnapshot snapshot;
	struct NativeCanonicalDriversRosterInput rosterInput;
};

static void FillCounting(uint8_t *bytes, size_t size, uint8_t start)
{
	for (size_t i = 0; i < size; i++)
	{
		bytes[i] = (uint8_t)(start + i);
	}
}

static int IsAllByte(const void *memory, size_t size, uint8_t value)
{
	const uint8_t *bytes = (const uint8_t *)memory;

	for (size_t i = 0; i < size; i++)
	{
		if (bytes[i] != value)
		{
			return 0;
		}
	}
	return 1;
}

/* The real arcade-link fixture, resolved through match select with its own characters, track, and laps. */
static int BuildFixtureRace(struct NativeMatchConfigV1 *config)
{
	struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;

	FillCounting(identity.build, sizeof(identity.build), 0x21u);
	FillCounting(identity.content, sizeof(identity.content), 0x61u);
	if (!NativeArcadeLinkFixture_Build(&identity, &base))
	{
		return 0;
	}
	memset(choices, 0, sizeof(choices));
	for (uint32_t i = 0; i < 2u; i++)
	{
		choices[i].characterID = base.slots[i].characterID;
		choices[i].trackID = (uint8_t)base.trackID;
		choices[i].lapCount = (uint8_t)base.lapCount;
		choices[i].nonce = UINT64_C(0x5151515151515151) + i;
	}
	return NativeMatchSelect_Resolve(&base, 2u, choices, &outcome) && NativeMatchSelect_BuildConfig(&base, &outcome, config);
}

/*
 * The source-shaped roster input for present/isBot, built the way
 * tests/main_arcade_bot_setup_test.c builds it: humans behavior 0 on a NULL
 * thread, bots behavior 1 on BOTS_ThTick_Drive, race order by slot, human
 * ranks by slot, and each bot on the nav list of its driver_pathIndexIDs
 * entry, added at the front as BOTS_Driver_Init does.
 */
static void BuildRosterInput(struct Race *race, int8_t numLaps)
{
	struct NativeCanonicalDriversRosterInput *input = &race->rosterInput;
	const struct MainArcadeRaceSetupLiveSnapshot *snapshot = &race->snapshot;

	memset(input, 0, sizeof(*input));
	memset(input->raceOrder, 0xff, sizeof(input->raceOrder));
	memset(input->winnerDriverIDs, 0xff, sizeof(input->winnerDriverIDs));
	memset(input->ranks, 0xff, sizeof(input->ranks));
	memset(input->navOrder, 0xff, sizeof(input->navOrder));
	input->numLaps = numLaps;
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		struct NativeCanonicalDriversRosterSlot *rosterSlot = &input->slots[slot];

		if (!snapshot->driverPresent[slot])
		{
			continue;
		}
		rosterSlot->present = 1;
		rosterSlot->driverID = slot;
		input->raceOrder[input->raceOrderCount++] = slot;
		if (snapshot->driverIsBot[slot])
		{
			const uint8_t path = (uint8_t)snapshot->driver_pathIndexIDs[slot];

			rosterSlot->kind = NATIVE_CANONICAL_DRIVER_KIND_BOT;
			rosterSlot->behaviorID = 1;
			rosterSlot->threadBehaviorID = NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE;
			memmove(&input->navOrder[path][1], &input->navOrder[path][0], SLOTS - 1u);
			input->navOrder[path][0] = slot;
			input->navCount[path]++;
		}
		else
		{
			rosterSlot->kind = NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
			rosterSlot->behaviorID = 0;
			rosterSlot->threadBehaviorID = NATIVE_CANONICAL_DRIVER_THREAD_NULL;
			input->ranks[input->playerCount] = input->playerCount;
			input->playerCount++;
		}
	}
}

/*
 * The fixture race after MainInit_Drivers, with the retail values the audit
 * derives: arcade spawn order 0..7; pathOrder {0, f, s+1, 2, 0, f^1,
 * (s^1)+1, 2} with f = 1, s = 0; accelOrder with front rotation 2 and rear
 * rotation 1. Slots 6..7 hold their retail array entries but no driver.
 */
static void BuildFixtureSnapshot(const struct NativeMatchConfigV1 *config, struct Race *race)
{
	static const int8_t paths[SLOTS] = { 0, 1, 1, 2, 0, 0, 2, 2 };
	static const uint8_t accel[SLOTS] = { 2, 3, 0, 1, 5, 4, 7, 6 };
	struct MainArcadeRaceSetupLiveSnapshot *snapshot = &race->snapshot;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->numPlyrCurrGame = 2;
	snapshot->numBotsNextGame = 4;
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		snapshot->driverPresent[slot] = (uint8_t)(slot < 6u);
		snapshot->driverID[slot] = slot < 6u ? slot : 0u;
		snapshot->driverIsBot[slot] = (uint8_t)((slot >= 2u) && (slot < 6u));
		snapshot->characterIDs[slot] = slot < 6u ? (int16_t)config->slots[slot].characterID : (int16_t)(slot + 4u);
		snapshot->kartSpawnOrderArray[slot] = slot;
		snapshot->driver_pathIndexIDs[slot] = paths[slot];
		snapshot->accelerateOrder[slot] = accel[slot];
	}
	snapshot->arcadeDifficulty = (int32_t)config->slots[2].difficulty;
	BuildRosterInput(race, (int8_t)config->lapCount);
}

/*
 * A retail 1P arcade race after MainInit_Drivers (the header's 1P audit):
 * driver 0 the human, 1..7 bots, all 8 present, characterIDs as the config
 * (LOAD_Robots1P's result), arcade spawn order 0..7, and the same pathOrder
 * and accelOrder formulas as BuildFixtureSnapshot (f = 1, s = 0; front
 * rotation 2, rear rotation 1).
 */
static void BuildOneCabSnapshot(const struct NativeMatchConfigV1 *config, struct Race *race)
{
	static const int8_t paths[SLOTS] = { 0, 1, 1, 2, 0, 0, 2, 2 };
	static const uint8_t accel[SLOTS] = { 2, 3, 0, 1, 5, 4, 7, 6 };
	struct MainArcadeRaceSetupLiveSnapshot *snapshot = &race->snapshot;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->numPlyrCurrGame = (uint8_t)NATIVE_ARCADE_BOT_RULES_1P_HUMAN_COUNT;
	snapshot->numBotsNextGame = (uint8_t)NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT;
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		snapshot->driverPresent[slot] = 1u;
		snapshot->driverID[slot] = slot;
		snapshot->driverIsBot[slot] = (uint8_t)(slot >= NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT);
		snapshot->characterIDs[slot] = (int16_t)config->slots[slot].characterID;
		snapshot->kartSpawnOrderArray[slot] = slot;
		snapshot->driver_pathIndexIDs[slot] = paths[slot];
		snapshot->accelerateOrder[slot] = accel[slot];
	}
	snapshot->arcadeDifficulty = (int32_t)config->slots[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT].difficulty;
	BuildRosterInput(race, (int8_t)config->lapCount);
}

static uint64_t MatchSetupDraws(const struct NativeDeterministicRngBankV1 *bank)
{
	for (uint32_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		if (bank->streams[i].tag == (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP)
		{
			return bank->streams[i].drawCount;
		}
	}
	return UINT64_MAX;
}

static int ExpectBuildReject(const struct NativeMatchConfigV1 *config, const struct Race *race)
{
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;

	memset(&rosterFacts, SENTINEL_BYTE, sizeof(rosterFacts));
	memset(&setupFacts, SENTINEL_BYTE, sizeof(setupFacts));
	CHECK(MainArcadeRaceSetupFacts_Build(config, &race->snapshot, &race->rosterInput, &rosterFacts, &setupFacts) == 0);
	CHECK(IsAllByte(&rosterFacts, sizeof(rosterFacts), SENTINEL_BYTE));
	CHECK(IsAllByte(&setupFacts, sizeof(setupFacts), SENTINEL_BYTE));
	return 0;
}

/*
 * A well-formed ARCADE_ONE_CAB config (RS-19, RS-20): the ONE_CAB profile,
 * *valid's table track, lap count, and nonzero identities and seed, 30/1 ticks,
 * CAB1 a base character at difficulty 0, slots 1..7 exactly ExpectedBots1P at
 * one table difficulty, and the 1P bot rules digest.
 */
static int BuildOneCab(const struct NativeMatchConfigV1 *valid, uint8_t human, uint8_t difficulty,
	struct NativeMatchConfigV1 *config)
{
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];

	if (!NativeArcadeBotRules_ExpectedBots1P(human, bots))
	{
		return 0;
	}
	memset(config, 0, sizeof(*config));
	NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = valid->trackID;
	config->lapCount = valid->lapCount;
	config->tickRateNumerator = 30u;
	config->tickRateDenominator = 1u;
	config->masterSeed = valid->masterSeed;
	memcpy(config->buildIdentity, valid->buildIdentity, sizeof(config->buildIdentity));
	memcpy(config->contentIdentity, valid->contentIdentity, sizeof(config->contentIdentity));
	config->slots[0].characterID = human;
	config->slots[0].difficulty = 0u;
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT; i++)
	{
		config->slots[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i].characterID = bots[i];
		config->slots[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i].difficulty = difficulty;
	}
	return NativeArcadeBotRules_Digest1PV1(config->botRulesDigest);
}

/* Build succeeds, and MainArcadeRoster_ValidateNativeFacts rejects with its output untouched. */
static int ExpectRosterReject(const struct NativeMatchConfigV1 *config, const struct Race *race)
{
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validated;

	CHECK(MainArcadeRaceSetupFacts_Build(config, &race->snapshot, &race->rosterInput, &rosterFacts, &setupFacts) == 1);
	CHECK(MainArcadeRoster_BuildPlan(config, &rosterPlan) == 1);
	memset(&validated, SENTINEL_BYTE, sizeof(validated));
	CHECK(MainArcadeRoster_ValidateNativeFacts(&rosterPlan, config, &rosterFacts, &validated) == 0);
	CHECK(IsAllByte(&validated, sizeof(validated), SENTINEL_BYTE));
	return 0;
}

/* Build and the roster validation succeed, and MainArcadeBotSetup_Plan returns `expected` with both outputs untouched. */
static int ExpectSetupReject(const struct NativeMatchConfigV1 *config, const struct Race *race,
	enum MainArcadeBotSetupResult expected)
{
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validated;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeDeterministicRngBankV1 after;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct MainArcadeBotSetupPlan plan;

	CHECK(MainArcadeRaceSetupFacts_Build(config, &race->snapshot, &race->rosterInput, &rosterFacts, &setupFacts) == 1);
	CHECK(MainArcadeRoster_BuildPlan(config, &rosterPlan) == 1);
	CHECK(MainArcadeRoster_ValidateNativeFacts(&rosterPlan, config, &rosterFacts, &validated) == 1);
	CHECK(NativeDeterministicRngBankV1_Init(&bank, config->masterSeed, config->rngDerivationVersion) == 1);
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&bank, &seeds) == 1);
	memset(&plan, SENTINEL_BYTE, sizeof(plan));
	memset(&after, SENTINEL_BYTE, sizeof(after));
	CHECK(MainArcadeBotSetup_Plan(config, &rosterPlan, &validated, &setupFacts, &bank, &plan, &after) == expected);
	CHECK(IsAllByte(&plan, sizeof(plan), SENTINEL_BYTE));
	CHECK(IsAllByte(&after, sizeof(after), SENTINEL_BYTE));
	return 0;
}

static int TestFixtureRace(void)
{
	struct NativeMatchConfigV1 config;
	struct Race race;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct MainArcadeRosterNativeFacts again;
	struct MainArcadeBotSetupSourceFacts setupAgain;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validated;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeDeterministicRngBankV1 after;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct MainArcadeBotSetupPlan plan;

	CHECK(BuildFixtureRace(&config) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(config.slots[2].difficulty == NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY);
	BuildFixtureSnapshot(&config, &race);

	memset(&rosterFacts, SENTINEL_BYTE, sizeof(rosterFacts));
	memset(&setupFacts, SENTINEL_BYTE, sizeof(setupFacts));
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &race.snapshot, &race.rosterInput, &rosterFacts, &setupFacts) == 1);

	/* Exact roster facts. */
	CHECK(memcmp(&rosterFacts.rosterInput, &race.rosterInput, sizeof(race.rosterInput)) == 0);
	CHECK((rosterFacts.numPlyrCurrGame == 2u) && (rosterFacts.numBotsNextGame == 4u) && (rosterFacts.nativeDriverCount == 6u));
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		const struct MainArcadeRosterNativeSlotFacts *observed = &rosterFacts.slots[slot];

		if (slot >= 6u)
		{
			CHECK(rosterFacts.nativeDriverSlots[slot] == MAIN_ARCADE_ROSTER_SLOT_NONE);
			CHECK(IsAllByte(observed, sizeof(*observed), 0u));
			continue;
		}
		CHECK(rosterFacts.nativeDriverSlots[slot] == slot);
		CHECK((observed->present == 1u) && (observed->driverID == slot));
		CHECK(observed->role == config.slots[slot].role);
		CHECK(observed->initialLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
		CHECK(observed->characterID == config.slots[slot].characterID);
		CHECK(observed->difficulty == (slot < 2u ? 0u : 0xa0u));
		CHECK((observed->reserved[0] == 0u) && (observed->reserved[1] == 0u));
	}
	CHECK(rosterFacts.slots[0].role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
	CHECK(rosterFacts.slots[1].role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);

	/* Exact setup facts, ascending stable slots. */
	CHECK((setupFacts.factCount == 8u) && IsAllByte(setupFacts.reserved, sizeof(setupFacts.reserved), 0u));
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		const struct MainArcadeBotSetupSourceSlot *fact = &setupFacts.facts[slot];

		CHECK(fact->stableSlot == slot);
		CHECK(IsAllByte(fact->reserved, sizeof(fact->reserved), 0u));
		if (slot >= 6u)
		{
			CHECK((fact->present == 0u) && (fact->role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) && (fact->characterID == 0u) &&
			      (fact->difficulty == 0u));
			CHECK((fact->nativeDriverSlot == 0xffu) && (fact->spawnOrder == 0xffu) && (fact->navPathIndex == 0xffu) &&
			      (fact->accelerationOrder == 0xffu));
			continue;
		}
		CHECK((fact->present == 1u) && (fact->nativeDriverSlot == slot) && (fact->role == config.slots[slot].role));
		CHECK((fact->characterID == config.slots[slot].characterID) && (fact->difficulty == config.slots[slot].difficulty));
		CHECK(fact->spawnOrder == race.snapshot.kartSpawnOrderArray[slot]);
		CHECK(fact->navPathIndex == (uint8_t)race.snapshot.driver_pathIndexIDs[slot]);
		CHECK(fact->accelerationOrder == race.snapshot.accelerateOrder[slot]);
	}

	/* Deterministic. */
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &race.snapshot, &race.rosterInput, &again, &setupAgain) == 1);
	CHECK(memcmp(&again, &rosterFacts, sizeof(again)) == 0);
	CHECK(memcmp(&setupAgain, &setupFacts, sizeof(setupAgain)) == 0);

	/* The facts pass the roster and bot setup validators, on the post-seed bank. */
	CHECK(MainArcadeRoster_BuildPlan(&config, &rosterPlan) == 1);
	CHECK(MainArcadeRoster_ValidateNativeFacts(&rosterPlan, &config, &rosterFacts, &validated) == 1);
	CHECK((validated.humanCount == 2u) && (validated.botCount == 4u) && (validated.driverCount == 6u) &&
	      (validated.presenceMask == 0x3fu));
	CHECK(NativeDeterministicRngBankV1_Init(&bank, config.masterSeed, config.rngDerivationVersion) == 1);
	CHECK(MatchSetupDraws(&bank) == 0u);
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&bank, &seeds) == 1);
	CHECK(MatchSetupDraws(&bank) == 5u);
	CHECK(MainArcadeBotSetup_Plan(&config, &rosterPlan, &validated, &setupFacts, &bank, &plan, &after) ==
	      MAIN_ARCADE_BOT_SETUP_OK);
	CHECK((plan.botCount == 4u) && (plan.botMask == 0x3cu) && (plan.locked == 1u));
	CHECK(MatchSetupDraws(&after) == 9u);
	for (uint8_t slot = 2; slot < 6u; slot++)
	{
		CHECK(plan.assignments[slot].enabled == 1u);
		CHECK(plan.assignments[slot].navPathIndex == (uint8_t)race.snapshot.driver_pathIndexIDs[slot]);
		CHECK(plan.assignments[slot].accelerationOrder == race.snapshot.accelerateOrder[slot]);
		CHECK(plan.assignments[slot].characterID == config.slots[slot].characterID);
	}
	return 0;
}

static int TestRejections(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 oneCab;
	struct Race valid;
	struct Race race;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;

	CHECK(BuildFixtureRace(&config) == 1);
	BuildFixtureSnapshot(&config, &valid);

	/* NULL arguments. */
	CHECK(ExpectBuildReject(NULL, &valid) == 0);
	memset(&rosterFacts, SENTINEL_BYTE, sizeof(rosterFacts));
	memset(&setupFacts, SENTINEL_BYTE, sizeof(setupFacts));
	CHECK(MainArcadeRaceSetupFacts_Build(&config, NULL, &valid.rosterInput, &rosterFacts, &setupFacts) == 0);
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &valid.snapshot, NULL, &rosterFacts, &setupFacts) == 0);
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &valid.snapshot, &valid.rosterInput, NULL, &setupFacts) == 0);
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &valid.snapshot, &valid.rosterInput, &rosterFacts, NULL) == 0);
	CHECK(IsAllByte(&rosterFacts, sizeof(rosterFacts), SENTINEL_BYTE));
	CHECK(IsAllByte(&setupFacts, sizeof(setupFacts), SENTINEL_BYTE));

	/* The ONE_CAB profile over TWO_CAB slot roles: the generic validator rejects it. */
	oneCab = config;
	oneCab.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB;
	CHECK(NativeMatchConfigV1_Validate(&oneCab) == 0);
	CHECK(ExpectBuildReject(&oneCab, &valid) == 0);

	/*
	 * A well-formed ONE_CAB config over a retail 1P race snapshot (slot 0 human,
	 * slots 1..7 bots): valid under the bot rules, and the facts builder
	 * accepts it (OC-2; TestOneCabRace validates it end to end).
	 */
	CHECK(BuildOneCab(&config, 2u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD, &oneCab) == 1);
	CHECK(NativeMatchConfigV1_Validate(&oneCab) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&oneCab) == 1);
	race = valid;
	race.snapshot.numPlyrCurrGame = (uint8_t)NATIVE_ARCADE_BOT_RULES_1P_HUMAN_COUNT;
	race.snapshot.numBotsNextGame = (uint8_t)NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT;
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		race.snapshot.driverPresent[slot] = 1;
		race.snapshot.driverID[slot] = slot;
		race.snapshot.driverIsBot[slot] = (uint8_t)(slot >= NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT);
		race.snapshot.characterIDs[slot] = (int16_t)oneCab.slots[slot].characterID;
	}
	race.snapshot.arcadeDifficulty = (int32_t)oneCab.slots[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT].difficulty;
	BuildRosterInput(&race, (int8_t)oneCab.lapCount);
	CHECK(MainArcadeRaceSetupFacts_Build(&oneCab, &race.snapshot, &race.rosterInput, &rosterFacts, &setupFacts) == 1);

	/* A profile the builder has no shape for. */
	oneCab.profile = 3u;
	CHECK(ExpectBuildReject(&oneCab, &race) == 0);
	oneCab.profile = 0u;
	CHECK(ExpectBuildReject(&oneCab, &race) == 0);

	/* A human in slot 3: no TWO_CAB role. */
	race = valid;
	race.snapshot.driverIsBot[3] = 0;
	race.snapshot.numPlyrCurrGame = 3;
	race.snapshot.numBotsNextGame = 3;
	BuildRosterInput(&race, (int8_t)config.lapCount);
	CHECK(ExpectBuildReject(&config, &race) == 0);

	/* arcadeDifficulty that does not fit uint8_t. */
	race = valid;
	race.snapshot.arcadeDifficulty = 0x100;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race.snapshot.arcadeDifficulty = -1;
	CHECK(ExpectBuildReject(&config, &race) == 0);

	/* Snapshot values the facts cannot carry, and a snapshot the roster input disagrees with. */
	race = valid;
	race.snapshot.characterIDs[4] = 0x100;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.snapshot.characterIDs[0] = -1;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.snapshot.driver_pathIndexIDs[2] = -1;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.snapshot.driverPresent[2] = 2;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.snapshot.driverID[7] = 7;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.snapshot.reserved[1] = 1;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.rosterInput.slots[4].present = 0;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.rosterInput.slots[1].kind = NATIVE_CANONICAL_DRIVER_KIND_BOT;
	CHECK(ExpectBuildReject(&config, &race) == 0);
	race = valid;
	race.snapshot.driverID[3] = 2;
	CHECK(ExpectBuildReject(&config, &race) == 0);

	/* A bot in slot 0: observed as BOT, refused by the roster validator. */
	race = valid;
	race.snapshot.driverIsBot[0] = 1;
	race.snapshot.numPlyrCurrGame = 1;
	race.snapshot.numBotsNextGame = 5;
	BuildRosterInput(&race, (int8_t)config.lapCount);
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* A bot in slot 1 (the CAB2 player): observed as BOT, refused by the roster validator. */
	race = valid;
	race.snapshot.driverIsBot[1] = 1;
	race.snapshot.numPlyrCurrGame = 1;
	race.snapshot.numBotsNextGame = 5;
	BuildRosterInput(&race, (int8_t)config.lapCount);
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &race.snapshot, &race.rosterInput, &rosterFacts, &setupFacts) == 1);
	CHECK((rosterFacts.slots[1].role == NATIVE_MATCH_SLOT_ROLE_BOT) && (rosterFacts.slots[1].difficulty == 0xa0u));
	CHECK((setupFacts.facts[1].role == NATIVE_MATCH_SLOT_ROLE_BOT) && (setupFacts.facts[1].difficulty == 0xa0u));
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* Tampered counts with an otherwise valid roster: each is refused by the roster validator. */
	race = valid;
	race.snapshot.numPlyrCurrGame = 3;
	CHECK(ExpectRosterReject(&config, &race) == 0);
	race = valid;
	race.snapshot.numPlyrCurrGame = 1;
	CHECK(ExpectRosterReject(&config, &race) == 0);
	race = valid;
	race.snapshot.numBotsNextGame = 5;
	CHECK(ExpectRosterReject(&config, &race) == 0);
	race = valid;
	race.snapshot.numBotsNextGame = 3;
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* A missing driver. */
	race = valid;
	race.snapshot.driverPresent[4] = 0;
	race.snapshot.driverIsBot[4] = 0;
	race.snapshot.driverID[4] = 0;
	race.snapshot.numBotsNextGame = 3;
	BuildRosterInput(&race, (int8_t)config.lapCount);
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* A compaction: slot 5's bot observed in slot 6. */
	race = valid;
	race.snapshot.driverPresent[5] = 0;
	race.snapshot.driverIsBot[5] = 0;
	race.snapshot.driverID[5] = 0;
	race.snapshot.driverPresent[6] = 1;
	race.snapshot.driverIsBot[6] = 1;
	race.snapshot.driverID[6] = 6;
	race.snapshot.characterIDs[6] = race.snapshot.characterIDs[5];
	BuildRosterInput(&race, (int8_t)config.lapCount);
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* A character that differs from the plan, bot and human. */
	race = valid;
	race.snapshot.characterIDs[3] = (int16_t)(race.snapshot.characterIDs[3] == 7 ? 5 : 7);
	CHECK(ExpectRosterReject(&config, &race) == 0);
	race = valid;
	race.snapshot.characterIDs[1] = 7;
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* A table difficulty other than the config's. */
	race = valid;
	race.snapshot.arcadeDifficulty = 0xf0;
	CHECK(ExpectRosterReject(&config, &race) == 0);

	/* Setup facts the bot setup refuses: a bot off its nav list, a repeated spawn or acceleration order. */
	race = valid;
	race.snapshot.driver_pathIndexIDs[3] = 1; /* the roster input keeps the bot on path 2 */
	CHECK(ExpectSetupReject(&config, &race, MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH) == 0);
	race = valid;
	race.snapshot.kartSpawnOrderArray[4] = race.snapshot.kartSpawnOrderArray[0];
	CHECK(ExpectSetupReject(&config, &race, MAIN_ARCADE_BOT_SETUP_DUPLICATE_SPAWN) == 0);
	race = valid;
	race.snapshot.accelerateOrder[1] = race.snapshot.accelerateOrder[5];
	CHECK(ExpectSetupReject(&config, &race, MAIN_ARCADE_BOT_SETUP_DUPLICATE_ACCELERATION) == 0);
	race = valid;
	race.snapshot.driver_pathIndexIDs[1] = 3; /* a human off every path */
	CHECK(ExpectSetupReject(&config, &race, MAIN_ARCADE_BOT_SETUP_RANGE) == 0);
	return 0;
}

/* A retail 1P race: the facts build, then pass the roster and bot setup validators. */
static int TestOneCabRace(void)
{
	struct NativeMatchConfigV1 twoCab;
	struct NativeMatchConfigV1 config;
	struct Race race;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct MainArcadeRosterNativeFacts again;
	struct MainArcadeBotSetupSourceFacts setupAgain;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validated;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeDeterministicRngBankV1 after;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct MainArcadeBotSetupPlan plan;

	CHECK(BuildFixtureRace(&twoCab) == 1);
	CHECK(BuildOneCab(&twoCab, 0u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM, &config) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	BuildOneCabSnapshot(&config, &race);

	memset(&rosterFacts, SENTINEL_BYTE, sizeof(rosterFacts));
	memset(&setupFacts, SENTINEL_BYTE, sizeof(setupFacts));
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &race.snapshot, &race.rosterInput, &rosterFacts, &setupFacts) == 1);

	/* Exact roster facts: slot 0 CAB1, 1..7 bots at the table difficulty. */
	CHECK(memcmp(&rosterFacts.rosterInput, &race.rosterInput, sizeof(race.rosterInput)) == 0);
	CHECK((rosterFacts.numPlyrCurrGame == 1u) && (rosterFacts.numBotsNextGame == 7u) && (rosterFacts.nativeDriverCount == 8u));
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		const struct MainArcadeRosterNativeSlotFacts *observed = &rosterFacts.slots[slot];

		CHECK(rosterFacts.nativeDriverSlots[slot] == slot);
		CHECK((observed->present == 1u) && (observed->driverID == slot));
		CHECK(observed->role == config.slots[slot].role);
		CHECK(observed->role == (slot == 0u ? NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN : NATIVE_MATCH_SLOT_ROLE_BOT));
		CHECK(observed->initialLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
		CHECK(observed->characterID == config.slots[slot].characterID);
		CHECK(observed->characterID == slot); /* human Crash (0), then LOAD_Robots1P's 1..7 */
		CHECK(observed->difficulty == (slot == 0u ? 0u : 0xa0u));
	}

	/* Exact setup facts: every slot present. */
	CHECK(setupFacts.factCount == 8u);
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		const struct MainArcadeBotSetupSourceSlot *fact = &setupFacts.facts[slot];

		CHECK((fact->stableSlot == slot) && (fact->present == 1u) && (fact->nativeDriverSlot == slot));
		CHECK((fact->role == config.slots[slot].role) && (fact->characterID == config.slots[slot].characterID) &&
		      (fact->difficulty == config.slots[slot].difficulty));
		CHECK(fact->spawnOrder == slot);
		CHECK(fact->navPathIndex == (uint8_t)race.snapshot.driver_pathIndexIDs[slot]);
		CHECK(fact->accelerationOrder == race.snapshot.accelerateOrder[slot]);
	}

	/* Deterministic. */
	CHECK(MainArcadeRaceSetupFacts_Build(&config, &race.snapshot, &race.rosterInput, &again, &setupAgain) == 1);
	CHECK(memcmp(&again, &rosterFacts, sizeof(again)) == 0);
	CHECK(memcmp(&setupAgain, &setupFacts, sizeof(setupAgain)) == 0);

	/* The roster and bot setup validators accept them: 7 bots, 12 MATCH_SETUP draws. */
	CHECK(MainArcadeRoster_BuildPlan(&config, &rosterPlan) == 1);
	CHECK(MainArcadeRoster_ValidateNativeFacts(&rosterPlan, &config, &rosterFacts, &validated) == 1);
	CHECK((validated.humanCount == 1u) && (validated.botCount == 7u) && (validated.driverCount == 8u) &&
	      (validated.presenceMask == 0xffu));
	CHECK(NativeDeterministicRngBankV1_Init(&bank, config.masterSeed, config.rngDerivationVersion) == 1);
	CHECK(NativeArcadeBotRules_DeriveRetailSeedsV1(&bank, &seeds) == 1);
	CHECK(MatchSetupDraws(&bank) == 5u);
	CHECK(MainArcadeBotSetup_Plan(&config, &rosterPlan, &validated, &setupFacts, &bank, &plan, &after) ==
	      MAIN_ARCADE_BOT_SETUP_OK);
	CHECK((plan.botCount == 7u) && (plan.botMask == 0xfeu) && (plan.locked == 1u));
	CHECK(MatchSetupDraws(&after) == 12u);
	for (uint8_t slot = 1; slot < SLOTS; slot++)
	{
		CHECK(plan.assignments[slot].enabled == 1u);
		CHECK(plan.assignments[slot].navPathIndex == (uint8_t)race.snapshot.driver_pathIndexIDs[slot]);
		CHECK(plan.assignments[slot].accelerationOrder == race.snapshot.accelerateOrder[slot]);
		CHECK(plan.assignments[slot].characterID == config.slots[slot].characterID);
	}
	CHECK(plan.assignments[0].enabled == 0u);

	/* Malformed ONE_CAB races. A human in slot 1 or slot 7: no ONE_CAB role. */
	{
		struct Race bad;

		bad = race;
		bad.snapshot.driverIsBot[1] = 0u;
		bad.snapshot.numPlyrCurrGame = 2u;
		bad.snapshot.numBotsNextGame = 6u;
		BuildRosterInput(&bad, (int8_t)config.lapCount);
		CHECK(ExpectBuildReject(&config, &bad) == 0);
		bad = race;
		bad.snapshot.driverIsBot[7] = 0u;
		bad.snapshot.numPlyrCurrGame = 2u;
		bad.snapshot.numBotsNextGame = 6u;
		BuildRosterInput(&bad, (int8_t)config.lapCount);
		CHECK(ExpectBuildReject(&config, &bad) == 0);

		/* Slot 7 absent (7 drivers): observed, then refused by the roster validator. */
		bad = race;
		bad.snapshot.driverPresent[7] = 0u;
		bad.snapshot.driverIsBot[7] = 0u;
		bad.snapshot.driverID[7] = 0u;
		bad.snapshot.numBotsNextGame = 6u;
		BuildRosterInput(&bad, (int8_t)config.lapCount);
		CHECK(ExpectRosterReject(&config, &bad) == 0);

		/* A TWO_CAB race (2 humans, 6 drivers) under the ONE_CAB config: a human in slot 1. */
		BuildFixtureSnapshot(&twoCab, &bad);
		CHECK(ExpectBuildReject(&config, &bad) == 0);

		/* The ONE_CAB race under a TWO_CAB config: slot 1 is observed as a
		 * bot, and the roster validator refuses it. */
		CHECK(MainArcadeRaceSetupFacts_Build(&twoCab, &race.snapshot, &race.rosterInput, &rosterFacts, &setupFacts) == 1);
		CHECK(rosterFacts.slots[1].role == NATIVE_MATCH_SLOT_ROLE_BOT);
		CHECK(ExpectRosterReject(&twoCab, &race) == 0);

		/* A bot in slot 0: observed as BOT, refused by the roster validator. */
		bad = race;
		bad.snapshot.driverIsBot[0] = 1u;
		bad.snapshot.numPlyrCurrGame = 0u;
		bad.snapshot.numBotsNextGame = 8u;
		BuildRosterInput(&bad, (int8_t)config.lapCount);
		CHECK(ExpectRosterReject(&config, &bad) == 0);

		/* A bot character other than LOAD_Robots1P's. */
		bad = race;
		bad.snapshot.characterIDs[7] = 0;
		CHECK(ExpectRosterReject(&config, &bad) == 0);

		/* A repeated acceleration order among the eight drivers. */
		bad = race;
		bad.snapshot.accelerateOrder[7] = bad.snapshot.accelerateOrder[0];
		CHECK(ExpectSetupReject(&config, &bad, MAIN_ARCADE_BOT_SETUP_DUPLICATE_ACCELERATION) == 0);
	}
	return 0;
}

int main(void)
{
	CHECK(TestFixtureRace() == 0);
	CHECK(TestRejections() == 0);
	CHECK(TestOneCabRace() == 0);
	puts("main_arcade_race_setup_facts_test: ok");
	return 0;
}
