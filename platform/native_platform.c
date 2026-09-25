#include <platform.h>

#include <macros.h>

#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_roster_proof.h"
#include "platform/native_audio.h"
#include "platform/native_frame_capture.h"
#include "platform/native_glad.h"
#include "platform/native_gpu.h"
#include "platform/native_hold_banner.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"
#include "platform/native_sdl_assert.h"
#include "platform/native_vblank_pacing.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Window *g_window = NULL;
int g_dbg_polygonSelected = 0;

extern int g_dbg_emulatorPaused;
extern int g_dbg_texturelessMode;
extern int g_dbg_wireframeMode;
extern int g_windowHeight;
extern int g_windowWidth;

#define HOST_ALT_LEFT  (1 << 0)
#define HOST_ALT_RIGHT (1 << 1)
global_variable int s_hostAltKeyState = 0;
global_variable int s_platformInitialized = 0;
global_variable int s_platformBeginScene = 0;
global_variable int s_pinnedVramDisplayFrames = 0;
global_variable int s_pinnedVramDisplayCustomRect = 0;
global_variable int s_pinnedVramDisplayX = 0;
global_variable int s_pinnedVramDisplayY = 0;
global_variable int s_pinnedVramDisplayW = 0;
global_variable int s_pinnedVramDisplayH = 0;
/* Host-local: the text of the one Platform_PresentVRAMDisplayBanner present
 * in progress; NULL otherwise. */
global_variable const char *s_presentBannerText = NULL;
/* Host-local: that present's glyph table (LR-S11; NULL: the block font),
 * and the font its draw used (NATIVE_HOLD_BANNER_FONT_*), for the log. */
global_variable const struct NativeHoldBannerGlyphs *s_presentBannerGlyphs = NULL;
global_variable u32 s_presentBannerFont = 0;
#if defined(CTR_INTERNAL)
/* Host-local presentation state only; never observed by game/replay code. */
global_variable struct NativeFrameCaptureConfig s_frameCaptureConfig;
global_variable int s_frameCaptureActive = 0;
global_variable int s_frameCaptureFrameIndex = 0;
/* The code SDL_EVENT_QUIT exits with: 0 unless Platform_RequestExit set it. */
global_variable int s_requestedExitCode = 0;
#endif
#define NATIVE_FPS_REPORT_FRAME_WINDOW 2000
global_variable int s_fpsFrameCount = 0;
global_variable u64 s_fpsLastCounter = 0;

internal void Platform_CalcFPS(void)
{
#if defined(CTR_INTERNAL)
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 now = SDL_GetPerformanceCounter();

	if (freq == 0)
	{
		return;
	}

	if (s_fpsLastCounter == 0)
	{
		s_fpsLastCounter = now;
		s_fpsFrameCount = 0;
		return;
	}

	s_fpsFrameCount++;
	if (s_fpsFrameCount < NATIVE_FPS_REPORT_FRAME_WINDOW)
	{
		return;
	}

	if (now > s_fpsLastCounter)
	{
		const f64 elapsedSeconds = (f64)(now - s_fpsLastCounter) / (f64)freq;
		const f64 fps = (f64)s_fpsFrameCount / elapsedSeconds;

		Platform_Log("[CTR Native] FPS: %.2f (last %d frames)\n", fps, s_fpsFrameCount);
	}

	s_fpsFrameCount = 0;
	s_fpsLastCounter = now;
#endif
}

internal void Platform_GetWindowName(const char *appName, char *buffer, size_t bufferSize)
{
#ifdef CTR_INTERNAL
	snprintf(buffer, bufferSize, "%s | Internal", appName);
#else
	snprintf(buffer, bufferSize, "%s", appName);
#endif
}

/* OpenGL operates in drawable pixels, which can differ from logical window
 * points on a high-DPI 4K desktop. Keep the presentation viewport, blit, and
 * screenshot dimensions in that same pixel space. */
internal void Platform_RefreshDrawableSize(void)
{
	int width = 0;
	int height = 0;

	if ((g_window == NULL) || !SDL_GetWindowSizeInPixels(g_window, &width, &height) || (width <= 0) || (height <= 0))
	{
		return;
	}

	g_windowWidth = width;
	g_windowHeight = height;
	NativeRenderer_ResetDevice();
}

internal void Platform_UpdateCursorVisibility(void)
{
	if (g_window == NULL)
	{
		return;
	}

	if ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0)
	{
		SDL_HideCursor();
	}
	else
	{
		SDL_ShowCursor();
	}
}

internal void Platform_HandleFullscreenToggle(void)
{
	int fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;

	if (!SDL_SetWindowFullscreen(g_window, fullscreen == 0))
	{
		Platform_LogWarn("[CTR Native] failed to toggle fullscreen: %s\n", SDL_GetError());
		return;
	}
	/* Fullscreen changes can be asynchronous on some backends. Synchronize so
	 * the very next presentation uses the real desktop drawable extent. */
	SDL_SyncWindow(g_window);
	Platform_RefreshDrawableSize();
	Platform_UpdateCursorVisibility();
}

internal void Platform_UpdateHostAltKeyState(const s32 key, const s8 down)
{
	s32 altKeyBit = 0;

	if (key == SDL_SCANCODE_LALT)
	{
		altKeyBit = HOST_ALT_LEFT;
	}
	else if (key == SDL_SCANCODE_RALT)
	{
		altKeyBit = HOST_ALT_RIGHT;
	}

	if (altKeyBit == 0)
	{
		return;
	}

	if (down != 0)
	{
		s_hostAltKeyState |= altKeyBit;
	}
	else
	{
		s_hostAltKeyState &= ~altKeyBit;
	}
}

#if defined(CTR_INTERNAL)
internal void Platform_TakeScreenshot(void)
{
	u8 *pixels = (u8 *)malloc(g_windowWidth * g_windowHeight * 4);

	glReadPixels(0, 0, g_windowWidth, g_windowHeight, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

	/* BGRA32 is the byte-order alias (B,G,R,A in memory) that matches GL_BGRA. */
	SDL_Surface *surface = SDL_CreateSurfaceFrom(g_windowWidth, g_windowHeight, SDL_PIXELFORMAT_BGRA32, pixels, g_windowWidth * 4);

	SDL_SaveBMP(surface, "SCREENSHOT.BMP");
	SDL_DestroySurface(surface);

	free(pixels);
}

/*
 * Unattended capture path. Unlike Platform_TakeScreenshot above, which reads
 * whatever framebuffer happens to be bound, this helper explicitly reads the
 * default (window) framebuffer and restores the previous read binding, so the
 * captured image is unambiguous across presentation paths. Failures are logged
 * and ignored: a capture must never crash the host or alter frame pacing.
 */
internal void Platform_CaptureFrameToFile(const char *path)
{
	const int width = g_windowWidth;
	const int height = g_windowHeight;
	const size_t stride = (size_t)width * 4u;
	GLint previousReadFramebuffer = 0;
	u8 *pixels = NULL;
	u8 *rowScratch = NULL;
	SDL_Surface *surface = NULL;

	if ((path == NULL) || (path[0] == '\0') || (width <= 0) || (height <= 0))
	{
		Platform_LogWarn("[CTR Native] frame capture skipped: invalid target or window size\n");
		return;
	}

	pixels = (u8 *)malloc(stride * (size_t)height);
	rowScratch = (u8 *)malloc(stride);
	if ((pixels == NULL) || (rowScratch == NULL))
	{
		Platform_LogWarn("[CTR Native] frame capture failed: out of memory for %s\n", path);
		free(pixels);
		free(rowScratch);
		return;
	}

	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previousReadFramebuffer);

	/* GL returns bottom-up rows; the BMP we want is top-down. */
	for (int row = 0; row < (height / 2); row++)
	{
		u8 *top = pixels + ((size_t)row * stride);
		u8 *bottom = pixels + ((size_t)(height - 1 - row) * stride);

		memcpy(rowScratch, top, stride);
		memcpy(top, bottom, stride);
		memcpy(bottom, rowScratch, stride);
	}

	/* BGRA32 is the byte-order alias (B,G,R,A in memory) that matches GL_BGRA;
	 * BGRA8888 is a packed-integer order and swaps R<->G on little-endian. */
	surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_BGRA32, pixels, (int)stride);
	if (surface == NULL)
	{
		Platform_LogWarn("[CTR Native] frame capture failed to wrap pixels for %s: %s\n", path, SDL_GetError());
	}
	else
	{
		if (!SDL_SaveBMP(surface, path))
		{
			Platform_LogWarn("[CTR Native] frame capture failed to write %s: %s\n", path, SDL_GetError());
		}
		else
		{
			Platform_LogWarn("[CTR Native] frame capture wrote %s (%dx%d)\n", path, width, height);
		}
		SDL_DestroySurface(surface);
	}

	free(rowScratch);
	free(pixels);
}

void Platform_SetFrameCaptureConfig(const struct NativeFrameCaptureConfig *config)
{
	if (config == NULL)
	{
		s_frameCaptureActive = 0;
		return;
	}

	s_frameCaptureConfig = *config;
	s_frameCaptureActive = ((config->requestCount > 0) || (config->exitAfterFrame > 0)) ? 1 : 0;
}

/*
 * Called after the present call and before the window swap on every
 * presentation path, so frame indices are comparable between runs. The fast
 * path when nothing was requested is a single integer compare.
 */
internal void Platform_ServiceFrameCapture(void)
{
	int frame;
	const char *path;

	s_frameCaptureFrameIndex++;
	if (!s_frameCaptureActive)
	{
		return;
	}

	frame = s_frameCaptureFrameIndex;

	path = NativeFrameCapture_PathForFrame(&s_frameCaptureConfig, frame);
	if (path != NULL)
	{
		Platform_CaptureFrameToFile(path);
	}

	if (NativeFrameCapture_ShouldExitAfterFrame(&s_frameCaptureConfig, frame))
	{
		/* The host has no quit flag: SDL_EVENT_QUIT and window-close are both
		 * handled directly in Platform_PollHostEvents. Pushing SDL_EVENT_QUIT
		 * reuses that exact shutdown path instead of calling exit() here. */
		SDL_Event quitEvent;

		Platform_LogWarn("[CTR Native] frame capture requested exit after frame %d\n", frame);
		s_frameCaptureActive = 0;
		SDL_zero(quitEvent);
		quitEvent.type = SDL_EVENT_QUIT;
		if (!SDL_PushEvent(&quitEvent))
		{
			Platform_LogWarn("[CTR Native] failed to queue quit after frame %d: %s\n", frame, SDL_GetError());
		}
	}
}

/*
 * Leaves through the same shutdown path as --exit-after-frame: it queues
 * SDL_EVENT_QUIT, which Platform_PollHostEvents turns into exit(), and makes
 * that exit use exitCode. Internal unattended runs (the roster proof) use it
 * to report their result as the process exit code.
 */
void Platform_RequestExit(int exitCode)
{
	SDL_Event quitEvent;

	s_requestedExitCode = exitCode;
	Platform_LogWarn("[CTR Native] exit requested with code %d\n", exitCode);
	SDL_zero(quitEvent);
	quitEvent.type = SDL_EVENT_QUIT;
	if (!SDL_PushEvent(&quitEvent))
	{
		Platform_LogWarn("[CTR Native] failed to queue quit with code %d: %s; exiting now\n", exitCode, SDL_GetError());
		exit(exitCode);
	}
}
#endif

internal void Platform_HandleKey(int key, char down)
{
	if (down == 0)
	{
		SubmitName_UseKeyboard(0);
	}
	else
	{
		SubmitName_UseKeyboard(key);
	}

#ifdef CTR_INTERNAL
	if (!down)
	{
		switch (key)
		{
		case SDL_SCANCODE_F1:
			g_dbg_wireframeMode ^= 1;
			Platform_LogWarn("[CTR Native] wireframe mode: %d\n", g_dbg_wireframeMode);
			break;

		case SDL_SCANCODE_F2:
			g_dbg_texturelessMode ^= 1;
			Platform_LogWarn("[CTR Native] textureless mode: %d\n", g_dbg_texturelessMode);
			break;
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
			if (g_dbg_emulatorPaused)
			{
				g_dbg_polygonSelected += (key == SDL_SCANCODE_UP) ? 3 : -3;
			}
			break;
		case SDL_SCANCODE_F9:
			if (NativeReplayScheduler_RequestStart() != 0)
			{
				break;
			}
			break;
		case SDL_SCANCODE_F10:
			NativeReplayScheduler_RequestStop();
			break;
		case SDL_SCANCODE_F7:
			Platform_LogWarn("[CTR Native] saving VRAM.TGA\n");
			NativeRenderer_SaveVRAM("VRAM.TGA", 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 1);
			break;
		case SDL_SCANCODE_F12:
			Platform_LogWarn("[CTR Native] Saving screenshot...\n");
			Platform_TakeScreenshot();
			break;
		case SDL_SCANCODE_F3:
			NativeRenderer_SetTextureFilter(NativeRenderer_GetTextureFilter() != 0 ? 0 : 1);
			Platform_LogWarn("[CTR Native] texture filter: %s\n", NativeRenderer_GetTextureFilter() != 0 ? "bilinear" : "nearest");
			break;
		/* Quick states are disabled in arcade-link link and preview mode: a
		 * checkpoint captures the retail main-menu box the layer hides, so a
		 * state saved there would leave that box invisible in a later normal
		 * run (docs/GAME_LOOP_UI_MILESTONE.md section 2.5). They are disabled
		 * the same way in the internal roster proof (docs/ROSTER_MILESTONE.md
		 * section 3.4), whose race setup state is never checkpointed. With the
		 * host mode OFF and no proof both hotkeys behave as before. */
		case SDL_SCANCODE_F5:
			if (NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)
			{
				Platform_LogWarn("[CTR Native] quick states are disabled in arcade-link mode\n");
				break;
			}
			if (NativeArcadeRosterProof_Active())
			{
				Platform_LogWarn("[CTR Native] quick states are disabled in arcade roster proof mode\n");
				break;
			}
			NativeSaveState_RequestSave();
			break;
		case SDL_SCANCODE_F8:
			if (NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)
			{
				Platform_LogWarn("[CTR Native] quick states are disabled in arcade-link mode\n");
				break;
			}
			if (NativeArcadeRosterProof_Active())
			{
				Platform_LogWarn("[CTR Native] quick states are disabled in arcade roster proof mode\n");
				break;
			}
			NativeSaveState_RequestLoad();
			break;
		}
	}
#endif
}

/* SDL assertions are reported through the platform log and then ignored, so
 * no build ever blocks on SDL's modal assertion dialog. */
internal void Platform_LogSdlAssertion(const char *line)
{
	Platform_LogError("%s", line);
}

/* Each init step clears the SDL error first, but a step can also fail for a
 * non-SDL reason (gladLoadGL, GL-only PSX setup) and leave it empty. */
internal const char *Platform_SdlErrorText(void)
{
	const char *error = SDL_GetError();
	return ((error != NULL) && (error[0] != '\0')) ? error : "no SDL error reported";
}

int Platform_Init(const char *title, int width, int height, int fullscreen)
{
	char windowName[128];

	Platform_LogInit(title);
	NativeSdlAssert_Install(Platform_LogSdlAssertion);
	Platform_GetWindowName(title, windowName, sizeof(windowName));

	Platform_Log("[CTR Native] Initialising platform\n");

	SDL_ClearError();
	if (SDL_Init(SDL_INIT_VIDEO) == 0)
	{
		/* The ignored SDL_hid.c:258 assertion leaves SDL's device-notification
		 * counter at -1.  That is safe only because startup stops here: never
		 * retry SDL_Init(SDL_INIT_VIDEO) in-process. */
		Platform_LogError("[CTR Native] Failed to initialise SDL video: %s\n", Platform_SdlErrorText());
		Platform_LogShutdown();
		return 0;
	}

	s_platformInitialized = 1;

	SDL_ClearError();
	if (!NativeRenderer_InitialiseRender(windowName, width, height, fullscreen != 0))
	{
		Platform_LogError("[CTR Native] Failed to initialise window: %s\n", Platform_SdlErrorText());
		Platform_Shutdown();
		return 0;
	}

	/* SDL fullscreen uses the desktop resolution. Preserve the renderer's
	 * fixed 4:3 presentation aspect (set from the logical 800x600 window),
	 * but update its drawable bounds before any PSX render target is created. */
	Platform_RefreshDrawableSize();

	SDL_ClearError();
	if (!NativeRenderer_InitialisePSX())
	{
		Platform_LogError("[CTR Native] Failed to initialise PSX renderer state: %s\n", Platform_SdlErrorText());
		Platform_Shutdown();
		return 0;
	}

	atexit(Platform_Shutdown);
	Platform_UpdateCursorVisibility();
	/* Input/HID initialisation is deliberately non-fatal: keyboard play must
	 * still work when no controller subsystem is available. */
	Platform_InputInit();
	return 1;
}

void Platform_Shutdown(void)
{
	if (s_platformInitialized == 0)
	{
		return;
	}

	s_platformInitialized = 0;
#if defined(CTR_INTERNAL)
	NativeRenderer_FinishGpuMeasurements();
	NativePerf_Shutdown();
	NativeReplayScheduler_Shutdown();
#endif
	Platform_InputShutdown();
	NativeAudio_Shutdown();
	NativeRenderer_Shutdown();

	if (g_window != NULL)
	{
		SDL_DestroyWindow(g_window);
		g_window = NULL;
	}

	SDL_Quit();

	Platform_LogShutdown();
}

void Platform_BeginFrame(void)
{
	// NOTE(aalhendi): Normal rendering begins from DrawOTag after the current
	// draw env is installed. Starting a host scene here clears the previous env
	// and can force the host GL driver to block before the retail render-submit path.
}

int Platform_BeginScene(void)
{
	if (s_platformBeginScene)
	{
		return 0;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	// NOTE(aalhendi): CTR already throttles through the retail VSync/draw-sync
	// path. Do not add a second SDL swap wait; some GL drivers charge that wait
	// to the next frame's first clear instead of SDL_GL_SwapWindow.
	NativeRenderer_UpdateSwapIntervalState(0);

	NativeRenderer_BeginScene();

	if (activeDrawEnv.isbg)
	{
		const RECT16 clipenv = activeDrawEnv.clip;
		const u8 r = activeDrawEnv.r0;
		const u8 g = activeDrawEnv.g0;
		const u8 b = activeDrawEnv.b0;

		NativeRenderer_Clear(clipenv.x, clipenv.y, clipenv.w, clipenv.h, r, g, b);
	}

	s_platformBeginScene = 1;

	Platform_LogFlush();

	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	return 1;
}

void Platform_EndScene(void)
{
	if (!s_platformBeginScene)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
	s_platformBeginScene = 0;

	NativeRenderer_EndScene();

	if (s_pinnedVramDisplayFrames > 0)
	{
		if (s_pinnedVramDisplayCustomRect)
		{
			NativeRenderer_PresentVRAMRect(s_pinnedVramDisplayX, s_pinnedVramDisplayY, s_pinnedVramDisplayW, s_pinnedVramDisplayH);
		}
		else
		{
			NativeRenderer_PresentVRAMDisplay();
		}
		if (s_presentBannerText != NULL)
		{
			/* Platform_PresentVRAMDisplayBanner(Glyphs): a host overlay on
			 * the presented image only, drawn before the capture reads it. */
			s_presentBannerFont = NativeRenderer_DrawPresentBanner(s_presentBannerText, s_presentBannerGlyphs);
		}
		NativeRenderer_EndGpuFrame();
#if defined(CTR_INTERNAL)
		Platform_ServiceFrameCapture();
#endif
		NativeRenderer_SwapWindow();
		s_pinnedVramDisplayFrames--;
		if (s_pinnedVramDisplayFrames <= 0)
		{
			s_pinnedVramDisplayCustomRect = 0;
		}
		NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
		return;
	}

	// NOTE(aalhendi): Keep the displayed VRAM region current for screen-copy
	// effects without forcing a CPU readback. Preserve the original packed-VRAM
	// presentation at 1x; higher scales present the high-resolution render
	// target after this pack. Pinned and VRAM-only paths above always use the
	// native VRAM presenter.
	NativeRenderer_StoreFrameBuffer(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
	if (NativeRenderer_GetRenderScale() > 1)
	{
		NativeRenderer_PresentMainRenderTarget();
	}
	else
	{
		NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
	}
	NativeRenderer_EndGpuFrame();
#if defined(CTR_INTERNAL)
	Platform_ServiceFrameCapture();
#endif
	NativeRenderer_SwapWindow();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
}

// NOTE(aalhendi): Frame timing is handled by VSync() in the platform layer,
// matching PS1 hardware behavior. Platform_EndFrame only does buffer swap + FPS.
void Platform_EndFrame(void)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
	Platform_EndScene();
	Platform_CalcFPS();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
}

void Platform_PresentVRAMDisplay(void)
{
	Platform_PinVRAMDisplayFrames(1);
	Platform_BeginScene();
	Platform_EndFrame();
}

/* The two banner presents (include/platform.h): glyphs NULL is the block
 * font, the roster proof's banner, exactly as before LR-S11. */
internal int Platform_PresentBanner(const char *text, const struct NativeHoldBannerGlyphs *glyphs)
{
	/* A scene in progress, or a pinned presentation the game asked for, is
	 * render-pass state this overlay must not end or consume. */
	if ((s_platformInitialized == 0) || (s_platformBeginScene != 0) || (s_pinnedVramDisplayFrames > 0) || (text == NULL))
	{
		return 0;
	}
	s_presentBannerText = text;
	s_presentBannerGlyphs = glyphs;
	s_presentBannerFont = NATIVE_HOLD_BANNER_FONT_NO_TABLE;
	Platform_PresentVRAMDisplay();
	s_presentBannerText = NULL;
	s_presentBannerGlyphs = NULL;
#if defined(CTR_INTERNAL)
	if (glyphs == NULL)
	{
		Platform_Log("[CTR Native] hold banner presented as capture frame %d\n", s_frameCaptureFrameIndex);
	}
	else
	{
		Platform_Log("[CTR Native] hold banner presented as capture frame %d in the %s\n", s_frameCaptureFrameIndex,
		             NativeHoldBanner_FontName(s_presentBannerFont));
	}
#endif
	return 1;
}

int Platform_PresentVRAMDisplayBanner(const char *text)
{
	return Platform_PresentBanner(text, NULL);
}

int Platform_PresentVRAMDisplayBannerGlyphs(const char *text, const struct NativeHoldBannerGlyphs *glyphs)
{
	return Platform_PresentBanner(text, glyphs);
}

/* Host-local (include/platform.h): the monotonic host time. */
unsigned long long Platform_HostClockUs(void)
{
	return (unsigned long long)(SDL_GetTicksNS() / 1000u);
}

/* Host-local (include/platform.h): a plain host sleep. It emits no VBlank. */
void Platform_HostWaitMs(unsigned int milliseconds)
{
	SDL_Delay((Uint32)milliseconds);
}

void Platform_PinVRAMDisplayFrames(int frameCount)
{
	if (frameCount > s_pinnedVramDisplayFrames)
	{
		s_pinnedVramDisplayFrames = frameCount;
		s_pinnedVramDisplayCustomRect = 0;
	}
}

void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount)
{
	if ((frameCount <= 0) || (w <= 0) || (h <= 0))
	{
		return;
	}

	s_pinnedVramDisplayX = x;
	s_pinnedVramDisplayY = y;
	s_pinnedVramDisplayW = w;
	s_pinnedVramDisplayH = h;
	s_pinnedVramDisplayFrames = frameCount;
	s_pinnedVramDisplayCustomRect = 1;
}

void Platform_PollHostEvents(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_JOYSTICK_ADDED:
		case SDL_EVENT_GAMEPAD_ADDED:
			Platform_InputControllerAdded(event.jdevice.which);
			break;
		case SDL_EVENT_JOYSTICK_REMOVED:
		case SDL_EVENT_GAMEPAD_REMOVED:
			Platform_InputControllerRemoved(event.jdevice.which);
			break;
		case SDL_EVENT_QUIT:
#if defined(CTR_INTERNAL)
			/* While the internal roster proof is active, every exit is nonzero
			 * until the proof reported (docs/ROSTER_MILESTONE.md section 3.4);
			 * otherwise the code Platform_RequestExit set, 0 by default. */
			exit(NativeArcadeRosterProof_ExitCode(s_requestedExitCode));
#else
			exit(0);
#endif
			break;
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			Platform_RefreshDrawableSize();
			break;
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			Platform_UpdateCursorVisibility();
			break;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
#if defined(CTR_INTERNAL)
			/* Closing the window during the internal roster proof must never
			 * look like PASS; without the proof this is exit(0) as before. */
			exit(NativeArcadeRosterProof_ExitCode(0));
#else
			exit(0);
#endif
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			int key = event.key.scancode;
			char down = (event.type == SDL_EVENT_KEY_UP) ? 0 : 1;

			Platform_UpdateHostAltKeyState(key, down);

			if (key == SDL_SCANCODE_F11)
			{
				if ((down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if (key == SDL_SCANCODE_RETURN)
			{
				if ((s_hostAltKeyState != 0) && (down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if (key == SDL_SCANCODE_RSHIFT)
			{
				key = SDL_SCANCODE_LSHIFT;
			}
			else if (key == SDL_SCANCODE_RCTRL)
			{
				key = SDL_SCANCODE_LCTRL;
			}
			else if (key == SDL_SCANCODE_RALT)
			{
				key = SDL_SCANCODE_LALT;
			}

			if ((key == SDL_SCANCODE_F4) && (down == 0))
			{
#ifdef CTR_INTERNAL
				Platform_LogWarn("[CTR Native] Keyboard assigned to player %d\n", Platform_InputCycleKeyboardController());
#endif
				break;
			}

			if ((key == SDL_SCANCODE_F6) && (down == 0))
			{
#ifdef CTR_INTERNAL
				int player = Platform_InputCycleGamepadController();
				if (player == 0)
				{
					Platform_LogWarn("[CTR Native] No gamepad connected\n");
				}
				else
				{
					Platform_LogWarn("[CTR Native] Gamepad assigned to player %d\n", player);
				}
#endif
				break;
			}

			Platform_HandleKey(key, down);
			break;
		}
		}
	}
}

int Platform_PollInput(void)
{
	Platform_PollHostEvents();
	Platform_InputUpdate();
	return 1;
}

int NikoGetEnterKey(void)
{
	const bool *kb = SDL_GetKeyboardState(NULL);
	return (kb && kb[SDL_SCANCODE_RETURN]) ? 1 : 0;
}

// NOTE(aalhendi): VSyncCallback uses the PSX facade, but native owns the VBlank
// clock that emits the registered callback.
// NOTE(aalhendi): Native paces VBlank from PS1 NTSC video timing instead of
// rounded 60Hz. PSX-SPX lists NTSC as 263 scanlines/frame and about 3413 video
// cycles/scanline. With the NTSC GPU clock used here, this is ~59.817Hz, making
// VSync(2) roughly 29.909 FPS. This affects host wall pacing; game state still
// advances from emitted VBlank counts and retail RCNT1 ticks.
#define NATIVE_VBLANK_GPU_CYCLES 897619ull // 3413 * 263
#define NATIVE_GPU_CLOCK_HZ      53693175ull
#define NATIVE_VSYNC_CATCHUP_MAX 8
// NOTE(aalhendi): SDL_DelayPrecise handles most of the wait; the final window
// spins against SDL's performance counter so pacing follows the VBlank target.
#define NATIVE_VSYNC_SPIN_US     200

global_variable u64 s_nextVBlankCounter = 0;
global_variable u64 s_vblankRemainder = 0;
global_variable int s_nativeVBlankCount = 0;
/* Host-local fixed VBlank pacing (platform/native_vblank_pacing.h): off by
 * default, so every normal run keeps the retail-faithful catch-up; only the
 * roster proof and a linked race (from its Launch frame to its Disarm frame)
 * turn it on. Only Platform_SetFixedVBlankPacing writes it, and only
 * Native_CatchUpDueVBlanks reads it. */
global_variable int s_fixedVBlankPacing = 0;

internal u64 Native_CounterFromMicroseconds(u64 freq, u64 microseconds)
{
	return (freq * microseconds) / 1000000;
}

internal void Native_AdvanceVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	// counter ticks per vblank = freq * (897619 / 53693175) sec, kept exact with a
	// running remainder. freq*897619 fits u64 for any realistic QPC frequency.
	const u64 numer = freq * NATIVE_VBLANK_GPU_CYCLES;

	s_nextVBlankCounter += numer / NATIVE_GPU_CLOCK_HZ;
	s_vblankRemainder += numer % NATIVE_GPU_CLOCK_HZ;
	if (s_vblankRemainder >= NATIVE_GPU_CLOCK_HZ)
	{
		s_nextVBlankCounter++;
		s_vblankRemainder -= NATIVE_GPU_CLOCK_HZ;
	}
}

internal void Native_EnsureVBlankTarget(void)
{
	const u64 now = SDL_GetPerformanceCounter();

	if (s_nextVBlankCounter == 0)
	{
		s_nextVBlankCounter = now;
		s_vblankRemainder = 0;
		Native_AdvanceVBlankTarget();
	}
}

internal void Native_WaitUntilVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 spinWindow = Native_CounterFromMicroseconds(freq, NATIVE_VSYNC_SPIN_US);

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
	while (1)
	{
		const u64 now = SDL_GetPerformanceCounter();

		if (now >= s_nextVBlankCounter)
		{
			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		u64 remaining = s_nextVBlankCounter - now;
		if (remaining <= spinWindow)
		{
			// NOTE(penta3): OS sleeps can wake late. Sleep while safely far from
			// the VBlank target (high-res waitable timer), then spin only this
			// final small window so the native VBlank emitter is paced by our
			// clock, not the OS scheduler.
			while (SDL_GetPerformanceCounter() < s_nextVBlankCounter)
			{
			}

			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		u64 sleepUs = ((remaining - spinWindow) * 1000000) / freq;
		if (sleepUs > 0)
		{
			// Cross-platform precise sleep: SDL_DelayPrecise uses the best per-OS
			// primitive (Win32 high-res waitable timer, Linux clock_nanosleep) and
			// yields the CPU instead of busy-waiting. Waking slightly late is safe:
			// the vblank schedule is absolute, so no drift accumulates and the loop
			// re-checks against the target.
			SDL_DelayPrecise(sleepUs * 1000ull);
		}
	}
}

internal void Native_EmitVBlank(void)
{
	NativeRCnt_EmitVBlank();

	if (vsync_callback != NULL)
	{
		vsync_callback();
	}

	NativeAudio_StepVBlank();
	s_nativeVBlankCount++;
}

internal int Native_CatchUpDueVBlanks(void)
{
	int emittedVBlanks = 0;

	Native_EnsureVBlankTarget();

	// NOTE(aalhendi): Native host stalls can be much longer than retail frame
	// stalls, for example during window dragging or a debugger break. Replay a few
	// late VBlanks normally, but rebase pathological stalls instead of bursting
	// many callbacks into one host frame.
	// Fixed pacing (off by default; on only for the roster proof and a linked
	// race, LR-7) never replays a late VBlank: it re-anchors the schedule at
	// now, so each wait emits exactly its own VBlanks (NativeVBlankPacing_Plan).
	{
		const u64 now = SDL_GetPerformanceCounter();
		const u64 freq = SDL_GetPerformanceFrequency();
		const u64 step = (freq * NATIVE_VBLANK_GPU_CYCLES) / NATIVE_GPU_CLOCK_HZ;

		switch (NativeVBlankPacing_Plan(s_fixedVBlankPacing, now, s_nextVBlankCounter, step, NATIVE_VSYNC_CATCHUP_MAX))
		{
		case NATIVE_VBLANK_PACING_REBASE:
			s_nextVBlankCounter = now;
			s_vblankRemainder = 0;
			Native_AdvanceVBlankTarget();
			return 0;
		case NATIVE_VBLANK_PACING_REANCHOR:
			s_nextVBlankCounter = now;
			s_vblankRemainder = 0;
			return 0;
		case NATIVE_VBLANK_PACING_ON_TIME:
			return 0;
		default:
			break;
		}
	}

	while (SDL_GetPerformanceCounter() >= s_nextVBlankCounter)
	{
		const u64 now = SDL_GetPerformanceCounter();

		Native_EmitVBlank();
		emittedVBlanks++;

		if (emittedVBlanks >= NATIVE_VSYNC_CATCHUP_MAX)
		{
			// NOTE(aalhendi): Keep normal late frames faithful, but rebase if the
			// due count grew past the cap while we were replaying.
			s_nextVBlankCounter = now;
			s_vblankRemainder = 0;
			Native_AdvanceVBlankTarget();
			break;
		}

		Native_AdvanceVBlankTarget();
	}

	return emittedVBlanks;
}

internal void Native_WaitAndEmitVBlank(void)
{
	Native_EnsureVBlankTarget();
	Native_WaitUntilVBlankTarget();
	Native_EmitVBlank();
	Native_AdvanceVBlankTarget();
}

int VSync(int mode)
{
	int emittedVBlanks;

	if (mode < 0)
	{
		return s_nativeVBlankCount;
	}

	int requestedVBlanks = (mode == 0) ? 1 : mode;
	emittedVBlanks = 0;

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return s_nativeVBlankCount;
	}
#endif

	emittedVBlanks += Native_CatchUpDueVBlanks();

	for (s32 i = 0; i < requestedVBlanks; i++)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif

	return s_nativeVBlankCount;
}

int Platform_GetVBlankCount(void)
{
	return s_nativeVBlankCount;
}

/*
 * Host-local (include/platform.h). main.c turns it on for the internal live
 * roster proof, before CTR_Main; the arcade-link host glue
 * (platform/native_arcade_link_host.c) turns it on for a linked race and off
 * again (docs/LOCKSTEP_RACE_MILESTONE.md LR-7). It changes only how the pacer
 * treats late VBlanks (NativeVBlankPacing_Plan); game code never observes it.
 */
void Platform_SetFixedVBlankPacing(int enabled)
{
	s_fixedVBlankPacing = (enabled != 0) ? 1 : 0;
}

void Platform_WaitUntilVBlank(int targetVBlank)
{
	int emittedVBlanks = 0;
	int requestedVBlanks = targetVBlank - s_nativeVBlankCount;

	if (requestedVBlanks <= 0)
	{
		return;
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return;
	}
#endif

	emittedVBlanks += Native_CatchUpDueVBlanks();

	while (s_nativeVBlankCount < targetVBlank)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif
}
