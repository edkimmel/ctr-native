#include "platform/native_arcade_config.h"

#include "platform/native_arcade_link_options.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Synthetic argv: program, link, seat, port option, port, and one option-value pair per peer. */
#define NATIVE_ARCADE_CONFIG_LINK_ARGV_MAX (5u + (2u * NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS))

enum NativeArcadeConfigKey
{
	NATIVE_ARCADE_CONFIG_KEY_NONE = 0,
	NATIVE_ARCADE_CONFIG_KEY_DATA_DIR,
	NATIVE_ARCADE_CONFIG_KEY_SEAT,
	NATIVE_ARCADE_CONFIG_KEY_PORT,
	NATIVE_ARCADE_CONFIG_KEY_PEER,
	NATIVE_ARCADE_CONFIG_KEY_FULLSCREEN
};

static int NativeArcadeConfig_IsBlank(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\r');
}

static void NativeArcadeConfig_SetStatus(struct NativeArcadeConfigStatus *status, uint32_t error, uint32_t line)
{
	if (status != NULL)
	{
		status->error = error;
		status->line = line;
	}
}

/* Copies a NUL-terminated value that must fit; returns 0 when it does not. */
static int NativeArcadeConfig_CopyValue(char *dst, size_t dstSize, const char *value)
{
	const size_t length = strlen(value);

	if (length >= dstSize)
	{
		return 0;
	}
	memcpy(dst, value, length + 1u);
	return 1;
}

/*
 * Runs the command line's own link parser over
 *   <program> --arcade-link <seat> --arcade-link-port <port> [--arcade-link-peer <peer>]...
 * with the entries that are not NULL. Returns its result; *options is only
 * written on success.
 */
static int NativeArcadeConfig_RunLinkParser(char *seat, char *port, char *const *peers, uint32_t peerCount, struct NativeArcadeLinkOptions *options)
{
	char program[] = NATIVE_ARCADE_CONFIG_DEFAULT_NAME;
	char linkOption[] = "--arcade-link";
	char portOption[] = "--arcade-link-port";
	char peerOption[] = "--arcade-link-peer";
	char *argv[NATIVE_ARCADE_CONFIG_LINK_ARGV_MAX];
	int argc = 0;

	if (peerCount > NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS)
	{
		return 0;
	}
	argv[argc++] = program;
	if (seat != NULL)
	{
		argv[argc++] = linkOption;
		argv[argc++] = seat;
	}
	if (port != NULL)
	{
		argv[argc++] = portOption;
		argv[argc++] = port;
	}
	for (uint32_t i = 0; i < peerCount; i++)
	{
		argv[argc++] = peerOption;
		argv[argc++] = peers[i];
	}
	return NativeArcadeLinkOptions_ApplyArgs(argc, argv, options);
}

/*
 * Checks one link value in isolation with the command line's parser: the
 * other two members of the group are fixed known-good samples.
 */
static int NativeArcadeConfig_LinkValueAccepted(enum NativeArcadeConfigKey key, char *value)
{
	char sampleSeat[] = "cab1";
	char samplePort[] = "1";
	char samplePeer[] = "127.0.0.1:1";
	char *peer = (key == NATIVE_ARCADE_CONFIG_KEY_PEER) ? value : samplePeer;
	struct NativeArcadeLinkOptions probe;

	NativeArcadeLinkOptions_SetDefaults(&probe);
	return NativeArcadeConfig_RunLinkParser((key == NATIVE_ARCADE_CONFIG_KEY_SEAT) ? value : sampleSeat,
	                                        (key == NATIVE_ARCADE_CONFIG_KEY_PORT) ? value : samplePort, &peer, 1u, &probe);
}

static int NativeArcadeConfig_ParseFullscreen(const char *value, int *fullscreen)
{
	if ((strcmp(value, "1") == 0) || (strcmp(value, "yes") == 0) || (strcmp(value, "true") == 0))
	{
		*fullscreen = 1;
		return 1;
	}
	if ((strcmp(value, "0") == 0) || (strcmp(value, "no") == 0) || (strcmp(value, "false") == 0))
	{
		*fullscreen = 0;
		return 1;
	}
	return 0;
}

static enum NativeArcadeConfigKey NativeArcadeConfig_LookupKey(const char *key)
{
	if (strcmp(key, "data_dir") == 0)
	{
		return NATIVE_ARCADE_CONFIG_KEY_DATA_DIR;
	}
	if (strcmp(key, "seat") == 0)
	{
		return NATIVE_ARCADE_CONFIG_KEY_SEAT;
	}
	if (strcmp(key, "port") == 0)
	{
		return NATIVE_ARCADE_CONFIG_KEY_PORT;
	}
	if (strcmp(key, "peer") == 0)
	{
		return NATIVE_ARCADE_CONFIG_KEY_PEER;
	}
	if (strcmp(key, "fullscreen") == 0)
	{
		return NATIVE_ARCADE_CONFIG_KEY_FULLSCREEN;
	}
	return NATIVE_ARCADE_CONFIG_KEY_NONE;
}

/* Parses one line (NUL-terminated, line end removed) into *config. Returns an enum NativeArcadeConfigError. */
static uint32_t NativeArcadeConfig_ParseLine(char *line, struct NativeArcadeConfig *config, uint32_t lineNumber, uint32_t *firstLinkLine)
{
	char *key = line;
	char *equals;
	char *value;
	size_t length;
	enum NativeArcadeConfigKey keyID;

	while (NativeArcadeConfig_IsBlank(*key))
	{
		key++;
	}
	if ((*key == '\0') || (*key == '#') || (*key == ';'))
	{
		return NATIVE_ARCADE_CONFIG_OK;
	}

	equals = strchr(key, '=');
	if (equals == NULL)
	{
		return NATIVE_ARCADE_CONFIG_ERROR_SYNTAX;
	}
	*equals = '\0';
	length = strlen(key);
	while ((length != 0) && NativeArcadeConfig_IsBlank(key[length - 1u]))
	{
		key[--length] = '\0';
	}
	if (length == 0)
	{
		return NATIVE_ARCADE_CONFIG_ERROR_SYNTAX;
	}

	value = equals + 1;
	while (NativeArcadeConfig_IsBlank(*value))
	{
		value++;
	}
	length = strlen(value);
	while ((length != 0) && NativeArcadeConfig_IsBlank(value[length - 1u]))
	{
		value[--length] = '\0';
	}

	keyID = NativeArcadeConfig_LookupKey(key);
	if (keyID == NATIVE_ARCADE_CONFIG_KEY_NONE)
	{
		return NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY;
	}
	if (((keyID == NATIVE_ARCADE_CONFIG_KEY_DATA_DIR) && config->hasDataDir) || ((keyID == NATIVE_ARCADE_CONFIG_KEY_SEAT) && config->hasSeat) ||
	    ((keyID == NATIVE_ARCADE_CONFIG_KEY_PORT) && config->hasPort) || ((keyID == NATIVE_ARCADE_CONFIG_KEY_FULLSCREEN) && config->hasFullscreen))
	{
		return NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY;
	}
	if (length == 0)
	{
		return NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE;
	}

	switch (keyID)
	{
	case NATIVE_ARCADE_CONFIG_KEY_DATA_DIR:
	{
		if (!NativeArcadeConfig_CopyValue(config->dataDir, sizeof(config->dataDir), value))
		{
			return NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE;
		}
		config->hasDataDir = 1;
		return NATIVE_ARCADE_CONFIG_OK;
	}
	case NATIVE_ARCADE_CONFIG_KEY_FULLSCREEN:
	{
		if (!NativeArcadeConfig_ParseFullscreen(value, &config->fullscreen))
		{
			return NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE;
		}
		config->hasFullscreen = 1;
		return NATIVE_ARCADE_CONFIG_OK;
	}
	case NATIVE_ARCADE_CONFIG_KEY_SEAT:
	case NATIVE_ARCADE_CONFIG_KEY_PORT:
	case NATIVE_ARCADE_CONFIG_KEY_PEER:
	{
		if ((keyID == NATIVE_ARCADE_CONFIG_KEY_PEER) && (config->peerCount >= NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS))
		{
			return NATIVE_ARCADE_CONFIG_ERROR_TOO_MANY_PEERS;
		}
		if (!NativeArcadeConfig_LinkValueAccepted(keyID, value))
		{
			return NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE;
		}
		/* An accepted value always fits: a seat is 4 characters, a port
		 * at most 5 digits, a peer at most 21 characters. */
		if (keyID == NATIVE_ARCADE_CONFIG_KEY_SEAT)
		{
			if (!NativeArcadeConfig_CopyValue(config->seat, sizeof(config->seat), value))
			{
				return NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE;
			}
			config->hasSeat = 1;
		}
		else if (keyID == NATIVE_ARCADE_CONFIG_KEY_PORT)
		{
			if (!NativeArcadeConfig_CopyValue(config->port, sizeof(config->port), value))
			{
				return NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE;
			}
			config->hasPort = 1;
		}
		else
		{
			if (!NativeArcadeConfig_CopyValue(config->peers[config->peerCount], sizeof(config->peers[0]), value))
			{
				return NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE;
			}
			config->peerCount++;
		}
		if (*firstLinkLine == 0)
		{
			*firstLinkLine = lineNumber;
		}
		return NATIVE_ARCADE_CONFIG_OK;
	}
	case NATIVE_ARCADE_CONFIG_KEY_NONE:
	default:
	{
		return NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY;
	}
	}
}

void NativeArcadeConfig_SetDefaults(struct NativeArcadeConfig *config)
{
	if (config == NULL)
	{
		return;
	}
	memset(config, 0, sizeof(*config));
}

int NativeArcadeConfig_Parse(const char *text, size_t size, struct NativeArcadeConfig *config, struct NativeArcadeConfigStatus *status)
{
	struct NativeArcadeConfig candidate;
	char line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 1u];
	size_t position = 0;
	uint32_t lineNumber = 0;
	uint32_t firstLinkLine = 0;

	if ((config == NULL) || ((text == NULL) && (size != 0)))
	{
		NativeArcadeConfig_SetStatus(status, NATIVE_ARCADE_CONFIG_ERROR_ARGUMENT, 0);
		return 0;
	}
	if (size > NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES)
	{
		NativeArcadeConfig_SetStatus(status, NATIVE_ARCADE_CONFIG_ERROR_FILE_TOO_LARGE, 0);
		return 0;
	}

	NativeArcadeConfig_SetDefaults(&candidate);
	if ((size >= 3u) && ((unsigned char)text[0] == 0xEFu) && ((unsigned char)text[1] == 0xBBu) && ((unsigned char)text[2] == 0xBFu))
	{
		position = 3u;
	}

	while (position < size)
	{
		size_t end = position;
		size_t length;
		uint32_t error;

		lineNumber++;
		while ((end < size) && (text[end] != '\n'))
		{
			end++;
		}
		length = end - position;
		if ((length != 0) && (text[end - 1u] == '\r'))
		{
			length--;
		}
		if (length > NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS)
		{
			NativeArcadeConfig_SetStatus(status, NATIVE_ARCADE_CONFIG_ERROR_LINE_TOO_LONG, lineNumber);
			return 0;
		}
		if (memchr(text + position, '\0', length) != NULL)
		{
			NativeArcadeConfig_SetStatus(status, NATIVE_ARCADE_CONFIG_ERROR_NUL_BYTE, lineNumber);
			return 0;
		}
		memcpy(line, text + position, length);
		line[length] = '\0';

		error = NativeArcadeConfig_ParseLine(line, &candidate, lineNumber, &firstLinkLine);
		if (error != NATIVE_ARCADE_CONFIG_OK)
		{
			NativeArcadeConfig_SetStatus(status, error, lineNumber);
			return 0;
		}
		position = (end < size) ? (end + 1u) : size;
	}

	/* All-or-none, by the command line's own rules. */
	if (NativeArcadeConfig_HasLink(&candidate))
	{
		struct NativeArcadeLinkOptions probe;

		NativeArcadeLinkOptions_SetDefaults(&probe);
		if (!NativeArcadeConfig_ApplyLink(&candidate, &probe))
		{
			NativeArcadeConfig_SetStatus(status, NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, firstLinkLine);
			return 0;
		}
	}

	*config = candidate;
	NativeArcadeConfig_SetStatus(status, NATIVE_ARCADE_CONFIG_OK, 0);
	return 1;
}

int NativeArcadeConfig_HasLink(const struct NativeArcadeConfig *config)
{
	return (config != NULL) && ((config->hasSeat != 0) || (config->hasPort != 0) || (config->peerCount != 0));
}

int NativeArcadeConfig_ApplyLink(const struct NativeArcadeConfig *config, struct NativeArcadeLinkOptions *options)
{
	struct NativeArcadeConfig copy;
	char *peers[NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS];

	if ((options == NULL) || !NativeArcadeConfig_HasLink(config) || (config->peerCount > NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS))
	{
		return 0;
	}
	/* The link parser takes a mutable argv; it never writes through it. */
	copy = *config;
	for (uint32_t i = 0; i < copy.peerCount; i++)
	{
		peers[i] = copy.peers[i];
	}
	return NativeArcadeConfig_RunLinkParser((copy.hasSeat != 0) ? copy.seat : NULL, (copy.hasPort != 0) ? copy.port : NULL, peers, copy.peerCount, options);
}

const char *NativeArcadeConfig_ErrorText(uint32_t error)
{
	switch (error)
	{
	case NATIVE_ARCADE_CONFIG_OK:
	{
		return "no error";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_ARGUMENT:
	{
		return "invalid argument";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_FILE_TOO_LARGE:
	{
		return "file is larger than 16384 bytes";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_LINE_TOO_LONG:
	{
		return "line is longer than 1023 characters";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_NUL_BYTE:
	{
		return "line contains a NUL byte";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_SYNTAX:
	{
		return "expected 'key = value'";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY:
	{
		return "unknown key (expected data_dir, seat, port, peer, or fullscreen)";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY:
	{
		return "key given more than once (only peer may repeat)";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE:
	{
		return "empty value";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE:
	{
		return "invalid value (seat: cab1 or cab2; port: 1-65535; peer: a.b.c.d:port; fullscreen: 0, 1, yes, no, true, or false)";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_TOO_MANY_PEERS:
	{
		return "more than 8 peer lines";
	}
	case NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE:
	{
		return "incomplete link group: seat, port, and at least one peer must all be set, or none of them";
	}
	default:
	{
		return "unknown error";
	}
	}
}

int NativeArcadeConfig_ParseArgs(int argc, char *argv[], struct NativeArcadeConfigArgs *args)
{
	struct NativeArcadeConfigArgs candidate;

	if ((args == NULL) || ((argc > 1) && (argv == NULL)))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	for (int index = 1; index < argc; index++)
	{
		const char *arg = argv[index];
		const char **target = NULL;

		if (arg == NULL)
		{
			return 0;
		}
		if ((strcmp(arg, "--arcade-link") == 0) || (strcmp(arg, "--arcade-link-port") == 0) || (strcmp(arg, "--arcade-link-peer") == 0) ||
		    (strcmp(arg, "--arcade-link-preview") == 0))
		{
			candidate.namesLinkOption = 1;
			continue;
		}
		if ((strcmp(arg, "--fullscreen") == 0) || (strcmp(arg, "--windowed") == 0))
		{
			candidate.namesWindowMode = 1;
			continue;
		}
		if (strcmp(arg, "--config") == 0)
		{
			target = &candidate.configPath;
		}
		else if (strcmp(arg, "--data-dir") == 0)
		{
			target = &candidate.dataDir;
		}
		else
		{
			continue;
		}
		if ((*target != NULL) || (index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '\0') || (argv[index + 1][0] == '-'))
		{
			return 0;
		}
		*target = argv[++index];
	}

	*args = candidate;
	return 1;
}
