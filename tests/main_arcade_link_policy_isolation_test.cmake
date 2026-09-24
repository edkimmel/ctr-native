# Structural isolation for the arcade-link live hook decision policy
# (game/MAIN/MainArcadeLinkPolicy.{c,h}, docs/GAME_LOOP_UI_MILESTONE.md
# section 2.5, Task 6b-3): a pure decision function that never talks to the
# host glue, the netplay adapter, the link, or its state, reads no game
# global, draws nothing, uses no heap and no stdio, and never names the
# topology lease, replay, checkpoint, or canonical state. Its only includes
# are stdint.h, stddef.h, string.h, the menu-input header (for its button bit
# macros), and its own header; the library links nothing; ctr_native does not
# link it (the source is unity-included); the target stays portable C17 with
# extensions off; and its mirrored retail button bits, title states,
# menu-ready frame, and host modes stay in step with the retail and host
# headers (MainArcadeLink.c also static-asserts each one).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link policy isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link policy isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_literal relative_path source literal)
    string(FIND "${source}" "${literal}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "arcade link policy isolation: required text '${literal}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "arcade link policy isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

set(policy_header "game/MAIN/MainArcadeLinkPolicy.h")
set(policy_source "game/MAIN/MainArcadeLinkPolicy.c")
set(policy_files "${policy_header}" "${policy_source}")

# 1a. No host glue, netplay, link, or lobby dependency.
set(netplay_tokens
    NativeArcadeLinkHost_ NativeArcadeNetplay native_arcade_netplay
    NativeArcadeFlow_ lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport NativeUdp winsock WSA sockaddr)

# 1b. No game global, no retail header, and no rendering: the hook owns them.
set(game_tokens
    sdata gGT GameTracker GamepadSystem common.h MM_ D230 RectMenu RECTMENU DecalFont CTR_Box PrimMem OTMem)

# 1c. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 1d. No stdio.
set(stdio_tokens printf snprintf stdio.h Platform_Log)

# 1e. No topology-lease, replay, checkpoint, or canonical-state symbol.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile
    NativeReplay native_replay NativeCanonical native_canonical Checkpoint checkpoint)

foreach(relative_path IN LISTS policy_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS netplay_tokens game_tokens alloc_tokens stdio_tokens lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 2. #include lines may only name stdint.h, stddef.h, string.h, the
    #    menu-input header, or the policy's own header. No conditional
    #    compilation beyond the header guard.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"platform/native_arcade_menu_input\\.h\"|\"MAIN/MainArcadeLinkPolicy\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade link policy isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|elif|else)[ \t\r\n]" conditionals "${source}")
    if(NOT "${conditionals}" STREQUAL "")
        message(FATAL_ERROR "arcade link policy isolation: ${relative_path} must not use conditional compilation")
    endif()
endforeach()

# 2b. No mutable state: no file-scope static object in the source.
ctr_read_source("${policy_source}" source)
string(REGEX MATCH "(^|\n)static[ \t]+[^\r\n(]*(=|;)" static_object "${source}")
if(NOT "${static_object}" STREQUAL "")
    message(FATAL_ERROR "arcade link policy isolation: ${policy_source} must hold no file-scope static object (found '${static_object}')")
endif()

# 3a. ctr_native_arcade_link_policy links nothing.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_link_policy)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 0)
    message(FATAL_ERROR "arcade link policy isolation: ${target} must link nothing (found ${link_call_count} target_link_libraries call)")
endif()

# 3b. Declared once, from the policy source only, with include dirs include and game.
string(REGEX MATCHALL "add_library\\(${target} STATIC[^)]*\\)" declarations "${cmake}")
list(LENGTH declarations declaration_count)
if(NOT declaration_count EQUAL 1)
    message(FATAL_ERROR "arcade link policy isolation: expected exactly one add_library(${target} STATIC ...), found ${declaration_count}")
endif()
list(GET declarations 0 declaration)
if(NOT declaration STREQUAL "add_library(${target} STATIC game/MAIN/MainArcadeLinkPolicy.c)")
    message(FATAL_ERROR "arcade link policy isolation: ${target} must build game/MAIN/MainArcadeLinkPolicy.c alone (found '${declaration}')")
endif()
ctr_require_literal("CMakeLists.txt" "${cmake}"
    "target_include_directories(${target} PUBLIC \${CMAKE_SOURCE_DIR}/include \${CMAKE_SOURCE_DIR}/game)")

# 3c. C17, no extensions, on the policy target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade link policy isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade link policy isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade link policy isolation: ${target} C17/no-extensions properties are out of order")
endif()

# 3d. ctr_native does not link the policy library (the source is unity-included).
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
if(native_link_start EQUAL -1)
    message(FATAL_ERROR "arcade link policy isolation: missing target_link_libraries(ctr_native ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(FIND "${native_link_block}" "${target}" policy_hit)
if(NOT policy_hit EQUAL -1)
    message(FATAL_ERROR "arcade link policy isolation: ctr_native must not link ${target} (the policy is unity-included)")
endif()

# 4. The mirrored values stay in step with the retail and host headers.
ctr_read_source("include/namespace_Gamepad.h" gamepad)
ctr_read_source("include/ovr_230.h" ovr230)
ctr_read_source("include/platform/native_arcade_link_host.h" host)
foreach(literal
        "BTN_UP = 0x1," "BTN_DOWN = 0x2," "BTN_LEFT = 0x4," "BTN_RIGHT = 0x8," "BTN_CROSS_one = 0x10,"
        "BTN_SQUARE_one = 0x20," "BTN_CIRCLE = 0x40," "BTN_R1 = 0x400," "BTN_START = 0x1000,"
        "BTN_SELECT = 0x2000," "BTN_TRIANGLE = 0x40000")
    ctr_require_literal("include/namespace_Gamepad.h" "${gamepad}" "${literal}")
endforeach()
foreach(literal
        "TITLE_MENU_STATE_INTRO = 0," "TITLE_MENU_STATE_IN_MENU = 1," "TITLE_MENU_STATE_EXITING = 2,"
        "TITLE_MENU_STATE_RETURNING = 3," "TITLE_INTRO_MENU_READY_FRAME = 230,")
    ctr_require_literal("include/ovr_230.h" "${ovr230}" "${literal}")
endforeach()
foreach(literal
        "NATIVE_ARCADE_LINK_HOST_MODE_OFF = 0," "NATIVE_ARCADE_LINK_HOST_MODE_LINK = 1,"
        "NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW = 2")
    ctr_require_literal("include/platform/native_arcade_link_host.h" "${host}" "${literal}")
endforeach()

ctr_read_source("${policy_header}" header)
foreach(mirror
        "MODE_OFF 0u" "MODE_LINK 1u" "MODE_PREVIEW 2u"
        "TITLE_INTRO 0u" "TITLE_IN_MENU 1u" "TITLE_EXITING 2u" "TITLE_RETURNING 3u"
        "MENU_READY_FRAME 230"
        "BTN_UP 0x1u" "BTN_DOWN 0x2u" "BTN_LEFT 0x4u" "BTN_RIGHT 0x8u" "BTN_CROSS_ONE 0x10u"
        "BTN_SQUARE_ONE 0x20u" "BTN_CIRCLE 0x40u" "BTN_R1 0x400u" "BTN_START 0x1000u"
        "BTN_SELECT 0x2000u" "BTN_TRIANGLE 0x40000u")
    ctr_require_regex("${policy_header}" "${header}" "(^|\n)#define MAIN_ARCADE_LINK_POLICY_${mirror}\r?\n")
endforeach()

# 5. The hook static-asserts every mirror against the retail or host value.
set(hook_path "game/MAIN/MainArcadeLink.c")
ctr_read_source("${hook_path}" hook)
foreach(pair
        "MODE_OFF NATIVE_ARCADE_LINK_HOST_MODE_OFF" "MODE_LINK NATIVE_ARCADE_LINK_HOST_MODE_LINK"
        "MODE_PREVIEW NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW"
        "TITLE_INTRO TITLE_MENU_STATE_INTRO" "TITLE_IN_MENU TITLE_MENU_STATE_IN_MENU"
        "TITLE_EXITING TITLE_MENU_STATE_EXITING" "TITLE_RETURNING TITLE_MENU_STATE_RETURNING"
        "MENU_READY_FRAME TITLE_INTRO_MENU_READY_FRAME"
        "BTN_UP BTN_UP" "BTN_DOWN BTN_DOWN" "BTN_LEFT BTN_LEFT" "BTN_RIGHT BTN_RIGHT"
        "BTN_CROSS_ONE BTN_CROSS_one" "BTN_SQUARE_ONE BTN_SQUARE_one" "BTN_CIRCLE BTN_CIRCLE"
        "BTN_R1 BTN_R1" "BTN_START BTN_START" "BTN_SELECT BTN_SELECT" "BTN_TRIANGLE BTN_TRIANGLE")
    string(REPLACE " " ";" pair_items "${pair}")
    list(GET pair_items 0 mirror)
    list(GET pair_items 1 retail)
    ctr_require_regex("${hook_path}" "${hook}"
        "_Static_assert\\(\\((int|uint32_t)\\)MAIN_ARCADE_LINK_POLICY_${mirror} == \\((int|uint32_t)\\)${retail},")
endforeach()

# 6. The unity chain includes the policy after the layout and before the hook.
ctr_read_source("game/game_unity.h" unity)
string(FIND "${unity}" "#include \"MAIN/MainArcadeLinkLayout.c\"" layout_at)
string(FIND "${unity}" "#include \"MAIN/MainArcadeLinkPolicy.c\"" policy_at)
string(FIND "${unity}" "#include \"MAIN/MainArcadeLink.c\"" hook_at)
if(layout_at EQUAL -1 OR policy_at EQUAL -1 OR hook_at EQUAL -1 OR NOT (layout_at LESS policy_at AND policy_at LESS hook_at))
    message(FATAL_ERROR "arcade link policy isolation: game/game_unity.h must include the layout, then the policy, then the hook")
endif()

# 7. The return step's load-stage classes (race-launch risk 10) are the
#    policy's own values, and the hook maps the retail stages to them (its
#    mapping is pinned by main_arcade_link_hook_isolation_test.cmake 16f2).
foreach(stage_class "STAGE_IDLE 0u" "STAGE_REQUESTED 1u" "STAGE_OTHER 2u")
    ctr_require_regex("${policy_header}" "${header}" "(^|\n)#define MAIN_ARCADE_LINK_POLICY_${stage_class}\r?\n")
endforeach()
ctr_require_literal("${policy_header}" "${header}"
    "int MainArcadeLinkPolicy_ReturnStep(uint8_t *pending, uint8_t returnAction, uint32_t loadingStage, uint8_t onMainMenuLevel);")
