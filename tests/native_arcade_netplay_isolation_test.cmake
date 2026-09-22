# Structural isolation for the arcade-link host adapter (native_arcade_netplay):
# the only production module that composes the lobby layer with the
# failure-handling layer and drives the pure arcade screen flow. It names no
# topology-lease symbol, uses no heap, clock, or engine source, reaches no OS
# networking API directly, writes no replay, checkpoint, or canonical state,
# never touches the virtual-datagram test harness, includes only its allowed
# headers, exposes a public API whose own names stay free of every token the
# lockstep and failure-handling isolation rules forbid under the engine
# sources (so engine code can call it), links exactly its six composed
# libraries and never the transport directly, stays portable C17 with
# extensions off, and keeps its four defaults frozen.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade netplay isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "arcade netplay isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# The adapter files.
set(netplay_header "include/platform/native_arcade_netplay.h")
set(netplay_files
    "${netplay_header}"
    "platform/native_arcade_netplay.c")

# 1. Forbidden tokens, by category.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile)
set(alloc_tokens malloc calloc realloc "free(" alloca)
set(clock_game_tokens "time(" "clock(" QueryPerformance "game/" Game_ "main.c")
set(network_tokens
    SDL winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl
    "<sys/" netinet getaddrinfo "select(" "poll(")
set(state_tokens NativeReplay Checkpoint checkpoint NativeCanonical)
set(virtual_datagram_tokens native_virtual_datagram NativeVirtualDatagram)

foreach(relative_path IN LISTS netplay_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS lease_tokens alloc_tokens clock_game_tokens network_tokens state_tokens
            virtual_datagram_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 2. #include lines may only name the C headers string.h, stdint.h, and
    #    stddef.h, the module's own header, and the seven composed platform
    #    headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<string\\.h>|<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_netplay\\.h\"|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_lobby_state\\.h\"|\"platform/native_lockstep_match_outcome\\.h\"|\"platform/native_lockstep_match_roster\\.h\"|\"platform/native_lockstep_rematch\\.h\"|\"platform/native_match_config\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade netplay isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 3. The adapter's own public names carry none of the tokens the lockstep and
#    failure-handling isolation rules forbid under the engine sources, so
#    engine code can call every NativeArcadeNetplay_* name without tripping
#    either rule.
ctr_read_source("${netplay_header}" header)
string(REGEX MATCHALL "(NativeArcadeNetplay|NATIVE_ARCADE_NETPLAY)[A-Za-z0-9_]*" public_names "${header}")
list(LENGTH public_names public_name_count)
if(public_name_count LESS 10)
    message(FATAL_ERROR "arcade netplay isolation: found only ${public_name_count} NativeArcadeNetplay/NATIVE_ARCADE_NETPLAY identifiers in ${netplay_header}; the scan is broken")
endif()
foreach(public_name IN LISTS public_names)
    foreach(term IN ITEMS lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch)
        string(FIND "${public_name}" "${term}" offset)
        if(NOT offset EQUAL -1)
            message(FATAL_ERROR "arcade netplay isolation: public name '${public_name}' in ${netplay_header} contains '${term}', which engine code may not name")
        endif()
    endforeach()
endforeach()

# 4. ctr_native_arcade_netplay links its six composed libraries, in exactly
#    one target_link_libraries call, and reaches the transport only through
#    the lobby layer: never ctr_native_udp_transport or the virtual-datagram
#    harness directly.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_netplay)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "arcade netplay isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
foreach(expected IN ITEMS
        ctr_native_arcade_flow ctr_native_lobby_state ctr_native_lockstep_match_outcome
        ctr_native_lockstep_match_roster ctr_native_lockstep_rematch ctr_native_match_config)
    list(FIND link_items "${expected}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: ${target} must link ${expected} (found '${link_items}')")
    endif()
endforeach()
foreach(forbidden IN ITEMS ctr_native_virtual_datagram ctr_native_udp_transport)
    string(FIND "${link_call}" "${forbidden}" leak)
    if(NOT leak EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: ${target} must not link ${forbidden}")
    endif()
endforeach()

# 5. C17, no extensions, on the adapter target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade netplay isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade netplay isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade netplay isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade netplay isolation: ${target} C17/no-extensions properties are out of order")
endif()

#    The four defaults are frozen, whole line for whole line (UX-5, UX-9);
#    the line end may be LF or CRLF.
ctr_require_regex("${netplay_header} (INPUT_DELAY must stay 2u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY 2u\r?\n")
ctr_require_regex("${netplay_header} (ATTEMPT_TICKS_PER_CANDIDATE must stay 150u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_ATTEMPT_TICKS_PER_CANDIDATE 150u\r?\n")
ctr_require_regex("${netplay_header} (RETRANSMIT_INTERVAL_TICKS must stay 15u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS 15u\r?\n")
ctr_require_regex("${netplay_header} (STALL_TIMEOUT_TICKS must stay 90u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS 90u   /\\* 3 s at the 30 Hz loop, UX-9 \\*/\r?\n")

