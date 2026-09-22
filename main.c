#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include "platform/native_win32.h"
#else
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()

#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_assets.h"
#include "platform/native_display_config.h"
#include "platform/native_frame_capture.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"

#include <platform.h>

#include "game/game_unity.h"

#include "game/zGlobal_RDATA.c"
#include "game/zGlobal_DATA.c"
#include "game/zGlobal_SDATA.c"

#undef RECT

#include "platform/native_sha256.c"
#include "platform/native_disc_image.c"
#include "platform/native_display_config.c"
#include "platform/native_frame_capture.c"
#include "platform/native_identity.c"
#include "platform/native_assets.c"
#include "platform/native_audio.c"
#include "platform/native_memory.c"
#include "platform/native_checkpoint.c"
#include "platform/native_checkpoint_file.c"
#include "platform/native_cd.c"
#include "platform/native_gpu_links.c"
#include "platform/native_gpu.c"
#include "platform/native_gte_core.c"
#include "platform/native_glad.c"
#include "platform/native_input.c"
#include "platform/native_inline_c.c"
#include "platform/native_libapi.c"
#include "platform/native_libetc.c"
#include "platform/native_libgte.c"
#include "platform/native_libgpu.c"
#include "platform/native_libpad.c"
#include "platform/native_libspu.c"
#include "platform/native_log.c"
#include "platform/native_memcard.c"
#include "platform/native_memcard_adapter.c"
#include "platform/native_perf.c"
#include "platform/native_platform.c"
#include "platform/native_replay_scheduler.c"
#include "platform/native_renderer.c"
#include "platform/native_savestate.c"
#include "platform/native_state.c"
#include "platform/native_str.c"

#ifndef CC
#if defined(__GNUC__)
#if _WIN32
#ifndef __clang__
#define CC "MINGW-GCC"
#else
#define CC "MINGW-CLANG"
#endif
#else
#ifndef __clang__
#define CC "GCC"
#else
#define CC "CLANG"
#endif
#endif
#elif defined(_MSC_VER)
#define CC "MSVC"
#else
#define CC "Unknown"
#endif
#endif

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

static int NativeConsole_ShouldPauseOnError(void)
{
#if defined(_WIN32)
	DWORD consoleProcesses[2];
	DWORD consoleProcessCount;

	if (GetConsoleWindow() == NULL)
		return 0;

	consoleProcessCount = GetConsoleProcessList(consoleProcesses, (DWORD)(sizeof(consoleProcesses) / sizeof(consoleProcesses[0])));
	return (consoleProcessCount == 1) && (consoleProcesses[0] == GetCurrentProcessId());
#else
	return 0;
#endif
}

static s32 NativeConsole_Return(const u32 result)
{
	if ((result != 0) && NativeConsole_ShouldPauseOnError())
	{
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "\n[CTR Native] Press Enter to close this window...");
		fflush(stderr);

		while (getchar() != '\n' && !feof(stdin))
		{
		}
	}

	return (s32)result;
}

// TODO(aalhendi): just make an argparser?
static int NativeArg_IsVersion(const char *arg)
{
	return (arg != NULL) && ((strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0));
}

/* Returns 1 when argv names any replay record, playback, or report option
 * NativeReplayScheduler_ParseArgs accepts (the options behind
 * NativeReplayScheduler_ConfigureFromArgs and
 * NativeReplayScheduler_PrepareReportFromArgs). Matched by name only. */
static int NativeArg_NamesReplayOption(int argc, char *argv[])
{
	static const char *const replayOptions[] = {"--record", "--record-v2", "--record-v3", "--replay", "--replay-v2", "--replay-v3", "--replay-bypass-header", "--toggle", "--detailed"};

	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		for (size_t optionIndex = 0; optionIndex < sizeof(replayOptions) / sizeof(replayOptions[0]); optionIndex++)
		{
			if ((argv[argIndex] != NULL) && (strcmp(argv[argIndex], replayOptions[optionIndex]) == 0))
			{
				return 1;
			}
		}
	}
	return 0;
}


int main(int argc, char *argv[])
{
	struct NativeDisplayConfig displayConfig;

	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (NativeArg_IsVersion(argv[argIndex]))
		{
			printf("CTR Native %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
			return 0;
		}
	}

	NativeDisplayConfig_SetDefaults(&displayConfig);
	if (!NativeDisplayConfig_ApplyArgs(argc, argv, &displayConfig))
	{
		/* A malformed cabinet-local preference must never prevent the recovery
		 * launch path. Fall back to 1x/windowed; it remains intentionally
		 * outside game, replay, and canonical-state configuration. */
		fprintf(stderr, "[CTR Native] invalid local display option; falling back to 1x windowed (supported render scales: 1, 2, 3, 4, 6, 8; supported texture filters: nearest, bilinear).\n");
		NativeDisplayConfig_SetDefaults(&displayConfig);
	}

	/* Host-local arcade-link launch configuration, never match identity
	 * (docs/GAME_LOOP_UI_MILESTONE.md section 2.5). Like the frame-capture
	 * options, a malformed request is fatal: a cabinet must not silently come
	 * up unlinked. With no arcade-link option everything stays dormant. */
	struct NativeArcadeLinkOptions arcadeLinkOptions;

	NativeArcadeLinkOptions_SetDefaults(&arcadeLinkOptions);
	if (!NativeArcadeLinkOptions_ApplyArgs(argc, argv, &arcadeLinkOptions))
	{
		fprintf(stderr, "[CTR Native] invalid arcade-link option; expected --arcade-link cab1|cab2 --arcade-link-port <1-65535> --arcade-link-peer <a.b.c.d:port> (repeatable), or --arcade-link-preview <screen> alone.\n");
		return NativeConsole_Return(1);
	}
#if !defined(CTR_INTERNAL)
	if (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE)
	{
		fprintf(stderr, "[CTR Native] --arcade-link-preview is available in internal builds only.\n");
		return NativeConsole_Return(1);
	}
#endif
	/* Link and preview modes hide the retail main-menu box and clear the menu
	 * input and pad taps they own, all of which a recording captures, so a
	 * recording would not play back without the option. Rejected here, after
	 * the arcade-link parser and before the replay parsers run, so no report
	 * folder or recording file is created first. */
	if (((arcadeLinkOptions.enabled != 0u) || (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE)) && NativeArg_NamesReplayOption(argc, argv))
	{
		fprintf(stderr, "[CTR Native] --arcade-link and --arcade-link-preview cannot be combined with replay record or playback options.\n");
		return NativeConsole_Return(1);
	}

	printf("[CTR Native] Starting...\n");
	printf("[CTR Native] Local render scale: %dx\n", displayConfig.renderScale);
	printf("[CTR Native] Local window mode: %s\n", displayConfig.fullscreen ? "fullscreen" : "windowed");
	printf("[CTR Native] Local texture filter: %s\n", NativeDisplayConfig_TextureFilterName(displayConfig.textureFilter));
	fflush(stdout);

	const char *sdlBasePath = SDL_GetBasePath();
	printf("[CTR Native] SDL base path: %s\n", sdlBasePath ? sdlBasePath : "(null)");
	fflush(stdout);

	if (!NativeAssets_Init(sdlBasePath))
	{
		fprintf(stderr, "[CTR Native] Failed to initialize asset paths.\n");
		return NativeConsole_Return(1);
	}

	printf("[CTR Native] Version: %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("[CTR Native] Built with: " CC "\n");
	printf("[CTR Native] Base: %s\n", NativeAssets_GetBaseDir());
	printf("[CTR Native] Assets: %s\n", NativeAssets_GetAssetDir());
	fflush(stdout);

	if (chdir(NativeAssets_GetBaseDir()) != 0)
	{
		fprintf(stderr, "[CTR Native] Failed to enter base directory: %s\n", NativeAssets_GetBaseDir());
		return NativeConsole_Return(1);
	}

	if (!NativeAssets_Validate())
	{
		return NativeConsole_Return(1);
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

#ifdef USE_16BY9
	printf("[CTR Native] Widescreen\n");
	if (!Platform_Init("Crash Team Racing", 1280, 720, displayConfig.fullscreen))
	{
		fprintf(stderr, "[CTR Native] Platform initialisation failed; see the log above.\n");
		fflush(stderr);
		return NativeConsole_Return(1);
	}
#else
	printf("[CTR Native] 4:3\n");
	if (!Platform_Init("Crash Team Racing", 800, 600, displayConfig.fullscreen))
	{
		fprintf(stderr, "[CTR Native] Platform initialisation failed; see the log above.\n");
		fflush(stderr);
		return NativeConsole_Return(1);
	}
#endif
	/* This is host presentation state only. The renderer applies it to its GL
	 * attachments; no game, replay, or canonical-state code observes it. */
	NativeRenderer_SetRenderScale(displayConfig.renderScale);
	/* Host presentation state only: the sampling mode is applied to the PSX
	 * shaders and is never observed by game, replay, or canonical-state code. */
	NativeRenderer_SetTextureFilter(displayConfig.textureFilter);

#if defined(CTR_INTERNAL)
	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}

	/* Host-local, presentation-only capture requests. Unlike the display
	 * config, a malformed request is fatal: an unattended capture run must not
	 * silently produce nothing. Relative paths resolve against the base
	 * directory entered above. */
	struct NativeFrameCaptureConfig captureConfig;

	NativeFrameCapture_SetDefaults(&captureConfig);
	if (!NativeFrameCapture_ApplyArgs(argc, argv, &captureConfig))
	{
		fprintf(stderr, "[CTR Native] invalid frame-capture option; expected --capture-frame <frame>=<path.bmp> (frame >= 1, repeatable) and --exit-after-frame <frame>.\n");
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
	Platform_SetFrameCaptureConfig(&captureConfig);
#endif

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#else
	(void)argc;
	(void)argv;
#endif

	/* The link fixture carries the build and content identity. It is read
	 * once here, after the disc image was opened by NativeAssets_Init; the
	 * disc image caches the content digest, the same one the replay
	 * scheduler reads, so nothing is hashed per frame. */
	struct NativeIdentityV1 arcadeLinkIdentity;
	const struct NativeIdentityV1 *arcadeLinkIdentityPtr = NULL;

	if (arcadeLinkOptions.enabled != 0u)
	{
		if (!NativeIdentity_Get(&arcadeLinkIdentity))
		{
			fprintf(stderr, "[CTR Native] arcade link requires a known build and content identity.\n");
			Platform_LogFlush();
			Platform_Shutdown();
			return NativeConsole_Return(1);
		}
		arcadeLinkIdentityPtr = &arcadeLinkIdentity;
	}
	if (!NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr))
	{
		fprintf(stderr, "[CTR Native] failed to configure the arcade link.\n");
		NativeArcadeLinkHost_Shutdown();
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
	if (arcadeLinkOptions.enabled != 0u)
	{
		printf("[CTR Native] arcade link: cab%u port %u, %u peers\n", (arcadeLinkOptions.localRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) ? 2u : 1u,
		       (unsigned)arcadeLinkOptions.localPort, (unsigned)arcadeLinkOptions.peerCount);
	}
	else if (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE)
	{
		printf("[CTR Native] arcade link: preview %s\n", NativeArcadeLinkOptions_PreviewName(arcadeLinkOptions.preview));
	}
	else
	{
		printf("[CTR Native] arcade link: off\n");
	}
	fflush(stdout);
	if (NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)
	{
		/* The host's quit and window-close paths leave through exit(), which
		 * runs Platform_Shutdown from its atexit registration in Platform_Init.
		 * Handlers run in reverse order, so registering here closes the link
		 * before Platform_Shutdown on those paths too. */
		(void)atexit(NativeArcadeLinkHost_Shutdown);
	}

	const int result = CTR_Main();

	NativeArcadeLinkHost_Shutdown();
	Platform_Shutdown();
	return NativeConsole_Return(result);
}
