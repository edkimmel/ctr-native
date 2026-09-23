/*
 * Live race setup adapter (docs/ROSTER_MILESTONE.md section 3.2, task R-5b).
 * See MAIN/MainArcadeRaceSetup.h for the state machine and
 * MAIN/MainArcadeRaceSetupPlan.h for the R-5 contract it implements. Native
 * only; dormant unless MainArcadeRaceSetup_Arm is called.
 *
 * Unity-included after the MainCanonical* sources (it reads the live roster
 * input through MainCanonicalDrivers_ExtractRosterInput, and its live
 * snapshot follows the same gGT/sdata/data conventions) and before the
 * arcade-link sources, so every later native launcher (the internal roster
 * proof, Task 7's link) sees it. MainInit.c, earlier in the chain, calls the
 * two hooks through MAIN/MainArcadeRaceSetup.h.
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRaceSetup.h"
#include "MAIN/MainArcadeRaceSetupFacts.h"
#include "MAIN/MainArcadeRaceSetupPlan.h"
#include "MAIN/MainArcadeRoster.h"
#include "MAIN/MainCanonicalDrivers.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_log.h"
#include "platform/native_match_config.h"

/* The plan mirrors these retail values without including game headers;
 * keep the mirrors the adapter relies on honest. */
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL == (uint32_t)PAUSE_ALL, "MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL must match PAUSE_ALL");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE == (uint32_t)ARCADE_MODE, "MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE must match ARCADE_MODE");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK == (uint32_t)GAME_MODE_VIBRATION_MASK, "MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK must match GAME_MODE_VIBRATION_MASK");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL == (uint32_t)CHEAT_ALL, "MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL must match CHEAT_ALL");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT == sizeof(data.characterIDs) / sizeof(data.characterIDs[0]), "MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT must match data.characterIDs");
_Static_assert(MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT == sizeof(sdata->kartSpawnOrderArray), "the facts snapshot must hold every kartSpawnOrderArray entry");
_Static_assert(MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT == sizeof(sdata->driver_pathIndexIDs), "the facts snapshot must hold every driver_pathIndexIDs entry");
_Static_assert(MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT == sizeof(sdata->accelerateOrder), "the facts snapshot must hold every accelerateOrder entry");
_Static_assert(MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES == NATIVE_SHA256_DIGEST_BYTES, "MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES must match NATIVE_SHA256_DIGEST_BYTES");
_Static_assert(MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT == sizeof(sdata->NavPath_ptrHeader) / sizeof(sdata->NavPath_ptrHeader[0]), "MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT must match NavPath_ptrHeader");

#define MAIN_ARCADE_RACE_SETUP_LOG "[CTR Native] arcade race setup: "

/*
 * The whole adapter state: file-scope static, outside every saved-state
 * region, never recorded or canonical (RS-11). bank is the post-Arm bank,
 * then the post-seed bank from SEEDED, then the post-setup bank from
 * VALIDATED.
 */
struct MainArcadeRaceSetupState
{
	enum MainArcadeRaceSetupStatus status;
	enum MainArcadeRaceSetupFailure failure;
	uint32_t savedVibration; /* gameMode1 & HOST_LOCAL_MASK at Arm */
	uint8_t fieldsWritten;   /* 1 once Launch wrote the live fields */
	uint8_t reserved[3];
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;
	struct NativeDeterministicRngBankV1 bank;
	struct MainArcadeBotSetupPlan botSetupPlan;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t racePlanDigest[NATIVE_SHA256_DIGEST_BYTES];
};

static struct MainArcadeRaceSetupState s_mainArcadeRaceSetup;

/* Scratch for the post-drivers validation, too large for comfort on the
 * game stack; only OnDriversInitialized uses it. */
struct MainArcadeRaceSetupScratch
{
	struct MainArcadeRaceSetupLiveSnapshot snapshot;
	struct NativeCanonicalDriversRosterInput rosterInput;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct MainArcadeRosterPlan rosterPlan;
	struct MainArcadeRosterValidated validated;
	struct MainArcadeBotSetupPlan botSetupPlan;
	struct NativeDeterministicRngBankV1 bankAfter;
};

static struct MainArcadeRaceSetupScratch s_mainArcadeRaceSetupScratch;

const char *MainArcadeRaceSetup_StatusName(enum MainArcadeRaceSetupStatus status)
{
	switch (status)
	{
	case MAIN_ARCADE_RACE_SETUP_IDLE:
		return "IDLE";
	case MAIN_ARCADE_RACE_SETUP_ARMED:
		return "ARMED";
	case MAIN_ARCADE_RACE_SETUP_LAUNCHED:
		return "LAUNCHED";
	case MAIN_ARCADE_RACE_SETUP_SEEDED:
		return "SEEDED";
	case MAIN_ARCADE_RACE_SETUP_VALIDATED:
		return "VALIDATED";
	case MAIN_ARCADE_RACE_SETUP_FAILED:
		return "FAILED";
	default:
		return "UNKNOWN";
	}
}

const char *MainArcadeRaceSetup_FailureName(enum MainArcadeRaceSetupFailure failure)
{
	switch (failure)
	{
	case MAIN_ARCADE_RACE_SETUP_FAILURE_NONE:
		return "NONE";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN:
		return "PLAN";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_BANK:
		return "BANK";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION:
		return "PRECONDITION";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_LEVEL_MISMATCH:
		return "LEVEL_MISMATCH";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH:
		return "LOAD_FIELDS_MISMATCH";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_SEED:
		return "SEED";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS:
		return "FACTS";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_ROSTER:
		return "ROSTER";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_BOT_SETUP:
		return "BOT_SETUP";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_STATE:
		return "STATE";
	default:
		return "UNKNOWN";
	}
}

static void MainArcadeRaceSetup_Enter(enum MainArcadeRaceSetupStatus status)
{
	s_mainArcadeRaceSetup.status = status;
	Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "%s\n", MainArcadeRaceSetup_StatusName(status));
}

/* Latches FAILED with the first failure code; detail says why. */
static void MainArcadeRaceSetup_Fail(enum MainArcadeRaceSetupFailure failure, const char *detail)
{
	s_mainArcadeRaceSetup.status = MAIN_ARCADE_RACE_SETUP_FAILED;
	s_mainArcadeRaceSetup.failure = failure;
	Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "FAILED %s (%s)\n", MainArcadeRaceSetup_FailureName(failure), detail);
}

/* A call in the wrong state: fail closed while a setup is in flight. */
static void MainArcadeRaceSetup_WrongState(const char *call)
{
	const enum MainArcadeRaceSetupStatus status = s_mainArcadeRaceSetup.status;

	if ((status == MAIN_ARCADE_RACE_SETUP_IDLE) || (status == MAIN_ARCADE_RACE_SETUP_FAILED))
	{
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "%s refused in state %s\n", call, MainArcadeRaceSetup_StatusName(status));
		return;
	}
	MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_STATE, call);
}

/* The live values of the fields the plan owns (characterIDs from data). */
static void MainArcadeRaceSetup_ReadFields(const struct GameTracker *gGT, struct MainArcadeRaceSetupRetailFields *fields)
{
	memset(fields, 0, sizeof(*fields));
	fields->levelID = (int32_t)gGT->levelID;
	fields->gameMode1 = (uint32_t)gGT->gameMode1;
	fields->gameMode2 = (uint32_t)gGT->gameMode2;
	fields->arcadeDifficulty = (int32_t)gGT->arcadeDifficulty;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		fields->characterIDs[slot] = (int16_t)data.characterIDs[slot];
	}
	fields->numLaps = (int8_t)gGT->numLaps;
	fields->numPlyrNextGame = (uint8_t)gGT->numPlyrNextGame;
	fields->boolDemoMode = (uint8_t)gGT->boolDemoMode;
}

/* The mode words, arcadeDifficulty, and boolDemoMode: what the pre-drivers hook re-applies. */
static void MainArcadeRaceSetup_WriteModeFields(struct GameTracker *gGT, const struct MainArcadeRaceSetupRetailFields *fields)
{
	gGT->gameMode1 = (int)fields->gameMode1;
	gGT->gameMode2 = (int)fields->gameMode2;
	gGT->arcadeDifficulty = (int)fields->arcadeDifficulty;
	gGT->boolDemoMode = (char)fields->boolDemoMode;
}

int MainArcadeRaceSetup_Arm(const struct NativeMatchConfigV1 *config)
{
	struct MainArcadeRaceSetupState *state = &s_mainArcadeRaceSetup;

	if (state->status != MAIN_ARCADE_RACE_SETUP_IDLE)
	{
		MainArcadeRaceSetup_WrongState("Arm");
		return 0;
	}
	memset(state, 0, sizeof(*state));
	if ((config == NULL) || !MainArcadeRaceSetupPlan_Build(config, &state->plan) ||
	    !MainArcadeRaceSetupPlan_Digest(&state->plan, state->racePlanDigest))
	{
		memset(state, 0, sizeof(*state));
		state->failure = MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN;
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "Arm refused: PLAN (the plan refused the config); still IDLE\n");
		return 0;
	}
	if (!NativeDeterministicRngBankV1_Init(&state->bank, config->masterSeed, config->rngDerivationVersion))
	{
		memset(state, 0, sizeof(*state));
		state->failure = MAIN_ARCADE_RACE_SETUP_FAILURE_BANK;
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "Arm refused: BANK (the bank could not be derived); still IDLE\n");
		return 0;
	}
	state->config = *config;
	memcpy(state->configDigest, state->plan.configDigest, sizeof(state->configDigest));
	state->savedVibration = (uint32_t)sdata->gGT->gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK;
	Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "armed track %d laps %d seed 0x%08X%08X difficulty 0x%X characters %d %d %d %d %d %d\n",
		(int)state->plan.levelID, (int)state->plan.numLaps, (unsigned)(uint32_t)(state->plan.masterSeed >> 32),
		(unsigned)(uint32_t)(state->plan.masterSeed & 0xFFFFFFFFu), (unsigned)state->plan.arcadeDifficulty,
		(int)state->plan.characterIDs[0], (int)state->plan.characterIDs[1], (int)state->plan.characterIDs[2],
		(int)state->plan.characterIDs[3], (int)state->plan.characterIDs[4], (int)state->plan.characterIDs[5]);
	MainArcadeRaceSetup_Enter(MAIN_ARCADE_RACE_SETUP_ARMED);
	return 1;
}

int MainArcadeRaceSetup_Launch(void)
{
	struct MainArcadeRaceSetupState *state = &s_mainArcadeRaceSetup;
	struct GameTracker *gGT = sdata->gGT;
	struct MainArcadeRaceSetupRetailFields fields;

	if (state->status != MAIN_ARCADE_RACE_SETUP_ARMED)
	{
		MainArcadeRaceSetup_WrongState("Launch");
		return 0;
	}
	if (sdata->Loading.stage != LOAD_IDLE)
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "a load is in progress");
		return 0;
	}
	if ((sdata->Loading.OnBegin.AddBitsConfig0 != 0u) || (sdata->Loading.OnBegin.RemBitsConfig0 != 0u) ||
	    (sdata->Loading.OnBegin.AddBitsConfig8 != 0u) || (sdata->Loading.OnBegin.RemBitsConfig8 != 0u))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "pending OnBegin mode bits");
		return 0;
	}
	if (sdata->boolHasLoadedOptions == 0)
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "the game options are not loaded yet");
		return 0;
	}
	if (((uint32_t)gGT->gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL) != 0u)
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "the game is paused");
		return 0;
	}

	MainArcadeRaceSetup_ReadFields(gGT, &fields);
	if (!MainArcadeRaceSetupPlan_Apply(&state->plan, &fields, &fields))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN, "the plan could not be applied");
		return 0;
	}
	/* Every owned field except levelID: the load request carries it, and
	 * LOAD_LevelFile writes it (never while the old level runs). */
	MainArcadeRaceSetup_WriteModeFields(gGT, &fields);
	gGT->numLaps = (s8)fields.numLaps;
	gGT->numPlyrNextGame = (u8)fields.numPlyrNextGame;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		data.characterIDs[slot] = (s16)fields.characterIDs[slot];
	}
	state->fieldsWritten = 1u;
	MainRaceTrack_RequestLoad((s16)state->plan.levelID);
	MainArcadeRaceSetup_Enter(MAIN_ARCADE_RACE_SETUP_LAUNCHED);
	return 1;
}

void MainArcadeRaceSetup_OnFinalizeInitBegin(struct GameTracker *gGT)
{
	struct MainArcadeRaceSetupState *state = &s_mainArcadeRaceSetup;
	struct MainArcadeRaceSetupRetailFields fields;
	struct NativeDeterministicRngBankV1 bank;
	struct NativeArcadeRetailRngSeedsV1 seeds;

	if ((state->status == MAIN_ARCADE_RACE_SETUP_SEEDED) || (state->status == MAIN_ARCADE_RACE_SETUP_VALIDATED))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_STATE, "a new race init over an unfinished or validated setup");
		return;
	}
	if ((state->status != MAIN_ARCADE_RACE_SETUP_LAUNCHED) || (gGT == NULL))
	{
		return;
	}

	/* 1. Verify the fields the load consumed. */
	if ((int32_t)gGT->levelID != state->plan.levelID)
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_LEVEL_MISMATCH, "the loaded level is not the plan's");
		return;
	}
	if (((int8_t)gGT->numLaps != state->plan.numLaps) || ((uint8_t)gGT->numPlyrCurrGame != state->plan.numPlyrNextGame))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH, "numLaps or numPlyrCurrGame");
		return;
	}
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		/* characterIDs[2..5] are the bots as LOAD_Robots2P wrote them; the
		 * plan holds exactly those (RS-4). Slots 6 and 7 are not owned. */
		if ((((uint32_t)state->plan.characterWriteMask >> slot) & 1u) != 0u &&
		    ((int16_t)data.characterIDs[slot] != state->plan.characterIDs[slot]))
		{
			MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH, "characterIDs");
			return;
		}
	}

	/* 2. Re-apply only the mode words, arcadeDifficulty, and boolDemoMode. */
	MainArcadeRaceSetup_ReadFields(gGT, &fields);
	if (!MainArcadeRaceSetupPlan_Apply(&state->plan, &fields, &fields))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN, "the plan could not be re-applied");
		return;
	}

	/* 3. Seed the retail RNG states (RS-7), transactionally on a copy. */
	bank = state->bank;
	if (!NativeArcadeBotRules_DeriveRetailSeedsV1(&bank, &seeds))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_SEED, "the retail seeds could not be derived");
		return;
	}
	MainArcadeRaceSetup_WriteModeFields(gGT, &fields);
	sdata->randomNumber = (int)seeds.randomNumber;
	sdata->advRng.state0 = seeds.advRng0;
	sdata->advRng.state1 = seeds.advRng1;
	PSX_BIOS_SetRandSeed(seeds.psxRandSeed);
	sdata->audioRNG = seeds.audioRNG;
	state->bank = bank;
	Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "seeded randomNumber 0x%04X advRng 0x%08X 0x%08X psxRand 0x%08X audioRNG 0x%08X\n",
		(unsigned)seeds.randomNumber, (unsigned)seeds.advRng0, (unsigned)seeds.advRng1, (unsigned)seeds.psxRandSeed,
		(unsigned)seeds.audioRNG);
	MainArcadeRaceSetup_Enter(MAIN_ARCADE_RACE_SETUP_SEEDED);
}

/* The pointer-free snapshot of the race after MainInit_Drivers. */
static void MainArcadeRaceSetup_ReadSnapshot(const struct GameTracker *gGT, struct MainArcadeRaceSetupLiveSnapshot *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->numPlyrCurrGame = (uint8_t)gGT->numPlyrCurrGame;
	snapshot->numBotsNextGame = (uint8_t)gGT->numBotsNextGame;
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT; slot++)
	{
		const struct Driver *driver = gGT->drivers[slot];

		if (driver != NULL)
		{
			snapshot->driverPresent[slot] = 1u;
			snapshot->driverID[slot] = (uint8_t)driver->driverID;
			snapshot->driverIsBot[slot] = ((driver->actionsFlagSet & ACTION_BOT) != 0) ? 1u : 0u;
		}
		snapshot->characterIDs[slot] = (int16_t)data.characterIDs[slot];
		snapshot->kartSpawnOrderArray[slot] = (uint8_t)sdata->kartSpawnOrderArray[slot];
		snapshot->driver_pathIndexIDs[slot] = (int8_t)sdata->driver_pathIndexIDs[slot];
		snapshot->accelerateOrder[slot] = (uint8_t)sdata->accelerateOrder[slot];
	}
	snapshot->arcadeDifficulty = (int32_t)gGT->arcadeDifficulty;
}

/* A NAV_MISMATCH is either tampering or BOTS_Driver_Init falling back to a
 * lower path when the chosen one has fewer than 2 points (BOTS.c:3061-3085);
 * the retail nav headers tell them apart. */
static void MainArcadeRaceSetup_LogNavHeaders(void)
{
	for (uint32_t path = 0; path < MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT; path++)
	{
		const struct NavHeader *header = sdata->NavPath_ptrHeader[path];

		if (header == NULL)
		{
			Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "nav path %u: no header\n", (unsigned)path);
		}
		else
		{
			Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "nav path %u: numPoints %d\n", (unsigned)path, (int)header->numPoints);
		}
	}
}

void MainArcadeRaceSetup_OnDriversInitialized(struct GameTracker *gGT)
{
	struct MainArcadeRaceSetupState *state = &s_mainArcadeRaceSetup;
	struct MainArcadeRaceSetupScratch *scratch = &s_mainArcadeRaceSetupScratch;
	enum MainArcadeBotSetupResult result;
	const uint32_t expectedGameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK;
	const uint32_t ownedGameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK | MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK;
	const uint32_t ownedGameMode2 = MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK | MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK;

	if (state->status == MAIN_ARCADE_RACE_SETUP_LAUNCHED)
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_STATE, "drivers initialized before the seeding hook ran");
		return;
	}
	if ((state->status != MAIN_ARCADE_RACE_SETUP_SEEDED) || (gGT == NULL))
	{
		return;
	}

	/* The plan's mode bits still hold, and no cheat bit came back. */
	if ((((uint32_t)gGT->gameMode2 & MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL) != 0u) ||
	    (((uint32_t)gGT->gameMode1 & ownedGameMode1) != expectedGameMode1) ||
	    (((uint32_t)gGT->gameMode2 & ownedGameMode2) != MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS, "the plan's mode bits no longer hold");
		return;
	}

	memset(scratch, 0, sizeof(*scratch));
	MainArcadeRaceSetup_ReadSnapshot(gGT, &scratch->snapshot);
	if (!MainCanonicalDrivers_ExtractRosterInput(gGT, sdata, &scratch->rosterInput))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS, "the live roster input could not be extracted");
		return;
	}
	if (!MainArcadeRaceSetupFacts_Build(&state->config, &scratch->snapshot, &scratch->rosterInput, &scratch->rosterFacts,
	        &scratch->setupFacts))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS, "the facts could not be built from the live race");
		return;
	}
	if (!MainArcadeRoster_BuildPlan(&state->config, &scratch->rosterPlan) ||
	    !MainArcadeRoster_ValidateNativeFacts(&scratch->rosterPlan, &state->config, &scratch->rosterFacts, &scratch->validated))
	{
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_ROSTER, "the live roster does not match the config");
		return;
	}
	result = MainArcadeBotSetup_Plan(&state->config, &scratch->rosterPlan, &scratch->validated, &scratch->setupFacts,
		&state->bank, &scratch->botSetupPlan, &scratch->bankAfter);
	if (result != MAIN_ARCADE_BOT_SETUP_OK)
	{
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "bot setup result %d\n", (int)result);
		if (result == MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH)
		{
			MainArcadeRaceSetup_LogNavHeaders();
		}
		MainArcadeRaceSetup_Fail(MAIN_ARCADE_RACE_SETUP_FAILURE_BOT_SETUP, "the bot setup refused the live facts");
		return;
	}

	state->botSetupPlan = scratch->botSetupPlan;
	state->setupFacts = scratch->setupFacts;
	state->bank = scratch->bankAfter;
	MainArcadeRaceSetup_Enter(MAIN_ARCADE_RACE_SETUP_VALIDATED);
}

enum MainArcadeRaceSetupStatus MainArcadeRaceSetup_Status(void)
{
	return s_mainArcadeRaceSetup.status;
}

enum MainArcadeRaceSetupFailure MainArcadeRaceSetup_Failure(void)
{
	return s_mainArcadeRaceSetup.failure;
}

int MainArcadeRaceSetup_Digests(uint8_t configDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t racePlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t botSetupPlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t bankDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES])
{
	const struct MainArcadeRaceSetupState *state = &s_mainArcadeRaceSetup;
	uint8_t botSetup[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t bank[NATIVE_SHA256_DIGEST_BYTES];

	if ((configDigest == NULL) || (racePlanDigest == NULL) || (botSetupPlanDigest == NULL) || (bankDigest == NULL) ||
	    (state->status != MAIN_ARCADE_RACE_SETUP_VALIDATED) ||
	    !MainArcadeBotSetupPlan_Digest(&state->botSetupPlan, botSetup) ||
	    !NativeDeterministicRngBankV1_Digest(&state->bank, bank))
	{
		return 0;
	}
	memcpy(configDigest, state->configDigest, NATIVE_SHA256_DIGEST_BYTES);
	memcpy(racePlanDigest, state->racePlanDigest, NATIVE_SHA256_DIGEST_BYTES);
	memcpy(botSetupPlanDigest, botSetup, NATIVE_SHA256_DIGEST_BYTES);
	memcpy(bankDigest, bank, NATIVE_SHA256_DIGEST_BYTES);
	return 1;
}

int MainArcadeRaceSetup_SlotFacts(struct MainArcadeBotSetupSourceFacts *out)
{
	if ((out == NULL) || (s_mainArcadeRaceSetup.status != MAIN_ARCADE_RACE_SETUP_VALIDATED))
	{
		return 0;
	}
	*out = s_mainArcadeRaceSetup.setupFacts;
	return 1;
}

const struct NativeDeterministicRngBankV1 *MainArcadeRaceSetup_Bank(void)
{
	return (s_mainArcadeRaceSetup.status == MAIN_ARCADE_RACE_SETUP_VALIDATED) ? &s_mainArcadeRaceSetup.bank : NULL;
}

void MainArcadeRaceSetup_Disarm(void)
{
	struct MainArcadeRaceSetupState *state = &s_mainArcadeRaceSetup;
	struct GameTracker *gGT = sdata->gGT;

	if (state->fieldsWritten != 0u)
	{
		if ((gGT->levelID == MAIN_MENU_LEVEL) && (sdata->Loading.stage == LOAD_IDLE) && ((gGT->gameMode1 & LOADING) == 0))
		{
			gGT->gameMode1 = (int)(((uint32_t)gGT->gameMode1 & ~MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK) | state->savedVibration);
			Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "vibration bits 0x%X restored\n", (unsigned)state->savedVibration);
		}
		else
		{
			Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "vibration bits 0x%X not restored (not idle on the main-menu level)\n",
				(unsigned)state->savedVibration);
		}
	}
	memset(state, 0, sizeof(*state));
	memset(&s_mainArcadeRaceSetupScratch, 0, sizeof(s_mainArcadeRaceSetupScratch));
	MainArcadeRaceSetup_Enter(MAIN_ARCADE_RACE_SETUP_IDLE);
}

#endif
