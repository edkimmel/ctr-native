#ifndef PLATFORM_H
#define PLATFORM_H

struct PlatformMempackArena
{
	void *base;
	void *start;
	void *endOfMemory;
	int size;
	int backingSize;
};

/* Returns 1 on success and 0 on failure. A failure has already been logged
 * with SDL_GetError() and the platform has been shut down. */
int Platform_Init(const char *title, int width, int height, int fullscreen);
void Platform_Shutdown(void);
void Platform_InitScratchpad(void);
const struct PlatformMempackArena *Platform_InitMempackArena(void);
const struct PlatformMempackArena *Platform_GetMempackArena(void);
void Platform_BeginFrame(void);
int Platform_BeginScene(void);
void Platform_EndScene(void);
void Platform_EndFrame(void);
void Platform_PresentVRAMDisplay(void);
void Platform_PinVRAMDisplayFrames(int frameCount);
void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount);
int Platform_GetVBlankCount(void);
void Platform_WaitUntilVBlank(int targetVBlank);
void Platform_PollHostEvents(void);
int Platform_PollInput(void);

/*
 * Host-local stall hold support (docs/LOCKSTEP_RACE_MILESTONE.md LR-9,
 * slice LR-S2 (a)); only the hold loop (game/MAIN/MainArcadeRaceHold.c)
 * calls them (tests/native_host_wait_isolation_test.cmake).
 *
 * Platform_HostClockUs: the host's monotonic time in microseconds.
 * Platform_HostWaitMs: sleeps about `milliseconds` of host time. It emits no
 * VBlank, runs no VSync callback, and steps no audio: unlike VSync and
 * Platform_WaitUntilVBlank it leaves the VBlank schedule, the root counter,
 * the pads, and the mixer untouched.
 * Platform_PresentVRAMDisplayBanner: Platform_PresentVRAMDisplay with `text`
 * drawn by the host over the presented image only (a bar across the middle
 * of the display area, in a built-in block font; include/platform/native_hold_banner.h).
 * The banner draw itself touches only the window framebuffer: it never
 * writes VRAM or the game's ordering table. The pinned present it rides on
 * does go through Platform_BeginScene (NativeRenderer_BeginScene), which
 * binds the main render target and clears it or reloads it from VRAM, as
 * any Platform_PresentVRAMDisplay does; the next DrawOTag rebuilds that
 * target, so the next rendered frame replaces the banner. Returns 1 when it
 * presented; 0, with nothing done, before the platform started, while a
 * scene is in progress (the displayed frame is not settled then), while a
 * pinned present the game asked for is still owed, or for a NULL text.
 * Platform_PresentVRAMDisplayBannerGlyphs (LR-S11): the same present with
 * the text in the game font, from the caller's pointer-free glyph table
 * (NULL: exactly Platform_PresentVRAMDisplayBanner). The glyphs' texels are
 * read from the CPU VRAM mirror only, and only where it is current (no GPU
 * readback, no upload, nothing written); a table the renderer refuses, or
 * texels the mirror does not hold, fall back to the block font
 * (include/platform/native_hold_banner.h).
 */
struct NativeHoldBannerGlyphs;
unsigned long long Platform_HostClockUs(void);
void Platform_HostWaitMs(unsigned int milliseconds);
int Platform_PresentVRAMDisplayBanner(const char *text);
int Platform_PresentVRAMDisplayBannerGlyphs(const char *text, const struct NativeHoldBannerGlyphs *glyphs);

#if defined(CTR_INTERNAL)
/*
 * Host-local, presentation-only frame capture. The platform layer keeps its
 * own copy of the config; passing NULL disables capture. No game, replay, or
 * canonical-state code observes this.
 */
struct NativeFrameCaptureConfig;
void Platform_SetFrameCaptureConfig(const struct NativeFrameCaptureConfig *config);

/*
 * Queues the host's normal quit (the --exit-after-frame path) and makes it
 * exit the process with exitCode. For unattended internal runs only.
 */
void Platform_RequestExit(int exitCode);
#endif

/*
 * Host-local fixed VBlank pacing, off by default. Enabled, the VBlank pacer
 * never emits a late (catch-up) VBlank: each VSync or Platform_WaitUntilVBlank
 * still waits for its own VBlank slot(s) and re-anchors the schedule when it
 * is late, so every game tick advances exactly the VBlanks it asked for,
 * whatever the host frame time (platform/native_vblank_pacing.h). Exactly two
 * host files call it (tests/native_vblank_pacing_isolation_test.cmake):
 * main.c enables it for the internal live roster proof, and the arcade-link
 * host glue (platform/native_arcade_link_host.c) enables it for a linked race
 * from its Launch frame to its Disarm frame (docs/LOCKSTEP_RACE_MILESTONE.md
 * LR-7). No game code names or observes it.
 */
void Platform_SetFixedVBlankPacing(int enabled);

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif
