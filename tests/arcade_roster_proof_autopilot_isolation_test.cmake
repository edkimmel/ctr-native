# Structural isolation for the LR-S2 (b) autopilot finish spike
# (docs/LOCKSTEP_RACE_MILESTONE.md section 6 LR-S2, LR-16):
#  1. the steering decision in platform/native_arcade_link_autopilot.{c,h}
#     stays pure: the module's include allow-list is unchanged (no <math.h>,
#     no game header), it names no game state (gGT, sdata, GameTracker,
#     Driver, Level, restart points, NavHeader), no topology-lease, lockstep,
#     replay, checkpoint, or canonical-state token, and its library is C17
#     with extensions off and links nothing;
#  2. the steering entries (NativeArcadeLinkAutopilot_Angle, _Steer, _Passed)
#     are called, outside the module and its unit test, only by the roster
#     proof hook, game/MAIN/MainArcadeRosterProof.c, and (since LR-S10 part 2)
#     by the race caller, game/MAIN/MainArcadeRaceLaunch.c, which calls
#     _Passed and _Steer once each, only inside its #if defined(CTR_INTERNAL)
#     part (its restart point read is pinned by
#     main_arcade_link_hook_isolation_test.cmake);
#  3. in the proof hook (one CTR_NATIVE && CTR_INTERNAL file), the restart
#     point read (ptr_restart_points) happens only in the autopilot's helpers
#     and step, never in the report, digest, tick-line, or pad-install code;
#     the step runs in EndFrame only when the option is on, after the tick
#     line was kept and before the race tick advances; the autopilot's state
#     (s_mainArcadeRosterProofAutopilot) is named only in its declaration,
#     the helpers and step, and the gated branch of the pad install; the
#     proof hook never names NavHeader;
#  4. the platform proof module (the report and the tick lines) never names
#     the restart points or the steering entries;
#  5. the determinism check (A..K) never passes the autopilot option.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade roster proof autopilot isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
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

function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

function(ctr_count source pattern out_var)
    string(REGEX MATCHALL "${pattern}" hits "${source}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# The text from opener through the end of the block that follows it.
function(ctr_block_text relative_path source opener out_var)
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
                math(EXPR block_length "${position} + 1")
                string(SUBSTRING "${tail}" 0 ${block_length} block)
                set(${out_var} "${block}" PARENT_SCOPE)
                return()
            endif()
        endif()
        math(EXPR position "${position} + 1")
    endwhile()
    message(FATAL_ERROR "${prefix}: unbalanced block after '${opener}' in ${relative_path}")
endfunction()

# 1. The pure steering decision.
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
    foreach(term IN ITEMS gGT sdata GameTracker "struct Driver" "struct Level" level1 ptr_restart_points CheckpointNode
            NavHeader navHeader nav_header TopologyLease topology_lease Topology topology Lease lease
            NativeLockstep native_lockstep Lockstep lockstep NativeReplay native_replay Replay replay
            Checkpoint checkpoint NativeCanonical native_canonical MainCanonical
            "#include <math.h>" "#include <common.h>" "atan2" "sqrt" "float" "double"
            malloc calloc realloc "free(" alloca)
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
# The steering entries exist, and no steering body does I/O or keeps state.
foreach(opener IN ITEMS "int32_t NativeArcadeLinkAutopilot_Angle(int32_t dx, int32_t dz)"
        "uint32_t NativeArcadeLinkAutopilot_Steer(const struct NativeArcadeLinkAutopilotSteerFacts *facts)"
        "int NativeArcadeLinkAutopilot_Passed(const struct NativeArcadeLinkAutopilotPassFacts *facts)")
    ctr_block_text("${module_source}" "${module_code}" "${opener}" body)
    foreach(term IN ITEMS "static " fopen fwrite fclose printf "->done" "->result" "NativeArcadeLinkAutopilot_Fail")
        ctr_forbid("${module_source} (${opener})" "${body}" "${term}")
    endforeach()
    string(FIND "${header_code}" "${opener};" declared_at)
    if(declared_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${module_header} must declare '${opener}'")
    endif()
endforeach()
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

# 2. Who calls the steering entries.
set(proof_source "game/MAIN/MainArcadeRosterProof.c")
set(race_caller_source "game/MAIN/MainArcadeRaceLaunch.c")
set(race_caller_steers 0)
file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(scanned 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path STREQUAL module_source OR relative_path STREQUAL module_header OR relative_path STREQUAL proof_source)
        continue()
    endif()
    file(READ "${path}" source)
    string(REGEX MATCH "NativeArcadeLinkAutopilot_(Angle|Steer|Passed)[ \t]*\\(" raw_hit "${source}")
    if("${raw_hit}" STREQUAL "")
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    if(relative_path STREQUAL race_caller_source)
        # LR-S10 part 2: the race caller steers its internal-build sample;
        # every steering call must sit inside its CTR_INTERNAL part.
        string(REPLACE "\r\n" "\n" code "${code}")
        string(FIND "${code}" "#if defined(CTR_INTERNAL)" internal_at)
        string(FIND "${code}" "#else" internal_else_at)
        if(internal_at EQUAL -1 OR internal_else_at LESS internal_at)
            message(FATAL_ERROR "${prefix}: ${race_caller_source} calls the autopilot steering without a #if defined(CTR_INTERNAL) ... #else part")
        endif()
        math(EXPR internal_length "${internal_else_at} - ${internal_at}")
        string(SUBSTRING "${code}" 0 ${internal_at} before_internal)
        string(SUBSTRING "${code}" ${internal_else_at} -1 after_internal)
        string(REGEX MATCH "NativeArcadeLinkAutopilot_(Angle|Steer|Passed)[ \t]*\\(" outside_hit "${before_internal}${after_internal}")
        if(NOT "${outside_hit}" STREQUAL "")
            message(FATAL_ERROR "${prefix}: ${race_caller_source} calls the autopilot steering (${outside_hit}) outside its CTR_INTERNAL part")
        endif()
        string(SUBSTRING "${code}" ${internal_at} ${internal_length} internal_part)
        string(REGEX MATCHALL "NativeArcadeLinkAutopilot_(Angle|Steer|Passed)[ \t]*\\(" internal_hits "${internal_part}")
        if(NOT "${internal_hits}" STREQUAL "NativeArcadeLinkAutopilot_Passed(;NativeArcadeLinkAutopilot_Steer(")
            message(FATAL_ERROR "${prefix}: ${race_caller_source} must call NativeArcadeLinkAutopilot_Passed and then NativeArcadeLinkAutopilot_Steer once each (found '${internal_hits}')")
        endif()
        set(race_caller_steers 1)
        continue()
    endif()
    string(REGEX MATCH "NativeArcadeLinkAutopilot_(Angle|Steer|Passed)[ \t]*\\(" code_hit "${code}")
    if(NOT "${code_hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} calls the autopilot steering (${code_hit}); only ${proof_source} and ${race_caller_source} may")
    endif()
endforeach()
if(NOT race_caller_steers)
    message(FATAL_ERROR "${prefix}: ${race_caller_source} must steer its internal-build sample (LR-S10 part 2)")
endif()
if(scanned LESS 300)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()

# 3. The proof hook.
ctr_read_source("${proof_source}" proof)
ctr_strip_comments("${proof}" proof_code)
if(NOT proof MATCHES "^#if defined\\(CTR_NATIVE\\) && defined\\(CTR_INTERNAL\\)\n" OR NOT proof MATCHES "#endif[ \t\r\n]*$")
    message(FATAL_ERROR "${prefix}: ${proof_source} must be one #if defined(CTR_NATIVE) && defined(CTR_INTERNAL) block")
endif()
foreach(term IN ITEMS NavHeader navHeader nav_header)
    ctr_forbid("${proof_source}" "${proof}" "${term}")
endforeach()
ctr_require_literal("${proof_source}" "${proof_code}" "#include \"platform/native_arcade_link_autopilot.h\"")
# The restart point read only in the autopilot's helpers and step.
set(autopilot_openers
    "static uint32_t MainArcadeRosterProof_AutopilotNext(const struct Level *level, uint32_t count, uint32_t index)"
    "static uint32_t MainArcadeRosterProof_AutopilotNearest(const struct Level *level, uint32_t count, int32_t x, int32_t y, int32_t z)"
    "static uint32_t MainArcadeRosterProof_AutopilotSteer(const struct Level *level, uint32_t count, const struct Driver *driver,\n\tuint32_t player)"
    "static void MainArcadeRosterProof_AutopilotStep(const struct GameTracker *gGT, uint32_t raceTick)")
set(outside "${proof_code}")
set(autopilot_hits 0)
foreach(opener IN LISTS autopilot_openers)
    ctr_block_text("${proof_source}" "${proof_code}" "${opener}" body)
    ctr_count("${body}" "ptr_restart_points" body_hits)
    math(EXPR autopilot_hits "${autopilot_hits} + ${body_hits}")
    # Nothing the autopilot reads may reach a digest, a tick line, or the report.
    foreach(term IN ITEMS RecordTick "line." "report" Digest domainDigests NativeSha256 NativeCodec Platform_InputInstallPadSnapshots
            "state->" "s_mainArcadeRosterProof." "s_mainArcadeRosterProofDrivers" Lease lease Topology topology)
        ctr_forbid("${proof_source} (${opener})" "${body}" "${term}")
    endforeach()
    string(REPLACE "${body}" "" outside "${outside}")
endforeach()
if(autopilot_hits EQUAL 0)
    message(FATAL_ERROR "${prefix}: ${proof_source} no longer reads ptr_restart_points in the autopilot; the scan is broken")
endif()
ctr_forbid("${proof_source} (outside the autopilot helpers)" "${outside}" "ptr_restart_points")
ctr_forbid("${proof_source} (outside the autopilot helpers)" "${outside}" "level1")
ctr_forbid("${proof_source} (outside the autopilot helpers)" "${outside}" "NativeArcadeLinkAutopilot_Steer")
ctr_forbid("${proof_source} (outside the autopilot helpers)" "${outside}" "NativeArcadeLinkAutopilot_Passed")
# The step: once, in EndFrame, gated by the option, after the tick line.
ctr_count("${proof_code}" "MainArcadeRosterProof_AutopilotStep\\(" step_hits)
if(NOT step_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${proof_source} must define MainArcadeRosterProof_AutopilotStep and call it once (found ${step_hits})")
endif()
ctr_block_text("${proof_source}" "${proof_code}"
    "void MainArcadeRosterProof_EndFrame(struct GameTracker *gGT, const struct NativeCanonicalStateV1 *frameState)" end_frame)
ctr_require_order("${proof_source} (MainArcadeRosterProof_EndFrame)" "${end_frame}"
    "if (!NativeArcadeRosterProof_RecordTick(&line))"
    "if (NativeArcadeRosterProof_Autopilot() != 0u)\n\t{\n\t\tMainArcadeRosterProof_AutopilotStep(gGT, state->raceTick);\n\t}"
    "state->raceTick++;")
# The pads: the autopilot's buttons replace the pattern only with the option
# on and only on logged race ticks.
ctr_block_text("${proof_source}" "${proof_code}" "static int MainArcadeRosterProof_InstallPads(uint32_t raceTick)" install)
ctr_require_order("${proof_source} (MainArcadeRosterProof_InstallPads)" "${install}"
    "NativeArcadeRosterProof_ScriptedPads(NativeArcadeRosterProof_Profile(), raceTick, pads);"
    "if ((raceTick != NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) && (NativeArcadeRosterProof_Autopilot() != 0u))"
    "s_mainArcadeRosterProofAutopilot.held[pad]"
    "Platform_InputInstallPadSnapshots(snapshots, PLATFORM_INPUT_PAD_COUNT)")
# The autopilot's state (it holds target[], restart point indices from level
# topology) is named only in its declaration, the autopilot's helpers and
# step, and the gated branch of the pad install, where only held[pad] is read.
# So it can never reach the drivers digest, a tick line, or the report.
set(autopilot_state "s_mainArcadeRosterProofAutopilot")
set(autopilot_declaration "static struct MainArcadeRosterProofAutopilotState ${autopilot_state};")
string(FIND "${proof_code}" "${autopilot_declaration}" declaration_first)
string(FIND "${proof_code}" "${autopilot_declaration}" declaration_last REVERSE)
if(declaration_first EQUAL -1 OR NOT declaration_first EQUAL declaration_last)
    message(FATAL_ERROR "${prefix}: ${proof_source} must declare '${autopilot_declaration}' once")
endif()
ctr_block_text("${proof_source}" "${install}"
    "if ((raceTick != NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE) && (NativeArcadeRosterProof_Autopilot() != 0u))" install_branch)
ctr_count("${install_branch}" "${autopilot_state}" branch_state_hits)
ctr_count("${install_branch}" "${autopilot_state}\\.held\\[pad\\]" branch_held_hits)
if(NOT branch_state_hits EQUAL branch_held_hits)
    message(FATAL_ERROR "${prefix}: ${proof_source} (MainArcadeRosterProof_InstallPads) may read only ${autopilot_state}.held[pad]")
endif()
set(state_outside "${outside}")
string(REPLACE "${autopilot_declaration}" "" state_outside "${state_outside}")
string(REPLACE "${install_branch}" "" state_outside "${state_outside}")
ctr_forbid("${proof_source} (outside the autopilot declaration, helpers, and gated pad install)" "${state_outside}" "${autopilot_state}")

# 4. The platform proof module: the report and the tick lines.
foreach(relative_path IN ITEMS "platform/native_arcade_roster_proof.c" "include/platform/native_arcade_roster_proof.h")
    ctr_read_source("${relative_path}" platform_source)
    ctr_strip_comments("${platform_source}" platform_code)
    foreach(term IN ITEMS ptr_restart_points level1 NativeArcadeLinkAutopilot native_arcade_link_autopilot NavHeader)
        ctr_forbid("${relative_path}" "${platform_code}" "${term}")
    endforeach()
endforeach()

# 5. Runs A..K never use the autopilot.
ctr_read_source("tools/arcade-roster-proof-check.ps1" checker)
ctr_forbid("tools/arcade-roster-proof-check.ps1" "${checker}" "--arcade-roster-proof-autopilot")
