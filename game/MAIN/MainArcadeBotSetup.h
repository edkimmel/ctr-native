#ifndef MAIN_ARCADE_BOT_SETUP_H
#define MAIN_ARCADE_BOT_SETUP_H

#include "MAIN/MainArcadeRoster.h"
#include "platform/native_deterministic_rng.h"

#include <stdint.h>

#define MAIN_ARCADE_BOT_SETUP_SLOT_COUNT MAIN_ARCADE_ROSTER_SLOT_COUNT
#define MAIN_ARCADE_BOT_SETUP_SLOT_NONE UINT8_C(0xff)
#define MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT 3u

/*
 * One complete, pointer-free observation per stable slot. Records may arrive
 * in any order. A later, explicitly gated game adapter may populate these
 * from kartSpawnOrderArray, driver_pathIndexIDs, accelerateOrder, and the
 * effective difficulty selected by native bot setup.
 */
struct MainArcadeBotSetupSourceSlot
{
	uint8_t present;
	uint8_t stableSlot;
	uint8_t nativeDriverSlot;
	uint8_t role;
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t spawnOrder;
	uint8_t navPathIndex;
	uint8_t accelerationOrder;
	uint8_t reserved[3];
};

struct MainArcadeBotSetupSourceFacts
{
	uint8_t factCount;
	uint8_t reserved[3];
	struct MainArcadeBotSetupSourceSlot facts[MAIN_ARCADE_BOT_SETUP_SLOT_COUNT];
};

struct MainArcadeBotSetupAssignment
{
	uint8_t enabled;
	uint8_t stableSlot;
	uint8_t nativeDriverSlot;
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t spawnOrder;
	uint8_t navPathIndex;
	uint8_t accelerationOrder;
	uint8_t setupSequence;
	uint8_t reserved[3];
	uint32_t setupRandom;
};

struct MainArcadeBotSetupPlan
{
	uint32_t profile;
	uint32_t botMask;
	uint8_t botCount;
	uint8_t locked;
	uint8_t reserved[2];
	uint8_t matchConfigDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t rngBeforeDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t rngAfterDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct MainArcadeBotSetupAssignment assignments[MAIN_ARCADE_BOT_SETUP_SLOT_COUNT];
};

enum MainArcadeBotSetupResult
{
	MAIN_ARCADE_BOT_SETUP_OK = 0,
	MAIN_ARCADE_BOT_SETUP_INVALID_ARGUMENT,
	MAIN_ARCADE_BOT_SETUP_INVALID_CONFIG,
	MAIN_ARCADE_BOT_SETUP_INVALID_ROSTER,
	MAIN_ARCADE_BOT_SETUP_INVALID_RNG,
	MAIN_ARCADE_BOT_SETUP_FACT_COUNT,
	MAIN_ARCADE_BOT_SETUP_DUPLICATE_SLOT,
	MAIN_ARCADE_BOT_SETUP_MISSING_SLOT,
	MAIN_ARCADE_BOT_SETUP_MISSING_ACTIVE,
	MAIN_ARCADE_BOT_SETUP_INVALID_INACTIVE,
	MAIN_ARCADE_BOT_SETUP_OWNERSHIP,
	MAIN_ARCADE_BOT_SETUP_COMPACTION,
	MAIN_ARCADE_BOT_SETUP_MISMATCH,
	MAIN_ARCADE_BOT_SETUP_NAV_MISMATCH,
	MAIN_ARCADE_BOT_SETUP_RANGE,
	MAIN_ARCADE_BOT_SETUP_DUPLICATE_SPAWN,
	MAIN_ARCADE_BOT_SETUP_DUPLICATE_ACCELERATION
};

/*
 * Transactional: only OK commits either output. rngBefore and rngAfter may
 * alias. Global setup draws are assigned in ascending stable-slot order.
 */
enum MainArcadeBotSetupResult MainArcadeBotSetup_Plan(
	const struct NativeMatchConfigV1 *config,
	const struct MainArcadeRosterPlan *rosterPlan,
	const struct MainArcadeRosterValidated *validatedRoster,
	const struct MainArcadeBotSetupSourceFacts *sourceFacts,
	const struct NativeDeterministicRngBankV1 *rngBefore,
	struct MainArcadeBotSetupPlan *out,
	struct NativeDeterministicRngBankV1 *rngAfter);

/* Canonical, field-by-field digest of the locked portable setup plan. */
int MainArcadeBotSetupPlan_Digest(const struct MainArcadeBotSetupPlan *plan,
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

#endif
