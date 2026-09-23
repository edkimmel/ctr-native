#ifndef MAIN_ARCADE_RACE_SETUP_FACTS_H
#define MAIN_ARCADE_RACE_SETUP_FACTS_H

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRoster.h"
#include "platform/native_canonical_drivers_roster.h"
#include "platform/native_match_config.h"

#include <stdint.h>

/*
 * Race setup facts builder (docs/ROSTER_MILESTONE.md section 3.2, task R-5a).
 * Turns a pointer-free snapshot of the retail race, taken after
 * MainInit_Drivers, into the observed facts MainArcadeRoster_ValidateNativeFacts
 * and MainArcadeBotSetup_Plan validate against the plan. Dormant: no live
 * callsite, not in game/game_unity.h; the R-5b live adapter fills the
 * snapshot from the retail globals and the roster input from
 * MainCanonicalDrivers_ExtractRosterInput.
 *
 * Pure: reads no game global, does no I/O, uses no heap, keeps no hidden
 * state. Build returns 1 on success, or 0 with both outputs untouched on
 * NULL arguments or invalid input.
 *
 * ---------------------------------------------------------------------------
 * Audit (R-5a): what the retail source holds after MainInit_Drivers in a
 * 2-human arcade race (game/MAIN/MainInit.c:284-402), drivers 0..1 humans,
 * 2..5 bots, 6..7 absent.
 *
 * Order inside MainInit_Drivers: drivers[0..7] = NULL (:290-293);
 * numBotsNextGame = 0 (:295); BOTS_Adv_AdjustDifficulty (:299, skipped only
 * in a cutscene, the adventure arena, or the main menu); humans
 * drivers[i] = VehBirth_Player(i) for i = numPlyrCurrGame-1 .. 0 (:312-315);
 * numDrivers = 6 for 2 humans (:355); BOTS_Driver_Init(i) for i = 2..5 (:360-363).
 *
 * kartSpawnOrderArray (include/regionsEXE.h:3113, char[8]): all 8 entries are
 * written this race init by BOTS_Adv_AdjustDifficulty through
 * BOTS_Adv_CopySpawnOrder (game/BOTS.c:178-182). With no cup
 * (game/BOTS.c:330) it first copies VS_2P for 2 humans (:332-335), then the
 * mode chain replaces it with arcade_1/arcade_2 whenever ADVENTURE_MODE is
 * clear (:357-360), so a 2P arcade race holds {0,1,2,3,4,5,6,7}
 * (game/zGlobal_DATA.c:955-956). Human entries are defined, not stale: the
 * array is indexed by driver slot and VehBirth reads kartSpawnOrderArray[driverID]
 * for the human start position (game/Vehicle/VehBirth.c:241-244, :400);
 * BOTS_GotoStartingLine reads it for bots (game/BOTS.c:3004). Every reader
 * treats the value as u8.
 *
 * driver_pathIndexIDs (include/regionsEXE.h:2402, char[8]): all 8 entries are
 * written as pathOrder[kartSpawnOrderArray[i]] (game/BOTS.c:377-380), where
 * pathOrder = {0, f, s+1, 2, 0, f^1, (s^1)+1, 2} (:363-375) and f, s are two
 * advRng bits (:369, :373); the boss (:382-386) and battle (:388-394)
 * overrides do not apply. So every entry, human entries included, is 0, 1,
 * or 2. BOTS_Driver_Init reads it as s8 (game/BOTS.c:3060) and falls back to
 * a lower path when the chosen path has fewer than 2 points (:3061-3085);
 * the bot's list membership (navBotList, :3102-3105) is then the fallback,
 * and MainArcadeBotSetup reports NAV_MISMATCH (fail-closed). Humans never
 * read it outside BOTS_Driver_Convert (demo mode, pinned off by the plan).
 *
 * accelerateOrder (include/regionsEXE.h:3092, u8[8]): with no cup
 * (game/BOTS.c:396) all 8 entries are written as
 * accelOrder[kartSpawnOrderArray[i]] (:411-414), where accelOrder[0..3] is a
 * rotation of 0..3 and accelOrder[4..7] is 4 plus a descending rotation of
 * 0..3 (:399-409), from two advRng draws. So the 8 entries are a permutation
 * of 0..7, human entries included. The cup swap (:417-441) does not apply.
 * Readers: BOTS_GotoStartingLine indexes it by spawn position
 * (accelerateOrder[kartSpawnOrderArray[driverID]], game/BOTS.c:3004, :3027),
 * bot steering by driverID (:1243, :1484); the arcade spawn order is the
 * identity, so both give accelerateOrder[slot]. The facts use the
 * per-driver index the writer uses.
 *
 * None of the three arrays is written again between MainInit_Drivers and
 * the first race tick (other writers: race end MainGameEnd.c:532, adventure
 * warp pads AH_WarpPad.c:508-552). Entries 6..7 are written too but belong
 * to no driver.
 *
 * driverID: VehBirth_NonGhost sets driver->driverID = playerIndex
 * (game/Vehicle/VehBirth.c:835), called with the slot by VehBirth_Player
 * (:865) and by BOTS_Driver_Init (game/BOTS.c:3098), which also stores
 * drivers[driverID] (:3099); humans are stored by MainInit.c:314. So
 * driverID == slot for every present driver.
 *
 * Bot recognition: ACTION_BOT (include/namespace_Vehicle.h:602) in
 * actionsFlagSet, set by BOTS_Driver_Init (game/BOTS.c:3104) and again by
 * BOTS_GotoStartingLine (:3038). A human Driver is zeroed by VehBirth_Player
 * (game/Vehicle/VehBirth.c:863) and gains ACTION_BOT only through
 * BOTS_Driver_Convert (game/BOTS.c:3188). Its demo-mode call is at
 * MainInit.c:551 (under the boolDemoMode test at :547), pinned off by the
 * plan; the other callers (PlayLevel.c:226, GhostReplay.c:88,
 * MainGameEnd.c:157) run after the snapshot point.
 *
 * Consequence for the dormant validators: the MainArcadeBotSetup range rules
 * that every present slot, humans included, has spawnOrder < 8,
 * navPathIndex < 3, accelerationOrder < 8, and unique spawn and acceleration
 * orders all hold for retail values; no validator rule is corrected.
 * ---------------------------------------------------------------------------
 */

#define MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT 8u

/*
 * Pointer-free copy of the retail values the builder needs, taken after
 * MainInit_Drivers. Every array is indexed by retail driver slot 0..7.
 * driverPresent and driverIsBot are 0 or 1; driverID and driverIsBot of an
 * absent slot are 0 (characterIDs and the three sdata arrays are copied for
 * all 8 slots, as retail holds them). Widths follow retail; the two char
 * arrays carry the signedness their retail readers use.
 */
struct MainArcadeRaceSetupLiveSnapshot
{
	uint8_t numPlyrCurrGame;  /* GameTracker numPlyrCurrGame (u8) */
	uint8_t numBotsNextGame;  /* GameTracker numBotsNextGame (u8) */
	uint8_t reserved[2];      /* 0 */
	uint8_t driverPresent[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT]; /* drivers[i] != NULL */
	uint8_t driverID[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT];      /* Driver driverID (u8) */
	uint8_t driverIsBot[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT];   /* ACTION_BOT in actionsFlagSet */
	int16_t characterIDs[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT];  /* data.characterIDs (s16[8]) */
	uint8_t kartSpawnOrderArray[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT]; /* char[8], read as u8 */
	int8_t driver_pathIndexIDs[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT];  /* char[8], read as s8 */
	uint8_t accelerateOrder[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT];     /* u8[8] */
	int32_t arcadeDifficulty; /* GameTracker arcadeDifficulty (int) */
};

/*
 * Builds the observed facts. The config is used only to reject a profile
 * other than ARCADE_TWO_CAB; nothing is copied from it.
 *
 * Per slot, from observation: a present non-bot driver in slot 0 is
 * CAB1_HUMAN and in slot 1 CAB2_HUMAN (TWO_CAB maps retail players 0 and 1
 * one to one); a present bot is BOT; a non-bot in any other slot fails.
 * Present drivers are ACTIVE, carry their driverID and characterIDs[slot]
 * (fails unless 0..255), difficulty arcadeDifficulty for bots (fails unless
 * 0..255) and 0 for humans, spawnOrder kartSpawnOrderArray[slot],
 * navPathIndex driver_pathIndexIDs[slot] (fails if negative), and
 * accelerationOrder accelerateOrder[slot]; nativeDriverSlot is the retail
 * slot itself. nativeDriverCount and nativeDriverSlots list the present
 * slots in slot order (SLOT_NONE after). numPlyrCurrGame and numBotsNextGame
 * are copied. rosterInput must agree with the snapshot (presence, driverID,
 * and HUMAN/BOT kind per slot) and is copied into rosterFacts->rosterInput.
 * Absent slots take the exact inactive encodings: all-zero roster slot
 * facts, and setup facts with nativeDriverSlot, spawnOrder, navPathIndex,
 * and accelerationOrder SLOT_NONE, role INACTIVE, everything else 0.
 * setupFacts lists all 8 stable slots in ascending order.
 *
 * Whether the facts match the race is for MainArcadeRoster_ValidateNativeFacts
 * and MainArcadeBotSetup_Plan to decide.
 */
int MainArcadeRaceSetupFacts_Build(const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRaceSetupLiveSnapshot *snapshot,
	const struct NativeCanonicalDriversRosterInput *rosterInput,
	struct MainArcadeRosterNativeFacts *rosterFacts,
	struct MainArcadeBotSetupSourceFacts *setupFacts);

#endif
