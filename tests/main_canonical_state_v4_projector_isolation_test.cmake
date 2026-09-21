set(root "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${root}/CMakeLists.txt" cmake)

# Revised boundary: the dormant runtime coordinator is a permitted consumer of
# the V4 projector and may link it so its own declared dependency graph is
# complete.  The invariant that must hold is call-site dormancy: every live
# path stays unreferenced from the projector, its context, and its V4 mode.
# Only the isolated ABI/value tests link the projector.
string(REGEX MATCH "target_link_libraries\\(ctr_native[ \\t][^)]*ctr_native_canonical_projector_v4" executable_link "${cmake}")
if(executable_link)
    message(FATAL_ERROR "main_canonical_state_v4_projector_isolation: game executable must not link the V4 projector")
endif()

set(live_units "${root}/game/MAIN/MainMain.c" "${root}/main.c")
file(GLOB live_schedulers "${root}/platform/native_replay_scheduler*.c")
if(NOT live_schedulers)
    message(FATAL_ERROR "main_canonical_state_v4_projector_isolation: no replay scheduler sources found")
endif()
list(APPEND live_units ${live_schedulers})

foreach(live_unit IN LISTS live_units)
    file(READ "${live_unit}" live_source)
    foreach(forbidden IN ITEMS
        MainCanonicalState_ProjectV4
        MainCanonicalStateV4Context_Init
        MainCanonicalStateV4
        "--record-v4"
        "--replay-v4")
        string(FIND "${live_source}" "${forbidden}" dormancy_hit)
        if(NOT dormancy_hit EQUAL -1)
            message(FATAL_ERROR "main_canonical_state_v4_projector_isolation: ${live_unit} must not reference ${forbidden}")
        endif()
    endforeach()
endforeach()
