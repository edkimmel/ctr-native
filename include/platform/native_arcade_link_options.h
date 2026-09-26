#ifndef PLATFORM_NATIVE_ARCADE_LINK_OPTIONS_H
#define PLATFORM_NATIVE_ARCADE_LINK_OPTIONS_H

#include <stdint.h>

#include "platform/native_identity.h"
#include "platform/native_match_config.h"

/*
 * Arcade-link host options and fixed race fixture
 * (docs/GAME_LOOP_UI_MILESTONE.md sections 2.5 and 3, UX-8).
 *
 * Host options are host-local launch configuration, never match identity:
 *
 *   --arcade-link cab1|cab2|auto     enable the link as cabinet 1 or 2, or
 *                                    with the seat left to discovery
 *   --arcade-link-port N             local port, decimal 1..65535
 *   --arcade-link-peer a.b.c.d:port  one candidate peer; repeatable, up to
 *                                    NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS
 *   --arcade-link-group name         discovery group (DISC-9)
 *   --arcade-link-preview name       drive one screen with no socket
 *   --arcade-discovery-port N        discovery bind port, 1..65535 (DISC-18)
 *   --arcade-discovery-target a.b.c.d:port
 *                                    one explicit beacon target; repeatable,
 *                                    up to NATIVE_ARCADE_LINK_OPTIONS_MAX_DISCOVERY_TARGETS
 *
 * Parsing is transactional: on any error the caller's options are left
 * untouched. Arguments that are not one of these options are ignored,
 * because other host parsers own them. An option whose value is missing (end
 * of argv, a NULL entry, or a next argument starting with '-') is an error.
 * Every option but the peer and the discovery target may be given once.
 *
 * After the scan (docs/DISCOVERY_MILESTONE.md DISC-11):
 * - Static mode, a peer given: the seat must be cab1 or cab2 and the port is
 *   required, and no group may be given (it would be silently ignored).
 * - Discovery mode, --arcade-link without a peer: seat cab1, cab2, or auto
 *   (cab1 and cab2 become the beacon's seat preference); the port is
 *   optional and defaults to NATIVE_ARCADE_LINK_OPTIONS_DEFAULT_LINK_PORT;
 *   the group is optional (NATIVE_ARCADE_DISCOVERY_DEFAULT_GROUP when
 *   unset, applied by the consumer).
 * - An enabled link may not also request a preview. A port, peer, or group
 *   without --arcade-link is an error. A preview on its own is valid.
 * The two discovery flags are parsed here but checked only after the
 * config file's link group has been merged, by
 * NativeArcadeLinkOptions_ValidateMerged (DISC-18): they are not link-group
 * options, so argv naming them does not make the file's link group ignored.
 *
 * The fixture is the one race both cabinets propose (UX-8). It is fixed by
 * the build rather than chosen per cabinet, because the link handshake is
 * validate-and-reject, not negotiation. Two cabinets with the same build and
 * content identity build byte-identical fixtures. Two cabinets with different
 * builds or discs build fixtures with different build or content identity,
 * which the handshake rejects with CONFIG_MISMATCH; that is the intended
 * "LINK REFUSED: SETTINGS DO NOT MATCH" path.
 *
 * The fixture is a race the arcade bot rules v1 can build
 * (include/platform/native_arcade_bot_rules.h, docs/ROSTER_MILESTONE.md
 * section 4): gameMode1, gameMode2, and rules are 0, meaning a retail arcade
 * single race without cheats or cup (RS-2); botRulesDigest is
 * NativeArcadeBotRules_DigestV1, the SHA-256 of the bot rules' canonical
 * encoding; the human slots carry difficulty 0 and every bot slot the
 * default medium speed NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY, 0xA0
 * (RS-3); and the bot characters are the LOAD_Robots2P rule's retail 2P AI
 * set for the two human characters, in set order (RS-4). The fixture
 * masterSeed seeds the first match only; each rematch derives a new seed
 * (UX-7).
 *
 * Pure: caller-owned state, no heap use, no I/O, no hidden state, and fully
 * deterministic. The identity is supplied by the caller.
 */

#define NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS 8u

/* Discovery (docs/DISCOVERY_MILESTONE.md DISC-11, DISC-18). */
#define NATIVE_ARCADE_LINK_OPTIONS_MAX_DISCOVERY_TARGETS  4u
#define NATIVE_ARCADE_LINK_OPTIONS_GROUP_BYTES            33u   /* 32 characters and the NUL */
#define NATIVE_ARCADE_LINK_OPTIONS_DEFAULT_LINK_PORT      7001u /* discovery mode without a port */
#define NATIVE_ARCADE_LINK_OPTIONS_DEFAULT_DISCOVERY_PORT 7000u /* discoveryPort 0, applied at use */

/* seatPreference values; the same numbers as the beacon's seat preference. */
#define NATIVE_ARCADE_LINK_SEAT_AUTO 0u
#define NATIVE_ARCADE_LINK_SEAT_CAB1 1u
#define NATIVE_ARCADE_LINK_SEAT_CAB2 2u

/* Frozen fixture values (UX-8). */
#define NATIVE_ARCADE_LINK_FIXTURE_TRACK_ID 3u /* CRASH_COVE in include/namespace_Level.h */
#define NATIVE_ARCADE_LINK_FIXTURE_LAP_COUNT 3u
#define NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_NUMERATOR 30u
#define NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_DENOMINATOR 1u
#define NATIVE_ARCADE_LINK_FIXTURE_MASTER_SEED UINT64_C(0x4354524e41524331) /* "CTRNARC1" */

/* Screens reachable through --arcade-link-preview, by name in comments. */
enum NativeArcadeLinkPreview
{
	NATIVE_ARCADE_LINK_PREVIEW_NONE = 0,                  /* "none" (not accepted as a name) */
	NATIVE_ARCADE_LINK_PREVIEW_TITLE = 1,                 /* "title" */
	NATIVE_ARCADE_LINK_PREVIEW_LOBBY_WAITING = 2,         /* "lobby" */
	NATIVE_ARCADE_LINK_PREVIEW_LOBBY_CONNECTING = 3,      /* "lobby-connecting" */
	NATIVE_ARCADE_LINK_PREVIEW_LOBBY_REJECTED = 4,        /* "lobby-rejected" */
	NATIVE_ARCADE_LINK_PREVIEW_MATCH_FOUND = 5,           /* "match-found" */
	NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED = 6,      /* "results" */
	NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT = 7,  /* "results-timeout" */
	NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC = 8,        /* "results-desync" */
	NATIVE_ARCADE_LINK_PREVIEW_RESULTS_LINK_ERROR = 9,    /* "results-link-error" */
	NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT = 10,         /* "rematch" */
	NATIVE_ARCADE_LINK_PREVIEW_EXIT = 11,                 /* "exit" */
	NATIVE_ARCADE_LINK_PREVIEW_EXIT_OPPONENT_LEFT = 12,   /* "exit-opponent-left" */
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER = 13,     /* "select-character" */
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK = 14,         /* "select-track" */
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS = 15,          /* "select-laps" */
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT = 16,          /* "select-wait" */
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT = 17,        /* "select-result" */
	/* Solo (docs/SOLO_CAB_MILESTONE.md SOLO-S3): the offer, the one-human
	 * select, and solo RESULTS with the other cabinet heard or a race error. */
	NATIVE_ARCADE_LINK_PREVIEW_LOBBY_SOLO = 18,           /* "lobby-solo" */
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_SOLO = 19,          /* "select-solo" */
	NATIVE_ARCADE_LINK_PREVIEW_RESULTS_SOLO = 20,         /* "results-solo" */
	NATIVE_ARCADE_LINK_PREVIEW_RESULTS_SOLO_ERROR = 21    /* "results-solo-error" */
};

/* ipv4 and port are host byte order, the same meaning as NativeUdpTransportAddress. */
struct NativeArcadeLinkPeer
{
	uint32_t ipv4;
	uint16_t port;
	uint16_t reserved;
};

struct NativeArcadeLinkOptions
{
	uint8_t enabled;   /* --arcade-link given */
	uint8_t localRole; /* NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN or _CAB2_HUMAN for cab1 or cab2, else 0 (auto included) */
	uint16_t localPort;
	uint32_t peerCount;
	struct NativeArcadeLinkPeer peers[NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS];
	uint32_t preview; /* enum NativeArcadeLinkPreview */
	uint32_t reserved;
	/* Host-local entropy for the select nonces (docs/MATCH_SELECT_MILESTONE.md
	 * section 2.6). Never parsed from argv: ApplyArgs leaves it unchanged and
	 * SetDefaults zeroes it; main.c fills it for a link run only. It is not
	 * match identity: it reaches the match only through the exchanged select
	 * nonces and so the agreed masterSeed. Previews and default runs never
	 * read it. */
	uint64_t selectEntropy;
	/* Discovery (docs/DISCOVERY_MILESTONE.md DISC-11, DISC-13, DISC-18). */
	uint8_t seatPreference; /* NATIVE_ARCADE_LINK_SEAT_*: auto, cab1, or cab2; 0 when disabled */
	uint8_t discovery;      /* 1 when enabled with no peer (discovery mode), else 0 */
	uint8_t hasGroup;       /* --arcade-link-group given */
	char group[NATIVE_ARCADE_LINK_OPTIONS_GROUP_BYTES]; /* NUL-terminated when hasGroup, else empty */
	uint16_t discoveryPort; /* --arcade-discovery-port, or 0 for NATIVE_ARCADE_LINK_OPTIONS_DEFAULT_DISCOVERY_PORT */
	uint16_t discoveryReserved;
	uint32_t discoveryTargetCount;
	struct NativeArcadeLinkPeer discoveryTargets[NATIVE_ARCADE_LINK_OPTIONS_MAX_DISCOVERY_TARGETS];
	uint32_t discoveryReserved2;
	/* The discovery instance nonce (DISC-13). Like selectEntropy, never
	 * parsed from argv: ApplyArgs leaves it unchanged and SetDefaults zeroes
	 * it; main.c fills it for a discovery-mode run only. Not match identity. */
	uint64_t discoveryNonce;
};

/* NULL is a no-op. Otherwise zeroes the options: disabled, preview NONE,
 * selectEntropy and discoveryNonce 0, no group, no discovery flags. */
void NativeArcadeLinkOptions_SetDefaults(struct NativeArcadeLinkOptions *options);

/* Returns 1 and updates *options on success; 0 with *options untouched
 * otherwise. selectEntropy and discoveryNonce are never read or changed. */
int NativeArcadeLinkOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeLinkOptions *options);

/*
 * The post-merge check (DISC-18), run after argv and the config file's link
 * group have both been applied: --arcade-discovery-port and
 * --arcade-discovery-target need discovery mode, since they would otherwise
 * be silently ignored. Returns 1 when the options are consistent, 0 for
 * NULL or a discovery flag without discovery mode. Never writes.
 */
int NativeArcadeLinkOptions_ValidateMerged(const struct NativeArcadeLinkOptions *options);

/*
 * Strict "a.b.c.d:port": four decimal octets 0..255 of 1-3 digits, one ':',
 * and a decimal port 1..65535 of 1-5 digits, with nothing else. Returns 0
 * with *peer untouched on anything else. 127.0.0.1 gives ipv4 0x7F000001.
 */
int NativeArcadeLinkOptions_ParsePeer(const char *text, struct NativeArcadeLinkPeer *peer);

/* Returns 0 with *preview untouched on NULL or an unknown name ("none" is unknown). */
int NativeArcadeLinkOptions_ParsePreview(const char *text, uint32_t *preview);

/* "none" for NONE, the option name for TITLE..SELECT_RESULT, else "unknown". */
const char *NativeArcadeLinkOptions_PreviewName(uint32_t preview);

/*
 * Builds the fixed two-cabinet fixture for the caller's identity. Returns 0
 * with *config untouched on NULL arguments, an all-zero build or content
 * digest, or a candidate that fails NativeMatchConfigV1_Validate or
 * NativeArcadeBotRules_ValidateConfigV1; otherwise writes the fixture and
 * returns 1.
 *
 * Fixture: profile ARCADE_TWO_CAB; trackID, lapCount, tick rate, and
 * masterSeed from the FIXTURE defines; gameMode1 = gameMode2 = rules = 0;
 * slot 0 CAB1 Crash (characterID 0) and slot 1 CAB2 Cortex (1), difficulty
 * 0; slots 2..5 the bots NativeArcadeBotRules_ExpectedBots2P(0, 1) in set
 * order (retail 2P AI set 0: Polar 6, N. Gin 4, Tiny 2, Coco 3), each at
 * difficulty NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY (0xA0); slots 6..7
 * inactive; build and content identity copied from *identity;
 * botRulesDigest = NativeArcadeBotRules_DigestV1.
 */
int NativeArcadeLinkFixture_Build(const struct NativeIdentityV1 *identity, struct NativeMatchConfigV1 *config);

#endif
