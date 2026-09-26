# The live roster determinism check's two-way split, without a game run
# (tools/arcade-roster-proof-check.ps1 -Group and -ListChecks): the ctests
# arcade_roster_determinism_two_cab and _one_cab run -Group two-cab and
# -Group one-cab, and together they must run every check of -Group all.
#   - -Group all launches A-K and runs every check;
#   - -Group two-cab launches exactly A B C D E K, -Group one-cab exactly
#     A F G H I J (A as the base of the cross-profile checks);
#   - the checks two-cab and one-cab run together are exactly all's, and the
#     cross-profile checks (F != A, F's input digests against A and H) run in
#     one-cab;
#   - an unknown group is rejected.

if(NOT REPO_DIR)
    message(FATAL_ERROR "roster proof groups: REPO_DIR is required")
endif()

set(roster_script "${REPO_DIR}/tools/arcade-roster-proof-check.ps1")
if(NOT EXISTS "${roster_script}")
    message(FATAL_ERROR "roster proof groups: missing ${roster_script}")
endif()

# Lists group ${group}: sets ${group}_runs to its "group ... runs" text,
# ${group}_ran and ${group}_other to the ids of the checks it runs and leaves
# to the other group.  Nothing is launched: the executable and output
# directory are never used.
function(list_group group)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${roster_script}"
            -Executable "unused.exe" -OutputDirectory "C:/unused" -Group "${group}" -ListChecks
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
    set(ran "")
    set(other "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^group ${group} runs (.*)$")
            set(runs "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^check (.*): runs$")
            list(APPEND ran "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^check (.*): other group$")
            list(APPEND other "${CMAKE_MATCH_1}")
        elseif(NOT line STREQUAL "")
            message(FATAL_ERROR "roster proof groups: -Group ${group} printed an unexpected line '${line}'")
        endif()
    endforeach()
    set(${group}_runs "${runs}" PARENT_SCOPE)
    set(${group}_ran "${ran}" PARENT_SCOPE)
    set(${group}_other "${other}" PARENT_SCOPE)
endfunction()

list_group(all)
list_group(two-cab)
list_group(one-cab)

if(NOT all_runs STREQUAL "A B C D E F G H I J K")
    message(FATAL_ERROR "roster proof groups: -Group all runs '${all_runs}', expected A-K")
endif()
if(NOT two-cab_runs STREQUAL "A B C D E K")
    message(FATAL_ERROR "roster proof groups: -Group two-cab runs '${two-cab_runs}', expected 'A B C D E K'")
endif()
if(NOT one-cab_runs STREQUAL "A F G H I J")
    message(FATAL_ERROR "roster proof groups: -Group one-cab runs '${one-cab_runs}', expected 'A F G H I J'")
endif()

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
foreach(check IN ITEMS "K = A and hold" "A = B bytes" "C E = A")
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

message(STATUS "roster proof groups: PASS (${all_count} checks; two-cab and one-cab together run all of them)")
