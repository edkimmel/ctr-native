# Structural isolation for the race setup decision core (game/MAIN/
# MainArcadeRaceSetupCore, docs/ROSTER_MILESTONE.md section 3.2, R-5c):
#  1. the module is pure: no game global, load, heap, stdio, clock (excepted
#     only: the LR-8 pins-struct member clockFrameStart, a retail field
#     mirror, in its declaration and its pins.clockFrameStart uses), platform,
#     lease or topology (lease verbs included), checkpoint, replay, lockstep,
#     or match-select token in its code; the only canonical names it may use
#     are the drivers roster input it forwards (no canonical-state token); and
#     it keeps no mutable static state (every static is a function or a const
#     object);
#  2. its includes are limited to the allowlist (no retail header);
#  3. it has no levelID write target: the load request is the only way the
#     plan's level reaches the retail state;
#  4. ctr_native_arcade_race_setup_core builds only the module's .c, the .c is
#     named by exactly one CMake target, and the library links exactly the
#     plan and the facts libraries;
#  5. the target is C17 with extensions off;
#  6. it is linked, never unity-included: game/game_unity.h does not name it,
#     ctr_native links it, and only ctr_native and its unit test link it;
#  7. it is confined: no game/, platform/, include/platform/, or main.c file
#     other than the module and the live adapter
#     (game/MAIN/MainArcadeRaceSetup.{c,h}) names MainArcadeRaceSetupCore in
#     code (comments may cite it);
#  8. the adapter static-asserts every retail mirror the core compares against.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "race setup core isolation")
set(module_header "game/MAIN/MainArcadeRaceSetupCore.h")
set(module_source "game/MAIN/MainArcadeRaceSetupCore.c")
set(adapter_source "game/MAIN/MainArcadeRaceSetup.c")
set(adapter_header "game/MAIN/MainArcadeRaceSetup.h")
set(target ctr_native_arcade_race_setup_core)

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
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

# Sets out_var to the static declarations in code (comments stripped) that
# are mutable state: every static must be a function (an identifier directly
# followed by '(') or a const object that is not a pointer to mutable data.
function(ctr_mutable_statics code out_var)
    set(violations "")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])static[ \t\r\n][^;{=]*" heads "${code}")
    foreach(head IN LISTS heads)
        string(REGEX REPLACE "^[^s]*static" "static" head "${head}")
        string(REGEX REPLACE "[ \t\r\n]+" " " head "${head}")
        string(STRIP "${head}" head)
        if(head MATCHES "^static( inline)? [A-Za-z_][A-Za-z0-9_ *]*[ *]([A-Za-z_][A-Za-z0-9_]*) ?\\(" AND NOT head MATCHES "\\( ?\\*")
            continue()
        endif()
        if(head MATCHES "^static const " AND NOT head MATCHES "\\(")
            if(NOT head MATCHES "\\*" OR head MATCHES "\\* ?const ")
                continue()
            endif()
        endif()
        list(APPEND violations "${head}")
    endforeach()
    set(${out_var} "${violations}" PARENT_SCOPE)
endfunction()

# The mutable-static scan must itself work.
ctr_mutable_statics("static int counter;\nstatic uint8_t buffer[4] = {0};\nstatic const uint8_t *cursor;\nstatic int (*hook)(void);\nvoid F(void) { static int calls; }\n" probe_bad)
list(LENGTH probe_bad probe_bad_count)
ctr_mutable_statics("static int IsOk(const struct A *a)\n{\n}\nstatic const uint8_t k_table[2] = { 1, 2 };\nstatic const char *const k_name = \"x\";\n" probe_good)
list(LENGTH probe_good probe_good_count)
if(NOT probe_bad_count EQUAL 5 OR NOT probe_good_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: the mutable-static scan is broken (flagged ${probe_bad_count} of 5 bad, ${probe_good_count} of 0 good)")
endif()

# 1 and 2. Purity and includes, on the code with comments removed (the
# comments cite the retail fields the views mirror).
set(pure_tokens
    gGT sdata "data." GameTracker MEMPACK
    Lease lease LEASE Topology topology Acquire Activate Publish Retire
    MainCanonical CanonicalState canonical_state
    Checkpoint checkpoint Replay replay Lockstep lockstep LOCKSTEP
    NativeMatchSelect native_match_select
    Platform_ platform.h native_log Capture capture
    malloc calloc realloc "free(" alloca
    stdio printf fopen fwrite "puts(" FILE
    clock "time(" "time.h" QueryPerformance SDL_)
set(canonical_allowed
    NativeCanonicalDriversRosterInput native_canonical_drivers_roster)
set(canonical_seen 0)
foreach(relative_path IN ITEMS "${module_header}" "${module_source}")
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${source}" code)
    # The one allowed 'clock': clockFrameStart, the pins-struct member that
    # mirrors the retail field the setup pins (LR-8), exempted only in its
    # two uses: the struct MainArcadeRaceSetupPins member declaration
    # "int32_t clockFrameStart;" and the seeding step's "pins.clockFrameStart"
    # accesses. Every other 'clock' (clock(), clock_gettime, a clockFrameStart
    # reached any other way, ...) is still forbidden.
    string(REGEX REPLACE "(^|[^A-Za-z0-9_])int32_t[ \t]+clockFrameStart[ \t]*;" "\\1@PIN_MEMBER@;" pure_code "${code}")
    string(REGEX REPLACE "(^|[^A-Za-z0-9_])pins\\.clockFrameStart([^A-Za-z0-9_]|$)" "\\1@PIN_MEMBER@\\2" pure_code "${pure_code}")
    foreach(term IN LISTS pure_tokens)
        ctr_forbid("${relative_path}" "${pure_code}" "${term}")
    endforeach()
    # Retail load names start an identifier with LOAD_ (the failure code
    # MAIN_ARCADE_RACE_SETUP_FAILURE_LOAD_FIELDS_MISMATCH and its "LOAD_FIELDS_MISMATCH"
    # name do not).
    if(code MATCHES "(^|[^A-Za-z0-9_\"])LOAD_")
        message(FATAL_ERROR "${prefix}: a retail LOAD_ name is used in ${relative_path}")
    endif()
    string(REGEX MATCHALL "(NativeCanonical|NATIVE_CANONICAL|native_canonical|Canonical)[A-Za-z0-9_]*" canonical_names "${code}")
    foreach(name IN LISTS canonical_names)
        list(FIND canonical_allowed "${name}" allowed_at)
        if(allowed_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: canonical name '${name}' in ${relative_path} is not the drivers roster input")
        endif()
        math(EXPR canonical_seen "${canonical_seen} + 1")
    endforeach()
    ctr_mutable_statics("${code}" mutable_statics)
    if(NOT "${mutable_statics}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: mutable static state in ${relative_path}: '${mutable_statics}'")
    endif()
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${code}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"MAIN/MainArcadeRaceSetupCore\\.h\"|\"MAIN/MainArcadeBotSetup\\.h\"|\"MAIN/MainArcadeRaceSetupFacts\\.h\"|\"MAIN/MainArcadeRaceSetupPlan\\.h\"|\"MAIN/MainArcadeRoster\\.h\"|\"platform/native_arcade_bot_rules\\.h\"|\"platform/native_canonical_drivers_roster\\.h\"|\"platform/native_deterministic_rng\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_sha256\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()
if(canonical_seen EQUAL 0)
    message(FATAL_ERROR "${prefix}: found no roster input name; the canonical scan is broken")
endif()

# 3. No levelID write target.
ctr_read_source("${module_header}" header)
ctr_strip_comments("${header}" header_code)
string(FIND "${header_code}" "enum MainArcadeRaceSetupCoreTarget\n{" targets_at)
if(targets_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: enum MainArcadeRaceSetupCoreTarget not found in ${module_header}")
endif()
string(SUBSTRING "${header_code}" ${targets_at} -1 targets_tail)
string(FIND "${targets_tail}" "};" targets_end)
string(SUBSTRING "${targets_tail}" 0 ${targets_end} targets_body)
string(REGEX MATCHALL "MAIN_ARCADE_RACE_SETUP_CORE_TARGET_[A-Z0-9_]+" target_names "${targets_body}")
list(LENGTH target_names target_count)
if(target_count LESS 10)
    message(FATAL_ERROR "${prefix}: found only ${target_count} write targets; the scan is broken")
endif()
foreach(name IN LISTS target_names)
    if(name MATCHES "LEVEL")
        message(FATAL_ERROR "${prefix}: write target ${name} would write the level; only REQUEST_LOAD may carry it")
    endif()
endforeach()
list(FIND target_names "MAIN_ARCADE_RACE_SETUP_CORE_TARGET_REQUEST_LOAD" request_load_at)
if(request_load_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: the REQUEST_LOAD target is missing")
endif()

# 4 and 5. The library: exactly the module's .c, linking the plan and the facts, C17.
ctr_read_source("CMakeLists.txt" cmake)
string(REGEX MATCHALL "add_library\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" add_calls "${cmake}")
list(LENGTH add_calls add_call_count)
if(NOT add_call_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one add_library(${target} ...), found ${add_call_count}")
endif()
list(GET add_calls 0 add_call)
string(REGEX REPLACE "[ \t\r\n]+" " " add_call "${add_call}")
if(NOT add_call STREQUAL "add_library(${target} STATIC ${module_source})")
    message(FATAL_ERROR "${prefix}: ${target} must build exactly ${module_source} (found '${add_call}')")
endif()
string(REGEX MATCHALL "MainArcadeRaceSetupCore\\.c" source_mentions "${cmake}")
list(LENGTH source_mentions source_mention_count)
if(NOT source_mention_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${module_source} must be named by exactly one target in CMakeLists.txt (found ${source_mention_count} mentions)")
endif()
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
list(SORT link_items)
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_race_setup_facts;ctr_native_arcade_race_setup_plan")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly the plan and the facts libraries (found '${link_items}')")
endif()
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(properties_at EQUAL -1 OR standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "${prefix}: ${target} C17/no-extensions properties are out of order")
endif()

# 6. Linked, never unity-included; only ctr_native and the unit test link it.
ctr_read_source("game/game_unity.h" unity)
ctr_forbid("game/game_unity.h" "${unity}" "MainArcadeRaceSetupCore")
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*[A-Za-z0-9_]+[^)]*\\)" all_link_calls "${cmake}")
set(linked_by_test 0)
set(linked_by_native 0)
foreach(link_call IN LISTS all_link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*([A-Za-z0-9_]+).*$" "\\1" linking_target "${link_call}")
    string(REGEX MATCH "[ \t\r\n]${target}[ \t\r\n)]" names_module "${link_call}")
    if(names_module)
        if(linking_target STREQUAL "main_arcade_race_setup_core_test")
            set(linked_by_test 1)
        elseif(linking_target STREQUAL "ctr_native")
            set(linked_by_native 1)
        else()
            message(FATAL_ERROR "${prefix}: ${linking_target} links ${target}; only main_arcade_race_setup_core_test and ctr_native may")
        endif()
    endif()
endforeach()
if(NOT linked_by_test OR NOT linked_by_native)
    message(FATAL_ERROR "${prefix}: both ctr_native and main_arcade_race_setup_core_test must link ${target} (native ${linked_by_native}, test ${linked_by_test})")
endif()

# 7. Confined to the module and the live adapter.
set(core_users "${module_header}" "${module_source}" "${adapter_source}" "${adapter_header}")
file(GLOB_RECURSE scan_files "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/platform/*.h" "${repo}/main.c")
set(scanned 0)
set(scanned_platform 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    list(FIND core_users "${relative_path}" user_at)
    if(NOT user_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    # Code only: the plan and facts headers cite the core as their caller.
    ctr_strip_comments("${scanned_source}" scanned_code)
    ctr_forbid("${relative_path}" "${scanned_code}" "MainArcadeRaceSetupCore")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path MATCHES "^platform/")
        math(EXPR scanned_platform "${scanned_platform} + 1")
    endif()
endforeach()
if(scanned LESS 100 OR scanned_platform LESS 20)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} game/ and platform/ files (${scanned_platform} in platform/); the scan is broken")
endif()

# 8. The adapter pins every retail mirror the core compares against.
ctr_read_source("${adapter_source}" adapter)
foreach(assertion IN ITEMS
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_STAGE_IDLE == LOAD_IDLE,"
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_CORE_MAIN_MENU_LEVEL == MAIN_MENU_LEVEL,"
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_LOADING == (uint32_t)LOADING,"
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL == (uint32_t)PAUSE_ALL,"
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE == (uint32_t)ARCADE_MODE,"
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK == (uint32_t)GAME_MODE_VIBRATION_MASK,"
        "_Static_assert(MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL == (uint32_t)CHEAT_ALL,")
    ctr_require("${adapter_source}" "${adapter}" "${assertion}")
endforeach()
