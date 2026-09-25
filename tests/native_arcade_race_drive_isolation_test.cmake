# Structural isolation for the linked-race drive core
# (native_arcade_race_drive, docs/LOCKSTEP_RACE_MILESTONE.md LR-1, slice
# LR-S7). The core is pure over caller-owned values: no SDL, OS networking,
# clock, heap, stdio, game code, game global, or platform call, no
# topology-lease token, and no checkpoint, replay, or canonical-state token
# beyond the one canonical type name it may use (NativeCanonicalInputPadV1;
# LR-S8 adds the state type under its own review). Both files include only
# stdint.h, stddef.h, string.h, the core's own header, and
# platform/native_canonical_state.h; the library stays portable C17 with
# extensions off and links nothing. Only the allowed targets
# (drive_allowed_linkers: the unit test for now) may link the core.
#
# The token ban is judged on the code with every // and /* */ comment
# removed in one left-to-right pass (each comment replaced by a space), so
# the documentation may name what the code must not use. The scan asserts it
# found both files and the normalization entry point, so it cannot pass
# trivially.
#
# LR-S8 extends these rules: it adds the lockstep session header to the
# include allow-list (drive_allowed_includes), the session library to the
# link allow-list (drive_allowed_links), the state type to
# drive_allowed_canonical_names, and its entry points to
# drive_required_code; LR-S8 and LR-S9 add the targets that link the core
# to drive_allowed_linkers.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade race drive isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term what)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${what} of ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

# Removes every /* */ and // comment in one left-to-right pass, replacing
# each with a space (as tests/native_arcade_launch_isolation_test.cmake
# does). A /* left over afterwards opened a comment that never closed.
function(ctr_strip_comments label source out_var)
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\r\n]*" " " stripped "${source}")
    string(FIND "${stripped}" "/*" open_at)
    if(NOT open_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: unterminated /* comment in ${label}")
    endif()
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# 0. The comment stripper itself: comments go, code stays.
ctr_strip_comments("self-check" "code1 /* SDL\n replay */ code2 // malloc\ncode3 /**/code4 /** x **/ code5 /* a // b */ code6" self_check)
foreach(term IN ITEMS SDL replay malloc "/*" "*/" "//")
    ctr_forbid("self-check" "${self_check}" "${term}" "stripped code")
endforeach()
foreach(term IN ITEMS code1 code2 code3 code4 code5 code6)
    ctr_require("self-check" "${self_check}" "${term}")
endforeach()

set(drive_header "include/platform/native_arcade_race_drive.h")
set(drive_source "platform/native_arcade_race_drive.c")
set(drive_files "${drive_header}" "${drive_source}")
set(drive_target ctr_native_arcade_race_drive)

# The only headers either file may include.
set(drive_allowed_includes
    "<stdint\\.h>" "<stddef\\.h>" "<string\\.h>"
    "\"platform/native_arcade_race_drive\\.h\""
    "\"platform/native_canonical_state\\.h\"")

# The only libraries the core may link (none until LR-S8).
set(drive_allowed_links "")

# The only targets that may link the core (the unit test until LR-S8/LR-S9).
set(drive_allowed_linkers native_arcade_race_drive_test)

# Code each file must contain after comment stripping, so a scan that lost
# the code (or the wrong file) fails.
set(drive_required_code_header
    "void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out);"
    "void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out);"
    "#define NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL 0x41u"
    "#define NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG  0x73u"
    "#define NATIVE_ARCADE_RACE_DRIVE_START_MASK 0x0008u")
set(drive_required_code_source
    "void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out)"
    "void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out)")

# The canonical type names the core may use; stripped before the ban so
# that every other NativeCanonical token still trips it.
set(drive_allowed_canonical_names NativeCanonicalInputPadV1)

# 1. Banned tokens, by category, judged on the comment-free code.
set(sdl_network_tokens SDL winsock WinSock WSA sockaddr)
set(clock_tokens "time(" "clock(" QueryPerformance)
set(heap_tokens malloc calloc realloc "free(" alloca)
set(io_tokens stdio printf)
set(game_tokens "game/" gGT sdata Platform_)
set(lease_tokens TopologyLease Acquire Activate Capture Publish Retire LOAD_Hub_ReadFile)
set(state_tokens Checkpoint checkpoint NativeReplay Replay replay NativeCanonical)

set(scanned_files 0)
foreach(relative_path IN LISTS drive_files)
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${relative_path}" "${source}" code)

    if(relative_path STREQUAL drive_header)
        set(required "${drive_required_code_header}")
    else()
        set(required "${drive_required_code_source}")
    endif()
    foreach(term IN LISTS required)
        ctr_require("${relative_path} (comment-free)" "${code}" "${term}")
    endforeach()

    set(banned_code "${code}")
    foreach(name IN LISTS drive_allowed_canonical_names)
        string(REPLACE "${name}" " " banned_code "${banned_code}")
    endforeach()
    foreach(term IN LISTS sdl_network_tokens clock_tokens heap_tokens io_tokens game_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${banned_code}" "${term}" "the code")
    endforeach()

    # 2. #include lines (in the raw text, so a commented-out include counts
    #    too) may only name the allowed headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    list(JOIN drive_allowed_includes "|" allowed_pattern)
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(${allowed_pattern})[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    math(EXPR scanned_files "${scanned_files} + 1")
endforeach()
if(NOT scanned_files EQUAL 2)
    message(FATAL_ERROR "${prefix}: scanned ${scanned_files} files, expected 2")
endif()

# 1b. The ban bites: a canonical token other than the allowed name (the
#     state type included, until LR-S8 allows it) is still caught after the
#     allowed name is stripped, and the allowed name alone is not.
foreach(probe IN ITEMS "struct NativeCanonicalInputPadV1 a, struct NativeCanonicalStateV4 b"
                       "struct NativeCanonicalInputPadV1 a, NativeCanonicalStateV1_Init(&c)")
    foreach(name IN LISTS drive_allowed_canonical_names)
        string(REPLACE "${name}" " " probe "${probe}")
    endforeach()
    string(FIND "${probe}" "NativeCanonical" probe_at)
    if(probe_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: the canonical-name allowance hides other NativeCanonical tokens")
    endif()
endforeach()
set(probe "struct NativeCanonicalInputPadV1 a")
foreach(name IN LISTS drive_allowed_canonical_names)
    string(REPLACE "${name}" " " probe "${probe}")
endforeach()
string(FIND "${probe}" "NativeCanonical" probe_at)
if(NOT probe_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: the canonical-name allowance does not strip NativeCanonicalInputPadV1")
endif()

ctr_read_source("CMakeLists.txt" cmake)

# 3. C17, no extensions, on the core target, in order.
string(FIND "${cmake}" "add_library(${drive_target} STATIC platform/native_arcade_race_drive.c)" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing add_library(${drive_target} STATIC platform/native_arcade_race_drive.c) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${drive_target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing set_target_properties(${drive_target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${drive_target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "${prefix}: ${drive_target} C17/no-extensions properties are out of order")
endif()

# 4. The core links only the allowed libraries (none until LR-S8): every
#    target_link_libraries call on the target, together, names exactly
#    drive_allowed_links.
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${drive_target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
set(link_items "")
foreach(link_call IN LISTS link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${drive_target}[ \t\r\n]+" "" link_body "${link_call}")
    string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
    string(REGEX REPLACE "[ \t\r\n]+" ";" call_items "${link_body}")
    list(APPEND link_items ${call_items})
endforeach()
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
list(SORT link_items)
set(expected_links ${drive_allowed_links})
list(SORT expected_links)
if(NOT "${link_items}" STREQUAL "${expected_links}")
    message(FATAL_ERROR "${prefix}: ${drive_target} must link exactly '${expected_links}' (found '${link_items}')")
endif()

# 5. Only the allowed targets link the core: every target_link_libraries
#    call (other than the core's own) that names ${drive_target} as an item
#    belongs to a target in drive_allowed_linkers, and every allowed linker
#    was found, so the scan cannot pass by missing the calls.
string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" all_link_calls "${cmake}")
set(found_linkers "")
foreach(link_call IN LISTS all_link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*" "" link_body "${link_call}")
    string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
    string(REGEX REPLACE "[ \t\r\n]+" ";" call_items "${link_body}")
    list(GET call_items 0 linker)
    list(REMOVE_AT call_items 0)
    if(linker STREQUAL drive_target)
        continue()
    endif()
    set(names_core FALSE)
    foreach(item IN LISTS call_items)
        if(item MATCHES "(^|[^A-Za-z0-9_])${drive_target}($|[^A-Za-z0-9_])")
            set(names_core TRUE)
        endif()
    endforeach()
    if(names_core)
        if(NOT linker IN_LIST drive_allowed_linkers)
            message(FATAL_ERROR "${prefix}: ${linker} links ${drive_target} but is not in drive_allowed_linkers ('${drive_allowed_linkers}')")
        endif()
        list(APPEND found_linkers "${linker}")
    endif()
endforeach()
foreach(linker IN LISTS drive_allowed_linkers)
    if(NOT linker IN_LIST found_linkers)
        message(FATAL_ERROR "${prefix}: allowed linker ${linker} not found linking ${drive_target}; the scan is broken")
    endif()
endforeach()
