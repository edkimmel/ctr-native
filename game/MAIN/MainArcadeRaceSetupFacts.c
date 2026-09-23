#include "MAIN/MainArcadeRaceSetupFacts.h"

#include "MAIN/MainArcadeBotSetup.h"
#include "MAIN/MainArcadeRoster.h"
#include "platform/native_canonical_drivers_roster.h"
#include "platform/native_match_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The snapshot is well formed and agrees with the roster input. */
static int MainArcadeRaceSetupFacts_SnapshotIsConsistent(const struct MainArcadeRaceSetupLiveSnapshot *snapshot,
	const struct NativeCanonicalDriversRosterInput *rosterInput)
{
	int anyBot = 0;

	if ((snapshot->reserved[0] != 0u) || (snapshot->reserved[1] != 0u))
	{
		return 0;
	}
	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT; slot++)
	{
		const struct NativeCanonicalDriversRosterSlot *rosterSlot = &rosterInput->slots[slot];
		const uint8_t present = snapshot->driverPresent[slot];
		const uint8_t isBot = snapshot->driverIsBot[slot];

		if ((present > 1u) || (isBot > 1u) || (rosterSlot->present != present))
		{
			return 0;
		}
		if (present == 0u)
		{
			if ((isBot != 0u) || (snapshot->driverID[slot] != 0u))
			{
				return 0;
			}
			continue;
		}
		/* TWO_CAB: retail players 0 and 1 are the two cabinets; any other human has no role. */
		if ((isBot == 0u) && (slot > 1u))
		{
			return 0;
		}
		if ((rosterSlot->driverID != snapshot->driverID[slot]) ||
		    (rosterSlot->kind != (isBot ? NATIVE_CANONICAL_DRIVER_KIND_BOT : NATIVE_CANONICAL_DRIVER_KIND_HUMAN)) ||
		    (snapshot->characterIDs[slot] < 0) || (snapshot->characterIDs[slot] > (int16_t)UINT8_MAX) ||
		    (snapshot->driver_pathIndexIDs[slot] < 0))
		{
			return 0;
		}
		anyBot |= (isBot != 0u);
	}
	return !anyBot || ((snapshot->arcadeDifficulty >= 0) && (snapshot->arcadeDifficulty <= (int32_t)UINT8_MAX));
}

int MainArcadeRaceSetupFacts_Build(const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRaceSetupLiveSnapshot *snapshot,
	const struct NativeCanonicalDriversRosterInput *rosterInput,
	struct MainArcadeRosterNativeFacts *rosterFacts,
	struct MainArcadeBotSetupSourceFacts *setupFacts)
{
	struct MainArcadeRosterNativeFacts roster;
	struct MainArcadeBotSetupSourceFacts setup;

	if ((config == NULL) || (snapshot == NULL) || (rosterInput == NULL) || (rosterFacts == NULL) ||
	    (setupFacts == NULL) || (config->profile != NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB) ||
	    !MainArcadeRaceSetupFacts_SnapshotIsConsistent(snapshot, rosterInput))
	{
		return 0;
	}

	memset(&roster, 0, sizeof(roster));
	memset(&setup, 0, sizeof(setup));
	roster.rosterInput = *rosterInput;
	roster.numPlyrCurrGame = snapshot->numPlyrCurrGame;
	roster.numBotsNextGame = snapshot->numBotsNextGame;
	memset(roster.nativeDriverSlots, MAIN_ARCADE_ROSTER_SLOT_NONE, sizeof(roster.nativeDriverSlots));
	setup.factCount = (uint8_t)MAIN_ARCADE_BOT_SETUP_SLOT_COUNT;

	for (uint32_t slot = 0; slot < MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT; slot++)
	{
		struct MainArcadeRosterNativeSlotFacts *rosterSlot = &roster.slots[slot];
		struct MainArcadeBotSetupSourceSlot *fact = &setup.facts[slot];
		uint8_t role;
		uint8_t difficulty;

		fact->stableSlot = (uint8_t)slot;
		if (snapshot->driverPresent[slot] == 0u)
		{
			fact->nativeDriverSlot = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			fact->role = (uint8_t)NATIVE_MATCH_SLOT_ROLE_INACTIVE;
			fact->spawnOrder = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			fact->navPathIndex = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			fact->accelerationOrder = MAIN_ARCADE_BOT_SETUP_SLOT_NONE;
			continue;
		}

		if (snapshot->driverIsBot[slot] != 0u)
		{
			role = (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT;
			difficulty = (uint8_t)snapshot->arcadeDifficulty;
		}
		else
		{
			role = (uint8_t)(slot == 0u ? NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN : NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
			difficulty = 0u;
		}

		rosterSlot->present = 1u;
		rosterSlot->driverID = snapshot->driverID[slot];
		rosterSlot->role = role;
		rosterSlot->initialLifecycle = (uint8_t)NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE;
		rosterSlot->characterID = (uint8_t)snapshot->characterIDs[slot];
		rosterSlot->difficulty = difficulty;
		roster.nativeDriverSlots[roster.nativeDriverCount++] = (uint8_t)slot;

		fact->present = 1u;
		fact->nativeDriverSlot = (uint8_t)slot;
		fact->role = role;
		fact->characterID = rosterSlot->characterID;
		fact->difficulty = difficulty;
		fact->spawnOrder = snapshot->kartSpawnOrderArray[slot];
		fact->navPathIndex = (uint8_t)snapshot->driver_pathIndexIDs[slot];
		fact->accelerationOrder = snapshot->accelerateOrder[slot];
	}

	*rosterFacts = roster;
	*setupFacts = setup;
	return 1;
}
