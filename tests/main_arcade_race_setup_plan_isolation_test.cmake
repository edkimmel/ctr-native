# Structural isolation for the race setup pure core (game/MAIN/
# MainArcadeRaceSetupPlan, docs/ROSTER_MILESTONE.md section 3.2, R-4):
#  1. the module is pure: no game global, load, heap, stdio, clock, lease or
#     topology (lease verbs included), checkpoint, replay, canonical-state,
#     lockstep, or match-select token in its code, and no mutable static
#     state (every static is a function or a const object);
#  2. its includes are limited to the allowlist (no retail header);
#  3. ctr_native_arcade_race_setup_plan builds only the module's .c, the .c is
#     named by exactly one CMake target, and the library links exactly
#     ctr_native_arcade_bot_rules and ctr_native_match_config;
#  4. the target is C17 with extensions off;
#  5. the mirrored GameMode1, ADVENTURE_BOSS, and GameMode2 values equal
#     include/namespace_Main.h, every retail name is mirrored, and every
#     mirror names a retail value; the AI set mirrors (RS-22) equal
#     include/platform/native_match_select_rules.h, and the v2 encoding
#     (RS-21) is the declared one with its size static-asserted;
#  6. the retail field widths the mirror struct assumes still hold
#     (include/namespace_Main.h GameTracker, include/regionsEXE.h);
#  7. it is confined: no game/ or platform/ file other than the module
#     and the live adapter and its decision core
#     (game/MAIN/MainArcadeRaceSetup{,Core}.{c,h},
#     R-5b/R-5c) names MainArcadeRaceSetupPlan, it is not in game/game_unity.h,
#     and only its unit test, the roster proof unit test, the core library,
#     and ctr_native (for
#     the adapter) link it.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(module_header "game/MAIN/MainArcadeRaceSetupPlan.h")
set(module_source "game/MAIN/MainArcadeRaceSetupPlan.c")
set(target ctr_native_arcade_race_setup_plan)

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "race setup plan isolation: missing source ${relative_path}")
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
        message(FATAL_ERROR "race setup plan isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "race setup plan isolation: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

# Requires a whole declaration "<type> <name>;" at the start of a line (after
# indentation), so "int gameMode1;" does not match "unsigned int gameMode1;".
# name_regex is a regex (brackets escaped by the caller).
function(ctr_require_field relative_path source type name_regex)
    string(REGEX MATCH "(^|[\r\n])[ \t]*${type}[ \t]+${name_regex}[ \t]*;" found "${source}")
    if(found STREQUAL "")
        message(FATAL_ERROR "race setup plan isolation: declaration '${type} ${name_regex};' missing from ${relative_path}")
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
    message(FATAL_ERROR "race setup plan isolation: the mutable-static scan is broken (flagged ${probe_bad_count} of 5 bad, ${probe_good_count} of 0 good: '${probe_bad}' '${probe_good}')")
endif()

# 1. Purity, on the code with comments removed (the header's audit comment
#    cites retail files and globals by name).
set(pure_tokens
    gGT sdata "data." GameTracker MEMPACK LOAD_
    Lease lease Topology topology Acquire Activate Publish Retire
    NativeCanonical CanonicalState MainCanonical
    Checkpoint checkpoint Replay replay Lockstep lockstep LOCKSTEP
    NativeMatchSelect native_match_select
    malloc calloc realloc "free(" alloca
    stdio printf fopen fwrite "puts("
    clock "time(" "time.h" QueryPerformance SDL_)
foreach(relative_path IN ITEMS "${module_header}" "${module_source}")
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${source}" code)
    foreach(term IN LISTS pure_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
    ctr_mutable_statics("${code}" mutable_statics)
    if(NOT "${mutable_statics}" STREQUAL "")
        message(FATAL_ERROR "race setup plan isolation: mutable static state in ${relative_path}: '${mutable_statics}'")
    endif()

    # 2. Include allowlist.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${code}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "race setup plan isolation: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"MAIN/MainArcadeRaceSetupPlan\\.h\"|\"platform/native_arcade_bot_rules\\.h\"|\"platform/native_canonical_codec\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_sha256\\.h\")[ \t]*$")
            message(FATAL_ERROR "race setup plan isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 3. The library builds only the module and links exactly the bot rules and
#    the match config, in one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
string(REGEX MATCHALL "add_library\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" add_calls "${cmake}")
list(LENGTH add_calls add_call_count)
if(NOT add_call_count EQUAL 1)
    message(FATAL_ERROR "race setup plan isolation: expected exactly one add_library(${target} ...), found ${add_call_count}")
endif()
list(GET add_calls 0 add_call)
string(REGEX REPLACE "[ \t\r\n]+" " " add_call "${add_call}")
if(NOT add_call STREQUAL "add_library(${target} STATIC ${module_source})")
    message(FATAL_ERROR "race setup plan isolation: ${target} must build exactly ${module_source} (found '${add_call}')")
endif()
# The .c is named once in CMakeLists.txt (that add_library), so no other
# target compiles it, and no other CMake file names it.
string(REGEX MATCHALL "MainArcadeRaceSetupPlan\\.c" source_mentions "${cmake}")
list(LENGTH source_mentions source_mention_count)
if(NOT source_mention_count EQUAL 1)
    message(FATAL_ERROR "race setup plan isolation: ${module_source} must be named by exactly one target in CMakeLists.txt (found ${source_mention_count} mentions)")
endif()
file(GLOB_RECURSE cmake_files "${repo}/cmake/*" "${repo}/game/*.txt" "${repo}/platform/*.txt")
foreach(path IN LISTS cmake_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" other_cmake)
    ctr_forbid("${relative_path}" "${other_cmake}" "MainArcadeRaceSetupPlan.c")
endforeach()
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "race setup plan isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
list(SORT link_items)
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_bot_rules;ctr_native_match_config")
    message(FATAL_ERROR "race setup plan isolation: ${target} must link exactly ctr_native_arcade_bot_rules and ctr_native_match_config (found '${link_items}')")
endif()

# 4. C17, no extensions, in order.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(properties_at EQUAL -1 OR standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "race setup plan isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "race setup plan isolation: ${target} C17/no-extensions properties are out of order")
endif()

# 5. The mirrors equal include/namespace_Main.h.
ctr_read_source("include/namespace_Main.h" retail_main)
ctr_strip_comments("${retail_main}" retail_main_code)
ctr_read_source("${module_header}" header)

# Parses "NAME = value," entries of the enum named enum_name into
# retail_<prefix>_<NAME> (decimal) and appends each NAME to <prefix>_names.
# A value may be a literal or an OR of names of the same enum.
function(ctr_parse_enum enum_name prefix)
    string(FIND "${retail_main_code}" "enum ${enum_name}\n{" enum_at)
    if(enum_at EQUAL -1)
        string(FIND "${retail_main_code}" "enum ${enum_name}\r\n{" enum_at)
    endif()
    if(enum_at EQUAL -1)
        message(FATAL_ERROR "race setup plan isolation: enum ${enum_name} not found in include/namespace_Main.h")
    endif()
    string(SUBSTRING "${retail_main_code}" ${enum_at} -1 enum_tail)
    string(FIND "${enum_tail}" "};" enum_end)
    string(SUBSTRING "${enum_tail}" 0 ${enum_end} enum_body)
    string(REGEX MATCHALL "[A-Za-z0-9_]+[ \t]*=[ \t]*[^,]+," entries "${enum_body}")
    set(names "")
    foreach(entry IN LISTS entries)
        string(REGEX REPLACE "^([A-Za-z0-9_]+)[ \t]*=.*$" "\\1" name "${entry}")
        string(REGEX REPLACE "^[A-Za-z0-9_]+[ \t]*=[ \t]*([^,]+),$" "\\1" value "${entry}")
        string(STRIP "${value}" value)
        if(value MATCHES "^(0x[0-9A-Fa-f]+|[0-9]+)u?$")
            string(REGEX REPLACE "u$" "" value "${value}")
            math(EXPR decimal "${value}")
        else()
            string(REPLACE "|" ";" parts "${value}")
            set(decimal 0)
            foreach(part IN LISTS parts)
                string(STRIP "${part}" part)
                if(NOT DEFINED retail_${prefix}_${part})
                    message(FATAL_ERROR "race setup plan isolation: ${enum_name} value '${value}' names unknown '${part}'")
                endif()
                math(EXPR decimal "${decimal} | ${retail_${prefix}_${part}}")
            endforeach()
        endif()
        set(retail_${prefix}_${name} "${decimal}")
        set(retail_${prefix}_${name} "${decimal}" PARENT_SCOPE)
        list(APPEND names "${name}")
    endforeach()
    list(LENGTH names name_count)
    if(name_count LESS 20)
        message(FATAL_ERROR "race setup plan isolation: parsed only ${name_count} entries of enum ${enum_name}; the scan is broken")
    endif()
    set(${prefix}_names "${names}" PARENT_SCOPE)
endfunction()

ctr_parse_enum(GameMode1 GM1)
ctr_parse_enum(GameMode2 GM2)
string(REGEX MATCH "#define ADVENTURE_BOSS[ \t]+(0x[0-9A-Fa-f]+)u?[\r\n]" boss_line "${retail_main_code}")
if(NOT boss_line)
    message(FATAL_ERROR "race setup plan isolation: #define ADVENTURE_BOSS not found in include/namespace_Main.h")
endif()
math(EXPR boss_value "${CMAKE_MATCH_1}")
set(retail_GM1_ADVENTURE_BOSS "${boss_value}")
list(APPEND GM1_names ADVENTURE_BOSS)

foreach(prefix IN ITEMS GM1 GM2)
    foreach(name IN LISTS ${prefix}_names)
        string(REGEX MATCHALL "#define MAIN_ARCADE_RACE_SETUP_${prefix}_${name} UINT32_C\\((0x[0-9A-Fa-f]+)\\)[\r\n]" mirror_lines "${header}")
        list(LENGTH mirror_lines mirror_count)
        if(NOT mirror_count EQUAL 1)
            message(FATAL_ERROR "race setup plan isolation: ${module_header} must mirror ${name} exactly once as MAIN_ARCADE_RACE_SETUP_${prefix}_${name} UINT32_C(0x...) (found ${mirror_count})")
        endif()
        string(REGEX MATCH "UINT32_C\\((0x[0-9A-Fa-f]+)\\)" mirror_value "${mirror_lines}")
        math(EXPR mirror_decimal "${CMAKE_MATCH_1}")
        if(NOT mirror_decimal EQUAL retail_${prefix}_${name})
            message(FATAL_ERROR "race setup plan isolation: MAIN_ARCADE_RACE_SETUP_${prefix}_${name} is ${mirror_decimal}, include/namespace_Main.h has ${retail_${prefix}_${name}}")
        endif()
    endforeach()

    # Every literal mirror names a retail value (the policy masks are derived).
    string(REGEX MATCHALL "#define MAIN_ARCADE_RACE_SETUP_${prefix}_[A-Za-z0-9_]+ UINT32_C" header_mirrors "${header}")
    foreach(mirror IN LISTS header_mirrors)
        string(REGEX REPLACE "^#define MAIN_ARCADE_RACE_SETUP_${prefix}_([A-Za-z0-9_]+) UINT32_C$" "\\1" mirror_name "${mirror}")
        if(mirror_name MATCHES "_MASK$")
            continue()
        endif()
        list(FIND ${prefix}_names "${mirror_name}" known_at)
        if(known_at EQUAL -1)
            message(FATAL_ERROR "race setup plan isolation: MAIN_ARCADE_RACE_SETUP_${prefix}_${mirror_name} mirrors no retail value")
        endif()
    endforeach()
endforeach()

# The AI set mirrors equal match select's values (the plan may not name match
# select, so it mirrors them), and the v2 encoding is declared once with its
# size static-asserted in the source.
ctr_read_source("include/platform/native_match_select_rules.h" match_select)
foreach(name IN ITEMS AI_SET_COUNT AI_SET_NONE)
    string(REGEX MATCH "#define NATIVE_MATCH_SELECT_${name}[ \t]+(0x[0-9A-Fa-f]+|[0-9]+)u?[\r\n]" retail_line "${match_select}")
    if(NOT retail_line)
        message(FATAL_ERROR "race setup plan isolation: NATIVE_MATCH_SELECT_${name} not found in include/platform/native_match_select_rules.h")
    endif()
    math(EXPR retail_value "${CMAKE_MATCH_1}")
    string(REGEX MATCHALL "#define MAIN_ARCADE_RACE_SETUP_${name}[ \t]+(0x[0-9A-Fa-f]+|[0-9]+)u?[\r\n]" mirror_lines "${header}")
    list(LENGTH mirror_lines mirror_count)
    if(NOT mirror_count EQUAL 1)
        message(FATAL_ERROR "race setup plan isolation: ${module_header} must define MAIN_ARCADE_RACE_SETUP_${name} exactly once (found ${mirror_count})")
    endif()
    string(REGEX MATCH "[ \t]+(0x[0-9A-Fa-f]+|[0-9]+)u?[\r\n]" mirror_value "${mirror_lines}")
    math(EXPR mirror_decimal "${CMAKE_MATCH_1}")
    if(NOT mirror_decimal EQUAL retail_value)
        message(FATAL_ERROR "race setup plan isolation: MAIN_ARCADE_RACE_SETUP_${name} is ${mirror_decimal}, NATIVE_MATCH_SELECT_${name} is ${retail_value}")
    endif()
endforeach()
ctr_require("${module_header}" "${header}" "#define MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG \"CTRN arcade race setup plan v2\"")
ctr_require("${module_header}" "${header}" "#define MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES 135u")
ctr_forbid("${module_header}" "${header}" "PLAN_V1_")
ctr_read_source("${module_source}" plan_source)
ctr_require("${module_source}" "${plan_source}" "_Static_assert(MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES ==")

# 6. Retail field widths behind struct MainArcadeRaceSetupRetailFields.
string(FIND "${retail_main_code}" "struct GameTracker\n{" tracker_at)
if(tracker_at EQUAL -1)
    string(FIND "${retail_main_code}" "struct GameTracker\r\n{" tracker_at)
endif()
if(tracker_at EQUAL -1)
    message(FATAL_ERROR "race setup plan isolation: struct GameTracker not found in include/namespace_Main.h")
endif()
string(SUBSTRING "${retail_main_code}" ${tracker_at} -1 tracker_tail)
string(FIND "${tracker_tail}" "\n};" tracker_end)
string(SUBSTRING "${tracker_tail}" 0 ${tracker_end} tracker_body)
foreach(field IN ITEMS "int gameMode1" "int gameMode2" "int levelID" "s8 numLaps" "u8 numPlyrNextGame"
        "int arcadeDifficulty" "char boolDemoMode")
    string(REPLACE " " ";" field_parts "${field}")
    list(GET field_parts 0 field_type)
    list(GET field_parts 1 field_name)
    ctr_require_field("include/namespace_Main.h (struct GameTracker)" "${tracker_body}" "${field_type}" "${field_name}")
endforeach()
# The anchored check itself rejects a widened or re-signed declaration.
ctr_require_field("probe" "\n\tint gameMode1;\n" "int" "gameMode1")
string(REGEX MATCH "(^|[\r\n])[ \t]*int[ \t]+gameMode1[ \t]*;" probe_found "\n\tunsigned int gameMode1;\n")
if(NOT probe_found STREQUAL "")
    message(FATAL_ERROR "race setup plan isolation: the field-width check accepts 'unsigned int gameMode1;' for 'int gameMode1;'")
endif()
ctr_read_source("include/regionsEXE.h" regions)
ctr_require_field("include/regionsEXE.h" "${regions}" "s16" "characterIDs\\[8\\]")
string(FIND "${header}" "struct MainArcadeRaceSetupRetailFields\n{" fields_at)
if(fields_at EQUAL -1)
    string(FIND "${header}" "struct MainArcadeRaceSetupRetailFields\r\n{" fields_at)
endif()
if(fields_at EQUAL -1)
    message(FATAL_ERROR "race setup plan isolation: struct MainArcadeRaceSetupRetailFields not found in ${module_header}")
endif()
string(SUBSTRING "${header}" ${fields_at} -1 fields_tail)
string(FIND "${fields_tail}" "};" fields_end)
string(SUBSTRING "${fields_tail}" 0 ${fields_end} fields_body)
foreach(field IN ITEMS "int32_t levelID" "uint32_t gameMode1" "uint32_t gameMode2" "int32_t arcadeDifficulty"
        "int16_t characterIDs\\[MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT\\]" "int8_t numLaps" "uint8_t numPlyrNextGame"
        "uint8_t boolDemoMode")
    string(REPLACE " " ";" field_parts "${field}")
    list(GET field_parts 0 field_type)
    list(GET field_parts 1 field_name)
    ctr_require_field("${module_header} (struct MainArcadeRaceSetupRetailFields)" "${fields_body}" "${field_type}" "${field_name}")
endforeach()
ctr_require("${module_header}" "${header}" "#define MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT 8u")

# 7. Confined: only the module, the R-5b live adapter, and its R-5c core name
# it, and only its unit test, the roster proof unit test, the core, and ctr_native link
# it.
set(plan_adapter_paths "game/MAIN/MainArcadeRaceSetup.c" "game/MAIN/MainArcadeRaceSetup.h"
    "game/MAIN/MainArcadeRaceSetupCore.c" "game/MAIN/MainArcadeRaceSetupCore.h")
file(GLOB_RECURSE game_files "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/platform/*.h" "${repo}/main.c")
set(scanned 0)
set(scanned_platform 0)
foreach(path IN LISTS game_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL module_header OR relative_path STREQUAL module_source)
        continue()
    endif()
    list(FIND plan_adapter_paths "${relative_path}" adapter_at)
    if(NOT adapter_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" game_source)
    ctr_forbid("${relative_path}" "${game_source}" "MainArcadeRaceSetupPlan")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path MATCHES "^platform/")
        math(EXPR scanned_platform "${scanned_platform} + 1")
    endif()
endforeach()
if(scanned LESS 100 OR scanned_platform LESS 20)
    message(FATAL_ERROR "race setup plan isolation: scanned only ${scanned} game/ and platform/ files (${scanned_platform} in platform/); the scan is broken")
endif()
ctr_read_source("game/game_unity.h" unity)
ctr_forbid("game/game_unity.h" "${unity}" "MainArcadeRaceSetupPlan")
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*[A-Za-z0-9_]+[^)]*\\)" all_link_calls "${cmake}")
foreach(link_call IN LISTS all_link_calls)
    string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*([A-Za-z0-9_]+).*$" "\\1" linking_target "${link_call}")
    string(REGEX MATCH "[ \t\r\n]${target}[ \t\r\n)]" names_module "${link_call}")
    if(names_module AND NOT linking_target STREQUAL "main_arcade_race_setup_plan_test"
       AND NOT linking_target STREQUAL "native_arcade_roster_proof_test"
       AND NOT linking_target STREQUAL "ctr_native_arcade_race_setup_core"
       AND NOT linking_target STREQUAL "ctr_native")
        message(FATAL_ERROR "race setup plan isolation: ${linking_target} links ${target}; only main_arcade_race_setup_plan_test, native_arcade_roster_proof_test, ctr_native_arcade_race_setup_core, and ctr_native may")
    endif()
endforeach()
