# Structural isolation for the race launch decision core
# (game/MAIN/MainArcadeRaceLaunchCore.{c,h}, docs/RACE_LAUNCH_MILESTONE.md
# section 4 RL-8..RL-11, section 5, slice RL-S7):
#  1. the module is pure: its code (comments removed) names no game global
#     (gGT, sdata, Loading, numPlyrNextGame, levelID, gameMode1, ...), no
#     retail header or LOAD_ name, no platform, host, netplay, flow, link hook,
#     race setup, roster proof, input, or sound call, no heap, stdio, or clock,
#     and it keeps no mutable static state;
#  2. section 5 ban, on the comment-free code: no topology-lease acquire,
#     activate, capture, or publish token, and no checkpoint, replay, or
#     NativeCanonical token (the token list of
#     tests/native_arcade_launch_isolation_test.cmake), and no Retire or
#     LOAD_Hub_ReadFile either;
#  3. its includes are limited to stdint.h, stddef.h, string.h, and its own
#     header, with no conditional compilation beyond the header guard (the
#     setup status is mirrored, not included);
#  4. ctr_native_arcade_race_launch_core builds only the module's .c, the .c
#     is named by exactly one CMake target, the library links nothing, and it
#     is C17 with extensions off;
#  5. it is never unity-included, and among the tests only its unit test
#     links it, plus the pure cross-module return interleave test
#     (main_arcade_link_return_interleave_test, race-launch risk 10), which
#     links only it and the link policy; since RL-S8b ctr_native links it
#     too (the live race caller, game/MAIN/MainArcadeRaceLaunch.c, drives
#     it);
#  6. the three RL-8/RL-10 bounds are defined literally, exactly once:
#     launchWindowTimeoutTicks 900, launchValidateTimeoutTicks 1800, and
#     launchRehearsalTicks 150; and the setup status mirrors match the order
#     of enum MainArcadeRaceSetupStatus. Since LR-S10 part 1 (LR-59) the
#     driver slot count 8 and the finished bit mirror 0x2000000 are pinned
#     the same way, the retail ACTION_RACE_FINISHED still has that value,
#     and the race caller static-asserts the mirror.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "race launch core isolation")
set(module_header "game/MAIN/MainArcadeRaceLaunchCore.h")
set(module_source "game/MAIN/MainArcadeRaceLaunchCore.c")
set(target ctr_native_arcade_race_launch_core)

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

# Sets out_var to the static declarations in code that are mutable state
# (tests/main_arcade_race_setup_core_isolation_test.cmake): every static must
# be a function or a const object that is not a pointer to mutable data.
function(ctr_mutable_statics code out_var)
    set(violations "")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])static[ \t\r\n][^;{=]*" heads "${code}")
    foreach(head IN LISTS heads)
        string(REGEX REPLACE "^[^s]*static" "static" head "${head}")
        string(REGEX REPLACE "[ \t\r\n]+" " " head "${head}")
        string(STRIP "${head}" head)
        if(head MATCHES "^static( inline)? [A-Za-z_][A-Za-z0-9_ *]*[ *]([A-Za-z_][A-Za-z0-9_]*) ?\\(" AND NOT head MATCHES "\\( ?\\*")
            continue()
        endif()
        if(head MATCHES "^static const " AND NOT head MATCHES "\\(")
            if(NOT head MATCHES "\\*" OR head MATCHES "\\* ?const ")
                continue()
            endif()
        endif()
        list(APPEND violations "${head}")
    endforeach()
    set(${out_var} "${violations}" PARENT_SCOPE)
endfunction()

# 0. The helpers themselves work.
ctr_strip_comments("self-check" "code1 /* Lease\n replay */ code2 // lease\ncode3 /**/code4 /** x **/ code5 /* a // b */ code6" self_check)
foreach(term IN ITEMS Lease lease replay "/*" "*/" "//")
    ctr_forbid("self-check" "${self_check}" "${term}")
endforeach()
foreach(term IN ITEMS code1 code2 code3 code4 code5 code6)
    ctr_require("self-check" "${self_check}" "${term}")
endforeach()
ctr_mutable_statics("static int counter;\nstatic uint8_t buffer[4] = {0};\nstatic const uint8_t *cursor;\nstatic int (*hook)(void);\nvoid F(void) { static int calls; }\n" probe_bad)
list(LENGTH probe_bad probe_bad_count)
ctr_mutable_statics("static int IsOk(const struct A *a)\n{\n}\nstatic const uint8_t k_table[2] = { 1, 2 };\nstatic const char *const k_name = \"x\";\n" probe_good)
list(LENGTH probe_good probe_good_count)
if(NOT probe_bad_count EQUAL 5 OR NOT probe_good_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: the mutable-static scan is broken (flagged ${probe_bad_count} of 5 bad, ${probe_good_count} of 0 good)")
endif()

# 1. Purity: game globals and retail headers, calls into the game, platform,
#    host, setup, proof, input, or sound, heap, stdio, and clock.
set(game_tokens
    gGT sdata "data." GameTracker GamepadSystem gGamepads common.h namespace_ ovr_
    Loading numPlyrNextGame boolDemoMode mainMenuState levelID gameMode1 LOADING
    MainRaceTrack RECTMENU RectMenu MM_ D230 MainFrame MainInit MainMain MEMPACK)
set(call_tokens
    Platform_ platform.h platform/ native_ Native NATIVE_
    MainArcadeRaceSetup MainArcadeLink MainArcadeRosterProof MainArcadeBotSetup MainArcadeRoster
    OtherFX countSounds CountSounds Howl HOWL SDL_)
set(runtime_tokens
    malloc calloc realloc "free(" alloca
    stdio printf fopen fwrite "puts(" FILE
    clock "time(" "time.h" QueryPerformance)
# 2. Section 5 ban (tests/native_arcade_launch_isolation_test.cmake), plus
#    Retire and the hub read.
set(section5_tokens
    TopologyLease topology_lease Lease lease Acquire Activate Capture Publish
    Checkpoint checkpoint Replay replay NativeCanonical
    NATIVE_CANONICAL REPLAY CHECKPOINT LEASE ACQUIRE ACTIVATE CAPTURE PUBLISH
    Topology topology TOPOLOGY capture Retire LOAD_Hub_ReadFile)

foreach(relative_path IN ITEMS "${module_header}" "${module_source}")
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${relative_path}" "${source}" code)
    string(FIND "${code}" "MainArcadeRaceLaunchCore_Step(" step_at)
    if(step_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: comment stripping lost the code of ${relative_path}")
    endif()
    foreach(term IN LISTS game_tokens call_tokens runtime_tokens section5_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
    # A retail load name starts an identifier with LOAD_.
    if(code MATCHES "(^|[^A-Za-z0-9_])LOAD_")
        message(FATAL_ERROR "${prefix}: a retail LOAD_ name is used in ${relative_path}")
    endif()
    ctr_mutable_statics("${code}" mutable_statics)
    if(NOT "${mutable_statics}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: mutable static state in ${relative_path}: '${mutable_statics}'")
    endif()

    # 3. Includes and conditional compilation.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${code}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"MAIN/MainArcadeRaceLaunchCore\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|elif|else)[ \t\r\n]" conditionals "${code}")
    if(NOT "${conditionals}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} must not use conditional compilation")
    endif()
endforeach()

# 4. The library: exactly the module's .c, linking nothing, C17.
ctr_read_source("CMakeLists.txt" cmake)
string(REGEX MATCHALL "add_library\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" add_calls "${cmake}")
list(LENGTH add_calls add_call_count)
if(NOT add_call_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one add_library(${target} ...), found ${add_call_count}")
endif()
list(GET add_calls 0 add_call)
string(REGEX REPLACE "[ \t\r\n]+" " " add_call "${add_call}")
if(NOT add_call STREQUAL "add_library(${target} STATIC ${module_source})")
    message(FATAL_ERROR "${prefix}: ${target} must build exactly ${module_source} (found '${add_call}')")
endif()
string(REGEX MATCHALL "MainArcadeRaceLaunchCore\\.c" source_mentions "${cmake}")
list(LENGTH source_mentions source_mention_count)
if(NOT source_mention_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${module_source} must be named by exactly one target in CMakeLists.txt (found ${source_mention_count} mentions)")
endif()
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: ${target} must link nothing (found ${link_call_count} target_link_libraries call)")
endif()
ctr_require("CMakeLists.txt" "${cmake}"
    "target_include_directories(${target} PUBLIC \${CMAKE_SOURCE_DIR}/include \${CMAKE_SOURCE_DIR}/game)")
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(properties_at EQUAL -1 OR standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "${prefix}: ${target} C17/no-extensions properties are out of order")
endif()

# 5. Never unity-included; the unit test links it, the pure return
#    interleave test links it with the link policy and nothing else, and no
#    other test does.
ctr_read_source("game/game_unity.h" unity)
ctr_forbid("game/game_unity.h" "${unity}" "MainArcadeRaceLaunchCore")
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*[A-Za-z0-9_]+[^)]*\\)" all_link_calls "${cmake}")
set(linked_by_test 0)
set(linked_by_game 0)
foreach(link_call IN LISTS all_link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*([A-Za-z0-9_]+).*$" "\\1" linking_target "${link_call}")
    string(REGEX MATCH "[ \t\r\n]${target}[ \t\r\n)]" names_module "${link_call}")
    if(names_module)
        if(linking_target STREQUAL "main_arcade_race_launch_core_test")
            set(linked_by_test 1)
        elseif(linking_target STREQUAL "ctr_native")
            set(linked_by_game 1)
        elseif(linking_target STREQUAL "main_arcade_link_return_interleave_test")
            string(REGEX REPLACE "[ \t\r\n]+" " " interleave_link_flat "${link_call}")
            if(NOT interleave_link_flat STREQUAL "target_link_libraries(main_arcade_link_return_interleave_test PRIVATE ctr_native_arcade_link_policy ctr_native_arcade_race_launch_core)")
                message(FATAL_ERROR "${prefix}: main_arcade_link_return_interleave_test must link only ctr_native_arcade_link_policy and ${target} (found '${interleave_link_flat}')")
            endif()
        else()
            message(FATAL_ERROR "${prefix}: ${linking_target} links ${target}; only main_arcade_race_launch_core_test, main_arcade_link_return_interleave_test (and ctr_native) may")
        endif()
    endif()
endforeach()
if(NOT linked_by_test)
    message(FATAL_ERROR "${prefix}: main_arcade_race_launch_core_test must link ${target}")
endif()
if(NOT linked_by_game)
    message(FATAL_ERROR "${prefix}: ctr_native must link ${target} (the RL-S8b race caller drives it)")
endif()

# 6. The bounds, literally and once each, and the setup status mirrors.
ctr_read_source("${module_header}" header)
foreach(define IN ITEMS
        "MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_WINDOW_TIMEOUT_TICKS 900u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS 1800u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_REHEARSAL_TICKS 150u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_IDLE 0u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_ARMED 1u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED 2u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED 3u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED 4u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED 5u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS 8u"
        "MAIN_ARCADE_RACE_LAUNCH_CORE_ACTION_RACE_FINISHED 0x2000000u")
    string(REPLACE " " ";" define_items "${define}")
    list(GET define_items 0 define_name)
    list(GET define_items 1 define_value)
    string(REGEX MATCHALL "#define ${define_name}[ \t]" define_hits "${header}")
    list(LENGTH define_hits define_count)
    if(NOT define_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${define_name} must be defined exactly once in ${module_header} (found ${define_count})")
    endif()
    # clang-format aligns the values, so any run of blanks may separate them.
    string(REGEX MATCH "(^|\n)#define ${define_name}[ \t]+${define_value}\n" define_line "${header}")
    if("${define_line}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${module_header} must define '${define}' literally")
    endif()
endforeach()
ctr_read_source("game/MAIN/MainArcadeRaceSetupCore.h" setup_core_header)
ctr_require("game/MAIN/MainArcadeRaceSetupCore.h" "${setup_core_header}"
    "enum MainArcadeRaceSetupStatus\n{\n\tMAIN_ARCADE_RACE_SETUP_IDLE = 0,\n\tMAIN_ARCADE_RACE_SETUP_ARMED,\n\tMAIN_ARCADE_RACE_SETUP_LAUNCHED,\n\tMAIN_ARCADE_RACE_SETUP_SEEDED,\n\tMAIN_ARCADE_RACE_SETUP_VALIDATED,\n\tMAIN_ARCADE_RACE_SETUP_FAILED\n};")
# The finished bit mirror (docs/LOCKSTEP_RACE_MILESTONE.md LR-59): the retail
# value it mirrors, and the race caller's static assert of it.
ctr_read_source("include/namespace_Vehicle.h" vehicle_header)
ctr_require("include/namespace_Vehicle.h" "${vehicle_header}" "\tACTION_RACE_FINISHED = 0x2000000,\n")
ctr_read_source("game/MAIN/MainArcadeRaceLaunch.c" caller_source)
string(REPLACE "\r\n" "\n" caller_source "${caller_source}")
ctr_require("game/MAIN/MainArcadeRaceLaunch.c" "${caller_source}"
    "_Static_assert((uint32_t)MAIN_ARCADE_RACE_LAUNCH_CORE_ACTION_RACE_FINISHED == (uint32_t)ACTION_RACE_FINISHED,")
