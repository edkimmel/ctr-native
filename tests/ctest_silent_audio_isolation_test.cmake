# Every process ctest starts runs on SDL's dummy audio driver (owner request:
# the development machine is also an arcade cabinet, and a test run must not
# play retail music through its speakers; the track sweep alone launches
# sixteen games at once).  The top-level CMakeLists.txt appends
# SDL_AUDIO_DRIVER=dummy to the ENVIRONMENT of every test in the directory at
# the end of its BUILD_TESTING block, so a future test is covered without a
# label.  The live check scripts start ctr_native with Start-Process, which
# inherits that environment.  Normal (non-test) runs are unaffected: nothing
# under platform/ forces the driver.
#
# 1. This script itself runs with SDL_AUDIO_DRIVER=dummy.
# 2. `ctest --show-only=json-v1` for this build and configuration lists this
#    test and the nine live game tests (each labelled "live"), so an empty or
#    partial listing cannot pass.
# 3. Every listed test has an ENVIRONMENT entry that is exactly
#    SDL_AUDIO_DRIVER=dummy, no other SDL_AUDIO_DRIVER entry, and no
#    ENVIRONMENT_MODIFICATION of SDL_AUDIO_DRIVER.
# 4. The host code does not override the driver choice: outside the Linux
#    default (set only when no hint or environment value is present), no
#    platform/ source names SDL_HINT_AUDIO_DRIVER or SDL_AUDIO_DRIVER, and
#    none sets a hint with override priority for it.
#
# Inputs: -DCTEST_COMMAND=... -DBUILD_DIR=... -DCONFIG=...

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "ctest silent audio")
set(expected_env "SDL_AUDIO_DRIVER=dummy")

foreach(input CTEST_COMMAND BUILD_DIR CONFIG)
    if(NOT DEFINED ${input} OR "${${input}}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: -D${input}=... is required")
    endif()
endforeach()

# 1. The environment this test was started with.
if(NOT "$ENV{SDL_AUDIO_DRIVER}" STREQUAL "dummy")
    message(FATAL_ERROR
        "${prefix}: this test runs with SDL_AUDIO_DRIVER='$ENV{SDL_AUDIO_DRIVER}', expected 'dummy'")
endif()

# 2. The registered tests.
execute_process(
    COMMAND "${CTEST_COMMAND}" --test-dir "${BUILD_DIR}" -C "${CONFIG}" --show-only=json-v1
    RESULT_VARIABLE listing_result
    OUTPUT_VARIABLE listing
    ERROR_VARIABLE listing_error)
if(NOT listing_result EQUAL 0)
    message(FATAL_ERROR "${prefix}: ctest --show-only=json-v1 failed (${listing_result}): ${listing_error}")
endif()

string(JSON test_count ERROR_VARIABLE json_error LENGTH "${listing}" tests)
if(json_error)
    message(FATAL_ERROR "${prefix}: cannot read the test listing: ${json_error}")
endif()
if(test_count LESS 1)
    message(FATAL_ERROR "${prefix}: the test listing is empty")
endif()

set(required_tests
    ctest_silent_audio_isolation
    ctr_native_version
    ctr_native_config_startup)
set(required_live_tests
    arcade_link_preview_render
    arcade_roster_determinism_two_cab
    arcade_roster_determinism_one_cab
    arcade_roster_track_sweep
    arcade_link_launch
    arcade_solo_race
    arcade_solo_wake_link
    arcade_discovery_link
    package_arcade_smoke)

set(seen_tests "")
set(seen_live_tests "")
math(EXPR last_test "${test_count} - 1")
foreach(test_index RANGE 0 ${last_test})
    string(JSON test_json GET "${listing}" tests ${test_index})
    string(JSON test_name GET "${test_json}" name)
    list(APPEND seen_tests "${test_name}")

    set(environment "")
    set(environment_modification "")
    set(labels "")
    string(JSON property_count ERROR_VARIABLE property_error LENGTH "${test_json}" properties)
    if(property_error)
        set(property_count 0)
    endif()
    if(property_count GREATER 0)
        math(EXPR last_property "${property_count} - 1")
        foreach(property_index RANGE 0 ${last_property})
            string(JSON property_name GET "${test_json}" properties ${property_index} name)
            if(NOT property_name MATCHES "^(ENVIRONMENT|ENVIRONMENT_MODIFICATION|LABELS)$")
                continue()
            endif()
            string(JSON value_count LENGTH "${test_json}" properties ${property_index} value)
            if(value_count LESS 1)
                continue()
            endif()
            math(EXPR last_value "${value_count} - 1")
            foreach(value_index RANGE 0 ${last_value})
                string(JSON value GET "${test_json}" properties ${property_index} value ${value_index})
                if(property_name STREQUAL "ENVIRONMENT")
                    list(APPEND environment "${value}")
                elseif(property_name STREQUAL "ENVIRONMENT_MODIFICATION")
                    list(APPEND environment_modification "${value}")
                else()
                    list(APPEND labels "${value}")
                endif()
            endforeach()
        endforeach()
    endif()

    # 3. Exactly one SDL_AUDIO_DRIVER entry, and it is the dummy driver.
    set(driver_entries "")
    foreach(entry IN LISTS environment)
        if(entry MATCHES "^SDL_AUDIO_DRIVER=")
            list(APPEND driver_entries "${entry}")
        endif()
    endforeach()
    if(NOT driver_entries STREQUAL expected_env)
        message(FATAL_ERROR
            "${prefix}: test ${test_name} has SDL_AUDIO_DRIVER entries '${driver_entries}' "
            "in ENVIRONMENT '${environment}', expected exactly '${expected_env}'")
    endif()
    foreach(entry IN LISTS environment_modification)
        if(entry MATCHES "^SDL_AUDIO_DRIVER=")
            message(FATAL_ERROR
                "${prefix}: test ${test_name} modifies SDL_AUDIO_DRIVER with ENVIRONMENT_MODIFICATION '${entry}'")
        endif()
    endforeach()

    if("live" IN_LIST labels)
        list(APPEND seen_live_tests "${test_name}")
    endif()
endforeach()

foreach(required IN LISTS required_tests required_live_tests)
    if(NOT required IN_LIST seen_tests)
        message(FATAL_ERROR "${prefix}: test ${required} is not in the ${CONFIG} test listing")
    endif()
endforeach()
foreach(required IN LISTS required_live_tests)
    if(NOT required IN_LIST seen_live_tests)
        message(FATAL_ERROR "${prefix}: live test ${required} is not labelled live")
    endif()
endforeach()

# 4. The host code leaves the driver choice to the environment.
file(GLOB platform_sources LIST_DIRECTORIES false
    "${repo}/platform/*.c" "${repo}/platform/*.h")
if(NOT platform_sources)
    message(FATAL_ERROR "${prefix}: no platform sources found")
endif()
set(linux_default_blocks 0)
foreach(path IN LISTS platform_sources)
    file(READ "${path}" source)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    # The one allowed use: the Linux default in NativeAudio_SelectDriverHint,
    # applied only when neither a hint nor the environment names a driver.
    # Matched one block at a time (the block holds ';', so a MATCHALL list
    # would split it).
    while(TRUE)
        string(REGEX MATCH
            "#if defined\\(__linux__\\)[\r\n\t ]+if \\(SDL_GetHint\\(SDL_HINT_AUDIO_DRIVER\\) == NULL\\)[^#]*#endif"
            block "${source}")
        if(block STREQUAL "")
            break()
        endif()
        math(EXPR linux_default_blocks "${linux_default_blocks} + 1")
        string(FIND "${block}" "SDL_SetHintWithPriority" priority_offset)
        if(NOT priority_offset EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${relative_path} sets the Linux audio driver default with a priority")
        endif()
        string(REPLACE "${block}" "" source "${source}")
    endwhile()
    foreach(term SDL_HINT_AUDIO_DRIVER SDL_AUDIO_DRIVER)
        string(FIND "${source}" "${term}" offset)
        if(NOT offset EQUAL -1)
            message(FATAL_ERROR
                "${prefix}: ${relative_path} names ${term} outside the guarded Linux default")
        endif()
    endforeach()
endforeach()
if(linux_default_blocks GREATER 1)
    message(FATAL_ERROR "${prefix}: expected at most one guarded Linux audio driver default, found ${linux_default_blocks}")
endif()

list(LENGTH seen_live_tests live_count)
message(STATUS "${prefix}: ${test_count} tests (${live_count} live) all run with ${expected_env}")
