# Process-level startup checks for the per-cabinet config file
# (docs/PACKAGING.md PK-2..PK-6): runs the real ctr_native (CTR_EXE) with
# fabricated config files under WORK_DIR and requires exit code 1 and the
# expected message for each fatal case. Every case fails before the assets
# are validated and before Platform_Init, so no game data and no display are
# needed. stdin is an empty file, so the console "Press Enter" pause (only
# for a console the process owns alone) can never wait.

if(NOT CTR_EXE OR NOT WORK_DIR)
    message(FATAL_ERROR "config startup: CTR_EXE and WORK_DIR are required")
endif()
if(NOT EXISTS "${CTR_EXE}")
    message(FATAL_ERROR "config startup: missing ${CTR_EXE}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/empty_data")
file(WRITE "${WORK_DIR}/stdin.txt" "")
file(WRITE "${WORK_DIR}/empty.cfg" "")
file(WRITE "${WORK_DIR}/unknown_key.cfg" "# cabinet\nfullscreen = 1\nrender_scale = 2\n")
string(REPEAT "#\n" 8193 too_large)
file(WRITE "${WORK_DIR}/too_large.cfg" "${too_large}")
file(WRITE "${WORK_DIR}/link.cfg" "seat = cab1\nport = 7001\npeer = 127.0.0.1:7002\n")
file(WRITE "${WORK_DIR}/drive_relative.cfg" "data_dir = D:ctr-data\n")

# A UTF-16LE file (BOM FF FE, then "seat = cab1"): CMake strings cannot hold
# a NUL byte, so the bytes are written by the host shell.
set(utf16_cfg "${WORK_DIR}/utf16.cfg")
if(CMAKE_HOST_WIN32)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "[System.IO.File]::WriteAllBytes('${utf16_cfg}', [byte[]]([byte[]](0xFF,0xFE) + [System.Text.Encoding]::Unicode.GetBytes('seat = cab1')))"
        RESULT_VARIABLE utf16_result)
else()
    execute_process(
        COMMAND sh -c "printf '\\377\\376s\\000e\\000a\\000t\\000' > \"$1\"" sh "${utf16_cfg}"
        RESULT_VARIABLE utf16_result)
endif()
file(SIZE "${utf16_cfg}" utf16_size)
if(NOT utf16_result EQUAL 0 OR utf16_size LESS 4)
    message(FATAL_ERROR "config startup: could not write ${utf16_cfg} (${utf16_result}, ${utf16_size} bytes)")
endif()

set(case_count 0)
function(expect_startup_failure name expected_text)
    execute_process(
        COMMAND "${CTR_EXE}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        INPUT_FILE "${WORK_DIR}/stdin.txt"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
        TIMEOUT 60)
    set(all_output "${output}${error_output}")
    message(STATUS "config startup [${name}] exit ${result}:\n${all_output}")
    if(NOT "${result}" STREQUAL "1")
        message(FATAL_ERROR "config startup: case '${name}' exited '${result}', expected 1")
    endif()
    string(FIND "${all_output}" "${expected_text}" text_at)
    if(text_at EQUAL -1)
        message(FATAL_ERROR "config startup: case '${name}' output lacks '${expected_text}'")
    endif()
    string(FIND "${all_output}" "[CTR Native] Starting..." started_at)
    if(NOT started_at EQUAL -1 AND NOT name MATCHES "^data_dir_")
        message(FATAL_ERROR "config startup: case '${name}' got past the option checks")
    endif()
    math(EXPR next_count "${case_count} + 1")
    set(case_count ${next_count} PARENT_SCOPE)
endfunction()

expect_startup_failure(missing_config "cannot open config file ${WORK_DIR}/missing.cfg (file not found)"
    --config "${WORK_DIR}/missing.cfg")
expect_startup_failure(unknown_key "config file ${WORK_DIR}/unknown_key.cfg line 3: unknown key"
    --config "${WORK_DIR}/unknown_key.cfg")
expect_startup_failure(too_large "config file ${WORK_DIR}/too_large.cfg: file is larger than 16384 bytes"
    --config "${WORK_DIR}/too_large.cfg")
expect_startup_failure(utf16 "config file ${utf16_cfg} line 1: line contains a NUL byte (save the file as UTF-8 or ANSI text)"
    --config "${utf16_cfg}")
expect_startup_failure(link_with_replay
    "cannot be combined with replay record or playback options (link group from config file ${WORK_DIR}/link.cfg)"
    --config "${WORK_DIR}/link.cfg" --replay x)
expect_startup_failure(link_with_roster_proof
    "--arcade-roster-proof cannot be combined with --arcade-link, --arcade-link-preview, --exit-after-frame, or replay record or playback options (link group from config file ${WORK_DIR}/link.cfg)"
    --config "${WORK_DIR}/link.cfg" --arcade-roster-proof x)
expect_startup_failure(duplicate_config "invalid config option; expected --config <path> and --data-dir <dir>, each at most once"
    --config "${WORK_DIR}/empty.cfg" --config "${WORK_DIR}/empty.cfg")
expect_startup_failure(data_dir_empty "data directory ${WORK_DIR}/empty_data (resolved: ${WORK_DIR}/empty_data, from --data-dir) does not hold ctr-u.bin or BIGFILE.BIG"
    --config "${WORK_DIR}/empty.cfg" --data-dir "${WORK_DIR}/empty_data")
expect_startup_failure(data_dir_drive_relative "data directory C:ctr-data (from --data-dir) is drive-relative"
    --config "${WORK_DIR}/empty.cfg" --data-dir "C:ctr-data")
expect_startup_failure(data_dir_drive_relative_config
    "data directory D:ctr-data (from config file ${WORK_DIR}/drive_relative.cfg) is drive-relative"
    --config "${WORK_DIR}/drive_relative.cfg")

# Nothing but the fixtures was created (no replay report, roster log, or log file).
file(GLOB created RELATIVE "${WORK_DIR}" "${WORK_DIR}/*")
list(SORT created)
set(expected_created drive_relative.cfg empty.cfg empty_data link.cfg stdin.txt too_large.cfg unknown_key.cfg utf16.cfg)
if(NOT "${created}" STREQUAL "${expected_created}")
    message(FATAL_ERROR "config startup: the work folder holds '${created}', expected '${expected_created}'")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "config startup: ${case_count} cases passed")
