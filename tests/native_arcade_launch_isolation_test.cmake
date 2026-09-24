# Structural isolation for the race launch agreement module
# (native_arcade_launch, docs/RACE_LAUNCH_MILESTONE.md section 4 RL-2, RL-3,
# RL-4, section 5, slice RL-S2). The module is a pure codec plus caller-owned
# state: no OS-networking, SDL, game, lockstep, lobby, match-config, match
# select, roster, or netplay dependency, no heap or clock, no topology-lease
# token, and no checkpoint, replay, or canonical-state token. Its includes are
# limited to stddef.h, stdint.h, string.h, its own header, and the binary
# codec header (platform/native_canonical_codec.h, which provides
# NativeCodecWriter, NativeCodecReader, and NativeCodecDigest64); the library
# links exactly ctr_native_canonical_codec; the target stays portable C17
# with extensions off; ctr_native does not link it in this slice (RL-S5
# wires it through the netplay adapter).
#
# The token bans run twice: on the raw text (so comment prose stays clean
# too), and, for the section 5 ban, on the code with every // and /* */
# comment removed (each comment replaced by a space, as the C preprocessor
# does), so the ban judges the code itself.
#
# The structural rule: the wire format is frozen. The header must define the
# magic 0x314C414E ("NAL1"), version 1, 64 encoded bytes, digest offset 56,
# 32 config digest bytes, the roles, the HEARD flag, and the 300-tick default
# linger literally, each exactly once, and the fault-cause enum values must
# keep their numbers (append-only), so a change to any of them fails this
# test.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade launch isolation")

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

# Removes every /* */ comment, then every // comment, replacing each with a
# space. An unterminated /* is an error rather than a silent truncation.
function(ctr_strip_comments label source out_var)
    set(remaining "${source}")
    set(stripped "")
    while(TRUE)
        string(FIND "${remaining}" "/*" open_at)
        if(open_at EQUAL -1)
            string(APPEND stripped "${remaining}")
            break()
        endif()
        string(SUBSTRING "${remaining}" 0 ${open_at} before)
        string(APPEND stripped "${before} ")
        math(EXPR body_at "${open_at} + 2")
        string(SUBSTRING "${remaining}" ${body_at} -1 remaining)
        string(FIND "${remaining}" "*/" close_at)
        if(close_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: unterminated /* comment in ${label}")
        endif()
        math(EXPR after_at "${close_at} + 2")
        string(SUBSTRING "${remaining}" ${after_at} -1 remaining)
    endwhile()
    string(REGEX REPLACE "//[^\r\n]*" " " stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# 0. The comment stripper itself: comments go, code stays.
ctr_strip_comments("self-check" "code1 /* Lease\n replay */ code2 // lease\ncode3 /**/code4" self_check)
foreach(term IN ITEMS Lease lease replay "/*" "*/" "//")
    ctr_forbid("self-check" "${self_check}" "${term}" "stripped code")
endforeach()
foreach(term IN ITEMS code1 code2 code3 code4)
    ctr_require("self-check" "${self_check}" "${term}")
endforeach()

set(launch_header "include/platform/native_arcade_launch.h")
set(launch_source "platform/native_arcade_launch.c")
set(launch_files "${launch_header}" "${launch_source}")

# 1. No socket / OS-networking header or symbol, and no SDL.
set(network_tokens
    winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo
    "select(" "poll(" SDL)

# 2. No clock or heap use (so no comment names those either).
set(clock_tokens "time(" "clock(" QueryPerformance)
set(heap_tokens malloc calloc realloc "free(" alloca)

# 3. No game code, lockstep, lobby, outcome, roster, netplay, or select
#    dependency, and no sound-table names.
set(dependency_tokens
    game/ Game_ lockstep Lockstep LOCKSTEP NativeLobby MatchOutcome MatchRoster
    NativeArcadeNetplay NativeMatchSelect countSounds CountSounds OtherFX)

# 4. No topology lease, checkpoint, replay, or canonical state.
set(state_tokens TopologyLease LOAD_Hub_ReadFile NativeReplay Checkpoint checkpoint NativeCanonicalState)

# 5. Section 5 ban, judged on the comment-free code: no topology-lease
#    acquire, activate, capture, or publish token, and no checkpoint,
#    replay, or NativeCanonical token.
set(code_tokens
    TopologyLease topology_lease Lease lease Acquire Activate Capture Publish
    Checkpoint checkpoint Replay replay NativeCanonical)

foreach(relative_path IN LISTS launch_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS network_tokens clock_tokens heap_tokens dependency_tokens state_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}" "the text")
    endforeach()

    ctr_strip_comments("${relative_path}" "${source}" code)
    string(FIND "${code}" "NativeArcadeLaunch_Accept(" accept_at)
    if(accept_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: comment stripping lost the code of ${relative_path}")
    endif()
    foreach(term IN LISTS code_tokens network_tokens clock_tokens heap_tokens dependency_tokens state_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}" "the code")
    endforeach()

    # 6. #include lines may only name the allowlisted headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_launch\\.h\"|\"platform/native_canonical_codec\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 7. Frozen wire constants, literally in the header, each defined once.
ctr_read_source("${launch_header}" header)
foreach(definition IN ITEMS
    "#define NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC UINT32_C(0x314C414E)"
    "#define NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION 1u"
    "#define NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES 64u"
    "#define NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET 56u"
    "#define NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES 32u"
    "#define NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED0_BYTES 2u"
    "#define NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED1_BYTES 8u"
    "#define NATIVE_ARCADE_LAUNCH_ROLE_CAB1 1u"
    "#define NATIVE_ARCADE_LAUNCH_ROLE_CAB2 2u"
    "#define NATIVE_ARCADE_LAUNCH_FLAG_HEARD 0x01u"
    "#define NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS 300u")
    ctr_require("${launch_header}" "${header}" "${definition}")
endforeach()
foreach(name IN ITEMS
    RECORD_V1_MAGIC RECORD_V1_VERSION RECORD_V1_ENCODED_BYTES RECORD_V1_DIGEST_OFFSET
    CONFIG_DIGEST_BYTES RECORD_V1_RESERVED0_BYTES RECORD_V1_RESERVED1_BYTES
    ROLE_CAB1 ROLE_CAB2 FLAG_HEARD DEFAULT_LINGER_TICKS)
    string(REGEX MATCHALL "#[ \t]*define[ \t]+NATIVE_ARCADE_LAUNCH_${name}[ \t\r\n(]" definitions "${header}")
    list(LENGTH definitions definition_count)
    if(NOT definition_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: NATIVE_ARCADE_LAUNCH_${name} must be defined exactly once (found ${definition_count})")
    endif()
endforeach()
ctr_read_source("${launch_source}" source)
string(FIND "${source}" "#define NATIVE_ARCADE_LAUNCH_" source_define_at)
if(NOT source_define_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${launch_source} must not redefine a NATIVE_ARCADE_LAUNCH_ constant")
endif()

# 8. The fault-cause enum is append-only: every existing value keeps its
#    number.
set(fault_causes
    NONE BAD_SIZE BAD_MAGIC BAD_VERSION BAD_ENCODED_SIZE BAD_DIGEST BAD_RESERVED
    BAD_ROLE BAD_FLAGS BAD_SEQUENCE)
set(value 0)
foreach(cause IN LISTS fault_causes)
    string(REGEX MATCHALL "NATIVE_ARCADE_LAUNCH_RECORD_FAULT_${cause} = ${value}[,\r\n]" found "${header}")
    list(LENGTH found found_count)
    if(NOT found_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: NATIVE_ARCADE_LAUNCH_RECORD_FAULT_${cause} must be declared exactly once as ${value} (append-only enum)")
    endif()
    math(EXPR value "${value} + 1")
endforeach()

ctr_read_source("CMakeLists.txt" cmake)

# 9. ctr_native_arcade_launch links exactly ctr_native_canonical_codec, in
#    exactly one target_link_libraries call.
set(target ctr_native_arcade_launch)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
list(SORT link_items)
if(NOT "${link_items}" STREQUAL "ctr_native_canonical_codec")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_canonical_codec (found '${link_items}')")
endif()

# 10. C17, no extensions, on the launch target, in order.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "${prefix}: ${target} C17/no-extensions properties are out of order")
endif()

# 11. The game executable does not link the module in this slice.
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*ctr_native[ \t\r\n][^)]*\\)" game_link_calls "${cmake}")
list(LENGTH game_link_calls game_link_call_count)
if(game_link_call_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: found no target_link_libraries(ctr_native ...) call; the link scan cannot run")
endif()
foreach(game_link_call IN LISTS game_link_calls)
    string(FIND "${game_link_call}" "${target}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: ctr_native must not link ${target}")
    endif()
endforeach()
