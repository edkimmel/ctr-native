# Generates a configuration-specific identity header at build time.  This is
# deliberately a script rather than configure-time logic: a reused build tree
# must become ineligible as soon as Git worktree/index/untracked state changes.

function(native_identity_read_input key output)
    file(STRINGS "${IDENTITY_INPUT_FILE}" inputLines REGEX "^${key}=.*$")
    list(LENGTH inputLines inputCount)
    if(inputCount EQUAL 1)
        list(GET inputLines 0 inputLine)
        string(REGEX REPLACE "^${key}=" "" inputValue "${inputLine}")
        set(${output} "${inputValue}" PARENT_SCOPE)
    else()
        set(${output} "" PARENT_SCOPE)
    endif()
endfunction()

function(native_identity_is_safe_field value output)
    if("${value}" MATCHES "^[A-Za-z0-9._-]+$")
        set(${output} 1 PARENT_SCOPE)
    else()
        set(${output} 0 PARENT_SCOPE)
    endif()
endfunction()

function(native_identity_write_header known digest)
    get_filename_component(outputDirectory "${IDENTITY_OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${outputDirectory}")
    set(header "#ifndef CTR_NATIVE_GENERATED_BUILD_IDENTITY_H\n#define CTR_NATIVE_GENERATED_BUILD_IDENTITY_H\n\n")
    string(APPEND header "#define CTR_NATIVE_BUILD_IDENTITY_KNOWN ${known}\n")
    string(APPEND header "#define CTR_NATIVE_BUILD_IDENTITY_HEX \"${digest}\"\n\n")
    string(APPEND header "#endif\n")

    set(writeHeader 1)
    if(EXISTS "${IDENTITY_OUTPUT_FILE}")
        file(READ "${IDENTITY_OUTPUT_FILE}" existingHeader)
        if(existingHeader STREQUAL header)
            set(writeHeader 0)
        endif()
    endif()
    if(writeHeader)
        file(WRITE "${IDENTITY_OUTPUT_FILE}" "${header}")
    endif()
endfunction()

if(NOT DEFINED IDENTITY_REPOSITORY OR NOT DEFINED IDENTITY_INPUT_FILE OR NOT DEFINED IDENTITY_OUTPUT_FILE)
    message(FATAL_ERROR "Native build identity requires repository, input, and output paths")
endif()
if(NOT EXISTS "${IDENTITY_INPUT_FILE}")
    native_identity_write_header(0 "")
    return()
endif()
if(NOT DEFINED IDENTITY_GIT_EXECUTABLE OR IDENTITY_GIT_EXECUTABLE STREQUAL "")
    set(IDENTITY_GIT_EXECUTABLE git)
endif()

native_identity_read_input("profile" identityProfile)
native_identity_read_input("configuration" identityConfiguration)
native_identity_read_input("target" identityTarget)
native_identity_read_input("compiler-id" identityCompilerId)
native_identity_read_input("compiler-version" identityCompilerVersion)
native_identity_read_input("generator-platform" identityGeneratorPlatform)
native_identity_read_input("vs-toolset" identityVsToolset)
native_identity_read_input("msvc-toolset-version" identityMsvcToolsetVersion)
native_identity_read_input("windows-sdk" identityWindowsSdk)
native_identity_read_input("c-standard" identityCStandard)
native_identity_read_input("msvc-runtime" identityMsvcRuntime)

set(identityValid 1)
if(NOT identityProfile STREQUAL "ctr-native-arcade-v1" OR NOT identityTarget STREQUAL "win32" OR NOT identityCompilerId STREQUAL "MSVC" OR
   NOT identityCStandard STREQUAL "17" OR (NOT identityMsvcRuntime STREQUAL "MultiThreaded" AND NOT identityMsvcRuntime STREQUAL "MultiThreadedDebug"))
    set(identityValid 0)
endif()
foreach(identityField IN ITEMS identityConfiguration identityCompilerVersion identityGeneratorPlatform identityVsToolset identityMsvcToolsetVersion identityWindowsSdk)
    native_identity_is_safe_field("${${identityField}}" identityFieldSafe)
    if(NOT identityFieldSafe)
        set(identityValid 0)
    endif()
endforeach()

execute_process(
    COMMAND "${IDENTITY_GIT_EXECUTABLE}" -C "${IDENTITY_REPOSITORY}" status --porcelain=v1 --untracked-files=all
    RESULT_VARIABLE identityStatusResult
    OUTPUT_VARIABLE identityStatus
    ERROR_QUIET
)
execute_process(
    COMMAND "${IDENTITY_GIT_EXECUTABLE}" -C "${IDENTITY_REPOSITORY}" rev-parse HEAD
    RESULT_VARIABLE identityCommitResult
    OUTPUT_VARIABLE identityCommit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND "${IDENTITY_GIT_EXECUTABLE}" -C "${IDENTITY_REPOSITORY}" rev-parse HEAD^{tree}
    RESULT_VARIABLE identityTreeResult
    OUTPUT_VARIABLE identityTree
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(LENGTH "${identityCommit}" identityCommitLength)
string(LENGTH "${identityTree}" identityTreeLength)
if(NOT identityStatusResult EQUAL 0 OR NOT identityStatus STREQUAL "" OR NOT identityCommitResult EQUAL 0 OR NOT identityTreeResult EQUAL 0 OR
   NOT identityCommitLength EQUAL 40 OR NOT identityTreeLength EQUAL 40 OR NOT identityCommit MATCHES "^[0-9a-f]+$" OR NOT identityTree MATCHES "^[0-9a-f]+$")
    set(identityValid 0)
endif()

if(identityValid)
    string(CONCAT identityManifest
        "ctr-native-build-identity-v2\n"
        "git-commit=${identityCommit}\n"
        "git-tree=${identityTree}\n"
        "profile=${identityProfile}\n"
        "configuration=${identityConfiguration}\n"
        "target=${identityTarget}\n"
        "compiler-id=${identityCompilerId}\n"
        "compiler-version=${identityCompilerVersion}\n"
        "generator-platform=${identityGeneratorPlatform}\n"
        "vs-toolset=${identityVsToolset}\n"
        "msvc-toolset-version=${identityMsvcToolsetVersion}\n"
        "windows-sdk=${identityWindowsSdk}\n"
        "c-standard=${identityCStandard}\n"
        "msvc-runtime=${identityMsvcRuntime}\n"
    )
    string(SHA256 identityDigest "${identityManifest}")
    native_identity_write_header(1 "${identityDigest}")
else()
    native_identity_write_header(0 "")
endif()
