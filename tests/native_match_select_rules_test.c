#include "platform/native_match_select_rules.h"

#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SENTINEL_BYTE 0xa5u
#define FIXTURE_SEED UINT64_C(0x4354524e41524331) /* "CTRNARC1" */

/*
 * Frozen golden values, computed independently with sha256sum over the byte
 * strings spelled out beside each one. Frozen: changing any of them changes
 * every agreed match.
 */

/* SHA-256("CTRN match select seed v1" || 00 01 .. 1f || 02 ||
 * 08 07 06 05 04 03 02 01 || 18 17 16 15 14 13 12 11), word 0 LE. */
static const uint64_t k_goldenSeed = UINT64_C(0xd31b73ec85c0614c);

/* SHA-256("CTRN match select draw v1" || 01 || ef cd ab 89 67 45 23 01), word 0 LE
 * = 0x77d0dc2d0b60db2b; and with domain 02 = 0x7481f821719cc173. */
#define GOLDEN_DRAW_SEED UINT64_C(0x0123456789abcdef)
static const uint32_t k_goldenDrawCounts[4] = { 2u, 3u, 16u, UINT32_C(0xffffffff) };
static const uint32_t k_goldenTrackDraw[4] = { 1u, 1u, 11u, UINT32_C(2201073496) };
static const uint32_t k_goldenLapsDraw[4] = { 1u, 2u, 3u, UINT32_C(3860773268) };

/* SHA-256("CTRN match select outcome v1" || 00 01 .. 1f || ef cd ab 89 67 45 23 01 ||
 * 03 05 02 04 01 00 02 00 || 00 01 00 00 || 06 04 02 03 00 00 00 00). */
static const uint8_t k_goldenOutcomeDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0xe4, 0xaa, 0x87, 0xad, 0x06, 0xa4, 0xf3, 0x37, 0x21, 0x84, 0x48, 0x8b, 0x0f, 0xd8, 0xfb, 0x8f,
	0xf1, 0x74, 0xdc, 0x5e, 0x91, 0x35, 0x77, 0xcf, 0x91, 0xb8, 0xf7, 0xbf, 0x84, 0xd4, 0x1a, 0x75,
};

static const uint8_t k_expectedTracks[NATIVE_MATCH_SELECT_TRACK_COUNT] = { 3, 6, 4, 14, 9, 2, 8, 0, 5, 1, 12, 10, 15, 7, 11, 16 };
static const uint8_t k_expectedLaps[NATIVE_MATCH_SELECT_LAP_OPTION_COUNT] = { 3, 5, 7 };
static const uint8_t k_expectedAiSets[NATIVE_MATCH_SELECT_AI_SET_COUNT][NATIVE_MATCH_SELECT_AI_SET_RACERS] = {
	{ 6, 4, 2, 3 }, { 0, 6, 3, 5 }, { 0, 6, 1, 2 }, { 0, 6, 4, 7 }, { 1, 2, 3, 5 }, { 4, 7, 3, 5 }, { 4, 7, 1, 2 },
};

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

/*
 * A hand-built two-cabinet base (not the arcade-link fixture): characters
 * 0..5 in slots 0..5, difficulty 0, and a counting-pattern botRulesDigest.
 */
static void BuildTwoCabBase(struct NativeMatchConfigV1 *config)
{
	memset(config, 0, sizeof(*config));
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = 3;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = FIXTURE_SEED;
	FillCounting(config->buildIdentity, sizeof(config->buildIdentity), 0x40u);
	FillCounting(config->contentIdentity, sizeof(config->contentIdentity), 0x80u);
	FillCounting(config->botRulesDigest, sizeof(config->botRulesDigest), 0xc0u);
	for (uint32_t i = 0; i <= 5u; i++)
	{
		config->slots[i].characterID = (uint8_t)i;
		config->slots[i].difficulty = 0;
	}
}

static void BuildOneCabBase(struct NativeMatchConfigV1 *config)
{
	memset(config, 0, sizeof(*config));
	NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = 3;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = FIXTURE_SEED;
	FillCounting(config->buildIdentity, sizeof(config->buildIdentity), 0x40u);
	FillCounting(config->contentIdentity, sizeof(config->contentIdentity), 0x80u);
	FillCounting(config->botRulesDigest, sizeof(config->botRulesDigest), 0xc0u);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config->slots[i].characterID = (uint8_t)i;
	}
}

static void SetChoice(struct NativeMatchSelectChoice *choice, uint8_t characterID, uint8_t trackID, uint8_t lapCount,
	uint64_t nonce)
{
	memset(choice, 0, sizeof(*choice));
	choice->characterID = characterID;
	choice->trackID = trackID;
	choice->lapCount = lapCount;
	choice->nonce = nonce;
}

static uint64_t LoadLe64(const uint8_t *bytes)
{
	uint64_t value = 0;

	for (uint32_t i = 0; i < 8u; i++)
	{
		value |= (uint64_t)bytes[i] << (8u * i);
	}
	return value;
}

/* Independent reference for the seed hash: the whole message assembled by hand. */
static void ReferenceSeedDigest(const uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES], uint32_t humanCount,
	const uint64_t nonces[], uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	static const char tag[] = "CTRN match select seed v1";
	uint8_t message[sizeof(tag) - 1u + NATIVE_SHA256_DIGEST_BYTES + 1u + (8u * NATIVE_MATCH_SELECT_MAX_HUMANS)];
	size_t size = 0;
	struct NativeSha256 sha;

	memcpy(message, tag, sizeof(tag) - 1u);
	size += sizeof(tag) - 1u;
	memcpy(&message[size], baseDigest, NATIVE_SHA256_DIGEST_BYTES);
	size += NATIVE_SHA256_DIGEST_BYTES;
	message[size++] = (uint8_t)humanCount;
	for (uint32_t h = 0; h < humanCount; h++)
	{
		for (uint32_t b = 0; b < 8u; b++)
		{
			message[size++] = (uint8_t)(nonces[h] >> (8u * b));
		}
	}
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, message, size);
	NativeSha256_Final(&sha, digest);
}

static int TestTables(void)
{
	uint32_t index = 0;

	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_CHARACTER_COUNT; i++)
	{
		CHECK(NativeMatchSelect_CharacterAt(i) == i);
		index = 99u;
		CHECK(NativeMatchSelect_CharacterIndex((uint8_t)i, &index) == 1);
		CHECK(index == i);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_TRACK_COUNT; i++)
	{
		CHECK(NativeMatchSelect_TrackAt(i) == k_expectedTracks[i]);
		index = 99u;
		CHECK(NativeMatchSelect_TrackIndex(k_expectedTracks[i], &index) == 1);
		CHECK(index == i);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_LAP_OPTION_COUNT; i++)
	{
		CHECK(NativeMatchSelect_LapOptionAt(i) == k_expectedLaps[i]);
		index = 99u;
		CHECK(NativeMatchSelect_LapOptionIndex(k_expectedLaps[i], &index) == 1);
		CHECK(index == i);
	}
	for (uint32_t s = 0; s < NATIVE_MATCH_SELECT_AI_SET_COUNT; s++)
	{
		for (uint32_t r = 0; r < NATIVE_MATCH_SELECT_AI_SET_RACERS; r++)
		{
			CHECK(NativeMatchSelect_AiSetRacer(s, r) == k_expectedAiSets[s][r]);
		}
		CHECK(NativeMatchSelect_AiSetRacer(s, NATIVE_MATCH_SELECT_AI_SET_RACERS) == 0xffu);
	}

	/* Out of range. */
	CHECK(NativeMatchSelect_CharacterAt(NATIVE_MATCH_SELECT_CHARACTER_COUNT) == 0xffu);
	CHECK(NativeMatchSelect_TrackAt(NATIVE_MATCH_SELECT_TRACK_COUNT) == 0xffu);
	CHECK(NativeMatchSelect_LapOptionAt(NATIVE_MATCH_SELECT_LAP_OPTION_COUNT) == 0xffu);
	CHECK(NativeMatchSelect_AiSetRacer(NATIVE_MATCH_SELECT_AI_SET_COUNT, 0) == 0xffu);
	CHECK(NativeMatchSelect_CharacterAt(UINT32_MAX) == 0xffu);
	CHECK(NativeMatchSelect_TrackAt(UINT32_MAX) == 0xffu);
	CHECK(NativeMatchSelect_LapOptionAt(UINT32_MAX) == 0xffu);
	CHECK(NativeMatchSelect_AiSetRacer(UINT32_MAX, UINT32_MAX) == 0xffu);

	/* Non-members are rejected with *index untouched. */
	index = 0xa5a5a5a5u;
	CHECK(NativeMatchSelect_TrackIndex(13, &index) == 0); /* OXIDE_STATION, 1P only */
	CHECK(NativeMatchSelect_TrackIndex(17, &index) == 0); /* TURBO_TRACK, save unlock */
	CHECK(NativeMatchSelect_TrackIndex(18, &index) == 0); /* NITRO_COURT, battle */
	CHECK(NativeMatchSelect_TrackIndex(0xff, &index) == 0);
	CHECK(NativeMatchSelect_CharacterIndex(8, &index) == 0);
	CHECK(NativeMatchSelect_CharacterIndex(15, &index) == 0); /* NITROS_OXIDE */
	CHECK(NativeMatchSelect_CharacterIndex(0xff, &index) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(0, &index) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(1, &index) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(2, &index) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(4, &index) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(6, &index) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(8, &index) == 0);
	CHECK(index == 0xa5a5a5a5u);

	/* NULL index. */
	CHECK(NativeMatchSelect_CharacterIndex(0, NULL) == 0);
	CHECK(NativeMatchSelect_TrackIndex(3, NULL) == 0);
	CHECK(NativeMatchSelect_LapOptionIndex(3, NULL) == 0);
	return 0;
}

static int TestDeriveSeed(void)
{
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reference[NATIVE_SHA256_DIGEST_BYTES];
	uint64_t nonces[NATIVE_MATCH_SELECT_MAX_HUMANS] = {
		UINT64_C(0x0102030405060708), UINT64_C(0x1112131415161718), UINT64_C(0x2122232425262728),
		UINT64_C(0x3132333435363738),
	};
	uint64_t seed = 0;
	uint64_t other = 0;
	uint64_t sentinel;

	FillCounting(digest, sizeof(digest), 0);

	/* Golden. */
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 2, nonces, &seed) == 1);
	CHECK(seed == k_goldenSeed);
	CHECK(seed != 0);
	CHECK(seed != FIXTURE_SEED);

	/* Deterministic. */
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 2, nonces, &other) == 1);
	CHECK(other == seed);

	/* Matches the hand-assembled reference for every humanCount. */
	for (uint32_t count = 1; count <= NATIVE_MATCH_SELECT_MAX_HUMANS; count++)
	{
		ReferenceSeedDigest(digest, count, nonces, reference);
		CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, count, nonces, &other) == 1);
		CHECK(other == LoadLe64(reference));
	}

	/* A word equal to the base seed is skipped for the next word. */
	ReferenceSeedDigest(digest, 2, nonces, reference);
	CHECK(NativeMatchSelect_DeriveSeed(digest, seed, 2, nonces, &other) == 1);
	CHECK(other == LoadLe64(&reference[8]));
	CHECK(other != seed);
	CHECK(other != 0);

	/* Changes with every nonce, humanCount, and baseDigest. */
	for (uint32_t h = 0; h < 2u; h++)
	{
		nonces[h] ^= 1u;
		CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 2, nonces, &other) == 1);
		CHECK(other != seed);
		nonces[h] ^= 1u;
	}
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 3, nonces, &other) == 1);
	CHECK(other != seed);
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 1, nonces, &other) == 1);
	CHECK(other != seed);
	for (uint32_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		digest[i] ^= 0x80u;
		CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 2, nonces, &other) == 1);
		CHECK(other != seed);
		digest[i] ^= 0x80u;
	}
	/* The base seed does not feed the hash; it only excludes a word. */
	CHECK(NativeMatchSelect_DeriveSeed(digest, 0, 2, nonces, &other) == 1);
	CHECK(other == seed);

	/* Invalid inputs leave *seedOut untouched. */
	memset(&sentinel, SENTINEL_BYTE, sizeof(sentinel));
	other = sentinel;
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 0, nonces, &other) == 0);
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 5, nonces, &other) == 0);
	CHECK(NativeMatchSelect_DeriveSeed(NULL, FIXTURE_SEED, 2, nonces, &other) == 0);
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 2, NULL, &other) == 0);
	CHECK(other == sentinel);
	CHECK(NativeMatchSelect_DeriveSeed(digest, FIXTURE_SEED, 2, nonces, NULL) == 0);
	return 0;
}

static int TestDraw(void)
{
	uint32_t index = 0;
	uint32_t sentinel;
	int differ = 0;

	/* Golden. */
	for (uint32_t i = 0; i < 4u; i++)
	{
		CHECK(NativeMatchSelect_Draw(GOLDEN_DRAW_SEED, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, k_goldenDrawCounts[i], &index) == 1);
		CHECK(index == k_goldenTrackDraw[i]);
		CHECK(NativeMatchSelect_Draw(GOLDEN_DRAW_SEED, NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS, k_goldenDrawCounts[i], &index) == 1);
		CHECK(index == k_goldenLapsDraw[i]);
	}

	/* Range, and domain separation. */
	for (uint64_t seed = 1; seed <= 64u; seed++)
	{
		uint32_t track = 0;
		uint32_t laps = 0;

		for (uint32_t count = 1; count <= 17u; count++)
		{
			CHECK(NativeMatchSelect_Draw(seed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, count, &index) == 1);
			CHECK(index < count);
			CHECK(NativeMatchSelect_Draw(seed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS, count, &index) == 1);
			CHECK(index < count);
		}
		CHECK(NativeMatchSelect_Draw(seed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, 1, &index) == 1);
		CHECK(index == 0);
		CHECK(NativeMatchSelect_Draw(seed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, 16, &track) == 1);
		CHECK(NativeMatchSelect_Draw(seed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS, 16, &laps) == 1);
		if (track != laps)
		{
			differ = 1;
		}
	}
	CHECK(differ == 1);

	/* Invalid inputs leave *indexOut untouched. */
	memset(&sentinel, SENTINEL_BYTE, sizeof(sentinel));
	index = sentinel;
	CHECK(NativeMatchSelect_Draw(1, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, 0, &index) == 0);
	CHECK(NativeMatchSelect_Draw(1, 0, 2, &index) == 0);
	CHECK(NativeMatchSelect_Draw(1, 3, 2, &index) == 0);
	CHECK(NativeMatchSelect_Draw(1, 0xff, 2, &index) == 0);
	CHECK(index == sentinel);
	CHECK(NativeMatchSelect_Draw(1, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, 2, NULL) == 0);
	return 0;
}

static int ResolveSeedFor(const struct NativeMatchConfigV1 *base, uint32_t humanCount,
	const struct NativeMatchSelectChoice choices[], uint64_t *seed)
{
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint64_t nonces[NATIVE_MATCH_SELECT_MAX_HUMANS];

	for (uint32_t h = 0; h < humanCount; h++)
	{
		nonces[h] = choices[h].nonce;
	}
	return NativeMatchConfigV1_Digest(base, digest) &&
	       NativeMatchSelect_DeriveSeed(digest, base->masterSeed, humanCount, nonces, seed);
}

static int TestVotes(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectChoice choices[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome;
	struct NativeMatchSelectOutcome swapped;
	uint32_t trackWins[2] = { 0, 0 };
	uint32_t lapWins[2] = { 0, 0 };
	uint64_t seed = 0;

	BuildTwoCabBase(&base);

	/* Agreement: no draw. */
	SetChoice(&choices[0], 0, 9, 5, 11);
	SetChoice(&choices[1], 1, 9, 5, 22);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.trackID == 9);
	CHECK(outcome.lapCount == 5);
	CHECK(outcome.trackDrawn == 0);
	CHECK(outcome.lapsDrawn == 0);
	CHECK(outcome.humanCount == 2);
	CHECK(ResolveSeedFor(&base, 2, choices, &seed) == 1);
	CHECK(outcome.masterSeed == seed);
	CHECK(outcome.masterSeed != base.masterSeed);

	/* Two humans disagree on both: a draw between the two votes, fair over 64 nonces. */
	for (uint64_t n = 0; n < 64u; n++)
	{
		uint32_t pick = 0;

		SetChoice(&choices[0], 0, 6, 7, n);
		SetChoice(&choices[1], 1, 3, 3, UINT64_C(0x5555));
		CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
		CHECK(outcome.trackDrawn == 1);
		CHECK(outcome.lapsDrawn == 1);
		CHECK((outcome.trackID == 3) || (outcome.trackID == 6));
		CHECK((outcome.lapCount == 3) || (outcome.lapCount == 7));
		trackWins[outcome.trackID == 3 ? 0 : 1]++;
		lapWins[outcome.lapCount == 3 ? 0 : 1]++;

		/* Candidates are in table order: CRASH_COVE (3) then ROO_TUBES (6); laps 3 then 7. */
		CHECK(NativeMatchSelect_Draw(outcome.masterSeed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, 2, &pick) == 1);
		CHECK(outcome.trackID == (pick == 0 ? 3 : 6));
		CHECK(NativeMatchSelect_Draw(outcome.masterSeed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS, 2, &pick) == 1);
		CHECK(outcome.lapCount == (pick == 0 ? 3 : 7));

		/* Who voted for what does not matter, only the set of votes. */
		choices[0].trackID = 3;
		choices[0].lapCount = 3;
		choices[1].trackID = 6;
		choices[1].lapCount = 7;
		CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &swapped) == 1);
		CHECK(swapped.trackID == outcome.trackID);
		CHECK(swapped.lapCount == outcome.lapCount);
	}
	CHECK(trackWins[0] >= 10u);
	CHECK(trackWins[1] >= 10u);
	CHECK(lapWins[0] >= 10u);
	CHECK(lapWins[1] >= 10u);

	/* Three humans, 2-1: the majority, no draw. */
	SetChoice(&choices[0], 0, 16, 7, 1);
	SetChoice(&choices[1], 1, 2, 3, 2);
	SetChoice(&choices[2], 2, 16, 7, 3);
	CHECK(NativeMatchSelect_Resolve(&base, 3, choices, &outcome) == 1);
	CHECK(outcome.trackID == 16);
	CHECK(outcome.lapCount == 7);
	CHECK(outcome.trackDrawn == 0);
	CHECK(outcome.lapsDrawn == 0);

	/* Four humans, 2-2: one of the two tied, drawn. */
	for (uint64_t n = 0; n < 16u; n++)
	{
		SetChoice(&choices[0], 0, 14, 5, n);
		SetChoice(&choices[1], 1, 11, 3, 2);
		SetChoice(&choices[2], 2, 11, 3, 3);
		SetChoice(&choices[3], 3, 14, 5, 4);
		CHECK(NativeMatchSelect_Resolve(&base, 4, choices, &outcome) == 1);
		CHECK((outcome.trackID == 14) || (outcome.trackID == 11));
		CHECK((outcome.lapCount == 5) || (outcome.lapCount == 3));
		CHECK(outcome.trackDrawn == 1);
		CHECK(outcome.lapsDrawn == 1);
	}

	/* Four humans, 1-1-1-1 on tracks (one of four); 2-1-1 on laps (majority). */
	for (uint64_t n = 0; n < 16u; n++)
	{
		uint32_t pick = 0;
		/* Table order of 3, 6, 4, 0 is 3 (0), 6 (1), 4 (2), 0 (7). */
		static const uint8_t tableOrder[4] = { 3, 6, 4, 0 };

		SetChoice(&choices[0], 0, 0, 3, n);
		SetChoice(&choices[1], 1, 4, 5, 2);
		SetChoice(&choices[2], 2, 6, 7, 3);
		SetChoice(&choices[3], 3, 3, 5, 4);
		CHECK(NativeMatchSelect_Resolve(&base, 4, choices, &outcome) == 1);
		CHECK(outcome.trackDrawn == 1);
		CHECK(NativeMatchSelect_Draw(outcome.masterSeed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, 4, &pick) == 1);
		CHECK(outcome.trackID == tableOrder[pick]);
		CHECK(outcome.lapCount == 5);
		CHECK(outcome.lapsDrawn == 0);
	}

	/* Three humans, 1-1-1 laps: one of three. */
	SetChoice(&choices[0], 0, 3, 3, 1);
	SetChoice(&choices[1], 1, 3, 5, 2);
	SetChoice(&choices[2], 2, 3, 7, 3);
	CHECK(NativeMatchSelect_Resolve(&base, 3, choices, &outcome) == 1);
	CHECK(outcome.trackDrawn == 0);
	CHECK(outcome.lapsDrawn == 1);
	CHECK((outcome.lapCount == 3) || (outcome.lapCount == 5) || (outcome.lapCount == 7));

	/* One human: its own vote, never drawn. */
	SetChoice(&choices[0], 4, 12, 7, 9);
	CHECK(NativeMatchSelect_Resolve(&base, 1, choices, &outcome) == 1);
	CHECK(outcome.trackID == 12);
	CHECK(outcome.lapCount == 7);
	CHECK(outcome.trackDrawn == 0);
	CHECK(outcome.lapsDrawn == 0);
	return 0;
}

static int TestCharacters(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectChoice choices[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome;

	BuildTwoCabBase(&base);

	/* Distinct: kept. */
	SetChoice(&choices[0], 5, 3, 3, 1);
	SetChoice(&choices[1], 2, 3, 3, 2);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[0] == 5);
	CHECK(outcome.humanCharacter[1] == 2);
	CHECK(outcome.humanCharacter[2] == 0);
	CHECK(outcome.humanCharacter[3] == 0);
	CHECK(outcome.characterReassignedMask == 0);

	/* Two humans on one character: CAB2 gets the lowest free. */
	SetChoice(&choices[0], 0, 3, 3, 1);
	SetChoice(&choices[1], 0, 3, 3, 2);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[0] == 0);
	CHECK(outcome.humanCharacter[1] == 1);
	CHECK(outcome.characterReassignedMask == 0x2u);

	SetChoice(&choices[0], 3, 3, 3, 1);
	SetChoice(&choices[1], 3, 3, 3, 2);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[0] == 3);
	CHECK(outcome.humanCharacter[1] == 0);
	CHECK(outcome.characterReassignedMask == 0x2u);

	/* Four humans all on 3: humans 1..3 get 0, 1, 2. */
	for (uint32_t h = 0; h < 4u; h++)
	{
		SetChoice(&choices[h], 3, 3, 3, h);
	}
	CHECK(NativeMatchSelect_Resolve(&base, 4, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[0] == 3);
	CHECK(outcome.humanCharacter[1] == 0);
	CHECK(outcome.humanCharacter[2] == 1);
	CHECK(outcome.humanCharacter[3] == 2);
	CHECK(outcome.characterReassignedMask == 0xeu);

	/* A later human's original pick is already marked: 0, 0, 1 -> 0, 2, 1. */
	SetChoice(&choices[0], 0, 3, 3, 1);
	SetChoice(&choices[1], 0, 3, 3, 2);
	SetChoice(&choices[2], 1, 3, 3, 3);
	CHECK(NativeMatchSelect_Resolve(&base, 3, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[0] == 0);
	CHECK(outcome.humanCharacter[1] == 2);
	CHECK(outcome.humanCharacter[2] == 1);
	CHECK(outcome.characterReassignedMask == 0x2u);
	return 0;
}

static int TestBotsTwoHumansExhaustive(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;
	uint32_t pairs = 0;

	BuildTwoCabBase(&base);
	for (uint8_t a = 0; a < NATIVE_MATCH_SELECT_CHARACTER_COUNT; a++)
	{
		for (uint8_t b = 0; b < NATIVE_MATCH_SELECT_CHARACTER_COUNT; b++)
		{
			uint8_t finalB = b;
			uint8_t all[6];
			uint32_t expectedSet = NATIVE_MATCH_SELECT_AI_SET_COUNT;

			SetChoice(&choices[0], a, 3, 3, 100u + a);
			SetChoice(&choices[1], b, 3, 3, 200u + b);
			CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);

			if (a == b)
			{
				finalB = (a == 0) ? 1u : 0u;
			}
			CHECK(outcome.humanCharacter[0] == a);
			CHECK(outcome.humanCharacter[1] == finalB);
			CHECK(outcome.characterReassignedMask == ((a == b) ? 0x2u : 0u));

			for (uint32_t s = 0; s < NATIVE_MATCH_SELECT_AI_SET_COUNT; s++)
			{
				int holds = 0;

				for (uint32_t r = 0; r < NATIVE_MATCH_SELECT_AI_SET_RACERS; r++)
				{
					if ((k_expectedAiSets[s][r] == a) || (k_expectedAiSets[s][r] == finalB))
					{
						holds = 1;
					}
				}
				if (!holds)
				{
					expectedSet = s;
					break;
				}
			}
			CHECK(expectedSet < NATIVE_MATCH_SELECT_AI_SET_COUNT);
			CHECK(outcome.aiSetIndex == expectedSet);
			CHECK(outcome.botCount == 4);
			for (uint32_t r = 0; r < NATIVE_MATCH_SELECT_AI_SET_RACERS; r++)
			{
				CHECK(outcome.botCharacter[r] == k_expectedAiSets[expectedSet][r]);
			}
			for (uint32_t r = NATIVE_MATCH_SELECT_AI_SET_RACERS; r < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; r++)
			{
				CHECK(outcome.botCharacter[r] == 0);
			}

			all[0] = outcome.humanCharacter[0];
			all[1] = outcome.humanCharacter[1];
			memcpy(&all[2], outcome.botCharacter, 4u);
			for (uint32_t i = 0; i < 6u; i++)
			{
				CHECK(all[i] < NATIVE_MATCH_SELECT_CHARACTER_COUNT);
				for (uint32_t j = i + 1u; j < 6u; j++)
				{
					CHECK(all[i] != all[j]);
				}
			}
			pairs++;
		}
	}
	CHECK(pairs == 64u);

	/* Two spot checks against the retail sets by hand. */
	SetChoice(&choices[0], 0, 3, 3, 1);
	SetChoice(&choices[1], 1, 3, 3, 2);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.aiSetIndex == 0);
	SetChoice(&choices[0], 6, 3, 3, 1);
	SetChoice(&choices[1], 4, 3, 3, 2);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.aiSetIndex == 4);
	CHECK(outcome.botCharacter[0] == 1);
	CHECK(outcome.botCharacter[3] == 5);
	return 0;
}

static int TestBotsOtherHumanCounts(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 oneCab;
	struct NativeMatchSelectChoice choices[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome;

	BuildTwoCabBase(&base);

	/* Three humans 2, 2, 5 -> 2, 0, 5; bots ascending unpicked 1, 3, 4, 6. */
	SetChoice(&choices[0], 2, 3, 3, 1);
	SetChoice(&choices[1], 2, 3, 3, 2);
	SetChoice(&choices[2], 5, 3, 3, 3);
	CHECK(NativeMatchSelect_Resolve(&base, 3, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[1] == 0);
	CHECK(outcome.aiSetIndex == NATIVE_MATCH_SELECT_AI_SET_NONE);
	CHECK(outcome.botCount == 4);
	CHECK(outcome.botCharacter[0] == 1);
	CHECK(outcome.botCharacter[1] == 3);
	CHECK(outcome.botCharacter[2] == 4);
	CHECK(outcome.botCharacter[3] == 6);
	CHECK(outcome.botCharacter[4] == 0);

	/* Four humans 7, 6, 5, 4: bots 0, 1, 2, 3. */
	SetChoice(&choices[0], 7, 3, 3, 1);
	SetChoice(&choices[1], 6, 3, 3, 2);
	SetChoice(&choices[2], 5, 3, 3, 3);
	SetChoice(&choices[3], 4, 3, 3, 4);
	CHECK(NativeMatchSelect_Resolve(&base, 4, choices, &outcome) == 1);
	CHECK(outcome.humanCount == 4);
	CHECK(outcome.aiSetIndex == NATIVE_MATCH_SELECT_AI_SET_NONE);
	CHECK(outcome.botCount == 4);
	for (uint32_t i = 0; i < 4u; i++)
	{
		CHECK(outcome.botCharacter[i] == i);
	}

	/* One human on the two-cab base: ascending unpicked, no retail set. */
	SetChoice(&choices[0], 1, 3, 3, 1);
	CHECK(NativeMatchSelect_Resolve(&base, 1, choices, &outcome) == 1);
	CHECK(outcome.aiSetIndex == NATIVE_MATCH_SELECT_AI_SET_NONE);
	CHECK(outcome.botCharacter[0] == 0);
	CHECK(outcome.botCharacter[1] == 2);
	CHECK(outcome.botCharacter[2] == 3);
	CHECK(outcome.botCharacter[3] == 4);

	/* One-cab base: seven bots from the seven unpicked. */
	BuildOneCabBase(&oneCab);
	CHECK(NativeMatchConfigV1_Validate(&oneCab) == 1);
	SetChoice(&choices[0], 3, 3, 3, 1);
	CHECK(NativeMatchSelect_Resolve(&oneCab, 1, choices, &outcome) == 1);
	CHECK(outcome.botCount == 7);
	CHECK(outcome.botCharacter[0] == 0);
	CHECK(outcome.botCharacter[2] == 2);
	CHECK(outcome.botCharacter[3] == 4);
	CHECK(outcome.botCharacter[6] == 7);
	CHECK(outcome.botCharacter[7] == 0);
	return 0;
}

static int TestResolveRejects(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 invalid;
	struct NativeMatchConfigV1 oneCab;
	struct NativeMatchSelectChoice choices[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome;

	BuildTwoCabBase(&base);
	BuildOneCabBase(&oneCab);
	for (uint32_t h = 0; h < NATIVE_MATCH_SELECT_MAX_HUMANS; h++)
	{
		SetChoice(&choices[h], (uint8_t)h, 3, 3, h);
	}
	memset(&outcome, SENTINEL_BYTE, sizeof(outcome));

	CHECK(NativeMatchSelect_Resolve(NULL, 2, choices, &outcome) == 0);
	CHECK(NativeMatchSelect_Resolve(&base, 2, NULL, &outcome) == 0);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, NULL) == 0);
	CHECK(NativeMatchSelect_Resolve(&base, 0, choices, &outcome) == 0);
	CHECK(NativeMatchSelect_Resolve(&base, 5, choices, &outcome) == 0);

	invalid = base;
	invalid.lapCount = 0;
	CHECK(NativeMatchSelect_Resolve(&invalid, 2, choices, &outcome) == 0);
	invalid = base;
	memset(invalid.botRulesDigest, 0, sizeof(invalid.botRulesDigest));
	CHECK(NativeMatchSelect_Resolve(&invalid, 2, choices, &outcome) == 0);

	/* Choices outside the tables, checked on the second human. */
	choices[1].characterID = 8;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].characterID = 15;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].characterID = 1;
	choices[1].trackID = 13;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].trackID = 17;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].trackID = 18;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].trackID = 3;
	choices[1].lapCount = 4;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].lapCount = 1;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].lapCount = 0;
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
	choices[1].lapCount = 3;

	/* Every reserved byte must be zero. */
	for (uint32_t i = 0; i < sizeof(choices[1].reserved); i++)
	{
		choices[1].reserved[i] = 1;
		CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 0);
		choices[1].reserved[i] = 0;
	}

	/* Only the first humanCount choices are read: an invalid third choice is ignored for 2. */
	choices[2].characterID = 8;
	CHECK(IsAllByte(&outcome, sizeof(outcome), SENTINEL_BYTE));
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(NativeMatchSelect_Resolve(&base, 3, choices, &outcome) == 0);
	choices[2].characterID = 2;

	/* Not enough unpicked characters: two humans on the one-cab base need 7 of 6. */
	memset(&outcome, SENTINEL_BYTE, sizeof(outcome));
	CHECK(NativeMatchSelect_Resolve(&oneCab, 2, choices, &outcome) == 0);
	CHECK(IsAllByte(&outcome, sizeof(outcome), SENTINEL_BYTE));
	return 0;
}

static int TestOutcomeDigest(void)
{
	uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reference[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t changed[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeMatchSelectOutcome outcome;
	struct NativeMatchSelectOutcome mutated;
	uint8_t *fields[8];

	FillCounting(baseDigest, sizeof(baseDigest), 0);
	memset(&outcome, 0, sizeof(outcome));
	outcome.masterSeed = UINT64_C(0x0123456789abcdef);
	outcome.trackID = 3;
	outcome.lapCount = 5;
	outcome.humanCount = 2;
	outcome.botCount = 4;
	outcome.trackDrawn = 1;
	outcome.lapsDrawn = 0;
	outcome.characterReassignedMask = 2;
	outcome.aiSetIndex = 0;
	outcome.humanCharacter[0] = 0;
	outcome.humanCharacter[1] = 1;
	outcome.botCharacter[0] = 6;
	outcome.botCharacter[1] = 4;
	outcome.botCharacter[2] = 2;
	outcome.botCharacter[3] = 3;

	/* Golden. */
	CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &outcome, digest) == 1);
	CHECK(memcmp(digest, k_goldenOutcomeDigest, sizeof(digest)) == 0);
	memcpy(reference, digest, sizeof(reference));

	/* Every single field and array element changes it, in the first 8 bytes. */
	for (uint32_t b = 0; b < 64u; b++)
	{
		mutated = outcome;
		mutated.masterSeed ^= UINT64_C(1) << b;
		CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &mutated, changed) == 1);
		CHECK(memcmp(changed, reference, NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) != 0);
	}
	for (uint32_t f = 0; f < 8u; f++)
	{
		mutated = outcome;
		fields[0] = &mutated.trackID;
		fields[1] = &mutated.lapCount;
		fields[2] = &mutated.humanCount;
		fields[3] = &mutated.botCount;
		fields[4] = &mutated.trackDrawn;
		fields[5] = &mutated.lapsDrawn;
		fields[6] = &mutated.characterReassignedMask;
		fields[7] = &mutated.aiSetIndex;
		*fields[f] ^= 0x01u;
		CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &mutated, changed) == 1);
		CHECK(memcmp(changed, reference, NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) != 0);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_MAX_HUMANS; i++)
	{
		mutated = outcome;
		mutated.humanCharacter[i] ^= 0x01u;
		CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &mutated, changed) == 1);
		CHECK(memcmp(changed, reference, NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) != 0);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		mutated = outcome;
		mutated.botCharacter[i] ^= 0x01u;
		CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &mutated, changed) == 1);
		CHECK(memcmp(changed, reference, NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) != 0);
	}
	for (uint32_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		baseDigest[i] ^= 0x01u;
		CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &outcome, changed) == 1);
		CHECK(memcmp(changed, reference, NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) != 0);
		baseDigest[i] ^= 0x01u;
	}

	/* NULLs leave the digest untouched. */
	memset(changed, SENTINEL_BYTE, sizeof(changed));
	CHECK(NativeMatchSelect_OutcomeDigest(NULL, &outcome, changed) == 0);
	CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, NULL, changed) == 0);
	CHECK(IsAllByte(changed, sizeof(changed), SENTINEL_BYTE));
	CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &outcome, NULL) == 0);
	return 0;
}

/* BuildConfig must fail and leave *config byte-for-byte untouched. */
static int ExpectBuildFails(const struct NativeMatchConfigV1 *base, const struct NativeMatchSelectOutcome *outcome)
{
	struct NativeMatchConfigV1 built;

	memset(&built, SENTINEL_BYTE, sizeof(built));
	CHECK(NativeMatchSelect_BuildConfig(base, outcome, &built) == 0);
	CHECK(IsAllByte(&built, sizeof(built), SENTINEL_BYTE));
	return 0;
}

static int TestBuildConfig(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 oneCab;
	struct NativeMatchConfigV1 built;
	struct NativeMatchConfigV1 expected;
	struct NativeMatchSelectChoice choices[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome;
	struct NativeMatchSelectOutcome bad;
	uint8_t builtDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t expectedDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t slotIndex = 0;

	BuildTwoCabBase(&base);
	/* Give the slots distinct difficulties so a stray write would show. */
	for (uint32_t i = 0; i <= 5u; i++)
	{
		base.slots[i].difficulty = (uint8_t)(0x10u + i);
	}
	base.gameMode1 = UINT32_C(0x11223344);
	base.gameMode2 = UINT32_C(0x55667788);
	base.rules = UINT32_C(0x99aabbcc);
	CHECK(NativeMatchConfigV1_Validate(&base) == 1);

	SetChoice(&choices[0], 7, 16, 7, 41);
	SetChoice(&choices[1], 7, 16, 7, 42);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	CHECK(outcome.humanCharacter[0] == 7);
	CHECK(outcome.humanCharacter[1] == 0);

	memset(&built, SENTINEL_BYTE, sizeof(built));
	CHECK(NativeMatchSelect_BuildConfig(&base, &outcome, &built) == 1);
	CHECK(NativeMatchConfigV1_Validate(&built) == 1);
	CHECK(built.trackID == 16);
	CHECK(built.lapCount == 7);
	CHECK(built.masterSeed == outcome.masterSeed);
	CHECK(built.masterSeed != base.masterSeed);
	CHECK(built.slots[0].characterID == 7);
	CHECK(built.slots[1].characterID == 0);
	for (uint32_t i = 0; i < 4u; i++)
	{
		CHECK(built.slots[2u + i].characterID == outcome.botCharacter[i]);
	}

	/* Everything else is byte-identical to base. */
	CHECK(built.configurationVersion == base.configurationVersion);
	CHECK(built.profile == base.profile);
	CHECK(built.gameMode1 == base.gameMode1);
	CHECK(built.gameMode2 == base.gameMode2);
	CHECK(built.rules == base.rules);
	CHECK(built.tickRateNumerator == base.tickRateNumerator);
	CHECK(built.tickRateDenominator == base.tickRateDenominator);
	CHECK(built.rngDerivationVersion == base.rngDerivationVersion);
	CHECK(built.canonicalSchemaVersion == base.canonicalSchemaVersion);
	CHECK(built.replayFormatVersion == base.replayFormatVersion);
	CHECK(built.protocolVersion == base.protocolVersion);
	CHECK(memcmp(built.buildIdentity, base.buildIdentity, sizeof(base.buildIdentity)) == 0);
	CHECK(memcmp(built.contentIdentity, base.contentIdentity, sizeof(base.contentIdentity)) == 0);
	CHECK(memcmp(built.botRulesDigest, base.botRulesDigest, sizeof(base.botRulesDigest)) == 0);
	CHECK(memcmp(built.reserved, base.reserved, sizeof(base.reserved)) == 0);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		CHECK(built.slots[i].role == base.slots[i].role);
		CHECK(built.slots[i].initialLifecycle == base.slots[i].initialLifecycle);
		CHECK(built.slots[i].difficulty == base.slots[i].difficulty);
		CHECK(memcmp(built.slots[i].reserved, base.slots[i].reserved, sizeof(base.slots[i].reserved)) == 0);
		if (base.slots[i].role == NATIVE_MATCH_SLOT_ROLE_INACTIVE)
		{
			CHECK(built.slots[i].characterID == base.slots[i].characterID);
		}
	}
	expected = base;
	expected.trackID = 16;
	expected.lapCount = 7;
	expected.masterSeed = outcome.masterSeed;
	expected.slots[0].characterID = 7;
	expected.slots[1].characterID = 0;
	for (uint32_t i = 0; i < 4u; i++)
	{
		expected.slots[2u + i].characterID = outcome.botCharacter[i];
	}
	CHECK(NativeMatchConfigV1_Digest(&built, builtDigest) == 1);
	CHECK(NativeMatchConfigV1_Digest(&expected, expectedDigest) == 1);
	CHECK(memcmp(builtDigest, expectedDigest, sizeof(builtDigest)) == 0);

	/* Human h lands in the CAB1_HUMAN + h slot (human 0 is CAB1, human 1 is CAB2), never swapped. */
	bad = outcome;
	bad.humanCharacter[0] = 1;
	bad.humanCharacter[1] = 5;
	CHECK(NativeMatchSelect_BuildConfig(&base, &bad, &built) == 1);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&built, NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &slotIndex) == 1);
	CHECK(built.slots[slotIndex].characterID == 1);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&built, NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slotIndex) == 1);
	CHECK(built.slots[slotIndex].characterID == 5);

	/* One human on the one-cab base: CAB1 and seven bots. */
	BuildOneCabBase(&oneCab);
	SetChoice(&choices[0], 3, 1, 5, 7);
	CHECK(NativeMatchSelect_Resolve(&oneCab, 1, choices, &outcome) == 1);
	CHECK(NativeMatchSelect_BuildConfig(&oneCab, &outcome, &built) == 1);
	CHECK(built.slots[0].characterID == 3);
	CHECK(built.slots[1].characterID == 0);
	CHECK(built.slots[7].characterID == 7);
	CHECK(built.trackID == 1);
	CHECK(built.lapCount == 5);

	/* Failures leave *config untouched. */
	SetChoice(&choices[0], 0, 3, 3, 1);
	SetChoice(&choices[1], 1, 3, 3, 2);
	SetChoice(&choices[2], 2, 3, 3, 3);
	SetChoice(&choices[3], 3, 3, 3, 4);

	/* humanCount 3 and 4 have no role slot on the two-cab base. */
	CHECK(NativeMatchSelect_Resolve(&base, 3, choices, &outcome) == 1);
	CHECK(ExpectBuildFails(&base, &outcome) == 0);
	CHECK(NativeMatchSelect_Resolve(&base, 4, choices, &outcome) == 1);
	CHECK(ExpectBuildFails(&base, &outcome) == 0);

	/*
	 * One human on the two-cab base resolves (bots 0, 2, 3, 4, matching the
	 * base's four bot slots) but must not build: CAB2's slot would keep its
	 * base character, 1, which the human also holds.
	 */
	SetChoice(&choices[0], 1, 3, 3, 1);
	CHECK(NativeMatchSelect_Resolve(&base, 1, choices, &outcome) == 1);
	CHECK(outcome.humanCount == 1);
	CHECK(outcome.botCount == 4);
	CHECK(ExpectBuildFails(&base, &outcome) == 0);
	SetChoice(&choices[0], 0, 3, 3, 1);

	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	bad = outcome;
	bad.botCount = 3;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.botCount = 5;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	CHECK(ExpectBuildFails(&oneCab, &outcome) == 0); /* 2 humans vs 1 slot, 4 bots vs 7 */
	bad = outcome;
	bad.humanCount = 0;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.humanCount = 1;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.humanCount = 5;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	expected = base;
	expected.lapCount = 0;
	CHECK(ExpectBuildFails(&expected, &outcome) == 0);
	CHECK(NativeMatchSelect_BuildConfig(NULL, &outcome, &built) == 0);
	CHECK(NativeMatchSelect_BuildConfig(&base, NULL, &built) == 0);
	CHECK(NativeMatchSelect_BuildConfig(&base, &outcome, NULL) == 0);
	return 0;
}

/* BuildConfig validates the outcome itself, not only the resulting config. */
static int TestBuildRejectsMalformedOutcome(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 built;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;
	struct NativeMatchSelectOutcome bad;

	BuildTwoCabBase(&base);
	SetChoice(&choices[0], 0, 3, 3, 1);
	SetChoice(&choices[1], 1, 3, 3, 2);
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);
	/* Humans 0, 1; bots set 0 = 6, 4, 2, 3. */
	CHECK(outcome.aiSetIndex == 0);
	CHECK(NativeMatchSelect_BuildConfig(&base, &outcome, &built) == 1);

	/* Track outside the table. */
	bad = outcome;
	bad.trackID = 13; /* OXIDE_STATION */
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.trackID = 17; /* TURBO_TRACK */
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Laps outside the table (NativeMatchConfigV1_Validate accepts any nonzero count). */
	bad = outcome;
	bad.lapCount = 4;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.lapCount = 0;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Characters outside the table, human and bot. */
	bad = outcome;
	bad.humanCharacter[1] = 8;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.botCharacter[3] = 8;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.humanCharacter[0] = 15; /* NITROS_OXIDE */
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Human/bot duplicate: CAB1 = 2 while the bot set 6, 4, 2, 3 also holds 2. */
	bad = outcome;
	bad.humanCharacter[0] = 2;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Human/human and bot/bot duplicates. */
	bad = outcome;
	bad.humanCharacter[1] = 0;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.botCharacter[1] = 6;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Unused entries must be 0. */
	bad = outcome;
	bad.humanCharacter[2] = 5;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.botCharacter[4] = 5;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.botCharacter[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT - 1u] = 7;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* masterSeed 0 or the base's seed. */
	bad = outcome;
	bad.masterSeed = 0;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.masterSeed = base.masterSeed;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Drawn flags are 0 or 1. */
	bad = outcome;
	bad.trackDrawn = 2;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.lapsDrawn = 0xff;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* characterReassignedMask has no bit at or above humanCount. */
	bad = outcome;
	bad.characterReassignedMask = 0x4u;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.characterReassignedMask = 0x80u;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	/* Bit 0 is never set: human 0 (CAB1) is never reassigned. */
	bad.characterReassignedMask = 0x3u;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.characterReassignedMask = 0x1u;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.characterReassignedMask = 0x2u;
	CHECK(NativeMatchSelect_BuildConfig(&base, &bad, &built) == 1);

	/*
	 * Two humans, four bots: aiSetIndex is exactly the first retail set
	 * holding neither human (set 0 = 6, 4, 2, 3 for humans 0 and 1), never
	 * AI_SET_NONE, out of range, or another set.
	 */
	bad = outcome;
	bad.aiSetIndex = NATIVE_MATCH_SELECT_AI_SET_COUNT;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.aiSetIndex = 0xfeu;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.aiSetIndex = NATIVE_MATCH_SELECT_AI_SET_NONE;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.aiSetIndex = NATIVE_MATCH_SELECT_AI_SET_COUNT - 1u;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/*
	 * A valid retail set that is not the first qualifying one: set 5
	 * (4, 7, 3, 5) holds neither human 0 nor 1, but set 0 comes first. Its
	 * characters are distinct table characters, so only the first-set rule
	 * rejects it: with its own index, with set 0's index, and set 0's bots
	 * under set 5's index.
	 */
	CHECK(NativeMatchSelect_AiSetRacer(5, 0) == 4);
	CHECK(NativeMatchSelect_AiSetRacer(5, 1) == 7);
	CHECK(NativeMatchSelect_AiSetRacer(5, 2) == 3);
	CHECK(NativeMatchSelect_AiSetRacer(5, 3) == 5);
	bad = outcome;
	bad.aiSetIndex = 5;
	for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
	{
		bad.botCharacter[racer] = NativeMatchSelect_AiSetRacer(5, racer);
	}
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad.aiSetIndex = 0;
	CHECK(ExpectBuildFails(&base, &bad) == 0);
	bad = outcome;
	bad.aiSetIndex = 5;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* The first set's racers, but out of set order. */
	bad = outcome;
	bad.botCharacter[0] = outcome.botCharacter[1];
	bad.botCharacter[1] = outcome.botCharacter[0];
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Other humans select another first set, which then builds: humans 4
	 * and 7 skip set 0 (holds 4), so set 1 = 0, 6, 3, 5. */
	bad = outcome;
	bad.humanCharacter[0] = 4;
	bad.humanCharacter[1] = 7;
	bad.aiSetIndex = 1;
	for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
	{
		bad.botCharacter[racer] = NativeMatchSelect_AiSetRacer(1, racer);
	}
	CHECK(NativeMatchSelect_BuildConfig(&base, &bad, &built) == 1);
	bad.aiSetIndex = 0;
	CHECK(ExpectBuildFails(&base, &bad) == 0);

	/* Any other shape (one human, seven bots): aiSetIndex is AI_SET_NONE. */
	{
		struct NativeMatchConfigV1 oneCab;
		struct NativeMatchSelectOutcome oneCabOutcome;

		BuildOneCabBase(&oneCab);
		SetChoice(&choices[0], 3, 1, 5, 7);
		CHECK(NativeMatchSelect_Resolve(&oneCab, 1, choices, &oneCabOutcome) == 1);
		CHECK(oneCabOutcome.aiSetIndex == NATIVE_MATCH_SELECT_AI_SET_NONE);
		CHECK(NativeMatchSelect_BuildConfig(&oneCab, &oneCabOutcome, &built) == 1);
		bad = oneCabOutcome;
		for (uint32_t setIndex = 0; setIndex < NATIVE_MATCH_SELECT_AI_SET_COUNT; setIndex++)
		{
			bad.aiSetIndex = (uint8_t)setIndex;
			CHECK(ExpectBuildFails(&oneCab, &bad) == 0);
		}
		bad.aiSetIndex = NATIVE_MATCH_SELECT_AI_SET_COUNT;
		CHECK(ExpectBuildFails(&oneCab, &bad) == 0);
		/* Bit 0 of the reassigned mask is refused here too. */
		bad = oneCabOutcome;
		bad.characterReassignedMask = 0x1u;
		CHECK(ExpectBuildFails(&oneCab, &bad) == 0);
	}
	return 0;
}

/*
 * End to end: fixed two-cab base, fixed choices (both humans on TINY_TIGER,
 * so CAB2 is reassigned; different track votes, so the track is drawn; the
 * same lap vote), fixed nonces. Computed independently with a Perl
 * Digest::SHA reference written from the header spec and the V1 config
 * encoding. Frozen: changing any of these changes every agreed match.
 */
static const uint64_t k_frozenNonces[2] = { UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210) };
static const uint64_t k_frozenSeed = UINT64_C(0x56ec93a7ef7aa836);
static const uint8_t k_frozenOutcomeDigestPrefix[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES] = {
	0xe5, 0xfc, 0x38, 0x5f, 0x94, 0x23, 0x00, 0xb3,
};
static const uint8_t k_frozenBuiltDigest[NATIVE_SHA256_DIGEST_BYTES] = {
	0x44, 0x51, 0x07, 0x30, 0x7c, 0x12, 0xde, 0xf4, 0x22, 0x76, 0x86, 0x33, 0xae, 0xb3, 0x53, 0x39,
	0x5b, 0xf9, 0xc7, 0xb6, 0xc5, 0xc3, 0x78, 0x0b, 0xbb, 0x78, 0x53, 0xaa, 0x85, 0x4d, 0x31, 0x92,
};

static int TestFrozenEndToEnd(void)
{
	static const uint8_t frozenBots[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT] = { 4, 7, 3, 5, 0, 0, 0, 0 };
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 built;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome outcome;
	uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t outcomeDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t builtDigest[NATIVE_SHA256_DIGEST_BYTES];

	BuildTwoCabBase(&base);
	SetChoice(&choices[0], 2, 6, 5, k_frozenNonces[0]);  /* CAB1: TINY_TIGER, ROO_TUBES, 5 laps */
	SetChoice(&choices[1], 2, 14, 5, k_frozenNonces[1]); /* CAB2: TINY_TIGER, MYSTERY_CAVES, 5 laps */
	CHECK(NativeMatchSelect_Resolve(&base, 2, choices, &outcome) == 1);

	CHECK(outcome.masterSeed == k_frozenSeed);
	CHECK(outcome.trackID == 6);
	CHECK(outcome.lapCount == 5);
	CHECK(outcome.humanCount == 2);
	CHECK(outcome.botCount == 4);
	CHECK(outcome.trackDrawn == 1);
	CHECK(outcome.lapsDrawn == 0);
	CHECK(outcome.characterReassignedMask == 0x2u);
	CHECK(outcome.aiSetIndex == 5);
	CHECK(outcome.humanCharacter[0] == 2);
	CHECK(outcome.humanCharacter[1] == 0);
	CHECK(outcome.humanCharacter[2] == 0);
	CHECK(outcome.humanCharacter[3] == 0);
	CHECK(memcmp(outcome.botCharacter, frozenBots, sizeof(frozenBots)) == 0);

	CHECK(NativeMatchConfigV1_Digest(&base, baseDigest) == 1);
	CHECK(NativeMatchSelect_OutcomeDigest(baseDigest, &outcome, outcomeDigest) == 1);
	CHECK(memcmp(outcomeDigest, k_frozenOutcomeDigestPrefix, sizeof(k_frozenOutcomeDigestPrefix)) == 0);

	CHECK(NativeMatchSelect_BuildConfig(&base, &outcome, &built) == 1);
	CHECK(NativeMatchConfigV1_Digest(&built, builtDigest) == 1);
	CHECK(memcmp(builtDigest, k_frozenBuiltDigest, sizeof(builtDigest)) == 0);
	return 0;
}

int main(void)
{
	CHECK(TestTables() == 0);
	CHECK(TestDeriveSeed() == 0);
	CHECK(TestDraw() == 0);
	CHECK(TestVotes() == 0);
	CHECK(TestCharacters() == 0);
	CHECK(TestBotsTwoHumansExhaustive() == 0);
	CHECK(TestBotsOtherHumanCounts() == 0);
	CHECK(TestResolveRejects() == 0);
	CHECK(TestOutcomeDigest() == 0);
	CHECK(TestBuildConfig() == 0);
	CHECK(TestBuildRejectsMalformedOutcome() == 0);
	CHECK(TestFrozenEndToEnd() == 0);
	puts("native_match_select_rules_test: ok");
	return 0;
}
