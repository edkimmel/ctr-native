# Structural isolation for the arcade bot rules (native_arcade_bot_rules,
# docs/ROSTER_MILESTONE.md sections 3.1 and 3.3, R-2). The module is pure:
# no OS-networking, SDL, game, lockstep, peer link, lobby, virtual datagram,
# topology or lease, canonical-state, replay, or checkpoint dependency, no
# heap, clock, or stdio use. Its includes are limited to stddef.h, stdint.h,
# string.h, its own header, and the match-config, SHA-256, canonical-codec,
# deterministic-RNG, and match-select-rules headers; the library links
# exactly ctr_native_match_config, ctr_native_sha256,
# ctr_native_canonical_codec, ctr_native_deterministic_rng, and
# ctr_native_match_select_rules; the target stays portable C17 with
# extensions off.
#
# The structural rules: the module mirrors retail. The difficulty table (the
# module initializer and the EASY/MEDIUM/HARD defines) must equal the
# game/230/D230.c `.cupDifficulty` `.speed` initializer, in order; the
# advRng fallback defines must equal the two constant assignments to
# advRng.state0 and advRng.state1 in game/BOTS.c, which retail makes only
# when both words are 0. Both sides are parsed here, so a change to either
# fails this test. The V1 tag, version, and encoded size are frozen: a change
# is a new bot-rules version.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade bot rules isolation")

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

function(ctr_require_equal description actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${prefix}: ${description}: module '${actual}' != retail '${expected}'")
    endif()
endfunction()

set(rules_header "include/platform/native_arcade_bot_rules.h")
set(rules_source "platform/native_arcade_bot_rules.c")
set(rules_files "${rules_header}" "${rules_source}")

# 1. No lockstep, peer link, lobby, virtual datagram, or netplay dependency.
set(link_tokens
    lockstep Lockstep LOCKSTEP PeerLink peer_link NativeLobby native_lobby Lobby lobby
    VirtualDatagram virtual_datagram udp_transport UdpTransport ArcadeNetplay arcade_netplay)

# 2. No socket / OS-networking header or symbol, and no SDL.
set(network_tokens
    winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo
    inet_ "poll(" SDL)

# 3. No clock, stdio, or game-code dependency. (FILE is matched as a type,
#    since NATIVE_MATCH_CONFIG_V1_PROFILE_* contains the letters.)
set(dependency_tokens "time(" "clock(" QueryPerformance stdio printf fopen "FILE *" "FILE*" Game_ "gGT" "sdata"
    "include \"game" "namespace_")

# 4. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 5. No topology or lease symbol.
set(lease_tokens Topology topology TopologyLease Lease lease Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 6. No canonical-state, replay, or checkpoint state. (The canonical codec
#    header is allowed; canonical state is not.)
set(state_tokens NativeCanonical native_canonical_state CanonicalState canonical_state
    NativeReplay native_replay Replay replay Checkpoint checkpoint)

foreach(relative_path IN LISTS rules_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS link_tokens network_tokens dependency_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 7. #include lines may only name the allowlisted headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_bot_rules\\.h\"|\"platform/native_match_config\\.h\"|\"platform/native_sha256\\.h\"|\"platform/native_canonical_codec\\.h\"|\"platform/native_deterministic_rng\\.h\"|\"platform/native_match_select_rules\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 8. ctr_native_arcade_bot_rules links exactly the five pure libraries, in
#    exactly one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_bot_rules)
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
set(expected_links
    ctr_native_canonical_codec ctr_native_deterministic_rng ctr_native_match_config ctr_native_match_select_rules
    ctr_native_sha256)
list(SORT expected_links)
if(NOT "${link_items}" STREQUAL "${expected_links}")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly '${expected_links}' (found '${link_items}')")
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

# 10. Module side of the mirrors.
ctr_read_source("${rules_source}" module_source)
ctr_read_source("${rules_header}" module_header)

# Reads `#define <name> <integer>` (decimal or 0x hex, optional u suffix)
# from the header, which must define it exactly once.
function(ctr_header_define name out_var)
    string(REGEX MATCHALL "#define ${name} [^\r\n]*" definitions "${module_header}")
    list(LENGTH definitions definition_count)
    if(NOT definition_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: #define ${name} must appear exactly once in ${rules_header} (found ${definition_count})")
    endif()
    string(REGEX MATCH "#define ${name} (0[xX][0-9a-fA-F]+|[0-9]+)[uU]?([ \t]|/|$)" found "${definitions}")
    if(found STREQUAL "")
        message(FATAL_ERROR "${prefix}: #define ${name} is not an integer literal in ${rules_header}")
    endif()
    ctr_parse_numbers("${name}" "${CMAKE_MATCH_1}" value)
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# The module difficulty table: one simple one-level initializer, declared once.
string(REGEX MATCHALL "static const uint8_t k_arcadeBotRulesDifficulty[^a-zA-Z0-9_]" declarations "${module_source}")
list(LENGTH declarations declaration_count)
if(NOT declaration_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: k_arcadeBotRulesDifficulty must be declared exactly once in the module (found ${declaration_count})")
endif()
string(REGEX MATCH "static const uint8_t k_arcadeBotRulesDifficulty\\[[A-Za-z0-9_ *]*\\][ \t]*=[ \t]*\\{([^{}]*)\\}" table "${module_source}")
if(table STREQUAL "")
    message(FATAL_ERROR "${prefix}: k_arcadeBotRulesDifficulty is not a simple one-level initializer")
endif()
ctr_parse_numbers("module k_arcadeBotRulesDifficulty" "${CMAKE_MATCH_1}" module_difficulty)

ctr_header_define(NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT header_difficulty_count)
ctr_header_define(NATIVE_ARCADE_BOT_RULES_DIFFICULTY_EASY header_easy)
ctr_header_define(NATIVE_ARCADE_BOT_RULES_DIFFICULTY_MEDIUM header_medium)
ctr_header_define(NATIVE_ARCADE_BOT_RULES_DIFFICULTY_HARD header_hard)
ctr_header_define(NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0 header_fallback0)
ctr_header_define(NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1 header_fallback1)

list(LENGTH module_difficulty length)
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_DIFFICULTY_COUNT vs the module table" "${header_difficulty_count}" "${length}")
ctr_require_equal("EASY/MEDIUM/HARD defines vs the module table" "${header_easy};${header_medium};${header_hard}"
    "${module_difficulty}")

# 11. Difficulty: the game/230/D230.c `.cupDifficulty` initializer, exactly
#     once, and its `.speed` row.
ctr_read_source("game/230/D230.c" d230)
string(REGEX MATCHALL "\\.cupDifficulty[ \t]*=" occurrences "${d230}")
list(LENGTH occurrences occurrence_count)
if(NOT occurrence_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: .cupDifficulty must be initialized exactly once in game/230/D230.c (found ${occurrence_count})")
endif()
string(REGEX MATCH "\\.cupDifficulty[ \t]*=[ \t\r\n]*\\{([^{}]*(\\{[^{}]*\\}[^{}]*)*)\\}" cup_block "${d230}")
if(cup_block STREQUAL "")
    message(FATAL_ERROR "${prefix}: could not parse .cupDifficulty in game/230/D230.c")
endif()
string(REGEX REPLACE "//[^\r\n]*" "" cup_block "${cup_block}")
string(REGEX MATCHALL "\\.speed[ \t]*=" occurrences "${cup_block}")
list(LENGTH occurrences occurrence_count)
if(NOT occurrence_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: .cupDifficulty must initialize .speed exactly once in game/230/D230.c (found ${occurrence_count})")
endif()
string(REGEX MATCH "\\.speed[ \t]*=[ \t\r\n]*\\{([^{}]*)\\}" found "${cup_block}")
if(found STREQUAL "")
    message(FATAL_ERROR "${prefix}: could not parse .cupDifficulty.speed in game/230/D230.c")
endif()
ctr_parse_numbers("game/230/D230.c .cupDifficulty.speed" "${CMAKE_MATCH_1}" retail_speed)
ctr_require_equal("difficulty table vs .cupDifficulty.speed" "${module_difficulty}" "${retail_speed}")
ctr_require_equal("difficulty table" "${module_difficulty}" "80;160;240")

# 12. advRng fallback: game/BOTS.c assigns one constant to each advRng word,
#     exactly once each, under the both-words-zero condition.
ctr_read_source("game/BOTS.c" bots)
string(REGEX MATCHALL "advRng\\.state0[ \t]*==[ \t]*0[ \t]*\\)[ \t]*&&[ \t]*\\([^()]*advRng\\.state1[ \t]*==[ \t]*0" guards "${bots}")
list(LENGTH guards guard_count)
if(NOT guard_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: expected exactly one both-words-zero advRng guard in game/BOTS.c (found ${guard_count})")
endif()
foreach(word IN ITEMS 0 1)
    # No trailing ';' in the pattern: it would split the CMake list.
    string(REGEX MATCHALL "advRng\\.state${word}[ \t]*=[ \t]*(0[xX][0-9a-fA-F]+|[0-9]+)[uU]?" assignments "${bots}")
    list(LENGTH assignments assignment_count)
    if(NOT assignment_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: expected exactly one constant assignment to advRng.state${word} in game/BOTS.c (found ${assignment_count})")
    endif()
    string(REGEX MATCH "=[ \t]*(0[xX][0-9a-fA-F]+|[0-9]+)" found "${assignments}")
    ctr_parse_numbers("game/BOTS.c advRng.state${word}" "${CMAKE_MATCH_1}" retail_state${word})
endforeach()
# The guard must precede both assignments.
string(FIND "${bots}" "${guards}" guard_at)
string(REGEX MATCH "advRng\\.state0[ \t]*=[ \t]*(0[xX][0-9a-fA-F]+|[0-9]+)" assignment0 "${bots}")
string(REGEX MATCH "advRng\\.state1[ \t]*=[ \t]*(0[xX][0-9a-fA-F]+|[0-9]+)" assignment1 "${bots}")
string(FIND "${bots}" "${assignment0}" assignment0_at)
string(FIND "${bots}" "${assignment1}" assignment1_at)
if(NOT (guard_at LESS assignment0_at AND guard_at LESS assignment1_at))
    message(FATAL_ERROR "${prefix}: the advRng constant assignments in game/BOTS.c must follow the both-words-zero guard")
endif()
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0 vs advRng.state0" "${header_fallback0}" "${retail_state0}")
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1 vs advRng.state1" "${header_fallback1}" "${retail_state1}")
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK0" "${header_fallback0}" "807490560")
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_ADV_RNG_FALLBACK1" "${header_fallback1}" "1228243966")

# 13. Frozen V1 identity: the tag, the version, and the encoded size. The tag
#     literal appears only in the header's define.
string(REGEX MATCHALL "#define NATIVE_ARCADE_BOT_RULES_V1_TAG [^\r\n]*" tag_defines "${module_header}")
list(LENGTH tag_defines tag_define_count)
if(NOT tag_define_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: NATIVE_ARCADE_BOT_RULES_V1_TAG must be defined exactly once (found ${tag_define_count})")
endif()
if(NOT tag_defines MATCHES "^#define NATIVE_ARCADE_BOT_RULES_V1_TAG \"CTRN arcade bot rules v1\"[ \t]*$")
    message(FATAL_ERROR "${prefix}: NATIVE_ARCADE_BOT_RULES_V1_TAG is frozen as \"CTRN arcade bot rules v1\" (found '${tag_defines}')")
endif()
string(FIND "${module_source}" "\"CTRN arcade bot rules" source_tag_at)
if(NOT source_tag_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: the module must take its tag from NATIVE_ARCADE_BOT_RULES_V1_TAG, not a literal")
endif()
ctr_header_define(NATIVE_ARCADE_BOT_RULES_V1_VERSION header_version)
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_V1_VERSION" "${header_version}" "1")
ctr_header_define(NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES header_encoded_bytes)
ctr_require_equal("NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES" "${header_encoded_bytes}" "111")
