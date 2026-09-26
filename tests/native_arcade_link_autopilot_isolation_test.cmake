# Structural isolation for the internal two-process gate autopilot
# (docs/RACE_LAUNCH_MILESTONE.md RL-15, slice RL-S10):
#  1. the pure module (platform/native_arcade_link_autopilot.{c,h}) includes
#     only its allowed headers, uses no heap, keeps no file-scope mutable
#     state, names no pad, socket, link, lockstep, replay, checkpoint,
#     canonical-state, or topology-lease token, and does its one I/O (fopen)
#     only in NativeArcadeLinkAutopilot_WriteReport; its library links
#     nothing, is C17 with extensions off, and ctr_native links it;
#  2. the game glue (game/MAIN/MainArcadeLinkAutopilot.{c,h}) is one
#     CTR_NATIVE block holding a CTR_INTERNAL implementation and inert
#     entries otherwise; it never touches a pad (no installed-pad call, no
#     gamepad field) and never calls NativeArcadeLinkHost_Enter or
#     NativeArcadeLinkHost_Tick itself (the hook feeds its decision to them);
#     it probes the arcade-link policy exactly once for the enter window; it
#     never names the race setup, the roster proof, replay, checkpoint,
#     canonical state, or the topology lease, and no stdio; each frame entry
#     returns first when inactive; its only race-caller names are the
#     read-only RL-12 evidence getters; since LR-S10 part 2 (the Task 8
#     race plan) its internal part also defines MainArcadeLinkAutopilot_Active,
#     which only returns the active flag, and Configure copies the race tick
#     cap into the report state right after the Init;
#  3. the arcade-link hook calls MainArcadeLinkAutopilot_Input exactly once,
#     inside its LINK branch right before its owned-frame link tick, and
#     MainArcadeLinkAutopilot_AfterTick exactly once, right after its host
#     tick, which still takes output->heldButtons; nothing else outside the
#     glue, main.c, and the race caller names the glue;
#  4. main.c parses the option after the roster proof checks and before any
#     replay parser, rejects it in non-internal builds, rejects it without
#     --arcade-link and together with replay options, --arcade-roster-proof,
#     and --exit-after-frame; the roster proof exclusion is unchanged; it
#     configures the glue once, in an internal-build guard, after the link
#     host and before CTR_Main;
#  5. the unity chain includes the glue once, after the roster proof launcher
#     and before the 231 overlay;
#  6. the race caller's evidence getters only read: the caller writes its
#     RL-15 fields only in its RL-12 log step; it names the glue only in its
#     include and one MainArcadeLinkAutopilot_Active call, the first
#     statement of its autopilot sample, inside its CTR_INTERNAL part;
#  7. the live gate is registered: arcade_link_launch runs the checker on
#     Windows with SKIP_RETURN_CODE 77, RUN_SERIAL TRUE, and the label live,
#     and the checker uses the fixed loopback ports and the autopilot option,
#     passes the race tick cap 300, and reads the v2 report's race ticks line.
# Since LR-S13 part A (docs/LOCKSTEP_RACE_MILESTONE.md LR-73, LR-74):
#  1b. the module parses --arcade-link-autopilot-freeze and
#      --arcade-link-autopilot-desync with the race tick count's value rule
#      and rejects any of the three race tick options without
#      --arcade-link-autopilot (so main.c's autopilot rejections, keyed on
#      the enabled flag, cover them: non-internal builds, replay options, the
#      roster proof, and --exit-after-frame), and defines the freeze length
#      45 periods literally;
#  2b. the glue's Configure copies the freeze and desync ticks right after
#      the race tick cap, and MainArcadeLinkAutopilot_Fault, internal only,
#      only answers the pure FaultAt over the run's state (NONE while
#      inactive);
#  4b. main.c's invalid-option message names both options, and main.c never
#      reads the two ticks itself;
#  6b. the race caller names the glue only in its include, two
#      MainArcadeLinkAutopilot_Active calls, and one MainArcadeLinkAutopilot_Fault
#      call, all inside its CTR_INTERNAL part, and its autopilot tick returns
#      first while the autopilot is inactive.
# Since LR-S13 part B (docs/LOCKSTEP_RACE_MILESTONE.md LR-75, LR-76):
#  1c. the run is three races (RACES 3u); EndAccepted is exactly the LR-16
#      scenario's table (race 1 FINISHED, race 2 DESYNC or PEER_TIMEOUT,
#      race 3 PEER_TIMEOUT) and the only place the module decides on
#      FINISHED; Observe records each RESULTS entry's end reason, then fails a
#      rejected one, then checks the evidence of the accepted one; Decide acts
#      on RESULTS only after an accepted end; the report is v3 with the fault
#      tick lines after the race ticks line and an end reason line per race;
#  7b. the live gate runs the three races: the checker passes the 6000-tick
#      cap to both cabinets and the freeze (600) and desync (300) options to
#      cab2 alone, one --capture-frame per cabinet, kills cab2 at its race 3
#      tick 300, checks the 90-period stall timeout, reads the v3 report, and
#      waits 780 s (ctest TIMEOUT 900).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade link autopilot isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    # Line endings depend on the checkout (and CMake on Windows reads in text
    # mode, other hosts do not); the checks below assume LF.
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_literal relative_path source literal)
    string(FIND "${source}" "${literal}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${literal}' missing from ${relative_path}")
    endif()
endfunction()

# Fails unless every term appears in source, each one after the previous one.
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

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# Counts regex matches of pattern in source.
function(ctr_count source pattern out_var)
    string(REGEX MATCHALL "${pattern}" hits "${source}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Sets out_begin and out_end to the offsets of the block following opener.
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

function(ctr_block_text relative_path source opener out_var)
    ctr_find_block("${relative_path}" "${source}" "${opener}" begin end)
    math(EXPR length "${end} - ${begin} + 1")
    string(SUBSTRING "${source}" ${begin} ${length} block)
    set(${out_var} "${block}" PARENT_SCOPE)
endfunction()

# Tokens no autopilot file may name: pads, sockets and the link layers,
# replay, checkpoints, canonical state, the topology lease, and the heap.
set(common_forbidden
    Platform_InputInstallPadSnapshots Platform_InputClearInstalledPadSnapshots PadSnapshot buttonsHeld buttonsTapped gamepad
    NativeArcadeNetplay_ native_arcade_netplay NativeLobby native_lobby NativeLockstep native_lockstep lockstep udp_transport NativeUdp
    winsock WSA sockaddr
    NativeReplay native_replay NativeCanonical native_canonical Checkpoint checkpoint
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner LOAD_Hub_ReadFile
    malloc calloc realloc "free(" alloca)

# 1. The pure module.
set(module_header "include/platform/native_arcade_link_autopilot.h")
set(module_source "platform/native_arcade_link_autopilot.c")
ctr_read_source("${module_header}" header)
ctr_read_source("${module_source}" module)
ctr_strip_comments("${header}" header_code)
ctr_strip_comments("${module}" module_code)
foreach(pair "${module_header}|header_code" "${module_source}|module_code")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 variable)
    foreach(term IN LISTS common_forbidden)
        ctr_forbid("${relative_path}" "${${variable}}" "${term}")
    endforeach()
    foreach(term IN ITEMS MainArcade Platform_ NativeArcadeLinkHost_ NativeMatchConfig NATIVE_MATCH_ SDL_ OtherFX)
        ctr_forbid("${relative_path}" "${${variable}}" "${term}")
    endforeach()
endforeach()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" header_includes "${header}")
foreach(include_line IN LISTS header_includes)
    if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|\"platform/native_arcade_link_host\\.h\")[ \t]*$")
        message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${module_header}")
    endif()
endforeach()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" module_includes "${module}")
foreach(include_line IN LISTS module_includes)
    if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdarg\\.h>|<stddef\\.h>|<stdint\\.h>|<stdio\\.h>|<string\\.h>|\"platform/native_arcade_link_autopilot\\.h\"|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_link_host\\.h\"|\"platform/native_arcade_menu_input\\.h\")[ \t]*$")
        message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${module_source}")
    endif()
endforeach()
# No file-scope mutable state: every file-scope static object is const.
# (';' is swapped for '@' first: a CMake list would split on it.)
string(REPLACE ";" "@" module_code_at "${module_code}")
string(REGEX MATCHALL "(^|\n)static[^\n(@]*@" module_statics "${module_code_at}")
foreach(declaration IN LISTS module_statics)
    if(NOT declaration MATCHES "^\n?static const ")
        message(FATAL_ERROR "${prefix}: ${module_source} holds file-scope mutable state ('${declaration}')")
    endif()
endforeach()
# The one I/O: fopen, fwrite, and fclose only in the report writer.
foreach(call IN ITEMS "fopen\\(" "fwrite\\(" "fclose\\(")
    ctr_count("${module_code}" "${call}" io_hits)
    if(NOT io_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${module_source} must call ${call} exactly once, in NativeArcadeLinkAutopilot_WriteReport (found ${io_hits})")
    endif()
endforeach()
ctr_block_text("${module_source}" "${module_code}"
    "int NativeArcadeLinkAutopilot_WriteReport(const char *path, const struct NativeArcadeLinkAutopilot *autopilot)" writer_block)
foreach(call IN ITEMS "fopen(path, \"wb\")" "fwrite(" "fclose(file)")
    ctr_require_literal("${module_source} (NativeArcadeLinkAutopilot_WriteReport)" "${writer_block}" "${call}")
endforeach()
foreach(term IN ITEMS printf puts fputs stdout stderr remove rename fprintf)
    ctr_count("${module_code}" "(^|[^A-Za-z_])${term}\\(" io_hits)
    if(NOT io_hits EQUAL 0)
        message(FATAL_ERROR "${prefix}: ${module_source} names ${term}; its only I/O is the report writer")
    endif()
endforeach()
ctr_require_literal("${module_source}" "${module_code}" "static const char k_autopilotOption[] = \"--arcade-link-autopilot\";")
# 1b. The fault options (LR-73): the same value rule as the race tick count,
# and each needs the autopilot.
foreach(literal IN ITEMS "static const char k_freezeOption[] = \"--arcade-link-autopilot-freeze\";"
        "static const char k_desyncOption[] = \"--arcade-link-autopilot-desync\";"
        "tickValue = &candidate.raceTickLimit;" "tickValue = &candidate.freezeTick;" "tickValue = &candidate.desyncTick;"
        "!NativeArcadeLinkAutopilotOptions_ParseRaceTicks(argv[index + 1], tickValue)"
        "if ((seenRaceTicks || seenFreeze || seenDesync) && !seen)")
    ctr_require_literal("${module_source}" "${module_code}" "${literal}")
endforeach()
ctr_count("${module_code}" "NativeArcadeLinkAutopilotOptions_ParseRaceTicks\\(" parse_value_hits)
if(NOT parse_value_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${module_source} must define the race tick value rule once and use it once, for all three race tick options (found ${parse_value_hits})")
endif()
ctr_count("${header_code}" "#define NATIVE_ARCADE_LINK_AUTOPILOT_FREEZE_PERIODS 45u\n" freeze_periods_hits)
if(NOT freeze_periods_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${module_header} must define NATIVE_ARCADE_LINK_AUTOPILOT_FREEZE_PERIODS as 45u (LR-16's 1.5 s freeze)")
endif()
ctr_require_literal("${module_header}" "${header_code}"
    "uint32_t NativeArcadeLinkAutopilot_FaultAt(const struct NativeArcadeLinkAutopilot *autopilot, uint32_t raceTick);")
# 1c. The three-race run (LR-75).
ctr_count("${header_code}" "#define NATIVE_ARCADE_LINK_AUTOPILOT_RACES 3u\n" races_hits)
if(NOT races_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${module_header} must define NATIVE_ARCADE_LINK_AUTOPILOT_RACES as 3u (LR-16's three races)")
endif()
ctr_require_literal("${module_header}" "${header_code}" "int NativeArcadeLinkAutopilot_EndAccepted(uint32_t race, uint32_t endReason);")
ctr_block_text("${module_source}" "${module_code}" "int NativeArcadeLinkAutopilot_EndAccepted(uint32_t race, uint32_t endReason)" accepted_block)
string(REGEX REPLACE "[ \t\r\n]+" " " accepted_flat "${accepted_block}")
if(NOT accepted_flat STREQUAL "{ switch (race) { case 1u: return endReason == NATIVE_ARCADE_FLOW_END_FINISHED; case 2u: return (endReason == NATIVE_ARCADE_FLOW_END_DESYNC) || (endReason == NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT); case 3u: return endReason == NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT; default: return 0; } }")
    message(FATAL_ERROR "${prefix}: NativeArcadeLinkAutopilot_EndAccepted must be exactly the LR-16 scenario's ends (found '${accepted_flat}')")
endif()
# FINISHED is decided on only in EndAccepted (the other use is its name).
ctr_count("${module_code}" "NATIVE_ARCADE_FLOW_END_FINISHED" finished_hits)
if(NOT finished_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${module_source} may name NATIVE_ARCADE_FLOW_END_FINISHED only in EndAccepted and EndReasonName (found ${finished_hits})")
endif()
ctr_block_text("${module_source}" "${module_code}"
    "int NativeArcadeLinkAutopilot_Observe(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view,\n\tuint32_t action)"
    observe_block)
ctr_require_order("${module_source} (NativeArcadeLinkAutopilot_Observe)" "${observe_block}"
    "if ((view->screen == NATIVE_ARCADE_FLOW_SCREEN_RESULTS) && (previous != NATIVE_ARCADE_FLOW_SCREEN_RESULTS))"
    "if (autopilot->racesEnded >= NATIVE_ARCADE_LINK_AUTOPILOT_RACES)"
    "NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE);"
    "race = &autopilot->races[autopilot->racesEnded];" "race->endReason = view->endReason;" "race->ended = 1u;"
    "if (!NativeArcadeLinkAutopilot_EndAccepted(autopilot->racesEnded + 1u, view->endReason))"
    "NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED);"
    "autopilot->racesEnded++;"
    "if ((autopilot->racesEnded != autopilot->racesStarted) || (autopilot->racesValidated != autopilot->racesStarted))"
    "NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING);"
    "(autopilot->racesEnded == NATIVE_ARCADE_LINK_AUTOPILOT_RACES)"
    "if (action == NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE)"
    "(autopilot->exitConfirmed != 0u) && (autopilot->racesStarted == NATIVE_ARCADE_LINK_AUTOPILOT_RACES)"
    "(autopilot->racesValidated == NATIVE_ARCADE_LINK_AUTOPILOT_RACES)"
    "(autopilot->racesEnded == NATIVE_ARCADE_LINK_AUTOPILOT_RACES)"
    "(autopilot->rematches == NATIVE_ARCADE_LINK_AUTOPILOT_RACES - 1u)")
ctr_count("${module_code}" "racesEnded\\+\\+" ended_increments)
if(NOT ended_increments EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${module_source} must count an ended race only once, after its end was accepted (found ${ended_increments})")
endif()
ctr_block_text("${module_source}" "${module_code}"
    "int NativeArcadeLinkAutopilot_Decide(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view,\n\tuint8_t enterReady, struct NativeArcadeLinkAutopilotOutput *output)"
    decide_block)
ctr_require_order("${module_source} (NativeArcadeLinkAutopilot_Decide)" "${decide_block}"
    "case NATIVE_ARCADE_FLOW_SCREEN_RESULTS:"
    "(view->rowsEnabled != 0u) && (autopilot->racesEnded != 0u) &&"
    "NativeArcadeLinkAutopilot_EndAccepted(autopilot->racesEnded, view->endReason))"
    "(autopilot->racesEnded >= NATIVE_ARCADE_LINK_AUTOPILOT_RACES) ? NATIVE_ARCADE_FLOW_ROW_EXIT")
ctr_block_text("${module_source}" "${module_code}"
    "int NativeArcadeLinkAutopilot_FormatReport(const struct NativeArcadeLinkAutopilot *autopilot, char *buffer, size_t capacity, size_t *length)"
    report_block)
ctr_require_order("${module_source} (NativeArcadeLinkAutopilot_FormatReport)" "${report_block}"
    "\"arcade link autopilot v3\\n\"" "\"race ticks %u\\n\", (unsigned)autopilot->raceTickLimit"
    "\"freeze tick %u\\n\", (unsigned)autopilot->freezeTick" "\"desync tick %u\\n\", (unsigned)autopilot->desyncTick"
    "\"race %u validated launch %u\"" "if (race->ended != 0u)" "\"race %u end reason %s\\n\""
    "NativeArcadeLinkAutopilot_EndReasonName(race->endReason)" "\"end races %u\\n\"")
ctr_forbid("${module_source}" "${module_code}" "autopilot v2")

ctr_read_source("CMakeLists.txt" cmake)
string(FIND "${cmake}" "add_library(ctr_native_arcade_link_autopilot STATIC platform/native_arcade_link_autopilot.c)" library_at)
if(library_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: CMakeLists.txt must declare ctr_native_arcade_link_autopilot from ${module_source} alone")
endif()
string(SUBSTRING "${cmake}" ${library_at} 400 library_block)
ctr_require_order("CMakeLists.txt (ctr_native_arcade_link_autopilot)" "${library_block}"
    "set_target_properties(ctr_native_arcade_link_autopilot PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF")
string(FIND "${cmake}" "target_link_libraries(ctr_native_arcade_link_autopilot" library_links)
if(NOT library_links EQUAL -1)
    message(FATAL_ERROR "${prefix}: ctr_native_arcade_link_autopilot must link nothing")
endif()
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(REGEX MATCH "[ \t\r\n]ctr_native_arcade_link_autopilot([ \t\r\n]|$)" autopilot_link_hit "${native_link_block}")
if("${autopilot_link_hit}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ctr_native must link ctr_native_arcade_link_autopilot")
endif()

# 2. The game glue.
set(glue_source "game/MAIN/MainArcadeLinkAutopilot.c")
set(glue_header "game/MAIN/MainArcadeLinkAutopilot.h")
ctr_read_source("${glue_source}" glue)
ctr_read_source("${glue_header}" glue_header_text)
ctr_strip_comments("${glue}" glue_code)
ctr_strip_comments("${glue_header_text}" glue_header_code)
foreach(pair "${glue_source}|glue_code" "${glue_header}|glue_header_code")
    string(REPLACE "|" ";" pair_items "${pair}")
    list(GET pair_items 0 relative_path)
    list(GET pair_items 1 variable)
    foreach(term IN LISTS common_forbidden)
        ctr_forbid("${relative_path}" "${${variable}}" "${term}")
    endforeach()
    foreach(term IN ITEMS NativeArcadeLinkHost_Enter NativeArcadeLinkHost_Tick NativeArcadeLinkHost_Configure
            NativeArcadeLinkHost_Shutdown NativeArcadeLinkHost_AbortToTitle NativeArcadeLinkHost_ReportRaceFailure
            MainArcadeRaceSetup MainArcadeRosterProof NativeArcadeRosterProof MainArcadeRaceLaunchCore
            NATIVE_MATCH_SLOT_ROLE_ NativeMatchConfig NATIVE_MATCH_SELECT_
            printf fflush stdout stderr fopen fwrite OtherFX)
        ctr_forbid("${relative_path}" "${${variable}}" "${term}")
    endforeach()
endforeach()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" glue_header_includes "${glue_header_text}")
if(NOT "${glue_header_includes}" STREQUAL "#include <stdint.h>")
    message(FATAL_ERROR "${prefix}: ${glue_header} must include only <stdint.h> (found '${glue_header_includes}')")
endif()
string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" glue_includes "${glue}")
foreach(include_line IN LISTS glue_includes)
    if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<common\\.h>|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_link_autopilot\\.h\"|\"platform/native_arcade_link_host\\.h\"|\"platform/native_log\\.h\"|\"MAIN/MainArcadeLinkPolicy\\.h\"|\"MAIN/MainArcadeRaceLaunch\\.h\"|\"MAIN/MainArcadeLinkAutopilot\\.h\")[ \t]*$")
        message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${glue_source}")
    endif()
endforeach()
# One CTR_NATIVE block around a CTR_INTERNAL implementation and its inert
# #else entries: nothing but comments before it, nothing after its #endif.
string(REGEX MATCHALL "#[ \t]*if[^\r\n]*" glue_ifs "${glue_code}")
string(REGEX MATCHALL "#[ \t]*else" glue_elses "${glue_code}")
string(REGEX MATCHALL "#[ \t]*elif" glue_elifs "${glue_code}")
string(REGEX MATCHALL "#[ \t]*endif" glue_endifs "${glue_code}")
list(LENGTH glue_elses glue_else_count)
list(LENGTH glue_elifs glue_elif_count)
list(LENGTH glue_endifs glue_endif_count)
if(NOT "${glue_ifs}" STREQUAL "#if defined(CTR_NATIVE);#if defined(CTR_INTERNAL)" OR NOT glue_else_count EQUAL 1 OR
        NOT glue_elif_count EQUAL 0 OR NOT glue_endif_count EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${glue_source} must be one #if defined(CTR_NATIVE) block holding one #if defined(CTR_INTERNAL) ... #else ... #endif (found '${glue_ifs}', ${glue_else_count} #else, ${glue_elif_count} #elif, ${glue_endif_count} #endif)")
endif()
string(FIND "${glue}" "#if defined(CTR_NATIVE)" glue_guard_at)
string(SUBSTRING "${glue}" 0 ${glue_guard_at} glue_before_guard)
ctr_strip_comments("${glue_before_guard}" glue_before_guard)
if(NOT glue_before_guard MATCHES "^[ \t\r\n]*$")
    message(FATAL_ERROR "${prefix}: ${glue_source} has code before #if defined(CTR_NATIVE)")
endif()
if(NOT glue MATCHES "#endif[ \t\r\n]*#endif[ \t\r\n]*$")
    message(FATAL_ERROR "${prefix}: ${glue_source} must end with the #endif of its CTR_INTERNAL block and then of its CTR_NATIVE block")
endif()
string(FIND "${glue_code}" "#if defined(CTR_INTERNAL)" internal_at)
string(FIND "${glue_code}" "#else" else_at)
string(SUBSTRING "${glue_code}" ${internal_at} -1 internal_tail)
string(FIND "${internal_tail}" "#else" internal_else)
string(SUBSTRING "${internal_tail}" 0 ${internal_else} internal_part)
string(SUBSTRING "${glue_code}" ${else_at} -1 else_part)
# The implementation (configure, the exit request, the state) is internal only.
foreach(term IN ITEMS "void MainArcadeLinkAutopilot_Configure(" "Platform_RequestExit(" "static struct MainArcadeLinkAutopilotState s_mainArcadeLinkAutopilot;"
        "uint8_t MainArcadeLinkAutopilot_Active(void)" "uint32_t MainArcadeLinkAutopilot_Fault(uint32_t raceTick)")
    ctr_require_literal("${glue_source} (CTR_INTERNAL part)" "${internal_part}" "${term}")
    ctr_forbid("${glue_source} (#else part)" "${else_part}" "${term}")
endforeach()
# LR-S10 part 2: Configure copies the race tick cap into the report state
# right after the Init, and the active query only reads the flag.
ctr_block_text("${glue_source}" "${internal_part}"
    "void MainArcadeLinkAutopilot_Configure(const struct NativeArcadeLinkAutopilotOptions *options)" configure_block)
ctr_require_order("${glue_source} (MainArcadeLinkAutopilot_Configure)" "${configure_block}"
    "NativeArcadeLinkAutopilot_Init(&state->autopilot);" "state->autopilot.raceTickLimit = options->raceTickLimit;"
    "state->autopilot.freezeTick = options->freezeTick;" "state->autopilot.desyncTick = options->desyncTick;"
    "state->active = 1u;")
foreach(field IN ITEMS raceTickLimit freezeTick desyncTick)
    ctr_count("${glue_code}" "${field}" field_hits)
    if(NOT field_hits EQUAL 2)
        message(FATAL_ERROR "${prefix}: ${glue_source} may name ${field} only in Configure's one copy (found ${field_hits})")
    endif()
endforeach()
ctr_block_text("${glue_source}" "${internal_part}" "uint8_t MainArcadeLinkAutopilot_Active(void)" active_block)
if(NOT active_block MATCHES "^\\{[ \t\r\n]*return s_mainArcadeLinkAutopilot\\.active;[ \t\r\n]*\\}$")
    message(FATAL_ERROR "${prefix}: MainArcadeLinkAutopilot_Active must only return the active flag (found '${active_block}')")
endif()
ctr_require_literal("${glue_header}" "${glue_header_code}" "uint8_t MainArcadeLinkAutopilot_Active(void);")
# 2b. The fault query (LR-73): only the pure decision over the run's state,
# NONE while inactive.
ctr_block_text("${glue_source}" "${internal_part}" "uint32_t MainArcadeLinkAutopilot_Fault(uint32_t raceTick)" fault_block)
string(REGEX REPLACE "[ \t\r\n]+" " " fault_flat "${fault_block}")
if(NOT fault_flat STREQUAL "{ if (s_mainArcadeLinkAutopilot.active == 0u) { return NATIVE_ARCADE_LINK_AUTOPILOT_FAULT_NONE; } return NativeArcadeLinkAutopilot_FaultAt(&s_mainArcadeLinkAutopilot.autopilot, raceTick); }")
    message(FATAL_ERROR "${prefix}: MainArcadeLinkAutopilot_Fault must only return FaultAt over the run's state, NONE while inactive (found '${fault_flat}')")
endif()
ctr_require_literal("${glue_header}" "${glue_header_code}" "uint32_t MainArcadeLinkAutopilot_Fault(uint32_t raceTick);")
ctr_count("${glue_code}" "NativeArcadeLinkAutopilot_FaultAt\\(" fault_at_hits)
if(NOT fault_at_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${glue_source} must ask NativeArcadeLinkAutopilot_FaultAt exactly once, in MainArcadeLinkAutopilot_Fault (found ${fault_at_hits})")
endif()
ctr_count("${glue_code}" "Platform_RequestExit\\(" exit_hits)
if(NOT exit_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${glue_source} must request the exit exactly once (found ${exit_hits})")
endif()
# The #else part holds only the two inert entries.
if(NOT else_part MATCHES "^#else[ \t\r\n]*void MainArcadeLinkAutopilot_Input\\(const struct MainArcadeLinkPolicyInput \\*input, struct MainArcadeLinkPolicyOutput \\*output\\)[ \t\r\n]*\\{[ \t\r\n]*\\(void\\)input;[ \t\r\n]*\\(void\\)output;[ \t\r\n]*\\}[ \t\r\n]*void MainArcadeLinkAutopilot_AfterTick\\(uint32_t action\\)[ \t\r\n]*\\{[ \t\r\n]*\\(void\\)action;[ \t\r\n]*\\}[ \t\r\n]*#endif[ \t\r\n]*#endif[ \t\r\n]*$")
    message(FATAL_ERROR "${prefix}: the #else part of ${glue_source} must hold only the two inert frame entries")
endif()
# Each frame entry returns first when inactive.
ctr_block_text("${glue_source}" "${internal_part}"
    "void MainArcadeLinkAutopilot_Input(const struct MainArcadeLinkPolicyInput *input, struct MainArcadeLinkPolicyOutput *output)" input_block)
if(NOT input_block MATCHES "^\\{([ \t\r\n]*(struct|uint8_t)[ \t]+[A-Za-z_][A-Za-z0-9_ ]*[ \t\\*]*[A-Za-z_][A-Za-z0-9_]*( = &s_mainArcadeLinkAutopilot| = 0u)?;)*[ \t\r\n]*if \\(state->active == 0u\\)[ \t\r\n]*\\{[ \t\r\n]*return;")
    message(FATAL_ERROR "${prefix}: MainArcadeLinkAutopilot_Input must return first, before touching anything, when inactive")
endif()
ctr_block_text("${glue_source}" "${internal_part}" "void MainArcadeLinkAutopilot_AfterTick(uint32_t action)" after_block)
if(NOT after_block MATCHES "^\\{([ \t\r\n]*(struct|uint8_t|uint32_t)[ \t]+[A-Za-z_][A-Za-z0-9_ ]*[ \t\\*]*[A-Za-z_][A-Za-z0-9_]*(\\[[A-Z_ *]+\\])?( = &s_mainArcadeLinkAutopilot| = 0u)?;)*[ \t\r\n]*if \\(\\(state->active == 0u\\) \\|\\| \\(state->finished != 0u\\)\\)[ \t\r\n]*\\{[ \t\r\n]*return;")
    message(FATAL_ERROR "${prefix}: MainArcadeLinkAutopilot_AfterTick must return first, before touching anything, when inactive or finished")
endif()
# The enter only inside the hook's own enter window: one policy probe, and
# the input's enterPressed set only when the probe allows it.
ctr_count("${glue_code}" "MainArcadeLinkPolicy_Decide\\(" probe_hits)
if(NOT probe_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${glue_source} must probe MainArcadeLinkPolicy_Decide exactly once (found ${probe_hits})")
endif()
ctr_block_text("${glue_source}" "${internal_part}"
    "static uint8_t MainArcadeLinkAutopilot_EnterReady(const struct MainArcadeLinkPolicyInput *input, uint8_t *resetDemoCountdown)" probe_block)
ctr_require_order("${glue_source} (MainArcadeLinkAutopilot_EnterReady)" "${probe_block}"
    "struct MainArcadeLinkPolicyInput probe = *input;" "probe.rawHeld = MAIN_ARCADE_LINK_POLICY_BTN_START;" "probe.prevRawHeld = 0u;"
    "MainArcadeLinkPolicy_Decide(&probe, &probeOutput)" "return probeOutput.enterPressed;")
ctr_count("${glue_code}" "output->enterPressed = 1u" enter_writes)
if(NOT enter_writes EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${glue_source} must set output->enterPressed to 1 in exactly one place (found ${enter_writes})")
endif()
ctr_find_block("${glue_source}" "${input_block}" "if ((decision.enter != 0u) && (enterReady != 0u))" enter_begin enter_end)
math(EXPR enter_length "${enter_end} - ${enter_begin} + 1")
string(SUBSTRING "${input_block}" ${enter_begin} ${enter_length} enter_block)
ctr_require_literal("${glue_source} (enter window)" "${enter_block}" "output->enterPressed = 1u;")
ctr_require_order("${glue_source} (MainArcadeLinkAutopilot_Input)" "${input_block}"
    "output->heldButtons = 0u;" "output->enterPressed = 0u;" "NativeArcadeLinkHost_GetView(&view)"
    "MainArcadeLinkAutopilot_EnterReady(input, &resetDemoCountdown)"
    "NativeArcadeLinkAutopilot_Decide(&state->autopilot, &view, enterReady, &decision)"
    "output->heldButtons = decision.heldButtons;")
# The race caller's evidence getters, read only; nothing else of the caller.
string(REGEX MATCHALL "MainArcadeRaceLaunch_[A-Za-z]+" glue_caller_names "${glue_code}")
list(REMOVE_DUPLICATES glue_caller_names)
list(SORT glue_caller_names)
if(NOT "${glue_caller_names}" STREQUAL "MainArcadeRaceLaunch_LastValidated;MainArcadeRaceLaunch_ValidatedRaces")
    message(FATAL_ERROR "${prefix}: ${glue_source} may name only MainArcadeRaceLaunch_ValidatedRaces and MainArcadeRaceLaunch_LastValidated of the race caller (found '${glue_caller_names}')")
endif()

# 3. The hook.
set(hook_source "game/MAIN/MainArcadeLink.c")
ctr_read_source("${hook_source}" hook)
ctr_strip_comments("${hook}" hook_code)
ctr_require_literal("${hook_source}" "${hook}" "#include \"MAIN/MainArcadeLinkAutopilot.h\"")
foreach(call IN ITEMS "MainArcadeLinkAutopilot_Input\\(" "MainArcadeLinkAutopilot_AfterTick\\(")
    ctr_count("${hook_code}" "${call}" call_hits)
    if(NOT call_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${hook_source} must call '${call}' exactly once (found ${call_hits})")
    endif()
endforeach()
ctr_forbid("${hook_source}" "${hook_code}" "MainArcadeLinkAutopilot_Configure")
if(NOT hook_code MATCHES "if \\(input\\.hostMode == \\(uint32_t\\)NATIVE_ARCADE_LINK_HOST_MODE_LINK\\)[ \t\r\n]*\\{[ \t\r\n]*MainArcadeLinkAutopilot_Input\\(&input, &output\\);[ \t\r\n]*MainArcadeLink_LinkTick\\(gGT, &output\\);[ \t\r\n]*\\}")
    message(FATAL_ERROR "${prefix}: ${hook_source} must call MainArcadeLinkAutopilot_Input(&input, &output) right before the owned-frame MainArcadeLink_LinkTick(gGT, &output), alone in its LINK branch")
endif()
if(NOT hook_code MATCHES "action = NativeArcadeLinkHost_Tick\\(output->heldButtons, MainArcadeRaceLaunch_RaceFinished\\(\\)\\);[ \t\r\n]*MainArcadeLinkAutopilot_AfterTick\\(action\\);")
    message(FATAL_ERROR "${prefix}: ${hook_source} must tick the host with output->heldButtons and call MainArcadeLinkAutopilot_AfterTick(action) right after")
endif()
ctr_count("${hook_code}" "NativeArcadeLinkHost_Enter\\(" hook_enter_hits)
if(NOT hook_enter_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${hook_source} must call NativeArcadeLinkHost_Enter exactly once, on output->enterPressed (found ${hook_enter_hits})")
endif()
ctr_require_literal("${hook_source}" "${hook_code}" "if (output->enterPressed != 0u)\n\t{\n\t\t(void)NativeArcadeLinkHost_Enter();")
# Nobody else names the glue.
file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(scanned 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path STREQUAL glue_source OR relative_path STREQUAL glue_header OR relative_path STREQUAL hook_source OR
            relative_path STREQUAL "main.c" OR relative_path STREQUAL "game/MAIN/MainArcadeRaceLaunch.c")
        continue()
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "MainArcadeLinkAutopilot_" raw_hit)
    if(raw_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    string(FIND "${code}" "MainArcadeLinkAutopilot_" code_hit)
    if(NOT code_hit EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${relative_path} names the autopilot glue; only ${hook_source} and main.c may")
    endif()
endforeach()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()

# 4. main.c.
ctr_read_source("main.c" main_source)
ctr_strip_comments("${main_source}" main_code)
set(autopilot_reject "if ((arcadeLinkAutopilotOptions.enabled != 0u) &&\n\t    ((arcadeLinkOptions.enabled == 0u) || NativeArg_NamesReplayOption(argc, argv) || (rosterProofOptions.enabled != 0u) ||\n\t     NativeArcadeRosterProof_NamesExitOption(argc, argv)))")
ctr_block_text("main.c" "${main_code}" "${autopilot_reject}" reject_block)
ctr_require_order("main.c (autopilot rejection)" "${reject_block}"
    "--arcade-link-autopilot needs --arcade-link and cannot be combined with --arcade-roster-proof, --exit-after-frame, or replay record or playback options%s%s%s."
    "return NativeConsole_Return(1);")
set(roster_reject "if ((rosterProofOptions.enabled != 0u) &&\n\t    ((arcadeLinkOptions.enabled != 0u) || (arcadeLinkOptions.preview != (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE) || NativeArg_NamesReplayOption(argc, argv) ||\n\t     NativeArcadeRosterProof_NamesExitOption(argc, argv)))")
ctr_require_order("main.c" "${main_code}"
    "NativeArcadeLinkOptions_ApplyArgs(argc, argv, &arcadeLinkOptions)"
    "NativeArcadeRosterProofOptions_ApplyArgs(argc, argv, &rosterProofOptions)"
    "${roster_reject}"
    "NativeArcadeLinkAutopilotOptions_ApplyArgs(argc, argv, &arcadeLinkAutopilotOptions)"
    "return NativeConsole_Return(1);"
    "#if !defined(CTR_INTERNAL)"
    "if (arcadeLinkAutopilotOptions.enabled != 0u)"
    "--arcade-link-autopilot is available in internal builds only."
    "return NativeConsole_Return(1);"
    "#endif"
    "${autopilot_reject}"
    "NativeReplayScheduler_PrepareReportFromArgs(argc, argv)"
    "NativeReplayScheduler_ConfigureFromArgs(argc, argv)"
    "NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr)"
    "#if defined(CTR_INTERNAL)\n\t\n\tif (arcadeLinkAutopilotOptions.enabled != 0u)\n\t{\n\t\tMainArcadeLinkAutopilot_Configure(&arcadeLinkAutopilotOptions);"
    "#endif"
    "CTR_Main()")
ctr_count("${main_code}" "MainArcadeLinkAutopilot_Configure\\(" configure_hits)
if(NOT configure_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: main.c must call MainArcadeLinkAutopilot_Configure exactly once (found ${configure_hits})")
endif()
ctr_count("${main_code}" "MainArcadeLinkAutopilot_(Input|AfterTick)\\(" frame_hits)
if(NOT frame_hits EQUAL 0)
    message(FATAL_ERROR "${prefix}: main.c must not call the autopilot's frame entries")
endif()
ctr_count("${main_code}" "NativeArcadeLinkAutopilotOptions_ApplyArgs\\(" parse_hits)
if(NOT parse_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: main.c must parse the autopilot option exactly once (found ${parse_hits})")
endif()
# 4b. The fault options (LR-73) need the autopilot (1b), so the enabled-flag
# rejections above cover them; main.c names them in its invalid-option
# message and never reads the two ticks itself (only the glue's Configure).
ctr_block_text("main.c" "${main_code}" "if (!NativeArcadeLinkAutopilotOptions_ApplyArgs(argc, argv, &arcadeLinkAutopilotOptions))" invalid_block)
ctr_require_order("main.c (autopilot invalid option)" "${invalid_block}"
    "--arcade-link-autopilot <report path> (once)"
    "[--arcade-link-autopilot-race-ticks <1-18000> (once, needs --arcade-link-autopilot)]"
    "[--arcade-link-autopilot-freeze <1-18000> (once, needs --arcade-link-autopilot)]"
    "[--arcade-link-autopilot-desync <1-18000> (once, needs --arcade-link-autopilot)]"
    "return NativeConsole_Return(1);")
foreach(field IN ITEMS freezeTick desyncTick)
    ctr_forbid("main.c" "${main_code}" "${field}")
endforeach()

# 5. The unity chain.
ctr_read_source("game/game_unity.h" unity)
ctr_require_order("game/game_unity.h" "${unity}"
    "#include \"MAIN/MainArcadeLink.c\"" "#include \"MAIN/MainArcadeRaceLaunch.c\"" "#include \"MAIN/MainArcadeRosterProof.c\""
    "#include \"MAIN/MainArcadeLinkAutopilot.c\"" "#include \"231/R231.c\"")
ctr_count("${unity}" "MainArcadeLinkAutopilot\\.c\"" unity_hits)
if(NOT unity_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: game/game_unity.h must include MAIN/MainArcadeLinkAutopilot.c exactly once (found ${unity_hits})")
endif()
ctr_forbid("game/game_unity.h" "${unity}" "native_arcade_link_autopilot.c")

# 6. The race caller's RL-15 evidence is written only in its RL-12 log step.
set(caller_source "game/MAIN/MainArcadeRaceLaunch.c")
ctr_read_source("${caller_source}" caller)
ctr_strip_comments("${caller}" caller_code)
ctr_block_text("${caller_source}" "${caller_code}" "static void MainArcadeRaceLaunch_LogDigests(uint32_t raceNumber)" log_block)
foreach(field IN ITEMS validatedRaces validatedRace validatedDigests)
    ctr_count("${caller_code}" "${field}(\\[[A-Za-z0-9_ *+]*\\])?[ \t]*(=[^=]|\\+\\+|\\+=)" field_writes)
    ctr_count("${log_block}" "${field}(\\[[A-Za-z0-9_ *+]*\\])?[ \t]*(=[^=]|\\+\\+|\\+=)" field_log_writes)
    if(NOT field_writes EQUAL field_log_writes)
        message(FATAL_ERROR "${prefix}: ${caller_source} may write ${field} only in MainArcadeRaceLaunch_LogDigests")
    endif()
endforeach()
ctr_require_order("${caller_source} (MainArcadeRaceLaunch_LogDigests)" "${log_block}"
    "MainArcadeRaceSetup_Digests(digests[0], digests[1], digests[2], digests[3])"
    "MAIN_ARCADE_RACE_LAUNCH_LOG \"race %u validated config %s plan %s bots %s bank %s\\n\""
    "s_mainArcadeRaceLaunch.validatedDigests" "s_mainArcadeRaceLaunch.validatedRace = raceNumber;" "s_mainArcadeRaceLaunch.validatedRaces++;")
# Since LR-S10 part 2 the caller asks the glue read-only questions, and only
# in internal builds: whether the autopilot runs (so its local sample is the
# steering pad, LR-16), and since LR-S13 part A (LR-73, LR-74) whether it
# runs (for the per-tick digest line) and which fault injection a race tick
# gets. It names the glue only in the header include, the two
# MainArcadeLinkAutopilot_Active calls (the first statements of
# MainArcadeRaceLaunch_AutopilotSample and MainArcadeRaceLaunch_AutopilotTick),
# and the one MainArcadeLinkAutopilot_Fault call, all inside its
# #if defined(CTR_INTERNAL) part.
string(REGEX MATCHALL "MainArcadeLinkAutopilot[A-Za-z0-9_.]*" caller_glue_names "${caller_code}")
if(NOT "${caller_glue_names}" STREQUAL "MainArcadeLinkAutopilot.h;MainArcadeLinkAutopilot_Active;MainArcadeLinkAutopilot_Active;MainArcadeLinkAutopilot_Fault")
    message(FATAL_ERROR "${prefix}: ${caller_source} may name the glue only in its include, two MainArcadeLinkAutopilot_Active calls, and one MainArcadeLinkAutopilot_Fault call (found '${caller_glue_names}')")
endif()
string(FIND "${caller_code}" "#if defined(CTR_INTERNAL)" caller_internal_at)
string(FIND "${caller_code}" "#else" caller_else_at)
if(caller_internal_at EQUAL -1 OR caller_else_at LESS caller_internal_at)
    message(FATAL_ERROR "${prefix}: ${caller_source} has no #if defined(CTR_INTERNAL) ... #else part")
endif()
math(EXPR caller_internal_length "${caller_else_at} - ${caller_internal_at}")
string(SUBSTRING "${caller_code}" ${caller_internal_at} ${caller_internal_length} caller_internal_part)
string(SUBSTRING "${caller_code}" 0 ${caller_internal_at} caller_before_internal)
string(SUBSTRING "${caller_code}" ${caller_else_at} -1 caller_after_internal)
string(FIND "${caller_before_internal}${caller_after_internal}" "MainArcadeLinkAutopilot_" caller_outside_glue_at)
ctr_count("${caller_internal_part}" "MainArcadeLinkAutopilot_(Active|Fault)\\(" caller_inside_glue_calls)
if(NOT caller_outside_glue_at EQUAL -1 OR NOT caller_inside_glue_calls EQUAL 3)
    message(FATAL_ERROR "${prefix}: ${caller_source} must call MainArcadeLinkAutopilot_Active and MainArcadeLinkAutopilot_Fault only inside its #if defined(CTR_INTERNAL) part (found ${caller_inside_glue_calls} calls inside)")
endif()
ctr_block_text("${caller_source}" "${caller_internal_part}"
    "static void MainArcadeRaceLaunch_AutopilotTick(const struct MainArcadeRaceLaunchCoreOutput *output)" tick_block)
if(NOT tick_block MATCHES "^\\{([ \t\r\n]*(struct|char|size_t|uint32_t)[ \t]+[A-Za-z_][A-Za-z0-9_ ]*[ \t\\*]*[A-Za-z_][A-Za-z0-9_]*(\\[[A-Z0-9_]+\\])?( = 0u)?;)*[ \t\r\n]*if \\(MainArcadeLinkAutopilot_Active\\(\\) == 0u\\)[ \t\r\n]*\\{[ \t\r\n]*return;")
    message(FATAL_ERROR "${prefix}: MainArcadeRaceLaunch_AutopilotTick must return first, before touching anything, while the autopilot is inactive")
endif()
ctr_count("${tick_block}" "MainArcadeLinkAutopilot_Fault\\(output->raceTick\\)" tick_fault_hits)
if(NOT tick_fault_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: MainArcadeRaceLaunch_AutopilotTick must ask MainArcadeLinkAutopilot_Fault(output->raceTick) exactly once (found ${tick_fault_hits})")
endif()
ctr_block_text("${caller_source}" "${caller_code}"
    "static int MainArcadeRaceLaunch_AutopilotSample(const struct GameTracker *gGT, uint32_t raceTick, struct NativeArcadeLinkHostPad *sample)"
    sample_block)
if(NOT sample_block MATCHES "^\\{([ \t\r\n]*(const )?(struct|uint32_t)[ \t]+[A-Za-z_][A-Za-z0-9_ ]*[ \t\\*]*[A-Za-z_][A-Za-z0-9_]*( = [A-Za-z0-9_>.-]+)?;)*[ \t\r\n]*if \\(MainArcadeLinkAutopilot_Active\\(\\) == 0u\\)[ \t\r\n]*\\{[ \t\r\n]*return 0;")
    message(FATAL_ERROR "${prefix}: MainArcadeRaceLaunch_AutopilotSample must return 0 first, before touching anything, while the autopilot is inactive")
endif()

# 7. The live gate.
string(FIND "${cmake}" "add_test(NAME arcade_link_launch" live_at)
if(live_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: CMakeLists.txt must register the arcade_link_launch live test")
endif()
string(SUBSTRING "${cmake}" 0 ${live_at} before_live)
string(FIND "${before_live}" "if(WIN32)" win32_at REVERSE)
string(FIND "${before_live}" "endif()" endif_at REVERSE)
if(win32_at EQUAL -1 OR (NOT endif_at EQUAL -1 AND endif_at GREATER win32_at))
    message(FATAL_ERROR "${prefix}: arcade_link_launch must be registered inside if(WIN32)")
endif()
string(SUBSTRING "${cmake}" ${live_at} 1200 live_block)
ctr_require_order("CMakeLists.txt (arcade_link_launch)" "${live_block}"
    "COMMAND powershell -NoProfile -ExecutionPolicy Bypass"
    "tools/arcade-link-launch-check.ps1"
    "-Executable \"$<TARGET_FILE:ctr_native>\""
    "set_tests_properties(arcade_link_launch PROPERTIES"
    "SKIP_RETURN_CODE 77" "TIMEOUT" "RUN_SERIAL TRUE" "LABELS live)")
ctr_read_source("tools/arcade-link-launch-check.ps1" checker)
foreach(literal IN ITEMS "--arcade-link-autopilot" "'--arcade-link-autopilot-race-ticks', \$raceTickCap" "\$raceTickCap = 6000"
        "^race ticks ([0-9]+)\$" "'arcade link autopilot v3'" "127.0.0.1:7002" "127.0.0.1:7001" "'7001'" "'7002'" "'cab1'" "'cab2'"
        "Start-Process" "exit \$skipExitCode" "--arcade-link-autopilot is available in internal builds only."
        "arcade link requires a known build and content identity." "No displays available")
    ctr_require_literal("tools/arcade-link-launch-check.ps1" "${checker}" "${literal}")
endforeach()
# 7b. The three races (LR-76): the fault options go to cab2 alone, the capture
# to both, and the kill and the stall timeout are checked.
foreach(literal IN ITEMS "\$races = 3\n" "\$freezeTick = 600\n" "\$desyncTick = 300\n" "\$freezePeriods = 45\n" "\$killRaceTick = 300\n"
        "\$stallTimeoutPeriods = 90\n" "\$stallMarginPercent = 25\n" "[int]\$TimeoutSeconds = 780\n"
        "@{ Name = 'cab1'; Cab = 'cab1'; CabNumber = 1; Port = '7001'; Peer = '127.0.0.1:7002'; FaultArguments = @() },"
        "@{ Name = 'cab2'; Cab = 'cab2'; CabNumber = 2; Port = '7002'; Peer = '127.0.0.1:7001'\n            FaultArguments = @('--arcade-link-autopilot-freeze', \"\$freezeTick\", '--arcade-link-autopilot-desync', \"\$desyncTick\") })"
        "'--capture-frame', \"\$captureFrame=\$(\$Run.CapturePath)\") + \$Run.FaultArguments"
        "CapturePath = Join-Path \$resolvedOutput \"\$(\$spec.Name).race1.bmp\""
        "if (\$watch.Tick -ge \$killRaceTick)" "\$Victim.Process.Kill()"
        "\$expectedReportEnds = @('FINISHED', 'DESYNC|PEER_TIMEOUT', 'PEER_TIMEOUT')"
        "\$raceOneEndKinds = @('end of race', 'finish grace')")
    ctr_require_literal("tools/arcade-link-launch-check.ps1" "${checker}" "${literal}")
endforeach()
foreach(option IN ITEMS "'--arcade-link-autopilot-freeze'" "'--arcade-link-autopilot-desync'" "'--capture-frame'")
    ctr_count("${checker}" "${option}" option_hits)
    if(NOT option_hits EQUAL 1)
        message(FATAL_ERROR "${prefix}: tools/arcade-link-launch-check.ps1 must name ${option} exactly once (found ${option_hits})")
    endif()
endforeach()
ctr_forbid("tools/arcade-link-launch-check.ps1" "${checker}" "--exit-after-frame")
ctr_require_order("CMakeLists.txt (arcade_link_launch)" "${live_block}" "-TimeoutSeconds 780)" "TIMEOUT 900")
