#include "platform/native_arcade_link_options.h"

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_discovery.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NATIVE_ARCADE_LINK_PREVIEW_LAST NATIVE_ARCADE_LINK_PREVIEW_RESULTS_SOLO_ERROR

/* The group buffer holds the longest name the discovery core accepts. */
_Static_assert(NATIVE_ARCADE_LINK_OPTIONS_GROUP_BYTES == NATIVE_ARCADE_DISCOVERY_GROUP_MAX_CHARS + 1u, "the group buffer fits a 32-character name");
/* The seat preference is the beacon's value as is. */
_Static_assert((NATIVE_ARCADE_LINK_SEAT_AUTO == NATIVE_ARCADE_DISCOVERY_SEAT_AUTO) && (NATIVE_ARCADE_LINK_SEAT_CAB1 == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1) &&
                   (NATIVE_ARCADE_LINK_SEAT_CAB2 == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2),
               "the seat preference values match the beacon's");
/* Every byte is a named field: the 88 bytes of the original fields, then 88
 * of discovery fields, then 8 of lan fields (DISC-19), the sum of their
 * sizes (no padding). */
_Static_assert(offsetof(struct NativeArcadeLinkOptions, seatPreference) == 88u, "the discovery fields follow the original fields");
_Static_assert(offsetof(struct NativeArcadeLinkOptions, lanNetwork) == 176u, "the lan fields follow the discovery fields");
_Static_assert(sizeof(struct NativeArcadeLinkOptions) == 184u, "struct NativeArcadeLinkOptions holds no padding");

static const char k_linkOption[] = "--arcade-link";
static const char k_portOption[] = "--arcade-link-port";
static const char k_peerOption[] = "--arcade-link-peer";
static const char k_groupOption[] = "--arcade-link-group";
static const char k_lanOption[] = "--arcade-link-lan";
static const char k_previewOption[] = "--arcade-link-preview";
static const char k_discoveryPortOption[] = "--arcade-discovery-port";
static const char k_discoveryTargetOption[] = "--arcade-discovery-target";

/* Indexed by enum NativeArcadeLinkPreview. */
static const char *const k_previewNames[NATIVE_ARCADE_LINK_PREVIEW_LAST + 1u] = {
	"none",
	"title",
	"lobby",
	"lobby-connecting",
	"lobby-rejected",
	"match-found",
	"results",
	"results-timeout",
	"results-desync",
	"results-link-error",
	"rematch",
	"exit",
	"exit-opponent-left",
	"select-character",
	"select-track",
	"select-laps",
	"select-wait",
	"select-result",
	"lobby-solo",
	"select-solo",
	"results-solo",
	"results-solo-error",
};

static int NativeArcadeLinkOptions_IsDigit(char c)
{
	return (c >= '0') && (c <= '9');
}

/*
 * Parses 1..maxDigits decimal digits at *cursor into a value no greater than
 * maxValue, advancing *cursor past them. No sign and no whitespace.
 */
static int NativeArcadeLinkOptions_ParseDecimal(const char **cursor, uint32_t maxDigits, uint32_t maxValue, uint32_t *value)
{
	const char *text = *cursor;
	uint32_t digits = 0;
	uint32_t result = 0;

	while (NativeArcadeLinkOptions_IsDigit(text[digits]))
	{
		if (digits == maxDigits)
		{
			return 0;
		}
		result = (result * 10u) + (uint32_t)(text[digits] - '0');
		digits++;
	}
	if ((digits == 0) || (result > maxValue))
	{
		return 0;
	}
	*cursor = text + digits;
	*value = result;
	return 1;
}

static int NativeArcadeLinkOptions_ParsePort(const char *text, uint16_t *port)
{
	const char *cursor = text;
	uint32_t value = 0;

	if ((text == NULL) || !NativeArcadeLinkOptions_ParseDecimal(&cursor, 5u, 65535u, &value) || (*cursor != '\0') ||
	    (value == 0))
	{
		return 0;
	}
	*port = (uint16_t)value;
	return 1;
}

static int NativeArcadeLinkOptions_IsZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;

	for (size_t i = 0; i < size; i++)
	{
		combined |= bytes[i];
	}
	return combined == 0;
}

void NativeArcadeLinkOptions_SetDefaults(struct NativeArcadeLinkOptions *options)
{
	if (options == NULL)
	{
		return;
	}
	memset(options, 0, sizeof(*options));
	options->preview = NATIVE_ARCADE_LINK_PREVIEW_NONE;
}

int NativeArcadeLinkOptions_ParsePeer(const char *text, struct NativeArcadeLinkPeer *peer)
{
	const char *cursor = text;
	uint32_t address = 0;
	uint32_t port = 0;

	if ((text == NULL) || (peer == NULL))
	{
		return 0;
	}
	for (uint32_t octetIndex = 0; octetIndex < 4u; octetIndex++)
	{
		uint32_t octet = 0;

		if (!NativeArcadeLinkOptions_ParseDecimal(&cursor, 3u, 255u, &octet))
		{
			return 0;
		}
		address = (address << 8) | octet;
		if (*cursor != (octetIndex < 3u ? '.' : ':'))
		{
			return 0;
		}
		cursor++;
	}
	if (!NativeArcadeLinkOptions_ParseDecimal(&cursor, 5u, 65535u, &port) || (*cursor != '\0') || (port == 0))
	{
		return 0;
	}
	peer->ipv4 = address;
	peer->port = (uint16_t)port;
	peer->reserved = 0;
	return 1;
}

int NativeArcadeLinkOptions_ParsePreview(const char *text, uint32_t *preview)
{
	if ((text == NULL) || (preview == NULL))
	{
		return 0;
	}
	for (uint32_t i = NATIVE_ARCADE_LINK_PREVIEW_TITLE; i <= NATIVE_ARCADE_LINK_PREVIEW_LAST; i++)
	{
		if (strcmp(text, k_previewNames[i]) == 0)
		{
			*preview = i;
			return 1;
		}
	}
	return 0;
}

const char *NativeArcadeLinkOptions_PreviewName(uint32_t preview)
{
	if (preview > NATIVE_ARCADE_LINK_PREVIEW_LAST)
	{
		return "unknown";
	}
	return k_previewNames[preview];
}

int NativeArcadeLinkOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeLinkOptions *options)
{
	struct NativeArcadeLinkOptions candidate;
	int linkGiven = 0;
	int portGiven = 0;
	int groupGiven = 0;
	int lanGiven = 0;
	int previewGiven = 0;
	int discoveryPortGiven = 0;

	if ((options == NULL) || ((argc > 1) && (argv == NULL)))
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
			return 0;
		}
		if ((strcmp(arg, k_linkOption) != 0) && (strcmp(arg, k_portOption) != 0) && (strcmp(arg, k_peerOption) != 0) && (strcmp(arg, k_groupOption) != 0) &&
		    (strcmp(arg, k_lanOption) != 0) && (strcmp(arg, k_previewOption) != 0) && (strcmp(arg, k_discoveryPortOption) != 0) &&
		    (strcmp(arg, k_discoveryTargetOption) != 0))
		{
			continue;
		}
		if ((index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '-'))
		{
			return 0;
		}
		value = argv[++index];

		if (strcmp(arg, k_linkOption) == 0)
		{
			if (linkGiven)
			{
				return 0;
			}
			if (strcmp(value, "cab1") == 0)
			{
				candidate.localRole = NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
				candidate.seatPreference = NATIVE_ARCADE_LINK_SEAT_CAB1;
			}
			else if (strcmp(value, "cab2") == 0)
			{
				candidate.localRole = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
				candidate.seatPreference = NATIVE_ARCADE_LINK_SEAT_CAB2;
			}
			else if (strcmp(value, "auto") == 0)
			{
				candidate.localRole = 0;
				candidate.seatPreference = NATIVE_ARCADE_LINK_SEAT_AUTO;
			}
			else
			{
				return 0;
			}
			candidate.enabled = 1;
			linkGiven = 1;
		}
		else if (strcmp(arg, k_portOption) == 0)
		{
			if (portGiven || !NativeArcadeLinkOptions_ParsePort(value, &candidate.localPort))
			{
				return 0;
			}
			portGiven = 1;
		}
		else if (strcmp(arg, k_peerOption) == 0)
		{
			if ((candidate.peerCount >= NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS) ||
			    !NativeArcadeLinkOptions_ParsePeer(value, &candidate.peers[candidate.peerCount]))
			{
				return 0;
			}
			candidate.peerCount++;
		}
		else if (strcmp(arg, k_groupOption) == 0)
		{
			/* The discovery core's one group grammar (DISC-9). */
			if (groupGiven || !NativeArcadeDiscovery_GroupNameValid(value))
			{
				return 0;
			}
			memset(candidate.group, 0, sizeof(candidate.group));
			memcpy(candidate.group, value, strlen(value));
			candidate.hasGroup = 1;
			groupGiven = 1;
		}
		else if (strcmp(arg, k_lanOption) == 0)
		{
			/* The discovery core's one lan grammar (DISC-19). */
			if (lanGiven || !NativeArcadeDiscovery_ParseLan(value, &candidate.lanNetwork, &candidate.lanPrefixLength))
			{
				return 0;
			}
			candidate.hasLan = 1;
			lanGiven = 1;
		}
		else if (strcmp(arg, k_discoveryPortOption) == 0)
		{
			if (discoveryPortGiven || !NativeArcadeLinkOptions_ParsePort(value, &candidate.discoveryPort))
			{
				return 0;
			}
			discoveryPortGiven = 1;
		}
		else if (strcmp(arg, k_discoveryTargetOption) == 0)
		{
			/* 0.0.0.0 is no destination: the discovery service refuses it, so
			 * it is refused here rather than silently disabling discovery. */
			if ((candidate.discoveryTargetCount >= NATIVE_ARCADE_LINK_OPTIONS_MAX_DISCOVERY_TARGETS) ||
			    !NativeArcadeLinkOptions_ParsePeer(value, &candidate.discoveryTargets[candidate.discoveryTargetCount]) ||
			    (candidate.discoveryTargets[candidate.discoveryTargetCount].ipv4 == 0))
			{
				return 0;
			}
			candidate.discoveryTargetCount++;
		}
		else
		{
			if (previewGiven || !NativeArcadeLinkOptions_ParsePreview(value, &candidate.preview))
			{
				return 0;
			}
			previewGiven = 1;
		}
	}

	/* DISC-11. A peer makes static mode (seat cab1 or cab2, port required,
	 * no group); no peer makes discovery mode (any seat, the port optional). */
	if (candidate.enabled)
	{
		if (candidate.preview != NATIVE_ARCADE_LINK_PREVIEW_NONE)
		{
			return 0;
		}
		if (candidate.peerCount != 0)
		{
			if ((candidate.seatPreference == NATIVE_ARCADE_LINK_SEAT_AUTO) || (candidate.localPort == 0) || (candidate.hasGroup != 0))
			{
				return 0;
			}
			candidate.discovery = 0;
		}
		else
		{
			if (candidate.localPort == 0)
			{
				candidate.localPort = (uint16_t)NATIVE_ARCADE_LINK_OPTIONS_DEFAULT_LINK_PORT;
			}
			candidate.discovery = 1;
		}
	}
	else if ((candidate.localPort != 0) || (candidate.peerCount != 0) || (candidate.hasGroup != 0) || (candidate.hasLan != 0))
	{
		return 0;
	}

	*options = candidate;
	return 1;
}

int NativeArcadeLinkOptions_ValidateMerged(const struct NativeArcadeLinkOptions *options)
{
	if (options == NULL)
	{
		return 0;
	}
	/* DISC-18: the discovery flags would be silently ignored outside discovery mode. */
	if (((options->discoveryPort != 0) || (options->discoveryTargetCount != 0)) && (options->discovery == 0))
	{
		return 0;
	}
	/* The discovery socket holds its port for the whole run, so a link port
	 * equal to it could never bind. */
	if ((options->discovery != 0) &&
	    (options->localPort == ((options->discoveryPort != 0) ? options->discoveryPort : (uint16_t)NATIVE_ARCADE_LINK_OPTIONS_DEFAULT_DISCOVERY_PORT)))
	{
		return 0;
	}
	/* DISC-19: with a lan, every explicit discovery target and every static
	 * peer lies inside it (a lan the grammar refused never gets here). */
	if (options->hasLan != 0)
	{
		for (uint32_t i = 0; (i < options->discoveryTargetCount) && (i < NATIVE_ARCADE_LINK_OPTIONS_MAX_DISCOVERY_TARGETS); i++)
		{
			if (!NativeArcadeDiscovery_LanContains(options->lanNetwork, options->lanPrefixLength, options->discoveryTargets[i].ipv4))
			{
				return 0;
			}
		}
		for (uint32_t i = 0; (i < options->peerCount) && (i < NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS); i++)
		{
			if (!NativeArcadeDiscovery_LanContains(options->lanNetwork, options->lanPrefixLength, options->peers[i].ipv4))
			{
				return 0;
			}
		}
	}
	return 1;
}

int NativeArcadeLinkFixture_Build(const struct NativeIdentityV1 *identity, struct NativeMatchConfigV1 *config)
{
	/* CAB1 Crash (0) and CAB2 Cortex (1); the bots follow from them (RS-4). */
	static const uint8_t humanCharacters[NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT] = { 0u, 1u };
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_BOT_COUNT];
	uint8_t aiSetIndex = 0;
	struct NativeMatchConfigV1 candidate;

	if ((identity == NULL) || (config == NULL) || NativeArcadeLinkOptions_IsZero(identity->build, sizeof(identity->build)) ||
	    NativeArcadeLinkOptions_IsZero(identity->content, sizeof(identity->content)))
	{
		return 0;
	}

	NativeMatchConfigV1_InitArcadeTwoCab(&candidate);
	candidate.trackID = NATIVE_ARCADE_LINK_FIXTURE_TRACK_ID;
	candidate.gameMode1 = 0;
	candidate.gameMode2 = 0;
	candidate.rules = 0;
	candidate.lapCount = NATIVE_ARCADE_LINK_FIXTURE_LAP_COUNT;
	candidate.tickRateNumerator = NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_NUMERATOR;
	candidate.tickRateDenominator = NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_DENOMINATOR;
	candidate.masterSeed = NATIVE_ARCADE_LINK_FIXTURE_MASTER_SEED;

	/* Humans in slots 0 and 1 at difficulty 0; bots in slots 2..5 in AI set order at the default (RS-3). */
	if (!NativeArcadeBotRules_ExpectedBots2P(humanCharacters[0], humanCharacters[1], bots, &aiSetIndex))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_HUMAN_COUNT; i++)
	{
		candidate.slots[i].characterID = humanCharacters[i];
		candidate.slots[i].difficulty = 0;
	}
	for (uint32_t i = 0; i < NATIVE_ARCADE_BOT_RULES_BOT_COUNT; i++)
	{
		candidate.slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i].characterID = bots[i];
		candidate.slots[NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT + i].difficulty =
			(uint8_t)NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY;
	}
	memcpy(candidate.buildIdentity, identity->build, sizeof(candidate.buildIdentity));
	memcpy(candidate.contentIdentity, identity->content, sizeof(candidate.contentIdentity));

	if (!NativeArcadeBotRules_DigestV1(candidate.botRulesDigest))
	{
		return 0;
	}

	if (!NativeMatchConfigV1_Validate(&candidate) || !NativeArcadeBotRules_ValidateConfigV1(&candidate))
	{
		return 0;
	}
	*config = candidate;
	return 1;
}
