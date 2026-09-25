# Structural isolation for the live V4 race digest
# (game/MAIN/MainArcadeRaceDigest.{c,h}; the Task 8 race plan,
# docs/LOCKSTEP_RACE_MILESTONE.md, LR-10, LR-17 ruled (a), section 5, slice
# LR-S4):
#  1. the digest module is read-only and lease-free: its code (comments
#     removed, string literals blanked) takes game state through const
#     pointers only and writes none of it; it names no topology lease, no
#     topology fact reader or live topology source, no nav path or restart
#     point structure (NavHeader, ptr_restart_points, ...), no replay,
#     checkpoint, or MainMain token, no game global (sdata, D231, data), no
#     platform call, and no heap; its source and header name no lockstep
#     token, comments included (LR-1); its includes are an allow-list; the
#     .c is one CTR_NATIVE block;
#  2. it runs the runtime's per-tick lifecycle in order (Reset on race
#     tick 0, the unavailable topology summary, BeginFrame, PrepareV4 with
#     the request carrying the caller's bank, ViewV4, ReleaseV4), ends the
#     frame before latching a VIEW or RELEASE failure (Release, else Reset),
#     invalidates the topology context only in EndRace, calls no other
#     runtime entry, calls both world extractors, and wraps the whole
#     projection in its one NativePerf scope;
#  3. MainArcadeRaceDigest_* is named only by the module and its callers:
#     the roster proof (LR-S4; the race caller joins in LR-S10). platform/
#     and include/ never name it. The proof projects once per logged tick
#     and ends the race once, with the setup's post-setup bank and the
#     racing overlay's mine pool;
#  4. the unity chain includes the world extractors and the module exactly
#     once, in order; ctr_native links neither world extractor library (a
#     pulled member would define sdata twice);
#  5. the LR-17 pin, for the owner's ruling (a): among the first-party
#     game/MAIN/MainCanonical* and game/MAIN/MainArcade* sources, the only
#     functions that name a nav header (NavHeader, or the two ways to one,
#     NavPath_ptrHeader and LevNavTable) and read a last member are
#     MainCanonicalTopologyLease_ObservePostInit and
#     MainCanonicalDrivers_BotNavIndex, and both do; the lease header no
#     longer calls ObservePostInit the "sole API", and it and the runtime's
#     comments name BotNavIndex and LR-17. So this test cannot pass while
#     the ruling is broken;
#  6. the unavailable topology digest the unit test pins is the one the
#     roster proof check requires, and the check requires report v11.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "race digest isolation")
set(digest_header "game/MAIN/MainArcadeRaceDigest.h")
set(digest_source "game/MAIN/MainArcadeRaceDigest.c")
set(proof_source "game/MAIN/MainArcadeRosterProof.c")
set(lease_header "game/MAIN/MainCanonicalTopologyLeaseAuthority.h")
set(runtime_header "game/MAIN/MainCanonicalRuntime.h")
set(runtime_source "game/MAIN/MainCanonicalRuntime.c")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

# Removes every /* */ and // comment (each becomes a space), then blanks
# string and character literals, so no text inside them counts as code.
function(ctr_code label source out_var)
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*" " " stripped "${source}")
    string(FIND "${stripped}" "/*" open_at)
    if(NOT open_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: unterminated /* comment in ${label}")
    endif()
    string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" stripped "${stripped}")
    string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)+'" "' '" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

function(ctr_forbid label source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${label}")
    endif()
endfunction()

function(ctr_forbid_regex label source regex what)
    string(REPLACE ";" "@SEMI@" masked "${source}")
    string(REGEX MATCH "${regex}" hit "${masked}")
    if(NOT "${hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${label} ${what} ('${hit}')")
    endif()
endfunction()

function(ctr_require label source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${label}")
    endif()
endfunction()

# The number of whole-identifier occurrences of name in code.
function(ctr_count_identifier code name out_var)
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# The number of occurrences of a regex in code.
function(ctr_count_regex code regex out_var)
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REGEX MATCHALL "${regex}" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Fails unless every term appears in source, each after the previous one.
function(ctr_require_order label source)
    set(remaining "${source}")
    foreach(term IN LISTS ARGN)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${prefix}: '${term}' is missing or out of order in ${label}")
        endif()
        string(LENGTH "${term}" term_length)
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endforeach()
endfunction()

# Splits code (comments removed, literals blanked) into its top-level units:
# each unit is the text since the previous top-level block closed, up to and
# including the next top-level block (a function body, a struct, an
# initializer). Sets out_units to the list of units, with ';', '[', ']', and
# '\' masked so that each unit is one list element. Unbalanced braces fail.
function(ctr_units label code out_units)
    string(REPLACE "\\" "@BS@" masked "${code}")
    string(REPLACE ";" "@SEMI@" masked "${masked}")
    string(REPLACE "[" "@LB@" masked "${masked}")
    string(REPLACE "]" "@RB@" masked "${masked}")
    string(REPLACE "{" ";{;" masked "${masked}")
    string(REPLACE "}" ";};" masked "${masked}")
    set(depth 0)
    set(unit "")
    set(units "")
    foreach(piece IN LISTS masked)
        if(piece STREQUAL "{")
            math(EXPR depth "${depth} + 1")
            string(APPEND unit "{")
        elseif(piece STREQUAL "}")
            math(EXPR depth "${depth} - 1")
            if(depth LESS 0)
                message(FATAL_ERROR "${prefix}: unbalanced '}' in ${label}")
            endif()
            string(APPEND unit "}")
            if(depth EQUAL 0)
                list(APPEND units "${unit}")
                set(unit "")
            endif()
        else()
            string(APPEND unit "${piece}")
        endif()
    endforeach()
    if(NOT depth EQUAL 0)
        message(FATAL_ERROR "${prefix}: unbalanced '{' in ${label}")
    endif()
    set(${out_units} "${units}" PARENT_SCOPE)
endfunction()

# The signature part of a unit: the text before its block, after the last
# ';' before it (so a preceding prototype or declaration does not count).
function(ctr_unit_signature unit out_var)
    string(FIND "${unit}" "{" brace_at)
    string(SUBSTRING "${unit}" 0 ${brace_at} head)
    string(FIND "${head}" "@SEMI@" semi_at REVERSE)
    if(NOT semi_at EQUAL -1)
        math(EXPR after "${semi_at} + 6")
        string(SUBSTRING "${head}" ${after} -1 head)
    endif()
    set(${out_var} "${head}" PARENT_SCOPE)
endfunction()

# The LR-17 pin over one file's code: appends to out_readers the allowed
# function names whose units read a NavHeader's last, and fails on any other
# unit that names a nav header and reads a last member. A unit names a nav
# header when it names NavHeader or one of the two ways to reach one,
# sdata's NavPath_ptrHeader and the level's LevNavTable, so a read such as
# sourceData->NavPath_ptrHeader[p]->last is seen without the type's name.
set(ctr_allowed_last_readers "MainCanonicalTopologyLease_ObservePostInit" "MainCanonicalDrivers_BotNavIndex")
set(ctr_last_read_regex "(->|[.])[ \t\n]*last([^A-Za-z0-9_]|$)")
set(ctr_nav_header_regex "NavHeader|NavPath_ptrHeader|LevNavTable")
function(ctr_pin_last_readers label code out_readers)
    ctr_units("${label}" "${code}" units)
    set(readers "")
    foreach(unit IN LISTS units)
        string(REGEX MATCH "${ctr_nav_header_regex}" names_header "${unit}")
        if("${names_header}" STREQUAL "")
            continue()
        endif()
        string(REGEX MATCH "${ctr_last_read_regex}" last_read "${unit}")
        if("${last_read}" STREQUAL "")
            continue()
        endif()
        ctr_unit_signature("${unit}" signature)
        set(allowed "")
        foreach(name IN LISTS ctr_allowed_last_readers)
            string(REGEX MATCH "(^|[^A-Za-z0-9_])${name}[ \t\n]*\\(" named "${signature}")
            if(NOT "${named}" STREQUAL "")
                set(allowed "${name}")
            endif()
        endforeach()
        if("${allowed}" STREQUAL "")
            string(STRIP "${signature}" shown)
            string(REPLACE "@SEMI@" ";" shown "${shown}")
            message(FATAL_ERROR "${prefix}: LR-17 pin: ${label} reads a nav header's last outside"
                "MainCanonicalTopologyLease_ObservePostInit and MainCanonicalDrivers_BotNavIndex, in '${shown}'")
        endif()
        list(APPEND readers "${allowed}")
    endforeach()
    set(${out_readers} "${readers}" PARENT_SCOPE)
endfunction()

# 0. The helpers themselves work.
ctr_code("self-check" "a /* NavHeader */ b // lockstep\nc \"x; { y\" d '{' e" self_check)
foreach(term IN ITEMS NavHeader lockstep "//" "/*" "x" "y")
    ctr_forbid("the self-check" "${self_check}" "${term}")
endforeach()
foreach(term IN ITEMS "a " " b " "c \"\" d" "' ' e")
    ctr_require("the self-check" "${self_check}" "${term}")
endforeach()
set(self_pin_ok "static int F(const struct LinkedList *l)\n{\n\treturn l->last != 0;\n}\n"
    "int MainCanonicalDrivers_BotNavIndex(const struct NavHeader *h);\n"
    "int Caller(void) { return MainCanonicalDrivers_BotNavIndex(0); }\n"
    "static int MainCanonicalDrivers_BotNavIndex(const struct NavHeader *h)\n{\n\tif (h) { int a[2] = {1, 2}; (void)a; }\n\treturn h->last != 0;\n}\n"
    "int MainCanonicalTopologyLease_ObservePostInit(void)\n{\n\tconst struct NavHeader *header = 0;\n\treturn header[0].last == 0;\n}\n"
    "static const int k[] = {1, 2};\n")
string(CONCAT self_pin_ok ${self_pin_ok})
ctr_pin_last_readers("the self-check" "${self_pin_ok}" self_readers)
if(NOT "${self_readers}" STREQUAL "MainCanonicalDrivers_BotNavIndex;MainCanonicalTopologyLease_ObservePostInit")
    message(FATAL_ERROR "${prefix}: the LR-17 pin's self-check found readers '${self_readers}'")
endif()
ctr_units("the self-check" "${self_pin_ok}" self_units)
list(LENGTH self_units self_unit_count)
if(NOT self_unit_count EQUAL 5)
    message(FATAL_ERROR "${prefix}: the unit splitter found ${self_unit_count} units in the self-check, expected 5")
endif()
foreach(reader_line IN ITEMS
        "static int Leak(const struct NavHeader *h) { return h->last != 0; }"
        "int X(void) { struct NavHeader copy; return copy . last == 0; }"
        "int MainCanonicalDrivers_BotNavIndexCopy(const struct NavHeader *h) { return h->last != 0; }"
        "int MainCanonicalDrivers_BotNavIndex(void); int Wrap(const struct NavHeader *h) { return h->\n last != 0; }"
        "int ByPath(const struct sData *s, int p) { return s->NavPath_ptrHeader[p]->last != 0; }"
        "int ByTable(const struct GameTracker *g, int p) { return g->level1->LevNavTable[p]->last != 0; }")
    set(self_probe_failed 0)
    ctr_units("the self-check" "${reader_line}" probe_units)
    foreach(unit IN LISTS probe_units)
        string(REGEX MATCH "${ctr_nav_header_regex}" probe_names "${unit}")
        string(REGEX MATCH "${ctr_last_read_regex}" probe_read "${unit}")
        ctr_unit_signature("${unit}" probe_signature)
        set(probe_allowed 0)
        foreach(name IN LISTS ctr_allowed_last_readers)
            string(REGEX MATCH "(^|[^A-Za-z0-9_])${name}[ \t\n]*\\(" probe_named "${probe_signature}")
            if(NOT "${probe_named}" STREQUAL "")
                set(probe_allowed 1)
            endif()
        endforeach()
        if((NOT "${probe_names}" STREQUAL "") AND (NOT "${probe_read}" STREQUAL "") AND (NOT probe_allowed))
            set(self_probe_failed 1)
        endif()
    endforeach()
    if(NOT self_probe_failed)
        message(FATAL_ERROR "${prefix}: the LR-17 pin misses the reader in '${reader_line}'")
    endif()
endforeach()
ctr_count_identifier("a sdata->b; sdata_static; xsdata" "sdata" probe_sdata)
if(NOT probe_sdata EQUAL 1)
    message(FATAL_ERROR "${prefix}: the identifier count is broken (${probe_sdata} of 1)")
endif()
set(lease_regex "(^|[^Ee])(lease|Lease|LEASE)")
foreach(sample IN ITEMS "MainCanonicalTopologyLease_Acquire" "lease" "LEASE_X" "x_lease")
    string(REGEX MATCH "${lease_regex}" lease_hit "${sample}")
    if("${lease_hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: self-check: the lease scan misses '${sample}'")
    endif()
endforeach()
foreach(sample IN ITEMS "MainCanonicalRuntime_ReleaseV4" "RELEASE" "released")
    string(REGEX MATCH "${lease_regex}" lease_hit "${sample}")
    if(NOT "${lease_hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: self-check: the lease scan flags '${sample}'")
    endif()
endforeach()

# 1. The digest module: read-only and lease-free.
ctr_read_source("${digest_source}" digest_c)
ctr_read_source("${digest_header}" digest_h)
ctr_code("${digest_source}" "${digest_c}" digest_c_code)
ctr_code("${digest_header}" "${digest_h}" digest_h_code)
if(NOT digest_c MATCHES "\n#if defined\\(CTR_NATIVE\\)\n" OR NOT digest_c MATCHES "#endif[ \t\n]*$")
    message(FATAL_ERROR "${prefix}: ${digest_source} must be one #if defined(CTR_NATIVE) block")
endif()
foreach(unit IN ITEMS c h)
    if(unit STREQUAL "c")
        set(label "${digest_source}")
        set(raw "${digest_c}")
        set(code "${digest_c_code}")
    else()
        set(label "${digest_header}")
        set(raw "${digest_h}")
        set(code "${digest_h_code}")
    endif()
    # LR-1: no lockstep token anywhere, comments included; nor the nav
    # structures, restart points, or MainMain, even in comments.
    foreach(term IN ITEMS lockstep Lockstep LOCKSTEP NavHeader ptr_restart_points MainMain)
        ctr_forbid("${label}" "${raw}" "${term}")
    endforeach()
    ctr_forbid_regex("${label} (code)" "${code}" "${lease_regex}" "names the topology lease")
    foreach(term IN ITEMS
            TopologyFact FactReader MainCanonicalTopologyFacts FromNormativeStreams CaptureTopology MainCanonicalTopology_
            TopologyLayoutContract Residency residency
            NavHeader navHeader NavFrame NavPath LevNavTable navBotList botNavFrame ptr_restart_points restart_points
            level1 ptr_mesh_info QuadBlock
            MainMain Replay REPLAY native_replay GetSubmission Checkpoint checkpoint CHECKPOINT SaveState
            sdata_static Platform_ VSync malloc calloc realloc "free(" alloca
            PrepareV3 ViewV3 ReleaseV3 TestForceFailure MAIN_CANONICAL_RUNTIME_TESTING)
        ctr_forbid("${label} (code)" "${code}" "${term}")
    endforeach()
    # replay: only the runtime request's frame field, request.replayFrame.
    string(REPLACE "request.replayFrame" "" no_frame_field "${code}")
    ctr_forbid("${label} (code, beyond request.replayFrame)" "${no_frame_field}" "replay")
    foreach(global IN ITEMS sdata D231 data gGT_static)
        ctr_count_identifier("${code}" "${global}" global_hits)
        if(NOT global_hits EQUAL 0)
            message(FATAL_ERROR "${prefix}: ${label} names the game global '${global}'; game state comes in through its sources")
        endif()
    endforeach()
    # Const game state only: every named game struct is const, apart from
    # the header's forward declarations.
    foreach(type IN ITEMS GameTracker sData OverlayDATA_231 Driver Level Thread)
        ctr_count_regex("${code}" "struct[ \t\n]+${type}[^A-Za-z0-9_]" all_hits)
        ctr_count_regex("${code}" "const[ \t\n]+struct[ \t\n]+${type}[^A-Za-z0-9_]" const_hits)
        ctr_count_regex("${code}" "(^|\n)struct[ \t]+${type}@SEMI@" forward_hits)
        math(EXPR nonconst "${all_hits} - ${const_hits} - ${forward_hits}")
        if(NOT nonconst EQUAL 0)
            message(FATAL_ERROR "${prefix}: ${label} names struct ${type} without const ${nonconst} time(s); the digest reads const game state only")
        endif()
    endforeach()
    # No write through a game-state source pointer.
    ctr_forbid_regex("${label} (code)" "${code}"
        "(gGT|sourceData|mineSource|sources->gGT|sources->sourceData)->[A-Za-z0-9_.]*([-][>][A-Za-z0-9_.]*)*[ \t]*(=[^=]|[-+*/|&^]=|[+][+]|--)"
        "writes game state")
endforeach()
# The includes are an allow-list.
foreach(pair IN ITEMS "c|${digest_source}" "h|${digest_header}")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 which)
    list(GET pair 1 label)
    if(which STREQUAL "c")
        set(code "${digest_c_code}")
        set(allowed_includes "<common.h>" "\"MAIN/MainArcadeRaceDigest.h\"" "\"MAIN/MainCanonicalRuntime.h\""
            "\"MAIN/MainCanonicalWorldCounters.h\"" "\"MAIN/MainCanonicalWorldMineRegistry.h\""
            "\"platform/native_canonical_topology.h\"" "\"platform/native_deterministic_rng.h\""
            "\"platform/native_match_config.h\"" "\"platform/native_perf.h\"" "<string.h>")
    else()
        set(code "${digest_h}")
        set(allowed_includes "<stdint.h>" "\"platform/native_canonical_state.h\"")
    endif()
    # The raw text: the code form blanks the quoted include names.
    if(which STREQUAL "c")
        set(code "${digest_c}")
    endif()
    string(REGEX MATCHALL "#include[ \t]+[<\"][^>\"]+[>\"]" includes "${code}")
    foreach(include IN LISTS includes)
        string(REGEX REPLACE "^#include[ \t]+" "" name "${include}")
        list(FIND allowed_includes "${name}" allowed_at)
        if(allowed_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${label} includes ${name}, which is not on its allow-list")
        endif()
    endforeach()
endforeach()

# 2. The lifecycle, the bank, the topology summary, the world, NativePerf.
string(FIND "${digest_c_code}" "static int MainArcadeRaceDigest_ProjectTick(" tick_at)
string(FIND "${digest_c_code}" "int MainArcadeRaceDigest_Project(uint32_t raceTick" project_at)
string(FIND "${digest_c_code}" "int MainArcadeRaceDigest_EndRace(void)" end_at)
if(tick_at EQUAL -1 OR project_at EQUAL -1 OR end_at EQUAL -1 OR NOT tick_at LESS project_at OR NOT project_at LESS end_at)
    message(FATAL_ERROR "${prefix}: ${digest_source} must define ProjectTick, then Project, then EndRace")
endif()
math(EXPR tick_length "${project_at} - ${tick_at}")
string(SUBSTRING "${digest_c_code}" ${tick_at} ${tick_length} tick_body)
math(EXPR project_length "${end_at} - ${project_at}")
string(SUBSTRING "${digest_c_code}" ${project_at} ${project_length} project_body)
string(SUBSTRING "${digest_c_code}" ${end_at} -1 end_body)
ctr_require_order("${digest_source} (MainArcadeRaceDigest_ProjectTick)" "${tick_body}"
    "if (raceTick == 0u)"
    "MainCanonicalRuntime_Reset(workspace);"
    "state->request.bank = &state->bank;"
    "MainCanonicalWorldCounters_ExtractV1("
    "MainCanonicalWorldMineRegistry_ExtractV1("
    "NativeCanonicalTopologyV1_Init(&state->topology);"
    "MainCanonicalRuntime_BeginFrame(workspace)"
    "MainCanonicalRuntime_PrepareV4(workspace, &state->request,"
    "&state->topology)"
    "MainCanonicalRuntime_ViewV4(workspace, &state->request)"
    "MainCanonicalRuntime_ReleaseV4(workspace, &state->request)")
# No failure leaves the runtime frame open: once PrepareV4 has succeeded, the
# VIEW failure releases the prepared state (or resets when there is no view
# to release), and a refused Release resets, each before latching; each
# path runs one Reset and has no return but its latch.
ctr_require_order("${digest_source} (the VIEW and RELEASE failure paths)" "${tick_body}"
    "view = MainCanonicalRuntime_ViewV4(workspace, &state->request);"
    "if ((view == NULL) || (view->frameNumber != raceTick))"
    "if ((view == NULL) || !MainCanonicalRuntime_ReleaseV4(workspace, &state->request))"
    "MainCanonicalRuntime_Reset(workspace);"
    "return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_VIEW);"
    "tick.frameNumber = view->frameNumber;"
    "if (!MainCanonicalRuntime_ReleaseV4(workspace, &state->request))"
    "MainCanonicalRuntime_Reset(workspace);"
    "return MainArcadeRaceDigest_Fail(MAIN_ARCADE_RACE_DIGEST_FAILURE_RELEASE);")
foreach(pair IN ITEMS
        "if ((view == NULL) || (view->frameNumber != raceTick))@@MAIN_ARCADE_RACE_DIGEST_FAILURE_VIEW)"
        "if (!MainCanonicalRuntime_ReleaseV4(workspace, &state->request))@@MAIN_ARCADE_RACE_DIGEST_FAILURE_RELEASE)")
    string(REPLACE "@@" ";" pair "${pair}")
    list(GET pair 0 opener)
    list(GET pair 1 latch)
    string(FIND "${tick_body}" "${opener}" opener_at)
    string(SUBSTRING "${tick_body}" ${opener_at} -1 failure_path)
    string(FIND "${failure_path}" "${latch}" latch_at)
    string(SUBSTRING "${failure_path}" 0 ${latch_at} failure_path)
    ctr_count_identifier("${failure_path}" "return" failure_returns)
    ctr_count_identifier("${failure_path}" "MainCanonicalRuntime_Reset" failure_resets)
    if(NOT failure_returns EQUAL 1 OR NOT failure_resets EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${digest_source}'s ${latch} path must end the frame (one Reset) before its one return")
    endif()
endforeach()
ctr_require_order("${digest_source} (MainArcadeRaceDigest_Project)" "${project_body}"
    "NativePerf_BeginScope(NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST);"
    "MainArcadeRaceDigest_ProjectTick(raceTick, sources, out);"
    "NativePerf_EndScope(NATIVE_PERF_BUCKET_ARCADE_RACE_DIGEST);")
ctr_require("${digest_source} (MainArcadeRaceDigest_EndRace)" "${end_body}"
    "MainCanonicalRuntime_InvalidateTopology(MainCanonicalRuntime_Global())")
foreach(pair IN ITEMS
        "MainCanonicalRuntime_Reset|3" "MainCanonicalRuntime_BeginFrame|1" "MainCanonicalRuntime_PrepareV4|1"
        "MainCanonicalRuntime_ViewV4|1" "MainCanonicalRuntime_ReleaseV4|2" "MainCanonicalRuntime_InvalidateTopology|1"
        "MainCanonicalRuntime_Init|0" "NativeCanonicalTopologyV1_Init|2" "NativePerf_BeginScope|1" "NativePerf_EndScope|1"
        "MainCanonicalWorldCounters_ExtractV1|1" "MainCanonicalWorldMineRegistry_ExtractV1|1"
        "NativeDeterministicRngBankV1_Init|0" "NativeDeterministicRngBankV1_InitInPlace|0")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 name)
    list(GET pair 1 expected)
    ctr_count_identifier("${digest_c_code}" "${name}" hits)
    if(NOT hits EQUAL expected)
        message(FATAL_ERROR "${prefix}: ${digest_source} names ${name} ${hits} time(s), expected ${expected}")
    endif()
endforeach()
# The only runtime entries it names.
string(REPLACE ";" "@SEMI@" masked_digest "${digest_c_code}")
string(REGEX MATCHALL "MainCanonicalRuntime_[A-Za-z0-9_]+" runtime_names "${masked_digest}")
list(REMOVE_DUPLICATES runtime_names)
foreach(name IN LISTS runtime_names)
    set(known_list MainCanonicalRuntime_Global MainCanonicalRuntime_Reset MainCanonicalRuntime_FailureReason
        MainCanonicalRuntime_BeginFrame MainCanonicalRuntime_PrepareV4 MainCanonicalRuntime_ViewV4
        MainCanonicalRuntime_ReleaseV4 MainCanonicalRuntime_InvalidateTopology)
    list(FIND known_list "${name}" known_at)
    if(known_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${digest_source} calls ${name}; its runtime calls are the per-tick lifecycle, Reset, and InvalidateTopology")
    endif()
endforeach()
# The header exposes no runtime type (its callers never see the runtime).
ctr_forbid("${digest_header} (code)" "${digest_h_code}" "MainCanonicalRuntime")

# 3. Who names the digest API.
file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(digest_callers "${proof_source}")
set(scanned 0)
set(named_by "")
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path STREQUAL digest_source OR relative_path STREQUAL digest_header)
        continue()
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "MainArcadeRaceDigest" raw_hit)
    if(raw_hit EQUAL -1)
        continue()
    endif()
    ctr_code("${relative_path}" "${source}" code)
    string(REGEX MATCH "MainArcadeRaceDigest_[A-Za-z]" code_hit "${code}")
    if("${code_hit}" STREQUAL "")
        continue()
    endif()
    list(FIND digest_callers "${relative_path}" caller_at)
    if(caller_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${relative_path} names the race digest API; only ${digest_callers} may (the race caller joins in LR-S10)")
    endif()
    list(APPEND named_by "${relative_path}")
endforeach()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()
if(NOT "${named_by}" STREQUAL "${proof_source}")
    message(FATAL_ERROR "${prefix}: the roster proof must name the race digest API (found in '${named_by}')")
endif()
ctr_read_source("${proof_source}" proof)
ctr_code("${proof_source}" "${proof}" proof_code)
foreach(pair IN ITEMS "MainArcadeRaceDigest_Project|1" "MainArcadeRaceDigest_EndRace|1")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 name)
    list(GET pair 1 expected)
    ctr_count_regex("${proof_code}" "${name}[ \t]*[(]" hits)
    if(NOT hits EQUAL expected)
        message(FATAL_ERROR "${prefix}: ${proof_source} calls ${name} ${hits} time(s), expected ${expected}")
    endif()
endforeach()
foreach(term IN ITEMS "sources.bank = MainArcadeRaceSetup_Bank();" "sources.mineSource = &D231;" "sources.input = &frameState->input;"
        "sources.config = NativeArcadeRosterProof_Config();" "MainArcadeRaceDigest_Project(state->raceTick, &sources, &tick)")
    ctr_require("${proof_source} (code)" "${proof_code}" "${term}")
endforeach()

# 4. The unity chain and ctr_native's links.
ctr_read_source("game/game_unity.h" unity)
foreach(name IN ITEMS MainCanonicalWorldCounters.c MainCanonicalWorldMineRegistry.c MainArcadeRaceDigest.c)
    ctr_count_regex("${unity}" "#include \"MAIN/${name}\"" unity_hits)
    if(NOT unity_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: game/game_unity.h must include MAIN/${name} exactly once (found ${unity_hits})")
    endif()
endforeach()
ctr_require_order("game/game_unity.h" "${unity}"
    "#include \"MAIN/MainCanonicalRuntime.c\""
    "#include \"MAIN/MainCanonicalWorldCounters.c\""
    "#include \"MAIN/MainCanonicalWorldMineRegistry.c\""
    "#include \"MAIN/MainArcadeRaceSetup.c\""
    "#include \"MAIN/MainArcadeRaceDigest.c\""
    "#include \"MAIN/MainArcadeRaceLaunch.c\""
    "#include \"MAIN/MainArcadeRosterProof.c\"")
ctr_read_source("CMakeLists.txt" cmake)
string(REGEX MATCH "target_link_libraries\\(ctr_native [^)]*\\)" ctr_native_links "${cmake}")
if("${ctr_native_links}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ctr_native's target_link_libraries line is missing")
endif()
foreach(library IN ITEMS ctr_native_main_canonical_world_counters ctr_native_main_canonical_world_mine_registry)
    string(FIND "${ctr_native_links}" "${library}" link_hit)
    if(NOT link_hit EQUAL -1)
        message(FATAL_ERROR "${prefix}: ctr_native links ${library}; its source is unity-included, and a pulled member would define sdata twice")
    endif()
endforeach()

# 5. The LR-17 pin: the only nav header last readers.
file(GLOB pin_files RELATIVE "${repo}"
    "${repo}/game/MAIN/MainCanonical*.c" "${repo}/game/MAIN/MainCanonical*.h"
    "${repo}/game/MAIN/MainArcade*.c" "${repo}/game/MAIN/MainArcade*.h")
list(LENGTH pin_files pin_count)
if(pin_count LESS 40)
    message(FATAL_ERROR "${prefix}: the LR-17 pin found only ${pin_count} MainCanonical*/MainArcade* sources; the glob is broken")
endif()
set(all_readers "")
foreach(relative_path IN LISTS pin_files)
    ctr_read_source("${relative_path}" pin_source)
    string(REGEX MATCH "${ctr_nav_header_regex}" pin_names "${pin_source}")
    if("${pin_names}" STREQUAL "")
        continue()
    endif()
    ctr_code("${relative_path}" "${pin_source}" pin_code)
    ctr_pin_last_readers("${relative_path}" "${pin_code}" readers)
    foreach(reader IN LISTS readers)
        list(APPEND all_readers "${relative_path}:${reader}")
    endforeach()
endforeach()
list(SORT all_readers)
set(expected_readers
    "game/MAIN/MainCanonicalDrivers.c:MainCanonicalDrivers_BotNavIndex"
    "game/MAIN/MainCanonicalTopologyLeaseAuthority.c:MainCanonicalTopologyLease_ObservePostInit")
if(NOT "${all_readers}" STREQUAL "${expected_readers}")
    message(FATAL_ERROR "${prefix}: LR-17 pin: the NavHeader last readers are '${all_readers}', expected exactly '${expected_readers}'")
endif()
# The corrected comments (LR-17, ruled (a)).
ctr_read_source("${lease_header}" lease_h)
ctr_forbid("${lease_header}" "${lease_h}" "sole API")
foreach(term IN ITEMS "lease's only API that" "MainCanonicalDrivers_BotNavIndex" "LR-17, ruled (a)")
    ctr_require("${lease_header}" "${lease_h}" "${term}")
endforeach()
ctr_read_source("${runtime_header}" runtime_h)
ctr_read_source("${runtime_source}" runtime_c)
ctr_forbid("${runtime_header}" "${runtime_h}" "reads D231, dereferences")
ctr_forbid("${runtime_source}" "${runtime_c}" "D231, NavHeader.last, and the topology")
foreach(label IN ITEMS runtime_header runtime_source)
    if(label STREQUAL "runtime_header")
        set(text "${runtime_h}")
    else()
        set(text "${runtime_c}")
    endif()
    foreach(term IN ITEMS "MainCanonicalDrivers_BotNavIndex" "LR-17, ruled (a)")
        ctr_require("${${label}}" "${text}" "${term}")
    endforeach()
endforeach()

# 6. The unavailable topology digest and the report version.
ctr_read_source("tests/main_arcade_race_digest_test.c" unit_test)
string(REGEX MATCH "#define UNAVAILABLE_TOPOLOGY_DIGEST UINT64_C\\(0x([0-9a-f]+)\\)" unit_constant "${unit_test}")
set(unit_digest "${CMAKE_MATCH_1}")
ctr_read_source("tools/arcade-roster-proof-check.ps1" checker)
string(REGEX MATCH "\\$unavailableTopologyDigest = '([0-9a-f]+)'" checker_constant "${checker}")
set(checker_digest "${CMAKE_MATCH_1}")
if("${unit_digest}" STREQUAL "" OR NOT "${unit_digest}" STREQUAL "${checker_digest}")
    message(FATAL_ERROR "${prefix}: the unit test's unavailable topology digest '${unit_digest}' is not the proof check's '${checker_digest}'")
endif()
string(LENGTH "${unit_digest}" unit_digest_length)
if(NOT unit_digest_length EQUAL 16)
    message(FATAL_ERROR "${prefix}: the unavailable topology digest '${unit_digest}' is not 16 hex digits")
endif()
ctr_require("tools/arcade-roster-proof-check.ps1" "${checker}" "'arcade roster proof v11'")
ctr_forbid("tools/arcade-roster-proof-check.ps1" "${checker}" "'arcade roster proof v10'")
