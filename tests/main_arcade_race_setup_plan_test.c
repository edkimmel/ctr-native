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
 * The V1 encoding of k_goldenPlan, spelled out by hand from the header's
 * field order (not produced by the module). Frozen.
 */
static const uint8_t k_goldenEncoding[MAIN_ARCADE_RACE_SETUP_PLAN_V1_ENCODED_BYTES] = {
	/* 0: tag "CTRN arcade race setup plan v1" */
	0x43, 0x54, 0x52, 0x4e, 0x20, 0x61, 0x72, 0x63, 0x61, 0x64, 0x65, 0x20, 0x72, 0x61, 0x63,
	0x65, 0x20, 0x73, 0x65, 0x74, 0x75, 0x70, 0x20, 0x70, 0x6c, 0x61, 0x6e, 0x20, 0x76, 0x31,
	/* 30: locked 1, numPlyrNextGame 2, numLaps 3, boolDemoMode 0 */
	0x01, 0x02, 0x03, 0x00,
	/* 34: levelID 3 */
	0x03, 0x00, 0x00, 0x00,
	/* 38: gameMode1 clear 0x9F9FCFBF, set 0x00400000 */
	0xbf, 0xcf, 0x9f, 0x9f, 0x00, 0x00, 0x40, 0x00,
	/* 46: gameMode2 clear 0xFFFFFE5F, set 0 */
	0x5f, 0xfe, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
	/* 54: arcadeDifficulty 0xA0 */
	0xa0, 0x00, 0x00, 0x00,
	/* 58: characterWriteMask 0x3F, aiSetIndex 0, reserved 0, 0 */
	0x3f, 0x00, 0x00, 0x00,
	/* 62: characterIDs 0, 1, 6, 4, 2, 3, 0, 0 (u16 each) */
	0x00, 0x00, 0x01, 0x00, 0x06, 0x00, 0x04, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 78: expectedBots 6, 4, 2, 3 */
	0x06, 0x04, 0x02, 0x03,
	/* 82: masterSeed 0x0123456789abcdef */
	0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
	/* 90: rngDerivationVersion 1 */
	0x01, 0x00, 0x00, 0x00,
	/* 94: configDigest 0xc0..0xdf */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
};

/*
 * SHA-256 of k_goldenEncoding. Obtained independently of this module: the
 * 126 bytes above were written to a file field by field with the shell's
 * printf and hashed with both `sha256sum` and `certutil -hashfile <file>
 * SHA256` (identical results); the same script with the previous gameMode1
 * clear byte reproduced the previous golden digest. The file was not
 * committed. Frozen.
 */
static const uint8_t k_goldenDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0xeb, 0x8e, 0xac, 0x76, 0xc4, 0x05, 0xa1, 0xea, 0x87, 0x61, 0x9a, 0xa4, 0x01, 0x02, 0x19, 0x05,
	0x40, 0xb2, 0xd6, 0xcd, 0x26, 0x3d, 0x87, 0x76, 0x88, 0x85, 0x86, 0x4b, 0xd2, 0x47, 0xeb, 0x55,
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

/* The hand-built plan k_goldenEncoding encodes. */
static void BuildGoldenPlan(struct MainArcadeRaceSetupPlan *plan)
{
	static const int16_t characters[MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT] = { 0, 1, 6, 4, 2, 3, 0, 0 };
	static const uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT] = { 6, 4, 2, 3 };

	memset(plan, 0, sizeof(*plan));
	plan->locked = 1;
	plan->numPlyrNextGame = 2;
	plan->numLaps = 3;
	plan->boolDemoMode = 0;
	plan->levelID = 3;
	plan->gameMode1ClearMask = UINT32_C(0x9F9FCFBF);
	plan->gameMode1SetMask = UINT32_C(0x00400000);
	plan->gameMode2ClearMask = UINT32_C(0xFFFFFE5F);
	plan->gameMode2SetMask = 0;
	plan->arcadeDifficulty = 0xa0;
	plan->characterWriteMask = 0x3f;
	plan->aiSetIndex = 0;
	memcpy(plan->characterIDs, characters, sizeof(characters));
	memcpy(plan->expectedBots, bots, sizeof(bots));
	plan->masterSeed = GOLDEN_MASTER_SEED;
	plan->rngDerivationVersion = 1;
	FillCounting(plan->configDigest, sizeof(plan->configDigest), 0xc0u);
}

static int PlansEqual(const struct MainArcadeRaceSetupPlan *a, const struct MainArcadeRaceSetupPlan *b)
{
	return (a->locked == b->locked) && (a->numPlyrNextGame == b->numPlyrNextGame) && (a->numLaps == b->numLaps) &&
	       (a->boolDemoMode == b->boolDemoMode) && (a->levelID == b->levelID) &&
	       (a->gameMode1ClearMask == b->gameMode1ClearMask) && (a->gameMode1SetMask == b->gameMode1SetMask) &&
	       (a->gameMode2ClearMask == b->gameMode2ClearMask) && (a->gameMode2SetMask == b->gameMode2SetMask) &&
	       (a->arcadeDifficulty == b->arcadeDifficulty) && (a->characterWriteMask == b->characterWriteMask) &&
	       (a->aiSetIndex == b->aiSetIndex) && (memcmp(a->reserved, b->reserved, sizeof(a->reserved)) == 0) &&
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
	CHECK(sizeof(MAIN_ARCADE_RACE_SETUP_PLAN_V1_TAG) - 1u == 30u);
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

	/* A well-formed ONE_CAB config: valid under the bot rules, but the plan is TWO_CAB-only for now. */
	CHECK(BuildOneCab(&valid, 2u, NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD, &config) == 1);
	CHECK(NativeMatchConfigV1_Validate(&config) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&config) == 1);
	CHECK(ExpectBuildReject(&config) == 0);
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

static int TestDigest(void)
{
	struct MainArcadeRaceSetupPlan golden;
	struct MainArcadeRaceSetupPlan plan;
	struct NativeSha256 sha;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reference[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t changed[NATIVE_SHA256_DIGEST_BYTES];

	CHECK(sizeof(k_goldenEncoding) == 126u);
	CHECK(memcmp(k_goldenEncoding, MAIN_ARCADE_RACE_SETUP_PLAN_V1_TAG, 30u) == 0);

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

	/* One changed field changes the digest. */
	for (uint32_t field = 0; field < 11u; field++)
	{
		plan = golden;
		switch (field)
		{
		case 0: plan.numLaps = 5; break;
		case 1: plan.levelID = 6; break;
		case 2: plan.arcadeDifficulty = 0x50; break;
		case 3: plan.aiSetIndex = 1; break;
		case 4: plan.characterIDs[1] = 7; break;
		case 5: plan.characterIDs[0] = -3; break;
		case 6: plan.expectedBots[3] = 5; plan.characterIDs[5] = 5; break;
		case 7: plan.masterSeed ^= UINT64_C(0x8000000000000000); break;
		case 8: plan.rngDerivationVersion = 2; break;
		case 9: plan.configDigest[31] ^= 0x01u; break;
		default: plan.configDigest[0] ^= 0x80u; break;
		}
		CHECK(MainArcadeRaceSetupPlan_Digest(&plan, changed) == 1);
		CHECK(memcmp(changed, k_goldenDigest, sizeof(changed)) != 0);
	}
	return 0;
}

int main(void)
{
	CHECK(TestPolicyConstants() == 0);
	CHECK(TestBuild() == 0);
	CHECK(TestBuildRejects() == 0);
	CHECK(TestApply() == 0);
	CHECK(TestMalformedPlans() == 0);
	CHECK(TestDigest() == 0);
	puts("main_arcade_race_setup_plan_test: ok");
	return 0;
}
