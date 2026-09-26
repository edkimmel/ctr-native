#include "platform/native_capture_check.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* BMP parsing                                                              */
/* ------------------------------------------------------------------------ */

#define CAPTURE_BMP_FILE_HEADER_SIZE 14u
#define CAPTURE_BMP_INFO_HEADER_MIN  40u
#define CAPTURE_BMP_BI_RGB           0u
#define CAPTURE_BMP_BI_BITFIELDS     3u
#define CAPTURE_BMP_MAX_DIMENSION    16384u

static uint32_t CaptureCheck_Read16(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t CaptureCheck_Read32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A usable channel mask is non-zero, one contiguous run of at most 16 bits. */
static int CaptureCheck_MaskLayout(uint32_t mask, uint32_t *shift, uint32_t *bits)
{
	uint32_t s = 0u;
	uint32_t n = 0u;

	if (mask == 0u)
		return 0;
	while (((mask >> s) & 1u) == 0u)
	{
		s++;
	}
	while ((s + n) < 32u && ((mask >> (s + n)) & 1u) != 0u)
	{
		n++;
	}
	if ((s + n) < 32u && (mask >> (s + n)) != 0u)
		return 0;
	if (n > 16u)
		return 0;
	*shift = s;
	*bits = n;
	return 1;
}

enum NativeCaptureBmpStatus NativeCaptureCheck_ParseBmp(const uint8_t *data, size_t size, struct NativeCaptureImage *out)
{
	struct NativeCaptureImage image;
	uint32_t pixelOffset;
	uint32_t infoSize;
	uint32_t rawWidth;
	uint32_t rawHeight;
	uint32_t planes;
	uint32_t bpp;
	uint32_t compression;
	uint32_t maskOffset;
	uint32_t height;
	uint32_t i;
	size_t stride;
	size_t pixelBytes;

	if ((data == NULL) || (out == NULL))
		return NATIVE_CAPTURE_BMP_ERR_ARGUMENT;
	if (size < (size_t)(CAPTURE_BMP_FILE_HEADER_SIZE + CAPTURE_BMP_INFO_HEADER_MIN))
		return NATIVE_CAPTURE_BMP_ERR_TRUNCATED;
	if ((data[0] != (uint8_t)'B') || (data[1] != (uint8_t)'M'))
		return NATIVE_CAPTURE_BMP_ERR_SIGNATURE;

	pixelOffset = CaptureCheck_Read32(data + 10);
	infoSize = CaptureCheck_Read32(data + 14);
	if (infoSize < CAPTURE_BMP_INFO_HEADER_MIN)
		return NATIVE_CAPTURE_BMP_ERR_HEADER;
	if ((size_t)infoSize > size - CAPTURE_BMP_FILE_HEADER_SIZE)
		return NATIVE_CAPTURE_BMP_ERR_TRUNCATED;

	rawWidth = CaptureCheck_Read32(data + 18);
	rawHeight = CaptureCheck_Read32(data + 22);
	planes = CaptureCheck_Read16(data + 26);
	bpp = CaptureCheck_Read16(data + 28);
	compression = CaptureCheck_Read32(data + 30);
	if (planes != 1u)
		return NATIVE_CAPTURE_BMP_ERR_HEADER;

	/* Width is a signed field that must be positive; height is signed and
	 * negative for top-down storage.  INT32_MIN has no magnitude. */
	if ((rawWidth == 0u) || (rawWidth > CAPTURE_BMP_MAX_DIMENSION))
		return NATIVE_CAPTURE_BMP_ERR_DIMENSIONS;
	if ((rawHeight & 0x80000000u) != 0u)
	{
		if (rawHeight == 0x80000000u)
			return NATIVE_CAPTURE_BMP_ERR_DIMENSIONS;
		height = (~rawHeight) + 1u;
		image.topDown = 1u;
	}
	else
	{
		height = rawHeight;
		image.topDown = 0u;
	}
	if ((height == 0u) || (height > CAPTURE_BMP_MAX_DIMENSION))
		return NATIVE_CAPTURE_BMP_ERR_DIMENSIONS;

	if ((bpp != 24u) && (bpp != 32u))
		return NATIVE_CAPTURE_BMP_ERR_FORMAT;
	if (compression == CAPTURE_BMP_BI_RGB)
	{
		image.mask[0] = 0x00ff0000u;
		image.mask[1] = 0x0000ff00u;
		image.mask[2] = 0x000000ffu;
	}
	else if ((compression == CAPTURE_BMP_BI_BITFIELDS) && (bpp == 32u))
	{
		/* The R, G, B masks sit directly after the 40-byte core of the info
		 * header, either as its V2+ extension or as a trailing table. */
		maskOffset = CAPTURE_BMP_FILE_HEADER_SIZE + CAPTURE_BMP_INFO_HEADER_MIN;
		if ((size_t)maskOffset + 12u > size)
			return NATIVE_CAPTURE_BMP_ERR_TRUNCATED;
		if ((infoSize == CAPTURE_BMP_INFO_HEADER_MIN) && (pixelOffset < maskOffset + 12u))
			return NATIVE_CAPTURE_BMP_ERR_HEADER;
		for (i = 0u; i < 3u; i++)
		{
			image.mask[i] = CaptureCheck_Read32(data + maskOffset + 4u * i);
		}
	}
	else
	{
		return NATIVE_CAPTURE_BMP_ERR_FORMAT;
	}
	for (i = 0u; i < 3u; i++)
	{
		if (!CaptureCheck_MaskLayout(image.mask[i], &image.shift[i], &image.bits[i]))
			return NATIVE_CAPTURE_BMP_ERR_MASKS;
		if ((bpp == 24u) && ((image.mask[i] >> 24) != 0u))
			return NATIVE_CAPTURE_BMP_ERR_MASKS;
	}
	if (((image.mask[0] & image.mask[1]) | (image.mask[0] & image.mask[2]) | (image.mask[1] & image.mask[2])) != 0u)
		return NATIVE_CAPTURE_BMP_ERR_MASKS;

	if (pixelOffset < CAPTURE_BMP_FILE_HEADER_SIZE + infoSize)
		return NATIVE_CAPTURE_BMP_ERR_HEADER;
	if ((size_t)pixelOffset > size)
		return NATIVE_CAPTURE_BMP_ERR_TRUNCATED;

	/* Dimensions are capped, so these products cannot overflow size_t. */
	stride = (((size_t)rawWidth * (size_t)bpp + 31u) / 32u) * 4u;
	pixelBytes = stride * (size_t)height;
	if (pixelBytes > size - (size_t)pixelOffset)
		return NATIVE_CAPTURE_BMP_ERR_TRUNCATED;

	image.pixels = data + pixelOffset;
	image.width = rawWidth;
	image.height = height;
	image.bytesPerPixel = bpp / 8u;
	image.stride = stride;
	*out = image;
	return NATIVE_CAPTURE_BMP_OK;
}

const char *NativeCaptureCheck_BmpStatusName(enum NativeCaptureBmpStatus status)
{
	switch (status)
	{
	case NATIVE_CAPTURE_BMP_OK:
		return "ok";
	case NATIVE_CAPTURE_BMP_ERR_ARGUMENT:
		return "bad argument";
	case NATIVE_CAPTURE_BMP_ERR_TRUNCATED:
		return "truncated";
	case NATIVE_CAPTURE_BMP_ERR_SIGNATURE:
		return "not a BMP (signature)";
	case NATIVE_CAPTURE_BMP_ERR_HEADER:
		return "malformed header";
	case NATIVE_CAPTURE_BMP_ERR_DIMENSIONS:
		return "unsupported dimensions";
	case NATIVE_CAPTURE_BMP_ERR_FORMAT:
		return "unsupported format (need uncompressed 24/32 bpp BI_RGB or 32 bpp BI_BITFIELDS)";
	case NATIVE_CAPTURE_BMP_ERR_MASKS:
		return "unsupported channel masks";
	default:
		return "unknown";
	}
}

static uint8_t CaptureCheck_Channel(uint32_t value, uint32_t shift, uint32_t bits)
{
	uint32_t v = value >> shift;
	uint32_t max = (bits >= 32u) ? 0xffffffffu : ((1u << bits) - 1u);

	v &= max;
	if (bits == 8u)
		return (uint8_t)v;
	if (bits > 8u)
		return (uint8_t)(v >> (bits - 8u));
	return (uint8_t)((v * 255u + max / 2u) / max);
}

struct NativeCaptureRgb NativeCaptureCheck_GetRgb(const struct NativeCaptureImage *image, uint32_t x, uint32_t y)
{
	struct NativeCaptureRgb rgb = {0u, 0u, 0u};
	const uint8_t *p;
	uint32_t row;
	uint32_t value;

	if ((image == NULL) || (image->pixels == NULL) || (x >= image->width) || (y >= image->height))
		return rgb;
	row = image->topDown ? y : (image->height - 1u - y);
	p = image->pixels + (size_t)row * image->stride + (size_t)x * image->bytesPerPixel;
	/* Only the colour masks are applied; the fourth byte (alpha / PS1 mask
	 * bit) never contributes. */
	value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
	if (image->bytesPerPixel == 4u)
		value |= (uint32_t)p[3] << 24;
	rgb.r = CaptureCheck_Channel(value & image->mask[0], image->shift[0], image->bits[0]);
	rgb.g = CaptureCheck_Channel(value & image->mask[1], image->shift[1], image->bits[1]);
	rgb.b = CaptureCheck_Channel(value & image->mask[2], image->shift[2], image->bits[2]);
	return rgb;
}

/* ------------------------------------------------------------------------ */
/* Screens                                                                  */
/* ------------------------------------------------------------------------ */

static const char *const CaptureCheck_ScreenNames[NATIVE_CAPTURE_SCREEN_COUNT] = {
    "title",   "lobby", "lobby-connecting",   "lobby-rejected",   "match-found",  "results",     "results-timeout", "results-desync", "results-link-error",
    "rematch", "exit",  "exit-opponent-left", "select-character", "select-track", "select-laps", "select-wait",     "select-result",
    "lobby-solo", "select-solo", "results-solo", "results-solo-error",
};

/* The screens before this one use the fixed 400-wide panel and the six
 * shared text bands; the five match-select screens from here on have their
 * own layout (CaptureCheck_SelectSpecs).  The solo screens after them
 * (CAPTURE_FIRST_SOLO_SCREEN on) are shared-panel screens again, except
 * select-solo, which is a select screen (CaptureCheck_SelectSoloSpec). */
#define CAPTURE_FIRST_SELECT_SCREEN NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER
#define CAPTURE_SELECT_SCREEN_COUNT 5u
#define CAPTURE_FIRST_SOLO_SCREEN   NATIVE_CAPTURE_SCREEN_LOBBY_SOLO
_Static_assert((unsigned)CAPTURE_FIRST_SELECT_SCREEN + CAPTURE_SELECT_SCREEN_COUNT == (unsigned)CAPTURE_FIRST_SOLO_SCREEN,
               "the solo screens follow the five match-select screens");

const char *NativeCaptureCheck_ScreenName(enum NativeCaptureScreen screen)
{
	if ((unsigned)screen >= (unsigned)NATIVE_CAPTURE_SCREEN_COUNT)
		return NULL;
	return CaptureCheck_ScreenNames[screen];
}

int NativeCaptureCheck_ScreenFromName(const char *name, enum NativeCaptureScreen *out)
{
	unsigned i;

	if ((name == NULL) || (out == NULL))
		return 0;
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; i++)
	{
		if (strcmp(name, CaptureCheck_ScreenNames[i]) == 0)
		{
			*out = (enum NativeCaptureScreen)i;
			return 1;
		}
	}
	return 0;
}

/* Per-band expectation for one screen. */
enum CaptureCheckBand
{
	CAPTURE_BAND_ABSENT = 0, /* the screen draws nothing here */
	CAPTURE_BAND_REQUIRED,   /* the screen always draws a line here */
	CAPTURE_BAND_OPTIONAL    /* blinking line: reported, never judged */
};

#define CAPTURE_TEXT_BAND_COUNT 6u

struct CaptureCheckScreenSpec
{
	uint8_t band[CAPTURE_TEXT_BAND_COUNT]; /* title, body1, body2, rematch, exit, footer */
	uint8_t highlight;                     /* 1: REMATCH highlight required, 0: absent */
};

/* Expected lines per screen, from MAIN/MainArcadeLinkLayout.c (the attract,
 * lobby, results and exit builders). */
static const struct CaptureCheckScreenSpec CaptureCheck_Specs[CAPTURE_FIRST_SELECT_SCREEN] = {
    /* title: body1 "PRESS START" blinks */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_OPTIONAL, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT}, 0u},
    /* lobby */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED}, 0u},
    /* lobby-connecting */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED}, 0u},
    /* lobby-rejected */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED}, 0u},
    /* match-found */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT}, 0u},
    /* results */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 1u},
    /* results-timeout */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 1u},
    /* results-desync */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 1u},
    /* results-link-error */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 1u},
    /* rematch */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED}, 0u},
    /* exit */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT}, 0u},
    /* exit-opponent-left */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT}, 0u},
};

/* The shared-panel solo screens (docs/SOLO_CAB_MILESTONE.md SOLO-S3), by
 * screen - CAPTURE_FIRST_SOLO_SCREEN; the select-solo entry is unused (it
 * is a select screen).  lobby-solo draws its FONT_SMALL prompt at y=145,
 * inside the EXIT row band, and nothing on the REMATCH row; results-solo
 * adds the "OTHER CABINET IS READY" notice on body1; results-solo-error has
 * the linked results bands. */
static const struct CaptureCheckScreenSpec CaptureCheck_SoloSpecs[NATIVE_CAPTURE_SCREEN_COUNT - CAPTURE_FIRST_SOLO_SCREEN] = {
    /* lobby-solo */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 0u},
    /* select-solo: unused */
    {{CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT}, 0u},
    /* results-solo */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 1u},
    /* results-solo-error */
    {{CAPTURE_BAND_REQUIRED, CAPTURE_BAND_ABSENT, CAPTURE_BAND_ABSENT, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED, CAPTURE_BAND_REQUIRED}, 1u},
};

/* ------------------------------------------------------------------------ */
/* Geometry (retail 512x216 space)                                          */
/* ------------------------------------------------------------------------ */

#define CAPTURE_RETAIL_W 512u
#define CAPTURE_RETAIL_H 216u

/* Panel: x=56 y=28 w=400 h=176, 3x2 px frame. */
#define CAPTURE_PANEL_X0 56u
#define CAPTURE_PANEL_Y0 28u
#define CAPTURE_PANEL_X1 456u
#define CAPTURE_PANEL_Y1 204u

/* Interior span used for text bands and gap statistics (inside the frame). */
#define CAPTURE_INNER_X0 62u
#define CAPTURE_INNER_X1 450u

/* Text bands [y0, y1): the drawn line plus one retail row of slack. */
static const uint16_t CaptureCheck_BandY[CAPTURE_TEXT_BAND_COUNT][2] = {
    {39u, 58u},   /* title, FONT_BIG at y=40 */
    {89u, 99u},   /* body1, FONT_SMALL at y=90 */
    {109u, 117u}, /* body2, FONT_SMALL at y=110; stops at the highlight top */
    {119u, 138u}, /* REMATCH row, FONT_BIG at y=120 */
    {144u, 163u}, /* EXIT row, FONT_BIG at y=145 */
    {185u, 195u}, /* footer, FONT_SMALL at y=186 */
};

/* Gap rows: panel interior away from every band and the highlight. */
static const uint16_t CaptureCheck_GapY[][2] = {
    {33u, 38u}, {59u, 88u}, {100u, 108u}, {139u, 143u}, {164u, 184u}, {196u, 199u},
};

/* Row highlight on REMATCH: x=136 y=117 w=240 h=21. */
#define CAPTURE_HIGHLIGHT_X0 136u
#define CAPTURE_HIGHLIGHT_Y0 117u
#define CAPTURE_HIGHLIGHT_X1 376u
#define CAPTURE_HIGHLIGHT_Y1 138u

/* A retail rectangle, [x0, x1) x [y0, y1). */
struct CaptureCheckRect
{
	uint16_t x0;
	uint16_t y0;
	uint16_t x1;
	uint16_t y1;
};

/* Select screens (MainArcadeLinkLayout.c, MATCH_SELECT_MILESTONE 2.8): the
 * panel widens to x=4 w=504 at the same y=28 h=176.  The dim check covers
 * the whole widened interior; the detail check keeps the central span every
 * panel shares, so a flat fill there fails on every screen alike.  Each
 * screen's gap rows avoid its text and every highlight rectangle it
 * probes, as the shared gap rows do. */
#define CAPTURE_SELECT_PANEL_X0 4u
#define CAPTURE_SELECT_PANEL_X1 508u
#define CAPTURE_SELECT_INNER_X0 10u
#define CAPTURE_SELECT_INNER_X1 502u

/* Text regions [y0, y1) hold a line plus one retail row of slack, as the
 * shared bands do: FONT_BIG at y is [y-1, y+18), FONT_SMALL [y-1, y+9).
 * Grid columns are 252 wide from x=4; the column spans below cover the
 * centred names (at least 4 FONT_BIG or 9 FONT_SMALL glyphs wide) and
 * stay clear of the P1/P2 markers packed against the cell edges.  Short
 * centred lines (TIME, wait and result lines) use the span 176..336.
 * Blank areas are split into strips at most 24 rows high, so one stray
 * short line is judged against about a line's area, as the shared bands
 * are, rather than diluted below CAPTURE_TEXT_ABSENT_MAX. */
static const struct CaptureCheckRect CaptureCheck_SelTitle[] = {{62u, 39u, 450u, 58u}};    /* FONT_BIG y=40 */
static const struct CaptureCheckRect CaptureCheck_SelTime[] = {{176u, 59u, 336u, 69u}};    /* FONT_SMALL y=60 */
static const struct CaptureCheckRect CaptureCheck_SelFooter[] = {{62u, 185u, 450u, 195u}}; /* FONT_SMALL y=186 */

/* Character: 2 columns x 4 FONT_BIG rows at y = 78 + 24r, centred x=130/382. */
static const struct CaptureCheckRect CaptureCheck_CharCol1[] = {
    {70u, 77u, 190u, 96u}, {70u, 101u, 190u, 120u}, {70u, 125u, 190u, 144u}, {70u, 149u, 190u, 168u}};
static const struct CaptureCheckRect CaptureCheck_CharCol2[] = {
    {322u, 77u, 442u, 96u}, {322u, 101u, 442u, 120u}, {322u, 125u, 442u, 144u}, {322u, 149u, 442u, 168u}};
static const struct CaptureCheckRect CaptureCheck_CharEmpty[] = {{10u, 169u, 502u, 184u}};
static const uint16_t CaptureCheck_CharGapY[][2] = {
    {33u, 38u}, {70u, 74u}, {97u, 101u}, {121u, 125u}, {145u, 149u}, {169u, 184u}, {196u, 199u},
};

/* Track: 2 columns x 8 FONT_SMALL rows at y = 76 + 13r, centred x=130/382. */
static const struct CaptureCheckRect CaptureCheck_TrackCol1[] = {{70u, 75u, 190u, 85u},   {70u, 88u, 190u, 98u},   {70u, 101u, 190u, 111u},
                                                                 {70u, 114u, 190u, 124u}, {70u, 127u, 190u, 137u}, {70u, 140u, 190u, 150u},
                                                                 {70u, 153u, 190u, 163u}, {70u, 166u, 190u, 176u}};
static const struct CaptureCheckRect CaptureCheck_TrackCol2[] = {{322u, 75u, 442u, 85u},   {322u, 88u, 442u, 98u},   {322u, 101u, 442u, 111u},
                                                                 {322u, 114u, 442u, 124u}, {322u, 127u, 442u, 137u}, {322u, 140u, 442u, 150u},
                                                                 {322u, 153u, 442u, 163u}, {322u, 166u, 442u, 176u}};
static const struct CaptureCheckRect CaptureCheck_TrackEmpty[] = {{10u, 177u, 502u, 184u}};
static const uint16_t CaptureCheck_TrackGapY[][2] = {
    {33u, 38u},
    {70u, 73u},
    {177u, 184u},
    {196u, 199u},
};

/* Laps: one column of 3 FONT_BIG rows at y = 86 + 25r, centred x=256 (cell
 * 130..382); nothing is drawn beside the cell. */
static const struct CaptureCheckRect CaptureCheck_LapsCol1[] = {{196u, 85u, 316u, 104u}, {196u, 110u, 316u, 129u}, {196u, 135u, 316u, 154u}};
static const struct CaptureCheckRect CaptureCheck_LapsEmpty[] = {{10u, 83u, 126u, 107u},  {10u, 107u, 126u, 131u},  {10u, 131u, 126u, 155u},
                                                                 {386u, 83u, 502u, 107u}, {386u, 107u, 502u, 131u}, {386u, 131u, 502u, 155u},
                                                                 {10u, 156u, 502u, 170u}, {10u, 170u, 502u, 184u}};
static const uint16_t CaptureCheck_LapsGapY[][2] = {
    {33u, 38u}, {70u, 82u}, {106u, 109u}, {131u, 134u}, {156u, 184u}, {196u, 199u},
};

/* Wait: FONT_SMALL own picks at y = 68, 80, 92 and the opponent at y=116;
 * no countdown and no footer. */
static const struct CaptureCheckRect CaptureCheck_WaitLines[] = {
    {176u, 67u, 336u, 77u}, {176u, 79u, 336u, 89u}, {176u, 91u, 336u, 101u}, {176u, 115u, 336u, 125u}};
static const struct CaptureCheckRect CaptureCheck_WaitEmpty[] = {
    {10u, 127u, 502u, 141u}, {10u, 141u, 502u, 155u}, {10u, 155u, 502u, 169u}, {10u, 169u, 502u, 184u}};
static const uint16_t CaptureCheck_WaitGapY[][2] = {
    {33u, 38u},
    {102u, 114u},
    {126u, 199u},
};

/* Result: FONT_SMALL track y=66, laps y=80, P1 y=100, P2 y=112, bots y=130,
 * and the GET READY footer. */
static const struct CaptureCheckRect CaptureCheck_ResultLines[] = {
    {176u, 65u, 336u, 75u}, {176u, 79u, 336u, 89u}, {176u, 99u, 336u, 109u}, {176u, 111u, 336u, 121u}, {176u, 129u, 336u, 139u}};
static const struct CaptureCheckRect CaptureCheck_ResultEmpty[] = {{10u, 140u, 502u, 155u}, {10u, 155u, 502u, 170u}, {10u, 170u, 502u, 184u}};
static const uint16_t CaptureCheck_ResultGapY[][2] = {
    {33u, 38u},
    {59u, 64u},
    {140u, 184u},
    {196u, 199u},
};

/* The local cursor's row highlight, inset 4 from its 252-wide cell: every
 * preview puts the local cursor on list entry 0.  Character and laps rows
 * use the 21-high highlight 3 above the row, track rows a 12-high one 2
 * above.  On wait and result the character rectangle must stay dark. */
#define CAPTURE_SELECT_CHAR_HIGHLIGHT    {8u, 75u, 252u, 96u}
#define CAPTURE_SELECT_TRACK_HIGHLIGHT   {8u, 74u, 252u, 86u}
#define CAPTURE_SELECT_LAPS_HIGHLIGHT    {134u, 83u, 378u, 104u}

#define CAPTURE_SELECT_MAX_REGION_CHECKS 7u

struct CaptureCheckRegionCheck
{
	uint8_t id;     /* enum NativeCaptureCheckId */
	uint8_t expect; /* AT_LEAST: every region holds text; AT_MOST: none does */
	uint8_t count;
	const struct CaptureCheckRect *rects;
};

struct CaptureCheckSelectSpec
{
	const uint16_t (*gapY)[2];
	uint32_t gapCount;
	struct CaptureCheckRect highlight;
	uint8_t highlightExpect;
	struct CaptureCheckRegionCheck checks[CAPTURE_SELECT_MAX_REGION_CHECKS];
};

#define CAPTURE_COUNT_OF(a)         ((uint8_t)(sizeof(a) / sizeof((a)[0])))
#define CAPTURE_REQUIRED(id, rects) {(uint8_t)(id), (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST, CAPTURE_COUNT_OF(rects), rects}
#define CAPTURE_ABSENT(id, rects)   {(uint8_t)(id), (uint8_t)NATIVE_CAPTURE_EXPECT_AT_MOST, CAPTURE_COUNT_OF(rects), rects}

/* Indexed by screen - CAPTURE_FIRST_SELECT_SCREEN. */
static const struct CaptureCheckSelectSpec CaptureCheck_SelectSpecs[CAPTURE_SELECT_SCREEN_COUNT] = {
    /* select-character */
    {CaptureCheck_CharGapY,
	 CAPTURE_COUNT_OF(CaptureCheck_CharGapY),
	 CAPTURE_SELECT_CHAR_HIGHLIGHT,
	 (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST,
	 {CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TITLE, CaptureCheck_SelTitle), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TIME, CaptureCheck_SelTime),
	  CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1, CaptureCheck_CharCol1),
	  CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2, CaptureCheck_CharCol2), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_FOOTER, CaptureCheck_SelFooter),
	  CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY, CaptureCheck_CharEmpty)}},
    /* select-track */
    {CaptureCheck_TrackGapY,
	 CAPTURE_COUNT_OF(CaptureCheck_TrackGapY),
	 CAPTURE_SELECT_TRACK_HIGHLIGHT,
	 (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST,
	 {CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TITLE, CaptureCheck_SelTitle), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TIME, CaptureCheck_SelTime),
	  CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1, CaptureCheck_TrackCol1),
	  CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2, CaptureCheck_TrackCol2), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_FOOTER, CaptureCheck_SelFooter),
	  CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY, CaptureCheck_TrackEmpty)}},
    /* select-laps */
    {CaptureCheck_LapsGapY,
	 CAPTURE_COUNT_OF(CaptureCheck_LapsGapY),
	 CAPTURE_SELECT_LAPS_HIGHLIGHT,
	 (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST,
	 {CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TITLE, CaptureCheck_SelTitle), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TIME, CaptureCheck_SelTime),
	  CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1, CaptureCheck_LapsCol1), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_FOOTER, CaptureCheck_SelFooter),
	  CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY, CaptureCheck_LapsEmpty)}},
    /* select-wait */
    {CaptureCheck_WaitGapY,
	 CAPTURE_COUNT_OF(CaptureCheck_WaitGapY),
	 CAPTURE_SELECT_CHAR_HIGHLIGHT,
	 (uint8_t)NATIVE_CAPTURE_EXPECT_AT_MOST,
	 {CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TITLE, CaptureCheck_SelTitle), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_LINES, CaptureCheck_WaitLines),
	  CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER, CaptureCheck_SelFooter), CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY, CaptureCheck_WaitEmpty)}},
    /* select-result */
    {CaptureCheck_ResultGapY,
	 CAPTURE_COUNT_OF(CaptureCheck_ResultGapY),
	 CAPTURE_SELECT_CHAR_HIGHLIGHT,
	 (uint8_t)NATIVE_CAPTURE_EXPECT_AT_MOST,
	 {CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TITLE, CaptureCheck_SelTitle), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_LINES, CaptureCheck_ResultLines),
	  CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_FOOTER, CaptureCheck_SelFooter), CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY, CaptureCheck_ResultEmpty)}},
};

/* select-solo (SOLO-5): the character screen for one human, as
 * select-character but with no opponent footer. */
static const struct CaptureCheckSelectSpec CaptureCheck_SelectSoloSpec = {
    CaptureCheck_CharGapY,
    CAPTURE_COUNT_OF(CaptureCheck_CharGapY),
    CAPTURE_SELECT_CHAR_HIGHLIGHT,
    (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST,
    {CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TITLE, CaptureCheck_SelTitle), CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_TIME, CaptureCheck_SelTime),
     CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1, CaptureCheck_CharCol1),
     CAPTURE_REQUIRED(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2, CaptureCheck_CharCol2), CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER, CaptureCheck_SelFooter),
     CAPTURE_ABSENT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY, CaptureCheck_CharEmpty)}};

/* ------------------------------------------------------------------------ */
/* Thresholds                                                               */
/* ------------------------------------------------------------------------ */

/* Calibrated on 800x600 v0 preview captures of all twelve screens (frame
 * 1320) against the default path (retail title plus main menu, no panel).
 * Observed ranges are noted per threshold.
 *
 * Glyph pixel: saturated fill (max channel >= BRIGHT) with a dark outline
 * pixel (max channel <= DARK) within two retail pixels on its row or column.
 * The panel halves the scene underneath (observed max channel <= 0x84), while
 * the dimmest glyph fill, RED, peaks at 0xbd-0xce.  Without the panel the
 * scene itself produces glyph-like pixels, so text results are meaningful
 * only together with the panel checks; every check must pass.
 *
 * The select screens reuse every threshold below unchanged.  They were
 * calibrated on 800x600 v0 captures of the five select previews (frame 1320)
 * rendered two ways: all 17 previews plus the default path in parallel (the
 * preview script's default) and each select preview alone.  The title scene
 * behind the translucent panel differs between the two (183k-261k of 480k
 * RGB pixels, 116k-178k of them inside the select panel), while runs under
 * the same condition are byte-identical; no measurement moved by more than
 * 9 between the two, and all of them pass under both:
 *   frame 987-997, dim 0, detail 34-35;
 *   required text (emptiest row or line where there are several): title
 *   61-114, TIME 94, grid columns 90-163, lines 77-132, footer 57-132;
 *   blank strips and the absent wait footer 0;
 *   cursor highlight 924-966 on the picking screens, 190-195 at the
 *   character cursor on wait and result (at most 350).
 * The default path, checked as each select screen: frame 41, dim 310-370. */
#define CAPTURE_GLYPH_BRIGHT         0xb0u
#define CAPTURE_GLYPH_DARK           0x30u

/* Text band density, glyph pixels per mille of band area.  Required bands
 * measured 29 (EXIT row) to 155; unused bands measured 0. */
#define CAPTURE_TEXT_REQUIRED_MIN    12
#define CAPTURE_TEXT_ABSENT_MAX      4

/* Frame: luma(frame) - luma(3 retail px inside) >= STEP on this share.
 * Panel screens 993, default path 71. */
#define CAPTURE_FRAME_STEP           32
#define CAPTURE_FRAME_MIN_PERMIL     700

/* Gap pixels with max channel >= DIM_BRIGHT: at most this share.
 * Panel screens 0, default path 410, white-filled panel 1000. */
#define CAPTURE_DIM_BRIGHT           0xa0u
#define CAPTURE_DIM_MAX_PERMIL       20

/* Gap luma standard deviation (the dimmed scene is not a flat fill).
 * Panel screens 32, flat fill 0. */
#define CAPTURE_DETAIL_MIN_STDDEV    8

/* Highlight edge: (R+G) inside - (R+G) outside >= STEP on this share.
 * Results screens 973; other panel screens 26-120; default path 85. */
#define CAPTURE_HIGHLIGHT_STEP       96
#define CAPTURE_HIGHLIGHT_MIN_PERMIL 700
#define CAPTURE_HIGHLIGHT_MAX_PERMIL 350

/* ------------------------------------------------------------------------ */
/* Measurements                                                             */
/* ------------------------------------------------------------------------ */

static uint32_t CaptureCheck_Sx(const struct NativeCaptureImage *image, uint32_t retailX2)
{
	/* retailX2 is in half retail pixels. */
	return (uint32_t)(((uint64_t)retailX2 * image->width) / (2u * CAPTURE_RETAIL_W));
}

static uint32_t CaptureCheck_Sy(const struct NativeCaptureImage *image, uint32_t retailY2)
{
	return (uint32_t)(((uint64_t)retailY2 * image->height) / (2u * CAPTURE_RETAIL_H));
}

static uint32_t CaptureCheck_Max3(struct NativeCaptureRgb c)
{
	uint32_t m = c.r;
	if (c.g > m)
		m = c.g;
	if (c.b > m)
		m = c.b;
	return m;
}

static int32_t CaptureCheck_Luma(struct NativeCaptureRgb c)
{
	return (int32_t)((77u * c.r + 150u * c.g + 29u * c.b) >> 8);
}

static int32_t CaptureCheck_Permil(uint64_t part, uint64_t whole)
{
	if (whole == 0u)
		return 0;
	return (int32_t)((part * 1000u) / whole);
}

static int CaptureCheck_IsDark(const struct NativeCaptureImage *image, uint32_t x, uint32_t y)
{
	return CaptureCheck_Max3(NativeCaptureCheck_GetRgb(image, x, y)) <= CAPTURE_GLYPH_DARK;
}

static int CaptureCheck_IsGlyph(const struct NativeCaptureImage *image, uint32_t x, uint32_t y, uint32_t rx, uint32_t ry)
{
	uint32_t d;

	if (CaptureCheck_Max3(NativeCaptureCheck_GetRgb(image, x, y)) < CAPTURE_GLYPH_BRIGHT)
		return 0;
	for (d = 1u; d <= rx; d++)
	{
		if ((x >= d) && CaptureCheck_IsDark(image, x - d, y))
			return 1;
		if (((x + d) < image->width) && CaptureCheck_IsDark(image, x + d, y))
			return 1;
	}
	for (d = 1u; d <= ry; d++)
	{
		if ((y >= d) && CaptureCheck_IsDark(image, x, y - d))
			return 1;
		if (((y + d) < image->height) && CaptureCheck_IsDark(image, x, y + d))
			return 1;
	}
	return 0;
}

/* Glyph pixels per mille of a retail rectangle. */
static int32_t CaptureCheck_RectDensity(const struct NativeCaptureImage *image, const struct CaptureCheckRect *rect)
{
	const uint32_t x0 = CaptureCheck_Sx(image, 2u * rect->x0);
	const uint32_t x1 = CaptureCheck_Sx(image, 2u * rect->x1);
	const uint32_t y0 = CaptureCheck_Sy(image, 2u * rect->y0);
	const uint32_t y1 = CaptureCheck_Sy(image, 2u * rect->y1);
	/* Two retail pixels of outline search, at least one image pixel. */
	const uint32_t rx = CaptureCheck_Sx(image, 4u) + 1u;
	const uint32_t ry = CaptureCheck_Sy(image, 4u) + 1u;
	uint64_t glyph = 0u;
	uint32_t x;
	uint32_t y;

	for (y = y0; y < y1; y++)
	{
		for (x = x0; x < x1; x++)
		{
			glyph += (uint64_t)CaptureCheck_IsGlyph(image, x, y, rx, ry);
		}
	}
	return CaptureCheck_Permil(glyph, (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0));
}

static void CaptureCheck_FrameSample(const struct NativeCaptureImage *image, uint32_t fx, uint32_t fy, uint32_t ix, uint32_t iy, uint64_t *hits,
                                     uint64_t *total)
{
	const int32_t step = CaptureCheck_Luma(NativeCaptureCheck_GetRgb(image, fx, fy)) - CaptureCheck_Luma(NativeCaptureCheck_GetRgb(image, ix, iy));

	*total += 1u;
	if (step >= CAPTURE_FRAME_STEP)
		*hits += 1u;
}

static int32_t CaptureCheck_Frame(const struct NativeCaptureImage *image, uint32_t panelX0, uint32_t panelX1)
{
	/* Frame centre lines: rows 28..30 and 202..204, columns panelX0..+3 and
	 * panelX1-3..panelX1 (56..59 and 453..456 on the shared panel).  The
	 * inside reference sits three retail pixels further in. */
	const uint32_t topY = CaptureCheck_Sy(image, 2u * (CAPTURE_PANEL_Y0 + 1u));
	const uint32_t topInY = CaptureCheck_Sy(image, 2u * (CAPTURE_PANEL_Y0 + 5u));
	const uint32_t botY = CaptureCheck_Sy(image, 2u * (CAPTURE_PANEL_Y1 - 1u));
	const uint32_t botInY = CaptureCheck_Sy(image, 2u * (CAPTURE_PANEL_Y1 - 5u));
	const uint32_t leftX = CaptureCheck_Sx(image, 2u * panelX0 + 3u);
	const uint32_t leftInX = CaptureCheck_Sx(image, 2u * (panelX0 + 6u));
	const uint32_t rightX = CaptureCheck_Sx(image, 2u * panelX1 - 3u);
	const uint32_t rightInX = CaptureCheck_Sx(image, 2u * (panelX1 - 6u));
	const uint32_t x0 = CaptureCheck_Sx(image, 2u * (panelX0 + 8u));
	const uint32_t x1 = CaptureCheck_Sx(image, 2u * (panelX1 - 8u));
	const uint32_t y0 = CaptureCheck_Sy(image, 2u * (CAPTURE_PANEL_Y0 + 6u));
	const uint32_t y1 = CaptureCheck_Sy(image, 2u * (CAPTURE_PANEL_Y1 - 6u));
	uint64_t hits = 0u;
	uint64_t total = 0u;
	uint32_t i;

	for (i = x0; i < x1; i++)
	{
		CaptureCheck_FrameSample(image, i, topY, i, topInY, &hits, &total);
		CaptureCheck_FrameSample(image, i, botY, i, botInY, &hits, &total);
	}
	for (i = y0; i < y1; i++)
	{
		CaptureCheck_FrameSample(image, leftX, i, leftInX, i, &hits, &total);
		CaptureCheck_FrameSample(image, rightX, i, rightInX, i, &hits, &total);
	}
	return CaptureCheck_Permil(hits, total);
}

/* The gap rows of one screen, measured across [spanX0, spanX1). */
struct CaptureCheckGaps
{
	const uint16_t (*gapY)[2];
	uint32_t gapCount;
	uint32_t spanX0;
	uint32_t spanX1;
};

/* Accumulates over the gap rows.  With mean16 == UINT32_MAX it counts pixels,
 * bright pixels and the luma sum; otherwise it sums (16 * luma - mean16)^2,
 * which stays far below 2^64 for capped dimensions. */
static void CaptureCheck_GapPass(const struct NativeCaptureImage *image, const struct CaptureCheckGaps *gaps, uint32_t mean16, uint64_t *n, uint64_t *bright,
                                 uint64_t *acc)
{
	const uint32_t x0 = CaptureCheck_Sx(image, 2u * gaps->spanX0);
	const uint32_t x1 = CaptureCheck_Sx(image, 2u * gaps->spanX1);
	uint32_t g;
	uint32_t x;
	uint32_t y;

	*n = 0u;
	*bright = 0u;
	*acc = 0u;
	for (g = 0u; g < gaps->gapCount; g++)
	{
		const uint32_t y0 = CaptureCheck_Sy(image, 2u * gaps->gapY[g][0]);
		const uint32_t y1 = CaptureCheck_Sy(image, 2u * gaps->gapY[g][1]);

		for (y = y0; y < y1; y++)
		{
			for (x = x0; x < x1; x++)
			{
				const struct NativeCaptureRgb c = NativeCaptureCheck_GetRgb(image, x, y);
				const int64_t luma16 = 16 * (int64_t)CaptureCheck_Luma(c);

				*n += 1u;
				if (CaptureCheck_Max3(c) >= CAPTURE_DIM_BRIGHT)
					*bright += 1u;
				if (mean16 == UINT32_MAX)
				{
					*acc += (uint64_t)luma16;
				}
				else
				{
					const int64_t d = luma16 - (int64_t)mean16;
					*acc += (uint64_t)(d * d);
				}
			}
		}
	}
}

/* The bright share over the `dim` span and the luma deviation over the
 * `detail` span of the same gap rows (the same span on the shared panel). */
static void CaptureCheck_Gaps(const struct NativeCaptureImage *image, const struct CaptureCheckGaps *dim, const struct CaptureCheckGaps *detail,
                              int32_t *brightPermil, int32_t *stddev)
{
	uint64_t n;
	uint64_t bright;
	uint64_t sum16;
	uint64_t sq;
	uint64_t variance;
	uint64_t root = 0u;

	CaptureCheck_GapPass(image, dim, UINT32_MAX, &n, &bright, &sum16);
	*brightPermil = CaptureCheck_Permil(bright, n);
	*stddev = 0;
	CaptureCheck_GapPass(image, detail, UINT32_MAX, &n, &bright, &sum16);
	if (n == 0u)
		return;
	CaptureCheck_GapPass(image, detail, (uint32_t)(sum16 / n), &n, &bright, &sq);
	/* Variance in luma^2 units: the samples were scaled by 16. */
	variance = (sq / n) / 256u;
	while ((root + 1u) * (root + 1u) <= variance)
	{
		root++;
	}
	*stddev = (int32_t)root;
}

static void CaptureCheck_HighlightSample(const struct NativeCaptureImage *image, uint32_t inX, uint32_t inY, uint32_t outX, uint32_t outY, uint64_t *hits,
                                         uint64_t *total)
{
	const struct NativeCaptureRgb in = NativeCaptureCheck_GetRgb(image, inX, inY);
	const struct NativeCaptureRgb out = NativeCaptureCheck_GetRgb(image, outX, outY);
	const int32_t step = ((int32_t)in.r + (int32_t)in.g) - ((int32_t)out.r + (int32_t)out.g);

	*total += 1u;
	if (step >= CAPTURE_HIGHLIGHT_STEP)
		*hits += 1u;
}

static int32_t CaptureCheck_Highlight(const struct NativeCaptureImage *image, const struct CaptureCheckRect *rect)
{
	/* Additive fill: each edge brightens R and G from outside to inside.
	 * Samples sit two retail pixels either side of the edge. */
	const uint32_t leftIn = CaptureCheck_Sx(image, 2u * (rect->x0 + 2u));
	const uint32_t leftOut = CaptureCheck_Sx(image, 2u * (rect->x0 - 2u));
	const uint32_t rightIn = CaptureCheck_Sx(image, 2u * (rect->x1 - 2u));
	const uint32_t rightOut = CaptureCheck_Sx(image, 2u * (rect->x1 + 2u));
	const uint32_t topIn = CaptureCheck_Sy(image, 2u * (rect->y0 + 1u));
	const uint32_t topOut = CaptureCheck_Sy(image, 2u * (rect->y0 - 2u));
	const uint32_t botIn = CaptureCheck_Sy(image, 2u * (rect->y1 - 1u));
	const uint32_t botOut = CaptureCheck_Sy(image, 2u * (rect->y1 + 2u));
	const uint32_t x0 = CaptureCheck_Sx(image, 2u * (rect->x0 + 4u));
	const uint32_t x1 = CaptureCheck_Sx(image, 2u * (rect->x1 - 4u));
	const uint32_t y0 = CaptureCheck_Sy(image, 2u * (rect->y0 + 2u));
	const uint32_t y1 = CaptureCheck_Sy(image, 2u * (rect->y1 - 2u));
	uint64_t hits = 0u;
	uint64_t total = 0u;
	uint32_t i;

	for (i = x0; i < x1; i++)
	{
		CaptureCheck_HighlightSample(image, i, topIn, i, topOut, &hits, &total);
		CaptureCheck_HighlightSample(image, i, botIn, i, botOut, &hits, &total);
	}
	for (i = y0; i < y1; i++)
	{
		CaptureCheck_HighlightSample(image, leftIn, i, leftOut, i, &hits, &total);
		CaptureCheck_HighlightSample(image, rightIn, i, rightOut, i, &hits, &total);
	}
	return CaptureCheck_Permil(hits, total);
}


/* ------------------------------------------------------------------------ */
/* Report                                                                   */
/* ------------------------------------------------------------------------ */

static const char *const CaptureCheck_CheckNames[NATIVE_CAPTURE_CHECK_COUNT] = {
    "panel-frame", "panel-dim",         "panel-detail", "text-title",     "text-body1",     "text-body2", "text-row-rematch", "text-row-exit",
    "text-footer", "highlight-rematch", "text-time",    "text-grid-col1", "text-grid-col2", "text-lines", "text-empty",       "highlight-cursor",
};

static const char *const CaptureCheck_CheckUnits[NATIVE_CAPTURE_CHECK_COUNT] = {
    "permil frame samples brighter than inside",
    "permil gap pixels with max channel >= 160",
    "gap luma stddev",
    "permil glyph pixels",
    "permil glyph pixels",
    "permil glyph pixels",
    "permil glyph pixels",
    "permil glyph pixels",
    "permil glyph pixels",
    "permil edge samples with additive step",
    "permil glyph pixels",
    "permil glyph pixels, emptiest row",
    "permil glyph pixels, emptiest row",
    "permil glyph pixels, emptiest line",
    "permil glyph pixels, fullest region",
    "permil edge samples with additive step",
};

const char *NativeCaptureCheck_CheckName(enum NativeCaptureCheckId id)
{
	if ((unsigned)id >= (unsigned)NATIVE_CAPTURE_CHECK_COUNT)
		return NULL;
	return CaptureCheck_CheckNames[id];
}

const char *NativeCaptureCheck_CheckUnit(enum NativeCaptureCheckId id)
{
	if ((unsigned)id >= (unsigned)NATIVE_CAPTURE_CHECK_COUNT)
		return NULL;
	return CaptureCheck_CheckUnits[id];
}

static void CaptureCheck_Set(struct NativeCaptureSubCheck *check, uint32_t expect, int32_t measured, int32_t threshold)
{
	check->expect = expect;
	check->measured = measured;
	check->threshold = threshold;
	if (expect == NATIVE_CAPTURE_EXPECT_AT_LEAST)
		check->passed = measured >= threshold ? 1u : 0u;
	else if (expect == NATIVE_CAPTURE_EXPECT_AT_MOST)
		check->passed = measured <= threshold ? 1u : 0u;
	else
		check->passed = 1u;
}

/* The shared-panel screens: six fixed bands and the REMATCH highlight. */
static void CaptureCheck_RunShared(const struct NativeCaptureImage *image, const struct CaptureCheckScreenSpec *spec, struct NativeCaptureReport *report)
{
	const struct CaptureCheckGaps gaps = {CaptureCheck_GapY, (uint32_t)(sizeof(CaptureCheck_GapY) / sizeof(CaptureCheck_GapY[0])), CAPTURE_INNER_X0,
	                                      CAPTURE_INNER_X1};
	const struct CaptureCheckRect highlight = {CAPTURE_HIGHLIGHT_X0, CAPTURE_HIGHLIGHT_Y0, CAPTURE_HIGHLIGHT_X1, CAPTURE_HIGHLIGHT_Y1};
	int32_t brightPermil;
	int32_t stddev;
	uint32_t band;

	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME], NATIVE_CAPTURE_EXPECT_AT_LEAST,
	                 CaptureCheck_Frame(image, CAPTURE_PANEL_X0, CAPTURE_PANEL_X1), CAPTURE_FRAME_MIN_PERMIL);
	CaptureCheck_Gaps(image, &gaps, &gaps, &brightPermil, &stddev);
	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_PANEL_DIM], NATIVE_CAPTURE_EXPECT_AT_MOST, brightPermil, CAPTURE_DIM_MAX_PERMIL);
	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_PANEL_DETAIL], NATIVE_CAPTURE_EXPECT_AT_LEAST, stddev, CAPTURE_DETAIL_MIN_STDDEV);

	for (band = 0u; band < CAPTURE_TEXT_BAND_COUNT; band++)
	{
		const struct CaptureCheckRect rect = {CAPTURE_INNER_X0, CaptureCheck_BandY[band][0], CAPTURE_INNER_X1, CaptureCheck_BandY[band][1]};
		const int32_t density = CaptureCheck_RectDensity(image, &rect);
		struct NativeCaptureSubCheck *check = &report->checks[NATIVE_CAPTURE_CHECK_TEXT_TITLE + band];

		if (spec->band[band] == CAPTURE_BAND_REQUIRED)
			CaptureCheck_Set(check, NATIVE_CAPTURE_EXPECT_AT_LEAST, density, CAPTURE_TEXT_REQUIRED_MIN);
		else if (spec->band[band] == CAPTURE_BAND_ABSENT)
			CaptureCheck_Set(check, NATIVE_CAPTURE_EXPECT_AT_MOST, density, CAPTURE_TEXT_ABSENT_MAX);
		else
			CaptureCheck_Set(check, NATIVE_CAPTURE_EXPECT_INFO, density, 0);
	}

	if (spec->highlight != 0u)
		CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT], NATIVE_CAPTURE_EXPECT_AT_LEAST, CaptureCheck_Highlight(image, &highlight),
		                 CAPTURE_HIGHLIGHT_MIN_PERMIL);
	else
		CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT], NATIVE_CAPTURE_EXPECT_AT_MOST, CaptureCheck_Highlight(image, &highlight),
		                 CAPTURE_HIGHLIGHT_MAX_PERMIL);
}

/* The select screens: the widened panel, per-screen regions and the local
 * cursor highlight.  A required check reports its emptiest region, an
 * absent check its fullest, so every region is judged. */
static void CaptureCheck_RunSelect(const struct NativeCaptureImage *image, const struct CaptureCheckSelectSpec *spec, struct NativeCaptureReport *report)
{
	const struct CaptureCheckGaps dim = {spec->gapY, spec->gapCount, CAPTURE_SELECT_INNER_X0, CAPTURE_SELECT_INNER_X1};
	const struct CaptureCheckGaps detail = {spec->gapY, spec->gapCount, CAPTURE_INNER_X0, CAPTURE_INNER_X1};
	int32_t brightPermil;
	int32_t stddev;
	uint32_t c;
	uint32_t r;

	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME], NATIVE_CAPTURE_EXPECT_AT_LEAST,
	                 CaptureCheck_Frame(image, CAPTURE_SELECT_PANEL_X0, CAPTURE_SELECT_PANEL_X1), CAPTURE_FRAME_MIN_PERMIL);
	CaptureCheck_Gaps(image, &dim, &detail, &brightPermil, &stddev);
	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_PANEL_DIM], NATIVE_CAPTURE_EXPECT_AT_MOST, brightPermil, CAPTURE_DIM_MAX_PERMIL);
	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_PANEL_DETAIL], NATIVE_CAPTURE_EXPECT_AT_LEAST, stddev, CAPTURE_DETAIL_MIN_STDDEV);

	for (c = 0u; c < CAPTURE_SELECT_MAX_REGION_CHECKS; c++)
	{
		const struct CaptureCheckRegionCheck *region = &spec->checks[c];
		const int required = region->expect == (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST;
		int32_t measured;

		if (region->count == 0u)
			continue;
		measured = CaptureCheck_RectDensity(image, &region->rects[0]);
		for (r = 1u; r < region->count; r++)
		{
			const int32_t density = CaptureCheck_RectDensity(image, &region->rects[r]);

			if (required ? (density < measured) : (density > measured))
				measured = density;
		}
		CaptureCheck_Set(&report->checks[region->id], region->expect, measured, required ? CAPTURE_TEXT_REQUIRED_MIN : CAPTURE_TEXT_ABSENT_MAX);
	}

	CaptureCheck_Set(&report->checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR], spec->highlightExpect, CaptureCheck_Highlight(image, &spec->highlight),
	                 (spec->highlightExpect == (uint8_t)NATIVE_CAPTURE_EXPECT_AT_LEAST) ? CAPTURE_HIGHLIGHT_MIN_PERMIL : CAPTURE_HIGHLIGHT_MAX_PERMIL);
}

int NativeCaptureCheck_Run(const struct NativeCaptureImage *image, enum NativeCaptureScreen screen, struct NativeCaptureReport *out)
{
	struct NativeCaptureReport report;
	uint32_t i;

	if ((image == NULL) || (image->pixels == NULL) || (out == NULL))
		return 0;
	if ((unsigned)screen >= (unsigned)NATIVE_CAPTURE_SCREEN_COUNT)
		return 0;
	/* Every region must span at least one pixel per retail pixel. */
	if ((image->width < CAPTURE_RETAIL_W) || (image->height < CAPTURE_RETAIL_H))
		return 0;

	memset(&report, 0, sizeof(report));
	report.screen = (uint32_t)screen;
	/* A check the screen's layout has no region for stays NONE. */
	for (i = 0u; i < NATIVE_CAPTURE_CHECK_COUNT; i++)
	{
		CaptureCheck_Set(&report.checks[i], NATIVE_CAPTURE_EXPECT_NONE, 0, 0);
	}
	if ((unsigned)screen < (unsigned)CAPTURE_FIRST_SELECT_SCREEN)
		CaptureCheck_RunShared(image, &CaptureCheck_Specs[screen], &report);
	else if ((unsigned)screen < (unsigned)CAPTURE_FIRST_SOLO_SCREEN)
		CaptureCheck_RunSelect(image, &CaptureCheck_SelectSpecs[screen - CAPTURE_FIRST_SELECT_SCREEN], &report);
	else if (screen == NATIVE_CAPTURE_SCREEN_SELECT_SOLO)
		CaptureCheck_RunSelect(image, &CaptureCheck_SelectSoloSpec, &report);
	else
		CaptureCheck_RunShared(image, &CaptureCheck_SoloSpecs[screen - CAPTURE_FIRST_SOLO_SCREEN], &report);

	report.passed = 1u;
	report.firstFailed = NATIVE_CAPTURE_CHECK_COUNT;
	for (i = 0u; i < NATIVE_CAPTURE_CHECK_COUNT; i++)
	{
		if (report.checks[i].passed == 0u)
		{
			report.failedMask |= 1u << i;
			if (report.passed != 0u)
				report.firstFailed = i;
			report.passed = 0u;
		}
	}
	*out = report;
	return 1;
}
