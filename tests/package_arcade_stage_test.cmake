# The package smoke tools without a game run (docs/PACKAGING.md "Package
# smoke gate"):
#   - tools/package-arcade.ps1 -StageExecutable/-StageDirectory writes the
#     package file set from a fabricated dummy exe (never game data) with a
#     MANIFEST marked as a staged test package, and refuses a destination
#     outside build-msvc-x86\, the build folder itself, a folder holding a
#     non-package file or a subdirectory, an exe inside the destination, and
#     half a parameter pair;
#   - the stage and the real package share one staging function;
#   - tools/package-arcade-smoke.ps1, tools/arcade-link-launch-check.ps1, and
#     tools/arcade-discovery-link-check.ps1 reject their argument misuses
#     before any run;
#   - the smoke refuses a package arcade.cfg that sets a port, a peer, a
#     group, or a seat other than auto, before any run, and runs the
#     discovery gate in config mode (DISC-S5).
# WORK_DIR must lie under REPO_DIR/build-msvc-x86 (the stage mode refuses
# anything else).

if(NOT REPO_DIR OR NOT WORK_DIR)
    message(FATAL_ERROR "package stage: REPO_DIR and WORK_DIR are required")
endif()

set(package_script "${REPO_DIR}/tools/package-arcade.ps1")
set(smoke_script "${REPO_DIR}/tools/package-arcade-smoke.ps1")
set(gate_script "${REPO_DIR}/tools/arcade-link-launch-check.ps1")
set(discovery_gate_script "${REPO_DIR}/tools/arcade-discovery-link-check.ps1")
foreach(script IN ITEMS "${package_script}" "${smoke_script}" "${gate_script}" "${discovery_gate_script}")
    if(NOT EXISTS "${script}")
        message(FATAL_ERROR "package stage: missing ${script}")
    endif()
endforeach()

get_filename_component(build_root "${REPO_DIR}/build-msvc-x86" ABSOLUTE)
get_filename_component(work_dir "${WORK_DIR}" ABSOLUTE)
string(FIND "${work_dir}/" "${build_root}/" work_at)
if(NOT work_at EQUAL 0)
    message(FATAL_ERROR "package stage: WORK_DIR ${work_dir} must lie under ${build_root}")
endif()

# Runs a script with the given arguments; checks the exit code and that the
# output holds expected_text.
function(expect_script name script expected_result expected_text)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${script}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output)
    message(STATUS "package stage [${name}] exit ${result}:\n${output}${error_output}")
    if(NOT "${result}" STREQUAL "${expected_result}")
        message(FATAL_ERROR "package stage: case '${name}' exited ${result}, expected ${expected_result}")
    endif()
    string(FIND "${output}${error_output}" "${expected_text}" text_at)
    if(text_at EQUAL -1)
        message(FATAL_ERROR "package stage: case '${name}' output lacks '${expected_text}'")
    endif()
endfunction()

file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
set(dummy_exe "${work_dir}/source/dummy_ctr_native.exe")
file(WRITE "${dummy_exe}" "dummy exe for the stage test, not a program\n")

# 1. A stage from the dummy exe: the exact file set, the exe and templates
# verbatim, and a MANIFEST marked as a staged test package listing every
# other file's size and SHA-256.
set(staged "${work_dir}/staged")
expect_script(stage "${package_script}" 0 "PASS: retail-data guard"
    -StageExecutable "${dummy_exe}" -StageDirectory "${staged}")
file(GLOB staged_entries RELATIVE "${staged}" "${staged}/*")
list(SORT staged_entries)
set(expected_entries MANIFEST.txt README.txt arcade.cfg ctr_native.exe)
list(SORT expected_entries)
if(NOT "${staged_entries}" STREQUAL "${expected_entries}")
    message(FATAL_ERROR "package stage: staged files are '${staged_entries}', expected '${expected_entries}'")
endif()
# file(READ) drops the CRs on Windows: the CRLF line ends are checked on the
# hex bytes (every LF preceded by a CR, and at least one line).
file(READ "${staged}/MANIFEST.txt" manifest_hex HEX)
string(REGEX MATCHALL "[0-9a-f][0-9a-f]" manifest_bytes "${manifest_hex}")
set(previous_byte "")
set(line_ends 0)
foreach(byte IN LISTS manifest_bytes)
    if(byte STREQUAL "0a")
        if(NOT previous_byte STREQUAL "0d")
            message(FATAL_ERROR "package stage: MANIFEST.txt has a line end that is not CRLF")
        endif()
        math(EXPR line_ends "${line_ends} + 1")
    endif()
    set(previous_byte "${byte}")
endforeach()
if(line_ends EQUAL 0 OR NOT previous_byte STREQUAL "0a")
    message(FATAL_ERROR "package stage: MANIFEST.txt must be CRLF text ending in a line end")
endif()
file(READ "${staged}/MANIFEST.txt" manifest)
string(REPLACE "\r" "" manifest "${manifest}")
foreach(literal IN ITEMS
        "CTR Native arcade package\npackage: ctr-arcade-staged\n"
        "\nSTAGED TEST PACKAGE: "
        "Never deploy it.\n"
        "\nfiles (name, size in bytes, SHA-256):\n")
    string(FIND "${manifest}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "package stage: MANIFEST.txt lacks '${literal}':\n${manifest}")
    endif()
endforeach()
foreach(pair IN ITEMS "ctr_native.exe|${dummy_exe}" "arcade.cfg|${REPO_DIR}/tools/package/arcade.cfg"
        "README.txt|${REPO_DIR}/tools/package/README.txt")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 name)
    list(GET pair 1 source)
    file(SHA256 "${staged}/${name}" staged_hash)
    file(SHA256 "${source}" source_hash)
    if(NOT staged_hash STREQUAL source_hash)
        message(FATAL_ERROR "package stage: ${name} differs from ${source}")
    endif()
    file(SIZE "${staged}/${name}" staged_size)
    string(TOUPPER "${staged_hash}" upper_hash)
    string(FIND "${manifest}" "\n${name}  ${staged_size}  ${upper_hash}\n" line_at)
    if(line_at EQUAL -1)
        message(FATAL_ERROR "package stage: MANIFEST.txt lacks '${name}  ${staged_size}  ${upper_hash}':\n${manifest}")
    endif()
endforeach()
expect_script(stage_guard "${package_script}" 0 "PASS: retail-data guard" -CheckFolder "${staged}")

# 2. A package folder is recreated.
expect_script(restage "${package_script}" 0 "package: " -StageExecutable "${dummy_exe}" -StageDirectory "${staged}")

# 3. A folder holding a non-package file or a subdirectory is refused and
# left as it was.
file(MAKE_DIRECTORY "${work_dir}/foreign")
file(WRITE "${work_dir}/foreign/notes.txt" "keep me\n")
expect_script(foreign_file "${package_script}" 1 "it holds notes.txt, which is not a package file"
    -StageExecutable "${dummy_exe}" -StageDirectory "${work_dir}/foreign")
if(NOT EXISTS "${work_dir}/foreign/notes.txt")
    message(FATAL_ERROR "package stage: the refused folder lost notes.txt")
endif()
file(MAKE_DIRECTORY "${work_dir}/subdirectory/memcards")
expect_script(subdirectory "${package_script}" 1 "it holds memcards, which is not a package file"
    -StageExecutable "${dummy_exe}" -StageDirectory "${work_dir}/subdirectory")
if(NOT IS_DIRECTORY "${work_dir}/subdirectory/memcards")
    message(FATAL_ERROR "package stage: the refused folder lost memcards")
endif()

# 4. Destinations outside build-msvc-x86\, and the build folder itself.
set(outside "${REPO_DIR}/package-stage-test-refused")
expect_script(outside "${package_script}" 1 "refusing stage destination outside"
    -StageExecutable "${dummy_exe}" -StageDirectory "${outside}")
if(EXISTS "${outside}")
    message(FATAL_ERROR "package stage: the refused destination ${outside} was created")
endif()
expect_script(build_root "${package_script}" 1 "refusing stage destination outside"
    -StageExecutable "${dummy_exe}" -StageDirectory "${build_root}")

# 5. An exe inside the destination, a missing exe, half a pair, and the
# guard mode combined with the stage mode.
expect_script(exe_inside "${package_script}" 1 "refusing to stage an exe from inside the stage destination"
    -StageExecutable "${staged}/ctr_native.exe" -StageDirectory "${staged}")
expect_script(missing_exe "${package_script}" 1 "stage executable not found"
    -StageExecutable "${work_dir}/source/missing.exe" -StageDirectory "${work_dir}/never")
expect_script(only_exe "${package_script}" 1 "-StageExecutable and -StageDirectory must be given together"
    -StageExecutable "${dummy_exe}")
expect_script(only_directory "${package_script}" 1 "-StageExecutable and -StageDirectory must be given together"
    -StageDirectory "${work_dir}/never")
expect_script(check_and_stage "${package_script}" 1 "-CheckFolder cannot be combined"
    -CheckFolder "${staged}" -StageDirectory "${work_dir}/never")
if(EXISTS "${work_dir}/never")
    message(FATAL_ERROR "package stage: a refused destination was created")
endif()

# 6. One staging function: the copy, the MANIFEST file list, and the MANIFEST
# write exist once, and both modes call it.
file(READ "${package_script}" package_text)
foreach(literal IN ITEMS "'files (name, size in bytes, SHA-256):'" "Copy-Item -LiteralPath $SourceExe"
        "'MANIFEST.txt'), (($manifest -join" "function Write-PackageFolder(")
    set(remaining "${package_text}")
    set(hits 0)
    while(TRUE)
        string(FIND "${remaining}" "${literal}" at)
        if(at EQUAL -1)
            break()
        endif()
        math(EXPR hits "${hits} + 1")
        string(LENGTH "${literal}" literal_length)
        math(EXPR next "${at} + ${literal_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endwhile()
    if(NOT hits EQUAL 1)
        message(FATAL_ERROR "package stage: tools/package-arcade.ps1 must hold '${literal}' exactly once (found ${hits})")
    endif()
endforeach()
string(REGEX MATCHALL "\nWrite-PackageFolder \\$exe \\$destination @\\(|\n    Write-PackageFolder \\$stageExe \\$stageDestination @\\(" calls "${package_text}")
list(LENGTH calls call_count)
if(NOT call_count EQUAL 2)
    message(FATAL_ERROR "package stage: the real package and the stage must both call Write-PackageFolder (found ${call_count} calls)")
endif()

# 7. The smoke script's argument misuses, before any run or write.
expect_script(smoke_neither "${smoke_script}" 1 "give exactly one of -PackageDirectory and -StageExecutable"
    -OutputDirectory "${work_dir}/smoke")
expect_script(smoke_both "${smoke_script}" 1 "give exactly one of -PackageDirectory and -StageExecutable"
    -PackageDirectory "${staged}" -StageExecutable "${dummy_exe}" -OutputDirectory "${work_dir}/smoke")
expect_script(smoke_outside "${smoke_script}" 1 "refusing output directory outside"
    -PackageDirectory "${staged}" -OutputDirectory "${outside}")
expect_script(smoke_overlap "${smoke_script}" 1 "overlap"
    -PackageDirectory "${staged}" -OutputDirectory "${staged}/smoke")
expect_script(smoke_missing_package "${smoke_script}" 1 "package folder not found"
    -PackageDirectory "${work_dir}/missing" -OutputDirectory "${work_dir}/smoke")
if(EXISTS "${outside}" OR EXISTS "${staged}/smoke" OR EXISTS "${work_dir}/smoke")
    message(FATAL_ERROR "package stage: a refused smoke run created its output directory")
endif()

# 8. The gate's config files: both or neither, and each must exist. Both
# fail before the disc image check, so they need no game data.
expect_script(gate_one_config "${gate_script}" 1 "-Cab1Config and -Cab2Config must be given together"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/gate" -Cab1Config "${staged}/arcade.cfg")
expect_script(gate_missing_config "${gate_script}" 1 "config file for cab2 not found"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/gate" -Cab1Config "${staged}/arcade.cfg"
    -Cab2Config "${work_dir}/missing.cfg")
# The gate's ports: only without the config files (which hold their own),
# in range, and distinct. All fail before the disc image check.
expect_script(gate_port_with_config "${gate_script}" 1 "-Cab1Port and -Cab2Port apply only without -Cab1Config and -Cab2Config"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/gate" -Cab1Config "${staged}/arcade.cfg"
    -Cab2Config "${staged}/arcade.cfg" -Cab2Port 7102)
expect_script(gate_same_ports "${gate_script}" 1 "-Cab1Port and -Cab2Port must differ (both 7101)"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/gate" -Cab1Port 7101 -Cab2Port 7101)
expect_script(gate_port_range "${gate_script}" 1 "invalid port 65536 (1..65535)"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/gate" -Cab1Port 65536)
if(EXISTS "${work_dir}/gate")
    message(FATAL_ERROR "package stage: a refused gate run created its output directory")
endif()

# 9. The smoke's gate, tools/arcade-discovery-link-check.ps1 (DISC-S5): its
# config files both or neither, each existing, and its four ports in range
# and distinct. All fail before the disc image check.
expect_script(discovery_one_config "${discovery_gate_script}" 1 "-ConfigA and -ConfigB must be given together"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/discovery_gate" -ConfigA "${staged}/arcade.cfg")
expect_script(discovery_other_config "${discovery_gate_script}" 1 "-ConfigA and -ConfigB must be given together"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/discovery_gate" -ConfigB "${staged}/arcade.cfg")
expect_script(discovery_missing_config "${discovery_gate_script}" 1 "config file for run b not found"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/discovery_gate" -ConfigA "${staged}/arcade.cfg"
    -ConfigB "${work_dir}/missing.cfg")
expect_script(discovery_same_ports "${discovery_gate_script}" 1 "-LinkPortA, -LinkPortB, -DiscoveryPortA, and -DiscoveryPortB must all differ (7001, 7002, 7003, 7001)"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/discovery_gate" -LinkPortA 7001 -LinkPortB 7002
    -DiscoveryPortA 7003 -DiscoveryPortB 7001)
expect_script(discovery_port_range "${discovery_gate_script}" 1 "invalid port 65536 (1..65535)"
    -Executable "${dummy_exe}" -OutputDirectory "${work_dir}/discovery_gate" -DiscoveryPortB 65536)
if(EXISTS "${work_dir}/discovery_gate")
    message(FATAL_ERROR "package stage: a refused discovery gate run created its output directory")
endif()

# 10. The smoke's template check, before any run: a package whose arcade.cfg
# (MANIFEST line updated to match) sets a port, a peer, a group, or a seat
# other than auto is refused. A dummy disc image gets past the skip; the
# check fails before the gate starts.
set(dummy_assets "${work_dir}/source/dummy-disc.bin")
file(WRITE "${dummy_assets}" "dummy disc image for the stage test, not game data\n")
file(READ "${staged}/arcade.cfg" template_text)
file(SIZE "${staged}/arcade.cfg" template_size)
file(SHA256 "${staged}/arcade.cfg" template_hash)
string(TOUPPER "${template_hash}" template_hash)
file(READ "${staged}/MANIFEST.txt" staged_manifest)
string(FIND "${template_text}" "\nseat = auto" template_seat_at)
if(template_seat_at EQUAL -1)
    message(FATAL_ERROR "package stage: the staged arcade.cfg has no 'seat = auto' line")
endif()
# Each case: its name, the line appended to the template (the seat case
# replaces the seat line instead), and the smoke's expected refusal.
foreach(case IN ITEMS "port|port = 7001|sets 'port'" "peer|peer = 127.0.0.1:7002|sets 'peer'" "group|group = hall|sets 'group'"
        "seat|seat = cab1|seat is 'cab1', the template's is 'auto'")
    string(REPLACE "|" ";" case "${case}")
    list(GET case 0 case_name)
    list(GET case 1 case_line)
    list(GET case 2 case_text)
    set(bad_package "${work_dir}/bad_template_${case_name}")
    file(REMOVE_RECURSE "${bad_package}")
    file(MAKE_DIRECTORY "${bad_package}")
    foreach(name IN ITEMS ctr_native.exe README.txt)
        file(COPY "${staged}/${name}" DESTINATION "${bad_package}")
    endforeach()
    if(case_name STREQUAL "seat")
        string(REPLACE "\nseat = auto" "\n${case_line}" bad_text "${template_text}")
    else()
        set(bad_text "${template_text}${case_line}\n")
    endif()
    file(WRITE "${bad_package}/arcade.cfg" "${bad_text}")
    file(SIZE "${bad_package}/arcade.cfg" bad_size)
    file(SHA256 "${bad_package}/arcade.cfg" bad_hash)
    string(TOUPPER "${bad_hash}" bad_hash)
    string(REPLACE "arcade.cfg  ${template_size}  ${template_hash}" "arcade.cfg  ${bad_size}  ${bad_hash}" bad_manifest "${staged_manifest}")
    if(bad_manifest STREQUAL staged_manifest)
        message(FATAL_ERROR "package stage: could not rewrite the arcade.cfg MANIFEST line for case ${case_name}")
    endif()
    file(WRITE "${bad_package}/MANIFEST.txt" "${bad_manifest}")
    expect_script(smoke_template_${case_name} "${smoke_script}" 1 "${case_text}"
        -PackageDirectory "${bad_package}" -OutputDirectory "${work_dir}/smoke_template_${case_name}" -AssetsFile "${dummy_assets}")
    if(EXISTS "${work_dir}/smoke_template_${case_name}/gate")
        message(FATAL_ERROR "package stage: the smoke started the gate for a bad template (case ${case_name})")
    endif()
endforeach()

# 11. The smoke runs the discovery gate in config mode on 7001-7004, not the
# static LR-16 gate (DISC-S5).
file(READ "${smoke_script}" smoke_text)
foreach(literal IN ITEMS "$gateScript = Join-Path $PSScriptRoot 'arcade-discovery-link-check.ps1'"
        "-File $gateScript -Executable $runExe -ConfigA $configs['a'] -ConfigB $configs['b']"
        "@{ Name = 'a'; LinkPort = '7001'; DiscoveryPort = '7003' },"
        "@{ Name = 'b'; LinkPort = '7002'; DiscoveryPort = '7004' })"
        "$templateKeys = @('data_dir', 'seat', 'fullscreen', 'render_scale', 'texture_filter')"
        "$templateSeat = 'auto'")
    string(FIND "${smoke_text}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "package stage: tools/package-arcade-smoke.ps1 lacks '${literal}'")
    endif()
endforeach()
string(FIND "${smoke_text}" "Join-Path $PSScriptRoot 'arcade-link-launch-check.ps1'" static_gate_at)
if(NOT static_gate_at EQUAL -1)
    message(FATAL_ERROR "package stage: tools/package-arcade-smoke.ps1 must not run tools/arcade-link-launch-check.ps1")
endif()

file(REMOVE_RECURSE "${work_dir}")
message(STATUS "package stage: PASS")
