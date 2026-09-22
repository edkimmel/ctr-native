#include <platform/native_sdl_assert.h>

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int s_captureCount = 0;
static char s_captured[1024];

static void CaptureLine(const char *line)
{
	s_captureCount++;
	(void)snprintf(s_captured, sizeof(s_captured), "%s", (line != NULL) ? line : "");
}

/* SDL keeps pointers to reported records until SDL_Quit, so they are static. */
static SDL_AssertData s_ignoredRecord = {false, 0u, "ignored_condition != 0", NULL, 0, NULL, NULL};
static SDL_AssertData s_delegatedRecord = {false, 0u, "delegated_condition != 0", NULL, 0, NULL, NULL};
static SDL_AssertData s_stderrOnlyRecord = {false, 0u, "stderr_only_condition != 0", NULL, 0, NULL, NULL};
static SDL_AssertData s_routedLogRecord = {false, 0u, "routed_log_condition != 0", NULL, 0, NULL, NULL};
static SDL_AssertData s_routedStderrRecord = {false, 0u, "routed_stderr_condition != 0", NULL, 0, NULL, NULL};

#define STDERR_CAPTURE_PATH "native_sdl_assert_test_stderr.txt"

static int s_stderrRedirected = 0;

/* stderr is redirected below, so these checks report on stdout. */
#define CHECK_STDOUT(expression) do { if (!(expression)) { printf("CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int TestFormatter(void)
{
	static const char expectedOnce[] =
		"[CTR Native] SDL assertion failed: 'x == 1' at fn (file.c:42), triggered 1 time; continuing\n";
	static const char expectedTwice[] =
		"[CTR Native] SDL assertion failed: 'x == 1' at fn (file.c:42), triggered 3 times; continuing\n";
	static const char expectedUnknown[] =
		"[CTR Native] SDL assertion failed: '(unknown)' at (unknown) ((unknown):7), triggered 1 time; continuing\n";
	SDL_AssertData data = {false, 1u, "x == 1", "file.c", 42, "fn", NULL};
	SDL_AssertData empty = {false, 1u, NULL, NULL, 7, NULL, NULL};
	char buf[256];
	char small[32];

	CHECK(NativeSdlAssert_FormatLine(buf, sizeof(buf), &data) == (int)strlen(expectedOnce));
	CHECK(strcmp(buf, expectedOnce) == 0);

	data.trigger_count = 3u;
	CHECK(NativeSdlAssert_FormatLine(buf, sizeof(buf), &data) == (int)strlen(expectedTwice));
	CHECK(strcmp(buf, expectedTwice) == 0);
	data.trigger_count = 1u;

	CHECK(NativeSdlAssert_FormatLine(buf, sizeof(buf), &empty) == (int)strlen(expectedUnknown));
	CHECK(strcmp(buf, expectedUnknown) == 0);

	/* Truncation: NUL-terminated inside cap, still newline-terminated, and
	 * nothing past cap is written. */
	memset(small, 'Z', sizeof(small));
	CHECK(NativeSdlAssert_FormatLine(small, 16, &data) == (int)strlen(expectedOnce));
	CHECK(strlen(small) == 15);
	CHECK(memcmp(small, expectedOnce, 14) == 0);
	CHECK(small[14] == '\n');
	CHECK(small[15] == '\0');
	CHECK(small[16] == 'Z');

	memset(small, 'Z', sizeof(small));
	CHECK(NativeSdlAssert_FormatLine(small, 1, &data) == (int)strlen(expectedOnce));
	CHECK(small[0] == '\0');
	CHECK(small[1] == 'Z');

	memset(small, 'Z', sizeof(small));
	CHECK(NativeSdlAssert_FormatLine(small, 2, &data) == (int)strlen(expectedOnce));
	CHECK(small[0] == '\n');
	CHECK(small[1] == '\0');
	CHECK(small[2] == 'Z');

	/* Unusable arguments. */
	memset(small, 'Z', sizeof(small));
	CHECK(NativeSdlAssert_FormatLine(small, 0, &data) == -1);
	CHECK(small[0] == 'Z');
	CHECK(NativeSdlAssert_FormatLine(NULL, sizeof(small), &data) == -1);
	CHECK(NativeSdlAssert_FormatLine(small, sizeof(small), NULL) == -1);
	CHECK(small[0] == '\0');

	return 0;
}

static int TestHandler(void)
{
	SDL_AssertState state;

	/* No SDL_ASSERT override may be active, or the handler would delegate.
	 * Even then SDL's default handler honours the override without a dialog;
	 * the only dialog path (default handler, no override) is unreachable. */
	(void)SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SDL_ASSERT");
	/* SDL_ResetHint returns false when the hint was never set; the effective
	 * value is what matters. */
	(void)SDL_ResetHint(SDL_HINT_ASSERT);
	CHECK(SDL_GetHint(SDL_HINT_ASSERT) == NULL);

	NativeSdlAssert_Install(CaptureLine);
	CHECK(SDL_GetAssertionHandler(NULL) != SDL_GetDefaultAssertionHandler());

	/* Non-delegate case: one log line, and SDL_ReportAssertion maps the
	 * handler's SDL_ASSERTION_ALWAYS_IGNORE to SDL_ASSERTION_IGNORE while
	 * marking the record always_ignore. */
	state = SDL_ReportAssertion(&s_ignoredRecord, "fn", "file.c", 42);
	CHECK(state == SDL_ASSERTION_IGNORE);
	CHECK(s_captureCount == 1);
	CHECK(strstr(s_captured, "'ignored_condition != 0'") != NULL);
	CHECK(strstr(s_captured, "at fn (file.c:42)") != NULL);
	CHECK(strstr(s_captured, "triggered 1 time;") != NULL);
	CHECK(strchr(s_captured, '\n') == s_captured + strlen(s_captured) - 1);
	CHECK(s_ignoredRecord.always_ignore);
	CHECK(s_ignoredRecord.trigger_count == 1u);

	/* Always-ignore: SDL no longer calls the handler for this record. */
	state = SDL_ReportAssertion(&s_ignoredRecord, "fn", "file.c", 42);
	CHECK(state == SDL_ASSERTION_IGNORE);
	CHECK(s_captureCount == 1);
	CHECK(s_ignoredRecord.trigger_count == 2u);

	/* Delegate case: an explicit override is honoured by SDL's default
	 * handler; this module does not log it. */
	CHECK(SDL_SetHint(SDL_HINT_ASSERT, "ignore"));
	CHECK(SDL_GetHint(SDL_HINT_ASSERT) != NULL);
	CHECK(strcmp(SDL_GetHint(SDL_HINT_ASSERT), "ignore") == 0);
	state = SDL_ReportAssertion(&s_delegatedRecord, "fn2", "other.c", 7);
	CHECK(state == SDL_ASSERTION_IGNORE);
	CHECK(s_captureCount == 1);
	CHECK(!s_delegatedRecord.always_ignore);
	CHECK(SDL_ResetHint(SDL_HINT_ASSERT));
	CHECK(SDL_GetHint(SDL_HINT_ASSERT) == NULL);

	/* A NULL log callback reports to stderr only. */
	NativeSdlAssert_Install(NULL);
	state = SDL_ReportAssertion(&s_stderrOnlyRecord, "fn3", "third.c", 9);
	CHECK(state == SDL_ASSERTION_IGNORE);
	CHECK(s_captureCount == 1);
	CHECK(s_stderrOnlyRecord.always_ignore);

	return 0;
}

/* With a log callback the line goes to the callback only (the platform log
 * owns the console copy, so the console must not see it twice); without one
 * it goes to stderr.  stderr cannot be portably restored after freopen, so
 * this runs last. */
static int TestStderrRouting(void)
{
	char captured[2048];
	size_t length;
	FILE *file;
	int captureBefore;

	if (freopen(STDERR_CAPTURE_PATH, "w", stderr) == NULL)
	{
		printf("CHECK failed: cannot redirect stderr to %s\n", STDERR_CAPTURE_PATH);
		return 1;
	}
	s_stderrRedirected = 1;

	captureBefore = s_captureCount;
	NativeSdlAssert_Install(CaptureLine);
	(void)SDL_ReportAssertion(&s_routedLogRecord, "fn4", "fourth.c", 11);
	CHECK_STDOUT(s_captureCount == captureBefore + 1);
	CHECK_STDOUT(strstr(s_captured, "'routed_log_condition != 0'") != NULL);

	NativeSdlAssert_Install(NULL);
	(void)SDL_ReportAssertion(&s_routedStderrRecord, "fn5", "fifth.c", 13);
	CHECK_STDOUT(s_captureCount == captureBefore + 1);

	fflush(stderr);
	file = fopen(STDERR_CAPTURE_PATH, "r");
	CHECK_STDOUT(file != NULL);
	length = fread(captured, 1, sizeof(captured) - 1, file);
	captured[length] = '\0';
	fclose(file);

	CHECK_STDOUT(strstr(captured, "routed_log_condition") == NULL);
	CHECK_STDOUT(strstr(captured, "'routed_stderr_condition != 0' at fn5 (fifth.c:13)") != NULL);

	return 0;
}

int main(void)
{
	int result;

	/* A run that died mid-test may have left the capture file behind. */
	(void)remove(STDERR_CAPTURE_PATH);

	if (TestFormatter() != 0)
	{
		return 1;
	}

	if (!SDL_Init(0))
	{
		fprintf(stderr, "SDL_Init(0) failed: %s\n", SDL_GetError());
		return 1;
	}

	result = TestHandler();
	if (result == 0)
	{
		result = TestStderrRouting();
	}
	SDL_Quit();

	/* stderr may name the capture file; close it so the file is removed on
	 * failure as well as success. */
	if (s_stderrRedirected != 0)
	{
		(void)fclose(stderr);
	}
	(void)remove(STDERR_CAPTURE_PATH);

	if (result != 0)
	{
		return 1;
	}

	printf("native_sdl_assert tests passed\n");
	return 0;
}
