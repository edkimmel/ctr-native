# Structural isolation for the arcade-link top LOD tier
# (docs/SOLO_CAB_MILESTONE.md section 8). In LINK mode only, every instance is
# drawn at its model's top LOD tier and no kart is drawn through the
# multiplayer impostor (DecalMP). The decision is the pure
# MainArcadeLinkPolicy_ForceTopLod (LINK -> 1; unit-tested in
# tests/main_arcade_link_policy_test.c); the only game-side reader is the thin
# accessor MainArcadeLink_ForceTopLod, and this test pins that:
#   1. the policy and accessor bodies;
#   2. the accessor is named only by its own header and source and the two
#      render call sites (RenderBucket_QueueExecute.c, DecalMP.c), once each,
#      each call (and each include of MainArcadeLink.h) inside an
#      #if defined(CTR_NATIVE) block, so no canonical, digest, replay,
#      checkpoint, or topology-lease file names it;
#   3. the retail LOD walk RenderBucket_SelectModelHeader is unchanged, with
#      its cull (lodExhausted, no draw) when a draw is past every header, and
#      QueueDraw applies the top tier only after that walk found a header;
#   4. RenderBucket_ForceTopLodHeader only reads (it writes nothing but its
#      lodIndex output), and the animation advance keeps the retail tier's
#      frame count;
#   5. in LINK, DecalMP_01 drops only PUSHBUFFER_EXISTS and the per-camera
#      pushBuffer redirect (the impostor); every DecalMP entry write still
#      runs, because a retail out-of-bounds read of the missile target check
#      reads those entries (docs/SOLO_CAB_MILESTONE.md section 8);
#      DecalMP_02 and DecalMP_03 are retail, and DecalMP_01 is called only
#      from the render frame;
#   6. the model headers are not edited: maxDistanceLOD is written only by
#      the retail cutscene opcode in game/233/CS_Thread.c.


set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade link top lod isolation")

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

set(accessor "MainArcadeLink_ForceTopLod")
set(accessor_call "MainArcadeLink_ForceTopLod()")
set(policy_fn "MainArcadeLinkPolicy_ForceTopLod")
set(top_header_fn "RenderBucket_ForceTopLodHeader")
set(bucket_path "game/RenderBucket/RenderBucket_QueueExecute.c")
set(decal_path "game/DecalMP.c")
set(render_frame_path "game/MAIN/MainFrame_RenderFrame.c")
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

# 2. Who names the accessor, the policy function, and the top-header helper,
#    and who writes maxDistanceLOD (item 6): game code, the host glue, and
#    main.c are scanned (comments stripped).
file(GLOB_RECURSE scan_paths
    "${repo}/game/*.c" "${repo}/game/*.h"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/*.h")
list(APPEND scan_paths "${repo}/main.c")
set(accessor_files "")
set(policy_files "")
set(top_header_files "")
set(max_lod_writers "")
foreach(path IN LISTS scan_paths)
    file(READ "${path}" source)
    string(FIND "${source}" "ForceTopLod" quick)
    string(FIND "${source}" "maxDistanceLOD" lod_quick)
    if(quick EQUAL -1 AND lod_quick EQUAL -1)
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
    string(FIND "${code}" "${top_header_fn}" top_header_at)
    if(NOT top_header_at EQUAL -1)
        list(APPEND top_header_files "${relative}")
    endif()
    # Any assignment to a maxDistanceLOD field (=, |=, +=, ...; not ==).
    if(code MATCHES "maxDistanceLOD[ \t\r\n]*([-+*/%&|^]|<<|>>)?=[^=]")
        list(APPEND max_lod_writers "${relative}")
    endif()
endforeach()
list(SORT accessor_files)
set(expected_accessor_files "${bucket_path}" "${decal_path}" "${hook_path}" "${hook_header_path}")
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
if(NOT "${top_header_files}" STREQUAL "${bucket_path}")
    message(FATAL_ERROR "${prefix}: ${top_header_fn} may be named only by ${bucket_path} (found ${top_header_files})")
endif()
# The list above already excludes every other file. This names the families
# that must never read the forced tier, so a later widening of the allowed
# list cannot quietly include one.
foreach(relative IN LISTS accessor_files policy_files)
    if(relative MATCHES "(MainCanonical|MainArcadeRaceDigest|native_canonical|native_checkpoint|native_replay|native_savestate|native_lockstep|TopologyLease)")
        message(FATAL_ERROR "${prefix}: ${relative} is a canonical, digest, replay, checkpoint, lockstep, or lease file and must not name ${accessor} or ${policy_fn}")
    endif()
endforeach()

# 2b. Each call site calls the accessor exactly once, inside a CTR_NATIVE
#     block, and includes the hook header inside a CTR_NATIVE block.
foreach(site IN ITEMS "${bucket_path}" "${decal_path}")
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

# 3. The retail LOD walk is unchanged, including its cull: past the last
#    header it sets lodExhausted and returns no header.
ctr_read_source("${bucket_path}" bucket_source)
ctr_strip_comments("${bucket_source}" bucket_code)
ctr_block_text("${bucket_path}" "${bucket_code}" "static struct ModelHeader *RenderBucket_SelectModelHeader(" select_block)
ctr_squash("${select_block}" select_squashed)
set(expected_select "{structModelHeader*mh;intheadersRemaining;intlodIndex;intprojectedDistance;*lodIndexOut=0;*lodExhaustedOut=0;if(inst->model->numHeaders<=0){return0;}if(pb->distanceToScreen_PREV==0){return0;}projectedDistance=(int)(u32)((s64)(pb->rect.w>>1)*viewDepth)/pb->distanceToScreen_PREV;mh=inst->model->headers;headersRemaining=inst->model->numHeaders;lodIndex=0;for(;;){if(RenderBucket_MipsSub(projectedDistance,(u16)mh->maxDistanceLOD)<0){*lodIndexOut=lodIndex;returnmh;}lodIndex++;headersRemaining--;mh++;if(headersRemaining==0){break;}}*lodExhaustedOut=1;return0;}")
if(NOT select_squashed STREQUAL "${expected_select}")
    message(FATAL_ERROR "${prefix}: RenderBucket_SelectModelHeader must stay the retail LOD walk and cull (found '${select_squashed}')")
endif()

# 3b. QueueDraw: the retail walk, its unchanged no-header exit, and only then
#     the LINK top tier, which is what idpp->mh and idpp->lodIndex store.
ctr_block_text("${bucket_path}" "${bucket_code}" "static struct RenderBucketEntry *RenderBucket_QueueDraw(" queue_draw)
ctr_require_order("${bucket_path} (QueueDraw)" "${queue_draw}"
    "mh = RenderBucket_SelectModelHeader(inst, pb, &lodIndex, &lodExhausted, viewDepth);"
    "if (mh == 0)"
    "retailMh = mh;"
    "if (${accessor_call} != 0)"
    "mh = ${top_header_fn}(inst, retailMh, &lodIndex);"
    "idpp->mh = mh;"
    "idpp->lodIndex = lodIndex;"
    "RenderBucket_BuildM3x3(inst, mh, viewDepth, &matrixState);"
    "frame = RenderBucket_GetFrame(inst, mh, &nextFrame, &deltaArray, &lastFrameAdvance);"
    "if (frame == 0)"
    "if (mh != retailMh)"
    "(void)RenderBucket_GetFrame(inst, retailMh, &retailNextFrame, &retailDeltaArray, &lastFrameAdvance);"
    "RenderBucket_AdvanceInstanceAnimWord(inst, gameMode1, playerIndex, lastFrameAdvance, &queuedFlags);")
string(FIND "${queue_draw}" "mh = RenderBucket_SelectModelHeader(" select_call_at)
string(SUBSTRING "${queue_draw}" ${select_call_at} -1 after_select)
ctr_block_text("${bucket_path} (QueueDraw)" "${after_select}" "if (mh == 0)" no_header_block)
ctr_squash("${no_header_block}" no_header_squashed)
if(NOT no_header_squashed STREQUAL "{if(lodExhausted!=0){idpp->mh=0;}idpp->instFlags=queuedFlags;returnrbi;}")
    message(FATAL_ERROR "${prefix}: QueueDraw's no-header exit (the LOD cull) must stay retail (found '${no_header_squashed}')")
endif()
ctr_require_native_guard("${bucket_path} (QueueDraw)" "${queue_draw}" "retailMh = mh;")
ctr_require_native_guard("${bucket_path} (QueueDraw)" "${queue_draw}" "if (mh != retailMh)")
ctr_block_text("${bucket_path} (QueueDraw)" "${queue_draw}" "if (mh != retailMh)" retail_count_block)
ctr_squash("${retail_count_block}" retail_count_squashed)
if(NOT retail_count_squashed STREQUAL "{structModelFrame*retailNextFrame;intretailDeltaArray;(void)RenderBucket_GetFrame(inst,retailMh,&retailNextFrame,&retailDeltaArray,&lastFrameAdvance);}")
    message(FATAL_ERROR "${prefix}: QueueDraw's retail frame count block must only reread lastFrameAdvance from the retail tier (found '${retail_count_squashed}')")
endif()
ctr_count("${queue_draw}" "retailMh" queue_draw_retail_names)
if(NOT queue_draw_retail_names EQUAL 5)
    message(FATAL_ERROR "${prefix}: QueueDraw must name retailMh only in its declaration, its set, the top-tier call, the tier compare, and the retail frame count (found ${queue_draw_retail_names})")
endif()

# 4. The top-header helper: a native-only read that writes nothing but its
#    lodIndex output, and keeps the retail tier when the walk already chose
#    the top one or the top one has no frame.
ctr_require_native_guard("${bucket_path}" "${bucket_code}" "static struct ModelHeader *${top_header_fn}(")
ctr_block_text("${bucket_path}" "${bucket_code}" "static struct ModelHeader *${top_header_fn}(" top_block)
ctr_squash("${top_block}" top_squashed)
set(expected_top "{structModelHeader*top=inst->model->headers;structModelFrame*topNextFrame;inttopDeltaArray;inttopLastFrame;inttopIndex=0;while((top!=retailMh)&&((u16)top->maxDistanceLOD==0)){top++;topIndex++;}if(top==retailMh){returnretailMh;}if(RenderBucket_GetFrame(inst,top,&topNextFrame,&topDeltaArray,&topLastFrame)==0){returnretailMh;}*lodIndex=topIndex;returntop;}")
if(NOT top_squashed STREQUAL "${expected_top}")
    message(FATAL_ERROR "${prefix}: ${top_header_fn} must stay the read-only top-tier pick (found '${top_squashed}')")
endif()
ctr_count("${bucket_code}" "${top_header_fn}(" top_header_names)
if(NOT top_header_names EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${top_header_fn} must be defined once and called once, from QueueDraw (found ${top_header_names})")
endif()

# 5. DecalMP_01: the accessor is read once, before the loops; in LINK it
#    drops only PUSHBUFFER_EXISTS (PIXEL_LOD stays) and the per-camera
#    pushBuffer redirect. Every DecalMP entry write still runs, unguarded
#    and in retail order, because the retail missile target check of a bot
#    reads gGT->pushBuffer[driverID] past the four cameras, into these
#    entries (entry 1's inst pointer for bot 5). DecalMP_02 and DecalMP_03
#    are retail.
ctr_read_source("${decal_path}" decal_source)
ctr_strip_comments("${decal_source}" decal_code)
ctr_block_text("${decal_path}" "${decal_code}" "void DecalMP_01(struct GameTracker *gGT)" decal01)
set(decal_flag_decl "const int forceTopLod = ${accessor_call};")
set(decal_flag_set "inst->flags |= (forceTopLod != 0) ? PIXEL_LOD : (PUSHBUFFER_EXISTS | PIXEL_LOD);")
ctr_require_order("${decal_path} (DecalMP_01)" "${decal01}"
    "if (gGT->numPlyrCurrGame == 0)"
    "${decal_flag_decl}"
    "int entryIndex = 0;"
    "for (int cameraID = 0; cameraID < gGT->numPlyrCurrGame; cameraID++)"
    "for (int driverID = 0; driverID < 8; driverID++)"
    "struct Instance *inst = driver->instSelf;"
    "${decal_flag_set}"
    "#else"
    "inst->flags |= PUSHBUFFER_EXISTS | PIXEL_LOD;"
    "#endif"
    "if (driverID == cameraID)"
    "struct DecalMPEntry *entry = DecalMP_GetEntry(gGT, entryIndex++);"
    "entry->kartState = KS_BLASTED;"
    "entry->kartState = 0;"
    "entry->timer = 1000;"
    "entry->pb.matrix_ViewProj = pb->matrix_ViewProj;"
    "entry->pb.pos = pb->pos;"
    "entry->pb.distanceToScreen_PREV = pb->distanceToScreen_PREV;"
    "entry->pb.rect = pb->rect;"
    "entry->pb.ptrOT = pb->ptrOT;"
    "entry->pb.cameraID = pb->cameraID;"
    "struct InstDrawPerPlayer *idpp = DecalMP_GetIdpp(inst, cameraID);"
    "if (forceTopLod == 0)"
    "idpp->pushBuffer = &entry->pb;"
    "entry->inst = inst;")
ctr_require_native_guard("${decal_path} (DecalMP_01)" "${decal01}" "${decal_flag_decl}")
ctr_require_native_guard("${decal_path} (DecalMP_01)" "${decal01}" "${decal_flag_set}")
ctr_require_native_guard("${decal_path} (DecalMP_01)" "${decal01}" "if (forceTopLod == 0)")
ctr_count("${decal01}" "forceTopLod" decal_flag_names)
if(NOT decal_flag_names EQUAL 3)
    message(FATAL_ERROR "${prefix}: DecalMP_01 must name forceTopLod only to set it, in the flag choice, and in the pushBuffer gate (found ${decal_flag_names})")
endif()
ctr_count("${decal01}" "return" decal01_returns)
if(NOT decal01_returns EQUAL 1)
    message(FATAL_ERROR "${prefix}: DecalMP_01 must keep only its retail early return, so no entry write is skipped (found ${decal01_returns})")
endif()
# The entry writes sit in plain code: no preprocessor line and no jump
# between the entry fetch and the idpp fetch.
string(FIND "${decal01}" "struct DecalMPEntry *entry = DecalMP_GetEntry(gGT, entryIndex++);" entry_writes_at)
string(FIND "${decal01}" "struct InstDrawPerPlayer *idpp = DecalMP_GetIdpp(inst, cameraID);" idpp_fetch_at)
math(EXPR entry_writes_length "${idpp_fetch_at} - ${entry_writes_at}")
string(SUBSTRING "${decal01}" ${entry_writes_at} ${entry_writes_length} entry_writes)
if(entry_writes MATCHES "#|return|continue|goto|break")
    message(FATAL_ERROR "${prefix}: the DecalMP entry writes in DecalMP_01 must run unconditionally in every mode (found '${CMAKE_MATCH_0}')")
endif()
# The gate holds exactly the redirect: #endif, then its block.
string(FIND "${decal01}" "if (forceTopLod == 0)" gate_at)
string(SUBSTRING "${decal01}" ${gate_at} -1 from_gate)
string(FIND "${from_gate}" "{" gate_brace_at)
string(SUBSTRING "${from_gate}" 0 ${gate_brace_at} gate_between)
if(NOT gate_between MATCHES "^if \\(forceTopLod == 0\\)[ \t\r\n]*#endif[ \t\r\n]*$")
    message(FATAL_ERROR "${prefix}: 'if (forceTopLod == 0)' in ${decal_path} must be followed only by #endif and its block")
endif()
ctr_block_text("${decal_path} (DecalMP_01)" "${from_gate}" "if (forceTopLod == 0)" gate_block)
ctr_squash("${gate_block}" gate_squashed)
if(NOT gate_squashed STREQUAL "{idpp->pushBuffer=&entry->pb;}")
    message(FATAL_ERROR "${prefix}: the forceTopLod gate in DecalMP_01 must hold only the pushBuffer redirect (found '${gate_squashed}')")
endif()
foreach(retail_fn IN ITEMS "void DecalMP_02(struct GameTracker *gGT)" "void DecalMP_03(struct GameTracker *gGT)")
    ctr_block_text("${decal_path}" "${decal_code}" "${retail_fn}" retail_block)
    ctr_require_order("${decal_path} (${retail_fn})" "${retail_block}"
        "struct DecalMPEntry *entry = DecalMP_GetEntry(gGT, index);"
        "if (entry->inst == NULL)"
        "return;"
        "struct InstDrawPerPlayer *idpp = DecalMP_GetIdpp(entry->inst, cameraID);"
        "(idpp->instFlags & 0x140)")
    string(FIND "${retail_block}" "forceTopLod" retail_flag_at)
    if(NOT retail_flag_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: '${retail_fn}' must stay retail (it names forceTopLod)")
    endif()
endforeach()
file(GLOB_RECURSE game_sources "${repo}/game/*.c")
set(decal01_callers "")
foreach(path IN LISTS game_sources)
    file(READ "${path}" source)
    string(FIND "${source}" "DecalMP_01(" decal_quick)
    if(decal_quick EQUAL -1)
        continue()
    endif()
    file(RELATIVE_PATH relative "${repo}" "${path}")
    if(relative STREQUAL "${decal_path}")
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    ctr_count("${code}" "DecalMP_01(" calls)
    if(calls GREATER 0)
        list(APPEND decal01_callers "${relative}")
    endif()
endforeach()
if(NOT "${decal01_callers}" STREQUAL "${render_frame_path}")
    message(FATAL_ERROR "${prefix}: DecalMP_01 may be called only from ${render_frame_path} (found ${decal01_callers})")
endif()

# 6. Model headers are live data, and this change never edits them: the only
#    maxDistanceLOD writer is the retail cutscene opcode.
if(NOT "${max_lod_writers}" STREQUAL "${csthread_path}")
    message(FATAL_ERROR "${prefix}: maxDistanceLOD may be written only by ${csthread_path} (found ${max_lod_writers})")
endif()

message(STATUS "${prefix}: ok")
