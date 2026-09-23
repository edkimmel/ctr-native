#ifndef PLATFORM_NATIVE_ARCADE_ROSTER_PROOF_H
#define PLATFORM_NATIVE_ARCADE_ROSTER_PROOF_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_arcade_bot_rules.h"
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
 *   --arcade-roster-proof-dwell <ticks>     decimal 0..7200; default 0
 *
 * The report path is opened as given when the report is written: a relative
 * path resolves against the base directory, because main.c changes into
 * NativeAssets_GetBaseDir() before the game starts, not against the shell's
 * working directory. Pass an absolute path to write elsewhere.
 *
 * Parsing is transactional: on any error the caller's options are left
 * untouched. Arguments that are not one of these three options are ignored,
 * because other host parsers own them. An option whose value is missing (end
 * of argv, a NULL entry, or a next argument starting with '-'), repeated, or
 * malformed is an error, and so is a seed or dwell without
 * --arcade-roster-proof. main.c rejects the proof together with any
 * arcade-link or replay option, and with --exit-after-frame (any frame-capture
 * exit option; NativeArcadeRosterProof_NamesExitOption), which would end the
 * run on a frame count instead of the proof result.
 *
 * Dwell and launch window. The game hook waits for the title's menu-ready
 * frame, then the dwell (in proof ticks), then launches from the first
 * launch window it sees: the title menu-ready window, or the attract demo
 * race running (not loading, the demo flag set, off the main-menu level; the
 * title starts it after TITLE_DEMO_IDLE_FRAMES of idle, and the first idle
 * timeout plays the intro cutscene instead). The dwell range covers the
 * whole first demo race, so a run can prove a launch from inside it.
 * Measured on the reference Debug build (proof ticks): menu ready at 732,
 * the intro cutscene loading from 1415, the title back at 4628, the demo
 * race loading from 5541 and running from about 5567 to 7364 (a tick of
 * load-time jitter run to run); so a dwell of about 4840..6630 launches
 * from inside it (5400 does, at tick 6132). When the
 * dwell ends outside both windows (a load, the intro cutscene) the hook waits
 * up to LAUNCH_WAIT_TIMEOUT_TICKS for one.
 *
 * Process exit codes while the proof is active (enum
 * NativeArcadeRosterProofResult is also the report's result):
 *
 *    0  PASS                 VALIDATED, and the report was written
 *    1  (startup failure)    main.c's generic failure: an invalid option or
 *                            combination, asset or platform initialisation,
 *                            or a proof that could not be configured; the
 *                            proof never ran and wrote no report. Never a
 *                            proof result.
 *   20  INCOMPLETE           the process ended before the proof reported
 *                            (window closed, host quit, CTR_Main returned):
 *                            the default exit code of every exit path while
 *                            the proof is active; exit code only, no report
 *   21  REPORT_WRITE_FAILED  VALIDATED, but the report could not be written;
 *                            exit code only
 *   22  SETUP_FAILED         the race setup latched FAILED (a refused Launch
 *                            included)
 *   23  ARM_FAILED           MainArcadeRaceSetup_Arm refused the config
 *   24  LAUNCH_FAILED        no launch window within LAUNCH_WAIT_TIMEOUT_TICKS
 *                            after the dwell
 *   25  MENU_READY_TIMEOUT   no menu-ready frame within MENU_READY_TIMEOUT_TICKS
 *   26  VALIDATE_TIMEOUT     not VALIDATED within VALIDATE_TIMEOUT_TICKS of launch
 *   27  SEED_MISMATCH        VALIDATED, but a retail seed field read back
 *                            right after the SEEDED writes differs from the
 *                            seed the setup produced (the adapter's field
 *                            mapping is wrong)
 *   28  EVIDENCE_MISSING     VALIDATED, but the digests, the slot facts, or
 *                            the seed readback could not be read; PASS needs
 *                            all three
 *
 * The failure codes start at 20 so that none collides with 1 or with the C
 * runtime's abort() code 3.
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
#define NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL 7200u
#define NATIVE_ARCADE_ROSTER_PROOF_NONCE_MIX UINT64_C(0x9E3779B97F4A7C15)
#define NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG "CTRN arcade roster proof build v1"
#define NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT NATIVE_MATCH_CONFIG_V1_SLOT_COUNT
#define NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES 24u
#define NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE UINT32_MAX

/* Watchdogs and the post-validation wait, in proof ticks (game frames). */
#define NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT_TICKS 3000u
#define NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_WAIT_TIMEOUT_TICKS 3600u
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

/* The proof's result, which is also its process exit code (the table above). */
enum NativeArcadeRosterProofResult
{
	NATIVE_ARCADE_ROSTER_PROOF_PASS = 0,                  /* VALIDATED, report written */
	NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE = 20,           /* exit code only: ended before the proof reported */
	NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED = 21,  /* exit code only: the report could not be written */
	NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED = 22,         /* the race setup latched FAILED */
	NATIVE_ARCADE_ROSTER_PROOF_ARM_FAILED = 23,           /* MainArcadeRaceSetup_Arm refused the config */
	NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED = 24,        /* no launch window within LAUNCH_WAIT_TIMEOUT_TICKS after the dwell */
	NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT = 25,   /* no menu-ready frame within MENU_READY_TIMEOUT_TICKS ticks */
	NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT = 26,     /* not VALIDATED within VALIDATE_TIMEOUT_TICKS ticks of launch */
	NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH = 27,        /* a seeded retail field read back differs from the produced seed */
	NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING = 28      /* VALIDATED without readable digests, slot facts, or seed readback */
};

/* Where the proof launched from (the report's "launch window" line). */
enum NativeArcadeRosterProofLaunchWindow
{
	NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE = 0,     /* not launched */
	NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE = 1,    /* the title menu-ready window */
	NATIVE_ARCADE_ROSTER_PROOF_WINDOW_DEMO_RACE = 2 /* inside the attract demo race */
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
 * VALIDATED). seedValid says whether seedStored holds the retail seed fields
 * read back right after the SEEDED writes, and seedMatch whether they equal
 * the seeds the setup produced (NativeArcadeRosterProof_SeedsMatch).
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
	uint32_t demoRaceTick; /* first tick the proof saw the demo race running */
	uint32_t launchTick;
	uint32_t launchWindow; /* enum NativeArcadeRosterProofLaunchWindow */
	uint32_t validatedTick;
	uint8_t digestsValid;
	uint8_t slotsValid;
	uint8_t seedValid;
	uint8_t seedMatch;
	struct NativeArcadeRetailRngSeedsV1 seedStored;
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

/* 1 when argv names a frame-capture exit option (--exit-after-frame, in its
 * separate or its --exit-after-frame=N form), matched by name only; else 0
 * (also for a NULL argv). main.c rejects these with the proof. */
int NativeArcadeRosterProof_NamesExitOption(int argc, char *argv[]);

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

/* Back to inactive (and no recorded exit code). */
void NativeArcadeRosterProof_Shutdown(void);

int NativeArcadeRosterProof_Active(void);

/* The game hook records the proof's exit code once, right before it requests
 * the exit; later calls and calls while inactive are ignored. */
void NativeArcadeRosterProof_RecordExitCode(int exitCode);

/*
 * The code every host exit path must use: inactiveExitCode when the proof is
 * inactive (so a default run is unchanged); the recorded code once the proof
 * reported; otherwise NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE, so that closing
 * the window or any other early exit never looks like PASS.
 */
int NativeArcadeRosterProof_ExitCode(int inactiveExitCode);

/* The configured config, dwell, seed, and log path; NULL/0/empty when inactive. */
const struct NativeMatchConfigV1 *NativeArcadeRosterProof_Config(void);
uint32_t NativeArcadeRosterProof_Dwell(void);
uint64_t NativeArcadeRosterProof_Seed(void);
const char *NativeArcadeRosterProof_LogPath(void);

/*
 * Formats the report as text into buffer (NUL-terminated) and stores its
 * length without the NUL. Returns 0 on NULL arguments or a buffer too small.
 * The format is line based: a header line ("arcade roster proof v3"), then
 * "result", "setup status", "setup failure", "seed", "dwell", "menu ready
 * tick", "demo race tick", "launch tick", "launch window" (title, demo race,
 * or none), "validated tick", the four digests as lowercase hex (or "none"),
 * the "seeded" line (the five retail seed fields as read back, and "match 1"
 * or "match 0"; "seeded none" without a readback), and one "slot" line per
 * slot.
 */
int NativeArcadeRosterProof_FormatReport(const struct NativeArcadeRosterProofReport *report, char *buffer,
	size_t bufferSize, size_t *length);

/* 1 when every one of the five seeds equals its readback; 0 otherwise (also
 * for NULL). */
int NativeArcadeRosterProof_SeedsMatch(const struct NativeArcadeRetailRngSeedsV1 *produced,
	const struct NativeArcadeRetailRngSeedsV1 *stored);

/*
 * The result a finished proof reports: requested unless it is PASS, and PASS
 * only when the digests, the slot facts, and the seed readback are all valid
 * (else EVIDENCE_MISSING) and the readback matches (else SEED_MISMATCH).
 */
uint32_t NativeArcadeRosterProof_FinalResult(uint32_t requested, const struct NativeArcadeRosterProofReport *report);

/* Formats the report and writes it to the configured log path. 0 when
 * inactive or on any I/O failure. */
int NativeArcadeRosterProof_WriteReport(const struct NativeArcadeRosterProofReport *report);

/* The fixed name of a result ("PASS", "SETUP_FAILED", ...); "UNKNOWN" otherwise. */
const char *NativeArcadeRosterProof_ResultName(uint32_t result);

/* The fixed name of a launch window ("title", "demo race", "none"); "unknown" otherwise. */
const char *NativeArcadeRosterProof_LaunchWindowName(uint32_t window);

#endif
