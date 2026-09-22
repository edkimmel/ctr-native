# Structural isolation for the connect/handshake seam
# (native_lockstep_handshake): no OS-networking dependency, no topology-lease
# symbol, no dynamic allocation, no clock, game, or presentation dependency,
# the leaf library never links the virtual datagram test harness or the real
# UDP transport, the frozen wire constants cannot silently change, and the
# target stays portable C17 with extensions off.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "lockstep handshake isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "lockstep handshake isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "lockstep handshake isolation: required token '${term}' missing from ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "lockstep handshake isolation: required pattern '${pattern}' missing from ${relative_path}")
    endif()
endfunction()

# The handshake seam files.
set(handshake_files
    "include/platform/native_lockstep_handshake.h"
    "platform/native_lockstep_handshake.c")

# 1. No socket / OS-networking header or symbol.
set(network_tokens
    winsock WinSock WSA socket Socket AF_INET sockaddr htons htonl ntohs ntohl
    SDL_net SDL "<sys/" netinet getaddrinfo "select(" "poll(")

# 2. No topology-lease symbol.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 3. No dynamic allocation.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 4. No clock / game / presentation dependency.
set(dependency_tokens
    "time(" "clock(" QueryPerformance "game/" Game_ "main.c" native_renderer
    native_display_config native_frame_capture texture_filter)

foreach(relative_path IN LISTS handshake_files)
    ctr_read_source("${relative_path}" source)

    # native_lockstep_handshake.h documents, in prose, that the module "has no
    # socket, OS networking, clock, or game dependency" (wrapped across two
    # comment lines, "or" then "game dependency") -- that sentence is the
    # seam's own no-dependency guarantee, not a use of any of those things, so
    # it is neutralized before the network-token scan below. The replacement
    # stops right after "or" so it never has to span the line wrap, mirroring
    # the same technique tests/native_lockstep_isolation_test.cmake uses for
    # native_lockstep_session.h's near-identical sentence. The substitution is
    # a no-op on the .c file and does not touch the copy used by the other
    # three categories, so a real socket/SDL usage anywhere, including
    # elsewhere in this same file, still fails the scan.
    string(REPLACE "has no socket, OS networking, clock, or" "has no OS-networking, clock, or"
        network_scan "${source}")

    foreach(term IN LISTS network_tokens)
        ctr_forbid("${relative_path}" "${network_scan}" "${term}")
    endforeach()
    foreach(term IN LISTS lease_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # native_lockstep_handshake.h documents itself, in prose, as having "no
    # dynamic allocation" -- that phrase's own no-dynamic-allocation guarantee
    # contains "alloca" as a plain substring of "allocation", which is not a
    # use of the alloca() function, mirroring the same neutralization
    # tests/native_lockstep_failure_handling_isolation_test.cmake uses. The
    # substitution is a no-op on the .c file and does not touch the copy used
    # by any other category, so a real alloca()/malloc()/etc. call anywhere,
    # including elsewhere in this same file, still fails the scan.
    string(REPLACE "no dynamic allocation" "no dynamic memory use" alloc_scan "${source}")
    foreach(term IN LISTS alloc_tokens)
        ctr_forbid("${relative_path}" "${alloc_scan}" "${term}")
    endforeach()
    foreach(term IN LISTS dependency_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()

# 5. ctr_native_lockstep_handshake must not link the virtual-datagram test
#    harness or the real UDP transport; only the fault-injection test
#    executable (native_lockstep_handshake_fault_test) is allowed to link
#    ctr_native_virtual_datagram, and neither is one of the leaf library's own
#    dependencies.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_lockstep_handshake)
string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*${target}[^)]*\\)" link_call "${cmake}")
if("${link_call}" STREQUAL "")
    message(FATAL_ERROR "lockstep handshake isolation: missing target_link_libraries(${target} ...) in CMakeLists.txt")
endif()
foreach(forbidden IN ITEMS ctr_native_virtual_datagram ctr_native_udp_transport)
    string(FIND "${link_call}" "${forbidden}" leak)
    if(NOT leak EQUAL -1)
        message(FATAL_ERROR "lockstep handshake isolation: ${target} must not link ${forbidden}")
    endif()
endforeach()

# 6. The wire constants are frozen. A changed literal must fail this test,
#    not silently change the handshake protocol.
ctr_read_source("include/platform/native_lockstep_handshake.h" handshake_header)
ctr_require("include/platform/native_lockstep_handshake.h" "${handshake_header}"
    "#define NATIVE_LOCKSTEP_HANDSHAKE_V1_MAGIC UINT32_C(0x31484c4e)")
ctr_require("include/platform/native_lockstep_handshake.h" "${handshake_header}"
    "#define NATIVE_LOCKSTEP_HANDSHAKE_V1_VERSION UINT32_C(1)")
ctr_require_regex("include/platform/native_lockstep_handshake.h (ENCODED_BYTES must stay 284u)" "${handshake_header}"
    "#define NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES 284u[^0-9a-zA-Z_]")

# 7. C17, no extensions, on the handshake target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "lockstep handshake isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "lockstep handshake isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "lockstep handshake isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "lockstep handshake isolation: ${target} C17/no-extensions properties are out of order")
endif()
