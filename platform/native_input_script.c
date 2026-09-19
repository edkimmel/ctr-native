#include <platform/native_input_script.h>

#include <string.h>

static void NativeInputScript_Clear(struct NativeInputScriptSchedule *schedule,
	                                   enum NativeInputScriptError error)
{
	if (schedule == NULL)
		return;
	memset(schedule, 0, sizeof(*schedule));
	schedule->lastError = error;
}

void NativeInputScript_Init(struct NativeInputScriptSchedule *schedule)
{
	NativeInputScript_Clear(schedule, NATIVE_INPUT_SCRIPT_ERROR_NONE);
}

static int NativeInputScript_IsSpace(char c)
{
	return (c == ' ') || (c == '\t');
}

static unsigned char NativeInputScript_ToLower(unsigned char c)
{
	if ((c >= (unsigned char)'A') && (c <= (unsigned char)'Z'))
		return (unsigned char)(c + ((unsigned char)'a' - (unsigned char)'A'));
	return c;
}

static int NativeInputScript_NameEquals(const char *name, size_t nameLength,
	                                       const char *literal)
{
	size_t index;

	for (index = 0u; index < nameLength && literal[index] != '\0'; ++index)
	{
		if (NativeInputScript_ToLower((unsigned char)name[index]) !=
			NativeInputScript_ToLower((unsigned char)literal[index]))
		{
			return 0;
		}
	}
	return (index == nameLength) && (literal[index] == '\0');
}

static int NativeInputScript_ParseKey(const char *name, size_t nameLength,
	                                     enum NativeInputScriptKey *key)
{
	if ((name == NULL) || (key == NULL) || (nameLength == 0u))
		return 0;
	if (NativeInputScript_NameEquals(name, nameLength, "enter") ||
		NativeInputScript_NameEquals(name, nameLength, "return"))
		*key = NATIVE_INPUT_SCRIPT_KEY_RETURN;
	else if (NativeInputScript_NameEquals(name, nameLength, "up"))
		*key = NATIVE_INPUT_SCRIPT_KEY_UP;
	else if (NativeInputScript_NameEquals(name, nameLength, "down"))
		*key = NATIVE_INPUT_SCRIPT_KEY_DOWN;
	else if (NativeInputScript_NameEquals(name, nameLength, "left"))
		*key = NATIVE_INPUT_SCRIPT_KEY_LEFT;
	else if (NativeInputScript_NameEquals(name, nameLength, "right"))
		*key = NATIVE_INPUT_SCRIPT_KEY_RIGHT;
	else if (NativeInputScript_NameEquals(name, nameLength, "x"))
		*key = NATIVE_INPUT_SCRIPT_KEY_X;
	else if (NativeInputScript_NameEquals(name, nameLength, "v"))
		*key = NATIVE_INPUT_SCRIPT_KEY_V;
	else if (NativeInputScript_NameEquals(name, nameLength, "z"))
		*key = NATIVE_INPUT_SCRIPT_KEY_Z;
	else if (NativeInputScript_NameEquals(name, nameLength, "c"))
		*key = NATIVE_INPUT_SCRIPT_KEY_C;
	else if (NativeInputScript_NameEquals(name, nameLength, "space"))
		*key = NATIVE_INPUT_SCRIPT_KEY_SPACE;
	else if (NativeInputScript_NameEquals(name, nameLength, "lshift"))
		*key = NATIVE_INPUT_SCRIPT_KEY_LSHIFT;
	else if (NativeInputScript_NameEquals(name, nameLength, "rshift"))
		*key = NATIVE_INPUT_SCRIPT_KEY_RSHIFT;
	else if (NativeInputScript_NameEquals(name, nameLength, "lctrl"))
		*key = NATIVE_INPUT_SCRIPT_KEY_LCTRL;
	else if (NativeInputScript_NameEquals(name, nameLength, "rctrl"))
		*key = NATIVE_INPUT_SCRIPT_KEY_RCTRL;
	else if (NativeInputScript_NameEquals(name, nameLength, "lbracket") ||
		NativeInputScript_NameEquals(name, nameLength, "leftbracket"))
		*key = NATIVE_INPUT_SCRIPT_KEY_LBRACKET;
	else if (NativeInputScript_NameEquals(name, nameLength, "rbracket") ||
		NativeInputScript_NameEquals(name, nameLength, "rightbracket"))
		*key = NATIVE_INPUT_SCRIPT_KEY_RBRACKET;
	else
		return 0;
	return 1;
}

static int NativeInputScript_ParseFrame(const char *line, size_t lineLength,
	                                       size_t *offset, uint64_t *frame)
{
	uint64_t value = 0u;
	size_t index;

	if ((line == NULL) || (offset == NULL) || (frame == NULL))
		return 0;
	index = *offset;
	if ((index >= lineLength) || (line[index] < '0') || (line[index] > '9'))
		return 0;
	while ((index < lineLength) && (line[index] >= '0') && (line[index] <= '9'))
	{
		unsigned int digit = (unsigned int)(line[index] - '0');
		if (value > (NATIVE_INPUT_SCRIPT_MAX_FRAME - digit) / 10u)
			return 0;
		value = (value * 10u) + digit;
		++index;
	}
	*offset = index;
	*frame = value;
	return 1;
}

static enum NativeInputScriptError NativeInputScript_ParseLine(
	const char *line, size_t lineLength, struct NativeInputScriptEntry *entry,
	int *hasEntry)
{
	size_t offset = 0u;
	uint64_t firstFrame;
	uint64_t lastFrame;
	uint32_t keyMask = 0u;
	int sawKey = 0;

	if ((line == NULL) || (entry == NULL) || (hasEntry == NULL))
		return NATIVE_INPUT_SCRIPT_ERROR_ARGUMENT;
	while ((offset < lineLength) && NativeInputScript_IsSpace(line[offset]))
		++offset;
	if ((offset == lineLength) || (line[offset] == '#'))
	{
		*hasEntry = 0;
		return NATIVE_INPUT_SCRIPT_ERROR_NONE;
	}
	if (!NativeInputScript_ParseFrame(line, lineLength, &offset, &firstFrame))
		return NATIVE_INPUT_SCRIPT_ERROR_RANGE;
	while ((offset < lineLength) && NativeInputScript_IsSpace(line[offset]))
		++offset;
	if ((offset >= lineLength) || (line[offset] != '-'))
		return NATIVE_INPUT_SCRIPT_ERROR_SYNTAX;
	++offset;
	while ((offset < lineLength) && NativeInputScript_IsSpace(line[offset]))
		++offset;
	if (!NativeInputScript_ParseFrame(line, lineLength, &offset, &lastFrame) ||
		(lastFrame < firstFrame))
	{
		return NATIVE_INPUT_SCRIPT_ERROR_RANGE;
	}
	if ((offset >= lineLength) || !NativeInputScript_IsSpace(line[offset]))
		return NATIVE_INPUT_SCRIPT_ERROR_SYNTAX;
	while ((offset < lineLength) && NativeInputScript_IsSpace(line[offset]))
		++offset;
	while (offset < lineLength)
	{
		size_t nameStart = offset;
		enum NativeInputScriptKey key;

		while ((offset < lineLength) && !NativeInputScript_IsSpace(line[offset]) &&
			(line[offset] != ','))
		{
			++offset;
		}
		if (!NativeInputScript_ParseKey(line + nameStart, offset - nameStart, &key))
			return NATIVE_INPUT_SCRIPT_ERROR_KEY;
		keyMask |= (uint32_t)1u << (unsigned int)key;
		sawKey = 1;
		while ((offset < lineLength) && NativeInputScript_IsSpace(line[offset]))
			++offset;
		if (offset == lineLength)
			break;
		if (line[offset] != ',')
			return NATIVE_INPUT_SCRIPT_ERROR_SYNTAX;
		++offset;
		while ((offset < lineLength) && NativeInputScript_IsSpace(line[offset]))
			++offset;
		if (offset == lineLength)
			return NATIVE_INPUT_SCRIPT_ERROR_SYNTAX;
	}
	if (!sawKey)
		return NATIVE_INPUT_SCRIPT_ERROR_KEY;
	entry->firstFrame = firstFrame;
	entry->lastFrame = lastFrame;
	entry->keyMask = keyMask;
	*hasEntry = 1;
	return NATIVE_INPUT_SCRIPT_ERROR_NONE;
}

int NativeInputScript_ParseText(struct NativeInputScriptSchedule *schedule,
	const char *text, size_t textByteCount)
{
	struct NativeInputScriptSchedule parsed;
	size_t lineStart = 0u;
	size_t index;

	if (schedule == NULL)
		return 0;
	NativeInputScript_Clear(schedule, NATIVE_INPUT_SCRIPT_ERROR_ARGUMENT);
	if ((text == NULL) || (textByteCount == 0u) ||
		(textByteCount > NATIVE_INPUT_SCRIPT_MAX_BYTES))
	{
		if (text == NULL)
			schedule->lastError = NATIVE_INPUT_SCRIPT_ERROR_ARGUMENT;
		else if (textByteCount > NATIVE_INPUT_SCRIPT_MAX_BYTES)
			schedule->lastError = NATIVE_INPUT_SCRIPT_ERROR_CAPACITY;
		else
			schedule->lastError = NATIVE_INPUT_SCRIPT_ERROR_EMPTY;
		return 0;
	}
	NativeInputScript_Init(&parsed);
	for (index = 0u; index <= textByteCount; ++index)
	{
		if ((index == textByteCount) || (text[index] == '\n'))
		{
			size_t lineLength = index - lineStart;
			struct NativeInputScriptEntry entry;
			int hasEntry = 0;
			enum NativeInputScriptError error;

			if ((lineLength > 0u) && (text[lineStart + lineLength - 1u] == '\r'))
				--lineLength;
			error = NativeInputScript_ParseLine(text + lineStart, lineLength, &entry, &hasEntry);
			if (error != NATIVE_INPUT_SCRIPT_ERROR_NONE)
			{
				schedule->lastError = error;
				return 0;
			}
			if (hasEntry != 0)
			{
				if (parsed.entryCount >= NATIVE_INPUT_SCRIPT_MAX_ENTRIES)
				{
					schedule->lastError = NATIVE_INPUT_SCRIPT_ERROR_CAPACITY;
					return 0;
				}
				parsed.entries[parsed.entryCount++] = entry;
			}
			lineStart = index + 1u;
		}
	}
	if (parsed.entryCount == 0u)
	{
		schedule->lastError = NATIVE_INPUT_SCRIPT_ERROR_EMPTY;
		return 0;
	}
	*schedule = parsed;
	return 1;
}

int NativeInputScript_IsKeyDown(const struct NativeInputScriptSchedule *schedule,
	uint64_t frame, enum NativeInputScriptKey key)
{
	unsigned int index;

	if ((schedule == NULL) || (schedule->lastError != NATIVE_INPUT_SCRIPT_ERROR_NONE) ||
		(key < NATIVE_INPUT_SCRIPT_KEY_RETURN) || (key >= NATIVE_INPUT_SCRIPT_KEY_COUNT))
	{
		return 0;
	}
	for (index = 0u; index < schedule->entryCount; ++index)
	{
		const struct NativeInputScriptEntry *entry = &schedule->entries[index];
		if ((frame >= entry->firstFrame) && (frame <= entry->lastFrame) &&
			((entry->keyMask & ((uint32_t)1u << (unsigned int)key)) != 0u))
		{
			return 1;
		}
	}
	return 0;
}

const char *NativeInputScript_ErrorString(enum NativeInputScriptError error)
{
	switch (error)
	{
	case NATIVE_INPUT_SCRIPT_ERROR_NONE: return "none";
	case NATIVE_INPUT_SCRIPT_ERROR_ARGUMENT: return "argument";
	case NATIVE_INPUT_SCRIPT_ERROR_EMPTY: return "empty";
	case NATIVE_INPUT_SCRIPT_ERROR_SYNTAX: return "syntax";
	case NATIVE_INPUT_SCRIPT_ERROR_RANGE: return "range";
	case NATIVE_INPUT_SCRIPT_ERROR_KEY: return "key";
	case NATIVE_INPUT_SCRIPT_ERROR_CAPACITY: return "capacity";
	case NATIVE_INPUT_SCRIPT_ERROR_IO: return "io";
	default: return "unknown";
	}
}
