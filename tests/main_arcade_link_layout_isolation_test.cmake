# Structural isolation for the arcade-link screen layout builder
# (game/MAIN/MainArcadeLinkLayout.{c,h}, docs/GAME_LOOP_UI_MILESTONE.md
# section 2.4): a pure draw-list builder that never talks to the netplay
# adapter, the link, or its state, reads no game global, draws nothing, uses
# no heap and no stdio, and never names the topology lease. Its only includes
# are stdint.h, stddef.h, string.h, the arcade flow header, and its own
# header; the library links only ctr_native_arcade_flow; the target stays
# portable C17 with extensions off; and its mirrored retail font, colour, and
# justification values stay in step with include/namespace_Decal.h.

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

foreach(relative_path IN LISTS layout_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS netplay_tokens game_tokens alloc_tokens stdio_tokens lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 2. #include lines may only name stdint.h, stddef.h, string.h, the
    #    arcade flow header, or the layout's own header.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|<string\\.h>|\"platform/native_arcade_flow\\.h\"|\"MAIN/MainArcadeLinkLayout\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade link layout isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

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
