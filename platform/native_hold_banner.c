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
