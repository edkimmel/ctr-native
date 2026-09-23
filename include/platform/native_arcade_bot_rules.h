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
 * the defaults RS-1..RS-9 of section 4). Defines, versions, and digests the
 * native rule choices a two-cabinet arcade race is built from, so that
 * NativeMatchConfigV1.botRulesDigest names them:
 * - shape: the ARCADE_TWO_CAB profile only (RS-1), two humans in slots 0 and
 *   1, four bots in slots 2..5, six drivers (MainInit_Drivers,
 *   game/MAIN/MainInit.c:284-402);
 * - mode: the config's gameMode1, gameMode2, and rules are all 0, meaning a
 *   retail arcade single race without cheats or cup (RS-2);
 * - difficulty: global, as in retail (RS-3). Every bot slot carries the same
 *   retail speed value from the D230 cupDifficulty speed table
 *   (game/230/D230.c), 0x50 easy, 0xA0 medium, 0xF0 hard; human slots
 *   carry 0;
 * - bot characters: the LOAD_Robots2P rule (RS-4,
 *   game/LOAD/LOAD_Assets.c:21-59): the first retail 2P AI set holding
 *   neither human's character, racers in set order;
 * - RNG ownership (RS-5..RS-8, section 3.3): the retail seeds are drawn from
 *   the bank's MATCH_SETUP stream (global slot) in the fixed target order of
 *   enum NativeArcadeBotRulesSeedTarget; ITEMS, HAZARDS, and BOT[0..7] are
 *   reserved and undrawn in v1.
 *
 * tests/native_arcade_bot_rules_isolation_test.cmake checks that the
 * difficulty table mirrors game/230/D230.c and that the advRng fallback
 * constants mirror game/BOTS.c.
 *
 * Canonical encoding V1: NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES bytes,
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

/* SHA-256 of the V1 encoding: the botRulesDigest of every v1 config. */
int NativeArcadeBotRules_DigestV1(uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

/*
 * The LOAD_Robots2P rule. human0 and human1 must be distinct base characters
 * (NativeMatchSelect_CharacterIndex). bots receives the racers of the first
 * retail 2P AI set holding neither, in set order, and *aiSetIndex that set's
 * index. Every pair of distinct base characters has such a set.
 */
int NativeArcadeBotRules_ExpectedBots2P(uint8_t human0, uint8_t human1, uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT],
	uint8_t *aiSetIndex);

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
 * - profile is ARCADE_TWO_CAB; gameMode1, gameMode2, and rules are all 0;
 * - botRulesDigest equals DigestV1;
 * - trackID is a match-select table track and lapCount a table lap option
 *   (NativeMatchSelect_TrackIndex, NativeMatchSelect_LapOptionIndex);
 * - the CAB1_HUMAN and CAB2_HUMAN slots (NativeMatchConfigV1_FindRoleSlot)
 *   hold distinct base characters and difficulty 0;
 * - the BOT slots, in ascending slot order, hold exactly
 *   ExpectedBots2P(CAB1 character, CAB2 character), and all share one
 *   difficulty for which IsDifficulty is 1.
 */
int NativeArcadeBotRules_ValidateConfigV1(const struct NativeMatchConfigV1 *config);

#endif
