# Structural isolation for the offline arcade-link capture check.
#
# - The library source and header include only their own header and a fixed
#   set of standard C headers: no game headers, no SDL, no platform runtime.
# - The library performs no dynamic allocation and no file I/O.
# - The library links nothing, and is linked only by its CLI and unit test;
#   ctr_native (and every other target) never links it.
# - Neither the runtime nor the unity chain references it.
set(root "${CMAKE_CURRENT_LIST_DIR}/..")
set(target ctr_native_capture_check)
set(allowed_consumers ctr_native_arcade_link_capture_check native_capture_check_test)

file(READ "${root}/CMakeLists.txt" cmake_text)

# Extract every target_link_libraries(...) call, including multi-line calls.
# Link arguments in this project contain no ')' so [^)]* spans one call.
function(collect_link_calls text output)
    string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" calls "${text}")
    set(${output} "${calls}" PARENT_SCOPE)
endfunction()

# Returns the consumer (first argument) and whether ${name} appears as a
# whole argument (not as a substring such as native_capture_check_test).
function(inspect_link_call call name consumer_out links_out)
    string(REGEX REPLACE "^target_link_libraries\\(" "" body "${call}")
    string(REGEX REPLACE "\\)$" "" body "${body}")
    string(REGEX REPLACE "[ \t\r\n]+" " " body "${body}")
    string(STRIP "${body}" body)
    string(REGEX MATCH "^[^ ]+" consumer "${body}")
    string(FIND " ${body} " " ${name} " offset)
    set(${consumer_out} "${consumer}" PARENT_SCOPE)
    if(offset EQUAL -1)
        set(${links_out} 0 PARENT_SCOPE)
    else()
        set(${links_out} 1 PARENT_SCOPE)
    endif()
endfunction()

string(FIND "${cmake_text}" "add_library(${target} STATIC platform/native_capture_check.c)" declaration)
if(declaration EQUAL -1)
    message(FATAL_ERROR "capture check library declaration is missing or changed")
endif()
string(FIND "${cmake_text}" "add_executable(ctr_native_arcade_link_capture_check tools/arcade_link_capture_check.c)" tool)
if(tool EQUAL -1)
    message(FATAL_ERROR "capture check CLI declaration is missing or changed")
endif()

# The source file is compiled only into its own library.
string(REGEX MATCHALL "native_capture_check\\.c" source_mentions "${cmake_text}")
list(LENGTH source_mentions source_mention_count)
if(NOT source_mention_count EQUAL 1)
    message(FATAL_ERROR "platform/native_capture_check.c must be listed exactly once (its own add_library)")
endif()

collect_link_calls("${cmake_text}" link_calls)
set(consumers "")
foreach(call IN LISTS link_calls)
    inspect_link_call("${call}" "${target}" consumer links_target)
    if(consumer STREQUAL "${target}")
        message(FATAL_ERROR "capture check library must not link anything: ${call}")
    endif()
    if(links_target)
        list(FIND allowed_consumers "${consumer}" allowed)
        if(allowed EQUAL -1)
            message(FATAL_ERROR "capture check library has forbidden consumer ${consumer}")
        endif()
        list(APPEND consumers "${consumer}")
    endif()
endforeach()
foreach(required IN LISTS allowed_consumers)
    list(FIND consumers "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "capture check consumer ${required} is missing")
    endif()
endforeach()

# Self-contract fixtures: a multi-line link from ctr_native is detected as a
# forbidden consumer, and a name-prefix match is not mistaken for a link.
collect_link_calls("target_link_libraries(ctr_native PRIVATE\n    SDL3::SDL3-static\n    ${target}\n)" fixture_calls)
list(GET fixture_calls 0 fixture_call)
inspect_link_call("${fixture_call}" "${target}" fixture_consumer fixture_links)
if(NOT fixture_consumer STREQUAL "ctr_native" OR NOT fixture_links)
    message(FATAL_ERROR "capture check multi-line consumer fixture was not detected")
endif()
inspect_link_call("target_link_libraries(ctr_native PRIVATE ${target}_extra)" "${target}" fixture_consumer fixture_links)
if(fixture_links)
    message(FATAL_ERROR "capture check prefix fixture was mistaken for a link")
endif()

# Includes: an explicit allowlist, checked on both the source and header.
set(allowed_includes
    "#include \"platform/native_capture_check.h\""
    "#include <stddef.h>"
    "#include <stdint.h>"
    "#include <string.h>")
foreach(file IN ITEMS platform/native_capture_check.c include/platform/native_capture_check.h)
    file(STRINGS "${root}/${file}" include_lines REGEX "^[ \t]*#[ \t]*include")
    if(include_lines STREQUAL "")
        message(FATAL_ERROR "capture check ${file} has no includes; the audit is not reading it")
    endif()
    foreach(line IN LISTS include_lines)
        string(STRIP "${line}" line)
        list(FIND allowed_includes "${line}" allowed)
        if(allowed EQUAL -1)
            message(FATAL_ERROR "capture check ${file} has a forbidden include: ${line}")
        endif()
    endforeach()

    file(READ "${root}/${file}" text)
    foreach(forbidden IN ITEMS SDL common.h game/ CTR_NATIVE malloc calloc realloc free\( fopen fread
            native_frame_capture native_gpu native_renderer native_platform)
        string(FIND "${text}" "${forbidden}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "capture check ${file} forbidden token ${forbidden}")
        endif()
    endforeach()
endforeach()

# Nothing in the runtime, the game unity chain or the other platform modules
# refers to the checker.
file(GLOB_RECURSE runtime_sources
    "${root}/main.c"
    "${root}/game/*.c" "${root}/game/*.h"
    "${root}/platform/*.c" "${root}/platform/*.h")
foreach(path IN LISTS runtime_sources)
    if(path MATCHES "/platform/native_capture_check\\.c$")
        continue()
    endif()
    file(READ "${path}" text)
    string(FIND "${text}" "native_capture_check" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "runtime source ${path} references the capture check")
    endif()
endforeach()
