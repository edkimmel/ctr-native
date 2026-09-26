# Fixture wrapper for native_assets_data_dir_unit (docs/PACKAGING.md PK-6):
# recreates fabricated data folders under WORK_DIR (tiny dummy files, never
# game data) and runs TEST_EXE on them.

if(NOT TEST_EXE OR NOT WORK_DIR)
    message(FATAL_ERROR "native assets data dir: TEST_EXE and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/exe/empty")
file(WRITE "${WORK_DIR}/exe/data/ctr-u.bin" "dummy")
file(WRITE "${WORK_DIR}/exe/data with space/ctr-u.bin" "dummy")
file(WRITE "${WORK_DIR}/exe/big/bigfile.big" "dummy")
file(WRITE "${WORK_DIR}/abs/CTR-U.BIN" "dummy")

execute_process(COMMAND "${TEST_EXE}" "${WORK_DIR}" RESULT_VARIABLE result)
file(REMOVE_RECURSE "${WORK_DIR}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "native assets data dir: ${TEST_EXE} failed (${result})")
endif()
