# Structural isolation for the arcade screen flow seam (native_arcade_flow):
# a pure state machine with no OS-networking, topology-lease, heap, clock,
# game, presentation, or netplay dependency. Its only includes are stdint.h,
# stddef.h, its own header, and the arcade menu input header; the library
# links only ctr_native_arcade_menu_input; the nine default timings cannot
# silently change; and the target stays portable C17 with extensions off.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade flow isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade flow isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "arcade flow isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# The flow seam files.
set(flow_header "include/platform/native_arcade_flow.h")
set(flow_files
    "${flow_header}"
    "platform/native_arcade_flow.c")

# 1. No socket / OS-networking header or symbol.
set(network_tokens
    winsock WinSock WSA socket Socket AF_INET sockaddr htons htonl ntohs ntohl
    SDL_net SDL "<sys/" netinet getaddrinfo "select(" "poll(")

# 2. No topology-lease symbol.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 3. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 4. No clock / game / presentation dependency.
set(dependency_tokens
    "time(" "clock(" QueryPerformance "game/" Game_ "main.c" native_renderer
    native_display_config native_frame_capture texture_filter)

# 5. No netplay, lobby, or deterministic-state dependency: the flow sees
#    only its own enums and never names the link or its state.
set(netplay_tokens
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport canonical replay)

foreach(relative_path IN LISTS flow_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS network_tokens lease_tokens alloc_tokens dependency_tokens netplay_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 6. #include lines may only name stdint.h, stddef.h, the module's own
    #    header, or the arcade menu input header.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_menu_input\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade flow isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 7. ctr_native_arcade_flow links ctr_native_arcade_menu_input and nothing
#    else, in exactly one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_flow)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "arcade flow isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_menu_input")
    message(FATAL_ERROR "arcade flow isolation: ${target} must link only ctr_native_arcade_menu_input (found '${link_items}')")
endif()

# 8. C17, no extensions, on the flow target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade flow isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade flow isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade flow isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade flow isolation: ${target} C17/no-extensions properties are out of order")
endif()

# 9. The nine default timings (30 Hz ticks, UX-3/5/7/10, SEL-8/9) are frozen.
ctr_read_source("${flow_header}" header)
ctr_require_regex("${flow_header} (LOBBY_RETRY_PAUSE_TICKS must stay 30u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_LOBBY_RETRY_PAUSE_TICKS 30u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (MATCH_FOUND_HOLD_TICKS must stay 45u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_MATCH_FOUND_HOLD_TICKS 45u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (RESULTS_DWELL_TICKS must stay 30u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_DWELL_TICKS 30u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (RESULTS_IDLE_TIMEOUT_TICKS must stay 900u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_RESULTS_IDLE_TIMEOUT_TICKS 900u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (REMATCH_WAIT_TIMEOUT_TICKS must stay 300u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_REMATCH_WAIT_TIMEOUT_TICKS 300u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (OPPONENT_LEFT_NOTICE_TICKS must stay 90u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_OPPONENT_LEFT_NOTICE_TICKS 90u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (EXIT_HOLD_TICKS must stay 60u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_EXIT_HOLD_TICKS 60u[^0-9a-zA-Z_]")
#    The two select-phase timings (SEL-8, SEL-9) are frozen too.
ctr_require_regex("${flow_header} (SELECT_RESULT_HOLD_TICKS must stay 60u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_SELECT_RESULT_HOLD_TICKS 60u[^0-9a-zA-Z_]")
ctr_require_regex("${flow_header} (LAUNCH_TIMEOUT_TICKS must stay 300u)" "${header}"
    "#define NATIVE_ARCADE_FLOW_DEFAULT_LAUNCH_TIMEOUT_TICKS 300u[^0-9a-zA-Z_]")
