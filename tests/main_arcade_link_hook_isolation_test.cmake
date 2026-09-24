# Structural isolation for the arcade-link live hook
# (game/MAIN/MainArcadeLink.{c,h}, docs/GAME_LOOP_UI_MILESTONE.md section 2.5,
# Tasks 6b-2, 6b-3, and 6b-4): the hook is native only and dormant by default. Its
# whole body sits inside one #if defined(CTR_NATIVE) block and its host-mode
# OFF check runs before anything else; it talks to the link only through the
# host glue API (never the adapter, lobby, failure-handling, or link
# modules), names no replay, canonical-state, or topology-lease token, logs
# only through Platform_Log (no stdio), and includes only its allowed headers;
# the eleven retail-mirror static asserts are present; the
# MainFrame_RenderFrame.c call sits inside a CTR_NATIVE guard and the input
# clear sits inside the retail menu-input collect block; the unity chain
# includes the layout, the policy, and the hook after the 230 overlay; main.c
# parses the options, reads the identity only inside the link-enabled branch,
# rejects link or preview mode combined with any replay option the replay
# scheduler parses, and configures and shuts the host down; ctr_native links
# the host glue but not the layout library (the layout is unity-included);
# the F5 and F8 quick-state hotkeys in native_platform.c are gated off in
# link and preview mode and no other source requests a quick state; and the
# START_RACE abort gives the retail box back when the host falls back to mode
# OFF. Since MS-8 the START_RACE branch also logs the agreed match before
# the abort, and main.c fills the host-local select entropy only inside the
# link-enabled branch. Since MS-8b no MainArcadeLink* game file names a
# match-config slot role, the match-config struct, or a match-select value:
# they use the host's own names. Since MS-9 the drawer static-asserts the
# four player-colour mirrors. Since MS-10b the drawer maps the host view into
# the layout input only through the layout's
# MainArcadeLinkLayout_InputFromHostView (whose header now holds the select
# value static asserts). The policy's own rules are in
# main_arcade_link_policy_isolation_test.cmake. Since the menu sounds
# (section 3.1) the hook may also include MAIN/MainArcadeLinkSound.h, and the
# drawer takes the host mode and the policy's enterPressed for the sound
# decision; the sound rules, including the one retail sound call, are in
# main_arcade_link_sound_isolation_test.cmake. Since RL-S8a
# (docs/RACE_LAUNCH_MILESTONE.md RL-8) the gather reads the host racing query
# and MainArcadeLink_Frame's tickOnly branch, right after the decision, only
# ticks the link and returns 0. Since RL-S8b the START_RACE branch no longer
# aborts to the title (so the hook names AbortToTitle nowhere, and the old
# fall-back-to-OFF box restore it needed is gone): it logs the agreed match
# and hands the launch to the live race caller
# (game/MAIN/MainArcadeRaceLaunch.{c,h}), whose finish report is the host
# tick's raceFinished input; MainFrame_RenderFrame steps the caller once per
# frame right after the hook; and section 16 pins the caller: dormant by
# default, its own post-tick racing read, the RL-10 pad install and clear
# frame, its LeaveTitle identical to the roster proof's, the RL-12 line, and
# its step and apply order.

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
    if(NOT include_line MATCHES "^#[ \t]*include[ \t]*[<\"](common\\.h|platform/native_arcade_link_host\\.h|platform/native_arcade_menu_input\\.h|platform/native_log\\.h|MAIN/MainArcadeLinkLayout\\.h|MAIN/MainArcadeLinkPolicy\\.h|MAIN/MainArcadeLinkSound\\.h|MAIN/MainArcadeLink\\.h|MAIN/MainArcadeRaceLaunch\\.h)[>\"][ \t]*$")
        message(FATAL_ERROR "arcade link hook isolation: disallowed include '${include_line}' in ${hook_source_path}")
    endif()
endforeach()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" header_includes "${hook_header}")
if(NOT "${header_includes}" STREQUAL "")
    message(FATAL_ERROR "arcade link hook isolation: ${hook_header_path} must include nothing (found '${header_includes}')")
endif()

# 4. The eleven retail-mirror static asserts (the four player colours since
#    MS-9).
foreach(pair
        "FONT_BIG FONT_BIG" "FONT_SMALL FONT_SMALL" "COLOR_ORANGE ORANGE" "COLOR_RED RED"
        "COLOR_WHITE WHITE" "COLOR_GRAY GRAY" "JUSTIFY_CENTER JUSTIFY_CENTER"
        "COLOR_PLAYER_BLUE PLAYER_BLUE" "COLOR_PLAYER_RED PLAYER_RED" "COLOR_PLAYER_GREEN PLAYER_GREEN"
        "COLOR_PLAYER_YELLOW PLAYER_YELLOW")
    string(REPLACE " " ";" pair_items "${pair}")
    list(GET pair_items 0 mirror)
    list(GET pair_items 1 retail)
    ctr_require_literal("${hook_source_path}" "${hook_source}"
        "_Static_assert((int)MAIN_ARCADE_LINK_${mirror} == ${retail},")
endforeach()

# 4b. The drawer turns the host view into the layout input only through the
#     layout's own mapping (MS-10b, MainArcadeLinkLayout_InputFromHostView,
#     which tests/main_arcade_link_view_layout_test.c proves every host view
#     passes MainArcadeLinkLayout_Build through; the layout header
#     static-asserts the mapped select values against the host names, see
#     main_arcade_link_layout_isolation_test.cmake): in
#     MainArcadeLink_BuildAndDraw, GetView, then the mapping, then Build, and
#     the hook keeps no copy of its own (no MapSelect helper, no write to a
#     layout input field).
ctr_strip_comments("${hook_source}" hook_code)
ctr_find_block("${hook_source_path}" "${hook_code}"
    "static void MainArcadeLink_BuildAndDraw(struct GameTracker *gGT, uint32_t hostMode, uint8_t enterPressed)" draw_begin draw_end)
math(EXPR draw_length "${draw_end} - ${draw_begin} + 1")
string(SUBSTRING "${hook_code}" ${draw_begin} ${draw_length} draw_block)
ctr_require_order("${hook_source_path} (MainArcadeLink_BuildAndDraw)" "${draw_block}"
    "NativeArcadeLinkHost_GetView(&view)"
    "MainArcadeLinkLayout_InputFromHostView(&view, &input)"
    "MainArcadeLinkLayout_Build(&input, &layout)"
    "MainArcadeLink_Draw(gGT, &layout);")
string(REGEX MATCHALL "MainArcadeLinkLayout_InputFromHostView\\(" mapping_calls "${hook_code}")
list(LENGTH mapping_calls mapping_call_count)
if(NOT mapping_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must call MainArcadeLinkLayout_InputFromHostView exactly once (found ${mapping_call_count})")
endif()
ctr_forbid("${hook_source_path}" "${hook_code}" "MapSelect")
if(draw_block MATCHES "input\\.[^ \t\r\n=;]+[ \t]*=[^=]")
    message(FATAL_ERROR "arcade link hook isolation: MainArcadeLink_BuildAndDraw writes a layout input field ('${CMAKE_MATCH_0}'); map the host view only through MainArcadeLinkLayout_InputFromHostView")
endif()

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

# 5b. Race frames are ticked, not owned (docs/RACE_LAUNCH_MILESTONE.md RL-8,
#     RL-S8a). The gather reads the host racing query into the policy's
#     hostRacing (the only NativeArcadeLinkHost_Racing call, so it is read
#     where the gather runs, before this frame's host tick). In
#     MainArcadeLink_Frame the tickOnly branch comes after the host-mode OFF
#     check, the gather, and the decision, and before any box restore, tap
#     clear, box hide, host tick of an owned frame, demo countdown reset, or
#     draw; only whitespace (in the comment-stripped code) separates the
#     decision-failure block's closing brace from the branch, so nothing
#     (a snapshot reset, a menu call, a pad write) can run in between; its
#     block is exactly the link tick and return 0.
ctr_find_block("${hook_source_path}" "${hook_code}"
    "static void MainArcadeLink_Gather(" gather_begin gather_end)
math(EXPR gather_length "${gather_end} - ${gather_begin} + 1")
string(SUBSTRING "${hook_code}" ${gather_begin} ${gather_length} gather_block)
ctr_require_literal("${hook_source_path} (MainArcadeLink_Gather)" "${gather_block}"
    "input->hostRacing = (NativeArcadeLinkHost_Racing() != 0u) ? 1u : 0u;")
string(REGEX MATCHALL "NativeArcadeLinkHost_Racing\\(" racing_calls "${hook_code}")
list(LENGTH racing_calls racing_call_count)
if(NOT racing_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must call NativeArcadeLinkHost_Racing exactly once, in MainArcadeLink_Gather (found ${racing_call_count})")
endif()
ctr_find_block("${hook_source_path}" "${hook_code}" "${frame_signature}" frame_begin frame_end)
math(EXPR frame_length "${frame_end} - ${frame_begin} + 1")
string(SUBSTRING "${hook_code}" ${frame_begin} ${frame_length} frame_block)
set(tick_only_check "if (output.tickOnly != 0u)")
ctr_require_order("${hook_source_path} (MainArcadeLink_Frame)" "${frame_block}"
    "${off_check}" "MainArcadeLink_Gather(gGT, gGS, &input);" "MainArcadeLinkPolicy_Decide(&input, &output)"
    "${tick_only_check}" "if (output.restoreBox != 0u)" "MainArcadeLink_RestoreMainMenu();"
    "MainArcadeLink_ClearTaps(gGS);" "MainArcadeLink_HideMainMenu();" "MainArcadeLink_LinkTick(gGT, &output);"
    "gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;" "MainArcadeLink_BuildAndDraw(")
string(FIND "${frame_block}" "${tick_only_check}" tick_only_at)
string(SUBSTRING "${frame_block}" 0 ${tick_only_at} before_tick_only)
foreach(term IN ITEMS "MainArcadeLink_RestoreMainMenu" "MainArcadeLink_ClearTaps" "MainArcadeLink_HideMainMenu"
        "MainArcadeLink_LinkTick" "NativeArcadeLinkHost_Tick" "NativeArcadeLinkHost_Enter" "demoCountdownTimer"
        "MainArcadeLink_BuildAndDraw" "buttonsTapped" "anyoneTapped" "INVISIBLE")
    string(FIND "${before_tick_only}" "${term}" early_at)
    if(NOT early_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: '${term}' runs before the tickOnly branch in MainArcadeLink_Frame")
    endif()
endforeach()
ctr_find_block("${hook_source_path} (MainArcadeLink_Frame)" "${frame_block}"
    "if (!MainArcadeLinkPolicy_Decide(&input, &output))" decide_fail_begin decide_fail_end)
math(EXPR after_decide_fail "${decide_fail_end} + 1")
if(after_decide_fail GREATER tick_only_at)
    message(FATAL_ERROR "arcade link hook isolation: the tickOnly branch in MainArcadeLink_Frame must follow the decision-failure block")
endif()
math(EXPR between_length "${tick_only_at} - ${after_decide_fail}")
string(SUBSTRING "${frame_block}" ${after_decide_fail} ${between_length} decide_to_tick_only)
if(NOT decide_to_tick_only MATCHES "^[ \t\r\n]*$")
    message(FATAL_ERROR "arcade link hook isolation: only whitespace may separate the decision-failure block from the tickOnly branch in MainArcadeLink_Frame (found '${decide_to_tick_only}')")
endif()
string(REGEX MATCHALL "output\\.tickOnly" tick_only_reads "${hook_code}")
list(LENGTH tick_only_reads tick_only_read_count)
if(NOT tick_only_read_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must read output.tickOnly exactly once, in the MainArcadeLink_Frame branch (found ${tick_only_read_count})")
endif()
ctr_find_block("${hook_source_path} (MainArcadeLink_Frame)" "${frame_block}" "${tick_only_check}" tick_begin tick_end)
math(EXPR tick_length "${tick_end} - ${tick_begin} + 1")
string(SUBSTRING "${frame_block}" ${tick_begin} ${tick_length} tick_block)
if(NOT tick_block MATCHES "^\\{[ \t\r\n]*MainArcadeLink_LinkTick\\(gGT, &output\\);[ \t\r\n]*return 0;[ \t\r\n]*\\}$")
    message(FATAL_ERROR "arcade link hook isolation: the tickOnly branch in MainArcadeLink_Frame must run MainArcadeLink_LinkTick(gGT, &output) and return 0, and nothing else (found '${tick_block}')")
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
# 6a. The race caller (RL-S8b) is stepped exactly once per frame, right after
#     the hook (so after this frame's host tick, on owned, ticked, and other
#     frames alike), inside the same CTR_NATIVE block, before the retail
#     menu-input collect; its prototype include is guarded too.
ctr_require_native_guard("${render_path}" "${render_source}" "#include \"MAIN/MainArcadeRaceLaunch.h\"")
ctr_require_native_guard("${render_path}" "${render_source}" "MainArcadeRaceLaunch_Frame(gGT, gGamepads)")
ctr_strip_comments("${render_source}" render_code_6a)
string(REGEX MATCHALL "MainArcadeRaceLaunch_Frame\\(" launch_frame_calls "${render_code_6a}")
list(LENGTH launch_frame_calls launch_frame_call_count)
if(NOT launch_frame_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${render_path} must call MainArcadeRaceLaunch_Frame exactly once (found ${launch_frame_call_count})")
endif()
if(NOT render_code_6a MATCHES "const int arcadeLinkOwnsMenu = MainArcadeLink_Frame\\(gGT, gGamepads\\);[ \t\r\n]*MainArcadeRaceLaunch_Frame\\(gGT, gGamepads\\);[ \t\r\n]*#endif")
    message(FATAL_ERROR "arcade link hook isolation: ${render_path} must call MainArcadeRaceLaunch_Frame(gGT, gGamepads) right after MainArcadeLink_Frame, alone, before the #endif of the same CTR_NATIVE block")
endif()
ctr_require_order("${render_path}" "${render_code_6a}"
    "MainArcadeLink_Frame(gGT, gGamepads)" "MainArcadeRaceLaunch_Frame(gGT, gGamepads)" "RECTMENU_CollectInput()")

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

# 8b. main.c reads the identity only inside the link-enabled branch: the
#     first NativeIdentity_Get call is inside the block of the first
#     `if (arcadeLinkOptions.enabled != 0u)`. Since R-5b the internal roster
#     proof (which main.c rejects together with link or preview mode) makes
#     exactly one more, inside the block of the first
#     `if (rosterProofOptions.enabled != 0u)` after the host is configured;
#     there is no other.
ctr_strip_comments("${main_source}" main_code)
string(REGEX MATCHALL "NativeIdentity_Get\\(" identity_calls "${main_code}")
list(LENGTH identity_calls identity_call_count)
if(NOT identity_call_count EQUAL 2)
    message(FATAL_ERROR "arcade link hook isolation: main.c must call NativeIdentity_Get exactly twice, once for the link host and once for the roster proof (found ${identity_call_count})")
endif()
string(FIND "${main_code}" "NativeIdentity_Get(" identity_at)
ctr_find_block("main.c" "${main_code}" "if (arcadeLinkOptions.enabled != 0u)" enabled_begin enabled_end)
if(NOT (identity_at GREATER enabled_begin AND identity_at LESS enabled_end))
    message(FATAL_ERROR "arcade link hook isolation: main.c must call NativeIdentity_Get only inside its if (arcadeLinkOptions.enabled != 0u) block")
endif()
string(FIND "${main_code}" "NativeIdentity_Get(" identity_last_at REVERSE)
string(FIND "${main_code}" "NativeArcadeLinkHost_Configure(" host_configure_at)
string(SUBSTRING "${main_code}" ${host_configure_at} -1 after_host_configure)
ctr_find_block("main.c (after the host configure)" "${after_host_configure}"
    "if (rosterProofOptions.enabled != 0u)" proof_begin proof_end)
math(EXPR proof_begin "${proof_begin} + ${host_configure_at}")
math(EXPR proof_end "${proof_end} + ${host_configure_at}")
if(NOT (identity_last_at GREATER proof_begin AND identity_last_at LESS proof_end))
    message(FATAL_ERROR "arcade link hook isolation: main.c may call NativeIdentity_Get a second time only inside its if (rosterProofOptions.enabled != 0u) block after NativeArcadeLinkHost_Configure")
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
# 8d. The rejection list covers every replay option the replay scheduler
#     parses: each quoted "--..." string in the scheduler's argument parser
#     must appear in main.c's replayOptions list, so a new replay option
#     cannot silently bypass the rejection.
set(replay_seam_path "platform/native_replay_scheduler_seam.c")
ctr_read_source("${replay_seam_path}" replay_seam_source)
ctr_strip_comments("${replay_seam_source}" replay_seam_code)
string(REGEX MATCHALL "\"--[^\"\r\n]*\"" replay_seam_options "${replay_seam_code}")
list(REMOVE_DUPLICATES replay_seam_options)
list(LENGTH replay_seam_options replay_seam_option_count)
if(replay_seam_option_count EQUAL 0)
    message(FATAL_ERROR "arcade link hook isolation: found no quoted \"--...\" option in ${replay_seam_path}")
endif()
set(replay_list_opener "static const char *const replayOptions[] = {")
string(FIND "${main_code}" "${replay_list_opener}" replay_list_at)
if(replay_list_at EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: main.c must define '${replay_list_opener}...}'")
endif()
string(SUBSTRING "${main_code}" ${replay_list_at} -1 replay_list_tail)
string(FIND "${replay_list_tail}" "};" replay_list_end)
if(replay_list_end EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: main.c replayOptions list is not terminated")
endif()
string(SUBSTRING "${replay_list_tail}" 0 ${replay_list_end} replay_list)
foreach(option IN LISTS replay_seam_options)
    string(FIND "${replay_list}" "${option}" option_at)
    if(option_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: replay option ${option} from ${replay_seam_path} is missing from main.c's replayOptions rejection list")
    endif()
endforeach()

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

# 10. Quick states are disabled in link and preview mode: in
#     native_platform.c each quick-state request is made exactly once, in its
#     own F5 or F8 case, after a host-mode gate whose block logs the warning
#     and breaks out before the request.
set(platform_path "platform/native_platform.c")
ctr_read_source("${platform_path}" platform_source)
ctr_require_literal("${platform_path}" "${platform_source}" "#include \"platform/native_arcade_link_host.h\"")
ctr_strip_comments("${platform_source}" platform_code)
set(quick_state_gate "if (NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)")
foreach(pair "F5 NativeSaveState_RequestSave()" "F8 NativeSaveState_RequestLoad()")
    string(REPLACE " " ";" pair_items "${pair}")
    list(GET pair_items 0 hotkey)
    list(GET pair_items 1 request)
    string(REPLACE "(" "\\(" request_regex "${request}")
    string(REPLACE ")" "\\)" request_regex "${request_regex}")
    string(REGEX MATCHALL "${request_regex}" request_calls "${platform_code}")
    list(LENGTH request_calls request_call_count)
    if(NOT request_call_count EQUAL 1)
        message(FATAL_ERROR "arcade link hook isolation: ${platform_path} must call ${request} exactly once (found ${request_call_count})")
    endif()
    set(case_label "case SDL_SCANCODE_${hotkey}:")
    string(FIND "${platform_code}" "${case_label}" case_at)
    if(case_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: required text '${case_label}' missing from ${platform_path}")
    endif()
    string(FIND "${platform_code}" "${request}" request_at)
    if(NOT request_at GREATER case_at)
        message(FATAL_ERROR "arcade link hook isolation: ${request} in ${platform_path} must sit in its ${case_label} case")
    endif()
    math(EXPR case_length "${request_at} - ${case_at}")
    string(SUBSTRING "${platform_code}" ${case_at} ${case_length} case_segment)
    string(LENGTH "${case_label}" case_label_length)
    string(SUBSTRING "${case_segment}" ${case_label_length} -1 case_body)
    foreach(intruder IN ITEMS "case " "default:")
        string(FIND "${case_body}" "${intruder}" intruder_at)
        if(NOT intruder_at EQUAL -1)
            message(FATAL_ERROR "arcade link hook isolation: ${request} in ${platform_path} must sit in its ${case_label} case")
        endif()
    endforeach()
    string(FIND "${case_body}" "${quick_state_gate}" gate_at)
    if(gate_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: ${case_label} in ${platform_path} must be gated by '${quick_state_gate}' before ${request}")
    endif()
    ctr_find_block("${platform_path} (${case_label})" "${case_body}" "${quick_state_gate}" gate_begin gate_end)
    math(EXPR gate_length "${gate_end} - ${gate_begin} + 1")
    string(SUBSTRING "${case_body}" ${gate_begin} ${gate_length} gate_block)
    ctr_require_order("${platform_path} (${case_label} gate)" "${gate_block}"
        "Platform_LogWarn(\"[CTR Native] quick states are disabled in arcade-link mode\\n\");"
        "break;")
endforeach()

# 10b. No other caller: outside native_platform.c (checked above) and
#      native_savestate.c (which defines them), no platform/, game/, or
#      main.c source names NativeSaveState_RequestSave or
#      NativeSaveState_RequestLoad in code, so a new quick-state caller
#      cannot bypass the host-mode gate.
file(GLOB_RECURSE quick_state_scan_paths
    "${repo}/platform/*.c" "${repo}/game/*.c")
list(APPEND quick_state_scan_paths "${repo}/main.c")
foreach(path IN LISTS quick_state_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL "platform/native_platform.c" OR relative_path STREQUAL "platform/native_savestate.c")
        continue()
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "NativeSaveState_Request" quick_state_hit)
    if(quick_state_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    if(code MATCHES "NativeSaveState_Request(Save|Load)([^A-Za-z0-9_]|$)")
        message(FATAL_ERROR "arcade link hook isolation: ${relative_path} names NativeSaveState_Request${CMAKE_MATCH_1}; quick states may be requested only from the gated F5 and F8 cases in ${platform_path}")
    endif()
endforeach()

# 11. START_RACE is handed to the race caller (RL-S8b). Until RL-S8b the
#     branch aborted to the title (NativeArcadeLinkHost_AbortToTitle, then a
#     host-mode OFF check that gave the box back if the link could not
#     reopen). The new code launches the race instead, so the abort pin
#     becomes its opposite: neither the hook nor the caller names
#     AbortToTitle (an abort on START_RACE would close the link under the
#     race; RL-11 local failures go through ReportRaceFailure instead), and
#     with no abort there is no fall-back to mode OFF on this path, so the
#     restore pin is dropped with it. The branch hands the launch over
#     exactly once, through MainArcadeRaceLaunch_StartRace, which the hook
#     calls nowhere else; and the host tick's raceFinished input is the
#     caller's finish report.
ctr_strip_comments("${hook_source}" hook_code)
set(caller_source_path "game/MAIN/MainArcadeRaceLaunch.c")
set(caller_header_path "game/MAIN/MainArcadeRaceLaunch.h")
ctr_read_source("${caller_source_path}" caller_source)
ctr_read_source("${caller_header_path}" caller_header)
ctr_strip_comments("${caller_source}" caller_code)
foreach(pair "${hook_source_path}|hook_code" "${caller_source_path}|caller_code")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 variable)
    ctr_forbid("${relative_path}" "${${variable}}" "NativeArcadeLinkHost_AbortToTitle")
endforeach()
ctr_forbid("${hook_source_path}" "${hook_source}" "networked race launch is not wired yet")
ctr_find_block("${hook_source_path}" "${hook_code}"
    "if (action == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_START_RACE)" start_begin start_end)
math(EXPR start_length "${start_end} - ${start_begin} + 1")
string(SUBSTRING "${hook_code}" ${start_begin} ${start_length} start_block)
ctr_require_literal("${hook_source_path} (START_RACE branch)" "${start_block}" "MainArcadeRaceLaunch_StartRace();")
string(REGEX MATCHALL "MainArcadeRaceLaunch_StartRace\\(" handoff_calls "${hook_code}")
list(LENGTH handoff_calls handoff_call_count)
if(NOT handoff_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must call MainArcadeRaceLaunch_StartRace exactly once, in its START_RACE branch (found ${handoff_call_count})")
endif()
ctr_require_literal("${hook_source_path}" "${hook_code}"
    "action = NativeArcadeLinkHost_Tick(output->heldButtons, MainArcadeRaceLaunch_RaceFinished());")
string(REGEX MATCHALL "MainArcadeRaceLaunch_[A-Za-z]+" hook_caller_names "${hook_code}")
list(REMOVE_DUPLICATES hook_caller_names)
list(SORT hook_caller_names)
if(NOT "${hook_caller_names}" STREQUAL "MainArcadeRaceLaunch_RaceFinished;MainArcadeRaceLaunch_StartRace")
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} may name only MainArcadeRaceLaunch_StartRace and MainArcadeRaceLaunch_RaceFinished of the race caller (found '${hook_caller_names}')")
endif()

# 12. The START_RACE branch logs the agreed match (MS-8,
#     docs/MATCH_SELECT_MILESTONE.md section 2.7) before it hands the launch
#     to the race caller, while the link still holds the agreed config: the
#     only GetAgreedMatch call is inside that branch, before the hand-off, and
#     its success block logs through the hook's Platform_Log helper.
string(REGEX MATCHALL "NativeArcadeLinkHost_GetAgreedMatch\\(" agreed_calls "${hook_code}")
list(LENGTH agreed_calls agreed_call_count)
if(NOT agreed_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${hook_source_path} must call NativeArcadeLinkHost_GetAgreedMatch exactly once (found ${agreed_call_count})")
endif()
set(agreed_check "if (NativeArcadeLinkHost_GetAgreedMatch(&match))")
ctr_require_order("${hook_source_path} (START_RACE branch)" "${start_block}"
    "${agreed_check}" "MainArcadeLink_LogAgreedMatch(&match);" "MainArcadeRaceLaunch_StartRace();")
ctr_find_block("${hook_source_path} (START_RACE branch)" "${start_block}" "${agreed_check}" agreed_begin agreed_end)
math(EXPR agreed_length "${agreed_end} - ${agreed_begin} + 1")
string(SUBSTRING "${start_block}" ${agreed_begin} ${agreed_length} agreed_block)
ctr_require_literal("${hook_source_path} (agreed-match log)" "${agreed_block}" "MainArcadeLink_LogAgreedMatch(&match);")
ctr_find_block("${hook_source_path}" "${hook_code}"
    "static void MainArcadeLink_LogAgreedMatch(const struct NativeArcadeLinkHostMatch *match)" log_begin log_end)
math(EXPR log_length "${log_end} - ${log_begin} + 1")
string(SUBSTRING "${hook_code}" ${log_begin} ${log_length} log_block)
ctr_require_literal("${hook_source_path} (agreed-match log)" "${log_block}"
    "Platform_Log(\"[CTR Native] arcade link: agreed match track %u laps %u seed 0x%08X%08X slots")

# 13. The host-side test read-back header is never included by game code:
#     the hook's include allowlist above already excludes it; this also keeps
#     the hook from naming the read-back itself.
ctr_forbid("${hook_source_path}" "${hook_source}" "native_arcade_link_host_internal")
ctr_forbid("${hook_source_path}" "${hook_source}" "NativeArcadeLinkHost_Internal")

# 14. main.c fills the host-local select entropy only inside the
#     link-enabled branch: exactly one assignment, inside the block of the
#     first `if (arcadeLinkOptions.enabled != 0u)`, after the identity read.
string(REGEX MATCHALL "arcadeLinkOptions\\.selectEntropy[ \t]*=" entropy_writes "${main_code}")
list(LENGTH entropy_writes entropy_write_count)
if(NOT entropy_write_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: main.c must assign arcadeLinkOptions.selectEntropy exactly once (found ${entropy_write_count})")
endif()
string(REGEX MATCHALL "selectEntropy" entropy_mentions "${main_code}")
list(LENGTH entropy_mentions entropy_mention_count)
if(NOT entropy_mention_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: main.c must name selectEntropy only in its one assignment (found ${entropy_mention_count})")
endif()
string(FIND "${main_code}" "arcadeLinkOptions.selectEntropy" entropy_at)
if(NOT (entropy_at GREATER identity_at AND entropy_at LESS enabled_end))
    message(FATAL_ERROR "arcade link hook isolation: main.c must fill arcadeLinkOptions.selectEntropy only inside its if (arcadeLinkOptions.enabled != 0u) block, after NativeIdentity_Get")
endif()

# 15. The arcade-link game files read select-view and agreed-match values
#     only through the host names (NATIVE_ARCADE_LINK_HOST_ROLE_*,
#     NATIVE_ARCADE_LINK_HOST_SELECT_*; MS-8b): no game/MAIN/MainArcadeLink*
#     source or header names a match-config slot role, the match-config
#     struct, or a match-select value. Scoped to the arcade-link files
#     because other game/MAIN sources (MainArcadeBotSetup, MainArcadeRoster,
#     MainArcadeSetupV4, MainArcadeBotTickEvidence, MainCanonicalStateV4,
#     MainCanonicalRuntime) name the match config for unrelated reasons.
file(GLOB arcade_link_game_paths "${repo}/game/MAIN/MainArcadeLink*.c" "${repo}/game/MAIN/MainArcadeLink*.h")
list(LENGTH arcade_link_game_paths arcade_link_game_count)
if(arcade_link_game_count LESS 8)
    message(FATAL_ERROR "arcade link hook isolation: expected at least the eight MainArcadeLink{,Layout,Policy,Sound}.{c,h} files, found ${arcade_link_game_count}; the scan is broken")
endif()
foreach(path IN LISTS arcade_link_game_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" source)
    foreach(term IN ITEMS NATIVE_MATCH_SLOT_ROLE_ NativeMatchConfig NATIVE_MATCH_SELECT_)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()
ctr_require_literal("${hook_source_path}" "${hook_code}" "case NATIVE_ARCADE_LINK_HOST_ROLE_CAB1:")
ctr_require_literal("${hook_source_path}" "${hook_code}" "case NATIVE_ARCADE_LINK_HOST_ROLE_CAB2:")
ctr_require_literal("${hook_source_path}" "${hook_code}" "case NATIVE_ARCADE_LINK_HOST_ROLE_BOT:")

# 16. The live race caller (game/MAIN/MainArcadeRaceLaunch.{c,h},
#     docs/RACE_LAUNCH_MILESTONE.md RL-8..RL-12, RL-S8b). The token ban of
#     section 5 of that document is in main_arcade_race_setup_isolation_test.cmake
#     (rule 4), next to the setup allow-list that names this caller.
# 16a. Native only and dormant by default: the whole source is one
#      #if defined(CTR_NATIVE) block, the header includes only stdint.h, and
#      in MainArcadeRaceLaunch_Frame nothing but plain declarations precede
#      the dormant check (host not in LINK mode and the core idle), which
#      returns at once; so with no arcade-link option nothing is touched.
string(REGEX MATCHALL "#[ \t]*if" caller_ifs "${caller_source}")
string(REGEX MATCHALL "#[ \t]*endif" caller_endifs "${caller_source}")
string(REGEX MATCHALL "#[ \t]*(else|elif)" caller_elses "${caller_source}")
list(LENGTH caller_ifs caller_if_count)
list(LENGTH caller_endifs caller_endif_count)
list(LENGTH caller_elses caller_else_count)
if(NOT caller_if_count EQUAL 1 OR NOT caller_endif_count EQUAL 1 OR NOT caller_else_count EQUAL 0)
    message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must hold exactly one #if/#endif block and no #else/#elif (found ${caller_if_count} #if, ${caller_endif_count} #endif, ${caller_else_count} #else/#elif)")
endif()
string(FIND "${caller_source}" "#if defined(CTR_NATIVE)" caller_guard_at)
if(caller_guard_at EQUAL -1)
    message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must open with #if defined(CTR_NATIVE)")
endif()
string(SUBSTRING "${caller_source}" 0 ${caller_guard_at} caller_before_guard)
ctr_strip_comments("${caller_before_guard}" caller_before_guard)
if(NOT caller_before_guard MATCHES "^[ \t\r\n]*$")
    message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} has code before #if defined(CTR_NATIVE)")
endif()
if(NOT caller_source MATCHES "#endif[ \t\r\n]*$")
    message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must end with the #endif of its CTR_NATIVE block")
endif()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" caller_header_includes "${caller_header}")
if(NOT "${caller_header_includes}" STREQUAL "#include <stdint.h>")
    message(FATAL_ERROR "arcade link hook isolation: ${caller_header_path} must include only <stdint.h> (found '${caller_header_includes}')")
endif()
set(caller_frame_signature "void MainArcadeRaceLaunch_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)")
ctr_require_literal("${caller_header_path}" "${caller_header}" "${caller_frame_signature};")
ctr_find_block("${caller_source_path}" "${caller_code}" "${caller_frame_signature}" caller_frame_begin caller_frame_end)
math(EXPR caller_frame_length "${caller_frame_end} - ${caller_frame_begin} + 1")
string(SUBSTRING "${caller_code}" ${caller_frame_begin} ${caller_frame_length} caller_frame_block)
if(NOT caller_frame_block MATCHES "^\\{([ \t\r\n]*struct[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\\*?[ \t]*[A-Za-z_][A-Za-z0-9_]*( = &s_mainArcadeRaceLaunch)?;)*[ \t\r\n]*if \\(\\(NativeArcadeLinkHost_Mode\\(\\) != \\(uint32_t\\)NATIVE_ARCADE_LINK_HOST_MODE_LINK\\) && \\(state->core\\.phase == MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE\\)\\)[ \t\r\n]*\\{[ \t\r\n]*return;[ \t\r\n]*\\}")
    message(FATAL_ERROR "arcade link hook isolation: MainArcadeRaceLaunch_Frame must return first, before touching anything, when the host is not in LINK mode and no race is in progress")
endif()

# 16b. Two separate racing reads (RL-8, and RL-S7 interpretation f): the
#      hook's gather reads it before the host tick for the policy (section
#      5b), and the caller reads it exactly once, in its own gather, into the
#      core's hostRacing, after the tick (the caller is stepped after the
#      hook, 6a). Neither is fed from the other: the caller never names the
#      policy, and the hook never names the launch core. The caller gathers,
#      steps, and applies exactly once per frame.
ctr_find_block("${caller_source_path}" "${caller_code}" "static void MainArcadeRaceLaunch_Gather(" caller_gather_begin caller_gather_end)
math(EXPR caller_gather_length "${caller_gather_end} - ${caller_gather_begin} + 1")
string(SUBSTRING "${caller_code}" ${caller_gather_begin} ${caller_gather_length} caller_gather_block)
ctr_require_literal("${caller_source_path} (MainArcadeRaceLaunch_Gather)" "${caller_gather_block}"
    "input->hostRacing = (NativeArcadeLinkHost_Racing() != 0u) ? 1u : 0u;")
string(REGEX MATCHALL "NativeArcadeLinkHost_Racing\\(" caller_racing_calls "${caller_code}")
list(LENGTH caller_racing_calls caller_racing_call_count)
if(NOT caller_racing_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must call NativeArcadeLinkHost_Racing exactly once, in MainArcadeRaceLaunch_Gather (found ${caller_racing_call_count})")
endif()
ctr_forbid("${caller_source_path}" "${caller_code}" "MainArcadeLinkPolicy")
ctr_forbid("${hook_source_path}" "${hook_source}" "MainArcadeRaceLaunchCore")
ctr_require_order("${caller_source_path} (MainArcadeRaceLaunch_Frame)" "${caller_frame_block}"
    "MainArcadeRaceLaunch_Gather(gGT, gGS, &input);"
    "MainArcadeRaceLaunchCore_Step(&state->core, &input, &output)"
    "if (output.armAndLaunch != 0u)" "MainArcadeRaceLaunch_ArmAndLaunch(&output);"
    "MainArcadeRaceLaunch_Apply(gGT, &output);")
foreach(call IN ITEMS "MainArcadeRaceLaunchCore_Step\\(" "MainArcadeRaceLaunch_Gather\\(gGT" "MainArcadeRaceLaunch_Apply\\(gGT")
    string(REGEX MATCHALL "${call}" call_hits "${caller_code}")
    list(LENGTH call_hits call_hit_count)
    if(NOT call_hit_count EQUAL 1)
        message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must call '${call}' exactly once, once per frame (found ${call_hit_count})")
    endif()
endforeach()

# 16c. Launch: the agreed config, then Arm, then Launch, then the result fed
#      back to the core on the same frame with the same output.
ctr_find_block("${caller_source_path}" "${caller_code}" "static void MainArcadeRaceLaunch_ArmAndLaunch(" arm_begin arm_end)
math(EXPR arm_length "${arm_end} - ${arm_begin} + 1")
string(SUBSTRING "${caller_code}" ${arm_begin} ${arm_length} arm_block)
ctr_require_order("${caller_source_path} (MainArcadeRaceLaunch_ArmAndLaunch)" "${arm_block}"
    "NativeArcadeLinkHost_GetAgreedConfig(&state->config)" "MainArcadeRaceSetup_Arm(" "MainArcadeRaceSetup_Launch()"
    "MainArcadeRaceLaunchCore_LaunchResult(&state->core, result, output)")

# 16d. The core's decisions are applied in its order, each only when the core
#      sets it: leave the title, report the failure or the finish, the return
#      step, the rehearsal pads, the pad clear, and the Disarm.
ctr_find_block("${caller_source_path}" "${caller_code}" "static void MainArcadeRaceLaunch_Apply(" apply_begin apply_end)
math(EXPR apply_length "${apply_end} - ${apply_begin} + 1")
string(SUBSTRING "${caller_code}" ${apply_begin} ${apply_length} apply_block)
ctr_require_order("${caller_source_path} (MainArcadeRaceLaunch_Apply)" "${apply_block}"
    "if (output->leaveTitle != 0u)" "MainArcadeRaceLaunch_LeaveTitle();"
    "if (output->reportFailure != 0u)" "NativeArcadeLinkHost_ReportRaceFailure();"
    "else if (output->reportFinished != 0u)" "state->finishedPending = 1u;"
    "if (output->requestReturn != 0u)" "MainArcadeRaceLaunch_RequestReturn(gGT);"
    "if (output->installPads != 0u)" "MainArcadeRaceLaunch_InstallPads();"
    "if (output->clearPads != 0u)" "Platform_InputClearInstalledPadSnapshots();"
    "if (output->disarm != 0u)" "MainArcadeRaceSetup_Disarm();")
foreach(pair
        "if (output->leaveTitle != 0u)|MainArcadeRaceLaunch_LeaveTitle();"
        "if (output->requestReturn != 0u)|MainArcadeRaceLaunch_RequestReturn(gGT);"
        "if (output->installPads != 0u)|MainArcadeRaceLaunch_InstallPads();"
        "if (output->disarm != 0u)|MainArcadeRaceSetup_Disarm();")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 guard)
    list(GET pair_items 1 call)
    ctr_find_block("${caller_source_path} (MainArcadeRaceLaunch_Apply)" "${apply_block}" "${guard}" guard_begin guard_end)
    math(EXPR guard_length "${guard_end} - ${guard_begin} + 1")
    string(SUBSTRING "${apply_block}" ${guard_begin} ${guard_length} guard_block)
    ctr_require_literal("${caller_source_path} (${guard})" "${guard_block}" "${call}")
endforeach()
foreach(name IN ITEMS MainArcadeRaceLaunch_LeaveTitle MainArcadeRaceLaunch_RequestReturn MainArcadeRaceLaunch_InstallPads
        MainArcadeRaceSetup_Disarm NativeArcadeLinkHost_ReportRaceFailure Platform_InputInstallPadSnapshots)
    string(REGEX MATCHALL "${name}\\(" name_hits "${caller_code}")
    list(LENGTH name_hits name_hit_count)
    if(name MATCHES "^MainArcadeRaceLaunch_")
        set(expected 2)
    else()
        set(expected 1)
    endif()
    if(NOT name_hit_count EQUAL expected)
        message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must name ${name}( ${expected} time(s), its definition and its one call where it has one (found ${name_hit_count})")
    endif()
endforeach()

# 16e. The pad clear frame (RL-10): the installed pads are cleared only when
#      the core sets clearPads, which it never sets on the end frame or
#      before the return step's frame (the core's rule, pinned by
#      tests/main_arcade_race_launch_core_test.c). So the caller makes
#      exactly one clear call, alone with its log line inside the clearPads
#      block, and never in the report, return, or install blocks; and no
#      other game source clears installed pads. The pads installed are the
#      proof's neutral pads.
string(REGEX MATCHALL "Platform_InputClearInstalledPadSnapshots\\(" clear_calls "${caller_code}")
list(LENGTH clear_calls clear_call_count)
if(NOT clear_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} must call Platform_InputClearInstalledPadSnapshots exactly once (found ${clear_call_count})")
endif()
ctr_find_block("${caller_source_path} (MainArcadeRaceLaunch_Apply)" "${apply_block}" "if (output->clearPads != 0u)" clear_begin clear_end)
math(EXPR clear_length "${clear_end} - ${clear_begin} + 1")
string(SUBSTRING "${apply_block}" ${clear_begin} ${clear_length} clear_block)
if(NOT clear_block MATCHES "^\\{[ \t\r\n]*Platform_InputClearInstalledPadSnapshots\\(\\);[ \t\r\n]*Platform_Log\\([^;]*\\);[ \t\r\n]*\\}$")
    message(FATAL_ERROR "arcade link hook isolation: the clearPads block of ${caller_source_path} must be the pad clear and its log line only (found '${clear_block}')")
endif()
foreach(guard IN ITEMS "if (output->reportFailure != 0u)" "else if (output->reportFinished != 0u)" "if (output->requestReturn != 0u)"
        "if (output->installPads != 0u)")
    ctr_find_block("${caller_source_path} (MainArcadeRaceLaunch_Apply)" "${apply_block}" "${guard}" other_begin other_end)
    math(EXPR other_length "${other_end} - ${other_begin} + 1")
    string(SUBSTRING "${apply_block}" ${other_begin} ${other_length} other_block)
    string(FIND "${other_block}" "Platform_InputClearInstalledPadSnapshots" misplaced_clear_at)
    if(NOT misplaced_clear_at EQUAL -1)
        message(FATAL_ERROR "arcade link hook isolation: ${caller_source_path} clears the pads inside '${guard}'; only the clearPads block may")
    endif()
endforeach()
file(GLOB_RECURSE clear_scan_paths "${repo}/game/*.c" "${repo}/game/*.h")
foreach(path IN LISTS clear_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL caller_source_path)
        continue()
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "Platform_InputClearInstalledPadSnapshots" clear_hit)
    if(NOT clear_hit EQUAL -1)
        ctr_strip_comments("${source}" code)
        string(FIND "${code}" "Platform_InputClearInstalledPadSnapshots" clear_code_hit)
        if(NOT clear_code_hit EQUAL -1)
            message(FATAL_ERROR "arcade link hook isolation: ${relative_path} names Platform_InputClearInstalledPadSnapshots; only ${caller_source_path} clears installed pads")
        endif()
    endif()
endforeach()
ctr_find_block("${caller_source_path}" "${caller_code}" "static void MainArcadeRaceLaunch_InstallPads(void)" pads_begin pads_end)
math(EXPR pads_length "${pads_end} - ${pads_begin} + 1")
string(SUBSTRING "${caller_code}" ${pads_begin} ${pads_length} pads_block)
ctr_require_order("${caller_source_path} (MainArcadeRaceLaunch_InstallPads)" "${pads_block}"
    "NativeArcadeRosterProof_ScriptedPads(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, pads);"
    "(void)Platform_InputInstallPadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT);")

# 16f. The duplicated LeaveTitle (RL-8): the caller's copy and the roster
#      proof's MainArcadeRosterProof_LeaveTitle have the same body, so they
#      make the same calls in the same order (compared on the comment-free
#      code, whitespace collapsed).
set(proof_source_path "game/MAIN/MainArcadeRosterProof.c")
ctr_read_source("${proof_source_path}" proof_source)
ctr_strip_comments("${proof_source}" proof_code)
ctr_find_block("${proof_source_path}" "${proof_code}" "static void MainArcadeRosterProof_LeaveTitle(void)" proof_leave_begin proof_leave_end)
math(EXPR proof_leave_length "${proof_leave_end} - ${proof_leave_begin} + 1")
string(SUBSTRING "${proof_code}" ${proof_leave_begin} ${proof_leave_length} proof_leave_block)
ctr_find_block("${caller_source_path}" "${caller_code}" "static void MainArcadeRaceLaunch_LeaveTitle(void)" caller_leave_begin caller_leave_end)
math(EXPR caller_leave_length "${caller_leave_end} - ${caller_leave_begin} + 1")
string(SUBSTRING "${caller_code}" ${caller_leave_begin} ${caller_leave_length} caller_leave_block)
string(REGEX REPLACE "[ \t\r\n]+" " " proof_leave_flat "${proof_leave_block}")
string(REGEX REPLACE "[ \t\r\n]+" " " caller_leave_flat "${caller_leave_block}")
if(NOT proof_leave_flat STREQUAL caller_leave_flat)
    message(FATAL_ERROR "arcade link hook isolation: MainArcadeRaceLaunch_LeaveTitle must make the same calls in the same order as MainArcadeRosterProof_LeaveTitle ('${caller_leave_flat}' vs '${proof_leave_flat}')")
endif()
ctr_require_order("${proof_source_path} (MainArcadeRosterProof_LeaveTitle)" "${proof_leave_block}"
    "MM_Title_CameraReset();" "MM_Title_KillThread();" "RECTMENU_Hide(&MM_MENU_MAIN);" "sdata->ptrDesiredMenu = NULL;"
    "sdata->ptrActiveMenu = NULL;")

# 16g. The RL-12 line, one per validated race, through the setup's digests.
ctr_require_literal("${caller_source_path}" "${caller_code}" "#define MAIN_ARCADE_RACE_LAUNCH_LOG \"[CTR Native] arcade link: \"")
ctr_require_order("${caller_source_path}" "${caller_code}"
    "static void MainArcadeRaceLaunch_LogDigests(uint32_t raceNumber)"
    "MainArcadeRaceSetup_Digests(digests[0], digests[1], digests[2], digests[3])"
    "MAIN_ARCADE_RACE_LAUNCH_LOG \"race %u validated config %s plan %s bots %s bank %s\\n\""
    "if (output->validated != 0u)" "MainArcadeRaceLaunch_LogDigests(output->raceNumber);")
