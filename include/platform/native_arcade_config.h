#ifndef PLATFORM_NATIVE_ARCADE_CONFIG_H
#define PLATFORM_NATIVE_ARCADE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_arcade_link_options.h"

/*
 * Per-cabinet config file (docs/PACKAGING.md PK-2..PK-6).
 *
 * A key = value text file, by default arcade.cfg next to the exe:
 *
 *   data_dir   = <dir>          folder holding the user's own ctr-u.bin (or
 *                               the extracted BIGFILE.BIG tree); the rest of
 *                               the line, spaces allowed, no quotes
 *   seat       = cab1|cab2      as --arcade-link
 *   port       = 1..65535       as --arcade-link-port
 *   peer       = a.b.c.d:port   as --arcade-link-peer; repeatable, up to
 *                               NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS
 *   fullscreen = 0|1|yes|no|true|false
 *
 * One key = value per line. Keys are lowercase and exact. Whitespace (space,
 * tab) around the key and the value is trimmed; the value is the rest of the
 * line after the first '=' (so no trailing comments). Blank lines and lines
 * whose first non-blank character is '#' or ';' are comments. CRLF line ends
 * and a leading UTF-8 BOM are accepted; a CR anywhere else in a line is a
 * syntax error.
 *
 * Errors carry the 1-based line number (0 when not line-specific): a file
 * over NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES, a line over
 * NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS characters (line end excluded), a NUL
 * byte, a line without '=', with an empty key, or with a bare CR (one not
 * part of a CRLF line end), an unknown key, a
 * non-repeatable key given twice, an empty value, a bad value, a peer beyond
 * the maximum, and an incomplete link group.
 *
 * The link group (seat, port, peer) is all-or-none with exactly the command
 * line's rules (PK-5): every seat, port, and peer value, and the group as a
 * whole, is checked by NativeArcadeLinkOptions_ApplyArgs itself over a
 * synthetic argv (--arcade-link <seat> --arcade-link-port <port>
 * --arcade-link-peer <peer>...), so this module has no port or peer grammar
 * of its own. The group reaches the link only through
 * NativeArcadeConfig_ApplyLink, which writes struct NativeArcadeLinkOptions
 * exactly as the flags do.
 *
 * Host-local launch configuration only. Pure: caller-owned state, no heap,
 * no I/O (the caller reads the file), no hidden state. Parse is
 * transactional: on any error *config is left untouched.
 */

#define NATIVE_ARCADE_CONFIG_MAX_FILE_BYTES 16384u
#define NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS 1023u
#define NATIVE_ARCADE_CONFIG_DATA_DIR_BYTES (NATIVE_ARCADE_CONFIG_MAX_LINE_CHARS + 1u)
#define NATIVE_ARCADE_CONFIG_SEAT_BYTES     8u
#define NATIVE_ARCADE_CONFIG_PORT_BYTES     8u
#define NATIVE_ARCADE_CONFIG_PEER_BYTES     24u
#define NATIVE_ARCADE_CONFIG_DEFAULT_NAME   "arcade.cfg"

enum NativeArcadeConfigError
{
	NATIVE_ARCADE_CONFIG_OK = 0,
	NATIVE_ARCADE_CONFIG_ERROR_ARGUMENT = 1,       /* NULL argument */
	NATIVE_ARCADE_CONFIG_ERROR_FILE_TOO_LARGE = 2, /* line 0 */
	NATIVE_ARCADE_CONFIG_ERROR_LINE_TOO_LONG = 3,
	NATIVE_ARCADE_CONFIG_ERROR_NUL_BYTE = 4,
	NATIVE_ARCADE_CONFIG_ERROR_SYNTAX = 5, /* no '=', an empty key, or a bare CR */
	NATIVE_ARCADE_CONFIG_ERROR_UNKNOWN_KEY = 6,
	NATIVE_ARCADE_CONFIG_ERROR_DUPLICATE_KEY = 7,
	NATIVE_ARCADE_CONFIG_ERROR_EMPTY_VALUE = 8,
	NATIVE_ARCADE_CONFIG_ERROR_BAD_VALUE = 9,
	NATIVE_ARCADE_CONFIG_ERROR_TOO_MANY_PEERS = 10,
	NATIVE_ARCADE_CONFIG_ERROR_LINK_INCOMPLETE = 11 /* line of the first link key */
};

struct NativeArcadeConfigStatus
{
	uint32_t error; /* enum NativeArcadeConfigError */
	uint32_t line;  /* 1-based, 0 when not line-specific */
};

/* Values as written in the file, each already accepted by its checker. */
struct NativeArcadeConfig
{
	uint8_t hasDataDir;
	uint8_t hasSeat;
	uint8_t hasPort;
	uint8_t hasFullscreen;
	int fullscreen; /* 0 or 1 when hasFullscreen */
	uint32_t peerCount;
	char dataDir[NATIVE_ARCADE_CONFIG_DATA_DIR_BYTES];
	char seat[NATIVE_ARCADE_CONFIG_SEAT_BYTES];
	char port[NATIVE_ARCADE_CONFIG_PORT_BYTES];
	char peers[NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS][NATIVE_ARCADE_CONFIG_PEER_BYTES];
};

/* NULL is a no-op. Otherwise zeroes the config: every key unset. */
void NativeArcadeConfig_SetDefaults(struct NativeArcadeConfig *config);

/*
 * Parses size bytes of text (not NUL-terminated; embedded NULs are an error).
 * Returns 1 and replaces *config on success; 0 with *config untouched
 * otherwise. *status (optional) receives the error and line; OK and 0 on
 * success.
 */
int NativeArcadeConfig_Parse(const char *text, size_t size, struct NativeArcadeConfig *config, struct NativeArcadeConfigStatus *status);

/* 1 when the config sets any of seat, port, or peer. 0 for NULL. */
int NativeArcadeConfig_HasLink(const struct NativeArcadeConfig *config);

/*
 * Feeds the link group to NativeArcadeLinkOptions_ApplyArgs as a synthetic
 * argv. Returns its result; 0 with *options untouched on NULL arguments or a
 * config without a link group. selectEntropy is never read or changed.
 */
int NativeArcadeConfig_ApplyLink(const struct NativeArcadeConfig *config, struct NativeArcadeLinkOptions *options);

/* A short English description of an enum NativeArcadeConfigError value. */
const char *NativeArcadeConfig_ErrorText(uint32_t error);

/* The command-line side of PK-2 and PK-4. */
struct NativeArcadeConfigArgs
{
	const char *configPath;  /* --config <path>, else NULL; points into argv */
	const char *dataDir;     /* --data-dir <dir>, else NULL; points into argv */
	uint8_t namesLinkOption; /* any --arcade-link, --arcade-link-port, --arcade-link-peer, or --arcade-link-preview */
	uint8_t namesWindowMode; /* any --fullscreen or --windowed */
	uint16_t reserved;
};

/*
 * Scans argv for --config <path> and --data-dir <dir> (each at most once;
 * the value must be present, non-empty, and not start with '-') and records
 * whether argv names a link or window-mode option (by name, like the replay
 * rejection). Other arguments are ignored. Returns 1 and writes *args on
 * success; 0 with *args untouched otherwise.
 */
int NativeArcadeConfig_ParseArgs(int argc, char *argv[], struct NativeArcadeConfigArgs *args);

#endif
