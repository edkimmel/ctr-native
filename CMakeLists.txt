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

# Local-only presentation options. This target has no match, replay, canonical
# state, game, or network dependency by design.
add_library(ctr_native_display_config STATIC platform/native_display_config.c)
set_target_properties(ctr_native_display_config PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_display_config PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Host-local unattended frame-capture requests. Presentation-only: this target
# has no match, replay, canonical state, game, renderer, GL, or SDL dependency
# by design, so the parser and exit decision stay unit-testable standalone.
add_library(ctr_native_frame_capture STATIC platform/native_frame_capture.c)
set_target_properties(ctr_native_frame_capture PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_frame_capture PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Opaque virtual delivery for later lockstep fault matrices.  This leaf has no
# packet format, clock, socket, game, replay, or executable dependency.
add_library(ctr_native_virtual_datagram STATIC platform/native_virtual_datagram.c)
set_target_properties(ctr_native_virtual_datagram PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_virtual_datagram PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Real-socket, leaf byte-mover over a Winsock2 UDP socket. Caller-owned
# buffers, no dynamic allocation, no packet/protocol format of its own, and
# no lockstep, match-config, canonical-state, or topology-lease dependency;
# it is the real-socket analog of the test-only native_virtual_datagram
# harness above and is never linked into it or vice versa.
add_library(ctr_native_udp_transport STATIC platform/native_udp_transport.c)
set_target_properties(ctr_native_udp_transport PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_udp_transport PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_udp_transport PUBLIC ws2_32)

# Fixed-delay lockstep frame-bundle wire format.  Transport-agnostic by
# design: this leaf encodes to and decodes from caller-owned byte buffers and
# has no socket, OS networking, SDL, clock, game, or virtual-datagram
# dependency.  The virtual datagram harness is a test transport only.
add_library(ctr_native_lockstep_protocol STATIC platform/native_lockstep_protocol.c)
set_target_properties(ctr_native_lockstep_protocol PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_protocol PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_protocol PUBLIC ctr_native_canonical_codec)

# Fixed-capacity lockstep delay / reorder window, one ring per remote peer.
# Transport-agnostic by design: this leaf stores caller-owned bytes and decoded
# bundles and has no wire format, socket, OS networking, SDL, clock, game, or
# virtual-datagram dependency.  It consumes the frame-bundle codec only.
add_library(ctr_native_lockstep_input_window STATIC platform/native_lockstep_input_window.c)
set_target_properties(ctr_native_lockstep_input_window PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_input_window PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_input_window PUBLIC ctr_native_lockstep_protocol)

# Fixed-delay lockstep session and first-divergence report.  Transport-agnostic
# by design: this leaf composes into and accepts from caller-owned byte buffers
# and has no socket, OS networking, SDL, clock, game, or virtual-datagram
# dependency.  It consumes match config and canonical state read-only, for the
# durable match identity and the per-frame verification digests, and never
# writes back to either wire format.
add_library(ctr_native_lockstep_session STATIC platform/native_lockstep_session.c)
set_target_properties(ctr_native_lockstep_session PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_session PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_session PUBLIC
    ctr_native_lockstep_protocol ctr_native_lockstep_input_window
    ctr_native_canonical_state_v4 ctr_native_match_config)

# Stall-timeout policy on top of the lockstep session: turns an indefinitely
# retried stall into a terminated match outcome after a bounded number of
# consecutive stalled frames, and mirrors the session's own
# DIVERGED-outranks-FAULTED priority.  Reads the session only through its
# public const-or-NULL accessors and never mutates it.
add_library(ctr_native_lockstep_match_outcome STATIC platform/native_lockstep_match_outcome.c)
set_target_properties(ctr_native_lockstep_match_outcome PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_match_outcome PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_match_outcome PUBLIC ctr_native_lockstep_session)

# Caller-owned per-slot lifecycle tracking for peer-drop policy, entirely
# separate from struct NativeMatchConfigV1.  It never touches canonical
# state, replay, or the topology lease, and never mutates the match config.
add_library(ctr_native_lockstep_match_roster STATIC platform/native_lockstep_match_roster.c)
set_target_properties(ctr_native_lockstep_match_roster PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_match_roster PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_match_roster PUBLIC ctr_native_match_config ctr_native_lockstep_match_outcome)

# Pure rematch config builder: a fresh NativeMatchConfigV1 for the same
# fixture (track, mode, rules, build/content identity, bot rules, per-slot
# character/difficulty) as a previous match, with a new masterSeed. No
# session, replay, roster, outcome, or topology lease dependency of any
# kind: this leaf consumes native_match_config only.
add_library(ctr_native_lockstep_rematch STATIC platform/native_lockstep_rematch.c)
set_target_properties(ctr_native_lockstep_rematch PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_rematch PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_rematch PUBLIC ctr_native_match_config)

# Connect/handshake protocol that lets two peers exchange and validate a
# NativeMatchConfigV1 proposal before a NativeLockstepSession is opened.
# Transport-agnostic by design, exactly like ctr_native_lockstep_protocol:
# this leaf encodes to and decodes from caller-owned byte buffers and has no
# socket, OS networking, clock, or game dependency, and it never links
# ctr_native_udp_transport, ctr_native_lockstep_session,
# ctr_native_lockstep_protocol, or ctr_native_virtual_datagram.
add_library(ctr_native_lockstep_handshake STATIC platform/native_lockstep_handshake.c)
set_target_properties(ctr_native_lockstep_handshake PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_handshake PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_handshake PUBLIC ctr_native_match_config ctr_native_canonical_codec)

# Real-transport integration glue (docs/LOBBY_MILESTONE.md section 2.3): the
# one module in this milestone allowed to depend on all three of
# ctr_native_udp_transport, ctr_native_lockstep_handshake, and
# ctr_native_lockstep_session -- nothing in Tasks 2-4 depends on this one.
# It owns one of each, drives the handshake to completion over a real
# socket, opens the session on the agreed config once the handshake
# completes, and from then on composes/sends and receives/accepts lockstep
# bundles over that same real socket.
add_library(ctr_native_lockstep_peer_link STATIC platform/native_lockstep_peer_link.c)
set_target_properties(ctr_native_lockstep_peer_link PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lockstep_peer_link PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lockstep_peer_link PUBLIC
    ctr_native_udp_transport ctr_native_lockstep_handshake ctr_native_lockstep_session)

# Lobby / waiting-flow state layer (docs/LOBBY_MILESTONE.md section 2.4): a
# data/state-layer-only policy wrapper over ctr_native_lockstep_peer_link
# adding a caller-supplied candidate peer address list and a frame-counted
# per-candidate attempt budget, exposing a small state enum an eventual
# waiting/lobby UI can read. No menu, no wheel input, no rendering; nothing
# under game/ references this module.
add_library(ctr_native_lobby_state STATIC platform/native_lobby_state.c)
set_target_properties(ctr_native_lobby_state PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_lobby_state PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_lobby_state PUBLIC ctr_native_lockstep_peer_link)

# Arcade menu input seam (docs/GAME_LOOP_UI_MILESTONE.md section 2.1): a pure
# classifier turning one player's held logical buttons into at most one
# PREV/NEXT/CONFIRM/BACK event per tick, release-to-arm and rising-edge only.
# A leaf with no link dependencies; reaches ctr_native through the host glue.
add_library(ctr_native_arcade_menu_input STATIC platform/native_arcade_menu_input.c)
set_target_properties(ctr_native_arcade_menu_input PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_menu_input PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Arcade screen flow seam (docs/GAME_LOOP_UI_MILESTONE.md section 2.2): a pure
# state machine for the arcade-link screens, fed one menu event and one
# observation per tick and returning at most one action. Links only the menu
# input seam for its event enum; reaches ctr_native through the host glue.
add_library(ctr_native_arcade_flow STATIC platform/native_arcade_flow.c)
set_target_properties(ctr_native_arcade_flow PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_flow PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_flow PUBLIC ctr_native_arcade_menu_input)

# Arcade-link host adapter (docs/GAME_LOOP_UI_MILESTONE.md section 2.3): the
# only production module that composes the lobby layer with the
# failure-handling layer and the match-select session
# (docs/MATCH_SELECT_MILESTONE.md section 2.6) and drives the pure arcade
# screen flow, with the race launch agreement (ctr_native_arcade_launch,
# docs/RACE_LAUNCH_MILESTONE.md RL-S5). It reaches the transport only through
# ctr_native_lobby_state.
# Reaches ctr_native through the host glue.
add_library(ctr_native_arcade_netplay STATIC platform/native_arcade_netplay.c)
set_target_properties(ctr_native_arcade_netplay PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_netplay PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_netplay PUBLIC
    ctr_native_arcade_flow ctr_native_arcade_menu_input ctr_native_lobby_state
    ctr_native_lockstep_match_outcome ctr_native_lockstep_match_roster ctr_native_lockstep_rematch
    ctr_native_match_config ctr_native_match_select_session ctr_native_arcade_launch)

# Arcade-link screen layout builder (docs/GAME_LOOP_UI_MILESTONE.md section
# 2.4, Task 5): a pure draw-list builder under game/MAIN with no game globals
# and no rendering. ctr_native unity-includes the same source (game/game_unity.h)
# instead of linking this target; the target exists for the unit test.
add_library(ctr_native_arcade_link_layout STATIC game/MAIN/MainArcadeLinkLayout.c)
set_target_properties(ctr_native_arcade_link_layout PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_link_layout PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_link_layout PUBLIC ctr_native_arcade_flow)

# Arcade-link live hook decision policy (docs/GAME_LOOP_UI_MILESTONE.md
# section 2.5, Task 6b-3): the pure ownership, input, and box decisions of the
# hook, with mirrored retail constants and no game globals. Like the layout,
# ctr_native unity-includes the same source (game/game_unity.h) instead of
# linking this target; the target exists for the unit test. It links nothing:
# it uses only the menu-input button bit macros.
add_library(ctr_native_arcade_link_policy STATIC game/MAIN/MainArcadeLinkPolicy.c)
set_target_properties(ctr_native_arcade_link_policy PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_link_policy PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)

# Arcade-link menu sound decision (docs/GAME_LOOP_UI_MILESTONE.md section
# 3.1, SND-1..SND-11): the pure cue choice behind the hook's retail menu
# sounds, from this cabinet's own host view fields only. Like the policy,
# ctr_native unity-includes the same source (game/game_unity.h) instead of
# linking this target; the target exists for the unit test. It links nothing:
# it uses only the host view and menu-event types and macros.
add_library(ctr_native_arcade_link_sound STATIC game/MAIN/MainArcadeLinkSound.c)
set_target_properties(ctr_native_arcade_link_sound PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_link_sound PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)

# Arcade-link host options and fixed race fixture (docs/GAME_LOOP_UI_MILESTONE.md
# sections 2.5 and 3, UX-8, Task 6a): pure argv parsing and a pure fixture
# builder over a caller-supplied identity, valid under the arcade bot rules
# (R-3). Uses only the identity struct, so it does not link
# ctr_native_identity. Reaches ctr_native through the host glue.
add_library(ctr_native_arcade_link_options STATIC platform/native_arcade_link_options.c)
set_target_properties(ctr_native_arcade_link_options PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_link_options PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_link_options PUBLIC ctr_native_match_config ctr_native_arcade_bot_rules)

# Per-cabinet config file (docs/PACKAGING.md PK-2..PK-6): pure key = value
# parsing of a caller-read buffer and of --config / --data-dir. The link group
# is checked by, and reaches the link only through, the arcade-link options
# parser. Reaches ctr_native through main.c only.
add_library(ctr_native_arcade_config STATIC platform/native_arcade_config.c)
set_target_properties(ctr_native_arcade_config PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_config PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_config PUBLIC ctr_native_arcade_link_options)

# Match-select resolution rules (docs/MATCH_SELECT_MILESTONE.md section 2.1,
# MS-2): pure tables mirrored from retail, seed derivation, tie draws, vote,
# character, and bot resolution, the outcome digest, and the resolved config
# builder. Reaches ctr_native through the select session, the arcade-link
# host adapter, and the host glue.
add_library(ctr_native_match_select_rules STATIC platform/native_match_select_rules.c)
set_target_properties(ctr_native_match_select_rules PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_match_select_rules PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_match_select_rules PUBLIC ctr_native_match_config ctr_native_sha256)

# Arcade bot rules v1 (docs/ROSTER_MILESTONE.md sections 3.1 and 3.3, R-2):
# pure, versioned rule choices for the two-cabinet arcade race (shape,
# difficulty table, retail 2P AI set rule, mode policy, retail RNG seed
# recipe), their canonical encoding and SHA-256 digest, the seed mapping
# and derivation, and the config validator. The arcade-link fixture builds on
# it (R-3).
add_library(ctr_native_arcade_bot_rules STATIC platform/native_arcade_bot_rules.c)
set_target_properties(ctr_native_arcade_bot_rules PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_bot_rules PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_bot_rules PUBLIC ctr_native_match_config ctr_native_sha256 ctr_native_canonical_codec ctr_native_deterministic_rng ctr_native_match_select_rules)

# Match-select wire message (docs/MATCH_SELECT_MILESTONE.md section 2.2,
# MS-3): the pure, transactional 64-byte select record codec with an FNV-1a
# 64 trailer; table membership from the rules. Reaches ctr_native through
# the select session, the arcade-link host adapter, and the host glue.
add_library(ctr_native_match_select_message STATIC platform/native_match_select_message.c)
set_target_properties(ctr_native_match_select_message PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_match_select_message PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_match_select_message PUBLIC ctr_native_match_select_rules ctr_native_canonical_codec)

# Match-select session (docs/MATCH_SELECT_MILESTONE.md section 2.3, MS-4):
# the pure, caller-owned select agreement state machine over the rules and
# the select message codec. Transport-free; only its fault test links the
# virtual datagram harness. Reaches ctr_native through the arcade-link host
# adapter and the host glue.
add_library(ctr_native_match_select_session STATIC platform/native_match_select_session.c)
set_target_properties(ctr_native_match_select_session PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_match_select_session PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_match_select_session PUBLIC ctr_native_match_select_rules ctr_native_match_select_message)

# Race launch agreement (docs/RACE_LAUNCH_MILESTONE.md section 4, RL-2,
# RL-3, RL-4; slice RL-S2): the pure, transactional 64-byte launch record v1
# codec with an FNV-1a 64 trailer, and the caller-owned launch agreement
# state. Linked only by the netplay adapter (ctr_native_arcade_netplay,
# RL-S5), which reaches ctr_native through the host glue.
add_library(ctr_native_arcade_launch STATIC platform/native_arcade_launch.c)
set_target_properties(ctr_native_arcade_launch PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_launch PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_launch PUBLIC ctr_native_canonical_codec)

# Linked-race drive core (docs/LOCKSTEP_RACE_MILESTONE.md LR-1). Pure, over
# caller-owned values: no socket, clock, SDL, heap, or game dependency.
# LR-S7 added the pad normalization (LR-4, LR-5); LR-S8 grows it into the
# per-tick drive core over a caller-owned lockstep session, kept-bundle
# ring, and callbacks (LR-41..LR-48). It links only the lockstep session
# (which brings the match config), and only its unit test links it until
# LR-S9 (tests/native_arcade_race_drive_isolation_test.cmake).
add_library(ctr_native_arcade_race_drive STATIC platform/native_arcade_race_drive.c)
set_target_properties(ctr_native_arcade_race_drive PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_race_drive PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_race_drive PUBLIC ctr_native_lockstep_session)

# Arcade-link host glue (docs/GAME_LOOP_UI_MILESTONE.md section 2.6, Task
# 6b-1): a process-wide singleton owning one arcade-link host adapter (link
# mode) or a scripted screen preview (preview mode), behind a small
# game-safe API. Dormant unless configured. Linked into ctr_native, whose
# main.c configures it from the host options (Task 6b-2). Since LR-S3
# (docs/LOCKSTEP_RACE_MILESTONE.md LR-7) it also has a link-time dependency
# on Platform_SetFixedVBlankPacing, which only the ctr_native executable
# defines (platform/native_platform.c): a test that links this library
# defines its own stub. Since LR-S9 it also links the linked-race drive core
# (ctr_native_arcade_race_drive), whose drive it runs over the adapter.
add_library(ctr_native_arcade_link_host STATIC platform/native_arcade_link_host.c)
set_target_properties(ctr_native_arcade_link_host PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_link_host PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_link_host PUBLIC ctr_native_arcade_netplay ctr_native_arcade_link_options
    ctr_native_arcade_race_drive)

# Internal two-process gate autopilot (docs/RACE_LAUNCH_MILESTONE.md RL-15,
# RL-S10): the --arcade-link-autopilot option parser, the pure menu decision
# and progress bookkeeping over a host view, and the report formatter; the
# report writer is its only I/O. Since LR-S13 part A
# (docs/LOCKSTEP_RACE_MILESTONE.md LR-73, LR-74) it also parses the fault
# options --arcade-link-autopilot-freeze and --arcade-link-autopilot-desync,
# decides the race 1 freeze and race 2 digest-XOR injections
# (NativeArcadeLinkAutopilot_FaultAt), which the race caller carries out
# next to its per-tick digest line. Uses only the host view and flow
# headers, so it links nothing. Linked into ctr_native, whose main.c parses it and whose
# unity-included game glue (game/MAIN/MainArcadeLinkAutopilot.c) runs it in
# internal builds.
add_library(ctr_native_arcade_link_autopilot STATIC platform/native_arcade_link_autopilot.c)
set_target_properties(ctr_native_arcade_link_autopilot PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_link_autopilot PUBLIC ${CMAKE_SOURCE_DIR}/include)

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

# Race setup pure core (docs/ROSTER_MILESTONE.md section 3.2, R-4): match
# config -> the retail race-defining field values, applied to a pointer-free
# mirror. Linked into ctr_native for the live adapter
# (game/MAIN/MainArcadeRaceSetup, R-5b), its only game caller.
add_library(ctr_native_arcade_race_setup_plan STATIC game/MAIN/MainArcadeRaceSetupPlan.c)
set_target_properties(ctr_native_arcade_race_setup_plan PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_race_setup_plan PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_race_setup_plan PUBLIC ctr_native_arcade_bot_rules ctr_native_match_config)

# Race setup facts builder (docs/ROSTER_MILESTONE.md section 3.2, R-5a): a
# pointer-free snapshot of the retail race after MainInit_Drivers -> the
# observed roster and bot setup facts. Linked into ctr_native for the live
# adapter (game/MAIN/MainArcadeRaceSetup, R-5b), its only game caller.
add_library(ctr_native_arcade_race_setup_facts STATIC game/MAIN/MainArcadeRaceSetupFacts.c)
set_target_properties(ctr_native_arcade_race_setup_facts PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_race_setup_facts PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_race_setup_facts PUBLIC ctr_native_arcade_bot_setup)

# Race setup decision core (docs/ROSTER_MILESTONE.md section 3.2, R-5c): every
# decision of the live race setup adapter as a pure step over a pointer-free
# view, with caller-owned state and scratch. Linked into ctr_native for the
# live adapter (game/MAIN/MainArcadeRaceSetup), its only game caller; never
# unity-included.
add_library(ctr_native_arcade_race_setup_core STATIC game/MAIN/MainArcadeRaceSetupCore.c)
set_target_properties(ctr_native_arcade_race_setup_core PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_race_setup_core PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_race_setup_core PUBLIC ctr_native_arcade_race_setup_plan ctr_native_arcade_race_setup_facts)

# Race launch decision core (docs/RACE_LAUNCH_MILESTONE.md RL-8..RL-11, slice
# RL-S7): the per-frame launch, bounded-wait, race tick 0, drive
# (docs/LOCKSTEP_RACE_MILESTONE.md LR-S10), return, pad, Disarm, and failure
# decisions of the live race caller, pure, with caller-owned state. Never
# unity-included; the RL-S8b caller links it. It links nothing.
add_library(ctr_native_arcade_race_launch_core STATIC game/MAIN/MainArcadeRaceLaunchCore.c)
set_target_properties(ctr_native_arcade_race_launch_core PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_race_launch_core PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)

# Stall hold period core (docs/LOCKSTEP_RACE_MILESTONE.md LR-9, slice LR-S2
# (a)): the tick-period, pump, and banner bookkeeping of the hold loop
# (game/MAIN/MainArcadeRaceHold.c), pure, with caller-owned state and no
# clock. Never unity-included; ctr_native links it. It links nothing.
add_library(ctr_native_arcade_race_hold_core STATIC game/MAIN/MainArcadeRaceHoldCore.c)
set_target_properties(ctr_native_arcade_race_hold_core PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_race_hold_core PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)

# Hold banner layout (include/platform/native_hold_banner.h, LR-S2 (a)): the
# built-in block font and the banner geometry the renderer fills over the
# presented image. No GL, SDL, clock, or heap; linked into ctr_native.
add_library(ctr_native_hold_banner STATIC platform/native_hold_banner.c)
set_target_properties(ctr_native_hold_banner PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_hold_banner PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Internal live roster proof host glue (docs/ROSTER_MILESTONE.md section 3.4,
# R-5b): the --arcade-roster-proof options, the proof config (the fixture
# resolved through match select), a singleton, the report writer, and the
# race-relative control digest (R-6b, over the V1 canonical state).
# Dormant unless main.c configures it; linked into ctr_native.
add_library(ctr_native_arcade_roster_proof STATIC platform/native_arcade_roster_proof.c)
set_target_properties(ctr_native_arcade_roster_proof PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_arcade_roster_proof PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_arcade_roster_proof PUBLIC ctr_native_arcade_link_options ctr_native_match_select_rules ctr_native_arcade_bot_rules
    ctr_native_canonical_state)

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

# Pointer-free active-mempack topology residency/layout validator.  It is
# intentionally standalone: no extractor, runtime, replay, or game target
# consumes this source-only pre-dereference contract.
add_library(ctr_native_topology_residency STATIC platform/native_topology_residency.c)
set_target_properties(ctr_native_topology_residency PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_topology_residency PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Standalone M2 WORLD mine registry value.  No replay or scheduler target links
# it directly; ctr_native gets it through the V4 state library, for the
# game-owned mine registry extractor below (Task 8 race plan LR-10).
add_library(ctr_native_canonical_world_mine_registry_v1 STATIC platform/native_canonical_world_mine_registry.c)
set_target_properties(ctr_native_canonical_world_mine_registry_v1 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_world_mine_registry_v1 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_world_mine_registry_v1 PUBLIC ctr_native_canonical_codec)

# Standalone, source-only M2 WORLD counter facts.  No replay or runtime target
# links it directly; ctr_native gets it through the V4 state library, for the
# game-owned counter extractor below (Task 8 race plan LR-10).
add_library(ctr_native_canonical_world_counters_v1 STATIC platform/native_canonical_world_counters.c)
set_target_properties(ctr_native_canonical_world_counters_v1 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_world_counters_v1 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_world_counters_v1 PUBLIC ctr_native_canonical_codec)

# Game-owned M2 WORLD counter extraction.  No runtime, state, or replay target
# links it; this target serves its unit test.  ctr_native compiles the source
# through the unity chain (game/game_unity.h), like the other MainCanonical*
# sources, for its one live caller, the V4 race digest
# (game/MAIN/MainArcadeRaceDigest.c, Task 8 race plan LR-10).  ctr_native must
# not link this target: a pulled member would define sdata a second time
# (include/regionsEXE.h).
add_library(ctr_native_main_canonical_world_counters STATIC game/MAIN/MainCanonicalWorldCounters.c)
set_target_properties(ctr_native_main_canonical_world_counters PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_main_canonical_world_counters PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(ctr_native_main_canonical_world_counters PRIVATE CTR_NATIVE)
target_link_libraries(ctr_native_main_canonical_world_counters PUBLIC ctr_native_canonical_world_counters_v1)

# Mine-pool adapter: no runtime, replay, canonical-state, or codec-consumer
# target links it; this target serves its unit test.  ctr_native compiles the
# source through the unity chain (game/game_unity.h), like the other
# MainCanonical* sources, for its one live caller, the V4 race digest
# (game/MAIN/MainArcadeRaceDigest.c, Task 8 race plan LR-10).  ctr_native must
# not link this target: a pulled member would define sdata a second time
# (include/regionsEXE.h).
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

# Offline-only, hash-pinned raw MODE2/BIG/LOD1 topology census.  This target
# has no game/runtime/emulator/network dependency and is never linked by ctr_native.
add_library(ctr_native_topology_asset_census STATIC platform/native_topology_asset_census.c)
set_target_properties(ctr_native_topology_asset_census PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_topology_asset_census PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_topology_asset_census PUBLIC ctr_native_sha256 ctr_native_canonical_topology_v1)

add_executable(ctr_native_topology_asset_census_tool tools/topology_asset_census.c)
set_target_properties(ctr_native_topology_asset_census_tool PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(ctr_native_topology_asset_census_tool PRIVATE ctr_native_topology_asset_census)

# Offline-only RGB check of arcade-link preview captures (BMP).  Pure analysis
# over a caller-owned buffer; never linked by ctr_native.
add_library(ctr_native_capture_check STATIC platform/native_capture_check.c)
set_target_properties(ctr_native_capture_check PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_capture_check PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(ctr_native_arcade_link_capture_check tools/arcade_link_capture_check.c)
set_target_properties(ctr_native_arcade_link_capture_check PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(ctr_native_arcade_link_capture_check PRIVATE ctr_native_capture_check)

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

# Game-owned active-mempack lease authority. The executable's private owner
# may retire it at audited destructive boundaries, but no executable path can
# acquire, activate, capture, publish, serialize, replay, or network it.
add_library(main_canonical_topology_lease_authority STATIC
    game/MAIN/MainCanonicalTopologyLeaseAuthority.c)
set_target_properties(main_canonical_topology_lease_authority PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(main_canonical_topology_lease_authority PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(main_canonical_topology_lease_authority PUBLIC CTR_NATIVE)
target_link_libraries(main_canonical_topology_lease_authority PUBLIC ctr_native_topology_residency)

# Value-only lifecycle observation log. Its only executable consumer is the
# private retirement owner; it still has no canonical-state, replay,
# scheduler, transport, topology-candidate, or network consumer.
add_library(main_canonical_topology_lifecycle_events STATIC
    game/MAIN/MainCanonicalTopologyLifecycleEvents.c)
set_target_properties(main_canonical_topology_lifecycle_events PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(main_canonical_topology_lifecycle_events PUBLIC
    ${CMAKE_SOURCE_DIR}/game)

# Dormant active-mempack topology candidate.  This bridge streams proven
# resident facts into a pointer-free value and emits a matching residency
# witness, but deliberately has no runtime, replay, or executable consumer.
add_library(main_canonical_topology_lease_adapter STATIC
    game/MAIN/MainCanonicalTopologyLeaseAdapter.c)
set_target_properties(main_canonical_topology_lease_adapter PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(main_canonical_topology_lease_adapter PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(main_canonical_topology_lease_adapter PUBLIC CTR_NATIVE)
target_link_libraries(main_canonical_topology_lease_adapter PUBLIC
    main_canonical_topology_lease_authority ctr_native_canonical_topology_facts)

# Compile-only layout assertions for the narrow, source-grounded topology/nav
# surface.  This object probe has no runtime/executable dependency.
add_library(ctr_native_main_canonical_topology_layout_contract_probe OBJECT
    tests/main_canonical_topology_layout_contract_probe.c)
set_target_properties(ctr_native_main_canonical_topology_layout_contract_probe PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_main_canonical_topology_layout_contract_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(ctr_native_main_canonical_topology_layout_contract_probe PRIVATE CTR_NATIVE)

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

# Source-only raw-ingress observation.  It accepts only caller-provided
# snapshots and remains isolated from the runtime, replay, transport, and
# game-loop targets until tick and input ownership are explicitly decided.
add_library(ctr_native_deterministic_input_observation STATIC game/MAIN/MainDeterministicInputObservation.c)
set_target_properties(ctr_native_deterministic_input_observation PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_deterministic_input_observation PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_deterministic_input_observation PUBLIC ctr_native_canonical_projector)

# Isolated V4 explicit-value projector.  It is linked by the dormant runtime
# coordinator as an explicit dependency so the runtime's declared graph is
# complete, but still not called from any live path; the scheduler, replay,
# file, and game executable targets do not name it directly.
add_library(ctr_native_canonical_projector_v4 STATIC game/MAIN/MainCanonicalStateV4.c)
set_target_properties(ctr_native_canonical_projector_v4 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_projector_v4 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_canonical_projector_v4 PUBLIC ctr_native_canonical_state_v4 ctr_native_match_config)

# Exact-domain candidate adapter.  Fact elements and output are pointer-free;
# topology item/count ranges are read-only borrowed inputs for the call only.
# This source-only bridge is deliberately separate from extraction and every
# live/replay target.
add_library(ctr_native_canonical_exact_domains_v4 STATIC
    game/MAIN/MainCanonicalStateV4ExactFacts.c
    game/MAIN/MainCanonicalTopologyFacts.c)
set_target_properties(ctr_native_canonical_exact_domains_v4 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_exact_domains_v4 PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_canonical_exact_domains_v4 PUBLIC ctr_native_canonical_projector_v4 ctr_native_canonical_drivers_detailed)

# Source-only M3-to-V4 setup composition.  This one-way adapter is not linked
# by unity, runtime, scheduler, replay, file, network, UI, or the executable.
add_library(ctr_native_arcade_setup_v4 STATIC game/MAIN/MainArcadeSetupV4.c)
set_target_properties(ctr_native_arcade_setup_v4 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_arcade_setup_v4 PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_arcade_setup_v4 PUBLIC ctr_native_arcade_bot_setup ctr_native_canonical_projector_v4)

# Source-only bot-tick evidence: records supplied values; it has no bot
# controller, native extraction, simulation, runtime, replay, or UI consumer.
add_library(ctr_native_bot_tick_evidence STATIC game/MAIN/MainArcadeBotTickEvidence.c)
set_target_properties(ctr_native_bot_tick_evidence PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_bot_tick_evidence PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(ctr_native_bot_tick_evidence PUBLIC ctr_native_arcade_bot_setup ctr_native_canonical_drivers_detailed ctr_native_match_config ctr_native_deterministic_rng)

# Dormant, static game-owned V3 coordinator.  It is compiled for link
# coverage but no live game path calls it in this slice.
add_library(ctr_native_canonical_runtime STATIC game/MAIN/MainCanonicalRuntime.c)
set_target_properties(ctr_native_canonical_runtime PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_canonical_runtime PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
target_compile_definitions(ctr_native_canonical_runtime PUBLIC CTR_NATIVE CTR_INTERNAL)
target_link_libraries(ctr_native_canonical_runtime PUBLIC ctr_native_canonical_projector ctr_native_canonical_projector_v4 ctr_native_canonical_drivers_roster ctr_native_canonical_topology ctr_native_canonical_state_v4 ctr_native_match_config ctr_native_replay_scheduler_seam)

add_library(ctr_native_replay_scheduler_seam STATIC platform/native_replay_scheduler_seam.c)
set_target_properties(ctr_native_replay_scheduler_seam PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_replay_scheduler_seam PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_scheduler_seam PUBLIC ctr_native_canonical_state ctr_native_canonical_state_v3 ctr_native_canonical_state_v4)

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

# Isolated CRV4/CRF4 value codec.  It deliberately has no file/session or runtime consumer.
add_library(ctr_native_replay_v4 STATIC platform/native_replay_v4.c)
set_target_properties(ctr_native_replay_v4 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_replay_v4 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v4 PUBLIC ctr_native_canonical_state_v4 ctr_native_match_config)

# Isolated CRV4 file transaction/reader; no runtime consumer.
add_library(ctr_native_replay_v4_file STATIC platform/native_replay_v4_file.c)
set_target_properties(ctr_native_replay_v4_file PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_replay_v4_file PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_v4_file PUBLIC ctr_native_replay_v4)

# Dormant V4 scheduler foundation: source/test-only, never linked into ctr_native.
add_library(ctr_native_replay_scheduler_v4 STATIC platform/native_replay_scheduler_v4.c)
set_target_properties(ctr_native_replay_scheduler_v4 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ctr_native_replay_scheduler_v4 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_replay_scheduler_v4 PUBLIC ctr_native_replay_v4_file)

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

# Pure VBlank pacing decision (include/platform/native_vblank_pacing.h): what
# the native VBlank pacer in platform/native_platform.c does with late
# VBlanks, by default (catch up) and under the host-local fixed pacing
# (re-anchor, never catch up), which only the roster proof and a linked race
# turn on (docs/LOCKSTEP_RACE_MILESTONE.md LR-7). No clock, no I/O; linked
# into ctr_native.
add_library(ctr_native_vblank_pacing STATIC platform/native_vblank_pacing.c)
set_target_properties(ctr_native_vblank_pacing PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_vblank_pacing PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Host-only SDL assertion policy: log one line and continue instead of SDL's
# modal assertion dialog, so an unattended Debug run can never block on it.
# Linked into ctr_native as a library; it is not part of the game unity chain.
add_library(ctr_native_sdl_assert STATIC platform/native_sdl_assert.c)
set_target_properties(ctr_native_sdl_assert PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(ctr_native_sdl_assert PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ctr_native_sdl_assert PUBLIC SDL3::SDL3)

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
    # /OPT:NOICF: the game and the canonical driver binding identify behaviour
    # by function-pointer identity (MainCanonicalDrivers.c TokenFor, the
    # RegistrySelfTest, retail funcPtr/funcThDestroy comparisons).  The MSVC
    # Release default (identical COMDAT folding) merges identical bodies into
    # one address (e.g.
    # VehPhysProc_SpinStop_{Update,PhysLinear,PhysAngular} into their SlamWall
    # twins), which breaks C17 6.5.9p6 function-pointer distinctness and makes
    # the Release live roster facts fail to extract.  /OPT:REF is left at the
    # linker default: on for Release and MinSizeRel, off for Debug and
    # RelWithDebInfo (their /DEBUG link turns it off).
    # Enforced by tests/native_function_identity_link_isolation_test.cmake.
    target_link_options(ctr_native PRIVATE /Brepro /OPT:NOICF)
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
target_link_libraries(ctr_native SDL3::SDL3 ctr_native_sdl_assert ctr_native_g29_input ctr_native_canonical_projector ctr_native_canonical_runtime ctr_native_replay_scheduler_seam ctr_native_replay_v2_file ctr_native_replay_v3_file ctr_native_canonical_drivers_roster ctr_native_canonical_pool ctr_native_canonical_fixed_array ctr_native_canonical_topology ctr_native_match_config ctr_native_topology_residency main_canonical_topology_lifecycle_events ctr_native_arcade_link_host ctr_native_arcade_race_setup_plan ctr_native_arcade_race_setup_facts ctr_native_arcade_race_setup_core ctr_native_arcade_race_launch_core ctr_native_arcade_roster_proof ctr_native_arcade_link_autopilot ctr_native_vblank_pacing ctr_native_arcade_race_hold_core ctr_native_hold_banner ctr_native_arcade_config)
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

    # Presentation settings are deliberately cabinet-local.  Keep them out of
    # the match, canonical-state, and replay transport boundaries so changing
    # an integer render scale cannot alter lockstep/replay identity.
    add_test(NAME native_render_scale_determinism_contract
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_render_scale_determinism_contract_test.cmake")

    # The host-local texture filter is owned by the renderer and reaches the
    # GPU only through the PSX fragment-shader uniform.  Vertex construction,
    # the VRAM sampler state, and every transport boundary stay independent of
    # it.
    add_test(NAME native_texture_filter_seam_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_texture_filter_seam_isolation_test.cmake")

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

    # The VBlank pacing decision (R-6b): the default catch-up and the fixed
    # pacing, with an injected host stall; the isolation test keeps the
    # fixed-pacing switch host-local, out of game/, and called only by main.c
    # (the roster proof) and the arcade-link host glue (a linked race, LR-7).
    add_executable(native_vblank_pacing_test tests/native_vblank_pacing_test.c)
    set_target_properties(native_vblank_pacing_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_vblank_pacing_test PRIVATE ctr_native_vblank_pacing)
    add_test(NAME native_vblank_pacing_unit COMMAND $<TARGET_FILE:native_vblank_pacing_test>)
    add_test(NAME native_vblank_pacing_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_vblank_pacing_isolation_test.cmake")

    add_executable(native_sdl_assert_test tests/native_sdl_assert_test.c)
    set_target_properties(native_sdl_assert_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_sdl_assert_test PRIVATE ctr_native_sdl_assert)
    add_test(NAME native_sdl_assert_unit COMMAND $<TARGET_FILE:native_sdl_assert_test>)

    # SDL assertions must never block startup on a modal dialog: the handler
    # is installed before SDL_Init, no host code overrides SDL_ASSERT, and no
    # host code disables HIDAPI or hides the G29.
    add_test(NAME native_sdl_startup_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_sdl_startup_isolation_test.cmake")

    add_executable(native_display_config_test tests/native_display_config_test.c)
    set_target_properties(native_display_config_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_display_config_test PRIVATE ctr_native_display_config)
    add_test(NAME native_display_config_unit COMMAND $<TARGET_FILE:native_display_config_test>)

    add_executable(native_frame_capture_test tests/native_frame_capture_test.c)
    set_target_properties(native_frame_capture_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_frame_capture_test PRIVATE ctr_native_frame_capture)
    add_test(NAME native_frame_capture_unit COMMAND $<TARGET_FILE:native_frame_capture_test>)

    add_executable(native_virtual_datagram_test tests/native_virtual_datagram_test.c)
    set_target_properties(native_virtual_datagram_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_virtual_datagram_test PRIVATE ctr_native_virtual_datagram)
    add_test(NAME native_virtual_datagram_unit COMMAND $<TARGET_FILE:native_virtual_datagram_test>)
    add_test(NAME native_virtual_datagram_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_virtual_datagram_isolation_test.cmake")

    add_executable(native_udp_transport_test tests/native_udp_transport_test.c)
    set_target_properties(native_udp_transport_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_udp_transport_test PRIVATE ctr_native_udp_transport)
    add_test(NAME native_udp_transport_unit COMMAND $<TARGET_FILE:native_udp_transport_test>)

    # Not registered as its own add_test: it is a helper process spawned by
    # native_udp_transport_process_test below.
    add_executable(native_udp_transport_echo_helper tests/native_udp_transport_echo_helper.c)
    set_target_properties(native_udp_transport_echo_helper PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_udp_transport_echo_helper PRIVATE ctr_native_udp_transport)

    add_executable(native_udp_transport_process_test tests/native_udp_transport_process_test.c)
    set_target_properties(native_udp_transport_process_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_udp_transport_process_test PRIVATE ctr_native_udp_transport)
    add_test(NAME native_udp_transport_process_unit
        COMMAND $<TARGET_FILE:native_udp_transport_process_test> $<TARGET_FILE:native_udp_transport_echo_helper>)

    add_test(NAME native_udp_transport_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_udp_transport_isolation_test.cmake")

    add_executable(native_lockstep_protocol_test tests/native_lockstep_protocol_test.c)
    set_target_properties(native_lockstep_protocol_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_protocol_test PRIVATE ctr_native_lockstep_protocol)
    add_test(NAME native_lockstep_protocol_unit COMMAND $<TARGET_FILE:native_lockstep_protocol_test>)

    add_executable(native_lockstep_input_window_test tests/native_lockstep_input_window_test.c)
    set_target_properties(native_lockstep_input_window_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_input_window_test PRIVATE ctr_native_lockstep_input_window)
    add_test(NAME native_lockstep_input_window_unit COMMAND $<TARGET_FILE:native_lockstep_input_window_test>)

    add_executable(native_lockstep_session_test tests/native_lockstep_session_test.c)
    set_target_properties(native_lockstep_session_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_session_test PRIVATE ctr_native_lockstep_session)
    add_test(NAME native_lockstep_session_unit COMMAND $<TARGET_FILE:native_lockstep_session_test>)

    add_executable(native_lockstep_match_outcome_test tests/native_lockstep_match_outcome_test.c)
    set_target_properties(native_lockstep_match_outcome_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_match_outcome_test PRIVATE ctr_native_lockstep_match_outcome ctr_native_lockstep_session)
    add_test(NAME native_lockstep_match_outcome_unit COMMAND $<TARGET_FILE:native_lockstep_match_outcome_test>)

    add_executable(native_lockstep_match_roster_test tests/native_lockstep_match_roster_test.c)
    set_target_properties(native_lockstep_match_roster_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_match_roster_test PRIVATE ctr_native_lockstep_match_roster ctr_native_match_config)
    add_test(NAME native_lockstep_match_roster_unit COMMAND $<TARGET_FILE:native_lockstep_match_roster_test>)

    add_executable(native_lockstep_rematch_test tests/native_lockstep_rematch_test.c)
    set_target_properties(native_lockstep_rematch_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_rematch_test PRIVATE ctr_native_lockstep_rematch)
    add_test(NAME native_lockstep_rematch_unit COMMAND $<TARGET_FILE:native_lockstep_rematch_test>)

    add_executable(native_lockstep_handshake_test tests/native_lockstep_handshake_test.c)
    set_target_properties(native_lockstep_handshake_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_handshake_test PRIVATE ctr_native_lockstep_handshake)
    add_test(NAME native_lockstep_handshake_unit COMMAND $<TARGET_FILE:native_lockstep_handshake_test>)

    add_executable(native_lockstep_transport_fault_test tests/native_lockstep_transport_fault_test.c)
    set_target_properties(native_lockstep_transport_fault_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_transport_fault_test PRIVATE ctr_native_lockstep_session ctr_native_virtual_datagram)
    add_test(NAME native_lockstep_transport_fault_unit COMMAND $<TARGET_FILE:native_lockstep_transport_fault_test>)

    add_executable(native_lockstep_failure_handling_fault_test tests/native_lockstep_failure_handling_fault_test.c)
    set_target_properties(native_lockstep_failure_handling_fault_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_failure_handling_fault_test PRIVATE
        ctr_native_lockstep_session ctr_native_lockstep_match_outcome ctr_native_lockstep_match_roster
        ctr_native_lockstep_rematch ctr_native_virtual_datagram)
    add_test(NAME native_lockstep_failure_handling_fault_unit COMMAND $<TARGET_FILE:native_lockstep_failure_handling_fault_test>)

    add_executable(native_lockstep_handshake_fault_test tests/native_lockstep_handshake_fault_test.c)
    set_target_properties(native_lockstep_handshake_fault_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_handshake_fault_test PRIVATE ctr_native_lockstep_handshake ctr_native_virtual_datagram)
    add_test(NAME native_lockstep_handshake_fault_unit COMMAND $<TARGET_FILE:native_lockstep_handshake_fault_test>)

    # Not registered as its own add_test: it is the second real OS process
    # spawned by native_lockstep_peer_link_process_test below.
    add_executable(native_lockstep_peer_link_helper tests/native_lockstep_peer_link_helper.c)
    set_target_properties(native_lockstep_peer_link_helper PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_peer_link_helper PRIVATE ctr_native_lockstep_peer_link)

    # Master two-process, real-socket, real-lockstep-bundle proof test for
    # the whole lobby milestone (docs/LOBBY_MILESTONE.md section 2.3 and
    # section 3 constraint 8).
    add_executable(native_lockstep_peer_link_process_test tests/native_lockstep_peer_link_process_test.c)
    set_target_properties(native_lockstep_peer_link_process_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_peer_link_process_test PRIVATE ctr_native_lockstep_peer_link)
    add_test(NAME native_lockstep_peer_link_process_unit
        COMMAND $<TARGET_FILE:native_lockstep_peer_link_process_test> $<TARGET_FILE:native_lockstep_peer_link_helper>)

    # Single-process, real-socket hardening tests (Task 5b, following up on
    # Task 5 commit 8fdfb0413): deterministically controls call ordering
    # itself, unlike native_lockstep_peer_link_process_test's two real OS
    # processes.
    add_executable(native_lockstep_peer_link_test tests/native_lockstep_peer_link_test.c)
    set_target_properties(native_lockstep_peer_link_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lockstep_peer_link_test PRIVATE ctr_native_lockstep_peer_link)
    add_test(NAME native_lockstep_peer_link_unit COMMAND $<TARGET_FILE:native_lockstep_peer_link_test>)

    # Lobby / waiting-flow state layer (docs/LOBBY_MILESTONE.md section 2.4,
    # Task 6): real loopback sockets, all within this one test process.
    add_executable(native_lobby_state_test tests/native_lobby_state_test.c)
    set_target_properties(native_lobby_state_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_lobby_state_test PRIVATE ctr_native_lobby_state)
    add_test(NAME native_lobby_state_unit COMMAND $<TARGET_FILE:native_lobby_state_test>)

    # Arcade menu input seam (docs/GAME_LOOP_UI_MILESTONE.md section 2.1,
    # Task 2): pure input classification, no sockets and no clock.
    add_executable(native_arcade_menu_input_test tests/native_arcade_menu_input_test.c)
    set_target_properties(native_arcade_menu_input_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_menu_input_test PRIVATE ctr_native_arcade_menu_input)
    add_test(NAME native_arcade_menu_input_unit COMMAND $<TARGET_FILE:native_arcade_menu_input_test>)

    add_test(NAME native_arcade_menu_input_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_menu_input_isolation_test.cmake")

    # Arcade screen flow seam (docs/GAME_LOOP_UI_MILESTONE.md section 2.2,
    # Task 3): pure state machine, no sockets and no clock.
    add_executable(native_arcade_flow_test tests/native_arcade_flow_test.c)
    set_target_properties(native_arcade_flow_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_flow_test PRIVATE ctr_native_arcade_flow)
    add_test(NAME native_arcade_flow_unit COMMAND $<TARGET_FILE:native_arcade_flow_test>)

    add_test(NAME native_arcade_flow_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_flow_isolation_test.cmake")

    # Arcade-link host adapter (docs/GAME_LOOP_UI_MILESTONE.md section 2.3,
    # Task 4): two in-process adapters over real loopback sockets, every
    # advance tick-counted, never wall-clock.
    add_executable(native_arcade_netplay_test tests/native_arcade_netplay_test.c)
    set_target_properties(native_arcade_netplay_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_netplay_test PRIVATE ctr_native_arcade_netplay)
    add_test(NAME native_arcade_netplay_unit COMMAND $<TARGET_FILE:native_arcade_netplay_test>)

    add_test(NAME native_arcade_netplay_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_netplay_isolation_test.cmake")

    # Arcade-link screen layout builder (docs/GAME_LOOP_UI_MILESTONE.md
    # section 2.4, Task 5): pure draw lists, no game globals, no rendering.
    add_executable(main_arcade_link_layout_test tests/main_arcade_link_layout_test.c)
    set_target_properties(main_arcade_link_layout_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_link_layout_test PRIVATE ctr_native_arcade_link_layout)
    add_test(NAME main_arcade_link_layout_unit COMMAND $<TARGET_FILE:main_arcade_link_layout_test>)

    add_test(NAME main_arcade_link_layout_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_link_layout_isolation_test.cmake")

    # Arcade-link live hook decision policy (docs/GAME_LOOP_UI_MILESTONE.md
    # section 2.5, Task 6b-3): every ownership, input, and box branch, pure.
    add_executable(main_arcade_link_policy_test tests/main_arcade_link_policy_test.c)
    set_target_properties(main_arcade_link_policy_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_link_policy_test PRIVATE ctr_native_arcade_link_policy)
    add_test(NAME main_arcade_link_policy_unit COMMAND $<TARGET_FILE:main_arcade_link_policy_test>)

    add_test(NAME main_arcade_link_policy_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_link_policy_isolation_test.cmake")

    # Arcade-link menu sound decision (docs/GAME_LOOP_UI_MILESTONE.md
    # section 3.1): every SND rule and its negatives, pure.
    add_executable(main_arcade_link_sound_test tests/main_arcade_link_sound_test.c)
    set_target_properties(main_arcade_link_sound_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_link_sound_test PRIVATE ctr_native_arcade_link_sound)
    add_test(NAME main_arcade_link_sound_unit COMMAND $<TARGET_FILE:main_arcade_link_sound_test>)

    add_test(NAME main_arcade_link_sound_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_link_sound_isolation_test.cmake")

    # Arcade-link host options and fixed fixture (docs/GAME_LOOP_UI_MILESTONE.md
    # section 2.5, Task 6a): pure argv parsing and fixture building, no sockets.
    add_executable(native_arcade_link_options_test tests/native_arcade_link_options_test.c)
    set_target_properties(native_arcade_link_options_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_link_options_test PRIVATE ctr_native_arcade_link_options)
    add_test(NAME native_arcade_link_options_unit COMMAND $<TARGET_FILE:native_arcade_link_options_test>)

    add_test(NAME native_arcade_link_options_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_link_options_isolation_test.cmake")

    # Per-cabinet config file (docs/PACKAGING.md PK-2..PK-6): the grammar,
    # every error class, the link group through the real link parser, and the
    # committed package templates.
    add_executable(native_arcade_config_test tests/native_arcade_config_test.c)
    set_target_properties(native_arcade_config_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_config_test PRIVATE ctr_native_arcade_config)
    add_test(NAME native_arcade_config_unit
        COMMAND $<TARGET_FILE:native_arcade_config_test>
            "${CMAKE_SOURCE_DIR}/tools/package/cab1.cfg"
            "${CMAKE_SOURCE_DIR}/tools/package/cab2.cfg")

    add_test(NAME native_arcade_config_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_config_isolation_test.cmake")

    # The explicit data directory (PK-6): NativeAssets_InitWithAssetDir over
    # fabricated folders with tiny dummy files under the build tree.
    add_executable(native_assets_data_dir_test
        tests/native_assets_data_dir_test.c
        platform/native_sha256.c
        platform/native_disc_image.c
    )
    set_target_properties(native_assets_data_dir_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(native_assets_data_dir_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(native_assets_data_dir_test PRIVATE CTR_NATIVE)
    add_test(NAME native_assets_data_dir_unit
        COMMAND "${CMAKE_COMMAND}"
            -DTEST_EXE=$<TARGET_FILE:native_assets_data_dir_test>
            -DWORK_DIR=${CMAKE_BINARY_DIR}/native_assets_data_dir_test/$<CONFIG>
            -P "${CMAKE_SOURCE_DIR}/tests/native_assets_data_dir_test.cmake")

    # The package retail-data guard (docs/PACKAGING.md PK-7): the exact file
    # allowlist passes; retail or BIOS names, unknown files, subdirectories,
    # and oversize files fail. Fabricated tiny dummies under the build tree.
    if(WIN32)
        add_test(NAME package_arcade_content_guard
            COMMAND "${CMAKE_COMMAND}"
                -DREPO_DIR=${CMAKE_SOURCE_DIR}
                -DWORK_DIR=${CMAKE_BINARY_DIR}/package_guard_test/$<CONFIG>
                -P "${CMAKE_SOURCE_DIR}/tests/package_arcade_content_guard_test.cmake")
    endif()

    # Internal two-process gate autopilot (docs/RACE_LAUNCH_MILESTONE.md
    # RL-15, RL-S10): the option parser, the pure menu decision and progress
    # bookkeeping, and the report format; the isolation test pins its
    # purity, the hook and main.c wiring (the option is rejected with replay
    # options), the glue that never touches a pad, and the live gate's
    # ctest registration. Since LR-S13 part A the unit test also links the
    # canonical codec, to pin the desync injection's CONTROL digest index
    # against NativeCanonicalDomainOrder (LR-73).
    add_executable(native_arcade_link_autopilot_test tests/native_arcade_link_autopilot_test.c)
    set_target_properties(native_arcade_link_autopilot_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_link_autopilot_test PRIVATE ctr_native_arcade_link_autopilot ctr_native_canonical_codec)
    add_test(NAME native_arcade_link_autopilot_unit COMMAND $<TARGET_FILE:native_arcade_link_autopilot_test>)

    add_test(NAME native_arcade_link_autopilot_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_link_autopilot_isolation_test.cmake")

    # The LR-S2 (b) autopilot finish spike (docs/LOCKSTEP_RACE_MILESTONE.md
    # LR-16): the steering decision stays pure, only the roster proof calls
    # it, and the proof reads the restart points only in its autopilot
    # helpers, never into a digest, a tick line, or the report. The unit test
    # of the steering decision is native_arcade_link_autopilot_unit.
    add_test(NAME arcade_roster_proof_autopilot_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/arcade_roster_proof_autopilot_isolation_test.cmake")

    # Match-select resolution rules (docs/MATCH_SELECT_MILESTONE.md section
    # 2.1, MS-2): pure; the isolation test checks the retail table mirrors.
    add_executable(native_match_select_rules_test tests/native_match_select_rules_test.c)
    set_target_properties(native_match_select_rules_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_match_select_rules_test PRIVATE ctr_native_match_select_rules)
    add_test(NAME native_match_select_rules_unit COMMAND $<TARGET_FILE:native_match_select_rules_test>)

    add_test(NAME native_match_select_rules_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_match_select_rules_isolation_test.cmake")

    # Arcade bot rules v1 (docs/ROSTER_MILESTONE.md section 3.1, R-2): golden
    # encoding and digest; the isolation test checks purity and the retail
    # difficulty and advRng fallback mirrors. The unit test also runs the real
    # arcade-link fixture through match select and rematch (R-3b).
    add_executable(native_arcade_bot_rules_test tests/native_arcade_bot_rules_test.c)
    set_target_properties(native_arcade_bot_rules_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_bot_rules_test PRIVATE ctr_native_arcade_bot_rules ctr_native_arcade_link_options
        ctr_native_lockstep_rematch)
    add_test(NAME native_arcade_bot_rules_unit COMMAND $<TARGET_FILE:native_arcade_bot_rules_test>)

    add_test(NAME native_arcade_bot_rules_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_bot_rules_isolation_test.cmake")

    # Match-select wire message (docs/MATCH_SELECT_MILESTONE.md section 2.2,
    # MS-3): frozen golden bytes, every fault cause in check order, and the
    # isolation test freezes the wire constants.
    add_executable(native_match_select_message_test tests/native_match_select_message_test.c)
    set_target_properties(native_match_select_message_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_match_select_message_test PRIVATE ctr_native_match_select_message)
    add_test(NAME native_match_select_message_unit COMMAND $<TARGET_FILE:native_match_select_message_test>)

    add_test(NAME native_match_select_message_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_match_select_message_isolation_test.cmake")

    # Match-select session (docs/MATCH_SELECT_MILESTONE.md section 2.3,
    # MS-4): every rule driven directly, then fault injection (loss, delay,
    # reorder, duplication, corruption, silence, stale selects, 4 humans)
    # over the virtual datagram harness, which only the fault test links.
    add_executable(native_match_select_session_test tests/native_match_select_session_test.c)
    set_target_properties(native_match_select_session_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_match_select_session_test PRIVATE ctr_native_match_select_session)
    add_test(NAME native_match_select_session_unit COMMAND $<TARGET_FILE:native_match_select_session_test>)

    add_executable(native_match_select_session_fault_test tests/native_match_select_session_fault_test.c)
    set_target_properties(native_match_select_session_fault_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_match_select_session_fault_test PRIVATE ctr_native_match_select_session ctr_native_virtual_datagram)
    add_test(NAME native_match_select_session_fault COMMAND $<TARGET_FILE:native_match_select_session_fault_test>)

    add_test(NAME native_match_select_session_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_match_select_session_isolation_test.cmake")

    # Race launch agreement (docs/RACE_LAUNCH_MILESTONE.md RL-2, RL-3, RL-4;
    # slice RL-S2): frozen golden bytes, every fault cause in check order,
    # and the agreement driven in memory (commit, linger, one-direction
    # loss); the isolation test freezes the wire constants.
    add_executable(native_arcade_launch_test tests/native_arcade_launch_test.c)
    set_target_properties(native_arcade_launch_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_launch_test PRIVATE ctr_native_arcade_launch)
    add_test(NAME native_arcade_launch_unit COMMAND $<TARGET_FILE:native_arcade_launch_test>)

    add_test(NAME native_arcade_launch_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_launch_isolation_test.cmake")

    # Linked-race drive core (docs/LOCKSTEP_RACE_MILESTONE.md LR-S7, LR-S8):
    # pad normalization over every status and id byte, and two drive cores
    # over real sessions exchanging bundles in memory, with a real outcome
    # tracker (linked into the test only) behind OnTakeResult. The test also
    # links the roster proof, the RL-10 source of truth for the disconnected
    # pad bytes (LR-43). The isolation test keeps the core pure (includes,
    # token ban, C17, links, send order).
    add_executable(native_arcade_race_drive_test tests/native_arcade_race_drive_test.c)
    set_target_properties(native_arcade_race_drive_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_race_drive_test PRIVATE ctr_native_arcade_race_drive ctr_native_lockstep_match_outcome
        ctr_native_arcade_roster_proof)
    add_test(NAME native_arcade_race_drive_unit COMMAND $<TARGET_FILE:native_arcade_race_drive_test>)

    add_test(NAME native_arcade_race_drive_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_race_drive_isolation_test.cmake")

    # The local sample seam (docs/LOCKSTEP_RACE_MILESTONE.md LR-4, LR-S7):
    # platform/native_input.c compiled into the test as main.c compiles it
    # into ctr_native (the test includes the .c to reach its statics), with
    # SDL input initialized headless, every host device closed, and SDL
    # virtual joysticks standing in for a gamepad and a G29. It installs the
    # host SDL assertion handler first, as Platform_Init does, so no SDL
    # assertion can block it on a modal dialog. SDL3 is built static here
    # (SDL_SHARED OFF), so there is no SDL3.dll to find at run time.
    add_executable(native_input_sample_test tests/native_input_sample_test.c)
    set_target_properties(native_input_sample_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(native_input_sample_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(native_input_sample_test PRIVATE CTR_NATIVE)
    target_link_libraries(native_input_sample_test PRIVATE SDL3::SDL3 ctr_native_g29_input ctr_native_sdl_assert)
    add_test(NAME native_input_sample_unit COMMAND $<TARGET_FILE:native_input_sample_test>)
    set_tests_properties(native_input_sample_unit PROPERTIES TIMEOUT 60)

    # Sound IDs stay out of cross-cabinet identity (docs/RACE_LAUNCH_MILESTONE.md
    # RL-14; slice RL-S3): identity-bearing sources name no sound-ID token.
    add_test(NAME arcade_sound_identity_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/arcade_sound_identity_isolation_test.cmake")

    # Arcade-link host glue (docs/GAME_LOOP_UI_MILESTONE.md section 2.6,
    # Task 6b-1): the singleton in every mode, one loopback socket on a dead
    # peer port, every advance tick-counted, never wall-clock.
    add_executable(native_arcade_link_host_test tests/native_arcade_link_host_test.c)
    set_target_properties(native_arcade_link_host_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_link_host_test PRIVATE ctr_native_arcade_link_host)
    add_test(NAME native_arcade_link_host_unit COMMAND $<TARGET_FILE:native_arcade_link_host_test>)

    add_test(NAME native_arcade_link_host_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_arcade_link_host_isolation_test.cmake")

    # Arcade-link host view into the screen layout (MS-10b): every tick of
    # every preview and of a live loopback LINK run (host as cabinet 1 on
    # ports 48520-48521 against a test-owned adapter) passes the drawer's
    # path, GetView -> MainArcadeLinkLayout_InputFromHostView -> Build.
    add_executable(main_arcade_link_view_layout_test tests/main_arcade_link_view_layout_test.c)
    set_target_properties(main_arcade_link_view_layout_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_link_view_layout_test PRIVATE ctr_native_arcade_link_host ctr_native_arcade_link_layout)
    add_test(NAME main_arcade_link_view_layout_unit COMMAND $<TARGET_FILE:main_arcade_link_view_layout_test>)

    # Arcade-link live hook (docs/GAME_LOOP_UI_MILESTONE.md section 2.5, Task
    # 6b-2): native only, dormant by default, host glue API only, and wired
    # into main.c, the unity chain, and the retail menu seam as designed.
    add_test(NAME main_arcade_link_hook_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_link_hook_isolation_test.cmake")

    add_test(NAME native_lockstep_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_lockstep_isolation_test.cmake")

    add_test(NAME native_lockstep_failure_handling_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_lockstep_failure_handling_isolation_test.cmake")

    add_test(NAME native_lockstep_handshake_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_lockstep_handshake_isolation_test.cmake")

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

    # Race setup pure core (R-4): plans built from the real fixture through
    # match select, menu-history independence of Apply, rejections, and a
    # golden digest; the isolation test checks purity, the link set, the
    # retail mirrors, and that nothing in game/ uses it yet.
    add_executable(main_arcade_race_setup_plan_test tests/main_arcade_race_setup_plan_test.c)
    set_target_properties(main_arcade_race_setup_plan_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_race_setup_plan_test PRIVATE ctr_native_arcade_race_setup_plan
        ctr_native_arcade_link_options)
    add_test(NAME main_arcade_race_setup_plan_unit COMMAND $<TARGET_FILE:main_arcade_race_setup_plan_test>)

    add_test(NAME main_arcade_race_setup_plan_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_setup_plan_isolation_test.cmake")

    # Race setup facts builder (R-5a): the fixture race's snapshot and roster
    # input pass the roster and bot setup validators on the post-seed bank;
    # rejections leave outputs untouched. The isolation test checks purity,
    # the link set, and that nothing uses it yet.
    add_executable(main_arcade_race_setup_facts_test tests/main_arcade_race_setup_facts_test.c)
    set_target_properties(main_arcade_race_setup_facts_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_race_setup_facts_test PRIVATE ctr_native_arcade_race_setup_facts
        ctr_native_arcade_bot_rules ctr_native_arcade_link_options)
    add_test(NAME main_arcade_race_setup_facts_unit COMMAND $<TARGET_FILE:main_arcade_race_setup_facts_test>)

    add_test(NAME main_arcade_race_setup_facts_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_setup_facts_isolation_test.cmake")

    # Race setup decision core (R-5c): every adapter decision, unit-tested
    # through its pointer-free views and ordered write lists; the isolation
    # test keeps it pure and linked, never unity-included.
    add_executable(main_arcade_race_setup_core_test tests/main_arcade_race_setup_core_test.c)
    set_target_properties(main_arcade_race_setup_core_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_race_setup_core_test PRIVATE ctr_native_arcade_race_setup_core
        ctr_native_arcade_bot_rules ctr_native_arcade_link_options)
    add_test(NAME main_arcade_race_setup_core_unit COMMAND $<TARGET_FILE:main_arcade_race_setup_core_test>)

    add_test(NAME main_arcade_race_setup_core_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_setup_core_isolation_test.cmake")

    # Race launch decision core (RL-S7): every RL-8..RL-11 frame rule, the
    # bounds on and off by one, and two races in a row; the isolation test
    # keeps it pure and applies the section 5 token ban.
    add_executable(main_arcade_race_launch_core_test tests/main_arcade_race_launch_core_test.c)
    set_target_properties(main_arcade_race_launch_core_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_race_launch_core_test PRIVATE ctr_native_arcade_race_launch_core)
    add_test(NAME main_arcade_race_launch_core_unit COMMAND $<TARGET_FILE:main_arcade_race_launch_core_test>)

    add_test(NAME main_arcade_race_launch_core_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_launch_core_isolation_test.cmake")

    # Stall hold (LR-S2 (a)): the period core's grace, period, and pump
    # rules, and the banner layout; the isolation tests keep the hold loop
    # free of VSync, pads, game state, and lockstep tokens, and the host wait
    # free of any VBlank, callback, or audio step and called only by the hold.
    add_executable(main_arcade_race_hold_core_test tests/main_arcade_race_hold_core_test.c)
    set_target_properties(main_arcade_race_hold_core_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    # LR-S11: the hold's banner rule is pinned equal to the drive's BannerDue
    # (LR-44), so this test also links the drive core.
    target_link_libraries(main_arcade_race_hold_core_test PRIVATE ctr_native_arcade_race_hold_core ctr_native_arcade_race_drive)
    add_test(NAME main_arcade_race_hold_core_unit COMMAND $<TARGET_FILE:main_arcade_race_hold_core_test>)

    # The hold loop itself (MainArcadeRaceHold_Run) over stubbed platform
    # calls: the pump/step/banner/wait order, the grace, and the result.
    add_executable(main_arcade_race_hold_test tests/main_arcade_race_hold_test.c game/MAIN/MainArcadeRaceHold.c)
    set_target_properties(main_arcade_race_hold_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_compile_definitions(main_arcade_race_hold_test PRIVATE CTR_NATIVE)
    target_include_directories(main_arcade_race_hold_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
    target_link_libraries(main_arcade_race_hold_test PRIVATE ctr_native_arcade_race_hold_core)
    add_test(NAME main_arcade_race_hold_unit COMMAND $<TARGET_FILE:main_arcade_race_hold_test>)

    # Root-counter wrap (LR-S3 (b), LR-8): the real game/Timer.c, included
    # through the real game headers with a stubbed sdata and root counter,
    # shows the elapsedTimeMS 31 crossing and that the race setup's pin puts
    # it on the same race tick for every boot-relative phase. Signed overflow
    # is UB in C17: only the reference toolchain (MSVC x86) is evidence, so
    # the test is registered only there.
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(game_timer_wrap_test tests/game_timer_wrap_test.c)
        set_target_properties(game_timer_wrap_test PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_compile_definitions(game_timer_wrap_test PRIVATE CTR_NATIVE)
        target_include_directories(game_timer_wrap_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
        if(CTR_NATIVE_MSVC_FRONTEND)
            # The game headers' mixed-enum masks, as for ctr_native.
            target_compile_options(game_timer_wrap_test PRIVATE /wd5287)
        endif()
        add_test(NAME game_timer_wrap_unit COMMAND $<TARGET_FILE:game_timer_wrap_test>)
    endif()

    add_executable(native_hold_banner_test tests/native_hold_banner_test.c)
    set_target_properties(native_hold_banner_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_hold_banner_test PRIVATE ctr_native_hold_banner)
    add_test(NAME native_hold_banner_unit COMMAND $<TARGET_FILE:native_hold_banner_test>)

    add_test(NAME main_arcade_race_hold_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_hold_isolation_test.cmake")
    add_test(NAME native_host_wait_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_host_wait_isolation_test.cmake")

    # The link's and the race caller's returns to title, interleaved in the
    # live hook-then-caller order against a model of the retail load
    # (race-launch risk 10): exactly one main-menu load per return.
    add_executable(main_arcade_link_return_interleave_test tests/main_arcade_link_return_interleave_test.c)
    set_target_properties(main_arcade_link_return_interleave_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(main_arcade_link_return_interleave_test PRIVATE ctr_native_arcade_link_policy
        ctr_native_arcade_race_launch_core)
    add_test(NAME main_arcade_link_return_interleave_unit COMMAND $<TARGET_FILE:main_arcade_link_return_interleave_test>)

    # R-5a2/R-5c: MainCanonicalDrivers_ExtractRosterInput and its pre-race
    # variant are named only by their own module, the live race setup adapter
    # (which must use the pre-race variant), and tests/.
    add_test(NAME main_canonical_drivers_roster_input_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_drivers_roster_input_isolation_test.cmake")

    # Live race setup adapter and internal roster proof launcher (R-5b): the
    # proof options, config builder, singleton, and report formatter; the
    # isolation test fixes the hook placement, the arming callers, the unity
    # position, and the token bans.
    add_executable(native_arcade_roster_proof_test tests/native_arcade_roster_proof_test.c)
    set_target_properties(native_arcade_roster_proof_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(native_arcade_roster_proof_test PRIVATE ctr_native_arcade_roster_proof
        ctr_native_arcade_race_setup_plan)
    add_test(NAME native_arcade_roster_proof_unit COMMAND $<TARGET_FILE:native_arcade_roster_proof_test>)

    add_test(NAME main_arcade_race_setup_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_setup_isolation_test.cmake")

    # Pause-menu vibration guard (RL-S9, RL-13): the DualShock row's gameMode1
    # toggle in game/MAIN/MainFreeze.c is skipped while the race setup is not
    # IDLE, inside CTR_NATIVE; the analog-controller row stays retail.
    add_test(NAME main_freeze_vibration_guard_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_freeze_vibration_guard_isolation_test.cmake")

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

    add_executable(native_topology_residency_test tests/native_topology_residency_test.c)
    set_target_properties(native_topology_residency_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_topology_residency_test PRIVATE ctr_native_topology_residency)
    add_test(NAME native_topology_residency_unit COMMAND $<TARGET_FILE:native_topology_residency_test>)
    add_test(NAME native_topology_residency_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_topology_residency_isolation_test.cmake")

    add_executable(native_topology_asset_census_test tests/native_topology_asset_census_test.c)
    set_target_properties(native_topology_asset_census_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_topology_asset_census_test PRIVATE ctr_native_topology_asset_census)
    add_test(NAME native_topology_asset_census_unit COMMAND $<TARGET_FILE:native_topology_asset_census_test>)
    add_test(NAME native_topology_asset_census_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_topology_asset_census_isolation_test.cmake")

    add_executable(native_capture_check_test tests/native_capture_check_test.c)
    set_target_properties(native_capture_check_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_capture_check_test PRIVATE ctr_native_capture_check)
    add_test(NAME native_capture_check_unit COMMAND $<TARGET_FILE:native_capture_check_test>)
    add_test(NAME native_capture_check_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_capture_check_isolation_test.cmake")

    # Renders every arcade-link preview screen (plus the default path) with
    # ctr_native and checks each capture with the RGB checker.  Needs the
    # user-supplied assets/ctr-u.bin and a desktop session with a display;
    # skips (77) otherwise.  Captures and logs are written only under the
    # build tree.
    if(WIN32)
        add_test(NAME arcade_link_preview_render
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tools/arcade-link-preview-check.ps1"
                -Executable "$<TARGET_FILE:ctr_native>"
                -Checker "$<TARGET_FILE:ctr_native_arcade_link_capture_check>"
                -OutputDirectory "${CMAKE_BINARY_DIR}/arcade_link_preview_captures/$<CONFIG>"
                -IncludeDefault)
        set_tests_properties(arcade_link_preview_render PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 420
            RUN_SERIAL TRUE
            LABELS live)

        # The live roster determinism proof (docs/ROSTER_MILESTONE.md section
        # 3.4, R-6, R-6b, R-6c, R-6d, and OC-3; run K is
        # docs/LOCKSTEP_RACE_MILESTONE.md LR-S2 (a)): eleven internal
        # --arcade-roster-proof runs of ctr_native, all at once under the
        # proof's fixed VBlank pacing, five of the two-cabinet profile (A-E)
        # and five of the single-cabinet profile (F-J), each profile run as:
        # same seed twice, same seed launched from inside the attract demo
        # race, another seed, same seed launched an odd number of ticks late;
        # compared tick by tick, within each profile and across the two; and
        # A again with the stall hold (K), which must equal A.
        # Needs the user-supplied assets/ctr-u.bin, a desktop session with a
        # display, and an internal build; skips (77) otherwise.  Reports and
        # logs are written only under the build tree.
        # The live tests carry the label "live": `ctest -LE live` leaves them
        # out, and the default full run still includes them.
        add_test(NAME arcade_roster_determinism
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tools/arcade-roster-proof-check.ps1"
                -Executable "$<TARGET_FILE:ctr_native>"
                -OutputDirectory "${CMAKE_BINARY_DIR}/arcade_roster_proof/$<CONFIG>"
                -Ticks 900
                -TimeoutSeconds 600)
        set_tests_properties(arcade_roster_determinism PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 900
            RUN_SERIAL TRUE
            LABELS live)

        # The two-process live race gate (docs/RACE_LAUNCH_MILESTONE.md
        # RL-15, RL-S10; since LR-S13 part B the one-machine proof of
        # docs/LOCKSTEP_RACE_MILESTONE.md LR-16, LR-76): two internal
        # ctr_native processes linked over loopback (127.0.0.1 ports 7001 and
        # 7002, cab1 and cab2), each driven by --arcade-link-autopilot through
        # START, the select, and three lockstep races with a 6000-tick cap:
        # race 1 to the natural finish (or the finish grace) on the same race
        # tick, with cab2 frozen for 45 tick periods at race tick 600 (cab1
        # holds with the banner in the game font); REMATCH; race 2, where
        # cab2 flips its CONTROL digest of race tick 300 (RACE OUT OF SYNC);
        # REMATCH; race 3, where the check kills cab2 at its race tick 300 and
        # cab1 ends OPPONENT DISCONNECTED after the 90-period stall timeout,
        # takes EXIT, and exits 0 with PASS. The check compares the k-th race
        # of the two cabinets (the agreed match, the config, plan, bots, and
        # bank digests, the LR-8 triples, and every per-tick V4 digest line),
        # requires each race's config to differ from the previous race's, and
        # checks each race's end, and one race 1 frame capture per cabinet
        # (criterion 9, kept under the build tree). RUN_SERIAL like the other
        # live tests, and because the ports are fixed. Needs the user-supplied
        # assets/ctr-u.bin, a desktop session with a display, an internal
        # build, and a known build identity (a build from a clean tree); skips
        # (77) otherwise, and a skip is not a pass. Reports, captures, and
        # logs are written only under the build tree.
        add_test(NAME arcade_link_launch
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tools/arcade-link-launch-check.ps1"
                -Executable "$<TARGET_FILE:ctr_native>"
                -OutputDirectory "${CMAKE_BINARY_DIR}/arcade_link_launch/$<CONFIG>"
                -TimeoutSeconds 780)
        set_tests_properties(arcade_link_launch PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 900
            RUN_SERIAL TRUE
            LABELS live)
    endif()

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
    target_link_libraries(main_canonical_drivers_binding_test PRIVATE ctr_native_canonical_drivers_roster ctr_native_canonical_pool ctr_native_canonical_fixed_array ctr_native_canonical_state_v3 ctr_native_canonical_state_v4 ctr_native_match_config ctr_native_replay_scheduler_seam)
    if(CTR_NATIVE_MSVC_FRONTEND)
        # Compiles MainCanonicalDrivers.c: keep its stubbed callbacks at
        # distinct addresses, as for ctr_native (no identical code folding).
        target_link_options(main_canonical_drivers_binding_test PRIVATE /OPT:NOICF)
    endif()
    add_test(NAME main_canonical_drivers_binding_unit COMMAND $<TARGET_FILE:main_canonical_drivers_binding_test>)
    # The binding above stubs the game callbacks; the shipped ctr_native link
    # must also keep every callback at a distinct address (no identical code
    # folding).
    add_test(NAME native_function_identity_link_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/native_function_identity_link_isolation_test.cmake")
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        # The generated ctr_native project must carry that into every
        # configuration's link settings (COMDAT folding false, never true).
        string(REPLACE ";" "," ctr_native_link_output_configs "${CMAKE_CONFIGURATION_TYPES}")
        add_test(NAME native_function_identity_link_output
            COMMAND "${CMAKE_COMMAND}"
                "-DCTR_VCXPROJ=${CMAKE_BINARY_DIR}/ctr_native.vcxproj"
                "-DCTR_CONFIGS=${ctr_native_link_output_configs}"
                -P "${CMAKE_SOURCE_DIR}/tests/native_function_identity_link_output_test.cmake")
    endif()

    # The live V4 race digest (game/MAIN/MainArcadeRaceDigest.c, Task 8 race
    # plan LR-10, slice LR-S4) over the real runtime, drivers extraction,
    # projector, and world extractors (all included as sources, as the unity
    # chain includes them); NativePerf is stubbed.
    add_executable(main_arcade_race_digest_test tests/main_arcade_race_digest_test.c)
    set_target_properties(main_arcade_race_digest_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_arcade_race_digest_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_arcade_race_digest_test PRIVATE CTR_NATIVE CTR_INTERNAL)
    target_link_libraries(main_arcade_race_digest_test PRIVATE ctr_native_canonical_drivers_roster ctr_native_canonical_pool ctr_native_canonical_fixed_array ctr_native_canonical_state_v3 ctr_native_canonical_state_v4 ctr_native_match_config ctr_native_replay_scheduler_seam)
    if(CTR_NATIVE_MSVC_FRONTEND)
        # Compiles MainCanonicalDrivers.c: keep its stubbed callbacks at
        # distinct addresses, as for ctr_native (no identical code folding).
        target_link_options(main_arcade_race_digest_test PRIVATE /OPT:NOICF)
    endif()
    add_test(NAME main_arcade_race_digest_unit COMMAND $<TARGET_FILE:main_arcade_race_digest_test>)
    add_test(NAME main_arcade_race_digest_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_race_digest_isolation_test.cmake")

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

    add_test(NAME main_canonical_topology_layout_contract_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_topology_layout_contract_isolation_test.cmake")

    add_executable(main_canonical_topology_lease_authority_test tests/main_canonical_topology_lease_authority_test.c)
    set_target_properties(main_canonical_topology_lease_authority_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_topology_lease_authority_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_topology_lease_authority_test PRIVATE CTR_NATIVE)
    # Compile the dormant source into its fixture executable, avoiding the
    # legacy global sdata definition in the game-layout header crossing two
    # object files.  The standalone library is built independently above.
    target_link_libraries(main_canonical_topology_lease_authority_test PRIVATE ctr_native_topology_residency)
    add_test(NAME main_canonical_topology_lease_authority_unit COMMAND $<TARGET_FILE:main_canonical_topology_lease_authority_test>)
    add_test(NAME main_canonical_topology_lease_authority_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_topology_lease_authority_isolation_test.cmake")

    add_executable(main_canonical_topology_lifecycle_events_test tests/main_canonical_topology_lifecycle_events_test.c)
    set_target_properties(main_canonical_topology_lifecycle_events_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_topology_lifecycle_events_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_link_libraries(main_canonical_topology_lifecycle_events_test PRIVATE main_canonical_topology_lifecycle_events)
    add_test(NAME main_canonical_topology_lifecycle_events_unit COMMAND $<TARGET_FILE:main_canonical_topology_lifecycle_events_test>)
    add_test(NAME main_canonical_topology_lifecycle_events_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_topology_lifecycle_events_isolation_test.cmake")

    add_executable(main_canonical_topology_lease_adapter_test tests/main_canonical_topology_lease_adapter_test.c)
    set_target_properties(main_canonical_topology_lease_adapter_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_topology_lease_adapter_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_topology_lease_adapter_test PRIVATE CTR_NATIVE)
    # The fixture includes the three game-owned sources in one translation
    # unit, avoiding the legacy game-layout global crossing object files.
    target_link_libraries(main_canonical_topology_lease_adapter_test PRIVATE ctr_native_topology_residency ctr_native_canonical_topology_v1)
    add_test(NAME main_canonical_topology_lease_adapter_unit COMMAND $<TARGET_FILE:main_canonical_topology_lease_adapter_test>)
    add_test(NAME main_canonical_topology_lease_adapter_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_topology_lease_adapter_isolation_test.cmake")

    add_executable(main_canonical_topology_lease_runtime_test tests/main_canonical_topology_lease_runtime_test.c)
    set_target_properties(main_canonical_topology_lease_runtime_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_topology_lease_runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/game)
    target_compile_definitions(main_canonical_topology_lease_runtime_test PRIVATE CTR_NATIVE)
    target_link_libraries(main_canonical_topology_lease_runtime_test PRIVATE ctr_native_topology_residency main_canonical_topology_lifecycle_events)
    add_test(NAME main_canonical_topology_lease_runtime_unit COMMAND $<TARGET_FILE:main_canonical_topology_lease_runtime_test>)
    add_test(NAME main_canonical_topology_lease_runtime_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_topology_lease_runtime_isolation_test.cmake")

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
    target_compile_definitions(native_canonical_state_v4_test PRIVATE
        NCV4_GOLDEN_PATH=\"${CMAKE_SOURCE_DIR}/tests/fixtures/native_canonical_state_v4_golden.bin\")
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

    add_executable(main_deterministic_input_observation_test tests/main_deterministic_input_observation_test.c)
    set_target_properties(main_deterministic_input_observation_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_deterministic_input_observation_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_link_libraries(main_deterministic_input_observation_test PRIVATE ctr_native_deterministic_input_observation)
    add_test(NAME main_deterministic_input_observation_unit COMMAND $<TARGET_FILE:main_deterministic_input_observation_test>)
    add_test(NAME main_deterministic_input_observation_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_deterministic_input_observation_isolation_test.cmake")

    add_executable(main_canonical_state_v4_projector_test tests/main_canonical_state_v4_projector_test.c)
    set_target_properties(main_canonical_state_v4_projector_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(main_canonical_state_v4_projector_test PRIVATE ctr_native_canonical_projector_v4)
    add_test(NAME main_canonical_state_v4_projector_unit COMMAND $<TARGET_FILE:main_canonical_state_v4_projector_test>)
    add_test(NAME main_canonical_state_v4_projector_link_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_state_v4_projector_isolation_test.cmake")

    add_executable(main_canonical_state_v4_exact_facts_test tests/main_canonical_state_v4_exact_facts_test.c)
    set_target_properties(main_canonical_state_v4_exact_facts_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_canonical_state_v4_exact_facts_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_link_libraries(main_canonical_state_v4_exact_facts_test PRIVATE ctr_native_canonical_exact_domains_v4)
    add_test(NAME main_canonical_state_v4_exact_facts_unit COMMAND $<TARGET_FILE:main_canonical_state_v4_exact_facts_test>)
    add_test(NAME main_canonical_state_v4_exact_facts_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_state_v4_exact_facts_isolation_test.cmake")

    add_executable(main_arcade_setup_v4_test tests/main_arcade_setup_v4_test.c)
    set_target_properties(main_arcade_setup_v4_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_arcade_setup_v4_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_link_libraries(main_arcade_setup_v4_test PRIVATE ctr_native_arcade_setup_v4)
    add_test(NAME main_arcade_setup_v4_unit COMMAND $<TARGET_FILE:main_arcade_setup_v4_test>)
    add_test(NAME main_arcade_setup_v4_link_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_setup_v4_isolation_test.cmake")

    add_executable(main_arcade_bot_tick_evidence_test tests/main_arcade_bot_tick_evidence_test.c)
    set_target_properties(main_arcade_bot_tick_evidence_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(main_arcade_bot_tick_evidence_test PRIVATE ${CMAKE_SOURCE_DIR}/game)
    target_link_libraries(main_arcade_bot_tick_evidence_test PRIVATE ctr_native_bot_tick_evidence)
    add_test(NAME main_arcade_bot_tick_evidence_unit COMMAND $<TARGET_FILE:main_arcade_bot_tick_evidence_test>)
    add_test(NAME main_arcade_bot_tick_evidence_link_isolation
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_arcade_bot_tick_evidence_isolation_test.cmake")

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

    add_executable(native_replay_v4_test tests/native_replay_v4_test.c)
    set_target_properties(native_replay_v4_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_replay_v4_test PRIVATE ctr_native_replay_v4)
    add_test(NAME native_replay_v4_unit COMMAND $<TARGET_FILE:native_replay_v4_test>)
    set_tests_properties(native_replay_v4_unit PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(native_replay_v4_file_test tests/native_replay_v4_file_test.c)
    set_target_properties(native_replay_v4_file_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_replay_v4_file_test PRIVATE ctr_native_replay_v4_file)
    add_test(NAME native_replay_v4_file_unit COMMAND $<TARGET_FILE:native_replay_v4_file_test>)
    set_tests_properties(native_replay_v4_file_unit PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(native_replay_scheduler_v4_test tests/native_replay_scheduler_v4_test.c)
    set_target_properties(native_replay_scheduler_v4_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(native_replay_scheduler_v4_test PRIVATE ctr_native_replay_scheduler_v4)
    add_test(NAME native_replay_scheduler_v4_unit COMMAND $<TARGET_FILE:native_replay_scheduler_v4_test>)

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
            ctr_native_canonical_state_v4
            ctr_native_canonical_world_counters_v1
            ctr_native_canonical_world_mine_registry_v1
            ctr_native_canonical_topology_v1
            ctr_native_deterministic_rng
            ctr_native_match_config
            ctr_native_sha256
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
        add_test(NAME main_canonical_runtime_stack_budget_v4
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/tests/main_canonical_runtime_stack_budget.ps1"
                -AsmDirectory "${CTR_NATIVE_RUNTIME_STACK_LISTING_DIR}"
                -BudgetBytes 1536
                -Root _MainCanonicalRuntime_PrepareV4
        )
        set_tests_properties(main_canonical_runtime_stack_budget_v4 PROPERTIES CONFIGURATIONS Release)
        add_test(NAME main_canonical_runtime_isolation
            COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/main_canonical_runtime_isolation_test.cmake")
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
