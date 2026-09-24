# Structural isolation for the arcade-link host glue (native_arcade_link_host,
# docs/GAME_LOOP_UI_MILESTONE.md section 2.6): the game-facing singleton over
# the arcade-link host adapter. Its header is meant to be included by game
# code, so it names no lockstep, failure-handling, or lobby token and does
# not include the adapter header; only the .c does. Neither file names a
# topology-lease symbol, uses the heap or a clock, reaches an OS networking
# API or an engine source, writes replay, checkpoint, or canonical state, or
# reads the live identity itself (the caller supplies it). Both include only
# their allowed headers; the library links exactly the adapter and the host
# options, and stays portable C17 with extensions off. The host-side test
# read-back header (native_arcade_link_host_internal.h, MS-8) follows the
# header rules and is named by no game source or main.c, and the select
# view keeps the adapter's layout, field offset for field offset. Since
# MS-8b the header names the select-view and agreed-match values itself
# (NATIVE_ARCADE_LINK_HOST_SELECT_*, _ROLE_*, _MAX_*), each static-asserted
# in the .c against the module value it mirrors. Since RL-S6 the header
# forward-declares struct NativeMatchConfigV1 for the agreed-config copy
# without including or naming the match-config header, and pins the
# race-launch host API (agreed config, local race failure, racing query).

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
    "<string\\.h>|<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_link_host\\.h\"|\"platform/native_arcade_link_host_internal\\.h\"|\"platform/native_arcade_netplay\\.h\"|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_link_options\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_identity\\.h\"")

# 3b. The host-side test read-back header (MS-8): the same header-only and
#     category rules as the public header, it includes only stdint.h, and no
#     game source or main.c names it or its read-back.
set(internal_header "include/platform/native_arcade_link_host_internal.h")
ctr_read_source("${internal_header}" internal)
foreach(term IN LISTS header_only_tokens network_tokens clock_game_tokens alloc_tokens lease_tokens state_tokens)
    ctr_forbid("${internal_header}" "${internal}" "${term}")
endforeach()
ctr_check_includes("${internal_header}" "${internal}" "<stdint\\.h>")
file(GLOB_RECURSE internal_scan_paths "${repo}/game/*.c" "${repo}/game/*.h")
list(APPEND internal_scan_paths "${repo}/main.c")
foreach(path IN LISTS internal_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" scanned)
    foreach(term IN ITEMS native_arcade_link_host_internal NativeArcadeLinkHost_Internal)
        ctr_forbid("${relative_path}" "${scanned}" "${term}")
    endforeach()
endforeach()

# 3c. The select view (MS-8) mirrors the adapter's layout without naming
#     it in the header: the .c static-asserts the capacities and sizes, and
#     the public header declares the agreed-match read and the pure entropy
#     mix.
foreach(literal IN ITEMS
        "_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS,"
        "_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS,"
        "_Static_assert(sizeof(struct NativeArcadeLinkHostSelectView) == sizeof(struct NativeArcadeNetplaySelectView),"
        "_Static_assert(sizeof(struct NativeArcadeLinkHostSelectHumanView) == sizeof(struct NativeArcadeNetplaySelectHumanView),"
        "_Static_assert(NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()
foreach(literal IN ITEMS
        "struct NativeArcadeLinkHostSelectView select;"
        "int NativeArcadeLinkHost_GetAgreedMatch(struct NativeArcadeLinkHostMatch *out);"
        "uint64_t NativeArcadeLinkHost_MixSelectEntropy(uint64_t entropy, uint64_t epoch);")
    string(FIND "${header}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_header}")
    endif()
endforeach()

# 3d. The host names for the select view and agreed-match values (MS-8b):
#     each is defined exactly once, literally, in the header, and the .c
#     static-asserts it against the module value it mirrors. The .c also
#     checks every select-view field offset against the adapter's.
foreach(definition IN ITEMS
        "SELECT_ITEM_CHARACTER=0u" "SELECT_ITEM_TRACK=1u" "SELECT_ITEM_LAPS=2u" "SELECT_ITEM_DONE=3u"
        "SELECT_LOCK_CHARACTER=0x1u" "SELECT_LOCK_TRACK=0x2u" "SELECT_LOCK_LAPS=0x4u"
        "SELECT_STATUS_PICKING=0u" "SELECT_STATUS_WAITING=1u" "SELECT_STATUS_RESOLVED=2u"
        "SELECT_STATUS_CONFIRMED=3u" "SELECT_STATUS_FAILED=4u"
        "ROLE_INACTIVE=0u" "ROLE_CAB1=1u" "ROLE_CAB2=2u" "ROLE_BOT=3u"
        "MAX_HUMANS=4u" "MAX_BOTS=8u")
    string(REPLACE "=" ";" definition_parts "${definition}")
    list(GET definition_parts 0 name)
    list(GET definition_parts 1 literal)
    string(REGEX MATCHALL "#define NATIVE_ARCADE_LINK_HOST_${name}[ \t]" definitions "${header}")
    list(LENGTH definitions definition_count)
    if(NOT definition_count EQUAL 1)
        message(FATAL_ERROR "arcade link host isolation: NATIVE_ARCADE_LINK_HOST_${name} must be defined exactly once in ${host_header} (found ${definition_count})")
    endif()
    string(REGEX MATCH "#define NATIVE_ARCADE_LINK_HOST_${name}[ \t]+${literal}[ \t\r\n]" defined "${header}")
    if(defined STREQUAL "")
        message(FATAL_ERROR "arcade link host isolation: NATIVE_ARCADE_LINK_HOST_${name} must be defined literally as ${literal} in ${host_header}")
    endif()
    string(FIND "${source}" "_Static_assert(NATIVE_ARCADE_LINK_HOST_${name} ==" asserted_at)
    if(asserted_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${host_source} must static-assert NATIVE_ARCADE_LINK_HOST_${name} against its module value")
    endif()
endforeach()
foreach(literal IN ITEMS
        "_Static_assert(offsetof(struct hostStruct, field) == offsetof(struct netplayStruct, field),"
        "NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, currentItem);"
        "NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, peerLockedCharacterMask);"
        "NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humans);")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()

# 3e. The race-launch host API (docs/RACE_LAUNCH_MILESTONE.md RL-S6): the
#     header forward-declares struct NativeMatchConfigV1 exactly once and
#     never defines it or names the match-config header (the include
#     allowlist in 3 is unchanged), and it declares the agreed-config copy,
#     the local race-failure input, and the racing query. The .c copies the
#     agreed config byte for byte from the adapter and reports the failure
#     through the adapter's own latch.
#     (The match text holds a ';', so it is located with FIND, not counted
#     as a CMake list.)
string(FIND "${header}" "struct NativeMatchConfigV1;" forward_first)
string(FIND "${header}" "struct NativeMatchConfigV1;" forward_last REVERSE)
string(REGEX MATCH "(^|[\r\n])struct NativeMatchConfigV1;[ \t]*[\r\n]" forward_line "${header}")
if(forward_first EQUAL -1 OR NOT forward_first EQUAL forward_last OR forward_line STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must forward-declare 'struct NativeMatchConfigV1;' exactly once, on its own line")
endif()
string(REGEX MATCH "struct[ \t\r\n]+NativeMatchConfigV1[ \t\r\n]*\\{" definition "${header}")
if(NOT definition STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must not define struct NativeMatchConfigV1")
endif()
foreach(term IN ITEMS native_match_config NATIVE_MATCH_ NativeMatchConfigV1_)
    ctr_forbid("${host_header}" "${header}" "${term}")
endforeach()
foreach(literal IN ITEMS
        "int NativeArcadeLinkHost_GetAgreedConfig(struct NativeMatchConfigV1 *out);"
        "int NativeArcadeLinkHost_ReportRaceFailure(void);"
        "uint8_t NativeArcadeLinkHost_Racing(void);")
    string(FIND "${header}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_header}")
    endif()
endforeach()
foreach(literal IN ITEMS
        "agreed = NativeArcadeNetplay_AgreedConfig(&g_netplay);"
        "memcpy(out, agreed, sizeof(*out));"
        "return NativeArcadeNetplay_ReportLocalRaceFailure(&g_netplay);")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()

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
