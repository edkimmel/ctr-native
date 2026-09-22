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
#endif

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif
