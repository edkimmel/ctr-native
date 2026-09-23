/* Unit test for the arcade-link capture check.  Synthetic in-memory BMPs
 * only: no retail data, no captures, no files. */
#include "platform/native_capture_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                           \
	do                                                                     \
	{                                                                      \
		if (!(x))                                                          \
		{                                                                  \
			fprintf(stderr, "failed %s:%d: %s\n", __FILE__, __LINE__, #x); \
			return 1;                                                      \
		}                                                                  \
	} while (0)

#define W 800u
#define H 600u

/* Canvas in display order (row 0 at the top), RGB. */
static uint8_t g_canvas[H][W][3];
/* Large enough for 32 bpp plus a 124-byte header and a mask table. */
static uint8_t g_bmp[14u + 124u + 12u + W * H * 4u];
static uint8_t g_bmpOther[sizeof(g_bmp)];

/* ------------------------------------------------------------------------ */
/* Canvas painting in retail 512x216 coordinates                            */
/* ------------------------------------------------------------------------ */

static uint32_t Sx(uint32_t rx)
{
	return (rx * W) / 512u;
}

static uint32_t Sy(uint32_t ry)
{
	return (ry * H) / 216u;
}

static void FillPx(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint8_t r, uint8_t g, uint8_t b)
{
	uint32_t x;
	uint32_t y;

	for (y = y0; y < y1 && y < H; y++)
	{
		for (x = x0; x < x1 && x < W; x++)
		{
			g_canvas[y][x][0] = r;
			g_canvas[y][x][1] = g;
			g_canvas[y][x][2] = b;
		}
	}
}

static void FillRetail(uint32_t rx0, uint32_t ry0, uint32_t rx1, uint32_t ry1, uint8_t r, uint8_t g, uint8_t b)
{
	FillPx(Sx(rx0), Sy(ry0), Sx(rx1), Sy(ry1), r, g, b);
}

/* Title-art stand-in: bright checker flag plus an orange block, so an
 * undimmed scene is full of bright, high-contrast pixels. */
static void PaintScene(void)
{
	uint32_t x;
	uint32_t y;

	for (y = 0u; y < H; y++)
	{
		for (x = 0u; x < W; x++)
		{
			const int light = (((x / 40u) + (y / 40u)) & 1u) != 0u;
			const uint8_t v = light ? 0xe0u : 0x50u;
			g_canvas[y][x][0] = v;
			g_canvas[y][x][1] = v;
			g_canvas[y][x][2] = v;
		}
	}
	FillPx(120u, 150u, 360u, 420u, 0xd6u, 0x52u, 0x00u);
}

/* Panel: translucent dark fill (halves the scene) and a bright grey frame. */
static void PaintPanel(void)
{
	uint32_t x;
	uint32_t y;

	for (y = Sy(28u); y < Sy(204u); y++)
	{
		for (x = Sx(56u); x < Sx(456u); x++)
		{
			g_canvas[y][x][0] = (uint8_t)(g_canvas[y][x][0] / 2u);
			g_canvas[y][x][1] = (uint8_t)(g_canvas[y][x][1] / 2u);
			g_canvas[y][x][2] = (uint8_t)(g_canvas[y][x][2] / 2u);
		}
	}
	FillRetail(56u, 28u, 456u, 30u, 0xdeu, 0xffu, 0xffu);
	FillRetail(56u, 202u, 456u, 204u, 0xdeu, 0xffu, 0xffu);
	FillRetail(56u, 28u, 59u, 204u, 0xdeu, 0xffu, 0xffu);
	FillRetail(453u, 28u, 456u, 204u, 0xdeu, 0xffu, 0xffu);
}

/* Additive REMATCH highlight at x=136 y=117 w=240 h=21. */
static void PaintHighlight(void)
{
	uint32_t x;
	uint32_t y;

	for (y = Sy(117u); y < Sy(138u); y++)
	{
		for (x = Sx(136u); x < Sx(376u); x++)
		{
			const uint32_t r = g_canvas[y][x][0] + 0x7cu;
			const uint32_t g = g_canvas[y][x][1] + 0x5bu;
			g_canvas[y][x][0] = (uint8_t)(r > 0xffu ? 0xffu : r);
			g_canvas[y][x][1] = (uint8_t)(g > 0xffu ? 0xffu : g);
		}
	}
}

/* A centred text line of `chars` glyph cells: black outline, coloured fill. */
static void PaintLine(uint32_t y, uint32_t chars, int big, uint8_t r, uint8_t g, uint8_t b)
{
	const uint32_t cw = big ? 17u : 13u;
	const uint32_t ch = big ? 17u : 8u;
	const uint32_t x0 = 256u - (chars * cw) / 2u;
	uint32_t i;

	for (i = 0u; i < chars; i++)
	{
		const uint32_t gx = x0 + i * cw;
		FillRetail(gx + 1u, y, gx + cw - 1u, y + ch, 0x00u, 0x00u, 0x00u);
		FillRetail(gx + 3u, y + 1u, gx + cw - 3u, y + ch - 1u, r, g, b);
	}
}

#define ORANGE 0xffu, 0x73u, 0x00u
#define RED    0xc6u, 0x00u, 0x00u
#define WHITE  0xffu, 0xffu, 0xffu

enum Frame
{
	FRAME_LOBBY,
	FRAME_RESULTS,
	FRAME_RESULTS_RED,
	FRAME_RESULTS_NO_HIGHLIGHT,
	FRAME_PANEL_NO_TEXT,
	FRAME_WHITE_PANEL,
	FRAME_NO_PANEL_LOBBY
};

static void PaintFrame(enum Frame frame)
{
	PaintScene();
	if (frame != FRAME_NO_PANEL_LOBBY)
		PaintPanel();
	switch (frame)
	{
	case FRAME_LOBBY:
	case FRAME_NO_PANEL_LOBBY:
	{
		PaintLine(40u, 11u, 1, ORANGE);  /* ARCADE LINK */
		PaintLine(90u, 20u, 0, WHITE);   /* WAITING FOR OPPONENT */
		PaintLine(110u, 19u, 0, WHITE);  /* THIS CABINET: CAB 1 */
		PaintLine(186u, 14u, 0, ORANGE); /* TRIANGLE: BACK */
		break;
	}
	case FRAME_RESULTS:
	case FRAME_RESULTS_RED:
	case FRAME_RESULTS_NO_HIGHLIGHT:
	{
		if (frame != FRAME_RESULTS_NO_HIGHLIGHT)
			PaintHighlight();
		if (frame == FRAME_RESULTS_RED)
			PaintLine(40u, 10u, 1, RED); /* LINK ERROR */
		else
			PaintLine(40u, 13u, 1, ORANGE); /* RACE COMPLETE */
		PaintLine(120u, 7u, 1, ORANGE);     /* REMATCH */
		PaintLine(145u, 4u, 1, ORANGE);     /* EXIT */
		PaintLine(186u, 13u, 0, ORANGE);    /* CROSS: SELECT */
		break;
	}
	case FRAME_WHITE_PANEL:
	{
		FillRetail(56u, 28u, 456u, 204u, 0xffu, 0xffu, 0xffu);
		break;
	}
	default:
	{
		break;
	}
	}
}

/* ------------------------------------------------------------------------ */
/* BMP encoding                                                             */
/* ------------------------------------------------------------------------ */

enum Encoding
{
	ENC_RGB32 = 0,
	ENC_RGB24,
	ENC_BITFIELDS40, /* 40-byte header plus a 12-byte mask table */
	ENC_BITFIELDS124 /* V5 header with masks inside, as captures are written */
};

static void W16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void W32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* Bitfield encodings store R in bits 0-7 and B in bits 16-23 (the reverse of
 * BI_RGB) so a parser that ignores the masks decodes the wrong colours. */
static size_t Encode(uint8_t *out, uint32_t w, uint32_t h, enum Encoding enc, int topDown, uint8_t alpha)
{
	const uint32_t bpp = enc == ENC_RGB24 ? 24u : 32u;
	const uint32_t info = enc == ENC_BITFIELDS124 ? 124u : 40u;
	const uint32_t table = enc == ENC_BITFIELDS40 ? 12u : 0u;
	const uint32_t offset = 14u + info + table;
	const size_t stride = (((size_t)w * bpp + 31u) / 32u) * 4u;
	const int bitfields = (enc == ENC_BITFIELDS40) || (enc == ENC_BITFIELDS124);
	uint32_t x;
	uint32_t y;

	memset(out, 0, offset + stride * h);
	out[0] = 'B';
	out[1] = 'M';
	W32(out + 2, (uint32_t)(offset + stride * h));
	W32(out + 10, offset);
	W32(out + 14, info);
	W32(out + 18, w);
	W32(out + 22, topDown ? (uint32_t)(0u - h) : h);
	W16(out + 26, 1u);
	W16(out + 28, bpp);
	W32(out + 30, bitfields ? 3u : 0u);
	if (bitfields)
	{
		W32(out + 54, 0x000000ffu); /* R */
		W32(out + 58, 0x0000ff00u); /* G */
		W32(out + 62, 0x00ff0000u); /* B */
		if (enc == ENC_BITFIELDS124)
			W32(out + 66, 0xff000000u);
	}
	for (y = 0u; y < h; y++)
	{
		const uint32_t row = topDown ? y : (h - 1u - y);
		uint8_t *p = out + offset + (size_t)row * stride;

		for (x = 0u; x < w; x++)
		{
			const uint8_t *c = g_canvas[y][x];
			if (bitfields)
			{
				p[0] = c[0];
				p[1] = c[1];
				p[2] = c[2];
			}
			else
			{
				p[0] = c[2];
				p[1] = c[1];
				p[2] = c[0];
			}
			if (bpp == 32u)
				p[3] = alpha;
			p += bpp / 8u;
		}
	}
	return offset + stride * h;
}

static int RunOn(const uint8_t *bmp, size_t size, enum NativeCaptureScreen screen, struct NativeCaptureReport *report)
{
	struct NativeCaptureImage image;

	if (NativeCaptureCheck_ParseBmp(bmp, size, &image) != NATIVE_CAPTURE_BMP_OK)
		return 0;
	return NativeCaptureCheck_Run(&image, screen, report);
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                    */
/* ------------------------------------------------------------------------ */

static int TestScreenNames(void)
{
	static const char *const names[] = {"title",           "lobby",          "lobby-connecting",   "lobby-rejected", "match-found", "results",
	                                    "results-timeout", "results-desync", "results-link-error", "rematch",        "exit",        "exit-opponent-left"};
	enum NativeCaptureScreen screen;
	unsigned i;

	CHECK(sizeof(names) / sizeof(names[0]) == (size_t)NATIVE_CAPTURE_SCREEN_COUNT);
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; i++)
	{
		CHECK(NativeCaptureCheck_ScreenFromName(names[i], &screen));
		CHECK((unsigned)screen == i);
		CHECK(strcmp(NativeCaptureCheck_ScreenName(screen), names[i]) == 0);
	}
	CHECK(!NativeCaptureCheck_ScreenFromName("Lobby", &screen));
	CHECK(!NativeCaptureCheck_ScreenFromName("results-", &screen));
	CHECK(!NativeCaptureCheck_ScreenFromName("", &screen));
	CHECK(!NativeCaptureCheck_ScreenFromName(NULL, &screen));
	CHECK(!NativeCaptureCheck_ScreenFromName("lobby", NULL));
	CHECK(NativeCaptureCheck_ScreenName(NATIVE_CAPTURE_SCREEN_COUNT) == NULL);
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_CHECK_COUNT; i++)
	{
		CHECK(NativeCaptureCheck_CheckName((enum NativeCaptureCheckId)i) != NULL);
		CHECK(NativeCaptureCheck_CheckUnit((enum NativeCaptureCheckId)i) != NULL);
	}
	CHECK(NativeCaptureCheck_CheckName(NATIVE_CAPTURE_CHECK_COUNT) == NULL);
	return 0;
}

/* Every encoding decodes to the same RGB, and alpha never leaks in. */
static int TestPixelFormats(void)
{
	static const enum Encoding encodings[] = {ENC_RGB32, ENC_RGB24, ENC_BITFIELDS40, ENC_BITFIELDS124};
	struct NativeCaptureImage image;
	unsigned e;
	int topDown;
	uint32_t x;
	uint32_t y;

	memset(g_canvas, 0, sizeof(g_canvas));
	for (y = 0u; y < 5u; y++)
	{
		for (x = 0u; x < 7u; x++)
		{
			g_canvas[y][x][0] = (uint8_t)(x * 30u + y);
			g_canvas[y][x][1] = (uint8_t)(200u - y * 20u);
			g_canvas[y][x][2] = (uint8_t)(x * 7u + y * 40u);
		}
	}
	for (e = 0u; e < sizeof(encodings) / sizeof(encodings[0]); e++)
	{
		for (topDown = 0; topDown <= 1; topDown++)
		{
			const size_t size = Encode(g_bmp, 7u, 5u, encodings[e], topDown, 0xa5u);

			CHECK(NativeCaptureCheck_ParseBmp(g_bmp, size, &image) == NATIVE_CAPTURE_BMP_OK);
			CHECK(image.width == 7u && image.height == 5u);
			CHECK(image.topDown == (uint32_t)topDown);
			CHECK(image.bytesPerPixel == (encodings[e] == ENC_RGB24 ? 3u : 4u));
			for (y = 0u; y < 5u; y++)
			{
				for (x = 0u; x < 7u; x++)
				{
					const struct NativeCaptureRgb c = NativeCaptureCheck_GetRgb(&image, x, y);
					CHECK(c.r == g_canvas[y][x][0] && c.g == g_canvas[y][x][1] && c.b == g_canvas[y][x][2]);
				}
			}
			/* Out of range reads return black rather than touching memory. */
			{
				const struct NativeCaptureRgb c = NativeCaptureCheck_GetRgb(&image, 7u, 0u);
				const struct NativeCaptureRgb d = NativeCaptureCheck_GetRgb(&image, 0u, 5u);
				CHECK(c.r == 0u && c.g == 0u && c.b == 0u && d.r == 0u && d.g == 0u && d.b == 0u);
			}
		}
	}

	/* 10-bit channel masks (2:10:10:10) are scaled down to 8 bits. */
	{
		const size_t size = Encode(g_bmp, 7u, 5u, ENC_BITFIELDS124, 0, 0u);
		uint8_t *p = g_bmp + 14u + 124u + (size_t)4u * 7u * 4u; /* bottom-up: stored row 4 is y=0 */

		W32(g_bmp + 54, 0x3ff00000u);
		W32(g_bmp + 58, 0x000ffc00u);
		W32(g_bmp + 62, 0x000003ffu);
		W32(p, (0x3ffu << 20) | (0x200u << 10) | 0x004u);
		CHECK(NativeCaptureCheck_ParseBmp(g_bmp, size, &image) == NATIVE_CAPTURE_BMP_OK);
		{
			const struct NativeCaptureRgb c = NativeCaptureCheck_GetRgb(&image, 0u, 0u);
			CHECK(c.r == 0xffu && c.g == 0x80u && c.b == 0x01u);
		}
	}
	return 0;
}

static int TestMalformed(void)
{
	struct NativeCaptureImage image;
	struct NativeCaptureImage untouched;
	size_t size;
	size_t n;

	memset(g_canvas, 0x40, sizeof(g_canvas));
	memset(&untouched, 0x5a, sizeof(untouched));

	/* Every truncation of a valid file is rejected, from empty through a
	 * single missing pixel byte, for each header layout. */
	{
		static const enum Encoding encodings[] = {ENC_RGB32, ENC_RGB24, ENC_BITFIELDS40, ENC_BITFIELDS124};
		unsigned e;
		for (e = 0u; e < sizeof(encodings) / sizeof(encodings[0]); e++)
		{
			size = Encode(g_bmp, 9u, 3u, encodings[e], e & 1u, 0u);
			CHECK(NativeCaptureCheck_ParseBmp(g_bmp, size, &image) == NATIVE_CAPTURE_BMP_OK);
			for (n = 0u; n < size; n++)
			{
				image = untouched;
				CHECK(NativeCaptureCheck_ParseBmp(g_bmp, n, &image) != NATIVE_CAPTURE_BMP_OK);
				CHECK(memcmp(&image, &untouched, sizeof(image)) == 0);
			}
		}
	}

	size = Encode(g_bmp, 9u, 3u, ENC_RGB32, 0, 0u);
	CHECK(NativeCaptureCheck_ParseBmp(NULL, size, &image) == NATIVE_CAPTURE_BMP_ERR_ARGUMENT);
	CHECK(NativeCaptureCheck_ParseBmp(g_bmp, size, NULL) == NATIVE_CAPTURE_BMP_ERR_ARGUMENT);

#define EXPECT_REJECT(mutation, status)                                           \
	do                                                                            \
	{                                                                             \
		memcpy(g_bmpOther, g_bmp, size);                                          \
		mutation;                                                                 \
		CHECK(NativeCaptureCheck_ParseBmp(g_bmpOther, size, &image) == (status)); \
	} while (0)

	EXPECT_REJECT(g_bmpOther[0] = 'X', NATIVE_CAPTURE_BMP_ERR_SIGNATURE);
	EXPECT_REJECT(W32(g_bmpOther + 14, 12u), NATIVE_CAPTURE_BMP_ERR_HEADER);            /* OS/2 core header */
	EXPECT_REJECT(W32(g_bmpOther + 14, 0xfffffff0u), NATIVE_CAPTURE_BMP_ERR_TRUNCATED); /* header past end */
	EXPECT_REJECT(W16(g_bmpOther + 26, 2u), NATIVE_CAPTURE_BMP_ERR_HEADER);             /* planes */
	EXPECT_REJECT(W16(g_bmpOther + 28, 16u), NATIVE_CAPTURE_BMP_ERR_FORMAT);
	EXPECT_REJECT(W16(g_bmpOther + 28, 8u), NATIVE_CAPTURE_BMP_ERR_FORMAT);
	EXPECT_REJECT(W32(g_bmpOther + 30, 1u), NATIVE_CAPTURE_BMP_ERR_FORMAT); /* RLE8 */
	EXPECT_REJECT(W32(g_bmpOther + 30, 4u), NATIVE_CAPTURE_BMP_ERR_FORMAT); /* JPEG */
	EXPECT_REJECT(W32(g_bmpOther + 18, 0u), NATIVE_CAPTURE_BMP_ERR_DIMENSIONS);
	EXPECT_REJECT(W32(g_bmpOther + 18, 0xfffffff7u), NATIVE_CAPTURE_BMP_ERR_DIMENSIONS); /* negative width */
	EXPECT_REJECT(W32(g_bmpOther + 22, 0u), NATIVE_CAPTURE_BMP_ERR_DIMENSIONS);
	EXPECT_REJECT(W32(g_bmpOther + 22, 0x80000000u), NATIVE_CAPTURE_BMP_ERR_DIMENSIONS);
	EXPECT_REJECT(W32(g_bmpOther + 18, 100000u), NATIVE_CAPTURE_BMP_ERR_DIMENSIONS);
	EXPECT_REJECT(W32(g_bmpOther + 18, 10u), NATIVE_CAPTURE_BMP_ERR_TRUNCATED); /* rows longer than data */
	EXPECT_REJECT(W32(g_bmpOther + 22, 4u), NATIVE_CAPTURE_BMP_ERR_TRUNCATED);
	EXPECT_REJECT(W32(g_bmpOther + 10, 20u), NATIVE_CAPTURE_BMP_ERR_HEADER); /* pixels inside header */
	EXPECT_REJECT(W32(g_bmpOther + 10, 0xffffff00u), NATIVE_CAPTURE_BMP_ERR_TRUNCATED);
	EXPECT_REJECT(W32(g_bmpOther + 10, 58u), NATIVE_CAPTURE_BMP_ERR_TRUNCATED); /* pixels run past end */

	/* BITFIELDS: 24 bpp is not a valid combination; masks must be sane. */
	size = Encode(g_bmp, 9u, 3u, ENC_RGB24, 0, 0u);
	EXPECT_REJECT(W32(g_bmpOther + 30, 3u), NATIVE_CAPTURE_BMP_ERR_FORMAT);
	size = Encode(g_bmp, 9u, 3u, ENC_BITFIELDS40, 0, 0u);
	EXPECT_REJECT(W32(g_bmpOther + 54, 0u), NATIVE_CAPTURE_BMP_ERR_MASKS);
	EXPECT_REJECT(W32(g_bmpOther + 58, 0x000000f0u), NATIVE_CAPTURE_BMP_ERR_MASKS); /* overlaps R */
	EXPECT_REJECT(W32(g_bmpOther + 62, 0x00ff00ffu), NATIVE_CAPTURE_BMP_ERR_MASKS); /* not contiguous */
	EXPECT_REJECT((W32(g_bmpOther + 54, 0x0000000fu), W32(g_bmpOther + 58, 0x000000f0u), W32(g_bmpOther + 62, 0xffffff00u)),
	              NATIVE_CAPTURE_BMP_ERR_MASKS); /* 24-bit blue: wider than 16 */
	EXPECT_REJECT((W32(g_bmpOther + 54, 0x0000000fu), W32(g_bmpOther + 58, 0x000000f0u), W32(g_bmpOther + 62, 0x00ffff00u)),
	              NATIVE_CAPTURE_BMP_OK);                                    /* 16-bit blue is still accepted */
	EXPECT_REJECT(W32(g_bmpOther + 10, 54u), NATIVE_CAPTURE_BMP_ERR_HEADER); /* table overlaps pixels */
	size = Encode(g_bmp, 9u, 3u, ENC_BITFIELDS124, 1, 0u);
	EXPECT_REJECT(W32(g_bmpOther + 54, 0u), NATIVE_CAPTURE_BMP_ERR_MASKS);
#undef EXPECT_REJECT

	/* A parse that fails never writes the output view. */
	image = untouched;
	CHECK(NativeCaptureCheck_ParseBmp(g_bmp, 10u, &image) == NATIVE_CAPTURE_BMP_ERR_TRUNCATED);
	CHECK(memcmp(&image, &untouched, sizeof(image)) == 0);
	CHECK(NativeCaptureCheck_BmpStatusName(NATIVE_CAPTURE_BMP_ERR_MASKS) != NULL);
	return 0;
}

static int TestRunArguments(void)
{
	struct NativeCaptureImage image;
	struct NativeCaptureReport report;
	size_t size;

	memset(g_canvas, 0x40, sizeof(g_canvas));
	size = Encode(g_bmp, 511u, 216u, ENC_RGB32, 0, 0u);
	CHECK(NativeCaptureCheck_ParseBmp(g_bmp, size, &image) == NATIVE_CAPTURE_BMP_OK);
	CHECK(!NativeCaptureCheck_Run(&image, NATIVE_CAPTURE_SCREEN_LOBBY, &report)); /* below 512x216 */
	size = Encode(g_bmp, 512u, 216u, ENC_RGB32, 0, 0u);
	CHECK(NativeCaptureCheck_ParseBmp(g_bmp, size, &image) == NATIVE_CAPTURE_BMP_OK);
	CHECK(NativeCaptureCheck_Run(&image, NATIVE_CAPTURE_SCREEN_LOBBY, &report));
	CHECK(!report.passed); /* flat grey: no panel */
	CHECK(!NativeCaptureCheck_Run(&image, NATIVE_CAPTURE_SCREEN_COUNT, &report));
	CHECK(!NativeCaptureCheck_Run(NULL, NATIVE_CAPTURE_SCREEN_LOBBY, &report));
	CHECK(!NativeCaptureCheck_Run(&image, NATIVE_CAPTURE_SCREEN_LOBBY, NULL));
	return 0;
}

/* The same frame gives an identical report in every encoding and with alpha
 * 0 or 255 (the capture's alpha is the PS1 mask bit, not coverage). */
static int TestEncodingInvariance(enum Frame frame, enum NativeCaptureScreen screen, struct NativeCaptureReport *out)
{
	static const enum Encoding encodings[] = {ENC_RGB32, ENC_RGB24, ENC_BITFIELDS40, ENC_BITFIELDS124};
	struct NativeCaptureReport reference;
	struct NativeCaptureReport report;
	unsigned e;
	int topDown;
	unsigned alpha;
	size_t size;

	PaintFrame(frame);
	size = Encode(g_bmp, W, H, ENC_RGB32, 0, 0u);
	CHECK(RunOn(g_bmp, size, screen, &reference));
	for (e = 0u; e < sizeof(encodings) / sizeof(encodings[0]); e++)
	{
		for (topDown = 0; topDown <= 1; topDown++)
		{
			for (alpha = 0u; alpha <= 0xffu; alpha += 0xffu)
			{
				size = Encode(g_bmp, W, H, encodings[e], topDown, (uint8_t)alpha);
				CHECK(RunOn(g_bmp, size, screen, &report));
				CHECK(memcmp(&report, &reference, sizeof(report)) == 0);
			}
		}
	}
	*out = reference;
	return 0;
}

static int FrameReport(enum Frame frame, enum NativeCaptureScreen screen, struct NativeCaptureReport *out)
{
	size_t size;

	PaintFrame(frame);
	size = Encode(g_bmp, W, H, ENC_RGB32, 0, 0u);
	CHECK(RunOn(g_bmp, size, screen, out));
	return 0;
}

static int TestVerdicts(void)
{
	struct NativeCaptureReport report;
	unsigned i;

	/* Good lobby-type frame. */
	CHECK(TestEncodingInvariance(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_LOBBY, &report) == 0);
	CHECK(report.passed && report.failedMask == 0u && report.firstFailed == NATIVE_CAPTURE_CHECK_COUNT);
	CHECK(report.screen == (uint32_t)NATIVE_CAPTURE_SCREEN_LOBBY);
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_LOBBY_CONNECTING, &report) == 0 && report.passed);
	/* The title screen's blinking body1 is optional: reported, never judged. */
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_TITLE, &report) == 0);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_BODY1].expect == NATIVE_CAPTURE_EXPECT_INFO);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_BODY1].passed);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_BODY1].measured > 0);
	CHECK(!report.passed && report.failedMask == (1u << NATIVE_CAPTURE_CHECK_TEXT_FOOTER));

	/* Good results-type frames, orange and red titles. */
	CHECK(TestEncodingInvariance(FRAME_RESULTS, NATIVE_CAPTURE_SCREEN_RESULTS, &report) == 0);
	CHECK(report.passed && report.failedMask == 0u);
	CHECK(TestEncodingInvariance(FRAME_RESULTS_RED, NATIVE_CAPTURE_SCREEN_RESULTS_LINK_ERROR, &report) == 0);
	CHECK(report.passed);
	CHECK(FrameReport(FRAME_RESULTS_RED, NATIVE_CAPTURE_SCREEN_RESULTS_DESYNC, &report) == 0 && report.passed);

	/* Results without the highlight fail on exactly that check. */
	CHECK(FrameReport(FRAME_RESULTS_NO_HIGHLIGHT, NATIVE_CAPTURE_SCREEN_RESULTS, &report) == 0);
	CHECK(!report.passed);
	CHECK(report.failedMask == (1u << NATIVE_CAPTURE_CHECK_HIGHLIGHT));
	CHECK(report.firstFailed == NATIVE_CAPTURE_CHECK_HIGHLIGHT);

	/* A panel without text fails the required title first. */
	CHECK(FrameReport(FRAME_PANEL_NO_TEXT, NATIVE_CAPTURE_SCREEN_LOBBY, &report) == 0);
	CHECK(!report.passed && report.firstFailed == NATIVE_CAPTURE_CHECK_TEXT_TITLE);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_PANEL_DIM].passed);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_PANEL_DETAIL].passed);
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; i++)
	{
		CHECK(FrameReport(FRAME_PANEL_NO_TEXT, (enum NativeCaptureScreen)i, &report) == 0 && !report.passed);
	}

	/* A solid white (opaque) panel fails the panel and text checks. */
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; i++)
	{
		CHECK(FrameReport(FRAME_WHITE_PANEL, (enum NativeCaptureScreen)i, &report) == 0);
		CHECK(!report.passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_DIM].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_DETAIL].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_TEXT_TITLE].passed);
	}

	/* Text over the undimmed scene (no panel) fails for every screen. */
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; i++)
	{
		CHECK(FrameReport(FRAME_NO_PANEL_LOBBY, (enum NativeCaptureScreen)i, &report) == 0);
		CHECK(!report.passed && !report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_DIM].passed);
	}

	/* Cross negatives: lines present where the screen draws none, or missing
	 * where it requires them. */
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_RESULTS, &report) == 0 && !report.passed);
	CHECK(!report.checks[NATIVE_CAPTURE_CHECK_TEXT_BODY1].passed);
	CHECK(!report.checks[NATIVE_CAPTURE_CHECK_TEXT_ROW_REMATCH].passed);
	CHECK(!report.checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT].passed);
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_EXIT, &report) == 0 && !report.passed);
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_MATCH_FOUND, &report) == 0 && !report.passed);
	CHECK(report.failedMask == (1u << NATIVE_CAPTURE_CHECK_TEXT_FOOTER));
	CHECK(FrameReport(FRAME_RESULTS, NATIVE_CAPTURE_SCREEN_LOBBY, &report) == 0 && !report.passed);
	CHECK(!report.checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT].passed);
	CHECK(FrameReport(FRAME_RESULTS, NATIVE_CAPTURE_SCREEN_REMATCH, &report) == 0 && !report.passed);
	return 0;
}

int main(void)
{
	CHECK(TestScreenNames() == 0);
	CHECK(TestPixelFormats() == 0);
	CHECK(TestMalformed() == 0);
	CHECK(TestRunArguments() == 0);
	CHECK(TestVerdicts() == 0);
	printf("native_capture_check_unit: ok\n");
	return 0;
}
