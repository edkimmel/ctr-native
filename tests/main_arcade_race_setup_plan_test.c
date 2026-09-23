#include "MAIN/MainArcadeRaceSetupPlan.h"

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SENTINEL_BYTE 0xa5u
#define MAIN_MENU_LEVEL_ID 39 /* include/namespace_Level.h */

/*
 * The V2 encoding (RS-21) of the ARCADE_TWO_CAB golden plan (BuildGoldenPlan),
 * spelled out by hand from the header's offset table (not produced by the
 * module). Its field values are the v1 golden's; v2 adds profile,
 * firstBotSlot, botCount, and three more expectedBots entries (0). Frozen.
 */
static const uint8_t k_goldenEncoding[MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES] = {
	/* 0: tag "CTRN arcade race setup plan v2" */
	0x43, 0x54, 0x52, 0x4e, 0x20, 0x61, 0x72, 0x63, 0x61, 0x64, 0x65, 0x20, 0x72, 0x61, 0x63,
	0x65, 0x20, 0x73, 0x65, 0x74, 0x75, 0x70, 0x20, 0x70, 0x6c, 0x61, 0x6e, 0x20, 0x76, 0x32,
	/* 30: locked 1, numPlyrNextGame 2, numLaps 3, boolDemoMode 0 */
	0x01, 0x02, 0x03, 0x00,
	/* 34: profile 1 (ARCADE_TWO_CAB) */
	0x01, 0x00, 0x00, 0x00,
	/* 38: levelID 3 */
	0x03, 0x00, 0x00, 0x00,
	/* 42: gameMode1 clear 0x9F9FCFBF, set 0x00400000 */
	0xbf, 0xcf, 0x9f, 0x9f, 0x00, 0x00, 0x40, 0x00,
	/* 50: gameMode2 clear 0xFFFFFE5F, set 0 */
	0x5f, 0xfe, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
	/* 58: arcadeDifficulty 0xA0 */
	0xa0, 0x00, 0x00, 0x00,
	/* 62: characterWriteMask 0x3F, aiSetIndex 0, firstBotSlot 2, botCount 4, reserved 0, 0 */
	0x3f, 0x00, 0x02, 0x04, 0x00, 0x00,
	/* 68: characterIDs 0, 1, 6, 4, 2, 3, 0, 0 (u16 each) */
	0x00, 0x00, 0x01, 0x00, 0x06, 0x00, 0x04, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 84: expectedBots 6, 4, 2, 3, 0, 0, 0 */
	0x06, 0x04, 0x02, 0x03, 0x00, 0x00, 0x00,
	/* 91: masterSeed 0x0123456789abcdef */
	0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
	/* 99: rngDerivationVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 103: configDigest 0xc0..0xdf */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
};

/*
 * SHA-256 of k_goldenEncoding (927e7d2f...). Obtained independently of this
 * module: the 135 bytes above were written to a file field by field with the
 * shell's printf, from the header's v2 offset table, and hashed with both
 * `sha256sum` and `certutil -hashfile <file> SHA256` (identical results); the
 * same method on the v1 layout reproduced the previous v1 golden digest
 * (eb8eac76...). The file was not committed. Frozen.
 */
static const uint8_t k_goldenDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0x92, 0x7e, 0x7d, 0x2f, 0x3f, 0x62, 0xc1, 0xb5, 0x66, 0x66, 0xac, 0x3a, 0x56, 0xe3, 0x32, 0x82,
	0x21, 0x58, 0x5e, 0xf7, 0x1b, 0x9d, 0x54, 0xd4, 0x12, 0xd9, 0x96, 0x5a, 0xb0, 0x90, 0x2b, 0xb2,
};

/*
 * The V2 encoding of the ARCADE_ONE_CAB golden plan (BuildGoldenOneCabPlan:
 * the human Crash (0) on the fixture's track 3 and 3 laps, medium 0xA0, the
 * golden seed), spelled out by hand from the header's offset table. Frozen.
 */
static const uint8_t k_goldenOneCabEncoding[MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES] = {
	/* 0: tag "CTRN arcade race setup plan v2" */
	0x43, 0x54, 0x52, 0x4e, 0x20, 0x61, 0x72, 0x63, 0x61, 0x64, 0x65, 0x20, 0x72, 0x61, 0x63,
	0x65, 0x20, 0x73, 0x65, 0x74, 0x75, 0x70, 0x20, 0x70, 0x6c, 0x61, 0x6e, 0x20, 0x76, 0x32,
	/* 30: locked 1, numPlyrNextGame 1, numLaps 3, boolDemoMode 0 */
	0x01, 0x01, 0x03, 0x00,
	/* 34: profile 2 (ARCADE_ONE_CAB) */
	0x02, 0x00, 0x00, 0x00,
	/* 38: levelID 3 */
	0x03, 0x00, 0x00, 0x00,
	/* 42: gameMode1 clear 0x9F9FCFBF, set 0x00400000 */
	0xbf, 0xcf, 0x9f, 0x9f, 0x00, 0x00, 0x40, 0x00,
	/* 50: gameMode2 clear 0xFFFFFE5F, set 0 */
	0x5f, 0xfe, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
	/* 58: arcadeDifficulty 0xA0 */
	0xa0, 0x00, 0x00, 0x00,
	/* 62: characterWriteMask 0xFF, aiSetIndex 0xFF (none), firstBotSlot 1, botCount 7, reserved 0, 0 */
	0xff, 0xff, 0x01, 0x07, 0x00, 0x00,
	/* 68: characterIDs 0, 1, 2, 3, 4, 5, 6, 7 (u16 each) */
	0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x00,
	/* 84: expectedBots 1, 2, 3, 4, 5, 6, 7 */
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	/* 91: masterSeed 0x0123456789abcdef */
	0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
	/* 99: rngDerivationVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 103: configDigest 0xc0..0xdf */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
};

/*
 * SHA-256 of k_goldenOneCabEncoding (c7f0dabb...). Obtained independently of
 * this module the same way as k_goldenDigest: printf-written bytes from the
 * header's offset table, hashed with `sha256sum` and `certutil -hashfile
 * <file> SHA256` (identical results). The file was not committed. Frozen.
 */
static const uint8_t k_goldenOneCabDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0xc7, 0xf0, 0xda, 0xbb, 0xf0, 0x03, 0xed, 0xa2, 0xf4, 0xce, 0x19, 0x68, 0xda, 0xc4, 0xde, 0x22,
	0x85, 0x53, 0x3a, 0x16, 0x3f, 0x49, 0x57, 0x22, 0x1a, 0x69, 0x70, 0x72, 0x42, 0x0b, 0xc4, 0x0d,
};

#define GOLDEN_MASTER_SEED UINT64_C(0x0123456789abcdef)

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

/* The hand-built ARCADE_TWO_CAB plan k_goldenEncoding encodes. */
static void BuildGoldenPlan(struct MainArcadeRaceSetupPlan *plan)
{
	static const int16_t characters[MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT] = { 0, 1, 6, 4, 2, 3, 0, 0 };
	static const uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT] = { 6, 4, 2, 3 };

	memset(plan, 0, sizeof(*plan));
	plan->locked = 1;
	plan->numPlyrNextGame = 2;
	plan->numLaps = 3;
	plan->boolDemoMode = 0;
	plan->profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB;
	plan->levelID = 3;
	plan->gameMode1ClearMask = UINT32_C(0x9F9FCFBF);
	plan->gameMode1SetMask = UINT32_C(0x00400000);
	plan->gameMode2ClearMask = UINT32_C(0xFFFFFE5F);
	plan->gameMode2SetMask = 0;
	plan->arcadeDifficulty = 0xa0;
	plan->characterWriteMask = 0x3f;
	plan->aiSetIndex = 0;
	plan->firstBotSlot = 2;
	plan->botCount = 4;
	memcpy(plan->characterIDs, characters, sizeof(characters));
	memcpy(plan->expectedBots, bots, sizeof(bots)); /* expectedBots[4..6] stay 0 */
	plan->masterSeed = GOLDEN_MASTER_SEED;
	plan->rngDerivationVersion = 1;
	FillCounting(plan->configDigest, sizeof(plan->configDigest), 0xc0u);
}

/* The hand-built ARCADE_ONE_CAB plan k_goldenOneCabEncoding encodes. */
static void BuildGoldenOneCabPlan(struct MainArcadeRaceSetupPlan *plan)
{
	static const int16_t characters[MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	static const uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT] = { 1, 2, 3, 4, 5, 6, 7 };

	memset(plan, 0, sizeof(*plan));
	plan->locked = 1;
	plan->numPlyrNextGame = 1;
	plan->numLaps = 3;
	plan->boolDemoMode = 0;
	plan->profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB;
	plan->levelID = 3;
	plan->gameMode1ClearMask = UINT32_C(0x9F9FCFBF);
	plan->gameMode1SetMask = UINT32_C(0x00400000);
	plan->gameMode2ClearMask = UINT32_C(0xFFFFFE5F);
	plan->gameMode2SetMask = 0;
	plan->arcadeDifficulty = 0xa0;
	plan->characterWriteMask = 0xff;
	plan->aiSetIndex = 0xff;
	plan->firstBotSlot = 1;
	plan->botCount = 7;
	memcpy(plan->characterIDs, characters, sizeof(characters));
	memcpy(plan->expectedBots, bots, sizeof(bots));
	plan->masterSeed = GOLDEN_MASTER_SEED;
	plan->rngDerivationVersion = 1;
	FillCounting(plan->configDigest, sizeof(plan->configDigest), 0xc0u);
}

static int PlansEqual(const struct MainArcadeRaceSetupPlan *a, const struct MainArcadeRaceSetupPlan *b)
{
	return (a->locked == b->locked) && (a->numPlyrNextGame == b->numPlyrNextGame) && (a->numLaps == b->numLaps) &&
	       (a->boolDemoMode == b->boolDemoMode) && (a->profile == b->profile) && (a->levelID == b->levelID) &&
	       (a->gameMode1ClearMask == b->gameMode1ClearMask) && (a->gameMode1SetMask == b->gameMode1SetMask) &&
	       (a->gameMode2ClearMask == b->gameMode2ClearMask) && (a->gameMode2SetMask == b->gameMode2SetMask) &&
	       (a->arcadeDifficulty == b->arcadeDifficulty) && (a->characterWriteMask == b->characterWriteMask) &&
	       (a->aiSetIndex == b->aiSetIndex) && (a->firstBotSlot == b->firstBotSlot) && (a->botCount == b->botCount) &&
	       (memcmp(a->reserved, b->reserved, sizeof(a->reserved)) == 0) &&
	       (memcmp(a->characterIDs, b->characterIDs, sizeof(a->characterIDs)) == 0) &&
	       (memcmp(a->expectedBots, b->expectedBots, sizeof(a->expectedBots)) == 0) && (a->masterSeed == b->masterSeed) &&
	       (a->rngDerivationVersion == b->rngDerivationVersion) &&
	       (memcmp(a->configDigest, b->configDigest, sizeof(a->configDigest)) == 0);
}

static int FieldsEqual(const struct MainArcadeRaceSetupRetailFields *a, const struct MainArcadeRaceSetupRetailFields *b)
{
	return (a->levelID == b->levelID) && (a->gameMode1 == b->gameMode1) && (a->gameMode2 == b->gameMode2) &&
	       (a->arcadeDifficulty == b->arcadeDifficulty) &&
	       (memcmp(a->characterIDs, b->characterIDs, sizeof(a->characterIDs)) == 0) && (a->numLaps == b->numLaps) &&
	       (a->numPlyrNextGame == b->numPlyrNextGame) && (a->boolDemoMode == b->boolDemoMode);
}

/*
 * The real arcade-link fixture with every bot at `difficulty`, resolved
 * through match select with both humans' picks and equal track and lap votes.
 */
static int BuildResolved(uint8_t human0, uint8_t human1, uint8_t trackID, uint8_t lapCount, uint8_t difficulty,
	struct NativeMatchConfigV1 *config)
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
	for (uint32_t slot = NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT;
	     slot < NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_BOT_COUNT; slot++)
	{
		base.slots[slot].difficulty = difficulty;
	}
	memset(choices, 0, sizeof(choices));
	choices[0].characterID = human0;
	choices[0].trackID = trackID;
	choices[0].lapCount = lapCount;
	choices[0].nonce = UINT64_C(0x1111111111111111) ^ human0;
	choices[1].characterID = human1;
	choices[1].trackID = trackID;
	choices[1].lapCount = lapCount;
	choices[1].nonce = UINT64_C(0x2222222222222222) ^ ((uint64_t)human1 << 8);
	return NativeMatchSelect_Resolve(&base, 2u, choices, &outcome) && NativeMatchSelect_BuildConfig(&base, &outcome, config);
}

static int TestPolicyConstants(void)
{
	CHECK(MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK == UINT32_C(0x60203040));
	CHECK(MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK == UINT32_C(0x00400000));
	CHECK(MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK == UINT32_C(0x9F9FCFBF));
	CHECK(MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK == UINT32_C(0x00000F00));
	CHECK(MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK == UINT32_C(0x000001A0));
	CHECK(MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK == 0u);
	CHECK(MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK == UINT32_C(0xFFFFFE5F));

	/* Each word is split exactly into clear, set, and transient bits. */
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK) == 0u);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) == 0u);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK & MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) == 0u);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK | MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK |
	          MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) == UINT32_C(0xFFFFFFFF));
	CHECK((MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK) == 0u);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK | MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK) == UINT32_C(0xFFFFFFFF));

	/* HOST-LOCAL vibration is pinned to 0; every cheat and cup bit and every retail mode bit but ARCADE is cleared. */
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK) ==
	      MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK);
	/* PAUSE_1..4 are MODE clear: no load or race path resets them. */
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL) ==
	      MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK & MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL) == 0u);
	CHECK(MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL ==
	      (MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_1 | MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_2 | MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_3 |
	          MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_4));
	CHECK((MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL) == MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM2_CUP_ANY_KIND) != 0u);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM2_CUP_NEW_WIN) != 0u);
	CHECK((MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK & MAIN_ARCADE_RACE_SETUP_GM2_CUP_NEW_BATTLE) != 0u);
	{
		const uint32_t retailModes = MAIN_ARCADE_RACE_SETUP_GM1_BATTLE_MODE | MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_MODE |
		                             MAIN_ARCADE_RACE_SETUP_GM1_TIME_TRIAL | MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_ARENA |
		                             MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_CUP | MAIN_ARCADE_RACE_SETUP_GM1_RELIC_RACE |
		                             MAIN_ARCADE_RACE_SETUP_GM1_CRYSTAL_CHALLENGE | MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_BOSS |
		                             MAIN_ARCADE_RACE_SETUP_GM1_POINT_LIMIT | MAIN_ARCADE_RACE_SETUP_GM1_LIFE_LIMIT |
		                             MAIN_ARCADE_RACE_SETUP_GM1_TIME_LIMIT;

		CHECK((MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK & retailModes) == retailModes);
	}
	CHECK(MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL == UINT32_C(0x08FD8E00));
	CHECK(sizeof(MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG) - 1u == 30u);
	CHECK(MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES == 135u);

	/* The per-profile shape (RS-22), against the bot rules' race shapes. */
	CHECK(MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_TWO_CAB == 2u);
	CHECK(MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_TWO_CAB == NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT);
	CHECK(MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_TWO_CAB == 0x3Fu);
	CHECK(NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_BOT_COUNT == NATIVE_ARCADE_BOT_RULES_DRIVER_COUNT);
	CHECK(MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_ONE_CAB == 1u);
	CHECK(MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_ONE_CAB == NATIVE_ARCADE_BOT_RULES_1P_HUMAN_COUNT);
	CHECK(MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_ONE_CAB == 0xFFu);
	CHECK(NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT ==
	      NATIVE_ARCADE_BOT_RULES_1P_DRIVER_COUNT);
	/* The AI set mirrors equal match select's values. */
	CHECK(MAIN_ARCADE_RACE_SETUP_AI_SET_COUNT == NATIVE_MATCH_SELECT_AI_SET_COUNT);
	CHECK(MAIN_ARCADE_RACE_SETUP_AI_SET_NONE == NATIVE_MATCH_SELECT_AI_SET_NONE);
	return 0;
}

struct ExpectedCase
{
	uint8_t human0;
	uint8_t human1;
	uint8_t trackID;
	uint8_t lapCount;
	uint8_t aiSetIndex;
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
};

/* Hand-derived from the seven retail 2P AI sets (game/zGlobal_DATA.c characterIDs_2P_AIs). */
static const struct ExpectedCase k_cases[] = {
	{ 0, 1, 3, 3, 0, { 6, 4, 2, 3 } },
	{ 6, 4, 6, 5, 4, { 1, 2, 3, 5 } },
	{ 2, 5, 3, 7, 3, { 0, 6, 4, 7 } },
	{ 3, 6, 6, 3, 6, { 4, 7, 1, 2 } },
	{ 7, 0, 6, 7, 0, { 6, 4, 2, 3 } },
};

/* The seven retail 2P AI sets, copied by hand from game/zGlobal_DATA.c:3558-3580. */
static const uint8_t k_retailAiSets[7][NATIVE_ARCADE_BOT_RULES_BOT_COUNT] = {
	{ 6, 4, 2, 3 }, { 0, 6, 3, 5 }, { 0, 6, 1, 2 }, { 0, 6, 4, 7 }, { 1, 2, 3, 5 }, { 4, 7, 3, 5 }, { 4, 7, 1, 2 },
};

/* The LOAD_Robots2P rule over k_retailAiSets: the first set holding neither human. */
static int ExpectedFromRetailSets(uint8_t human0, uint8_t human1, struct ExpectedCase *expected)
{
	for (uint32_t set = 0; set < 7u; set++)
	{
		int holdsHuman = 0;

		for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
		{
			holdsHuman |= (k_retailAiSets[set][i] == human0) || (k_retailAiSets[set][i] == human1);
		}
		if (!holdsHuman)
		{
			expected->human0 = human0;
			expected->human1 = human1;
			expected->aiSetIndex = (uint8_t)set;
			memcpy(expected->bots, k_retailAiSets[set], sizeof(expected->bots));
			return 1;
		}
	}
	return 0;
}

static int TestBuild(void)
{
	static const uint8_t difficulties[3] = { 0x50, 0xa0, 0xf0 };
	struct MainArcadeRaceSetupPlan plan;
	struct NativeMatchConfigV1 config;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint32_t pairCount = 0;

	/* The retail-set derivation agrees with the hand-derived cases. */
	for (uint32_t c = 0; c < sizeof(k_cases) / sizeof(k_cases[0]); c++)
	{
		struct ExpectedCase derived;

		CHECK(ExpectedFromRetailSets(k_cases[c].human0, k_cases[c].human1, &derived) == 1);
		CHECK(derived.aiSetIndex == k_cases[c].aiSetIndex);
		CHECK(memcmp(derived.bots, k_cases[c].bots, sizeof(derived.bots)) == 0);
	}

	/* Every ordered pair of distinct base characters, over every track, lap option, and difficulty. */
	for (uint32_t pair = 0; pair < 64u; pair++)
	{
		struct ExpectedCase expectedCase;
		const struct ExpectedCase *expected = &expectedCase;
		const uint8_t human0 = (uint8_t)(pair / 8u);
		const uint8_t human1 = (uint8_t)(pair % 8u);

		if (human0 == human1)
		{
			continue;
		}
		CHECK(ExpectedFromRetailSets(human0, human1, &expectedCase) == 1);
		expectedCase.trackID = NativeMatchSelect_TrackAt(pairCount % NATIVE_MATCH_SELECT_TRACK_COUNT);
		expectedCase.lapCount = NativeMatchSelect_LapOptionAt(pairCount % NATIVE_MATCH_SELECT_LAP_OPTION_COUNT);
		CHECK((expectedCase.trackID != 0xffu) && (expectedCase.lapCount != 0xffu));
		pairCount++;

		for (uint32_t d = 0; d < 3u; d++)
		{
			CHECK(BuildResolved(expected->human0, expected->human1, expected->trackID, expected->lapCount, difficulties[d],
			          &config) == 1);
			memset(&plan, SENTINEL_BYTE, sizeof(plan));
			CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);

			CHECK(plan.locked == 1u);
			CHECK(plan.numPlyrNextGame == 2u);
			CHECK(plan.numLaps == (int8_t)expected->lapCount);
			CHECK(plan.boolDemoMode == 0u);
			CHECK(plan.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
			CHECK((plan.firstBotSlot == 2u) && (plan.botCount == 4u));
			CHECK((plan.expectedBots[4] == 0u) && (plan.expectedBots[5] == 0u) && (plan.expectedBots[6] == 0u));
			CHECK(plan.levelID == (int32_t)expected->trackID);
			CHECK(plan.gameMode1ClearMask == UINT32_C(0x9F9FCFBF));
			CHECK(plan.gameMode1SetMask == UINT32_C(0x00400000));
			CHECK(plan.gameMode2ClearMask == UINT32_C(0xFFFFFE5F));
			CHECK(plan.gameMode2SetMask == 0u);
			CHECK(plan.arcadeDifficulty == (int32_t)difficulties[d]);
			CHECK(plan.characterWriteMask == 0x3fu);
			CHECK(plan.aiSetIndex == expected->aiSetIndex);
			CHECK((plan.reserved[0] == 0u) && (plan.reserved[1] == 0u));
			CHECK(plan.characterIDs[0] == (int16_t)expected->human0);
			CHECK(plan.characterIDs[1] == (int16_t)expected->human1);
			for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
			{
				CHECK(plan.characterIDs[2u + i] == (int16_t)expected->bots[i]);
				CHECK(plan.expectedBots[i] == expected->bots[i]);
			}
			CHECK((plan.characterIDs[6] == 0) && (plan.characterIDs[7] == 0));
			CHECK(plan.masterSeed == config.masterSeed);
			CHECK(plan.masterSeed != NATIVE_ARCADE_LINK_FIXTURE_MASTER_SEED);
			CHECK(plan.masterSeed != 0u);
			CHECK(plan.rngDerivationVersion == 1u);
			CHECK(NativeMatchConfigV1_Digest(&config, digest) == 1);
			CHECK(memcmp(plan.configDigest, digest, sizeof(digest)) == 0);
			CHECK(MainArcadeRaceSetupPlan_Digest(&plan, digest) == 1);
		}
	}
	CHECK(pairCount == 56u);

	/* The fixture's own race, with the golden seed and config digest, is the golden plan. */
	{
		struct MainArcadeRaceSetupPlan golden;

		CHECK(BuildResolved(0, 1, 3, 3, 0xa0, &config) == 1);
		CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);
		plan.masterSeed = GOLDEN_MASTER_SEED;
		FillCounting(plan.configDigest, sizeof(plan.configDigest), 0xc0u);
		BuildGoldenPlan(&golden);
		CHECK(PlansEqual(&plan, &golden));
	}

	/* Deterministic: the same config builds the same plan. */
	{
		struct MainArcadeRaceSetupPlan again;

		CHECK(BuildResolved(2, 5, 3, 7, 0x50, &config) == 1);
		CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);
		CHECK(MainArcadeRaceSetupPlan_Build(&config, &again) == 1);
		CHECK(PlansEqual(&plan, &again));
	}
	return 0;
}

/* Build fails with the output untouched; the config must still be otherwise valid where noted. */
static int ExpectBuildReject(const struct NativeMatchConfigV1 *config)
{
	struct MainArcadeRaceSetupPlan plan;

	memset(&plan, SENTINEL_BYTE, sizeof(plan));
	CHECK(MainArcadeRaceSetupPlan_Build(config, &plan) == 0);
	CHECK(IsAllByte(&plan, sizeof(plan), SENTINEL_BYTE));
	return 0;
}

/*
 * A well-formed ARCADE_ONE_CAB config (RS-19, RS-20): the ONE_CAB profile,
 * *valid's table track, lap count, and nonzero identities and seed, 30/1 ticks,
 * CAB1 a base character at difficulty 0, slots 1..7 exactly ExpectedBots1P at
 * one table difficulty, and the 1P bot rules digest.
 */
static int BuildOneCab(const struct NativeMatchConfigV1 *valid, uint8_t human, uint8_t difficulty,
	struct NativeMatchConfigV1 *config)
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
		config->slots[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i].difficulty = difficulty;
	}
	return NativeArcadeBotRules_Digest1PV1(config->botRulesDigest);
}

static int TestBuildRejects(void)
{
	struct NativeMatchConfigV1 valid;
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;

	CHECK(BuildResolved(2, 5, 6, 5, 0xa0, &valid) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&valid, &plan) == 1);

	/* NULL arguments. */
	CHECK(ExpectBuildReject(NULL) == 0);
	CHECK(MainArcadeRaceSetupPlan_Build(&valid, NULL) == 0);

	/* One representative of every NativeArcadeBotRules_ValidateConfigV1 failure class. */
	config = valid; /* generic validator: a nonzero inactive slot */
	config.slots[6].characterID = 1u;
	CHECK(NativeMatchConfigV1_Validate(&config) == 0);
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* generic validator: the ONE_CAB profile over TWO_CAB slot roles */
	config.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB;
	CHECK(NativeMatchConfigV1_Validate(&config) == 0);
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* mode fields */
	config.gameMode1 = 1u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid;
	config.gameMode2 = 1u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid;
	config.rules = 1u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* bot rules digest */
	config.botRulesDigest[17] ^= 0x01u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* off-table track */
	config.trackID = 13u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* off-table laps */
	config.lapCount = 4u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* equal human characters */
	config.slots[1].characterID = config.slots[0].characterID;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* non-base human character */
	config.slots[0].characterID = 8u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* nonzero human difficulty */
	config.slots[1].difficulty = 0xa0u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* bot difficulty off the table, all equal */
	for (uint32_t slot = 2; slot <= 5u; slot++)
	{
		config.slots[slot].difficulty = 0x51u;
	}
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* unequal bot difficulties */
	config.slots[4].difficulty = 0xf0u;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* a bot character off the LOAD_Robots2P rule */
	config.slots[3].characterID = (uint8_t)(config.slots[3].characterID == 1u ? 3u : 1u);
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid; /* bots out of set order */
	{
		const uint8_t swap = config.slots[2].characterID;

		config.slots[2].characterID = config.slots[5].characterID;
		config.slots[5].characterID = swap;
	}
	CHECK(ExpectBuildReject(&config) == 0);

	/* Tick rate: valid under the bot rules, but not the retail 30 Hz loop (RS-14). */
	config = valid;
	config.tickRateNumerator = 60u;
	config.tickRateDenominator = 1u;
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid;
	config.tickRateNumerator = 30u;
	config.tickRateDenominator = 2u;
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(ExpectBuildReject(&config) == 0);

	/* A well-formed ONE_CAB config: valid under the bot rules, and the plan builds it (OC-2). */
	CHECK(BuildOneCab(&valid, 2u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD, &config) == 1);
	CHECK(NativeMatchConfigV1_Validate(&config) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);
	CHECK(plan.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
	return 0;
}

/* Malformed ONE_CAB configs, one field each, from a well-formed one. */
static int TestBuildRejectsOneCab(void)
{
	struct NativeMatchConfigV1 twoCab;
	struct NativeMatchConfigV1 valid;
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;

	CHECK(BuildResolved(2, 5, 6, 5, 0xa0, &twoCab) == 1);
	CHECK(BuildOneCab(&twoCab, 3u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM, &valid) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&valid, &plan) == 1);

	/* The TWO_CAB bot rules digest on a ONE_CAB config (RS-19). */
	config = valid;
	CHECK(NativeArcadeBotRules_DigestV1(config.botRulesDigest) == 1);
	CHECK(ExpectBuildReject(&config) == 0);
	/* Two bots swapped: not the LOAD_Robots1P slot order. */
	config = valid;
	{
		const uint8_t swap = config.slots[2].characterID;

		config.slots[2].characterID = config.slots[6].characterID;
		config.slots[6].characterID = swap;
	}
	CHECK(ExpectBuildReject(&config) == 0);
	/* A bot character off the LOAD_Robots1P rule (the human's own). */
	config = valid;
	config.slots[7].characterID = config.slots[0].characterID;
	CHECK(ExpectBuildReject(&config) == 0);
	/* A nonzero human difficulty. */
	config = valid;
	config.slots[0].difficulty = 0xa0u;
	CHECK(ExpectBuildReject(&config) == 0);
	/* Mixed bot difficulties. */
	config = valid;
	config.slots[5].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD;
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid;
	config.slots[1].difficulty = NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY;
	CHECK(ExpectBuildReject(&config) == 0);
	/* A non-base human character. */
	config = valid;
	config.slots[0].characterID = 8u;
	CHECK(ExpectBuildReject(&config) == 0);
	/* Tick rate: valid under the bot rules, but not the retail 30 Hz loop (RS-14). */
	config = valid;
	config.tickRateNumerator = 60u;
	config.tickRateDenominator = 1u;
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(ExpectBuildReject(&config) == 0);
	config = valid;
	config.tickRateNumerator = 30u;
	config.tickRateDenominator = 2u;
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(ExpectBuildReject(&config) == 0);
	/* The TWO_CAB profile over ONE_CAB slot roles: the generic validator rejects it. */
	config = valid;
	config.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB;
	CHECK(NativeMatchConfigV1_Validate(&config) == 0);
	CHECK(ExpectBuildReject(&config) == 0);
	/* A profile the plan has no shape for. */
	config = valid;
	config.profile = 3u;
	CHECK(ExpectBuildReject(&config) == 0);
	config.profile = 0u;
	CHECK(ExpectBuildReject(&config) == 0);
	return 0;
}

/* LOAD_Robots1P over the base characters, by hand: {0..7} without human, ascending. */
static void ExpectedBots1PByHand(uint8_t human, uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT])
{
	uint32_t count = 0;

	for (uint8_t id = 0; id < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; id++)
	{
		if (id != human)
		{
			bots[count++] = id;
		}
	}
}

static int TestBuildOneCab(void)
{
	static const uint8_t difficulties[3] = { 0x50, 0xa0, 0xf0 };
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	CHECK(BuildResolved(0, 1, 3, 3, 0xa0, &base) == 1);

	/* Every base human, at every table difficulty. */
	for (uint8_t human = 0; human < 8u; human++)
	{
		uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];
		uint8_t byHand[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];

		CHECK(NativeArcadeBotRules_ExpectedBots1P(human, bots) == 1);
		ExpectedBots1PByHand(human, byHand);
		CHECK(memcmp(bots, byHand, sizeof(bots)) == 0);
		for (uint32_t d = 0; d < 3u; d++)
		{
			CHECK(BuildOneCab(&base, human, difficulties[d], &config) == 1);
			memset(&plan, SENTINEL_BYTE, sizeof(plan));
			CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);

			CHECK(plan.locked == 1u);
			CHECK(plan.numPlyrNextGame == 1u);
			CHECK(plan.numLaps == 3);
			CHECK(plan.boolDemoMode == 0u);
			CHECK(plan.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
			CHECK(plan.levelID == 3);
			CHECK(plan.gameMode1ClearMask == UINT32_C(0x9F9FCFBF));
			CHECK(plan.gameMode1SetMask == UINT32_C(0x00400000));
			CHECK(plan.gameMode2ClearMask == UINT32_C(0xFFFFFE5F));
			CHECK(plan.gameMode2SetMask == 0u);
			CHECK(plan.arcadeDifficulty == (int32_t)difficulties[d]);
			CHECK(plan.characterWriteMask == 0xffu);
			CHECK(plan.aiSetIndex == 0xffu);
			CHECK((plan.firstBotSlot == 1u) && (plan.botCount == 7u));
			CHECK((plan.reserved[0] == 0u) && (plan.reserved[1] == 0u));
			CHECK(plan.characterIDs[0] == (int16_t)human);
			for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT; i++)
			{
				CHECK(plan.characterIDs[1u + i] == (int16_t)byHand[i]);
				CHECK(plan.expectedBots[i] == byHand[i]);
			}
			CHECK(plan.masterSeed == config.masterSeed);
			CHECK(plan.rngDerivationVersion == 1u);
			CHECK(NativeMatchConfigV1_Digest(&config, digest) == 1);
			CHECK(memcmp(plan.configDigest, digest, sizeof(digest)) == 0);
			CHECK(MainArcadeRaceSetupPlan_Digest(&plan, digest) == 1);
		}
	}

	/* The golden ONE_CAB config: Crash on the fixture's track and laps, medium, the golden seed. */
	{
		struct MainArcadeRaceSetupPlan golden;

		CHECK(BuildOneCab(&base, 0u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM, &config) == 1);
		config.masterSeed = GOLDEN_MASTER_SEED;
		CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);
		FillCounting(plan.configDigest, sizeof(plan.configDigest), 0xc0u);
		BuildGoldenOneCabPlan(&golden);
		CHECK(PlansEqual(&plan, &golden));
	}

	/* Deterministic: the same config builds the same plan. */
	{
		struct MainArcadeRaceSetupPlan again;

		CHECK(BuildOneCab(&base, 5u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY, &config) == 1);
		CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);
		CHECK(MainArcadeRaceSetupPlan_Build(&config, &again) == 1);
		CHECK(PlansEqual(&plan, &again));
	}
	return 0;
}

static void FreshBootFields(struct MainArcadeRaceSetupRetailFields *fields)
{
	memset(fields, 0, sizeof(*fields));
	fields->levelID = MAIN_MENU_LEVEL_ID;
	fields->gameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU;
	fields->gameMode2 = 0;
	fields->arcadeDifficulty = 0x50;
	for (uint32_t i = 0; i < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; i++)
	{
		fields->characterIDs[i] = (int16_t)i;
	}
	fields->numLaps = 3;
	fields->numPlyrNextGame = 1;
	fields->boolDemoMode = 0;
}

/* After adventure, a quit relic and boss race, cheats, a cup, vibration off, and a demo. */
static void LongHistoryFields(struct MainArcadeRaceSetupRetailFields *fields)
{
	memset(fields, 0, sizeof(*fields));
	fields->levelID = 17;
	fields->gameMode1 = MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_1 | MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_4 |
	                    MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_MODE |
	                    MAIN_ARCADE_RACE_SETUP_GM1_RELIC_RACE | MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_BOSS |
	                    MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK | MAIN_ARCADE_RACE_SETUP_GM1_ROLLING_ITEM |
	                    MAIN_ARCADE_RACE_SETUP_GM1_AKU_SONG | MAIN_ARCADE_RACE_SETUP_GM1_BATTLE_MODE |
	                    MAIN_ARCADE_RACE_SETUP_GM1_POINT_LIMIT | MAIN_ARCADE_RACE_SETUP_GM1_TIME_TRIAL |
	                    MAIN_ARCADE_RACE_SETUP_GM1_DEBUG_MENU | UINT32_C(0x80) | MAIN_ARCADE_RACE_SETUP_GM1_LOADING;
	fields->gameMode2 = MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL | MAIN_ARCADE_RACE_SETUP_GM2_CUP_ANY_KIND |
	                    MAIN_ARCADE_RACE_SETUP_GM2_CUP_NEW_WIN | MAIN_ARCADE_RACE_SETUP_GM2_TOKEN_RACE |
	                    MAIN_ARCADE_RACE_SETUP_GM2_SPAWN_AT_BOSS | MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP |
	                    MAIN_ARCADE_RACE_SETUP_GM2_GARAGE_OSK | UINT32_C(0x80000000);
	fields->arcadeDifficulty = 0xf0;
	for (uint32_t i = 0; i < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT; i++)
	{
		fields->characterIDs[i] = (int16_t)(15 - (int)i);
	}
	fields->numLaps = 1;
	fields->numPlyrNextGame = 4;
	fields->boolDemoMode = 1;
}

static int CheckApplied(const struct MainArcadeRaceSetupPlan *plan, const struct MainArcadeRaceSetupRetailFields *before,
	const struct MainArcadeRaceSetupRetailFields *after)
{
	/* Owned fields. */
	CHECK(after->levelID == plan->levelID);
	CHECK(after->numLaps == plan->numLaps);
	CHECK(after->numPlyrNextGame == 2u);
	CHECK(after->arcadeDifficulty == plan->arcadeDifficulty);
	CHECK(after->boolDemoMode == 0u);
	CHECK((after->gameMode1 & ~MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) == MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE);
	CHECK((after->gameMode2 & ~MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK) == 0u);
	for (uint32_t i = 0; i < 6u; i++)
	{
		CHECK(after->characterIDs[i] == plan->characterIDs[i]);
	}
	/* TRANSIENT bits and characterIDs[6..7] untouched. */
	CHECK((after->gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) ==
	      (before->gameMode1 & MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK));
	CHECK((after->gameMode2 & MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK) ==
	      (before->gameMode2 & MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK));
	CHECK((after->characterIDs[6] == before->characterIDs[6]) && (after->characterIDs[7] == before->characterIDs[7]));
	return 0;
}

/* Every owned field, compared between two applied states. */
static int OwnedEqual(const struct MainArcadeRaceSetupRetailFields *a, const struct MainArcadeRaceSetupRetailFields *b)
{
	CHECK(a->levelID == b->levelID);
	CHECK(a->numLaps == b->numLaps);
	CHECK(a->numPlyrNextGame == b->numPlyrNextGame);
	CHECK(a->arcadeDifficulty == b->arcadeDifficulty);
	CHECK(a->boolDemoMode == b->boolDemoMode);
	CHECK((a->gameMode1 & ~MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK) ==
	      (b->gameMode1 & ~MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK));
	CHECK((a->gameMode2 & ~MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK) ==
	      (b->gameMode2 & ~MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK));
	CHECK(memcmp(a->characterIDs, b->characterIDs, 6u * sizeof(a->characterIDs[0])) == 0);
	return 0;
}

static int TestApply(void)
{
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;
	struct MainArcadeRaceSetupRetailFields befores[4];
	struct MainArcadeRaceSetupRetailFields afters[4];
	struct MainArcadeRaceSetupRetailFields again;
	struct MainArcadeRaceSetupRetailFields aliased;

	CHECK(BuildResolved(6, 4, 6, 5, 0xf0, &config) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);

	FreshBootFields(&befores[0]);
	LongHistoryFields(&befores[1]);
	memset(&befores[2], 0xff, sizeof(befores[2])); /* every bit set, characterIDs -1 */
	memset(&befores[3], 0, sizeof(befores[3]));

	for (uint32_t i = 0; i < 4u; i++)
	{
		memset(&afters[i], SENTINEL_BYTE, sizeof(afters[i]));
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &befores[i], &afters[i]) == 1);
		CHECK(CheckApplied(&plan, &befores[i], &afters[i]) == 0);
		CHECK(OwnedEqual(&afters[0], &afters[i]) == 0);

		/* Deterministic, and in place. */
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &befores[i], &again) == 1);
		CHECK(FieldsEqual(&again, &afters[i]));
		aliased = befores[i];
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &aliased, &aliased) == 1);
		CHECK(FieldsEqual(&aliased, &afters[i]));
		/* Idempotent. */
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &afters[i], &again) == 1);
		CHECK(FieldsEqual(&again, &afters[i]));
	}

	/* Exact values for the fresh boot and the long history. */
	CHECK(afters[0].gameMode1 == (MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(afters[0].gameMode2 == 0u);
	CHECK(afters[1].gameMode1 == (MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_LOADING |
	                                 MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(afters[1].gameMode2 == MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP);
	CHECK(afters[2].gameMode1 == (MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(afters[2].gameMode2 == MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK);
	CHECK(afters[3].gameMode1 == MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE);
	CHECK(afters[3].gameMode2 == 0u);
	CHECK((afters[1].levelID == 6) && (afters[1].numLaps == 5) && (afters[1].numPlyrNextGame == 2u) &&
	      (afters[1].arcadeDifficulty == 0xf0) && (afters[1].boolDemoMode == 0u));
	CHECK((afters[1].characterIDs[0] == 6) && (afters[1].characterIDs[1] == 4) && (afters[1].characterIDs[2] == 1) &&
	      (afters[1].characterIDs[3] == 2) && (afters[1].characterIDs[4] == 3) && (afters[1].characterIDs[5] == 5));
	CHECK((afters[1].characterIDs[6] == 9) && (afters[1].characterIDs[7] == 8));
	CHECK((afters[2].characterIDs[6] == -1) && (afters[2].characterIDs[7] == -1));
	return 0;
}

/* ONE_CAB: the same mode words, numPlyrNextGame 1, and all eight characterIDs written. */
static int TestApplyOneCab(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 config;
	struct MainArcadeRaceSetupPlan plan;
	struct MainArcadeRaceSetupPlan twoCabPlan;
	struct MainArcadeRaceSetupRetailFields befores[4];
	struct MainArcadeRaceSetupRetailFields afters[4];
	struct MainArcadeRaceSetupRetailFields twoCabAfter;
	struct MainArcadeRaceSetupRetailFields again;
	struct MainArcadeRaceSetupRetailFields aliased;

	CHECK(BuildResolved(6, 4, 6, 5, 0xf0, &base) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&base, &twoCabPlan) == 1);
	CHECK(BuildOneCab(&base, 6u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD, &config) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&config, &plan) == 1);

	FreshBootFields(&befores[0]);
	LongHistoryFields(&befores[1]);
	memset(&befores[2], 0xff, sizeof(befores[2]));
	memset(&befores[3], 0, sizeof(befores[3]));

	for (uint32_t i = 0; i < 4u; i++)
	{
		memset(&afters[i], SENTINEL_BYTE, sizeof(afters[i]));
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &befores[i], &afters[i]) == 1);
		CHECK(afters[i].levelID == 6 && afters[i].numLaps == 5 && afters[i].numPlyrNextGame == 1u);
		CHECK(afters[i].arcadeDifficulty == 0xf0 && afters[i].boolDemoMode == 0u);
		/* Every characterID is the plan's: the human 6, then LOAD_Robots1P's 0, 1, 2, 3, 4, 5, 7. */
		CHECK((afters[i].characterIDs[0] == 6) && (afters[i].characterIDs[1] == 0) && (afters[i].characterIDs[2] == 1) &&
		      (afters[i].characterIDs[3] == 2) && (afters[i].characterIDs[4] == 3) && (afters[i].characterIDs[5] == 4) &&
		      (afters[i].characterIDs[6] == 5) && (afters[i].characterIDs[7] == 7));
		CHECK(memcmp(afters[i].characterIDs, plan.characterIDs, sizeof(plan.characterIDs)) == 0);

		/* The mode words are exactly the TWO_CAB plan's (the audit is profile-independent). */
		CHECK(MainArcadeRaceSetupPlan_Apply(&twoCabPlan, &befores[i], &twoCabAfter) == 1);
		CHECK((afters[i].gameMode1 == twoCabAfter.gameMode1) && (afters[i].gameMode2 == twoCabAfter.gameMode2));
		/* ... and TWO_CAB writes only characterIDs[0..5]. */
		CHECK((twoCabAfter.characterIDs[6] == befores[i].characterIDs[6]) &&
		      (twoCabAfter.characterIDs[7] == befores[i].characterIDs[7]));
		CHECK(memcmp(twoCabAfter.characterIDs, twoCabPlan.characterIDs, 6u * sizeof(twoCabPlan.characterIDs[0])) == 0);

		/* Deterministic, in place, and idempotent. */
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &befores[i], &again) == 1);
		CHECK(FieldsEqual(&again, &afters[i]));
		aliased = befores[i];
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &aliased, &aliased) == 1);
		CHECK(FieldsEqual(&aliased, &afters[i]));
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &afters[i], &again) == 1);
		CHECK(FieldsEqual(&again, &afters[i]));
	}
	CHECK(afters[0].gameMode1 == (MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE));
	CHECK(afters[1].gameMode2 == MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP);
	return 0;
}

static int ExpectApplyReject(const struct MainArcadeRaceSetupPlan *plan)
{
	struct MainArcadeRaceSetupRetailFields before;
	struct MainArcadeRaceSetupRetailFields after;

	FreshBootFields(&before);
	memset(&after, SENTINEL_BYTE, sizeof(after));
	CHECK(MainArcadeRaceSetupPlan_Apply(plan, &before, &after) == 0);
	CHECK(IsAllByte(&after, sizeof(after), SENTINEL_BYTE));
	return 0;
}

static int ExpectDigestReject(const struct MainArcadeRaceSetupPlan *plan)
{
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(MainArcadeRaceSetupPlan_Digest(plan, digest) == 0);
	CHECK(IsAllByte(digest, sizeof(digest), SENTINEL_BYTE));
	return 0;
}

static int TestMalformedPlans(void)
{
	struct MainArcadeRaceSetupPlan valid;
	struct MainArcadeRaceSetupPlan plan;
	struct MainArcadeRaceSetupRetailFields fields;
	struct MainArcadeRaceSetupRetailFields untouched;

	BuildGoldenPlan(&valid);
	FreshBootFields(&fields);
	CHECK(MainArcadeRaceSetupPlan_Apply(&valid, &fields, &fields) == 1);

	/* NULL arguments. */
	CHECK(ExpectApplyReject(NULL) == 0);
	CHECK(ExpectDigestReject(NULL) == 0);
	CHECK(MainArcadeRaceSetupPlan_Digest(&valid, NULL) == 0);
	memset(&untouched, SENTINEL_BYTE, sizeof(untouched));
	fields = untouched;
	CHECK(MainArcadeRaceSetupPlan_Apply(&valid, NULL, &fields) == 0);
	CHECK(IsAllByte(&fields, sizeof(fields), SENTINEL_BYTE));
	FreshBootFields(&fields);
	CHECK(MainArcadeRaceSetupPlan_Apply(&valid, &fields, NULL) == 0);

	/* Each fixed field off the policy. */
	memset(&plan, 0, sizeof(plan));
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.locked = 0;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.numPlyrNextGame = 1;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.boolDemoMode = 1;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.gameMode1ClearMask &= ~MAIN_ARCADE_RACE_SETUP_GM1_P1_VIBRATE;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.gameMode1SetMask |= MAIN_ARCADE_RACE_SETUP_GM1_BATTLE_MODE;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.gameMode2ClearMask |= MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.gameMode2SetMask = MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBO;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.characterWriteMask = 0xffu;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.reserved[1] = 1u;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);
	plan = valid;
	plan.characterIDs[7] = 3;
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);

	/* A bot slot that differs from expectedBots, either side. */
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
	{
		plan = valid;
		plan.characterIDs[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i] = 7;
		CHECK(ExpectApplyReject(&plan) == 0);
		CHECK(ExpectDigestReject(&plan) == 0);
		plan = valid;
		plan.expectedBots[i] = 7u;
		CHECK(ExpectApplyReject(&plan) == 0);
		CHECK(ExpectDigestReject(&plan) == 0);
	}
	plan = valid;
	plan.characterIDs[2] = (int16_t)(plan.expectedBots[0] + 256); /* same low byte */
	CHECK(ExpectApplyReject(&plan) == 0);
	CHECK(ExpectDigestReject(&plan) == 0);

	/* arcadeDifficulty off the bot-rules table. */
	{
		static const int32_t offTable[] = { 0, 0x4f, 0x51, 0xa1, 0xef, 0xf1, 0x1a0, -0xa0, INT32_MIN, INT32_MAX };

		for (uint32_t i = 0; i < sizeof(offTable) / sizeof(offTable[0]); i++)
		{
			plan = valid;
			plan.arcadeDifficulty = offTable[i];
			CHECK(ExpectApplyReject(&plan) == 0);
			CHECK(ExpectDigestReject(&plan) == 0);
		}
	}
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT; i++)
	{
		plan = valid;
		plan.arcadeDifficulty = (int32_t)NativeArcadeBotRules_DifficultyAt(i);
		FreshBootFields(&fields);
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &fields, &fields) == 1);
	}
	return 0;
}

/* Both Apply and Digest refuse the plan with their outputs untouched. */
static int ExpectPlanReject(const struct MainArcadeRaceSetupPlan *plan)
{
	CHECK(ExpectApplyReject(plan) == 0);
	CHECK(ExpectDigestReject(plan) == 0);
	return 0;
}

/* A TWO_CAB plan's whole roster replaced: humans 0 and 1, their ExpectedBots2P bots, and its AI set. */
static int SetTwoCabRoster(struct MainArcadeRaceSetupPlan *plan, uint8_t human0, uint8_t human1)
{
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
	uint8_t set = 0;

	if (!NativeArcadeBotRules_ExpectedBots2P(human0, human1, bots, &set))
	{
		return 0;
	}
	plan->characterIDs[0] = (int16_t)human0;
	plan->characterIDs[1] = (int16_t)human1;
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
	{
		plan->characterIDs[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i] = (int16_t)bots[i];
		plan->expectedBots[i] = bots[i];
	}
	plan->aiSetIndex = set;
	return 1;
}

/* A ONE_CAB plan's whole roster replaced: human 0 and its ExpectedBots1P bots. */
static int SetOneCabRoster(struct MainArcadeRaceSetupPlan *plan, uint8_t human)
{
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];

	if (!NativeArcadeBotRules_ExpectedBots1P(human, bots))
	{
		return 0;
	}
	plan->characterIDs[0] = (int16_t)human;
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT; i++)
	{
		plan->characterIDs[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i] = (int16_t)bots[i];
		plan->expectedBots[i] = bots[i];
	}
	return 1;
}

/* The bots are tied to the humans: a plan whose bots are not its humans' bots is refused. */
static int TestBotsTiedToHumans(void)
{
	struct MainArcadeRaceSetupPlan twoCab;
	struct MainArcadeRaceSetupPlan oneCab;
	struct MainArcadeRaceSetupPlan plan;
	struct MainArcadeRaceSetupRetailFields fields;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	BuildGoldenPlan(&twoCab);
	BuildGoldenOneCabPlan(&oneCab);

	/* TWO_CAB: every ordered pair of distinct base characters, roster whole, passes. */
	for (uint8_t human0 = 0; human0 < 8u; human0++)
	{
		for (uint8_t human1 = 0; human1 < 8u; human1++)
		{
			plan = twoCab;
			if (human0 == human1)
			{
				/* not distinct: ExpectedBots2P fails */
				plan.characterIDs[0] = (int16_t)human0;
				plan.characterIDs[1] = (int16_t)human0;
				CHECK(ExpectPlanReject(&plan) == 0);
				continue;
			}
			CHECK(SetTwoCabRoster(&plan, human0, human1) == 1);
			FreshBootFields(&fields);
			CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &fields, &fields) == 1);
			CHECK(MainArcadeRaceSetupPlan_Digest(&plan, digest) == 1);
			/* ... and every other AI set with those bots is refused. */
			for (uint8_t set = 0; set < MAIN_ARCADE_RACE_SETUP_AI_SET_COUNT; set++)
			{
				struct MainArcadeRaceSetupPlan wrongSet = plan;

				if (set != plan.aiSetIndex)
				{
					wrongSet.aiSetIndex = set;
					CHECK(ExpectPlanReject(&wrongSet) == 0);
				}
			}
		}
	}
	/* TWO_CAB: a human changed alone, the golden bots and set 0 kept, where
	 * the new pair's bots are another set's. */
	plan = twoCab;
	plan.characterIDs[1] = 6; /* humans 0, 6: set 0 holds 6 */
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = twoCab;
	plan.characterIDs[0] = 4; /* humans 4, 1: set 0 holds 4 */
	CHECK(ExpectPlanReject(&plan) == 0);
	/* TWO_CAB: the right bots in another order, in both the slots and expectedBots. */
	plan = twoCab;
	plan.characterIDs[2] = 4;
	plan.characterIDs[3] = 6;
	plan.expectedBots[0] = 4u;
	plan.expectedBots[1] = 6u;
	CHECK(ExpectPlanReject(&plan) == 0);
	/* TWO_CAB: a human outside 0..255, or a byte-equal alias of a valid human. */
	{
		static const int16_t offByte[] = { -1, -3, 256, 257, INT16_MIN, INT16_MAX };

		for (uint32_t i = 0; i < sizeof(offByte) / sizeof(offByte[0]); i++)
		{
			plan = twoCab;
			plan.characterIDs[0] = offByte[i];
			CHECK(ExpectPlanReject(&plan) == 0);
			plan = twoCab;
			plan.characterIDs[1] = offByte[i];
			CHECK(ExpectPlanReject(&plan) == 0);
		}
	}
	/* TWO_CAB: a human that is no base character. */
	plan = twoCab;
	plan.characterIDs[1] = 8;
	CHECK(ExpectPlanReject(&plan) == 0);

	/* ONE_CAB: every base character as the human, roster whole, passes. */
	for (uint8_t human = 0; human < 8u; human++)
	{
		plan = oneCab;
		CHECK(SetOneCabRoster(&plan, human) == 1);
		FreshBootFields(&fields);
		CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &fields, &fields) == 1);
		CHECK(MainArcadeRaceSetupPlan_Digest(&plan, digest) == 1);
	}
	/* ONE_CAB: a human changed alone, the golden bots kept. */
	for (int16_t human = 1; human < 8; human++)
	{
		plan = oneCab;
		plan.characterIDs[0] = human;
		CHECK(ExpectPlanReject(&plan) == 0);
	}
	/* ONE_CAB: the right bots in another order, in both the slots and expectedBots. */
	plan = oneCab;
	plan.characterIDs[1] = 2;
	plan.characterIDs[2] = 1;
	plan.expectedBots[0] = 2u;
	plan.expectedBots[1] = 1u;
	CHECK(ExpectPlanReject(&plan) == 0);
	/* ONE_CAB: a human outside 0..255, a byte-equal alias, or no base character. */
	{
		static const int16_t offHuman[] = { -1, 256, INT16_MIN, INT16_MAX, 8 };

		for (uint32_t i = 0; i < sizeof(offHuman) / sizeof(offHuman[0]); i++)
		{
			plan = oneCab;
			plan.characterIDs[0] = offHuman[i];
			CHECK(ExpectPlanReject(&plan) == 0);
		}
	}
	return 0;
}

/* Every mixed-profile or tampered shape field, on both profiles' golden plans (RS-22). */
static int TestMalformedProfilePlans(void)
{
	struct MainArcadeRaceSetupPlan twoCab;
	struct MainArcadeRaceSetupPlan oneCab;
	struct MainArcadeRaceSetupPlan plan;
	struct MainArcadeRaceSetupRetailFields fields;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	BuildGoldenPlan(&twoCab);
	BuildGoldenOneCabPlan(&oneCab);
	FreshBootFields(&fields);
	CHECK(MainArcadeRaceSetupPlan_Apply(&twoCab, &fields, &fields) == 1);
	CHECK(MainArcadeRaceSetupPlan_Apply(&oneCab, &fields, &fields) == 1);
	CHECK(MainArcadeRaceSetupPlan_Digest(&oneCab, digest) == 1);

	/* A profile with no shape. */
	for (uint32_t which = 0; which < 2u; which++)
	{
		static const uint32_t profiles[] = { 0u, 3u, 0xFFFFFFFFu };

		for (uint32_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++)
		{
			plan = (which == 0u) ? twoCab : oneCab;
			plan.profile = profiles[i];
			CHECK(ExpectPlanReject(&plan) == 0);
		}
	}

	/* The other profile's tag on otherwise unchanged fields. */
	plan = twoCab;
	plan.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB;
	CHECK(ExpectPlanReject(&plan) == 0);

	/* ONE_CAB with one TWO_CAB shape field each. */
	plan = oneCab;
	plan.characterWriteMask = 0x3fu;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.numPlyrNextGame = 2u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.firstBotSlot = 2u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.botCount = 4u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.aiSetIndex = 0u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.aiSetIndex = 6u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.numPlyrNextGame = 0u;
	CHECK(ExpectPlanReject(&plan) == 0);
	/* ... every TWO_CAB shape field at once, over ONE_CAB characters. */
	plan = oneCab;
	plan.numPlyrNextGame = 2u;
	plan.characterWriteMask = 0x3fu;
	plan.firstBotSlot = 2u;
	plan.botCount = 4u;
	plan.aiSetIndex = 0u;
	CHECK(ExpectPlanReject(&plan) == 0);

	/* TWO_CAB with one ONE_CAB shape field each. */
	plan = twoCab;
	plan.characterWriteMask = 0xffu;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = twoCab;
	plan.numPlyrNextGame = 1u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = twoCab;
	plan.firstBotSlot = 1u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = twoCab;
	plan.botCount = 7u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = twoCab;
	plan.aiSetIndex = 0xffu;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = twoCab;
	plan.aiSetIndex = (uint8_t)MAIN_ARCADE_RACE_SETUP_AI_SET_COUNT;
	CHECK(ExpectPlanReject(&plan) == 0);
	/* Only the humans' own AI set passes: humans 0 and 1 hold set 0 (bots 6, 4, 2, 3). */
	for (uint8_t set = 0; set < MAIN_ARCADE_RACE_SETUP_AI_SET_COUNT; set++)
	{
		plan = twoCab;
		plan.aiSetIndex = set;
		if (set == 0u)
		{
			FreshBootFields(&fields);
			CHECK(MainArcadeRaceSetupPlan_Apply(&plan, &fields, &fields) == 1);
			CHECK(MainArcadeRaceSetupPlan_Digest(&plan, digest) == 1);
		}
		else
		{
			CHECK(ExpectPlanReject(&plan) == 0);
		}
	}

	/* A nonzero unused expectedBots entry (TWO_CAB's tail 4..6). */
	for (uint32_t i = NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i < NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT; i++)
	{
		plan = twoCab;
		plan.expectedBots[i] = 1u;
		CHECK(ExpectPlanReject(&plan) == 0);
	}
	/* An unowned TWO_CAB slot not 0. */
	plan = twoCab;
	plan.characterIDs[6] = 5;
	CHECK(ExpectPlanReject(&plan) == 0);

	/* ONE_CAB: each bot slot differs from expectedBots, either side. */
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT; i++)
	{
		plan = oneCab;
		plan.characterIDs[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i] =
			(int16_t)(plan.characterIDs[NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT + i] == 0 ? 1 : 0);
		CHECK(ExpectPlanReject(&plan) == 0);
		plan = oneCab;
		plan.expectedBots[i] = (uint8_t)(plan.expectedBots[i] == 0u ? 1u : 0u);
		CHECK(ExpectPlanReject(&plan) == 0);
	}
	plan = oneCab;
	plan.characterIDs[7] = (int16_t)(plan.expectedBots[6] + 256); /* same low byte */
	CHECK(ExpectPlanReject(&plan) == 0);

	/* ONE_CAB: the shared policy fields too. */
	plan = oneCab;
	plan.locked = 0u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.boolDemoMode = 1u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.reserved[0] = 1u;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.gameMode1ClearMask &= ~MAIN_ARCADE_RACE_SETUP_GM1_P1_VIBRATE;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.gameMode2SetMask = MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBO;
	CHECK(ExpectPlanReject(&plan) == 0);
	plan = oneCab;
	plan.arcadeDifficulty = 0xa1;
	CHECK(ExpectPlanReject(&plan) == 0);
	return 0;
}

static int TestDigest(void)
{
	struct MainArcadeRaceSetupPlan golden;
	struct MainArcadeRaceSetupPlan plan;
	struct NativeSha256 sha;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reference[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t changed[NATIVE_SHA256_DIGEST_BYTES];

	CHECK(sizeof(k_goldenEncoding) == 135u);
	CHECK(memcmp(k_goldenEncoding, MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG, 30u) == 0);

	/* The golden digest, and SHA-256 of the golden bytes via NativeSha256. */
	BuildGoldenPlan(&golden);
	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(MainArcadeRaceSetupPlan_Digest(&golden, digest) == 1);
	CHECK(memcmp(digest, k_goldenDigest, sizeof(digest)) == 0);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_goldenEncoding, sizeof(k_goldenEncoding));
	NativeSha256_Final(&sha, reference);
	CHECK(memcmp(reference, k_goldenDigest, sizeof(reference)) == 0);
	CHECK(MainArcadeRaceSetupPlan_Digest(&golden, reference) == 1);
	CHECK(memcmp(reference, digest, sizeof(digest)) == 0);

	/* One changed field, or one whole roster (humans, bots, and AI set, which
	 * are tied), changes the digest. */
	for (uint32_t field = 0; field < 11u; field++)
	{
		plan = golden;
		switch (field)
		{
		case 0: plan.numLaps = 5; break;
		case 1: plan.levelID = 6; break;
		case 2: plan.arcadeDifficulty = 0x50; break;
		case 3: CHECK(SetTwoCabRoster(&plan, 6u, 4u) == 1); CHECK(plan.aiSetIndex == 4u); break;
		case 4: CHECK(SetTwoCabRoster(&plan, 0u, 7u) == 1); CHECK(plan.aiSetIndex == 0u); break;
		case 5: CHECK(SetTwoCabRoster(&plan, 1u, 0u) == 1); CHECK(plan.aiSetIndex == 0u); break;
		case 6: CHECK(SetTwoCabRoster(&plan, 3u, 6u) == 1); CHECK(plan.aiSetIndex == 6u); break;
		case 7: plan.masterSeed ^= UINT64_C(0x8000000000000000); break;
		case 8: plan.rngDerivationVersion = 2; break;
		case 9: plan.configDigest[31] ^= 0x01u; break;
		default: plan.configDigest[0] ^= 0x80u; break;
		}
		CHECK(MainArcadeRaceSetupPlan_Digest(&plan, changed) == 1);
		CHECK(memcmp(changed, k_goldenDigest, sizeof(changed)) != 0);
	}
	/* A roster field changed alone no longer digests (the bots are tied to the humans). */
	for (uint32_t field = 0; field < 4u; field++)
	{
		plan = golden;
		switch (field)
		{
		case 0: plan.aiSetIndex = 1; break;
		case 1: plan.characterIDs[1] = 6; break; /* humans 0, 6: set 4, not the golden bots */
		case 2: plan.characterIDs[0] = -3; break;
		default: plan.expectedBots[3] = 5; plan.characterIDs[5] = 5; break;
		}
		CHECK(ExpectDigestReject(&plan) == 0);
	}

	/* The ONE_CAB golden, the same way. */
	CHECK(sizeof(k_goldenOneCabEncoding) == 135u);
	CHECK(memcmp(k_goldenOneCabEncoding, MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG, 30u) == 0);
	BuildGoldenOneCabPlan(&golden);
	memset(digest, SENTINEL_BYTE, sizeof(digest));
	CHECK(MainArcadeRaceSetupPlan_Digest(&golden, digest) == 1);
	CHECK(memcmp(digest, k_goldenOneCabDigest, sizeof(digest)) == 0);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_goldenOneCabEncoding, sizeof(k_goldenOneCabEncoding));
	NativeSha256_Final(&sha, reference);
	CHECK(memcmp(reference, k_goldenOneCabDigest, sizeof(reference)) == 0);
	CHECK(memcmp(k_goldenOneCabDigest, k_goldenDigest, sizeof(k_goldenDigest)) != 0);
	for (uint32_t field = 0; field < 6u; field++)
	{
		plan = golden;
		switch (field)
		{
		case 0: plan.numLaps = 5; break;
		case 1: plan.levelID = 6; break;
		case 2: plan.arcadeDifficulty = 0xf0; break;
		case 3: CHECK(SetOneCabRoster(&plan, 3u) == 1); break; /* human 3: bots 0, 1, 2, 4, 5, 6, 7 */
		case 4: plan.masterSeed ^= 1u; break;
		default:
			/* Human 7: LOAD_Robots1P gives 0..6. */
			plan.characterIDs[0] = 7;
			for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT; i++)
			{
				plan.expectedBots[i] = (uint8_t)i;
				plan.characterIDs[1u + i] = (int16_t)i;
			}
			break;
		}
		CHECK(MainArcadeRaceSetupPlan_Digest(&plan, changed) == 1);
		CHECK(memcmp(changed, k_goldenOneCabDigest, sizeof(changed)) != 0);
	}
	/* The human changed alone (Crash's bots kept) no longer digests. */
	plan = golden;
	plan.characterIDs[0] = 7;
	CHECK(ExpectDigestReject(&plan) == 0);
	return 0;
}

int main(void)
{
	CHECK(TestPolicyConstants() == 0);
	CHECK(TestBuild() == 0);
	CHECK(TestBuildRejects() == 0);
	CHECK(TestBuildRejectsOneCab() == 0);
	CHECK(TestBuildOneCab() == 0);
	CHECK(TestApply() == 0);
	CHECK(TestApplyOneCab() == 0);
	CHECK(TestMalformedPlans() == 0);
	CHECK(TestMalformedProfilePlans() == 0);
	CHECK(TestBotsTiedToHumans() == 0);
	CHECK(TestDigest() == 0);
	puts("main_arcade_race_setup_plan_test: ok");
	return 0;
}
