# Structural isolation for the stall hold loop and its period core
# (game/MAIN/MainArcadeRaceHold.{c,h}, game/MAIN/MainArcadeRaceHoldCore.{c,h};
# docs/LOCKSTEP_RACE_MILESTONE.md LR-9, section 5, slice LR-S2 (a)):
#  1. the hold module (comments removed) names no VSync, VBlank, or VSync
#     callback token, no GAMEPAD_ or Platform_Input call, no game global or
#     retail type (gGT, sdata, GameTracker, ...), no retail draw path
#     (DecalFont, the ordering table, primitive memory), no audio or SDL
#     name, no lockstep, netplay, lobby, or session token, no section 5
#     token (lease, topology, checkpoint, replay, canonical state), no heap,
#     and no static state; its only platform calls are the event pump, the
#     host clock, the host wait, and the two banner presents (block font and,
#     since LR-S11, game font); its includes are an allow-list; the .c is one
#     CTR_NATIVE block;
#  2. MainArcadeRaceHold_Run does its host work in the LR-9 order: pump,
#     period bookkeeping, the caller's step, the banner, the host wait;
#  3. the period core is pure (no game, platform, SDL, clock, I/O, heap, or
#     static state, and the same token bans), includes only stdint.h,
#     stddef.h, string.h, and its own header, uses no conditional
#     compilation, and holds the LR-9 constants literally: the 33435 us tick
#     period and holdGraceTicks = 10;
#  4. ctr_native_arcade_race_hold_core builds exactly the core's .c, links
#     nothing, is C17 with extensions off, is linked by ctr_native and, among
#     the tests, only by its unit test and the hold loop's unit test
#     (main_arcade_race_hold_test); the core is never unity-included, and
#     the hold module is unity-included exactly once, after the 230 overlay
#     and before the roster proof that calls it;
#  5. MainArcadeRaceHold_Run is named only by the hold module and the roster
#     proof hook (LR-S2 (a)), and the proof calls it only from its hold
#     helper, which MainArcadeRosterProof_Frame calls in its VALIDATED case;
#  6. since LR-S10 part 2 the loop is MainArcadeRaceHold_RunMode, whose mode
#     only gates step 3 (the banner): mode is named in the loop only in the
#     banner guard, the two mode values are defined literally,
#     MainArcadeRaceHold_Run is exactly RunMode with the banner (the roster
#     proof keeps its banner), and RunMode is named only by the hold module
#     and the race caller (game/MAIN/MainArcadeRaceLaunch.c). Since LR-S11
#     (LR-72): RunMode takes the caller's glyph table and hands it on unread
#     (named only in the parameter, the NULL test, and the glyph present; the
#     banner guard holds exactly the count and one present: the glyph
#     present for a table, the block present for none); Run passes NULL, so
#     the roster proof's banner stays the block font and the proof names no
#     glyph table; the race caller's race hold is with the banner and its
#     table (&glyphs), and it presents no banner itself. Since LR-S13 part A
#     (LR-73) the race caller calls RunMode exactly twice: that race hold, and
#     the internal autopilot's freeze injection,
#     RunMode(MainArcadeRaceLaunch_FreezeStep, NULL, NO_BANNER, NULL, &freeze),
#     the one place it names NO_BANNER (main_arcade_link_hook_isolation 16m
#     pins the freeze in the caller's CTR_INTERNAL part and its step).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "race hold isolation")
set(hold_header "game/MAIN/MainArcadeRaceHold.h")
set(hold_source "game/MAIN/MainArcadeRaceHold.c")
set(core_header "game/MAIN/MainArcadeRaceHoldCore.h")
set(core_source "game/MAIN/MainArcadeRaceHoldCore.c")
set(core_target ctr_native_arcade_race_hold_core)
set(proof_source "game/MAIN/MainArcadeRosterProof.c")
set(launch_source "game/MAIN/MainArcadeRaceLaunch.c")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in the code of ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

# Removes every /* */ and // comment in one left-to-right pass, replacing
# each with a space (tests/native_arcade_launch_isolation_test.cmake).
function(ctr_strip_comments label source out_var)
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\r\n]*" " " stripped "${source}")
    string(FIND "${stripped}" "/*" open_at)
    if(NOT open_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: unterminated /* comment in ${label}")
    endif()
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# Fails unless every term appears in source, each after the previous one.
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

# Counts the whole-identifier occurrences of name in code.
function(ctr_count_identifier code name out_var)
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Sets out_var to the first brace block after opener, braces included.
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

# 0. The helpers themselves work.
ctr_strip_comments("self-check" "code1 /* VSync\n gGT */ code2 // lockstep\ncode3 /**/code4 /** x **/ code5 /* a // b */ code6" self_check)
foreach(term IN ITEMS VSync gGT lockstep "/*" "*/" "//")
    ctr_forbid("self-check" "${self_check}" "${term}")
endforeach()
foreach(term IN ITEMS code1 code2 code3 code4 code5 code6)
    ctr_require("self-check" "${self_check}" "${term}")
endforeach()

# The bans shared by the hold module and the core.
set(vblank_tokens VSync VSYNC Vsync vsync VBlank Vblank vblank VBLANK WaitUntil RCnt rcnt frameTimer)
set(input_tokens GAMEPAD_ Gamepad gamepad Platform_Input Platform_PollInput buttons Pad_ PadSnapshot)
set(game_tokens
    gGT sdata "data." GameTracker GamepadSystem gGamepads common.h namespace_ ovr_ levelID gameMode1
    DecalFont RECTMENU otMem primMem backBuffer pushBuffer DrawOTag DrawPrim MainFrame MainMain)
set(host_tokens SDL_ Audio audio Howl HOWL OtherFX malloc calloc realloc "free(" alloca fopen printf FILE)
set(lockstep_tokens
    Lockstep lockstep LOCKSTEP Netplay netplay NETPLAY Lobby lobby Session session Bundle bundle
    TakeFrameInputs OnTakeResult Peer peer Digest digest)
set(section5_tokens
    TopologyLease topology_lease Lease lease Acquire Activate Publish
    Checkpoint checkpoint Replay replay NativeCanonical NATIVE_CANONICAL REPLAY CHECKPOINT LEASE
    Topology topology TOPOLOGY Retire LOAD_Hub_ReadFile)

# 1. The hold module.
ctr_read_source("${hold_source}" hold)
ctr_read_source("${hold_header}" hold_h)
ctr_strip_comments("${hold_source}" "${hold}" hold_code)
ctr_strip_comments("${hold_header}" "${hold_h}" hold_h_code)
ctr_require("${hold_source}" "${hold_code}" "void MainArcadeRaceHold_Run(MainArcadeRaceHoldStepFn step, void *context, struct MainArcadeRaceHoldResult *result)")
foreach(pair "${hold_source}|hold_code" "${hold_header}|hold_h_code")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 code_variable)
    foreach(term IN LISTS vblank_tokens input_tokens game_tokens host_tokens lockstep_tokens section5_tokens)
        ctr_forbid("${relative_path}" "${${code_variable}}" "${term}")
    endforeach()
    ctr_forbid("${relative_path}" "${${code_variable}}" "static")
    if(${code_variable} MATCHES "(^|[^A-Za-z0-9_])LOAD_")
        message(FATAL_ERROR "${prefix}: a retail LOAD_ name is used in ${relative_path}")
    endif()
endforeach()
# Its only platform calls.
string(REGEX MATCHALL "Platform_[A-Za-z0-9_]+" platform_names "${hold_code}")
list(REMOVE_DUPLICATES platform_names)
list(SORT platform_names)
if(NOT "${platform_names}" STREQUAL "Platform_HostClockUs;Platform_HostWaitMs;Platform_PollHostEvents;Platform_PresentVRAMDisplayBanner;Platform_PresentVRAMDisplayBannerGlyphs")
    message(FATAL_ERROR "${prefix}: ${hold_source} may call only Platform_PollHostEvents, Platform_HostClockUs, Platform_HostWaitMs, Platform_PresentVRAMDisplayBanner, and Platform_PresentVRAMDisplayBannerGlyphs (found '${platform_names}')")
endif()
string(REGEX MATCHALL "Platform_[A-Za-z0-9_]+" header_platform_names "${hold_h_code}")
if(NOT "${header_platform_names}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ${hold_header} must name no platform call (found '${header_platform_names}')")
endif()
# The includes.
string(REGEX MATCHALL "#[ \t]*include[^\n]*" hold_includes "${hold_code}")
if(NOT "${hold_includes}" STREQUAL "#include <platform.h>;#include <stddef.h>;#include <stdint.h>;#include <string.h>;#include \"MAIN/MainArcadeRaceHold.h\";#include \"MAIN/MainArcadeRaceHoldCore.h\"")
    message(FATAL_ERROR "${prefix}: ${hold_source} includes outside its allow-list (found '${hold_includes}')")
endif()
string(REGEX MATCHALL "#[ \t]*include[^\n]*" hold_h_includes "${hold_h_code}")
if(NOT "${hold_h_includes}" STREQUAL "#include <stdint.h>")
    message(FATAL_ERROR "${prefix}: ${hold_header} may include only <stdint.h> (found '${hold_h_includes}')")
endif()
# One CTR_NATIVE block: the first directive opens it and the last closes it.
string(REGEX MATCHALL "#[ \t]*(if|ifdef|ifndef|elif|else|endif)[^\n]*" hold_conditionals "${hold_code}")
if(NOT "${hold_conditionals}" STREQUAL "#if defined(CTR_NATIVE);#endif")
    message(FATAL_ERROR "${prefix}: ${hold_source} must be exactly one #if defined(CTR_NATIVE) block (found '${hold_conditionals}')")
endif()
string(STRIP "${hold_code}" hold_trimmed)
if(NOT hold_trimmed MATCHES "^#if defined\\(CTR_NATIVE\\)\n" OR NOT hold_trimmed MATCHES "\n#endif$")
    message(FATAL_ERROR "${prefix}: ${hold_source} must open with #if defined(CTR_NATIVE) and close with its #endif")
endif()

# 2. The LR-9 order of one iteration.
string(REGEX REPLACE "[ \t\n]+" " " hold_flat "${hold_code}")
string(REGEX REPLACE "[ \t\n]+" " " hold_h_flat "${hold_h_code}")
ctr_require("${hold_source}" "${hold_flat}"
    "void MainArcadeRaceHold_RunMode(MainArcadeRaceHoldStepFn step, void *context, uint32_t mode, const struct NativeHoldBannerGlyphs *glyphs, struct MainArcadeRaceHoldResult *result)")
ctr_block("${hold_source}" "${hold_code}" "void MainArcadeRaceHold_RunMode(" run_body)
ctr_block("${hold_source} (MainArcadeRaceHold_RunMode)" "${run_body}" "for (;;)" loop_body)
ctr_require_order("${hold_source} (the hold loop)" "${loop_body}"
    "Platform_PollHostEvents();"
    "MainArcadeRaceHoldCore_Pump(&core, Platform_HostClockUs())"
    "step(context, core.periods,"
    "break;"
    "MAIN_ARCADE_RACE_HOLD_DRAW_BANNER"
    "Platform_PresentVRAMDisplayBannerGlyphs(MAIN_ARCADE_RACE_HOLD_BANNER_TEXT, glyphs)"
    "Platform_PresentVRAMDisplayBanner(MAIN_ARCADE_RACE_HOLD_BANNER_TEXT)"
    "Platform_HostWaitMs(MAIN_ARCADE_RACE_HOLD_WAIT_MS);")
foreach(name IN ITEMS Platform_PollHostEvents Platform_HostWaitMs Platform_PresentVRAMDisplayBanner Platform_PresentVRAMDisplayBannerGlyphs
        MainArcadeRaceHoldCore_Pump)
    ctr_count_identifier("${hold_code}" "${name}" name_hits)
    if(NOT name_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${hold_source} must call ${name} exactly once, in the loop (found ${name_hits})")
    endif()
endforeach()
ctr_require("${hold_header}" "${hold_h_code}" "#define MAIN_ARCADE_RACE_HOLD_BANNER_TEXT \"WAITING FOR OPPONENT\"")
ctr_require("${hold_header}" "${hold_h_code}" "#define MAIN_ARCADE_RACE_HOLD_WAIT_MS 1u")

# 6. The mode gates only the banner (LR-S10 part 2), and the glyph table
#    only which present draws it (LR-S11).
ctr_require("${hold_header}" "${hold_h_code}" "#define MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER 0u\n")
ctr_require("${hold_header}" "${hold_h_code}" "#define MAIN_ARCADE_RACE_HOLD_MODE_BANNER 1u\n")
ctr_require("${hold_header}" "${hold_h_flat}"
    "struct NativeHoldBannerGlyphs; void MainArcadeRaceHold_RunMode(MainArcadeRaceHoldStepFn step, void *context, uint32_t mode, const struct NativeHoldBannerGlyphs *glyphs, struct MainArcadeRaceHoldResult *result);")
ctr_require("${hold_header}" "${hold_h_code}"
    "void MainArcadeRaceHold_Run(MainArcadeRaceHoldStepFn step, void *context, struct MainArcadeRaceHoldResult *result);")
set(banner_guard "if ((mode != MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER) && ((flags & MAIN_ARCADE_RACE_HOLD_DRAW_BANNER) != 0u))")
ctr_block("${hold_source} (the hold loop)" "${loop_body}" "${banner_guard}" banner_block)
string(REGEX REPLACE "[ \t\n]+" " " banner_flat "${banner_block}")
if(NOT banner_flat STREQUAL "{ int presented; bannersDue++; if (glyphs != NULL) { presented = Platform_PresentVRAMDisplayBannerGlyphs(MAIN_ARCADE_RACE_HOLD_BANNER_TEXT, glyphs); } else { presented = Platform_PresentVRAMDisplayBanner(MAIN_ARCADE_RACE_HOLD_BANNER_TEXT); } if (presented != 0) { bannersPresented++; } }")
    message(FATAL_ERROR "${prefix}: the banner guard of ${hold_source} must hold the banner count and one present only, the glyph present for a table and the block present for none (found '${banner_flat}')")
endif()
ctr_count_identifier("${run_body}" "mode" run_mode_hits)
ctr_count_identifier("${hold_code}" "mode" hold_mode_hits)
if(NOT run_mode_hits EQUAL 1 OR NOT hold_mode_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${hold_source} may name mode only in RunMode's parameter and its banner guard (found ${run_mode_hits} in the body, ${hold_mode_hits} in the file)")
endif()
# The table is handed on, never read: glyphs is named only in RunMode's
# parameter, the NULL test, and the glyph present, and never dereferenced.
ctr_count_identifier("${banner_block}" "glyphs" banner_glyphs_hits)
ctr_count_identifier("${hold_code}" "glyphs" hold_glyphs_hits)
if(NOT banner_glyphs_hits EQUAL 2 OR NOT hold_glyphs_hits EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${hold_source} may name glyphs only in RunMode's parameter, the banner guard's NULL test, and the glyph present (found ${banner_glyphs_hits} in the guard, ${hold_glyphs_hits} in the file)")
endif()
foreach(term IN ITEMS "glyphs->" "glyphs[" "NativeHoldBannerGlyph " "native_hold_banner")
    ctr_forbid("${hold_source}" "${hold_code}" "${term}")
    ctr_forbid("${hold_header}" "${hold_h_code}" "${term}")
endforeach()
ctr_block("${hold_source}" "${hold_code}" "void MainArcadeRaceHold_Run(MainArcadeRaceHoldStepFn step" run_wrapper)
string(REGEX REPLACE "[ \t\n]+" " " run_wrapper_flat "${run_wrapper}")
if(NOT run_wrapper_flat STREQUAL "{ MainArcadeRaceHold_RunMode(step, context, MAIN_ARCADE_RACE_HOLD_MODE_BANNER, NULL, result); }")
    message(FATAL_ERROR "${prefix}: MainArcadeRaceHold_Run must be exactly RunMode with the banner in the block font (no glyph table: the roster proof's hold is unchanged by LR-S11) (found '${run_wrapper_flat}')")
endif()
ctr_count_identifier("${hold_code}" "MainArcadeRaceHold_RunMode" hold_run_mode_hits)
if(NOT hold_run_mode_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${hold_source} must define MainArcadeRaceHold_RunMode and call it once, from Run (found ${hold_run_mode_hits})")
endif()

# 3. The period core is pure.
set(core_call_tokens Platform_ platform.h platform/ native_ Native NATIVE_ MainArcadeRaceHold_ MainArcadeRosterProof
    MainArcadeRaceSetup MainArcadeLink clock "time(" "time.h" QueryPerformance stdio)
foreach(relative_path IN ITEMS "${core_header}" "${core_source}")
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${relative_path}" "${source}" code)
    string(FIND "${code}" "MainArcadeRaceHoldCore_Pump(" pump_at)
    if(pump_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: comment stripping lost the code of ${relative_path}")
    endif()
    foreach(term IN LISTS vblank_tokens input_tokens game_tokens host_tokens lockstep_tokens section5_tokens core_call_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
    ctr_forbid("${relative_path}" "${code}" "static")
    string(REGEX MATCHALL "#[ \t]*include[^\n]*" include_lines "${code}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"MAIN/MainArcadeRaceHoldCore\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|elif|else)[ \t\n]" conditionals "${code}")
    if(NOT "${conditionals}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} must not use conditional compilation")
    endif()
endforeach()
ctr_read_source("${core_header}" core_h)
ctr_require("${core_header}" "${core_h}" "#define MAIN_ARCADE_RACE_HOLD_PERIOD_US 33435u\n")
ctr_require("${core_header}" "${core_h}" "#define MAIN_ARCADE_RACE_HOLD_GRACE_PERIODS 10u\n")

# 4. The library and the unity chain.
ctr_read_source("CMakeLists.txt" cmake)
string(REGEX MATCHALL "add_library\\([ \t\n]*${core_target}[ \t\n][^)]*\\)" add_calls "${cmake}")
list(LENGTH add_calls add_call_count)
if(NOT add_call_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one add_library(${core_target} ...), found ${add_call_count}")
endif()
list(GET add_calls 0 add_call)
string(REGEX REPLACE "[ \t\n]+" " " add_call "${add_call}")
if(NOT add_call STREQUAL "add_library(${core_target} STATIC ${core_source})")
    message(FATAL_ERROR "${prefix}: ${core_target} must build exactly ${core_source} (found '${add_call}')")
endif()
string(REGEX MATCHALL "MainArcadeRaceHoldCore\\.c" source_mentions "${cmake}")
list(LENGTH source_mentions source_mention_count)
if(NOT source_mention_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${core_source} must be named by exactly one target in CMakeLists.txt (found ${source_mention_count})")
endif()
string(REGEX MATCHALL "target_link_libraries\\([ \t\n]*${core_target}[ \t\n][^)]*\\)" core_link_calls "${cmake}")
if(NOT "${core_link_calls}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ${core_target} must link nothing")
endif()
string(FIND "${cmake}" "add_library(${core_target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
ctr_require_order("CMakeLists.txt (${core_target})" "${target_block}"
    "set_target_properties(${core_target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF")
string(REGEX MATCHALL "target_link_libraries\\([ \t\n]*[A-Za-z0-9_]+[^)]*\\)" all_link_calls "${cmake}")
set(linked_by_test 0)
set(linked_by_game 0)
foreach(link_call IN LISTS all_link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\n]*([A-Za-z0-9_]+).*$" "\\1" linking_target "${link_call}")
    string(REGEX MATCH "[ \t\n]${core_target}[ \t\n)]" names_core "${link_call}")
    if(names_core)
        if((linking_target STREQUAL "main_arcade_race_hold_core_test") OR (linking_target STREQUAL "main_arcade_race_hold_test"))
            set(linked_by_test 1)
        elseif(linking_target STREQUAL "ctr_native")
            set(linked_by_game 1)
        else()
            message(FATAL_ERROR "${prefix}: ${linking_target} links ${core_target}; only main_arcade_race_hold_core_test, main_arcade_race_hold_test, and ctr_native may")
        endif()
    endif()
endforeach()
if(NOT linked_by_test OR NOT linked_by_game)
    message(FATAL_ERROR "${prefix}: ${core_target} must be linked by main_arcade_race_hold_core_test and ctr_native")
endif()
ctr_read_source("game/game_unity.h" unity)
ctr_forbid("game/game_unity.h" "${unity}" "MainArcadeRaceHoldCore")
string(REGEX MATCHALL "MainArcadeRaceHold\\.c\"" hold_unity_hits "${unity}")
list(LENGTH hold_unity_hits hold_unity_count)
if(NOT hold_unity_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: game/game_unity.h must include MAIN/MainArcadeRaceHold.c exactly once (found ${hold_unity_count})")
endif()
ctr_require_order("game/game_unity.h" "${unity}"
    "#include \"230.c\"" "#include \"MAIN/MainArcadeRaceHold.c\"" "#include \"MAIN/MainArcadeRosterProof.c\"")

# 5. Who names the hold.
file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(scanned 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    file(READ "${path}" source)
    string(FIND "${source}" "MainArcadeRaceHold_Run" raw_at)
    if(raw_at EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${relative_path}" "${source}" code)
    ctr_count_identifier("${code}" "MainArcadeRaceHold_Run" run_hits)
    if(run_hits GREATER 0 AND NOT relative_path MATCHES "^game/MAIN/MainArcadeRaceHold\\.(c|h)$" AND NOT relative_path STREQUAL "${proof_source}")
        message(FATAL_ERROR "${prefix}: ${relative_path} names MainArcadeRaceHold_Run; only the hold module and ${proof_source} may")
    endif()
    ctr_count_identifier("${code}" "MainArcadeRaceHold_RunMode" run_mode_name_hits)
    if(run_mode_name_hits GREATER 0 AND NOT relative_path MATCHES "^game/MAIN/MainArcadeRaceHold\\.(c|h)$" AND NOT relative_path STREQUAL "${launch_source}")
        message(FATAL_ERROR "${prefix}: ${relative_path} names MainArcadeRaceHold_RunMode; only the hold module and ${launch_source} may")
    endif()
endforeach()
# 6 (cont). The race caller's race hold is with the banner and its glyph
#    table (LR-S11: the game font; LR-S10 part 2 held without the banner);
#    its only other hold is the internal freeze injection (LR-73), without
#    the banner or a table; it never presents a banner itself; the roster
#    proof never passes a table.
ctr_read_source("${launch_source}" launch)
ctr_strip_comments("${launch_source}" "${launch}" launch_code)
ctr_count_identifier("${launch_code}" "MainArcadeRaceHold_RunMode" launch_run_mode_hits)
ctr_count_identifier("${launch_code}" "MainArcadeRaceHold_Run" launch_run_hits)
if(NOT launch_run_mode_hits EQUAL 2 OR NOT launch_run_hits EQUAL 0)
    message(FATAL_ERROR "${prefix}: ${launch_source} must call MainArcadeRaceHold_RunMode exactly twice (the race hold and the freeze injection) and never MainArcadeRaceHold_Run (found ${launch_run_mode_hits} and ${launch_run_hits})")
endif()
# (';' is masked while the calls are listed: a CMake list would split on it.)
string(REPLACE ";" "@SEMI@" launch_code_masked "${launch_code}")
string(REGEX MATCHALL "MainArcadeRaceHold_RunMode\\([^@]*\\)@SEMI@" launch_hold_calls "${launch_code_masked}")
set(launch_banner_holds 0)
set(launch_freeze_holds 0)
foreach(launch_hold_call IN LISTS launch_hold_calls)
    string(REPLACE "@SEMI@" ";" launch_hold_call "${launch_hold_call}")
    if(launch_hold_call MATCHES "^MainArcadeRaceHold_RunMode\\([A-Za-z0-9_]+, [^,]+, MAIN_ARCADE_RACE_HOLD_MODE_BANNER, &glyphs, [^,]+\\);$")
        math(EXPR launch_banner_holds "${launch_banner_holds} + 1")
    elseif(launch_hold_call STREQUAL "MainArcadeRaceHold_RunMode(MainArcadeRaceLaunch_FreezeStep, NULL, MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER, NULL, &freeze);")
        math(EXPR launch_freeze_holds "${launch_freeze_holds} + 1")
    else()
        message(FATAL_ERROR "${prefix}: ${launch_source} holds neither with MAIN_ARCADE_RACE_HOLD_MODE_BANNER and its glyph table &glyphs (LR-S11) nor as the freeze injection (LR-73; found '${launch_hold_call}')")
    endif()
endforeach()
if(NOT launch_banner_holds EQUAL 1 OR NOT launch_freeze_holds EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${launch_source} must hold once with the banner and its glyph table and once as the freeze injection (found ${launch_banner_holds} and ${launch_freeze_holds})")
endif()
ctr_count_identifier("${launch_code}" "MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER" launch_no_banner_hits)
if(NOT launch_no_banner_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${launch_source} may name MAIN_ARCADE_RACE_HOLD_MODE_NO_BANNER only in the freeze injection's hold (found ${launch_no_banner_hits})")
endif()
ctr_forbid("${launch_source}" "${launch_code}" "Platform_PresentVRAMDisplayBanner")
ctr_read_source("${proof_source}" proof)
foreach(term IN ITEMS NativeHoldBannerGlyphs native_hold_banner MainArcadeRaceLaunch_BannerGlyphs MainArcadeRaceHold_RunMode)
    string(FIND "${proof}" "${term}" proof_glyph_at)
    if(NOT proof_glyph_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${proof_source} names ${term}; the roster proof's hold stays in the block font (LR-S11)")
    endif()
endforeach()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()
ctr_read_source("${proof_source}" proof)
ctr_strip_comments("${proof_source}" "${proof}" proof_code)
ctr_count_identifier("${proof_code}" "MainArcadeRaceHold_Run" proof_run_hits)
if(NOT proof_run_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${proof_source} must call MainArcadeRaceHold_Run exactly once (found ${proof_run_hits})")
endif()
ctr_block("${proof_source}" "${proof_code}" "static void MainArcadeRosterProof_Hold(const struct GameTracker *gGT)" proof_hold_body)
ctr_require("${proof_source} (MainArcadeRosterProof_Hold)" "${proof_hold_body}"
    "MainArcadeRaceHold_Run(MainArcadeRosterProof_HoldStep, NULL, &result);")
ctr_count_identifier("${proof_code}" "MainArcadeRosterProof_Hold" proof_hold_hits)
if(NOT proof_hold_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${proof_source} must define MainArcadeRosterProof_Hold and call it once (found ${proof_hold_hits})")
endif()
ctr_block("${proof_source}" "${proof_code}" "void MainArcadeRosterProof_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)" frame_body)
ctr_require_order("${proof_source} (MainArcadeRosterProof_Frame)" "${frame_body}"
    "case MAIN_ARCADE_ROSTER_PROOF_VALIDATED:"
    "(state->raceTick == NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK)"
    "MainArcadeRosterProof_Hold(gGT);"
    "default:")
