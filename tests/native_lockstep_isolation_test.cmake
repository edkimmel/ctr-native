# Structural isolation for the lockstep seam (native_lockstep_protocol,
# native_lockstep_input_window, native_lockstep_session): no OS-networking
# dependency, no topology-lease symbol, no dynamic allocation, no clock, game,
# or presentation dependency, the two leaf libraries never link the virtual
# datagram test harness, the frozen wire/window constants cannot silently
# change, no other seam picks up a lockstep identifier, and the three targets
# stay portable C17 with extensions off. It also freezes the peer link's
# generic aux-route widths and keeps any select-layer token out of the peer
# link and holds it to the lease and allocation scans (section 9). The fault
# cause enum is append-only and its session-local VERIFY_AHEAD cause stays out
# of the codec and the window (section 10). The listen-only link and the
# lobby's LISTENING poll never send (section 11).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "lockstep isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: required token '${term}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "lockstep isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# The six lockstep seam files.
set(lockstep_files
    "include/platform/native_lockstep_protocol.h"
    "platform/native_lockstep_protocol.c"
    "include/platform/native_lockstep_input_window.h"
    "platform/native_lockstep_input_window.c"
    "include/platform/native_lockstep_session.h"
    "platform/native_lockstep_session.c")

# 1. No socket / OS-networking header or symbol.
set(network_tokens
    winsock WinSock WSA socket Socket AF_INET sockaddr htons htonl ntohs ntohl
    SDL_net SDL "<sys/" netinet getaddrinfo "select(" "poll(")

# 2. No topology-lease symbol.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 3. No dynamic allocation.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 4. No clock / game / presentation dependency.
set(dependency_tokens
    "time(" "clock(" QueryPerformance "game/" Game_ "main.c" native_renderer
    native_display_config native_frame_capture texture_filter)

foreach(relative_path IN LISTS lockstep_files)
    ctr_read_source("${relative_path}" source)

    # native_lockstep_session.h documents, in prose, that the session has "no
    # socket, OS networking, SDL, clock, or game dependency" -- that sentence
    # is the seam's own no-dependency guarantee, not a use of any of those
    # things, so it is neutralized before the network-token scan below. The
    # substitution is a no-op on every other file and does not touch the
    # copy used by the other three categories, so a real socket/SDL usage
    # anywhere, including elsewhere in this same file, still fails the scan.
    string(REPLACE "no socket, OS networking, SDL, clock, or game" "no OS-networking, clock, or game" network_scan "${source}")

    foreach(term IN LISTS network_tokens)
        ctr_forbid("${relative_path}" "${network_scan}" "${term}")
    endforeach()
    foreach(term IN LISTS lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
    foreach(term IN LISTS alloc_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
    foreach(term IN LISTS dependency_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 5. ctr_native_lockstep_protocol and ctr_native_lockstep_input_window must not
#    link the virtual-datagram test harness; only the fault-injection test
#    executable (native_lockstep_transport_fault_test) is allowed to, and it is
#    not one of the two library targets checked here.
ctr_read_source("CMakeLists.txt" cmake)
foreach(target IN ITEMS ctr_native_lockstep_protocol ctr_native_lockstep_input_window)
    string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*${target}[^)]*\\)" link_call "${cmake}")
    if("${link_call}" STREQUAL "")
        message(FATAL_ERROR "lockstep isolation: missing target_link_libraries(${target} ...) in CMakeLists.txt")
    endif()
    string(FIND "${link_call}" "ctr_native_virtual_datagram" leak)
    if(NOT leak EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: ${target} must not link ctr_native_virtual_datagram")
    endif()
endforeach()

# 6. The wire/window constants are frozen. A changed literal must fail this
#    test, not silently change the protocol.
ctr_read_source("include/platform/native_lockstep_protocol.h" protocol_header)
ctr_require("include/platform/native_lockstep_protocol.h" "${protocol_header}"
    "#define NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC UINT32_C(0x31424c4e)")
ctr_require("include/platform/native_lockstep_protocol.h" "${protocol_header}"
    "#define NATIVE_LOCKSTEP_BUNDLE_V1_VERSION UINT32_C(1)")
ctr_require_regex("include/platform/native_lockstep_protocol.h (ENCODED_BYTES must stay 128u)" "${protocol_header}"
    "#define NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES 128u[^0-9a-zA-Z_]")

ctr_read_source("include/platform/native_lockstep_input_window.h" window_header)
ctr_require_regex("include/platform/native_lockstep_input_window.h (RING_CAPACITY must stay 8)" "${window_header}"
    "#define NATIVE_LOCKSTEP_RING_CAPACITY 8[^0-9]")
ctr_require_regex("include/platform/native_lockstep_input_window.h (MAX_INPUT_DELAY must stay 6)" "${window_header}"
    "#define NATIVE_LOCKSTEP_MAX_INPUT_DELAY 6[^0-9]")

# 7. No lockstep identifier leaks into unrelated seams: canonical state,
#    replay, match config, or game code.
set(lockstep_free_sources
    "platform/native_canonical_state.c"
    "platform/native_canonical_state_v3.c"
    "platform/native_canonical_state_v4.c"
    "platform/native_replay_scheduler.c"
    "platform/native_replay_scheduler_seam.c"
    "platform/native_replay_scheduler_v4.c"
    "platform/native_replay_v2.c"
    "platform/native_replay_v2_file.c"
    "platform/native_replay_v3.c"
    "platform/native_replay_v3_file.c"
    "platform/native_replay_v4.c"
    "platform/native_replay_v4_file.c"
    "platform/native_match_config.c")

file(GLOB_RECURSE game_sources RELATIVE "${repo}" "${repo}/game/*.c" "${repo}/game/*.h")
list(LENGTH game_sources game_source_count)
if(game_source_count EQUAL 0)
    message(FATAL_ERROR "lockstep isolation: game/ source glob is empty; the leak scan cannot run")
endif()
list(APPEND lockstep_free_sources ${game_sources})

foreach(relative_path IN LISTS lockstep_free_sources)
    ctr_read_source("${relative_path}" source)
    foreach(term IN ITEMS lockstep Lockstep LOCKSTEP)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 8. C17, no extensions, on all three lockstep targets.
foreach(target IN ITEMS ctr_native_lockstep_protocol ctr_native_lockstep_input_window ctr_native_lockstep_session)
    string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
    if(declare_at EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
    endif()
    string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
    string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
    if(properties_at EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
    endif()
    string(FIND "${target_block}" "C_STANDARD 17" standard_at)
    string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
    string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
    if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
    endif()
    if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
        message(FATAL_ERROR "lockstep isolation: ${target} C17/no-extensions properties are out of order")
    endif()
endforeach()

# 9. Peer-link aux route (docs/MATCH_SELECT_MILESTONE.md section 2.4). The
#    aux datagram width and inbox capacity are frozen: a changed literal must
#    fail this test, not silently change what the peer link routes. The peer
#    link carries aux datagrams generically and must not know the layer that
#    uses them, so no match-select or arcade-flow token (including the select
#    message magic "NMS1", 0x31534d4e) may appear in its sources. The same
#    topology-lease and dynamic-allocation scans as sections 2 and 3 apply to
#    it too.
ctr_read_source("include/platform/native_lockstep_peer_link.h" peer_link_header)
ctr_require_regex("include/platform/native_lockstep_peer_link.h (AUX_BYTES must stay 64u)" "${peer_link_header}"
    "#define NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES 64u[^0-9a-zA-Z_]")
ctr_require_regex("include/platform/native_lockstep_peer_link.h (AUX_CAPACITY must stay 16u)" "${peer_link_header}"
    "#define NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY 16u[^0-9a-zA-Z_]")

foreach(relative_path IN ITEMS "include/platform/native_lockstep_peer_link.h" "platform/native_lockstep_peer_link.c")
    ctr_read_source("${relative_path}" source)
    foreach(term IN ITEMS MatchSelect match_select MATCH_SELECT NativeArcade native_arcade NMS1 0x31534d4e 0x31534D4E)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
    foreach(term IN LISTS lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
    foreach(term IN LISTS alloc_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 10. The fault-cause enum is append-only: every existing value keeps its
#     number, and VERIFY_AHEAD (docs/LOCKSTEP_RACE_MILESTONE.md LR-S5) is the
#     15th, appended. VERIFY_AHEAD is a lockstep-session cause only: the codec
#     and the input window never produce it, so the wire never carries it.
set(fault_causes
    NONE BAD_MAGIC BAD_VERSION BAD_SIZE BAD_DIGEST BAD_RESERVED MATCH_IDENTITY
    PROTOCOL_VERSION INPUT_DELAY BAD_SLOT BAD_PAD_COUNT CONFLICTING_INPUT
    WINDOW_OVERRUN VERIFY_LAG VERIFY_SHAPE VERIFY_AHEAD)
set(value 0)
foreach(cause IN LISTS fault_causes)
    string(REGEX MATCHALL "NATIVE_LOCKSTEP_FAULT_${cause} = ${value}[, \r\n]" found "${protocol_header}")
    list(LENGTH found found_count)
    if(NOT found_count EQUAL 1)
        message(FATAL_ERROR "lockstep isolation: NATIVE_LOCKSTEP_FAULT_${cause} must be declared exactly once as ${value} (append-only enum)")
    endif()
    math(EXPR value "${value} + 1")
endforeach()
# Every enumerator in the enum body is counted, with or without a value, and
# every one must carry an explicit value, so a cause appended without "= N"
# cannot slip past the count. Comments are stripped first so a cause named in
# one is not counted.
string(REGEX MATCH "enum NativeLockstepFaultCause[ \t\r\n]*{[^}]*}" fault_enum "${protocol_header}")
if(fault_enum STREQUAL "")
    message(FATAL_ERROR "lockstep isolation: native_lockstep_protocol.h must declare enum NativeLockstepFaultCause")
endif()
string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" fault_enum "${fault_enum}")
string(REGEX REPLACE "//[^\n]*" "" fault_enum "${fault_enum}")
string(REGEX MATCHALL "NATIVE_LOCKSTEP_FAULT_[A-Z0-9_]+" declared_causes "${fault_enum}")
string(REGEX MATCHALL "NATIVE_LOCKSTEP_FAULT_[A-Z0-9_]+[ \t\r\n]*=[ \t\r\n]*[0-9]+[ \t\r\n]*[,}]" valued_causes "${fault_enum}")
list(LENGTH declared_causes declared_count)
list(LENGTH valued_causes valued_count)
list(LENGTH fault_causes expected_count)
if(NOT declared_count EQUAL expected_count)
    message(FATAL_ERROR "lockstep isolation: enum NativeLockstepFaultCause declares ${declared_count} enumerators, expected ${expected_count}; append new causes to this list")
endif()
if(NOT valued_count EQUAL declared_count)
    message(FATAL_ERROR "lockstep isolation: ${valued_count} of ${declared_count} NativeLockstepFaultCause enumerators carry an explicit value; every cause must be written as NAME = N")
endif()
foreach(relative_path IN ITEMS "platform/native_lockstep_protocol.c" "platform/native_lockstep_input_window.c")
    ctr_read_source("${relative_path}" source)
    ctr_forbid("${relative_path}" "${source}" "NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD")
endforeach()
ctr_read_source("platform/native_lockstep_session.c" session_source)
ctr_require("platform/native_lockstep_session.c" "${session_source}" "NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD")

# 11. The listen-only link never sends (docs/SOLO_CAB_MILESTONE.md SOLO-4).
#     The bodies of NativeLockstepPeerLink_OpenListen and
#     NativeLockstepPeerLink_PollListen name no send path (no transport send,
#     no link send helper, no retransmit, no composed handshake), and each
#     calls only the functions listed here. The LISTENING branch of
#     NativeLobbyState_Poll calls only NativeLockstepPeerLink_PollListen.
#     Comments are stripped before the scans, and a body ends at the first
#     closing brace in column 0 (a branch at the first "\n\t}").
function(ctr_code_after relative_path source opener closer out_var)
    string(REPLACE "\r\n" "\n" text "${source}")
    string(FIND "${text}" "${opener}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: ${relative_path} must contain '${opener}'")
    endif()
    string(SUBSTRING "${text}" ${at} -1 tail)
    string(FIND "${tail}" "${closer}" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "lockstep isolation: cannot find the end of '${opener}' in ${relative_path}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" body "${body}")
    string(REGEX REPLACE "//[^\n]*" "" body "${body}")
    set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

# Every identifier called in body (skipping the opener's own name and the C
# keywords that take parentheses) must be one of the allowed names.
function(ctr_calls_only label body opener_name)
    set(allowed ${ARGN})
    string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*[ \t\n]*\\(" calls "${body}")
    foreach(call IN LISTS calls)
        string(REGEX REPLACE "[ \t\n]*\\($" "" name "${call}")
        if(name STREQUAL opener_name OR name MATCHES "^(if|for|while|switch|return|sizeof)$")
            continue()
        endif()
        list(FIND allowed "${name}" allowed_at)
        if(allowed_at EQUAL -1)
            message(FATAL_ERROR "lockstep isolation: ${label} calls '${name}', outside its listen-only allow-list")
        endif()
    endforeach()
endfunction()

set(listen_send_tokens Send sendto Retransmit ComposeMessage HandshakeDatagram "NativeLockstepPeerLink_Poll(")
ctr_read_source("platform/native_lockstep_peer_link.c" peer_link_source)
ctr_code_after("platform/native_lockstep_peer_link.c" "${peer_link_source}"
    "int NativeLockstepPeerLink_OpenListen(" "\n}" open_listen_body)
ctr_code_after("platform/native_lockstep_peer_link.c" "${peer_link_source}"
    "uint32_t NativeLockstepPeerLink_PollListen(" "\n}" poll_listen_body)
foreach(term IN LISTS listen_send_tokens)
    ctr_forbid("platform/native_lockstep_peer_link.c (OpenListen)" "${open_listen_body}" "${term}")
    ctr_forbid("platform/native_lockstep_peer_link.c (PollListen)" "${poll_listen_body}" "${term}")
endforeach()
ctr_require("platform/native_lockstep_peer_link.c (OpenListen)" "${open_listen_body}" "NativeUdpTransport_Open(")
ctr_require("platform/native_lockstep_peer_link.c (OpenListen)" "${open_listen_body}"
    "link->mode = NATIVE_LOCKSTEP_PEER_LINK_LISTENING;")
ctr_require("platform/native_lockstep_peer_link.c (PollListen)" "${poll_listen_body}" "NativeUdpTransport_Receive(")
ctr_calls_only("NativeLockstepPeerLink_OpenListen" "${open_listen_body}" NativeLockstepPeerLink_OpenListen
    NativeUdpTransport_GlobalInit NativeUdpTransport_Open NativeUdpTransport_GlobalShutdown
    NativeLockstepHandshake_Init memset NativeLockstepPeerLink_ResetAux)
ctr_calls_only("NativeLockstepPeerLink_PollListen" "${poll_listen_body}" NativeLockstepPeerLink_PollListen
    NativeUdpTransport_Receive NativeLockstepPeerLink_IsListedPeer NativeCodecReader_Init
    NativeLockstepHandshakeMessageV1_Decode)
# The two helpers the listen bodies call send nothing either.
ctr_code_after("platform/native_lockstep_peer_link.c" "${peer_link_source}"
    "static void NativeLockstepPeerLink_ResetAux(" "\n}" reset_aux_body)
ctr_calls_only("NativeLockstepPeerLink_ResetAux" "${reset_aux_body}" NativeLockstepPeerLink_ResetAux memset)
ctr_code_after("platform/native_lockstep_peer_link.c" "${peer_link_source}"
    "static int NativeLockstepPeerLink_IsListedPeer(" "\n}" listed_peer_body)
ctr_calls_only("NativeLockstepPeerLink_IsListedPeer" "${listed_peer_body}" NativeLockstepPeerLink_IsListedPeer)

ctr_read_source("platform/native_lobby_state.c" lobby_source)
ctr_code_after("platform/native_lobby_state.c" "${lobby_source}"
    "void NativeLobbyState_Poll(struct NativeLobbyState *state)" "\n}" lobby_poll_body)
ctr_code_after("platform/native_lobby_state.c (NativeLobbyState_Poll)" "${lobby_poll_body}"
    "else if (state->mode == NATIVE_LOBBY_STATE_LISTENING)" "\n\t}" lobby_listening_branch)
ctr_require("platform/native_lobby_state.c (LISTENING branch)" "${lobby_listening_branch}"
    "NativeLockstepPeerLink_PollListen(&state->link, state->candidates, state->candidateCount)")
ctr_calls_only("the LISTENING branch of NativeLobbyState_Poll" "${lobby_listening_branch}" ""
    NativeLockstepPeerLink_PollListen)
