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
 * Two fonts (LR-S11, LR-72). The game font: the glyphs of the arcade-link
 * layout's banner font (FONT_SMALL), decoded read-only from the VRAM mirror
 * through a pointer-free glyph table the race caller reads from the game's
 * icon data (NativeHoldBanner_GlyphLayout, below). The retail font path
 * itself (DecalFont) draws through the game's ordering table and primitive
 * memory, which the hold must not touch (the LR-S2 (a) finding), so only
 * its texels are borrowed. The fallback, and the roster proof's banner: a
 * built-in 5x7 block font of A..Z (every other character is a blank cell),
 * 6 columns per character (NativeHoldBanner_Layout).
 *
 * Geometry, in pixels of the presentation viewport, origin top-left: the
 * text is scaled by an integer `scale` (at least 1), the largest that keeps
 * the text within 4/5 of the viewport width and at most viewportH / 40; the
 * bar spans the viewport width, is (line height + 6) * scale high (the
 * glyph rows, 7 for the block font, and 3 rows of margin above and below),
 * and is centred vertically; the text is centred in it. Each glyph row
 * becomes one rectangle per run of set pixels (for the game font, per run
 * of one colour).
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

/*
 * The game font (LR-S11, LR-72).
 *
 * The glyph table is pointer-free: one entry per character of the banner
 * text, filled by the race caller (game/MAIN/MainArcadeRaceLaunch.c) with a
 * read-only lookup of the icon DecalFont_DrawLineStrlen would draw
 * (data.font_characterIconID, the font's icon group, ICONGROUP_GETICONS),
 * and passed through the hold to Platform_PresentVRAMDisplayBannerGlyphs.
 * An entry is the icon's texture page and CLUT words, its top-left texel
 * (u, v), its size as DecalHUD_DrawPolyGT4 draws it (width u1 - u0, height
 * v2 - v0), and the pen advance DecalFont uses for the character.
 *
 * NativeHoldBanner_GlyphLayout validates the whole table against a VRAM
 * image (NATIVE_HOLD_BANNER_VRAM_WIDTH x HEIGHT 16-bit words, read only)
 * and lays it out, or refuses with the first reason it finds; the renderer
 * then draws the block font instead. The rules:
 *
 *   - the table must match the text (count == strlen(text), 1..MAX_CHARS):
 *     otherwise TEXT;
 *   - a character is an ICON (drawn), or BLANK, which only a space may be
 *     (DecalFont draws no icon for it and advances); anything else (no icon
 *     group, no icon for the character, an icon ID outside the group) is
 *     NO_ICON;
 *   - the texture page's colour depth must be 4-bit or 8-bit CLUT (tpage
 *     bits 7-8 = 0 or 1); 15-bit direct and the reserved mode are DEPTH;
 *   - width and height 1..GLYPH_MAX_SIZE, u + width and v + height within
 *     the 256-texel page, the advance 0..2 * GLYPH_MAX_SIZE, the texels and
 *     the CLUT (16 or 256 entries, on a row below 512: CLUT word bit 15
 *     clear) inside VRAM:
 *     otherwise BOUNDS;
 *   - the resident callback (when not NULL) must accept the texels' and the
 *     CLUT's VRAM rectangles, and vram must not be NULL: otherwise
 *     NOT_RESIDENT (the renderer refuses a rectangle whose mirror is older
 *     than the GPU copy: reading it would need a readback);
 *   - every ICON glyph must have at least one opaque texel, and the text at
 *     least one ICON: otherwise TRANSPARENT;
 *   - at most MAX_GLYPH_RECTS rectangles (RECTS), and a viewport that fits
 *     the text at scale 1 (VIEWPORT).
 *
 * Texels: a 4-bit page holds 4 texels per VRAM word, the lowest nibble
 * first; an 8-bit page 2, the low byte first; page x = (tpage & 0xF) * 64,
 * page y = 256 when tpage bit 4 is set; CLUT x = (clut & 0x3F) * 16,
 * CLUT y = clut >> 6 (platform/native_gpu.c GET_CLUT_X, GET_CLUT_Y). A CLUT
 * entry of 0x0000 is transparent (the PS1 rule; the glyphs are drawn
 * opaque, as DecalFont draws them). Every other entry is modulated by the
 * text colour as the PS1 does a textured, shaded primitive: each 5-bit
 * channel times the colour's 8-bit channel, over 128, clamped to 31, then
 * widened to 8 bits ((c << 3) | (c >> 2)).
 *
 * Layout: the pen starts at 0 and moves by each character's advance; glyph
 * i's texel (s, t) is at pen_i + s, t (top-aligned: DecalFont draws the
 * font's letters with no extra offset). The text extent is the largest of
 * every pen and every pen_i + width_i; the line height is the tallest ICON
 * glyph. Scale, bar, and centring are the block font's rules with the
 * extent as the width and the line height as the glyph rows.
 */

#define NATIVE_HOLD_BANNER_VRAM_WIDTH 1024
#define NATIVE_HOLD_BANNER_VRAM_HEIGHT 512
#define NATIVE_HOLD_BANNER_GLYPH_MAX_SIZE 32
#define NATIVE_HOLD_BANNER_MAX_GLYPH_RECTS 4096

/* NativeHoldBannerGlyph.kind. */
#define NATIVE_HOLD_BANNER_GLYPH_MISSING 0u
#define NATIVE_HOLD_BANNER_GLYPH_ICON 1u
#define NATIVE_HOLD_BANNER_GLYPH_BLANK 2u

/* The font a banner was drawn in: GAME, or the reason for the block font. */
#define NATIVE_HOLD_BANNER_FONT_GAME 0u
#define NATIVE_HOLD_BANNER_FONT_NO_TABLE 1u     /* no glyph table (the roster proof's hold) */
#define NATIVE_HOLD_BANNER_FONT_TEXT 2u         /* the table does not match the text */
#define NATIVE_HOLD_BANNER_FONT_NO_ICON 3u      /* a character with no icon */
#define NATIVE_HOLD_BANNER_FONT_DEPTH 4u        /* a colour depth not decoded */
#define NATIVE_HOLD_BANNER_FONT_BOUNDS 5u       /* a size, advance, page, texel, or CLUT outside its range */
#define NATIVE_HOLD_BANNER_FONT_NOT_RESIDENT 6u /* the mirror does not hold the texels without a readback */
#define NATIVE_HOLD_BANNER_FONT_TRANSPARENT 7u  /* a glyph, or the text, with no opaque texel */
#define NATIVE_HOLD_BANNER_FONT_VIEWPORT 8u     /* the viewport is too small */
#define NATIVE_HOLD_BANNER_FONT_RECTS 9u        /* more than MAX_GLYPH_RECTS rectangles */

struct NativeHoldBannerGlyph
{
	uint16_t tpage;  /* the icon's texture page word */
	uint16_t clut;   /* the icon's CLUT word */
	uint8_t u;       /* top-left texel */
	uint8_t v;
	uint8_t kind;    /* NATIVE_HOLD_BANNER_GLYPH_* */
	uint8_t reserved;
	int16_t width;   /* u1 - u0 */
	int16_t height;  /* v2 - v0 */
	int16_t advance; /* the pen advance DecalFont uses for the character */
	int16_t reserved2;
};

struct NativeHoldBannerGlyphs
{
	uint32_t count; /* entries used: strlen of the banner text */
	uint32_t color; /* the text colour, 0x00BBGGRR (DecalFont's vertex colour; 0x80 leaves a channel as is) */
	struct NativeHoldBannerGlyph glyphs[NATIVE_HOLD_BANNER_MAX_CHARS];
};

/* A game-font rectangle: color is 0x00BBGGRR, 8 bits per channel. */
struct NativeHoldBannerColorRect
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	uint32_t color;
};

struct NativeHoldBannerGlyphLayout
{
	int32_t scale;
	uint32_t rectCount;
	struct NativeHoldBannerRect bar;
	struct NativeHoldBannerColorRect rects[NATIVE_HOLD_BANNER_MAX_GLYPH_RECTS];
};

/* Whether VRAM rectangle (x, y, w, h) may be read now; nonzero: yes. */
typedef int (*NativeHoldBannerResidentFn)(void *context, int32_t x, int32_t y, int32_t w, int32_t h);

/*
 * Checks glyph, the table's entry for character c, by the kind, depth, and
 * bounds rules above. Returns NATIVE_HOLD_BANNER_FONT_GAME when it may be
 * drawn, with *texels and *clut (when not NULL) set to the VRAM rectangles
 * an ICON reads (all zero for a BLANK), else the reason (NO_ICON for a NULL
 * glyph), with *texels and *clut untouched.
 */
uint32_t NativeHoldBanner_GlyphSource(const struct NativeHoldBannerGlyph *glyph, char c, struct NativeHoldBannerRect *texels,
                                      struct NativeHoldBannerRect *clut);

/*
 * Texel (s, t) of an ICON glyph that NativeHoldBanner_GlyphSource accepted
 * (s < width, t < height): its CLUT entry into *entry. Returns 1 when
 * opaque, 0 when transparent (entry 0x0000) or for arguments out of range
 * (a NULL argument, s or t outside the glyph, or a glyph GlyphSource
 * refuses), with *entry untouched then.
 */
int NativeHoldBanner_GlyphTexel(const struct NativeHoldBannerGlyph *glyph, const uint16_t *vram, uint32_t s, uint32_t t, uint16_t *entry);

/* A 15-bit CLUT entry modulated by the 0x00BBGGRR colour, as 8-bit 0x00BBGGRR. */
uint32_t NativeHoldBanner_Modulate(uint16_t entry, uint32_t color);

/*
 * Lays out text in the game font from glyphs over vram (read only; the
 * resident callback is asked before any read) for a viewport of the given
 * size. Returns NATIVE_HOLD_BANNER_FONT_GAME with *layout filled, or the
 * first refusal reason with *layout untouched (a NULL glyphs is NO_TABLE;
 * a NULL text or layout is TEXT).
 */
uint32_t NativeHoldBanner_GlyphLayout(const char *text, const struct NativeHoldBannerGlyphs *glyphs, const uint16_t *vram,
                                      NativeHoldBannerResidentFn resident, void *residentContext, int32_t viewportW, int32_t viewportH,
                                      struct NativeHoldBannerGlyphLayout *layout);

/* "game font", or "block font (<reason>)"; "block font (unknown)" for any other value. */
const char *NativeHoldBanner_FontName(uint32_t font);

#endif
