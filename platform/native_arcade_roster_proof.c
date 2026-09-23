#include "platform/native_arcade_roster_proof.h"

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Live roster proof host glue (docs/ROSTER_MILESTONE.md section 3.4). A
 * process-wide singleton, like platform/native_arcade_link_host.c, so its
 * state lives in file-scope statics. It is internal-build evidence plumbing
 * and never touches game state: the game hook
 * (game/MAIN/MainArcadeRosterProof.c) reads the config and dwell from it and
 * hands it the report.
 */

static const char k_proofOption[] = "--arcade-roster-proof";
static const char k_seedOption[] = "--arcade-roster-proof-seed";
static const char k_dwellOption[] = "--arcade-roster-proof-dwell";
static const char k_exitAfterFrameOption[] = "--exit-after-frame";
static const char k_exitAfterFrameEqualsOption[] = "--exit-after-frame=";

struct NativeArcadeRosterProofSingleton
{
	uint8_t active;
	uint8_t exitCodeRecorded; /* 1 once the game hook recorded the proof's exit code */
	uint8_t reserved[2];
	int32_t exitCode;
	struct NativeArcadeRosterProofOptions options;
	struct NativeMatchConfigV1 config;
};

static struct NativeArcadeRosterProofSingleton s_nativeArcadeRosterProof;

void NativeArcadeRosterProofOptions_SetDefaults(struct NativeArcadeRosterProofOptions *options)
{
	if (options == NULL)
	{
		return;
	}
	memset(options, 0, sizeof(*options));
	options->seed = NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_SEED;
	options->dwellTicks = NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_DWELL;
}

static int NativeArcadeRosterProof_HexValue(char c, uint32_t *value)
{
	if ((c >= '0') && (c <= '9'))
	{
		*value = (uint32_t)(c - '0');
		return 1;
	}
	if ((c >= 'a') && (c <= 'f'))
	{
		*value = (uint32_t)(c - 'a') + 10u;
		return 1;
	}
	if ((c >= 'A') && (c <= 'F'))
	{
		*value = (uint32_t)(c - 'A') + 10u;
		return 1;
	}
	return 0;
}

int NativeArcadeRosterProof_ParseSeed(const char *text, uint64_t *value)
{
	uint64_t result = 0;
	size_t digits = 0;

	if ((text == NULL) || (value == NULL))
	{
		return 0;
	}
	if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
	{
		const char *hex = text + 2;

		for (; hex[digits] != '\0'; digits++)
		{
			uint32_t nibble;

			if ((digits == 16u) || !NativeArcadeRosterProof_HexValue(hex[digits], &nibble))
			{
				return 0;
			}
			result = (result << 4) | (uint64_t)nibble;
		}
	}
	else
	{
		for (; text[digits] != '\0'; digits++)
		{
			const uint64_t digit = (uint64_t)(text[digits] - '0');

			if ((text[digits] < '0') || (text[digits] > '9') || (digits == 20u) ||
			    (result > ((UINT64_MAX - digit) / 10u)))
			{
				return 0;
			}
			result = (result * 10u) + digit;
		}
	}
	if (digits == 0u)
	{
		return 0;
	}
	*value = result;
	return 1;
}

/* Decimal 0..NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL, 1..4 digits, nothing else. */
static int NativeArcadeRosterProof_ParseDwell(const char *text, uint32_t *value)
{
	uint32_t result = 0;
	size_t digits = 0;

	for (; text[digits] != '\0'; digits++)
	{
		if ((text[digits] < '0') || (text[digits] > '9') || (digits == 4u))
		{
			return 0;
		}
		result = (result * 10u) + (uint32_t)(text[digits] - '0');
	}
	if ((digits == 0u) || (result > NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL))
	{
		return 0;
	}
	*value = result;
	return 1;
}

/* The value after argv[index], or NULL when it is missing or looks like an option. */
static const char *NativeArcadeRosterProof_Value(int argc, char *argv[], int index)
{
	const char *value;

	if ((index + 1) >= argc)
	{
		return NULL;
	}
	value = argv[index + 1];
	if ((value == NULL) || (value[0] == '-'))
	{
		return NULL;
	}
	return value;
}

int NativeArcadeRosterProofOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeRosterProofOptions *options)
{
	struct NativeArcadeRosterProofOptions candidate;
	int seenProof = 0;
	int seenSeed = 0;
	int seenDwell = 0;

	if ((options == NULL) || (argc < 0) || ((argc > 0) && (argv == NULL)))
	{
		return 0;
	}
	candidate = *options;
	for (int index = 1; index < argc; index++)
	{
		const char *arg = argv[index];
		const char *value;

		if (arg == NULL)
		{
			continue;
		}
		if (strcmp(arg, k_proofOption) == 0)
		{
			size_t length;

			value = NativeArcadeRosterProof_Value(argc, argv, index);
			if ((value == NULL) || seenProof)
			{
				return 0;
			}
			length = strlen(value);
			if ((length == 0u) || (length >= sizeof(candidate.logPath)))
			{
				return 0;
			}
			memset(candidate.logPath, 0, sizeof(candidate.logPath));
			memcpy(candidate.logPath, value, length);
			candidate.enabled = 1u;
			seenProof = 1;
			index++;
		}
		else if (strcmp(arg, k_seedOption) == 0)
		{
			value = NativeArcadeRosterProof_Value(argc, argv, index);
			if ((value == NULL) || seenSeed || !NativeArcadeRosterProof_ParseSeed(value, &candidate.seed))
			{
				return 0;
			}
			seenSeed = 1;
			index++;
		}
		else if (strcmp(arg, k_dwellOption) == 0)
		{
			value = NativeArcadeRosterProof_Value(argc, argv, index);
			if ((value == NULL) || seenDwell || !NativeArcadeRosterProof_ParseDwell(value, &candidate.dwellTicks))
			{
				return 0;
			}
			seenDwell = 1;
			index++;
		}
	}
	/* A seed or dwell without the proof would be silently ignored. */
	if ((seenSeed || seenDwell) && !seenProof)
	{
		return 0;
	}
	*options = candidate;
	return 1;
}

int NativeArcadeRosterProof_NamesExitOption(int argc, char *argv[])
{
	if ((argc <= 0) || (argv == NULL))
	{
		return 0;
	}
	for (int index = 1; index < argc; index++)
	{
		const char *arg = argv[index];

		if ((arg != NULL) && ((strcmp(arg, k_exitAfterFrameOption) == 0) ||
		                      (strncmp(arg, k_exitAfterFrameEqualsOption, sizeof(k_exitAfterFrameEqualsOption) - 1u) == 0)))
		{
			return 1;
		}
	}
	return 0;
}

int NativeArcadeRosterProof_ProofBuildIdentity(uint8_t build[NATIVE_IDENTITY_DIGEST_BYTES])
{
	struct NativeSha256 sha;

	if (build == NULL)
	{
		return 0;
	}
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG, sizeof(NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG) - 1u);
	NativeSha256_Final(&sha, build);
	return 1;
}

int NativeArcadeRosterProof_BuildConfig(const struct NativeIdentityV1 *identity, uint64_t seed,
	struct NativeMatchConfigV1 *config)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 candidate;
	struct NativeMatchSelectChoice choices[NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT];
	struct NativeMatchSelectOutcome outcome;
	uint8_t cab1Slot = 0;
	uint8_t cab2Slot = 0;

	if ((identity == NULL) || (config == NULL) || !NativeArcadeLinkFixture_Build(identity, &base) ||
	    !NativeMatchConfigV1_FindRoleSlot(&base, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot) ||
	    !NativeMatchConfigV1_FindRoleSlot(&base, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &cab2Slot))
	{
		return 0;
	}
	memset(choices, 0, sizeof(choices));
	choices[0].characterID = base.slots[cab1Slot].characterID;
	choices[1].characterID = base.slots[cab2Slot].characterID;
	for (uint32_t human = 0; human < NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT; human++)
	{
		choices[human].trackID = (uint8_t)base.trackID;
		choices[human].lapCount = (uint8_t)base.lapCount;
	}
	choices[0].nonce = seed;
	choices[1].nonce = seed ^ NATIVE_ARCADE_ROSTER_PROOF_NONCE_MIX;
	if (!NativeMatchSelect_Resolve(&base, NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT, choices, &outcome) ||
	    !NativeMatchSelect_BuildConfig(&base, &outcome, &candidate) || !NativeArcadeBotRules_ValidateConfigV1(&candidate))
	{
		return 0;
	}
	*config = candidate;
	return 1;
}

int NativeArcadeRosterProof_Configure(const struct NativeArcadeRosterProofOptions *options,
	const struct NativeIdentityV1 *identity)
{
	struct NativeMatchConfigV1 config;

	NativeArcadeRosterProof_Shutdown();
	if ((options == NULL) || (options->enabled == 0u))
	{
		return 1;
	}
	if ((options->logPath[0] == '\0') || (memchr(options->logPath, '\0', sizeof(options->logPath)) == NULL) ||
	    (options->dwellTicks > NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL) ||
	    !NativeArcadeRosterProof_BuildConfig(identity, options->seed, &config))
	{
		return 0;
	}
	s_nativeArcadeRosterProof.options = *options;
	s_nativeArcadeRosterProof.config = config;
	s_nativeArcadeRosterProof.active = 1u;
	return 1;
}

void NativeArcadeRosterProof_Shutdown(void)
{
	memset(&s_nativeArcadeRosterProof, 0, sizeof(s_nativeArcadeRosterProof));
}

int NativeArcadeRosterProof_Active(void)
{
	return s_nativeArcadeRosterProof.active != 0u;
}

void NativeArcadeRosterProof_RecordExitCode(int exitCode)
{
	if ((s_nativeArcadeRosterProof.active == 0u) || (s_nativeArcadeRosterProof.exitCodeRecorded != 0u))
	{
		return;
	}
	s_nativeArcadeRosterProof.exitCode = (int32_t)exitCode;
	s_nativeArcadeRosterProof.exitCodeRecorded = 1u;
}

int NativeArcadeRosterProof_ExitCode(int inactiveExitCode)
{
	if (s_nativeArcadeRosterProof.active == 0u)
	{
		return inactiveExitCode;
	}
	if (s_nativeArcadeRosterProof.exitCodeRecorded != 0u)
	{
		return (int)s_nativeArcadeRosterProof.exitCode;
	}
	return (int)NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE;
}

const struct NativeMatchConfigV1 *NativeArcadeRosterProof_Config(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? &s_nativeArcadeRosterProof.config : NULL;
}

uint32_t NativeArcadeRosterProof_Dwell(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.options.dwellTicks : 0u;
}

uint64_t NativeArcadeRosterProof_Seed(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.options.seed : 0u;
}

const char *NativeArcadeRosterProof_LogPath(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.options.logPath : "";
}

const char *NativeArcadeRosterProof_ResultName(uint32_t result)
{
	switch (result)
	{
	case NATIVE_ARCADE_ROSTER_PROOF_PASS:
		return "PASS";
	case NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE:
		return "INCOMPLETE";
	case NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED:
		return "REPORT_WRITE_FAILED";
	case NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED:
		return "SETUP_FAILED";
	case NATIVE_ARCADE_ROSTER_PROOF_ARM_FAILED:
		return "ARM_FAILED";
	case NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED:
		return "LAUNCH_FAILED";
	case NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT:
		return "MENU_READY_TIMEOUT";
	case NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT:
		return "VALIDATE_TIMEOUT";
	case NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH:
		return "SEED_MISMATCH";
	case NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING:
		return "EVIDENCE_MISSING";
	default:
		return "UNKNOWN";
	}
}

int NativeArcadeRosterProof_SeedsMatch(const struct NativeArcadeRetailRngSeedsV1 *produced,
	const struct NativeArcadeRetailRngSeedsV1 *stored)
{
	return (produced != NULL) && (stored != NULL) && (produced->randomNumber == stored->randomNumber) &&
	       (produced->advRng0 == stored->advRng0) && (produced->advRng1 == stored->advRng1) &&
	       (produced->psxRandSeed == stored->psxRandSeed) && (produced->audioRNG == stored->audioRNG);
}

uint32_t NativeArcadeRosterProof_FinalResult(uint32_t requested, const struct NativeArcadeRosterProofReport *report)
{
	if (requested != (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS)
	{
		return requested;
	}
	if ((report == NULL) || (report->digestsValid == 0u) || (report->slotsValid == 0u) || (report->seedValid == 0u))
	{
		return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING;
	}
	if (report->seedMatch == 0u)
	{
		return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH;
	}
	return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS;
}

const char *NativeArcadeRosterProof_LaunchWindowName(uint32_t window)
{
	switch (window)
	{
	case NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE:
		return "none";
	case NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE:
		return "title";
	case NATIVE_ARCADE_ROSTER_PROOF_WINDOW_DEMO_RACE:
		return "demo race";
	default:
		return "unknown";
	}
}

static const char *NativeArcadeRosterProof_RoleName(uint8_t role)
{
	switch (role)
	{
	case NATIVE_MATCH_SLOT_ROLE_INACTIVE:
		return "INACTIVE";
	case NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN:
		return "CAB1_HUMAN";
	case NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN:
		return "CAB2_HUMAN";
	case NATIVE_MATCH_SLOT_ROLE_BOT:
		return "BOT";
	default:
		return "UNKNOWN";
	}
}

/* A bounded text builder: every Append fails once the buffer is full. */
struct NativeArcadeRosterProofText
{
	char *buffer;
	size_t size;
	size_t length;
	int ok;
};

static void NativeArcadeRosterProof_Append(struct NativeArcadeRosterProofText *text, const char *format, ...)
{
	va_list args;
	int written;

	if (!text->ok)
	{
		return;
	}
	va_start(args, format);
	written = vsnprintf(text->buffer + text->length, text->size - text->length, format, args);
	va_end(args);
	if ((written < 0) || ((size_t)written >= (text->size - text->length)))
	{
		text->ok = 0;
		return;
	}
	text->length += (size_t)written;
}

static void NativeArcadeRosterProof_AppendDigest(struct NativeArcadeRosterProofText *text, const char *name,
	const uint8_t digest[NATIVE_SHA256_DIGEST_BYTES], int valid)
{
	NativeArcadeRosterProof_Append(text, "%s ", name);
	if (!valid)
	{
		NativeArcadeRosterProof_Append(text, "none\n");
		return;
	}
	for (uint32_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		NativeArcadeRosterProof_Append(text, "%02x", (unsigned)digest[i]);
	}
	NativeArcadeRosterProof_Append(text, "\n");
}

static void NativeArcadeRosterProof_AppendTick(struct NativeArcadeRosterProofText *text, const char *name, uint32_t tick)
{
	if (tick == NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE)
	{
		NativeArcadeRosterProof_Append(text, "%s none\n", name);
	}
	else
	{
		NativeArcadeRosterProof_Append(text, "%s %u\n", name, (unsigned)tick);
	}
}

/* Copies a name field, bounded and NUL-terminated, for printing. */
static void NativeArcadeRosterProof_Name(const char field[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES],
	char out[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES])
{
	memcpy(out, field, NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES);
	out[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES - 1u] = '\0';
}

int NativeArcadeRosterProof_FormatReport(const struct NativeArcadeRosterProofReport *report, char *buffer,
	size_t bufferSize, size_t *length)
{
	struct NativeArcadeRosterProofText text;
	char statusName[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES];
	char failureName[NATIVE_ARCADE_ROSTER_PROOF_NAME_BYTES];
	const int digestsValid = (report != NULL) && (report->digestsValid != 0u);

	if ((report == NULL) || (buffer == NULL) || (bufferSize == 0u) || (length == NULL))
	{
		return 0;
	}
	text.buffer = buffer;
	text.size = bufferSize;
	text.length = 0;
	text.ok = 1;
	buffer[0] = '\0';
	NativeArcadeRosterProof_Name(report->setupStatusName, statusName);
	NativeArcadeRosterProof_Name(report->setupFailureName, failureName);

	NativeArcadeRosterProof_Append(&text, "arcade roster proof v3\n");
	NativeArcadeRosterProof_Append(&text, "result %s (%u)\n", NativeArcadeRosterProof_ResultName(report->result),
		(unsigned)report->result);
	NativeArcadeRosterProof_Append(&text, "setup status %s (%u)\n", statusName, (unsigned)report->setupStatus);
	NativeArcadeRosterProof_Append(&text, "setup failure %s (%u)\n", failureName, (unsigned)report->setupFailure);
	NativeArcadeRosterProof_Append(&text, "seed 0x%08X%08X\n", (unsigned)(uint32_t)(report->seed >> 32),
		(unsigned)(uint32_t)(report->seed & 0xFFFFFFFFu));
	NativeArcadeRosterProof_Append(&text, "dwell %u\n", (unsigned)report->dwellTicks);
	NativeArcadeRosterProof_AppendTick(&text, "menu ready tick", report->menuReadyTick);
	NativeArcadeRosterProof_AppendTick(&text, "demo race tick", report->demoRaceTick);
	NativeArcadeRosterProof_AppendTick(&text, "launch tick", report->launchTick);
	NativeArcadeRosterProof_Append(&text, "launch window %s\n", NativeArcadeRosterProof_LaunchWindowName(report->launchWindow));
	NativeArcadeRosterProof_AppendTick(&text, "validated tick", report->validatedTick);
	NativeArcadeRosterProof_AppendDigest(&text, "config digest", report->configDigest, digestsValid);
	NativeArcadeRosterProof_AppendDigest(&text, "race plan digest", report->racePlanDigest, digestsValid);
	NativeArcadeRosterProof_AppendDigest(&text, "bot setup plan digest", report->botSetupPlanDigest, digestsValid);
	NativeArcadeRosterProof_AppendDigest(&text, "bank digest", report->bankDigest, digestsValid);
	if (report->seedValid == 0u)
	{
		NativeArcadeRosterProof_Append(&text, "seeded none\n");
	}
	else
	{
		NativeArcadeRosterProof_Append(&text,
			"seeded randomNumber 0x%04X advRng0 0x%08X advRng1 0x%08X psxRand 0x%08X audioRNG 0x%08X match %u\n",
			(unsigned)report->seedStored.randomNumber, (unsigned)report->seedStored.advRng0,
			(unsigned)report->seedStored.advRng1, (unsigned)report->seedStored.psxRandSeed,
			(unsigned)report->seedStored.audioRNG, (report->seedMatch != 0u) ? 1u : 0u);
	}
	for (uint32_t slot = 0; slot < NATIVE_ARCADE_ROSTER_PROOF_SLOT_COUNT; slot++)
	{
		const struct NativeArcadeRosterProofSlotLine *line = &report->slots[slot];

		if (report->slotsValid == 0u)
		{
			NativeArcadeRosterProof_Append(&text, "slot %u none\n", (unsigned)slot);
		}
		else if (line->present == 0u)
		{
			NativeArcadeRosterProof_Append(&text, "slot %u role %s\n", (unsigned)slot,
				NativeArcadeRosterProof_RoleName(line->role));
		}
		else
		{
			NativeArcadeRosterProof_Append(&text,
				"slot %u role %s character %u difficulty 0x%02X spawn %u nav %u accel %u\n", (unsigned)slot,
				NativeArcadeRosterProof_RoleName(line->role), (unsigned)line->characterID, (unsigned)line->difficulty,
				(unsigned)line->spawnOrder, (unsigned)line->navPathIndex, (unsigned)line->accelerationOrder);
		}
	}
	if (!text.ok)
	{
		buffer[0] = '\0';
		return 0;
	}
	*length = text.length;
	return 1;
}

int NativeArcadeRosterProof_WriteReport(const struct NativeArcadeRosterProofReport *report)
{
	char text[4096];
	size_t length = 0;
	FILE *file;
	int ok;

	if ((s_nativeArcadeRosterProof.active == 0u) ||
	    !NativeArcadeRosterProof_FormatReport(report, text, sizeof(text), &length))
	{
		return 0;
	}
	file = fopen(s_nativeArcadeRosterProof.options.logPath, "wb");
	if (file == NULL)
	{
		return 0;
	}
	ok = (fwrite(text, 1u, length, file) == length);
	ok = (fclose(file) == 0) && ok;
	return ok;
}
