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

	/* ONE_CAB config. */
	oneCab = config;
	oneCab.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB;
	CHECK(ExpectBuildReject(&oneCab, &valid) == 0);

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

int main(void)
{
	CHECK(TestFixtureRace() == 0);
	CHECK(TestRejections() == 0);
	puts("main_arcade_race_setup_facts_test: ok");
	return 0;
}
