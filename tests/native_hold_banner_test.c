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


/*
 * The game font (LR-S11, LR-72) over a synthetic VRAM image: no retail data.
 * The banner's letters are synthetic 4-bit glyphs on texture page 5 at
 * y 256 (tpage 0x15), CLUT at (32, 480); an 8-bit page 7 at y 0 (tpage
 * 0x87) with a 256-entry CLUT at (256, 500) covers the other depth.
 */

#define VRAM_W NATIVE_HOLD_BANNER_VRAM_WIDTH
#define VRAM_H NATIVE_HOLD_BANNER_VRAM_HEIGHT
#define TPAGE4 0x0015u                       /* page x 5 * 64 = 320, y 256, 4-bit */
#define CLUT4 ((uint16_t)((480u << 6) | 2u)) /* (32, 480) */
#define TPAGE8 0x0087u                       /* page x 7 * 64 = 448, y 0, 8-bit */
#define CLUT8 ((uint16_t)((500u << 6) | 16u)) /* (256, 500) */
#define WHITE_SHADE 0x808080u                /* 0x80: texels as they are */
#define GLYPH_W 10
#define GLYPH_H 8
#define ADVANCE 13

static uint16_t s_vram[VRAM_W * VRAM_H];
static struct NativeHoldBannerGlyphs s_glyphs;
static struct NativeHoldBannerGlyphLayout s_glyphLayout;

/* The 5 CLUT entries of the 4-bit font: 0 transparent, then white, red,
 * green, and a mid grey. */
static const uint16_t k_clut4[5] = {0x0000u, 0x7FFFu, 0x001Fu, 0x03E0u, 0x4210u};

static void PutTexel(uint16_t tpage, uint32_t u, uint32_t v, uint32_t index)
{
	const uint32_t pageX = (tpage & 0xFu) * 64u;
	const uint32_t pageY = ((tpage & 0x10u) != 0u) ? 256u : 0u;
	uint16_t *word;
	uint32_t shift;
	uint32_t mask;

	if (((tpage >> 7) & 3u) == 0u)
	{
		word = &s_vram[((pageY + v) * VRAM_W) + pageX + (u >> 2)];
		shift = (u & 3u) * 4u;
		mask = 0xFu;
	}
	else
	{
		word = &s_vram[((pageY + v) * VRAM_W) + pageX + (u >> 1)];
		shift = (u & 1u) * 8u;
		mask = 0xFFu;
	}
	*word = (uint16_t)((*word & ~(mask << shift)) | ((index & mask) << shift));
}

static void PutClut(uint16_t clut, uint32_t index, uint16_t value)
{
	s_vram[((uint32_t)(clut >> 6) * VRAM_W) + ((clut & 0x3Fu) * 16u) + index] = value;
}

/* The synthetic pattern of glyph i: CLUT index 0..4, 0 transparent. */
static uint32_t Pattern(uint32_t i, uint32_t s, uint32_t t)
{
	return ((i * 7u) + (s * 3u) + (t * 5u)) % 5u;
}

/* Builds the 4-bit font's VRAM and a valid table for text: every letter an
 * ICON at its own (u, v), every space a BLANK, all advancing ADVANCE. */
static void BuildTable(const char *text)
{
	const size_t length = strlen(text);

	memset(s_vram, 0, sizeof(s_vram));
	for (uint32_t index = 0; index < 5u; index++)
	{
		PutClut(CLUT4, index, k_clut4[index]);
	}
	memset(&s_glyphs, 0, sizeof(s_glyphs));
	s_glyphs.count = (uint32_t)length;
	s_glyphs.color = WHITE_SHADE;
	for (uint32_t i = 0; i < (uint32_t)length; i++)
	{
		struct NativeHoldBannerGlyph *glyph = &s_glyphs.glyphs[i];

		glyph->advance = ADVANCE;
		if (text[i] == ' ')
		{
			glyph->kind = NATIVE_HOLD_BANNER_GLYPH_BLANK;
			continue;
		}
		glyph->kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
		glyph->tpage = TPAGE4;
		glyph->clut = CLUT4;
		glyph->u = (uint8_t)(((i % 10u) * 24u) + 3u);
		glyph->v = (uint8_t)(((i / 10u) * 16u) + 1u);
		glyph->width = GLYPH_W;
		glyph->height = GLYPH_H;
		for (uint32_t t = 0; t < (uint32_t)GLYPH_H; t++)
		{
			for (uint32_t s = 0; s < (uint32_t)GLYPH_W; s++)
			{
				PutTexel(TPAGE4, glyph->u + s, glyph->v + t, Pattern(i, s, t));
			}
		}
	}
}

static int TestModulate(void)
{
	/* 0x80 leaves a channel as is; red is bits 0-4 in, bits 0-7 out. */
	CHECK(NativeHoldBanner_Modulate(0x7FFFu, 0x808080u) == 0xFFFFFFu);
	CHECK(NativeHoldBanner_Modulate(0x4210u, 0x808080u) == 0x848484u); /* 16 -> (16 << 3) | (16 >> 2) */
	CHECK(NativeHoldBanner_Modulate(0x001Fu, 0x808080u) == 0x0000FFu);
	CHECK(NativeHoldBanner_Modulate(0x03E0u, 0x808080u) == 0x00FF00u);
	CHECK(NativeHoldBanner_Modulate(0x7C00u, 0x808080u) == 0xFF0000u);
	/* Brighter than 0x80 doubles at 0xFF and clamps at 31. */
	CHECK(NativeHoldBanner_Modulate(0x4210u, 0xFFFFFFu) == 0xFFFFFFu); /* 16 * 255 >> 7 = 31 */
	CHECK(NativeHoldBanner_Modulate(0x2108u, 0xFFFFFFu) == 0x7B7B7Bu); /* 8 * 255 >> 7 = 15 -> (15 << 3) | (15 >> 2) */
	/* Per channel, and darker. */
	CHECK(NativeHoldBanner_Modulate(0x7FFFu, 0x000040u) == 0x00007Bu); /* 31 * 64 >> 7 = 15 -> 0x7B */
	CHECK(NativeHoldBanner_Modulate(0x7FFFu, 0x000000u) == 0x000000u);
	CHECK(NativeHoldBanner_Modulate(0x8000u, 0xFFFFFFu) == 0x000000u); /* the STP bit is no colour */
	return 0;
}

static int TestTexels(void)
{
	struct NativeHoldBannerGlyph glyph;
	uint16_t entry = 0x1234u;

	memset(s_vram, 0, sizeof(s_vram));
	/* 4-bit: u 3..12 spans words 0..3 of the page row; the lowest nibble is
	 * the leftmost texel. */
	memset(&glyph, 0, sizeof(glyph));
	glyph.kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
	glyph.tpage = TPAGE4;
	glyph.clut = CLUT4;
	glyph.u = 3u;
	glyph.v = 250u;
	glyph.width = 10;
	glyph.height = 6;
	glyph.advance = 13;
	for (uint32_t index = 0; index < 16u; index++)
	{
		PutClut(CLUT4, index, (uint16_t)(index == 0u ? 0u : (0x0400u | index)));
	}
	s_vram[((256u + 250u) * VRAM_W) + 320u + 0u] = 0x5000u; /* texel 3 = 5 */
	s_vram[((256u + 250u) * VRAM_W) + 320u + 1u] = 0x0A21u; /* texels 4..7 = 1, 2, 0xA, 0 */
	s_vram[((256u + 255u) * VRAM_W) + 320u + 3u] = 0x00F0u; /* row 5, texel 13 = 0xF (outside), texel 12 = 0 */
	s_vram[((256u + 255u) * VRAM_W) + 320u + 3u] |= 0x000Eu; /* texel 12 = 0xE */
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 0u, 0u, &entry) == 1 && entry == 0x0405u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 1u, 0u, &entry) == 1 && entry == 0x0401u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 2u, 0u, &entry) == 1 && entry == 0x0402u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 3u, 0u, &entry) == 1 && entry == 0x040Au);
	/* Transparent: index 0 is entry 0x0000. */
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 4u, 0u, &entry) == 0 && entry == 0u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 9u, 5u, &entry) == 1 && entry == 0x040Eu);
	/* Out of range: untouched. */
	entry = 0x1234u;
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 10u, 0u, &entry) == 0 && entry == 0x1234u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 0u, 6u, &entry) == 0 && entry == 0x1234u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, NULL, 0u, 0u, &entry) == 0 && entry == 0x1234u);
	CHECK(NativeHoldBanner_GlyphTexel(NULL, s_vram, 0u, 0u, &entry) == 0 && entry == 0x1234u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 0u, 0u, NULL) == 0);
	/* A nonzero entry with every colour bit clear (STP only) is opaque. */
	PutClut(CLUT4, 5u, 0x8000u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 0u, 0u, &entry) == 1 && entry == 0x8000u);

	/* 8-bit: the low byte is the leftmost texel; a 256-entry CLUT. */
	memset(&glyph, 0, sizeof(glyph));
	glyph.kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
	glyph.tpage = TPAGE8;
	glyph.clut = CLUT8;
	glyph.u = 201u;
	glyph.v = 7u;
	glyph.width = 4;
	glyph.height = 1;
	glyph.advance = 13;
	PutClut(CLUT8, 0xC3u, 0x1234u);
	PutClut(CLUT8, 0x5Au, 0x0001u);
	PutClut(CLUT8, 0xFFu, 0x7FFFu);
	s_vram[(7u * VRAM_W) + 448u + 100u] = 0xC300u; /* texels 200, 201 = 0x00, 0xC3 */
	s_vram[(7u * VRAM_W) + 448u + 101u] = 0xFF5Au; /* texels 202, 203 = 0x5A, 0xFF */
	s_vram[(7u * VRAM_W) + 448u + 102u] = 0x0000u; /* texel 204 = 0 */
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 0u, 0u, &entry) == 1 && entry == 0x1234u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 1u, 0u, &entry) == 1 && entry == 0x0001u);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 2u, 0u, &entry) == 1 && entry == 0x7FFFu);
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 3u, 0u, &entry) == 0 && entry == 0x0000u);
	/* A depth the decode refuses: nothing read. */
	glyph.tpage = (uint16_t)(TPAGE8 + 0x80u); /* 15-bit */
	entry = 0x1234u;
	CHECK(NativeHoldBanner_GlyphTexel(&glyph, s_vram, 0u, 0u, &entry) == 0 && entry == 0x1234u);
	return 0;
}

/* One refusal of NativeHoldBanner_GlyphSource: the reason, rects untouched. */
static int ExpectSource(const struct NativeHoldBannerGlyph *glyph, char c, uint32_t expected)
{
	struct NativeHoldBannerRect texels;
	struct NativeHoldBannerRect clut;

	memset(&texels, 0x5A, sizeof(texels));
	memset(&clut, 0x5A, sizeof(clut));
	CHECK(NativeHoldBanner_GlyphSource(glyph, c, &texels, &clut) == expected);
	CHECK(texels.x == 0x5A5A5A5A && texels.w == 0x5A5A5A5A && clut.x == 0x5A5A5A5A && clut.h == 0x5A5A5A5A);
	return 0;
}

static int TestGlyphSource(void)
{
	struct NativeHoldBannerGlyph glyph;
	struct NativeHoldBannerGlyph bad;
	struct NativeHoldBannerRect texels;
	struct NativeHoldBannerRect clut;

	memset(&glyph, 0, sizeof(glyph));
	glyph.kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
	glyph.tpage = TPAGE4;
	glyph.clut = CLUT4;
	glyph.u = 3u;
	glyph.v = 250u;
	glyph.width = 10;
	glyph.height = 6;
	glyph.advance = 13;
	/* 4-bit: texels 3..12 are words 0..3 of the page; 16 CLUT entries. */
	CHECK(NativeHoldBanner_GlyphSource(&glyph, 'W', &texels, &clut) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(texels.x == 320 && texels.y == 506 && texels.w == 4 && texels.h == 6);
	CHECK(clut.x == 32 && clut.y == 480 && clut.w == 16 && clut.h == 1);
	CHECK(NativeHoldBanner_GlyphSource(&glyph, 'W', NULL, NULL) == NATIVE_HOLD_BANNER_FONT_GAME);
	/* 8-bit: texels 3..12 are words 1..6; 256 CLUT entries. */
	glyph.tpage = TPAGE8;
	glyph.clut = CLUT8;
	CHECK(NativeHoldBanner_GlyphSource(&glyph, 'W', &texels, &clut) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(texels.x == 449 && texels.y == 250 && texels.w == 6 && texels.h == 6);
	CHECK(clut.x == 256 && clut.y == 500 && clut.w == 256 && clut.h == 1);

	/* A blank: only a space, zero rects. */
	memset(&bad, 0, sizeof(bad));
	bad.kind = NATIVE_HOLD_BANNER_GLYPH_BLANK;
	bad.advance = 13;
	memset(&texels, 0x5A, sizeof(texels));
	memset(&clut, 0x5A, sizeof(clut));
	CHECK(NativeHoldBanner_GlyphSource(&bad, ' ', &texels, &clut) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(texels.x == 0 && texels.y == 0 && texels.w == 0 && texels.h == 0 && clut.w == 0 && clut.h == 0);
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	bad.advance = 65;
	CHECK(ExpectSource(&bad, ' ', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	/* No icon. */
	CHECK(ExpectSource(NULL, 'W', NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	bad = glyph;
	bad.kind = NATIVE_HOLD_BANNER_GLYPH_MISSING;
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	bad.kind = 3u;
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	/* Depths not decoded: 15-bit and the reserved mode. */
	bad = glyph;
	bad.tpage = (uint16_t)((glyph.tpage & ~0x180u) | 0x100u);
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_DEPTH) == 0);
	bad.tpage = (uint16_t)(glyph.tpage | 0x180u);
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_DEPTH) == 0);
	/* Bounds: size, page, advance, CLUT. */
	{
		static const int16_t badSizes[] = {0, -1, 33, INT16_MIN};

		for (uint32_t i = 0; i < 4u; i++)
		{
			bad = glyph;
			bad.width = badSizes[i];
			CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
			bad = glyph;
			bad.height = badSizes[i];
			CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
		}
	}
	bad = glyph;
	bad.width = 32;
	bad.u = 224u;
	CHECK(NativeHoldBanner_GlyphSource(&bad, 'W', NULL, NULL) == NATIVE_HOLD_BANNER_FONT_GAME); /* u + width = 256 */
	bad.u = 225u;
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	bad = glyph;
	bad.v = 251u; /* 251 + 6 = 257 */
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	bad = glyph;
	bad.advance = -1;
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	bad.advance = 65;
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	bad.advance = 64;
	CHECK(NativeHoldBanner_GlyphSource(&bad, 'W', NULL, NULL) == NATIVE_HOLD_BANNER_FONT_GAME);
	bad = glyph;
	bad.clut = (uint16_t)(glyph.clut | 0x8000u); /* CLUT y 512 and more */
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	/* An 8-bit page's texels past the right edge of VRAM: page 15 at x 960,
	 * u 200..231 are words 1060..1075. */
	bad = glyph;
	bad.tpage = (uint16_t)(0x0Fu | 0x80u);
	bad.u = 200u;
	bad.width = 32;
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	bad.u = 120u; /* words 1020..1023 */
	bad.width = 8;
	CHECK(NativeHoldBanner_GlyphSource(&bad, 'W', &texels, NULL) == NATIVE_HOLD_BANNER_FONT_GAME && texels.x + texels.w == 1024);
	/* A CLUT past the right edge: 256 entries at x 1008. 16 fit. */
	bad = glyph;
	bad.tpage = TPAGE8;
	bad.clut = (uint16_t)((500u << 6) | 63u);
	CHECK(ExpectSource(&bad, 'W', NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	bad.tpage = TPAGE4;
	CHECK(NativeHoldBanner_GlyphSource(&bad, 'W', NULL, &clut) == NATIVE_HOLD_BANNER_FONT_GAME && clut.x == 1008 && clut.w == 16);
	/* The bottom row of VRAM: page y 256, v 250, 6 rows end at 512. */
	bad = glyph;
	bad.tpage = TPAGE4;
	CHECK(NativeHoldBanner_GlyphSource(&bad, 'W', &texels, NULL) == NATIVE_HOLD_BANNER_FONT_GAME && texels.y + texels.h == 512);
	bad.clut = (uint16_t)((511u << 6) | 2u);
	CHECK(NativeHoldBanner_GlyphSource(&bad, 'W', NULL, &clut) == NATIVE_HOLD_BANNER_FONT_GAME && clut.y == 511);
	return 0;
}

/* The resident callback's record: every rectangle asked, and a refusal. */
struct ResidentProbe
{
	uint32_t calls;
	int32_t refuseX; /* refuse the rectangle starting at (refuseX, refuseY); -1: none */
	int32_t refuseY;
	int32_t lastX;
	int32_t lastY;
	int32_t lastW;
	int32_t lastH;
};

static int Resident(void *context, int32_t x, int32_t y, int32_t w, int32_t h)
{
	struct ResidentProbe *probe = (struct ResidentProbe *)context;

	probe->calls++;
	probe->lastX = x;
	probe->lastY = y;
	probe->lastW = w;
	probe->lastH = h;
	return ((x == probe->refuseX) && (y == probe->refuseY)) ? 0 : 1;
}

/* One refusal of NativeHoldBanner_GlyphLayout: the reason, layout untouched. */
static int ExpectLayout(const char *text, const struct NativeHoldBannerGlyphs *glyphs, const uint16_t *vram, struct ResidentProbe *probe,
                        int32_t viewportW, int32_t viewportH, uint32_t expected)
{
	memset(&s_glyphLayout, 0x5A, sizeof(s_glyphLayout));
	CHECK(NativeHoldBanner_GlyphLayout(text, glyphs, vram, (probe != NULL) ? Resident : NULL, probe, viewportW, viewportH, &s_glyphLayout) ==
	      expected);
	CHECK(s_glyphLayout.scale == 0x5A5A5A5A && s_glyphLayout.rectCount == 0x5A5A5A5Au && s_glyphLayout.rects[0].color == 0x5A5A5A5Au);
	return 0;
}

static int TestGlyphFallbacks(void)
{
	struct NativeHoldBannerGlyphs saved;
	struct ResidentProbe probe;
	struct NativeHoldBannerRect texels;
	struct NativeHoldBannerRect clut;

	BuildTable(BANNER);
	saved = s_glyphs;
	memset(&probe, 0, sizeof(probe));
	probe.refuseX = -1;
	probe.refuseY = -1;
	CHECK(NativeHoldBanner_GlyphLayout(BANNER, &s_glyphs, s_vram, Resident, &probe, 960, 720, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);
	/* Two rectangles (texels, CLUT) per letter, asked in each of the two
	 * passes: 18 letters. */
	CHECK(probe.calls == 2u * 2u * 18u);
	CHECK(NativeHoldBanner_GlyphLayout(BANNER, &s_glyphs, s_vram, NULL, NULL, 960, 720, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);

	/* No table: the block font, as the roster proof's hold. */
	CHECK(ExpectLayout(BANNER, NULL, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_NO_TABLE) == 0);
	/* The table does not match the text. */
	CHECK(ExpectLayout(NULL, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TEXT) == 0);
	CHECK(ExpectLayout("", &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TEXT) == 0);
	CHECK(ExpectLayout("WAITING FOR OPPONEN", &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TEXT) == 0);
	CHECK(NativeHoldBanner_GlyphLayout(BANNER, &s_glyphs, s_vram, NULL, NULL, 960, 720, NULL) == NATIVE_HOLD_BANNER_FONT_TEXT);
	s_glyphs.count = 21u;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TEXT) == 0);
	s_glyphs.count = 0u;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TEXT) == 0);
	{
		char longText[NATIVE_HOLD_BANNER_MAX_CHARS + 2];

		memset(longText, 'W', sizeof(longText));
		longText[NATIVE_HOLD_BANNER_MAX_CHARS + 1] = '\0';
		s_glyphs.count = NATIVE_HOLD_BANNER_MAX_CHARS + 1u;
		CHECK(ExpectLayout(longText, &s_glyphs, s_vram, NULL, 4000, 3000, NATIVE_HOLD_BANNER_FONT_TEXT) == 0);
	}
	/* A character with no icon: a letter MISSING, a letter BLANK, the space
	 * MISSING. */
	s_glyphs = saved;
	s_glyphs.glyphs[4].kind = NATIVE_HOLD_BANNER_GLYPH_MISSING;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	s_glyphs.glyphs[4].kind = NATIVE_HOLD_BANNER_GLYPH_BLANK;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	s_glyphs = saved;
	s_glyphs.glyphs[7].kind = NATIVE_HOLD_BANNER_GLYPH_MISSING; /* BANNER[7] is a space */
	CHECK(BANNER[7] == ' ');
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_NO_ICON) == 0);
	/* A depth not decoded, on the last letter. */
	s_glyphs = saved;
	s_glyphs.glyphs[19].tpage = (uint16_t)(TPAGE4 | 0x100u);
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_DEPTH) == 0);
	/* Out of bounds: a size, and a CLUT. */
	s_glyphs = saved;
	s_glyphs.glyphs[0].width = 0;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	s_glyphs = saved;
	s_glyphs.glyphs[1].clut = 0x8002u;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_BOUNDS) == 0);
	/* Not resident: the callback refuses a letter's texels, or its CLUT, or
	 * there is no VRAM image. */
	s_glyphs = saved;
	CHECK(NativeHoldBanner_GlyphSource(&s_glyphs.glyphs[12], BANNER[12], &texels, &clut) == NATIVE_HOLD_BANNER_FONT_GAME);
	memset(&probe, 0, sizeof(probe));
	probe.refuseX = texels.x;
	probe.refuseY = texels.y;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, &probe, 960, 720, NATIVE_HOLD_BANNER_FONT_NOT_RESIDENT) == 0);
	CHECK(probe.lastX == texels.x && probe.lastY == texels.y && probe.lastW == texels.w && probe.lastH == texels.h);
	memset(&probe, 0, sizeof(probe));
	probe.refuseX = clut.x;
	probe.refuseY = clut.y;
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, &probe, 960, 720, NATIVE_HOLD_BANNER_FONT_NOT_RESIDENT) == 0);
	CHECK(probe.calls == 2u && probe.lastW == 16 && probe.lastH == 1); /* the first letter's CLUT */
	CHECK(ExpectLayout(BANNER, &s_glyphs, NULL, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_NOT_RESIDENT) == 0);
	/* A glyph that decodes to all-transparent: its texels, or its CLUT. */
	for (uint32_t t = 0; t < (uint32_t)GLYPH_H; t++)
	{
		for (uint32_t s = 0; s < (uint32_t)GLYPH_W; s++)
		{
			PutTexel(TPAGE4, s_glyphs.glyphs[9].u + s, s_glyphs.glyphs[9].v + t, 0u);
		}
	}
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TRANSPARENT) == 0);
	BuildTable(BANNER);
	s_glyphs.glyphs[2].clut = (uint16_t)((490u << 6) | 2u); /* an unloaded (all-zero) CLUT */
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TRANSPARENT) == 0);
	/* Text with no icon at all. */
	BuildTable("   ");
	CHECK(ExpectLayout("   ", &s_glyphs, s_vram, NULL, 960, 720, NATIVE_HOLD_BANNER_FONT_TRANSPARENT) == 0);
	/* The viewport: 20 advances of 13 need 260 columns and 8 + 6 rows. */
	BuildTable(BANNER);
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 259, 720, NATIVE_HOLD_BANNER_FONT_VIEWPORT) == 0);
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 960, 13, NATIVE_HOLD_BANNER_FONT_VIEWPORT) == 0);
	CHECK(NativeHoldBanner_GlyphLayout(BANNER, &s_glyphs, s_vram, NULL, NULL, 260, 14, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);
	/* Too many rectangles: 32x32 glyphs of alternating colours (a run per
	 * texel), 20 of them, are 20480 > 4096. */
	for (uint32_t i = 0; i < 20u; i++)
	{
		struct NativeHoldBannerGlyph *glyph = &s_glyphs.glyphs[i];

		glyph->kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
		glyph->tpage = TPAGE4;
		glyph->clut = CLUT4;
		glyph->u = (uint8_t)((i % 7u) * 32u);
		glyph->v = (uint8_t)((i / 7u) * 32u);
		glyph->width = 32;
		glyph->height = 32;
		glyph->advance = 33;
		for (uint32_t t = 0; t < 32u; t++)
		{
			for (uint32_t s = 0; s < 32u; s++)
			{
				PutTexel(TPAGE4, glyph->u + s, glyph->v + t, 1u + ((s + t) & 1u));
			}
		}
	}
	CHECK(ExpectLayout(BANNER, &s_glyphs, s_vram, NULL, 4000, 3000, NATIVE_HOLD_BANNER_FONT_RECTS) == 0);
	/* Every name. */
	CHECK(strcmp(NativeHoldBanner_FontName(NATIVE_HOLD_BANNER_FONT_GAME), "game font") == 0);
	for (uint32_t font = NATIVE_HOLD_BANNER_FONT_NO_TABLE; font <= NATIVE_HOLD_BANNER_FONT_RECTS + 1u; font++)
	{
		CHECK(strncmp(NativeHoldBanner_FontName(font), "block font (", 12u) == 0);
		for (uint32_t other = NATIVE_HOLD_BANNER_FONT_GAME; other < font; other++)
		{
			CHECK(strcmp(NativeHoldBanner_FontName(font), NativeHoldBanner_FontName(other)) != 0);
		}
	}
	CHECK(strcmp(NativeHoldBanner_FontName(0xFFFFFFFFu), "block font (unknown)") == 0);
	return 0;
}

/* Paints the game-font layout into a grid of texel cells and checks that it
 * reproduces every glyph's modulated texels at its pen position exactly,
 * inside the bar, with transparent texels and blanks left empty, and that
 * each rectangle is one run of one colour (maximal: its neighbours in the
 * row differ or are empty). */
static int CheckGlyphLayout(const char *text, int32_t viewportW, int32_t viewportH, int32_t expectedScale, int32_t expectedExtent,
                            int32_t expectedLine)
{
	static uint32_t grid[NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE][NATIVE_HOLD_BANNER_MAX_CHARS * 2 * NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE];
	const struct NativeHoldBannerGlyphLayout *layout = &s_glyphLayout;
	int32_t scale;
	int32_t originX;
	int32_t originY;
	int32_t pen = 0;

	CHECK(NativeHoldBanner_GlyphLayout(text, &s_glyphs, s_vram, NULL, NULL, viewportW, viewportH, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);
	scale = layout->scale;
	CHECK(scale == expectedScale);
	CHECK(expectedExtent * scale <= viewportW);
	CHECK(layout->bar.x == 0 && layout->bar.w == viewportW);
	CHECK(layout->bar.h == (expectedLine + 6) * scale);
	CHECK(layout->bar.y == (viewportH - layout->bar.h) / 2);
	CHECK(layout->rectCount > 0u && layout->rectCount <= (uint32_t)NATIVE_HOLD_BANNER_MAX_GLYPH_RECTS);
	originX = (viewportW - (expectedExtent * scale)) / 2;
	originY = layout->bar.y + (3 * scale);
	memset(grid, 0, sizeof(grid));
	for (uint32_t i = 0; i < layout->rectCount; i++)
	{
		const struct NativeHoldBannerColorRect *rect = &layout->rects[i];
		int32_t cellX;
		int32_t cellY;

		CHECK(rect->w > 0 && rect->h == scale && (rect->w % scale) == 0);
		CHECK(rect->color != 0u && (rect->color & 0xFF000000u) == 0u);
		CHECK(rect->y >= layout->bar.y && rect->y + rect->h <= layout->bar.y + layout->bar.h);
		CHECK(((rect->x - originX) % scale) == 0 && ((rect->y - originY) % scale) == 0);
		cellX = (rect->x - originX) / scale;
		cellY = (rect->y - originY) / scale;
		CHECK(cellY >= 0 && cellY < expectedLine);
		for (int32_t c = 0; c < rect->w / scale; c++)
		{
			CHECK(cellX + c >= 0 && cellX + c < expectedExtent);
			CHECK(grid[cellY][cellX + c] == 0u);
			grid[cellY][cellX + c] = rect->color;
		}
	}
	/* Expected cells. */
	for (uint32_t i = 0; i < s_glyphs.count; i++)
	{
		const struct NativeHoldBannerGlyph *glyph = &s_glyphs.glyphs[i];

		if (glyph->kind == NATIVE_HOLD_BANNER_GLYPH_ICON)
		{
			for (uint32_t t = 0; t < (uint32_t)glyph->height; t++)
			{
				for (uint32_t s = 0; s < (uint32_t)glyph->width; s++)
				{
					uint16_t entry = 0u;
					const uint32_t expected =
						NativeHoldBanner_GlyphTexel(glyph, s_vram, s, t, &entry) ? NativeHoldBanner_Modulate(entry, s_glyphs.color) : 0u;

					CHECK(grid[t][pen + (int32_t)s] == expected);
					grid[t][pen + (int32_t)s] = 0u;
				}
			}
		}
		pen += glyph->advance;
	}
	/* Nothing else was painted. */
	for (int32_t row = 0; row < expectedLine; row++)
	{
		for (int32_t column = 0; column < expectedExtent; column++)
		{
			CHECK(grid[row][column] == 0u);
		}
	}
	/* Maximal runs: no two rectangles of one colour touch along a row. */
	for (uint32_t i = 0; i < layout->rectCount; i++)
	{
		for (uint32_t j = 0; j < layout->rectCount; j++)
		{
			const struct NativeHoldBannerColorRect *a = &layout->rects[i];
			const struct NativeHoldBannerColorRect *b = &layout->rects[j];

			CHECK(!((a->y == b->y) && (a->x + a->w == b->x) && (a->color == b->color)));
		}
	}
	return 0;
}

static int TestGlyphLayouts(void)
{
	static struct NativeHoldBannerGlyphLayout again;

	/* The banner: 20 advances of 13 = 260 columns, 8 rows. 960x720:
	 * min(768 / 260, 720 / 40) = 2. */
	BuildTable(BANNER);
	CHECK(CheckGlyphLayout(BANNER, 960, 720, 2, 260, 8) == 0);
	/* 1920x1080: min(1536 / 260, 27) = 5; 640x480: 512 / 260 = 1. */
	CHECK(CheckGlyphLayout(BANNER, 1920, 1080, 5, 260, 8) == 0);
	CHECK(CheckGlyphLayout(BANNER, 640, 480, 1, 260, 8) == 0);
	/* Short and wide: the height bounds it (4000 x 120: 120 / 40 = 3). */
	CHECK(CheckGlyphLayout(BANNER, 4000, 120, 3, 260, 8) == 0);
	/* Deterministic. */
	CHECK(NativeHoldBanner_GlyphLayout(BANNER, &s_glyphs, s_vram, NULL, NULL, 960, 720, &again) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(NativeHoldBanner_GlyphLayout(BANNER, &s_glyphs, s_vram, NULL, NULL, 960, 720, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(memcmp(&again, &s_glyphLayout, sizeof(again)) == 0);

	/* The advance is the table's, per character (DecalFont's punctuation
	 * width for '.'), and the extent the furthest of the pen and a glyph's
	 * right edge: "W.W" with advances 13, 7, 13 and the last glyph 16 wide
	 * spans 13 + 7 + 16 = 36 columns; the tallest glyph sets the line. */
	BuildTable("W.W");
	s_glyphs.glyphs[1].advance = 7;
	s_glyphs.glyphs[1].width = 4;
	s_glyphs.glyphs[1].height = 3;
	s_glyphs.glyphs[2].width = 16;
	s_glyphs.glyphs[2].height = 12;
	for (uint32_t t = 0; t < 12u; t++)
	{
		for (uint32_t s = 0; s < 16u; s++)
		{
			PutTexel(TPAGE4, s_glyphs.glyphs[2].u + s, s_glyphs.glyphs[2].v + t, (s < 4u) ? 1u : 3u);
		}
	}
	CHECK(CheckGlyphLayout("W.W", 400, 400, 8, 36, 12) == 0); /* min(320 / 36, 10) = 8 */
	/* A glyph narrower than the pen: the extent is the pen (13 + 13 = 26). */
	BuildTable("WW");
	CHECK(CheckGlyphLayout("WW", 400, 400, 10, 26, 8) == 0); /* min(320 / 26, 10) = 10 */
	/* One run per colour: a row of 16 texels, 4 white then 12 green, is 2
	 * rectangles. */
	BuildTable("W");
	s_glyphs.glyphs[0].width = 16;
	s_glyphs.glyphs[0].height = 1;
	for (uint32_t s = 0; s < 16u; s++)
	{
		PutTexel(TPAGE4, s_glyphs.glyphs[0].u + s, s_glyphs.glyphs[0].v, (s < 4u) ? 1u : 3u);
	}
	CHECK(NativeHoldBanner_GlyphLayout("W", &s_glyphs, s_vram, NULL, NULL, 400, 400, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(s_glyphLayout.rectCount == 2u);
	CHECK(s_glyphLayout.rects[0].w == 4 * s_glyphLayout.scale && s_glyphLayout.rects[0].color == 0xFFFFFFu);
	CHECK(s_glyphLayout.rects[1].w == 12 * s_glyphLayout.scale && s_glyphLayout.rects[1].color == 0x00FF00u);
	CHECK(s_glyphLayout.rects[1].x == s_glyphLayout.rects[0].x + s_glyphLayout.rects[0].w);
	/* The text colour modulates: 0x404040 halves white. */
	s_glyphs.color = 0x404040u;
	CHECK(NativeHoldBanner_GlyphLayout("W", &s_glyphs, s_vram, NULL, NULL, 400, 400, &s_glyphLayout) == NATIVE_HOLD_BANNER_FONT_GAME);
	CHECK(s_glyphLayout.rects[0].color == 0x7B7B7Bu && s_glyphLayout.rects[1].color == 0x007B00u);
	/* An 8-bit font lays out the same way. */
	memset(s_vram, 0, sizeof(s_vram));
	memset(&s_glyphs, 0, sizeof(s_glyphs));
	s_glyphs.count = 2u;
	s_glyphs.color = WHITE_SHADE;
	PutClut(CLUT8, 0x80u, 0x7C00u);
	for (uint32_t i = 0; i < 2u; i++)
	{
		struct NativeHoldBannerGlyph *glyph = &s_glyphs.glyphs[i];

		glyph->kind = NATIVE_HOLD_BANNER_GLYPH_ICON;
		glyph->tpage = TPAGE8;
		glyph->clut = CLUT8;
		glyph->u = (uint8_t)(17u + (i * 20u));
		glyph->v = 40u;
		glyph->width = 5;
		glyph->height = 5;
		glyph->advance = 6;
		for (uint32_t t = 0; t < 5u; t++)
		{
			PutTexel(TPAGE8, glyph->u + t, glyph->v + t, 0x80u); /* a diagonal */
		}
	}
	CHECK(CheckGlyphLayout("AB", 400, 400, 10, 12, 5) == 0); /* extent: the pen, 6 + 6 = 12; min(320 / 12, 10) = 10 */
	CHECK(s_glyphLayout.rectCount == 10u && s_glyphLayout.rects[0].color == 0xFF0000u);
	return 0;
}

int main(void)
{
	CHECK(TestGlyphs() == 0);
	CHECK(TestRejects() == 0);
	CHECK(TestLayouts() == 0);
	CHECK(TestModulate() == 0);
	CHECK(TestTexels() == 0);
	CHECK(TestGlyphSource() == 0);
	CHECK(TestGlyphFallbacks() == 0);
	CHECK(TestGlyphLayouts() == 0);
	puts("native_hold_banner_test: ok");
	return 0;
}
