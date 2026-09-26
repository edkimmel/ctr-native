# Structural isolation for the arcade-link discovery pure core
# (native_arcade_discovery, docs/DISCOVERY_MILESTONE.md DISC-S2 and DISC-14):
# the beacon codec, group hash, election, and caller-owned peer table.
# 1. Its includes are only stddef.h, stdint.h, string.h, and its own header.
# 2. Its code names no heap, clock, socket/Winsock/interface-list, match
#    config, lockstep, canonical, replay, checkpoint, topology lease, or
#    game/ token; no API name holds Publish, Activate, Acquire, or Retire
#    (the link host, which calls it from S4, forbids them,
#    tests/native_arcade_link_host_isolation_test.cmake).
# 3. The beacon goes to and from the wire byte by byte: memmove is never
#    used and every memcpy is one of a fixed list (magic, identity, and the
#    finished byte buffer), never a struct image.
# 4. ctr_native_arcade_discovery links nothing and is C17 with extensions
#    off; its unit test and this test are registered, without a live label.
# 5. Replay, checkpoint, canonical, and lease code never names the module:
#    platform/native_replay*, native_checkpoint*, native_canonical*, their
#    include/platform headers, and game/MAIN/MainCanonical* (the topology
#    lease files included). Every glob must match files, so the scan cannot
#    pass vacuously, and the scan is first proven against a planted hit.
# 6. No other game/, platform/, include/, or main.c file names the module
#    yet (S3 and S4 extend this allow-list for the service and link host).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade discovery isolation")

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

# Sets out_var to the first module name found in text (case-insensitive,
# comments included), or to "" when there is none.
set(module_names native_arcade_discovery nativearcadediscovery)
function(ctr_find_module_name text out_var)
    string(TOLOWER "${text}" lower)
    set(found "")
    foreach(term IN LISTS module_names)
        string(FIND "${lower}" "${term}" hit)
        if(NOT hit EQUAL -1)
            set(found "${term}")
            break()
        endif()
    endforeach()
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

set(core_header "include/platform/native_arcade_discovery.h")
set(core_source "platform/native_arcade_discovery.c")
set(core_files "${core_header}" "${core_source}")

# 1-2. Includes and code tokens.
set(heap_tokens malloc calloc realloc "free(" alloca "new(")
set(clock_tokens "time(" "clock(" time.h QueryPerformance GetTickCount timeGetTime GetSystemTime chrono)
set(network_tokens socket Socket winsock WinSock Winsock WSA sockaddr "bind(" "sendto(" "recvfrom(" htons htonl ntohs ntohl "inet_" getaddrinfo
    iphlpapi Iphlpapi GetAdaptersAddresses GetAdaptersInfo udp_transport UdpTransport net_interfaces NetInterfaces)
set(state_tokens MatchConfig match_config NativeMatch Lockstep lockstep LOCKSTEP Canonical canonical CANONICAL Replay replay REPLAY
    Checkpoint checkpoint CHECKPOINT)
set(lease_tokens Lease lease LEASE Topology topology TOPOLOGY LOAD_Hub_ReadFile Publish Activate Acquire Retire)
set(host_tokens "game/" Game_ Platform_ SDL stdio "FILE *" fopen getenv Netplay netplay Lobby lobby)
foreach(relative_path IN LISTS core_files)
    ctr_read_source("${relative_path}" source)
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: found no #include in ${relative_path}; the include scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_discovery\\.h\")[ \t\r]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    ctr_strip_comments("${source}" code)
    foreach(term IN LISTS heap_tokens clock_tokens network_tokens state_tokens lease_tokens host_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
endforeach()
ctr_read_source("${core_source}" source)
ctr_require("${core_source}" "${source}" "#include \"platform/native_arcade_discovery.h\"")
# The API the host will call, pinned so a rename cannot slip in a lease word.
ctr_read_source("${core_header}" header)
ctr_strip_comments("${header}" header_code)
string(REGEX MATCHALL "NativeArcadeDiscovery_[A-Za-z0-9_]*\\(" api_calls "${header_code}")
list(REMOVE_DUPLICATES api_calls)
list(SORT api_calls)
set(expected_api
    "NativeArcadeDiscovery_BuildBeacon(" "NativeArcadeDiscovery_Decode(" "NativeArcadeDiscovery_Elect(" "NativeArcadeDiscovery_Encode("
    "NativeArcadeDiscovery_GroupHash(" "NativeArcadeDiscovery_GroupNameValid(" "NativeArcadeDiscovery_Init(" "NativeArcadeDiscovery_Pairing("
    "NativeArcadeDiscovery_Receive(" "NativeArcadeDiscovery_TakeEvent(" "NativeArcadeDiscovery_Tick(")
if(NOT "${api_calls}" STREQUAL "${expected_api}")
    message(FATAL_ERROR "${prefix}: the public API changed; expected '${expected_api}', found '${api_calls}'")
endif()

# 3. Byte-by-byte wire packing.
ctr_strip_comments("${source}" code)
ctr_forbid("${core_source}" "${code}" "memmove")
# (Matched up to, not including, the ';', which would split the CMake list.)
string(REGEX MATCHALL "memcpy\\([^;]*" memcpy_calls "${code}")
list(LENGTH memcpy_calls memcpy_count)
if(memcpy_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: found no memcpy in ${core_source}; the wire-packing scan is broken")
endif()
set(allowed_memcpy
    "memcpy(&bytes[DISCOVERY_OFFSET_MAGIC], s_magic, sizeof(s_magic))"
    "memcpy(&bytes[DISCOVERY_OFFSET_IDENTITY], beacon->identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES)"
    "memcpy(out, bytes, sizeof(bytes))"
    "memcpy(beacon.identity, &bytes[DISCOVERY_OFFSET_IDENTITY], NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES)"
    "memcpy(table->identity, identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES)"
    "memcpy(beacon.identity, table->identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES)")
foreach(call IN LISTS memcpy_calls)
    list(FIND allowed_memcpy "${call}" allowed_at)
    if(allowed_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${core_source} has an unlisted memcpy '${call}'; pack the wire field by field")
    endif()
endforeach()
ctr_require("${core_source}" "${code}" "static void PutU64(")
ctr_require("${core_source}" "${code}" "static uint64_t GetU64(")

# 4. The library links nothing; C17, no extensions, in order; tests registered.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_discovery)
ctr_require("CMakeLists.txt" "${cmake}" "add_library(${target} STATIC platform/native_arcade_discovery.c)")
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: ${target} must link nothing (found ${link_calls})")
endif()
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
ctr_require_order("CMakeLists.txt (${target})" "${target_block}"
    "set_target_properties(${target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF"
    "target_include_directories(${target} PUBLIC \${CMAKE_SOURCE_DIR}/include)")
ctr_require("CMakeLists.txt" "${cmake}" "add_test(NAME native_arcade_discovery_unit COMMAND $<TARGET_FILE:native_arcade_discovery_test>)")
ctr_require("CMakeLists.txt" "${cmake}" "tests/native_arcade_discovery_isolation_test.cmake")
ctr_forbid("CMakeLists.txt" "${cmake}" "set_tests_properties(native_arcade_discovery")

# 5. Replay, checkpoint, canonical, and lease code never names the module.
#    First prove the matcher finds a planted name (so a broken matcher
#    cannot pass), then require every glob to match at least one file.
ctr_find_module_name("x = NativeArcadeDiscovery_Tick(t);" planted_hit)
if(NOT planted_hit STREQUAL "nativearcadediscovery")
    message(FATAL_ERROR "${prefix}: the module-name matcher missed a planted name; the scan is broken")
endif()
ctr_find_module_name("#include \"platform/NATIVE_ARCADE_DISCOVERY.h\"" planted_hit)
if(NOT planted_hit STREQUAL "native_arcade_discovery")
    message(FATAL_ERROR "${prefix}: the module-name matcher missed a planted include; the scan is broken")
endif()
ctr_find_module_name("native_arcade_link_host" planted_hit)
if(NOT planted_hit STREQUAL "")
    message(FATAL_ERROR "${prefix}: the module-name matcher hit a clean text; the scan is broken")
endif()
set(state_globs
    "platform/native_replay*" "platform/native_checkpoint*" "platform/native_canonical*"
    "include/platform/native_replay*" "include/platform/native_checkpoint*" "include/platform/native_canonical*"
    "game/MAIN/MainCanonical*")
set(state_files "")
foreach(pattern IN LISTS state_globs)
    file(GLOB matched LIST_DIRECTORIES false "${repo}/${pattern}")
    list(LENGTH matched matched_count)
    if(matched_count EQUAL 0)
        message(FATAL_ERROR "${prefix}: the glob ${pattern} matches no file; the scan would pass vacuously")
    endif()
    list(APPEND state_files ${matched})
endforeach()
foreach(required IN ITEMS "platform/native_checkpoint.c" "platform/native_replay_v4.c" "platform/native_canonical_state_v4.c"
        "game/MAIN/MainCanonicalTopologyLeaseAuthority.c" "game/MAIN/MainCanonicalTopologyLeaseRuntime.c"
        "game/MAIN/MainCanonicalTopologyLeaseAdapter.c")
    list(FIND state_files "${repo}/${required}" required_at)
    if(required_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: the replay/checkpoint/canonical/lease scan misses ${required}")
    endif()
endforeach()
foreach(path IN LISTS state_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" scanned_source)
    ctr_find_module_name("${scanned_source}" hit)
    if(NOT hit STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} names '${hit}'; replay, checkpoint, canonical, and lease code must not name discovery")
    endif()
endforeach()

# 6. No other source names the module yet.
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
    if(relative_path STREQUAL core_header OR relative_path STREQUAL core_source)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    ctr_find_module_name("${scanned_source}" hit)
    if(NOT hit STREQUAL "")
        message(FATAL_ERROR "${prefix}: ${relative_path} names '${hit}'; no module may use discovery before its slice allows it")
    endif()
endforeach()
