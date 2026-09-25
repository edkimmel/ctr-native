#include "platform/native_hold_banner.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define BANNER "WAITING FOR OPPONENT"

static struct NativeHoldBannerLayout s_layout;

static int TestGlyphs(void)
{
	const char *text = BANNER;

	/* Every letter of the banner has a glyph; space and anything outside
	 * A..Z is blank; rows outside 0..6 are blank. */
	for (size_t i = 0; text[i] != '\0'; i++)
	{
		uint32_t pixels = 0u;

		for (uint32_t row = 0; row < (uint32_t)NATIVE_HOLD_BANNER_GLYPH_HEIGHT; row++)
		{
			const uint8_t bits = NativeHoldBanner_GlyphRow(text[i], row);

			CHECK((bits & 0xE0u) == 0u);
			pixels += (uint32_t)((bits & 1u) + ((bits >> 1) & 1u) + ((bits >> 2) & 1u) + ((bits >> 3) & 1u) + ((bits >> 4) & 1u));
		}
		CHECK((text[i] == ' ') ? (pixels == 0u) : (pixels > 0u));
	}
	for (char c = 'A'; c <= 'Z'; c++)
	{
		CHECK(NativeHoldBanner_GlyphRow(c, 7u) == 0u);
		CHECK((NativeHoldBanner_GlyphRow(c, 0u) | NativeHoldBanner_GlyphRow(c, 3u) | NativeHoldBanner_GlyphRow(c, 6u)) != 0u);
	}
	CHECK(NativeHoldBanner_GlyphRow('a', 0u) == 0u);
	CHECK(NativeHoldBanner_GlyphRow('0', 3u) == 0u);
	CHECK(NativeHoldBanner_GlyphRow('@', 3u) == 0u);
	CHECK(NativeHoldBanner_GlyphRow('[', 3u) == 0u);
	CHECK(NativeHoldBanner_GlyphRow('O', 0u) == 0x0Eu);
	CHECK(NativeHoldBanner_GlyphRow('T', 0u) == 0x1Fu);
	return 0;
}

static int TestRejects(void)
{
	char longText[NATIVE_HOLD_BANNER_MAX_CHARS + 2];

	memset(&s_layout, 0x5A, sizeof(s_layout));
	CHECK(!NativeHoldBanner_Layout(NULL, 960, 720, &s_layout));
	CHECK(!NativeHoldBanner_Layout(BANNER, 960, 720, NULL));
	CHECK(!NativeHoldBanner_Layout("", 960, 720, &s_layout));
	memset(longText, 'A', sizeof(longText));
	longText[NATIVE_HOLD_BANNER_MAX_CHARS + 1] = '\0';
	CHECK(!NativeHoldBanner_Layout(longText, 4000, 3000, &s_layout));
	/* 20 characters need 119 columns and 13 rows at scale 1. */
	CHECK(!NativeHoldBanner_Layout(BANNER, 118, 720, &s_layout));
	CHECK(!NativeHoldBanner_Layout(BANNER, 960, 12, &s_layout));
	CHECK(!NativeHoldBanner_Layout(BANNER, -5, -5, &s_layout));
	CHECK(s_layout.scale == 0x5A5A5A5A);
	longText[NATIVE_HOLD_BANNER_MAX_CHARS] = '\0';
	CHECK(NativeHoldBanner_Layout(longText, 4000, 3000, &s_layout));
	return 0;
}

/* Paints the layout's text rectangles into a grid of font cells and checks
 * that it reproduces the glyphs exactly, inside the bar and the viewport. */
static int CheckLayout(const char *text, int32_t viewportW, int32_t viewportH, int32_t expectedScale)
{
	static uint8_t grid[NATIVE_HOLD_BANNER_GLYPH_HEIGHT][NATIVE_HOLD_BANNER_MAX_CHARS * NATIVE_HOLD_BANNER_ADVANCE];
	const struct NativeHoldBannerLayout *layout = &s_layout;
	const int32_t length = (int32_t)strlen(text);
	const int32_t units = (length * NATIVE_HOLD_BANNER_ADVANCE) - 1;
	int32_t originX;
	int32_t originY;
	int32_t scale;

	CHECK(NativeHoldBanner_Layout(text, viewportW, viewportH, &s_layout));
	scale = layout->scale;
	CHECK(scale == expectedScale);
	CHECK(units * scale <= viewportW);
	CHECK(layout->bar.x == 0 && layout->bar.w == viewportW);
	CHECK(layout->bar.h == 13 * scale);
	CHECK(layout->bar.y >= 0 && layout->bar.y + layout->bar.h <= viewportH);
	CHECK(layout->bar.y == (viewportH - layout->bar.h) / 2);
	CHECK(layout->textRectCount > 0u && layout->textRectCount <= (uint32_t)NATIVE_HOLD_BANNER_MAX_RECTS);
	originX = (viewportW - (units * scale)) / 2;
	originY = layout->bar.y + (3 * scale);
	memset(grid, 0, sizeof(grid));
	for (uint32_t i = 0; i < layout->textRectCount; i++)
	{
		const struct NativeHoldBannerRect *rect = &layout->text[i];
		int32_t cellX;
		int32_t cellY;

		CHECK(rect->w > 0 && rect->h == scale && (rect->w % scale) == 0);
		CHECK(rect->x >= layout->bar.x && rect->x + rect->w <= layout->bar.x + layout->bar.w);
		CHECK(rect->y >= layout->bar.y && rect->y + rect->h <= layout->bar.y + layout->bar.h);
		CHECK(((rect->x - originX) % scale) == 0 && ((rect->y - originY) % scale) == 0);
		cellX = (rect->x - originX) / scale;
		cellY = (rect->y - originY) / scale;
		CHECK(cellY >= 0 && cellY < NATIVE_HOLD_BANNER_GLYPH_HEIGHT);
		for (int32_t c = 0; c < rect->w / scale; c++)
		{
			CHECK(cellX + c >= 0 && cellX + c < units);
			CHECK(grid[cellY][cellX + c] == 0u);
			grid[cellY][cellX + c] = 1u;
		}
	}
	for (int32_t row = 0; row < NATIVE_HOLD_BANNER_GLYPH_HEIGHT; row++)
	{
		for (int32_t column = 0; column < units; column++)
		{
			const int32_t character = column / NATIVE_HOLD_BANNER_ADVANCE;
			const int32_t pixel = column % NATIVE_HOLD_BANNER_ADVANCE;
			uint8_t expected = 0u;

			if (pixel < NATIVE_HOLD_BANNER_GLYPH_WIDTH)
			{
				expected = ((NativeHoldBanner_GlyphRow(text[character], (uint32_t)row) & (0x10u >> pixel)) != 0u) ? 1u : 0u;
			}
			CHECK(grid[row][column] == expected);
		}
	}
	return 0;
}

static int TestLayouts(void)
{
	/* 1280x720 window, 4:3 viewport 960x720: min(768 / 119, 720 / 40) = 6. */
	CHECK(CheckLayout(BANNER, 960, 720, 6) == 0);
	/* 640x480: min(512 / 119, 12) = 4. */
	CHECK(CheckLayout(BANNER, 640, 480, 4) == 0);
	/* Tall and narrow: the width bounds it. Short and wide: the height does. */
	CHECK(CheckLayout(BANNER, 300, 2000, 2) == 0);
	CHECK(CheckLayout(BANNER, 4000, 120, 3) == 0);
	/* Tiny: scale 1. */
	CHECK(CheckLayout(BANNER, 119, 13, 1) == 0);
	CHECK(CheckLayout("W", 40, 40, 1) == 0);
	/* The same input gives the same layout. */
	CHECK(NativeHoldBanner_Layout(BANNER, 960, 720, &s_layout));
	{
		static struct NativeHoldBannerLayout again;

		CHECK(NativeHoldBanner_Layout(BANNER, 960, 720, &again));
		CHECK(memcmp(&again, &s_layout, sizeof(again)) == 0);
	}
	return 0;
}

int main(void)
{
	CHECK(TestGlyphs() == 0);
	CHECK(TestRejects() == 0);
	CHECK(TestLayouts() == 0);
	puts("native_hold_banner_test: ok");
	return 0;
}
