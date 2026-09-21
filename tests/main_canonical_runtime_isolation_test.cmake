set(root "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${root}/CMakeLists.txt" cmake)
file(READ "${root}/game/MAIN/MainCanonicalRuntime.h" runtime_header)
file(READ "${root}/game/MAIN/MainCanonicalRuntime.c" runtime_source)
file(READ "${root}/main.c" main_source)
file(READ "${root}/game/MAIN/MainMain.c" mainmain_source)
file(READ "${root}/platform/native_replay_scheduler_seam.c" scheduler_source)

# Inspect the runtime's own direct link declaration.  Its consumers being clean
# does not prove the dormant coordinator itself stays off the V4 file/session,
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
# stays a typed, dormant seam until a live gate authorizes it.
foreach(cli_unit IN ITEMS main_source mainmain_source scheduler_source)
    foreach(forbidden IN ITEMS "--record-v4" "--replay-v4")
        string(FIND "${${cli_unit}}" "${forbidden}" cli_hit)
        if(NOT cli_hit EQUAL -1)
            message(FATAL_ERROR "main_canonical_runtime_isolation: ${cli_unit} must not parse ${forbidden}")
        endif()
    endforeach()
endforeach()