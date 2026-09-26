# ctr_native identifies driver behaviour by function-pointer identity
# (MainCanonicalDrivers.c TokenFor and its RegistrySelfTest, plus retail
# function-pointer comparisons).  Identical COMDAT folding gives distinct
# functions with identical bodies one address, so the MSVC link must disable
# it explicitly (the Release default enables it) and nothing may re-enable it.
# The same holds for every test executable that compiles MainCanonicalDrivers.c.
#
# Rules (matched case-insensitively, with CMake comments stripped):
#   - ctr_native and each such test target pass /OPT:NOICF unconditionally in a
#     target_link_options call (not inside a generator expression);
#   - no /OPT:NOICF anywhere sits inside a generator expression;
#   - no [-/]OPT: option list names ICF other than as NOICF, and no --icf,
#     in CMakeLists.txt, cmake/*.cmake, CMakePresets.json, or
#     CMakeUserPresets.json (when present).
cmake_minimum_required(VERSION 3.20)
set(root "${CMAKE_CURRENT_LIST_DIR}/..")

# Remove bracket comments (#[[ ]], #[=[ ]=], ...) and then line comments.
function(ctr_identity_strip_comments in out)
    set(text "${in}")
    while(TRUE)
        string(REGEX MATCH "#\\[=*\\[" open "${text}")
        if(NOT open)
            break()
        endif()
        string(FIND "${text}" "${open}" start)
        string(LENGTH "${open}" open_length)
        string(REGEX REPLACE "[^=]" "" equals "${open}")
        string(SUBSTRING "${text}" 0 ${start} head)
        math(EXPR body_start "${start} + ${open_length}")
        string(SUBSTRING "${text}" ${body_start} -1 rest)
        string(FIND "${rest}" "]${equals}]" close)
        if(close EQUAL -1)
            set(tail "")
        else()
            string(LENGTH "]${equals}]" close_length)
            math(EXPR tail_start "${close} + ${close_length}")
            string(SUBSTRING "${rest}" ${tail_start} -1 tail)
        endif()
        set(text "${head} ${tail}")
    endwhile()
    string(REGEX REPLACE "#[^\n]*" "" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Remove generator expressions, innermost first.
function(ctr_identity_strip_genex in out)
    set(text "${in}")
    while(TRUE)
        string(REGEX REPLACE "\\$<[^<>]*>" "" next "${text}")
        if(next STREQUAL text)
            break()
        endif()
        set(text "${next}")
    endwhile()
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

file(READ "${root}/CMakeLists.txt" cmake_raw)
ctr_identity_strip_comments("${cmake_raw}" cmake_text)
string(TOUPPER "${cmake_text}" cmake_upper)

set(scan_names "CMakeLists.txt")
set(scan_text_CMakeLists.txt "${cmake_upper}")
file(GLOB module_files RELATIVE "${root}" "${root}/cmake/*.cmake")
foreach(module IN LISTS module_files)
    file(READ "${root}/${module}" module_raw)
    ctr_identity_strip_comments("${module_raw}" module_text)
    string(TOUPPER "${module_text}" scan_text_${module})
    list(APPEND scan_names "${module}")
endforeach()
foreach(preset_file IN ITEMS CMakePresets.json CMakeUserPresets.json)
    if(EXISTS "${root}/${preset_file}")
        file(READ "${root}/${preset_file}" preset_raw)
        string(TOUPPER "${preset_raw}" scan_text_${preset_file})
        list(APPEND scan_names "${preset_file}")
    endif()
endforeach()

# Targets that must link with /OPT:NOICF: ctr_native, and each executable
# that compiles MainCanonicalDrivers.c (directly or by #include from its test
# source).
set(identity_targets CTR_NATIVE)
string(REGEX MATCHALL "ADD_EXECUTABLE\\([ \t\r\n]*[A-Z0-9_]+[ \t\r\n][^)]*MAINCANONICALDRIVERS\\.C[^)]*\\)" direct_blocks "${cmake_upper}")
foreach(block IN LISTS direct_blocks)
    string(REGEX REPLACE "^ADD_EXECUTABLE\\([ \t\r\n]*([A-Z0-9_]+)[ \t\r\n].*$" "\\1" target "${block}")
    list(APPEND identity_targets "${target}")
endforeach()
file(GLOB test_sources RELATIVE "${root}/tests" "${root}/tests/*.c")
foreach(source IN LISTS test_sources)
    file(STRINGS "${root}/tests/${source}" includes_drivers
        REGEX "^[ \t]*#[ \t]*include[ \t]+\"[^\"]*MainCanonicalDrivers\\.c\"")
    if(NOT includes_drivers)
        continue()
    endif()
    string(TOUPPER "${source}" source_upper)
    string(REPLACE "." "\\." source_pattern "${source_upper}")
    string(REGEX MATCH "ADD_EXECUTABLE\\([ \t\r\n]*([A-Z0-9_]+)[ \t\r\n][^)]*TESTS/${source_pattern}" test_block "${cmake_upper}")
    if(NOT test_block)
        message(FATAL_ERROR "function identity: tests/${source} includes MainCanonicalDrivers.c but no add_executable builds it")
    endif()
    list(APPEND identity_targets "${CMAKE_MATCH_1}")
endforeach()
list(REMOVE_DUPLICATES identity_targets)
list(LENGTH identity_targets identity_target_count)
if(identity_target_count LESS 2)
    message(FATAL_ERROR "function identity: found no test executable that compiles MainCanonicalDrivers.c (detection broken?)")
endif()

foreach(target IN LISTS identity_targets)
    string(REGEX MATCHALL "TARGET_LINK_OPTIONS\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_blocks "${cmake_upper}")
    set(unconditional_noicf FALSE)
    foreach(block IN LISTS link_blocks)
        ctr_identity_strip_genex("${block}" plain_block)
        if(plain_block MATCHES "[ \t\r\n\"][-/]OPT:([A-Z0-9=]+,)*NOICF[ \t\r\n\",)]")
            set(unconditional_noicf TRUE)
        endif()
    endforeach()
    if(NOT unconditional_noicf)
        string(TOLOWER "${target}" target_lower)
        message(FATAL_ERROR "function identity: ${target_lower} MSVC link must pass /OPT:NOICF unconditionally (not commented out, not inside a generator expression)")
    endif()
endforeach()

foreach(name IN LISTS scan_names)
    set(upper_text "${scan_text_${name}}")

    # A generator expression around NOICF keeps the fix in some configurations
    # only; the stripped text must hold every NOICF the full text holds.
    ctr_identity_strip_genex("${upper_text}" plain_text)
    string(REGEX MATCHALL "NOICF" all_noicf "${upper_text}")
    string(REGEX MATCHALL "NOICF" plain_noicf "${plain_text}")
    list(LENGTH all_noicf all_noicf_count)
    list(LENGTH plain_noicf plain_noicf_count)
    if(NOT all_noicf_count EQUAL plain_noicf_count)
        message(FATAL_ERROR "function identity: /OPT:NOICF inside a generator expression in ${name}")
    endif()

    # /OPT:ICF, /OPT:REF,ICF, -opt:icf=2, ...: any OPT item naming ICF other
    # than NOICF re-enables folding.
    string(REGEX MATCHALL "[-/]OPT:[^ \t\r\n\"';)>]*" opt_lists "${upper_text}")
    foreach(opt_list IN LISTS opt_lists)
        string(REGEX REPLACE "^[-/]OPT:" "" opt_items "${opt_list}")
        string(REPLACE "," ";" opt_items "${opt_items}")
        foreach(opt_item IN LISTS opt_items)
            if("${opt_item}" MATCHES "ICF" AND NOT "${opt_item}" STREQUAL "NOICF")
                message(FATAL_ERROR "function identity: forbidden identical-code-folding option ${opt_list} in ${name}")
            endif()
        endforeach()
    endforeach()

    string(FIND "${upper_text}" "--ICF" gnu_icf_offset)
    if(NOT gnu_icf_offset EQUAL -1)
        message(FATAL_ERROR "function identity: forbidden identical-code-folding flag --icf in ${name}")
    endif()
endforeach()
