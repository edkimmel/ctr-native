# Structural isolation for the arcade-link host adapter (native_arcade_netplay):
# the only production module that composes the lobby layer with the
# failure-handling layer and the match-select session and drives the pure
# arcade screen flow. It names no topology-lease symbol, uses no heap,
# clock, or engine source, reaches no OS networking API directly, writes no
# replay, checkpoint, or canonical state,
# never touches the virtual-datagram test harness, includes only its allowed
# headers, exposes a public API whose own names stay free of every token the
# lockstep and failure-handling isolation rules forbid under the engine
# sources (so engine code can call it), links exactly its nine composed
# libraries and never the transport directly, static-asserts that a select
# record and a launch record each fill exactly one peer-link aux datagram,
# stays portable C17 with extensions off, keeps its four defaults and its
# launch linger cap frozen, and keeps the race hold service to its slice of
# Tick (docs/LOCKSTEP_RACE_MILESTONE.md LR-50).

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
    #    stddef.h, the module's own header, the seven original composed
    #    platform headers, the three match-select headers, native_sha256.h
    #    (the select nonce hash), and native_arcade_launch.h (the race launch
    #    agreement, docs/RACE_LAUNCH_MILESTONE.md RL-S5).
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<string\\.h>|<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_netplay\\.h\"|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_launch\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_lobby_state\\.h\"|\"platform/native_lockstep_match_outcome\\.h\"|\"platform/native_lockstep_match_roster\\.h\"|\"platform/native_lockstep_rematch\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_match_select_message\\.h\"|\"platform/native_match_select_rules\\.h\"|\"platform/native_match_select_session\\.h\"|\"platform/native_sha256\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade netplay isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 3. The adapter's own public names carry none of the tokens the lockstep and
#    failure-handling isolation rules forbid under the engine sources. This
#    checks names only: NativeArcadeNetplay_OnTakeResult and
#    NativeArcadeNetplay_Link carry lockstep types in their signatures and
#    are platform-side hooks for the Task 8 race driver only (which lives
#    under platform/). Engine code must not call those two; naming their
#    types there would fail tests/native_lockstep_isolation_test.cmake.
#    NativeArcadeNetplay_RaceService (docs/LOCKSTEP_RACE_MILESTONE.md LR-9,
#    LR-50) is the third platform-side hook: its signature carries no
#    lockstep type, but it runs a slice of Tick behind the flow's back, so
#    only the race driver's host glue calls it (section 8 pins its body).
#    Every other NativeArcadeNetplay_* name is callable from engine code
#    without tripping either rule.
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

# 4. ctr_native_arcade_netplay links exactly its nine composed libraries
#    (the seven of Task 4, ctr_native_match_select_session, and
#    ctr_native_arcade_launch from RL-S5) and nothing
#    else, in exactly one target_link_libraries call, and reaches the
#    transport only through the lobby layer: never ctr_native_udp_transport
#    or the virtual-datagram harness directly.
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
set(expected_link_items
    ctr_native_arcade_flow ctr_native_arcade_menu_input ctr_native_lobby_state
    ctr_native_lockstep_match_outcome ctr_native_lockstep_match_roster ctr_native_lockstep_rematch
    ctr_native_match_config ctr_native_match_select_session ctr_native_arcade_launch)
foreach(expected IN LISTS expected_link_items)
    list(FIND link_items "${expected}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: ${target} must link ${expected} (found '${link_items}')")
    endif()
endforeach()
foreach(item IN LISTS link_items)
    list(FIND expected_link_items "${item}" item_index)
    if(item_index EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: ${target} links unexpected item '${item}'; only the nine composed libraries are allowed")
    endif()
endforeach()
list(LENGTH link_items link_item_count)
if(NOT link_item_count EQUAL 9)
    message(FATAL_ERROR "arcade netplay isolation: ${target} must link exactly nine libraries, found ${link_item_count} ('${link_items}')")
endif()
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
#    the line end may be LF or CRLF. The HELLO retransmit interval is 1u
#    (every tick) because the peer link requires Retransmit before Poll on
#    every tick while HANDSHAKING.
ctr_require_regex("${netplay_header} (INPUT_DELAY must stay 2u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY 2u\r?\n")
ctr_require_regex("${netplay_header} (ATTEMPT_TICKS_PER_CANDIDATE must stay 150u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_ATTEMPT_TICKS_PER_CANDIDATE 150u\r?\n")
ctr_require_regex("${netplay_header} (RETRANSMIT_INTERVAL_TICKS must stay 1u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS 1u\r?\n")
ctr_require_regex("${netplay_header} (STALL_TIMEOUT_TICKS must stay 90u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS 90u   /\\* 3 s at the 30 Hz loop, UX-9 \\*/\r?\n")


# 6. A composed select record fills exactly one peer-link aux datagram: the
#    adapter carries the static assert that ties the two widths together
#    (docs/MATCH_SELECT_MILESTONE.md section 2.4), so neither can drift alone.
ctr_read_source("platform/native_arcade_netplay.c" netplay_source)
ctr_require_regex("platform/native_arcade_netplay.c (select record width == aux width)" "${netplay_source}"
    "_Static_assert\\(NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES,")

# 7. A composed launch record fills exactly one peer-link aux datagram
#    (docs/RACE_LAUNCH_MILESTONE.md RL-2): the launch module includes no
#    transport header, so the adapter carries that static assert, and the
#    ones tying the launch roles to the cabinet roles, the launch configDigest
#    width to the SHA-256 digest width, and the adapter's linger to the RL-4
#    default. The launch linger cap is frozen at 300 ticks (RL-4
#    launchLingerTicks).
ctr_require_regex("platform/native_arcade_netplay.c (launch record width == aux width)" "${netplay_source}"
    "_Static_assert\\(NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES,")
ctr_require_regex("platform/native_arcade_netplay.c (launch CAB1 role)" "${netplay_source}"
    "_Static_assert\\(NATIVE_ARCADE_LAUNCH_ROLE_CAB1 == \\(unsigned\\)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,")
ctr_require_regex("platform/native_arcade_netplay.c (launch CAB2 role)" "${netplay_source}"
    "_Static_assert\\(NATIVE_ARCADE_LAUNCH_ROLE_CAB2 == \\(unsigned\\)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,")
ctr_require_regex("platform/native_arcade_netplay.c (launch configDigest width == SHA-256 width)" "${netplay_source}"
    "_Static_assert\\(NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES == NATIVE_SHA256_DIGEST_BYTES,")
ctr_require_regex("platform/native_arcade_netplay.c (adapter linger == RL-4 default)" "${netplay_source}"
    "_Static_assert\\(NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS == NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS,")
ctr_require_regex("${netplay_header} (LAUNCH_LINGER_TICKS must stay 300u)" "${header}"
    "\n#define NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS 300u\r?\n")

# 8. The race drive's hold service (docs/LOCKSTEP_RACE_MILESTONE.md LR-9,
#    LR-50). NativeArcadeNetplay_RaceService runs only Tick's step 2 (the
#    lobby poll and the foreign-drop read, through the one helper Tick itself
#    calls, so the two cannot drift apart) and, on a launch period, the
#    launch intake and the launch send and linger tick, only on RACING. Its
#    body must name exactly those helpers and the RACING gate, and nothing
#    that runs or feeds the flow, the menu input, the select session, the
#    outcome tracker, the roster, the race-end record, a lobby action, or the
#    launch agreement directly. It reaches the link and the session only
#    through the lobby, and the lobby only through the poll helper: no
#    NativeLockstepPeerLink_ or NativeLockstepSession_ call and no direct
#    ->lobby access in its body.
string(FIND "${netplay_source}" "\nvoid NativeArcadeNetplay_RaceService(" race_service_at)
if(race_service_at EQUAL -1)
    message(FATAL_ERROR "arcade netplay isolation: platform/native_arcade_netplay.c must define NativeArcadeNetplay_RaceService")
endif()
string(SUBSTRING "${netplay_source}" "${race_service_at}" -1 race_service_tail)
string(FIND "${race_service_tail}" "\n}" race_service_end)
if(race_service_end EQUAL -1)
    message(FATAL_ERROR "arcade netplay isolation: cannot find the end of NativeArcadeNetplay_RaceService")
endif()
string(SUBSTRING "${race_service_tail}" 0 "${race_service_end}" race_service_body)
foreach(required IN ITEMS
        "NativeArcadeNetplay_PollLobby(netplay);"
        "NativeArcadeNetplay_DriveLaunch(netplay);"
        "NativeArcadeNetplay_SendLaunch(netplay);"
        "NativeArcadeFlow_Screen(&netplay->flow) != NATIVE_ARCADE_FLOW_SCREEN_RACING"
        "launchPeriod != 0")
    string(FIND "${race_service_body}" "${required}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: NativeArcadeNetplay_RaceService must contain '${required}'")
    endif()
endforeach()
string(REPLACE "NativeArcadeFlow_Screen(" "" race_service_scan "${race_service_body}")
foreach(term IN ITEMS
        NativeArcadeFlow_ NativeArcadeMenuInput_ NativeArcadeLaunch_ NativeLobbyState_
        NativeLockstepMatchOutcome_ NativeLockstepMatchRoster_ NativeMatchSelect
        NativeLockstepPeerLink_ NativeLockstepSession_ "->lobby"
        NativeArcadeNetplay_Tick NativeArcadeNetplay_OnTakeResult DriveSelect SendSelect ApplyLatchedOutcome BeginLaunch BeginLobby CloseLobby RestartLobby
        BeginRematch BeginSelect Relink ArmRace lastMenuEvent pendingLinkFailure localRaceFailure raceEnd)
    string(FIND "${race_service_scan}" "${term}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "arcade netplay isolation: NativeArcadeNetplay_RaceService must not name '${term}'")
    endif()
endforeach()
# Tick shares the same poll helper, and nothing else polls the lobby.
string(REGEX MATCHALL "NativeLobbyState_Poll[(]" lobby_polls "${netplay_source}")
list(LENGTH lobby_polls lobby_poll_count)
if(NOT lobby_poll_count EQUAL 1)
    message(FATAL_ERROR "arcade netplay isolation: expected exactly one NativeLobbyState_Poll call (in NativeArcadeNetplay_PollLobby), found ${lobby_poll_count}")
endif()
string(REGEX MATCHALL "NativeArcadeNetplay_PollLobby[(]netplay[)]" poll_lobby_calls "${netplay_source}")
list(LENGTH poll_lobby_calls poll_lobby_call_count)
if(NOT poll_lobby_call_count EQUAL 2)
    message(FATAL_ERROR "arcade netplay isolation: expected NativeArcadeNetplay_PollLobby called exactly twice (Tick and RaceService), found ${poll_lobby_call_count}")
endif()
