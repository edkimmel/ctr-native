#ifndef MAIN_ARCADE_ROSTER_H
#define MAIN_ARCADE_ROSTER_H

#include "platform/native_canonical_drivers_roster.h"
#include "platform/native_match_config.h"

#include <stdint.h>

#define MAIN_ARCADE_ROSTER_SLOT_COUNT NATIVE_MATCH_CONFIG_V1_SLOT_COUNT
#define MAIN_ARCADE_ROSTER_SLOT_NONE UINT8_C(0xff)

/*
 * Pure, dormant construction plan.  This freezes stable ownership and the
 * initial setup values from the portable match configuration; it is not a
 * Driver allocator and has no live game callsite.
 */
struct MainArcadeRosterPlanSlot
{
	uint8_t present;
	uint8_t stableSlot;
	uint8_t role;
	uint8_t initialLifecycle;
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t kind;
	uint8_t reserved;
};

struct MainArcadeRosterPlan
{
	uint32_t profile;
	uint32_t presenceMask;
	uint8_t humanCount;
	uint8_t botCount;
	uint8_t driverCount;
	uint8_t locked;
	uint8_t matchConfigDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct MainArcadeRosterPlanSlot slots[MAIN_ARCADE_ROSTER_SLOT_COUNT];
};

/*
 * Pointer-free facts for a later game-owned adapter.  rosterInput is the same
 * source-shaped contract consumed by MainCanonicalDrivers_ProjectPrelude and
 * MainCanonicalDrivers_ExtractRosterPrelude.  nativeDriverSlots records the
 * native compact prefix explicitly so compaction/reordering cannot be hidden
 * by otherwise valid stable-slot facts.  Unused entries must be SLOT_NONE.
 */
struct MainArcadeRosterNativeSlotFacts
{
	uint8_t present;
	uint8_t driverID;
	uint8_t role;
	uint8_t initialLifecycle;
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t reserved[2];
};

struct MainArcadeRosterNativeFacts
{
	struct NativeCanonicalDriversRosterInput rosterInput;
	uint8_t numPlyrCurrGame;
	uint8_t numBotsNextGame;
	uint8_t nativeDriverCount;
	uint8_t nativeDriverSlots[MAIN_ARCADE_ROSTER_SLOT_COUNT];
	struct MainArcadeRosterNativeSlotFacts slots[MAIN_ARCADE_ROSTER_SLOT_COUNT];
};

struct MainArcadeRosterValidated
{
	uint32_t presenceMask;
	uint8_t humanCount;
	uint8_t botCount;
	uint8_t driverCount;
	uint8_t reserved;
	uint8_t matchConfigDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCanonicalDriversRosterCandidate roster;
	struct MainArcadeRosterNativeSlotFacts slots[MAIN_ARCADE_ROSTER_SLOT_COUNT];
};

/* Both calls are transactional: failed calls leave their output untouched. */
int MainArcadeRoster_BuildPlan(const struct NativeMatchConfigV1 *config,
	struct MainArcadeRosterPlan *out);
int MainArcadeRoster_ValidateNativeFacts(const struct MainArcadeRosterPlan *plan,
	const struct NativeMatchConfigV1 *lockedConfig,
	const struct MainArcadeRosterNativeFacts *facts,
	struct MainArcadeRosterValidated *out);

#endif
