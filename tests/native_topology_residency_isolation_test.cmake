file(READ "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" cmake)
set(target ctr_native_topology_residency)
string(FIND "${cmake}" "add_library(${target} STATIC platform/native_topology_residency.c)" declaration)
if(declaration EQUAL -1)
    message(FATAL_ERROR "topology residency: missing standalone target")
endif()
string(REGEX MATCH "target_link_libraries\\([ \\t\\r\\n]*${target}[^)]*\\)" links "${cmake}")
if(links)
    message(FATAL_ERROR "topology residency: target must not link game/runtime dependencies")
endif()
foreach(forbidden_consumer IN ITEMS ctr_native ctr_native_canonical_runtime ctr_native_replay_scheduler_v4 ctr_native_replay_v4 ctr_native_replay_v4_file ctr_native_canonical_topology ctr_native_canonical_projector_v4)
    string(REGEX MATCH "target_link_libraries\\([ \\t\\r\\n]*${forbidden_consumer}[^)]*${target}" bad "${cmake}")
    if(bad)
        message(FATAL_ERROR "topology residency: ${forbidden_consumer} must not consume validator")
    endif()
endforeach()
file(READ "${CMAKE_CURRENT_LIST_DIR}/../include/platform/native_topology_residency.h" header)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../platform/native_topology_residency.c" source)
# This is a supplied-facts validator only.  Keep it free of game ownership,
# lifecycle, and every replay/network/runtime acquisition path; the tokens are
# intentionally narrow enough that ordinary C implementation vocabulary does
# not make this contract noisy.
foreach(forbidden IN ITEMS
    "#include \"common.h\""
    GameTracker Mempack MainMain
    "NavHeader.last" lifecycle
    BOTS bot "UI_" "ui/"
    Schema schema Replay replay Scheduler scheduler
    Network network Socket socket
    Runtime runtime Extract extractor
    "game/" "Game_"
    GetActiveMempack AcquireLease ReleaseLease Mempack_Acquire Mempack_Release)
    string(FIND "${header}" "${forbidden}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "topology residency: forbidden header token ${forbidden}")
    endif()
    string(FIND "${source}" "${forbidden}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "topology residency: forbidden source token ${forbidden}")
    endif()
endforeach()
