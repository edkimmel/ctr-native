# Structural isolation for the arcade-link live hook
# (game/MAIN/MainArcadeLink.{c,h}, docs/GAME_LOOP_UI_MILESTONE.md section 2.5,
# Tasks 6b-2 and 6b-3): the hook is native only and dormant by default. Its
# whole body sits inside one #if defined(CTR_NATIVE) block and its host-mode
# OFF check runs before anything else; it talks to the link only through the
# host glue API (never the adapter, lobby, failure-handling, or link
# modules), names no replay, canonical-state, or topology-lease token, logs
# only through Platform_Log (no stdio), and includes only its allowed headers;
# the seven retail-mirror static asserts are present; the
# MainFrame_RenderFrame.c call sits inside a CTR_NATIVE guard and the input
# clear sits inside the retail menu-input collect block; the unity chain
# includes the layout, the policy, and the hook after the 230 overlay; main.c
# parses the options, reads the identity only inside the link-enabled branch,
# rejects link or preview mode combined with a replay option, and configures
# and shuts the host down; and ctr_native links the host glue but not the
# layout library (the layout is unity-included). The policy's own rules are
# in main_arcade_link_policy_isolation_test.cmake.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link hook isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_literal relative_path source literal)
    string(FIND "${source}" "${literal}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: required text '${literal}' missing from ${relative_path}")
    endif()
endfunction()

# Fails unless every term appears in source, each one found after the end of
# the previous one (earlier occurrences are skipped).
function(ctr_require_order relative_path source)
    set(remaining "${source}")
    foreach(term IN LISTS ARGN)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "arcade link hook isolation: '${term}' is missing or out of order in ${relative_path}")
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

# Fails unless the first occurrence of call in source is inside an
# #if defined(CTR_NATIVE) block that has not been closed before it.
function(ctr_require_native_guard relative_path source call)
    string(FIND "${source}" "${call}" call_at)
    if(call_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: required text '${call}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" 0 ${call_at} before_call)
    string(FIND "${before_call}" "#if defined(CTR_NATIVE)" guard_at REVERSE)
    if(guard_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: '${call}' in ${relative_path} is not inside #if defined(CTR_NATIVE)")
    endif()
    string(SUBSTRING "${before_call}" ${guard_at} -1 guarded)
    foreach(closer IN ITEMS "#endif" "#else" "#elif")
        string(FIND "${guarded}" "${closer}" closer_at)
        if(NOT closer_at EQUAL -1)
            message(FATAL_ERROR "arcade link hook isolation: '${call}' in ${relative_path} is not inside #if defined(CTR_NATIVE)")
        endif()
    endforeach()
endfunction()

# Finds the first occurrence of opener in source, then the first '{' after
# it, and sets out_begin and out_end to the offsets of that '{' and of its
# matching '}' (braces counted; the sources checked here have no brace in a
# string or character literal inside the blocks).
function(ctr_find_block relative_path source opener out_begin out_end)
    string(FIND "${source}" "${opener}" opener_at)
    if(opener_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: required text '${opener}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" ${opener_at} -1 tail)
    string(FIND "${tail}" "{" brace_offset)
    if(brace_offset EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: no block follows '${opener}' in ${relative_path}")
    endif()
    math(EXPR begin "${opener_at} + ${brace_offset}")
    string(LENGTH "${source}" length)
    set(depth 0)
    set(position ${begin})
    while(position LESS length)
        string(SUBSTRING "${source}" ${position} 1 character)
        if(character STREQUAL "{")
            math(EXPR depth "${depth} + 1")
        elseif(character STREQUAL "}")
            math(EXPR depth "${depth} - 1")
            if(depth EQUAL 0)
                set(${out_begin} ${begin} PARENT_SCOPE)
                set(${out_end} ${position} PARENT_SCOPE)
                return()
            endif()
        endif()
        math(EXPR position "${position} + 1")
    endwhile()
    message(FATAL_ERROR "arcade link hook isolation: unbalanced block after '${opener}' in ${relative_path}")
endfunction()

set(hook_source_path "game/MAIN/MainArcadeLink.c")
set(hook_header_path "game/MAIN/MainArcadeLink.h")
ctr_read_source("${hook_source_path}" hook_source)
ctr_read_source("${hook_header_path}" hook_header)

# 1. The hook's whole body is one #if defined(CTR_NATIVE) block: nothing but
#    comments before it, nothing but whitespace after its #endif, and no
#    other conditional inside it.
string(REGEX MATCHALL "#[ \t]*if" if_directives "${hook_source}")
string(REGEX MATCHALL "#[ \t]*endif" endif_directives "${hook_source}")
string(REGEX MATCHALL "#[ \t]*(else|elif)" else_directives "${hook_source}")
list(LENGTH if_directives if_count)
list(LENGTH endif_directives endif_count)
list(LENGTH else_directives else_count)
if(NOT if_count EQUAL 1 OR NOT endif_count EQUAL 1 OR NOT else_count EQUAL 0)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must hold exactly one #if/#endif block and no #else/#elif (found ${if_count} #if, ${endif_count} #endif, ${else_count} #else/#elif)")
endif()
string(FIND "${hook_source}" "#if defined(CTR_NATIVE)" guard_at)
if(guard_at EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must open with #if defined(CTR_NATIVE)")
endif()
string(SUBSTRING "${hook_source}" 0 ${guard_at} before_guard)
ctr_strip_comments("${before_guard}" before_guard)
if(NOT before_guard MATCHES "^[ \t\r\n]*$")
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} has code before #if defined(CTR_NATIVE)")
endif()
if(NOT hook_source MATCHES "#endif[ \t\r\n]*$")
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must end with the #endif of its CTR_NATIVE block")
endif()

# 2. Link access only through the host glue API: no adapter, lobby,
#    failure-handling, or transport name, and no host configure/shutdown,
#    option, or identity call (main.c owns those). No replay, canonical-state,
#    checkpoint, or topology-lease token. No heap use.
set(forbidden_tokens
    NativeArcadeNetplay_ native_arcade_netplay NativeArcadeFlow_ native_arcade_flow.h
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport NativeUdp winsock WSA sockaddr
    NativeArcadeLinkHost_Configure NativeArcadeLinkHost_Shutdown NativeArcadeLinkOptions_ NativeIdentity_
    NativeReplay native_replay NativeCanonical native_canonical Checkpoint checkpoint
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile
    malloc calloc realloc "free(" alloca
    printf fflush stdout)
foreach(relative_path IN ITEMS "${hook_source_path}" "${hook_header_path}")
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS forbidden_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 3. Includes: the hook source may include only its allowed headers; the
#    prototype header includes nothing.
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${hook_source}")
foreach(include_line IN LISTS include_lines)
    if(NOT include_line MATCHES "^#[ \t]*include[ \t]*[<\"](common\\.h|platform/native_arcade_link_host\\.h|platform/native_arcade_menu_input\\.h|platform/native_log\\.h|MAIN/MainArcadeLinkLayout\\.h|MAIN/MainArcadeLinkPolicy\\.h|MAIN/MainArcadeLink\\.h)[>\"][ \t]*$")
        message(FATAL_ERROR "arcade link hook isolation: disallowed include '${include_line}' in ${hook_source_path}")
    endif()
endforeach()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" header_includes "${hook_header}")
if(NOT "${header_includes}" STREQUAL "")
    message(FATAL_ERROR "arcade link hook isolation: ${hook_header_path} must include nothing (found '${header_includes}')")
endif()

# 4. The seven retail-mirror static asserts.
foreach(pair
        "FONT_BIG FONT_BIG" "FONT_SMALL FONT_SMALL" "COLOR_ORANGE ORANGE" "COLOR_RED RED"
        "COLOR_WHITE WHITE" "COLOR_GRAY GRAY" "JUSTIFY_CENTER JUSTIFY_CENTER")
    string(REPLACE " " ";" pair_items "${pair}")
    list(GET pair_items 0 mirror)
    list(GET pair_items 1 retail)
    ctr_require_literal("${hook_source_path}" "${hook_source}"
        "_Static_assert((int)MAIN_ARCADE_LINK_${mirror} == ${retail},")
endforeach()

# 5. Dormant by default: in MainArcadeLink_Frame nothing but plain
#    declarations precede the host-mode OFF check, which returns 0.
set(frame_signature "int MainArcadeLink_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)")
ctr_require_literal("${hook_header_path}" "${hook_header}" "${frame_signature};")
string(FIND "${hook_source}" "${frame_signature}" frame_at)
if(frame_at EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must define ${frame_signature}")
endif()
string(SUBSTRING "${hook_source}" ${frame_at} -1 frame_tail)
string(FIND "${frame_tail}" "{" body_at)
math(EXPR body_at "${body_at} + 1")
string(SUBSTRING "${frame_tail}" ${body_at} -1 frame_body)
set(off_check "if (NativeArcadeLinkHost_Mode() == (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)")
string(FIND "${frame_body}" "${off_check}" off_at)
if(off_at EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: MainArcadeLink_Frame must start with '${off_check}'")
endif()
string(SUBSTRING "${frame_body}" 0 ${off_at} before_off)
ctr_strip_comments("${before_off}" before_off)
if(NOT before_off MATCHES "^([ \t\r\n]*(uint32_t|struct[ \t]+[A-Za-z_][A-Za-z0-9_]*)[ \t]+[A-Za-z_][A-Za-z0-9_]*;)*[ \t\r\n]*$")
    message(FATAL_ERROR "arcade link hook isolation: only plain declarations may precede the host-mode OFF check in MainArcadeLink_Frame")
endif()
string(SUBSTRING "${frame_body}" ${off_at} 200 off_block)
if(NOT off_block MATCHES "^if \\(NativeArcadeLinkHost_Mode\\(\\) == \\(uint32_t\\)NATIVE_ARCADE_LINK_HOST_MODE_OFF\\)[ \t\r\n]*\\{[ \t\r\n]*return 0;")
    message(FATAL_ERROR "arcade link hook isolation: the host-mode OFF check in MainArcadeLink_Frame must return 0 immediately")
endif()

# 6. The MainFrame_RenderFrame.c call, its input clear, and the prototype
#    include each sit inside a CTR_NATIVE guard.
set(render_path "game/MAIN/MainFrame_RenderFrame.c")
ctr_read_source("${render_path}" render_source)
ctr_require_native_guard("${render_path}" "${render_source}" "#include \"MAIN/MainArcadeLink.h\"")
ctr_require_native_guard("${render_path}" "${render_source}" "MainArcadeLink_Frame(gGT, gGamepads)")
ctr_require_native_guard("${render_path}" "${render_source}" "if (arcadeLinkOwnsMenu != 0)")
ctr_require_order("${render_path}" "${render_source}"
    "MainArcadeLink_Frame(gGT, gGamepads)" "RECTMENU_CollectInput()" "if (arcadeLinkOwnsMenu != 0)"
    "RECTMENU_ClearInput()" "RECTMENU_ProcessState()")

# 6b. The input clear sits inside the retail if-block that collects the menu
#     input, after the collect, so it clears exactly what was collected.
ctr_strip_comments("${render_source}" render_code)
ctr_find_block("${render_path}" "${render_code}"
    "if ((sdata->ptrActiveMenu != 0) || ((gGT->gameMode1 & END_OF_RACE) != 0))" collect_begin collect_end)
math(EXPR collect_length "${collect_end} - ${collect_begin} + 1")
string(SUBSTRING "${render_code}" ${collect_begin} ${collect_length} collect_block)
ctr_require_order("${render_path} (menu-input collect block)" "${collect_block}"
    "RECTMENU_CollectInput();" "if (arcadeLinkOwnsMenu != 0)" "RECTMENU_ClearInput();")

# 7. The unity chain includes the layout, the policy, and then the hook,
#    after the 230 overlay (the hook reads its title state) and before the
#    231 overlay.
set(unity_path "game/game_unity.h")
ctr_read_source("${unity_path}" unity_source)
ctr_require_order("${unity_path}" "${unity_source}"
    "#include \"230.c\"" "#include \"MAIN/MainArcadeLinkLayout.c\"" "#include \"MAIN/MainArcadeLinkPolicy.c\""
    "#include \"MAIN/MainArcadeLink.c\"" "#include \"231/R231.c\"")

# 8. main.c parses the options, configures the host, and shuts it down before
#    Platform_Shutdown once the game loop returns.
ctr_read_source("main.c" main_source)
ctr_require_order("main.c" "${main_source}"
    "NativeArcadeLinkOptions_ApplyArgs(argc, argv, &arcadeLinkOptions)"
    "NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr)"
    "atexit(NativeArcadeLinkHost_Shutdown)"
    "CTR_Main()"
    "NativeArcadeLinkHost_Shutdown()"
    "Platform_Shutdown()")

# 8b. main.c reads the identity only inside the link-enabled branch: exactly
#     one NativeIdentity_Get call, inside the block of the first
#     `if (arcadeLinkOptions.enabled != 0u)`.
ctr_strip_comments("${main_source}" main_code)
string(REGEX MATCHALL "NativeIdentity_Get\\(" identity_calls "${main_code}")
list(LENGTH identity_calls identity_call_count)
if(NOT identity_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: main.c must call NativeIdentity_Get exactly once (found ${identity_call_count})")
endif()
string(FIND "${main_code}" "NativeIdentity_Get(" identity_at)
ctr_find_block("main.c" "${main_code}" "if (arcadeLinkOptions.enabled != 0u)" enabled_begin enabled_end)
if(NOT (identity_at GREATER enabled_begin AND identity_at LESS enabled_end))
    message(FATAL_ERROR "arcade link hook isolation: main.c must call NativeIdentity_Get only inside its if (arcadeLinkOptions.enabled != 0u) block")
endif()

# 8c. main.c rejects link or preview mode combined with any replay record,
#     playback, or report option, by name, before the replay parsers run
#     (so nothing is created first) and before the host is configured.
foreach(option IN ITEMS --record --record-v2 --record-v3 --replay --replay-v2 --replay-v3
        --replay-bypass-header --toggle --detailed)
    ctr_require_literal("main.c" "${main_code}" "\"${option}\"")
endforeach()
set(replay_reject "if (((arcadeLinkOptions.enabled != 0u) || (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE)) && NativeArg_NamesReplayOption(argc, argv))")
ctr_find_block("main.c" "${main_code}" "${replay_reject}" reject_begin reject_end)
math(EXPR reject_length "${reject_end} - ${reject_begin} + 1")
string(SUBSTRING "${main_code}" ${reject_begin} ${reject_length} reject_block)
ctr_require_order("main.c (replay rejection)" "${reject_block}"
    "[CTR Native] --arcade-link and --arcade-link-preview cannot be combined with replay record or playback options."
    "return NativeConsole_Return(1);")
ctr_require_order("main.c" "${main_code}"
    "NativeArcadeLinkOptions_ApplyArgs(argc, argv, &arcadeLinkOptions)"
    "${replay_reject}"
    "NativeReplayScheduler_PrepareReportFromArgs(argc, argv)"
    "NativeReplayScheduler_ConfigureFromArgs(argc, argv)"
    "NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr)")

# 9. ctr_native links the host glue and not the layout library.
ctr_read_source("CMakeLists.txt" cmake)
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
if(native_link_start EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: missing target_link_libraries(ctr_native ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(REGEX MATCH "[ \t\r\n]ctr_native_arcade_link_host([ \t\r\n]|$)" host_hit "${native_link_block}")
if("${host_hit}" STREQUAL "")
    message(FATAL_ERROR "arcade link hook isolation: ctr_native must link ctr_native_arcade_link_host")
endif()
string(FIND "${native_link_block}" "ctr_native_arcade_link_layout" layout_hit)
if(NOT layout_hit EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: ctr_native must not link ctr_native_arcade_link_layout (the layout is unity-included)")
endif()
