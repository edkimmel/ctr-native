# Structural isolation for the match-select wire message codec
# (native_match_select_message, docs/MATCH_SELECT_MILESTONE.md section 2.2,
# MS-3). The module is a pure codec: no OS-networking, SDL, game, peer link,
# lobby, UDP, virtual datagram, topology-lease, canonical-state, or
# checkpoint dependency, no heap, clock, or stdio use, and no dependency on
# the frame-bundle/handshake stack. Its includes are limited to stddef.h,
# stdint.h, string.h, its own header, the binary codec header
# (platform/native_canonical_codec.h, which provides NativeCodecWriter,
# NativeCodecReader, and NativeCodecDigest64), and the match-select rules
# header; the library links exactly ctr_native_match_select_rules and
# ctr_native_canonical_codec; the target stays portable C17 with extensions
# off; ctr_native does not link it.
#
# The structural rule: the wire format is frozen. The header must define the
# magic 0x31534d4e ("NMS1"), version 1, 64 encoded bytes, and digest offset
# 56 literally, and the fault-cause enum values must keep their numbers
# (append-only), so a change to any of them fails this test.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "match select message isolation")

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

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

set(message_header "include/platform/native_match_select_message.h")
set(message_source "platform/native_match_select_message.c")
set(message_files "${message_header}" "${message_source}")
set(codec_include "#include \"platform/native_canonical_codec.h\"")

# 1. No frame-bundle/handshake stack, peer link, lobby, virtual datagram,
#    UDP, or netplay dependency.
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

# 6. No canonical-state, replay, or checkpoint state. The one allowed use of
#    the word is the binary codec include, which is removed before the scan.
set(state_tokens canonical Canonical CANONICAL Replay replay REPLAY Checkpoint checkpoint)

foreach(relative_path IN LISTS message_files)
    ctr_read_source("${relative_path}" source)
    string(REPLACE "${codec_include}" "" scanned "${source}")
    foreach(term IN LISTS link_tokens network_tokens dependency_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${scanned}" "${term}")
    endforeach()

    # 7. #include lines may only name the allowlisted headers.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_match_select_message\\.h\"|\"platform/native_canonical_codec\\.h\"|\"platform/native_match_select_rules\\.h\")[ \t]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 8. Frozen wire constants, literally in the header.
ctr_read_source("${message_header}" header)
foreach(definition IN ITEMS
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC UINT32_C(0x31534d4e)"
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION 1u"
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES 64u"
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET 56u"
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES 8u"
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED0_BYTES 4u"
    "#define NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED1_BYTES 8u")
    ctr_require("${message_header}" "${header}" "${definition}")
endforeach()
foreach(name IN ITEMS MAGIC VERSION ENCODED_BYTES DIGEST_OFFSET)
    string(REGEX MATCHALL "#define NATIVE_MATCH_SELECT_MESSAGE_V1_${name} " definitions "${header}")
    list(LENGTH definitions definition_count)
    if(NOT definition_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: NATIVE_MATCH_SELECT_MESSAGE_V1_${name} must be defined exactly once (found ${definition_count})")
    endif()
endforeach()

# 9. The fault-cause enum is append-only: every existing value keeps its
#    number.
set(fault_causes
    NONE BAD_SIZE BAD_MAGIC BAD_VERSION BAD_ENCODED_SIZE BAD_DIGEST BAD_RESERVED
    BAD_HUMAN_COUNT BAD_SENDER BAD_PHASE BAD_LOCK_MASK BAD_SEQUENCE BAD_CHARACTER
    BAD_TRACK BAD_LAPS BAD_ITEM BAD_RESOLVED_DIGEST)
set(value 0)
foreach(cause IN LISTS fault_causes)
    string(REGEX MATCHALL "NATIVE_MATCH_SELECT_MESSAGE_FAULT_${cause} = ${value}[,\r\n]" found "${header}")
    list(LENGTH found found_count)
    if(NOT found_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: NATIVE_MATCH_SELECT_MESSAGE_FAULT_${cause} must be declared exactly once as ${value} (append-only enum)")
    endif()
    math(EXPR value "${value} + 1")
endforeach()

ctr_read_source("CMakeLists.txt" cmake)

# 10. ctr_native_match_select_message links exactly
#     ctr_native_match_select_rules and ctr_native_canonical_codec, in exactly
#     one target_link_libraries call.
set(target ctr_native_match_select_message)
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
if(NOT "${link_items}" STREQUAL "ctr_native_canonical_codec;ctr_native_match_select_rules")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_match_select_rules and ctr_native_canonical_codec (found '${link_items}')")
endif()

# 11. C17, no extensions, on the message target, in order.
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

# 12. The game executable does not link the codec (not yet integrated).
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*ctr_native[ \t\r\n][^)]*\\)" game_link_calls "${cmake}")
list(LENGTH game_link_calls game_link_call_count)
if(game_link_call_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: found no target_link_libraries(ctr_native ...) call; the link scan cannot run")
endif()
foreach(game_link_call IN LISTS game_link_calls)
    string(FIND "${game_link_call}" "${target}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: ctr_native must not link ${target}")
    endif()
endforeach()
