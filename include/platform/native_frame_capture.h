#ifndef PLATFORM_NATIVE_FRAME_CAPTURE_H
#define PLATFORM_NATIVE_FRAME_CAPTURE_H

/*
 * Host-local, unattended frame-capture requests.  This is presentation-only
 * state: it must never enter MatchConfig, canonical state, replay headers, or
 * any network handshake.  Capturing a frame does not change simulation,
 * replay, or canonical behaviour; it only reads back the window framebuffer
 * after the frame has already been presented.
 *
 * This module is deliberately free of OpenGL, SDL, and renderer dependencies
 * so that it can be built and unit-tested standalone.  It owns only the
 * argument parsing, the request table, and the exit decision.
 *
 * Frame numbering: frame N is the Nth completed call to Platform_EndScene,
 * 1-based, so the first completed EndScene is frame 1.  The counter advances
 * identically on every presentation path, so indices are comparable between
 * runs.
 *
 * Paths: the host performs chdir(NativeAssets_GetBaseDir()) during start-up
 * before any frame is presented, so a relative capture path resolves against
 * the asset base directory, not the shell's working directory.
 *
 * Duplicate frame numbers: the last matching request on the command line wins.
 */

#define NATIVE_FRAME_CAPTURE_MAX_REQUESTS 16
#define NATIVE_FRAME_CAPTURE_MAX_PATH     260

struct NativeFrameCaptureRequest
{
	int frame;
	char path[NATIVE_FRAME_CAPTURE_MAX_PATH];
};

struct NativeFrameCaptureConfig
{
	struct NativeFrameCaptureRequest requests[NATIVE_FRAME_CAPTURE_MAX_REQUESTS];
	int requestCount;
	int exitAfterFrame; /* 0 = never exit on a frame index */
};

void NativeFrameCapture_SetDefaults(struct NativeFrameCaptureConfig *config);

/*
 * Applies local command-line overrides transactionally.  The parser owns
 * `--capture-frame N=PATH`, `--capture-frame=N=PATH`, `--exit-after-frame N`,
 * and `--exit-after-frame=N`; unrelated options are left for their respective
 * subsystems.  Returns zero on any malformed capture argument and leaves
 * `*config` bit-for-bit unchanged in that case, including when the caller
 * pre-populated it.
 */
int NativeFrameCapture_ApplyArgs(int argc, char *argv[], struct NativeFrameCaptureConfig *config);

/*
 * Returns the destination path requested for `frame`, or NULL when no capture
 * was requested for it.  The returned pointer aliases `config`.
 */
const char *NativeFrameCapture_PathForFrame(const struct NativeFrameCaptureConfig *config, int frame);

/*
 * Non-zero when the host should quit after completing `frame`.  The semantic
 * is "at or after": any frame index >= exitAfterFrame reports true, so a host
 * that never observes the exact index still terminates.
 */
int NativeFrameCapture_ShouldExitAfterFrame(const struct NativeFrameCaptureConfig *config, int frame);

#endif
