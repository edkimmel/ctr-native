# Structural isolation for the host-local hold support in the platform layer
# (include/platform.h, platform/native_platform.c, platform/native_renderer.c,
# platform/native_hold_banner.c; docs/LOCKSTEP_RACE_MILESTONE.md LR-9,
# section 5, slice LR-S2 (a)):
#  1. Platform_HostWaitMs, Platform_HostClockUs, and
#     Platform_PresentVRAMDisplayBanner are declared once in
#     include/platform.h and defined once in platform/native_platform.c; the
#     wait is exactly one SDL_Delay and the clock exactly one SDL_GetTicksNS
#     read, so neither emits a VBlank, runs the VSync callback, or steps
#     audio;
#  2. the banner present and every platform function it reaches
#     (Platform_PresentVRAMDisplay, Platform_PinVRAMDisplayFrames,
#     Platform_BeginScene, Platform_EndFrame, Platform_EndScene,
#     Platform_CalcFPS, Platform_ServiceFrameCapture), and the event pump
#     the hold calls (Platform_PollHostEvents), name no VBlank emit, VSync,
#     VSync callback, root counter, audio step, or pad poll; the present
#     refuses while a scene is in progress or a pinned presentation is owed,
#     and clears its one-shot text after the present; the banner text is
#     named only where it is declared, set, cleared, and drawn;
#  3. the renderer's banner draw fills only the window framebuffer with
#     scissored clears and restores the state it changed: it names no VRAM
#     write, framebuffer store, or render-target binding;
#  4. only the hold module (game/MAIN/MainArcadeRaceHold.c) calls the wait,
#     the clock, and the banner present, and only native_platform.c calls the
#     renderer's banner draw;
#  5. the banner layout module is pure (no GL, SDL, platform call, clock,
#     heap, game state, or static mutable state), includes only its header,
#     stddef.h, stdint.h, and string.h, and its library builds exactly its .c,
#     links nothing, is C17 with extensions off, and is linked by ctr_native.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "host wait isolation")
set(platform_header "include/platform.h")
set(platform_path "platform/native_platform.c")
set(renderer_path "platform/native_renderer.c")
set(banner_header "include/platform/native_hold_banner.h")
set(banner_source "platform/native_hold_banner.c")
set(banner_target ctr_native_hold_banner)
set(hold_source "game/MAIN/MainArcadeRaceHold.c")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_strip_comments label source out_var)
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\r\n]*" " " stripped "${source}")
    string(FIND "${stripped}" "/*" open_at)
    if(NOT open_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: unterminated /* comment in ${label}")
    endif()
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_order relative_path source)
    set(remaining "${source}")
    foreach(term IN LISTS ARGN)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${prefix}: '${term}' is missing or out of order in ${relative_path}")
        endif()
        string(LENGTH "${term}" term_length)
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endforeach()
endfunction()

function(ctr_count_identifier code name out_var)
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(ctr_block relative_path source opener out_var)
    string(FIND "${source}" "${opener}" opener_at)
    if(opener_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${opener}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" ${opener_at} -1 tail)
    string(FIND "${tail}" "{" brace_offset)
    if(brace_offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: no block follows '${opener}' in ${relative_path}")
    endif()
    string(LENGTH "${tail}" length)
    set(depth 0)
    set(position ${brace_offset})
    while(position LESS length)
        string(SUBSTRING "${tail}" ${position} 1 character)
        if(character STREQUAL "{")
            math(EXPR depth "${depth} + 1")
        elseif(character STREQUAL "}")
            math(EXPR depth "${depth} - 1")
            if(depth EQUAL 0)
                math(EXPR block_length "${position} - ${brace_offset} + 1")
                string(SUBSTRING "${tail}" ${brace_offset} ${block_length} block)
                set(${out_var} "${block}" PARENT_SCOPE)
                return()
            endif()
        endif()
        math(EXPR position "${position} + 1")
    endwhile()
    message(FATAL_ERROR "${prefix}: unbalanced block after '${opener}' in ${relative_path}")
endfunction()

ctr_read_source("${platform_header}" platform_h)
ctr_read_source("${platform_path}" platform)
ctr_read_source("${renderer_path}" renderer)
ctr_strip_comments("${platform_header}" "${platform_h}" platform_h_code)
ctr_strip_comments("${platform_path}" "${platform}" platform_code)
ctr_strip_comments("${renderer_path}" "${renderer}" renderer_code)

# Names that emit or stand for a VBlank, the VSync callback, the root
# counter, an audio step, or a pad poll.
set(vblank_tokens
    VSync vsync_callback Native_EmitVBlank Native_WaitAndEmitVBlank Native_CatchUpDueVBlanks Native_WaitUntilVBlankTarget
    Native_AdvanceVBlankTarget Platform_WaitUntilVBlank s_nativeVBlankCount s_nextVBlankCounter NativeRCnt_ NativeAudio_
    Platform_PollInput Platform_InputUpdate GAMEPAD_Poll GAMEPAD_Process)

# 1. Declarations and the two exact bodies.
foreach(declaration IN ITEMS
        "unsigned long long Platform_HostClockUs(void);"
        "void Platform_HostWaitMs(unsigned int milliseconds);"
        "int Platform_PresentVRAMDisplayBanner(const char *text);")
    ctr_require("${platform_header}" "${platform_h_code}" "${declaration}")
endforeach()
foreach(name IN ITEMS Platform_HostClockUs Platform_HostWaitMs Platform_PresentVRAMDisplayBanner)
    ctr_count_identifier("${platform_h_code}" "${name}" declaration_hits)
    if(NOT declaration_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${platform_header} must declare ${name} exactly once (found ${declaration_hits})")
    endif()
    ctr_count_identifier("${platform_code}" "${name}" definition_hits)
    if(NOT definition_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${platform_path} must define ${name} once and never call it (found ${definition_hits})")
    endif()
endforeach()
ctr_block("${platform_path}" "${platform_code}" "void Platform_HostWaitMs(unsigned int milliseconds)" wait_body)
string(REGEX REPLACE "[ \t\n]+" " " wait_normalized "${wait_body}")
if(NOT wait_normalized STREQUAL "{ SDL_Delay((Uint32)milliseconds); }")
    message(FATAL_ERROR "${prefix}: Platform_HostWaitMs must be exactly one SDL_Delay (found '${wait_normalized}')")
endif()
ctr_block("${platform_path}" "${platform_code}" "unsigned long long Platform_HostClockUs(void)" clock_body)
string(REGEX REPLACE "[ \t\n]+" " " clock_normalized "${clock_body}")
if(NOT clock_normalized STREQUAL "{ return (unsigned long long)(SDL_GetTicksNS() / 1000u); }")
    message(FATAL_ERROR "${prefix}: Platform_HostClockUs must only read SDL_GetTicksNS (found '${clock_normalized}')")
endif()

# 2. The banner present and what it reaches, and the event pump.
foreach(opener IN ITEMS
        "int Platform_PresentVRAMDisplayBanner(const char *text)"
        "void Platform_PresentVRAMDisplay(void)"
        "void Platform_PinVRAMDisplayFrames(int frameCount)"
        "int Platform_BeginScene(void)"
        "void Platform_EndFrame(void)"
        "void Platform_EndScene(void)"
        "internal void Platform_CalcFPS(void)"
        "internal void Platform_ServiceFrameCapture(void)"
        "void Platform_PollHostEvents(void)")
    ctr_block("${platform_path}" "${platform_code}" "${opener}" body)
    foreach(term IN LISTS vblank_tokens)
        ctr_forbid("${platform_path} (${opener})" "${body}" "${term}")
    endforeach()
endforeach()
ctr_block("${platform_path}" "${platform_code}" "int Platform_PresentVRAMDisplayBanner(const char *text)" banner_body)
ctr_require_order("${platform_path} (Platform_PresentVRAMDisplayBanner)" "${banner_body}"
    "if ((s_platformInitialized == 0) || (s_platformBeginScene != 0) || (s_pinnedVramDisplayFrames > 0) || (text == NULL))"
    "return 0;"
    "s_presentBannerText = text;"
    "Platform_PresentVRAMDisplay();"
    "s_presentBannerText = NULL;"
    "return 1;")
ctr_require("${platform_path}" "${platform_code}" "\nglobal_variable const char *s_presentBannerText = NULL;\n")
ctr_count_identifier("${platform_code}" "s_presentBannerText" text_hits)
if(NOT text_hits EQUAL 5)
    message(FATAL_ERROR "${prefix}: ${platform_path} must name s_presentBannerText exactly five times: its declaration, the set and the clear in the banner present, and the check and the draw in Platform_EndScene (found ${text_hits})")
endif()
ctr_block("${platform_path}" "${platform_code}" "void Platform_EndScene(void)" end_scene_body)
ctr_require_order("${platform_path} (Platform_EndScene)" "${end_scene_body}"
    "if (s_pinnedVramDisplayFrames > 0)"
    "NativeRenderer_PresentVRAMDisplay();"
    "if (s_presentBannerText != NULL)"
    "NativeRenderer_DrawPresentBanner(s_presentBannerText);"
    "NativeRenderer_EndGpuFrame();"
    "Platform_ServiceFrameCapture();"
    "NativeRenderer_SwapWindow();")

# 3. The renderer's banner draw.
ctr_block("${renderer_path}" "${renderer_code}" "void NativeRenderer_DrawPresentBanner(const char *text)" draw_body)
foreach(term IN LISTS vblank_tokens ITEMS
        CopyVRAM MarkVRAMDirty MarkGpuVRAMNewer cpuPixels StoreFrameBuffer GpuPackTextureToVRAM UpdateVRAM
        s_mainRenderTarget s_offscreenRenderTarget BindMainRenderTarget LoadRenderTarget glDraw DrawVRAMRegion activeDrawEnv activeDispEnv)
    ctr_forbid("${renderer_path} (NativeRenderer_DrawPresentBanner)" "${draw_body}" "${term}")
endforeach()
ctr_require_order("${renderer_path} (NativeRenderer_DrawPresentBanner)" "${draw_body}"
    "NativeHoldBanner_Layout(text, s_presentViewport.w, s_presentViewport.h, layout)"
    "glBindFramebuffer(GL_FRAMEBUFFER, 0);"
    "glEnable(GL_SCISSOR_TEST);"
    "NativeRenderer_ClearHostRect("
    "glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);"
    "glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);"
    "s_previousScissorState = previousScissorEnabled ? 1 : 0;")

# 4. Callers, over every game, platform, include, and main.c file.
file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(hold_owners "${platform_header}" "${platform_path}" "${hold_source}")
set(draw_owners "include/platform/native_renderer.h" "${renderer_path}" "${platform_path}")
set(text_owners "${platform_path}")
set(scanned 0)
set(hold_callers "")
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    file(READ "${path}" source)
    string(REGEX MATCH "Platform_HostWaitMs|Platform_HostClockUs|Platform_PresentVRAMDisplayBanner|NativeRenderer_DrawPresentBanner|s_presentBannerText" raw_hit "${source}")
    if(raw_hit STREQUAL "")
        continue()
    endif()
    ctr_strip_comments("${relative_path}" "${source}" code)
    foreach(rule "Platform_HostWaitMs|hold_owners" "Platform_HostClockUs|hold_owners" "Platform_PresentVRAMDisplayBanner|hold_owners"
            "NativeRenderer_DrawPresentBanner|draw_owners" "s_presentBannerText|text_owners")
        string(REPLACE "|" ";" rule_items "${rule}")
        list(GET rule_items 0 name)
        list(GET rule_items 1 owners)
        ctr_count_identifier("${code}" "${name}" hits)
        list(FIND ${owners} "${relative_path}" owner_at)
        if(hits GREATER 0 AND owner_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${relative_path} names ${name}; only ${${owners}} may")
        endif()
        if(hits GREATER 0 AND relative_path STREQUAL "${hold_source}")
            list(APPEND hold_callers "${name}")
        endif()
    endforeach()
endforeach()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()
list(SORT hold_callers)
if(NOT "${hold_callers}" STREQUAL "Platform_HostClockUs;Platform_HostWaitMs;Platform_PresentVRAMDisplayBanner")
    message(FATAL_ERROR "${prefix}: ${hold_source} must call the wait, the clock, and the banner present (found '${hold_callers}')")
endif()
ctr_count_identifier("${platform_code}" "NativeRenderer_DrawPresentBanner" draw_calls)
if(NOT draw_calls EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${platform_path} must call NativeRenderer_DrawPresentBanner exactly once (found ${draw_calls})")
endif()

# 5. The banner layout module is pure.
foreach(relative_path IN ITEMS "${banner_header}" "${banner_source}")
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${relative_path}" "${source}" code)
    string(FIND "${code}" "NativeHoldBanner_Layout(" layout_at)
    if(layout_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: comment stripping lost the code of ${relative_path}")
    endif()
    foreach(term IN LISTS vblank_tokens ITEMS
            SDL_ glClear glScissor glad GL_ Platform_ NativeRenderer platform.h common.h gGT sdata malloc calloc realloc "free(" alloca
            fopen printf FILE clock "time(" "time.h" Lockstep lockstep Replay replay Lease lease Topology topology)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
    string(REGEX MATCHALL "#[ \t]*include[^\n]*" include_lines "${code}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"platform/native_hold_banner\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    # The only static is the const glyph table.
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])static[ \t\n][^;{=]*" statics "${code}")
    foreach(static_head IN LISTS statics)
        if(NOT static_head MATCHES "static const uint8_t k_nativeHoldBannerGlyphs\\[")
            message(FATAL_ERROR "${prefix}: ${relative_path} holds static state '${static_head}'")
        endif()
    endforeach()
endforeach()
ctr_read_source("CMakeLists.txt" cmake)
string(REGEX MATCHALL "add_library\\([ \t\n]*${banner_target}[ \t\n][^)]*\\)" add_calls "${cmake}")
list(LENGTH add_calls add_call_count)
if(NOT add_call_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one add_library(${banner_target} ...), found ${add_call_count}")
endif()
list(GET add_calls 0 add_call)
string(REGEX REPLACE "[ \t\n]+" " " add_call "${add_call}")
if(NOT add_call STREQUAL "add_library(${banner_target} STATIC ${banner_source})")
    message(FATAL_ERROR "${prefix}: ${banner_target} must build exactly ${banner_source} (found '${add_call}')")
endif()
string(REGEX MATCHALL "target_link_libraries\\([ \t\n]*${banner_target}[ \t\n][^)]*\\)" banner_link_calls "${cmake}")
if(NOT "${banner_link_calls}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ${banner_target} must link nothing")
endif()
string(FIND "${cmake}" "add_library(${banner_target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
ctr_require_order("CMakeLists.txt (${banner_target})" "${target_block}"
    "set_target_properties(${banner_target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF")
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(REGEX REPLACE "[ \t\n]+" ";" native_link_items "${native_link_block}")
list(FIND native_link_items "${banner_target}" banner_link_at)
if(banner_link_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ctr_native must link ${banner_target}")
endif()
# Linked, never unity-included.
ctr_read_source("main.c" main_source)
ctr_forbid("main.c" "${main_source}" "native_hold_banner.c")
