# The live roster determinism check's two-way split, without a game run
# (tools/arcade-roster-proof-check.ps1 -Group and -ListChecks): the ctests
# arcade_roster_determinism_two_cab and _one_cab run -Group two-cab and
# -Group one-cab, and together they must run every check of -Group all.
#   - -Group all launches A-M and runs every check;
#   - -Group two-cab launches exactly A B C D E K L M, -Group one-cab exactly
#     A F G H I J (A as the base of the cross-profile checks);
#   - the checks two-cab and one-cab run together are exactly all's, and the
#     cross-profile checks (F != A, F's input digests against A and H) run in
#     one-cab;
#   - every group lists "track 3" (the arcade-link fixture's track) without
#     -Track, and "track 5" with -Track 5; a -Track outside 0..255 is
#     rejected;
#   - an unknown group is rejected;
#   - CMakeLists.txt registers the split: one foreach over exactly
#     "two-cab one-cab" whose add_test (the only
#     arcade_roster_determinism_ add_test in the file) runs the checker on
#     ctr_native with -Group ${roster_group}, its own output directory, and
#     -Ticks above both of the checker's fixed ticks (run K's $holdTick 300
#     and runs L and M's $clockTick 600: at or below either, the checker
#     skips those runs with a note and still passes), with SKIP_RETURN_CODE
#     77 and LABELS "live;live-roster" (the RESOURCE_LOCKs are pinned by
#     tests/arcade_roster_track_sweep_plan_test.cmake).

if(NOT REPO_DIR)
    message(FATAL_ERROR "roster proof groups: REPO_DIR is required")
endif()

set(roster_script "${REPO_DIR}/tools/arcade-roster-proof-check.ps1")
if(NOT EXISTS "${roster_script}")
    message(FATAL_ERROR "roster proof groups: missing ${roster_script}")
endif()

# Lists group ${group}: sets ${group}_runs to its "group ... runs" text,
# ${group}_track to its "track N" value, ${group}_ran and ${group}_other to
# the ids of the checks it runs and leaves to the other group.  Extra
# arguments are passed to the script (-Track N).  Nothing is launched: the
# executable and output directory are never used.
function(list_group group)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${roster_script}"
            -Executable "unused.exe" -OutputDirectory "C:/unused" -Group "${group}" -ListChecks ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output)
    message(STATUS "roster proof groups [${group}] exit ${result}:\n${output}${error_output}")
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "roster proof groups: -Group ${group} -ListChecks exited ${result}")
    endif()
    string(REPLACE "\r" "" output "${output}")
    string(REPLACE "\n" ";" lines "${output}")
    set(runs "")
    set(track "")
    set(ran "")
    set(other "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^group ${group} runs (.*)$")
            set(runs "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^track ([0-9]+)$")
            set(track "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^check (.*): runs$")
            list(APPEND ran "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^check (.*): other group$")
            list(APPEND other "${CMAKE_MATCH_1}")
        elseif(NOT line STREQUAL "")
            message(FATAL_ERROR "roster proof groups: -Group ${group} printed an unexpected line '${line}'")
        endif()
    endforeach()
    set(${group}_runs "${runs}" PARENT_SCOPE)
    set(${group}_track "${track}" PARENT_SCOPE)
    set(${group}_ran "${ran}" PARENT_SCOPE)
    set(${group}_other "${other}" PARENT_SCOPE)
endfunction()

list_group(all)
list_group(two-cab)
list_group(one-cab)

if(NOT all_runs STREQUAL "A B C D E F G H I J K L M")
    message(FATAL_ERROR "roster proof groups: -Group all runs '${all_runs}', expected A-M")
endif()
if(NOT two-cab_runs STREQUAL "A B C D E K L M")
    message(FATAL_ERROR "roster proof groups: -Group two-cab runs '${two-cab_runs}', expected 'A B C D E K L M'")
endif()
if(NOT one-cab_runs STREQUAL "A F G H I J")
    message(FATAL_ERROR "roster proof groups: -Group one-cab runs '${one-cab_runs}', expected 'A F G H I J'")
endif()

# Without -Track every group runs on the fixture's track 3; -Track 5 lists
# track 5 (and the same runs); a track outside 0..255 is rejected.
foreach(group IN ITEMS all two-cab one-cab)
    if(NOT "${${group}_track}" STREQUAL "3")
        message(FATAL_ERROR "roster proof groups: -Group ${group} lists track '${${group}_track}', expected the fixture's 3")
    endif()
endforeach()
set(default_one_cab_ran "${one-cab_ran}")
list_group(one-cab -Track 5)
if(NOT one-cab_track STREQUAL "5" OR NOT one-cab_runs STREQUAL "A F G H I J" OR NOT one-cab_ran STREQUAL default_one_cab_ran)
    message(FATAL_ERROR "roster proof groups: -Group one-cab -Track 5 lists track '${one-cab_track}' and runs '${one-cab_runs}', expected track 5 and the default runs and checks")
endif()
list_group(one-cab)
foreach(bad_track IN ITEMS 256 -1)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${roster_script}"
            -Executable "unused.exe" -OutputDirectory "C:/unused" -Group "one-cab" -ListChecks -Track ${bad_track}
        RESULT_VARIABLE bad_track_result
        OUTPUT_VARIABLE bad_track_output
        ERROR_VARIABLE bad_track_error)
    if(bad_track_result EQUAL 0)
        message(FATAL_ERROR "roster proof groups: -Track ${bad_track} was accepted:\n${bad_track_output}${bad_track_error}")
    endif()
endforeach()

list(LENGTH all_ran all_count)
if(all_count LESS 22 OR NOT "${all_other}" STREQUAL "")
    message(FATAL_ERROR "roster proof groups: -Group all runs ${all_count} checks and leaves '${all_other}'; it must run every check")
endif()
foreach(group IN ITEMS two-cab one-cab)
    list(LENGTH ${group}_ran count)
    list(LENGTH ${group}_other other_count)
    math(EXPR total "${count} + ${other_count}")
    if(NOT total EQUAL all_count)
        message(FATAL_ERROR "roster proof groups: -Group ${group} lists ${total} checks, -Group all ${all_count}")
    endif()
endforeach()

# The union of the split is exactly all's set.
set(split_ran ${two-cab_ran} ${one-cab_ran})
list(REMOVE_DUPLICATES split_ran)
foreach(check IN LISTS all_ran)
    if(NOT check IN_LIST split_ran)
        message(FATAL_ERROR "roster proof groups: check '${check}' runs in -Group all but in neither two-cab nor one-cab")
    endif()
endforeach()
foreach(check IN LISTS split_ran)
    if(NOT check IN_LIST all_ran)
        message(FATAL_ERROR "roster proof groups: check '${check}' runs in the split but not in -Group all")
    endif()
endforeach()

# The cross-profile checks stay in one-cab, the stall hold in two-cab.
foreach(check IN ITEMS "F != A" "F input vs A and H" "report A")
    if(NOT check IN_LIST one-cab_ran)
        message(FATAL_ERROR "roster proof groups: -Group one-cab does not run '${check}'")
    endif()
endforeach()
foreach(check IN ITEMS "K = A and hold" "A = B bytes" "C E = A" "L = M bytes")
    if(NOT check IN_LIST two-cab_ran)
        message(FATAL_ERROR "roster proof groups: -Group two-cab does not run '${check}'")
    endif()
endforeach()

execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${roster_script}"
        -Executable "unused.exe" -OutputDirectory "C:/unused" -Group "three-cab" -ListChecks
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error)
if(bad_result EQUAL 0)
    message(FATAL_ERROR "roster proof groups: -Group three-cab was accepted:\n${bad_output}${bad_error}")
endif()

# The live ctests' registration in CMakeLists.txt: the foreach over the
# split's groups and the add_test and properties inside it.
set(cmake_lists "${REPO_DIR}/CMakeLists.txt")
if(NOT EXISTS "${cmake_lists}")
    message(FATAL_ERROR "roster proof groups: missing ${cmake_lists}")
endif()
file(READ "${cmake_lists}" cmake_source)
string(REPLACE "\r" "" cmake_source "${cmake_source}")
set(foreach_header "foreach(roster_group IN ITEMS ")
string(FIND "${cmake_source}" "${foreach_header}" foreach_at)
if(foreach_at EQUAL -1)
    message(FATAL_ERROR "roster proof groups: CMakeLists.txt has no '${foreach_header}...' registering the roster determinism groups")
endif()
string(SUBSTRING "${cmake_source}" ${foreach_at} -1 foreach_tail)
string(FIND "${foreach_tail}" "endforeach()" foreach_end)
if(foreach_end EQUAL -1)
    message(FATAL_ERROR "roster proof groups: the roster_group foreach in CMakeLists.txt has no endforeach()")
endif()
string(SUBSTRING "${foreach_tail}" 0 ${foreach_end} foreach_block)
if(NOT foreach_block MATCHES "^foreach\\(roster_group IN ITEMS ([^)]*)\\)")
    message(FATAL_ERROR "roster proof groups: the roster_group foreach header is malformed:\n${foreach_block}")
endif()
string(STRIP "${CMAKE_MATCH_1}" registered_groups)
if(NOT registered_groups STREQUAL "two-cab one-cab")
    message(FATAL_ERROR "roster proof groups: CMakeLists.txt registers the groups '${registered_groups}', expected exactly 'two-cab one-cab' (the split, both and no other)")
endif()
string(REGEX MATCHALL "add_test\\(NAME arcade_roster_determinism_" registrations "${cmake_source}")
list(LENGTH registrations registration_count)
if(NOT registration_count EQUAL 1)
    message(FATAL_ERROR "roster proof groups: CMakeLists.txt has ${registration_count} arcade_roster_determinism_ add_tests, expected the one inside the roster_group foreach")
endif()
if(NOT foreach_block MATCHES "string\\(REPLACE \"-\" \"_\" roster_suffix \"\\\${roster_group}\"\\)")
    message(FATAL_ERROR "roster proof groups: the roster_group foreach no longer derives roster_suffix from roster_group:\n${foreach_block}")
endif()
if(NOT foreach_block MATCHES "add_test\\(NAME arcade_roster_determinism_\\\${roster_suffix}[ \t\n]+COMMAND([^)]*)\\)")
    message(FATAL_ERROR "roster proof groups: no add_test(NAME arcade_roster_determinism_\${roster_suffix} ...) inside the roster_group foreach:\n${foreach_block}")
endif()
set(group_command "${CMAKE_MATCH_1}")
foreach(argument IN ITEMS
        "-File \"\${CMAKE_SOURCE_DIR}/tools/arcade-roster-proof-check.ps1\""
        "-Executable \"$<TARGET_FILE:ctr_native>\""
        "-OutputDirectory \"\${CMAKE_BINARY_DIR}/arcade_roster_proof/\${roster_group}/$<CONFIG>\""
        "-Group \${roster_group}")
    string(REGEX REPLACE "[ \t\n]+" " " normalized_command "${group_command}")
    string(FIND "${normalized_command} " "${argument} " argument_at)
    if(argument_at EQUAL -1)
        message(FATAL_ERROR "roster proof groups: the roster determinism add_test lacks '${argument}':${group_command}")
    endif()
endforeach()
# -Ticks: an integer above run K's hold tick and runs L and M's clock tick,
# read from the checker, or the checker drops those runs (with a note) and
# the ctest still passes.
if(NOT group_command MATCHES "[ \t\n]-Ticks[ \t\n]+([^ \t\n]+)")
    message(FATAL_ERROR "roster proof groups: the roster determinism add_test registers no -Ticks:${group_command}")
endif()
set(registered_ticks "${CMAKE_MATCH_1}")
if(NOT registered_ticks MATCHES "^[0-9]+$")
    message(FATAL_ERROR "roster proof groups: the roster determinism add_test registers -Ticks '${registered_ticks}', not an integer")
endif()
file(READ "${roster_script}" roster_source)
foreach(fixed IN ITEMS holdTick clockTick)
    if(NOT roster_source MATCHES "\n\\$${fixed} = ([0-9]+)")
        message(FATAL_ERROR "roster proof groups: tools/arcade-roster-proof-check.ps1 has no '$${fixed} = N' line")
    endif()
    set(checker_${fixed} "${CMAKE_MATCH_1}")
    if(registered_ticks LESS_EQUAL checker_${fixed})
        message(FATAL_ERROR "roster proof groups: the roster determinism groups register -Ticks ${registered_ticks}, not above the checker's $${fixed} ${checker_${fixed}}: its runs would be skipped")
    endif()
endforeach()
if(checker_holdTick LESS 300 OR checker_clockTick LESS 600)
    message(FATAL_ERROR "roster proof groups: the checker's hold tick ${checker_holdTick} or clock tick ${checker_clockTick} fell below the documented 300 and 600")
endif()
if(NOT foreach_block MATCHES "set_tests_properties\\(arcade_roster_determinism_\\\${roster_suffix} PROPERTIES([^)]*)\\)")
    message(FATAL_ERROR "roster proof groups: no set_tests_properties for arcade_roster_determinism_\${roster_suffix} inside the roster_group foreach")
endif()
set(group_properties "${CMAKE_MATCH_1}")
if(NOT group_properties MATCHES "[ \t\n]SKIP_RETURN_CODE 77[ \t\n]" OR NOT group_properties MATCHES "[ \t\n]LABELS \"live;live-roster\"")
    message(FATAL_ERROR "roster proof groups: the roster determinism groups must be SKIP_RETURN_CODE 77 and LABELS \"live;live-roster\":${group_properties}")
endif()

message(STATUS "roster proof groups: PASS (${all_count} checks; two-cab and one-cab together run all of them; registered ${registered_groups} at -Ticks ${registered_ticks})")
