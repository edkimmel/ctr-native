#include <platform/native_input_script.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

int main(void)
{
	struct NativeInputScriptSchedule schedule;
	char capacity[NATIVE_INPUT_SCRIPT_MAX_ENTRIES * 12u + 32u];
	size_t offset = 0u;
	unsigned int index;
	static const char valid[] =
		"# local capture only\r\n"
		"0-1 Return\r\n"
		"5 - 7 down, C, Space\n"
		"7-7 ENTER, leftbracket\n";

	NativeInputScript_Init(&schedule);
	CHECK(schedule.entryCount == 0u);
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_NONE);
	CHECK(NativeInputScript_ParseText(&schedule, valid, sizeof(valid) - 1u));
	CHECK(schedule.entryCount == 3u);
	CHECK(NativeInputScript_IsKeyDown(&schedule, 0u, NATIVE_INPUT_SCRIPT_KEY_RETURN));
	CHECK(NativeInputScript_IsKeyDown(&schedule, 1u, NATIVE_INPUT_SCRIPT_KEY_RETURN));
	CHECK(!NativeInputScript_IsKeyDown(&schedule, 2u, NATIVE_INPUT_SCRIPT_KEY_RETURN));
	CHECK(NativeInputScript_IsKeyDown(&schedule, 5u, NATIVE_INPUT_SCRIPT_KEY_DOWN));
	CHECK(NativeInputScript_IsKeyDown(&schedule, 7u, NATIVE_INPUT_SCRIPT_KEY_C));
	CHECK(NativeInputScript_IsKeyDown(&schedule, 7u, NATIVE_INPUT_SCRIPT_KEY_SPACE));
	CHECK(NativeInputScript_IsKeyDown(&schedule, 7u, NATIVE_INPUT_SCRIPT_KEY_RETURN));
	CHECK(NativeInputScript_IsKeyDown(&schedule, 7u, NATIVE_INPUT_SCRIPT_KEY_LBRACKET));
	CHECK(!NativeInputScript_IsKeyDown(&schedule, 8u, NATIVE_INPUT_SCRIPT_KEY_C));
	CHECK(!NativeInputScript_IsKeyDown(&schedule, 7u, NATIVE_INPUT_SCRIPT_KEY_COUNT));

	CHECK(!NativeInputScript_ParseText(&schedule, "1-0 C", 5u));
	CHECK(schedule.entryCount == 0u);
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_RANGE);
	CHECK(!NativeInputScript_IsKeyDown(&schedule, 0u, NATIVE_INPUT_SCRIPT_KEY_C));
	CHECK(!NativeInputScript_ParseText(&schedule, "0-1 C X", 7u));
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_SYNTAX);
	CHECK(!NativeInputScript_ParseText(&schedule, "0-1 Nope", 8u));
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_KEY);
	CHECK(!NativeInputScript_ParseText(&schedule, "# only comments\n\n", 17u));
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_EMPTY);
	CHECK(!NativeInputScript_ParseText(&schedule, NULL, 0u));
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_ARGUMENT);

	for (index = 0u; index <= NATIVE_INPUT_SCRIPT_MAX_ENTRIES; ++index)
	{
		offset += (size_t)snprintf(capacity + offset, sizeof(capacity) - offset,
			"%u-%u C\n", index, index);
	}
	CHECK(offset < sizeof(capacity));
	CHECK(!NativeInputScript_ParseText(&schedule, capacity, offset));
	CHECK(schedule.lastError == NATIVE_INPUT_SCRIPT_ERROR_CAPACITY);

	CHECK(strcmp(NativeInputScript_ErrorString(NATIVE_INPUT_SCRIPT_ERROR_KEY), "key") == 0);
	return 0;
}
