# Structural isolation for the arcade-link host options and fixed fixture
# (native_arcade_link_options, docs/GAME_LOOP_UI_MILESTONE.md section 2.5,
# Task 6a): pure argv parsing and a pure fixture builder. No OS-networking,
# netplay, topology-lease, heap, clock, game, or deterministic-state
# dependency, and the identity arrives from the caller (the module never
# fetches it). Its includes are limited to stddef.h, stdint.h, string.h, its
# own header, and the arcade-bot-rules, identity, and match-config headers;
# the library links exactly ctr_native_arcade_bot_rules and
# ctr_native_match_config (no SHA-256 of its own); the target stays portable
# C17 with extensions off; the fixture values (UX-8) cannot silently change;
# and the fixture's botRulesDigest comes from the real bot rules (R-3).

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
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_link_options\\.h\"|\"platform/native_arcade_bot_rules\\.h\"|\"platform/native_identity\\.h\"|\"platform/native_match_config\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade link options isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 8. ctr_native_arcade_link_options links exactly ctr_native_arcade_bot_rules
#    and ctr_native_match_config, in exactly one target_link_libraries call.
#    It never links ctr_native_identity, and never ctr_native_sha256: the
#    module hashes nothing itself (section 13; the bot rules link it).
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
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_bot_rules;ctr_native_match_config")
    message(FATAL_ERROR "arcade link options isolation: ${target} must link exactly ctr_native_arcade_bot_rules and ctr_native_match_config (found '${link_items}')")
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

# Each fixture define name is defined exactly once, so a second, conflicting
# definition cannot slip in beside the frozen one.
foreach(name TRACK_ID LAP_COUNT TICK_RATE_NUMERATOR TICK_RATE_DENOMINATOR MASTER_SEED)
    ctr_require_single("${options_header}" "${name} definition" "${header}"
        "#[ \t]*define[ \t]+NATIVE_ARCADE_LINK_FIXTURE_${name}[^0-9a-zA-Z_]")
endforeach()

# 11. The preview enum is append-only: every value is pinned, with its
#     option name in the trailing comment, and the name table in the .c
#     lists the names in enum order (the five select previews, MS-8, follow
#     the original twelve).
set(preview_pins
    "NONE 0 none" "TITLE 1 title" "LOBBY_WAITING 2 lobby" "LOBBY_CONNECTING 3 lobby-connecting"
    "LOBBY_REJECTED 4 lobby-rejected" "MATCH_FOUND 5 match-found" "RESULTS_FINISHED 6 results"
    "RESULTS_PEER_TIMEOUT 7 results-timeout" "RESULTS_DESYNC 8 results-desync"
    "RESULTS_LINK_ERROR 9 results-link-error" "REMATCH_WAIT 10 rematch" "EXIT 11 exit"
    "EXIT_OPPONENT_LEFT 12 exit-opponent-left" "SELECT_CHARACTER 13 select-character"
    "SELECT_TRACK 14 select-track" "SELECT_LAPS 15 select-laps" "SELECT_WAIT 16 select-wait"
    "SELECT_RESULT 17 select-result")
ctr_read_source("platform/native_arcade_link_options.c" options_source)
string(FIND "${options_source}" "k_previewNames[NATIVE_ARCADE_LINK_PREVIEW_LAST + 1u] = {" names_at)
if(names_at EQUAL -1)
    message(FATAL_ERROR "arcade link options isolation: missing the k_previewNames table in platform/native_arcade_link_options.c")
endif()
string(SUBSTRING "${options_source}" ${names_at} -1 names_tail)
string(FIND "${names_tail}" "};" names_end)
string(SUBSTRING "${names_tail}" 0 ${names_end} names_table)
set(expected_names "")
foreach(pin IN LISTS preview_pins)
    string(REPLACE " " ";" pin_items "${pin}")
    list(GET pin_items 0 pin_name)
    list(GET pin_items 1 pin_value)
    list(GET pin_items 2 pin_option)
    ctr_require_single("${options_header}" "PREVIEW_${pin_name} = ${pin_value}" "${header}"
        "NATIVE_ARCADE_LINK_PREVIEW_${pin_name} = ${pin_value},?[ \t]*/\\* \"${pin_option}\"")
    list(APPEND expected_names "${pin_option}")
endforeach()
string(REGEX MATCHALL "\"[^\"]*\"" table_names "${names_table}")
string(REPLACE "\"" "" table_names "${table_names}")
if(NOT "${table_names}" STREQUAL "${expected_names}")
    message(FATAL_ERROR "arcade link options isolation: k_previewNames must list '${expected_names}' in enum order (found '${table_names}')")
endif()
ctr_require_single("platform/native_arcade_link_options.c" "PREVIEW_LAST definition" "${options_source}"
    "#define NATIVE_ARCADE_LINK_PREVIEW_LAST NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT[\r\n]")

# 12. selectEntropy is host-local and never parsed: the options source never
#     names it (SetDefaults zeroes it with the rest of the struct), and the
#     header declares it once.
ctr_forbid("platform/native_arcade_link_options.c" "${options_source}" "selectEntropy")
# (The pattern stops before the ';', which a CMake list would split on.)
ctr_require_single("${options_header}" "the selectEntropy field" "${header}" "uint64_t selectEntropy")

# 13. The fixture is built on the real bot rules (R-3): the builder takes
#     botRulesDigest from NativeArcadeBotRules_DigestV1, exactly once, and
#     checks the result with NativeArcadeBotRules_ValidateConfigV1. No
#     placeholder remains: no BOT_RULES_TEXT define or use, no placeholder
#     text, and no hashing of its own (the module never calls NativeSha256_).
#     The fixture needs only DigestV1, ExpectedBots2P, ValidateConfigV1, and
#     the default difficulty: it draws no RNG, derives or maps no retail
#     seeds, and encodes nothing itself (no NativeDeterministicRng,
#     NativeArcadeBotRules_DeriveRetailSeeds, NativeArcadeBotRules_MapRetailSeeds,
#     or NativeCodec token).
string(FIND "${options_source}" "int NativeArcadeLinkFixture_Build(" build_at)
if(build_at EQUAL -1)
    message(FATAL_ERROR "arcade link options isolation: missing NativeArcadeLinkFixture_Build in platform/native_arcade_link_options.c")
endif()
string(SUBSTRING "${options_source}" ${build_at} -1 build_body)
ctr_require_single("platform/native_arcade_link_options.c" "NativeArcadeBotRules_DigestV1(candidate.botRulesDigest)"
    "${build_body}" "NativeArcadeBotRules_DigestV1\\([ \t]*candidate\\.botRulesDigest[ \t]*\\)")
ctr_require_single("platform/native_arcade_link_options.c" "NativeArcadeBotRules_DigestV1" "${options_source}"
    "NativeArcadeBotRules_DigestV1")
ctr_require_single("platform/native_arcade_link_options.c" "NativeArcadeBotRules_ValidateConfigV1(&candidate)"
    "${build_body}" "NativeArcadeBotRules_ValidateConfigV1\\([ \t]*&candidate[ \t]*\\)")
# botRulesDigest is written nowhere else in the module.
ctr_require_single("platform/native_arcade_link_options.c" "botRulesDigest" "${options_source}" "botRulesDigest")
foreach(relative_path IN LISTS options_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN ITEMS BOT_RULES_TEXT "arcade-link fixture bot rules" placeholder NativeSha256_
            NativeDeterministicRng NativeArcadeBotRules_DeriveRetailSeeds NativeArcadeBotRules_MapRetailSeeds NativeCodec)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()
