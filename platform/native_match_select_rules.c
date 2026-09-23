#include "platform/native_match_select_rules.h"

#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * The tables stay simple one-line-per-table initializers so the isolation
 * test can read them and compare them with the retail sources.
 */

/* Base characters (enum Characters, include/namespace_Vehicle.h:37-55). */
static const uint8_t k_matchSelectCharacters[NATIVE_MATCH_SELECT_CHARACTER_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7 };

/* arcadeTracks levelIDs with unlock 0xFFFF, retail menu order (game/230/D230.c:561-599). */
static const uint8_t k_matchSelectTracks[NATIVE_MATCH_SELECT_TRACK_COUNT] = { 3, 6, 4, 14, 9, 2, 8, 0, 5, 1, 12, 10, 15, 7, 11, 16 };

/* Nonzero lapCountByRow rows (game/230/D230.c:621). */
static const uint8_t k_matchSelectLapOptions[NATIVE_MATCH_SELECT_LAP_OPTION_COUNT] = { 3, 5, 7 };

/* characterIDs_2P_AIs, four racers per set, sets in order (game/zGlobal_DATA.c:3558-3580). */
static const uint8_t k_matchSelectAiSets[NATIVE_MATCH_SELECT_AI_SET_COUNT * NATIVE_MATCH_SELECT_AI_SET_RACERS] = { 6, 4, 2, 3, 0, 6, 3, 5, 0, 6, 1, 2, 0, 6, 4, 7, 1, 2, 3, 5, 4, 7, 3, 5, 4, 7, 1, 2 };

static const char k_seedTag[] = "CTRN match select seed v1";
static const char k_drawTag[] = "CTRN match select draw v1";
static const char k_outcomeTag[] = "CTRN match select outcome v1";

static void NativeMatchSelect_StoreLe64(uint8_t bytes[8], uint64_t value)
{
	for (uint32_t i = 0; i < 8u; i++)
	{
		bytes[i] = (uint8_t)(value >> (8u * i));
	}
}

static uint64_t NativeMatchSelect_LoadLe64(const uint8_t bytes[8])
{
	uint64_t value = 0;

	for (uint32_t i = 0; i < 8u; i++)
	{
		value |= (uint64_t)bytes[i] << (8u * i);
	}
	return value;
}

static int NativeMatchSelect_FindIndex(const uint8_t *table, uint32_t count, uint8_t value, uint32_t *index)
{
	if (index == NULL)
	{
		return 0;
	}
	for (uint32_t i = 0; i < count; i++)
	{
		if (table[i] == value)
		{
			*index = i;
			return 1;
		}
	}
	return 0;
}

uint8_t NativeMatchSelect_CharacterAt(uint32_t index)
{
	return index < NATIVE_MATCH_SELECT_CHARACTER_COUNT ? k_matchSelectCharacters[index] : 0xffu;
}

uint8_t NativeMatchSelect_TrackAt(uint32_t index)
{
	return index < NATIVE_MATCH_SELECT_TRACK_COUNT ? k_matchSelectTracks[index] : 0xffu;
}

uint8_t NativeMatchSelect_LapOptionAt(uint32_t index)
{
	return index < NATIVE_MATCH_SELECT_LAP_OPTION_COUNT ? k_matchSelectLapOptions[index] : 0xffu;
}

int NativeMatchSelect_CharacterIndex(uint8_t characterID, uint32_t *index)
{
	return NativeMatchSelect_FindIndex(k_matchSelectCharacters, NATIVE_MATCH_SELECT_CHARACTER_COUNT, characterID, index);
}

int NativeMatchSelect_TrackIndex(uint8_t trackID, uint32_t *index)
{
	return NativeMatchSelect_FindIndex(k_matchSelectTracks, NATIVE_MATCH_SELECT_TRACK_COUNT, trackID, index);
}

int NativeMatchSelect_LapOptionIndex(uint8_t lapCount, uint32_t *index)
{
	return NativeMatchSelect_FindIndex(k_matchSelectLapOptions, NATIVE_MATCH_SELECT_LAP_OPTION_COUNT, lapCount, index);
}

uint8_t NativeMatchSelect_AiSetRacer(uint32_t setIndex, uint32_t racer)
{
	if ((setIndex >= NATIVE_MATCH_SELECT_AI_SET_COUNT) || (racer >= NATIVE_MATCH_SELECT_AI_SET_RACERS))
	{
		return 0xffu;
	}
	return k_matchSelectAiSets[(setIndex * NATIVE_MATCH_SELECT_AI_SET_RACERS) + racer];
}

int NativeMatchSelect_DeriveSeed(const uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES], uint64_t baseMasterSeed,
	uint32_t humanCount, const uint64_t nonces[], uint64_t *seedOut)
{
	struct NativeSha256 sha;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t count;

	if ((baseDigest == NULL) || (nonces == NULL) || (seedOut == NULL) || (humanCount == 0) ||
	    (humanCount > NATIVE_MATCH_SELECT_MAX_HUMANS))
	{
		return 0;
	}

	count = (uint8_t)humanCount;
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_seedTag, sizeof(k_seedTag) - 1u);
	NativeSha256_Update(&sha, baseDigest, NATIVE_SHA256_DIGEST_BYTES);
	NativeSha256_Update(&sha, &count, 1u);
	for (uint32_t h = 0; h < humanCount; h++)
	{
		uint8_t nonceBytes[8];

		NativeMatchSelect_StoreLe64(nonceBytes, nonces[h]);
		NativeSha256_Update(&sha, nonceBytes, sizeof(nonceBytes));
	}
	NativeSha256_Final(&sha, digest);

	for (uint32_t word = 0; word < (NATIVE_SHA256_DIGEST_BYTES / 8u); word++)
	{
		const uint64_t candidate = NativeMatchSelect_LoadLe64(&digest[word * 8u]);

		if ((candidate != 0) && (candidate != baseMasterSeed))
		{
			*seedOut = candidate;
			return 1;
		}
	}
	return 0;
}

int NativeMatchSelect_Draw(uint64_t matchSeed, uint8_t domain, uint32_t candidateCount, uint32_t *indexOut)
{
	struct NativeSha256 sha;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t seedBytes[8];

	if ((indexOut == NULL) || (candidateCount == 0) ||
	    ((domain != NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK) && (domain != NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS)))
	{
		return 0;
	}

	NativeMatchSelect_StoreLe64(seedBytes, matchSeed);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_drawTag, sizeof(k_drawTag) - 1u);
	NativeSha256_Update(&sha, &domain, 1u);
	NativeSha256_Update(&sha, seedBytes, sizeof(seedBytes));
	NativeSha256_Final(&sha, digest);

	*indexOut = (uint32_t)(NativeMatchSelect_LoadLe64(digest) % candidateCount);
	return 1;
}

/*
 * Plurality over one table: the entries with the most votes, in table order,
 * are the candidates; a single candidate wins, several are drawn.
 */
static int NativeMatchSelect_TallyVotes(const uint8_t *table, uint32_t tableCount, const uint32_t *voteIndex,
	uint32_t humanCount, uint64_t matchSeed, uint8_t domain, uint8_t *winner, uint8_t *drawn)
{
	uint32_t votes[NATIVE_MATCH_SELECT_TRACK_COUNT];
	uint32_t candidates[NATIVE_MATCH_SELECT_TRACK_COUNT];
	uint32_t candidateCount = 0;
	uint32_t best = 0;
	uint32_t pick = 0;

	memset(votes, 0, sizeof(votes));
	for (uint32_t h = 0; h < humanCount; h++)
	{
		votes[voteIndex[h]]++;
	}
	for (uint32_t i = 0; i < tableCount; i++)
	{
		if (votes[i] > best)
		{
			best = votes[i];
		}
	}
	for (uint32_t i = 0; i < tableCount; i++)
	{
		if (votes[i] == best)
		{
			candidates[candidateCount++] = i;
		}
	}

	if (candidateCount == 1u)
	{
		*winner = table[candidates[0]];
		*drawn = 0;
		return 1;
	}
	if (!NativeMatchSelect_Draw(matchSeed, domain, candidateCount, &pick))
	{
		return 0;
	}
	*winner = table[candidates[pick]];
	*drawn = 1;
	return 1;
}

static int NativeMatchSelect_AiSetHolds(uint32_t setIndex, uint8_t characterID)
{
	for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
	{
		if (k_matchSelectAiSets[(setIndex * NATIVE_MATCH_SELECT_AI_SET_RACERS) + racer] == characterID)
		{
			return 1;
		}
	}
	return 0;
}

/* The LOAD_Robots2P rule: the first set holding neither character, or AI_SET_COUNT if none does. */
static uint32_t NativeMatchSelect_FirstAiSetWithout(uint8_t first, uint8_t second)
{
	uint32_t setIndex = 0;

	while ((setIndex < NATIVE_MATCH_SELECT_AI_SET_COUNT) &&
	       (NativeMatchSelect_AiSetHolds(setIndex, first) || NativeMatchSelect_AiSetHolds(setIndex, second)))
	{
		setIndex++;
	}
	return setIndex;
}

int NativeMatchSelect_Resolve(const struct NativeMatchConfigV1 *base, uint32_t humanCount,
	const struct NativeMatchSelectChoice choices[], struct NativeMatchSelectOutcome *out)
{
	struct NativeMatchSelectOutcome candidate;
	uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint64_t nonces[NATIVE_MATCH_SELECT_MAX_HUMANS] = { 0 };
	uint32_t trackVotes[NATIVE_MATCH_SELECT_MAX_HUMANS] = { 0 };
	uint32_t lapVotes[NATIVE_MATCH_SELECT_MAX_HUMANS] = { 0 };
	uint8_t taken[NATIVE_MATCH_SELECT_CHARACTER_COUNT];
	uint32_t botCount = 0;

	if ((base == NULL) || (choices == NULL) || (out == NULL) || (humanCount == 0) ||
	    (humanCount > NATIVE_MATCH_SELECT_MAX_HUMANS) || !NativeMatchConfigV1_Validate(base))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	memset(taken, 0, sizeof(taken));
	for (uint32_t h = 0; h < humanCount; h++)
	{
		const struct NativeMatchSelectChoice *choice = &choices[h];
		uint32_t characterIndex = 0;

		for (uint32_t i = 0; i < sizeof(choice->reserved); i++)
		{
			if (choice->reserved[i] != 0)
			{
				return 0;
			}
		}
		if (!NativeMatchSelect_CharacterIndex(choice->characterID, &characterIndex) ||
		    !NativeMatchSelect_TrackIndex(choice->trackID, &trackVotes[h]) ||
		    !NativeMatchSelect_LapOptionIndex(choice->lapCount, &lapVotes[h]))
		{
			return 0;
		}
		nonces[h] = choice->nonce;
		candidate.humanCharacter[h] = choice->characterID;
		/* MM_Characters_PreventOverlap first marks every chosen character taken. */
		taken[characterIndex] = 1;
	}

	if (!NativeMatchConfigV1_Digest(base, baseDigest) ||
	    !NativeMatchSelect_DeriveSeed(baseDigest, base->masterSeed, humanCount, nonces, &candidate.masterSeed))
	{
		return 0;
	}

	if (!NativeMatchSelect_TallyVotes(k_matchSelectTracks, NATIVE_MATCH_SELECT_TRACK_COUNT, trackVotes, humanCount,
	        candidate.masterSeed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK, &candidate.trackID, &candidate.trackDrawn) ||
	    !NativeMatchSelect_TallyVotes(k_matchSelectLapOptions, NATIVE_MATCH_SELECT_LAP_OPTION_COUNT, lapVotes, humanCount,
	        candidate.masterSeed, NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS, &candidate.lapCount, &candidate.lapsDrawn))
	{
		return 0;
	}

	/* Then a later human holding an earlier human's character takes the lowest unmarked one. */
	for (uint32_t h = 1; h < humanCount; h++)
	{
		for (uint32_t g = 0; g < h; g++)
		{
			if (candidate.humanCharacter[h] == candidate.humanCharacter[g])
			{
				for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_CHARACTER_COUNT; i++)
				{
					if (taken[i] == 0)
					{
						candidate.humanCharacter[h] = k_matchSelectCharacters[i];
						taken[i] = 1;
						candidate.characterReassignedMask |= (uint8_t)(1u << h);
						break;
					}
				}
				break;
			}
		}
	}

	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		if (base->slots[i].role == NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			botCount++;
		}
	}

	if ((humanCount == 2u) && (botCount == NATIVE_MATCH_SELECT_AI_SET_RACERS))
	{
		/* The LOAD_Robots2P rule: the first set holding neither human. */
		const uint32_t setIndex = NativeMatchSelect_FirstAiSetWithout(candidate.humanCharacter[0], candidate.humanCharacter[1]);

		if (setIndex >= NATIVE_MATCH_SELECT_AI_SET_COUNT)
		{
			return 0;
		}
		for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
		{
			candidate.botCharacter[racer] = k_matchSelectAiSets[(setIndex * NATIVE_MATCH_SELECT_AI_SET_RACERS) + racer];
		}
		candidate.aiSetIndex = (uint8_t)setIndex;
	}
	else
	{
		/* Provisional: ascending base characters no human holds. */
		uint32_t filled = 0;

		for (uint32_t i = 0; (i < NATIVE_MATCH_SELECT_CHARACTER_COUNT) && (filled < botCount); i++)
		{
			if (taken[i] == 0)
			{
				candidate.botCharacter[filled++] = k_matchSelectCharacters[i];
			}
		}
		if (filled < botCount)
		{
			return 0;
		}
		candidate.aiSetIndex = NATIVE_MATCH_SELECT_AI_SET_NONE;
	}

	candidate.humanCount = (uint8_t)humanCount;
	candidate.botCount = (uint8_t)botCount;
	*out = candidate;
	return 1;
}

int NativeMatchSelect_OutcomeDigest(const uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES],
	const struct NativeMatchSelectOutcome *outcome, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	struct NativeSha256 sha;
	uint8_t encoded[8u + 8u + NATIVE_MATCH_SELECT_MAX_HUMANS + NATIVE_MATCH_CONFIG_V1_SLOT_COUNT];
	uint32_t at = 8u;

	if ((baseDigest == NULL) || (outcome == NULL) || (digest == NULL))
	{
		return 0;
	}

	NativeMatchSelect_StoreLe64(encoded, outcome->masterSeed);
	encoded[at++] = outcome->trackID;
	encoded[at++] = outcome->lapCount;
	encoded[at++] = outcome->humanCount;
	encoded[at++] = outcome->botCount;
	encoded[at++] = outcome->trackDrawn;
	encoded[at++] = outcome->lapsDrawn;
	encoded[at++] = outcome->characterReassignedMask;
	encoded[at++] = outcome->aiSetIndex;
	memcpy(&encoded[at], outcome->humanCharacter, sizeof(outcome->humanCharacter));
	at += (uint32_t)sizeof(outcome->humanCharacter);
	memcpy(&encoded[at], outcome->botCharacter, sizeof(outcome->botCharacter));

	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_outcomeTag, sizeof(k_outcomeTag) - 1u);
	NativeSha256_Update(&sha, baseDigest, NATIVE_SHA256_DIGEST_BYTES);
	NativeSha256_Update(&sha, encoded, sizeof(encoded));
	NativeSha256_Final(&sha, digest);
	return 1;
}

/*
 * One character list (humans or bots): the first usedCount entries are
 * distinct table characters not yet in taken (and are marked there); the
 * rest are 0.
 */
static int NativeMatchSelect_CharactersWellFormed(const uint8_t *characters, uint32_t entryCount, uint32_t usedCount,
	uint8_t taken[NATIVE_MATCH_SELECT_CHARACTER_COUNT])
{
	for (uint32_t i = 0; i < entryCount; i++)
	{
		uint32_t characterIndex = 0;

		if (i >= usedCount)
		{
			if (characters[i] != 0)
			{
				return 0;
			}
			continue;
		}
		if (!NativeMatchSelect_CharacterIndex(characters[i], &characterIndex) || (taken[characterIndex] != 0))
		{
			return 0;
		}
		taken[characterIndex] = 1;
	}
	return 1;
}

/* The outcome fields Resolve guarantees, checked against base. Counts are checked by the caller. */
static int NativeMatchSelect_OutcomeWellFormed(const struct NativeMatchConfigV1 *base,
	const struct NativeMatchSelectOutcome *outcome)
{
	uint8_t taken[NATIVE_MATCH_SELECT_CHARACTER_COUNT];
	uint32_t index = 0;

	if (!NativeMatchSelect_TrackIndex(outcome->trackID, &index) ||
	    !NativeMatchSelect_LapOptionIndex(outcome->lapCount, &index) || (outcome->masterSeed == 0) ||
	    (outcome->masterSeed == base->masterSeed) || (outcome->trackDrawn > 1u) || (outcome->lapsDrawn > 1u) ||
	    (((uint32_t)outcome->characterReassignedMask >> outcome->humanCount) != 0u) ||
	    ((outcome->characterReassignedMask & 0x1u) != 0u))
	{
		return 0;
	}

	if ((outcome->humanCount == 2u) && (outcome->botCount == NATIVE_MATCH_SELECT_AI_SET_RACERS))
	{
		/* Retail derives the 2P bots from the humans: exactly the first set holding neither, in set order. */
		const uint32_t setIndex = NativeMatchSelect_FirstAiSetWithout(outcome->humanCharacter[0], outcome->humanCharacter[1]);

		if ((setIndex >= NATIVE_MATCH_SELECT_AI_SET_COUNT) || (outcome->aiSetIndex != setIndex))
		{
			return 0;
		}
		for (uint32_t racer = 0; racer < NATIVE_MATCH_SELECT_AI_SET_RACERS; racer++)
		{
			if (outcome->botCharacter[racer] != k_matchSelectAiSets[(setIndex * NATIVE_MATCH_SELECT_AI_SET_RACERS) + racer])
			{
				return 0;
			}
		}
	}
	else if (outcome->aiSetIndex != NATIVE_MATCH_SELECT_AI_SET_NONE)
	{
		return 0;
	}

	memset(taken, 0, sizeof(taken));
	return NativeMatchSelect_CharactersWellFormed(outcome->humanCharacter, NATIVE_MATCH_SELECT_MAX_HUMANS,
	           outcome->humanCount, taken) &&
	       NativeMatchSelect_CharactersWellFormed(outcome->botCharacter, NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,
	           outcome->botCount, taken);
}

int NativeMatchSelect_BuildConfig(const struct NativeMatchConfigV1 *base, const struct NativeMatchSelectOutcome *outcome,
	struct NativeMatchConfigV1 *config)
{
	struct NativeMatchConfigV1 built;
	uint32_t humanSlotCount = 0;
	uint32_t botCount = 0;
	uint32_t bot = 0;

	if ((base == NULL) || (outcome == NULL) || (config == NULL) || !NativeMatchConfigV1_Validate(base))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		if ((base->slots[i].role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) ||
		    (base->slots[i].role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
		{
			humanSlotCount++;
		}
		else if (base->slots[i].role == NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			botCount++;
		}
	}
	/* Every human slot gets a human: a partial outcome would leave a base character behind. */
	if ((outcome->humanCount == 0) || (outcome->humanCount != humanSlotCount) || (outcome->botCount != botCount) ||
	    !NativeMatchSelect_OutcomeWellFormed(base, outcome))
	{
		return 0;
	}

	built = *base;
	built.trackID = outcome->trackID;
	built.lapCount = outcome->lapCount;
	built.masterSeed = outcome->masterSeed;
	for (uint32_t h = 0; h < outcome->humanCount; h++)
	{
		uint8_t slotIndex = 0;

		if (!NativeMatchConfigV1_FindRoleSlot(base, (uint8_t)(NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN + h), &slotIndex))
		{
			return 0;
		}
		built.slots[slotIndex].characterID = outcome->humanCharacter[h];
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		if (base->slots[i].role == NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			built.slots[i].characterID = outcome->botCharacter[bot++];
		}
	}

	if (!NativeMatchConfigV1_Validate(&built))
	{
		return 0;
	}
	*config = built;
	return 1;
}
