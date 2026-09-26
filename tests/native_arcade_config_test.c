#include "platform/native_arcade_config.h"

#include "platform/native_arcade_link_options.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define ARGC(args) ((int)(sizeof(args) / sizeof((args)[0])))

static char s_big[NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES + 64u];

static void Sentinel(struct NativeArcadeConfig *config)
{
	memset(config, 0xA5, sizeof(*config));
}

static int ParseText(const char *text, struct NativeArcadeConfig *config, struct NativeArcadeConfigStatus *status)
{
	return NativeArcadeConfig_Parse(text, strlen(text), config, status);
}

/* Parse must fail with (error, line) and leave a sentinel-filled config byte-identical. */
static int RejectsAt(const char *text, size_t size, uint32_t error, uint32_t line)
{
	struct NativeArcadeConfig config;
	struct NativeArcadeConfig snapshot;
	struct NativeArcadeConfigStatus status;

	Sentinel(&config);
	snapshot = config;
	status.error = 0xFFFFFFFFu;
	status.line = 0xFFFFFFFFu;
	if (NativeArcadeConfig_Parse(text, size, &config, &status))
	{
		fprintf(stderr, "accepted: %.60s\n", text);
		return 0;
	}
	if ((status.error != error) || (status.line != line))
	{
		fprintf(stderr, "error %u line %u, expected error %u line %u: %.60s\n", (unsigned)status.error, (unsigned)status.line, (unsigned)error,
		        (unsigned)line, text);
		return 0;
	}
	return memcmp(&config, &snapshot, sizeof(config)) == 0;
}

static int RejectsTextAt(const char *text, uint32_t error, uint32_t line)
{
	return RejectsAt(text, strlen(text), error, line);
}

static int TestDefaults(void)
{
	struct NativeArcadeConfig config;
	struct NativeArcadeConfigStatus status;

	NativeArcadeConfig_SetDefaults(NULL);
	Sentinel(&config);
	NativeArcadeConfig_SetDefaults(&config);
	CHECK((config.hasDataDir == 0) && (config.hasSeat == 0) && (config.hasPort == 0) && (config.hasFullscreen == 0));
	CHECK((config.hasRenderScale == 0) && (config.hasTextureFilter == 0) && (config.renderScaleText[0] == '\0') && (config.textureFilterText[0] == '\0'));
	CHECK((config.peerCount == 0) && (config.fullscreen == 0) && (config.dataDir[0] == '\0'));
	CHECK(!NativeArcadeConfig_HasLink(&config));
	CHECK(!NativeArcadeConfig_HasLink(NULL));

	/* An empty file and a comment-only file set nothing. */
	Sentinel(&config);
	CHECK(NativeArcadeConfig_Parse(NULL, 0, &config, &status));
	CHECK((status.error == NATIVE_ARCADE_CONFIG_OK) && (status.line == 0));
	CHECK((config.hasDataDir == 0) && (config.hasSeat == 0) && (config.peerCount == 0) && (config.hasFullscreen == 0));
	Sentinel(&config);
	CHECK(ParseText("# comment\n\n   ; other\n\t\n", &config, NULL));
	CHECK((config.hasDataDir == 0) && (config.hasSeat == 0) && (config.hasPort == 0) && (config.peerCount == 0) && (config.hasFullscreen == 0));
	CHECK((config.hasRenderScale == 0) && (config.hasTextureFilter == 0));

	/* NULL arguments. */
	CHECK(!NativeArcadeConfig_Parse("seat = cab1", 11, NULL, &status));
	CHECK(status.error == NATIVE_ARCADE_CONFIG_ERROR_ARGUMENT);
	CHECK(RejectsAt(NULL, 3, NATIVE_ARCADE_CONFIG_ERROR_ARGUMENT, 0));
	return 0;
}

static int TestGrammar(void)
{
	static const char text[] = "\xEF\xBB\xBF"
	                           "# arcade.cfg\r\n"
	                           "; also a comment\r\n"
	                           "   # indented comment\r\n"
	                           "\r\n"
	                           "  data_dir\t=   C:\\Program Files\\CTR Data = mine  \t\r\n"
	                           "seat=cab2\r\n"
	                           "\tport =7002\r\n"
	                           "peer= 10.0.0.1:7001 \r\n"
	                           "peer = 127.0.0.1:7003\r\n"
	                           "render_scale\t=  6 \t\r\n"
	                           "texture_filter=nearest\r\n"
	                           "fullscreen = yes";
	struct NativeArcadeConfig config;
	struct NativeArcadeConfigStatus status;

	Sentinel(&config);
	CHECK(NativeArcadeConfig_Parse(text, sizeof(text) - 1u, &config, &status));
	CHECK((status.error == NATIVE_ARCADE_CONFIG_OK) && (status.line == 0));
	CHECK(config.hasDataDir == 1);
	CHECK(strcmp(config.dataDir, "C:\\Program Files\\CTR Data = mine") == 0);
	CHECK((config.hasSeat == 1) && (strcmp(config.seat, "cab2") == 0));
	CHECK((config.hasPort == 1) && (strcmp(config.port, "7002") == 0));
	CHECK(config.peerCount == 2);
	CHECK(strcmp(config.peers[0], "10.0.0.1:7001") == 0);
	CHECK(strcmp(config.peers[1], "127.0.0.1:7003") == 0);
	CHECK((config.hasFullscreen == 1) && (config.fullscreen == 1));
	CHECK((config.hasRenderScale == 1) && (strcmp(config.renderScaleText, "6") == 0));
	CHECK((config.hasTextureFilter == 1) && (strcmp(config.textureFilterText, "nearest") == 0));
	CHECK(NativeArcadeConfig_HasLink(&config));

	/* LF only, a trailing newline, and a lone data_dir: no link group. */
	Sentinel(&config);
	CHECK(ParseText("data_dir = ..\\ctr data\n", &config, NULL));
	CHECK((config.hasDataDir == 1) && (strcmp(config.dataDir, "..\\ctr data") == 0));
	CHECK((config.hasSeat == 0) && (config.hasPort == 0) && (config.peerCount == 0) && (config.hasFullscreen == 0));
	CHECK((config.hasRenderScale == 0) && (config.hasTextureFilter == 0));
	CHECK(!NativeArcadeConfig_HasLink(&config));

	/* A BOM alone, and a BOM before a comment. */
	CHECK(ParseText("\xEF\xBB\xBF", &config, NULL));
	CHECK(ParseText("\xEF\xBB\xBF# c\r\n", &config, NULL));
	/* A BOM that is not at the start is part of the line. */
	CHECK(RejectsTextAt("# c\n\xEF\xBB\xBF" "fullscreen = 1\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 2));

	/* A line of exactly 1023 characters is accepted, with or without CRLF. */
	{
		char line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 3u];

		memset(line, 'x', NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS);
		line[0] = '#';
		line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS] = '\r';
		line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 1u] = '\n';
		line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 2u] = '\0';
		CHECK(ParseText(line, &config, NULL));
		line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS] = '\0';
		CHECK(ParseText(line, &config, NULL));

		/* A data_dir of 1023 - strlen("data_dir=") characters fills the line. */
		memset(line, 'd', NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS);
		memcpy(line, "data_dir=", 9);
		line[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS] = '\0';
		CHECK(ParseText(line, &config, NULL));
		CHECK(strlen(config.dataDir) == NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS - 9u);
	}

	/* A file of exactly 16384 bytes is accepted. */
	memset(s_big, '\n', NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES);
	s_big[0] = '#';
	CHECK(NativeArcadeConfig_Parse(s_big, NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES, &config, &status));
	return 0;
}

static int TestFullscreen(void)
{
	static const struct
	{
		const char *text;
		int value;
	} accepted[] = {
		{ "fullscreen = 1", 1 }, { "fullscreen = yes", 1 }, { "fullscreen = true", 1 },
		{ "fullscreen = 0", 0 }, { "fullscreen = no", 0 },  { "fullscreen = false", 0 },
	};
	static const char *const rejected[] = { "fullscreen = on", "fullscreen = YES", "fullscreen = 2", "fullscreen = 01", "fullscreen = 1 # c", "fullscreen = true1" };
	struct NativeArcadeConfig config;

	for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++)
	{
		Sentinel(&config);
		CHECK(ParseText(accepted[i].text, &config, NULL));
		CHECK((config.hasFullscreen == 1) && (config.fullscreen == accepted[i].value));
		CHECK(!NativeArcadeConfig_HasLink(&config));
	}
	for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
	{
		CHECK(RejectsTextAt(rejected[i], NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 1));
	}
	return 0;
}

static int TestErrors(void)
{
	char text[NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 64u];

	/* File over 16 KiB: line 0, even when every line is valid. */
	memset(s_big, '\n', sizeof(s_big));
	CHECK(RejectsAt(s_big, NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES + 1u, NATIVE_ARCADE_CONFIG_ERROR_FILE_TOO_LARGE, 0));

	/* Line over 1023 characters (the line end does not count). */
	memcpy(text, "# a\n\n", 5);
	memset(text + 5, '#', NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 1u);
	memcpy(text + 5 + NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 1u, "\r\n", 3);
	CHECK(RejectsTextAt(text, NATIVE_ARCADE_CONFIG_ERROR_LINE_TOO_LONG, 3));

	/* NUL byte. */
	CHECK(RejectsAt("seat = cab1\nport = 7\0001\n", 23, NATIVE_ARCADE_CONFIG_ERROR_NUL_BYTE, 2));
	/* A UTF-16 file (BOM FF FE) is a NUL byte on line 1, and the message says how to save it. */
	CHECK(RejectsAt("\xFF\xFE" "s\0e\0a\0t\0", 10, NATIVE_ARCADE_CONFIG_ERROR_NUL_BYTE, 1));
	CHECK(strstr(NativeArcadeConfig_ErrorText(NATIVE_ARCADE_CONFIG_ERROR_NUL_BYTE), "(save the file as UTF-8 or ANSI text)") != NULL);

	/* Syntax: no '=', or an empty key. */
	CHECK(RejectsTextAt("# c\nseat cab1\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 2));
	CHECK(RejectsTextAt("\n\n = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 3));
	CHECK(RejectsTextAt("=\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 1));

	/* A bare CR (not part of a CRLF line end) is a syntax error at its line:
	 * inside a value, before a CRLF, around the key, and old Mac line ends. */
	CHECK(RejectsTextAt("# c\r\ndata_dir = C:\\ctr\rdata\r\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 2));
	CHECK(RejectsTextAt("fullscreen = 1\n\nfullscreen = 1\r\r\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 3));
	CHECK(RejectsTextAt("\rseat = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 1));
	CHECK(RejectsTextAt("seat = cab1\rport = 7001\rpeer = 1.2.3.4:5\r\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 1));
	CHECK(RejectsTextAt("# c\n\r\n\r\r\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 3));
	/* A comment line is checked too, and a CR that ends the file is a line end. */
	CHECK(RejectsTextAt("# a\rb\n", NATIVE_ARCADE_CONFIG_ERROR_SYNTAX, 1));
	{
		struct NativeArcadeConfig config;

		CHECK(ParseText("fullscreen = 1\r", &config, NULL));
		CHECK((config.hasFullscreen == 1) && (config.fullscreen == 1));
	}

	/* Unknown keys: exact and lowercase only. */
	CHECK(RejectsTextAt("fullscreen = 1\nSeat = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 2));
	CHECK(RejectsTextAt("data dir = x\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 1));
	CHECK(RejectsTextAt("\n\n\nrenderscale = 2\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 4));
	CHECK(RejectsTextAt("Render_scale = 2\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 1));
	CHECK(RejectsTextAt("texture-filter = bilinear\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 1));
	CHECK(RejectsTextAt("# c\nwindow_mode = fullscreen\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 2));
	CHECK(strstr(NativeArcadeConfig_ErrorText(NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY), "render_scale, or texture_filter") != NULL);
	CHECK(strstr(NativeArcadeConfig_ErrorText(NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE), "render_scale: 1, 2, 3, 4, 6, or 8; texture_filter: nearest or bilinear") != NULL);
	CHECK(RejectsTextAt("peers = 1.2.3.4:5\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 1));

	/* Duplicate non-repeatable keys. */
	CHECK(RejectsTextAt("data_dir = a\ndata_dir = a\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 2));
	CHECK(RejectsTextAt("seat = cab1\nport = 7001\npeer = 1.2.3.4:5\nseat = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 4));
	CHECK(RejectsTextAt("port = 7001\nport = 7002\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 2));
	CHECK(RejectsTextAt("fullscreen = 1\n# c\nfullscreen = 1\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 3));

	/* Empty values. */
	CHECK(RejectsTextAt("data_dir =\n", NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE, 1));
	CHECK(RejectsTextAt("# c\npeer =  \t \r\n", NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE, 2));
	CHECK(RejectsTextAt("seat=", NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE, 1));

	/* Bad link values, rejected by the link parser's own grammar. */
	{
		static const char *const badSeats[] = { "seat = cab3", "seat = CAB1", "seat = cab1 # c", "seat = -cab1", "seat = cab" };
		static const char *const badPorts[] = { "port = 0", "port = 65536", "port = 70a", "port = +7001", "port = -1", "port = 007001", "port = 7 001" };
		static const char *const badPeers[] = {
			"peer = 1.2.3.4",     "peer = 1.2.3.4:0",    "peer = 256.1.1.1:5", "peer = 1.2.3:5",      "peer = localhost:7001",
			"peer = 1.2.3.4:5 x", "peer = 1.2.3.4:65536", "peer = -1.2.3.4:5",  "peer = 1.2.3.4:5:6",
		};

		for (size_t i = 0; i < sizeof(badSeats) / sizeof(badSeats[0]); i++)
		{
			snprintf(text, sizeof(text), "# c\n%s\n", badSeats[i]);
			CHECK(RejectsTextAt(text, NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 2));
		}
		for (size_t i = 0; i < sizeof(badPorts) / sizeof(badPorts[0]); i++)
		{
			snprintf(text, sizeof(text), "seat = cab1\n%s\n", badPorts[i]);
			CHECK(RejectsTextAt(text, NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 2));
		}
		for (size_t i = 0; i < sizeof(badPeers) / sizeof(badPeers[0]); i++)
		{
			snprintf(text, sizeof(text), "seat = cab1\nport = 1\n%s\n", badPeers[i]);
			CHECK(RejectsTextAt(text, NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 3));
		}
	}

	/* The ninth peer. */
	CHECK(RejectsTextAt("seat = cab1\nport = 7001\n"
	                    "peer = 10.0.0.1:1\npeer = 10.0.0.2:2\npeer = 10.0.0.3:3\npeer = 10.0.0.4:4\n"
	                    "peer = 10.0.0.5:5\npeer = 10.0.0.6:6\npeer = 10.0.0.7:7\npeer = 10.0.0.8:8\n"
	                    "peer = 10.0.0.9:9\n",
	                    NATIVE_ARCADE_CONFIG_ERROR_TOO_MANY_PEERS, 11));

	/* Link group all-or-none, reported at the first link line. */
	CHECK(RejectsTextAt("# c\nseat = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 2));
	CHECK(RejectsTextAt("fullscreen = 1\nport = 7001\npeer = 1.2.3.4:5\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 2));
	CHECK(RejectsTextAt("data_dir = x\n\npeer = 1.2.3.4:5\nseat = cab2\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 3));
	CHECK(RejectsTextAt("seat = cab1\nport = 7001\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 1));
	CHECK(RejectsTextAt("port = 7001\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 1));
	CHECK(RejectsTextAt("peer = 1.2.3.4:5\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 1));

	/* The first error wins. */
	CHECK(RejectsTextAt("seat = cab1\nbogus = 1\nseat = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 2));

	/* Every error has a description. */
	for (uint32_t error = NATIVE_ARCADE_CONFIG_OK; error <= NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE; error++)
	{
		CHECK(strcmp(NativeArcadeConfig_ErrorText(error), "unknown error") != 0);
	}
	CHECK(strcmp(NativeArcadeConfig_ErrorText(NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE + 1u), "unknown error") == 0);
	return 0;
}

/* The link group matches the equivalent command line exactly. */
static int TestApplyLink(void)
{
	static const char text[] = "seat = cab2\nport = 7002\n"
	                           "peer = 10.0.0.1:1\npeer = 10.0.0.2:2\npeer = 10.0.0.3:3\npeer = 10.0.0.4:4\n"
	                           "peer = 10.0.0.5:5\npeer = 10.0.0.6:6\npeer = 10.0.0.7:7\npeer = 192.168.1.101:7001\n";
	char *argv[] = {
		"ctr_native",         "--arcade-link",      "cab2",        "--arcade-link-port", "7002",        "--arcade-link-peer",
		"10.0.0.1:1",         "--arcade-link-peer", "10.0.0.2:2",  "--arcade-link-peer", "10.0.0.3:3",  "--arcade-link-peer",
		"10.0.0.4:4",         "--arcade-link-peer", "10.0.0.5:5",  "--arcade-link-peer", "10.0.0.6:6",  "--arcade-link-peer",
		"10.0.0.7:7",         "--arcade-link-peer", "192.168.1.101:7001",
	};
	struct NativeArcadeConfig config;
	struct NativeArcadeLinkOptions fromConfig;
	struct NativeArcadeLinkOptions fromArgs;
	struct NativeArcadeLinkOptions snapshot;

	CHECK(ParseText(text, &config, NULL));
	CHECK(config.peerCount == NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS);

	NativeArcadeLinkOptions_SetDefaults(&fromConfig);
	fromConfig.selectEntropy = UINT64_C(0x0123456789ABCDEF);
	NativeArcadeLinkOptions_SetDefaults(&fromArgs);
	fromArgs.selectEntropy = UINT64_C(0x0123456789ABCDEF);
	CHECK(NativeArcadeConfig_ApplyLink(&config, &fromConfig));
	CHECK(NativeArcadeLinkOptions_ApplyArgs(ARGC(argv), argv, &fromArgs));
	CHECK(memcmp(&fromConfig, &fromArgs, sizeof(fromConfig)) == 0);
	CHECK((fromConfig.enabled == 1) && (fromConfig.localRole == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) && (fromConfig.localPort == 7002));
	CHECK((fromConfig.peerCount == 8) && (fromConfig.peers[7].ipv4 == 0xC0A80165u) && (fromConfig.peers[7].port == 7001));
	CHECK(fromConfig.selectEntropy == UINT64_C(0x0123456789ABCDEF));

	/* No link group: 0 and untouched. */
	CHECK(ParseText("fullscreen = 0\ndata_dir = x\n", &config, NULL));
	NativeArcadeLinkOptions_SetDefaults(&fromConfig);
	snapshot = fromConfig;
	CHECK(!NativeArcadeConfig_ApplyLink(&config, &fromConfig));
	CHECK(memcmp(&fromConfig, &snapshot, sizeof(fromConfig)) == 0);
	CHECK(!NativeArcadeConfig_ApplyLink(NULL, &fromConfig));
	CHECK(!NativeArcadeConfig_ApplyLink(&config, NULL));

	/* An incomplete group assembled by hand is rejected by the link parser, untouched. */
	NativeArcadeConfig_SetDefaults(&config);
	config.hasSeat = 1;
	memcpy(config.seat, "cab1", 5);
	CHECK(!NativeArcadeConfig_ApplyLink(&config, &fromConfig));
	CHECK(memcmp(&fromConfig, &snapshot, sizeof(fromConfig)) == 0);

	/* A link options struct that already holds a preview rejects the link, as on the command line. */
	CHECK(ParseText("seat = cab1\nport = 7001\npeer = 127.0.0.1:7002\n", &config, NULL));
	fromConfig.preview = NATIVE_ARCADE_LINK_PREVIEW_TITLE;
	snapshot = fromConfig;
	CHECK(!NativeArcadeConfig_ApplyLink(&config, &fromConfig));
	CHECK(memcmp(&fromConfig, &snapshot, sizeof(fromConfig)) == 0);
	return 0;
}

/*
 * What the command line's display parser says about option + value, from the
 * defaults: the '=' form is the result, *twoToken the two-token form's.
 */
static int FlagAccepts(const char *option, const char *value, int *twoToken)
{
	char program[] = "ctr_native";
	char joined[64];
	char optionCopy[32];
	char valueCopy[32];
	char *equalsArgv[] = { program, joined };
	char *twoTokenArgv[] = { program, optionCopy, valueCopy };
	struct NativeDisplayConfig display;
	int equals;

	snprintf(joined, sizeof(joined), "%s=%s", option, value);
	snprintf(optionCopy, sizeof(optionCopy), "%s", option);
	snprintf(valueCopy, sizeof(valueCopy), "%s", value);
	NativeDisplayConfig_SetDefaults(&display);
	equals = NativeDisplayConfig_ApplyArgs(ARGC(equalsArgv), equalsArgv, &display);
	NativeDisplayConfig_SetDefaults(&display);
	*twoToken = NativeDisplayConfig_ApplyArgs(ARGC(twoTokenArgv), twoTokenArgv, &display);
	return equals;
}

/*
 * key = value on line 3 is accepted exactly when the flag accepts the value in
 * both forms; *accepted receives the verdict. A rejected value is BAD_VALUE
 * at its line with the config untouched; an accepted one is stored as written.
 */
static int FileAgreesWithFlag(const char *key, const char *option, const char *value, int *accepted)
{
	char text[128];
	struct NativeArcadeConfig config;
	int twoToken = -1;
	const int equals = FlagAccepts(option, value, &twoToken);

	CHECK(equals == twoToken);
	snprintf(text, sizeof(text), "# c\nfullscreen = 0\n%s = %s\n", key, value);
	if (equals)
	{
		const int isScale = (strcmp(key, "render_scale") == 0);

		Sentinel(&config);
		CHECK(ParseText(text, &config, NULL));
		CHECK((config.hasFullscreen == 1) && (config.fullscreen == 0));
		if (isScale)
		{
			CHECK((config.hasRenderScale == 1) && (config.hasTextureFilter == 0) && (strcmp(config.renderScaleText, value) == 0));
		}
		else
		{
			CHECK((config.hasTextureFilter == 1) && (config.hasRenderScale == 0) && (strcmp(config.textureFilterText, value) == 0));
		}
	}
	else
	{
		CHECK(RejectsTextAt(text, NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 3));
	}
	*accepted = equals;
	return 0;
}

static int TestDisplayKeys(void)
{
	static const char *const goodScales[] = { "1", "2", "3", "4", "6", "8" };
	static const char *const badScales[] = { "0", "5", "7", "16", "-1", "-8", "8x", "x8", "2.0", "0x8", "1e1", "8 8", "999999999999999999999" };
	/* strtol, behind the flag, takes a leading '+' or zeros: the file agrees. */
	static const char *const flagQuirkScales[] = { "08", "+8", "0000008" };
	static const char *const goodFilters[] = { "nearest", "bilinear" };
	static const char *const badFilters[] = { "Bilinear", "BILINEAR", "linear", "Nearest", "trilinear", "none", "bilinear2", "-bilinear", "near est" };
	struct NativeArcadeConfig config;
	struct NativeDisplayConfig display;
	int accepted = -1;

	for (size_t i = 0; i < sizeof(goodScales) / sizeof(goodScales[0]); i++)
	{
		CHECK(FileAgreesWithFlag("render_scale", "--render-scale", goodScales[i], &accepted) == 0);
		CHECK(accepted == 1);
	}
	for (size_t i = 0; i < sizeof(badScales) / sizeof(badScales[0]); i++)
	{
		CHECK(FileAgreesWithFlag("render_scale", "--render-scale", badScales[i], &accepted) == 0);
		CHECK(accepted == 0);
	}
	for (size_t i = 0; i < sizeof(flagQuirkScales) / sizeof(flagQuirkScales[0]); i++)
	{
		CHECK(FileAgreesWithFlag("render_scale", "--render-scale", flagQuirkScales[i], &accepted) == 0);
		CHECK(accepted == 1);
		snprintf(s_big, sizeof(s_big), "render_scale = %s\n", flagQuirkScales[i]);
		CHECK(ParseText(s_big, &config, NULL));
		NativeDisplayConfig_SetDefaults(&display);
		CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
		CHECK(display.renderScale == 8);
	}
	for (size_t i = 0; i < sizeof(goodFilters) / sizeof(goodFilters[0]); i++)
	{
		CHECK(FileAgreesWithFlag("texture_filter", "--texture-filter", goodFilters[i], &accepted) == 0);
		CHECK(accepted == 1);
	}
	for (size_t i = 0; i < sizeof(badFilters) / sizeof(badFilters[0]); i++)
	{
		CHECK(FileAgreesWithFlag("texture_filter", "--texture-filter", badFilters[i], &accepted) == 0);
		CHECK(accepted == 0);
	}

	/* The one exception: a value that does not fit its buffer (7 and 15
	 * characters) is a bad value, even where the flag takes it. */
	{
		int twoToken = 0;

		CHECK(FlagAccepts("--render-scale", "00000008", &twoToken) && twoToken);
		CHECK(RejectsTextAt("render_scale = 00000008\n", NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 1));
		CHECK(RejectsTextAt("texture_filter = bilinearbilinear\n", NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 1));
	}

	/* Blanks around the value are trimmed; the value is stored as written. */
	Sentinel(&config);
	CHECK(ParseText("render_scale = \t 4 \t\r\ntexture_filter\t=\tbilinear  \r\n", &config, NULL));
	CHECK((config.hasRenderScale == 1) && (strcmp(config.renderScaleText, "4") == 0));
	CHECK((config.hasTextureFilter == 1) && (strcmp(config.textureFilterText, "bilinear") == 0));
	CHECK((config.hasFullscreen == 0) && (config.hasDataDir == 0) && !NativeArcadeConfig_HasLink(&config));

	/* Not repeatable, not empty, and a trailing comment is part of the value. */
	CHECK(RejectsTextAt("render_scale = 2\nrender_scale = 2\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 2));
	CHECK(RejectsTextAt("texture_filter = nearest\n# c\ntexture_filter = bilinear\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 3));
	CHECK(RejectsTextAt("render_scale = 2\nrender_scale =\n", NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY, 2));
	CHECK(RejectsTextAt("render_scale =\n", NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE, 1));
	CHECK(RejectsTextAt("# c\ntexture_filter = \t \r\n", NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE, 2));
	CHECK(RejectsTextAt("render_scale = 8 # c\n", NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 1));
	CHECK(RejectsTextAt("texture_filter = bilinear # smooth\n", NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 1));

	/* The line of a bad value, after good lines of every other key; the first error wins. */
	CHECK(RejectsTextAt("data_dir = x\nseat = cab1\nport = 7001\npeer = 1.2.3.4:5\nfullscreen = 1\ntexture_filter = bilinear\nrender_scale = 5\n",
	                    NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 7));
	CHECK(RejectsTextAt("render_scale = 8\n\ntexture_filter = linear\nrender_scale = 8\n", NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE, 3));
	/* A good display key does not complete a link group. */
	CHECK(RejectsTextAt("render_scale = 8\nseat = cab1\n", NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE, 2));
	return 0;
}

static int TestApplyDisplay(void)
{
	struct NativeArcadeConfig config;
	struct NativeDisplayConfig display;
	struct NativeDisplayConfig fromArgs;
	struct NativeDisplayConfig snapshot;

	/* NULL arguments: 0, untouched. */
	CHECK(ParseText("render_scale = 8\n", &config, NULL));
	NativeDisplayConfig_SetDefaults(&display);
	snapshot = display;
	CHECK(!NativeArcadeConfig_ApplyDisplay(NULL, &display));
	CHECK(!NativeArcadeConfig_ApplyDisplay(&config, NULL));
	CHECK(memcmp(&display, &snapshot, sizeof(display)) == 0);

	/* Neither key: a successful no-op, even on a display the flag parser would refuse. */
	CHECK(ParseText("fullscreen = 0\ndata_dir = x\n", &config, NULL));
	display.renderScale = 3;
	display.fullscreen = 1;
	display.textureFilter = NATIVE_TEXTURE_FILTER_BILINEAR;
	snapshot = display;
	CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK(memcmp(&display, &snapshot, sizeof(display)) == 0);
	display.renderScale = 5;
	snapshot = display;
	CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK(memcmp(&display, &snapshot, sizeof(display)) == 0);

	/* One key: only that field changes; the file's fullscreen is never applied here. */
	CHECK(ParseText("fullscreen = 0\nrender_scale = 6\n", &config, NULL));
	display.renderScale = 2;
	display.fullscreen = 1;
	display.textureFilter = NATIVE_TEXTURE_FILTER_BILINEAR;
	CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK((display.renderScale == 6) && (display.fullscreen == 1) && (display.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR));
	CHECK(ParseText("fullscreen = 1\ntexture_filter = nearest\n", &config, NULL));
	display.fullscreen = 0;
	CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK((display.renderScale == 6) && (display.fullscreen == 0) && (display.textureFilter == NATIVE_TEXTURE_FILTER_NEAREST));

	/* Both keys: exactly the equivalent command line, from the same start. */
	{
		char *argv[] = { "ctr_native", "--render-scale", "8", "--texture-filter", "bilinear" };

		CHECK(ParseText("texture_filter = bilinear\nrender_scale = 8\n", &config, NULL));
		NativeDisplayConfig_SetDefaults(&display);
		display.fullscreen = 1;
		fromArgs = display;
		CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
		CHECK(NativeDisplayConfig_ApplyArgs(ARGC(argv), argv, &fromArgs));
		CHECK(memcmp(&display, &fromArgs, sizeof(display)) == 0);
		CHECK((display.renderScale == 8) && (display.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR) && (display.fullscreen == 1));
	}

	/* The flags then override per key, as in main.c. */
	{
		char *argv[] = { "ctr_native", "--render-scale", "4" };

		CHECK(NativeDisplayConfig_ApplyArgs(ARGC(argv), argv, &display));
		CHECK((display.renderScale == 4) && (display.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR) && (display.fullscreen == 1));
	}

	/* A display the flag parser refuses: 0, untouched. */
	display.fullscreen = 2;
	snapshot = display;
	CHECK(!NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK(memcmp(&display, &snapshot, sizeof(display)) == 0);

	/* A hand-built config with a bad or unterminated value: 0, untouched. */
	NativeDisplayConfig_SetDefaults(&display);
	snapshot = display;
	NativeArcadeConfig_SetDefaults(&config);
	config.hasRenderScale = 1;
	memcpy(config.renderScaleText, "5", 2);
	CHECK(!NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK(memcmp(&display, &snapshot, sizeof(display)) == 0);
	memset(config.renderScaleText, '8', sizeof(config.renderScaleText));
	CHECK(!NativeArcadeConfig_ApplyDisplay(&config, &display));
	NativeArcadeConfig_SetDefaults(&config);
	config.hasTextureFilter = 1;
	memset(config.textureFilterText, 'x', sizeof(config.textureFilterText));
	CHECK(!NativeArcadeConfig_ApplyDisplay(&config, &display));
	/* A set flag with an empty value is refused by the flag parser too. */
	config.textureFilterText[0] = '\0';
	CHECK(!NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK(memcmp(&display, &snapshot, sizeof(display)) == 0);
	return 0;
}

static int TestParseArgs(void)
{
	struct NativeArcadeConfigArgs args;
	struct NativeArcadeConfigArgs snapshot;

	{
		char *argv[] = { "ctr_native" };

		memset(&args, 0xA5, sizeof(args));
		CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
		CHECK((args.configPath == NULL) && (args.dataDir == NULL) && (args.namesLinkOption == 0) && (args.namesWindowMode == 0));
		CHECK((args.namesRenderScale == 0) && (args.namesTextureFilter == 0));
		CHECK(NativeArcadeConfig_ParseArgs(0, NULL, &args));
	}
	{
		char *argv[] = { "ctr_native", "--render-scale", "2", "--config", "C:\\cabinet one\\arcade.cfg", "--data-dir", "..\\data", "--windowed" };

		CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
		CHECK(args.configPath == argv[4]);
		CHECK(args.dataDir == argv[6]);
		CHECK((args.namesLinkOption == 0) && (args.namesWindowMode == 1) && (args.namesRenderScale == 1) && (args.namesTextureFilter == 0));
	}
	{
		/* Each display flag in both forms, and near misses that name neither. */
		static const struct
		{
			const char *arg;
			uint8_t scale;
			uint8_t filter;
		} names[] = {
			{ "--render-scale", 1, 0 },    { "--render-scale=4", 1, 0 },       { "--render-scale=", 1, 0 },
			{ "--texture-filter", 0, 1 },  { "--texture-filter=bilinear", 0, 1 }, { "--texture-filter=", 0, 1 },
			{ "--render-scaled", 0, 0 },   { "-render-scale", 0, 0 },          { "--texture-filters", 0, 0 },
			{ "--Render-scale", 0, 0 },    { "render_scale", 0, 0 },           { "--texture_filter=bilinear", 0, 0 },
		};

		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		{
			char name[32];
			char *argv[] = { "ctr_native", name };

			snprintf(name, sizeof(name), "%s", names[i].arg);
			memset(&args, 0xA5, sizeof(args));
			CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
			CHECK((args.namesRenderScale == names[i].scale) && (args.namesTextureFilter == names[i].filter));
			CHECK((args.namesLinkOption == 0) && (args.namesWindowMode == 0) && (args.configPath == NULL) && (args.dataDir == NULL));
		}
	}
	{
		char *argv[] = { "ctr_native", "--texture-filter", "bilinear", "--render-scale=8" };

		CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
		CHECK((args.namesRenderScale == 1) && (args.namesTextureFilter == 1) && (args.namesWindowMode == 0));
	}
	{
		static const char *const linkNames[] = { "--arcade-link", "--arcade-link-port", "--arcade-link-peer", "--arcade-link-preview" };

		for (size_t i = 0; i < sizeof(linkNames) / sizeof(linkNames[0]); i++)
		{
			char name[32];
			char *argv[] = { "ctr_native", name };

			snprintf(name, sizeof(name), "%s", linkNames[i]);
			CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
			CHECK((args.namesLinkOption == 1) && (args.namesWindowMode == 0));
		}
	}
	{
		char *argv[] = { "ctr_native", "--fullscreen", "--arcade-link-autopilot", "x" };

		CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
		CHECK((args.namesLinkOption == 0) && (args.namesWindowMode == 1) && (args.namesRenderScale == 0) && (args.namesTextureFilter == 0));
	}

	/* Errors leave *args untouched. */
	{
		char *twice[] = { "ctr_native", "--config", "a.cfg", "--config", "b.cfg" };
		char *twiceDir[] = { "ctr_native", "--data-dir", "a", "--data-dir", "a" };
		char *missing[] = { "ctr_native", "--config" };
		char *dash[] = { "ctr_native", "--data-dir", "--fullscreen" };
		char *empty[] = { "ctr_native", "--config", "" };
		char *nullValue[] = { "ctr_native", "--config", NULL };
		char *nullArg[] = { "ctr_native", NULL };

		memset(&args, 0x5A, sizeof(args));
		snapshot = args;
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(twice), twice, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(twiceDir), twiceDir, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(missing), missing, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(dash), dash, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(empty), empty, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(nullValue), nullValue, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(nullArg), nullArg, &args));
		CHECK(!NativeArcadeConfig_ParseArgs(2, NULL, &args));
		CHECK(memcmp(&args, &snapshot, sizeof(args)) == 0);
		CHECK(!NativeArcadeConfig_ParseArgs(ARGC(twice), twice, NULL));
	}
	return 0;
}

static int ReadFile(const char *path, char *buffer, size_t capacity, size_t *size)
{
	FILE *file = fopen(path, "rb");

	if (file == NULL)
	{
		fprintf(stderr, "cannot open %s\n", path);
		return 0;
	}
	*size = fread(buffer, 1, capacity, file);
	fclose(file);
	return *size < capacity;
}

/* The committed package templates parse with the real parser to the documented values (PK-8). */
static int TestTemplate(const char *path, const char *seat, uint8_t role, uint16_t port, uint32_t peerIPv4, uint16_t peerPort)
{
	struct NativeArcadeConfig config;
	struct NativeArcadeConfigStatus status;
	struct NativeArcadeLinkOptions options;
	struct NativeDisplayConfig display;
	size_t size = 0;
	char peerText[32];

	CHECK(ReadFile(path, s_big, sizeof(s_big), &size));
	CHECK(NativeArcadeConfig_Parse(s_big, size, &config, &status));
	CHECK((config.hasDataDir == 1) && (strcmp(config.dataDir, "C:\\ctr-data") == 0));
	CHECK((config.hasFullscreen == 1) && (config.fullscreen == 1));
	CHECK((config.hasSeat == 1) && (strcmp(config.seat, seat) == 0));
	CHECK((config.hasPort == 1) && (config.peerCount == 1));
	snprintf(peerText, sizeof(peerText), "%u.%u.%u.%u:%u", (unsigned)(peerIPv4 >> 24), (unsigned)((peerIPv4 >> 16) & 0xFFu),
	         (unsigned)((peerIPv4 >> 8) & 0xFFu), (unsigned)(peerIPv4 & 0xFFu), (unsigned)peerPort);
	CHECK(strcmp(config.peers[0], peerText) == 0);

	NativeArcadeLinkOptions_SetDefaults(&options);
	CHECK(NativeArcadeConfig_ApplyLink(&config, &options));
	CHECK((options.enabled == 1) && (options.localRole == role) && (options.localPort == port));
	CHECK((options.peerCount == 1) && (options.peers[0].ipv4 == peerIPv4) && (options.peers[0].port == peerPort));
	CHECK(options.preview == NATIVE_ARCADE_LINK_PREVIEW_NONE);

	/* Presentation defaults: 8x, bilinear (fullscreen 1 is checked above). */
	CHECK((config.hasRenderScale == 1) && (strcmp(config.renderScaleText, "8") == 0));
	CHECK((config.hasTextureFilter == 1) && (strcmp(config.textureFilterText, "bilinear") == 0));
	NativeDisplayConfig_SetDefaults(&display);
	CHECK(NativeArcadeConfig_ApplyDisplay(&config, &display));
	CHECK((display.renderScale == 8) && (display.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR));
	CHECK(display.fullscreen == NATIVE_DISPLAY_CONFIG_DEFAULT_FULLSCREEN);
	return 0;
}

int main(int argc, char *argv[])
{
	CHECK(TestDefaults() == 0);
	CHECK(TestGrammar() == 0);
	CHECK(TestFullscreen() == 0);
	CHECK(TestErrors() == 0);
	CHECK(TestApplyLink() == 0);
	CHECK(TestDisplayKeys() == 0);
	CHECK(TestApplyDisplay() == 0);
	CHECK(TestParseArgs() == 0);
	/* tools/package/cab1.cfg and cab2.cfg, passed by ctest. */
	CHECK(argc == 3);
	CHECK(TestTemplate(argv[1], "cab1", NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, 7001, 0xC0A80166u, 7002) == 0);
	CHECK(TestTemplate(argv[2], "cab2", NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, 7002, 0xC0A80165u, 7001) == 0);
	puts("native_arcade_config_test: ok");
	return 0;
}
