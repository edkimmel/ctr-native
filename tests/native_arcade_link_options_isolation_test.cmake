# Structural isolation for the arcade-link host options and fixed fixture
# (native_arcade_link_options, docs/GAME_LOOP_UI_MILESTONE.md section 2.5,
# Task 6a): pure argv parsing and a pure fixture builder. No OS-networking,
# netplay, topology-lease, heap, clock, game, or deterministic-state
# dependency, and the identity arrives from the caller (the module never
# fetches it). Its includes are limited to stddef.h, stdint.h, string.h, its
# own header, and the identity, match-config, and SHA-256 headers; the
# library links exactly ctr_native_match_config and ctr_native_sha256; the
# target stays portable C17 with extensions off; and the fixture values
# (UX-8) cannot silently change.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link options isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link options isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require_single relative_path description source pattern)
    string(REGEX MATCHALL "${pattern}" matches "${source}")
    list(LENGTH matches match_count)
    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR "arcade link options isolation: ${description} must appear exactly once in ${relative_path} (found ${match_count})")
    endif()
endfunction()

set(options_header "include/platform/native_arcade_link_options.h")
set(options_files
    "${options_header}"
    "platform/native_arcade_link_options.c")

# 1. No netplay or lobby dependency: the module only describes the link.
set(netplay_tokens
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport)

# 2. No socket / OS-networking header or symbol.
set(network_tokens
    winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo
    inet_ "select(" "poll(" SDL)

# 3. No clock or game dependency.
set(dependency_tokens "time(" "clock(" QueryPerformance "game/" Game_)

# 4. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 5. No topology-lease symbol.
set(lease_tokens TopologyLease Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 6. No replay or checkpoint state, and the identity is never fetched here.
set(state_tokens NativeReplay Checkpoint checkpoint NativeIdentity_Get)

foreach(relative_path IN LISTS options_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS netplay_tokens network_tokens dependency_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 7. #include lines may only name the allowlisted headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_link_options\\.h\"|\"platform/native_identity\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_sha256\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade link options isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 8. ctr_native_arcade_link_options links exactly ctr_native_match_config and
#    ctr_native_sha256, in exactly one target_link_libraries call. In
#    particular it never links ctr_native_identity.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_link_options)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link options isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
list(SORT link_items)
if(NOT "${link_items}" STREQUAL "ctr_native_match_config;ctr_native_sha256")
    message(FATAL_ERROR "arcade link options isolation: ${target} must link exactly ctr_native_match_config and ctr_native_sha256 (found '${link_items}')")
endif()

# 9. C17, no extensions, on the options target, in order.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade link options isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade link options isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade link options isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade link options isolation: ${target} C17/no-extensions properties are out of order")
endif()

# 10. The fixture (UX-8) is frozen: each define appears once with its value.
ctr_read_source("${options_header}" header)
ctr_require_single("${options_header}" "TRACK_ID 3u" "${header}"
    "#define NATIVE_ARCADE_LINK_FIXTURE_TRACK_ID 3u[^0-9a-zA-Z_]")
ctr_require_single("${options_header}" "LAP_COUNT 3u" "${header}"
    "#define NATIVE_ARCADE_LINK_FIXTURE_LAP_COUNT 3u[^0-9a-zA-Z_]")
ctr_require_single("${options_header}" "TICK_RATE_NUMERATOR 30u" "${header}"
    "#define NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_NUMERATOR 30u[^0-9a-zA-Z_]")
ctr_require_single("${options_header}" "TICK_RATE_DENOMINATOR 1u" "${header}"
    "#define NATIVE_ARCADE_LINK_FIXTURE_TICK_RATE_DENOMINATOR 1u[^0-9a-zA-Z_]")
ctr_require_single("${options_header}" "MASTER_SEED UINT64_C(0x4354524e41524331)" "${header}"
    "#define NATIVE_ARCADE_LINK_FIXTURE_MASTER_SEED UINT64_C\\(0x4354524e41524331\\)[^0-9a-zA-Z_]")
ctr_require_single("${options_header}" "BOT_RULES_TEXT" "${header}"
    "#define NATIVE_ARCADE_LINK_FIXTURE_BOT_RULES_TEXT \"CTRN arcade-link fixture bot rules v1\"[\r\n]")

# Each fixture define name is defined exactly once, so a second, conflicting
# definition cannot slip in beside the frozen one.
foreach(name TRACK_ID LAP_COUNT TICK_RATE_NUMERATOR TICK_RATE_DENOMINATOR MASTER_SEED BOT_RULES_TEXT)
    ctr_require_single("${options_header}" "${name} definition" "${header}"
        "#[ \t]*define[ \t]+NATIVE_ARCADE_LINK_FIXTURE_${name}[^0-9a-zA-Z_]")
endforeach()
