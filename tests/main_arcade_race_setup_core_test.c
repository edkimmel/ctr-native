#include "MAIN/MainArcadeRaceSetupCore.h"

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRaceSetupFacts.h"
#include "MAIN/MainArcadeRaceSetupPlan.h"
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

#define SLOTS MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT
#define GOLDEN_MASTER_SEED UINT64_C(0x0123456789ABCDEF)
#define GOLDEN_RANDOM_NUMBER INT64_C(0x7D2E)
#define GOLDEN_ADV_RNG0 INT64_C(0x60C79386)
#define GOLDEN_ADV_RNG1 INT64_C(0x78DFDBA8)
#define GOLDEN_PSX_RAND INT64_C(0x1472E10B)
#define GOLDEN_AUDIO_RNG INT64_C(0x75599A57)

/* The retail values the tests pretend to observe. */
#define LIVE_MAIN_MENU_LEVEL MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL
#define LIVE_STAGE_IDLE MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE
#define LIVE_STAGE_LOADING 3

/* The core state and its scratch are large; keep them off the test stack. */
static struct MainArcadeRaceSetupCore s_core;
static struct MainArcadeRaceSetupCore s_other;
static struct MainArcadeRaceSetupCoreScratch s_scratch;
static struct MainArcadeRaceSetupCoreOutcome s_outcome;
static struct MainArcadeRaceSetupCoreDriversView s_drivers;
static struct NativeMatchConfigV1 s_config;
static struct MainArcadeRaceSetupPlan s_plan;

static void FillCounting(uint8_t *bytes, size_t size, uint8_t start)
{
	for (size_t i = 0; i < size; i++)
	{
		bytes[i] = (uint8_t)(start + i);
	}
}

/* The arcade-link fixture resolved through match select with its own
 * choices, then pinned to the golden master seed. */
static int BuildConfig(struct NativeMatchConfigV1 *config)
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
	if (!NativeMatchSelect_Resolve(&base, 2u, choices, &outcome) || !NativeMatchSelect_BuildConfig(&base, &outcome, config))
	{
		return 0;
	}
	config->masterSeed = GOLDEN_MASTER_SEED;
	return NativeArcadeBotRules_ValidateConfigV1(config);
}

/* Title-screen live values: idle, options loaded, no pause, the main-menu
 * level, stale 1P fields, and a few host-local and cheat bits set. */
static void TitleLaunchView(struct MainArcadeRaceSetupCoreLaunchView *view)
{
	memset(view, 0, sizeof(*view));
	view->loadingStage = LIVE_STAGE_IDLE;
	view->optionsLoaded = 1u;
	view->fields.levelID = LIVE_MAIN_MENU_LEVEL;
	view->fields.gameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_P2_VIBRATE |
	                         MAIN_ARCADE_RACE_SETUP_GM1_BATTLE_MODE | MAIN_ARCADE_RACE_SETUP_GM1_ROLLING_ITEM;
	view->fields.gameMode2 = MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBO | MAIN_ARCADE_RACE_SETUP_GM2_CUP_ANY_KIND |
	                         MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP;
	view->fields.arcadeDifficulty = 0x50;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		view->fields.characterIDs[slot] = (int16_t)(100 + (int)slot);
	}
	view->fields.numLaps = 7;
	view->fields.numPlyrNextGame = 1u;
	view->fields.boolDemoMode = 1u;
}

/* The race-init live values after the load of the plan's race: the plan's
 * characterIDs in its owned slots (0..5 TWO_CAB, 0..7 ONE_CAB), stale values
 * elsewhere. */
static void LoadedBeginView(const struct MainArcadeRaceSetupPlan *plan, struct MainArcadeRaceSetupCoreBeginView *view)
{
	memset(view, 0, sizeof(*view));
	view->trackerPresent = 1u;
	view->numPlyrCurrGame = plan->numPlyrNextGame;
	view->fields.levelID = plan->levelID;
	view->fields.gameMode1 = 0xFFFFFFFFu;
	view->fields.gameMode2 = 0xFFFFFFFFu;
	view->fields.arcadeDifficulty = 0x7777;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		view->fields.characterIDs[slot] = ((((uint32_t)plan->characterWriteMask >> slot) & 1u) != 0u) ?
		                                      plan->characterIDs[slot] :
		                                      (int16_t)(200 + (int)slot);
	}
	view->fields.numLaps = plan->numLaps;
	view->fields.numPlyrNextGame = 9u;
	view->fields.boolDemoMode = 1u;
}

/*
 * The race after MainInit_Drivers, as tests/main_arcade_race_setup_facts_test.c
 * builds it, from the config's slot roles: TWO_CAB humans 0..1 and bots 2..5
 * (6..7 absent), ONE_CAB human 0 and bots 1..7; each bot on the nav list of
 * its path, and the roster input as the pre-race extractor reads it (no race
 * order, no winners).
 */
static void DriversView(const struct NativeMatchConfigV1 *config, struct MainArcadeRaceSetupCoreDriversView *view)
{
	static const int8_t paths[SLOTS] = { 0, 1, 1, 2, 0, 0, 2, 2 };
	static const uint8_t accel[SLOTS] = { 2, 3, 0, 1, 5, 4, 7, 6 };
	struct MainArcadeRaceSetupLiveSnapshot *snapshot = &view->snapshot;
	struct NativeCanonicalDriversRosterInput *input = &view->rosterInput;
	int difficultySet = 0;

	memset(view, 0, sizeof(*view));
	view->trackerPresent = 1u;
	view->rosterInputValid = 1u;
	view->gameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE | MAIN_ARCADE_RACE_SETUP_GM1_START_OF_RACE;
	view->gameMode2 = 0u;
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		const uint8_t role = config->slots[slot].role;
		const uint8_t present = (uint8_t)(role != NATIVE_MATCH_SLOT_ROLE_INACTIVE);
		const uint8_t isBot = (uint8_t)(role == NATIVE_MATCH_SLOT_ROLE_BOT);

		snapshot->driverPresent[slot] = present;
		snapshot->driverID[slot] = present ? slot : 0u;
		snapshot->driverIsBot[slot] = isBot;
		snapshot->characterIDs[slot] = present ? (int16_t)config->slots[slot].characterID : (int16_t)(slot + 4u);
		snapshot->kartSpawnOrderArray[slot] = slot;
		snapshot->driver_pathIndexIDs[slot] = paths[slot];
		snapshot->accelerateOrder[slot] = accel[slot];
		snapshot->numPlyrCurrGame = (uint8_t)(snapshot->numPlyrCurrGame + ((present != 0u) && (isBot == 0u)));
		snapshot->numBotsNextGame = (uint8_t)(snapshot->numBotsNextGame + isBot);
		if ((isBot != 0u) && !difficultySet)
		{
			snapshot->arcadeDifficulty = (int32_t)config->slots[slot].difficulty;
			difficultySet = 1;
		}
	}

	memset(input->raceOrder, 0xff, sizeof(input->raceOrder));
	memset(input->winnerDriverIDs, 0xff, sizeof(input->winnerDriverIDs));
	memset(input->ranks, 0xff, sizeof(input->ranks));
	memset(input->navOrder, 0xff, sizeof(input->navOrder));
	input->numLaps = (int8_t)config->lapCount;
	for (uint8_t slot = 0; slot < SLOTS; slot++)
	{
		struct NativeCanonicalDriversRosterSlot *rosterSlot = &input->slots[slot];

		if (!snapshot->driverPresent[slot])
		{
			continue;
		}
		rosterSlot->present = 1;
		rosterSlot->driverID = slot;
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

/* The outcome of a step that changed nothing and wrote nothing. */
static int ExpectNoOp(const struct MainArcadeRaceSetupCore *core, const struct MainArcadeRaceSetupCore *before,
	const struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	CHECK(outcome->opCount == 0u);
	CHECK(outcome->result == 0u);
	CHECK(outcome->log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_NONE);
	CHECK(memcmp(core, before, sizeof(*core)) == 0);
	return 0;
}

/* The outcome of a step that latched FAILED with `failure` and wrote nothing. */
static int ExpectFailed(const struct MainArcadeRaceSetupCore *core, const struct MainArcadeRaceSetupCoreOutcome *outcome,
	enum MainArcadeRaceSetupFailure failure)
{
	CHECK(outcome->opCount == 0u);
	CHECK(outcome->result == 0u);
	CHECK(outcome->log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_FAILED);
	CHECK(outcome->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILED);
	CHECK(outcome->failure == (uint32_t)failure);
	CHECK(outcome->detail != NULL && outcome->detail[0] != '\0');
	CHECK(MainArcadeRaceSetupCore_Status(core) == MAIN_ARCADE_RACE_SETUP_FAILED);
	CHECK(MainArcadeRaceSetupCore_Failure(core) == failure);
	return 0;
}

/* Both hooks, with and without a tracker, change nothing and write nothing. */
static int ExpectHooksNoOp(struct MainArcadeRaceSetupCore *core)
{
	struct MainArcadeRaceSetupCore before;
	struct MainArcadeRaceSetupCoreBeginView begin;

	for (uint8_t tracker = 0; tracker < 2u; tracker++)
	{
		LoadedBeginView(&s_plan, &begin);
		begin.trackerPresent = tracker;
		before = *core;
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(core, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectNoOp(core, &before, &s_outcome) == 0);
		DriversView(&s_config, &s_drivers);
		s_drivers.trackerPresent = tracker;
		CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(core, &s_drivers, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectNoOp(core, &before, &s_outcome) == 0);
	}
	CHECK(MainArcadeRaceSetupCore_HookReadsView(core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN) == 0);
	CHECK(MainArcadeRaceSetupCore_HookReadsView(core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED) == 0);
	return 0;
}

static int Arm(struct MainArcadeRaceSetupCore *core, uint32_t liveGameMode1)
{
	MainArcadeRaceSetupCore_Reset(core);
	return MainArcadeRaceSetupCore_Arm(core, &s_config, liveGameMode1, 1u, &s_outcome);
}

static int ArmAndLaunch(struct MainArcadeRaceSetupCore *core)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;

	TitleLaunchView(&launch);
	return Arm(core, launch.fields.gameMode1) && MainArcadeRaceSetupCore_Launch(core, &launch, &s_outcome);
}

static int ArmLaunchSeed(struct MainArcadeRaceSetupCore *core)
{
	struct MainArcadeRaceSetupCoreBeginView begin;

	LoadedBeginView(&s_plan, &begin);
	return ArmAndLaunch(core) && MainArcadeRaceSetupCore_OnFinalizeInitBegin(core, &begin, &s_scratch, &s_outcome);
}

static int ArmLaunchSeedValidate(struct MainArcadeRaceSetupCore *core)
{
	DriversView(&s_config, &s_drivers);
	return ArmLaunchSeed(core) && MainArcadeRaceSetupCore_OnDriversInitialized(core, &s_drivers, &s_scratch, &s_outcome);
}

static int TestArm(void)
{
	struct NativeMatchConfigV1 bad;

	/* IDLE: both hooks are no-ops. */
	MainArcadeRaceSetupCore_Reset(&s_core);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_IDLE);
	CHECK(ExpectHooksNoOp(&s_core) == 0);

	/* Arm: the plan, its digest, the bank, and the saved vibration bits. */
	CHECK(Arm(&s_core, 0xFFFFFFFFu) == 1);
	CHECK(s_outcome.result == 1u && s_outcome.opCount == 0u);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED);
	CHECK(s_outcome.status == (uint32_t)MAIN_ARCADE_RACE_SETUP_ARMED);
	CHECK(memcmp(&s_core.plan, &s_plan, sizeof(s_plan)) == 0);
	CHECK(memcmp(&s_core.config, &s_config, sizeof(s_config)) == 0);
	CHECK(s_core.savedVibration == MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK);
	CHECK(s_core.fieldsWritten == 0u);
	CHECK(Arm(&s_core, MAIN_ARCADE_RACE_SETUP_GM1_P3_VIBRATE | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE) == 1);
	CHECK(s_core.savedVibration == MAIN_ARCADE_RACE_SETUP_GM1_P3_VIBRATE);
	{
		uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

		CHECK(MainArcadeRaceSetupPlan_Digest(&s_plan, digest) == 1);
		CHECK(memcmp(s_core.racePlanDigest, digest, sizeof(digest)) == 0);
		CHECK(memcmp(s_core.configDigest, s_plan.configDigest, sizeof(digest)) == 0);
	}

	/* ARMED: both hooks are no-ops. */
	CHECK(ExpectHooksNoOp(&s_core) == 0);

	/* Refused configs stay IDLE with PLAN, and log ARM_REFUSED. */
	MainArcadeRaceSetupCore_Reset(&s_other);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, NULL, 0u, 1u, &s_outcome) == 0);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ARM_REFUSED && s_outcome.opCount == 0u);
	CHECK(MainArcadeRaceSetupCore_Status(&s_other) == MAIN_ARCADE_RACE_SETUP_IDLE);
	CHECK(MainArcadeRaceSetupCore_Failure(&s_other) == MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN);
	bad = s_config;
	bad.tickRateNumerator = 60u;
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &bad, 0u, 1u, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Status(&s_other) == MAIN_ARCADE_RACE_SETUP_IDLE);
	CHECK(MainArcadeRaceSetupCore_Failure(&s_other) == MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN);
	/* BANK is unreachable through a config: the plan refuses a wrong
	 * derivation version first (PLAN), and every version the plan accepts
	 * derives (the core static-asserts the two versions equal). */
	bad = s_config;
	bad.rngDerivationVersion = NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION + 1u;
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &bad, 0u, 1u, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Failure(&s_other) == MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN);
	CHECK(NativeDeterministicRngBankV1_Init(&s_other.bank, bad.masterSeed, bad.rngDerivationVersion) == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName(MAIN_ARCADE_RACE_SETUP_FAILURE_BANK), "BANK") == 0);
	MainArcadeRaceSetupCore_Reset(&s_other);
	/* A refused Arm leaves a usable IDLE. */
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 1u, &s_outcome) == 1);
	CHECK(MainArcadeRaceSetupCore_Failure(&s_other) == MAIN_ARCADE_RACE_SETUP_FAILURE_NONE);

	/* NULL arguments touch nothing. */
	CHECK(MainArcadeRaceSetupCore_Arm(NULL, &s_config, 0u, 1u, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 1u, NULL) == 0);
	CHECK(MainArcadeRaceSetupCore_Status(&s_other) == MAIN_ARCADE_RACE_SETUP_ARMED);
	return 0;
}

/* A fresh cabinet with no memcard save (boolHasLoadedOptions 0): Arm emits
 * the one OPTIONS_LOADED op, value 1, and nothing else; with the flag set it
 * emits nothing. Only a successful Arm marks; Launch's precondition stays. */
static int TestArmMarksOptions(void)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;
	struct MainArcadeRaceSetupCoreLaunchView unapplied;
	struct NativeMatchConfigV1 bad;

	/* Flag set (a memcard save's options were loaded): no op, as before. */
	MainArcadeRaceSetupCore_Reset(&s_other);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 1u, &s_outcome) == 1);
	CHECK(s_outcome.opCount == 0u && s_outcome.optionsMarked == 0u);
	MainArcadeRaceSetupCore_Reset(&s_other);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 0xFFFFu, &s_outcome) == 1);
	CHECK(s_outcome.opCount == 0u && s_outcome.optionsMarked == 0u);

	/* Flag clear: exactly the OPTIONS_LOADED op, and the same armed state. */
	MainArcadeRaceSetupCore_Reset(&s_core);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 0u, &s_outcome) == 1);
	CHECK(s_outcome.result == 1u && s_outcome.optionsMarked == 1u && s_outcome.overflowed == 0u);
	CHECK(s_outcome.opCount == 1u);
	CHECK(s_outcome.ops[0].target == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_OPTIONS_LOADED);
	CHECK(s_outcome.ops[0].index == 0u && s_outcome.ops[0].value == 1);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED);
	CHECK(s_outcome.status == (uint32_t)MAIN_ARCADE_RACE_SETUP_ARMED);
	CHECK(memcmp(&s_core, &s_other, sizeof(s_core)) == 0);

	/* Launch keeps its precondition: without the op applied it still fails
	 * closed; with the op's value in the view it passes. */
	TitleLaunchView(&unapplied);
	unapplied.optionsLoaded = 0u;
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &unapplied, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION) == 0);
	CHECK(strcmp(s_outcome.detail, "the game options are not loaded yet") == 0);
	MainArcadeRaceSetupCore_Reset(&s_core);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 0u, &s_outcome) == 1);
	TitleLaunchView(&launch);
	launch.optionsLoaded = (uint32_t)s_outcome.ops[0].value;
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 1);
	CHECK(s_outcome.status == (uint32_t)MAIN_ARCADE_RACE_SETUP_LAUNCHED);
	CHECK(s_outcome.opCount == MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT && s_outcome.optionsMarked == 0u);
	for (uint32_t i = 0; i < s_outcome.opCount; i++)
	{
		CHECK(s_outcome.ops[i].target != (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_OPTIONS_LOADED);
	}

	/* A refused or wrong-state Arm writes nothing, even with the flag clear. */
	MainArcadeRaceSetupCore_Reset(&s_other);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, NULL, 0u, 0u, &s_outcome) == 0);
	CHECK(s_outcome.opCount == 0u && s_outcome.optionsMarked == 0u);
	bad = s_config;
	bad.tickRateNumerator = 60u;
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &bad, 0u, 0u, &s_outcome) == 0);
	CHECK(s_outcome.opCount == 0u && s_outcome.optionsMarked == 0u);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 0u, &s_outcome) == 1);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 0u, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_other, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(s_outcome.optionsMarked == 0u);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 0u, &s_outcome) == 0);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED);
	CHECK(s_outcome.opCount == 0u && s_outcome.optionsMarked == 0u);

	/* No other step marks the options. */
	CHECK(ArmLaunchSeedValidate(&s_core) == 1);
	CHECK(s_outcome.optionsMarked == 0u);
	return 0;
}

static int TestWrongState(void)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;
	struct MainArcadeRaceSetupCore before;

	TitleLaunchView(&launch);

	/* From IDLE and FAILED a wrong call changes nothing (REFUSED). */
	MainArcadeRaceSetupCore_Reset(&s_core);
	before = s_core;
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED && s_outcome.opCount == 0u);
	CHECK(strcmp(s_outcome.detail, "Launch") == 0);
	CHECK(memcmp(&s_core, &before, sizeof(before)) == 0);

	/* While in flight a wrong call latches STATE: Arm in ARMED, LAUNCHED,
	 * SEEDED, and VALIDATED, and Launch in LAUNCHED, SEEDED, and VALIDATED
	 * (each state below; Launch in ARMED is the success case). */
	CHECK(Arm(&s_core, 0u) == 1);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 1u, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(strcmp(s_outcome.detail, "Arm") == 0);
	before = s_core;
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED && s_outcome.opCount == 0u);
	CHECK(memcmp(&s_core, &before, sizeof(before)) == 0);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 1u, &s_outcome) == 0);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_FAILED);

	/* FAILED: both hooks are no-ops. */
	CHECK(ExpectHooksNoOp(&s_core) == 0);

	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 1u, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(ArmLaunchSeed(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(ArmLaunchSeed(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 1u, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(ArmLaunchSeedValidate(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_VALIDATED);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(strcmp(s_outcome.detail, "Launch") == 0);
	CHECK(ArmLaunchSeedValidate(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &s_config, 0u, 1u, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(strcmp(s_outcome.detail, "Arm") == 0);

	/* Hooks out of order latch STATE: drivers before seeding, and a second
	 * race init over SEEDED. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	DriversView(&s_config, &s_drivers);
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, &s_drivers, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	CHECK(ArmLaunchSeed(&s_core) == 1);
	{
		struct MainArcadeRaceSetupCoreBeginView begin;

		LoadedBeginView(&s_plan, &begin);
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	}
	return 0;
}

/* One Launch precondition broken: FAILED/PRECONDITION, nothing written. */
static int ExpectPreconditionFails(const struct MainArcadeRaceSetupCoreLaunchView *launch)
{
	CHECK(Arm(&s_core, 0u) == 1);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, launch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION) == 0);
	CHECK(s_core.fieldsWritten == 0u);
	return 0;
}

static int TestLaunch(void)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;
	struct MainArcadeRaceSetupCoreLaunchView broken;
	static const uint8_t expectedTargets[] = {
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE2,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ARCADE_DIFFICULTY, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_BOOL_DEMO_MODE,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_LAPS, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_PLYR_NEXT_GAME,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_REQUEST_LOAD};
	const uint32_t expectedCount = (uint32_t)sizeof(expectedTargets);

	TitleLaunchView(&launch);

	/* Each precondition fails closed on its own. */
	broken = launch;
	broken.loadingStage = LIVE_STAGE_LOADING;
	CHECK(ExpectPreconditionFails(&broken) == 0);
	CHECK(strcmp(s_outcome.detail, "a load is in progress") == 0);
	broken = launch;
	broken.onBeginAddBits0 = 1u;
	CHECK(ExpectPreconditionFails(&broken) == 0);
	broken = launch;
	broken.onBeginRemBits0 = 0x80000000u;
	CHECK(ExpectPreconditionFails(&broken) == 0);
	broken = launch;
	broken.onBeginAddBits8 = 2u;
	CHECK(ExpectPreconditionFails(&broken) == 0);
	broken = launch;
	broken.onBeginRemBits8 = 4u;
	CHECK(ExpectPreconditionFails(&broken) == 0);
	CHECK(strcmp(s_outcome.detail, "pending OnBegin mode bits") == 0);
	broken = launch;
	broken.optionsLoaded = 0u;
	CHECK(ExpectPreconditionFails(&broken) == 0);
	CHECK(strcmp(s_outcome.detail, "the game options are not loaded yet") == 0);
	for (uint32_t pause = 0; pause < 4u; pause++)
	{
		broken = launch;
		broken.fields.gameMode1 |= MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_1 << pause;
		CHECK(ExpectPreconditionFails(&broken) == 0);
		CHECK(strcmp(s_outcome.detail, "the game is paused") == 0);
	}

	/* A plan that no longer applies (tampered after Arm): FAILED/PLAN, nothing written. */
	CHECK(Arm(&s_core, launch.fields.gameMode1) == 1);
	s_core.plan.locked = 0u;
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN) == 0);
	CHECK(strcmp(s_outcome.detail, "the plan could not be applied") == 0);
	CHECK(s_core.fieldsWritten == 0u);

	/* Success: exactly the owned fields except levelID, then the load request. */
	CHECK(Arm(&s_core, launch.fields.gameMode1) == 1);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 1);
	CHECK(s_outcome.result == 1u && s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_LAUNCHED);
	CHECK(s_core.fieldsWritten == 1u);
	CHECK(s_outcome.opCount == expectedCount);
	CHECK(expectedCount == MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT && s_outcome.overflowed == 0u);
	for (uint32_t i = 0; i < expectedCount; i++)
	{
		CHECK(s_outcome.ops[i].target == expectedTargets[i]);
		CHECK(s_outcome.ops[i].index == ((i >= 6u && i < 14u) ? (uint8_t)(i - 6u) : 0u));
	}
	CHECK(s_outcome.ops[0].value ==
	      (int64_t)((launch.fields.gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(s_outcome.ops[1].value == (int64_t)(launch.fields.gameMode2 & MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK));
	CHECK(s_outcome.ops[2].value == (int64_t)s_plan.arcadeDifficulty && s_plan.arcadeDifficulty == 0xA0);
	CHECK(s_outcome.ops[3].value == 0);
	CHECK(s_outcome.ops[4].value == (int64_t)s_plan.numLaps);
	CHECK(s_outcome.ops[5].value == 2);
	for (uint32_t slot = 0; slot < 8u; slot++)
	{
		CHECK(s_outcome.ops[6u + slot].value ==
		      (int64_t)((slot < 6u) ? s_plan.characterIDs[slot] : launch.fields.characterIDs[slot]));
	}
	/* levelID is never written: the load request carries the plan's level. */
	CHECK(s_outcome.ops[14].value == (int64_t)s_plan.levelID && s_plan.levelID != LIVE_MAIN_MENU_LEVEL);
	for (uint32_t i = 0; i < s_outcome.opCount; i++)
	{
		CHECK(s_outcome.ops[i].target != (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NONE);
		CHECK(s_outcome.ops[i].target <= (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_AUDIO_RNG);
	}

	/* LAUNCHED: the hook that should act without a tracker latches NO_TRACKER. */
	{
		struct MainArcadeRaceSetupCoreBeginView begin;

		CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN) == 1);
		CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED) == 0);
		memset(&begin, 0, sizeof(begin));
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER) == 0);
	}

	/* NULL arguments touch nothing. */
	CHECK(Arm(&s_core, 0u) == 1);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, NULL, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, NULL) == 0);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_ARMED);
	return 0;
}

/* The pre-drivers hook fails closed with `failure`, writing nothing. */
static int ExpectBeginFails(const struct MainArcadeRaceSetupCoreBeginView *begin, enum MainArcadeRaceSetupFailure failure)
{
	struct NativeDeterministicRngBankV1 bankBefore;

	CHECK(ArmAndLaunch(&s_core) == 1);
	bankBefore = s_core.bank;
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, begin, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, failure) == 0);
	CHECK(memcmp(&s_core.bank, &bankBefore, sizeof(bankBefore)) == 0);
	return 0;
}

static int TestFinalizeInitBegin(void)
{
	struct MainArcadeRaceSetupCoreBeginView begin;
	struct MainArcadeRaceSetupCoreBeginView broken;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeArcadeRetailRngSeedsV1 produced;
	struct NativeArcadeRetailRngSeedsV1 readback;
	struct NativeArcadeRetailRngSeedsV1 stored;
	uint32_t draws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT];
	static const uint8_t expectedTargets[] = {
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE2,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ARCADE_DIFFICULTY, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_BOOL_DEMO_MODE,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_TIMER, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_FRAME_TIMER_CONFETTI,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RCNT_TOTAL_UNITS, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CLOCK_FRAME_START,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RANDOM_NUMBER, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG0,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG1, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_PSX_RAND_SEED,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_AUDIO_RNG};
	/* The golden seeds of GOLDEN_MASTER_SEED (MATCH_SETUP draws 0..4). */
	static const int64_t goldenSeeds[5] = {GOLDEN_RANDOM_NUMBER, GOLDEN_ADV_RNG0, GOLDEN_ADV_RNG1, GOLDEN_PSX_RAND, GOLDEN_AUDIO_RNG};

	LoadedBeginView(&s_plan, &begin);

	/* Verification failures write nothing. The main-menu level in LAUNCHED is
	 * also the expected second failure of an abort whose return load replaced
	 * the queued race level (docs/RACE_LAUNCH_MILESTONE.md RL-9): unlike
	 * VALIDATED, LAUNCHED has no main-menu no-op. */
	broken = begin;
	broken.fields.levelID = LIVE_MAIN_MENU_LEVEL;
	CHECK(ExpectBeginFails(&broken, MAIN_ARCADE_RACE_SETUP_FAILURE_LEVEL_MISMATCH) == 0);
	broken = begin;
	broken.fields.numLaps = (int8_t)(begin.fields.numLaps + 1);
	CHECK(ExpectBeginFails(&broken, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH) == 0);
	broken = begin;
	broken.numPlyrCurrGame = 1u;
	CHECK(ExpectBeginFails(&broken, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH) == 0);
	for (uint32_t slot = 0; slot < 6u; slot++)
	{
		broken = begin;
		broken.fields.characterIDs[slot] = (int16_t)(broken.fields.characterIDs[slot] + 1);
		CHECK(ExpectBeginFails(&broken, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH) == 0);
		CHECK(strcmp(s_outcome.detail, "characterIDs") == 0);
	}

	/* A plan that no longer applies at race init (tampered after Launch, the
	 * verified fields intact): FAILED/PLAN, nothing written. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	s_core.plan.reserved[0] = 1u;
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN) == 0);
	CHECK(strcmp(s_outcome.detail, "the plan could not be re-applied") == 0);

	/* A bank that no longer validates: FAILED/SEED, nothing written, the bank
	 * as it was. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	s_core.bank.bankVersion = 0u;
	bank = s_core.bank;
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_SEED) == 0);
	CHECK(strcmp(s_outcome.detail, "the retail seeds could not be derived") == 0);
	CHECK(memcmp(&s_core.bank, &bank, sizeof(bank)) == 0);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_core, &produced, &readback) == 0);

	/* The golden seeds, independently: five MATCH_SETUP draws mapped by the rules. */
	CHECK(NativeDeterministicRngBankV1_Init(&bank, GOLDEN_MASTER_SEED, s_config.rngDerivationVersion) == 1);
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT; i++)
	{
		CHECK(NativeDeterministicRngBankV1_NextU32(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
			NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &draws[i]) == 1);
	}
	CHECK((int64_t)(draws[0] & 0xFFFFu) == goldenSeeds[0]);
	CHECK((int64_t)draws[1] == goldenSeeds[1] && (int64_t)draws[2] == goldenSeeds[2]);
	CHECK((int64_t)draws[3] == goldenSeeds[3] && (int64_t)draws[4] == goldenSeeds[4]);

	/* Unowned fields (numPlyrNextGame, characterIDs[6..7]) differ from the
	 * plan and are not written; the mode words are all ones. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 1);
	CHECK(s_outcome.result == 1u && s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_SEEDED);
	CHECK(s_outcome.opCount == (uint32_t)sizeof(expectedTargets));
	CHECK(s_outcome.opCount == MAIN_ARCADE_RACE_SETUP_CORE_BEGIN_OP_COUNT && s_outcome.overflowed == 0u);
	for (uint32_t i = 0; i < s_outcome.opCount; i++)
	{
		CHECK(s_outcome.ops[i].target == expectedTargets[i] && s_outcome.ops[i].index == 0u);
	}
	CHECK(s_outcome.ops[0].value ==
	      (int64_t)(MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(s_outcome.ops[1].value == (int64_t)MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK);
	CHECK(s_outcome.ops[2].value == (int64_t)s_plan.arcadeDifficulty);
	CHECK(s_outcome.ops[3].value == 0);
	/* The pinned boot-relative counters (RS-17): both at 0, before the seeds;
	 * then the pinned root-counter phase (LR-8): rcntTotalUnits 0 and
	 * clockFrameStart -200, so the first race GameLogic computes a 200 ms
	 * delta, 64 after the scale (before retail's after-load override to 32). */
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_PIN_TIMER == 0 && MAIN_ARCADE_RACE_SETUP_CORE_PIN_FRAME_TIMER_CONFETTI == 0);
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_PIN_RCNT_TOTAL_UNITS == 0 && MAIN_ARCADE_RACE_SETUP_CORE_PIN_CLOCK_FRAME_START == -200);
	CHECK(s_outcome.ops[4].value == 0 && s_outcome.ops[5].value == 0);
	CHECK(s_outcome.ops[6].value == 0 && s_outcome.ops[7].value == -200);
	CHECK(s_outcome.pins.timer == 0 && s_outcome.pins.frameTimerConfetti == 0);
	CHECK(s_outcome.pins.rcntTotalUnits == 0 && s_outcome.pins.clockFrameStart == -200);
	CHECK((MAIN_ARCADE_RACE_SETUP_CORE_PIN_RCNT_TOTAL_UNITS - MAIN_ARCADE_RACE_SETUP_CORE_PIN_CLOCK_FRAME_START) * 32 / 100 == 64);
	for (uint32_t i = 0; i < 5u; i++)
	{
		CHECK(s_outcome.ops[8u + i].value == goldenSeeds[i]);
	}
	CHECK((int64_t)s_outcome.seeds.randomNumber == goldenSeeds[0] && (int64_t)s_outcome.seeds.audioRNG == goldenSeeds[4]);
	/* The core keeps the post-seed bank: five draws in. */
	CHECK(memcmp(&s_core.bank, &bank, sizeof(bank)) == 0);

	/* The seed readback: recorded once, only in SEEDED, and returned with the
	 * produced seeds; the core only stores it (a mismatch is the proof's call). */
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_core, &produced, &readback) == 0);
	stored = s_outcome.seeds;
	stored.advRng0 ^= 1u;
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(&s_core, NULL) == 0);
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(NULL, &stored) == 0);
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(&s_core, &stored) == 1);
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(&s_core, &s_outcome.seeds) == 0);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_SEEDED);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_core, &produced, &readback) == 1);
	CHECK(memcmp(&produced, &s_outcome.seeds, sizeof(produced)) == 0);
	CHECK(memcmp(&readback, &stored, sizeof(readback)) == 0);
	CHECK((int64_t)produced.advRng0 == goldenSeeds[1] && (int64_t)produced.psxRandSeed == goldenSeeds[3]);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_core, NULL, &readback) == 0);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_core, &produced, NULL) == 0);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(NULL, &produced, &readback) == 0);
	CHECK(ArmAndLaunch(&s_other) == 1);
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(&s_other, &stored) == 0);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_other, &produced, &readback) == 0);
	/* VALIDATED keeps it; a readback arriving only now is refused. */
	CHECK(ArmLaunchSeed(&s_other) == 1);
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(&s_other, &s_outcome.seeds) == 1);
	DriversView(&s_config, &s_drivers);
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_other, &s_drivers, &s_scratch, &s_outcome) == 1);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_other, &produced, &readback) == 1);
	CHECK(memcmp(&produced, &readback, sizeof(produced)) == 0);
	CHECK(ArmLaunchSeedValidate(&s_other) == 1);
	CHECK(MainArcadeRaceSetupCore_RecordSeedReadback(&s_other, &stored) == 0);
	CHECK(MainArcadeRaceSetupCore_SeedReadback(&s_other, &produced, &readback) == 0);

	/* The pin readback (RS-17), with the same rules: recorded once, only in
	 * SEEDED, returned with the produced pins; the core only stores it. */
	{
		struct MainArcadeRaceSetupPins pinsProduced;
		struct MainArcadeRaceSetupPins pinsReadback;
		struct MainArcadeRaceSetupPins pinsStored;

		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_core, &pinsProduced, &pinsReadback) == 0);
		pinsStored = s_outcome.pins;
		pinsStored.frameTimerConfetti = 74;
		pinsStored.rcntTotalUnits = 526;
		pinsStored.clockFrameStart = 100;
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(&s_core, NULL) == 0);
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(NULL, &pinsStored) == 0);
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(&s_core, &pinsStored) == 1);
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(&s_core, &s_outcome.pins) == 0);
		CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_SEEDED);
		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_core, &pinsProduced, &pinsReadback) == 1);
		CHECK(pinsProduced.timer == 0 && pinsProduced.frameTimerConfetti == 0);
		CHECK(pinsProduced.rcntTotalUnits == 0 && pinsProduced.clockFrameStart == -200);
		CHECK(pinsReadback.timer == 0 && pinsReadback.frameTimerConfetti == 74);
		CHECK(pinsReadback.rcntTotalUnits == 526 && pinsReadback.clockFrameStart == 100);
		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_core, NULL, &pinsReadback) == 0);
		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_core, &pinsProduced, NULL) == 0);
		CHECK(MainArcadeRaceSetupCore_PinReadback(NULL, &pinsProduced, &pinsReadback) == 0);
		CHECK(ArmAndLaunch(&s_other) == 1);
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(&s_other, &pinsStored) == 0);
		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_other, &pinsProduced, &pinsReadback) == 0);
		/* VALIDATED keeps it; a readback arriving only now is refused. */
		CHECK(ArmLaunchSeed(&s_other) == 1);
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(&s_other, &s_outcome.pins) == 1);
		DriversView(&s_config, &s_drivers);
		CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_other, &s_drivers, &s_scratch, &s_outcome) == 1);
		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_other, &pinsProduced, &pinsReadback) == 1);
		CHECK(memcmp(&pinsProduced, &pinsReadback, sizeof(pinsProduced)) == 0);
		CHECK(ArmLaunchSeedValidate(&s_other) == 1);
		CHECK(MainArcadeRaceSetupCore_RecordPinReadback(&s_other, &pinsStored) == 0);
		CHECK(MainArcadeRaceSetupCore_PinReadback(&s_other, &pinsProduced, &pinsReadback) == 0);
	}

	/* SEEDED: the drivers hook reads its view; without a tracker it latches NO_TRACKER. */
	CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED) == 1);
	CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN) == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.trackerPresent = 0u;
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, &s_drivers, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER) == 0);

	/* NULL arguments touch nothing. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, NULL, &s_scratch, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, NULL, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, NULL) == 0);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_LAUNCHED);
	return 0;
}

/* The post-drivers hook fails closed with `failure`, writing nothing. */
static int ExpectDriversFails(const struct MainArcadeRaceSetupCoreDriversView *view, enum MainArcadeRaceSetupFailure failure)
{
	CHECK(ArmLaunchSeed(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, view, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, failure) == 0);
	CHECK(MainArcadeRaceSetupCore_Bank(&s_core) == NULL);
	return 0;
}

static int TestDriversInitialized(void)
{
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t racePlanDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t botSetupDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t bankDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t expected[NATIVE_SHA256_DIGEST_BYTES];
	struct MainArcadeBotSetupSourceFacts facts;
	struct MainArcadeRaceSetupCore before;

	/* Failures, each with nothing written. */
	DriversView(&s_config, &s_drivers);
	s_drivers.gameMode2 = MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBO;
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS) == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.gameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_BATTLE_MODE | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE;
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS) == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.gameMode1 = 0u;
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS) == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.rosterInputValid = 0u;
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS) == 0);
	CHECK(strcmp(s_outcome.detail, "the live roster input could not be extracted") == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.snapshot.driverIsBot[2] = 0u;
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS) == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.snapshot.characterIDs[3] = (int16_t)(s_drivers.snapshot.characterIDs[3] + 1);
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_ROSTER) == 0);
	DriversView(&s_config, &s_drivers);
	s_drivers.snapshot.driver_pathIndexIDs[2] = 2;
	CHECK(ExpectDriversFails(&s_drivers, MAIN_ARCADE_RACE_SETUP_FAILURE_BOT_SETUP) == 0);
	CHECK(s_outcome.botSetupResult == (int32_t)MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH);

	/* Success on the pre-race roster input (empty race order and winners). */
	DriversView(&s_config, &s_drivers);
	CHECK(s_drivers.rosterInput.raceOrderCount == 0u && s_drivers.rosterInput.winnerCount == 0u);
	CHECK(ArmLaunchSeed(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Digests(&s_core, configDigest, racePlanDigest, botSetupDigest, bankDigest) == 0);
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, &s_drivers, &s_scratch, &s_outcome) == 1);
	CHECK(s_outcome.result == 1u && s_outcome.opCount == 0u);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED);
	CHECK(s_outcome.botSetupResult == (int32_t)MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_VALIDATED);
	CHECK(MainArcadeRaceSetupCore_Bank(&s_core) == &s_core.bank);
	CHECK(MainArcadeRaceSetupCore_Digests(&s_core, configDigest, racePlanDigest, botSetupDigest, bankDigest) == 1);
	CHECK(memcmp(configDigest, s_plan.configDigest, sizeof(expected)) == 0);
	CHECK(MainArcadeRaceSetupPlan_Digest(&s_plan, expected) == 1 && memcmp(racePlanDigest, expected, sizeof(expected)) == 0);
	CHECK(MainArcadeBotSetupPlan_Digest(&s_core.botSetupPlan, expected) == 1 &&
	      memcmp(botSetupDigest, expected, sizeof(expected)) == 0);
	CHECK(NativeDeterministicRngBankV1_Digest(&s_core.bank, expected) == 1 && memcmp(bankDigest, expected, sizeof(expected)) == 0);
	CHECK(s_core.botSetupPlan.botCount == 4u && s_core.botSetupPlan.botMask == 0x3cu);
	CHECK(MainArcadeRaceSetupCore_SlotFacts(&s_core, &facts) == 1);
	CHECK(facts.factCount == 8u && facts.facts[2].navPathIndex == 1u && facts.facts[3].accelerationOrder == 1u);
	CHECK(facts.facts[0].role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN && facts.facts[6].present == 0u);

	/* Same config, same live facts: the same digests on another run. */
	{
		uint8_t again[4][NATIVE_SHA256_DIGEST_BYTES];

		CHECK(ArmLaunchSeed(&s_other) == 1);
		CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_other, &s_drivers, &s_scratch, &s_outcome) == 1);
		CHECK(MainArcadeRaceSetupCore_Digests(&s_other, again[0], again[1], again[2], again[3]) == 1);
		CHECK(memcmp(again[0], configDigest, sizeof(again[0])) == 0 && memcmp(again[1], racePlanDigest, sizeof(again[1])) == 0);
		CHECK(memcmp(again[2], botSetupDigest, sizeof(again[2])) == 0 && memcmp(again[3], bankDigest, sizeof(again[3])) == 0);
	}

	/* VALIDATED: the drivers hook is a no-op and does not read its view; the
	 * pre-drivers hook reads its view (RL-9: the level being initialized). */
	CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN) == 1);
	CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_core, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED) == 0);
	before = s_core;
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, &s_drivers, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectNoOp(&s_core, &before, &s_outcome) == 0);
	s_drivers.trackerPresent = 0u;
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, &s_drivers, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectNoOp(&s_core, &before, &s_outcome) == 0);
	{
		struct MainArcadeRaceSetupCoreBeginView begin;

		/* RL-9: the return load to the main-menu level after the race is a
		 * no-op in VALIDATED (the owner Disarms on the first idle main-menu
		 * frame after it): nothing written, nothing changed, and the
		 * validated results still readable. */
		LoadedBeginView(&s_plan, &begin);
		begin.fields.levelID = LIVE_MAIN_MENU_LEVEL;
		begin.numPlyrCurrGame = 1u;
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectNoOp(&s_core, &before, &s_outcome) == 0);
		CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_VALIDATED);
		CHECK(MainArcadeRaceSetupCore_Digests(&s_core, configDigest, racePlanDigest, botSetupDigest, bankDigest) == 1);
		CHECK(MainArcadeRaceSetupCore_SlotFacts(&s_core, &facts) == 1);
		/* Again (a second main-menu init before the Disarm): still a no-op. */
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectNoOp(&s_core, &before, &s_outcome) == 0);

		/* VALIDATED with no tracker, even with the main-menu level in the
		 * view, still latches STATE without writing. */
		CHECK(ArmLaunchSeedValidate(&s_other) == 1);
		begin.trackerPresent = 0u;
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_other, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(&s_other, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);

		/* SEEDED on the main-menu level still latches STATE: the no-op is
		 * VALIDATED's only. */
		CHECK(ArmLaunchSeed(&s_other) == 1);
		begin.trackerPresent = 1u;
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_other, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(&s_other, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);

		/* FAILED stays a no-op on the main-menu level too. */
		before = s_other;
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_other, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectNoOp(&s_other, &before, &s_outcome) == 0);
		CHECK(MainArcadeRaceSetupCore_HookReadsView(&s_other, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN) == 0);

		/* VALIDATED with a race level (a new race init over the validated
		 * setup) latches STATE without writing. */
		LoadedBeginView(&s_plan, &begin);
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE) == 0);
	}
	CHECK(MainArcadeRaceSetupCore_Digests(&s_core, configDigest, racePlanDigest, botSetupDigest, bankDigest) == 0);
	CHECK(MainArcadeRaceSetupCore_SlotFacts(&s_core, &facts) == 0);
	return 0;
}

static int ExpectDisarm(const struct MainArcadeRaceSetupCoreDisarmView *view, enum MainArcadeRaceSetupCoreVibration vibration)
{
	struct MainArcadeRaceSetupCore idle;

	CHECK(MainArcadeRaceSetupCore_Disarm(&s_core, view, &s_outcome) == 1);
	CHECK(s_outcome.vibration == (uint8_t)vibration);
	CHECK(s_outcome.log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED);
	CHECK(s_outcome.status == (uint32_t)MAIN_ARCADE_RACE_SETUP_IDLE);
	MainArcadeRaceSetupCore_Reset(&idle);
	CHECK(memcmp(&s_core, &idle, sizeof(idle)) == 0);
	if (vibration != MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_RESTORED)
	{
		CHECK(s_outcome.opCount == 0u);
	}
	return 0;
}

static int TestDisarm(void)
{
	const uint32_t saved = MAIN_ARCADE_RACE_SETUP_GM1_P2_VIBRATE;
	struct MainArcadeRaceSetupCoreDisarmView idleMenu;
	struct MainArcadeRaceSetupCoreDisarmView view;

	memset(&idleMenu, 0, sizeof(idleMenu));
	idleMenu.currentLevel = LIVE_MAIN_MENU_LEVEL;
	idleMenu.loadingStage = LIVE_STAGE_IDLE;
	idleMenu.gameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_P1_VIBRATE |
	                     MAIN_ARCADE_RACE_SETUP_GM1_P4_VIBRATE | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE;

	/* Launched, idle on the main-menu level: one GAME_MODE1 op restores the bits. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(s_core.savedVibration == saved);
	CHECK(ExpectDisarm(&idleMenu, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_RESTORED) == 0);
	CHECK(s_outcome.opCount == 1u && s_outcome.ops[0].target == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1);
	CHECK(s_outcome.ops[0].value == (int64_t)((idleMenu.gameMode1 & ~MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK) | saved));
	CHECK(s_outcome.savedVibration == saved);

	/* Any one of the three conditions false: nothing written. */
	CHECK(ArmAndLaunch(&s_core) == 1);
	view = idleMenu;
	view.currentLevel = s_plan.levelID;
	CHECK(ExpectDisarm(&view, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED) == 0);
	CHECK(ArmAndLaunch(&s_core) == 1);
	view = idleMenu;
	view.loadingStage = LIVE_STAGE_LOADING;
	CHECK(ExpectDisarm(&view, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED) == 0);
	CHECK(ArmAndLaunch(&s_core) == 1);
	view = idleMenu;
	view.gameMode1 |= MAIN_ARCADE_RACE_SETUP_GM1_LOADING;
	CHECK(ExpectDisarm(&view, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED) == 0);

	/* Launch never wrote the fields (ARMED, then IDLE): untouched. */
	CHECK(Arm(&s_core, 0xFFFFFFFFu) == 1);
	CHECK(ExpectDisarm(&idleMenu, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_UNTOUCHED) == 0);
	CHECK(ExpectDisarm(&idleMenu, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_UNTOUCHED) == 0);

	/* Later states still restore by the same rule; FAILED after Launch too. */
	CHECK(ArmLaunchSeed(&s_core) == 1);
	CHECK(ExpectDisarm(&idleMenu, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_RESTORED) == 0);
	CHECK(ArmAndLaunch(&s_core) == 1);
	{
		struct MainArcadeRaceSetupCoreBeginView begin;

		memset(&begin, 0, sizeof(begin));
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 0);
	}
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_FAILED);
	CHECK(ExpectDisarm(&view, MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED) == 0);

	CHECK(MainArcadeRaceSetupCore_Disarm(NULL, &idleMenu, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Disarm(&s_core, NULL, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Disarm(&s_core, &idleMenu, NULL) == 0);
	return 0;
}

/* The number of ops that write a pinned boot-relative counter (RS-17, LR-8). */
static uint32_t PinOpCount(const struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	uint32_t count = 0;

	for (uint32_t i = 0; (i < outcome->opCount) && (i < MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS); i++)
	{
		if ((outcome->ops[i].target == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_TIMER) ||
		    (outcome->ops[i].target == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_FRAME_TIMER_CONFETTI) ||
		    (outcome->ops[i].target == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RCNT_TOTAL_UNITS) ||
		    (outcome->ops[i].target == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CLOCK_FRAME_START))
		{
			count++;
		}
	}
	return count;
}

/*
 * Every step, from a copy of *core, writes no pinned counter: Arm, Launch,
 * both hooks with and without a tracker, and Disarm (restoring or not). Only
 * the pre-drivers hook in LAUNCHED with a tracker and the verified fields
 * pins; seeds is 1 when *core is that LAUNCHED state, and then that one step
 * must write each pin exactly once.
 */
static int ExpectPinsOnlyWhenSeeding(const struct MainArcadeRaceSetupCore *core, int seeds)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;
	struct MainArcadeRaceSetupCoreBeginView begin;
	struct MainArcadeRaceSetupCoreDisarmView disarm;

	TitleLaunchView(&launch);
	for (uint8_t tracker = 0; tracker < 2u; tracker++)
	{
		s_other = *core;
		LoadedBeginView(&s_plan, &begin);
		begin.trackerPresent = tracker;
		(void)MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_other, &begin, &s_scratch, &s_outcome);
		if ((seeds != 0) && (tracker != 0u))
		{
			CHECK(s_outcome.result == 1u && PinOpCount(&s_outcome) == MAIN_ARCADE_RACE_SETUP_CORE_PIN_OP_COUNT);
		}
		else
		{
			CHECK(PinOpCount(&s_outcome) == 0u);
		}
		/* A refused load (the wrong level) never pins either. */
		s_other = *core;
		begin.fields.levelID = LIVE_MAIN_MENU_LEVEL;
		(void)MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_other, &begin, &s_scratch, &s_outcome);
		CHECK(PinOpCount(&s_outcome) == 0u);
		s_other = *core;
		DriversView(&s_config, &s_drivers);
		s_drivers.trackerPresent = tracker;
		(void)MainArcadeRaceSetupCore_OnDriversInitialized(&s_other, &s_drivers, &s_scratch, &s_outcome);
		CHECK(PinOpCount(&s_outcome) == 0u);
	}
	s_other = *core;
	(void)MainArcadeRaceSetupCore_Arm(&s_other, &s_config, 0u, 1u, &s_outcome);
	CHECK(PinOpCount(&s_outcome) == 0u);
	s_other = *core;
	(void)MainArcadeRaceSetupCore_Launch(&s_other, &launch, &s_outcome);
	CHECK(PinOpCount(&s_outcome) == 0u);
	for (uint32_t restore = 0; restore < 2u; restore++)
	{
		s_other = *core;
		disarm.currentLevel = (restore != 0u) ? LIVE_MAIN_MENU_LEVEL : (int32_t)s_plan.levelID;
		disarm.loadingStage = LIVE_STAGE_IDLE;
		disarm.gameMode1 = 0u;
		(void)MainArcadeRaceSetupCore_Disarm(&s_other, &disarm, &s_outcome);
		CHECK(PinOpCount(&s_outcome) == 0u);
	}
	return 0;
}

/* RS-17: the pins are written only by the seeding step of a launched setup;
 * default boot (IDLE) and every other state never pin. */
static int TestPinsOnlyWhenSeeding(void)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;

	TitleLaunchView(&launch);
	MainArcadeRaceSetupCore_Reset(&s_core);
	CHECK(ExpectPinsOnlyWhenSeeding(&s_core, 0) == 0);
	CHECK(Arm(&s_core, 0u) == 1);
	CHECK(ExpectPinsOnlyWhenSeeding(&s_core, 0) == 0);
	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(ExpectPinsOnlyWhenSeeding(&s_core, 1) == 0);
	CHECK(ArmLaunchSeed(&s_core) == 1);
	CHECK(ExpectPinsOnlyWhenSeeding(&s_core, 0) == 0);
	CHECK(ArmLaunchSeedValidate(&s_core) == 1);
	CHECK(ExpectPinsOnlyWhenSeeding(&s_core, 0) == 0);
	CHECK(ArmAndLaunch(&s_core) == 1);
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 0);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_FAILED);
	CHECK(ExpectPinsOnlyWhenSeeding(&s_core, 0) == 0);
	/* The op count covers exactly the four pins (RS-17's two, LR-8's two). */
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_PIN_OP_COUNT == 4u);
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_BEGIN_OP_COUNT == 13u);
	return 0;
}

/*
 * An ARCADE_ONE_CAB config from the fixture config: its track, laps,
 * identities, and seed; CAB1 `human` at difficulty 0, slots 1..7 exactly
 * ExpectedBots1P at medium, and the 1P bot rules digest (RS-19, RS-20).
 */
static int BuildOneCabConfig(const struct NativeMatchConfigV1 *valid, uint8_t human, struct NativeMatchConfigV1 *config)
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
		config->slots[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM;
	}
	return NativeArcadeBotRules_Digest1PV1(config->botRulesDigest) && NativeArcadeBotRules_ValidateConfigV1(config);
}

/* Arm with config from the title, and Launch. */
static int ArmAndLaunchConfig(struct MainArcadeRaceSetupCore *core, const struct NativeMatchConfigV1 *config)
{
	struct MainArcadeRaceSetupCoreLaunchView launch;

	TitleLaunchView(&launch);
	MainArcadeRaceSetupCore_Reset(core);
	return MainArcadeRaceSetupCore_Arm(core, config, launch.fields.gameMode1, 1u, &s_outcome) &&
	       MainArcadeRaceSetupCore_Launch(core, &launch, &s_outcome);
}

/* ... then the pre-drivers hook on the loaded race and the post-drivers hook on its drivers. */
static int RunToValidated(struct MainArcadeRaceSetupCore *core, const struct NativeMatchConfigV1 *config)
{
	struct MainArcadeRaceSetupCoreBeginView begin;

	if (!ArmAndLaunchConfig(core, config))
	{
		return 0;
	}
	LoadedBeginView(&core->plan, &begin);
	DriversView(config, &s_drivers);
	return MainArcadeRaceSetupCore_OnFinalizeInitBegin(core, &begin, &s_scratch, &s_outcome) &&
	       MainArcadeRaceSetupCore_OnDriversInitialized(core, &s_drivers, &s_scratch, &s_outcome);
}

/* The draw count of one bank stream, or UINT64_MAX if the bank has no such stream. */
static uint64_t StreamDraws(const struct NativeDeterministicRngBankV1 *bank, uint32_t tag)
{
	for (uint32_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		if (bank->streams[i].tag == tag)
		{
			return bank->streams[i].drawCount;
		}
	}
	return UINT64_MAX;
}

/* An ARCADE_ONE_CAB setup end to end (OC-2), and what differs from TWO_CAB. */
static int TestOneCab(void)
{
	static struct NativeMatchConfigV1 oneCab;
	static struct NativeMatchConfigV1 reseeded;
	static struct MainArcadeRaceSetupPlan plan;
	static const uint8_t expectedLaunchTargets[] = {
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE2,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ARCADE_DIFFICULTY, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_BOOL_DEMO_MODE,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_LAPS, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_PLYR_NEXT_GAME,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID,
		MAIN_ARCADE_RACE_SETUP_CORE_TARGET_REQUEST_LOAD};
	static const int64_t goldenSeeds[5] = {GOLDEN_RANDOM_NUMBER, GOLDEN_ADV_RNG0, GOLDEN_ADV_RNG1, GOLDEN_PSX_RAND, GOLDEN_AUDIO_RNG};
	struct MainArcadeRaceSetupCoreBeginView begin;
	struct MainArcadeRaceSetupCoreBeginView broken;
	struct MainArcadeRaceSetupCoreLaunchView launch;
	struct MainArcadeBotSetupSourceFacts facts;
	uint8_t digests[4][NATIVE_SHA256_DIGEST_BYTES];
	uint8_t again[4][NATIVE_SHA256_DIGEST_BYTES];
	uint8_t expected[NATIVE_SHA256_DIGEST_BYTES];

	/* The TWO_CAB drivers view is unchanged: humans 0..1, bots 2..5, 6..7 absent. */
	DriversView(&s_config, &s_drivers);
	CHECK(s_drivers.snapshot.numPlyrCurrGame == 2u && s_drivers.snapshot.numBotsNextGame == 4u);
	CHECK(s_drivers.snapshot.driverPresent[5] == 1u && s_drivers.snapshot.driverPresent[6] == 0u);
	CHECK(s_drivers.snapshot.characterIDs[6] == 10 && s_drivers.snapshot.arcadeDifficulty == 0xA0);

	/* Launch has one op count for both profiles, within MAX_OPS. */
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT == 15u);
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT <= MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS);
	CHECK(MAIN_ARCADE_RACE_SETUP_CORE_BEGIN_OP_COUNT <= MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS);

	/* Human Crash (0) on the fixture's track, laps, and golden seed. */
	CHECK(BuildOneCabConfig(&s_config, 0u, &oneCab) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&oneCab, &plan) == 1);
	CHECK(plan.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB && plan.numPlyrNextGame == 1u);
	CHECK(plan.characterWriteMask == 0xFFu && plan.firstBotSlot == 1u && plan.botCount == 7u);

	/* Arm. */
	MainArcadeRaceSetupCore_Reset(&s_core);
	CHECK(MainArcadeRaceSetupCore_Arm(&s_core, &oneCab, 0u, 1u, &s_outcome) == 1);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_ARMED);
	CHECK(memcmp(&s_core.plan, &plan, sizeof(plan)) == 0);
	CHECK(MainArcadeRaceSetupPlan_Digest(&plan, expected) == 1 && memcmp(s_core.racePlanDigest, expected, sizeof(expected)) == 0);

	/* Launch: numPlyrNextGame 1 and all eight characterIDs from the plan. The
	 * live numPlyrNextGame and characterIDs differ from the plan, so each op
	 * value below is the plan's write, not a pass-through. */
	TitleLaunchView(&launch);
	launch.fields.numPlyrNextGame = 4u;
	for (uint32_t slot = 0; slot < 8u; slot++)
	{
		launch.fields.characterIDs[slot] = (int16_t)(40 + (int)slot);
	}
	CHECK(MainArcadeRaceSetupCore_Launch(&s_core, &launch, &s_outcome) == 1);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_LAUNCHED);
	CHECK(s_outcome.opCount == (uint32_t)sizeof(expectedLaunchTargets));
	CHECK(s_outcome.opCount == MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT && s_outcome.overflowed == 0u);
	for (uint32_t i = 0; i < s_outcome.opCount; i++)
	{
		CHECK(s_outcome.ops[i].target == expectedLaunchTargets[i]);
		CHECK(s_outcome.ops[i].index == ((i >= 6u && i < 14u) ? (uint8_t)(i - 6u) : 0u));
	}
	CHECK(s_outcome.ops[0].value ==
	      (int64_t)((launch.fields.gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(s_outcome.ops[1].value == (int64_t)(launch.fields.gameMode2 & MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK));
	CHECK(s_outcome.ops[2].value == 0xA0 && s_outcome.ops[3].value == 0);
	CHECK(s_outcome.ops[4].value == (int64_t)plan.numLaps);
	CHECK(s_outcome.ops[5].value == 1);
	CHECK(s_outcome.ops[5].value != (int64_t)launch.fields.numPlyrNextGame);
	for (uint32_t slot = 0; slot < 8u; slot++)
	{
		CHECK(s_outcome.ops[6u + slot].value == (int64_t)plan.characterIDs[slot]);
		CHECK(s_outcome.ops[6u + slot].value == (int64_t)slot); /* Crash, then LOAD_Robots1P's 1..7 */
		CHECK(s_outcome.ops[6u + slot].value != (int64_t)launch.fields.characterIDs[slot]);
	}
	CHECK(s_outcome.ops[14].value == (int64_t)plan.levelID);

	/* The pre-drivers hook: every owned slot is verified, slot 7 included. */
	LoadedBeginView(&plan, &begin);
	CHECK(begin.numPlyrCurrGame == 1u && begin.fields.characterIDs[7] == 7);
	for (uint32_t slot = 0; slot < 8u; slot++)
	{
		broken = begin;
		broken.fields.characterIDs[slot] = (int16_t)(broken.fields.characterIDs[slot] + 1);
		CHECK(ArmAndLaunchConfig(&s_core, &oneCab) == 1);
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &broken, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH) == 0);
		CHECK(strcmp(s_outcome.detail, "characterIDs") == 0);
	}
	/* numPlyrCurrGame 2 (a 2P load) fails. */
	broken = begin;
	broken.numPlyrCurrGame = 2u;
	CHECK(ArmAndLaunchConfig(&s_core, &oneCab) == 1);
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &broken, &s_scratch, &s_outcome) == 0);
	CHECK(ExpectFailed(&s_core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH) == 0);
	CHECK(strcmp(s_outcome.detail, "numLaps or numPlyrCurrGame") == 0);
	/* The same characterIDs[7] mismatch is ignored under TWO_CAB (slot 7 not owned). */
	{
		struct MainArcadeRaceSetupCoreBeginView twoCab;

		LoadedBeginView(&s_plan, &twoCab);
		twoCab.fields.characterIDs[6] = 7;
		twoCab.fields.characterIDs[7] = (int16_t)(begin.fields.characterIDs[7] + 1);
		CHECK(ArmAndLaunch(&s_core) == 1);
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &twoCab, &s_scratch, &s_outcome) == 1);
		CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_SEEDED);
	}

	/* Success: the same seeds as TWO_CAB for the same master seed (the seeds
	 * are profile-independent), five MATCH_SETUP draws in. */
	CHECK(ArmAndLaunchConfig(&s_core, &oneCab) == 1);
	CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_core, &begin, &s_scratch, &s_outcome) == 1);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_SEEDED);
	CHECK(s_outcome.opCount == MAIN_ARCADE_RACE_SETUP_CORE_BEGIN_OP_COUNT && s_outcome.overflowed == 0u);
	for (uint32_t i = 0; i < 5u; i++)
	{
		CHECK(s_outcome.ops[8u + i].value == goldenSeeds[i]);
	}
	CHECK(StreamDraws(&s_core.bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) == 5u);

	/* The post-drivers hook on the 8-driver race: VALIDATED, nothing written. */
	DriversView(&oneCab, &s_drivers);
	CHECK(s_drivers.snapshot.numPlyrCurrGame == 1u && s_drivers.snapshot.numBotsNextGame == 7u);
	CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(&s_core, &s_drivers, &s_scratch, &s_outcome) == 1);
	CHECK(s_outcome.opCount == 0u && s_outcome.botSetupResult == (int32_t)MAIN_ARCADE_BOT_SETUP_OK);
	CHECK(MainArcadeRaceSetupCore_Status(&s_core) == MAIN_ARCADE_RACE_SETUP_VALIDATED);
	CHECK(s_core.botSetupPlan.botCount == 7u && s_core.botSetupPlan.botMask == 0xFEu);
	CHECK(MainArcadeRaceSetupCore_SlotFacts(&s_core, &facts) == 1);
	for (uint32_t slot = 0; slot < 8u; slot++)
	{
		CHECK(facts.facts[slot].present == 1u);
		CHECK(facts.facts[slot].role == (slot == 0u ? NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN : NATIVE_MATCH_SLOT_ROLE_BOT));
	}
	/* The post-setup bank: MATCH_SETUP drawn exactly 12 times (5 seeds + 7 bots), every other stream untouched. */
	CHECK(MainArcadeRaceSetupCore_Bank(&s_core) == &s_core.bank);
	for (uint32_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		const struct NativeDeterministicRngStreamV1 *stream = &s_core.bank.streams[i];

		CHECK(stream->drawCount ==
		      ((stream->tag == (uint32_t)NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) ? UINT64_C(12) : UINT64_C(0)));
	}
	CHECK(StreamDraws(&s_core.bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) == 12u);
	CHECK(MainArcadeRaceSetupCore_Digests(&s_core, digests[0], digests[1], digests[2], digests[3]) == 1);
	CHECK(MainArcadeRaceSetupPlan_Digest(&plan, expected) == 1 && memcmp(digests[1], expected, sizeof(expected)) == 0);

	/* Deterministic: a second run from the same config gives the same digests. */
	CHECK(RunToValidated(&s_other, &oneCab) == 1);
	CHECK(MainArcadeRaceSetupCore_Digests(&s_other, again[0], again[1], again[2], again[3]) == 1);
	CHECK(memcmp(again, digests, sizeof(again)) == 0);
	CHECK(memcmp(&s_other.seeds, &s_core.seeds, sizeof(s_core.seeds)) == 0);

	/* A different master seed: different seeds, bank, config, and plan digests. */
	reseeded = oneCab;
	reseeded.masterSeed ^= UINT64_C(0x8000000000000001);
	CHECK(RunToValidated(&s_other, &reseeded) == 1);
	CHECK(MainArcadeRaceSetupCore_Digests(&s_other, again[0], again[1], again[2], again[3]) == 1);
	CHECK(memcmp(&s_other.seeds, &s_core.seeds, sizeof(s_core.seeds)) != 0);
	CHECK(memcmp(again[3], digests[3], sizeof(again[3])) != 0);
	CHECK(memcmp(again[0], digests[0], sizeof(again[0])) != 0 && memcmp(again[1], digests[1], sizeof(again[1])) != 0);
	CHECK(StreamDraws(&s_other.bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) == 12u);

	/* The TWO_CAB run from the same master seed differs in its plan and bank (9 draws). */
	CHECK(RunToValidated(&s_other, &s_config) == 1);
	CHECK(StreamDraws(&s_other.bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) == 9u);
	CHECK(memcmp(&s_other.seeds, &s_core.seeds, sizeof(s_core.seeds)) == 0);
	CHECK(MainArcadeRaceSetupCore_Digests(&s_other, again[0], again[1], again[2], again[3]) == 1);
	CHECK(memcmp(again[1], digests[1], sizeof(again[1])) != 0 && memcmp(again[3], digests[3], sizeof(again[3])) != 0);

	/* Drivers views that are not a 1P race fail closed with FACTS or ROSTER. */
	{
		struct MainArcadeRaceSetupCore *core = &s_core;

		CHECK(ArmAndLaunchConfig(core, &oneCab) == 1);
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(core, &begin, &s_scratch, &s_outcome) == 1);
		DriversView(&s_config, &s_drivers); /* the 2P race: a human in slot 1 */
		CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(core, &s_drivers, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS) == 0);

		CHECK(ArmAndLaunchConfig(core, &oneCab) == 1);
		CHECK(MainArcadeRaceSetupCore_OnFinalizeInitBegin(core, &begin, &s_scratch, &s_outcome) == 1);
		DriversView(&oneCab, &s_drivers);
		s_drivers.snapshot.characterIDs[7] = 0; /* not LOAD_Robots1P's bot */
		CHECK(MainArcadeRaceSetupCore_OnDriversInitialized(core, &s_drivers, &s_scratch, &s_outcome) == 0);
		CHECK(ExpectFailed(core, &s_outcome, MAIN_ARCADE_RACE_SETUP_FAILURE_ROSTER) == 0);
	}
	return 0;
}

static int TestNames(void)
{
	CHECK(strcmp(MainArcadeRaceSetupCore_StatusName(MAIN_ARCADE_RACE_SETUP_IDLE), "IDLE") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_StatusName(MAIN_ARCADE_RACE_SETUP_VALIDATED), "VALIDATED") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_StatusName(MAIN_ARCADE_RACE_SETUP_FAILED), "FAILED") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_StatusName((enum MainArcadeRaceSetupStatus)99), "UNKNOWN") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName(MAIN_ARCADE_RACE_SETUP_FAILURE_NONE), "NONE") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName(MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH), "LOAD_FIELDS_MISMATCH") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName(MAIN_ARCADE_RACE_SETUP_FAILURE_STATE), "STATE") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName(MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER), "NO_TRACKER") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName((enum MainArcadeRaceSetupFailure)99), "UNKNOWN") == 0);
	CHECK(strcmp(MainArcadeRaceSetupCore_FailureName(MAIN_ARCADE_RACE_SETUP_FAILURE_OPS), "OPS") == 0);
	/* The failure codes are append-only. */
	CHECK((int)MAIN_ARCADE_RACE_SETUP_FAILURE_STATE == 10 && (int)MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER == 11);
	CHECK((int)MAIN_ARCADE_RACE_SETUP_FAILURE_OPS == 12);
	CHECK(MainArcadeRaceSetupCore_Status(NULL) == MAIN_ARCADE_RACE_SETUP_IDLE);
	CHECK(MainArcadeRaceSetupCore_Failure(NULL) == MAIN_ARCADE_RACE_SETUP_FAILURE_NONE);
	CHECK(MainArcadeRaceSetupCore_Bank(NULL) == NULL);
	CHECK(MainArcadeRaceSetupCore_HookReadsView(NULL, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN) == 0);
	MainArcadeRaceSetupCore_Reset(NULL);
	return 0;
}

int main(void)
{
	CHECK(BuildConfig(&s_config) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&s_config, &s_plan) == 1);
	CHECK(TestArm() == 0);
	CHECK(TestArmMarksOptions() == 0);
	CHECK(TestWrongState() == 0);
	CHECK(TestLaunch() == 0);
	CHECK(TestFinalizeInitBegin() == 0);
	CHECK(TestDriversInitialized() == 0);
	CHECK(TestDisarm() == 0);
	CHECK(TestPinsOnlyWhenSeeding() == 0);
	CHECK(TestOneCab() == 0);
	CHECK(TestNames() == 0);
	puts("main_arcade_race_setup_core_test: ok");
	return 0;
}
