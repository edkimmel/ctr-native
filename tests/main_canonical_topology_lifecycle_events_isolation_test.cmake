file(READ "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" cmake)

string(FIND "${cmake}" "add_library(main_canonical_topology_lifecycle_events STATIC" target_decl)
if(target_decl EQUAL -1)
    message(FATAL_ERROR "main_canonical_topology_lifecycle_events_isolation: missing standalone target declaration")
endif()

string(REGEX MATCH "target_link_libraries\\(main_canonical_topology_lifecycle_events[ \\t\\r\\n]+" recorder_link "${cmake}")
if(recorder_link)
    message(FATAL_ERROR "main_canonical_topology_lifecycle_events_isolation: recorder must have no direct dependencies")
endif()

foreach(forbidden IN ITEMS
    ctr_native_canonical_runtime ctr_native_replay_scheduler_seam
    ctr_native_replay_scheduler_v3 ctr_native_replay_scheduler_v4
    ctr_native_replay_v2 ctr_native_replay_v2_file ctr_native_replay_v3 ctr_native_replay_v3_file
    ctr_native_replay_v4 ctr_native_replay_v4_file main_canonical_topology_lease_authority
    main_canonical_topology_lease_adapter ctr_native_deterministic_input_observation)
    string(REGEX MATCH "target_link_libraries\\(${forbidden}[^)]*main_canonical_topology_lifecycle_events" hit "${cmake}")
    if(hit)
        message(FATAL_ERROR "main_canonical_topology_lifecycle_events_isolation: ${forbidden} must not consume the recorder")
    endif()
endforeach()

set(recorder_header "${CMAKE_CURRENT_LIST_DIR}/../game/MAIN/MainCanonicalTopologyLifecycleEvents.h")
set(recorder_source "${CMAKE_CURRENT_LIST_DIR}/../game/MAIN/MainCanonicalTopologyLifecycleEvents.c")
file(READ "${recorder_header}" header_text)
file(READ "${recorder_source}" source_text)

string(REGEX MATCHALL "#[ \\t]*include[ \\t]+\\\"[^\\\"]+\\\"" header_includes "${header_text}")
if(header_includes)
    message(FATAL_ERROR "main_canonical_topology_lifecycle_events_isolation: public header must not include game headers")
endif()
string(REGEX MATCHALL "#[ \\t]*include[ \\t]+\\\"[^\\\"]+\\\"" source_includes "${source_text}")
if(NOT source_includes STREQUAL "#include \"MAIN/MainCanonicalTopologyLifecycleEvents.h\"")
    message(FATAL_ERROR "main_canonical_topology_lifecycle_events_isolation: unexpected recorder-source game include")
endif()

foreach(recorder_unit IN ITEMS header_text source_text)
    foreach(forbidden IN ITEMS
        MainMain MainFrame GameTracker gGT sdata Mempack MEMPACK Lease Adapter
        Runtime Replay Scheduler Network Socket Protocol Serialize Capture Activate Publish
        Platform VSync Tick)
        string(FIND "${${recorder_unit}}" "${forbidden}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "main_canonical_topology_lifecycle_events_isolation: forbidden ${recorder_unit} token ${forbidden}")
        endif()
    endforeach()
endforeach()
