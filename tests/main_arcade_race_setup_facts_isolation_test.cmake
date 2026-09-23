# Structural isolation for the race setup facts builder (game/MAIN/
# MainArcadeRaceSetupFacts, docs/ROSTER_MILESTONE.md section 3.2, R-5a):
#  1. the module is pure: no game global, load, heap, stdio, clock, lease or
#     topology (lease verbs included), checkpoint, replay, lockstep, or
#     match-select token in its code; the only canonical names it may use are
#     the drivers roster input it consumes (no canonical-state token), and it
#     keeps no mutable static state (every static is a function or a const
#     object);
#  2. its includes are limited to the allowlist (no retail header);
#  3. ctr_native_arcade_race_setup_facts builds only the module's .c, the .c
#     is named by exactly one CMake target, and the library links exactly
#     ctr_native_arcade_bot_setup (which brings the roster);
#  4. the target is C17 with extensions off;
#  5. the retail field widths the snapshot assumes still hold
#     (include/namespace_Main.h GameTracker, include/regionsEXE.h,
#     include/namespace_Vehicle.h Driver);
#  6. it is confined: no game/, platform/, include/platform/, or main.c
#     file other than the module and the live adapter
#     (game/MAIN/MainArcadeRaceSetup.{c,h}, R-5b) and its decision core
#     (game/MAIN/MainArcadeRaceSetupCore.{c,h}, R-5c) names
#     MainArcadeRaceSetupFacts, it is not in game/game_unity.h, and only its
#     unit test, the core library, and ctr_native (for the adapter) link it.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "race setup facts isolation")
set(module_header "game/MAIN/MainArcadeRaceSetupFacts.h")
set(module_source "game/MAIN/MainArcadeRaceSetupFacts.c")
set(target ctr_native_arcade_race_setup_facts)

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
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

# Requires a whole declaration "<type> <name>;" at the start of a line (after
# indentation), so "u8 driverID;" does not match "su8 driverID;" and
# "int arcadeDifficulty;" does not match "unsigned int arcadeDifficulty;".
# name_regex is a regex (brackets escaped by the caller).
function(ctr_require_field relative_path source type name_regex)
    string(REGEX MATCH "(^|[\r\n])[ \t]*${type}[ \t]+${name_regex}[ \t]*;" found "${source}")
    if(found STREQUAL "")
        message(FATAL_ERROR "${prefix}: declaration '${type} ${name_regex};' missing from ${relative_path}")
    endif()
endfunction()

# Returns the body of "struct <name>" (up to its closing "};") in source.
function(ctr_struct_body relative_path source name out_var)
    string(FIND "${source}" "struct ${name}\n{" at)
    if(at EQUAL -1)
        string(FIND "${source}" "struct ${name}\r\n{" at)
    endif()
    if(at EQUAL -1)
        message(FATAL_ERROR "${prefix}: struct ${name} not found in ${relative_path}")
    endif()
    string(SUBSTRING "${source}" ${at} -1 tail)
    string(FIND "${tail}" "\n};" end)
    string(SUBSTRING "${tail}" 0 ${end} body)
    set(${out_var} "${body}" PARENT_SCOPE)
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

# 1. Purity, on the code with comments removed (the header's audit comment
#    cites retail files and globals by name).
set(pure_tokens
    gGT sdata "data." GameTracker MEMPACK LOAD_
    Lease lease Topology topology Acquire Activate Publish Retire
    MainCanonical CanonicalState canonical_state
    Checkpoint checkpoint Replay replay Lockstep lockstep LOCKSTEP
    NativeMatchSelect native_match_select
    malloc calloc realloc "free(" alloca
    stdio printf fopen fwrite "puts("
    clock "time(" "time.h" QueryPerformance SDL_)
# The only canonical names allowed: the drivers roster input and its kinds.
set(canonical_allowed
    NativeCanonicalDriversRosterInput NativeCanonicalDriversRosterSlot
    NATIVE_CANONICAL_DRIVER_KIND_BOT NATIVE_CANONICAL_DRIVER_KIND_HUMAN
    native_canonical_drivers_roster)
set(canonical_seen 0)
foreach(relative_path IN ITEMS "${module_header}" "${module_source}")
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${source}" code)
    foreach(term IN LISTS pure_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
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

    # 2. Include allowlist.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${code}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"MAIN/MainArcadeRaceSetupFacts\\.h\"|\"MAIN/MainArcadeBotSetup\\.h\"|\"MAIN/MainArcadeRoster\\.h\"|\"platform/native_canonical_drivers_roster\\.h\"|\"platform/native_match_config\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()
if(canonical_seen EQUAL 0)
    message(FATAL_ERROR "${prefix}: found no roster input name; the canonical scan is broken")
endif()

# 3. The library builds only the module and links exactly the bot setup, in
#    one target_link_libraries call; the .c is named once in CMakeLists.txt.
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
string(REGEX MATCHALL "MainArcadeRaceSetupFacts\\.c" source_mentions "${cmake}")
list(LENGTH source_mentions source_mention_count)
if(NOT source_mention_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${module_source} must be named by exactly one target in CMakeLists.txt (found ${source_mention_count} mentions)")
endif()
file(GLOB_RECURSE other_cmake_files "${repo}/cmake/*" "${repo}/game/*.txt" "${repo}/platform/*.txt")
foreach(path IN LISTS other_cmake_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" other_cmake)
    ctr_forbid("${relative_path}" "${other_cmake}" "MainArcadeRaceSetupFacts.c")
endforeach()
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
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_bot_setup")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_arcade_bot_setup (found '${link_items}')")
endif()

# 4. C17, no extensions, in order.
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

# 5. Retail field widths behind struct MainArcadeRaceSetupLiveSnapshot.
ctr_read_source("include/namespace_Main.h" retail_main)
ctr_strip_comments("${retail_main}" retail_main_code)
ctr_struct_body("include/namespace_Main.h" "${retail_main_code}" GameTracker tracker_body)
ctr_require_field("include/namespace_Main.h (struct GameTracker)" "${tracker_body}" "u8" "numPlyrCurrGame")
ctr_require_field("include/namespace_Main.h (struct GameTracker)" "${tracker_body}" "u8" "numBotsNextGame")
ctr_require_field("include/namespace_Main.h (struct GameTracker)" "${tracker_body}" "int" "arcadeDifficulty")
ctr_read_source("include/regionsEXE.h" regions)
ctr_require_field("include/regionsEXE.h" "${regions}" "s16" "characterIDs\\[8\\]")
ctr_require_field("include/regionsEXE.h" "${regions}" "char" "kartSpawnOrderArray\\[0x8\\]")
ctr_require_field("include/regionsEXE.h" "${regions}" "char" "driver_pathIndexIDs\\[8\\]")
ctr_require_field("include/regionsEXE.h" "${regions}" "u8" "accelerateOrder\\[8\\]")
ctr_read_source("include/namespace_Vehicle.h" vehicle)
ctr_strip_comments("${vehicle}" vehicle_code)
ctr_struct_body("include/namespace_Vehicle.h" "${vehicle_code}" Driver driver_body)
ctr_require_field("include/namespace_Vehicle.h (struct Driver)" "${driver_body}" "u8" "driverID")
string(REGEX MATCH "ACTION_BOT[ \t]*=[ \t]*0x100000[ \t]*," action_bot "${vehicle_code}")
if(action_bot STREQUAL "")
    message(FATAL_ERROR "${prefix}: ACTION_BOT = 0x100000 not found in include/namespace_Vehicle.h")
endif()
# The anchored check itself rejects a widened or re-signed declaration.
string(REGEX MATCH "(^|[\r\n])[ \t]*int[ \t]+arcadeDifficulty[ \t]*;" probe_found "\n\tunsigned int arcadeDifficulty;\n")
if(NOT probe_found STREQUAL "")
    message(FATAL_ERROR "${prefix}: the field-width check accepts 'unsigned int arcadeDifficulty;' for 'int arcadeDifficulty;'")
endif()
ctr_read_source("${module_header}" header)
ctr_struct_body("${module_header}" "${header}" MainArcadeRaceSetupLiveSnapshot snapshot_body)
foreach(field IN ITEMS "uint8_t numPlyrCurrGame" "uint8_t numBotsNextGame"
        "uint8_t driverPresent\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "uint8_t driverID\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "uint8_t driverIsBot\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "int16_t characterIDs\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "uint8_t kartSpawnOrderArray\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "int8_t driver_pathIndexIDs\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "uint8_t accelerateOrder\\[MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT\\]"
        "int32_t arcadeDifficulty")
    string(REPLACE " " ";" field_parts "${field}")
    list(GET field_parts 0 field_type)
    list(GET field_parts 1 field_name)
    ctr_require_field("${module_header} (struct MainArcadeRaceSetupLiveSnapshot)" "${snapshot_body}" "${field_type}" "${field_name}")
endforeach()
string(FIND "${header}" "#define MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT 8u" slot_count_at)
if(slot_count_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${module_header} must define MAIN_ARCADE_RACE_SETUP_FACTS_SLOT_COUNT 8u")
endif()

# 6. Confined: only the module, the R-5b live adapter, and its R-5c core name
# it, and only its
# unit test, the core, and ctr_native link it.
set(facts_adapter_paths "game/MAIN/MainArcadeRaceSetup.c" "game/MAIN/MainArcadeRaceSetup.h"
    "game/MAIN/MainArcadeRaceSetupCore.c" "game/MAIN/MainArcadeRaceSetupCore.h")
file(GLOB_RECURSE scan_files "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/platform/*.h" "${repo}/main.c")
set(scanned 0)
set(scanned_platform 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL module_header OR relative_path STREQUAL module_source)
        continue()
    endif()
    list(FIND facts_adapter_paths "${relative_path}" adapter_at)
    if(NOT adapter_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    ctr_forbid("${relative_path}" "${scanned_source}" "MainArcadeRaceSetupFacts")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path MATCHES "^platform/")
        math(EXPR scanned_platform "${scanned_platform} + 1")
    endif()
endforeach()
if(scanned LESS 100 OR scanned_platform LESS 20)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} game/ and platform/ files (${scanned_platform} in platform/); the scan is broken")
endif()
ctr_read_source("game/game_unity.h" unity)
ctr_forbid("game/game_unity.h" "${unity}" "MainArcadeRaceSetupFacts")
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*[A-Za-z0-9_]+[^)]*\\)" all_link_calls "${cmake}")
set(linked_by_test 0)
foreach(link_call IN LISTS all_link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*([A-Za-z0-9_]+).*$" "\\1" linking_target "${link_call}")
    string(REGEX MATCH "[ \t\r\n]${target}[ \t\r\n)]" names_module "${link_call}")
    if(names_module)
        if(linking_target STREQUAL "main_arcade_race_setup_facts_test")
            set(linked_by_test 1)
        elseif(NOT linking_target STREQUAL "ctr_native" AND NOT linking_target STREQUAL "ctr_native_arcade_race_setup_core")
            message(FATAL_ERROR "${prefix}: ${linking_target} links ${target}; only main_arcade_race_setup_facts_test, ctr_native_arcade_race_setup_core, and ctr_native may")
        endif()
    endif()
endforeach()
if(NOT linked_by_test)
    message(FATAL_ERROR "${prefix}: main_arcade_race_setup_facts_test must link ${target}")
endif()
