#ifndef PLATFORM_NATIVE_SDL_ASSERT_H
#define PLATFORM_NATIVE_SDL_ASSERT_H

#include <SDL3/SDL_assert.h>

#include <stddef.h>

/* Host-only SDL assertion policy.  SDL's default handler opens a modal
 * "Assertion Failed" dialog, which blocks an unattended cabinet forever (for
 * example SDL_hid.c's device-notification counter assertion when
 * SDL_Init(SDL_INIT_VIDEO) fails on a session with no display).  The installed
 * handler logs one line and ignores the assertion from then on, matching
 * Release semantics where SDL_assert compiles out.  An explicit developer
 * override through the SDL_ASSERT hint or environment variable (for example
 * "break" or "abort") is still honoured by delegating to SDL's default
 * handler. */

typedef void (*NativeSdlAssertLogFn)(const char *line);

/* Installs the handler with SDL_SetAssertionHandler.  logFn receives each
 * formatted line instead of stderr (it owns any console copy); NULL means
 * stderr only. */
void NativeSdlAssert_Install(NativeSdlAssertLogFn logFn);

/* Formats one newline-terminated report line into buf.  Returns the length
 * the full line would have (as snprintf does), or -1 when buf/cap/data is
 * unusable.  When cap > 0 the result is always NUL-terminated, and a
 * truncated result with cap >= 2 still ends in '\n'.  NULL condition,
 * function and filename fields are printed as "(unknown)". */
int NativeSdlAssert_FormatLine(char *buf, size_t cap, const SDL_AssertData *data);

#endif
