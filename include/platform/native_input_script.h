#ifndef PLATFORM_NATIVE_INPUT_SCRIPT_H
#define PLATFORM_NATIVE_INPUT_SCRIPT_H

/*
 * Bounded, host-only schedule parser for automated local capture input.
 *
 * This module knows neither SDL nor PSX buttons.  native_input.c maps the
 * parsed key names back through its ordinary SDL keyboard mapping, so a
 * script cannot address game state directly.  It is deliberately absent from
 * replay, checkpoint, canonical-state, and match schemas.
 *
 * Set CTR_NATIVE_INPUT_SCRIPT to a plain-text file.  Each non-comment line
 * has an inclusive frame range and one or more comma-separated key names:
 *
 *   0-1 Return
 *   45-50 Down, C
 *
 * Blank lines and lines beginning with # are ignored.  Valid names are
 * Enter/Return, Up/Down/Left/Right, X/V/Z/C/Space, LShift/RShift,
 * LCtrl/RCtrl, and LBracket/RBracket.  Key names are ASCII
 * case-insensitive.  Any malformed line, oversized file, or empty schedule
 * disables the entire script rather than accepting a partial capture plan.
 */

#include <stddef.h>
#include <stdint.h>

#define NATIVE_INPUT_SCRIPT_MAX_ENTRIES 256u
#define NATIVE_INPUT_SCRIPT_MAX_BYTES (64u * 1024u)
#define NATIVE_INPUT_SCRIPT_MAX_FRAME 10000000ull

enum NativeInputScriptKey
{
	NATIVE_INPUT_SCRIPT_KEY_RETURN = 0,
	NATIVE_INPUT_SCRIPT_KEY_UP,
	NATIVE_INPUT_SCRIPT_KEY_DOWN,
	NATIVE_INPUT_SCRIPT_KEY_LEFT,
	NATIVE_INPUT_SCRIPT_KEY_RIGHT,
	NATIVE_INPUT_SCRIPT_KEY_X,
	NATIVE_INPUT_SCRIPT_KEY_V,
	NATIVE_INPUT_SCRIPT_KEY_Z,
	NATIVE_INPUT_SCRIPT_KEY_C,
	NATIVE_INPUT_SCRIPT_KEY_SPACE,
	NATIVE_INPUT_SCRIPT_KEY_LSHIFT,
	NATIVE_INPUT_SCRIPT_KEY_RSHIFT,
	NATIVE_INPUT_SCRIPT_KEY_LCTRL,
	NATIVE_INPUT_SCRIPT_KEY_RCTRL,
	NATIVE_INPUT_SCRIPT_KEY_LBRACKET,
	NATIVE_INPUT_SCRIPT_KEY_RBRACKET,
	NATIVE_INPUT_SCRIPT_KEY_COUNT,
};

enum NativeInputScriptError
{
	NATIVE_INPUT_SCRIPT_ERROR_NONE = 0,
	NATIVE_INPUT_SCRIPT_ERROR_ARGUMENT,
	NATIVE_INPUT_SCRIPT_ERROR_EMPTY,
	NATIVE_INPUT_SCRIPT_ERROR_SYNTAX,
	NATIVE_INPUT_SCRIPT_ERROR_RANGE,
	NATIVE_INPUT_SCRIPT_ERROR_KEY,
	NATIVE_INPUT_SCRIPT_ERROR_CAPACITY,
	NATIVE_INPUT_SCRIPT_ERROR_IO,
};

struct NativeInputScriptEntry
{
	uint64_t firstFrame;
	uint64_t lastFrame;
	uint32_t keyMask;
};

struct NativeInputScriptSchedule
{
	unsigned int entryCount;
	enum NativeInputScriptError lastError;
	struct NativeInputScriptEntry entries[NATIVE_INPUT_SCRIPT_MAX_ENTRIES];
};

void NativeInputScript_Init(struct NativeInputScriptSchedule *schedule);

/* Parses exactly `textByteCount` bytes; text need not be NUL terminated.
 * Failure clears every prior entry, making the result disabled. */
int NativeInputScript_ParseText(struct NativeInputScriptSchedule *schedule,
	const char *text, size_t textByteCount);

int NativeInputScript_IsKeyDown(const struct NativeInputScriptSchedule *schedule,
	uint64_t frame, enum NativeInputScriptKey key);

const char *NativeInputScript_ErrorString(enum NativeInputScriptError error);

#endif
