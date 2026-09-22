# Structural isolation for the real-socket UDP transport leaf
# (native_udp_transport): no lockstep, match-config, canonical-state, or
# topology-lease dependency, no dynamic allocation, no game dependency, and
# -- the mirror image of native_virtual_datagram_isolation_test.cmake's
# negative-only checks -- a positive assertion that this really is a
# real-socket module calling genuine Winsock symbols, plus the ws2_32 link
# and C17/no-extensions target checks.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "udp transport isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "udp transport isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "udp transport isolation: required token '${term}' missing from ${relative_path}")
    endif()
endfunction()

set(udp_transport_files
    "include/platform/native_udp_transport.h"
    "platform/native_udp_transport.c")

# a-f. no lockstep / match-config / canonical-state / topology-lease token,
# no dynamic allocation, no game dependency.
set(forbidden_tokens
    Lockstep lockstep LOCKSTEP
    MatchConfig NativeMatchConfigV1
    CanonicalState NativeCanonicalState
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner LOAD_Hub_ReadFile
    malloc calloc realloc "free(" alloca
    "game/" "Game_" "main.c")

foreach(relative_path IN LISTS udp_transport_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS forbidden_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# Positive assertion: this really is a real-socket module, the mirror image
# of the virtual-datagram harness forbidding these same tokens.
ctr_read_source("platform/native_udp_transport.c" udp_source)
foreach(term IN ITEMS "socket(" "sendto(" "recvfrom(" "WSAStartup")
    ctr_require("platform/native_udp_transport.c" "${udp_source}" "${term}")
endforeach()

ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_udp_transport)
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "udp transport isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()

string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*${target}[^)]*\\)" link_call "${cmake}")
if("${link_call}" STREQUAL "")
    message(FATAL_ERROR "udp transport isolation: missing target_link_libraries(${target} ...) in CMakeLists.txt")
endif()
string(FIND "${link_call}" "ws2_32" ws2_32_at)
if(ws2_32_at EQUAL -1)
    message(FATAL_ERROR "udp transport isolation: ${target} must link ws2_32")
endif()

string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "udp transport isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "udp transport isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "udp transport isolation: ${target} C17/no-extensions properties are out of order")
endif()
