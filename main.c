#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

#include "platform/native_arcade_link_autopilot.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_roster_proof.h"
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

	/* Internal live roster proof (docs/ROSTER_MILESTONE.md section 3.4):
	 * evidence plumbing, never match identity. A malformed request is fatal,
	 * like the arcade-link options. It launches a race from the title and
	 * owns its setup, so it is exclusive with link and preview mode, and a
	 * recording would capture a race the replay could not set up, so it is
	 * rejected with every replay option, before any replay parser runs. Its
	 * result is the process exit code, so a frame-capture exit option
	 * (--exit-after-frame), which would end the run on a frame count, is
	 * rejected with it too. */
	struct NativeArcadeRosterProofOptions rosterProofOptions;

	NativeArcadeRosterProofOptions_SetDefaults(&rosterProofOptions);
	if (!NativeArcadeRosterProofOptions_ApplyArgs(argc, argv, &rosterProofOptions))
	{
		fprintf(stderr, "[CTR Native] invalid arcade roster proof option; expected --arcade-roster-proof <log path> [--arcade-roster-proof-seed <u64, decimal or 0x hex>] [--arcade-roster-proof-dwell <0-7200>] [--arcade-roster-proof-ticks <1-3600, or 1-6000 with the autopilot>] [--arcade-roster-proof-profile <two-cab|one-cab>] [--arcade-roster-proof-hold (needs more than 300 ticks)] [--arcade-roster-proof-autopilot (two-cab only)].\n");
		return NativeConsole_Return(1);
	}
#if !defined(CTR_INTERNAL)
	if (rosterProofOptions.enabled != 0u)
	{
		fprintf(stderr, "[CTR Native] --arcade-roster-proof is available in internal builds only.\n");
		return NativeConsole_Return(1);
	}
#endif
	if ((rosterProofOptions.enabled != 0u) &&
	    ((arcadeLinkOptions.enabled != 0u) || (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE) || NativeArg_NamesReplayOption(argc, argv) ||
	     NativeArcadeRosterProof_NamesExitOption(argc, argv)))
	{
		fprintf(stderr, "[CTR Native] --arcade-roster-proof cannot be combined with --arcade-link, --arcade-link-preview, --exit-after-frame, or replay record or playback options.\n");
		return NativeConsole_Return(1);
	}

	/* Internal two-process gate autopilot (docs/RACE_LAUNCH_MILESTONE.md
	 * RL-15): evidence plumbing, never match identity. A malformed request is
	 * fatal, like the other arcade options. It drives a link cabinet, so it
	 * needs --arcade-link (which already excludes a preview). Like the other
	 * arcade options it is rejected with every replay option, before any
	 * replay parser runs; and like the roster proof, whose race setup it would
	 * contend with, its result is the process exit code, so it is rejected
	 * with --arcade-roster-proof and with --exit-after-frame, which would end
	 * the run on a frame count. */
	struct NativeArcadeLinkAutopilotOptions arcadeLinkAutopilotOptions;

	NativeArcadeLinkAutopilotOptions_SetDefaults(&arcadeLinkAutopilotOptions);
	if (!NativeArcadeLinkAutopilotOptions_ApplyArgs(argc, argv, &arcadeLinkAutopilotOptions))
	{
		fprintf(stderr, "[CTR Native] invalid arcade link autopilot option; expected --arcade-link-autopilot <report path> (once) [--arcade-link-autopilot-race-ticks <1-18000> (once, needs --arcade-link-autopilot)] [--arcade-link-autopilot-freeze <1-18000> (once, needs --arcade-link-autopilot)] [--arcade-link-autopilot-desync <1-18000> (once, needs --arcade-link-autopilot)].\n");
		return NativeConsole_Return(1);
	}
#if !defined(CTR_INTERNAL)
	if (arcadeLinkAutopilotOptions.enabled != 0u)
	{
		fprintf(stderr, "[CTR Native] --arcade-link-autopilot is available in internal builds only.\n");
		return NativeConsole_Return(1);
	}
#endif
	if ((arcadeLinkAutopilotOptions.enabled != 0u) &&
	    ((arcadeLinkOptions.enabled == 0u) || NativeArg_NamesReplayOption(argc, argv) || (rosterProofOptions.enabled != 0u) ||
	     NativeArcadeRosterProof_NamesExitOption(argc, argv)))
	{
		fprintf(stderr, "[CTR Native] --arcade-link-autopilot needs --arcade-link and cannot be combined with --arcade-roster-proof, --exit-after-frame, or replay record or playback options.\n");
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
		/* Host-local select entropy (docs/MATCH_SELECT_MILESTONE.md section
		 * 2.6): the wall clock mixed with the high-resolution counter, read
		 * once. It is not match identity: it reaches the match only through
		 * the exchanged select nonces and so the agreed masterSeed. Preview
		 * and default runs never read it; it stays 0 there. */
		arcadeLinkOptions.selectEntropy = ((uint64_t)time(NULL) << 32) ^ (uint64_t)SDL_GetPerformanceCounter();
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
#if defined(CTR_INTERNAL)
	/* The autopilot drives the link host configured above (LINK mode: the
	 * option needs --arcade-link). */
	if (arcadeLinkAutopilotOptions.enabled != 0u)
	{
		MainArcadeLinkAutopilot_Configure(&arcadeLinkAutopilotOptions);
		printf("[CTR Native] arcade link autopilot: report %s\n", arcadeLinkAutopilotOptions.reportPath);
		/* The internal race-length cap (docs/LOCKSTEP_RACE_MILESTONE.md
		 * LR-60): set after the host's Configure, which resets it; 0 keeps
		 * the default bound. The parser already bounded it to 18000. */
		if (!NativeArcadeLinkHost_SetRaceTickLimit(arcadeLinkAutopilotOptions.raceTickLimit))
		{
			fprintf(stderr, "[CTR Native] failed to set the arcade link race tick limit.\n");
			NativeArcadeLinkHost_Shutdown();
			Platform_LogFlush();
			Platform_Shutdown();
			return NativeConsole_Return(1);
		}
		if (arcadeLinkAutopilotOptions.raceTickLimit != 0u)
		{
			printf("[CTR Native] arcade link autopilot: race tick limit %u\n", (unsigned)arcadeLinkAutopilotOptions.raceTickLimit);
		}
		fflush(stdout);
	}
#endif

	/* The proof config carries the build and content identity, read once
	 * here like the link fixture's. The proof is single-machine: a dirty tree
	 * has no build identity, so it then uses the fixed, logged proof build
	 * identity, but always the real content identity of the open disc. */
	if (rosterProofOptions.enabled != 0u)
	{
		struct NativeIdentityV1 rosterProofIdentity;

		if (!NativeIdentity_Get(&rosterProofIdentity))
		{
			if (!NativeDiscImage_GetContentIdentity(rosterProofIdentity.content) ||
			    !NativeArcadeRosterProof_ProofBuildIdentity(rosterProofIdentity.build))
			{
				fprintf(stderr, "[CTR Native] arcade roster proof requires the disc content identity.\n");
				NativeArcadeLinkHost_Shutdown();
				Platform_LogFlush();
				Platform_Shutdown();
				return NativeConsole_Return(1);
			}
			printf("[CTR Native] arcade roster proof: build identity unknown (dirty tree); using the fixed proof build identity SHA-256(\"%s\")\n",
			       NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG);
		}
		if (!NativeArcadeRosterProof_Configure(&rosterProofOptions, &rosterProofIdentity))
		{
			fprintf(stderr, "[CTR Native] failed to configure the arcade roster proof.\n");
			NativeArcadeLinkHost_Shutdown();
			Platform_LogFlush();
			Platform_Shutdown();
			return NativeConsole_Return(1);
		}
		printf("[CTR Native] arcade roster proof: profile %s seed 0x%08X%08X dwell %u ticks %u%s%s report %s\n",
		       NativeArcadeRosterProof_ProfileName(rosterProofOptions.profile), (unsigned)(uint32_t)(rosterProofOptions.seed >> 32),
		       (unsigned)(uint32_t)(rosterProofOptions.seed & 0xFFFFFFFFu), (unsigned)rosterProofOptions.dwellTicks,
		       (unsigned)rosterProofOptions.tickCount, (rosterProofOptions.hold != 0u) ? " hold" : "",
		       (rosterProofOptions.autopilot != 0u) ? " autopilot" : "", rosterProofOptions.logPath);
		fflush(stdout);
#if defined(CTR_INTERNAL)
		/* Scripted pads from the first frame to exit: no host input reaches
		 * the game while the proof runs. */
		if (!MainArcadeRosterProof_Start())
		{
			fprintf(stderr, "[CTR Native] failed to install the arcade roster proof pads.\n");
			NativeArcadeRosterProof_Shutdown();
			NativeArcadeLinkHost_Shutdown();
			Platform_LogFlush();
			Platform_Shutdown();
			return NativeConsole_Return(1);
		}
		/* Proof-only fixed VBlank pacing: a slow host frame must not emit late
		 * VBlanks, which would move elapsedTimeMS and the VBlank count and so
		 * make the proof depend on host timing. Every other run keeps the
		 * default pacing, but for a linked race's own (the arcade-link host,
		 * LR-7; the proof excludes the link). */
		Platform_SetFixedVBlankPacing(1);
#endif
	}

	/* With the roster proof active, returning here without its report is
	 * INCOMPLETE, never PASS; otherwise CTR_Main's own result. */
	const int result = NativeArcadeRosterProof_ExitCode(CTR_Main());

	NativeArcadeLinkHost_Shutdown();
	Platform_Shutdown();
	return NativeConsole_Return(result);
}
