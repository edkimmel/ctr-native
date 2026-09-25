# Structural isolation for the fixed VBlank pacing (R-6b;
# include/platform/native_vblank_pacing.h, docs/ROSTER_MILESTONE.md section
# 3.4; since LR-S3 also docs/LOCKSTEP_RACE_MILESTONE.md LR-7):
#  1. Platform_SetFixedVBlankPacing is declared once in include/platform.h and
#     defined once in platform/native_platform.c, neither inside any
#     conditional (so outside CTR_INTERNAL, LR-7) but the header's include
#     guard, and the definition only stores the normalized flag. Exactly two
#     files call it:
#     - main.c, exactly once, as Platform_SetFixedVBlankPacing(1), inside the
#       roster proof's configure block and its CTR_INTERNAL guard, after the
#       proof was configured and its pads installed;
#     - platform/native_arcade_link_host.c, which has no conditional,
#       exactly three times: RaceBegin's (1), only in LINK mode, and the (0)
#       of RaceEnd and of Shutdown, each only while the host's own
#       g_racePacing flag says RaceBegin turned it on.
#     No other file names it, and nothing in game/ does (the race caller
#     reaches it only through the host's RaceBegin and RaceEnd; their call
#     sites are pinned by tests/main_arcade_link_hook_isolation_test.cmake);
#  2. the flag s_fixedVBlankPacing is a file-scope static of
#     native_platform.c that starts at 0, is written only by the setter, and
#     is read only once, as the first argument of the one
#     NativeVBlankPacing_Plan call in Native_CatchUpDueVBlanks; no other file
#     names it;
#  3. the pacer honours the flag only through that plan: every plan the call
#     can return is handled, and REANCHOR and ON_TIME return before the
#     catch-up loop; VSync and Platform_WaitUntilVBlank still try the V2
#     playback packet path before any catch-up;
#  4. the pacing module is pure (no SDL, clock, I/O, heap, state, game,
#     replay, or platform-setter token), C17 with extensions off, links
#     nothing, and ctr_native links it; nothing in game/ names the switch or
#     the module.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "vblank pacing isolation")

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
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Sets out_var to the first brace block after opener, braces included.
function(ctr_block relative_path source opener out_var)
    string(FIND "${source}" "${opener}" opener_at)
    if(opener_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${opener}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" ${opener_at} -1 tail)
    string(FIND "${tail}" "{" brace_offset)
    if(brace_offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: no block follows '${opener}' in ${relative_path}")
    endif()
    string(LENGTH "${tail}" length)
    set(depth 0)
    set(position ${brace_offset})
    while(position LESS length)
        string(SUBSTRING "${tail}" ${position} 1 character)
        if(character STREQUAL "{")
            math(EXPR depth "${depth} + 1")
        elseif(character STREQUAL "}")
            math(EXPR depth "${depth} - 1")
            if(depth EQUAL 0)
                math(EXPR block_length "${position} - ${brace_offset} + 1")
                string(SUBSTRING "${tail}" ${brace_offset} ${block_length} block)
                set(${out_var} "${block}" PARENT_SCOPE)
                return()
            endif()
        endif()
        math(EXPR position "${position} + 1")
    endwhile()
    message(FATAL_ERROR "${prefix}: unbalanced block after '${opener}' in ${relative_path}")
endfunction()

# The #if lines guarding each occurrence of name in source (preprocessor lines
# kept), as a stack; fails unless every occurrence sits inside `guard`.
function(ctr_require_guarded relative_path source name guard)
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|ifndef|else|elif|endif)[^\n]*|${name}" tokens "${source}")
    set(stack "")
    set(seen 0)
    foreach(token IN LISTS tokens)
        if(token MATCHES "^#[ \t]*(if|ifdef|ifndef)")
            list(APPEND stack "${token}")
        elseif(token MATCHES "^#[ \t]*endif")
            list(POP_BACK stack)
        elseif(token MATCHES "^#")
            list(POP_BACK stack)
            list(APPEND stack "${token}")
        else()
            math(EXPR seen "${seen} + 1")
            list(FIND stack "${guard}" guard_at)
            if(guard_at EQUAL -1)
                message(FATAL_ERROR "${prefix}: ${relative_path} names ${name} outside ${guard}")
            endif()
        endif()
    endforeach()
    if(seen EQUAL 0)
        message(FATAL_ERROR "${prefix}: ${relative_path} never names ${name}; the guard scan is broken")
    endif()
endfunction()

# Fails unless every occurrence of name in source sits inside no conditional
# but, when given, the file's include guard `allowed` (the outermost entry).
function(ctr_require_unguarded relative_path source name allowed)
    string(REGEX MATCHALL "#[ \t]*(if|ifdef|ifndef|else|elif|endif)[^\n]*|${name}" tokens "${source}")
    set(stack "")
    set(seen 0)
    foreach(token IN LISTS tokens)
        if(token MATCHES "^#[ \t]*(if|ifdef|ifndef)")
            list(APPEND stack "${token}")
        elseif(token MATCHES "^#[ \t]*endif")
            list(POP_BACK stack)
        elseif(token MATCHES "^#")
            list(POP_BACK stack)
            list(APPEND stack "${token}")
        else()
            math(EXPR seen "${seen} + 1")
            if(NOT "${stack}" STREQUAL "${allowed}")
                message(FATAL_ERROR "${prefix}: ${relative_path} names ${name} inside '${stack}'; it must sit outside every conditional (LR-7)")
            endif()
        endif()
    endforeach()
    if(seen EQUAL 0)
        message(FATAL_ERROR "${prefix}: ${relative_path} never names ${name}; the guard scan is broken")
    endif()
endfunction()

set(header_path "include/platform/native_vblank_pacing.h")
set(module_path "platform/native_vblank_pacing.c")
set(platform_path "platform/native_platform.c")
set(platform_header_path "include/platform.h")
ctr_read_source("${header_path}" pacing_h)
ctr_read_source("${module_path}" pacing)
ctr_read_source("${platform_path}" platform)
ctr_read_source("${platform_header_path}" platform_h)
ctr_read_source("main.c" main_source)
ctr_strip_comments("${pacing}" pacing_code)
ctr_strip_comments("${pacing_h}" pacing_h_code)
ctr_strip_comments("${platform}" platform_code)
ctr_strip_comments("${platform_h}" platform_h_code)
ctr_strip_comments("${main_source}" main_code)

# 1 and 2. Who names the switch, the flag, and the plan, over every game,
#    platform, include, and main.c file (code only).
set(host_path "platform/native_arcade_link_host.c")
set(setter_owners "${platform_header_path}" "${platform_path}" "main.c" "${host_path}")
set(flag_owners "${platform_path}")
set(plan_owners "${header_path}" "${module_path}" "${platform_path}")
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
    if(relative_path MATCHES "^game/")
        foreach(term IN ITEMS FixedVBlankPacing fixedVBlankPacing NativeVBlankPacing native_vblank_pacing NATIVE_VBLANK_PACING)
            ctr_forbid("${relative_path}" "${source}" "${term}")
        endforeach()
    endif()
    string(REGEX MATCH "[Ff]ixedVBlankPacing|NativeVBlankPacing" raw_hit "${source}")
    if(raw_hit STREQUAL "")
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    foreach(rule "Platform_SetFixedVBlankPacing|setter_owners" "s_fixedVBlankPacing|flag_owners" "NativeVBlankPacing_Plan|plan_owners")
        string(REPLACE "|" ";" rule_items "${rule}")
        list(GET rule_items 0 name)
        list(GET rule_items 1 owners)
        ctr_count_identifier("${code}" "${name}" hits)
        list(FIND ${owners} "${relative_path}" owner_at)
        if(hits GREATER 0 AND owner_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${relative_path} names ${name}; only ${${owners}} may")
        endif()
    endforeach()
endforeach()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()

# 1. The declaration, the definition, and the one call.
ctr_count_identifier("${platform_h_code}" "Platform_SetFixedVBlankPacing" declaration_hits)
if(NOT declaration_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${platform_header_path} must declare Platform_SetFixedVBlankPacing exactly once (found ${declaration_hits})")
endif()
ctr_require("${platform_header_path}" "${platform_h_code}" "void Platform_SetFixedVBlankPacing(int enabled);")
ctr_require_unguarded("${platform_header_path}" "${platform_h_code}" "Platform_SetFixedVBlankPacing" "#ifndef PLATFORM_H")
ctr_count_identifier("${platform_code}" "Platform_SetFixedVBlankPacing" definition_hits)
if(NOT definition_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${platform_path} must define Platform_SetFixedVBlankPacing once and never call it (found ${definition_hits})")
endif()
ctr_require_unguarded("${platform_path}" "${platform_code}" "Platform_SetFixedVBlankPacing" "")
ctr_block("${platform_path}" "${platform_code}" "void Platform_SetFixedVBlankPacing(int enabled)" setter_body)
string(REGEX REPLACE "[ \t\n]+" " " setter_normalized "${setter_body}")
if(NOT setter_normalized STREQUAL "{ s_fixedVBlankPacing = (enabled != 0) ? 1 : 0; }")
    message(FATAL_ERROR "${prefix}: Platform_SetFixedVBlankPacing must only store the normalized flag (found '${setter_normalized}')")
endif()

ctr_count_identifier("${main_code}" "Platform_SetFixedVBlankPacing" call_hits)
if(NOT call_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: main.c must call Platform_SetFixedVBlankPacing exactly once (found ${call_hits})")
endif()
ctr_block("main.c" "${main_code}" "if (rosterProofOptions.enabled != 0u)\n\t{\n\t\tstruct NativeIdentityV1 rosterProofIdentity;" proof_block)
ctr_require_order("main.c (the roster proof configure block)" "${proof_block}"
    "NativeArcadeRosterProof_Configure(&rosterProofOptions, &rosterProofIdentity)"
    "#if defined(CTR_INTERNAL)"
    "if (!MainArcadeRosterProof_Start())"
    "Platform_SetFixedVBlankPacing(1);"
    "#endif")
# The call sits between the pads' install and the #endif of the same guard:
# no other conditional in between.
string(FIND "${proof_block}" "if (!MainArcadeRosterProof_Start())" start_at)
string(FIND "${proof_block}" "Platform_SetFixedVBlankPacing(1);" set_at)
math(EXPR between_length "${set_at} - ${start_at}")
string(SUBSTRING "${proof_block}" ${start_at} ${between_length} between)
if(between MATCHES "#[ \t]*(if|else|elif|endif)")
    message(FATAL_ERROR "${prefix}: main.c must enable the fixed pacing in the same CTR_INTERNAL guard as MainArcadeRosterProof_Start")
endif()
ctr_require_guarded("main.c" "${main_code}" "Platform_SetFixedVBlankPacing" "#if defined(CTR_INTERNAL)")
ctr_require_order("main.c" "${main_code}" "Platform_SetFixedVBlankPacing(1);" "CTR_Main()")

# 1 (LR-7). The arcade-link host glue: exactly three calls, one per function,
#    RaceBegin's (1) only in LINK mode, and the (0) of RaceEnd and Shutdown
#    only while its own flag says RaceBegin turned the pacing on; the file
#    has no conditional, so the calls exist in every build.
ctr_read_source("${host_path}" host_source)
ctr_strip_comments("${host_source}" host_code)
ctr_count_identifier("${host_code}" "Platform_SetFixedVBlankPacing" host_hits)
if(NOT host_hits EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${host_path} must call Platform_SetFixedVBlankPacing exactly three times, in RaceBegin, RaceEnd, and Shutdown (found ${host_hits})")
endif()
if(host_code MATCHES "#[ \t]*(if|ifdef|ifndef|else|elif|endif)")
    message(FATAL_ERROR "${prefix}: ${host_path} must hold no conditional, so its pacing calls are not build-dependent")
endif()
foreach(spec
        "int NativeArcadeLinkHost_RaceBegin(void)|Platform_SetFixedVBlankPacing(1);"
        "void NativeArcadeLinkHost_RaceEnd(void)|Platform_SetFixedVBlankPacing(0);"
        "void NativeArcadeLinkHost_Shutdown(void)|Platform_SetFixedVBlankPacing(0);")
    string(REPLACE "|" ";" spec_items "${spec}")
    list(GET spec_items 0 opener)
    list(GET spec_items 1 call)
    ctr_block("${host_path}" "${host_code}" "${opener}\n{" body)
    ctr_count_identifier("${body}" "Platform_SetFixedVBlankPacing" body_hits)
    if(NOT body_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${host_path} (${opener}) must call Platform_SetFixedVBlankPacing exactly once (found ${body_hits})")
    endif()
    ctr_require("${host_path} (${opener})" "${body}" "${call}")
endforeach()
ctr_block("${host_path}" "${host_code}" "int NativeArcadeLinkHost_RaceBegin(void)\n{" race_begin_body)
string(REGEX REPLACE "[ \t\n]+" " " race_begin_normalized "${race_begin_body}")
if(NOT race_begin_normalized STREQUAL "{ if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK) { return 0; } Platform_SetFixedVBlankPacing(1); g_racePacing = 1u; return 1; }")
    message(FATAL_ERROR "${prefix}: NativeArcadeLinkHost_RaceBegin must turn the pacing on only in LINK mode and record it (found '${race_begin_normalized}')")
endif()
ctr_block("${host_path}" "${host_code}" "void NativeArcadeLinkHost_RaceEnd(void)\n{" race_end_body)
string(REGEX REPLACE "[ \t\n]+" " " race_end_normalized "${race_end_body}")
if(NOT race_end_normalized STREQUAL "{ if (g_racePacing == 0u) { return; } Platform_SetFixedVBlankPacing(0); g_racePacing = 0u; }")
    message(FATAL_ERROR "${prefix}: NativeArcadeLinkHost_RaceEnd must turn off only the pacing RaceBegin turned on (found '${race_end_normalized}')")
endif()
ctr_block("${host_path}" "${host_code}" "void NativeArcadeLinkHost_Shutdown(void)\n{" shutdown_body)
string(REGEX REPLACE "[ \t\n]+" " " shutdown_normalized "${shutdown_body}")
string(FIND "${shutdown_normalized}" "{ if (g_racePacing != 0u) { Platform_SetFixedVBlankPacing(0); g_racePacing = 0u; } " shutdown_off_at)
if(NOT shutdown_off_at EQUAL 0)
    message(FATAL_ERROR "${prefix}: NativeArcadeLinkHost_Shutdown must first turn off only the pacing RaceBegin turned on (found '${shutdown_normalized}')")
endif()
ctr_count_identifier("${host_code}" "g_racePacing" race_flag_hits)
if(NOT race_flag_hits EQUAL 6)
    message(FATAL_ERROR "${prefix}: ${host_path} must name g_racePacing exactly six times: its declaration, RaceBegin's set, and the check and clear of RaceEnd and Shutdown (found ${race_flag_hits})")
endif()
ctr_require("${host_path}" "${host_code}" "\nstatic uint8_t g_racePacing;\n")

# 2 and 3. The flag and the one place the pacer reads it.
ctr_count_identifier("${platform_code}" "s_fixedVBlankPacing" flag_hits)
if(NOT flag_hits EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${platform_path} must name s_fixedVBlankPacing exactly three times: its declaration, the setter's store, and the plan call (found ${flag_hits})")
endif()
ctr_require("${platform_path}" "${platform_code}" "\nglobal_variable int s_fixedVBlankPacing = 0;\n")
ctr_count_identifier("${platform_code}" "NativeVBlankPacing_Plan" plan_hits)
if(NOT plan_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${platform_path} must call NativeVBlankPacing_Plan exactly once (found ${plan_hits})")
endif()
ctr_block("${platform_path}" "${platform_code}" "internal int Native_CatchUpDueVBlanks(void)" catch_up_body)
ctr_require("${platform_path} (Native_CatchUpDueVBlanks)" "${catch_up_body}"
    "switch (NativeVBlankPacing_Plan(s_fixedVBlankPacing, now, s_nextVBlankCounter, step, NATIVE_VSYNC_CATCHUP_MAX))")
ctr_block("${platform_path} (Native_CatchUpDueVBlanks)" "${catch_up_body}" "switch (NativeVBlankPacing_Plan(" plan_switch)
string(REGEX REPLACE "[ \t\n]+" " " plan_switch_normalized "${plan_switch}")
foreach(expected_case IN ITEMS
        "case NATIVE_VBLANK_PACING_REBASE: s_nextVBlankCounter = now; s_vblankRemainder = 0; Native_AdvanceVBlankTarget(); return 0;"
        "case NATIVE_VBLANK_PACING_REANCHOR: s_nextVBlankCounter = now; s_vblankRemainder = 0; return 0;"
        "case NATIVE_VBLANK_PACING_ON_TIME: return 0;"
        "default: break;")
    ctr_require("${platform_path} (the plan switch)" "${plan_switch_normalized}" "${expected_case}")
endforeach()
string(REGEX MATCHALL "case " switch_cases "${plan_switch_normalized}")
list(LENGTH switch_cases switch_case_count)
if(NOT switch_case_count EQUAL 3)
    message(FATAL_ERROR "${prefix}: the plan switch must handle exactly REBASE, REANCHOR, and ON_TIME (found ${switch_case_count} cases)")
endif()
ctr_require_order("${platform_path} (Native_CatchUpDueVBlanks)" "${catch_up_body}"
    "Native_EnsureVBlankTarget();" "switch (NativeVBlankPacing_Plan(" "while (SDL_GetPerformanceCounter() >= s_nextVBlankCounter)")
# The V2 playback packet path still comes first in both waits.
ctr_block("${platform_path}" "${platform_code}" "int VSync(int mode)" vsync_body)
ctr_require_order("${platform_path} (VSync)" "${vsync_body}"
    "NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks)" "Native_CatchUpDueVBlanks()"
    "NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks)")
ctr_block("${platform_path}" "${platform_code}" "void Platform_WaitUntilVBlank(int targetVBlank)" wait_body)
ctr_require_order("${platform_path} (Platform_WaitUntilVBlank)" "${wait_body}"
    "NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks)" "Native_CatchUpDueVBlanks()"
    "NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks)")
ctr_count_identifier("${platform_code}" "Native_CatchUpDueVBlanks" catch_up_hits)
if(NOT catch_up_hits EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${platform_path} must define Native_CatchUpDueVBlanks and call it only from VSync and Platform_WaitUntilVBlank (found ${catch_up_hits} names)")
endif()

# 4. The pacing module is pure.
foreach(pair "${module_path}|pacing|pacing_code" "${header_path}|pacing_h|pacing_h_code")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 whole_variable)
    list(GET pair_items 2 code_variable)
    # Whole files, comments included: the switch never names replay,
    # checkpoints, canonical state, or the lease.
    foreach(term IN ITEMS Replay replay REPLAY Checkpoint checkpoint Canonical canonical Lease lease Topology topology)
        ctr_forbid("${relative_path}" "${${whole_variable}}" "${term}")
    endforeach()
    # Code: no SDL, clock, I/O, heap, game, or platform-layer name.
    foreach(term IN ITEMS SDL Platform_ Native_ gGT sdata "#include <stdio" "#include <stdlib" "#include <time"
            malloc calloc realloc "free(" alloca fopen printf "#include <common" "#include <platform.h")
        ctr_forbid("${relative_path}" "${${code_variable}}" "${term}")
    endforeach()
endforeach()
ctr_forbid("${module_path}" "${pacing_code}" "static")
string(REGEX MATCHALL "#include[^\n]*" pacing_includes "${pacing_code}")
if(NOT "${pacing_includes}" STREQUAL "#include \"platform/native_vblank_pacing.h\";#include <stdint.h>")
    message(FATAL_ERROR "${prefix}: ${module_path} may include only its header and <stdint.h> (found '${pacing_includes}')")
endif()
ctr_read_source("CMakeLists.txt" cmake)
set(pacing_target ctr_native_vblank_pacing)
string(FIND "${cmake}" "add_library(${pacing_target} STATIC platform/native_vblank_pacing.c)" pacing_declare_at)
if(pacing_declare_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${pacing_target} must build exactly ${module_path}")
endif()
string(SUBSTRING "${cmake}" ${pacing_declare_at} 400 pacing_block)
ctr_require_order("CMakeLists.txt (${pacing_target})" "${pacing_block}"
    "set_target_properties(${pacing_target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF")
ctr_forbid("CMakeLists.txt" "${cmake}" "target_link_libraries(${pacing_target} ")
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(REGEX REPLACE "[ \t\r\n]+" ";" native_link_items "${native_link_block}")
list(FIND native_link_items "${pacing_target}" pacing_link_at)
if(pacing_link_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ctr_native must link ${pacing_target}")
endif()
