#ifndef PLATFORM_NATIVE_ARCADE_ROSTER_PROOF_H
#define PLATFORM_NATIVE_ARCADE_ROSTER_PROOF_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

/*
 * Live roster proof, internal builds only (docs/ROSTER_MILESTONE.md section
 * 3.4, R-5b's minimal launcher; R-6 adds scripted pads, per-tick digests,
 * and the ctest). Evidence plumbing: host-local options, the fixed proof
 * config, a game-facing singleton, and the report writer.
 *
 *   --arcade-roster-proof <log path>        enable the proof; the report is
 *                                           written to this path
 *   --arcade-roster-proof-seed <u64>        decimal, or 0x/0X hex; default 1
 *   --arcade-roster-proof-dwell <ticks>     decimal 0..600; default 0
 *
 * Parsing is transactional: on any error the caller's options are left
 * untouched. Arguments that are not one of these three options are ignored,
 * because other host parsers own them. An option whose value is missing (end
 * of argv, a NULL entry, or a next argument starting with '-'), repeated, or
 * malformed is an error, and so is a seed or dwell without
 * --arcade-roster-proof. main.c rejects the proof together with any
 * arcade-link or replay option.
 *
 * Config: the arcade-link fixture (NativeArcadeLinkFixture_Build) for the
 * caller's identity, resolved through match select with two fixed choices:
 * CAB1 picks the fixture's CAB1 character and CAB2 the fixture's CAB2
 * character, both vote the fixture track and lap count, and the nonces are
 * seed (CAB1) and seed XOR NATIVE_ARCADE_ROSTER_PROOF_NONCE_MIX (CAB2)
 * (NativeMatchSelect_Resolve, then NativeMatchSelect_BuildConfig). The same
 * identity and seed always give the same config; the seed reaches the
 * config only through the resolved masterSeed.
 *
 * Identity: the proof is single-machine. When the build identity is unknown
 * (a dirty tree), main.c uses the fixed proof build identity
 * (NativeArcadeRosterProof_ProofBuildIdentity, SHA-256 of
 * NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG) and logs it, but always the real
 * content identity of the open disc.
 *
 * The singleton is inert until Configure succeeds with the proof enabled.
 * No heap use; the report writer is the only I/O.
 */

#define NATIVE_ARCADE_ROSTER_PROOF_PATH_BYTES 260u /* including the NUL */
#define NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_SEED UINT64_C(1)
#define NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_DWELL 0u
#define NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL 600u
#define NATIVE_ARCADE_ROSTER_PROOF_NONCE_MIX UINT64_C(0x9E3779B97F4A7C15)
#define NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG "CTRN arcade roster proof build v1"
#define NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT NATIVE_MATCH_CONFIG_V1_SLOT_COUNT
#define NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES 24u
#define NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE UINT32_MAX

/* Watchdogs and the post-validation wait, in proof ticks (game frames). */
#define NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT_TICKS 3000u
#define NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT_TICKS 1800u
#define NATIVE_ARCADE_ROSTER_PROOF_POST_VALIDATED_TICKS 60u

struct NativeArcadeRosterProofOptions
{
	uint8_t enabled; /* --arcade-roster-proof given */
	uint8_t reserved[3];
	uint32_t dwellTicks;
	uint64_t seed;
	char logPath[NATIVE_ARCADE_ROSTER_PROOF_PATH_BYTES];
};

/* The proof's result, which is also its process exit code. */
enum NativeArcadeRosterProofResult
{
	NATIVE_ARCADE_ROSTER_PROOF_PASS = 0,                  /* VALIDATED, report written */
	NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED = 1,   /* exit code only: the report could not be written */
	NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED = 2,          /* the race setup latched FAILED */
	NATIVE_ARCADE_ROSTER_PROOF_ARM_FAILED = 3,            /* MainArcadeRaceSetup_Arm refused the config */
	NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED = 4,         /* the title left the menu-ready window before launch */
	NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT = 5,    /* no menu-ready frame within MENU_READY_TIMEOUT_TICKS ticks */
	NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT = 6       /* not VALIDATED within VALIDATE_TIMEOUT_TICKS ticks of launch */
};

/* One validated slot, as the race setup's slot facts report it. */
struct NativeArcadeRosterProofSlotLine
{
	uint8_t present;
	uint8_t role; /* NATIVE_MATCH_SLOT_ROLE_* */
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t spawnOrder;
	uint8_t navPathIndex;
	uint8_t accelerationOrder;
	uint8_t reserved;
};

/*
 * The report the game hook fills. Status and failure are the race setup's
 * own codes and names (the platform module does not include game headers).
 * Ticks are proof ticks, TICK_NONE when never reached. digestsValid and
 * slotsValid say whether the digests and slot lines are filled (only once
 * VALIDATED).
 */
struct NativeArcadeRosterProofReport
{
	uint32_t result; /* enum NativeArcadeRosterProofResult */
	uint32_t setupStatus;
	uint32_t setupFailure;
	char setupStatusName[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES];
	char setupFailureName[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES];
	uint64_t seed;
	uint32_t dwellTicks;
	uint32_t menuReadyTick;
	uint32_t launchTick;
	uint32_t validatedTick;
	uint8_t digestsValid;
	uint8_t slotsValid;
	uint8_t reserved[2];
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t racePlanDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t botSetupPlanDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t bankDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeArcadeRosterProofSlotLine slots[NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT];
};

/* NULL is a no-op. Otherwise: disabled, seed 1, dwell 0, empty path. */
void NativeArcadeRosterProofOptions_SetDefaults(struct NativeArcadeRosterProofOptions *options);

/* Returns 1 and updates *options on success; 0 with *options untouched otherwise. */
int NativeArcadeRosterProofOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeRosterProofOptions *options);

/* Strict u64: 1..20 decimal digits no greater than UINT64_MAX, or 0x/0X and
 * 1..16 hex digits, with nothing else. 0 with *value untouched otherwise. */
int NativeArcadeRosterProof_ParseSeed(const char *text, uint64_t *value);

/* SHA-256 of NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG (no NUL). */
int NativeArcadeRosterProof_ProofBuildIdentity(uint8_t build[NATIVE_IDENTITY_DIGEST_BYTES]);

/*
 * The proof config (see above). Returns 0 with *config untouched on NULL
 * arguments, a fixture the identity cannot build, a failed resolution, or a
 * result that fails NativeArcadeBotRules_ValidateConfigV1.
 */
int NativeArcadeRosterProof_BuildConfig(const struct NativeIdentityV1 *identity, uint64_t seed,
	struct NativeMatchConfigV1 *config);

/*
 * Configures the singleton. Disabled (or NULL) options leave it inactive and
 * return 1. Enabled options need a non-empty log path and a config
 * BuildConfig can build for *identity; on failure the singleton stays
 * inactive and 0 is returned.
 */
int NativeArcadeRosterProof_Configure(const struct NativeArcadeRosterProofOptions *options,
	const struct NativeIdentityV1 *identity);

/* Back to inactive. */
void NativeArcadeRosterProof_Shutdown(void);

int NativeArcadeRosterProof_Active(void);

/* The configured config, dwell, seed, and log path; NULL/0/empty when inactive. */
const struct NativeMatchConfigV1 *NativeArcadeRosterProof_Config(void);
uint32_t NativeArcadeRosterProof_Dwell(void);
uint64_t NativeArcadeRosterProof_Seed(void);
const char *NativeArcadeRosterProof_LogPath(void);

/*
 * Formats the report as text into buffer (NUL-terminated) and stores its
 * length without the NUL. Returns 0 on NULL arguments or a buffer too small.
 * The format is line based: a header line, then "result", "setup status",
 * "setup failure", "seed", "dwell", the three ticks, the four digests as
 * lowercase hex (or "none"), and one "slot" line per slot.
 */
int NativeArcadeRosterProof_FormatReport(const struct NativeArcadeRosterProofReport *report, char *buffer,
	size_t bufferSize, size_t *length);

/* Formats the report and writes it to the configured log path. 0 when
 * inactive or on any I/O failure. */
int NativeArcadeRosterProof_WriteReport(const struct NativeArcadeRosterProofReport *report);

/* The fixed name of a result ("PASS", "SETUP_FAILED", ...); "UNKNOWN" otherwise. */
const char *NativeArcadeRosterProof_ResultName(uint32_t result);

#endif
