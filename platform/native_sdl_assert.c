#include <platform/native_sdl_assert.h>

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_hints.h>

#include <stddef.h>
#include <stdio.h>

#define NATIVE_SDL_ASSERT_LINE_CAPACITY 1024

static NativeSdlAssertLogFn s_nativeSdlAssertLogFn = NULL;

static const char *NativeSdlAssert_Field(const char *value)
{
	return (value != NULL) ? value : "(unknown)";
}

int NativeSdlAssert_FormatLine(char *buf, size_t cap, const SDL_AssertData *data)
{
	int written;

	if ((buf == NULL) || (cap == 0))
	{
		return -1;
	}

	buf[0] = '\0';

	if (data == NULL)
	{
		return -1;
	}

	written = snprintf(
		buf,
		cap,
		"[CTR Native] SDL assertion failed: '%s' at %s (%s:%d), triggered %u %s; continuing\n",
		NativeSdlAssert_Field(data->condition),
		NativeSdlAssert_Field(data->function),
		NativeSdlAssert_Field(data->filename),
		data->linenum,
		data->trigger_count,
		(data->trigger_count == 1u) ? "time" : "times");

	if (written < 0)
	{
		buf[0] = '\0';
		return -1;
	}

	buf[cap - 1] = '\0';

	/* Keep a truncated report a whole line so the log stays line-oriented. */
	if (((size_t)written >= cap) && (cap >= 2))
	{
		buf[cap - 2] = '\n';
	}

	return written;
}

static SDL_AssertState SDLCALL NativeSdlAssert_Handler(const SDL_AssertData *data, void *userdata)
{
	char line[NATIVE_SDL_ASSERT_LINE_CAPACITY];

	/* An explicit SDL_ASSERT override (hint or environment variable) is a
	 * developer's choice; SDL's default handler honours it without a dialog. */
	if (SDL_GetHint(SDL_HINT_ASSERT) != NULL)
	{
		SDL_AssertionHandler defaultHandler = SDL_GetDefaultAssertionHandler();
		return defaultHandler(data, userdata);
	}

	if (NativeSdlAssert_FormatLine(line, sizeof(line), data) < 0)
	{
		(void)snprintf(line, sizeof(line), "[CTR Native] SDL assertion failed; continuing\n");
	}

	/* The log callback owns the console copy (the platform log already
	 * writes errors to stderr), so stderr is used directly only without one. */
	if (s_nativeSdlAssertLogFn != NULL)
	{
		s_nativeSdlAssertLogFn(line);
	}
	else
	{
		fputs(line, stderr);
		fflush(stderr);
	}

	return SDL_ASSERTION_ALWAYS_IGNORE;
}

void NativeSdlAssert_Install(NativeSdlAssertLogFn logFn)
{
	s_nativeSdlAssertLogFn = logFn;
	SDL_SetAssertionHandler(NativeSdlAssert_Handler, NULL);
}
