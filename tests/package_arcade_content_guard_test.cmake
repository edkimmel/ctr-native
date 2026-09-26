# The package retail-data guard (docs/PACKAGING.md PK-7), through
# tools/package-arcade.ps1 -CheckFolder: builds fabricated package folders
# under WORK_DIR (tiny dummy files, never game data) and requires the exact
# allowlist to pass and each violation to fail.

if(NOT REPO_DIR OR NOT WORK_DIR)
    message(FATAL_ERROR "package guard: REPO_DIR and WORK_DIR are required")
endif()

set(script "${REPO_DIR}/tools/package-arcade.ps1")
if(NOT EXISTS "${script}")
    message(FATAL_ERROR "package guard: missing ${script}")
endif()

set(allowlist ctr_native.exe cab1.cfg cab2.cfg README.txt MANIFEST.txt)

function(make_package_folder name)
    set(folder "${WORK_DIR}/${name}")
    file(REMOVE_RECURSE "${folder}")
    file(MAKE_DIRECTORY "${folder}")
    foreach(file_name IN LISTS allowlist)
        file(WRITE "${folder}/${file_name}" "dummy ${file_name}\n")
    endforeach()
endfunction()

function(expect_guard name expected_result expected_text)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${script}" -CheckFolder "${WORK_DIR}/${name}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output)
    message(STATUS "package guard [${name}] exit ${result}:\n${output}${error_output}")
    if(NOT "${result}" STREQUAL "${expected_result}")
        message(FATAL_ERROR "package guard: case '${name}' exited ${result}, expected ${expected_result}")
    endif()
    string(FIND "${output}" "${expected_text}" text_at)
    if(text_at EQUAL -1)
        message(FATAL_ERROR "package guard: case '${name}' output lacks '${expected_text}'")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")

# The exact allowlist passes, including files at the 64 KiB limit.
make_package_folder(allowlist)
string(REPEAT "x" 65536 at_limit)
file(WRITE "${WORK_DIR}/allowlist/README.txt" "${at_limit}")
expect_guard(allowlist 0 "PASS: retail-data guard")

# A user's disc image.
make_package_folder(disc_image)
file(WRITE "${WORK_DIR}/disc_image/ctr-u.bin" "dummy")
expect_guard(disc_image 1 "retail data extension: ctr-u.bin")

# A BIOS file.
make_package_folder(bios)
file(WRITE "${WORK_DIR}/bios/SCPH1001.BIN" "dummy")
expect_guard(bios 1 "BIOS-like name ('scph'): SCPH1001.BIN")

# An extracted asset.
make_package_folder(bigfile)
file(WRITE "${WORK_DIR}/bigfile/BIGFILE.BIG" "dummy")
expect_guard(bigfile 1 "retail data extension: BIGFILE.BIG")

# An unknown file.
make_package_folder(unknown)
file(WRITE "${WORK_DIR}/unknown/notes.txt" "dummy")
expect_guard(unknown 1 "file not in the allowlist: notes.txt")

# A subdirectory, even an empty one.
make_package_folder(subdirectory)
file(MAKE_DIRECTORY "${WORK_DIR}/subdirectory/memcards")
expect_guard(subdirectory 1 "subdirectory: memcards")

# An oversize config.
make_package_folder(oversize)
string(REPEAT "x" 65537 over_limit)
file(WRITE "${WORK_DIR}/oversize/cab1.cfg" "${over_limit}")
expect_guard(oversize 1 "cab1.cfg is 65537 bytes")

# A case variant of an allowlisted name (names are case-sensitive).
make_package_folder(case_variant)
file(REMOVE "${WORK_DIR}/case_variant/ctr_native.exe")
file(WRITE "${WORK_DIR}/case_variant/CTR_NATIVE.EXE" "dummy")
expect_guard(case_variant 1 "file not in the allowlist: CTR_NATIVE.EXE")
expect_guard(case_variant 1 "missing file: ctr_native.exe")

# Hidden files: an extra one, and an allowlisted name with the hidden attribute.
function(make_hidden path)
    file(TO_NATIVE_PATH "${path}" native_path)
    execute_process(COMMAND attrib +h "${native_path}" RESULT_VARIABLE attrib_result)
    if(NOT attrib_result EQUAL 0)
        message(FATAL_ERROR "package guard: attrib +h ${native_path} failed (${attrib_result})")
    endif()
endfunction()
make_package_folder(hidden_extra)
file(WRITE "${WORK_DIR}/hidden_extra/Thumbs.db" "dummy")
make_hidden("${WORK_DIR}/hidden_extra/Thumbs.db")
expect_guard(hidden_extra 1 "hidden or system file: Thumbs.db")
make_package_folder(hidden_allowlisted)
make_hidden("${WORK_DIR}/hidden_allowlisted/README.txt")
expect_guard(hidden_allowlisted 1 "hidden or system file: README.txt")

# An oversize exe (32 MiB + 1 byte): a zero-filled file from
# `fsutil file createnew`, which needs no elevation. Skipped, with a status
# line saying so, only if fsutil fails on this host.
make_package_folder(oversize_exe)
file(REMOVE "${WORK_DIR}/oversize_exe/ctr_native.exe")
file(TO_NATIVE_PATH "${WORK_DIR}/oversize_exe/ctr_native.exe" oversize_exe_path)
execute_process(COMMAND fsutil file createnew "${oversize_exe_path}" 33554433
    RESULT_VARIABLE fsutil_result OUTPUT_QUIET ERROR_QUIET)
if(fsutil_result EQUAL 0)
    expect_guard(oversize_exe 1 "exe is 33554433 bytes (limit: under 33554432)")
else()
    message(STATUS "package guard [oversize_exe] SKIPPED: fsutil file createnew failed (${fsutil_result})")
endif()
file(REMOVE_RECURSE "${WORK_DIR}/oversize_exe")

# A missing file.
make_package_folder(missing)
file(REMOVE "${WORK_DIR}/missing/MANIFEST.txt")
expect_guard(missing 1 "missing file: MANIFEST.txt")

# A folder that does not exist.
expect_guard(does_not_exist 1 "not a folder")

file(REMOVE_RECURSE "${WORK_DIR}")
