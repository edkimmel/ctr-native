# Structural isolation for the match-select resolution rules
# (native_match_select_rules, docs/MATCH_SELECT_MILESTONE.md section 2.1,
# MS-2). The module is pure: no OS-networking, SDL, game, lockstep, peer
# link, lobby, virtual datagram, topology-lease, canonical-state, replay, or
# checkpoint dependency, no heap, clock, or stdio use. Its includes are
# limited to stddef.h, stdint.h, string.h, its own header, and the
# match-config and SHA-256 headers; the library links exactly
# ctr_native_match_config and ctr_native_sha256; the target stays portable
# C17 with extensions off.
#
# The structural rule: the module's tables mirror retail. The seven 2P AI
# sets must equal game/zGlobal_DATA.c `.characterIDs_2P_AIs` in order; the
# lap options must equal the nonzero game/230/D230.c `.lapCountByRow` rows;
# the tracks must equal the levelIDs of the game/230/D230.c `.arcadeTracks`
# rows whose unlock field is 0xFFFF, in order; and the characters must equal
# the retail default-character table that MM_Characters_PreventOverlap scans
# (game/230/R230.c `.packedDefaultCharacterIDWords`, unpacked as
# little-endian bytes), whose length is game/230/MM_Characters.c
# MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT, and whose IDs must name, in
# order, the first entries of include/namespace_Vehicle.h `enum Characters`:
# CRASH_BANDICOOT = 0, then NEO_CORTEX, TINY_TIGER, COCO_BANDICOOT, N_GIN,
# DINGODILE, POLAR, PURA with implicit values. Both sides are parsed here,
# so a change to either fails this test.
#
# The milestone rule (docs/MATCH_SELECT_MILESTONE.md section 5.3): no
# `NativeMatchSelect` or `native_match_select` token in any game/*.c or
# game/*.h file; game code talks only to the host API.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "match select rules isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

# Parses a comma-separated list of decimal (optional u suffix) or 0x hex
# integers into a CMake list of decimal values.
function(ctr_parse_numbers description text out_var)
    string(REGEX REPLACE "[ \t\r\n]" "" text "${text}")
    string(REPLACE "," ";" items "${text}")
    set(values "")
    foreach(item IN LISTS items)
        if(item STREQUAL "")
            continue()
        endif()
        if(item MATCHES "^0[xX]([0-9a-fA-F]+)[uU]?$")
            math(EXPR value "0x${CMAKE_MATCH_1}")
        elseif(item MATCHES "^([0-9]+)[uU]?$")
            math(EXPR value "${CMAKE_MATCH_1}")
        else()
            message(FATAL_ERROR "${prefix}: ${description}: '${item}' is not an integer")
        endif()
        list(APPEND values "${value}")
    endforeach()
    set(${out_var} "${values}" PARENT_SCOPE)
endfunction()

# Reads the `static const uint8_t <name>[...] = { ... };` initializer from
# the module source, which must declare it exactly once.
function(ctr_module_table source name out_var)
    string(REGEX MATCHALL "static const uint8_t ${name}[^a-zA-Z0-9_]" declarations "${source}")
    list(LENGTH declarations declaration_count)
    if(NOT declaration_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${name} must be declared exactly once in the module (found ${declaration_count})")
    endif()
    string(REGEX MATCH "static const uint8_t ${name}\\[[A-Za-z0-9_ *]*\\][ \t]*=[ \t]*\\{([^{}]*)\\}" table "${source}")
    if(table STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${name} is not a simple one-level initializer")
    endif()
    ctr_parse_numbers("module ${name}" "${CMAKE_MATCH_1}" values)
    set(${out_var} "${values}" PARENT_SCOPE)
endfunction()

# Reads a retail `.<field> = { {..}, {..}, ... }` initializer of brace rows
# (comments removed) and returns the number of rows; row i is in
# <out_prefix>_<i>, each a list of decimal values.
function(ctr_retail_rows relative_path source field window out_prefix out_count)
    string(REGEX MATCHALL "\\.${field}[ \t]*=" occurrences "${source}")
    list(LENGTH occurrences occurrence_count)
    if(NOT occurrence_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: .${field} must be initialized exactly once in ${relative_path} (found ${occurrence_count})")
    endif()
    string(FIND "${source}" ".${field}" at)
    string(SUBSTRING "${source}" "${at}" "${window}" block)
    string(REGEX REPLACE "//[^\r\n]*" "" block "${block}")
    string(REGEX MATCH "^\\.${field}[ \t]*=[ \t\r\n]*\\{(([ \t\r\n,]*\\{[^{}]*\\})*)[ \t\r\n,]*\\}" whole "${block}")
    if(whole STREQUAL "")
        message(FATAL_ERROR "${prefix}: could not parse .${field} in ${relative_path}")
    endif()
    string(REGEX MATCHALL "\\{[^{}]*\\}" rows "${CMAKE_MATCH_1}")
    set(row_index 0)
    foreach(row IN LISTS rows)
        string(REGEX REPLACE "[{}]" "" row "${row}")
        ctr_parse_numbers("${relative_path} .${field} row ${row_index}" "${row}" values)
        set(${out_prefix}_${row_index} "${values}" PARENT_SCOPE)
        math(EXPR row_index "${row_index} + 1")
    endforeach()
    set(${out_count} "${row_index}" PARENT_SCOPE)
endfunction()

function(ctr_require_equal description actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${prefix}: ${description}: module '${actual}' != retail '${expected}'")
    endif()
endfunction()

set(rules_header "include/platform/native_match_select_rules.h")
set(rules_source "platform/native_match_select_rules.c")
set(rules_files "${rules_header}" "${rules_source}")

# 1. No lockstep, peer link, lobby, virtual datagram, or netplay dependency.
set(link_tokens
    lockstep Lockstep LOCKSTEP PeerLink peer_link NativeLobby native_lobby
    VirtualDatagram virtual_datagram udp_transport UdpTransport ArcadeNetplay arcade_netplay)

# 2. No socket / OS-networking header or symbol, and no SDL.
set(network_tokens
    winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo
    inet_ "poll(" SDL)

# 3. No clock, stdio, or game-code dependency.
set(dependency_tokens "time(" "clock(" QueryPerformance stdio printf fopen "FILE" Game_ "gGT" "sdata")

# 4. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 5. No topology-lease symbol.
set(lease_tokens TopologyLease Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 6. No canonical-state, replay, or checkpoint state.
set(state_tokens NativeCanonical native_canonical NativeReplay native_replay Replay Checkpoint checkpoint)

foreach(relative_path IN LISTS rules_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS link_tokens network_tokens dependency_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 7. #include lines may only name the allowlisted headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_match_select_rules\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_sha256\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 8. ctr_native_match_select_rules links exactly ctr_native_match_config and
#    ctr_native_sha256, in exactly one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_match_select_rules)
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
if(NOT "${link_items}" STREQUAL "ctr_native_match_config;ctr_native_sha256")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_match_config and ctr_native_sha256 (found '${link_items}')")
endif()

# 9. C17, no extensions, on the rules target, in order.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "${prefix}: ${target} C17/no-extensions properties are out of order")
endif()

# 10. Retail mirrors. First the module side.
ctr_read_source("${rules_source}" module_source)
ctr_read_source("${rules_header}" module_header)
ctr_module_table("${module_source}" k_matchSelectCharacters module_characters)
ctr_module_table("${module_source}" k_matchSelectTracks module_tracks)
ctr_module_table("${module_source}" k_matchSelectLapOptions module_laps)
ctr_module_table("${module_source}" k_matchSelectAiSets module_ai_sets)

function(ctr_header_count name out_var)
    string(REGEX MATCH "#define ${name} ([0-9]+)u[^0-9a-zA-Z_]" found "${module_header}")
    if(found STREQUAL "")
        message(FATAL_ERROR "${prefix}: missing #define ${name} <n>u in ${rules_header}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
ctr_header_count(NATIVE_MATCH_SELECT_CHARACTER_COUNT header_character_count)
ctr_header_count(NATIVE_MATCH_SELECT_TRACK_COUNT header_track_count)
ctr_header_count(NATIVE_MATCH_SELECT_LAP_OPTION_COUNT header_lap_count)
ctr_header_count(NATIVE_MATCH_SELECT_AI_SET_COUNT header_ai_set_count)
ctr_header_count(NATIVE_MATCH_SELECT_AI_SET_RACERS header_ai_set_racers)

# Characters. (a) The default-driver count, defined exactly once in
# game/230/MM_Characters.c.
ctr_read_source("game/230/MM_Characters.c" mm_characters)
string(REGEX MATCHALL "MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT[ \t]*=[ \t]*[0-9a-fA-FxX]+" definitions "${mm_characters}")
list(LENGTH definitions definition_count)
if(NOT definition_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT must be defined exactly once in game/230/MM_Characters.c (found ${definition_count})")
endif()
string(REGEX MATCH "=[ \t]*([0-9a-fA-FxX]+)" found "${definitions}")
ctr_parse_numbers("MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT" "${CMAKE_MATCH_1}" retail_default_count)
ctr_require_equal("NATIVE_MATCH_SELECT_CHARACTER_COUNT vs MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT"
    "${header_character_count}" "${retail_default_count}")
ctr_require_equal("MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT" "8" "${retail_default_count}")

# (b) The default-character table MM_Characters_PreventOverlap copies
#     (MM_DEFAULT_CHARACTER_ID_WORDS, game/230.c), initialized exactly once in
#     game/230/R230.c as little-endian packed u32 words.
ctr_read_source("game/230/R230.c" r230)
string(REGEX MATCHALL "\\.packedDefaultCharacterIDWords[ \t]*=" occurrences "${r230}")
list(LENGTH occurrences occurrence_count)
if(NOT occurrence_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: .packedDefaultCharacterIDWords must be initialized exactly once in game/230/R230.c (found ${occurrence_count})")
endif()
string(REGEX MATCH "\\.packedDefaultCharacterIDWords[ \t]*=[ \t\r\n]*\\{([^{}]*)\\}" found "${r230}")
if(found STREQUAL "")
    message(FATAL_ERROR "${prefix}: could not parse .packedDefaultCharacterIDWords in game/230/R230.c")
endif()
ctr_parse_numbers("game/230/R230.c .packedDefaultCharacterIDWords" "${CMAKE_MATCH_1}" packed_words)
set(retail_characters "")
foreach(word IN LISTS packed_words)
    foreach(shift IN ITEMS 0 8 16 24)
        math(EXPR byte "(${word} >> ${shift}) & 0xff")
        list(APPEND retail_characters "${byte}")
    endforeach()
endforeach()
list(LENGTH retail_characters retail_character_count)
ctr_require_equal("packedDefaultCharacterIDWords byte count vs MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT"
    "${retail_character_count}" "${retail_default_count}")
ctr_require_equal("characters vs packedDefaultCharacterIDWords" "${module_characters}" "${retail_characters}")
list(LENGTH module_characters length)
ctr_require_equal("NATIVE_MATCH_SELECT_CHARACTER_COUNT" "${header_character_count}" "${length}")

# (c) Those IDs are the first entries of `enum Characters`
#     (include/namespace_Vehicle.h), declared exactly once: CRASH_BANDICOOT = 0
#     and then the next seven base characters with implicit values, so entry i
#     has value i.
ctr_read_source("include/namespace_Vehicle.h" vehicle_header)
string(REGEX MATCHALL "enum[ \t\r\n]+Characters[^a-zA-Z0-9_]" occurrences "${vehicle_header}")
list(LENGTH occurrences occurrence_count)
if(NOT occurrence_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: enum Characters must be declared exactly once in include/namespace_Vehicle.h (found ${occurrence_count})")
endif()
string(REGEX MATCH "enum[ \t\r\n]+Characters[ \t\r\n]*\\{([^{}]*)\\}" found "${vehicle_header}")
if(found STREQUAL "")
    message(FATAL_ERROR "${prefix}: could not parse enum Characters in include/namespace_Vehicle.h")
endif()
set(enum_body "${CMAKE_MATCH_1}")
string(REGEX REPLACE "//[^\r\n]*" "" enum_body "${enum_body}")
string(REGEX REPLACE "[ \t\r\n]" "" enum_body "${enum_body}")
string(REPLACE "," ";" enum_entries "${enum_body}")
list(LENGTH enum_entries enum_entry_count)
if(enum_entry_count LESS retail_default_count)
    message(FATAL_ERROR "${prefix}: enum Characters has ${enum_entry_count} entries, fewer than ${retail_default_count}")
endif()
math(EXPR last_character "${retail_default_count} - 1")
list(SUBLIST enum_entries 0 ${retail_default_count} base_character_entries)
ctr_require_equal("enum Characters base entries"
    "CRASH_BANDICOOT=0;NEO_CORTEX;TINY_TIGER;COCO_BANDICOOT;N_GIN;DINGODILE;POLAR;PURA" "${base_character_entries}")
foreach(index RANGE 0 ${last_character})
    list(GET retail_characters ${index} character)
    ctr_require_equal("default character ${index} vs its enum Characters value" "${index}" "${character}")
endforeach()

# 2P AI sets: game/zGlobal_DATA.c .characterIDs_2P_AIs, with the retail
# set and racer counts from include/namespace_Load.h.
ctr_read_source("include/namespace_Load.h" load_header)
string(REGEX MATCH "LOAD_2P_AI_SET_RACER_COUNT = ([0-9]+)," found "${load_header}")
set(retail_racers "${CMAKE_MATCH_1}")
string(REGEX MATCH "LOAD_2P_AI_SET_COUNT = ([0-9]+)," found "${load_header}")
set(retail_sets "${CMAKE_MATCH_1}")
ctr_require_equal("NATIVE_MATCH_SELECT_AI_SET_COUNT vs LOAD_2P_AI_SET_COUNT" "${header_ai_set_count}" "${retail_sets}")
ctr_require_equal("NATIVE_MATCH_SELECT_AI_SET_RACERS vs LOAD_2P_AI_SET_RACER_COUNT" "${header_ai_set_racers}" "${retail_racers}")

ctr_read_source("game/zGlobal_DATA.c" zglobal)
ctr_retail_rows("game/zGlobal_DATA.c" "${zglobal}" characterIDs_2P_AIs 1500 ai_row ai_row_count)
ctr_require_equal("2P AI set count" "${header_ai_set_count}" "${ai_row_count}")
set(retail_ai_sets "")
math(EXPR last_row "${ai_row_count} - 1")
foreach(row RANGE 0 ${last_row})
    list(LENGTH ai_row_${row} racers)
    ctr_require_equal("2P AI set ${row} racer count" "${header_ai_set_racers}" "${racers}")
    list(APPEND retail_ai_sets ${ai_row_${row}})
endforeach()
ctr_require_equal("2P AI sets" "${module_ai_sets}" "${retail_ai_sets}")

# Laps: the nonzero game/230/D230.c .lapCountByRow rows (lapCount is each
# row's first field).
ctr_read_source("game/230/D230.c" d230)
ctr_retail_rows("game/230/D230.c" "${d230}" lapCountByRow 200 lap_row lap_row_count)
set(retail_laps "")
math(EXPR last_row "${lap_row_count} - 1")
foreach(row RANGE 0 ${last_row})
    list(GET lap_row_${row} 0 lap_count)
    if(NOT lap_count EQUAL 0)
        list(APPEND retail_laps "${lap_count}")
    endif()
endforeach()
ctr_require_equal("lap options" "${module_laps}" "${retail_laps}")
ctr_require_equal("lap options" "${module_laps}" "3;5;7")
list(LENGTH module_laps length)
ctr_require_equal("NATIVE_MATCH_SELECT_LAP_OPTION_COUNT" "${header_lap_count}" "${length}")

# Tracks: levelIDs of the game/230/D230.c .arcadeTracks rows whose unlock
# field (the 4th) is 0xFFFF (MM_TRACK_UNLOCK_ALWAYS), in order. Retail has
# 0x12 rows (include/ovr_230.h arcadeTracks[0x12]).
ctr_retail_rows("game/230/D230.c" "${d230}" arcadeTracks 2500 track_row track_row_count)
ctr_require_equal("arcadeTracks row count" "18" "${track_row_count}")
set(retail_tracks "")
math(EXPR last_row "${track_row_count} - 1")
foreach(row RANGE 0 ${last_row})
    list(LENGTH track_row_${row} fields)
    ctr_require_equal("arcadeTracks row ${row} field count" "6" "${fields}")
    list(GET track_row_${row} 0 level_id)
    list(GET track_row_${row} 3 unlock)
    if(unlock EQUAL 65535)
        list(APPEND retail_tracks "${level_id}")
    endif()
endforeach()
ctr_require_equal("tracks" "${module_tracks}" "${retail_tracks}")
list(LENGTH module_tracks length)
ctr_require_equal("NATIVE_MATCH_SELECT_TRACK_COUNT" "${header_track_count}" "${length}")

# 11. No match-select token under game/ (docs/MATCH_SELECT_MILESTONE.md
#     section 5.3): game code talks only to the host API.
file(GLOB_RECURSE game_sources RELATIVE "${repo}" "${repo}/game/*.c" "${repo}/game/*.h")
list(LENGTH game_sources game_source_count)
if(game_source_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: game/ source glob is empty; the match-select token scan cannot run")
endif()
foreach(relative_path IN LISTS game_sources)
    ctr_read_source("${relative_path}" source)
    foreach(term IN ITEMS NativeMatchSelect native_match_select)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()
