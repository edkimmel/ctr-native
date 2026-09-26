#ifndef PLATFORM_NATIVE_CAPTURE_CHECK_H
#define PLATFORM_NATIVE_CAPTURE_CHECK_H

#include <stddef.h>
#include <stdint.h>

/* Offline capture check for arcade-link preview frames.
 *
 * Pure analysis over a caller-owned BMP byte buffer: no allocation, no copy,
 * no dependency on the game, the video layer or the runtime.  Only RGB is
 * ever read.  The alpha byte of a 32 bpp capture carries the PS1 mask bit
 * (docs/TEXTURE_FILTER_MILESTONE.md section 5) and is always ignored.
 *
 * Geometry mirrors MAIN/MainArcadeLinkLayout.c in the 512x216 retail
 * space; every region is scaled by W/512 horizontally and H/216 vertically.
 *
 * Limits: the check tells screens apart only by where they draw text, the
 * panel and the row highlight.  It cannot tell apart screens that share a
 * band layout (the four linked results screens and results-solo-error; exit
 * and exit-opponent-left; lobby and lobby-connecting), and it does not read
 * wording, colour, the player markers, or which list entry is locked.  The
 * select checks expect the preview layout: two humans (one on select-solo,
 * which draws no footer) and the local cursor on list entry 0, so a cursor
 * elsewhere fails the highlight check.  Thresholds were calibrated on
 * 800x600 nearest-filter captures only (platform/native_capture_check.c). */

enum NativeCaptureBmpStatus
{
	NATIVE_CAPTURE_BMP_OK = 0,
	NATIVE_CAPTURE_BMP_ERR_ARGUMENT,
	NATIVE_CAPTURE_BMP_ERR_TRUNCATED,
	NATIVE_CAPTURE_BMP_ERR_SIGNATURE,
	NATIVE_CAPTURE_BMP_ERR_HEADER,
	NATIVE_CAPTURE_BMP_ERR_DIMENSIONS,
	NATIVE_CAPTURE_BMP_ERR_FORMAT,
	NATIVE_CAPTURE_BMP_ERR_MASKS
};

/* A parsed view into the caller's buffer.  The buffer must outlive the view. */
struct NativeCaptureImage
{
	const uint8_t *pixels; /* first stored row */
	uint32_t width;
	uint32_t height;
	uint32_t bytesPerPixel; /* 3 or 4 */
	size_t stride;          /* bytes per stored row, 4-byte aligned */
	uint32_t topDown;       /* 1 when row 0 is stored first */
	uint32_t mask[3];       /* R, G, B channel masks (alpha never used) */
	uint32_t shift[3];
	uint32_t bits[3];
};

struct NativeCaptureRgb
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

/* Uncompressed BI_RGB (24 or 32 bpp) or BI_BITFIELDS (32 bpp), info header
 * size >= 40, bottom-up or top-down.  Anything else, or any truncation, is
 * rejected without reading out of bounds.  `out` is written only on OK. */
enum NativeCaptureBmpStatus NativeCaptureCheck_ParseBmp(const uint8_t *data, size_t size, struct NativeCaptureImage *out);

const char *NativeCaptureCheck_BmpStatusName(enum NativeCaptureBmpStatus status);

/* (x, y) with y = 0 at the top of the displayed image.  Out-of-range
 * coordinates return black. */
struct NativeCaptureRgb NativeCaptureCheck_GetRgb(const struct NativeCaptureImage *image, uint32_t x, uint32_t y);

enum NativeCaptureScreen
{
	NATIVE_CAPTURE_SCREEN_TITLE = 0,
	NATIVE_CAPTURE_SCREEN_LOBBY,
	NATIVE_CAPTURE_SCREEN_LOBBY_CONNECTING,
	NATIVE_CAPTURE_SCREEN_LOBBY_REJECTED,
	NATIVE_CAPTURE_SCREEN_MATCH_FOUND,
	NATIVE_CAPTURE_SCREEN_RESULTS,
	NATIVE_CAPTURE_SCREEN_RESULTS_TIMEOUT,
	NATIVE_CAPTURE_SCREEN_RESULTS_DESYNC,
	NATIVE_CAPTURE_SCREEN_RESULTS_LINK_ERROR,
	NATIVE_CAPTURE_SCREEN_REMATCH,
	NATIVE_CAPTURE_SCREEN_EXIT,
	NATIVE_CAPTURE_SCREEN_EXIT_OPPONENT_LEFT,
	/* Match select: the widened select panel (x 4..508). */
	NATIVE_CAPTURE_SCREEN_SELECT_CHARACTER,
	NATIVE_CAPTURE_SCREEN_SELECT_TRACK,
	NATIVE_CAPTURE_SCREEN_SELECT_LAPS,
	NATIVE_CAPTURE_SCREEN_SELECT_WAIT,
	NATIVE_CAPTURE_SCREEN_SELECT_RESULT,
	/* Solo (docs/SOLO_CAB_MILESTONE.md SOLO-S3): select-solo on the select
	 * panel, the other three on the shared panel. */
	NATIVE_CAPTURE_SCREEN_LOBBY_SOLO,
	NATIVE_CAPTURE_SCREEN_SELECT_SOLO,
	NATIVE_CAPTURE_SCREEN_RESULTS_SOLO,
	NATIVE_CAPTURE_SCREEN_RESULTS_SOLO_ERROR,
	NATIVE_CAPTURE_SCREEN_COUNT
};

/* Preview names as accepted by --arcade-link-preview. */
const char *NativeCaptureCheck_ScreenName(enum NativeCaptureScreen screen);
int NativeCaptureCheck_ScreenFromName(const char *name, enum NativeCaptureScreen *out);

enum NativeCaptureCheckId
{
	NATIVE_CAPTURE_CHECK_PANEL_FRAME = 0, /* grey frame brighter than the panel just inside */
	NATIVE_CAPTURE_CHECK_PANEL_DIM,       /* interior gaps carry no bright/white fill */
	NATIVE_CAPTURE_CHECK_PANEL_DETAIL,    /* the scene still shows through (not flat) */
	NATIVE_CAPTURE_CHECK_TEXT_TITLE,
	NATIVE_CAPTURE_CHECK_TEXT_BODY1,
	NATIVE_CAPTURE_CHECK_TEXT_BODY2,
	NATIVE_CAPTURE_CHECK_TEXT_ROW_REMATCH,
	NATIVE_CAPTURE_CHECK_TEXT_ROW_EXIT,
	NATIVE_CAPTURE_CHECK_TEXT_FOOTER,
	NATIVE_CAPTURE_CHECK_HIGHLIGHT, /* additive row highlight on REMATCH */
	/* Select screens only; NATIVE_CAPTURE_EXPECT_NONE on the others, as the
	 * checks above that a select screen's layout has no band for. */
	NATIVE_CAPTURE_CHECK_TEXT_TIME,        /* "TIME s" countdown line */
	NATIVE_CAPTURE_CHECK_TEXT_GRID_COL1,   /* every row of the first list column */
	NATIVE_CAPTURE_CHECK_TEXT_GRID_COL2,   /* every row of the second list column */
	NATIVE_CAPTURE_CHECK_TEXT_LINES,       /* every body line (wait, result) */
	NATIVE_CAPTURE_CHECK_TEXT_EMPTY,       /* every region the layout leaves blank */
	NATIVE_CAPTURE_CHECK_HIGHLIGHT_CURSOR, /* additive row highlight on the local cursor */
	NATIVE_CAPTURE_CHECK_COUNT
};

enum NativeCaptureExpect
{
	NATIVE_CAPTURE_EXPECT_AT_LEAST = 0, /* measured >= threshold */
	NATIVE_CAPTURE_EXPECT_AT_MOST,      /* measured <= threshold */
	NATIVE_CAPTURE_EXPECT_INFO,         /* reported only, never fails */
	NATIVE_CAPTURE_EXPECT_NONE          /* not part of this screen: not measured, never fails */
};

struct NativeCaptureSubCheck
{
	uint32_t expect;  /* enum NativeCaptureExpect */
	int32_t measured; /* unit per check, see NativeCaptureCheck_CheckUnit */
	int32_t threshold;
	uint32_t passed;
};

struct NativeCaptureReport
{
	uint32_t screen;
	uint32_t passed;
	uint32_t failedMask;  /* bit i set when checks[i] failed */
	uint32_t firstFailed; /* NATIVE_CAPTURE_CHECK_COUNT when passed */
	struct NativeCaptureSubCheck checks[NATIVE_CAPTURE_CHECK_COUNT];
};

const char *NativeCaptureCheck_CheckName(enum NativeCaptureCheckId id);
const char *NativeCaptureCheck_CheckUnit(enum NativeCaptureCheckId id);

/* Returns 0 (and leaves `out` untouched) on bad arguments or an image too
 * small to hold the retail layout; otherwise fills `out` and returns 1.
 * `out->passed` carries the verdict. */
int NativeCaptureCheck_Run(const struct NativeCaptureImage *image, enum NativeCaptureScreen screen, struct NativeCaptureReport *out);

#endif
