# The arcade roster track sweep's plan, without a game run
# (tools/arcade-roster-track-sweep.ps1 -ListRuns):
#   - the live ctest arcade_roster_track_sweep's registered -Profile, -Ticks,
#     -Parallel, and -TimeoutSeconds are read from CMakeLists.txt, and the
#     plan they give is checked, so a change to them is caught: it runs the
#     two-cab profile (the autopilot, which reaches the hazards), its ticks
#     reach past race tick 946 (the latest known TWO_CAB DRIVERS_FAILED, Polar
#     Pass) and, with one-cab, past 1785 (the ONE_CAB one, Dingo Canyon), and
#     the test is live-roster with SKIP_RETURN_CODE 77;
#   - the default plan covers all 16 arcade match-select tracks, in the order
#     of platform/native_match_select_rules.c k_matchSelectTracks
#     (NativeMatchSelect_TrackAt), each once per profile;
#   - every run passes seed 0x5EED, dwell 0, the ticks, and its track, and
#     its own report path; two-cab adds --arcade-roster-proof-autopilot and
#     no profile option, one-cab adds --arcade-roster-proof-profile one-cab
#     and never the autopilot;
#   - -Tracks selects tracks in the given order, and a non-table track (13
#     OXIDE_STATION, 17 TURBO_TRACK, 255) is rejected, as are tick counts
#     outside the profile's range and an unknown profile.

if(NOT REPO_DIR)
    message(FATAL_ERROR "track sweep plan: REPO_DIR is required")
endif()

set(sweep_script "${REPO_DIR}/tools/arcade-roster-track-sweep.ps1")
foreach(required IN ITEMS "${sweep_script}" "${REPO_DIR}/CMakeLists.txt" "${REPO_DIR}/platform/native_match_select_rules.c")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "track sweep plan: missing ${required}")
    endif()
endforeach()

# The match-select track table, in menu order.
file(READ "${REPO_DIR}/platform/native_match_select_rules.c" rules_source)
if(NOT rules_source MATCHES "k_matchSelectTracks\\[NATIVE_MATCH_SELECT_TRACK_COUNT\\] = \\{([0-9, ]+)\\};")
    message(FATAL_ERROR "track sweep plan: k_matchSelectTracks not found in platform/native_match_select_rules.c")
endif()
string(REPLACE " " "" table_tracks "${CMAKE_MATCH_1}")
string(REPLACE "," ";" table_tracks "${table_tracks}")
list(LENGTH table_tracks table_count)
if(NOT table_count EQUAL 16)
    message(FATAL_ERROR "track sweep plan: k_matchSelectTracks has ${table_count} tracks, expected 16")
endif()

# The live test's registration.
file(READ "${REPO_DIR}/CMakeLists.txt" cmake_source)
if(NOT cmake_source MATCHES "add_test\\(NAME arcade_roster_track_sweep[ \t\r\n]+COMMAND([^)]*)\\)")
    message(FATAL_ERROR "track sweep plan: the arcade_roster_track_sweep add_test was not found in CMakeLists.txt")
endif()
set(registered_command "${CMAKE_MATCH_1}")
if(NOT registered_command MATCHES "tools/arcade-roster-track-sweep\\.ps1")
    message(FATAL_ERROR "track sweep plan: arcade_roster_track_sweep does not run tools/arcade-roster-track-sweep.ps1")
endif()
foreach(option IN ITEMS Profile Ticks Parallel TimeoutSeconds)
    if(NOT registered_command MATCHES "-${option}[ \t\r\n]+([A-Za-z0-9-]+)")
        message(FATAL_ERROR "track sweep plan: arcade_roster_track_sweep registers no -${option}")
    endif()
    set(registered_${option} "${CMAKE_MATCH_1}")
endforeach()
if(NOT cmake_source MATCHES "set_tests_properties\\(arcade_roster_track_sweep PROPERTIES([^)]*)\\)")
    message(FATAL_ERROR "track sweep plan: no set_tests_properties for arcade_roster_track_sweep")
endif()
set(registered_properties "${CMAKE_MATCH_1}")
if(NOT registered_properties MATCHES "SKIP_RETURN_CODE 77" OR NOT registered_properties MATCHES "LABELS \"live;live-roster\"")
    message(FATAL_ERROR "track sweep plan: arcade_roster_track_sweep must be SKIP_RETURN_CODE 77 and LABELS \"live;live-roster\":${registered_properties}")
endif()
if(NOT registered_Profile STREQUAL "two-cab" AND NOT registered_Profile STREQUAL "both")
    message(FATAL_ERROR "track sweep plan: arcade_roster_track_sweep registers -Profile ${registered_Profile}; it must run two-cab (the autopilot)")
endif()
if(registered_Ticks LESS_EQUAL 946)
    message(FATAL_ERROR "track sweep plan: arcade_roster_track_sweep registers -Ticks ${registered_Ticks}; it must reach past race tick 946")
endif()
if(registered_Profile STREQUAL "both" AND registered_Ticks LESS_EQUAL 1785)
    message(FATAL_ERROR "track sweep plan: arcade_roster_track_sweep runs one-cab to -Ticks ${registered_Ticks}; it must reach past race tick 1785")
endif()

# Lists a plan: sets plan_result, plan_header, and plan_runs (one
# "track|name|profile|flags" entry per run, backslashes as slashes).
function(list_plan)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${sweep_script}"
            -Executable "unused.exe" -OutputDirectory "C:/unused" -ListRuns ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output)
    string(REPLACE "\r" "" output "${output}")
    string(REPLACE "\\" "/" output "${output}")
    string(REPLACE ";" "," output "${output}")
    string(REPLACE "\n" ";" lines "${output}")
    set(header "")
    set(runs "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^sweep ")
            set(header "${line}")
        elseif(line MATCHES "^run track ([0-9]+) \\(([^)]*)\\) profile ([a-z-]+): (.*)$")
            list(APPEND runs "${CMAKE_MATCH_1}|${CMAKE_MATCH_2}|${CMAKE_MATCH_3}|${CMAKE_MATCH_4}")
        elseif(NOT line STREQUAL "" AND result EQUAL 0)
            message(FATAL_ERROR "track sweep plan: -ListRuns ${ARGN} printed an unexpected line '${line}'")
        endif()
    endforeach()
    set(plan_result "${result}" PARENT_SCOPE)
    set(plan_output "${output}${error_output}" PARENT_SCOPE)
    set(plan_header "${header}" PARENT_SCOPE)
    set(plan_runs "${runs}" PARENT_SCOPE)
endfunction()

# Checks plan_runs against tracks ${tracks}, profiles ${profiles}, ticks
# ${ticks}: one run per (track, profile), track-major, with the exact flags.
function(check_plan label tracks profiles ticks)
    list(LENGTH tracks track_count)
    list(LENGTH profiles profile_count)
    math(EXPR expected_count "${track_count} * ${profile_count}")
    list(LENGTH plan_runs run_count)
    if(NOT run_count EQUAL expected_count)
        message(FATAL_ERROR "track sweep plan [${label}]: ${run_count} runs, expected ${expected_count}:\n${plan_output}")
    endif()
    set(index 0)
    foreach(track IN LISTS tracks)
        foreach(profile IN LISTS profiles)
            list(GET plan_runs ${index} entry)
            string(REPLACE "|" ";" fields "${entry}")
            list(GET fields 0 run_track)
            list(GET fields 1 run_name)
            list(GET fields 2 run_profile)
            list(GET fields 3 run_flags)
            if(NOT run_track STREQUAL track OR NOT run_profile STREQUAL profile OR run_name STREQUAL "")
                message(FATAL_ERROR "track sweep plan [${label}]: run ${index} is track ${run_track} (${run_name}) ${run_profile}, expected track ${track} ${profile}")
            endif()
            if(track LESS 10)
                set(padded "0${track}")
            else()
                set(padded "${track}")
            endif()
            if(profile STREQUAL "two-cab")
                set(profile_flags "--arcade-roster-proof-autopilot")
            else()
                set(profile_flags "--arcade-roster-proof-profile one-cab")
            endif()
            set(expected_flags "--arcade-roster-proof C:/unused/track${padded}-${profile}/report.txt --arcade-roster-proof-seed 0x5EED --arcade-roster-proof-dwell 0 --arcade-roster-proof-ticks ${ticks} ${profile_flags} --arcade-roster-proof-track ${track}")
            if(NOT run_flags STREQUAL expected_flags)
                message(FATAL_ERROR "track sweep plan [${label}]: run ${index} flags\n  '${run_flags}'\nexpected\n  '${expected_flags}'")
            endif()
            if(profile STREQUAL "one-cab" AND run_flags MATCHES "autopilot")
                message(FATAL_ERROR "track sweep plan [${label}]: one-cab run ${index} passes the autopilot")
            endif()
            math(EXPR index "${index} + 1")
        endforeach()
    endforeach()
endfunction()

# The registered plan.
if(registered_Profile STREQUAL "both")
    set(registered_profiles two-cab one-cab)
else()
    set(registered_profiles ${registered_Profile})
endif()
list_plan(-Profile ${registered_Profile} -Ticks ${registered_Ticks} -Parallel ${registered_Parallel} -TimeoutSeconds ${registered_TimeoutSeconds})
message(STATUS "track sweep plan [registered] exit ${plan_result}:\n${plan_output}")
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "track sweep plan: the registered arguments' -ListRuns exited ${plan_result}")
endif()
list(LENGTH registered_profiles registered_profile_count)
math(EXPR registered_runs "16 * ${registered_profile_count}")
if(NOT plan_header STREQUAL "sweep ticks ${registered_Ticks} profile ${registered_Profile} parallel ${registered_Parallel} runs ${registered_runs}")
    message(FATAL_ERROR "track sweep plan: header '${plan_header}' does not match the registered arguments")
endif()
check_plan(registered "${table_tracks}" "${registered_profiles}" "${registered_Ticks}")

# Each profile alone, and both (the script's default).
list_plan(-Profile two-cab -Ticks 6000)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "track sweep plan: -Profile two-cab -Ticks 6000 exited ${plan_result}:\n${plan_output}")
endif()
check_plan(two-cab "${table_tracks}" "two-cab" 6000)
list_plan(-Profile one-cab -Ticks 3600)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "track sweep plan: -Profile one-cab -Ticks 3600 exited ${plan_result}:\n${plan_output}")
endif()
check_plan(one-cab "${table_tracks}" "one-cab" 3600)
list_plan(-Ticks 900)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "track sweep plan: the default profile exited ${plan_result}:\n${plan_output}")
endif()
check_plan(default "${table_tracks}" "two-cab;one-cab" 900)

# -Tracks: the given table tracks, in the given order.
list_plan(-Profile two-cab -Ticks 1200 -Tracks "12,0")
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "track sweep plan: -Tracks 12,0 exited ${plan_result}:\n${plan_output}")
endif()
check_plan(tracks "12;0" "two-cab" 1200)

# Rejected: non-table tracks, a repeated track, ticks outside the range, an
# unknown profile.
foreach(bad IN ITEMS "-Tracks;13" "-Tracks;17" "-Tracks;255" "-Tracks;3,17" "-Tracks;3,3"
        "-Profile;both;-Ticks;3601" "-Profile;one-cab;-Ticks;3601" "-Profile;two-cab;-Ticks;6001" "-Ticks;0"
        "-Profile;three-cab")
    list_plan(${bad})
    if(plan_result EQUAL 0)
        message(FATAL_ERROR "track sweep plan: '${bad}' was accepted:\n${plan_output}")
    endif()
endforeach()

message(STATUS "track sweep plan: PASS (${registered_runs} registered runs: ${registered_Profile}, ${registered_Ticks} ticks, ${registered_Parallel} at a time; tracks ${table_tracks})")
