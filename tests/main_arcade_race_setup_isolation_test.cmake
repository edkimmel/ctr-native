# Structural isolation for the live race setup adapter
# (game/MAIN/MainArcadeRaceSetup.{c,h}, docs/ROSTER_MILESTONE.md section 3.2,
# R-5b) and its minimal internal launcher (game/MAIN/MainArcadeRosterProof.{c,h}
# and platform/native_arcade_roster_proof, section 3.4):
#  1. the hooks: MainArcadeRaceSetup_OnFinalizeInitBegin is the first
#     statement block of MainInit_FinalizeInit and
#     MainArcadeRaceSetup_OnDriversInitialized sits immediately after
#     MainInit_Drivers(gGT), each exactly once, each inside a CTR_NATIVE
#     guard; no other file calls either hook;
#  2. MainArcadeRaceSetup_Arm, MainArcadeRaceSetup_Launch, and
#     MainArcadeRaceSetup_Disarm are named only in MainArcadeRaceSetup.{c,h}
#     and MainArcadeRosterProof.c (Task 7 adds its caller), and
#     MainArcadeRosterProof_Frame only in MainArcadeRosterProof.{c,h} and
#     MainFrame_RenderFrame.c;
#  3. MainArcadeRaceSetup.c is one CTR_NATIVE block and MainArcadeRosterProof.c
#     one CTR_NATIVE && CTR_INTERNAL block; the proof hook's first statement
#     is its inactive early return; its MainFrame_RenderFrame call sits in a
#     CTR_NATIVE && CTR_INTERNAL guard between the arcade-link hook and the
#     retail menu-input collect;
#  4. no lease, topology capture, checkpoint, replay, lockstep, or
#     match-select token in the adapter or the proof hook (sources and
#     headers), and neither they nor the decision core write levelID in any
#     spelling (gGT->levelID, sdata->gGT->levelID, (*gGT).levelID, compound
#     assignment, increment, or taking its address); the one levelID store
#     allowed is the adapter's copy into its pointer-free mirror;
#  5. the adapter state and scratch are file-scope statics, the only objects
#     of their types, and platform/native_checkpoint.c and every replay
#     module never name MainArcadeRaceSetup;
#  6. LOAD_Hub_ReadFile gains no hook;
#  7. the unity chain includes the adapter after the MainCanonical* sources
#     and before the arcade-link sources, and the proof hook after the
#     arcade-link hook and before the 231 overlay; the pure plan, facts, and
#     decision core are linked, never unity-included;
#  8. ctr_native links the plan, the facts, the decision core, and the proof
#     libraries, and neither ctr_native_arcade_setup_v4 nor a V4 projector
#     directly; the proof library links exactly the link options, the
#     match-select rules, the bot rules, and the V1 canonical state (for the
#     race-relative control digest, R-6b), C17 with extensions off; neither
#     game file names the V4 setup or projector;
#  9. main.c parses the proof options before any replay parser, rejects them
#     with link, preview, --exit-after-frame, or replay options, and
#     configures the proof after the link host and before CTR_Main;
# 10. while the proof is active no exit looks like PASS: main.c returns
#     NativeArcadeRosterProof_ExitCode(CTR_Main()), and both host exit paths
#     (SDL quit and window close) exit through NativeArcadeRosterProof_ExitCode
#     in internal builds; the proof hook records its code before it requests
#     the exit;
# 11. the adapter's retail stores are pinned: MainArcadeRaceSetup_Apply maps
#     every core write target (each one of the core header's enum) to
#     exactly its one retail field or call, with the exact width casts, and
#     holds no other store; outside MainArcadeRaceSetup_Apply the adapter
#     stores to no retail field (gGT, sdata, data, or an alias of them, in any
#     member spelling, (*gGT).x and (gGT)->x included; compound assignment,
#     increment, address taken, or mem* destination) and never calls
#     PSX_BIOS_SetRandSeed or MainRaceTrack_RequestLoad; the core names the
#     two boot-relative pin targets (gGT->timer, gGT->frameTimer_Confetti;
#     RS-17, R-6c) exactly once each, only in its seeding step, after the
#     load-field verification and before the first seed, at their documented
#     values;
# 12. the proof's scripted pads and per-tick digests (R-6):
#     MainArcadeRosterProof_Start is named only by main.c (once, in an
#     internal-build guard, after the proof was configured and before
#     CTR_Main), and _BeginFrame and _EndFrame only by MainMain.c (once each,
#     BeginFrame before GAMEPAD_ProcessAnyoneVars and EndFrame after the frame
#     was rendered); each returns first when the proof is inactive; every
#     proof name in MainMain.c sits in a CTR_NATIVE && CTR_INTERNAL block; the
#     proof's V1 state is local only (never handed to the replay scheduler);
#     the proof hook extracts only the Meta drivers candidate, names no
#     Physics symbol, encodes through NativeCanonicalDriversDetailedV1_Encode,
#     and installs its pads in one place.
# The roster input caller rules live in
# main_canonical_drivers_roster_input_isolation_test.cmake, and the decision
# core's purity in main_arcade_race_setup_core_isolation_test.cmake.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "race setup isolation")
set(adapter_source "game/MAIN/MainArcadeRaceSetup.c")
set(adapter_header "game/MAIN/MainArcadeRaceSetup.h")
set(proof_source "game/MAIN/MainArcadeRosterProof.c")
set(proof_header "game/MAIN/MainArcadeRosterProof.h")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    # Line endings depend on the checkout; the checks below assume LF.
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

# Fails unless every term appears in source, each after the previous one.
function(ctr_require_order relative_path source)
    set(remaining "${source}")
    foreach(term IN LISTS ARGN)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${prefix}: '${term}' is missing or out of order in ${relative_path}")
        endif()
        string(LENGTH "${term}" term_length)
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endforeach()
endfunction()

# Counts the whole-identifier occurrences of name in code.
function(ctr_count_identifier code name out_var)
    # ';' is masked first: a match holding it would count as two list items.
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Sets out_begin/out_end to the braces of the first block after opener.
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

# Fails unless source is exactly one guard block: nothing but comments before
# `guard`, nothing but whitespace after its #endif, and no other conditional.
function(ctr_require_whole_file_guard relative_path source guard)
    string(REGEX MATCHALL "#[ \t]*if" if_directives "${source}")
    string(REGEX MATCHALL "#[ \t]*endif" endif_directives "${source}")
    string(REGEX MATCHALL "#[ \t]*(else|elif)" else_directives "${source}")
    list(LENGTH if_directives if_count)
    list(LENGTH endif_directives endif_count)
    list(LENGTH else_directives else_count)
    if(NOT if_count EQUAL 1 OR NOT endif_count EQUAL 1 OR NOT else_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: ${relative_path} must hold exactly one #if/#endif block and no #else/#elif (found ${if_count} #if, ${endif_count} #endif, ${else_count} #else/#elif)")
    endif()
    string(FIND "${source}" "${guard}" guard_at)
    if(guard_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${relative_path} must open with ${guard}")
    endif()
    string(SUBSTRING "${source}" 0 ${guard_at} before_guard)
    ctr_strip_comments("${before_guard}" before_guard)
    if(NOT before_guard MATCHES "^[ \t\r\n]*$")
        message(FATAL_ERROR "${prefix}: ${relative_path} has code before ${guard}")
    endif()
    if(NOT source MATCHES "#endif[ \t\r\n]*$")
        message(FATAL_ERROR "${prefix}: ${relative_path} must end with the #endif of its guard")
    endif()
endfunction()

# The whole-file guard check must itself work.
set(probe_ok "/* c */\n#if defined(CTR_NATIVE)\nint x;\n#endif\n")
ctr_require_whole_file_guard("probe" "${probe_ok}" "#if defined(CTR_NATIVE)")

ctr_read_source("${adapter_source}" adapter)
ctr_read_source("${adapter_header}" adapter_h)
ctr_read_source("${proof_source}" proof)
ctr_read_source("${proof_header}" proof_h)
ctr_strip_comments("${adapter}" adapter_code)
ctr_strip_comments("${proof}" proof_code)

# 1. The hooks in MainInit_FinalizeInit.
set(init_path "game/MAIN/MainInit.c")
ctr_read_source("${init_path}" init)
ctr_strip_comments("${init}" init_code)
ctr_find_block("${init_path}" "${init_code}" "void MainInit_FinalizeInit(struct GameTracker *gGT)" finalize_begin finalize_end)
math(EXPR finalize_length "${finalize_end} - ${finalize_begin} + 1")
string(SUBSTRING "${init_code}" ${finalize_begin} ${finalize_length} finalize_body)
# The first statement block: only plain declarations precede it.
if(NOT finalize_body MATCHES "^\\{([ \t\r\n]*(int|struct[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\\*?)[ \t]*[A-Za-z_][A-Za-z0-9_]*;)*[ \t\r\n]*#if defined\\(CTR_NATIVE\\)[ \t\r\n]*MainArcadeRaceSetup_OnFinalizeInitBegin\\(gGT\\);[ \t\r\n]*#endif")
    message(FATAL_ERROR "${prefix}: MainArcadeRaceSetup_OnFinalizeInitBegin(gGT) must be the first statement block of MainInit_FinalizeInit, inside #if defined(CTR_NATIVE)")
endif()
if(NOT finalize_body MATCHES "[\r\n][ \t]*MainInit_Drivers\\(gGT\\);[ \t\r\n]*#if defined\\(CTR_NATIVE\\)[ \t\r\n]*MainArcadeRaceSetup_OnDriversInitialized\\(gGT\\);[ \t\r\n]*#endif")
    message(FATAL_ERROR "${prefix}: MainArcadeRaceSetup_OnDriversInitialized(gGT) must follow MainInit_Drivers(gGT) immediately, inside #if defined(CTR_NATIVE)")
endif()
foreach(hook IN ITEMS MainArcadeRaceSetup_OnFinalizeInitBegin MainArcadeRaceSetup_OnDriversInitialized)
    ctr_count_identifier("${init_code}" "${hook}" init_hits)
    ctr_count_identifier("${finalize_body}" "${hook}" body_hits)
    if(NOT init_hits EQUAL 1 OR NOT body_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${init_path} must call ${hook} exactly once, in MainInit_FinalizeInit (found ${init_hits} in the file, ${body_hits} in the function)")
    endif()
endforeach()
ctr_count_identifier("${init_code}" "MainInit_Drivers" drivers_hits)
if(NOT drivers_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${init_path} must define MainInit_Drivers and call it once (found ${drivers_hits} names)")
endif()
ctr_require_order("${init_path}" "${init}" "#if defined(CTR_NATIVE)\n#include \"MAIN/MainArcadeRaceSetup.h\"\n#endif")

# 1b and 2. Who names the hooks and the arming calls, over every game,
#     platform, include, and main.c file (code only).
file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(scanned 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    file(READ "${path}" source)
    string(FIND "${source}" "MainArcadeRaceSetup_" raw_hit)
    if(raw_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    foreach(hook IN ITEMS MainArcadeRaceSetup_OnFinalizeInitBegin MainArcadeRaceSetup_OnDriversInitialized)
        ctr_count_identifier("${code}" "${hook}" hits)
        if(hits GREATER 0 AND NOT relative_path STREQUAL init_path AND NOT relative_path STREQUAL adapter_source
                AND NOT relative_path STREQUAL adapter_header)
            message(FATAL_ERROR "${prefix}: ${relative_path} names ${hook}; only ${init_path} calls it")
        endif()
    endforeach()
    foreach(call IN ITEMS MainArcadeRaceSetup_Arm MainArcadeRaceSetup_Launch MainArcadeRaceSetup_Disarm)
        ctr_count_identifier("${code}" "${call}" hits)
        if(hits GREATER 0 AND NOT relative_path STREQUAL adapter_source AND NOT relative_path STREQUAL adapter_header
                AND NOT relative_path STREQUAL proof_source)
            message(FATAL_ERROR "${prefix}: ${relative_path} names ${call}; only ${adapter_source}, ${adapter_header}, and ${proof_source} may")
        endif()
    endforeach()
endforeach()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()
ctr_count_identifier("${adapter_code}" "MainArcadeRaceSetup_Disarm" disarm_definitions)
if(NOT disarm_definitions EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${adapter_source} must define MainArcadeRaceSetup_Disarm once and never call it (found ${disarm_definitions})")
endif()
# The proof hook's frame entry: only its own files and MainFrame_RenderFrame.c.
set(frame_callers "${proof_source}" "${proof_header}" "game/MAIN/MainFrame_RenderFrame.c")
set(frame_scanned 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR frame_scanned "${frame_scanned} + 1")
    list(FIND frame_callers "${relative_path}" frame_caller_at)
    if(NOT frame_caller_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "MainArcadeRosterProof_Frame" raw_frame_hit)
    if(raw_frame_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    ctr_count_identifier("${code}" "MainArcadeRosterProof_Frame" frame_hits)
    if(frame_hits GREATER 0)
        message(FATAL_ERROR "${prefix}: ${relative_path} names MainArcadeRosterProof_Frame; only ${frame_callers} may")
    endif()
endforeach()
if(frame_scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${frame_scanned} files for the frame hook; the scan is broken")
endif()
foreach(call IN ITEMS MainArcadeRaceSetup_Arm MainArcadeRaceSetup_Launch)
    ctr_count_identifier("${proof_code}" "${call}" proof_hits)
    if(NOT proof_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${proof_source} must call ${call} exactly once (found ${proof_hits})")
    endif()
endforeach()

# 3. Guards, the proof's dormant early return, and its render-frame call.
ctr_require_whole_file_guard("${adapter_source}" "${adapter}" "#if defined(CTR_NATIVE)")
ctr_require_whole_file_guard("${proof_source}" "${proof}" "#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)")
ctr_find_block("${proof_source}" "${proof_code}"
    "void MainArcadeRosterProof_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS)" frame_begin frame_end)
math(EXPR frame_length "${frame_end} - ${frame_begin} + 1")
string(SUBSTRING "${proof_code}" ${frame_begin} ${frame_length} frame_body)
if(NOT frame_body MATCHES "^\\{([ \t\r\n]*(struct[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\\*|enum[ \t]+[A-Za-z_][A-Za-z0-9_]*)[ \t]*[A-Za-z_][A-Za-z0-9_]*( = &s_mainArcadeRosterProof)?;)*[ \t\r\n]*if \\(!NativeArcadeRosterProof_Active\\(\\)\\)[ \t\r\n]*\\{[ \t\r\n]*return;")
    message(FATAL_ERROR "${prefix}: MainArcadeRosterProof_Frame must return first, before touching anything, when the proof is inactive")
endif()
set(render_path "game/MAIN/MainFrame_RenderFrame.c")
ctr_read_source("${render_path}" render)
ctr_strip_comments("${render}" render_code)
ctr_count_identifier("${render_code}" "MainArcadeRosterProof_Frame" render_hits)
if(NOT render_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${render_path} must call MainArcadeRosterProof_Frame exactly once (found ${render_hits})")
endif()
if(NOT render_code MATCHES "#if defined\\(CTR_NATIVE\\) && defined\\(CTR_INTERNAL\\)[ \t\r\n]*MainArcadeRosterProof_Frame\\(gGT, gGamepads\\);[ \t\r\n]*#endif")
    message(FATAL_ERROR "${prefix}: ${render_path} must call MainArcadeRosterProof_Frame(gGT, gGamepads) alone inside #if defined(CTR_NATIVE) && defined(CTR_INTERNAL)")
endif()
ctr_require_order("${render_path}" "${render_code}"
    "MainArcadeLink_Frame(gGT, gGamepads)" "MainArcadeRosterProof_Frame(gGT, gGamepads)" "RECTMENU_CollectInput()"
    "RECTMENU_ProcessState()")
ctr_require_order("${render_path}" "${render}"
    "#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)\n#include \"MAIN/MainArcadeRosterProof.h\"")

# 4. Token bans in the adapter and the proof hook (whole files, comments
#    included), and the adapter never writes levelID.
set(banned_tokens
    Lease lease LEASE Topology topology TOPOLOGY Capture capture
    Checkpoint checkpoint CHECKPOINT Replay replay REPLAY
    Lockstep lockstep LOCKSTEP NativeMatchSelect native_match_select NATIVE_MATCH_SELECT
    Acquire Activate Publish Retire LOAD_Hub_ReadFile
    malloc calloc realloc "free(" alloca)
foreach(pair "${adapter_source}|adapter" "${adapter_header}|adapter_h" "${proof_source}|proof" "${proof_header}|proof_h")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 variable)
    foreach(term IN LISTS banned_tokens)
        ctr_forbid("${relative_path}" "${${variable}}" "${term}")
    endforeach()
endforeach()
# Every levelID store, in any spelling: an assignment or compound assignment
# (not ==, !=, <=, >=), an increment or decrement on either side, or its
# address taken (a unary & after =, (, ",", ?, :, or return). out_var is the
# list of matched stores.
function(ctr_level_id_stores code out_var)
    set(member "[A-Za-z_(*][A-Za-z0-9_]*[)]?([ \t]*(->|\\.)[ \t]*[A-Za-z_][A-Za-z0-9_]*[)]?)*[ \t]*(->|\\.)[ \t]*levelID")
    string(REGEX MATCHALL "[A-Za-z0-9_>.()* \t-]*levelID[ \t\r\n]*(=[^=]|[-+*/%&|^]=|<<=|>>=|\\+\\+|--)" assignments "${code}")
    string(REGEX MATCHALL "(\\+\\+|--)[ \t]*[(]?[ \t]*${member}" pre_increments "${code}")
    string(REGEX MATCHALL "(^|[=(,?:][ \t]*|return[ \t]+)&[ \t]*[(]?[ \t]*${member}" addresses "${code}")
    set(${out_var} ${assignments} ${pre_increments} ${addresses} PARENT_SCOPE)
endfunction()
# The store scan must itself work.
foreach(probe IN ITEMS "gGT->levelID = 3;" "sdata->gGT->levelID = 3;" "gGT->levelID=3;" "(*gGT).levelID = 3;"
        "gGT->levelID += 1;" "gGT->levelID |= 1;" "gGT->levelID++;" "++gGT->levelID;" "--sdata->gGT->levelID;"
        "int *p = &gGT->levelID;" "int *p = &(sdata->gGT->levelID);" "tracker.levelID = 2;")
    ctr_level_id_stores("${probe}" probe_stores)
    list(LENGTH probe_stores probe_store_count)
    if(NOT probe_store_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: the levelID store scan found ${probe_store_count} stores in '${probe}', expected 1")
    endif()
endforeach()
foreach(probe IN ITEMS "if (gGT->levelID == 3)" "x = (gGT->levelID != MAIN_MENU_LEVEL) && (y <= 2);"
        "a = gGT->levelID;" "if ((a && gGT->levelID >= 2) || (b & gGT->levelID))" "for (i = 0; i < 3; i++) { x = gGT->levelID; }")
    ctr_level_id_stores("${probe}" probe_stores)
    list(LENGTH probe_stores probe_store_count)
    if(NOT probe_store_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: the levelID store scan flags a read in '${probe}' (${probe_stores})")
    endif()
endforeach()
set(core_source "game/MAIN/MainArcadeRaceSetupCore.c")
ctr_read_source("${core_source}" core)
ctr_strip_comments("${core}" core_code)
foreach(pair "${adapter_source}|adapter_code" "${proof_source}|proof_code" "${core_source}|core_code")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 variable)
    ctr_level_id_stores("${${variable}}" stores)
    if(relative_path STREQUAL adapter_source)
        # The one store allowed: the copy into the pointer-free mirror.
        list(FIND stores "\tfields->levelID = " mirror_store_at)
        if(mirror_store_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: the levelID store scan no longer sees the adapter's mirror copy; the scan is broken")
        endif()
        list(REMOVE_ITEM stores "\tfields->levelID = ")
        # Count by marker: a match holds ';', which would split a MATCHALL list.
        string(REPLACE "\n\tfields->levelID = (int32_t)gGT->levelID;\n" "\n@CTR_MIRROR_COPY@\n" marked "${${variable}}")
        string(REGEX MATCHALL "@CTR_MIRROR_COPY@" mirror_copies "${marked}")
        list(LENGTH mirror_copies mirror_copy_count)
        if(NOT mirror_copy_count EQUAL 1)
            message(FATAL_ERROR "${prefix}: ${adapter_source} must copy gGT->levelID into its mirror exactly once (found ${mirror_copy_count})")
        endif()
    endif()
    list(LENGTH stores store_count)
    if(NOT store_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: ${relative_path} stores to levelID (${stores}); the load request carries the level")
    endif()
endforeach()
foreach(relative_path IN ITEMS "${adapter_source}" "${proof_source}")
    foreach(term IN ITEMS MainArcadeSetupV4 NativeCanonicalProjectorV4 canonical_projector_v4 MainCanonicalRuntime MainCanonicalStateV4)
        if(relative_path STREQUAL adapter_source)
            ctr_forbid("${relative_path}" "${adapter}" "${term}")
        else()
            ctr_forbid("${relative_path}" "${proof}" "${term}")
        endif()
    endforeach()
endforeach()

# 5. File-scope static state, never named by the checkpoint or replay modules.
foreach(declaration IN ITEMS "MainArcadeRaceSetupCore|s_mainArcadeRaceSetup" "MainArcadeRaceSetupScratch|s_mainArcadeRaceSetupScratch")
    string(REPLACE "|" ";" declaration_items "${declaration}")
    list(GET declaration_items 0 type_name)
    list(GET declaration_items 1 object_name)
    ctr_require("${adapter_source}" "${adapter_code}" "\nstatic struct ${type_name} ${object_name};")
    # Every object of the type, in any file of the adapter or the proof: a
    # declarator after the struct name (not a pointer, not a member access).
    foreach(pair "${adapter_source}|adapter_code" "${adapter_header}|adapter_h" "${proof_source}|proof_code")
        string(REPLACE "|" ";" pair_items "${pair}")
        list(GET pair_items 0 relative_path)
        list(GET pair_items 1 variable)
        # ';' is masked first: a match holding it would split the list.
        string(REPLACE ";" "@SEMI@" masked "${${variable}}")
        string(REGEX MATCHALL "struct[ \t\r\n]+${type_name}[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*[ \t\r\n]*(\\[[^]]*\\][ \t\r\n]*)?(@SEMI@|[=,)])" objects "${masked}")
        list(LENGTH objects object_count)
        if(relative_path STREQUAL adapter_source)
            set(expected 1)
        else()
            set(expected 0)
        endif()
        if(NOT object_count EQUAL expected)
            message(FATAL_ERROR "${prefix}: ${relative_path} declares ${object_count} objects of struct ${type_name}, expected ${expected} (the file-scope static)")
        endif()
    endforeach()
    ctr_count_identifier("${adapter_code}" "${object_name}" object_hits)
    if(object_hits LESS 2)
        message(FATAL_ERROR "${prefix}: ${adapter_source} declares ${object_name} but never uses it")
    endif()
endforeach()
string(FIND "${adapter_code}" "\nstatic struct MainArcadeRaceSetupCore s_mainArcadeRaceSetup;" state_at)
string(FIND "${adapter_code}" ")\n{" first_function_at)
if(first_function_at EQUAL -1 OR NOT state_at LESS first_function_at)
    message(FATAL_ERROR "${prefix}: ${adapter_source} must declare its state at file scope, before the first function")
endif()
file(GLOB replay_paths
    "${repo}/platform/native_replay*.c" "${repo}/platform/native_replay*.h"
    "${repo}/include/platform/native_replay*.h" "${repo}/game/MAIN/MainReplay*.c" "${repo}/game/MAIN/MainReplay*.h")
list(LENGTH replay_paths replay_count)
if(replay_count LESS 15)
    message(FATAL_ERROR "${prefix}: found only ${replay_count} replay modules; the scan is broken")
endif()
list(APPEND replay_paths "${repo}/platform/native_checkpoint.c" "${repo}/platform/native_checkpoint_file.c"
    "${repo}/include/platform/native_checkpoint.h")
foreach(path IN LISTS replay_paths)
    if(NOT EXISTS "${path}")
        continue()
    endif()
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" source)
    ctr_forbid("${relative_path}" "${source}" "MainArcadeRaceSetup")
    ctr_forbid("${relative_path}" "${source}" "MainArcadeRosterProof")
    ctr_forbid("${relative_path}" "${source}" "s_mainArcadeRaceSetup")
endforeach()

# 6. LOAD_Hub_ReadFile gains no hook.
set(hub_path "game/LOAD/LOAD_Hub.c")
ctr_read_source("${hub_path}" hub)
ctr_strip_comments("${hub}" hub_code)
ctr_find_block("${hub_path}" "${hub_code}" "void LOAD_Hub_ReadFile(struct BigHeader *bigfile, int levID, int packID)" hub_begin hub_end)
math(EXPR hub_length "${hub_end} - ${hub_begin} + 1")
string(SUBSTRING "${hub_code}" ${hub_begin} ${hub_length} hub_body)
foreach(term IN ITEMS MainArcadeRaceSetup MainArcadeRosterProof NativeArcadeRosterProof)
    ctr_forbid("${hub_path} (LOAD_Hub_ReadFile)" "${hub_body}" "${term}")
endforeach()
ctr_forbid("${hub_path}" "${hub}" "MainArcadeRaceSetup")

# 7. The unity chain.
set(unity_path "game/game_unity.h")
ctr_read_source("${unity_path}" unity)
ctr_require_order("${unity_path}" "${unity}"
    "#include \"MAIN/MainInit.c\""
    "#include \"MAIN/MainCanonicalTopologyLeaseRuntime.c\"" "#include \"MAIN/MainArcadeRaceSetup.c\""
    "#include \"230.c\"" "#include \"MAIN/MainArcadeLinkLayout.c\"" "#include \"MAIN/MainArcadeLink.c\""
    "#include \"MAIN/MainArcadeRosterProof.c\"" "#include \"231/R231.c\"")
string(REGEX MATCHALL "#include \"MAIN/MainCanonical[A-Za-z]*\\.c\"" canonical_includes "${unity}")
list(GET canonical_includes -1 last_canonical)
string(FIND "${unity}" "${last_canonical}" last_canonical_at)
string(FIND "${unity}" "#include \"MAIN/MainArcadeRaceSetup.c\"" adapter_at)
if(NOT adapter_at GREATER last_canonical_at)
    message(FATAL_ERROR "${prefix}: ${unity_path} must include the adapter after every MainCanonical* source (last: ${last_canonical})")
endif()
foreach(term IN ITEMS "MainArcadeRaceSetup.c\"" "MainArcadeRosterProof.c\"")
    string(REGEX MATCHALL "${term}" hits "${unity}")
    list(LENGTH hits count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${unity_path} must include ${term} exactly once (found ${count})")
    endif()
endforeach()
ctr_forbid("${unity_path}" "${unity}" "MainArcadeRaceSetupPlan.c")
ctr_forbid("${unity_path}" "${unity}" "MainArcadeRaceSetupFacts.c")
ctr_forbid("${unity_path}" "${unity}" "MainArcadeRaceSetupCore.c")

# 8. Link lines.
ctr_read_source("CMakeLists.txt" cmake)
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
if(native_link_start EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing target_link_libraries(ctr_native ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(REGEX REPLACE "[ \t\r\n]+" ";" native_link_items "${native_link_block}")
foreach(required IN ITEMS ctr_native_arcade_race_setup_plan ctr_native_arcade_race_setup_facts ctr_native_arcade_race_setup_core
        ctr_native_arcade_roster_proof)
    list(FIND native_link_items "${required}" required_at)
    if(required_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ctr_native must link ${required}")
    endif()
endforeach()
foreach(forbidden IN ITEMS ctr_native_arcade_setup_v4 ctr_native_canonical_projector_v4 ctr_native_bot_tick_evidence)
    list(FIND native_link_items "${forbidden}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ctr_native must not link ${forbidden} directly")
    endif()
endforeach()
set(proof_target ctr_native_arcade_roster_proof)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${proof_target}[ \t\r\n][^)]*\\)" proof_links "${cmake}")
list(LENGTH proof_links proof_link_count)
if(NOT proof_link_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one target_link_libraries(${proof_target} ...) call, found ${proof_link_count}")
endif()
list(GET proof_links 0 proof_link)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${proof_target}[ \t\r\n]+" "" proof_body "${proof_link}")
string(REGEX REPLACE "\\)$" "" proof_body "${proof_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" proof_items "${proof_body}")
list(REMOVE_ITEM proof_items "" PUBLIC PRIVATE INTERFACE)
list(SORT proof_items)
if(NOT "${proof_items}" STREQUAL "ctr_native_arcade_bot_rules;ctr_native_arcade_link_options;ctr_native_canonical_state;ctr_native_match_select_rules")
    message(FATAL_ERROR "${prefix}: ${proof_target} must link exactly the link options, the match-select rules, the bot rules, and the V1 canonical state (found '${proof_items}')")
endif()
string(FIND "${cmake}" "add_library(${proof_target} STATIC platform/native_arcade_roster_proof.c)" proof_declare_at)
if(proof_declare_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${proof_target} must build exactly platform/native_arcade_roster_proof.c")
endif()
string(SUBSTRING "${cmake}" ${proof_declare_at} 400 proof_block)
ctr_require_order("CMakeLists.txt (${proof_target})" "${proof_block}"
    "set_target_properties(${proof_target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF")

# 9. main.c: options, rejection, and configuration order.
ctr_read_source("main.c" main_source)
ctr_strip_comments("${main_source}" main_code)
set(proof_reject "if ((rosterProofOptions.enabled != 0u) &&\n\t    ((arcadeLinkOptions.enabled != 0u) || (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE) || NativeArg_NamesReplayOption(argc, argv) ||\n\t     NativeArcadeRosterProof_NamesExitOption(argc, argv)))")
ctr_find_block("main.c" "${main_code}" "${proof_reject}" reject_begin reject_end)
math(EXPR reject_length "${reject_end} - ${reject_begin} + 1")
string(SUBSTRING "${main_code}" ${reject_begin} ${reject_length} reject_block)
ctr_require_order("main.c (proof rejection)" "${reject_block}"
    "--arcade-roster-proof cannot be combined with --arcade-link, --arcade-link-preview, --exit-after-frame, or replay record or playback options."
    "return NativeConsole_Return(1);")
ctr_require_order("main.c" "${main_code}"
    "NativeArcadeLinkOptions_ApplyArgs(argc, argv, &arcadeLinkOptions)"
    "NativeArcadeRosterProofOptions_ApplyArgs(argc, argv, &rosterProofOptions)"
    "#if !defined(CTR_INTERNAL)"
    "--arcade-roster-proof is available in internal builds only."
    "${proof_reject}"
    "NativeReplayScheduler_PrepareReportFromArgs(argc, argv)"
    "NativeReplayScheduler_ConfigureFromArgs(argc, argv)"
    "NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr)"
    "NativeArcadeRosterProof_Configure(&rosterProofOptions, &rosterProofIdentity)"
    "CTR_Main()")
ctr_count_identifier("${main_code}" "NativeArcadeRosterProof_Configure" configure_hits)
if(NOT configure_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: main.c must call NativeArcadeRosterProof_Configure exactly once (found ${configure_hits})")
endif()

# 10. No exit looks like PASS while the proof is active.
ctr_require_order("main.c" "${main_code}"
    "NativeArcadeRosterProof_Configure(&rosterProofOptions, &rosterProofIdentity)"
    "const int result = NativeArcadeRosterProof_ExitCode(CTR_Main());")
ctr_count_identifier("${main_code}" "CTR_Main" ctr_main_hits)
if(NOT ctr_main_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: main.c must call CTR_Main exactly once, through NativeArcadeRosterProof_ExitCode (found ${ctr_main_hits})")
endif()
set(platform_path "platform/native_platform.c")
ctr_read_source("${platform_path}" platform)
ctr_strip_comments("${platform}" platform_code)
ctr_require("${platform_path}" "${platform}" "#include \"platform/native_arcade_roster_proof.h\"")
foreach(event_exit IN ITEMS
        "case SDL_EVENT_QUIT:\n#if defined(CTR_INTERNAL)\n\t\t\t\n\t\t\texit(NativeArcadeRosterProof_ExitCode(s_requestedExitCode));\n#else\n\t\t\texit(0);\n#endif"
        "case SDL_EVENT_WINDOW_CLOSE_REQUESTED:\n#if defined(CTR_INTERNAL)\n\t\t\t\n\t\t\texit(NativeArcadeRosterProof_ExitCode(0));\n#else\n\t\t\texit(0);\n#endif")
    ctr_require("${platform_path}" "${platform_code}" "${event_exit}")
endforeach()
ctr_count_identifier("${platform_code}" "NativeArcadeRosterProof_ExitCode" platform_exit_hits)
if(NOT platform_exit_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${platform_path} must exit through NativeArcadeRosterProof_ExitCode exactly twice (found ${platform_exit_hits})")
endif()
ctr_find_block("${proof_source}" "${proof_code}" "static void MainArcadeRosterProof_Finish(" finish_begin finish_end)
math(EXPR finish_length "${finish_end} - ${finish_begin} + 1")
string(SUBSTRING "${proof_code}" ${finish_begin} ${finish_length} finish_body)
ctr_require_order("${proof_source} (MainArcadeRosterProof_Finish)" "${finish_body}"
    "NativeArcadeRosterProof_RecordExitCode(exitCode);" "Platform_RequestExit(exitCode);")
foreach(term IN ITEMS NativeArcadeRosterProof_RecordExitCode Platform_RequestExit)
    ctr_count_identifier("${proof_code}" "${term}" term_hits)
    if(NOT term_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${proof_source} must call ${term} exactly once, in MainArcadeRosterProof_Finish (found ${term_hits})")
    endif()
endforeach()

# 11. The adapter's retail stores.
# Sets out_var to the retail stores in code: a member chain rooted at gGT,
# sdata, or data (not a member of something else), or at an alias of them,
# plain, parenthesized ((gGT)->x), or dereferenced ((*gGT).x), that is
# assigned, compound assigned, or incremented; an increment before it; its
# address taken; or a mem* call writing through it. An alias is a name
# declared as a pointer to a retail type (struct GameTracker, sData, Data, or
# Driver) or assigned a retail root (x = gGT; x = &data; x = sdata->gGT;).
function(ctr_retail_stores code out_var)
    string(REPLACE ";" "@SEMI@" masked "${code}")
    set(roots "gGT|sdata|data")
    string(REGEX MATCHALL "struct[ \t\r\n]+(GameTracker|sData|Data|Driver)[ \t\r\n]*\\*[ \t\r\n]*(const[ \t\r\n]+)?[A-Za-z_][A-Za-z0-9_]*" typed_aliases "${masked}")
    string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*&?[ \t]*(gGT|sdata|data)([ \t]*->[ \t]*gGT)?[ \t]*@SEMI@" assigned_aliases "${masked}")
    foreach(alias_text IN LISTS typed_aliases)
        string(REGEX MATCH "[A-Za-z_][A-Za-z0-9_]*$" alias "${alias_text}")
        string(APPEND roots "|${alias}")
    endforeach()
    foreach(alias_text IN LISTS assigned_aliases)
        string(REGEX MATCH "^[A-Za-z_][A-Za-z0-9_]*" alias "${alias_text}")
        string(APPEND roots "|${alias}")
    endforeach()
    set(root "(\\([ \t]*\\*?[ \t]*(${roots})[ \t]*\\)|(${roots}))")
    set(chain "${root}([ \t]*(->|\\.)[ \t]*[A-Za-z_][A-Za-z0-9_]*([ \t]*\\[[^]]*\\])*)+")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_>.])${chain}[ \t\r\n]*(=[^=]|[-+*/%&|^]=|<<=|>>=|\\+\\+|--)" assignments "${masked}")
    string(REGEX MATCHALL "(\\+\\+|--)[ \t]*[(]?[ \t]*${chain}" increments "${masked}")
    string(REGEX MATCHALL "(^|[^&])&[ \t]*[(]?[ \t]*${root}[ \t]*(->|\\.)" addresses "${masked}")
    string(REGEX MATCHALL "mem(set|cpy|move)[ \t]*\\([ \t]*[(]?[ \t]*&?[ \t]*[(]?[ \t]*\\*?[ \t]*(${roots})([^A-Za-z0-9_]|$)" mem_writes "${masked}")
    set(${out_var} ${assignments} ${increments} ${addresses} ${mem_writes} PARENT_SCOPE)
endfunction()
# The retail store scan must itself work.
foreach(probe IN ITEMS "gGT->gameMode1 = 1;" "sdata->advRng.state0 = 2;" "data.characterIDs[3] = 4;" "gGT->numLaps++;"
        "++sdata->randomNumber;" "memset(&sdata->advRng, 0, 8);" "sdata->gGT->boolDemoMode |= 1;"
        "p = &gGT->numLaps;" "memcpy(gGT, &x, 4);" "data.characterIDs[op->index]=5;" "x = 1; sdata->audioRNG -= 3;"
        "(*gGT).gameMode1 = 1;" "(gGT)->numLaps = 2;" "( * sdata ).audioRNG = 3;" "(data).characterIDs[1] = 2;"
        "++(*gGT).numLaps;" "p = &(*gGT).numLaps;" "memset(*gGT, 0, 4);"
        "struct GameTracker *tracker = sdata->gGT; tracker->numLaps = 2;" "struct sData *s = x; s->audioRNG = 1;"
        "struct Data *d = &data; d->characterIDs[0] = 1;" "t = gGT; t->gameMode2 |= 4;" "q = &data; (*q).characterIDs[2] = 3;"
        "r = sdata->gGT; ++r->numLaps;" "struct Driver *driver = gGT->drivers[0]; driver->numWumpas = 3;")
    ctr_retail_stores("${probe}" probe_stores)
    list(LENGTH probe_stores probe_store_count)
    if(probe_store_count LESS 1)
        message(FATAL_ERROR "${prefix}: the retail store scan missed the store in '${probe}'")
    endif()
endforeach()
foreach(probe IN ITEMS "x = gGT->gameMode1;" "if (gGT->levelID == 3)" "view->gameMode1 = (uint32_t)gGT->gameMode1;"
        "fields->levelID = (int32_t)gGT->levelID;" "if ((gGT != NULL) && f(gGT))" "y = (sdata->advRng.state0 >= 2);"
        "snapshot->characterIDs[slot] = (int16_t)data.characterIDs[slot];" "z = sdata->kartSpawnOrderArray[slot] != 1;"
        "stored->audioRNG = (uint32_t)sdata->audioRNG;" "memset(view, 0, sizeof(*view));" "f(gGT, sdata, &x);"
        "view->gameMode1 = (uint32_t)(*gGT).gameMode1;" "x = (gGT)->numLaps;" "if ((*sdata).audioRNG == 2)"
        "const struct Driver *driver = gGT->drivers[slot]; y = driver->numWumpas;" "t = gGT; z = t->numLaps;")
    ctr_retail_stores("${probe}" probe_stores)
    list(LENGTH probe_stores probe_store_count)
    if(NOT probe_store_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: the retail store scan flags a read in '${probe}' (${probe_stores})")
    endif()
endforeach()

ctr_find_block("${adapter_source}" "${adapter_code}"
    "static void MainArcadeRaceSetup_Apply(struct GameTracker *gGT, const struct MainArcadeRaceSetupCoreOutcome *outcome)"
    apply_begin apply_end)
math(EXPR apply_length "${apply_end} - ${apply_begin} + 1")
string(SUBSTRING "${adapter_code}" ${apply_begin} ${apply_length} apply_body)
string(SUBSTRING "${adapter_code}" 0 ${apply_begin} before_apply)
math(EXPR after_apply_at "${apply_end} + 1")
string(SUBSTRING "${adapter_code}" ${after_apply_at} -1 after_apply)

# Every write target the core declares, NONE excluded.
set(core_header_path "game/MAIN/MainArcadeRaceSetupCore.h")
ctr_read_source("${core_header_path}" core_header)
ctr_strip_comments("${core_header}" core_header_code)
string(REGEX MATCHALL "MAIN_ARCADE_RACE_SETUP_CORE_TARGET_[A-Z0-9_]+" declared_targets "${core_header_code}")
list(REMOVE_DUPLICATES declared_targets)
list(REMOVE_ITEM declared_targets MAIN_ARCADE_RACE_SETUP_CORE_TARGET_NONE)
set(expected_GAME_MODE1 "gGT->gameMode1 = (int)(uint32_t)op->value;")
set(expected_GAME_MODE2 "gGT->gameMode2 = (int)(uint32_t)op->value;")
set(expected_ARCADE_DIFFICULTY "gGT->arcadeDifficulty = (int)(int32_t)op->value;")
set(expected_BOOL_DEMO_MODE "gGT->boolDemoMode = (char)(uint8_t)op->value;")
set(expected_NUM_LAPS "gGT->numLaps = (s8)(int8_t)op->value;")
set(expected_NUM_PLYR_NEXT_GAME "gGT->numPlyrNextGame = (u8)(uint8_t)op->value;")
set(expected_CHARACTER_ID "if (op->index < MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT) { data.characterIDs[op->index] = (s16)(int16_t)op->value; }")
set(expected_REQUEST_LOAD "MainRaceTrack_RequestLoad((s16)(int32_t)op->value);")
set(expected_RANDOM_NUMBER "sdata->randomNumber = (int)(uint32_t)op->value;")
set(expected_ADV_RNG0 "sdata->advRng.state0 = (uint32_t)op->value;")
set(expected_ADV_RNG1 "sdata->advRng.state1 = (uint32_t)op->value;")
set(expected_PSX_RAND_SEED "PSX_BIOS_SetRandSeed((uint32_t)op->value);")
set(expected_AUDIO_RNG "sdata->audioRNG = (uint32_t)op->value;")
set(expected_TIMER "gGT->timer = (int)(int32_t)op->value;")
set(expected_FRAME_TIMER_CONFETTI "gGT->frameTimer_Confetti = (int)(int32_t)op->value;")
list(LENGTH declared_targets declared_target_count)
if(NOT declared_target_count EQUAL 15)
    message(FATAL_ERROR "${prefix}: ${core_header_path} declares ${declared_target_count} write targets besides NONE, expected 15; map every new target here and in MainArcadeRaceSetup_Apply")
endif()
set(apply_remaining "${apply_body}")
foreach(target IN LISTS declared_targets)
    string(REPLACE "MAIN_ARCADE_RACE_SETUP_CORE_TARGET_" "" short "${target}")
    if(NOT DEFINED expected_${short})
        message(FATAL_ERROR "${prefix}: no pinned retail field for ${target}; add it to this test")
    endif()
    set(label "case ${target}:")
    string(FIND "${apply_body}" "${label}" label_at)
    string(FIND "${apply_body}" "${label}" label_last REVERSE)
    if(label_at EQUAL -1 OR NOT label_at EQUAL label_last)
        message(FATAL_ERROR "${prefix}: MainArcadeRaceSetup_Apply must handle ${target} in exactly one case")
    endif()
    string(SUBSTRING "${apply_body}" ${label_at} -1 case_tail)
    string(FIND "${case_tail}" "break;" break_at)
    if(break_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: the ${target} case of MainArcadeRaceSetup_Apply has no break")
    endif()
    math(EXPR case_length "${break_at} + 6")
    string(SUBSTRING "${case_tail}" 0 ${case_length} case_text)
    string(REGEX REPLACE "[ \t\r\n]+" " " case_normalized "${case_text}")
    set(case_expected "${label} ${expected_${short}} break;")
    if(NOT case_normalized STREQUAL case_expected)
        message(FATAL_ERROR "${prefix}: MainArcadeRaceSetup_Apply must map ${target} to exactly '${expected_${short}}' (found '${case_normalized}')")
    endif()
    string(REPLACE "${case_text}" "" apply_remaining "${apply_remaining}")
endforeach()
string(REGEX MATCHALL "case[ \t]" apply_cases "${apply_body}")
list(LENGTH apply_cases apply_case_count)
if(NOT apply_case_count EQUAL declared_target_count)
    message(FATAL_ERROR "${prefix}: MainArcadeRaceSetup_Apply has ${apply_case_count} cases, expected one per target (${declared_target_count})")
endif()
string(REGEX REPLACE "[ \t\r\n]+" " " apply_remaining_normalized "${apply_remaining}")
ctr_require("${adapter_source} (MainArcadeRaceSetup_Apply)" "${apply_remaining_normalized}" "default: break; }")
ctr_retail_stores("${apply_remaining}" apply_stray_stores)
if(NOT "${apply_stray_stores}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: MainArcadeRaceSetup_Apply stores outside its target cases (${apply_stray_stores})")
endif()
foreach(term IN ITEMS PSX_BIOS_SetRandSeed MainRaceTrack_RequestLoad)
    ctr_count_identifier("${adapter_code}" "${term}" term_hits)
    if(NOT term_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${adapter_source} must call ${term} exactly once, in its MainArcadeRaceSetup_Apply case (found ${term_hits})")
    endif()
endforeach()

# Outside MainArcadeRaceSetup_Apply the adapter stores to no retail field.
ctr_retail_stores("${before_apply}${after_apply}" outside_stores)
if(NOT "${outside_stores}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ${adapter_source} stores to retail state outside MainArcadeRaceSetup_Apply (${outside_stores})")
endif()

# 11, continued. The boot-relative pins (RS-17, R-6c): the core names each pin target
# exactly once, inside MainArcadeRaceSetupCore_OnFinalizeInitBegin, after the
# load-field verification and the seed derivation and between the re-applied
# mode fields and the first seed, with the documented values.
ctr_find_block("${core_source}" "${core_code}" "int MainArcadeRaceSetupCore_OnFinalizeInitBegin(" begin_body_begin begin_body_end)
math(EXPR begin_body_length "${begin_body_end} - ${begin_body_begin} + 1")
string(SUBSTRING "${core_code}" ${begin_body_begin} ${begin_body_length} begin_body)
foreach(pin IN ITEMS MAIN_ARCADE_RACE_SETUP_CORE_TARGET_TIMER MAIN_ARCADE_RACE_SETUP_CORE_TARGET_FRAME_TIMER_CONFETTI)
    ctr_count_identifier("${core_code}" "${pin}" pin_hits)
    ctr_count_identifier("${begin_body}" "${pin}" pin_body_hits)
    if(NOT pin_hits EQUAL 1 OR NOT pin_body_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${core_source} must name ${pin} exactly once, in MainArcadeRaceSetupCore_OnFinalizeInitBegin (found ${pin_hits}, ${pin_body_hits} there)")
    endif()
endforeach()
ctr_require_order("${core_source} (MainArcadeRaceSetupCore_OnFinalizeInitBegin)" "${begin_body}"
    "if (core->status != (uint32_t)MAIN_ARCADE_RACE_SETUP_LAUNCHED)"
    "view->fields.levelID != core->plan.levelID"
    "NativeArcadeBotRules_DeriveRetailSeedsV1(&scratch->seedBank, &seeds)"
    "pins.timer = (int32_t)MAIN_ARCADE_RACE_SETUP_CORE_PIN_TIMER;"
    "pins.frameTimerConfetti = (int32_t)MAIN_ARCADE_RACE_SETUP_CORE_PIN_FRAME_TIMER_CONFETTI;"
    "MainArcadeRaceSetupCore_PushModeFields(outcome, &fields);"
    "MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_TIMER, 0u, (int64_t)pins.timer);"
    "MainArcadeRaceSetupCore_Push(outcome, MAIN_ARCADE_RACE_SETUP_CORE_TARGET_FRAME_TIMER_CONFETTI, 0u,"
    "MAIN_ARCADE_RACE_SETUP_CORE_TARGET_RANDOM_NUMBER")
ctr_require("${core_header_path}" "${core_header_code}" "#define MAIN_ARCADE_RACE_SETUP_CORE_PIN_TIMER 0\n")
ctr_require("${core_header_path}" "${core_header_code}" "#define MAIN_ARCADE_RACE_SETUP_CORE_PIN_FRAME_TIMER_CONFETTI 0\n")

# 12. The proof's scripted pads and per-tick digests (R-6).
set(mainmain_path "game/MAIN/MainMain.c")
ctr_read_source("${mainmain_path}" mainmain)
ctr_strip_comments("${mainmain}" mainmain_code)
# Who names the new entry points: MainArcadeRosterProof_BeginFrame and
# _EndFrame only the proof files and MainMain.c; _Start only the proof files
# and main.c.
set(proof_entry_callers_BeginFrame "${proof_source}" "${proof_header}" "${mainmain_path}")
set(proof_entry_callers_EndFrame "${proof_source}" "${proof_header}" "${mainmain_path}")
set(proof_entry_callers_Start "${proof_source}" "${proof_header}" "main.c")
set(entry_scanned 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR entry_scanned "${entry_scanned} + 1")
    file(READ "${path}" source)
    string(FIND "${source}" "MainArcadeRosterProof_" raw_entry_hit)
    if(raw_entry_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    foreach(entry IN ITEMS BeginFrame EndFrame Start)
        list(FIND proof_entry_callers_${entry} "${relative_path}" entry_caller_at)
        if(NOT entry_caller_at EQUAL -1)
            continue()
        endif()
        ctr_count_identifier("${code}" "MainArcadeRosterProof_${entry}" entry_hits)
        if(entry_hits GREATER 0)
            message(FATAL_ERROR "${prefix}: ${relative_path} names MainArcadeRosterProof_${entry}; only ${proof_entry_callers_${entry}} may")
        endif()
    endforeach()
endforeach()
if(entry_scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${entry_scanned} files for the proof entry points; the scan is broken")
endif()
# Each new entry point returns first, before touching anything, when the
# proof is inactive.
foreach(opener IN ITEMS "int MainArcadeRosterProof_Start(void)" "int MainArcadeRosterProof_BeginFrame(void)"
        "void MainArcadeRosterProof_EndFrame(struct GameTracker *gGT, const struct NativeCanonicalStateV1 *frameState)")
    ctr_find_block("${proof_source}" "${proof_code}" "${opener}" entry_begin entry_end)
    math(EXPR entry_length "${entry_end} - ${entry_begin} + 1")
    string(SUBSTRING "${proof_code}" ${entry_begin} ${entry_length} entry_body)
    if(NOT entry_body MATCHES "^\\{([ \t\r\n]*(const[ \t]+)?(struct[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\\*?|u?int[0-9]*_t|int)[ \t]*[A-Za-z_][A-Za-z0-9_]*(\\[[A-Z_]+\\])?( = &s_[A-Za-z]+)?;)*[ \t\r\n]*if \\(!NativeArcadeRosterProof_Active\\(\\)\\)[ \t\r\n]*\\{[ \t\r\n]*return( [01])?;")
        message(FATAL_ERROR "${prefix}: '${opener}' must return first, before touching anything, when the proof is inactive")
    endif()
endforeach()
# MainMain.c: BeginFrame once, before GAMEPAD_ProcessAnyoneVars; EndFrame
# once, after the frame was rendered; both inside CTR_NATIVE && CTR_INTERNAL.
foreach(entry IN ITEMS MainArcadeRosterProof_BeginFrame MainArcadeRosterProof_EndFrame)
    ctr_count_identifier("${mainmain_code}" "${entry}" entry_hits)
    if(NOT entry_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${mainmain_path} must call ${entry} exactly once (found ${entry_hits})")
    endif()
endforeach()
ctr_require_order("${mainmain_path}" "${mainmain_code}"
    "#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)\n#include \"MAIN/MainArcadeRosterProof.h\""
    "NativeReplayScheduler_BeginFrame(&replayFrameInfo)"
    "rosterProofActive = MainArcadeRosterProof_BeginFrame();"
    "#endif"
    "GAMEPAD_ProcessAnyoneVars(gGS);"
    "MainFrame_RenderFrame(gGT, gGS);"
    "canonicalStateArg = &canonicalState;"
    "else if (rosterProofActive != 0)"
    "MainArcadeRosterProof_EndFrame(gGT, rosterProofState);"
    "NativeReplayScheduler_EndFrame(&replayFrameInfo, canonicalStateArg)")
# Every use of the proof's names in MainMain.c sits inside a
# CTR_NATIVE && CTR_INTERNAL block (the preprocessor lines are kept).
string(REGEX MATCHALL "#[ \t]*(if|ifdef|ifndef|else|elif|endif)[^\n]*|MainArcadeRosterProof_[A-Za-z]+|rosterProof[A-Za-z]*" mainmain_tokens "${mainmain_code}")
set(guard_stack "")
foreach(token IN LISTS mainmain_tokens)
    if(token MATCHES "^#[ \t]*(if|ifdef|ifndef)")
        list(APPEND guard_stack "${token}")
    elseif(token MATCHES "^#[ \t]*endif")
        list(POP_BACK guard_stack)
    elseif(token MATCHES "^#")
        list(POP_BACK guard_stack)
        list(APPEND guard_stack "${token}")
    else()
        list(FIND guard_stack "#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)" guard_at)
        if(guard_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${mainmain_path} names ${token} outside #if defined(CTR_NATIVE) && defined(CTR_INTERNAL)")
        endif()
    endif()
endforeach()
# The proof's V1 state is local only: the scheduler still gets exactly its own
# state, assigned in one place, and the proof's never.
ctr_count_identifier("${mainmain_code}" "canonicalStateArg" state_arg_hits)
if(NOT state_arg_hits EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${mainmain_path} must declare, assign once, and pass canonicalStateArg (found ${state_arg_hits} names)")
endif()
ctr_count_identifier("${mainmain_code}" "rosterProofState" proof_state_hits)
if(NOT proof_state_hits EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${mainmain_path} must name rosterProofState only to declare it, set it, and hand it to MainArcadeRosterProof_EndFrame (found ${proof_state_hits})")
endif()
# main.c installs the neutral pads once, after the proof was configured and
# before CTR_Main, in internal builds only.
ctr_count_identifier("${main_code}" "MainArcadeRosterProof_Start" start_hits)
if(NOT start_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: main.c must call MainArcadeRosterProof_Start exactly once (found ${start_hits})")
endif()
ctr_require_order("main.c" "${main_code}"
    "NativeArcadeRosterProof_Configure(&rosterProofOptions, &rosterProofIdentity)"
    "#if defined(CTR_INTERNAL)\n\t\t\n\t\tif (!MainArcadeRosterProof_Start())"
    "#endif"
    "CTR_Main()")
# The proof extracts only the Meta candidate (no Physics group, no detailed
# assembly), exactly once, and hashes it only through the canonical encoder.
ctr_count_identifier("${proof_code}" "MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta" meta_hits)
if(NOT meta_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${proof_source} must call MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta exactly once (found ${meta_hits})")
endif()
string(REGEX MATCHALL "MainCanonicalDrivers_[A-Za-z]+" proof_driver_calls "${proof_code}")
list(REMOVE_DUPLICATES proof_driver_calls)
if(NOT "${proof_driver_calls}" STREQUAL "MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta")
    message(FATAL_ERROR "${prefix}: ${proof_source} may name only MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta of the drivers module (found ${proof_driver_calls})")
endif()
foreach(term IN ITEMS NativeCanonicalDriversDetailedV1_BuildSummary "NativeCanonicalDriversV1 " NativeCanonicalStateV3
        NativeCanonicalStateV4 Platform_InputCapturePadSnapshots NativeReplayScheduler)
    ctr_forbid("${proof_source}" "${proof_code}" "${term}")
endforeach()
string(REGEX MATCHALL "[A-Za-z_]*Physics[A-Za-z_]*" physics_names "${proof_code}")
if(NOT "${physics_names}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ${proof_source} names a Physics symbol (${physics_names}); the proof's drivers digest excludes physics")
endif()
ctr_count_identifier("${proof_code}" "NativeCanonicalDriversDetailedV1_Encode" encode_hits)
if(NOT encode_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${proof_source} must encode the drivers record with NativeCanonicalDriversDetailedV1_Encode exactly once (found ${encode_hits})")
endif()
ctr_count_identifier("${proof_code}" "Platform_InputInstallPadSnapshots" install_hits)
if(NOT install_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${proof_source} must install its pads through Platform_InputInstallPadSnapshots in one place (found ${install_hits})")
endif()
