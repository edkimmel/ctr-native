# Structural isolation for the match-select session
# (native_match_select_session, docs/MATCH_SELECT_MILESTONE.md section 2.3,
# MS-4). The module is a pure, caller-owned state machine: no frame-bundle or
# handshake stack, peer link, lobby, UDP, socket, SDL, virtual datagram,
# topology-lease, canonical-state, replay, or checkpoint dependency, no heap,
# clock, or stdio use, and no game dependency. Its includes are limited to
# stddef.h, stdint.h, string.h, its own header, the match-select rules and
# message headers, the match config header, and the SHA-256 header; the
# library links exactly ctr_native_match_select_rules and
# ctr_native_match_select_message and never ctr_native_virtual_datagram
# (only its fault test links the harness); the target stays portable C17
# with extensions off; ctr_native does not link it.
#
# The structural rule: the two frozen defaults (the 600-tick item countdown,
# OD-1, and the 90-tick peer silence, SEL-9) are literal in the header, and
# the fault enum is append-only.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "match select session isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

set(session_header "include/platform/native_match_select_session.h")
set(session_source "platform/native_match_select_session.c")
set(session_files "${session_header}" "${session_source}")

# 1. No frame-bundle/handshake stack, peer link, lobby, virtual datagram,
#    UDP, or netplay dependency.
set(link_tokens
    lockstep Lockstep LOCKSTEP PeerLink peer_link "peer link" NativeLobby native_lobby lobby Lobby LOBBY
    VirtualDatagram virtual_datagram "virtual datagram" udp_transport UdpTransport UDP Udp
    ArcadeNetplay arcade_netplay)

# 2. No socket / OS-networking header or symbol, and no SDL.
set(network_tokens
    socket Socket SOCKET winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo
    inet_ "poll(" SDL)

# 3. No clock, stdio, or game-code dependency.
set(dependency_tokens "time(" "clock(" clock_ QueryPerformance stdio printf fopen "FILE" Game_ "gGT" "sdata")

# 4. No heap use.
set(alloc_tokens heap Heap malloc calloc realloc "free(" alloca)

# 5. No topology-lease symbol.
set(lease_tokens lease Lease LEASE TopologyLease Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 6. No canonical-state, replay, or checkpoint state.
set(state_tokens canonical Canonical CANONICAL Replay replay REPLAY Checkpoint checkpoint CHECKPOINT)

foreach(relative_path IN LISTS session_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS link_tokens network_tokens dependency_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 7. #include lines may only name the allowlisted headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_match_select_session\\.h\"|\"platform/native_match_select_message\\.h\"|\"platform/native_match_select_rules\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_sha256\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 8. Frozen defaults, literally in the header, each defined exactly once
#    (clang-format may align the values, so any run of blanks separates the
#    name from its literal).
ctr_read_source("${session_header}" header)
foreach(definition IN ITEMS "DEFAULT_ITEM_TICKS=600u" "DEFAULT_PEER_SILENCE_TICKS=90u")
    string(REPLACE "=" ";" definition_parts "${definition}")
    list(GET definition_parts 0 name)
    list(GET definition_parts 1 literal)
    string(REGEX MATCHALL "#define NATIVE_MATCH_SELECT_SESSION_${name}[ \t]" definitions "${header}")
    list(LENGTH definitions definition_count)
    if(NOT definition_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: NATIVE_MATCH_SELECT_SESSION_${name} must be defined exactly once (found ${definition_count})")
    endif()
    string(REGEX MATCH "#define NATIVE_MATCH_SELECT_SESSION_${name}[ \t]+${literal}[ \t\r\n]" frozen "${header}")
    if(frozen STREQUAL "")
        message(FATAL_ERROR "${prefix}: NATIVE_MATCH_SELECT_SESSION_${name} must be defined literally as ${literal}")
    endif()
endforeach()

# 9. The session fault enum is append-only: every existing value keeps its
#    number.
set(fault_causes NONE PEER_SILENT HUMAN_COUNT_MISMATCH NONCE_CHANGED LOCK_CHANGED DIGEST_MISMATCH RESOLVE_FAILED)
set(value 0)
foreach(cause IN LISTS fault_causes)
    string(REGEX MATCHALL "NATIVE_MATCH_SELECT_SESSION_FAULT_${cause} = ${value}[,\r\n]" found "${header}")
    list(LENGTH found found_count)
    if(NOT found_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: NATIVE_MATCH_SELECT_SESSION_FAULT_${cause} must be declared exactly once as ${value} (append-only enum)")
    endif()
    math(EXPR value "${value} + 1")
endforeach()

ctr_read_source("CMakeLists.txt" cmake)

# 10. ctr_native_match_select_session links exactly
#     ctr_native_match_select_rules and ctr_native_match_select_message, in
#     exactly one target_link_libraries call.
set(target ctr_native_match_select_session)
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
if(NOT "${link_items}" STREQUAL "ctr_native_match_select_message;ctr_native_match_select_rules")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_match_select_rules and ctr_native_match_select_message (found '${link_items}')")
endif()
string(FIND "${link_call}" "ctr_native_virtual_datagram" harness_at)
if(NOT harness_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${target} must never link ctr_native_virtual_datagram")
endif()

# 11. C17, no extensions, on the session target, in order.
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

# 12. The game executable does not link the session (not yet integrated).
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
