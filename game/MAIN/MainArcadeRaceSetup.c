/*
 * Live race setup adapter (docs/ROSTER_MILESTONE.md section 3.2, tasks R-5b
 * and R-5c). See MAIN/MainArcadeRaceSetup.h for the state machine and
 * MAIN/MainArcadeRaceSetupCore.h for the decisions. Native only; dormant
 * unless MainArcadeRaceSetup_Arm is called.
 *
 * Thin by design: each entry point reads a pointer-free view of the live
 * values, calls the pure core step, applies exactly the writes the core
 * returns, in order (MainArcadeRaceSetup_Apply), and logs. It decides nothing.
 *
 * Unity-included after the MainCanonical* sources (it reads the live roster
 * input through MainCanonicalDrivers_ExtractRosterInputPreRace, and its live
 * snapshot follows the same gGT/sdata/data conventions) and before the
 * arcade-link sources, so every later native launcher (the internal roster
 * proof, Task 7's link) sees it. MainInit.c, earlier in the chain, calls the
 * two hooks through MAIN/MainArcadeRaceSetup.h.
 */
#if defined(CTR_NATIVE)

#include <common.h>

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRaceSetup.h"
#include "MAIN/MainArcadeRaceSetupCore.h"
#include "MAIN/MainArcadeRaceSetupFacts.h"
#include "MAIN/MainArcadeRaceSetupPlan.h"
#include "MAIN/MainCanonicalDrivers.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_log.h"
#include "platform/native_match_config.h"

/* The plan and the core mirror these retail values without including game
 * headers; keep the mirrors the adapter relies on honest. */
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL == (uint32_t)PAUSE_ALL, "MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL must match PAUSE_ALL");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE == (uint32_t)ARCADE_MODE, "MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE must match ARCADE_MODE");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_LOADING == (uint32_t)LOADING, "MAIN_ARCADE_RACE_SETUP_GM1_LOADING must match LOADING");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK == (uint32_t)GAME_MODE_VIBRATION_MASK, "MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK must match GAME_MODE_VIBRATION_MASK");
_Static_assert(MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL == (uint32_t)CHEAT_ALL, "MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL must match CHEAT_ALL");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE == LOAD_IDLE, "MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE must match LOAD_IDLE");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL == MAIN_MENU_LEVEL, "MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL must match MAIN_MENU_LEVEL");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT == sizeof(data.characterIDs) / sizeof(data.characterIDs[0]), "MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT must match data.characterIDs");
_Static_assert(MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT == sizeof(sdata->kartSpawnOrderArray), "the facts snapshot must hold every kartSpawnOrderArray entry");
_Static_assert(MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT == sizeof(sdata->driver_pathIndexIDs), "the facts snapshot must hold every driver_pathIndexIDs entry");
_Static_assert(MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT == sizeof(sdata->accelerateOrder), "the facts snapshot must hold every accelerateOrder entry");
_Static_assert(MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES == NATIVE_SHA256_DIGEST_BYTES, "MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES must match NATIVE_SHA256_DIGEST_BYTES");
_Static_assert(MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT == sizeof(sdata->NavPath_ptrHeader) / sizeof(sdata->NavPath_ptrHeader[0]), "MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT must match NavPath_ptrHeader");

#define MAIN_ARCADE_RACE_SETUP_LOG "[CTR Native] arcade race setup: "

/* The whole setup state: file-scope static, outside every saved-state
 * region, never recorded or canonical (RS-11). */
static struct MainArcadeRaceSetupCore s_mainArcadeRaceSetup;

/* The views, the core's workspace, and the step outcome: too large for
 * comfort on the game stack, so file-scope static too. Every entry point runs
 * on the game thread, one at a time. */
struct MainArcadeRaceSetupScratch
{
	struct MainArcadeRaceSetupCoreLaunchView launchView;
	struct MainArcadeRaceSetupCoreBeginView beginView;
	struct MainArcadeRaceSetupCoreDriversView driversView;
	struct MainArcadeRaceSetupCoreScratch core;
	struct MainArcadeRaceSetupCoreOutcome outcome;
};

static struct MainArcadeRaceSetupScratch s_mainArcadeRaceSetupScratch;

const char *MainArcadeRaceSetup_StatusName(enum MainArcadeRaceSetupStatus status)
{
	return MainArcadeRaceSetupCore_StatusName(status);
}

const char *MainArcadeRaceSetup_FailureName(enum MainArcadeRaceSetupFailure failure)
{
	return MainArcadeRaceSetupCore_FailureName(failure);
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

/* Performs exactly the core's writes, in order. The core has no levelID
 * target: the level reaches the retail state only through the load request. */
static void MainArcadeRaceSetup_Apply(struct GameTracker *gGT, const struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	for (uint32_t i = 0; (i < outcome->opCount) && (i < MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS); i++)
	{
		const struct MainArcadeRaceSetupCoreOp *op = &outcome->ops[i];

		switch (op->target)
		{
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1:
			gGT->gameMode1 = (int)(uint32_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE2:
			gGT->gameMode2 = (int)(uint32_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ARCADE_DIFFICULTY:
			gGT->arcadeDifficulty = (int)(int32_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_BOOL_DEMO_MODE:
			gGT->boolDemoMode = (char)(uint8_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_LAPS:
			gGT->numLaps = (s8)(int8_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_PLYR_NEXT_GAME:
			gGT->numPlyrNextGame = (u8)(uint8_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID:
			if (op->index < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT)
			{
				data.characterIDs[op->index] = (s16)(int16_t)op->value;
			}
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_REQUEST_LOAD:
			MainRaceTrack_RequestLoad((s16)(int32_t)op->value);
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RANDOM_NUMBER:
			sdata->randomNumber = (int)(uint32_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG0:
			sdata->advRng.state0 = (uint32_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG1:
			sdata->advRng.state1 = (uint32_t)op->value;
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_PSX_RAND_SEED:
			PSX_BIOS_SetRandSeed((uint32_t)op->value);
			break;
		case MAIN_ARCADE_RACE_SETUP_CORE_TARGET_AUDIO_RNG:
			sdata->audioRNG = (uint32_t)op->value;
			break;
		default:
			break;
		}
	}
}

/* One log line per state change or failure, as the core reports it. */
static void MainArcadeRaceSetup_Log(const struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	const char *status = MainArcadeRaceSetupCore_StatusName((enum MainArcadeRaceSetupStatus)outcome->status);
	const char *failure = MainArcadeRaceSetupCore_FailureName((enum MainArcadeRaceSetupFailure)outcome->failure);

	switch (outcome->log)
	{
	case MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED:
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "%s\n", status);
		break;
	case MAIN_ARCADE_RACE_SETUP_CORE_LOG_FAILED:
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "FAILED %s (%s)\n", failure, outcome->detail);
		break;
	case MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED:
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "%s refused in state %s\n", outcome->detail, status);
		break;
	case MAIN_ARCADE_RACE_SETUP_CORE_LOG_ARM_REFUSED:
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "Arm refused: %s (%s); still IDLE\n", failure, outcome->detail);
		break;
	default:
		break;
	}
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

int MainArcadeRaceSetup_Arm(const struct NativeMatchConfigV1 *config)
{
	struct MainArcadeRaceSetupCoreOutcome *outcome = &s_mainArcadeRaceSetupScratch.outcome;
	const struct MainArcadeRaceSetupPlan *plan = &s_mainArcadeRaceSetup.plan;
	const int armed = MainArcadeRaceSetupCore_Arm(&s_mainArcadeRaceSetup, config, (uint32_t)sdata->gGT->gameMode1, outcome);

	if (armed)
	{
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "armed track %d laps %d seed 0x%08X%08X difficulty 0x%X characters %d %d %d %d %d %d\n",
			(int)plan->levelID, (int)plan->numLaps, (unsigned)(uint32_t)(plan->masterSeed >> 32),
			(unsigned)(uint32_t)(plan->masterSeed & 0xFFFFFFFFu), (unsigned)plan->arcadeDifficulty,
			(int)plan->characterIDs[0], (int)plan->characterIDs[1], (int)plan->characterIDs[2],
			(int)plan->characterIDs[3], (int)plan->characterIDs[4], (int)plan->characterIDs[5]);
	}
	MainArcadeRaceSetup_Log(outcome);
	return armed;
}

int MainArcadeRaceSetup_Launch(void)
{
	struct MainArcadeRaceSetupScratch *scratch = &s_mainArcadeRaceSetupScratch;
	struct MainArcadeRaceSetupCoreLaunchView *view = &scratch->launchView;
	struct GameTracker *gGT = sdata->gGT;
	int launched;

	memset(view, 0, sizeof(*view));
	view->loadingStage = (int32_t)sdata->Loading.stage;
	view->onBeginAddBits0 = (uint32_t)sdata->Loading.OnBegin.AddBitsConfig0;
	view->onBeginRemBits0 = (uint32_t)sdata->Loading.OnBegin.RemBitsConfig0;
	view->onBeginAddBits8 = (uint32_t)sdata->Loading.OnBegin.AddBitsConfig8;
	view->onBeginRemBits8 = (uint32_t)sdata->Loading.OnBegin.RemBitsConfig8;
	view->optionsLoaded = (uint32_t)sdata->boolHasLoadedOptions;
	MainArcadeRaceSetup_ReadFields(gGT, &view->fields);
	launched = MainArcadeRaceSetupCore_Launch(&s_mainArcadeRaceSetup, view, &scratch->outcome);
	MainArcadeRaceSetup_Apply(gGT, &scratch->outcome);
	MainArcadeRaceSetup_Log(&scratch->outcome);
	return launched;
}

void MainArcadeRaceSetup_OnFinalizeInitBegin(struct GameTracker *gGT)
{
	struct MainArcadeRaceSetupScratch *scratch = &s_mainArcadeRaceSetupScratch;
	struct MainArcadeRaceSetupCoreBeginView *view = &scratch->beginView;
	const struct MainArcadeRaceSetupCoreOutcome *outcome = &scratch->outcome;

	memset(view, 0, sizeof(*view));
	view->trackerPresent = (gGT != NULL) ? 1u : 0u;
	if ((gGT != NULL) && MainArcadeRaceSetupCore_HookReadsView(&s_mainArcadeRaceSetup, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN))
	{
		view->numPlyrCurrGame = (uint8_t)gGT->numPlyrCurrGame;
		MainArcadeRaceSetup_ReadFields(gGT, &view->fields);
	}
	MainArcadeRaceSetupCore_OnFinalizeInitBegin(&s_mainArcadeRaceSetup, view, &scratch->core, &scratch->outcome);
	if (outcome->opCount != 0u)
	{
		MainArcadeRaceSetup_Apply(gGT, outcome);
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "seeded randomNumber 0x%04X advRng 0x%08X 0x%08X psxRand 0x%08X audioRNG 0x%08X\n",
			(unsigned)outcome->seeds.randomNumber, (unsigned)outcome->seeds.advRng0, (unsigned)outcome->seeds.advRng1,
			(unsigned)outcome->seeds.psxRandSeed, (unsigned)outcome->seeds.audioRNG);
	}
	MainArcadeRaceSetup_Log(outcome);
}

void MainArcadeRaceSetup_OnDriversInitialized(struct GameTracker *gGT)
{
	struct MainArcadeRaceSetupScratch *scratch = &s_mainArcadeRaceSetupScratch;
	struct MainArcadeRaceSetupCoreDriversView *view = &scratch->driversView;
	const struct MainArcadeRaceSetupCoreOutcome *outcome = &scratch->outcome;

	memset(view, 0, sizeof(*view));
	view->trackerPresent = (gGT != NULL) ? 1u : 0u;
	if ((gGT != NULL) && MainArcadeRaceSetupCore_HookReadsView(&s_mainArcadeRaceSetup, MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED))
	{
		view->gameMode1 = (uint32_t)gGT->gameMode1;
		view->gameMode2 = (uint32_t)gGT->gameMode2;
		MainArcadeRaceSetup_ReadSnapshot(gGT, &view->snapshot);
		view->rosterInputValid = MainCanonicalDrivers_ExtractRosterInputPreRace(gGT, sdata, &view->rosterInput) ? 1u : 0u;
	}
	MainArcadeRaceSetupCore_OnDriversInitialized(&s_mainArcadeRaceSetup, view, &scratch->core, &scratch->outcome);
	if (outcome->failure == (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILURE_BOT_SETUP && outcome->log == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_FAILED)
	{
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "bot setup result %d\n", (int)outcome->botSetupResult);
		if (outcome->botSetupResult == (int32_t)MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH)
		{
			MainArcadeRaceSetup_LogNavHeaders();
		}
	}
	MainArcadeRaceSetup_Log(outcome);
}

enum MainArcadeRaceSetupStatus MainArcadeRaceSetup_Status(void)
{
	return MainArcadeRaceSetupCore_Status(&s_mainArcadeRaceSetup);
}

enum MainArcadeRaceSetupFailure MainArcadeRaceSetup_Failure(void)
{
	return MainArcadeRaceSetupCore_Failure(&s_mainArcadeRaceSetup);
}

int MainArcadeRaceSetup_Digests(uint8_t configDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t racePlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t botSetupPlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t bankDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES])
{
	return MainArcadeRaceSetupCore_Digests(&s_mainArcadeRaceSetup, configDigest, racePlanDigest, botSetupPlanDigest, bankDigest);
}

int MainArcadeRaceSetup_SlotFacts(struct MainArcadeBotSetupSourceFacts *out)
{
	return MainArcadeRaceSetupCore_SlotFacts(&s_mainArcadeRaceSetup, out);
}

const struct NativeDeterministicRngBankV1 *MainArcadeRaceSetup_Bank(void)
{
	return MainArcadeRaceSetupCore_Bank(&s_mainArcadeRaceSetup);
}

void MainArcadeRaceSetup_Disarm(void)
{
	struct MainArcadeRaceSetupScratch *scratch = &s_mainArcadeRaceSetupScratch;
	struct MainArcadeRaceSetupCoreDisarmView view;
	struct GameTracker *gGT = sdata->gGT;

	view.currentLevel = (int32_t)gGT->levelID;
	view.loadingStage = (int32_t)sdata->Loading.stage;
	view.gameMode1 = (uint32_t)gGT->gameMode1;
	MainArcadeRaceSetupCore_Disarm(&s_mainArcadeRaceSetup, &view, &scratch->outcome);
	MainArcadeRaceSetup_Apply(gGT, &scratch->outcome);
	if (scratch->outcome.vibration == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_RESTORED)
	{
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "vibration bits 0x%X restored\n", (unsigned)scratch->outcome.savedVibration);
	}
	else if (scratch->outcome.vibration == (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED)
	{
		Platform_Log(MAIN_ARCADE_RACE_SETUP_LOG "vibration bits 0x%X not restored (not idle on the main-menu level)\n",
			(unsigned)scratch->outcome.savedVibration);
	}
	MainArcadeRaceSetup_Log(&scratch->outcome);
	memset(&scratch->launchView, 0, sizeof(scratch->launchView));
	memset(&scratch->beginView, 0, sizeof(scratch->beginView));
	memset(&scratch->driversView, 0, sizeof(scratch->driversView));
	memset(&scratch->core, 0, sizeof(scratch->core));
}

#endif
