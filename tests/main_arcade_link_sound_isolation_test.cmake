# Structural isolation for the arcade-link menu sound decision
# (game/MAIN/MainArcadeLinkSound.{c,h}, docs/GAME_LOOP_UI_MILESTONE.md
# section 3.1, SND-1..SND-11): a pure cue decision that never talks to the
# host glue, the netplay adapter, the link, or its state, never plays a
# sound, reads no game global and no random state, uses no heap and no
# stdio, and never names the topology lease, replay, checkpoint, or
# canonical state. Its only includes are stdint.h, stddef.h, string.h, the
# host header (for the host view type and host value names), the menu-input
# header (for the menu events), and its own header; the library links
# nothing; ctr_native does not link it (the source is unity-included); the
# target stays portable C17 with extensions off; and its mirrored flow values
# stay in step with the flow header (MainArcadeLink.c and the unit test also
# static-assert each one).
#
# Sound stays in the thin hook: the retail sound call is named only by
# game/MAIN/MainArcadeLink.c, once, after the decision, and never by the
# decision, the layout, the policy, or any platform/native_arcade_* source or
# header. The hook resets the decision's snapshot on every frame the layer
# does not own and on RETURN_TO_TITLE; except race frames under RL-8
# (tickOnly), which leave the snapshot holding the RACING view stored on the
# last owned frame, so RESULTS entry keeps its SND-9 cue. Since RL-S8b the
# START_RACE branch hands the launch to the race caller instead of aborting
# to the title, so it no longer resets the snapshot either: the flow is on
# RACING and the snapshot keeps tracking it.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link sound isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link sound isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_literal relative_path source literal)
    string(FIND "${source}" "${literal}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "arcade link sound isolation: required text '${literal}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "arcade link sound isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# Fails unless the terms appear in source in the given order.
function(ctr_require_order relative_path source)
    set(remaining "${source}")
    foreach(term IN LISTS ARGN)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "arcade link sound isolation: '${term}' is missing or out of order in ${relative_path}")
        endif()
        string(LENGTH "${term}" term_length)
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endforeach()
endfunction()

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

set(sound_header "game/MAIN/MainArcadeLinkSound.h")
set(sound_source "game/MAIN/MainArcadeLinkSound.c")
set(sound_files "${sound_header}" "${sound_source}")

# 1a. No host glue call, netplay, link, or lobby dependency. The host header
#     is included for its types and value names only.
set(netplay_tokens
    NativeArcadeLinkHost_ NativeArcadeNetplay native_arcade_netplay
    NativeArcadeFlow_ lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport NativeUdp winsock WSA sockaddr
    NativeMatchSelect NATIVE_MATCH_SELECT_ NativeMatchConfig)

# 1b. No sound call, game global, retail header, random state, or rendering:
#     the hook plays the sound.
set(game_tokens
    OtherFX Howl HOWL howl sdata gGT GameTracker GamepadSystem common.h functions.h MM_TITLE MM_MENU D230 RectMenu RECTMENU_
    DecalFont CTR_Box PrimMem OTMem audioRNG randomNumber advRng RNG Rng "rand(")

# 1c. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 1d. No stdio.
set(stdio_tokens printf snprintf stdio.h Platform_Log)

# 1e. No topology-lease, replay, checkpoint, or canonical-state symbol.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire Capture LOAD_Hub_ReadFile
    NativeReplay native_replay Replay replay NativeCanonical native_canonical Canonical canonical Checkpoint checkpoint)

foreach(relative_path IN LISTS sound_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS netplay_tokens game_tokens alloc_tokens stdio_tokens lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 2. #include lines may only name stdint.h, stddef.h, string.h, the host
    #    header, the menu-input header, or the module's own header. No
    #    conditional compilation beyond the header guard.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"platform/native_arcade_link_host\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"MAIN/MainArcadeLinkSound\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade link sound isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|elif|else)[ \t\r\n]" conditionals "${source}")
    if(NOT "${conditionals}" STREQUAL "")
        message(FATAL_ERROR "arcade link sound isolation: ${relative_path} must not use conditional compilation")
    endif()
endforeach()

# 2b. No mutable state: no file-scope static object in the source (the
#     snapshot is caller-owned).
ctr_read_source("${sound_source}" source)
string(REGEX MATCH "(^|\n)static[ \t]+[^\r\n(]*(=|;)" static_object "${source}")
if(NOT "${static_object}" STREQUAL "")
    message(FATAL_ERROR "arcade link sound isolation: ${sound_source} must hold no file-scope static object (found '${static_object}')")
endif()

# 3a. ctr_native_arcade_link_sound links nothing.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_link_sound)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 0)
    message(FATAL_ERROR "arcade link sound isolation: ${target} must link nothing (found ${link_call_count} target_link_libraries call)")
endif()

# 3b. Declared once, from the sound source only, with include dirs include and game.
string(REGEX MATCHALL "add_library\\(${target} STATIC[^)]*\\)" declarations "${cmake}")
list(LENGTH declarations declaration_count)
if(NOT declaration_count EQUAL 1)
    message(FATAL_ERROR "arcade link sound isolation: expected exactly one add_library(${target} STATIC ...), found ${declaration_count}")
endif()
list(GET declarations 0 declaration)
if(NOT declaration STREQUAL "add_library(${target} STATIC game/MAIN/MainArcadeLinkSound.c)")
    message(FATAL_ERROR "arcade link sound isolation: ${target} must build game/MAIN/MainArcadeLinkSound.c alone (found '${declaration}')")
endif()
ctr_require_literal("CMakeLists.txt" "${cmake}"
    "target_include_directories(${target} PUBLIC \${CMAKE_SOURCE_DIR}/include \${CMAKE_SOURCE_DIR}/game)")

# 3c. C17, no extensions, on the sound target and its unit test.
foreach(checked IN ITEMS "add_library(${target} STATIC" "add_executable(main_arcade_link_sound_test ")
    string(FIND "${cmake}" "${checked}" declare_at)
    if(declare_at EQUAL -1)
        message(FATAL_ERROR "arcade link sound isolation: missing '${checked}' in CMakeLists.txt")
    endif()
    string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
    string(FIND "${target_block}" "set_target_properties(" properties_at)
    string(FIND "${target_block}" "C_STANDARD 17" standard_at)
    string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
    string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
    if(properties_at EQUAL -1 OR standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
        message(FATAL_ERROR "arcade link sound isolation: '${checked}' is missing set_target_properties with C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
    endif()
    if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
        message(FATAL_ERROR "arcade link sound isolation: '${checked}' C17/no-extensions properties are out of order")
    endif()
endforeach()
ctr_require_literal("CMakeLists.txt" "${cmake}"
    "target_link_libraries(main_arcade_link_sound_test PRIVATE ctr_native_arcade_link_sound)")

# 3d. ctr_native does not link the sound library (the source is unity-included).
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
if(native_link_start EQUAL -1)
    message(FATAL_ERROR "arcade link sound isolation: missing target_link_libraries(ctr_native ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(FIND "${native_link_block}" "${target}" sound_hit)
if(NOT sound_hit EQUAL -1)
    message(FATAL_ERROR "arcade link sound isolation: ctr_native must not link ${target} (the decision is unity-included)")
endif()

# 4. The retail sound IDs (SND-1) are frozen, and the mirrored flow values
#    stay in step with the flow header.
ctr_read_source("${sound_header}" header)
foreach(id IN ITEMS "MOVE 0u" "CONFIRM 1u" "BACK 2u" "ERROR 5u")
    ctr_require_regex("${sound_header}" "${header}" "(^|\n)#define MAIN_ARCADE_LINK_SOUND_ID_${id}\r?\n")
endforeach()
foreach(source_line IN ITEMS
        "game/RECTMENU.c:799" "game/RECTMENU.c:817" "game/RECTMENU.c:845" "game/RECTMENU.c:866"
        "game/230/MM_Characters.c:1156" "game/230/MM_Characters.c:1216" "game/230/MM_Characters.c:1239"
        "game/230/MM_TrackSelect.c:635" "game/230/MM_TrackSelect.c:677" "game/230/MM_Battle.c:500")
    ctr_require_literal("${sound_header}" "${header}" "${source_line}")
endforeach()

ctr_read_source("include/platform/native_arcade_flow.h" flow)
foreach(literal
        "NATIVE_ARCADE_FLOW_SCREEN_OFF = 0," "NATIVE_ARCADE_FLOW_SCREEN_LOBBY = 1,"
        "NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND = 2," "NATIVE_ARCADE_FLOW_SCREEN_RESULTS = 4,"
        "NATIVE_ARCADE_FLOW_SCREEN_SELECT = 7," "NATIVE_ARCADE_FLOW_LOBBY_REJECTED = 3,"
        "NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT = 2," "NATIVE_ARCADE_FLOW_END_DESYNC = 3,"
        "NATIVE_ARCADE_FLOW_END_LINK_ERROR = 4," "NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT = 5")
    ctr_require_literal("include/platform/native_arcade_flow.h" "${flow}" "${literal}")
endforeach()
set(mirror_pairs
    "SCREEN_OFF 0u" "SCREEN_LOBBY 1u" "SCREEN_MATCH_FOUND 2u" "SCREEN_RESULTS 4u" "SCREEN_SELECT 7u"
    "LOBBY_REJECTED 3u" "END_PEER_TIMEOUT 2u" "END_DESYNC 3u" "END_LINK_ERROR 4u" "END_OPPONENT_LEFT 5u")
foreach(mirror IN LISTS mirror_pairs)
    ctr_require_regex("${sound_header}" "${header}" "(^|\n)#define MAIN_ARCADE_LINK_SOUND_${mirror}\r?\n")
endforeach()

# 5. The hook static-asserts every mirror against the flow value, plays the
#    retail sound exactly once, in its sound helper, after the decision, and
#    resets the snapshot on every frame the layer does not own and on
#    RETURN_TO_TITLE; except race frames under RL-8 (tickOnly), which leave
#    the snapshot holding the RACING view stored on the last owned frame, so
#    RESULTS entry keeps its SND-9 cue. The START_RACE branch (RL-S8b) hands
#    the launch to the race caller and does not reset it.
set(hook_path "game/MAIN/MainArcadeLink.c")
ctr_read_source("${hook_path}" hook)
foreach(mirror IN LISTS mirror_pairs)
    string(REPLACE " " ";" mirror_items "${mirror}")
    list(GET mirror_items 0 name)
    ctr_require_regex("${hook_path}" "${hook}"
        "_Static_assert\\(\\(uint32_t\\)MAIN_ARCADE_LINK_SOUND_${name} == \\(uint32_t\\)NATIVE_ARCADE_FLOW_${name},")
endforeach()
ctr_strip_comments("${hook}" hook_code)
string(REGEX MATCHALL "OtherFX" sound_calls "${hook_code}")
list(LENGTH sound_calls sound_call_count)
if(NOT sound_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link sound isolation: ${hook_path} must name OtherFX exactly once, in its one retail sound call (found ${sound_call_count})")
endif()
ctr_require_order("${hook_path}" "${hook_code}"
    "static void MainArcadeLink_PlayMenuSound("
    "MainArcadeLinkSound_InputFromHostView(view, hostMode, &soundInput)"
    "MainArcadeLinkSound_Decide(&s_mainArcadeLinkSound, &soundInput, enterPressed)"
    "MainArcadeLinkSound_RetailId(cue, &soundID)"
    "(void)OtherFX_Play(soundID, 1);"
    "static void MainArcadeLink_BuildAndDraw("
    "NativeArcadeLinkHost_GetView(&view)"
    "MainArcadeLink_PlayMenuSound(&view, hostMode, enterPressed);"
    "MainArcadeLinkLayout_InputFromHostView(&view, &input)")
foreach(call IN ITEMS "MainArcadeLinkSound_Decide\\(" "MainArcadeLink_PlayMenuSound\\(&view")
    string(REGEX MATCHALL "${call}" call_hits "${hook_code}")
    list(LENGTH call_hits call_hit_count)
    if(NOT call_hit_count EQUAL 1)
        message(FATAL_ERROR "arcade link sound isolation: ${hook_path} must call '${call}' exactly once (found ${call_hit_count})")
    endif()
endforeach()
string(REGEX MATCHALL "NativeArcadeLinkHost_GetView\\(" get_view_calls "${hook_code}")
list(LENGTH get_view_calls get_view_count)
if(NOT get_view_count EQUAL 1)
    message(FATAL_ERROR "arcade link sound isolation: ${hook_path} must call NativeArcadeLinkHost_GetView exactly once (the sound reuses the drawer's view; found ${get_view_count})")
endif()
string(FIND "${hook_code}" "if (output.owns == 0u)" not_owned_at)
if(not_owned_at EQUAL -1)
    message(FATAL_ERROR "arcade link sound isolation: ${hook_path} has no 'if (output.owns == 0u)' block")
endif()
string(SUBSTRING "${hook_code}" ${not_owned_at} -1 not_owned_tail)
string(FIND "${not_owned_tail}" "return 0;" not_owned_return)
string(SUBSTRING "${not_owned_tail}" 0 ${not_owned_return} not_owned_block)
ctr_require_literal("${hook_path} (not-owned frame)" "${not_owned_block}" "MainArcadeLinkSound_Reset(&s_mainArcadeLinkSound);")
# The START_RACE branch hands the launch to the race caller and does not
# reset (RL-S8b: no abort, the flow is on RACING), and the RETURN_TO_TITLE
# branch resets before its retail title request; each check reads only its
# own branch, so the other branch's reset cannot satisfy it.
set(start_race_head "if (action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)")
set(return_title_head "else if (action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE)")
ctr_require_order("${hook_path}" "${hook_code}" "${start_race_head}" "${return_title_head}")
string(FIND "${hook_code}" "${start_race_head}" start_race_at)
string(FIND "${hook_code}" "${return_title_head}" return_title_at)
math(EXPR start_race_length "${return_title_at} - ${start_race_at}")
string(SUBSTRING "${hook_code}" ${start_race_at} ${start_race_length} start_race_block)
ctr_require_literal("${hook_path} (START_RACE branch)" "${start_race_block}" "MainArcadeRaceLaunch_StartRace();")
string(FIND "${start_race_block}" "MainArcadeLinkSound_Reset" start_race_reset_at)
if(NOT start_race_reset_at EQUAL -1)
    message(FATAL_ERROR "arcade link sound isolation: the START_RACE branch of ${hook_path} must not reset the sound snapshot (RL-S8b: the flow is on RACING)")
endif()
string(SUBSTRING "${hook_code}" ${return_title_at} -1 return_title_tail)
set(title_request_head "MainArcadeLink_ReturnStep(gGT, 1u);")
ctr_require_order("${hook_path}" "${return_title_tail}" "${return_title_head}" "${title_request_head}")
string(FIND "${return_title_tail}" "${title_request_head}" title_request_at)
string(SUBSTRING "${return_title_tail}" 0 ${title_request_at} return_title_block)
ctr_require_literal("${hook_path} (RETURN_TO_TITLE branch, before the title request)" "${return_title_block}"
    "MainArcadeLinkSound_Reset(&s_mainArcadeLinkSound);")
ctr_require_literal("${hook_path}" "${hook_code}" "static struct MainArcadeLinkSoundState s_mainArcadeLinkSound;")

# 6. Sound stays in the thin hook: no arcade-link platform source or header,
#    and neither the policy nor the layout, names the retail sound call.
file(GLOB arcade_platform_paths
    "${repo}/platform/native_arcade_*.c" "${repo}/include/platform/native_arcade_*.h")
list(LENGTH arcade_platform_paths arcade_platform_count)
if(arcade_platform_count LESS 10)
    message(FATAL_ERROR "arcade link sound isolation: expected the platform/native_arcade_* sources and headers, found ${arcade_platform_count}; the scan is broken")
endif()
foreach(path IN LISTS arcade_platform_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" source)
    ctr_forbid("${relative_path}" "${source}" "OtherFX")
endforeach()
foreach(relative_path IN ITEMS
        "game/MAIN/MainArcadeLinkPolicy.c" "game/MAIN/MainArcadeLinkPolicy.h"
        "game/MAIN/MainArcadeLinkLayout.c" "game/MAIN/MainArcadeLinkLayout.h")
    ctr_read_source("${relative_path}" source)
    ctr_forbid("${relative_path}" "${source}" "OtherFX")
endforeach()

# 7. The unity chain includes the policy, then the sound decision, then the
#    hook.
ctr_read_source("game/game_unity.h" unity)
ctr_require_order("game/game_unity.h" "${unity}"
    "#include \"MAIN/MainArcadeLinkPolicy.c\"" "#include \"MAIN/MainArcadeLinkSound.c\"" "#include \"MAIN/MainArcadeLink.c\"")
