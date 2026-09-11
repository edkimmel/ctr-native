cmake_minimum_required(VERSION 3.20)

function(identity_test_require_equal actual expected messageText)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "native_build_identity_test: ${messageText}")
    endif()
endfunction()

function(identity_test_write_inputs path configuration compilerVersion runtime)
    file(WRITE "${path}"
        "profile=ctr-native-arcade-v1\n"
        "configuration=${configuration}\n"
        "target=win32\n"
        "compiler-id=MSVC\n"
        "compiler-version=${compilerVersion}\n"
        "generator-platform=Win32\n"
        "vs-toolset=default\n"
        "msvc-toolset-version=144\n"
        "windows-sdk=10.0.22621.0\n"
        "c-standard=17\n"
        "msvc-runtime=${runtime}\n"
    )
endfunction()

function(identity_test_generate input output repository)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DIDENTITY_REPOSITORY=${repository}"
            "-DIDENTITY_INPUT_FILE=${input}"
            "-DIDENTITY_OUTPUT_FILE=${output}"
            "-DIDENTITY_GIT_EXECUTABLE=${IDENTITY_TEST_GIT}"
            -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/GenerateNativeBuildIdentity.cmake"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "native_build_identity_test: generator failed")
    endif()
endfunction()

function(identity_test_read_known_and_digest path knownOutput digestOutput)
    file(READ "${path}" header)
    string(REGEX MATCH "#define CTR_NATIVE_BUILD_IDENTITY_KNOWN ([01])" knownMatch "${header}")
    set(${knownOutput} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    string(REGEX MATCH "#define CTR_NATIVE_BUILD_IDENTITY_HEX \\\"([0-9a-f]*)\\\"" digestMatch "${header}")
    set(${digestOutput} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

find_program(IDENTITY_TEST_GIT git REQUIRED)
set(identityTestRoot "${CMAKE_CURRENT_BINARY_DIR}/native_build_identity_cmake_test")
file(REMOVE_RECURSE "${identityTestRoot}")
file(MAKE_DIRECTORY "${identityTestRoot}/repository")
set(identityTestRepository "${identityTestRoot}/repository")
execute_process(COMMAND "${IDENTITY_TEST_GIT}" -C "${identityTestRepository}" init RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "native_build_identity_test: could not initialize fixture repository (result ${result})")
endif()
file(WRITE "${identityTestRepository}/tracked.txt" "fixture\n")
execute_process(COMMAND "${IDENTITY_TEST_GIT}" -C "${identityTestRepository}" add tracked.txt RESULT_VARIABLE result)
identity_test_require_equal("${result}" "0" "could not add fixture file")
execute_process(
    COMMAND "${IDENTITY_TEST_GIT}" -C "${identityTestRepository}" -c user.name=IdentityTest -c user.email=identity@test.invalid commit -m fixture
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET
)
identity_test_require_equal("${result}" "0" "could not commit fixture repository")

set(releaseInput "${identityTestRoot}/release-input.txt")
set(debugInput "${identityTestRoot}/debug-input.txt")
set(toolchainInput "${identityTestRoot}/toolchain-input.txt")
set(releaseHeader "${identityTestRoot}/release.h")
set(debugHeader "${identityTestRoot}/debug.h")
set(toolchainHeader "${identityTestRoot}/toolchain.h")
identity_test_write_inputs("${releaseInput}" "Release" "19.44.35207" "MultiThreaded")
identity_test_generate("${releaseInput}" "${releaseHeader}" "${identityTestRepository}")
identity_test_read_known_and_digest("${releaseHeader}" releaseKnown releaseDigest)
identity_test_require_equal("${releaseKnown}" "1" "clean release fixture must be known")
if(NOT releaseDigest MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "native_build_identity_test: clean release digest must be populated")
endif()

identity_test_write_inputs("${debugInput}" "Debug" "19.44.35207" "MultiThreadedDebug")
identity_test_generate("${debugInput}" "${debugHeader}" "${identityTestRepository}")
identity_test_read_known_and_digest("${debugHeader}" debugKnown debugDigest)
identity_test_require_equal("${debugKnown}" "1" "clean debug fixture must be known")
if(debugDigest STREQUAL releaseDigest)
    message(FATAL_ERROR "native_build_identity_test: configuration must affect the digest")
endif()

identity_test_write_inputs("${toolchainInput}" "Release" "19.45.00000" "MultiThreaded")
identity_test_generate("${toolchainInput}" "${toolchainHeader}" "${identityTestRepository}")
identity_test_read_known_and_digest("${toolchainHeader}" toolchainKnown toolchainDigest)
identity_test_require_equal("${toolchainKnown}" "1" "clean toolchain fixture must be known")
if(toolchainDigest STREQUAL releaseDigest)
    message(FATAL_ERROR "native_build_identity_test: toolchain values must affect the digest")
endif()

file(WRITE "${identityTestRepository}/untracked.txt" "must reject stale identity\n")
identity_test_generate("${releaseInput}" "${releaseHeader}" "${identityTestRepository}")
identity_test_read_known_and_digest("${releaseHeader}" dirtyKnown dirtyDigest)
identity_test_require_equal("${dirtyKnown}" "0" "untracked worktree must invalidate a previously known header")
identity_test_require_equal("${dirtyDigest}" "" "unknown build must not retain the previous digest")

execute_process(COMMAND "${IDENTITY_TEST_GIT}" -C "${identityTestRepository}" add untracked.txt RESULT_VARIABLE result)
identity_test_require_equal("${result}" "0" "could stage fixture file")
identity_test_generate("${releaseInput}" "${releaseHeader}" "${identityTestRepository}")
identity_test_read_known_and_digest("${releaseHeader}" stagedKnown stagedDigest)
identity_test_require_equal("${stagedKnown}" "0" "staged worktree must invalidate the header")
identity_test_require_equal("${stagedDigest}" "" "staged worktree must not retain a digest")
