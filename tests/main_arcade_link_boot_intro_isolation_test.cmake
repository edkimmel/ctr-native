# Structural isolation for the arcade-link boot-intro skip
# (docs/SOLO_CAB_MILESTONE.md section 7). In LINK mode only, the cabinet boots
# straight to the title: StateZero skips the SCEA display and the "Start your
# engines" XA (play and wait), the first-boot branch of LOAD_TenStages stage 0
# skips the copyright display and its intro-song hold, and the Naughty Dog
# crate ends through the retail START skip on its first camera tick, with
# only the minimum-time gate bypassed and no pad tap injected. The decision is
# the pure MainArcadeLinkPolicy_SkipBootIntro (LINK -> 1; unit-tested in
# tests/main_arcade_link_policy_test.c); the only game-side reader is the
# thin accessor MainArcadeLink_SkipBootIntro, and this test pins that:
#   1. the policy and accessor bodies;
#   2. the accessor is named only by its own header and source and the three
#      call sites (MainMain.c, LOAD_TenStages.c, CS_Thread.c), once each,
#      each call (and each include of MainArcadeLink.h) inside an
#      #if defined(CTR_NATIVE) block;
#   3. StateZero keeps every load, init, and topology-lease call in its
#      retail order, and the two gated blocks hold exactly the SCEA display
#      and the XA play and wait;
#   4. the copyright gate holds exactly the display and the native hold, and
#      boolFirstBoot, the TIM load, and the not-first-boot branch are intact;
#   5. the crate skip is the retail START-skip path for NAUGHTY_DOG_CRATE
#      only, with the retail conditions kept verbatim under #else, and no
#      pad tap is written;
#   6. main.c configures the host (which fixes the mode) before CTR_Main.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade link boot intro isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_require_literal relative_path source literal)
    string(FIND "${source}" "${literal}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${literal}' missing from ${relative_path}")
    endif()
endfunction()

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# Removes preprocessor lines and all whitespace: the bare token text of a block.
function(ctr_squash source out_var)
    string(REGEX REPLACE "(^|\n)[ \t]*#[^\r\n]*" "\\1" squashed "${source}")
    string(REGEX REPLACE "[ \t\r\n]" "" squashed "${squashed}")
    set(${out_var} "${squashed}" PARENT_SCOPE)
endfunction()

# Fails unless every term after the first two arguments appears in source,
# each one found after the end of the previous one. Terms are read from
# ARGV so a ';' inside a term is kept.
function(ctr_require_order relative_path source)
    set(remaining "${source}")
    math(EXPR last_index "${ARGC} - 1")
    foreach(index RANGE 2 ${last_index})
        set(term "${ARGV${index}}")
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${prefix}: '${term}' is missing or out of order in ${relative_path}")
        endif()
        string(LENGTH "${term}" term_length)
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endforeach()
endfunction()

# Counts the non-overlapping occurrences of term in source.
function(ctr_count source term out_var)
    set(count 0)
    set(remaining "${source}")
    string(LENGTH "${term}" term_length)
    while(TRUE)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            break()
        endif()
        math(EXPR count "${count} + 1")
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endwhile()
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Finds the first occurrence of opener in source, then the first '{' after
# it, and sets out_begin and out_end to the offsets of that '{' and of its
# matching '}' (the blocks checked here hold no brace in a literal).
function(ctr_find_block relative_path source opener out_begin out_end)
    string(FIND "${source}" "${opener}" opener_at)
    if(opener_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${opener}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" ${opener_at} -1 tail)
    string(FIND "${tail}" "{" brace_offset)
    if(brace_offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: no block follows '${opener}' in ${relative_path}")
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
    message(FATAL_ERROR "${prefix}: unbalanced block after '${opener}' in ${relative_path}")
endfunction()

# Sets out_var to the text of the block ctr_find_block finds (braces included).
function(ctr_block_text relative_path source opener out_var)
    ctr_find_block("${relative_path}" "${source}" "${opener}" begin end)
    math(EXPR length "${end} - ${begin} + 1")
    string(SUBSTRING "${source}" ${begin} ${length} block)
    set(${out_var} "${block}" PARENT_SCOPE)
endfunction()

# Fails unless the gate opener is followed, up to its block's '{', only by
# whitespace and one #endif, and the block's squashed text equals expected.
function(ctr_require_gated_block relative_path source opener expected)
    ctr_find_block("${relative_path}" "${source}" "${opener}" begin end)
    string(FIND "${source}" "${opener}" opener_at)
    string(LENGTH "${opener}" opener_length)
    math(EXPR between_at "${opener_at} + ${opener_length}")
    math(EXPR between_length "${begin} - ${between_at}")
    string(SUBSTRING "${source}" ${between_at} ${between_length} between)
    if(NOT between MATCHES "^[ \t\r\n]*#endif[ \t\r\n]*$")
        message(FATAL_ERROR "${prefix}: '${opener}' in ${relative_path} must be followed only by #endif and its block")
    endif()
    math(EXPR length "${end} - ${begin} + 1")
    string(SUBSTRING "${source}" ${begin} ${length} block)
    ctr_squash("${block}" squashed)
    if(NOT squashed STREQUAL "${expected}")
        message(FATAL_ERROR "${prefix}: the block gated by '${opener}' in ${relative_path} must be exactly '${expected}' (found '${squashed}')")
    endif()
endfunction()

# Fails unless the directive in force at the first occurrence of term is an
# #if defined(CTR_NATIVE) (or #ifdef CTR_NATIVE) opened directly around it:
# the last preprocessor conditional before term opens that block.
function(ctr_require_native_guard relative_path source term)
    string(FIND "${source}" "${term}" term_at)
    if(term_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" 0 ${term_at} before)
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|ifndef|elif|else|endif)[^\r\n]*" directives "${before}")
    list(LENGTH directives directive_count)
    if(directive_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: '${term}' in ${relative_path} is not inside #if defined(CTR_NATIVE)")
    endif()
    list(GET directives -1 directive)
    string(STRIP "${directive}" directive)
    if(NOT (directive STREQUAL "#if defined(CTR_NATIVE)" OR directive STREQUAL "#ifdef CTR_NATIVE"))
        message(FATAL_ERROR "${prefix}: '${term}' in ${relative_path} is not directly inside #if defined(CTR_NATIVE) (last directive '${directive}')")
    endif()
endfunction()

set(accessor "MainArcadeLink_SkipBootIntro")
set(accessor_call "MainArcadeLink_SkipBootIntro()")
set(policy_fn "MainArcadeLinkPolicy_SkipBootIntro")
set(mainmain_path "game/MAIN/MainMain.c")
set(tenstages_path "game/LOAD/LOAD_TenStages.c")
set(csthread_path "game/233/CS_Thread.c")
set(hook_path "game/MAIN/MainArcadeLink.c")
set(hook_header_path "game/MAIN/MainArcadeLink.h")
set(policy_path "game/MAIN/MainArcadeLinkPolicy.c")
set(policy_header_path "game/MAIN/MainArcadeLinkPolicy.h")

# 1. The policy: LINK only, and nothing else in its body.
ctr_read_source("${policy_header_path}" policy_header)
ctr_require_literal("${policy_header_path}" "${policy_header}" "int ${policy_fn}(uint32_t hostMode);")
ctr_read_source("${policy_path}" policy_source)
ctr_strip_comments("${policy_source}" policy_code)
ctr_block_text("${policy_path}" "${policy_code}" "int ${policy_fn}(uint32_t hostMode)" policy_block)
ctr_squash("${policy_block}" policy_squashed)
if(NOT policy_squashed STREQUAL "{return(hostMode==MAIN_ARCADE_LINK_POLICY_MODE_LINK)?1:0;}")
    message(FATAL_ERROR "${prefix}: ${policy_fn} must return 1 for MAIN_ARCADE_LINK_POLICY_MODE_LINK and 0 otherwise (found '${policy_squashed}')")
endif()

# 1b. The accessor: the policy on the host mode, nothing else.
ctr_read_source("${hook_header_path}" hook_header)
ctr_require_literal("${hook_header_path}" "${hook_header}" "int ${accessor}(void);")
ctr_read_source("${hook_path}" hook_source)
ctr_strip_comments("${hook_source}" hook_code)
ctr_block_text("${hook_path}" "${hook_code}" "int ${accessor}(void)" accessor_block)
ctr_squash("${accessor_block}" accessor_squashed)
if(NOT accessor_squashed STREQUAL "{return${policy_fn}(NativeArcadeLinkHost_Mode());}")
    message(FATAL_ERROR "${prefix}: ${accessor} must only return ${policy_fn}(NativeArcadeLinkHost_Mode()) (found '${accessor_squashed}')")
endif()
ctr_count("${hook_code}" "${policy_fn}(" hook_policy_calls)
if(NOT hook_policy_calls EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${hook_path} must call ${policy_fn} only in ${accessor} (found ${hook_policy_calls})")
endif()

# 2. Who names the accessor and the policy function: game code, the host
#    glue, and main.c are scanned (comments stripped).
file(GLOB_RECURSE scan_paths
    "${repo}/game/*.c" "${repo}/game/*.h"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/*.h")
list(APPEND scan_paths "${repo}/main.c")
set(accessor_files "")
set(policy_files "")
foreach(path IN LISTS scan_paths)
    file(READ "${path}" source)
    string(FIND "${source}" "SkipBootIntro" quick)
    if(quick EQUAL -1)
        continue()
    endif()
    file(RELATIVE_PATH relative "${repo}" "${path}")
    ctr_strip_comments("${source}" code)
    string(FIND "${code}" "${accessor}" accessor_at)
    if(NOT accessor_at EQUAL -1)
        list(APPEND accessor_files "${relative}")
    endif()
    string(FIND "${code}" "${policy_fn}" policy_at)
    if(NOT policy_at EQUAL -1)
        list(APPEND policy_files "${relative}")
    endif()
endforeach()
list(SORT accessor_files)
set(expected_accessor_files "${csthread_path}" "${tenstages_path}" "${hook_path}" "${hook_header_path}" "${mainmain_path}")
list(SORT expected_accessor_files)
if(NOT "${accessor_files}" STREQUAL "${expected_accessor_files}")
    message(FATAL_ERROR "${prefix}: ${accessor} may be named only by ${expected_accessor_files} (found ${accessor_files})")
endif()
list(SORT policy_files)
set(expected_policy_files "${hook_path}" "${policy_path}" "${policy_header_path}")
list(SORT expected_policy_files)
if(NOT "${policy_files}" STREQUAL "${expected_policy_files}")
    message(FATAL_ERROR "${prefix}: ${policy_fn} may be named only by ${expected_policy_files} (found ${policy_files})")
endif()

# 2b. Each call site calls the accessor exactly once, inside a CTR_NATIVE
#     block, and includes the hook header inside a CTR_NATIVE block.
foreach(site IN ITEMS "${mainmain_path}" "${tenstages_path}" "${csthread_path}")
    ctr_read_source("${site}" site_source)
    ctr_strip_comments("${site_source}" site_code)
    ctr_count("${site_code}" "${accessor}" site_names)
    ctr_count("${site_code}" "${accessor_call}" site_calls)
    if(NOT site_names EQUAL 1 OR NOT site_calls EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${site} must call ${accessor_call} exactly once (found ${site_calls} calls, ${site_names} names)")
    endif()
    ctr_require_native_guard("${site}" "${site_code}" "${accessor_call}")
    ctr_require_native_guard("${site}" "${site_code}" "#include \"MAIN/MainArcadeLink.h\"")
endforeach()

# 3. StateZero: the call is in StateZero, every load, init, and lease call
#    keeps its retail order, the lease calls are the retail four, and the two
#    gated blocks hold exactly the SCEA display and the XA play and wait.
ctr_read_source("${mainmain_path}" mainmain_source)
ctr_strip_comments("${mainmain_source}" mainmain_code)
ctr_block_text("${mainmain_path}" "${mainmain_code}" "void StateZero()" statezero)
ctr_require_literal("${mainmain_path} (StateZero)" "${statezero}" "${accessor_call}")
ctr_require_order("${mainmain_path} (StateZero)" "${statezero}"
    "MainCanonicalTopologyLeaseRuntime_BeforeGameTrackerZero();"
    "memset(gGT, 0, sizeof(*gGT));"
    "MainCanonicalTopologyLeaseRuntime_ResetAfterGameTrackerZero();"
    "MainCanonicalTopologyLeaseRuntime_BeforeArenaReset();"
    "MEMPACK_Init(MEMPACK_SIZE);"
    "LOAD_InitCD();"
    "MEMCARD_InitCard();"
    "LOAD_LangFile((int)sdata->ptrBigfile1, 1);"
    "GAMEPROG_NewGame_OnBoot();"
    "gGT->levelID = NAUGHTY_DOG_CRATE;"
    "LOAD_VramFile(sdata->ptrBigfile1, 0x1fd, NULL, &vramSize, -1);"
    "const int skipBootIntro = ${accessor_call};"
    "if (skipBootIntro == 0)"
    "MainInit_VRAMDisplay();"
    "howl_InitGlobals(data.kartHwlPath);"
    "VSyncCallback(MainDrawCb_Vsync);"
    "Music_SetIntro();"
    "CseqMusic_StopAll();"
    "CseqMusic_Start(CSEQ_SONG_LEVEL, 0, NULL, 0, 0);"
    "Music_Start(0);"
    "if (skipBootIntro == 0)"
    "CDSYS_XAPlay(CDSYS_XA_TYPE_EXTRA, 0x50);"
    "CDSYS_XAPauseAtEnd();"
    "DecalGlobal_Clear(gGT);"
    "LOAD_VramFile(sdata->ptrBigfile1, 0x102, NULL, &vramSize, -1);"
    "sdata->mainGameState = 3;"
    "MainCanonicalTopologyLeaseRuntime_BeforeFullLoad();"
    "sdata->Loading.stage = LOAD_TEN_STAGES_0;")
ctr_count("${statezero}" "MainCanonicalTopologyLeaseRuntime_" statezero_lease_calls)
if(NOT statezero_lease_calls EQUAL 4)
    message(FATAL_ERROR "${prefix}: StateZero must hold exactly its four topology-lease runtime calls (found ${statezero_lease_calls})")
endif()
foreach(lease_call IN ITEMS BeforeGameTrackerZero ResetAfterGameTrackerZero BeforeArenaReset BeforeFullLoad)
    ctr_count("${statezero}" "MainCanonicalTopologyLeaseRuntime_${lease_call}();" lease_call_count)
    if(NOT lease_call_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: StateZero must call MainCanonicalTopologyLeaseRuntime_${lease_call} exactly once (found ${lease_call_count})")
    endif()
endforeach()
ctr_count("${statezero}" "skipBootIntro" statezero_flag_names)
ctr_count("${statezero}" "if (skipBootIntro == 0)" statezero_gates)
if(NOT statezero_flag_names EQUAL 3 OR NOT statezero_gates EQUAL 2)
    message(FATAL_ERROR "${prefix}: StateZero must name skipBootIntro only to set it and in its two gates (found ${statezero_flag_names} names, ${statezero_gates} gates)")
endif()
ctr_require_native_guard("${mainmain_path} (StateZero)" "${statezero}" "if (skipBootIntro == 0)")
ctr_require_gated_block("${mainmain_path} (StateZero)" "${statezero}" "if (skipBootIntro == 0)"
    "{MainInit_VRAMDisplay();}")
string(FIND "${statezero}" "Music_Start(0);" music_start_at)
string(SUBSTRING "${statezero}" ${music_start_at} -1 statezero_after_music)
ctr_require_native_guard("${mainmain_path} (StateZero, XA gate)" "${statezero_after_music}" "if (skipBootIntro == 0)")
ctr_require_gated_block("${mainmain_path} (StateZero, XA gate)" "${statezero_after_music}" "if (skipBootIntro == 0)"
    "{CDSYS_XAPlay(CDSYS_XA_TYPE_EXTRA,0x50);while(sdata->XA_State!=0){VSync(0);CDSYS_XAPauseAtEnd();}}")

# 4. LOAD_TenStages stage 0, first boot: boolFirstBoot is cleared and the
#    copyright TIM loads as retail; the gate holds exactly the display and
#    the native intro-song hold; the not-first-boot branch is intact.
ctr_read_source("${tenstages_path}" tenstages_source)
ctr_strip_comments("${tenstages_source}" tenstages_code)
set(first_boot_opener "if (sdata->boolFirstBoot != 0)")
ctr_block_text("${tenstages_path}" "${tenstages_code}" "${first_boot_opener}" first_boot)
ctr_require_order("${tenstages_path} (first boot)" "${first_boot}"
    "sdata->boolFirstBoot = 0;"
    "LOAD_VramFile(bigfile, LOAD_FIRST_BOOT_COPYRIGHT_TIM_BIGFILE_INDEX, NULL, &vramSize, -1);"
    "if (${accessor_call} == 0)"
    "MainInit_VRAMDisplay();"
    "Platform_PresentVRAMDisplay();"
    "gGT->db[0].drawEnv.isbg = 0;"
    "gGT->db[1].drawEnv.isbg = 0;")
ctr_require_native_guard("${tenstages_path} (first boot)" "${first_boot}" "if (${accessor_call} == 0)")
ctr_require_gated_block("${tenstages_path} (first boot)" "${first_boot}" "if (${accessor_call} == 0)"
    "{MainInit_VRAMDisplay();while(((sdata->songPool[0].flags&3)==1)&&(sdata->songPool[0].timeSpentPlaying<LOAD_NATIVE_NDBOX_INTRO_SONG_SYNC_TIME)){VSync(0);Platform_PresentVRAMDisplay();}}")
string(FIND "${tenstages_code}" "${first_boot_opener}" first_boot_at)
string(SUBSTRING "${tenstages_code}" ${first_boot_at} -1 tenstages_from_first_boot)
ctr_require_order("${tenstages_path} (stage 0)" "${tenstages_from_first_boot}"
    "${first_boot_opener}"
    "else"
    "MEMPACK_SwapPacks(LOAD_MAIN_PACK_INDEX);"
    "MEMPACK_PopToState(sdata->bookmarkID);"
    "sdata->bookmarkID = MEMPACK_PushState();")

# 5. The crate skip: the retail START-skip path, for NAUGHTY_DOG_CRATE only,
#    bypassing only the minimum-time gate, with the retail conditions kept
#    under #else and no pad tap written.
ctr_read_source("${csthread_path}" csthread_source)
ctr_strip_comments("${csthread_source}" csthread_code)
set(crate_flag "const int skipBootIntro = (gGT->levelID == NAUGHTY_DOG_CRATE) && (${accessor_call} != 0);")
ctr_require_order("${csthread_path}" "${csthread_code}"
    "${crate_flag}"
    "if (((sdata->gGamepads->gamepad[0].buttonsTapped & BTN_START) != 0) || (skipBootIntro != 0))"
    "#else"
    "if ((sdata->gGamepads->gamepad[0].buttonsTapped & BTN_START) != 0)"
    "#endif"
    "gGT->clockEffectEnabled &= ~CAM_PATH_FLAG_CLOCK_EFFECT;"
    "if ((u32)(gGT->levelID - CREDITS_CRASH) >= CS_CREDITS_LEVEL_COUNT)"
    "if (gGT->levelID == NAUGHTY_DOG_CRATE)"
    "#if defined(CTR_NATIVE)"
    "if ((skipBootIntro == 0) && ((u32)gGT->msInThisLEV >> CS_FRAME32_SHIFT < CS_ND_CRATE_SKIP_MIN_FRAME32))"
    "#else"
    "if ((u32)gGT->msInThisLEV >> CS_FRAME32_SHIFT < CS_ND_CRATE_SKIP_MIN_FRAME32)"
    "#endif"
    "goto afterCameraAndSkipChecks;"
    "RaceFlag_SetCanDraw(1);"
    "CseqMusic_StopAll();"
    "CDSYS_XAPauseRequest();"
    "RaceFlag_SetDrawOrder(0);"
    "levelToLoad = MAIN_MENU_LEVEL;"
    "MainRaceTrack_RequestLoad(levelToLoad);"
    "D233.isCutsceneOver = 1;")
ctr_require_native_guard("${csthread_path}" "${csthread_code}" "${crate_flag}")
ctr_count("${csthread_code}" "skipBootIntro" crate_flag_names)
if(NOT crate_flag_names EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${csthread_path} must name skipBootIntro only to set it, in the START condition, and in the minimum-time gate (found ${crate_flag_names})")
endif()
foreach(tap_path IN ITEMS "${csthread_path}" "${tenstages_path}")
    ctr_read_source("${tap_path}" tap_source)
    ctr_strip_comments("${tap_source}" tap_code)
    if(tap_code MATCHES "buttons(Tapped|HeldCurrFrame|HeldPrevFrame)[ \t]*(\\||&|\\^)?=[^=]")
        message(FATAL_ERROR "${prefix}: ${tap_path} must not write a pad word ('${CMAKE_MATCH_0}')")
    endif()
endforeach()

# 6. main.c configures the host, which fixes the mode for the run, before
#    CTR_Main (and so every StateZero) runs.
ctr_read_source("main.c" main_source)
ctr_strip_comments("${main_source}" main_code)
ctr_require_order("main.c" "${main_code}"
    "NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr)"
    "CTR_Main()")
