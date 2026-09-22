# Structural isolation for the arcade menu input seam
# (native_arcade_menu_input): a pure input classifier with no OS-networking,
# topology-lease, heap, clock, game, presentation, or netplay dependency. Its
# only includes are stdint.h, stddef.h, and its own header; the library is a
# leaf with no link dependencies; the G29-relevant button bits and the Back
# mask cannot silently change; and the target stays portable C17 with
# extensions off.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade menu input isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade menu input isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "arcade menu input isolation: required token '${term}' missing from ${relative_path}")
    endif()
endfunction()

# The menu input seam files.
set(menu_input_header "include/platform/native_arcade_menu_input.h")
set(menu_input_files
    "${menu_input_header}"
    "platform/native_arcade_menu_input.c")

# 1. No socket / OS-networking header or symbol.
set(network_tokens
    winsock WinSock WSA socket Socket AF_INET sockaddr htons htonl ntohs ntohl
    SDL_net SDL "<sys/" netinet getaddrinfo "select(" "poll(")

# 2. No topology-lease symbol.
set(lease_tokens
    TopologyLease topology_lease LeaseAuthority LeaseRuntime LeaseOwner
    Acquire Activate Publish Retire LOAD_Hub_ReadFile)

# 3. No heap use.
set(alloc_tokens malloc calloc realloc "free(" alloca)

# 4. No clock / game / presentation dependency.
set(dependency_tokens
    "time(" "clock(" QueryPerformance "game/" Game_ "main.c" native_renderer
    native_display_config native_frame_capture texture_filter)

# 5. No netplay, lobby, or deterministic-state dependency: menu input is
#    classified locally and never touches the link or its state.
set(netplay_tokens
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeLobby native_lobby udp_transport canonical replay)

foreach(relative_path IN LISTS menu_input_files)
    ctr_read_source("${relative_path}" source)
    foreach(term IN LISTS network_tokens lease_tokens alloc_tokens dependency_tokens netplay_tokens)
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()

    # 6. #include lines may only name stdint.h, stddef.h, or the module's
    #    own header.
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_menu_input\\.h\")[ \t]*$")
            message(FATAL_ERROR "arcade menu input isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endforeach()

# 7. ctr_native_arcade_menu_input is a leaf: no target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_menu_input)
string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n)]" link_call "${cmake}")
if(NOT "${link_call}" STREQUAL "")
    message(FATAL_ERROR "arcade menu input isolation: ${target} must not have a target_link_libraries call (it is a leaf)")
endif()

# 8. C17, no extensions, on the menu input target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade menu input isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade menu input isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade menu input isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade menu input isolation: ${target} C17/no-extensions properties are out of order")
endif()

# 9. The G29-relevant button bits are frozen (Cross is the throttle, Square
#    the brake, R1 the left paddle), and Back must never include Square.
ctr_read_source("${menu_input_header}" header)
ctr_require("${menu_input_header}" "${header}"
    "#define NATIVE_ARCADE_MENU_BUTTON_CROSS (UINT32_C(1) << 4)")
ctr_require("${menu_input_header}" "${header}"
    "#define NATIVE_ARCADE_MENU_BUTTON_SQUARE (UINT32_C(1) << 6)")
ctr_require("${menu_input_header}" "${header}"
    "#define NATIVE_ARCADE_MENU_BUTTON_TRIANGLE (UINT32_C(1) << 7)")
ctr_require("${menu_input_header}" "${header}"
    "#define NATIVE_ARCADE_MENU_BUTTON_R1 (UINT32_C(1) << 10)")
string(REGEX MATCH "#define NATIVE_ARCADE_MENU_BACK_MASK[^\r\n]*" back_line "${header}")
if("${back_line}" STREQUAL "")
    message(FATAL_ERROR "arcade menu input isolation: missing NATIVE_ARCADE_MENU_BACK_MASK define in ${menu_input_header}")
endif()
string(FIND "${back_line}" "SQUARE" square_at)
if(NOT square_at EQUAL -1)
    message(FATAL_ERROR "arcade menu input isolation: NATIVE_ARCADE_MENU_BACK_MASK must not include SQUARE (Square is the G29 brake)")
endif()
