# Structural isolation for the arcade-link screen layout builder
# (game/MAIN/MainArcadeLinkLayout.{c,h}, docs/GAME_LOOP_UI_MILESTONE.md
# section 2.4): a pure draw-list builder that never talks to the netplay
# adapter, the link, or its state, reads no game global, draws nothing, uses
# no heap and no stdio, and never names the topology lease. Its only includes
# are stdint.h, stddef.h, string.h, the arcade flow header, and its own
# header; the library links only ctr_native_arcade_flow; the target stays
# portable C17 with extensions off; and its mirrored retail font, colour, and
# justification values stay in step with include/namespace_Decal.h.
#
# Since MS-9 (the match-select screens, docs/MATCH_SELECT_MILESTONE.md
# section 2.8) also: the four player-colour mirrors exist and every mirrored
# colour equals its enum DecalFontStyle ordinal; the layout's select-order
# lists equal the retail tables (tracks: game/230/D230.c .arcadeTracks rows
# with unlock 0xFFFF; laps: .lapCountByRow; characters: the eight base
# enum Characters entries); its font advances equal game/zGlobal_DATA.c
# .font_charPixWidth; and every glyph of every layout string exists in the
# retail font map (.font_characterIconID), with no PSX button glyph.
#
# Since MS-10b the layout owns the host-view mapping
# (MainArcadeLinkLayout_InputFromHostView): its header, and only its header,
# may also include the game-safe host glue header
# (include/platform/native_arcade_link_host.h) for the plain view types and
# value names; neither file names a host function (NativeArcadeLinkHost_*);
# the header static-asserts the ten layout select values and capacities
# against the host names beside the mapping's prototype; and the library
# still links only ctr_native_arcade_flow (never the host glue).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link layout isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link layout isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_literal relative_path source literal)
    string(FIND "${source}" "${literal}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "arcade link layout isolation: required text '${literal}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "arcade link layout isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# The layout files.
set(layout_header "game/MAIN/MainArcadeLinkLayout.h")
set(layout_files
    "${layout_header}"
    "game/MAIN/MainArcadeLinkLayout.c")

# 1a. No netplay, link, or lobby dependency: the layout sees only the flow's
#     own enums and never talks to the adapter.
set(netplay_tokens
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport NativeArcadeNetplay)

# 1b. No game global and no rendering: the drawer (Task 6) owns both.
set(game_tokens
    sdata gGT GameTracker common.h DecalFont RECTMENU CTR_Box PrimMem OTMem)

# 1c. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 1d. No stdio: strings are built with bounded copies.
set(stdio_tokens printf snprintf stdio.h)

# 1e. No topology-lease symbol.
set(lease_tokens TopologyLease Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 1f. No host function (MS-10b): the layout uses only the host glue's plain
#     view types and value names and never calls into the host.
set(host_call_tokens NativeArcadeLinkHost_)

foreach(relative_path IN LISTS layout_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS netplay_tokens game_tokens alloc_tokens stdio_tokens lease_tokens host_call_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 2. #include lines may only name stdint.h, stddef.h, string.h, the
    #    arcade flow header, or the layout's own header; the layout header
    #    alone may also include the host glue header (MS-10b), exactly once.
    set(allowed_includes "<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"platform/native_arcade_flow\\.h\"|\"MAIN/MainArcadeLinkLayout\\.h\"")
    if(relative_path STREQUAL layout_header)
        string(APPEND allowed_includes "|\"platform/native_arcade_link_host\\.h\"")
    endif()
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(${allowed_includes})[ \t]*$")
            message(FATAL_ERROR "arcade link layout isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()
ctr_read_source("${layout_header}" header_source)
string(REGEX MATCHALL "#[ \t]*include[ \t]*\"platform/native_arcade_link_host\\.h\"" host_includes "${header_source}")
list(LENGTH host_includes host_include_count)
if(NOT host_include_count EQUAL 1)
    message(FATAL_ERROR "arcade link layout isolation: ${layout_header} must include platform/native_arcade_link_host.h exactly once (found ${host_include_count})")
endif()

# 3a. ctr_native_arcade_link_layout links ctr_native_arcade_flow and nothing
#     else, in exactly one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_link_layout)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link layout isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_flow")
    message(FATAL_ERROR "arcade link layout isolation: ${target} must link only ctr_native_arcade_flow (found '${link_items}')")
endif()

# 3b. C17, no extensions, on the layout target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade link layout isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade link layout isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade link layout isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade link layout isolation: ${target} C17/no-extensions properties are out of order")
endif()

# 4. The mirrored retail values stay in step with include/namespace_Decal.h.
set(decal_header "include/namespace_Decal.h")
ctr_read_source("${decal_header}" decal)
ctr_require_literal("${decal_header}" "${decal}" "FONT_BIG = 1,")
ctr_require_literal("${decal_header}" "${decal}" "FONT_SMALL = 2,")
ctr_require_literal("${decal_header}" "${decal}" "JUSTIFY_CENTER = 0x8000")

ctr_read_source("${layout_header}" header)
foreach(mirror
        "MAIN_ARCADE_LINK_FONT_BIG 1u"
        "MAIN_ARCADE_LINK_FONT_SMALL 2u"
        "MAIN_ARCADE_LINK_COLOR_ORANGE 0u"
        "MAIN_ARCADE_LINK_COLOR_RED 3u"
        "MAIN_ARCADE_LINK_COLOR_WHITE 4u"
        "MAIN_ARCADE_LINK_COLOR_GRAY 23u"
        "MAIN_ARCADE_LINK_JUSTIFY_CENTER 0x8000u")
    ctr_require_regex("${layout_header}" "${header}" "(^|\n)#define ${mirror}\r?\n")
endforeach()

# ---- Match-select screens (docs/MATCH_SELECT_MILESTONE.md section 2.8, MS-9) ----

function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
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
            message(FATAL_ERROR "arcade link layout isolation: ${description}: '${item}' is not an integer")
        endif()
        list(APPEND values "${value}")
    endforeach()
    set(${out_var} "${values}" PARENT_SCOPE)
endfunction()

# Reads a retail `.<field> = { ... }` one-level initializer (line comments
# removed) from a window of source text; the field must be initialized
# exactly once.
function(ctr_retail_flat relative_path source field window out_var)
    string(REGEX MATCHALL "\\.${field}[ \t]*=" occurrences "${source}")
    list(LENGTH occurrences occurrence_count)
    if(NOT occurrence_count EQUAL 1)
        message(FATAL_ERROR "arcade link layout isolation: .${field} must be initialized exactly once in ${relative_path} (found ${occurrence_count})")
    endif()
    string(FIND "${source}" ".${field}" at)
    string(SUBSTRING "${source}" "${at}" "${window}" block)
    string(REGEX REPLACE "//[^\r\n]*" "" block "${block}")
    string(REGEX MATCH "^\\.${field}[ \t]*=[ \t\r\n]*\\{([^{}]*)\\}" whole "${block}")
    if(whole STREQUAL "")
        message(FATAL_ERROR "arcade link layout isolation: could not parse .${field} in ${relative_path}")
    endif()
    ctr_parse_numbers("${relative_path} .${field}" "${CMAKE_MATCH_1}" values)
    set(${out_var} "${values}" PARENT_SCOPE)
endfunction()

# Reads a retail `.<field> = { {..}, {..} }` initializer of brace rows (line
# comments removed); row i is in <out_prefix>_<i>.
function(ctr_retail_rows relative_path source field window out_prefix out_count)
    string(REGEX MATCHALL "\\.${field}[ \t]*=" occurrences "${source}")
    list(LENGTH occurrences occurrence_count)
    if(NOT occurrence_count EQUAL 1)
        message(FATAL_ERROR "arcade link layout isolation: .${field} must be initialized exactly once in ${relative_path} (found ${occurrence_count})")
    endif()
    string(FIND "${source}" ".${field}" at)
    string(SUBSTRING "${source}" "${at}" "${window}" block)
    string(REGEX REPLACE "//[^\r\n]*" "" block "${block}")
    string(REGEX MATCH "^\\.${field}[ \t]*=[ \t\r\n]*\\{(([ \t\r\n,]*\\{[^{}]*\\})*)[ \t\r\n,]*\\}" whole "${block}")
    if(whole STREQUAL "")
        message(FATAL_ERROR "arcade link layout isolation: could not parse .${field} in ${relative_path}")
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

# Reads the layout's `static const uint8_t <name>[...] = { ... };` list,
# declared exactly once.
function(ctr_layout_table source name out_var)
    string(REGEX MATCHALL "static const uint8_t ${name}[^a-zA-Z0-9_]" declarations "${source}")
    list(LENGTH declarations declaration_count)
    if(NOT declaration_count EQUAL 1)
        message(FATAL_ERROR "arcade link layout isolation: ${name} must be declared exactly once in the layout (found ${declaration_count})")
    endif()
    string(REGEX MATCH "static const uint8_t ${name}\\[[A-Za-z0-9_ *]*\\][ \t]*=[ \t\r\n]*\\{([^{}]*)\\}" table "${source}")
    if(table STREQUAL "")
        message(FATAL_ERROR "arcade link layout isolation: ${name} is not a simple one-level initializer")
    endif()
    ctr_parse_numbers("layout ${name}" "${CMAKE_MATCH_1}" values)
    set(${out_var} "${values}" PARENT_SCOPE)
endfunction()

function(ctr_require_equal description actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "arcade link layout isolation: ${description}: layout '${actual}' != retail '${expected}'")
    endif()
endfunction()

ctr_read_source("game/MAIN/MainArcadeLinkLayout.c" layout_source)
ctr_strip_comments("${layout_source}" layout_code)

# 5. The player-colour mirrors (the retail multiplayer colours P1..P4) are
#    defined, and every mirrored colour equals its ordinal in
#    include/namespace_Decal.h `enum DecalFontStyle`, counting the entries
#    before the first explicit value.
foreach(mirror
        "MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE 24u"
        "MAIN_ARCADE_LINK_COLOR_PLAYER_RED 25u"
        "MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN 26u"
        "MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW 27u")
    ctr_require_regex("${layout_header}" "${header}" "(^|\n)#define ${mirror}\r?\n")
endforeach()
string(REGEX MATCH "enum[ \t\r\n]+DecalFontStyle[ \t\r\n]*\\{([^{}]*)\\}" found "${decal}")
if(found STREQUAL "")
    message(FATAL_ERROR "arcade link layout isolation: could not parse enum DecalFontStyle in ${decal_header}")
endif()
ctr_strip_comments("${CMAKE_MATCH_1}" style_body)
string(REGEX REPLACE "[ \t\r\n]" "" style_body "${style_body}")
string(REPLACE "," ";" style_entries "${style_body}")
set(ordinal 0)
foreach(entry IN LISTS style_entries)
    if(entry STREQUAL "")
        continue()
    endif()
    if(entry MATCHES "=")
        break()
    endif()
    set(style_${entry} ${ordinal})
    math(EXPR ordinal "${ordinal} + 1")
endforeach()
foreach(name ORANGE RED WHITE GRAY PLAYER_BLUE PLAYER_RED PLAYER_GREEN PLAYER_YELLOW)
    if(NOT DEFINED style_${name})
        message(FATAL_ERROR "arcade link layout isolation: ${name} has no implicit ordinal in enum DecalFontStyle")
    endif()
    string(REGEX MATCH "#define MAIN_ARCADE_LINK_COLOR_${name} ([0-9]+)u" found "${header}")
    ctr_require_equal("MAIN_ARCADE_LINK_COLOR_${name} vs enum DecalFontStyle ${name}" "${CMAKE_MATCH_1}" "${style_${name}}")
endforeach()

# 6. The select-order lists equal the retail tables the selection rules
#    mirror (tests/native_match_select_rules_isolation_test.cmake checks the
#    rules' side): the tracks are the levelIDs of the game/230/D230.c
#    .arcadeTracks rows whose unlock field (the 4th) is 0xFFFF, in order;
#    the laps are the nonzero .lapCountByRow rows; the characters are the
#    eight base characters, CRASH_BANDICOOT = 0 through PURA.
ctr_layout_table("${layout_code}" MainArcadeLinkLayout_TrackOrder layout_tracks)
ctr_layout_table("${layout_code}" MainArcadeLinkLayout_LapOrder layout_laps)
ctr_layout_table("${layout_code}" MainArcadeLinkLayout_CharacterOrder layout_characters)
ctr_read_source("game/230/D230.c" d230)
ctr_retail_rows("game/230/D230.c" "${d230}" arcadeTracks 2500 track_row track_row_count)
ctr_require_equal("arcadeTracks row count" "${track_row_count}" "18")
set(retail_tracks "")
math(EXPR last_row "${track_row_count} - 1")
foreach(row RANGE 0 ${last_row})
    list(LENGTH track_row_${row} fields)
    ctr_require_equal("arcadeTracks row ${row} field count" "${fields}" "6")
    list(GET track_row_${row} 0 level_id)
    list(GET track_row_${row} 3 unlock)
    if(unlock EQUAL 65535)
        list(APPEND retail_tracks "${level_id}")
    endif()
endforeach()
ctr_require_equal("track order vs arcadeTracks rows with unlock 0xFFFF" "${layout_tracks}" "${retail_tracks}")
ctr_retail_rows("game/230/D230.c" "${d230}" lapCountByRow 200 lap_row lap_row_count)
set(retail_laps "")
math(EXPR last_row "${lap_row_count} - 1")
foreach(row RANGE 0 ${last_row})
    list(GET lap_row_${row} 0 lap_count)
    if(NOT lap_count EQUAL 0)
        list(APPEND retail_laps "${lap_count}")
    endif()
endforeach()
ctr_require_equal("lap order vs lapCountByRow" "${layout_laps}" "${retail_laps}")
ctr_read_source("include/namespace_Vehicle.h" vehicle_header)
string(REGEX MATCH "enum[ \t\r\n]+Characters[ \t\r\n]*\\{([^{}]*)\\}" found "${vehicle_header}")
if(found STREQUAL "")
    message(FATAL_ERROR "arcade link layout isolation: could not parse enum Characters in include/namespace_Vehicle.h")
endif()
ctr_strip_comments("${CMAKE_MATCH_1}" character_body)
string(REGEX REPLACE "[ \t\r\n]" "" character_body "${character_body}")
string(REPLACE "," ";" character_entries "${character_body}")
list(SUBLIST character_entries 0 8 base_character_entries)
ctr_require_equal("enum Characters base entries"
    "${base_character_entries}" "CRASH_BANDICOOT=0;NEO_CORTEX;TINY_TIGER;COCO_BANDICOOT;N_GIN;DINGODILE;POLAR;PURA")
ctr_require_equal("character order" "${layout_characters}" "0;1;2;3;4;5;6;7")

# 7. The layout's font advances equal the retail per-glyph widths
#    (game/zGlobal_DATA.c .font_charPixWidth, indexed by FONT_BIG and
#    FONT_SMALL), which the marker slots are packed with.
ctr_read_source("game/zGlobal_DATA.c" zglobal)
ctr_retail_flat("game/zGlobal_DATA.c" "${zglobal}" font_charPixWidth 400 char_widths)
list(GET char_widths 1 big_width)
list(GET char_widths 2 small_width)
string(REGEX MATCH "#define MAIN_ARCADE_LINK_LAYOUT_BIG_ADVANCE ([0-9]+)\r?\n" found "${layout_source}")
ctr_require_equal("MAIN_ARCADE_LINK_LAYOUT_BIG_ADVANCE vs font_charPixWidth[FONT_BIG]" "${CMAKE_MATCH_1}" "${big_width}")
string(REGEX MATCH "#define MAIN_ARCADE_LINK_LAYOUT_SMALL_ADVANCE ([0-9]+)\r?\n" found "${layout_source}")
ctr_require_equal("MAIN_ARCADE_LINK_LAYOUT_SMALL_ADVANCE vs font_charPixWidth[FONT_SMALL]" "${CMAKE_MATCH_1}" "${small_width}")

# 8. Every glyph of every string the layout draws exists in the retail font:
#    each character of each string literal in the layout's code (comments
#    and #include lines removed) is a space or maps to an icon other than
#    0xFF in game/zGlobal_DATA.c .font_characterIconID (indexed from ASCII
#    0x21), and is none of the PSX button glyphs '@', '[', '^', '*'. No
#    literal uses an escape sequence.
ctr_retail_flat("game/zGlobal_DATA.c" "${zglobal}" font_characterIconID 12000 icon_ids)
list(LENGTH icon_ids icon_count)
if(icon_count LESS 90)
    message(FATAL_ERROR "arcade link layout isolation: .font_characterIconID parsed only ${icon_count} entries")
endif()
string(REGEX REPLACE "#[ \t]*include[^\r\n]*" "" literal_code "${layout_code}")
string(REGEX MATCHALL "\"[^\"\r\n]*\"" literals "${literal_code}")
list(LENGTH literals literal_count)
if(literal_count LESS 40)
    message(FATAL_ERROR "arcade link layout isolation: found only ${literal_count} string literals in the layout; the glyph scan is broken")
endif()
foreach(literal IN LISTS literals)
    string(LENGTH "${literal}" literal_length)
    math(EXPR last_char "${literal_length} - 2")
    if(last_char LESS 1)
        continue()
    endif()
    foreach(position RANGE 1 ${last_char})
        string(SUBSTRING "${literal}" ${position} 1 ch)
        if(ch STREQUAL " ")
            continue()
        endif()
        if(ch STREQUAL "\\" OR ch STREQUAL "@" OR ch STREQUAL "[" OR ch STREQUAL "^" OR ch STREQUAL "*")
            message(FATAL_ERROR "arcade link layout isolation: layout string ${literal} uses '${ch}' (an escape or a PSX button glyph)")
        endif()
        string(HEX "${ch}" ch_hex)
        math(EXPR icon_index "0x${ch_hex} - 0x21")
        if(icon_index LESS 0 OR NOT icon_index LESS icon_count)
            message(FATAL_ERROR "arcade link layout isolation: layout string ${literal} uses '${ch}', outside the retail font map")
        endif()
        list(GET icon_ids ${icon_index} icon_id)
        if(icon_id EQUAL 255)
            message(FATAL_ERROR "arcade link layout isolation: layout string ${literal} uses '${ch}', which the retail font lacks (font_characterIconID 0xFF)")
        endif()
    endforeach()
endforeach()

# 9. The host-view mapping (MS-10b): the header declares
#    MainArcadeLinkLayout_InputFromHostView and, beside it, static-asserts
#    each of the ten select values and capacities the mapping relies on
#    against the host name it mirrors; the source defines the mapping.
ctr_require_literal("${layout_header}" "${header}"
    "int MainArcadeLinkLayout_InputFromHostView(const struct NativeArcadeLinkHostView *view, struct MainArcadeLinkLayoutInput *input);")
foreach(pair
        "LAYOUT_MAX_HUMANS HOST_VIEW_MAX_HUMANS" "LAYOUT_MAX_BOTS HOST_VIEW_MAX_BOTS"
        "SELECT_ITEM_CHARACTER HOST_SELECT_ITEM_CHARACTER" "SELECT_ITEM_TRACK HOST_SELECT_ITEM_TRACK"
        "SELECT_ITEM_LAPS HOST_SELECT_ITEM_LAPS" "SELECT_ITEM_DONE HOST_SELECT_ITEM_DONE"
        "SELECT_LOCK_CHARACTER HOST_SELECT_LOCK_CHARACTER" "SELECT_LOCK_TRACK HOST_SELECT_LOCK_TRACK"
        "SELECT_LOCK_LAPS HOST_SELECT_LOCK_LAPS" "SELECT_STATUS_FAILED HOST_SELECT_STATUS_FAILED")
    string(REPLACE " " ";" pair_items "${pair}")
    list(GET pair_items 0 mirror)
    list(GET pair_items 1 host)
    ctr_require_literal("${layout_header}" "${header}"
        "_Static_assert(MAIN_ARCADE_LINK_${mirror} == NATIVE_ARCADE_LINK_${host},")
endforeach()
ctr_require_literal("game/MAIN/MainArcadeLinkLayout.c" "${layout_code}"
    "int MainArcadeLinkLayout_InputFromHostView(const struct NativeArcadeLinkHostView *view, struct MainArcadeLinkLayoutInput *input)")
