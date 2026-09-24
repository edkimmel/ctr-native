#ifndef MAIN_ARCADE_RACE_SETUP_H
#define MAIN_ARCADE_RACE_SETUP_H

#include <stdint.h>

#include "MAIN/MainArcadeRaceSetupCore.h"

/*
 * Live race setup adapter (docs/ROSTER_MILESTONE.md section 3.2, tasks R-5b
 * and R-5c). Native only: game/MAIN/MainArcadeRaceSetup.c is compiled only
 * with CTR_NATIVE and is dormant unless MainArcadeRaceSetup_Arm is called. It
 * turns a validated NativeMatchConfigV1 of either supported profile into a
 * retail arcade race: ARCADE_TWO_CAB into the retail 2P arcade race (two
 * humans, six drivers), ARCADE_ONE_CAB into the retail 1P arcade race (one
 * human, seven bots, eight drivers). The per-profile shape (RS-21, RS-22) is
 * the pure plan's and the core's; the adapter holds no profile logic. It works
 * through the pure plan (MAIN/MainArcadeRaceSetupPlan.h, whose R-5 contract
 * comment this module implements), seeds the retail RNGs from the match's
 * deterministic bank, and validates the live roster and bot setup facts
 * (MAIN/MainArcadeRaceSetupFacts.h, MAIN/MainArcadeRoster.h,
 * MAIN/MainArcadeBotSetup.h) right after MainInit_Drivers.
 *
 * The adapter is thin: every decision is made by the pure core
 * (MAIN/MainArcadeRaceSetupCore.h, which also defines the status and failure
 * codes and documents the state machine, the wrong-state rules, and the exact
 * writes of each step). Each entry point reads a pointer-free view of the live
 * values, calls the core step, applies exactly the writes the core returns, in
 * order, and logs.
 *
 * State machine:
 *
 *   IDLE --Arm--> ARMED --Launch--> LAUNCHED --OnFinalizeInitBegin--> SEEDED
 *        --OnDriversInitialized--> VALIDATED
 *
 * and FAILED (fail closed, with a failure code) from any step after Arm.
 * Disarm returns every state to IDLE.
 *
 * - Arm (IDLE only) builds the plan and derives the bank from the config's
 *   masterSeed. On failure it stays IDLE, records PLAN or BANK as the
 *   failure code, and returns 0.
 * - Launch (ARMED only) checks the preconditions (no load in progress, no
 *   pending OnBegin mode bits, options loaded, no pause bit), applies the
 *   plan to the live fields except levelID, and requests the load of the
 *   plan's level. It may run from inside the attract demo race, which then
 *   ticks on for a few frames with the written fields until the load starts;
 *   MAIN/MainArcadeRaceSetupCore.h ("Launch from inside the attract demo
 *   race") says why that is safe and how it squares with the Disarm rule
 *   below.
 * - OnFinalizeInitBegin (the very start of MainInit_FinalizeInit) acts only
 *   in LAUNCHED: it verifies the fields the load consumed, re-applies the
 *   mode words, arcadeDifficulty, and boolDemoMode, pins the boot-relative
 *   counters that feed the race (gGT->timer and gGT->frameTimer_Confetti,
 *   RS-17), and seeds the retail RNG states from the bank's MATCH_SETUP
 *   stream, then reads the five seeded fields and the two pinned counters
 *   back for MainArcadeRaceSetup_SeedReadback and _PinReadback.
 * - OnDriversInitialized (right after MainInit_Drivers) acts only in SEEDED:
 *   it builds the facts from the live race, reading the roster input with
 *   MainCanonicalDrivers_ExtractRosterInputPreRace (the race order is not
 *   rebuilt before the first race tick), and validates them against the
 *   roster plan and the bot setup, drawing the per-bot setup values from the
 *   post-seed bank.
 *
 * A call or hook in the wrong state (STATE): Arm outside IDLE and Launch
 * outside ARMED return 0; while a setup is in flight (ARMED, LAUNCHED,
 * SEEDED, VALIDATED) the misuse also latches FAILED/STATE, since a second
 * owner can no longer be trusted, and from IDLE or FAILED nothing changes.
 * OnFinalizeInitBegin in SEEDED on any level (the drivers hook of the previous
 * race init never ran), or in VALIDATED on any level but the main-menu level
 * or with a NULL game tracker (a new race init over a validated setup, which
 * the owner must Disarm first), latches FAILED/STATE. OnFinalizeInitBegin in
 * VALIDATED on the main-menu level is a no-op (docs/RACE_LAUNCH_MILESTONE.md
 * RL-9): it is the return load after the race, and the race caller Disarms
 * only on the first idle main-menu frame after it; so in VALIDATED the hook
 * reads the level being initialized. OnDriversInitialized in LAUNCHED (the
 * seeding hook was skipped) latches FAILED/STATE. A NULL game tracker in a
 * hook that should act (LAUNCHED, SEEDED) latches FAILED/NO_TRACKER. Every
 * other hook call is a no-op, so normal boot and every load not launched here
 * are unchanged.
 *
 * The state and the scratch are file-scope static in the module: never in a
 * saved state, a recording, or canonical state (RS-11). Every state change and
 * failure logs one Platform_Log line with the prefix "arcade race setup:".
 */

struct GameTracker;

/* IDLE only. Returns 1 and moves to ARMED; 0 otherwise (see above). */
int MainArcadeRaceSetup_Arm(const struct NativeMatchConfigV1 *config);

/* ARMED only. Returns 1 and moves to LAUNCHED; 0 otherwise, FAILED on a
 * failed precondition. Never clears pause itself. */
int MainArcadeRaceSetup_Launch(void);

/* Hook at the very start of MainInit_FinalizeInit. Reads the live fields
 * only when MainArcadeRaceSetupCore_HookReadsView says so (LAUNCHED, and
 * VALIDATED for the RL-9 main-menu check). */
void MainArcadeRaceSetup_OnFinalizeInitBegin(struct GameTracker *gGT);

/* Hook immediately after MainInit_Drivers in MainInit_FinalizeInit. */
void MainArcadeRaceSetup_OnDriversInitialized(struct GameTracker *gGT);

enum MainArcadeRaceSetupStatus MainArcadeRaceSetup_Status(void);

/* The latched failure code; NONE unless FAILED, or IDLE after a failed Arm. */
enum MainArcadeRaceSetupFailure MainArcadeRaceSetup_Failure(void);

/* Fixed names for logs and reports ("IDLE", "PLAN", ...); "UNKNOWN" out of range. */
const char *MainArcadeRaceSetup_StatusName(enum MainArcadeRaceSetupStatus status);
const char *MainArcadeRaceSetup_FailureName(enum MainArcadeRaceSetupFailure failure);

/* Only when VALIDATED: the config, race plan, locked bot setup plan, and
 * post-setup bank digests. Returns 0 with every output untouched otherwise. */
int MainArcadeRaceSetup_Digests(uint8_t configDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t racePlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t botSetupPlanDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES],
	uint8_t bankDigest[MAIN_ARCADE_RACE_SETUP_DIGEST_BYTES]);

/* Only when VALIDATED: the validated per-slot setup facts (role, character,
 * difficulty, spawn, nav path, acceleration). Returns 0 with *out untouched
 * otherwise. */
int MainArcadeRaceSetup_SlotFacts(struct MainArcadeBotSetupSourceFacts *out);

/* In SEEDED or VALIDATED: the retail seeds the setup produced and the values
 * read back from randomNumber, advRng state0/state1, the PSX BIOS rand seed,
 * and audioRNG right after they were written (MainArcadeRaceSetupCore_SeedReadback).
 * Returns 0 with both outputs untouched otherwise. */
int MainArcadeRaceSetup_SeedReadback(struct NativeArcadeRetailRngSeedsV1 *produced, struct NativeArcadeRetailRngSeedsV1 *stored);

/* In SEEDED or VALIDATED: the boot-relative counters the setup pinned (RS-17)
 * and the values read back from gGT->timer and gGT->frameTimer_Confetti right
 * after they were written (MainArcadeRaceSetupCore_PinReadback). Returns 0
 * with both outputs untouched otherwise. */
int MainArcadeRaceSetup_PinReadback(struct MainArcadeRaceSetupPins *produced, struct MainArcadeRaceSetupPins *stored);

/* The post-setup bank when VALIDATED, else NULL (Task 8 projects it). */
const struct NativeDeterministicRngBankV1 *MainArcadeRaceSetup_Bank(void);

/*
 * Any state to IDLE. The vibration bits saved at Arm are restored only when
 * Launch wrote the fields and no race armed here can be running: the level
 * is the main-menu level, no load is in progress, and LOADING is clear.
 * Otherwise (mid-race, or on the way into or out of a race) they are not
 * restored, since gameMode1 is in the canonical control domain and must not
 * change under a running race; the drop is logged, and the bits stay at the
 * plan's pinned 0 (rumble enabled on every pad) until the owner changes them.
 */
void MainArcadeRaceSetup_Disarm(void);

#endif
