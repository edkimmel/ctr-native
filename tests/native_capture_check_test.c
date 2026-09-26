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

/* The select panel: the same fill and frame, widened to x=4 w=504. */
static void PaintSelectPanel(void)
{
	uint32_t x;
	uint32_t y;

	for (y = Sy(28u); y < Sy(204u); y++)
	{
		for (x = Sx(4u); x < Sx(508u); x++)
		{
			g_canvas[y][x][0] = (uint8_t)(g_canvas[y][x][0] / 2u);
			g_canvas[y][x][1] = (uint8_t)(g_canvas[y][x][1] / 2u);
			g_canvas[y][x][2] = (uint8_t)(g_canvas[y][x][2] / 2u);
		}
	}
	FillRetail(4u, 28u, 508u, 30u, 0xdeu, 0xffu, 0xffu);
	FillRetail(4u, 202u, 508u, 204u, 0xdeu, 0xffu, 0xffu);
	FillRetail(4u, 28u, 7u, 204u, 0xdeu, 0xffu, 0xffu);
	FillRetail(505u, 28u, 508u, 204u, 0xdeu, 0xffu, 0xffu);
}

/* Additive row highlight over a retail rectangle. */
static void PaintHighlightRect(uint32_t rx0, uint32_t ry0, uint32_t rx1, uint32_t ry1)
{
	uint32_t x;
	uint32_t y;

	for (y = Sy(ry0); y < Sy(ry1); y++)
	{
		for (x = Sx(rx0); x < Sx(rx1); x++)
		{
			const uint32_t r = g_canvas[y][x][0] + 0x7cu;
			const uint32_t g = g_canvas[y][x][1] + 0x5bu;
			g_canvas[y][x][0] = (uint8_t)(r > 0xffu ? 0xffu : r);
			g_canvas[y][x][1] = (uint8_t)(g > 0xffu ? 0xffu : g);
		}
	}
}

/* Additive REMATCH highlight at x=136 y=117 w=240 h=21. */
static void PaintHighlight(void)
{
	PaintHighlightRect(136u, 117u, 376u, 138u);
}

/* A text line of `chars` glyph cells centred on retail x=cx: black
 * outline, coloured fill. */
static void PaintLineAt(uint32_t cx, uint32_t y, uint32_t chars, int big, uint8_t r, uint8_t g, uint8_t b)
{
	const uint32_t cw = big ? 17u : 13u;
	const uint32_t ch = big ? 17u : 8u;
	const uint32_t x0 = cx - (chars * cw) / 2u;
	uint32_t i;

	for (i = 0u; i < chars; i++)
	{
		const uint32_t gx = x0 + i * cw;
		FillRetail(gx + 1u, y, gx + cw - 1u, y + ch, 0x00u, 0x00u, 0x00u);
		FillRetail(gx + 3u, y + 1u, gx + cw - 3u, y + ch - 1u, r, g, b);
	}
}

/* A text line centred on the screen. */
static void PaintLine(uint32_t y, uint32_t chars, int big, uint8_t r, uint8_t g, uint8_t b)
{
	PaintLineAt(256u, y, chars, big, r, g, b);
}

#define ORANGE 0xffu, 0x73u, 0x00u
#define RED    0xc6u, 0x00u, 0x00u
#define WHITE  0xffu, 0xffu, 0xffu
#define BLUE   0x10u, 0x40u, 0xffu

enum Frame
{
	FRAME_LOBBY,
	FRAME_RESULTS,
	FRAME_RESULTS_RED,
	FRAME_RESULTS_NO_HIGHLIGHT,
	FRAME_PANEL_NO_TEXT,
	FRAME_WHITE_PANEL,
	FRAME_NO_PANEL_LOBBY,
	/* Solo (SOLO-S3): the offer's prompt on the EXIT row; solo RESULTS
	 * with the "OTHER CABINET IS READY" notice on body1. */
	FRAME_LOBBY_SOLO,
	FRAME_RESULTS_SOLO
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
	case FRAME_LOBBY_SOLO:
	{
		PaintLine(40u, 11u, 1, ORANGE);  /* ARCADE LINK */
		PaintLine(90u, 25u, 0, WHITE);   /* WAITING FOR OTHER CABINET */
		PaintLine(110u, 19u, 0, WHITE);  /* THIS CABINET: CAB 1 */
		PaintLine(145u, 24u, 0, ORANGE); /* PRESS START TO RACE SOLO */
		PaintLine(186u, 14u, 0, ORANGE); /* TRIANGLE: BACK */
		break;
	}
	case FRAME_RESULTS_SOLO:
	{
		PaintHighlight();
		PaintLine(40u, 13u, 1, ORANGE);  /* RACE COMPLETE */
		PaintLine(90u, 22u, 0, WHITE);   /* OTHER CABINET IS READY */
		PaintLine(120u, 10u, 1, ORANGE); /* RACE AGAIN */
		PaintLine(145u, 5u, 1, ORANGE);  /* LOBBY */
		PaintLine(186u, 13u, 0, ORANGE); /* CROSS: SELECT */
		break;
	}
	default:
	{
		break;
	}
	}
}

/* A select screen as MainArcadeLinkLayout.c draws its preview (local cursor
 * on list entry 0), optionally with one flaw. */
enum SelectFlaw
{
	SEL_OK = 0,
	SEL_NO_PANEL,       /* text over the undimmed scene */
	SEL_WHITE_PANEL,    /* opaque white select panel, no text */
	SEL_NO_HIGHLIGHT,   /* picking screens: the cursor highlight is missing */
	SEL_HIGHLIGHT,      /* wait, result: a stray highlight on the character cursor */
	SEL_DROP_REQUIRED,  /* one grid row or body line is missing */
	SEL_DROP_TIME,      /* picking screens: the countdown is missing */
	SEL_TEXT_IN_EMPTY,  /* a short line where the layout draws nothing */
	SEL_FOOTER_ON_WAIT, /* wait, select-solo: a footer line neither screen draws */
	SEL_FLAW_COUNT
};

static void PaintSelect(enum NativeCaptureScreen screen, enum SelectFlaw flaw)
{
	uint32_t r;

	PaintScene();
	if (flaw != SEL_NO_PANEL)
		PaintSelectPanel();
	if (flaw == SEL_WHITE_PANEL)
	{
		FillRetail(4u, 28u, 508u, 204u, 0xffu, 0xffu, 0xffu);
		return;
	}
	switch (screen)
	{
	case NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER:
	{
		if (flaw != SEL_NO_HIGHLIGHT)
			PaintHighlightRect(8u, 75u, 252u, 96u);
		PaintLine(40u, 16u, 1, ORANGE); /* SELECT CHARACTER */
		if (flaw != SEL_DROP_TIME)
			PaintLine(60u, 7u, 0, WHITE); /* TIME 18 */
		for (r = 0u; r < 4u; r++)
		{
			PaintLineAt(130u, 78u + 24u * r, 5u, 1, ORANGE);
			if ((flaw != SEL_DROP_REQUIRED) || (r != 3u))
				PaintLineAt(382u, 78u + 24u * r, 6u, 1, ORANGE);
		}
		PaintLineAt(25u, 78u, 2u, 1, BLUE);  /* P1 on CRASH */
		PaintLineAt(235u, 126u, 2u, 1, RED); /* P2 on TINY */
		PaintLine(186u, 22u, 0, RED);        /* P2: CHOOSING CHARACTER */
		if (flaw == SEL_TEXT_IN_EMPTY)
			PaintLine(172u, 3u, 0, WHITE);
		break;
	}
	case NATIVE_CAPTURE_SCREEN_SELECT_SOLO:
	{
		/* The character screen for one human: no opponent, no footer. */
		if (flaw != SEL_NO_HIGHLIGHT)
			PaintHighlightRect(8u, 75u, 252u, 96u);
		PaintLine(40u, 16u, 1, ORANGE); /* SELECT CHARACTER */
		if (flaw != SEL_DROP_TIME)
			PaintLine(60u, 7u, 0, WHITE); /* TIME 18 */
		for (r = 0u; r < 4u; r++)
		{
			PaintLineAt(130u, 78u + 24u * r, 5u, 1, ORANGE);
			if ((flaw != SEL_DROP_REQUIRED) || (r != 3u))
				PaintLineAt(382u, 78u + 24u * r, 6u, 1, ORANGE);
		}
		PaintLineAt(25u, 78u, 2u, 1, BLUE); /* P1 on CRASH */
		if (flaw == SEL_TEXT_IN_EMPTY)
			PaintLine(172u, 3u, 0, WHITE);
		if (flaw == SEL_FOOTER_ON_WAIT)
			PaintLine(186u, 9u, 0, ORANGE);
		break;
	}
	case NATIVE_CAPTURE_SCREEN_SELECT_TRACK:
	{
		if (flaw != SEL_NO_HIGHLIGHT)
			PaintHighlightRect(8u, 74u, 252u, 86u);
		PaintLine(40u, 10u, 1, ORANGE); /* VOTE TRACK */
		if (flaw != SEL_DROP_TIME)
			PaintLine(60u, 7u, 0, WHITE);
		for (r = 0u; r < 8u; r++)
		{
			if ((flaw != SEL_DROP_REQUIRED) || (r != 5u))
				PaintLineAt(130u, 76u + 13u * r, 11u, 0, ORANGE);
			PaintLineAt(382u, 76u + 13u * r, 12u, 0, ORANGE);
		}
		PaintLineAt(25u, 76u, 2u, 0, BLUE);  /* P1 on CRASH COVE */
		PaintLineAt(239u, 102u, 2u, 0, RED); /* P2 on TIGER TEMPLE */
		PaintLine(186u, 16u, 0, RED);        /* P2: VOTING TRACK */
		if (flaw == SEL_TEXT_IN_EMPTY)
			PaintLine(177u, 3u, 0, WHITE);
		break;
	}
	case NATIVE_CAPTURE_SCREEN_SELECT_LAPS:
	{
		if (flaw != SEL_NO_HIGHLIGHT)
			PaintHighlightRect(134u, 83u, 378u, 104u);
		PaintLine(40u, 9u, 1, ORANGE); /* VOTE LAPS */
		if (flaw != SEL_DROP_TIME)
			PaintLine(60u, 7u, 0, WHITE);
		for (r = 0u; r < 3u; r++)
		{
			if ((flaw != SEL_DROP_REQUIRED) || (r != 2u))
				PaintLine(86u + 25u * r, 6u, 1, ORANGE); /* 3 LAPS */
		}
		PaintLineAt(151u, 86u, 2u, 1, BLUE); /* P1 on 3 LAPS */
		PaintLineAt(361u, 136u, 2u, 1, RED); /* P2 on 7 LAPS */
		PaintLine(186u, 15u, 0, RED);        /* P2: VOTING LAPS */
		if (flaw == SEL_TEXT_IN_EMPTY)
			PaintLineAt(60u, 90u, 3u, 0, WHITE); /* beside the laps column */
		break;
	}
	case NATIVE_CAPTURE_SCREEN_SELECT_WAIT:
	{
		if (flaw == SEL_HIGHLIGHT)
			PaintHighlightRect(8u, 75u, 252u, 96u);
		PaintLine(40u, 14u, 1, ORANGE); /* WAITING FOR P2 */
		PaintLine(68u, 21u, 0, WHITE);  /* YOUR CHARACTER: CRASH */
		PaintLine(80u, 29u, 0, WHITE);  /* YOUR TRACK VOTE: CRASH COVE */
		if (flaw != SEL_DROP_REQUIRED)
			PaintLine(92u, 22u, 0, WHITE); /* YOUR LAP VOTE: 3 LAPS */
		PaintLine(116u, 23u, 0, RED);      /* P2: VOTING LAPS  7 LAPS */
		if (flaw == SEL_TEXT_IN_EMPTY)
			PaintLine(150u, 3u, 0, WHITE);
		if (flaw == SEL_FOOTER_ON_WAIT)
			PaintLine(186u, 9u, 0, ORANGE);
		break;
	}
	case NATIVE_CAPTURE_SCREEN_SELECT_RESULT:
	{
		if (flaw == SEL_HIGHLIGHT)
			PaintHighlightRect(8u, 75u, 252u, 96u);
		PaintLine(40u, 9u, 1, ORANGE); /* MATCH SET */
		PaintLine(66u, 27u, 0, WHITE); /* TRACK TIGER TEMPLE - RANDOM */
		if (flaw != SEL_DROP_REQUIRED)
			PaintLine(80u, 6u, 0, WHITE); /* LAPS 3 */
		PaintLine(100u, 8u, 0, BLUE);     /* P1 CRASH */
		PaintLine(112u, 9u, 0, RED);      /* P2 CORTEX */
		PaintLine(130u, 30u, 0, WHITE);   /* CPU POLAR, N. GIN, TINY, COCO */
		PaintLine(186u, 9u, 0, ORANGE);   /* GET READY */
		if (flaw == SEL_TEXT_IN_EMPTY)
			PaintLine(160u, 3u, 0, WHITE);
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
	static const char *const names[] = {
	    "title",   "lobby", "lobby-connecting",   "lobby-rejected",   "match-found",  "results",     "results-timeout", "results-desync", "results-link-error",
	    "rematch", "exit",  "exit-opponent-left", "select-character", "select-track", "select-laps", "select-wait",     "select-result",
	    "lobby-solo", "select-solo", "results-solo", "results-solo-error"};
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
	CHECK(!NativeCaptureCheck_ScreenFromName("select", &screen));
	CHECK(!NativeCaptureCheck_ScreenFromName("select-characters", &screen));
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

/* ------------------------------------------------------------------------ */
/* Select screens                                                           */
/* ------------------------------------------------------------------------ */

#define SELECT_SCREEN_COUNT 6u

static const enum NativeCaptureScreen g_selectScreens[SELECT_SCREEN_COUNT] = {NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER, NATIVE_CAPTURE_SCREEN_SELECT_TRACK,
                                                                              NATIVE_CAPTURE_SCREEN_SELECT_LAPS, NATIVE_CAPTURE_SCREEN_SELECT_WAIT,
                                                                              NATIVE_CAPTURE_SCREEN_SELECT_RESULT,    NATIVE_CAPTURE_SCREEN_SELECT_SOLO};

static int IsSelectScreen(enum NativeCaptureScreen screen)
{
	unsigned s;

	for (s = 0u; s < SELECT_SCREEN_COUNT; s++)
	{
		if (g_selectScreens[s] == screen)
			return 1;
	}
	return 0;
}

static int IsPickingScreen(enum NativeCaptureScreen screen)
{
	return (screen == NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER) || (screen == NATIVE_CAPTURE_SCREEN_SELECT_TRACK) ||
	       (screen == NATIVE_CAPTURE_SCREEN_SELECT_LAPS) || (screen == NATIVE_CAPTURE_SCREEN_SELECT_SOLO);
}

static int SelectReport(enum NativeCaptureScreen painted, enum SelectFlaw flaw, enum NativeCaptureScreen screen, struct NativeCaptureReport *out)
{
	size_t size;

	PaintSelect(painted, flaw);
	size = Encode(g_bmp, W, H, ENC_RGB32, 0, 0u);
	CHECK(RunOn(g_bmp, size, screen, out));
	return 0;
}

/* Every check a screen judges, and only those, is present in its report. */
static uint32_t JudgedMask(const struct NativeCaptureReport *report)
{
	uint32_t mask = 0u;
	unsigned i;

	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_CHECK_COUNT; i++)
	{
		if (report->checks[i].expect != NATIVE_CAPTURE_EXPECT_NONE)
			mask |= 1u << i;
	}
	return mask;
}

#define BIT(id) (1u << (id))

static int TestSelectVerdicts(void)
{
	static const enum Encoding encodings[] = {ENC_RGB32, ENC_RGB24, ENC_BITFIELDS40, ENC_BITFIELDS124};
	const uint32_t panel = BIT(NATIVE_CAPTURE_CHECK_PANEL_FRAME) | BIT(NATIVE_CAPTURE_CHECK_PANEL_DIM) | BIT(NATIVE_CAPTURE_CHECK_PANEL_DETAIL);
	const uint32_t picking = panel | BIT(NATIVE_CAPTURE_CHECK_TEXT_TITLE) | BIT(NATIVE_CAPTURE_CHECK_TEXT_TIME) | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1) |
	                         BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER) | BIT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY) | BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR);
	const uint32_t expectJudged[SELECT_SCREEN_COUNT] = {
	    picking | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2),
	    picking | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2),
	    picking,
	    panel | BIT(NATIVE_CAPTURE_CHECK_TEXT_TITLE) | BIT(NATIVE_CAPTURE_CHECK_TEXT_LINES) | BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER) |
	        BIT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY) | BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR),
	    panel | BIT(NATIVE_CAPTURE_CHECK_TEXT_TITLE) | BIT(NATIVE_CAPTURE_CHECK_TEXT_LINES) | BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER) |
	        BIT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY) | BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR),
	    /* select-solo judges its footer too, as absent. */
	    picking | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2),
	};
	/* The check a dropped grid row or body line fails. */
	const uint32_t dropFails[SELECT_SCREEN_COUNT] = {BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2), BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1),
	                                                 BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1), BIT(NATIVE_CAPTURE_CHECK_TEXT_LINES),
	                                                 BIT(NATIVE_CAPTURE_CHECK_TEXT_LINES),     BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2)};
	struct NativeCaptureReport reference;
	struct NativeCaptureReport report;
	unsigned s;
	unsigned t;
	unsigned e;
	int topDown;
	unsigned alpha;
	size_t size;

	for (s = 0u; s < SELECT_SCREEN_COUNT; s++)
	{
		const enum NativeCaptureScreen screen = g_selectScreens[s];

		/* The preview frame passes, identically in every encoding. */
		CHECK(SelectReport(screen, SEL_OK, screen, &reference) == 0);
		CHECK(reference.passed && reference.failedMask == 0u && reference.firstFailed == NATIVE_CAPTURE_CHECK_COUNT);
		CHECK(reference.screen == (uint32_t)screen);
		CHECK(JudgedMask(&reference) == expectJudged[s]);
		CHECK(reference.checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR].expect ==
		      (IsPickingScreen(screen) ? NATIVE_CAPTURE_EXPECT_AT_LEAST : NATIVE_CAPTURE_EXPECT_AT_MOST));
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

		/* Missing panel: the text sits on the undimmed scene. */
		CHECK(SelectReport(screen, SEL_NO_PANEL, screen, &report) == 0);
		CHECK(!report.passed && report.firstFailed == NATIVE_CAPTURE_CHECK_PANEL_FRAME);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_DIM].passed);

		/* An opaque white select panel fails the panel and title checks. */
		CHECK(SelectReport(screen, SEL_WHITE_PANEL, screen, &report) == 0);
		CHECK(!report.passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_DIM].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_PANEL_DETAIL].passed);
		CHECK(!report.checks[NATIVE_CAPTURE_CHECK_TEXT_TITLE].passed);

		/* The local cursor highlight: required on the picking screens, and
		 * must be absent on wait and result.  Each fails on exactly that. */
		if (IsPickingScreen(screen))
		{
			CHECK(SelectReport(screen, SEL_NO_HIGHLIGHT, screen, &report) == 0);
			CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR));

			CHECK(SelectReport(screen, SEL_DROP_TIME, screen, &report) == 0);
			CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_TIME));
		}
		else
		{
			CHECK(SelectReport(screen, SEL_HIGHLIGHT, screen, &report) == 0);
			CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR));
		}

		/* A missing grid row or body line fails that region's check alone,
		 * though every other row or line is present. */
		CHECK(SelectReport(screen, SEL_DROP_REQUIRED, screen, &report) == 0);
		CHECK(!report.passed && report.failedMask == dropFails[s]);

		/* A short line where the layout draws nothing fails text-empty. */
		CHECK(SelectReport(screen, SEL_TEXT_IN_EMPTY, screen, &report) == 0);
		CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY));

		/* Cross negatives: each select frame fails as every other select
		 * screen, and as every shared-panel screen (the frame is elsewhere). */
		for (t = 0u; t < SELECT_SCREEN_COUNT; t++)
		{
			if (t == s)
				continue;
			CHECK(SelectReport(screen, SEL_OK, g_selectScreens[t], &report) == 0);
			CHECK(!report.passed);
		}
		for (t = 0u; t < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; t++)
		{
			if (IsSelectScreen((enum NativeCaptureScreen)t))
				continue;
			CHECK(SelectReport(screen, SEL_OK, (enum NativeCaptureScreen)t, &report) == 0);
			CHECK(!report.passed && !report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
		}
	}

	/* The wait screen draws no footer, and neither does select-solo. */
	CHECK(SelectReport(NATIVE_CAPTURE_SCREEN_SELECT_WAIT, SEL_FOOTER_ON_WAIT, NATIVE_CAPTURE_SCREEN_SELECT_WAIT, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER));
	CHECK(SelectReport(NATIVE_CAPTURE_SCREEN_SELECT_SOLO, SEL_FOOTER_ON_WAIT, NATIVE_CAPTURE_SCREEN_SELECT_SOLO, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER));
	/* select-character and select-solo differ in the footer alone. */
	CHECK(SelectReport(NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER, SEL_OK, NATIVE_CAPTURE_SCREEN_SELECT_SOLO, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER));
	CHECK(SelectReport(NATIVE_CAPTURE_SCREEN_SELECT_SOLO, SEL_OK, NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_FOOTER));

	/* Shared-panel frames fail as every select screen on the panel frame,
	 * and their reports judge none of the select-only checks. */
	for (s = 0u; s < SELECT_SCREEN_COUNT; s++)
	{
		CHECK(FrameReport(FRAME_LOBBY, g_selectScreens[s], &report) == 0);
		CHECK(!report.passed && !report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
		CHECK(FrameReport(FRAME_RESULTS, g_selectScreens[s], &report) == 0);
		CHECK(!report.passed && !report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
	}
	CHECK(FrameReport(FRAME_RESULTS, NATIVE_CAPTURE_SCREEN_RESULTS, &report) == 0 && report.passed);
	CHECK((JudgedMask(&report) & (BIT(NATIVE_CAPTURE_CHECK_TEXT_TIME) | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1) | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2) |
	                              BIT(NATIVE_CAPTURE_CHECK_TEXT_LINES) | BIT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY) | BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR))) ==
	      0u);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1].passed && report.checks[NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1].measured == 0);
	return 0;
}

/* The shared-panel solo screens (SOLO-S3): each solo frame passes as its
 * own screen in every encoding and fails its linked counterpart on exactly
 * the band that tells them apart, and the other way round. */
static int TestSoloVerdicts(void)
{
	struct NativeCaptureReport report;
	unsigned s;

	/* lobby-solo: the prompt on the EXIT row band. */
	CHECK(TestEncodingInvariance(FRAME_LOBBY_SOLO, NATIVE_CAPTURE_SCREEN_LOBBY_SOLO, &report) == 0);
	CHECK(report.passed && report.failedMask == 0u && report.firstFailed == NATIVE_CAPTURE_CHECK_COUNT);
	CHECK(report.screen == (uint32_t)NATIVE_CAPTURE_SCREEN_LOBBY_SOLO);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_ROW_REMATCH].expect == NATIVE_CAPTURE_EXPECT_AT_MOST);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_ROW_EXIT].expect == NATIVE_CAPTURE_EXPECT_AT_LEAST);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT].expect == NATIVE_CAPTURE_EXPECT_AT_MOST);
	CHECK(FrameReport(FRAME_LOBBY_SOLO, NATIVE_CAPTURE_SCREEN_LOBBY, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_ROW_EXIT));
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_LOBBY_SOLO, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_ROW_EXIT));

	/* results-solo: the notice on body1. */
	CHECK(TestEncodingInvariance(FRAME_RESULTS_SOLO, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO, &report) == 0);
	CHECK(report.passed && report.failedMask == 0u);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_TEXT_BODY1].expect == NATIVE_CAPTURE_EXPECT_AT_LEAST);
	CHECK(report.checks[NATIVE_CAPTURE_CHECK_HIGHLIGHT].expect == NATIVE_CAPTURE_EXPECT_AT_LEAST);
	CHECK(FrameReport(FRAME_RESULTS_SOLO, NATIVE_CAPTURE_SCREEN_RESULTS, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_BODY1));
	CHECK(FrameReport(FRAME_RESULTS_SOLO, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO_ERROR, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_BODY1));
	CHECK(FrameReport(FRAME_RESULTS, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_TEXT_BODY1));

	/* results-solo-error: the linked results bands (the check reads no
	 * wording, so RACE ERROR and LINK ERROR measure alike). */
	CHECK(TestEncodingInvariance(FRAME_RESULTS_RED, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO_ERROR, &report) == 0);
	CHECK(report.passed && report.failedMask == 0u);
	CHECK(FrameReport(FRAME_RESULTS_NO_HIGHLIGHT, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO_ERROR, &report) == 0);
	CHECK(!report.passed && report.failedMask == BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT));
	CHECK(FrameReport(FRAME_RESULTS_NO_HIGHLIGHT, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO, &report) == 0);
	CHECK(!report.passed && ((report.failedMask & BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT)) != 0u));

	/* A panel without text, and the lobby frame, fail every solo results
	 * screen; the solo frames fail every select screen on the panel frame
	 * and judge none of the select-only checks. */
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO, &report) == 0 && !report.passed);
	CHECK(FrameReport(FRAME_LOBBY, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO_ERROR, &report) == 0 && !report.passed);
	CHECK(FrameReport(FRAME_RESULTS_SOLO, NATIVE_CAPTURE_SCREEN_LOBBY_SOLO, &report) == 0 && !report.passed);
	CHECK(FrameReport(FRAME_LOBBY_SOLO, NATIVE_CAPTURE_SCREEN_RESULTS_SOLO, &report) == 0 && !report.passed);
	for (s = 0u; s < SELECT_SCREEN_COUNT; s++)
	{
		CHECK(FrameReport(FRAME_LOBBY_SOLO, g_selectScreens[s], &report) == 0);
		CHECK(!report.passed && !report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
		CHECK(FrameReport(FRAME_RESULTS_SOLO, g_selectScreens[s], &report) == 0);
		CHECK(!report.passed && !report.checks[NATIVE_CAPTURE_CHECK_PANEL_FRAME].passed);
	}
	CHECK(FrameReport(FRAME_LOBBY_SOLO, NATIVE_CAPTURE_SCREEN_LOBBY_SOLO, &report) == 0 && report.passed);
	CHECK((JudgedMask(&report) & (BIT(NATIVE_CAPTURE_CHECK_TEXT_TIME) | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1) | BIT(NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2) |
	                              BIT(NATIVE_CAPTURE_CHECK_TEXT_LINES) | BIT(NATIVE_CAPTURE_CHECK_TEXT_EMPTY) | BIT(NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR))) ==
	      0u);
	return 0;
}

int main(void)
{
	CHECK(TestScreenNames() == 0);
	CHECK(TestPixelFormats() == 0);
	CHECK(TestMalformed() == 0);
	CHECK(TestRunArguments() == 0);
	CHECK(TestVerdicts() == 0);
	CHECK(TestSelectVerdicts() == 0);
	CHECK(TestSoloVerdicts() == 0);
	printf("native_capture_check_unit: ok\n");
	return 0;
}
