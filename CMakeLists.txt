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

add_library(ctr_native_sha256 STATIC platform/native_sha256.c)
set_target_properties(ctr_native_sha256 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_sha256 PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(ctr_native_identity STATIC
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
target_link_libraries(ctr_native_identity PRIVATE ctr_native_sha256)

# Pure, dormant M3 match identity. This target has no game-state dependency or
# live callsite; it freezes the two-cab slot ownership contract on the wire.
add_library(ctr_native_match_config STATIC platform/native_match_config.c)
set_target_properties(ctr_native_match_config PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_match_config PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_match_config PUBLIC ctr_native_canonical_codec ctr_native_sha256)

# Pure, dormant M3 simulation RNG bank. No retail RNG or live game callsite
# consumes this target; it is intended for a later config-bound schema.
add_library(ctr_native_deterministic_rng STATIC platform/native_deterministic_rng.c)
set_target_properties(ctr_native_deterministic_rng PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_deterministic_rng PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_deterministic_rng PUBLIC ctr_native_canonical_codec ctr_native_sha256)

# Pure CAB1-measured Logitech G29 device matching and input conversion.  SDL
# ownership remains in native_input.c; this target keeps the hardware mapping
# deterministic and unit-testable without a connected wheel.
add_library(ctr_native_g29_input STATIC platform/native_g29_input.c)
set_target_properties(ctr_native_g29_input PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_g29_input PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Dormant M3 stable-slot planner.  It consumes only portable config and the
# audited pointer-free roster prelude input; no live game path calls it.
add_library(ctr_native_arcade_roster STATIC game/MAIN/MainArcadeRoster.c)
set_target_properties(ctr_native_arcade_roster PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_roster PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_roster PUBLIC ctr_native_match_config ctr_native_canonical_drivers_roster)

# Dormant M3 transactional bot-setup planner. It consumes pointer-free source
# facts and stages deterministic RNG without reading or mutating game globals.
add_library(ctr_native_arcade_bot_setup STATIC game/MAIN/MainArcadeBotSetup.c)
set_target_properties(ctr_native_arcade_bot_setup PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_bot_setup PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_bot_setup PUBLIC ctr_native_arcade_roster ctr_native_deterministic_rng)

add_library(ctr_native_canonical_state STATIC platform/native_canonical_state.c)
set_target_properties(ctr_native_canonical_state PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_state PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_state PUBLIC ctr_native_canonical_codec)

add_library(ctr_native_canonical_drivers STATIC platform/native_canonical_drivers.c)
set_target_properties(ctr_native_canonical_drivers PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_drivers PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_drivers PUBLIC ctr_native_canonical_codec)

# Standalone pointer-free M2 topology summary.  It has no game ownership,
# projector, scheduler, replay, or state-schema dependency.
add_library(ctr_native_canonical_topology_v1 STATIC platform/native_canonical_topology.c)
set_target_properties(ctr_native_canonical_topology_v1 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_topology_v1 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_topology_v1 PUBLIC ctr_native_canonical_codec)

# Standalone, fixture-only M2 WORLD mine registry.  It is intentionally not
# linked into any canonical-state, replay, scheduler, or game-runtime target.
add_library(ctr_native_canonical_world_mine_registry_v1 STATIC platform/native_canonical_world_mine_registry.c)
set_target_properties(ctr_native_canonical_world_mine_registry_v1 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_world_mine_registry_v1 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_world_mine_registry_v1 PUBLIC ctr_native_canonical_codec)

# Standalone, source-only M2 WORLD counter facts.  No game source adapter or
# canonical-state/replay/runtime link consumes this dormant foundation.
add_library(ctr_native_canonical_world_counters_v1 STATIC platform/native_canonical_world_counters.c)
set_target_properties(ctr_native_canonical_world_counters_v1 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_world_counters_v1 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_world_counters_v1 PUBLIC ctr_native_canonical_codec)

# Dormant game-owned M2 WORLD counter extraction.  This deliberately remains a
# separate static target: no unity inclusion or runtime/state/replay consumer.
add_library(ctr_native_main_canonical_world_counters STATIC game/MAIN/MainCanonicalWorldCounters.c)
set_target_properties(ctr_native_main_canonical_world_counters PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_main_canonical_world_counters PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(ctr_native_main_canonical_world_counters PRIVATE CTR_NATIVE)
target_link_libraries(ctr_native_main_canonical_world_counters PUBLIC ctr_native_canonical_world_counters_v1)

# Dormant mine-pool adapter: deliberately isolated from unity and all runtime,
# replay, canonical-state, and codec-consumer targets.
add_library(ctr_native_main_canonical_world_mine_registry STATIC game/MAIN/MainCanonicalWorldMineRegistry.c)
set_target_properties(ctr_native_main_canonical_world_mine_registry PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_main_canonical_world_mine_registry PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(ctr_native_main_canonical_world_mine_registry PRIVATE CTR_NATIVE)
target_link_libraries(ctr_native_main_canonical_world_mine_registry PUBLIC ctr_native_canonical_world_mine_registry_v1)

# Fixture-only fact adapter: it intentionally has no live topology/runtime
# dependency and can be exercised against the portable normative codec alone.
add_library(ctr_native_canonical_topology_facts STATIC game/MAIN/MainCanonicalTopologyFacts.c)
set_target_properties(ctr_native_canonical_topology_facts PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_topology_facts PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_canonical_topology_facts PUBLIC ctr_native_canonical_topology_v1)

add_library(ctr_native_canonical_driver_behavior STATIC platform/native_canonical_driver_behavior.c)
set_target_properties(ctr_native_canonical_driver_behavior PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_driver_behavior PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Standalone JitPool geometry/list ownership validation. The dormant Driver
# projector consumes it only for pre-dereference root ownership gates.
add_library(ctr_native_canonical_pool STATIC platform/native_canonical_pool.c)
set_target_properties(ctr_native_canonical_pool PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_pool PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Portable fixed-array address-to-index identity validation for a later
# topology projector. It has no game-state or pointer dereference dependency.
add_library(ctr_native_canonical_fixed_array STATIC platform/native_canonical_fixed_array.c)
set_target_properties(ctr_native_canonical_fixed_array PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_fixed_array PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Dormant game-owned topology lifetime/ownership snapshot. It normalizes only
# already-validated QuadBlock identities and has no Physics or publish seam.
add_library(ctr_native_canonical_topology STATIC game/MAIN/MainCanonicalTopology.c)
set_target_properties(ctr_native_canonical_topology PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_topology PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native_canonical_topology PUBLIC CTR_NATIVE)
target_link_libraries(ctr_native_canonical_topology PUBLIC ctr_native_canonical_fixed_array)

add_library(ctr_native_canonical_drivers_detailed STATIC platform/native_canonical_drivers_detailed.c)
set_target_properties(ctr_native_canonical_drivers_detailed PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_drivers_detailed PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_drivers_detailed PUBLIC ctr_native_canonical_drivers ctr_native_canonical_driver_behavior)

add_library(ctr_native_canonical_drivers_roster STATIC platform/native_canonical_drivers_roster.c)
set_target_properties(ctr_native_canonical_drivers_roster PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_drivers_roster PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_drivers_roster PUBLIC ctr_native_canonical_drivers_detailed)

add_library(ctr_native_canonical_state_v3 STATIC platform/native_canonical_state_v3.c)
set_target_properties(ctr_native_canonical_state_v3 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_canonical_state_v3 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_state_v3 PUBLIC ctr_native_canonical_drivers ctr_native_canonical_state)

# Pure NCV4 value codec.  It deliberately has no projector, runtime, replay,
# scheduler, file, or unity consumer in this slice.
add_library(ctr_native_canonical_state_v4 STATIC platform/native_canonical_state_v4.c)
set_target_properties(ctr_native_canonical_state_v4 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_state_v4 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_state_v4 PUBLIC
    ctr_native_canonical_state ctr_native_canonical_drivers ctr_native_deterministic_rng
    ctr_native_canonical_world_counters_v1 ctr_native_canonical_world_mine_registry_v1
    ctr_native_canonical_topology_v1)

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
target_link_libraries(ctr_native_canonical_projector PUBLIC ctr_native_canonical_state ctr_native_canonical_state_v3)

# Dormant, static game-owned V3 coordinator.  It is compiled for link
# coverage but no live game path calls it in this slice.
add_library(ctr_native_canonical_runtime STATIC game/MAIN/MainCanonicalRuntime.c)
set_target_properties(ctr_native_canonical_runtime PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_runtime PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(ctr_native_canonical_runtime PUBLIC CTR_NATIVE CTR_INTERNAL)
target_link_libraries(ctr_native_canonical_runtime PUBLIC ctr_native_canonical_projector ctr_native_canonical_drivers_roster ctr_native_canonical_topology ctr_native_replay_scheduler_seam)

add_library(ctr_native_replay_scheduler_seam STATIC platform/native_replay_scheduler_seam.c)
set_target_properties(ctr_native_replay_scheduler_seam PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_scheduler_seam PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_scheduler_seam PUBLIC ctr_native_canonical_state ctr_native_canonical_state_v3)

add_library(ctr_native_replay_v2 STATIC platform/native_replay_v2.c)
set_target_properties(ctr_native_replay_v2 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_v2 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v2 PUBLIC ctr_native_canonical_state)

add_library(ctr_native_replay_v3 STATIC platform/native_replay_v3.c)
set_target_properties(ctr_native_replay_v3 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_v3 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v3 PUBLIC ctr_native_canonical_state_v3)

add_library(ctr_native_replay_v2_file STATIC platform/native_replay_v2_file.c)
set_target_properties(ctr_native_replay_v2_file PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_v2_file PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v2_file PUBLIC ctr_native_replay_v2)

add_library(ctr_native_replay_v3_file STATIC platform/native_replay_v3_file.c)
set_target_properties(ctr_native_replay_v3_file PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_v3_file PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v3_file PUBLIC ctr_native_replay_v3)

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
target_link_libraries(ctr_native SDL3::SDL3 ctr_native_g29_input ctr_native_canonical_projector ctr_native_canonical_runtime ctr_native_replay_scheduler_seam ctr_native_replay_v2_file ctr_native_replay_v3_file ctr_native_canonical_drivers_roster ctr_native_canonical_pool ctr_native_canonical_fixed_array ctr_native_canonical_topology)
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
    target_link_libraries(native_sha256_test PRIVATE ctr_native_sha256)
    add_test(NAME native_sha256_unit COMMAND $<TARGET_FILE:native_sha256_test>)

    add_executable(native_canonical_codec_test tests/native_canonical_codec_test.c)
    set_target_properties(native_canonical_codec_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_codec_test PRIVATE ctr_native_canonical_codec)
    add_test(NAME native_canonical_codec_unit COMMAND $<TARGET_FILE:native_canonical_codec_test>)

    add_executable(native_match_config_test tests/native_match_config_test.c)
    set_target_properties(native_match_config_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_match_config_test PRIVATE ctr_native_match_config)
    add_test(NAME native_match_config_unit COMMAND $<TARGET_FILE:native_match_config_test>)

    add_executable(native_deterministic_rng_test tests/native_deterministic_rng_test.c)
    set_target_properties(native_deterministic_rng_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_deterministic_rng_test PRIVATE ctr_native_deterministic_rng)
    add_test(NAME native_deterministic_rng_unit COMMAND $<TARGET_FILE:native_deterministic_rng_test>)

    add_executable(native_g29_input_test tests/native_g29_input_test.c)
    set_target_properties(native_g29_input_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_g29_input_test PRIVATE ctr_native_g29_input)
    add_test(NAME native_g29_input_unit COMMAND $<TARGET_FILE:native_g29_input_test>)

    add_executable(main_arcade_roster_test tests/main_arcade_roster_test.c)
    set_target_properties(main_arcade_roster_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_roster_test PRIVATE ctr_native_arcade_roster)
    add_test(NAME main_arcade_roster_unit COMMAND $<TARGET_FILE:main_arcade_roster_test>)

    add_executable(main_arcade_bot_setup_test tests/main_arcade_bot_setup_test.c)
    set_target_properties(main_arcade_bot_setup_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_bot_setup_test PRIVATE ctr_native_arcade_bot_setup)
    add_test(NAME main_arcade_bot_setup_unit COMMAND $<TARGET_FILE:main_arcade_bot_setup_test>)

    add_executable(native_canonical_state_test tests/native_canonical_state_test.c)
    set_target_properties(native_canonical_state_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_state_test PRIVATE ctr_native_canonical_state)
    add_test(NAME native_canonical_state_unit COMMAND $<TARGET_FILE:native_canonical_state_test>)

    add_executable(native_canonical_drivers_test tests/native_canonical_drivers_test.c)
    set_target_properties(native_canonical_drivers_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_drivers_test PRIVATE ctr_native_canonical_drivers)
    add_test(NAME native_canonical_drivers_unit COMMAND $<TARGET_FILE:native_canonical_drivers_test>)

    add_executable(native_canonical_topology_v1_test tests/native_canonical_topology_v1_test.c)
    set_target_properties(native_canonical_topology_v1_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_canonical_topology_v1_test PRIVATE ctr_native_canonical_topology_v1)
    add_test(NAME native_canonical_topology_v1_unit COMMAND $<TARGET_FILE:native_canonical_topology_v1_test>)

    add_executable(native_canonical_world_mine_registry_v1_test tests/native_canonical_world_mine_registry_v1_test.c)
    set_target_properties(native_canonical_world_mine_registry_v1_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_canonical_world_mine_registry_v1_test PRIVATE ctr_native_canonical_world_mine_registry_v1)
    add_test(NAME native_canonical_world_mine_registry_v1_unit COMMAND $<TARGET_FILE:native_canonical_world_mine_registry_v1_test>)

    add_executable(native_canonical_world_counters_v1_test tests/native_canonical_world_counters_v1_test.c)
    set_target_properties(native_canonical_world_counters_v1_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_canonical_world_counters_v1_test PRIVATE ctr_native_canonical_world_counters_v1)
    add_test(NAME native_canonical_world_counters_v1_unit COMMAND $<TARGET_FILE:native_canonical_world_counters_v1_test>)
    add_test(NAME native_canonical_world_counters_v1_source_contract
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_canonical_world_counters_v1_source_contract_test.cmake")

    add_executable(main_canonical_world_counters_test tests/main_canonical_world_counters_test.c)
    set_target_properties(main_canonical_world_counters_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_world_counters_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_world_counters_test PRIVATE CTR_NATIVE)
    target_link_libraries(main_canonical_world_counters_test PRIVATE ctr_native_main_canonical_world_counters)
    add_test(NAME main_canonical_world_counters_unit COMMAND $<TARGET_FILE:main_canonical_world_counters_test>)

    add_executable(main_canonical_world_mine_registry_test tests/main_canonical_world_mine_registry_test.c)
    set_target_properties(main_canonical_world_mine_registry_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_world_mine_registry_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_world_mine_registry_test PRIVATE CTR_NATIVE)
    target_link_libraries(main_canonical_world_mine_registry_test PRIVATE ctr_native_main_canonical_world_mine_registry)
    add_test(NAME main_canonical_world_mine_registry_unit COMMAND $<TARGET_FILE:main_canonical_world_mine_registry_test>)

    add_executable(main_canonical_topology_facts_test tests/main_canonical_topology_facts_test.c)
    set_target_properties(main_canonical_topology_facts_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(main_canonical_topology_facts_test PRIVATE ctr_native_canonical_topology_facts)
    add_test(NAME main_canonical_topology_facts_unit COMMAND $<TARGET_FILE:main_canonical_topology_facts_test>)

    add_executable(native_canonical_drivers_detailed_test tests/native_canonical_drivers_detailed_test.c)
    set_target_properties(native_canonical_drivers_detailed_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_drivers_detailed_test PRIVATE ctr_native_canonical_drivers_detailed)
    add_test(NAME native_canonical_drivers_detailed_unit COMMAND $<TARGET_FILE:native_canonical_drivers_detailed_test>)

    add_executable(native_canonical_drivers_roster_test tests/native_canonical_drivers_roster_test.c)
    set_target_properties(native_canonical_drivers_roster_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_canonical_drivers_roster_test PRIVATE ctr_native_canonical_drivers_roster)
    add_test(NAME native_canonical_drivers_roster_unit COMMAND $<TARGET_FILE:native_canonical_drivers_roster_test>)

    add_executable(main_canonical_drivers_binding_test tests/main_canonical_drivers_binding_test.c)
    set_target_properties(main_canonical_drivers_binding_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_drivers_binding_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_drivers_binding_test PRIVATE CTR_NATIVE CTR_INTERNAL)
    target_link_libraries(main_canonical_drivers_binding_test PRIVATE ctr_native_canonical_drivers_roster ctr_native_canonical_pool ctr_native_canonical_fixed_array ctr_native_canonical_state_v3 ctr_native_replay_scheduler_seam)
    add_test(NAME main_canonical_drivers_binding_unit COMMAND $<TARGET_FILE:main_canonical_drivers_binding_test>)

    add_executable(native_canonical_driver_behavior_test tests/native_canonical_driver_behavior_test.c)
    set_target_properties(native_canonical_driver_behavior_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_driver_behavior_test PRIVATE ctr_native_canonical_driver_behavior)
    add_test(NAME native_canonical_driver_behavior_unit COMMAND $<TARGET_FILE:native_canonical_driver_behavior_test>)

    add_executable(native_canonical_pool_test tests/native_canonical_pool_test.c)
    set_target_properties(native_canonical_pool_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_pool_test PRIVATE ctr_native_canonical_pool)
    add_test(NAME native_canonical_pool_unit COMMAND $<TARGET_FILE:native_canonical_pool_test>)

    add_executable(native_canonical_fixed_array_test tests/native_canonical_fixed_array_test.c)
    set_target_properties(native_canonical_fixed_array_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_fixed_array_test PRIVATE ctr_native_canonical_fixed_array)
    add_test(NAME native_canonical_fixed_array_unit COMMAND $<TARGET_FILE:native_canonical_fixed_array_test>)

    add_executable(main_canonical_topology_test tests/main_canonical_topology_test.c)
    set_target_properties(main_canonical_topology_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_topology_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_topology_test PRIVATE CTR_NATIVE)
    target_link_libraries(main_canonical_topology_test PRIVATE ctr_native_canonical_fixed_array)
    add_test(NAME main_canonical_topology_unit COMMAND $<TARGET_FILE:main_canonical_topology_test>)

    add_executable(native_canonical_state_v3_test tests/native_canonical_state_v3_test.c)
    set_target_properties(native_canonical_state_v3_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_state_v3_test PRIVATE ctr_native_canonical_state_v3)
    add_test(NAME native_canonical_state_v3_unit COMMAND $<TARGET_FILE:native_canonical_state_v3_test>)

    add_executable(native_canonical_state_v4_test tests/native_canonical_state_v4_test.c)
    set_target_properties(native_canonical_state_v4_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_canonical_state_v4_test PRIVATE ctr_native_canonical_state_v4)
    add_test(NAME native_canonical_state_v4_unit COMMAND $<TARGET_FILE:native_canonical_state_v4_test>)

    add_executable(native_canonical_projector_test tests/native_canonical_projector_test.c)
    set_target_properties(native_canonical_projector_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_canonical_projector_test PRIVATE ctr_native_canonical_projector ctr_native_replay_v3)
    add_test(NAME native_canonical_projector_unit COMMAND $<TARGET_FILE:native_canonical_projector_test>)

    add_executable(native_replay_scheduler_seam_test tests/native_replay_scheduler_seam_test.c)
    set_target_properties(native_replay_scheduler_seam_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_scheduler_seam_test PRIVATE ctr_native_replay_scheduler_seam)
    add_test(NAME native_replay_scheduler_seam_unit COMMAND $<TARGET_FILE:native_replay_scheduler_seam_test>)

    add_executable(native_replay_scheduler_v3_lifecycle_test tests/native_replay_scheduler_v3_lifecycle_test.c)
    set_target_properties(native_replay_scheduler_v3_lifecycle_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_scheduler_v3_lifecycle_test PRIVATE ctr_native_replay_scheduler_seam ctr_native_replay_v3_file)
    add_test(NAME native_replay_scheduler_v3_lifecycle_unit COMMAND $<TARGET_FILE:native_replay_scheduler_v3_lifecycle_test>)

    add_executable(native_replay_scheduler_integration_test tests/native_replay_scheduler_integration_test.c)
    set_target_properties(native_replay_scheduler_integration_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(native_replay_scheduler_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(native_replay_scheduler_integration_test PRIVATE
        CTR_NATIVE
        CTR_INTERNAL
        CTR_NATIVE_VERSION="${CTR_NATIVE_VERSION}"
        CTR_NATIVE_BUILD_ID="${CTR_NATIVE_BUILD_ID}"
    )
    target_link_libraries(native_replay_scheduler_integration_test PRIVATE ctr_native_replay_scheduler_seam ctr_native_replay_v2_file ctr_native_replay_v3_file)
    add_test(NAME native_replay_scheduler_integration_unit COMMAND $<TARGET_FILE:native_replay_scheduler_integration_test>)

    add_executable(native_replay_v2_test tests/native_replay_v2_test.c)
    set_target_properties(native_replay_v2_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_v2_test PRIVATE ctr_native_replay_v2)
    add_test(NAME native_replay_v2_unit COMMAND $<TARGET_FILE:native_replay_v2_test>)

    add_executable(native_replay_v3_test tests/native_replay_v3_test.c)
    set_target_properties(native_replay_v3_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_v3_test PRIVATE ctr_native_replay_v3)
    add_test(NAME native_replay_v3_unit COMMAND $<TARGET_FILE:native_replay_v3_test>)

    add_executable(native_replay_v2_file_test tests/native_replay_v2_file_test.c)
    set_target_properties(native_replay_v2_file_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_v2_file_test PRIVATE ctr_native_replay_v2_file)
    add_test(NAME native_replay_v2_file_unit COMMAND $<TARGET_FILE:native_replay_v2_file_test>)

    add_executable(native_replay_v3_file_test tests/native_replay_v3_file_test.c)
    set_target_properties(native_replay_v3_file_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_replay_v3_file_test PRIVATE ctr_native_replay_v3_file)
    add_test(NAME native_replay_v3_file_unit COMMAND $<TARGET_FILE:native_replay_v3_file_test>)

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

    if(CTR_NATIVE_MSVC_FRONTEND)
        # Produce Release x86 code listings for every translation unit reachable
        # from the dormant runtime probe.  The CTest parser proves the complete
        # direct-call path fits the declared stack budget; no game target uses
        # these listing flags.
        set(CTR_NATIVE_RUNTIME_STACK_LISTING_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime-stack-listings")
        file(MAKE_DIRECTORY "${CTR_NATIVE_RUNTIME_STACK_LISTING_DIR}")
        foreach(stack_target IN ITEMS
            main_canonical_drivers_binding_test
            ctr_native_canonical_codec
            ctr_native_canonical_state
            ctr_native_canonical_state_v3
            ctr_native_canonical_drivers
            ctr_native_canonical_drivers_detailed
            ctr_native_canonical_drivers_roster
            ctr_native_canonical_driver_behavior
            ctr_native_canonical_pool
            ctr_native_canonical_fixed_array
        )
            target_compile_options(${stack_target} PRIVATE
                "$<$<CONFIG:Release>:/FAcs>"
                "$<$<CONFIG:Release>:/Fa${CTR_NATIVE_RUNTIME_STACK_LISTING_DIR}/>"
            )
        endforeach()
        add_test(NAME main_canonical_runtime_stack_budget
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/tests/main_canonical_runtime_stack_budget.ps1"
                -AsmDirectory "${CTR_NATIVE_RUNTIME_STACK_LISTING_DIR}"
                -BudgetBytes 1536
        )
        set_tests_properties(main_canonical_runtime_stack_budget PROPERTIES CONFIGURATIONS Release)
    endif()

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
