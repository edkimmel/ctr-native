#ifndef PLATFORM_NATIVE_HOLD_BANNER_H
#define PLATFORM_NATIVE_HOLD_BANNER_H

#include <stdint.h>

/*
 * Hold banner layout (docs/LOCKSTEP_RACE_MILESTONE.md LR-9, slice LR-S2
 * (a)): the pure geometry of the WAITING FOR OPPONENT banner that
 * Platform_PresentVRAMDisplayBanner draws over the presented image. No GL,
 * SDL, clock, heap, or game state; the renderer
 * (NativeRenderer_DrawPresentBanner) fills each rectangle with a scissored
 * clear of the window framebuffer, after the presented image and before the
 * swap, so the banner draw writes neither VRAM nor the render target. (The
 * pinned present it rides on binds the main render target and clears or
 * reloads it from VRAM, as every Platform_PresentVRAMDisplay does; the next
 * DrawOTag rebuilds it. See include/platform.h.)
 *
 * Font: a built-in 5x7 block font of A..Z (every other character is a
 * blank cell), 6 columns per character. The retail font (DecalFont) draws
 * through the game's ordering table and primitive memory, which the hold
 * must not touch (LR-S2 (a) finding; LR-S11 settles the final style).
 *
 * Geometry, in pixels of the presentation viewport, origin top-left: the
 * text is scaled by an integer `scale` (at least 1), the largest that keeps
 * the text within 4/5 of the viewport width and at most viewportH / 40; the
 * bar spans the viewport width, is 13 * scale high (7 glyph rows and 3 rows
 * of margin above and below), and is centred vertically; the text is centred
 * in it. Each glyph row becomes one rectangle per run of set pixels.
 */

#define NATIVE_HOLD_BANNER_GLYPH_WIDTH 5
#define NATIVE_HOLD_BANNER_GLYPH_HEIGHT 7
#define NATIVE_HOLD_BANNER_ADVANCE 6
#define NATIVE_HOLD_BANNER_MARGIN 3
#define NATIVE_HOLD_BANNER_MAX_CHARS 32
/* At most 3 runs per 5-pixel row (10101). */
#define NATIVE_HOLD_BANNER_MAX_RECTS (NATIVE_HOLD_BANNER_MAX_CHARS * NATIVE_HOLD_BANNER_GLYPH_HEIGHT * 3)

struct NativeHoldBannerRect
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
};

struct NativeHoldBannerLayout
{
	int32_t scale;
	uint32_t textRectCount;
	struct NativeHoldBannerRect bar;
	struct NativeHoldBannerRect text[NATIVE_HOLD_BANNER_MAX_RECTS];
};

/* Row `row` (0 = top) of the glyph of c, the leftmost pixel in bit 4; 0 for
 * a character outside A..Z or a row outside 0..6. */
uint8_t NativeHoldBanner_GlyphRow(char c, uint32_t row);

/*
 * Lays out text (1..MAX_CHARS characters) for a viewport of the given size.
 * Returns 1 with *layout filled; 0 with *layout untouched for NULL
 * arguments, an empty or too long text, or a viewport too small for the
 * text at scale 1 (bar included).
 */
int NativeHoldBanner_Layout(const char *text, int32_t viewportW, int32_t viewportH, struct NativeHoldBannerLayout *layout);

#endif
