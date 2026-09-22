# Structural isolation for the failure-handling seam (native_lockstep_match_
# outcome, native_lockstep_match_roster, native_lockstep_rematch): no OS-
# networking dependency, no topology-lease symbol, no dynamic allocation, no
# clock, game, or presentation dependency, the three leaf libraries never link
# the virtual datagram test harness, the frozen stall-timeout constants cannot
# silently change, no other seam picks up a failure-handling identifier, the
# three targets stay portable C17 with extensions off, and none of the three
# modules ever includes the replay-scheduler-v4 or canonical-state-v4 headers.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "lockstep failure-handling isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "lockstep failure-handling isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "lockstep failure-handling isolation: required token '${term}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "lockstep failure-handling isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# The six failure-handling seam files.
set(failure_handling_files
    "include/platform/native_lockstep_match_outcome.h"
    "platform/native_lockstep_match_outcome.c"
    "include/platform/native_lockstep_match_roster.h"
    "platform/native_lockstep_match_roster.c"
    "include/platform/native_lockstep_rematch.h"
    "platform/native_lockstep_rematch.c")

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

foreach(relative_path IN LISTS failure_handling_files)
    ctr_read_source("${relative_path}" source)

    # None of the six failure-handling files contain a prose sentence that
    # collides (case-sensitively) with any network token: unlike
    # native_lockstep_session.h, there is nothing here to neutralize before
    # the network-token scan, so the scan below runs against the source
    # unmodified.
    foreach(term IN LISTS network_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
    foreach(term IN LISTS lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # native_lockstep_match_outcome.h documents itself, in prose, as an
    # "allocation-free policy layer" -- that phrase's own no-dynamic-
    # allocation guarantee contains "alloca" as a plain substring of
    # "allocation", which is not a use of the alloca() function. The
    # substitution below is narrowly scoped to this one phrase, is a no-op on
    # every other file (and on the rest of this file), and does not touch the
    # copy used by any other category, so a real alloca()/malloc()/etc. call
    # anywhere, including elsewhere in this same file, still fails the scan.
    string(REPLACE "allocation-free policy layer" "no-dynamic-memory-use policy layer" alloc_scan "${source}")
    foreach(term IN LISTS alloc_tokens)
        ctr_forbid("${relative_path}" "${alloc_scan}" "${term}")
    endforeach()
    foreach(term IN LISTS dependency_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 5. ctr_native_lockstep_match_outcome, ctr_native_lockstep_match_roster, and
#    ctr_native_lockstep_rematch must not link the virtual-datagram test
#    harness; only the fault-injection test executable
#    (native_lockstep_failure_handling_fault_test) is allowed to, and it is
#    not one of the three library targets checked here.
ctr_read_source("CMakeLists.txt" cmake)
foreach(target IN ITEMS ctr_native_lockstep_match_outcome ctr_native_lockstep_match_roster ctr_native_lockstep_rematch)
    string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*${target}[^)]*\\)" link_call "${cmake}")
    if("${link_call}" STREQUAL "")
        message(FATAL_ERROR "lockstep failure-handling isolation: missing target_link_libraries(${target} ...) in CMakeLists.txt")
    endif()
    string(FIND "${link_call}" "ctr_native_virtual_datagram" leak)
    if(NOT leak EQUAL -1)
        message(FATAL_ERROR "lockstep failure-handling isolation: ${target} must not link ctr_native_virtual_datagram")
    endif()
endforeach()

# 6. The stall-timeout constants are frozen. A changed literal must fail this
#    test, not silently change the failure-handling policy.
ctr_read_source("include/platform/native_lockstep_match_outcome.h" match_outcome_header)
ctr_require("include/platform/native_lockstep_match_outcome.h" "${match_outcome_header}"
    "#define NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES 180u")
ctr_require("include/platform/native_lockstep_match_outcome.h" "${match_outcome_header}"
    "#define NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES 30u")
ctr_require("include/platform/native_lockstep_match_outcome.h" "${match_outcome_header}"
    "#define NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES 600u")

# 7. No failure-handling identifier leaks into an unrelated seam: canonical
#    state, replay, match config, lockstep session/protocol/input-window, or
#    game code.
set(failure_handling_free_sources
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
    "platform/native_match_config.c"
    "include/platform/native_lockstep_session.h"
    "platform/native_lockstep_session.c"
    "include/platform/native_lockstep_protocol.h"
    "platform/native_lockstep_protocol.c"
    "include/platform/native_lockstep_input_window.h"
    "platform/native_lockstep_input_window.c")

file(GLOB_RECURSE game_sources RELATIVE "${repo}" "${repo}/game/*.c" "${repo}/game/*.h")
list(LENGTH game_sources game_source_count)
if(game_source_count EQUAL 0)
    message(FATAL_ERROR "lockstep failure-handling isolation: game/ source glob is empty; the leak scan cannot run")
endif()
list(APPEND failure_handling_free_sources ${game_sources})

foreach(relative_path IN LISTS failure_handling_free_sources)
    ctr_read_source("${relative_path}" source)
    foreach(term IN ITEMS MatchOutcome MatchRoster LockstepRematch)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 8. C17, no extensions, on all three failure-handling targets.
foreach(target IN ITEMS ctr_native_lockstep_match_outcome ctr_native_lockstep_match_roster ctr_native_lockstep_rematch)
    string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
    if(declare_at EQUAL -1)
        message(FATAL_ERROR "lockstep failure-handling isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
    endif()
    string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
    string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
    if(properties_at EQUAL -1)
        message(FATAL_ERROR "lockstep failure-handling isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
    endif()
    string(FIND "${target_block}" "C_STANDARD 17" standard_at)
    string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
    string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
    if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
        message(FATAL_ERROR "lockstep failure-handling isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
    endif()
    if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
        message(FATAL_ERROR "lockstep failure-handling isolation: ${target} C17/no-extensions properties are out of order")
    endif()
endforeach()

# 9. None of the three new .c/.h pairs may ever include
#    native_replay_scheduler_v4.h or native_canonical_state_v4.h. This is
#    stronger than a plain text-token scan: it regexes only #include lines, so
#    a legitimate mention of either header name in a comment (there is none
#    today) would not trip this check, but an actual #include would.
#    native_lockstep_match_outcome.h legitimately includes
#    native_lockstep_session.h, and native_lockstep_match_roster.h
#    legitimately includes native_lockstep_match_outcome.h and
#    native_match_config.h -- this check is specifically about the
#    replay-scheduler-v4 and canonical-state-v4 headers, which none of the
#    three modules need or may depend on.
foreach(relative_path IN LISTS failure_handling_files)
    ctr_read_source("${relative_path}" source)
    foreach(forbidden_header IN ITEMS native_replay_scheduler_v4.h native_canonical_state_v4.h)
        string(REGEX MATCH "#include[ \t]*[\"<][^\">]*${forbidden_header}[\">]" include_match "${source}")
        if(NOT "${include_match}" STREQUAL "")
            message(FATAL_ERROR "lockstep failure-handling isolation: ${relative_path} must not #include ${forbidden_header}")
        endif()
    endforeach()
endforeach()
