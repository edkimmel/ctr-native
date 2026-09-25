# Structural isolation for the linked-race drive core
# (native_arcade_race_drive, docs/LOCKSTEP_RACE_MILESTONE.md LR-1, slices
# LR-S7 and LR-S8). The core is pure over caller-owned values: no SDL, OS
# networking, clock, heap, stdio, game code, game global, or platform call,
# no topology-lease token, no checkpoint, replay, or canonical-state token
# beyond the two canonical type names it may use (NativeCanonicalInputPadV1,
# and NativeCanonicalStateV4 only as a const pointer, RecordLocalDigests'
# argument; no NativeCanonical function), and no peer-link, adapter, lobby,
# outcome-tracker, or host token: all I/O goes through the caller's
# callbacks (LR-41). Both files include only stdint.h, stddef.h, string.h,
# the core's own header, platform/native_canonical_state.h,
# platform/native_lockstep_session.h, and platform/native_match_config.h;
# the library stays portable C17 with extensions off and links exactly the
# lockstep session library. Only the allowed targets (drive_allowed_linkers:
# the unit test and, since LR-S9, the host glue) may link the core.
#
# The send order (LR-2, LR-29) is pinned structurally as far as text can:
# the source makes exactly one ComposeBundle call and one sendBundle call,
# each in a unit that first passes the NativeArcadeRaceDrive_MaySend guard,
# and the guard names the RUNNING mode, the first record, the recorded
# frame, and D. The unit test checks the order on every send.
#
# The token ban is judged on the code with every // and /* */ comment
# removed in one left-to-right pass (each comment replaced by a space), so
# the documentation may name what the code must not use. The allowed
# canonical names are stripped only as whole identifiers, so a longer name
# (NativeCanonicalStateV4_Validate) still trips the ban. The scan asserts it
# found both files and the API, so it cannot pass trivially.
#
# LR-S9 added the host glue (ctr_native_arcade_link_host) to
# drive_allowed_linkers: it runs the drive over the adapter (LR-1).

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

# Replaces every whole-identifier occurrence of each name with a space (a
# name inside a longer identifier stays), repeating until nothing changes so
# adjacent occurrences are all caught.
function(ctr_strip_names text out_var)
    set(result " ${text} ")
    foreach(name IN LISTS ARGN)
        set(previous "")
        while(NOT previous STREQUAL result)
            set(previous "${result}")
            string(REGEX REPLACE "([^A-Za-z0-9_])${name}([^A-Za-z0-9_])" "\\1 \\2" result "${result}")
        endwhile()
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
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
    "\"platform/native_canonical_state\\.h\""
    "\"platform/native_lockstep_session\\.h\""
    "\"platform/native_match_config\\.h\"")

# The only libraries the core may link: the lockstep session (which links
# the match config the core's FindRoleSlot call needs).
set(drive_allowed_links ctr_native_lockstep_session)

# The only targets that may link the core: the unit test and the host glue
# (LR-S9).
set(drive_allowed_linkers native_arcade_race_drive_test ctr_native_arcade_link_host)

# Code each file must contain after comment stripping, so a scan that lost
# the code (or the wrong file) fails.
set(drive_required_code_header
    "void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out);"
    "void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out);"
    "void NativeArcadeRaceDrive_DisconnectedPad(struct NativeCanonicalInputPadV1 *out);"
    "int NativeArcadeRaceDrive_Begin("
    "enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Step("
    "const struct NativeCanonicalStateV4 *state,"
    "enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Hold("
    "uint32_t NativeArcadeRaceDrive_LingerTick(struct NativeArcadeRaceDrive *drive, int onResults);"
    "int NativeArcadeRaceDrive_BannerDue(uint32_t periods);")
# Macros the header must define, "NAME VALUE" (any spacing between the two,
# so clang-format's macro alignment does not matter).
set(drive_required_macros_header
    "NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL 0x41u"
    "NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG 0x73u"
    "NATIVE_ARCADE_RACE_DRIVE_START_MASK 0x0008u"
    "NATIVE_ARCADE_RACE_DRIVE_MAX_INPUT_DELAY 3u"
    "NATIVE_ARCADE_RACE_DRIVE_START_GRACE_PERIODS 810u"
    "NATIVE_ARCADE_RACE_DRIVE_HOLD_GRACE_PERIODS 10u"
    "NATIVE_ARCADE_RACE_DRIVE_FINISH_GRACE_TICKS 900u"
    "NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS 15u"
    "NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT 18000u"
    "NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY NATIVE_LOCKSTEP_RING_CAPACITY")
set(drive_required_code_source
    "void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out)"
    "void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out)"
    "int NativeArcadeRaceDrive_Begin("
    "enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Step("
    "enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Hold("
    "uint32_t NativeArcadeRaceDrive_LingerTick(struct NativeArcadeRaceDrive *drive, int onResults)"
    "static int NativeArcadeRaceDrive_MaySend("
    "NativeLockstepSession_RecordLocalDigests(drive->session, state)")

# The canonical type names the core may use; stripped (as whole
# identifiers) before the ban so that every other NativeCanonical token
# still trips it.
set(drive_allowed_canonical_names NativeCanonicalInputPadV1 NativeCanonicalStateV4)

# 1. Banned tokens, by category, judged on the comment-free code.
set(sdl_network_tokens SDL winsock WinSock WSA sockaddr)
set(clock_tokens "time(" "clock(" QueryPerformance)
set(heap_tokens malloc calloc realloc "free(" alloca)
set(io_tokens stdio printf)
set(game_tokens "game/" gGT sdata Platform_)
set(lease_tokens TopologyLease Acquire Activate Capture Publish Retire LOAD_Hub_ReadFile)
set(state_tokens Checkpoint checkpoint NativeReplay Replay replay NativeCanonical)
# The core does no I/O of its own: the link, the adapter, the lobby, the
# outcome tracker, and the host are reached only through the callbacks.
set(link_tokens NativeLockstepPeerLink NativeArcadeNetplay NativeLobby NativeLockstepMatchOutcome NativeArcadeLinkHost NativeUdp)

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
    if(relative_path STREQUAL drive_header)
        foreach(macro IN LISTS drive_required_macros_header)
            string(REPLACE " " ";" macro_parts "${macro}")
            list(GET macro_parts 0 macro_name)
            list(GET macro_parts 1 macro_value)
            if(NOT code MATCHES "#define[ \t]+${macro_name}[ \t]+${macro_value}[ \t]*[\r\n]")
                message(FATAL_ERROR "${prefix}: ${relative_path} must define ${macro_name} as ${macro_value}")
            endif()
        endforeach()
    endif()

    ctr_strip_names("${code}" banned_code ${drive_allowed_canonical_names})
    foreach(term IN LISTS sdl_network_tokens clock_tokens heap_tokens io_tokens game_tokens lease_tokens state_tokens link_tokens)
        ctr_forbid("${relative_path}" "${banned_code}" "${term}" "the code")
    endforeach()

    # 1a. The state type only as a const pointer (read-only, LR-15).
    string(REGEX MATCHALL "NativeCanonicalStateV4[^A-Za-z0-9_]" state_names "${code}")
    string(REGEX MATCHALL "const struct NativeCanonicalStateV4 \\*" state_const_names "${code}")
    list(LENGTH state_names state_name_count)
    list(LENGTH state_const_names state_const_count)
    if(state_name_count EQUAL 0 OR NOT state_name_count EQUAL state_const_count)
        message(FATAL_ERROR "${prefix}: ${relative_path} names NativeCanonicalStateV4 other than as 'const struct NativeCanonicalStateV4 *' (${state_const_count} of ${state_name_count})")
    endif()

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

    if(relative_path STREQUAL drive_source)
        set(source_code "${code}")
    endif()
    math(EXPR scanned_files "${scanned_files} + 1")
endforeach()
if(NOT scanned_files EQUAL 2)
    message(FATAL_ERROR "${prefix}: scanned ${scanned_files} files, expected 2")
endif()

# 1b. The ban bites: a canonical token other than the allowed names (a
#     function of an allowed type included) is still caught after the
#     allowed names are stripped, and the allowed names alone are not.
foreach(probe IN ITEMS "struct NativeCanonicalInputPadV1 a, struct NativeCanonicalStateV1 b"
                       "struct NativeCanonicalInputPadV1 a, NativeCanonicalStateV1_Init(&c)"
                       "const struct NativeCanonicalStateV4 *s; NativeCanonicalStateV4_Validate(s)"
                       "struct NativeCanonicalInputPadV1x a"
                       "struct NativeCanonicalStateV4Copy b")
    ctr_strip_names("${probe}" probe_stripped ${drive_allowed_canonical_names})
    string(FIND "${probe_stripped}" "NativeCanonical" probe_at)
    if(probe_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: the canonical-name allowance hides other NativeCanonical tokens ('${probe}')")
    endif()
endforeach()
ctr_strip_names("struct NativeCanonicalInputPadV1 a,NativeCanonicalInputPadV1 b; const struct NativeCanonicalStateV4 *s" probe_stripped
                ${drive_allowed_canonical_names})
string(FIND "${probe_stripped}" "NativeCanonical" probe_at)
if(NOT probe_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: the canonical-name allowance does not strip the allowed names ('${probe_stripped}')")
endif()

# 1c. The send order (LR-29). Exactly one ComposeBundle call and one
#     sendBundle call, each in a top-level unit (the text since the last
#     closing brace at the start of a line) that passes the MaySend guard
#     first; the guard names the RUNNING mode, the first record, the
#     recorded frame, and D.
foreach(call IN ITEMS "NativeLockstepSession_ComposeBundle(" "callbacks.sendBundle(")
    string(REGEX REPLACE "([.()])" "\\\\\\1" call_pattern "${call}")
    string(REGEX MATCHALL "${call_pattern}" call_matches "${source_code}")
    list(LENGTH call_matches call_count)
    if(NOT call_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${drive_source} must make exactly one '${call}' call (found ${call_count})")
    endif()
    string(FIND "${source_code}" "${call}" call_at)
    string(SUBSTRING "${source_code}" 0 ${call_at} before_call)
    string(FIND "${before_call}" "\n}" unit_at REVERSE)
    if(unit_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: no unit found before '${call}'; the scan is broken")
    endif()
    string(SUBSTRING "${before_call}" ${unit_at} -1 unit_text)
    string(FIND "${unit_text}" "if (!NativeArcadeRaceDrive_MaySend(drive, frameIndex))" guard_at)
    if(guard_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: the '${call}' call is not behind the NativeArcadeRaceDrive_MaySend guard")
    endif()
endforeach()
string(FIND "${source_code}" "static int NativeArcadeRaceDrive_MaySend(" guard_def_at)
string(SUBSTRING "${source_code}" ${guard_def_at} -1 guard_tail)
string(FIND "${guard_tail}" "\n}" guard_end)
string(SUBSTRING "${guard_tail}" 0 ${guard_end} guard_body)
foreach(term IN ITEMS "NATIVE_LOCKSTEP_RUNNING" "recordedAny" "recordedFrame" "drive->inputDelay" "<=")
    ctr_require("${drive_source} (NativeArcadeRaceDrive_MaySend)" "${guard_body}" "${term}")
endforeach()

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

# 4. The core links only the allowed libraries: every
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
