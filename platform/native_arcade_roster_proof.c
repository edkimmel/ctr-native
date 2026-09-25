#include "platform/native_arcade_roster_proof.h"

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_canonical_state.h"
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
 * (game/MAIN/MainArcadeRosterProof.c) reads the config, profile, dwell, tick
 * count, and the profile's scripted pads from it, hands it one digest line
 * per race tick, and hands it the report.
 */

static const char k_proofOption[] = "--arcade-roster-proof";
static const char k_seedOption[] = "--arcade-roster-proof-seed";
static const char k_dwellOption[] = "--arcade-roster-proof-dwell";
static const char k_ticksOption[] = "--arcade-roster-proof-ticks";
static const char k_profileOption[] = "--arcade-roster-proof-profile";
static const char k_holdOption[] = "--arcade-roster-proof-hold";
static const char k_autopilotOption[] = "--arcade-roster-proof-autopilot";
static const char k_profileTwoCab[] = "two-cab";
static const char k_profileOneCab[] = "one-cab";
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
	uint32_t tickLineCount;
	struct NativeArcadeRosterProofTickLine tickLines[NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS];
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
	options->tickCount = NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_TICKS;
	options->profile = NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_PROFILE;
}

/* "two-cab" or "one-cab", exactly; 0 with *profile untouched otherwise. */
static int NativeArcadeRosterProof_ParseProfile(const char *text, uint32_t *profile)
{
	if (strcmp(text, k_profileTwoCab) == 0)
	{
		*profile = NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB;
		return 1;
	}
	if (strcmp(text, k_profileOneCab) == 0)
	{
		*profile = NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB;
		return 1;
	}
	return 0;
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

/* Decimal minimum..maximum, 1..4 digits, nothing else (the dwell and the
 * tick count; both maxima have four digits). */
static int NativeArcadeRosterProof_ParseDecimal(const char *text, uint32_t minimum, uint32_t maximum, uint32_t *value)
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
	if ((digits == 0u) || (result < minimum) || (result > maximum))
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
	int seenTicks = 0;
	int seenProfile = 0;
	int seenHold = 0;
	int seenAutopilot = 0;

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
			if ((value == NULL) || seenDwell ||
			    !NativeArcadeRosterProof_ParseDecimal(value, 0u, NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL, &candidate.dwellTicks))
			{
				return 0;
			}
			seenDwell = 1;
			index++;
		}
		else if (strcmp(arg, k_ticksOption) == 0)
		{
			value = NativeArcadeRosterProof_Value(argc, argv, index);
			if ((value == NULL) || seenTicks ||
			    !NativeArcadeRosterProof_ParseDecimal(value, 1u, NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS, &candidate.tickCount))
			{
				return 0;
			}
			seenTicks = 1;
			index++;
		}
		else if (strcmp(arg, k_profileOption) == 0)
		{
			value = NativeArcadeRosterProof_Value(argc, argv, index);
			if ((value == NULL) || seenProfile || !NativeArcadeRosterProof_ParseProfile(value, &candidate.profile))
			{
				return 0;
			}
			seenProfile = 1;
			index++;
		}
		else if (strcmp(arg, k_holdOption) == 0)
		{
			/* A flag: it takes no value. */
			if (seenHold)
			{
				return 0;
			}
			candidate.hold = 1u;
			seenHold = 1;
		}
		else if (strcmp(arg, k_autopilotOption) == 0)
		{
			/* A flag: it takes no value. */
			if (seenAutopilot)
			{
				return 0;
			}
			candidate.autopilot = 1u;
			seenAutopilot = 1;
		}
	}
	/* A seed, dwell, tick count, profile, hold, or autopilot without the proof would be silently ignored. */
	if ((seenSeed || seenDwell || seenTicks || seenProfile || seenHold || seenAutopilot) && !seenProof)
	{
		return 0;
	}
	/* Only the autopilot's race runs past MAX_TICKS, and it drives both TWO_CAB humans. */
	if ((candidate.autopilot == 0u) && (candidate.tickCount > NATIVE_ARCADE_ROSTER_PROOF_MAX_TICKS))
	{
		return 0;
	}
	if ((candidate.autopilot != 0u) && (candidate.profile != NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB))
	{
		return 0;
	}
	/* The hold needs the tick lines of HOLD_TICK - 1 and HOLD_TICK. */
	if ((candidate.hold != 0u) && (candidate.tickCount <= NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK))
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

/* TWO_CAB: the fixture resolved through match select with two fixed choices. */
static int NativeArcadeRosterProof_BuildTwoCabConfig(const struct NativeIdentityV1 *identity, uint64_t seed,
	struct NativeMatchConfigV1 *config)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 candidate;
	struct NativeMatchSelectChoice choices[NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT];
	struct NativeMatchSelectOutcome outcome;
	uint8_t cab1Slot = 0;
	uint8_t cab2Slot = 0;

	if (!NativeArcadeLinkFixture_Build(identity, &base) ||
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

/*
 * ONE_CAB (RS-23): no match select. The fixture gives the identity, track,
 * laps, tick rate, the CAB1 character, and the bots' difficulty; the bots are
 * the LOAD_Robots1P rule for that character, and the seed is the masterSeed.
 */
static int NativeArcadeRosterProof_BuildOneCabConfig(const struct NativeIdentityV1 *identity, uint64_t seed,
	struct NativeMatchConfigV1 *config)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 candidate;
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];
	uint8_t cab1Slot = 0;
	uint8_t botDifficulty = 0;
	int botFound = 0;
	uint32_t bot = 0;

	if (!NativeArcadeLinkFixture_Build(identity, &base) ||
	    !NativeMatchConfigV1_FindRoleSlot(&base, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot))
	{
		return 0;
	}
	for (uint32_t slot = 0; (slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT) && !botFound; slot++)
	{
		if (base.slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			botDifficulty = base.slots[slot].difficulty;
			botFound = 1;
		}
	}
	if (!botFound || !NativeArcadeBotRules_ExpectedBots1P(base.slots[cab1Slot].characterID, bots))
	{
		return 0;
	}
	NativeMatchConfigV1_InitArcadeOneCab(&candidate);
	candidate.trackID = base.trackID;
	candidate.lapCount = base.lapCount;
	candidate.tickRateNumerator = base.tickRateNumerator;
	candidate.tickRateDenominator = base.tickRateDenominator;
	candidate.masterSeed = seed;
	memcpy(candidate.buildIdentity, base.buildIdentity, sizeof(candidate.buildIdentity));
	memcpy(candidate.contentIdentity, base.contentIdentity, sizeof(candidate.contentIdentity));
	for (uint32_t slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		struct NativeMatchConfigSlotV1 *target = &candidate.slots[slot];

		if (target->role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN)
		{
			target->characterID = base.slots[cab1Slot].characterID;
			target->difficulty = 0u;
		}
		else if (target->role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			if (bot >= NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT)
			{
				return 0;
			}
			target->characterID = bots[bot];
			target->difficulty = botDifficulty;
			bot++;
		}
	}
	if ((bot != NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT) || !NativeArcadeBotRules_Digest1PV1(candidate.botRulesDigest) ||
	    !NativeArcadeBotRules_ValidateConfigV1(&candidate))
	{
		return 0;
	}
	*config = candidate;
	return 1;
}

int NativeArcadeRosterProof_BuildConfig(const struct NativeIdentityV1 *identity, uint32_t profile, uint64_t seed,
	struct NativeMatchConfigV1 *config)
{
	if ((identity == NULL) || (config == NULL))
	{
		return 0;
	}
	if (profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB)
	{
		return NativeArcadeRosterProof_BuildTwoCabConfig(identity, seed, config);
	}
	if (profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB)
	{
		return NativeArcadeRosterProof_BuildOneCabConfig(identity, seed, config);
	}
	return 0;
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
	    (options->dwellTicks > NATIVE_ARCADE_ROSTER_PROOF_MAX_DWELL) || (options->tickCount == 0u) ||
	    (options->tickCount > NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS) || (options->autopilot > 1u) ||
	    ((options->autopilot == 0u) && (options->tickCount > NATIVE_ARCADE_ROSTER_PROOF_MAX_TICKS)) ||
	    ((options->autopilot != 0u) && (options->profile != NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB)) || (options->hold > 1u) ||
	    ((options->hold != 0u) && (options->tickCount <= NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK)) ||
	    !NativeArcadeRosterProof_BuildConfig(identity, options->profile, options->seed, &config))
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

uint32_t NativeArcadeRosterProof_Profile(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.options.profile : 0u;
}

uint32_t NativeArcadeRosterProof_Dwell(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.options.dwellTicks : 0u;
}

uint32_t NativeArcadeRosterProof_Ticks(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.options.tickCount : 0u;
}

uint32_t NativeArcadeRosterProof_Hold(void)
{
	return ((s_nativeArcadeRosterProof.active != 0u) && (s_nativeArcadeRosterProof.options.hold != 0u)) ? 1u : 0u;
}

uint32_t NativeArcadeRosterProof_Autopilot(void)
{
	return ((s_nativeArcadeRosterProof.active != 0u) && (s_nativeArcadeRosterProof.options.autopilot != 0u)) ? 1u : 0u;
}

void NativeArcadeRosterProof_ScriptedPads(uint32_t profile, uint32_t raceTick,
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT])
{
	uint16_t buttons[2] = {NATIVE_ARCADE_ROSTER_PROOF_BUTTONS_NONE, NATIVE_ARCADE_ROSTER_PROOF_BUTTONS_NONE};

	if (pads == NULL)
	{
		return;
	}
	if (raceTick != NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE)
	{
		const uint32_t phase = raceTick % NATIVE_ARCADE_ROSTER_PROOF_STEER_PERIOD;
		const int steer = (phase >= NATIVE_ARCADE_ROSTER_PROOF_STEER_BEGIN) && (phase < NATIVE_ARCADE_ROSTER_PROOF_STEER_END);

		/* Active low: a held button clears its bit. */
		if (profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB)
		{
			/* Both humans accelerate; CAB2 (player 1) steers. */
			buttons[0] = (uint16_t)(buttons[0] & ~NATIVE_ARCADE_ROSTER_PROOF_BUTTON_CROSS);
			buttons[1] = (uint16_t)(buttons[1] & ~NATIVE_ARCADE_ROSTER_PROOF_BUTTON_CROSS);
			if (steer)
			{
				buttons[1] = (uint16_t)(buttons[1] & ~NATIVE_ARCADE_ROSTER_PROOF_BUTTON_RIGHT);
			}
		}
		else if (profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB)
		{
			/* The one human (CAB1, player 0) accelerates and steers; player 1 stays neutral. */
			buttons[0] = (uint16_t)(buttons[0] & ~NATIVE_ARCADE_ROSTER_PROOF_BUTTON_CROSS);
			if (steer)
			{
				buttons[0] = (uint16_t)(buttons[0] & ~NATIVE_ARCADE_ROSTER_PROOF_BUTTON_RIGHT);
			}
		}
	}
	memset(pads, 0, sizeof(*pads) * NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT);
	for (uint32_t pad = 0; pad < NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT; pad++)
	{
		struct NativeArcadeRosterProofPad *out = &pads[pad];

		for (uint32_t axis = 0; axis < 4u; axis++)
		{
			out->analog[axis] = (uint8_t)NATIVE_ARCADE_ROSTER_PROOF_PAD_ANALOG_CENTRE;
		}
		if (pad < 2u)
		{
			out->status = 0u;
			out->id = (uint8_t)NATIVE_ARCADE_ROSTER_PROOF_PAD_ID_DIGITAL;
			out->buttons[0] = (uint8_t)(buttons[pad] & 0xFFu);
			out->buttons[1] = (uint8_t)(buttons[pad] >> 8);
			out->connected = 1u;
		}
		else
		{
			out->status = (uint8_t)NATIVE_ARCADE_ROSTER_PROOF_PAD_STATUS_DISCONNECTED;
			out->id = (uint8_t)NATIVE_ARCADE_ROSTER_PROOF_PAD_ID_DISCONNECTED;
			out->buttons[0] = 0xFFu;
			out->buttons[1] = 0xFFu;
			out->connected = 0u;
		}
	}
}

int NativeArcadeRosterProof_RecordTick(const struct NativeArcadeRosterProofTickLine *line)
{
	struct NativeArcadeRosterProofSingleton *proof = &s_nativeArcadeRosterProof;

	if ((line == NULL) || (proof->active == 0u) || (line->tick != proof->tickLineCount) ||
	    (proof->tickLineCount >= proof->options.tickCount) || (proof->tickLineCount >= NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS))
	{
		return 0;
	}
	proof->tickLines[proof->tickLineCount] = *line;
	proof->tickLineCount++;
	return 1;
}

uint32_t NativeArcadeRosterProof_TickCount(void)
{
	return (s_nativeArcadeRosterProof.active != 0u) ? s_nativeArcadeRosterProof.tickLineCount : 0u;
}

int NativeArcadeRosterProof_RaceControlDigest(const struct NativeCanonicalStateV1 *state, uint64_t *digest)
{
	struct NativeCanonicalStateV1 race;
	uint32_t index = 0;

	if ((state == NULL) || (digest == NULL))
	{
		return 0;
	}
	race = *state;
	race.control.frameTimer = 0;
	race.control.frameCounter = 0;
	race.control.timer = 0;
	if (!NativeCanonicalStateV1_ComputeDigests(&race))
	{
		return 0;
	}
	while ((index < NATIVE_CANONICAL_DOMAIN_COUNT) && (NativeCanonicalDomainOrder[index] != (uint32_t)NATIVE_CANONICAL_DOMAIN_CONTROL))
	{
		index++;
	}
	if (index >= NATIVE_CANONICAL_DOMAIN_COUNT)
	{
		return 0;
	}
	*digest = race.domainDigests[index];
	return 1;
}

int NativeArcadeRosterProof_SetTickLineV4(struct NativeArcadeRosterProofTickLine *line, uint64_t combined,
	const uint64_t *domainDigests)
{
	/* The line's domain fields, in field order. */
	static const uint32_t domains[NATIVE_CANONICAL_DOMAIN_COUNT] = {NATIVE_CANONICAL_DOMAIN_CONTROL, NATIVE_CANONICAL_DOMAIN_RNG,
		NATIVE_CANONICAL_DOMAIN_INPUT, NATIVE_CANONICAL_DOMAIN_DRIVERS, NATIVE_CANONICAL_DOMAIN_WORLD, NATIVE_CANONICAL_DOMAIN_TOPOLOGY};
	uint64_t digests[NATIVE_CANONICAL_DOMAIN_COUNT];

	if ((line == NULL) || (domainDigests == NULL))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		uint32_t index = 0;

		while ((index < NATIVE_CANONICAL_DOMAIN_COUNT) && (NativeCanonicalDomainOrder[index] != domains[i]))
		{
			index++;
		}
		if (index >= NATIVE_CANONICAL_DOMAIN_COUNT)
		{
			return 0;
		}
		digests[i] = domainDigests[index];
	}
	line->v4 = combined;
	line->v4Control = digests[0];
	line->v4Rng = digests[1];
	line->v4Input = digests[2];
	line->v4Drivers = digests[3];
	line->v4World = digests[4];
	line->v4Topology = digests[5];
	return 1;
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
	case NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT:
		return "RACE_TICK_TIMEOUT";
	case NATIVE_ARCADE_ROSTER_PROOF_DRIVERS_FAILED:
		return "DRIVERS_FAILED";
	case NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED:
		return "DIGEST_FAILED";
	case NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH:
		return "PIN_MISMATCH";
	case NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_TIMEOUT:
		return "TICK_LOG_TIMEOUT";
	case NATIVE_ARCADE_ROSTER_PROOF_V4_FAILED:
		return "V4_FAILED";
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

int NativeArcadeRosterProof_PinsMatch(const struct NativeArcadeRosterProofPins *produced,
	const struct NativeArcadeRosterProofPins *stored)
{
	return (produced != NULL) && (stored != NULL) && (produced->timer == stored->timer) &&
	       (produced->frameTimerConfetti == stored->frameTimerConfetti) &&
	       (produced->rcntTotalUnits == stored->rcntTotalUnits) && (produced->clockFrameStart == stored->clockFrameStart);
}

uint32_t NativeArcadeRosterProof_FinalResult(uint32_t requested, const struct NativeArcadeRosterProofReport *report)
{
	if (requested != (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS)
	{
		return requested;
	}
	if ((report == NULL) || (report->digestsValid == 0u) || (report->slotsValid == 0u) || (report->seedValid == 0u) ||
	    (report->pinValid == 0u) || (report->launchCountersValid == 0u) || (report->countersValid == 0u) ||
	    (report->ticksRequested == 0u) || (report->tickLineCount != report->ticksRequested) ||
	    ((report->holdRequested != 0u) && ((report->holdDone == 0u) || (report->hold.frameTimerValid != 3u))))
	{
		return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING;
	}
	if (report->seedMatch == 0u)
	{
		return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH;
	}
	if (report->pinMatch == 0u)
	{
		return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH;
	}
	return (uint32_t)NATIVE_ARCADE_ROSTER_PROOF_PASS;
}

const char *NativeArcadeRosterProof_ProfileName(uint32_t profile)
{
	switch (profile)
	{
	case NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB:
		return "TWO_CAB";
	case NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB:
		return "ONE_CAB";
	default:
		return "UNKNOWN";
	}
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

/* The "hold" line (LR-S2 (a)); see the header. */
static void NativeArcadeRosterProof_AppendHold(struct NativeArcadeRosterProofText *text,
	const struct NativeArcadeRosterProofReport *report)
{
	const struct NativeArcadeRosterProofHold *hold = &report->hold;

	if (report->holdRequested == 0u)
	{
		NativeArcadeRosterProof_Append(text, "hold none\n");
		return;
	}
	if (report->holdDone == 0u)
	{
		NativeArcadeRosterProof_Append(text, "hold missing\n");
		return;
	}
	NativeArcadeRosterProof_Append(text, "hold tick %u periods %u wall us %llu independent us ", (unsigned)hold->raceTick,
		(unsigned)hold->periods, (unsigned long long)hold->wallUs);
	if (hold->independentValid == 0u)
	{
		NativeArcadeRosterProof_Append(text, "none");
	}
	else
	{
		NativeArcadeRosterProof_Append(text, "%llu", (unsigned long long)hold->independentUs);
	}
	NativeArcadeRosterProof_Append(text, " expected us %llu pumps %u min pumps per period ",
		(unsigned long long)hold->expectedUs, (unsigned)hold->pumps);
	if (hold->minPeriodPumps == UINT32_MAX)
	{
		NativeArcadeRosterProof_Append(text, "none");
	}
	else
	{
		NativeArcadeRosterProof_Append(text, "%u", (unsigned)hold->minPeriodPumps);
	}
	NativeArcadeRosterProof_Append(text, " banners due %u presented %u vsync entry %ld exit %ld frameTimer before ",
		(unsigned)hold->bannersDue, (unsigned)hold->bannersPresented, (long)hold->vsyncEntry, (long)hold->vsyncExit);
	if ((hold->frameTimerValid & 1u) == 0u)
	{
		NativeArcadeRosterProof_Append(text, "none");
	}
	else
	{
		NativeArcadeRosterProof_Append(text, "%ld", (long)hold->frameTimerBefore);
	}
	NativeArcadeRosterProof_Append(text, " after ");
	if ((hold->frameTimerValid & 2u) == 0u)
	{
		NativeArcadeRosterProof_Append(text, "none\n");
	}
	else
	{
		NativeArcadeRosterProof_Append(text, "%ld\n", (long)hold->frameTimerAfter);
	}
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

	NativeArcadeRosterProof_Append(&text, "arcade roster proof v11\n");
	NativeArcadeRosterProof_Append(&text, "drivers digest excludes physics\n");
	NativeArcadeRosterProof_Append(&text, "result %s (%u)\n", NativeArcadeRosterProof_ResultName(report->result),
		(unsigned)report->result);
	NativeArcadeRosterProof_Append(&text, "profile %s\n", NativeArcadeRosterProof_ProfileName(report->profile));
	NativeArcadeRosterProof_Append(&text, "setup status %s (%u)\n", statusName, (unsigned)report->setupStatus);
	NativeArcadeRosterProof_Append(&text, "setup failure %s (%u)\n", failureName, (unsigned)report->setupFailure);
	NativeArcadeRosterProof_Append(&text, "seed 0x%08X%08X\n", (unsigned)(uint32_t)(report->seed >> 32),
		(unsigned)(uint32_t)(report->seed & 0xFFFFFFFFu));
	NativeArcadeRosterProof_Append(&text, "dwell %u\n", (unsigned)report->dwellTicks);
	NativeArcadeRosterProof_Append(&text, "ticks %u\n", (unsigned)report->ticksRequested);
	NativeArcadeRosterProof_AppendTick(&text, "menu ready tick", report->menuReadyTick);
	NativeArcadeRosterProof_AppendTick(&text, "demo race tick", report->demoRaceTick);
	NativeArcadeRosterProof_AppendTick(&text, "launch tick", report->launchTick);
	NativeArcadeRosterProof_Append(&text, "launch window %s\n", NativeArcadeRosterProof_LaunchWindowName(report->launchWindow));
	if (report->launchCountersValid == 0u)
	{
		NativeArcadeRosterProof_Append(&text, "launch counters none\n");
	}
	else
	{
		NativeArcadeRosterProof_Append(&text, "launch counters timer %ld frameCounter %ld frameTimer %ld frameTimerConfetti %ld\n",
			(long)report->launchCounters.timer, (long)report->launchCounters.frameCounter,
			(long)report->launchCounters.frameTimer, (long)report->launchCounters.frameTimerConfetti);
	}
	NativeArcadeRosterProof_AppendTick(&text, "validated tick", report->validatedTick);
	NativeArcadeRosterProof_AppendTick(&text, "race tick 0 tick", report->raceTickZeroTick);
	if (report->countersValid == 0u)
	{
		NativeArcadeRosterProof_Append(&text, "race tick 0 counters none\n");
	}
	else
	{
		NativeArcadeRosterProof_Append(&text, "race tick 0 counters timer %ld frameCounter %ld frameTimer %ld frameTimerConfetti %ld\n",
			(long)report->raceTickZeroCounters.timer, (long)report->raceTickZeroCounters.frameCounter,
			(long)report->raceTickZeroCounters.frameTimer, (long)report->raceTickZeroCounters.frameTimerConfetti);
	}
	NativeArcadeRosterProof_AppendDigest(&text, "config digest", report->configDigest, digestsValid);
	NativeArcadeRosterProof_AppendDigest(&text, "race plan digest", report->racePlanDigest, digestsValid);
	NativeArcadeRosterProof_AppendDigest(&text, "bot setup plan digest", report->botSetupPlanDigest, digestsValid);
	NativeArcadeRosterProof_AppendDigest(&text, "bank digest", report->bankDigest, digestsValid);
	if ((report->seedValid == 0u) || (report->pinValid == 0u))
	{
		NativeArcadeRosterProof_Append(&text, "seeded none\n");
	}
	else
	{
		NativeArcadeRosterProof_Append(&text,
			"seeded randomNumber 0x%04X advRng0 0x%08X advRng1 0x%08X psxRand 0x%08X audioRNG 0x%08X "
			"timer %ld frameTimerConfetti %ld rcntTotalUnits %ld clockFrameStart %ld match %u\n",
			(unsigned)report->seedStored.randomNumber, (unsigned)report->seedStored.advRng0,
			(unsigned)report->seedStored.advRng1, (unsigned)report->seedStored.psxRandSeed,
			(unsigned)report->seedStored.audioRNG, (long)report->pinStored.timer, (long)report->pinStored.frameTimerConfetti,
			(long)report->pinStored.rcntTotalUnits, (long)report->pinStored.clockFrameStart,
			((report->seedMatch != 0u) && (report->pinMatch != 0u)) ? 1u : 0u);
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
	NativeArcadeRosterProof_AppendHold(&text, report);
	if (!text.ok)
	{
		buffer[0] = '\0';
		return 0;
	}
	*length = text.length;
	return 1;
}

/* v11 (LR-S4): the live V4 state's combined and domain digests, the tick
 * line's tail (no newline). Shared with FormatV4Digests (LR-74). */
static void NativeArcadeRosterProof_AppendV4(struct NativeArcadeRosterProofText *text, const struct NativeArcadeRosterProofTickLine *line)
{
	NativeArcadeRosterProof_Append(text, " v4 %08x%08x v4control %08x%08x v4rng %08x%08x v4input %08x%08x",
		(unsigned)(uint32_t)(line->v4 >> 32), (unsigned)(uint32_t)(line->v4 & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->v4Control >> 32), (unsigned)(uint32_t)(line->v4Control & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->v4Rng >> 32), (unsigned)(uint32_t)(line->v4Rng & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->v4Input >> 32), (unsigned)(uint32_t)(line->v4Input & 0xFFFFFFFFu));
	NativeArcadeRosterProof_Append(text, " v4drivers %08x%08x v4world %08x%08x v4topology %08x%08x",
		(unsigned)(uint32_t)(line->v4Drivers >> 32), (unsigned)(uint32_t)(line->v4Drivers & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->v4World >> 32), (unsigned)(uint32_t)(line->v4World & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->v4Topology >> 32), (unsigned)(uint32_t)(line->v4Topology & 0xFFFFFFFFu));
}

int NativeArcadeRosterProof_FormatV4Digests(uint64_t combined, const uint64_t *domainDigests, char *buffer, size_t bufferSize,
	size_t *length)
{
	struct NativeArcadeRosterProofTickLine line;
	struct NativeArcadeRosterProofText text;

	if ((buffer == NULL) || (bufferSize == 0u) || (length == NULL))
	{
		if ((buffer != NULL) && (bufferSize != 0u))
		{
			buffer[0] = '\0';
		}
		return 0;
	}
	buffer[0] = '\0';
	memset(&line, 0, sizeof(line));
	/* The domain digests by name, as the tick line maps them. */
	if (!NativeArcadeRosterProof_SetTickLineV4(&line, combined, domainDigests))
	{
		return 0;
	}
	text.buffer = buffer;
	text.size = bufferSize;
	text.length = 0;
	text.ok = 1;
	NativeArcadeRosterProof_AppendV4(&text, &line);
	if (!text.ok)
	{
		buffer[0] = '\0';
		return 0;
	}
	*length = text.length;
	return 1;
}

int NativeArcadeRosterProof_FormatTickLine(const struct NativeArcadeRosterProofTickLine *line, char *buffer,
	size_t bufferSize, size_t *length)
{
	struct NativeArcadeRosterProofText text;

	if ((line == NULL) || (buffer == NULL) || (bufferSize == 0u) || (length == NULL))
	{
		return 0;
	}
	text.buffer = buffer;
	text.size = bufferSize;
	text.length = 0;
	text.ok = 1;
	buffer[0] = '\0';
	NativeArcadeRosterProof_Append(&text, "tick %u control %08x%08x rcontrol %08x%08x rng %08x%08x input %08x%08x drivers ",
		(unsigned)line->tick, (unsigned)(uint32_t)(line->control >> 32), (unsigned)(uint32_t)(line->control & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->raceControl >> 32), (unsigned)(uint32_t)(line->raceControl & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->rng >> 32), (unsigned)(uint32_t)(line->rng & 0xFFFFFFFFu),
		(unsigned)(uint32_t)(line->input >> 32), (unsigned)(uint32_t)(line->input & 0xFFFFFFFFu));
	for (uint32_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		NativeArcadeRosterProof_Append(&text, "%02x", (unsigned)line->drivers[i]);
	}
	NativeArcadeRosterProof_AppendV4(&text, line);
	NativeArcadeRosterProof_Append(&text, "\n");
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
	const struct NativeArcadeRosterProofSingleton *proof = &s_nativeArcadeRosterProof;
	char text[4096];
	size_t length = 0;
	FILE *file;
	int ok;

	if ((proof->active == 0u) || !NativeArcadeRosterProof_FormatReport(report, text, sizeof(text), &length))
	{
		return 0;
	}
	file = fopen(proof->options.logPath, "wb");
	if (file == NULL)
	{
		return 0;
	}
	ok = (fwrite(text, 1u, length, file) == length);
	for (uint32_t i = 0; ok && (i < proof->tickLineCount); i++)
	{
		ok = NativeArcadeRosterProof_FormatTickLine(&proof->tickLines[i], text, sizeof(text), &length) &&
		     (fwrite(text, 1u, length, file) == length);
	}
	if (ok)
	{
		const int written = snprintf(text, sizeof(text), "end ticks %u\n", (unsigned)proof->tickLineCount);

		ok = (written > 0) && ((size_t)written < sizeof(text)) && (fwrite(text, 1u, (size_t)written, file) == (size_t)written);
	}
	ok = (fclose(file) == 0) && ok;
	return ok;
}
