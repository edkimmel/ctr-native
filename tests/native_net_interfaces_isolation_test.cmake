# Structural isolation for the IPv4 interface list (native_net_interfaces,
# docs/DISCOVERY_MILESTONE.md DISC-5, DISC-14, DISC-S3).
# 1. The OS adapter-list API (iphlpapi, GetAdaptersAddresses and its
#    relatives, the IP_ADAPTER_ types) is named by platform/native_net_interfaces.c
#    and by no other file of game/, platform/, include/, or main.c (the
#    header included), comments included. The scan is first proven against a
#    planted hit.
# 2. Includes: the header only <stdint.h>; the source its own header,
#    native_win32.h, <winsock2.h>, <iphlpapi.h>, <stddef.h>, and <stdint.h>.
# 3. No game/, topology lease, replay, checkpoint, canonical, match config,
#    lockstep, clock, or host token in either file (code, comments stripped).
# 4. No heap: no C allocator and no Win32 allocator. GetAdaptersAddresses is
#    called exactly once, on the caller's scratch at its full size
#    (sizeof(scratch->words)); a list that needs more is a failure, never a
#    heap retry.
# 5. ctr_native_net_interfaces links exactly iphlpapi, and no other target
#    links iphlpapi; C17, extensions off; the unit test and this test are
#    registered, without a live label.
# 6. Only the discovery service (and this module) names the module in
#    game/, platform/, include/, and main.c.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "net interfaces isolation")

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

# Sets out_var to the first term of the list named by list_var found in text
# (case-insensitive), or to "".
function(ctr_find_any text list_var out_var)
    string(TOLOWER "${text}" lower)
    set(found "")
    foreach(term IN LISTS ${list_var})
        string(TOLOWER "${term}" lower_term)
        string(FIND "${lower}" "${lower_term}" hit)
        if(NOT hit EQUAL -1)
            set(found "${term}")
            break()
        endif()
    endforeach()
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

set(header "include/platform/native_net_interfaces.h")
set(source_file "platform/native_net_interfaces.c")

file(GLOB_RECURSE scan_files LIST_DIRECTORIES false
    "${repo}/game/*.c" "${repo}/game/*.h"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
list(LENGTH scan_files scanned)
if(scanned LESS 100)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()

# 1. The adapter-list API is confined to the source.
set(adapter_terms iphlpapi GetAdaptersAddresses GetAdaptersInfo IP_ADAPTER_ GetIpAddrTable GetIfTable GetUnicastIpAddressTable
    GetInterfaceInfo NotifyAddrChange NotifyIpInterfaceChange)
ctr_find_any("x = GetAdaptersAddresses(AF_INET, 0, NULL, list, &size);" adapter_terms planted)
if(NOT planted STREQUAL "GetAdaptersAddresses")
    message(FATAL_ERROR "${prefix}: the adapter-list matcher missed a planted call; the scan is broken")
endif()
ctr_find_any("#include <IPHLPAPI.h>" adapter_terms planted)
if(NOT planted STREQUAL "iphlpapi")
    message(FATAL_ERROR "${prefix}: the adapter-list matcher missed a planted include; the scan is broken")
endif()
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL source_file)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    ctr_find_any("${scanned_source}" adapter_terms hit)
    if(NOT hit STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} names '${hit}'; only ${source_file} may use the OS adapter list")
    endif()
endforeach()
ctr_read_source("${source_file}" source)
ctr_require("${source_file}" "${source}" "#include <iphlpapi.h>")
ctr_require("${source_file}" "${source}" "GetAdaptersAddresses(")

# 2. Includes.
ctr_read_source("${header}" header_source)
foreach(pair IN ITEMS "header" "source")
    if(pair STREQUAL "header")
        set(relative_path "${header}")
        set(text "${header_source}")
        set(allowed "<stdint\\.h>")
    else()
        set(relative_path "${source_file}")
        set(text "${source}")
        set(allowed "<stddef\\.h>|<stdint\\.h>|<winsock2\\.h>|<iphlpapi\\.h>|\"platform/native_net_interfaces\\.h\"|\"platform/native_win32\\.h\"")
    endif()
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${text}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include in ${relative_path}; the include scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(${allowed})[ \t\r]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 3-4. Code tokens, comments stripped.
set(state_tokens "game/" Game_ MatchConfig match_config NativeMatch Lockstep lockstep LOCKSTEP Canonical canonical CANONICAL
    Replay replay REPLAY Checkpoint checkpoint CHECKPOINT)
set(lease_tokens Lease lease LEASE Topology topology TOPOLOGY LOAD_Hub_ReadFile Publish Activate Acquire Retire)
set(clock_tokens "time(" "clock(" QueryPerformance GetTickCount timeGetTime GetSystemTime)
set(host_tokens Platform_ SDL stdio "FILE *" fopen getenv Netplay netplay Lobby lobby Discovery discovery)
set(heap_tokens malloc calloc realloc "free(" alloca HeapAlloc HeapFree LocalAlloc GlobalAlloc VirtualAlloc CoTaskMem MALLOC FREE)
foreach(relative_path IN ITEMS "${header}" "${source_file}")
    ctr_read_source("${relative_path}" text)
    ctr_strip_comments("${text}" code)
    foreach(term IN LISTS state_tokens lease_tokens clock_tokens host_tokens heap_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
endforeach()
ctr_strip_comments("${source}" source_code)
string(REGEX MATCHALL "GetAdaptersAddresses\\(" gaa_calls "${source_code}")
list(LENGTH gaa_calls gaa_count)
if(NOT gaa_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${source_file} must call GetAdaptersAddresses exactly once (found ${gaa_count})")
endif()
ctr_require("${source_file}" "${source_code}" "size = (ULONG)sizeof(scratch->words);")
ctr_require("${source_file}" "${source_code}"
    "GetAdaptersAddresses(AF_INET, flags, NULL, (IP_ADAPTER_ADDRESSES *)(void *)scratch->words, &size)")
ctr_strip_comments("${header_source}" header_code)
ctr_require("${header}" "${header_code}" "uint64_t words[NATIVE_NET_INTERFACES_SCRATCH_WORDS];")

# 5. Link, target properties, and registration.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_net_interfaces)
ctr_require("CMakeLists.txt" "${cmake}" "add_library(${target} STATIC platform/native_net_interfaces.c)")
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
if(NOT "${link_items}" STREQUAL "iphlpapi")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly iphlpapi (found '${link_items}')")
endif()
string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" all_link_calls "${cmake}")
set(iphlpapi_links 0)
foreach(call IN LISTS all_link_calls)
    string(TOLOWER "${call}" lower_call)
    string(FIND "${lower_call}" "iphlpapi" hit)
    if(NOT hit EQUAL -1)
        math(EXPR iphlpapi_links "${iphlpapi_links} + 1")
    endif()
endforeach()
if(NOT iphlpapi_links EQUAL 1)
    message(FATAL_ERROR "${prefix}: exactly one target_link_libraries call may name iphlpapi, ${target}'s (found ${iphlpapi_links})")
endif()
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
ctr_require_order("CMakeLists.txt (${target})" "${target_block}"
    "set_target_properties(${target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF"
    "target_include_directories(${target} PUBLIC \${CMAKE_SOURCE_DIR}/include)")
ctr_require("CMakeLists.txt" "${cmake}" "add_test(NAME native_net_interfaces_unit COMMAND $<TARGET_FILE:native_net_interfaces_test>)")
ctr_require("CMakeLists.txt" "${cmake}" "tests/native_net_interfaces_isolation_test.cmake")
ctr_forbid("CMakeLists.txt" "${cmake}" "set_tests_properties(native_net_interfaces")

# 6. Consumers: the discovery service only.
set(module_terms native_net_interfaces NativeNetInterface NATIVE_NET_INTERFACES)
set(allowed_consumers "${header}" "${source_file}"
    "include/platform/native_arcade_discovery_service.h" "platform/native_arcade_discovery_service.c")
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    list(FIND allowed_consumers "${relative_path}" allowed_at)
    if(NOT allowed_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    ctr_find_any("${scanned_source}" module_terms hit)
    if(NOT hit STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} names '${hit}'; only the discovery service may use the interface list")
    endif()
endforeach()
