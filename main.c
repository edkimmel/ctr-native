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

#include "platform/native_assets.h"
#include "platform/native_display_config.h"
#include "platform/native_gpu.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_perf.h"
#include "platform/native_presentation_override_config.h"
#include "platform/native_presentation_pack.h"
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
#include "platform/native_identity.c"
#include "platform/native_presentation_override_config.c"
#include "platform/native_presentation_registry.c"
#include "platform/native_presentation_pack.c"
#include "platform/native_host_texture_asset.c"
#include "platform/native_presentation_asset_loader.c"
#include "platform/native_host_texture_store.c"
#include "platform/native_texture_override_policy.c"
#include "platform/native_host_texture_binding.c"
#include "platform/native_presentation_invalidation.c"
#include "platform/native_host_texture_plan.c"
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


int main(int argc, char *argv[])
{
	struct NativeDisplayConfig displayConfig;
	struct NativePresentationOverrideConfig presentationOverrideConfig;
	struct NativePresentationPack presentationPack;

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
		fprintf(stderr, "[CTR Native] invalid local display option; falling back to 1x windowed (supported render scales: 1, 2, 3, 4, 6, 8).\n");
		NativeDisplayConfig_SetDefaults(&displayConfig);
	}
	NativePresentationOverrideConfig_SetDefaults(&presentationOverrideConfig);
	if (!NativePresentationOverrideConfig_ApplyArgs(argc, argv, &presentationOverrideConfig))
	{
		/* A malformed local pack setting is recoverable: do not let a cabinet
		 * presentation preference prevent a normal retail-texture launch. */
		fprintf(stderr, "[CTR Native] invalid local presentation option; presentation overrides disabled.\n");
		NativePresentationOverrideConfig_SetDefaults(&presentationOverrideConfig);
	}

	printf("[CTR Native] Starting...\n");
	printf("[CTR Native] Local render scale: %dx\n", displayConfig.renderScale);
	printf("[CTR Native] Local window mode: %s\n", displayConfig.fullscreen ? "fullscreen" : "windowed");
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
	if (!NativePresentationPack_Load(&presentationOverrideConfig, &presentationPack))
	{
		fprintf(stderr, "[CTR Native] presentation pack disabled: %s.\n",
		        NativePresentationPack_ErrorString(NativePresentationPack_GetLastError(&presentationPack)));
	}
	else if (presentationOverrideConfig.enabled != 0)
	{
		printf("[CTR Native] presentation pack validated: %u entries (%s); awaiting GL preload.\n",
		       presentationPack.registry.entryCount,
		       NativePresentationRegistry_GetManifestFingerprint(&presentationPack.registry));
		fflush(stdout);
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

#ifdef USE_16BY9
	printf("[CTR Native] Widescreen\n");
	Platform_Init("Crash Team Racing", 1280, 720, displayConfig.fullscreen);
#else
	printf("[CTR Native] 4:3\n");
	Platform_Init("Crash Team Racing", 800, 600, displayConfig.fullscreen);
#endif
	/* This is host presentation state only. The renderer applies it to its GL
	 * attachments; no game, replay, or canonical-state code observes it. */
	NativeRenderer_SetRenderScale(displayConfig.renderScale);
	if (!NativeGpu_ConfigurePresentationOverrides(&presentationPack.registry))
	{
		fprintf(stderr, "[CTR Native] presentation overrides disabled: host preload/upload failed; using retail textures.\n");
	}

#if defined(CTR_INTERNAL)
	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		NativeGpu_ShutdownPresentationOverrides();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#endif

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		NativeGpu_ShutdownPresentationOverrides();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#else
	(void)argc;
	(void)argv;
#endif

	const int result = CTR_Main();

	NativeGpu_ShutdownPresentationOverrides();
	Platform_Shutdown();
	return NativeConsole_Return(result);
}
