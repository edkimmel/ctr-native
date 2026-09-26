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
	CHECK(NativeArcadeConfig_HasLink(&config));

	/* LF only, a trailing newline, and a lone data_dir: no link group. */
	Sentinel(&config);
	CHECK(ParseText("data_dir = ..\\ctr data\n", &config, NULL));
	CHECK((config.hasDataDir == 1) && (strcmp(config.dataDir, "..\\ctr data") == 0));
	CHECK((config.hasSeat == 0) && (config.hasPort == 0) && (config.peerCount == 0) && (config.hasFullscreen == 0));
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
	CHECK(RejectsTextAt("\n\n\nrender_scale = 2\n", NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY, 4));
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

static int TestParseArgs(void)
{
	struct NativeArcadeConfigArgs args;
	struct NativeArcadeConfigArgs snapshot;

	{
		char *argv[] = { "ctr_native" };

		memset(&args, 0xA5, sizeof(args));
		CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
		CHECK((args.configPath == NULL) && (args.dataDir == NULL) && (args.namesLinkOption == 0) && (args.namesWindowMode == 0));
		CHECK(NativeArcadeConfig_ParseArgs(0, NULL, &args));
	}
	{
		char *argv[] = { "ctr_native", "--render-scale", "2", "--config", "C:\\cabinet one\\arcade.cfg", "--data-dir", "..\\data", "--windowed" };

		CHECK(NativeArcadeConfig_ParseArgs(ARGC(argv), argv, &args));
		CHECK(args.configPath == argv[4]);
		CHECK(args.dataDir == argv[6]);
		CHECK((args.namesLinkOption == 0) && (args.namesWindowMode == 1));
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
		CHECK((args.namesLinkOption == 0) && (args.namesWindowMode == 1));
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
	return 0;
}

int main(int argc, char *argv[])
{
	CHECK(TestDefaults() == 0);
	CHECK(TestGrammar() == 0);
	CHECK(TestFullscreen() == 0);
	CHECK(TestErrors() == 0);
	CHECK(TestApplyLink() == 0);
	CHECK(TestParseArgs() == 0);
	/* tools/package/cab1.cfg and cab2.cfg, passed by ctest. */
	CHECK(argc == 3);
	CHECK(TestTemplate(argv[1], "cab1", NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, 7001, 0xC0A80166u, 7002) == 0);
	CHECK(TestTemplate(argv[2], "cab2", NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, 7002, 0xC0A80165u, 7001) == 0);
	puts("native_arcade_config_test: ok");
	return 0;
}
