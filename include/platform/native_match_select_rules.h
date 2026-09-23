#ifndef PLATFORM_NATIVE_MATCH_SELECT_RULES_H
#define PLATFORM_NATIVE_MATCH_SELECT_RULES_H

#include <stdint.h>

#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

/*
 * Match-select resolution rules (docs/MATCH_SELECT_MILESTONE.md section 2.1,
 * with OD-2 of section 3). Turns every human's choice (character, track
 * vote, lap vote, nonce) plus the agreed base config into one outcome that
 * every cabinet computes identically, and builds the resolved config from it.
 *
 * Human index h is the cabinet role minus 1 (CAB1 -> 0, CAB2 -> 1; indices 2
 * and 3 are reserved for step 8). Resolve accepts humanCount 1..4 on any
 * valid base, but BuildConfig builds only an outcome whose humanCount equals
 * the base's number of human-role slots (CAB1_HUMAN and CAB2_HUMAN): exactly
 * 2 on ARCADE_TWO_CAB and exactly 1 on ARCADE_ONE_CAB. NativeMatchConfigV1
 * has only two human roles, so humanCount 3 and 4 cannot be built until
 * step 8.
 *
 * The tables mirror retail (docs/MATCH_SELECT_MILESTONE.md section 1, and
 * tests/native_match_select_rules_isolation_test.cmake checks the mirror):
 * - characters: the base characters 0..7 (enum Characters,
 *   include/namespace_Vehicle.h:37-55); 8..14 need save unlocks and 15
 *   (NITROS_OXIDE) is not selectable.
 * - tracks: the levelIDs of the `arcadeTracks` rows (game/230/D230.c:561-599)
 *   whose unlock field is MM_TRACK_UNLOCK_ALWAYS (0xFFFF), in retail menu
 *   order. OXIDE_STATION (13, 1P only) and TURBO_TRACK (17, save unlock) are
 *   not offered.
 * - laps: the nonzero `lapCountByRow` rows (game/230/D230.c:621): 3, 5, 7.
 * - AI sets: the seven retail 2P robot sets `characterIDs_2P_AIs`
 *   (game/zGlobal_DATA.c:3558-3580), in order.
 *
 * Every hash input is a fixed ASCII tag (no NUL) followed by fixed-width
 * little-endian fields, so the results are identical on every host.
 *
 * Pure: caller-owned state, no heap use, no I/O, no hidden state, fully
 * deterministic. The table accessors (CharacterAt, TrackAt, LapOptionAt,
 * AiSetRacer) return the table value, or 0xff out of range. Every other
 * function returns 1 on success, or 0 with every output untouched on NULL
 * arguments or invalid input.
 */

#define NATIVE_MATCH_SELECT_MAX_HUMANS 4u
#define NATIVE_MATCH_SELECT_CHARACTER_COUNT 8u /* base characters 0..7 */
#define NATIVE_MATCH_SELECT_TRACK_COUNT 16u
#define NATIVE_MATCH_SELECT_LAP_OPTION_COUNT 3u /* 3, 5, 7 */
#define NATIVE_MATCH_SELECT_AI_SET_COUNT 7u
#define NATIVE_MATCH_SELECT_AI_SET_RACERS 4u
#define NATIVE_MATCH_SELECT_AI_SET_NONE 0xffu
#define NATIVE_MATCH_SELECT_DRAW_DOMAIN_TRACK 1u
#define NATIVE_MATCH_SELECT_DRAW_DOMAIN_LAPS 2u
#define NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES 8u

/* Table access, in retail menu order. Out of range -> 0xff. */
uint8_t NativeMatchSelect_CharacterAt(uint32_t index);
uint8_t NativeMatchSelect_TrackAt(uint32_t index);
uint8_t NativeMatchSelect_LapOptionAt(uint32_t index);

/* Index lookup: 1 and *index on success; 0 with *index untouched if the value
 * is not in the table or index is NULL. */
int NativeMatchSelect_CharacterIndex(uint8_t characterID, uint32_t *index);
int NativeMatchSelect_TrackIndex(uint8_t trackID, uint32_t *index);
int NativeMatchSelect_LapOptionIndex(uint8_t lapCount, uint32_t *index);

/* The retail 2P AI set `setIndex`, racer `racer`; out of range -> 0xff. */
uint8_t NativeMatchSelect_AiSetRacer(uint32_t setIndex, uint32_t racer);

/* One human's choice. reserved must be all zero. */
struct NativeMatchSelectChoice
{
	uint8_t characterID; /* a table character */
	uint8_t trackID;     /* a table track: this human's vote */
	uint8_t lapCount;    /* a table lap option: this human's vote */
	uint8_t reserved[5];
	uint64_t nonce;
};

struct NativeMatchSelectOutcome
{
	uint64_t masterSeed;
	uint8_t trackID;
	uint8_t lapCount;
	uint8_t humanCount;
	uint8_t botCount;
	uint8_t trackDrawn;               /* 1 if the track came from a tie draw */
	uint8_t lapsDrawn;                /* 1 if the lap count came from a tie draw */
	uint8_t characterReassignedMask;  /* bit h: human h was reassigned */
	uint8_t aiSetIndex;               /* retail set used, or AI_SET_NONE */
	uint8_t humanCharacter[NATIVE_MATCH_SELECT_MAX_HUMANS]; /* first humanCount used, rest 0 */
	uint8_t botCharacter[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT]; /* first botCount used, rest 0 */
};

/*
 * Match seed. humanCount is 1..4 and nonces has humanCount entries.
 * digest = SHA-256("CTRN match select seed v1" || baseDigest (32 bytes) ||
 * humanCount (1 byte) || nonces[0..humanCount-1] (8 bytes LE each)).
 * *seedOut = the first little-endian 8-byte word of digest, trying bytes
 * [0,8), [8,16), [16,24), [24,32) in order, that is nonzero and differs from
 * baseMasterSeed. Returns 0 (seedOut untouched) if no word qualifies.
 */
int NativeMatchSelect_DeriveSeed(const uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES], uint64_t baseMasterSeed,
	uint32_t humanCount, const uint64_t nonces[], uint64_t *seedOut);

/*
 * Tie draw. domain is DRAW_DOMAIN_TRACK or DRAW_DOMAIN_LAPS; candidateCount
 * is at least 1. value = the first little-endian 8-byte word of
 * SHA-256("CTRN match select draw v1" || domain (1 byte) || matchSeed
 * (8 bytes LE)); *indexOut = value % candidateCount. The domain keeps the
 * track and lap draws independent; the modulo bias is below 2^-60.
 */
int NativeMatchSelect_Draw(uint64_t matchSeed, uint8_t domain, uint32_t candidateCount, uint32_t *indexOut);

/*
 * Resolves the humans' choices against *base into *out.
 *
 * Fails if base fails NativeMatchConfigV1_Validate, humanCount is not 1..4,
 * or any choice has a character, track, or lap count outside the tables or a
 * nonzero reserved byte.
 *
 * - masterSeed: DeriveSeed(NativeMatchConfigV1_Digest(base),
 *   base->masterSeed, humanCount, the choices' nonces).
 * - Track and laps, each by plurality: the table entries with the most
 *   votes, in table order, are the candidates. One candidate wins outright
 *   (drawn flag 0); otherwise Draw(masterSeed, the item's domain, count)
 *   picks one (drawn flag 1). Two humans who disagree get a 50/50 draw
 *   between their two votes (SEL-1).
 * - Characters are unique (OD-2; the MM_Characters_PreventOverlap rule,
 *   game/230/MM_Characters.c:649-713): every human's chosen character is
 *   marked taken; then for h = 1..humanCount-1 in role order, a human whose
 *   character an earlier human currently holds gets the lowest-ID unmarked
 *   base character, which is then marked, and bit h of
 *   characterReassignedMask is set.
 * - Bots: botCount is the number of NATIVE_MATCH_SLOT_ROLE_BOT slots in base.
 *   With two humans and four bots, the bots are the first retail 2P AI set
 *   holding neither human's final character, in set order, and aiSetIndex is
 *   its index (the LOAD_Robots2P rule, game/LOAD/LOAD_Assets.c:21-59); every
 *   pair of distinct base characters has such a set. Otherwise the bots are
 *   the first botCount ascending base characters no human holds (the
 *   provisional rule for 1, 3, or 4 humans until step 8), aiSetIndex is
 *   AI_SET_NONE, and resolution fails if there are not enough.
 */
int NativeMatchSelect_Resolve(const struct NativeMatchConfigV1 *base, uint32_t humanCount,
	const struct NativeMatchSelectChoice choices[], struct NativeMatchSelectOutcome *out);

/*
 * SHA-256("CTRN match select outcome v1" || baseDigest (32 bytes) || the
 * outcome in declaration order: masterSeed (8 bytes LE), trackID, lapCount,
 * humanCount, botCount, trackDrawn, lapsDrawn, characterReassignedMask,
 * aiSetIndex (1 byte each), humanCharacter[4], botCharacter[8]). Peers
 * compare its first NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES bytes.
 */
int NativeMatchSelect_OutcomeDigest(const uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES],
	const struct NativeMatchSelectOutcome *outcome, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

/*
 * Builds the resolved config: a copy of *base with trackID, lapCount,
 * masterSeed, each human h's slot characterID (the slot whose role is
 * NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN + h, via
 * NativeMatchConfigV1_FindRoleSlot), and the bot slots' characterIDs in
 * ascending slot order taken from *outcome. Everything else (profile, tick
 * rate, identity, difficulty, bot rules, gameMode and rules) is unchanged.
 *
 * Fails if base is invalid; outcome->humanCount differs from base's number of
 * human-role slots (so one human on a two-cab base, and humanCount 3 or 4 on
 * any base, never build, although Resolve accepts them); outcome->botCount
 * differs from base's bot slot count; or the outcome is not one Resolve could
 * produce for that shape:
 * - trackID is not a table track, or lapCount is not a table lap option;
 * - a used human character (the first humanCount) or bot character (the
 *   first botCount) is not a table character, or any two of those
 *   humanCount + botCount characters are equal;
 * - an unused humanCharacter or botCharacter entry is nonzero;
 * - masterSeed is 0 or equals base->masterSeed;
 * - trackDrawn or lapsDrawn is not 0 or 1;
 * - characterReassignedMask has a bit at or above humanCount;
 * - aiSetIndex is neither AI_SET_NONE nor below AI_SET_COUNT.
 * It also fails if the result fails NativeMatchConfigV1_Validate.
 */
int NativeMatchSelect_BuildConfig(const struct NativeMatchConfigV1 *base, const struct NativeMatchSelectOutcome *outcome,
	struct NativeMatchConfigV1 *config);

#endif
