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
 *   --arcade-link cab1|cab2          enable the link as cabinet 1 or 2
 *   --arcade-link-port N             local port, decimal 1..65535
 *   --arcade-link-peer a.b.c.d:port  one candidate peer; repeatable, up to
 *                                    NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS
 *   --arcade-link-preview name       drive one screen with no socket
 *
 * Parsing is transactional: on any error the caller's options are left
 * untouched. Arguments that are not one of these four options are ignored,
 * because other host parsers own them. An option whose value is missing (end
 * of argv, a NULL entry, or a next argument starting with '-') is an error.
 * After the scan, an enabled link needs a port and at least one peer and may
 * not also request a preview; a port or peer without --arcade-link is an
 * error, since it would otherwise be silently ignored. A preview on its own
 * is valid.
 *
 * The fixture is the one race both cabinets propose (UX-8). It is fixed by
 * the build rather than chosen per cabinet, because the link handshake is
 * validate-and-reject, not negotiation. Two cabinets with the same build and
 * content identity build byte-identical fixtures. Two cabinets with different
 * builds or discs build fixtures with different build or content identity,
 * which the handshake rejects with CONFIG_MISMATCH; that is the intended
 * "LINK REFUSED: SETTINGS DO NOT MATCH" path.
 *
 * gameMode1, gameMode2, and rules are 0 in the fixture: mapping the fixture
 * onto the retail race flags is Task 7's to define. botRulesDigest is the
 * SHA-256 of NATIVE_ARCADE_LINK_FIXTURE_BOT_RULES_TEXT (without its
 * terminating NUL), a placeholder digest until integration step 3 defines
 * the bot rules. The fixture masterSeed seeds the first match only; each
 * rematch derives a new seed (UX-7).
 *
 * Pure: caller-owned state, no heap use, no I/O, no hidden state, and fully
 * deterministic. The identity is supplied by the caller.
 */

#define NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS 8u

/* Frozen fixture values (UX-8). */
#define NATIVE_ARCADE_LINK_FIXTURE_TRACK_ID 3u /* CRASH_COVE in include/namespace_Level.h */
#define NATIVE_ARCADE_LINK_FIXTURE_LAP_COUNT 3u
#define NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_NUMERATOR 30u
#define NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_DENOMINATOR 1u
#define NATIVE_ARCADE_LINK_FIXTURE_MASTER_SEED UINT64_C(0x4354524e41524331) /* "CTRNARC1" */
#define NATIVE_ARCADE_LINK_FIXTURE_BOT_RULES_TEXT "CTRN arcade-link fixture bot rules v1"

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
	NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT = 17         /* "select-result" */
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
	uint8_t localRole; /* NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN or _CAB2_HUMAN when enabled, else 0 */
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
};

/* NULL is a no-op. Otherwise zeroes the options: disabled, preview NONE,
 * selectEntropy 0. */
void NativeArcadeLinkOptions_SetDefaults(struct NativeArcadeLinkOptions *options);

/* Returns 1 and updates *options on success; 0 with *options untouched
 * otherwise. selectEntropy is never read or changed. */
int NativeArcadeLinkOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeLinkOptions *options);

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
 * digest, or a candidate that fails NativeMatchConfigV1_Validate; otherwise
 * writes the fixture and returns 1.
 *
 * Fixture: profile ARCADE_TWO_CAB; trackID, lapCount, tick rate, and
 * masterSeed from the FIXTURE defines; gameMode1 = gameMode2 = rules = 0;
 * slot characterIDs 0..5 for slots 0..5 (CAB1 Crash, CAB2 Cortex, then
 * bots), difficulty 0; build and content identity copied from *identity;
 * botRulesDigest as described above.
 */
int NativeArcadeLinkFixture_Build(const struct NativeIdentityV1 *identity, struct NativeMatchConfigV1 *config);

#endif
