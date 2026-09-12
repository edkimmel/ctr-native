cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

set(CTR_NATIVE_VERSION "0.1.0-beta.7.1")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CTR Native currently requires a 32-bit target. "
        "Select Win32 for MSVC, an i686 MinGW compiler on Windows, or pass -m32 "
        "in the initial compiler and linker flags on Linux."
    )
endif()

set(CTR_NATIVE_MSVC_FRONTEND OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(CTR_NATIVE_MSVC_FRONTEND ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty")
endif()

# Build identity is generated at build time, not configure time.  This prevents
# a reused build directory from retaining a clean identity after source changes.
# The generator validates the fixed arcade profile and CMake's actual values.
set(CTR_NATIVE_IDENTITY_PROFILE "ctr-native-arcade-v1")
find_program(CTR_NATIVE_IDENTITY_GIT_EXECUTABLE git)
if(NOT CTR_NATIVE_IDENTITY_GIT_EXECUTABLE)
    set(CTR_NATIVE_IDENTITY_GIT_EXECUTABLE git)
endif()

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# Explicit, native-layout-independent binary primitives for canonical state and replay v2.
add_library(ctr_native_canonical_codec STATIC platform/native_canonical_codec.c)
set_target_properties(ctr_native_canonical_codec PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_codec PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(ctr_native_identity STATIC
    platform/native_sha256.c
    platform/native_disc_image.c
    platform/native_identity.c
)
set_target_properties(ctr_native_identity PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_identity PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native_identity PRIVATE CTR_NATIVE)

add_library(ctr_native_canonical_state STATIC platform/native_canonical_state.c)
set_target_properties(ctr_native_canonical_state PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_state PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_state PUBLIC ctr_native_canonical_codec)

# The game-owned source is also unity-included by the executable.  This small
# standalone target makes its explicit value projection directly testable.
add_library(ctr_native_canonical_projector STATIC game/MAIN/MainCanonicalState.c)
set_target_properties(ctr_native_canonical_projector PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_projector PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native_canonical_projector PUBLIC CTR_NATIVE)
target_link_libraries(ctr_native_canonical_projector PUBLIC ctr_native_canonical_state)

add_library(ctr_native_replay_scheduler_seam STATIC platform/native_replay_scheduler_seam.c)
set_target_properties(ctr_native_replay_scheduler_seam PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_scheduler_seam PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_scheduler_seam PUBLIC ctr_native_canonical_state)

add_library(ctr_native_replay_v2 STATIC platform/native_replay_v2.c)
set_target_properties(ctr_native_replay_v2 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_v2 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v2 PUBLIC ctr_native_canonical_state)

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)
set_target_properties(ctr_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
if(CTR_NATIVE_MSVC_FRONTEND)
    target_compile_options(ctr_native PRIVATE
        /W4 #TODO(aalhendi): fix warnings
        /bigobj
        /arch:SSE2
        /wd4152
        /wd4244
        /wd4245
        /wd4310
        /wd5287
    )
    target_link_options(ctr_native PRIVATE /Brepro)
else()
    target_compile_options(ctr_native PRIVATE
        -msse
        -Wenum-conversion
        -Wenum-compare
        -Wswitch-enum
        -Wformat
        -Wparentheses
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-g>
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
        ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
    endif()
endif()
target_link_libraries(ctr_native SDL3::SDL3 ctr_native_canonical_projector ctr_native_replay_scheduler_seam)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(ctr_native PRIVATE -static-libgcc)
endif()
if(MINGW)
    target_link_options(ctr_native PRIVATE -static)
endif()

set(CTR_NATIVE_IDENTITY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CTR_NATIVE_IDENTITY_INPUT_FILE "${CTR_NATIVE_IDENTITY_GENERATED_DIR}/$<CONFIG>/native_build_identity_inputs.txt")
set(CTR_NATIVE_IDENTITY_HEADER_FILE "${CTR_NATIVE_IDENTITY_GENERATED_DIR}/$<CONFIG>/native_build_identity.h")
string(CONCAT CTR_NATIVE_IDENTITY_INPUT_CONTENT
    "profile=${CTR_NATIVE_IDENTITY_PROFILE}\n"
    "configuration=$<CONFIG>\n"
    "target=win32\n"
    "compiler-id=${CMAKE_C_COMPILER_ID}\n"
    "compiler-version=${CMAKE_C_COMPILER_VERSION}\n"
    "generator-platform=${CMAKE_GENERATOR_PLATFORM}\n"
    "vs-toolset=${CMAKE_VS_PLATFORM_TOOLSET}\n"
    "msvc-toolset-version=${MSVC_TOOLSET_VERSION}\n"
    "windows-sdk=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\n"
    "c-standard=$<TARGET_PROPERTY:ctr_native,C_STANDARD>\n"
    "msvc-runtime=$<TARGET_GENEX_EVAL:ctr_native,$<TARGET_PROPERTY:ctr_native,MSVC_RUNTIME_LIBRARY>>\n"
)
file(GENERATE
    OUTPUT "${CTR_NATIVE_IDENTITY_INPUT_FILE}"
    CONTENT "${CTR_NATIVE_IDENTITY_INPUT_CONTENT}"
)
add_custom_target(ctr_native_build_identity ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DIDENTITY_REPOSITORY=${CMAKE_SOURCE_DIR}"
        "-DIDENTITY_INPUT_FILE=${CTR_NATIVE_IDENTITY_INPUT_FILE}"
        "-DIDENTITY_OUTPUT_FILE=${CTR_NATIVE_IDENTITY_HEADER_FILE}"
        "-DIDENTITY_GIT_EXECUTABLE=${CTR_NATIVE_IDENTITY_GIT_EXECUTABLE}"
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateNativeBuildIdentity.cmake"
    COMMENT "Generating deterministic native build identity"
    VERBATIM
)
target_include_directories(ctr_native_identity PRIVATE "${CTR_NATIVE_IDENTITY_GENERATED_DIR}/$<CONFIG>")
target_include_directories(ctr_native PRIVATE "${CTR_NATIVE_IDENTITY_GENERATED_DIR}/$<CONFIG>")
add_dependencies(ctr_native_identity ctr_native_build_identity)
add_dependencies(ctr_native ctr_native_build_identity)

include(CTest)
if(BUILD_TESTING)

    add_executable(native_sha256_test tests/native_sha256_test.c)
    set_target_properties(native_sha256_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_sha256_test PRIVATE ctr_native_identity)
    add_test(NAME native_sha256_unit COMMAND $<TARGET_FILE:native_sha256_test>)

    add_executable(native_canonical_codec_test tests/native_canonical_codec_test.c)
    set_target_properties(native_canonical_codec_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_codec_test PRIVATE ctr_native_canonical_codec)
    add_test(NAME native_canonical_codec_unit COMMAND $<TARGET_FILE:native_canonical_codec_test>)

    add_executable(native_canonical_state_test tests/native_canonical_state_test.c)
    set_target_properties(native_canonical_state_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_state_test PRIVATE ctr_native_canonical_state)
    add_test(NAME native_canonical_state_unit COMMAND $<TARGET_FILE:native_canonical_state_test>)

    add_executable(native_canonical_projector_test tests/native_canonical_projector_test.c)
    set_target_properties(native_canonical_projector_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_projector_test PRIVATE ctr_native_canonical_projector)
    add_test(NAME native_canonical_projector_unit COMMAND $<TARGET_FILE:native_canonical_projector_test>)

    add_executable(native_replay_scheduler_seam_test tests/native_replay_scheduler_seam_test.c)
    set_target_properties(native_replay_scheduler_seam_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_scheduler_seam_test PRIVATE ctr_native_replay_scheduler_seam)
    add_test(NAME native_replay_scheduler_seam_unit COMMAND $<TARGET_FILE:native_replay_scheduler_seam_test>)

    add_executable(native_replay_v2_test tests/native_replay_v2_test.c)
    set_target_properties(native_replay_v2_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_v2_test PRIVATE ctr_native_replay_v2)
    add_test(NAME native_replay_v2_unit COMMAND $<TARGET_FILE:native_replay_v2_test>)

    add_executable(native_identity_test
        tests/native_identity_test.c
        platform/native_sha256.c
        platform/native_disc_image.c
        platform/native_identity.c
    )
    set_target_properties(native_identity_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(native_identity_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(native_identity_test PRIVATE
        CTR_NATIVE
        CTR_NATIVE_BUILD_IDENTITY_KNOWN=1
        CTR_NATIVE_BUILD_IDENTITY_HEX=\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"
    )
    target_link_libraries(native_identity_test PRIVATE ctr_native_canonical_state)
    add_test(NAME native_identity_unit COMMAND $<TARGET_FILE:native_identity_test>)

    add_test(NAME native_build_identity_cmake COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_build_identity_test.cmake")

    add_test(NAME ctr_native_version COMMAND $<TARGET_FILE:ctr_native> --version)
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^CTR Native ")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME ctr_match_unit
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/matching
            -p test_*.py
    )
endif()
