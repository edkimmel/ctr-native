#include "MAIN/MainArcadeRaceSetupCore.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Race setup decision core (docs/ROSTER_MILESTONE.md section 3.2, R-5c). See
 * MAIN/MainArcadeRaceSetupCore.h for the rules and MAIN/MainArcadeRaceSetupPlan.h
 * for the R-5 contract they implement.
 */

_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_LAUNCH_OP_COUNT <= MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS,
	"every Launch op must fit the outcome's write list");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_BEGIN_OP_COUNT <= MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS,
	"every pre-drivers op must fit the outcome's write list");
_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS >= 1u, "the Arm op and the Disarm op must each fit the outcome's write list");
/* Arm's BANK failure is defence in depth: the plan accepts only configs with
 * the match config's derivation version, and the bank derives exactly that
 * version, so a config the plan accepts always derives. */
_Static_assert(NATIVE_MATCH_CONFIG_V1_RNG_DERIVATION_VERSION == NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION,
	"every config the plan accepts must derive a bank");

void MainArcadeRaceSetupCore_Reset(struct MainArcadeRaceSetupCore *core)
{
	if (core != NULL)
	{
		memset(core, 0, sizeof(*core));
	}
}

static void MainArcadeRaceSetupCore_Begin(const struct MainArcadeRaceSetupCore *core,
	struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	memset(outcome, 0, sizeof(*outcome));
	outcome->detail = "";
	outcome->botSetupResult = (int32_t)MAIN_ARCADE_BOT_SETUP_OK;
	outcome->status = core->status;
	outcome->failure = core->failure;
}

static void MainArcadeRaceSetupCore_Enter(struct MainArcadeRaceSetupCore *core, enum MainArcadeRaceSetupStatus status,
	struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	core->status = (uint32_t)status;
	outcome->status = core->status;
	outcome->failure = core->failure;
	outcome->log = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ENTERED;
}

/* Latches FAILED with the failure code and drops any op already staged. */
static void MainArcadeRaceSetupCore_Fail(struct MainArcadeRaceSetupCore *core, enum MainArcadeRaceSetupFailure failure,
	const char *detail, struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	core->status = (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILED;
	core->failure = (uint32_t)failure;
	outcome->opCount = 0u;
	outcome->result = 0u;
	outcome->status = core->status;
	outcome->failure = core->failure;
	outcome->log = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_FAILED;
	outcome->detail = detail;
}

/* A call in the wrong state: fail closed while a setup is in flight. */
static void MainArcadeRaceSetupCore_WrongState(struct MainArcadeRaceSetupCore *core, const char *call,
	struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	if ((core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_IDLE) || (core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILED))
	{
		outcome->log = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_REFUSED;
		outcome->detail = call;
		return;
	}
	MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE, call, outcome);
}

/* Fails closed: a push that finds the list full drops the whole list and
 * marks the outcome overflowed, so the step latches OPS and nothing, not a
 * truncated list, is ever applied. The static asserts above prove it cannot
 * happen with the current steps. */
static void MainArcadeRaceSetupCore_Push(struct MainArcadeRaceSetupCoreOutcome *outcome,
	enum MainArcadeRaceSetupCoreTarget target, uint8_t index, int64_t value)
{
	struct MainArcadeRaceSetupCoreOp *op;

	if ((outcome->overflowed != 0u) || (outcome->opCount >= MAIN_ARCADE_RACE_SETUP_CORE_MAX_OPS))
	{
		outcome->overflowed = 1u;
		outcome->opCount = 0u;
		return;
	}
	op = &outcome->ops[outcome->opCount++];
	memset(op, 0, sizeof(*op));
	op->target = (uint8_t)target;
	op->index = index;
	op->value = value;
}

/* The mode words, arcadeDifficulty, and boolDemoMode, in this order. */
static void MainArcadeRaceSetupCore_PushModeFields(struct MainArcadeRaceSetupCoreOutcome *outcome,
	const struct MainArcadeRaceSetupRetailFields *fields)
{
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1, 0u, (int64_t)fields->gameMode1);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE2, 0u, (int64_t)fields->gameMode2);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ARCADE_DIFFICULTY, 0u,
		(int64_t)fields->arcadeDifficulty);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_BOOL_DEMO_MODE, 0u, (int64_t)fields->boolDemoMode);
}

int MainArcadeRaceSetupCore_Arm(struct MainArcadeRaceSetupCore *core, const struct NativeMatchConfigV1 *config,
	uint32_t liveGameMode1, uint32_t optionsLoaded, struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	if ((core == NULL) || (outcome == NULL))
	{
		return 0;
	}
	MainArcadeRaceSetupCore_Begin(core, outcome);
	if (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_IDLE)
	{
		MainArcadeRaceSetupCore_WrongState(core, "Arm", outcome);
		return 0;
	}
	memset(core, 0, sizeof(*core));
	if ((config == NULL) || !MainArcadeRaceSetupPlan_Build(config, &core->plan) ||
	    !MainArcadeRaceSetupPlan_Digest(&core->plan, core->racePlanDigest))
	{
		memset(core, 0, sizeof(*core));
		core->failure = (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN;
		outcome->status = core->status;
		outcome->failure = core->failure;
		outcome->log = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ARM_REFUSED;
		outcome->detail = "the plan refused the config";
		return 0;
	}
	if (!NativeDeterministicRngBankV1_Init(&core->bank, config->masterSeed, config->rngDerivationVersion))
	{
		memset(core, 0, sizeof(*core));
		core->failure = (uint32_t)MAIN_ARCADE_RACE_SETUP_FAILURE_BANK;
		outcome->status = core->status;
		outcome->failure = core->failure;
		outcome->log = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_LOG_ARM_REFUSED;
		outcome->detail = "the bank could not be derived";
		return 0;
	}
	core->config = *config;
	memcpy(core->configDigest, core->plan.configDigest, sizeof(core->configDigest));
	core->savedVibration = liveGameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK;
	/* A fresh cabinet with no memcard save never loaded its options: retail
	 * sets boolHasLoadedOptions only in RaceConfig_LoadGameOptions, after a
	 * memcard load (RefreshCard.c), so Launch's options precondition would
	 * fail on every race. The retail load is deliberately not run: with no
	 * save, sdata->gameOptions is all zeros (the new-profile memset, GAMEPROG.c)
	 * while the real defaults live in HOWL (howl_InitGlobals), so it would
	 * mute every volume, force mono, and zero data.rwd. Only the flag is
	 * marked (OPTIONS_LOADED): the live volumes, stereo mode, data.rwd, and
	 * vibration bits stay as they are, and any later retail options load
	 * becomes its retail no-op, the state Launch requires. With a save loaded
	 * the flag is already set and nothing is written. */
	if (optionsLoaded == 0u)
	{
		MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_OPTIONS_LOADED, 0u, 1);
		outcome->optionsMarked = 1u;
	}
	MainArcadeRaceSetupCore_Enter(core, MAIN_ARCADE_RACE_SETUP_ARMED, outcome);
	outcome->result = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_Launch(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreLaunchView *view, struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	struct MainArcadeRaceSetupRetailFields fields;

	if ((core == NULL) || (view == NULL) || (outcome == NULL))
	{
		return 0;
	}
	MainArcadeRaceSetupCore_Begin(core, outcome);
	if (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_ARMED)
	{
		MainArcadeRaceSetupCore_WrongState(core, "Launch", outcome);
		return 0;
	}
	if (view->loadingStage != MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "a load is in progress", outcome);
		return 0;
	}
	if ((view->onBeginAddBits0 != 0u) || (view->onBeginRemBits0 != 0u) || (view->onBeginAddBits8 != 0u) ||
	    (view->onBeginRemBits8 != 0u))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "pending OnBegin mode bits", outcome);
		return 0;
	}
	if (view->optionsLoaded == 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "the game options are not loaded yet",
			outcome);
		return 0;
	}
	if ((view->fields.gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL) != 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_PRECONDITION, "the game is paused", outcome);
		return 0;
	}
	if (!MainArcadeRaceSetupPlan_Apply(&core->plan, &view->fields, &fields))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN, "the plan could not be applied", outcome);
		return 0;
	}

	/* Every owned field except levelID: the load request carries it, and
	 * LOAD_LevelFile writes it (never while the old level runs). */
	MainArcadeRaceSetupCore_PushModeFields(outcome, &fields);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_LAPS, 0u, (int64_t)fields.numLaps);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NUM_PLYR_NEXT_GAME, 0u,
		(int64_t)fields.numPlyrNextGame);
	for (uint8_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CHARACTER_ID, slot,
			(int64_t)fields.characterIDs[slot]);
	}
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_REQUEST_LOAD, 0u, (int64_t)core->plan.levelID);
	if (outcome->overflowed != 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_OPS, "the Launch write list overflowed", outcome);
		return 0;
	}
	core->fieldsWritten = 1u;
	MainArcadeRaceSetupCore_Enter(core, MAIN_ARCADE_RACE_SETUP_LAUNCHED, outcome);
	outcome->result = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_HookReadsView(const struct MainArcadeRaceSetupCore *core, enum MainArcadeRaceSetupCoreHook hook)
{
	if (core == NULL)
	{
		return 0;
	}
	if (hook == MAIN_ARCADE_RACE_SETUP_CORE_HOOK_FINALIZE_INIT_BEGIN)
	{
		/* VALIDATED too (RL-9): the level being initialized decides whether
		 * the hook is the no-op of a return to the main-menu level. */
		return (core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_LAUNCHED) || (core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED);
	}
	if (hook == MAIN_ARCADE_RACE_SETUP_CORE_HOOK_DRIVERS_INITIALIZED)
	{
		return core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED;
	}
	return 0;
}

int MainArcadeRaceSetupCore_OnFinalizeInitBegin(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreBeginView *view, struct MainArcadeRaceSetupCoreScratch *scratch,
	struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	struct MainArcadeRaceSetupRetailFields fields;
	struct NativeArcadeRetailRngSeedsV1 seeds;
	struct MainArcadeRaceSetupPins pins;

	if ((core == NULL) || (view == NULL) || (scratch == NULL) || (outcome == NULL))
	{
		return 0;
	}
	MainArcadeRaceSetupCore_Begin(core, outcome);
	/* RL-9: the return load to the main-menu level after a validated race
	 * initializes that level while the owner still holds VALIDATED (its
	 * Disarm waits for the first idle main-menu frame). That init is not a
	 * new race init over the setup: a no-op. VALIDATED on any other level,
	 * or without a tracker, is still one (STATE, below). */
	if ((core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED) && (view->trackerPresent != 0u) &&
	    (view->fields.levelID == MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL))
	{
		return 0;
	}
	if ((core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED) || (core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE,
			"a new race init over an unfinished or validated setup", outcome);
		return 0;
	}
	if (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_LAUNCHED)
	{
		return 0;
	}
	if (view->trackerPresent == 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER, "race init without a game tracker", outcome);
		return 0;
	}

	/* 1. Verify the fields the load consumed. */
	if (view->fields.levelID != core->plan.levelID)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_LEVEL_MISMATCH, "the loaded level is not the plan's",
			outcome);
		return 0;
	}
	if ((view->fields.numLaps != core->plan.numLaps) || (view->numPlyrCurrGame != core->plan.numPlyrNextGame))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH, "numLaps or numPlyrCurrGame",
			outcome);
		return 0;
	}
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; slot++)
	{
		/* The owned slots: the humans, and the bots as the load wrote them,
		 * which the plan holds exactly. ARCADE_TWO_CAB: characterIDs[2..5]
		 * by LOAD_Robots2P (RS-4), slots 6 and 7 not owned. ARCADE_ONE_CAB:
		 * characterIDs[1..7] by LOAD_Robots1P (RS-20), every slot owned. */
		if (((((uint32_t)core->plan.characterWriteMask >> slot) & 1u) != 0u) &&
		    (view->fields.characterIDs[slot] != core->plan.characterIDs[slot]))
		{
			MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH, "characterIDs", outcome);
			return 0;
		}
	}

	/* 2. Re-apply only the mode words, arcadeDifficulty, and boolDemoMode. */
	if (!MainArcadeRaceSetupPlan_Apply(&core->plan, &view->fields, &fields))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_PLAN, "the plan could not be re-applied", outcome);
		return 0;
	}

	/* 3. Seed the retail RNG states (RS-7), transactionally on a copy. */
	scratch->seedBank = core->bank;
	if (!NativeArcadeBotRules_DeriveRetailSeedsV1(&scratch->seedBank, &seeds))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_SEED, "the retail seeds could not be derived", outcome);
		return 0;
	}
	/* 4. The ops: the re-applied mode fields, the pinned boot-relative
	 * counters (RS-17) and root-counter phase (LR-8), then the seeds. */
	pins.timer = (int32_t)MAIN_ARCADE_RACE_SETUP_CORE_PIN_TIMER;
	pins.frameTimerConfetti = (int32_t)MAIN_ARCADE_RACE_SETUP_CORE_PIN_FRAME_TIMER_CONFETTI;
	pins.rcntTotalUnits = (int32_t)MAIN_ARCADE_RACE_SETUP_CORE_PIN_RCNT_TOTAL_UNITS;
	pins.clockFrameStart = (int32_t)MAIN_ARCADE_RACE_SETUP_CORE_PIN_CLOCK_FRAME_START;
	MainArcadeRaceSetupCore_PushModeFields(outcome, &fields);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_TIMER, 0u, (int64_t)pins.timer);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_FRAME_TIMER_CONFETTI, 0u,
		(int64_t)pins.frameTimerConfetti);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RCNT_TOTAL_UNITS, 0u, (int64_t)pins.rcntTotalUnits);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_CLOCK_FRAME_START, 0u, (int64_t)pins.clockFrameStart);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RANDOM_NUMBER, 0u, (int64_t)seeds.randomNumber);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG0, 0u, (int64_t)seeds.advRng0);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_ADV_RNG1, 0u, (int64_t)seeds.advRng1);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_PSX_RAND_SEED, 0u, (int64_t)seeds.psxRandSeed);
	MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_AUDIO_RNG, 0u, (int64_t)seeds.audioRNG);
	if (outcome->overflowed != 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_OPS, "the seeding write list overflowed", outcome);
		return 0;
	}
	core->bank = scratch->seedBank;
	core->seeds = seeds;
	core->pins = pins;
	outcome->seeds = seeds;
	outcome->pins = pins;
	MainArcadeRaceSetupCore_Enter(core, MAIN_ARCADE_RACE_SETUP_SEEDED, outcome);
	outcome->result = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_OnDriversInitialized(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreDriversView *view, struct MainArcadeRaceSetupCoreScratch *scratch,
	struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	enum MainArcadeBotSetupResult result;
	const uint32_t expectedGameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK;
	const uint32_t ownedGameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK | MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK;
	const uint32_t ownedGameMode2 = MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK | MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK;

	if ((core == NULL) || (view == NULL) || (scratch == NULL) || (outcome == NULL))
	{
		return 0;
	}
	MainArcadeRaceSetupCore_Begin(core, outcome);
	if (core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_LAUNCHED)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_STATE, "drivers initialized before the seeding hook ran",
			outcome);
		return 0;
	}
	if (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED)
	{
		return 0;
	}
	if (view->trackerPresent == 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER, "drivers initialized without a game tracker",
			outcome);
		return 0;
	}

	/* The plan's mode bits still hold, and no cheat bit came back. */
	if (((view->gameMode2 & MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL) != 0u) ||
	    ((view->gameMode1 & ownedGameMode1) != expectedGameMode1) ||
	    ((view->gameMode2 & ownedGameMode2) != MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS, "the plan's mode bits no longer hold", outcome);
		return 0;
	}

	memset(scratch, 0, sizeof(*scratch));
	if (view->rosterInputValid == 0u)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS, "the live roster input could not be extracted",
			outcome);
		return 0;
	}
	if (!MainArcadeRaceSetupFacts_Build(&core->config, &view->snapshot, &view->rosterInput, &scratch->rosterFacts,
	        &scratch->setupFacts))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_FACTS, "the facts could not be built from the live race",
			outcome);
		return 0;
	}
	if (!MainArcadeRoster_BuildPlan(&core->config, &scratch->rosterPlan) ||
	    !MainArcadeRoster_ValidateNativeFacts(&scratch->rosterPlan, &core->config, &scratch->rosterFacts, &scratch->validated))
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_ROSTER, "the live roster does not match the config",
			outcome);
		return 0;
	}
	result = MainArcadeBotSetup_Plan(&core->config, &scratch->rosterPlan, &scratch->validated, &scratch->setupFacts, &core->bank,
		&scratch->botSetupPlan, &scratch->bankAfter);
	if (result != MAIN_ARCADE_BOT_SETUP_OK)
	{
		MainArcadeRaceSetupCore_Fail(core, MAIN_ARCADE_RACE_SETUP_FAILURE_BOT_SETUP, "the bot setup refused the live facts",
			outcome);
		outcome->botSetupResult = (int32_t)result;
		return 0;
	}

	core->botSetupPlan = scratch->botSetupPlan;
	core->setupFacts = scratch->setupFacts;
	core->bank = scratch->bankAfter;
	MainArcadeRaceSetupCore_Enter(core, MAIN_ARCADE_RACE_SETUP_VALIDATED, outcome);
	outcome->result = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_Disarm(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupCoreDisarmView *view, struct MainArcadeRaceSetupCoreOutcome *outcome)
{
	if ((core == NULL) || (view == NULL) || (outcome == NULL))
	{
		return 0;
	}
	MainArcadeRaceSetupCore_Begin(core, outcome);
	outcome->savedVibration = core->savedVibration;
	if (core->fieldsWritten != 0u)
	{
		if ((view->currentLevel == MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL) &&
		    (view->loadingStage == MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE) &&
		    ((view->gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_LOADING) == 0u))
		{
			MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_GAME_MODE1, 0u,
				(int64_t)((view->gameMode1 & ~MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK) | core->savedVibration));
			outcome->vibration = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_RESTORED;
		}
		else
		{
			outcome->vibration = (uint8_t)MAIN_ARCADE_RACE_SETUP_CORE_VIBRATION_NOT_RESTORED;
		}
	}
	memset(core, 0, sizeof(*core));
	MainArcadeRaceSetupCore_Enter(core, MAIN_ARCADE_RACE_SETUP_IDLE, outcome);
	outcome->result = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_RecordSeedReadback(struct MainArcadeRaceSetupCore *core,
	const struct NativeArcadeRetailRngSeedsV1 *readback)
{
	if ((core == NULL) || (readback == NULL) || (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED) ||
	    (core->seedReadbackRecorded != 0u))
	{
		return 0;
	}
	core->seedReadback = *readback;
	core->seedReadbackRecorded = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_SeedReadback(const struct MainArcadeRaceSetupCore *core,
	struct NativeArcadeRetailRngSeedsV1 *produced, struct NativeArcadeRetailRngSeedsV1 *readback)
{
	if ((core == NULL) || (produced == NULL) || (readback == NULL) || (core->seedReadbackRecorded == 0u) ||
	    ((core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED) && (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED)))
	{
		return 0;
	}
	*produced = core->seeds;
	*readback = core->seedReadback;
	return 1;
}

int MainArcadeRaceSetupCore_RecordPinReadback(struct MainArcadeRaceSetupCore *core,
	const struct MainArcadeRaceSetupPins *readback)
{
	if ((core == NULL) || (readback == NULL) || (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED) ||
	    (core->pinReadbackRecorded != 0u))
	{
		return 0;
	}
	core->pinReadback = *readback;
	core->pinReadbackRecorded = 1u;
	return 1;
}

int MainArcadeRaceSetupCore_PinReadback(const struct MainArcadeRaceSetupCore *core,
	struct MainArcadeRaceSetupPins *produced, struct MainArcadeRaceSetupPins *readback)
{
	if ((core == NULL) || (produced == NULL) || (readback == NULL) || (core->pinReadbackRecorded == 0u) ||
	    ((core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_SEEDED) && (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED)))
	{
		return 0;
	}
	*produced = core->pins;
	*readback = core->pinReadback;
	return 1;
}

enum MainArcadeRaceSetupStatus MainArcadeRaceSetupCore_Status(const struct MainArcadeRaceSetupCore *core)
{
	return (core != NULL) ? (enum MainArcadeRaceSetupStatus)core->status : MAIN_ARCADE_RACE_SETUP_IDLE;
}

enum MainArcadeRaceSetupFailure MainArcadeRaceSetupCore_Failure(const struct MainArcadeRaceSetupCore *core)
{
	return (core != NULL) ? (enum MainArcadeRaceSetupFailure)core->failure : MAIN_ARCADE_RACE_SETUP_FAILURE_NONE;
}

int MainArcadeRaceSetupCore_Digests(const struct MainArcadeRaceSetupCore *core,
	uint8_t configDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES], uint8_t racePlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t botSetupPlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES], uint8_t bankDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES])
{
	uint8_t botSetup[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t bank[NATIVE_SHA256_DIGEST_BYTES];

	if ((core == NULL) || (configDigest == NULL) || (racePlanDigest == NULL) || (botSetupPlanDigest == NULL) ||
	    (bankDigest == NULL) || (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED) ||
	    !MainArcadeBotSetupPlan_Digest(&core->botSetupPlan, botSetup) || !NativeDeterministicRngBankV1_Digest(&core->bank, bank))
	{
		return 0;
	}
	memcpy(configDigest, core->configDigest, NATIVE_SHA256_DIGEST_BYTES);
	memcpy(racePlanDigest, core->racePlanDigest, NATIVE_SHA256_DIGEST_BYTES);
	memcpy(botSetupPlanDigest, botSetup, NATIVE_SHA256_DIGEST_BYTES);
	memcpy(bankDigest, bank, NATIVE_SHA256_DIGEST_BYTES);
	return 1;
}

int MainArcadeRaceSetupCore_SlotFacts(const struct MainArcadeRaceSetupCore *core, struct MainArcadeBotSetupSourceFacts *out)
{
	if ((core == NULL) || (out == NULL) || (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED))
	{
		return 0;
	}
	*out = core->setupFacts;
	return 1;
}

const struct NativeDeterministicRngBankV1 *MainArcadeRaceSetupCore_Bank(const struct MainArcadeRaceSetupCore *core)
{
	return ((core != NULL) && (core->status == (uint32_t)MAIN_ARCADE_RACE_SETUP_VALIDATED)) ? &core->bank : NULL;
}

const char *MainArcadeRaceSetupCore_StatusName(enum MainArcadeRaceSetupStatus status)
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

const char *MainArcadeRaceSetupCore_FailureName(enum MainArcadeRaceSetupFailure failure)
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
	case MAIN_ARCADE_RACE_SETUP_FAILURE_NO_TRACKER:
		return "NO_TRACKER";
	case MAIN_ARCADE_RACE_SETUP_FAILURE_OPS:
		return "OPS";
	default:
		return "UNKNOWN";
	}
}
