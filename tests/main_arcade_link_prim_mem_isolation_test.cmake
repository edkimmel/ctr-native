# Structural isolation for the arcade-link primitive memory
# (docs/SOLO_CAB_MILESTONE.md section 8.5). The top LOD tier outgrows the
# retail per-level primMem on several 2P tracks, and the level draw then drops
# geometry at its primMem preflight. In LINK mode only, both draw buffers are
# moved to larger static host buffers; the retail MEMPACK allocations stay.
# The size is the pure MainArcadeLinkPolicy_PrimitiveBytes (LINK -> the LINK
# size, never below retail; unit-tested in tests/main_arcade_link_policy_test.c);
# the only game-side user is the thin accessor MainArcadeLink_GrowPrimMem, and
# this test pins that:
#   1. the policy body and the LINK size;
#   2. MainDB_PrimMem is the retail allocation, unchanged: MEMPACK_AllocMem of
#      the retail size, so the MEMPACK layout of every simulation object after
#      it is unchanged;
#   3. MainInit_PrimMem calls the accessor once, inside #if defined(CTR_NATIVE),
#      as its last statement, after both retail allocations;
#   4. the accessor reads the policy on the host mode, allocates nothing, and
#      writes only capacityBytes, cursor, start, end, and guardEnd (never
#      allocationStart, the retail MEMPACK block), with the retail 0x100
#      guard; its host buffers are static, sized by the policy constant;
#   5. the accessor, the policy, and the host buffers are named only by their
#      own files and MainInit.c, so no canonical, digest, replay, checkpoint,
#      lockstep, or topology-lease file names them;
#   6. no checkpoint holds a host pointer: arcade-link mode rejects replay
#      record and playback (main.c) and refuses both quick-state hotkeys
#      (platform/native_platform.c), the only checkpoint captures; and
#      because the host buffers stay bound after a fallback to OFF until the
#      next level load, the quick save and load themselves
#      (platform/native_savestate.c) refuse first while any draw buffer's
#      primMem start is not its allocationStart.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade link prim mem isolation")

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

set(accessor "MainArcadeLink_GrowPrimMem")
set(accessor_call "MainArcadeLink_GrowPrimMem(gGT);")
set(policy_fn "MainArcadeLinkPolicy_PrimitiveBytes")
set(buffer "s_mainArcadeLinkPrimMem")
set(link_bytes "MAIN_ARCADE_LINK_POLICY_LINK_PRIMITIVE_BYTES")
set(db_path "game/MAIN/MainDB.c")
set(init_path "game/MAIN/MainInit.c")
set(hook_path "game/MAIN/MainArcadeLink.c")
set(hook_header_path "game/MAIN/MainArcadeLink.h")
set(policy_path "game/MAIN/MainArcadeLinkPolicy.c")
set(policy_header_path "game/MAIN/MainArcadeLinkPolicy.h")
set(platform_path "platform/native_platform.c")
set(main_path "main.c")

# 1. The policy: LINK grows to the LINK size, never shrinks; every other mode
#    keeps the retail size.
ctr_read_source("${policy_header_path}" policy_header)
ctr_require_literal("${policy_header_path}" "${policy_header}" "#define ${link_bytes} 0x40000u")
ctr_require_literal("${policy_header_path}" "${policy_header}" "uint32_t ${policy_fn}(uint32_t hostMode, uint32_t retailBytes);")
ctr_read_source("${policy_path}" policy_source)
ctr_strip_comments("${policy_source}" policy_code)
ctr_block_text("${policy_path}" "${policy_code}" "uint32_t ${policy_fn}(uint32_t hostMode, uint32_t retailBytes)" policy_block)
ctr_squash("${policy_block}" policy_squashed)
if(NOT policy_squashed STREQUAL "{if((hostMode==MAIN_ARCADE_LINK_POLICY_MODE_LINK)&&(retailBytes<${link_bytes})){return${link_bytes};}returnretailBytes;}")
    message(FATAL_ERROR "${prefix}: ${policy_fn} must return ${link_bytes} only in LINK below it and the retail size otherwise (found '${policy_squashed}')")
endif()

# 2. The retail allocation is unchanged.
ctr_read_source("${db_path}" db_source)
ctr_strip_comments("${db_source}" db_code)
ctr_block_text("${db_path}" "${db_code}" "void MainDB_PrimMem(struct PrimMem *primMem, u32 size)" db_block)
ctr_squash("${db_block}" db_squashed)
if(NOT db_squashed STREQUAL "{u32alignedSize;void*pvVar1;pvVar1=MEMPACK_AllocMem(size);primMem->capacityBytes=size;primMem->allocationStart=pvVar1;primMem->cursor=pvVar1;primMem->start=pvVar1;alignedSize=(size>>2)<<2;pvVar1=(void*)((int)pvVar1+alignedSize);primMem->end=pvVar1;primMem->guardEnd=(void*)((int)pvVar1-0x100);}")
    message(FATAL_ERROR "${prefix}: MainDB_PrimMem must stay the retail MEMPACK allocation of the retail size (found '${db_squashed}')")
endif()

# 3. MainInit_PrimMem: both retail allocations of the retail size, then the
#    accessor once, native only, as the last statement.
ctr_read_source("${init_path}" init_source)
ctr_strip_comments("${init_source}" init_code)
ctr_require_native_guard("${init_path}" "${init_code}" "#include \"MAIN/MainArcadeLink.h\"")
ctr_block_text("${init_path}" "${init_code}" "void MainInit_PrimMem(struct GameTracker *gGT)" init_block)
ctr_squash("${init_block}" init_squashed)
if(NOT init_squashed STREQUAL "{intsize=MainInit_GetPrimMemSize(gGT);if(size==0){return;}MainDB_PrimMem(&gGT->db[0].primMem,size);MainDB_PrimMem(&gGT->db[1].primMem,size);${accessor}(gGT);}")
    message(FATAL_ERROR "${prefix}: MainInit_PrimMem must allocate both retail blocks and then call ${accessor} last (found '${init_squashed}')")
endif()
ctr_require_native_guard("${init_path} (MainInit_PrimMem)" "${init_block}" "${accessor_call}")
ctr_count("${init_code}" "${accessor}" init_names)
if(NOT init_names EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${init_path} must call ${accessor} exactly once (found ${init_names})")
endif()

# 4. The accessor: the policy on the host mode; static host buffers of the
#    policy size; only the five size and pointer fields written, with the
#    retail guard; no allocation and no allocationStart.
ctr_read_source("${hook_header_path}" hook_header)
ctr_require_literal("${hook_header_path}" "${hook_header}" "void ${accessor}(struct GameTracker *gGT);")
ctr_read_source("${hook_path}" hook_source)
ctr_strip_comments("${hook_source}" hook_code)
ctr_require_literal("${hook_path}" "${hook_code}" "static u32 ${buffer}[2][${link_bytes} / sizeof(u32)];")
ctr_block_text("${hook_path}" "${hook_code}" "void ${accessor}(struct GameTracker *gGT)" accessor_block)
ctr_require_order("${hook_path} (${accessor})" "${accessor_block}"
    "uint32_t hostMode = NativeArcadeLinkHost_Mode();"
    "for (int dbIndex = 0; dbIndex < 2; dbIndex++)"
    "const u32 bytes = ${policy_fn}(hostMode, primMem->capacityBytes);"
    "(bytes <= primMem->capacityBytes) || (bytes > sizeof(${buffer}[dbIndex]))"
    "continue;"
    "u8 *start = (u8 *)&${buffer}[dbIndex][0];"
    "u8 *end = start + ((bytes >> 2) << 2);"
    "primMem->capacityBytes = bytes;"
    "primMem->cursor = start;"
    "primMem->start = start;"
    "primMem->end = end;"
    "primMem->guardEnd = end - 0x100;")
ctr_count("${accessor_block}" "primMem->" accessor_field_names)
if(NOT accessor_field_names EQUAL 8)
    message(FATAL_ERROR "${prefix}: ${accessor} must read only capacityBytes and start and write only capacityBytes, cursor, start, end, and guardEnd (found ${accessor_field_names} primMem-> uses)")
endif()
if(accessor_block MATCHES "allocationStart|MEMPACK_|malloc|NativeCheckpoint")
    message(FATAL_ERROR "${prefix}: ${accessor} must not touch allocationStart, allocate, or checkpoint (found '${CMAKE_MATCH_0}')")
endif()
ctr_count("${hook_code}" "${policy_fn}(" hook_policy_calls)
if(NOT hook_policy_calls EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${hook_path} must call ${policy_fn} only in ${accessor} (found ${hook_policy_calls})")
endif()

# 5. Who names the accessor, the policy, and the host buffers: game code, the
#    host glue, headers, and main.c are scanned (comments stripped).
file(GLOB_RECURSE scan_paths
    "${repo}/game/*.c" "${repo}/game/*.h"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/*.h")
list(APPEND scan_paths "${repo}/${main_path}")
set(accessor_files "")
set(policy_files "")
set(buffer_files "")
foreach(path IN LISTS scan_paths)
    file(READ "${path}" source)
    if(NOT source MATCHES "${accessor}|${policy_fn}|${buffer}")
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
    string(FIND "${code}" "${buffer}" buffer_at)
    if(NOT buffer_at EQUAL -1)
        list(APPEND buffer_files "${relative}")
    endif()
endforeach()
list(SORT accessor_files)
set(expected_accessor_files "${hook_path}" "${hook_header_path}" "${init_path}")
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
if(NOT "${buffer_files}" STREQUAL "${hook_path}")
    message(FATAL_ERROR "${prefix}: ${buffer} may be named only by ${hook_path} (found ${buffer_files})")
endif()
foreach(relative IN LISTS accessor_files policy_files buffer_files)
    if(relative MATCHES "(MainCanonical|MainArcadeRaceDigest|native_canonical|native_checkpoint|native_replay|native_savestate|native_lockstep|TopologyLease)")
        message(FATAL_ERROR "${prefix}: ${relative} is a canonical, digest, replay, checkpoint, lockstep, or lease file and must not name ${accessor}, ${policy_fn}, or ${buffer}")
    endif()
endforeach()

# 6. No checkpoint holds a host pointer: arcade-link mode never captures one
#    (replay rejected, both quick-state hotkeys refused).
ctr_read_source("${main_path}" main_source)
ctr_require_literal("${main_path}" "${main_source}" "--arcade-link and --arcade-link-preview cannot be combined with replay record or playback options")
ctr_read_source("${platform_path}" platform_source)
ctr_strip_comments("${platform_source}" platform_code)
ctr_count("${platform_code}" "quick states are disabled in arcade-link mode" quick_state_refusals)
if(NOT quick_state_refusals EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${platform_path} must refuse both quick-state hotkeys in arcade-link mode (found ${quick_state_refusals})")
endif()
ctr_require_order("${platform_path}" "${platform_code}"
    "case SDL_SCANCODE_F5:"
    "if (NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)"
    "NativeSaveState_RequestSave();"
    "case SDL_SCANCODE_F8:"
    "if (NativeArcadeLinkHost_Mode() != (uint32_t)NATIVE_ARCADE_LINK_HOST_MODE_OFF)"
    "NativeSaveState_RequestLoad();")

# 6b. The host mode can fall back to OFF with the host buffers still bound
#     (until the next MainInit_PrimMem), so the quick save and load refuse
#     first, before any capture or restore, while any draw buffer's primMem
#     start is not its retail allocationStart: a structural check that names
#     neither the accessor nor the buffer.
set(savestate_path "platform/native_savestate.c")
set(bound_fn "NativeSaveState_HostDrawBuffersBound")
set(bound_refusal "Platform_Log(\"[CTR State] quick states are disabled while host draw buffers are bound\\n\");")
ctr_read_source("${savestate_path}" savestate_source)
ctr_strip_comments("${savestate_source}" savestate_code)
ctr_block_text("${savestate_path}" "${savestate_code}" "internal s32 ${bound_fn}(void)" bound_block)
ctr_squash("${bound_block}" bound_squashed)
if(NOT bound_squashed STREQUAL "{conststructGameTracker*gGT=&sdata_static.gameTracker;for(u32i=0;i<len(gGT->db);i++){if(gGT->db[i].primMem.start!=gGT->db[i].primMem.allocationStart){return1;}}return0;}")
    message(FATAL_ERROR "${prefix}: ${bound_fn} must report any draw buffer whose primMem start is not its allocationStart (found '${bound_squashed}')")
endif()
foreach(pair "NativeSaveState_SaveQuick NativeSaveState_PrepareDir()" "NativeSaveState_LoadQuick NativeSaveState_PreparePayload()")
    string(REPLACE " " ";" pair_items "${pair}")
    list(GET pair_items 0 quick_fn)
    list(GET pair_items 1 first_step)
    ctr_block_text("${savestate_path}" "${savestate_code}" "internal s32 ${quick_fn}(void)" quick_block)
    ctr_require_order("${savestate_path} (${quick_fn})" "${quick_block}"
        "if (${bound_fn}())"
        "${bound_refusal}"
        "return 0;"
        "${first_step}"
        "NativeCheckpoint")
    string(FIND "${quick_block}" "${bound_fn}()" bound_at)
    string(SUBSTRING "${quick_block}" 0 ${bound_at} before_bound)
    string(REGEX REPLACE "if[ \t]*\\($" "" before_bound "${before_bound}")
    if(before_bound MATCHES "[A-Za-z_][A-Za-z0-9_]*[ \t]*\\(")
        message(FATAL_ERROR "${prefix}: ${quick_fn} must call ${bound_fn} before any other call (found '${CMAKE_MATCH_0}')")
    endif()
endforeach()
foreach(name IN ITEMS "${accessor}" "${policy_fn}" "${buffer}")
    string(FIND "${savestate_code}" "${name}" name_at)
    if(NOT name_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${savestate_path} must refuse structurally and not name ${name}")
    endif()
endforeach()

message(STATUS "${prefix}: ok")
