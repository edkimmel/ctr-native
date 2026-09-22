# Structural isolation for the arcade-link host glue (native_arcade_link_host,
# docs/GAME_LOOP_UI_MILESTONE.md section 2.6): the game-facing singleton over
# the arcade-link host adapter. Its header is meant to be included by game
# code, so it names no lockstep, failure-handling, or lobby token and does
# not include the adapter header; only the .c does. Neither file names a
# topology-lease symbol, uses the heap or a clock, reaches an OS networking
# API or an engine source, writes replay, checkpoint, or canonical state, or
# reads the live identity itself (the caller supplies it). Both include only
# their allowed headers; the library links exactly the adapter and the host
# options, and stays portable C17 with extensions off.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link host isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

set(host_header "include/platform/native_arcade_link_host.h")
set(host_source "platform/native_arcade_link_host.c")

# 1. Header-only forbidden tokens: game code includes this header, so it
#    names nothing the lockstep and failure-handling isolation rules forbid
#    under the engine sources, and nothing of the adapter or lobby layer.
set(header_only_tokens
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeArcadeNetplay NativeLobby native_lobby udp_transport)

ctr_read_source("${host_header}" header)
foreach(term IN LISTS header_only_tokens)
    ctr_forbid("${host_header}" "${header}" "${term}")
endforeach()

# 2. Tokens forbidden in both files, by category.
set(network_tokens
    winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo "select(" "poll(" SDL)
set(clock_game_tokens "time(" "clock(" QueryPerformance "game/" Game_)
set(alloc_tokens malloc calloc realloc "free(" alloca)
set(lease_tokens TopologyLease Acquire Activate Publish Retire LOAD_Hub_ReadFile)
set(state_tokens NativeReplay Checkpoint checkpoint NativeCanonical NativeIdentity_Get)

ctr_read_source("${host_source}" source)
foreach(relative_path IN ITEMS "${host_header}" "${host_source}")
    if(relative_path STREQUAL host_header)
        set(text "${header}")
    else()
        set(text "${source}")
    endif()
    foreach(term IN LISTS network_tokens clock_game_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${text}" "${term}")
    endforeach()
endforeach()

# 3. #include allowlists. The header: stdint.h and the three game-safe
#    platform headers. The .c: string.h, stdint.h, stddef.h, its own header,
#    the adapter header, and the flow, options, menu-input, and identity
#    headers.
function(ctr_check_includes relative_path text allowed_pattern)
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${text}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "arcade link host isolation: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(${allowed_pattern})[ \t]*$")
            message(FATAL_ERROR "arcade link host isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endfunction()

ctr_check_includes("${host_header}" "${header}"
    "<stdint\\.h>|\"platform/native_arcade_link_options\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_identity\\.h\"")
ctr_check_includes("${host_source}" "${source}"
    "<string\\.h>|<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_link_host\\.h\"|\"platform/native_arcade_netplay\\.h\"|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_link_options\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_identity\\.h\"")

# 4. ctr_native_arcade_link_host links exactly the adapter and the host
#    options, in exactly one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_link_host)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link host isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
set(expected_link_items ctr_native_arcade_netplay ctr_native_arcade_link_options)
foreach(expected IN LISTS expected_link_items)
    list(FIND link_items "${expected}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${target} must link ${expected} (found '${link_items}')")
    endif()
endforeach()
foreach(item IN LISTS link_items)
    list(FIND expected_link_items "${item}" item_index)
    if(item_index EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${target} links unexpected item '${item}'; only ctr_native_arcade_netplay and ctr_native_arcade_link_options are allowed")
    endif()
endforeach()
list(LENGTH link_items link_item_count)
if(NOT link_item_count EQUAL 2)
    message(FATAL_ERROR "arcade link host isolation: ${target} must link exactly two libraries, found ${link_item_count} ('${link_items}')")
endif()

# 5. C17, no extensions, in order, on the host glue target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade link host isolation: ${target} C17/no-extensions properties are out of order")
endif()
