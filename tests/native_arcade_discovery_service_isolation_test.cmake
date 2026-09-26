# Structural isolation for the arcade-link discovery socket service
# (native_arcade_discovery_service, docs/DISCOVERY_MILESTONE.md DISC-14,
# slice DISC-S3).
# 1. Its includes are only stddef.h, stdint.h, string.h, its own header, the
#    UDP transport, the interface list, and the discovery core headers.
# 2. Its code (comments stripped) names no match config, lockstep, canonical,
#    replay, checkpoint, topology lease, game/, clock, heap, host, netplay,
#    or lobby token, and no raw socket or adapter-list API: it reaches the
#    network only through the transport and the interface list.
# 3. Its public API is pinned, and no name holds Publish, Activate, Acquire,
#    or Retire (the link host, which calls it from DISC-S4, forbids them,
#    tests/native_arcade_link_host_isolation_test.cmake).
# 4. ctr_native_arcade_discovery_service links exactly the transport, the
#    interface list, and the core; C17 with extensions off; its unit test and
#    this test are registered, without a live label.
# 5. No other game/, platform/, include/, or main.c file names the service
#    yet (the link host joins in DISC-S4).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade discovery service isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
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

function(ctr_require_order relative_path source)
    set(remaining "${source}")
    foreach(term IN LISTS ARGN)
        string(FIND "${remaining}" "${term}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${prefix}: '${term}' is missing or out of order in ${relative_path}")
        endif()
        string(LENGTH "${term}" term_length)
        math(EXPR next "${position} + ${term_length}")
        string(SUBSTRING "${remaining}" ${next} -1 remaining)
    endforeach()
endfunction()

set(service_header "include/platform/native_arcade_discovery_service.h")
set(service_source "platform/native_arcade_discovery_service.c")
set(service_files "${service_header}" "${service_source}")

# 1-2. Includes and code tokens.
set(heap_tokens malloc calloc realloc "free(" alloca "new(" HeapAlloc LocalAlloc GlobalAlloc VirtualAlloc)
set(clock_tokens "time(" "clock(" time.h QueryPerformance GetTickCount timeGetTime GetSystemTime chrono Sleep)
set(raw_network_tokens winsock WinSock Winsock WSA sockaddr "socket(" "bind(" "sendto(" "recvfrom(" "setsockopt(" htons htonl ntohs ntohl
    "inet_" getaddrinfo iphlpapi Iphlpapi GetAdaptersAddresses GetAdaptersInfo IP_ADAPTER_ native_win32 windows.h)
set(state_tokens MatchConfig match_config NativeMatch Lockstep lockstep LOCKSTEP Canonical canonical CANONICAL Replay replay REPLAY
    Checkpoint checkpoint CHECKPOINT)
set(lease_tokens Lease lease LEASE Topology topology TOPOLOGY LOAD_Hub_ReadFile Publish Activate Acquire Retire)
set(host_tokens "game/" Game_ Platform_ SDL stdio "FILE *" fopen getenv Netplay netplay Lobby lobby NativeArcadeLink native_arcade_link
    NativeIdentity NativeSha256)
foreach(relative_path IN LISTS service_files)
    ctr_read_source("${relative_path}" source)
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include in ${relative_path}; the include scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_discovery_service\\.h\"|\"platform/native_arcade_discovery\\.h\"|\"platform/native_net_interfaces\\.h\"|\"platform/native_udp_transport\\.h\")[ \t\r]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    ctr_strip_comments("${source}" code)
    foreach(term IN LISTS heap_tokens clock_tokens raw_network_tokens state_tokens lease_tokens host_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
endforeach()
ctr_read_source("${service_source}" source)
ctr_require("${service_source}" "${source}" "#include \"platform/native_arcade_discovery_service.h\"")
# The service really drives the core and the transport.
ctr_strip_comments("${source}" source_code)
foreach(term IN ITEMS "NativeUdpTransport_Open(" "NativeUdpTransport_EnableBroadcast(" "NativeUdpTransport_Receive(" "NativeUdpTransport_Send("
        "NativeUdpTransport_Close(" "NativeArcadeDiscovery_Receive(" "NativeArcadeDiscovery_Tick(" "NativeArcadeDiscovery_BuildBeacon("
        "NativeNetInterfaces_List(" "NativeNetInterfaces_BuildTargets(")
    ctr_require("${service_source}" "${source_code}" "${term}")
endforeach()

# 3. The public API, pinned.
ctr_read_source("${service_header}" header)
ctr_strip_comments("${header}" header_code)
string(REGEX MATCHALL "NativeArcadeDiscoveryService_[A-Za-z0-9_]*\\(" api_calls "${header_code}")
list(REMOVE_DUPLICATES api_calls)
list(SORT api_calls)
set(expected_api
    "NativeArcadeDiscoveryService_Close(" "NativeArcadeDiscoveryService_GetStatus(" "NativeArcadeDiscoveryService_Open("
    "NativeArcadeDiscoveryService_Pairing(" "NativeArcadeDiscoveryService_TakeEvent(" "NativeArcadeDiscoveryService_Target("
    "NativeArcadeDiscoveryService_Tick(")
if(NOT "${api_calls}" STREQUAL "${expected_api}")
    message(FATAL_ERROR "${prefix}: the public API changed; expected '${expected_api}', found '${api_calls}'")
endif()
foreach(call IN LISTS api_calls)
    foreach(term IN ITEMS Publish Activate Acquire Retire)
        string(FIND "${call}" "${term}" hit)
        if(NOT hit EQUAL -1)
            message(FATAL_ERROR "${prefix}: API name '${call}' contains '${term}'")
        endif()
    endforeach()
endforeach()

# 4. Link, target properties, and registration.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_discovery_service)
ctr_require("CMakeLists.txt" "${cmake}" "add_library(${target} STATIC platform/native_arcade_discovery_service.c)")
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
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_discovery;ctr_native_net_interfaces;ctr_native_udp_transport")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_udp_transport, ctr_native_net_interfaces, and ctr_native_arcade_discovery (found '${link_items}')")
endif()
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
ctr_require_order("CMakeLists.txt (${target})" "${target_block}"
    "set_target_properties(${target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF"
    "target_include_directories(${target} PUBLIC \${CMAKE_SOURCE_DIR}/include)")
ctr_require("CMakeLists.txt" "${cmake}" "add_test(NAME native_arcade_discovery_service_unit COMMAND $<TARGET_FILE:native_arcade_discovery_service_test>)")
ctr_require("CMakeLists.txt" "${cmake}" "tests/native_arcade_discovery_service_isolation_test.cmake")
ctr_forbid("CMakeLists.txt" "${cmake}" "set_tests_properties(native_arcade_discovery_service")

# 5. No other source names the service yet.
file(GLOB_RECURSE scan_files LIST_DIRECTORIES false
    "${repo}/game/*.c" "${repo}/game/*.h"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
list(LENGTH scan_files scanned)
if(scanned LESS 100)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    list(FIND service_files "${relative_path}" own_at)
    if(NOT own_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    string(TOLOWER "${scanned_source}" lower)
    foreach(term IN ITEMS native_arcade_discovery_service nativearcadediscoveryservice)
        string(FIND "${lower}" "${term}" hit)
        if(NOT hit EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${relative_path} names '${term}'; no module may use the discovery service before DISC-S4")
        endif()
    endforeach()
endforeach()
