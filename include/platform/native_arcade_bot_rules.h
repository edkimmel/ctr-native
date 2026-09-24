#ifndef PLATFORM_NATIVE_ARCADE_BOT_RULES_H
#define PLATFORM_NATIVE_ARCADE_BOT_RULES_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_canonical_codec.h"
#include "platform/native_deterministic_rng.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

/*
 * Arcade bot rules v1 (docs/ROSTER_MILESTONE.md sections 3.1 and 3.3, with
 * the defaults RS-1..RS-9 of section 4 and the defaults pending owner review
 * RS-19 and RS-20). Defines, versions, and digests the native rule choices
 * an arcade single race is built from, so that
 * NativeMatchConfigV1.botRulesDigest names them. Two profiles are supported
 * (RS-1, decided by the owner: both ARCADE_TWO_CAB and ARCADE_ONE_CAB), each
 * with its own rules, canonical encoding, and digest (RS-19, below).
 *
 * ARCADE_TWO_CAB, the V1 rules (the retail 2P arcade race):
 * - shape: two humans in slots 0 and 1, four bots in slots 2..5, six drivers
 *   (MainInit_Drivers, game/MAIN/MainInit.c:288-406);
 * - bot characters: the LOAD_Robots2P rule (RS-4,
 *   game/LOAD/LOAD_Assets.c:21-59): the first retail 2P AI set holding
 *   neither human's character, racers in set order.
 *
 * ARCADE_ONE_CAB, the 1P V1 rules (the retail 1P arcade race):
 * - shape: one human in slot 0 (CAB1_HUMAN), seven bots in slots 1..7, eight
 *   drivers: MainInit_Drivers (game/MAIN/MainInit.c:288-406) sets
 *   `numDrivers = 8` when numPlyrCurrGame is 1 in arcade (:352-355), and its
 *   bot loop `for (int i = numPlyrCurrGame; i < numDrivers; i++)` calls
 *   BOTS_Driver_Init(i) for i = 1..7 (:363-366). The slot roles come from
 *   NativeMatchConfigV1_InitArcadeOneCab;
 * - bot characters: the LOAD_Robots1P rule (game/LOAD/LOAD_Assets.c:61-76,
 *   called on the 1P path at :150-153 unless gameMode1 masked with
 *   TIME_TRIAL | MAIN_MENU is exactly MAIN_MENU). characterIDs[0] is the
 *   human; for i = 1..LOAD_CHARACTER_ID_COUNT - 1 (the Load namespace
 *   constant, 8) newCharacterID walks up from 0, skipping the human's ID
 *   once, and characterIDs[i] = newCharacterID. For a base human h that is
 *   {0..7} without h, ascending. No RNG and no AI-set table;
 * - RS-20: ONE_CAB humans are base characters only (match select's
 *   character table, NativeMatchSelect_CharacterIndex), and the bots are
 *   exactly LOAD_Robots1P's result for that human, in ascending slot order.
 *
 * Shared by both profiles:
 * - mode: the config's gameMode1, gameMode2, and rules are all 0, meaning a
 *   retail arcade single race without cheats or cup (RS-2);
 * - difficulty: global, as in retail (RS-3). Every bot slot carries the same
 *   retail speed value from the D230 cupDifficulty speed table
 *   (game/230/D230.c), 0x50 easy, 0xA0 medium, 0xF0 hard; human slots
 *   carry 0;
 * - RNG ownership (RS-5..RS-8, section 3.3): the retail seeds are drawn from
 *   the bank's MATCH_SETUP stream (global slot) in the fixed target order of
 *   enum NativeArcadeBotRulesSeedTarget; ITEMS, HAZARDS, and BOT[0..7] are
 *   reserved and undrawn in v1. MapRetailSeedsV1 and DeriveRetailSeedsV1
 *   serve both profiles unchanged.
 *
 * RS-19 (default pending owner review): each profile has its own canonical
 * encoding and digest, and a config's botRulesDigest must equal the digest
 * of its own profile's rules (DigestForProfileV1). The TWO_CAB encoding (V1
 * below, 111 bytes, tag "CTRN arcade bot rules v1") and DigestV1 are
 * unchanged byte for byte, because that digest is carried by the arcade-link
 * fixture, match select, and the netplay config paths; ONE_CAB gets the
 * separate 1P V1 encoding and Digest1PV1.
 *
 * tests/native_arcade_bot_rules_isolation_test.cmake checks that the
 * difficulty table mirrors game/230/D230.c, that the advRng fallback
 * constants mirror game/BOTS.c, that the 1P candidate count equals
 * LOAD_CHARACTER_ID_COUNT, that the LOAD_Robots1P body and its 1P call site
 * are unchanged, and that MainInit_Drivers still gives 1P arcade 8 drivers
 * with the first bot in slot 1.
 *
 * Canonical encoding V1 (ARCADE_TWO_CAB):
 * NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES bytes,
 * fixed, little-endian through NativeCodecWriter, in this order:
 *
 *   offset size field
 *   0      24   tag "CTRN arcade bot rules v1" (ASCII, no NUL)
 *   24     4    rulesVersion 1
 *   28     4    profile NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB
 *   32     8    slot roles 0..7 of the TWO_CAB profile, one byte each, as
 *               NativeMatchConfigV1_InitArcadeTwoCab sets them
 *   40     1    humanCount 2
 *   41     1    driverCount 6
 *   42     1    firstBotSlot 2
 *   43     1    botCount 4
 *   44     1    difficultyCount 3
 *   45     3    difficulty table 0x50, 0xA0, 0xF0
 *   48     1    difficultyPolicy 1 (global: every bot slot equal, humans 0)
 *   49     1    botCharacterPolicy 1 (retail 2P AI set rule)
 *   50     1    modePolicy 1 (arcade single race: gameMode1, gameMode2,
 *               and rules all 0)
 *   51     1    aiSetCount 7
 *   52     1    aiSetRacers 4
 *   53     28   the seven 2P AI sets in order, four racers each
 *               (NativeMatchSelect_AiSetRacer)
 *   81     4    rngDerivationVersion NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION
 *   85     4    seed stream tag NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP
 *   89     1    seedTargetCount 5
 *   90     5    seed target codes 1, 2, 3, 4, 5 in draw order
 *   95     4    randomNumberMask 0xFFFF
 *   99     4    advRngFallback0 0x30215400
 *   103    4    advRngFallback1 0x493583fe
 *   107    4    reservedStreamMask 0x3FF (bit 0 ITEMS, bit 1 HAZARDS,
 *               bits 2..9 BOT[0..7])
 *   111         end
 *
 * Canonical encoding 1P V1 (ARCADE_ONE_CAB):
 * NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES bytes, fixed, little-endian
 * through NativeCodecWriter, in this order:
 *
 *   offset size field
 *   0      27   tag "CTRN arcade bot rules 1P v1" (ASCII, no NUL)
 *   27     4    rulesVersion 1
 *   31     4    profile NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB
 *   35     8    slot roles 0..7 of the ONE_CAB profile, one byte each, as
 *               NativeMatchConfigV1_InitArcadeOneCab sets them
 *   43     1    humanCount 1
 *   44     1    driverCount 8
 *   45     1    firstBotSlot 1
 *   46     1    botCount 7
 *   47     1    difficultyCount 3
 *   48     3    difficulty table 0x50, 0xA0, 0xF0
 *   51     1    difficultyPolicy 1 (global: every bot slot equal, humans 0)
 *   52     1    botCharacterPolicy 2 (retail 1P rule, LOAD_Robots1P)
 *   53     1    modePolicy 1 (arcade single race: gameMode1, gameMode2,
 *               and rules all 0)
 *   54     1    candidateCount 8 (LOAD_CHARACTER_ID_COUNT)
 *   55     8    candidates 0..7 in LOAD_Robots1P walk order
 *   63     4    rngDerivationVersion NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION
 *   67     4    seed stream tag NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP
 *   71     1    seedTargetCount 5
 *   72     5    seed target codes 1, 2, 3, 4, 5 in draw order
 *   77     4    randomNumberMask 0xFFFF
 *   81     4    advRngFallback0 0x30215400
 *   85     4    advRngFallback1 0x493583fe
 *   89     4    reservedStreamMask 0x3FF (bit 0 ITEMS, bit 1 HAZARDS,
 *               bits 2..9 BOT[0..7])
 *   93          end
 *
 * Pure: no heap use, no I/O, no hidden state, fully deterministic. The table
 * accessor DifficultyAt returns the table value, or 0 out of range. Every
 * int function returns 1 on success, or 0 with every output untouched on
 * NULL arguments or invalid input.
 */

#define NATIVE_ARCADE_BOT_RULES_V1_VERSION 1u
#define NATIVE_ARCADE_BOT_RULES_V1_TAG "CTRN arcade bot rules v1"
#define NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT 2u
#define NATIVE_ARCADE_BOT_RULES_DRIVER_COUNT 6u
#define NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT 2u
#define NATIVE_ARCADE_BOT_RULES_BOT_COUNT 4u
#define NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT 3u
#define NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY 0x50u
#define NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM 0xA0u
#define NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD 0xF0u
#define NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM
#define NATIVE_ARCADE_BOT_RULES_RANDOM_NUMBER_MASK 0xFFFFu
#define NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0 0x30215400u
#define NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1 0x493583feu
#define NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT 5u
#define NATIVE_ARCADE_BOT_RULES_RESERVED_STREAM_MASK 0x3FFu /* bit0 ITEMS, bit1 HAZARDS, bits 2..9 BOT[0..7]: undrawn in v1 */
#define NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES 111u

/* The ONE_CAB (1P) rules: the 1P V1 encoding and its race shape. */
#define NATIVE_ARCADE_BOT_RULES_1P_V1_TAG "CTRN arcade bot rules 1P v1"
#define NATIVE_ARCADE_BOT_RULES_1P_HUMAN_COUNT 1u
#define NATIVE_ARCADE_BOT_RULES_1P_DRIVER_COUNT 8u
#define NATIVE_ARCADE_BOT_RULES_1P_FIRST_BOT_SLOT 1u
#define NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT 7u
#define NATIVE_ARCADE_BOT_RULES_1P_CANDIDATE_COUNT 8u /* LOAD_CHARACTER_ID_COUNT: the IDs LOAD_Robots1P walks */
#define NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES 93u

/* The larger of BOT_COUNT and 1P_BOT_COUNT: room for either profile's bots. */
#define NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT 7u

/* Retail seed targets, in draw order. The codes are part of the encoding. */
enum NativeArcadeBotRulesSeedTarget
{
	NATIVE_ARCADE_BOT_RULES_SEED_RANDOM_NUMBER = 1,
	NATIVE_ARCADE_BOT_RULES_SEED_ADV_RNG0 = 2,
	NATIVE_ARCADE_BOT_RULES_SEED_ADV_RNG1 = 3,
	NATIVE_ARCADE_BOT_RULES_SEED_PSX_RAND = 4,
	NATIVE_ARCADE_BOT_RULES_SEED_AUDIO_RNG = 5
};

/* The retail RNG seed values for one race, pointer-free. */
struct NativeArcadeRetailRngSeedsV1
{
	uint32_t randomNumber; /* 16-bit LCG state: always masked to RANDOM_NUMBER_MASK */
	uint32_t advRng0;      /* advRng state0 */
	uint32_t advRng1;      /* advRng state1; never both 0 with advRng0 */
	uint32_t psxRandSeed;  /* PSX BIOS rand seed (presentation) */
	uint32_t audioRNG;     /* audio RNG (presentation) */
};

/* The difficulty table {EASY, MEDIUM, HARD}; out of range -> 0. */
uint8_t NativeArcadeBotRules_DifficultyAt(uint32_t index);

/* 1 if value is a difficulty table value, else 0. */
int NativeArcadeBotRules_IsDifficulty(uint32_t value);

/* Always NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES. */
size_t NativeArcadeBotRules_EncodedSizeV1(void);

/*
 * Writes the V1 encoding (the table above). Transactional, like
 * NativeMatchConfigV1_Encode: fails with *writer untouched if writer is NULL
 * or failed, or has less than ENCODED_BYTES of room.
 */
int NativeArcadeBotRules_EncodeV1(struct NativeCodecWriter *writer);

/* SHA-256 of the V1 encoding: the botRulesDigest of every ARCADE_TWO_CAB config. */
int NativeArcadeBotRules_DigestV1(uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

/* Always NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES. */
size_t NativeArcadeBotRules_EncodedSize1PV1(void);

/*
 * Writes the 1P V1 encoding (the table above). Transactional, like
 * EncodeV1: fails with *writer untouched if writer is NULL or failed, or has
 * less than 1P_V1_ENCODED_BYTES of room.
 */
int NativeArcadeBotRules_Encode1PV1(struct NativeCodecWriter *writer);

/* SHA-256 of the 1P V1 encoding: the botRulesDigest of every ARCADE_ONE_CAB config. */
int NativeArcadeBotRules_Digest1PV1(uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

/*
 * The botRulesDigest a config of this profile must carry (RS-19):
 * ARCADE_TWO_CAB -> DigestV1, ARCADE_ONE_CAB -> Digest1PV1. Any other profile
 * fails with *digest untouched.
 */
int NativeArcadeBotRules_DigestForProfileV1(uint32_t profile, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

/*
 * The LOAD_Robots2P rule. human0 and human1 must be distinct base characters
 * (NativeMatchSelect_CharacterIndex). bots receives the racers of the first
 * retail 2P AI set holding neither, in set order, and *aiSetIndex that set's
 * index. Every pair of distinct base characters has such a set.
 */
int NativeArcadeBotRules_ExpectedBots2P(uint8_t human0, uint8_t human1, uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT],
	uint8_t *aiSetIndex);

/*
 * The LOAD_Robots1P rule, mirrored loop for loop (game/LOAD/LOAD_Assets.c:
 * 61-76). human must be a base character (NativeMatchSelect_CharacterIndex).
 * bots receives characterIDs[1..7] of that loop, which are the bots of
 * slots 1..7 in ascending slot order: every base character but human,
 * ascending.
 */
int NativeArcadeBotRules_ExpectedBots1P(uint8_t human, uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT]);

/*
 * Pure seed mapping; draws are in target order (draws[i] is target code
 * i + 1): randomNumber = draws[0] & RANDOM_NUMBER_MASK; advRng0 = draws[1];
 * advRng1 = draws[2], except that when both are 0 they become
 * ADV_RNG_FALLBACK0 and ADV_RNG_FALLBACK1 (BOTS_Adv_AdjustDifficulty's own
 * rule, game/BOTS.c:310-313); psxRandSeed = draws[3]; audioRNG = draws[4].
 */
int NativeArcadeBotRules_MapRetailSeedsV1(const uint32_t draws[NATIVE_ARCADE_BOT_RULES_SEED_TARGET_COUNT],
	struct NativeArcadeRetailRngSeedsV1 *out);

/*
 * Validates *bank, draws exactly SEED_TARGET_COUNT NextU32 values from
 * NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP with
 * NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT as both the stable and the requester
 * slot, in target order, and maps them with MapRetailSeedsV1. Transactional:
 * on any failure neither *bank nor *out changes. No other stream is drawn.
 */
int NativeArcadeBotRules_DeriveRetailSeedsV1(struct NativeDeterministicRngBankV1 *bank,
	struct NativeArcadeRetailRngSeedsV1 *out);

/*
 * 1 only if *config is a race these rules can build:
 * - NativeMatchConfigV1_Validate passes (which also fixes the slot roles,
 *   lifecycles, and the inactive slots);
 * - profile is ARCADE_TWO_CAB or ARCADE_ONE_CAB; gameMode1, gameMode2, and
 *   rules are all 0;
 * - botRulesDigest equals DigestForProfileV1(profile): DigestV1 for
 *   ARCADE_TWO_CAB, Digest1PV1 for ARCADE_ONE_CAB (RS-19);
 * - trackID is a match-select table track and lapCount a table lap option
 *   (NativeMatchSelect_TrackIndex, NativeMatchSelect_LapOptionIndex);
 * - ARCADE_TWO_CAB: the CAB1_HUMAN and CAB2_HUMAN slots
 *   (NativeMatchConfigV1_FindRoleSlot) hold distinct base characters and
 *   difficulty 0, and the BOT slots, in ascending slot order, hold exactly
 *   ExpectedBots2P(CAB1 character, CAB2 character);
 * - ARCADE_ONE_CAB: the CAB1_HUMAN slot holds a base character and
 *   difficulty 0, and the BOT slots, in ascending slot order, hold exactly
 *   ExpectedBots1P(CAB1 character) (RS-20);
 * - the BOT slots all share one difficulty for which IsDifficulty is 1.
 */
int NativeArcadeBotRules_ValidateConfigV1(const struct NativeMatchConfigV1 *config);

#endif
