#include "platform/native_arcade_link_options.h"

#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NATIVE_ARCADE_LINK_PREVIEW_LAST NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT

static const char k_linkOption[] = "--arcade-link";
static const char k_portOption[] = "--arcade-link-port";
static const char k_peerOption[] = "--arcade-link-peer";
static const char k_previewOption[] = "--arcade-link-preview";

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
	int previewGiven = 0;

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
		if ((strcmp(arg, k_linkOption) != 0) && (strcmp(arg, k_portOption) != 0) && (strcmp(arg, k_peerOption) != 0) &&
		    (strcmp(arg, k_previewOption) != 0))
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
			}
			else if (strcmp(value, "cab2") == 0)
			{
				candidate.localRole = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
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
		else
		{
			if (previewGiven || !NativeArcadeLinkOptions_ParsePreview(value, &candidate.preview))
			{
				return 0;
			}
			previewGiven = 1;
		}
	}

	if (candidate.enabled)
	{
		if ((candidate.localPort == 0) || (candidate.peerCount == 0) || (candidate.preview != NATIVE_ARCADE_LINK_PREVIEW_NONE))
		{
			return 0;
		}
	}
	else if ((candidate.localPort != 0) || (candidate.peerCount != 0))
	{
		return 0;
	}

	*options = candidate;
	return 1;
}

int NativeArcadeLinkFixture_Build(const struct NativeIdentityV1 *identity, struct NativeMatchConfigV1 *config)
{
	static const char botRulesText[] = NATIVE_ARCADE_LINK_FIXTURE_BOT_RULES_TEXT;
	struct NativeMatchConfigV1 candidate;
	struct NativeSha256 sha;

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
	for (uint32_t i = 0; i <= 5u; i++)
	{
		candidate.slots[i].characterID = (uint8_t)i;
		candidate.slots[i].difficulty = 0;
	}
	memcpy(candidate.buildIdentity, identity->build, sizeof(candidate.buildIdentity));
	memcpy(candidate.contentIdentity, identity->content, sizeof(candidate.contentIdentity));

	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, botRulesText, sizeof(botRulesText) - 1u);
	NativeSha256_Final(&sha, candidate.botRulesDigest);

	if (!NativeMatchConfigV1_Validate(&candidate))
	{
		return 0;
	}
	*config = candidate;
	return 1;
}
