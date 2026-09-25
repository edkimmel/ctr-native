set(root "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${root}/CMakeLists.txt" cmake)
file(READ "${root}/game/MAIN/MainCanonicalRuntime.h" runtime_header)
file(READ "${root}/game/MAIN/MainCanonicalRuntime.c" runtime_source)
file(READ "${root}/main.c" main_source)
file(READ "${root}/game/MAIN/MainMain.c" mainmain_source)
file(READ "${root}/platform/native_replay_scheduler_seam.c" scheduler_source)

# Inspect the runtime's own direct link declaration.  Its consumers being clean
# does not prove the coordinator itself stays off the V4 file/session,
# lease, and MainMain paths.
string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*ctr_native_canonical_runtime([^)]+)\\)" runtime_link_call "${cmake}")
if(NOT runtime_link_call)
    message(FATAL_ERROR "main_canonical_runtime_isolation: missing direct link declaration")
endif()
set(runtime_dependencies "${CMAKE_MATCH_1}")

foreach(forbidden IN ITEMS
    ctr_native_replay_scheduler_v4
    ctr_native_replay_v4
    ctr_native_replay_v4_file
    main_canonical_topology_lease_authority
    main_canonical_topology_lifecycle_events
    MainMain)
    string(FIND "${runtime_dependencies}" "${forbidden}" dependency_hit)
    if(NOT dependency_hit EQUAL -1)
        message(FATAL_ERROR "main_canonical_runtime_isolation: runtime must not link ${forbidden}")
    endif()
endforeach()

# The runtime translation unit and its public header must not pull in the
# lease owner, the lease runtime, the lifecycle recorder, or MainMain.
foreach(forbidden IN ITEMS
    MainCanonicalTopologyLeaseAuthority
    MainCanonicalTopologyLeaseRuntime
    MainCanonicalTopologyLifecycleEvents
    "MainMain.h"
    "MainMain_")
    string(FIND "${runtime_header}" "${forbidden}" header_hit)
    if(NOT header_hit EQUAL -1)
        message(FATAL_ERROR "main_canonical_runtime_isolation: runtime header must not reference ${forbidden}")
    endif()
    string(FIND "${runtime_source}" "${forbidden}" source_hit)
    if(NOT source_hit EQUAL -1)
        message(FATAL_ERROR "main_canonical_runtime_isolation: runtime source must not reference ${forbidden}")
    endif()
endforeach()

# No CLI selector parses the V4 record/playback switches; the runtime V4 path
# has no file or session recording on the live path: its one live caller
# projects and digests, and records nothing (below).
foreach(cli_unit IN ITEMS main_source mainmain_source scheduler_source)
    foreach(forbidden IN ITEMS "--record-v4" "--replay-v4")
        string(FIND "${${cli_unit}}" "${forbidden}" cli_hit)
        if(NOT cli_hit EQUAL -1)
            message(FATAL_ERROR "main_canonical_runtime_isolation: ${cli_unit} must not parse ${forbidden}")
        endif()
    endforeach()
endforeach()
# The runtime has one live caller file: the race digest module,
# game/MAIN/MainArcadeRaceDigest.c (the Task 8 race plan, LR-10, slice
# LR-S4). Its callers are the internal roster proof and (since LR-S10 part 2)
# the race caller (tests/main_arcade_race_digest_isolation_test.cmake pins them;
# the race caller's own pin is at the end of this file).
# No other first-party game, platform, include, or host source names a
# MainCanonicalRuntime_ entry in code (comments removed), and the caller file
# stays off the lease, replay, and MainMain paths.
set(runtime_caller "game/MAIN/MainArcadeRaceDigest.c")
file(GLOB_RECURSE runtime_scan
    "${root}/game/*.c" "${root}/game/*.h" "${root}/game/*.inc"
    "${root}/platform/*.c" "${root}/platform/*.h" "${root}/include/*.h")
list(APPEND runtime_scan "${root}/main.c")
list(LENGTH runtime_scan runtime_scan_count)
if(runtime_scan_count LESS 300)
    message(FATAL_ERROR "main_canonical_runtime_isolation: scanned only ${runtime_scan_count} files; the scan is broken")
endif()
set(runtime_callers "")
foreach(path IN LISTS runtime_scan)
    file(RELATIVE_PATH relative_path "${root}" "${path}")
    if(relative_path STREQUAL "game/MAIN/MainCanonicalRuntime.c" OR relative_path STREQUAL "game/MAIN/MainCanonicalRuntime.h")
        continue()
    endif()
    file(READ "${path}" scanned_source)
    string(FIND "${scanned_source}" "MainCanonicalRuntime_" raw_hit)
    if(raw_hit EQUAL -1)
        continue()
    endif()
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*" " " scanned_code "${scanned_source}")
    string(FIND "${scanned_code}" "MainCanonicalRuntime_" code_hit)
    if(NOT code_hit EQUAL -1)
        list(APPEND runtime_callers "${relative_path}")
    endif()
endforeach()
if(NOT "${runtime_callers}" STREQUAL "${runtime_caller}")
    message(FATAL_ERROR "main_canonical_runtime_isolation: the runtime's live callers are '${runtime_callers}'; the one live caller file is ${runtime_caller}")
endif()
file(READ "${root}/${runtime_caller}" caller_source)
string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*" " " caller_code "${caller_source}")
foreach(forbidden IN ITEMS
    MainCanonicalTopologyLease
    MainCanonicalTopologyLifecycleEvents
    native_replay
    Replay
    GetSubmission
    "MainMain")
    string(FIND "${caller_code}" "${forbidden}" caller_hit)
    if(NOT caller_hit EQUAL -1)
        message(FATAL_ERROR "main_canonical_runtime_isolation: ${runtime_caller} must not reference ${forbidden}")
    endif()
endforeach()
string(REGEX MATCH "(^|[^Ee])(lease|Lease|LEASE)" caller_lease "${caller_code}")
if(NOT "${caller_lease}" STREQUAL "")
    message(FATAL_ERROR "main_canonical_runtime_isolation: ${runtime_caller} must not name the topology lease")
endif()

# The race caller (game/MAIN/MainArcadeRaceLaunch.c, the Task 8 race plan
# LR-S10 part 2) reaches the runtime only through the race digest module: it
# names no MainCanonicalRuntime token at all (comments included), and it
# projects through MainArcadeRaceDigest_ProjectState.
set(race_caller "game/MAIN/MainArcadeRaceLaunch.c")
file(READ "${root}/${race_caller}" race_caller_source)
string(FIND "${race_caller_source}" "MainCanonicalRuntime" race_caller_runtime_hit)
if(NOT race_caller_runtime_hit EQUAL -1)
    message(FATAL_ERROR "main_canonical_runtime_isolation: ${race_caller} must reach the runtime only through MainArcadeRaceDigest")
endif()
string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*" " " race_caller_code "${race_caller_source}")
string(FIND "${race_caller_code}" "MainArcadeRaceDigest_ProjectState(" race_caller_project_hit)
if(race_caller_project_hit EQUAL -1)
    message(FATAL_ERROR "main_canonical_runtime_isolation: ${race_caller} must project its race ticks through MainArcadeRaceDigest_ProjectState")
endif()
