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

/*
 * Host-local fixed VBlank pacing, off by default. Enabled, the VBlank pacer
 * never emits a late (catch-up) VBlank: each VSync or Platform_WaitUntilVBlank
 * still waits for its own VBlank slot(s) and re-anchors the schedule when it
 * is late, so every game tick advances exactly the VBlanks it asked for,
 * whatever the host frame time (platform/native_vblank_pacing.h). Only the
 * internal live roster proof enables it (main.c); no game code observes it.
 */
void Platform_SetFixedVBlankPacing(int enabled);
#endif

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif
