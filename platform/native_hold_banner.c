#include "platform/native_hold_banner.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Hold banner layout (docs/LOCKSTEP_RACE_MILESTONE.md LR-9, slice LR-S2
 * (a)). See include/platform/native_hold_banner.h.
 */

/* The 5x7 glyphs of A..Z, one byte per row, the leftmost pixel in bit 4. */
static const uint8_t k_nativeHoldBannerGlyphs[26][NATIVE_HOLD_BANNER_GLYPH_HEIGHT] = {
	{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
	{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
	{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
	{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
	{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
	{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
	{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
	{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
	{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
	{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
	{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
	{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
	{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
	{0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
	{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
	{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
	{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
	{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
	{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
	{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
	{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
	{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
	{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
	{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
};

uint8_t NativeHoldBanner_GlyphRow(char c, uint32_t row)
{
	if ((c < 'A') || (c > 'Z') || (row >= (uint32_t)NATIVE_HOLD_BANNER_GLYPH_HEIGHT))
	{
		return 0u;
	}
	return k_nativeHoldBannerGlyphs[c - 'A'][row];
}

int NativeHoldBanner_Layout(const char *text, int32_t viewportW, int32_t viewportH, struct NativeHoldBannerLayout *layout)
{
	size_t length;
	int32_t units;
	int32_t scale;
	int32_t barH;
	int32_t originX;
	int32_t originY;

	if ((text == NULL) || (layout == NULL))
	{
		return 0;
	}
	length = strlen(text);
	if ((length == 0u) || (length > (size_t)NATIVE_HOLD_BANNER_MAX_CHARS))
	{
		return 0;
	}
	units = ((int32_t)length * NATIVE_HOLD_BANNER_ADVANCE) - (NATIVE_HOLD_BANNER_ADVANCE - NATIVE_HOLD_BANNER_GLYPH_WIDTH);
	barH = NATIVE_HOLD_BANNER_GLYPH_HEIGHT + (2 * NATIVE_HOLD_BANNER_MARGIN);
	if ((viewportW < units) || (viewportH < barH))
	{
		return 0;
	}
	scale = ((viewportW * 4) / 5) / units;
	if (scale > (viewportH / 40))
	{
		scale = viewportH / 40;
	}
	if (scale < 1)
	{
		scale = 1;
	}

	/* Every check that can fail is above: from here *layout is filled in
	 * place (no ~11 KB working copy on the stack). */
	memset(layout, 0, sizeof(*layout));
	layout->scale = scale;
	layout->bar.x = 0;
	layout->bar.w = viewportW;
	layout->bar.h = barH * scale;
	layout->bar.y = (viewportH - layout->bar.h) / 2;
	originX = (viewportW - (units * scale)) / 2;
	originY = layout->bar.y + (NATIVE_HOLD_BANNER_MARGIN * scale);
	for (size_t i = 0; i < length; i++)
	{
		const int32_t glyphX = originX + ((int32_t)i * NATIVE_HOLD_BANNER_ADVANCE * scale);

		for (uint32_t row = 0; row < (uint32_t)NATIVE_HOLD_BANNER_GLYPH_HEIGHT; row++)
		{
			const uint8_t bits = NativeHoldBanner_GlyphRow(text[i], row);
			int32_t column = 0;

			while (column < NATIVE_HOLD_BANNER_GLYPH_WIDTH)
			{
				int32_t run = 0;
				struct NativeHoldBannerRect *rect;

				if ((bits & (0x10u >> column)) == 0u)
				{
					column++;
					continue;
				}
				while (((column + run) < NATIVE_HOLD_BANNER_GLYPH_WIDTH) && ((bits & (0x10u >> (column + run))) != 0u))
				{
					run++;
				}
				rect = &layout->text[layout->textRectCount++];
				rect->x = glyphX + (column * scale);
				rect->y = originY + ((int32_t)row * scale);
				rect->w = run * scale;
				rect->h = scale;
				column += run;
			}
		}
	}
	return 1;
}

/* The game font (LR-S11, LR-72). See include/platform/native_hold_banner.h. */

uint32_t NativeHoldBanner_GlyphSource(const struct NativeHoldBannerGlyph *glyph, char c, struct NativeHoldBannerRect *texels,
                                      struct NativeHoldBannerRect *clut)
{
	uint32_t depth;
	uint32_t shift;
	int32_t pageX;
	int32_t pageY;
	struct NativeHoldBannerRect texelRect;
	struct NativeHoldBannerRect clutRect;

	if (glyph == NULL)
	{
		return NATIVE_HOLD_BANNER_FONT_NO_ICON;
	}
	if (glyph->kind == NATIVE_HOLD_BANNER_GLYPH_BLANK)
	{
		/* DecalFont draws no icon for a space; nothing else may lack one. */
		if (c != ' ')
		{
			return NATIVE_HOLD_BANNER_FONT_NO_ICON;
		}
		if ((glyph->advance < 0) || (glyph->advance > (2 * NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE)))
		{
			return NATIVE_HOLD_BANNER_FONT_BOUNDS;
		}
		if (texels != NULL)
		{
			memset(texels, 0, sizeof(*texels));
		}
		if (clut != NULL)
		{
			memset(clut, 0, sizeof(*clut));
		}
		return NATIVE_HOLD_BANNER_FONT_GAME;
	}
	if (glyph->kind != NATIVE_HOLD_BANNER_GLYPH_ICON)
	{
		return NATIVE_HOLD_BANNER_FONT_NO_ICON;
	}
	depth = ((uint32_t)glyph->tpage >> 7) & 3u;
	if (depth > 1u)
	{
		return NATIVE_HOLD_BANNER_FONT_DEPTH;
	}
	/* 4-bit: 4 texels per VRAM word; 8-bit: 2. */
	shift = (depth == 0u) ? 2u : 1u;
	if ((glyph->width < 1) || (glyph->width > NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE) || (glyph->height < 1) ||
	    (glyph->height > NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE) || (((int32_t)glyph->u + glyph->width) > 256) ||
	    (((int32_t)glyph->v + glyph->height) > 256) || (glyph->advance < 0) || (glyph->advance > (2 * NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE)))
	{
		return NATIVE_HOLD_BANNER_FONT_BOUNDS;
	}
	pageX = (int32_t)(glyph->tpage & 0xFu) * 64;
	pageY = ((glyph->tpage & 0x10u) != 0u) ? 256 : 0;
	texelRect.x = pageX + ((int32_t)glyph->u >> shift);
	texelRect.y = pageY + (int32_t)glyph->v;
	texelRect.w = (pageX + ((((int32_t)glyph->u + glyph->width) - 1) >> shift)) - texelRect.x + 1;
	texelRect.h = glyph->height;
	clutRect.x = (int32_t)(glyph->clut & 0x3Fu) * 16;
	/* Bits 6-15: a CLUT word with bit 15 set names a row of 512 or more,
	 * which the VRAM bound below refuses. */
	clutRect.y = (int32_t)(glyph->clut >> 6);
	clutRect.w = (depth == 0u) ? 16 : 256;
	clutRect.h = 1;
	if (((texelRect.x + texelRect.w) > NATIVE_HOLD_BANNER_VRAM_WIDTH) || ((texelRect.y + texelRect.h) > NATIVE_HOLD_BANNER_VRAM_HEIGHT) ||
	    ((clutRect.x + clutRect.w) > NATIVE_HOLD_BANNER_VRAM_WIDTH) || (clutRect.y >= NATIVE_HOLD_BANNER_VRAM_HEIGHT))
	{
		return NATIVE_HOLD_BANNER_FONT_BOUNDS;
	}
	if (texels != NULL)
	{
		*texels = texelRect;
	}
	if (clut != NULL)
	{
		*clut = clutRect;
	}
	return NATIVE_HOLD_BANNER_FONT_GAME;
}

int NativeHoldBanner_GlyphTexel(const struct NativeHoldBannerGlyph *glyph, const uint16_t *vram, uint32_t s, uint32_t t, uint16_t *entry)
{
	struct NativeHoldBannerRect texels;
	struct NativeHoldBannerRect clut;
	uint32_t u;
	uint32_t word;
	uint32_t index;
	size_t row;

	if ((glyph == NULL) || (vram == NULL) || (entry == NULL) || (glyph->kind != NATIVE_HOLD_BANNER_GLYPH_ICON) ||
	    (NativeHoldBanner_GlyphSource(glyph, 'A', &texels, &clut) != NATIVE_HOLD_BANNER_FONT_GAME) || (s >= (uint32_t)glyph->width) ||
	    (t >= (uint32_t)glyph->height))
	{
		return 0;
	}
	u = (uint32_t)glyph->u + s;
	row = ((size_t)texels.y + (size_t)t) * (size_t)NATIVE_HOLD_BANNER_VRAM_WIDTH;
	if (clut.w == 16)
	{
		/* 4-bit: the lowest nibble is the leftmost texel. */
		word = vram[row + (size_t)texels.x + (size_t)((u >> 2) - ((uint32_t)glyph->u >> 2))];
		index = (word >> ((u & 3u) * 4u)) & 0xFu;
	}
	else
	{
		/* 8-bit: the low byte is the leftmost texel. */
		word = vram[row + (size_t)texels.x + (size_t)((u >> 1) - ((uint32_t)glyph->u >> 1))];
		index = (word >> ((u & 1u) * 8u)) & 0xFFu;
	}
	*entry = vram[((size_t)clut.y * (size_t)NATIVE_HOLD_BANNER_VRAM_WIDTH) + (size_t)clut.x + (size_t)index];
	return (*entry != 0u) ? 1 : 0;
}

uint32_t NativeHoldBanner_Modulate(uint16_t entry, uint32_t color)
{
	uint32_t out = 0u;

	for (uint32_t channel = 0; channel < 3u; channel++)
	{
		const uint32_t texel = ((uint32_t)entry >> (channel * 5u)) & 0x1Fu;
		const uint32_t shade = (color >> (channel * 8u)) & 0xFFu;
		uint32_t value = (texel * shade) >> 7;

		if (value > 31u)
		{
			value = 31u;
		}
		out |= ((value << 3) | (value >> 2)) << (channel * 8u);
	}
	return out;
}

/* One pass over the table of the text: with no layout it validates and
 * measures (the extent, the line height, the rectangle count); with a
 * layout it also emits the rectangles (the table was validated already). */
struct NativeHoldBannerGlyphPass
{
	int32_t originX;
	int32_t originY;
	int32_t scale;
	int32_t extent;
	int32_t line;
	uint32_t rects;
};

static uint32_t NativeHoldBanner_GlyphPass(const char *text, const struct NativeHoldBannerGlyphs *glyphs, const uint16_t *vram,
                                           NativeHoldBannerResidentFn resident, void *residentContext, struct NativeHoldBannerGlyphPass *pass,
                                           struct NativeHoldBannerGlyphLayout *layout)
{
	int32_t pen = 0;
	uint32_t icons = 0u;

	pass->extent = 0;
	pass->line = 0;
	pass->rects = 0u;
	for (uint32_t i = 0; i < glyphs->count; i++)
	{
		const struct NativeHoldBannerGlyph *glyph = &glyphs->glyphs[i];
		struct NativeHoldBannerRect texels;
		struct NativeHoldBannerRect clut;
		const uint32_t status = NativeHoldBanner_GlyphSource(glyph, text[i], &texels, &clut);
		uint32_t opaque = 0u;

		if (status != NATIVE_HOLD_BANNER_FONT_GAME)
		{
			return status;
		}
		if (glyph->kind == NATIVE_HOLD_BANNER_GLYPH_ICON)
		{
			/* Ask before any read. */
			if ((vram == NULL) || ((resident != NULL) && ((resident(residentContext, texels.x, texels.y, texels.w, texels.h) == 0) ||
			                                               (resident(residentContext, clut.x, clut.y, clut.w, clut.h) == 0))))
			{
				return NATIVE_HOLD_BANNER_FONT_NOT_RESIDENT;
			}
			for (uint32_t t = 0; t < (uint32_t)glyph->height; t++)
			{
				uint32_t s = 0u;

				while (s < (uint32_t)glyph->width)
				{
					uint16_t entry = 0u;
					uint32_t color;
					uint32_t run = 1u;

					if (NativeHoldBanner_GlyphTexel(glyph, vram, s, t, &entry) == 0)
					{
						s++;
						continue;
					}
					color = NativeHoldBanner_Modulate(entry, glyphs->color);
					for (;;)
					{
						uint16_t next = 0u;

						if (((s + run) >= (uint32_t)glyph->width) || (NativeHoldBanner_GlyphTexel(glyph, vram, s + run, t, &next) == 0) ||
						    (NativeHoldBanner_Modulate(next, glyphs->color) != color))
						{
							break;
						}
						run++;
					}
					if ((layout != NULL) && (layout->rectCount < (uint32_t)NATIVE_HOLD_BANNER_MAX_GLYPH_RECTS))
					{
						struct NativeHoldBannerColorRect *rect = &layout->rects[layout->rectCount++];

						rect->x = pass->originX + ((pen + (int32_t)s) * pass->scale);
						rect->y = pass->originY + ((int32_t)t * pass->scale);
						rect->w = (int32_t)run * pass->scale;
						rect->h = pass->scale;
						rect->color = color;
					}
					opaque += run;
					pass->rects++;
					s += run;
				}
			}
			if (opaque == 0u)
			{
				return NATIVE_HOLD_BANNER_FONT_TRANSPARENT;
			}
			icons++;
			if (glyph->height > pass->line)
			{
				pass->line = glyph->height;
			}
			if ((pen + glyph->width) > pass->extent)
			{
				pass->extent = pen + glyph->width;
			}
		}
		pen += glyph->advance;
		if (pen > pass->extent)
		{
			pass->extent = pen;
		}
	}
	return (icons == 0u) ? NATIVE_HOLD_BANNER_FONT_TRANSPARENT : NATIVE_HOLD_BANNER_FONT_GAME;
}

uint32_t NativeHoldBanner_GlyphLayout(const char *text, const struct NativeHoldBannerGlyphs *glyphs, const uint16_t *vram,
                                      NativeHoldBannerResidentFn resident, void *residentContext, int32_t viewportW, int32_t viewportH,
                                      struct NativeHoldBannerGlyphLayout *layout)
{
	struct NativeHoldBannerGlyphPass pass;
	size_t length;
	uint32_t status;
	int32_t scale;
	int32_t barH;

	if (glyphs == NULL)
	{
		return NATIVE_HOLD_BANNER_FONT_NO_TABLE;
	}
	if ((text == NULL) || (layout == NULL))
	{
		return NATIVE_HOLD_BANNER_FONT_TEXT;
	}
	length = strlen(text);
	if ((length == 0u) || (length > (size_t)NATIVE_HOLD_BANNER_MAX_CHARS) || ((size_t)glyphs->count != length))
	{
		return NATIVE_HOLD_BANNER_FONT_TEXT;
	}
	/* Validate and measure first: after a refusal *layout is untouched. */
	memset(&pass, 0, sizeof(pass));
	pass.scale = 1;
	status = NativeHoldBanner_GlyphPass(text, glyphs, vram, resident, residentContext, &pass, NULL);
	if (status != NATIVE_HOLD_BANNER_FONT_GAME)
	{
		return status;
	}
	if (pass.rects > (uint32_t)NATIVE_HOLD_BANNER_MAX_GLYPH_RECTS)
	{
		return NATIVE_HOLD_BANNER_FONT_RECTS;
	}
	barH = pass.line + (2 * NATIVE_HOLD_BANNER_MARGIN);
	if ((viewportW < pass.extent) || (viewportH < barH))
	{
		return NATIVE_HOLD_BANNER_FONT_VIEWPORT;
	}
	scale = ((viewportW * 4) / 5) / pass.extent;
	if (scale > (viewportH / 40))
	{
		scale = viewportH / 40;
	}
	if (scale < 1)
	{
		scale = 1;
	}

	memset(layout, 0, sizeof(*layout));
	layout->scale = scale;
	layout->bar.x = 0;
	layout->bar.w = viewportW;
	layout->bar.h = barH * scale;
	layout->bar.y = (viewportH - layout->bar.h) / 2;
	pass.originX = (viewportW - (pass.extent * scale)) / 2;
	pass.originY = layout->bar.y + (NATIVE_HOLD_BANNER_MARGIN * scale);
	pass.scale = scale;
	(void)NativeHoldBanner_GlyphPass(text, glyphs, vram, resident, residentContext, &pass, layout);
	return NATIVE_HOLD_BANNER_FONT_GAME;
}

const char *NativeHoldBanner_FontName(uint32_t font)
{
	switch (font)
	{
	case NATIVE_HOLD_BANNER_FONT_GAME:
		return "game font";
	case NATIVE_HOLD_BANNER_FONT_NO_TABLE:
		return "block font (no glyph table)";
	case NATIVE_HOLD_BANNER_FONT_TEXT:
		return "block font (the glyph table does not match the text)";
	case NATIVE_HOLD_BANNER_FONT_NO_ICON:
		return "block font (a character with no icon)";
	case NATIVE_HOLD_BANNER_FONT_DEPTH:
		return "block font (a colour depth not decoded)";
	case NATIVE_HOLD_BANNER_FONT_BOUNDS:
		return "block font (a glyph out of bounds)";
	case NATIVE_HOLD_BANNER_FONT_NOT_RESIDENT:
		return "block font (the glyphs are not current in the VRAM mirror)";
	case NATIVE_HOLD_BANNER_FONT_TRANSPARENT:
		return "block font (a glyph with no opaque texel)";
	case NATIVE_HOLD_BANNER_FONT_VIEWPORT:
		return "block font (the viewport is too small)";
	case NATIVE_HOLD_BANNER_FONT_RECTS:
		return "block font (too many glyph rectangles)";
	default:
		return "block font (unknown)";
	}
}
