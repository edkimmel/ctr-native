# Structural isolation for MainCanonicalDrivers_ExtractRosterInput and its
# pre-race variant MainCanonicalDrivers_ExtractRosterInputPreRace
# (game/MAIN/MainCanonicalDrivers.{c,h}, docs/ROSTER_MILESTONE.md
# R-5a/R-5a2/R-5c): the source-shaped roster input extractors are named, in
# code (comments removed), only by their own module and the live race setup
# adapter (game/MAIN/MainArcadeRaceSetup.c, R-5b). The adapter reads the
# roster right after MainInit_Drivers, before the first race tick rebuilds
# the race order, so it must call the pre-race variant exactly once and must
# not name ExtractRosterInput (whose stale-order check would refuse a launch
# after the attract demo race). tests/ may name both freely and is not
# scanned. Scanned: every .c, .h, and .inc under game/ and platform/, every .h
# under include/, and main.c.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "canonical drivers roster input isolation")
set(symbols "MainCanonicalDrivers_ExtractRosterInput" "MainCanonicalDrivers_ExtractRosterInputPreRace")
set(module_paths
    "game/MAIN/MainCanonicalDrivers.c"
    "game/MAIN/MainCanonicalDrivers.h")
set(adapter_path "game/MAIN/MainArcadeRaceSetup.c")
set(allowed_paths ${module_paths} "${adapter_path}")

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# The number of whole-identifier occurrences of symbol in code.
function(ctr_count_symbol code symbol out_var)
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${symbol}([^A-Za-z0-9_]|$)" hits "${code}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# The scan must itself work: a call is caught, a comment and a longer
# identifier are not, and the two symbols never count each other.
foreach(symbol IN LISTS symbols)
    ctr_strip_comments("int f(void) { return ${symbol}(a, b, &c); }\n" probe_call)
    ctr_count_symbol("${probe_call}" "${symbol}" probe_call_hits)
    ctr_strip_comments("/* ${symbol} */\n// ${symbol}\nint g;\n" probe_comment)
    ctr_count_symbol("${probe_comment}" "${symbol}" probe_comment_hits)
    ctr_count_symbol("int ${symbol}Other(void);\n" "${symbol}" probe_longer_hits)
    if(NOT probe_call_hits EQUAL 1 OR NOT probe_comment_hits EQUAL 0 OR NOT probe_longer_hits EQUAL 0)
        message(FATAL_ERROR "${prefix}: the symbol scan is broken for ${symbol} (call ${probe_call_hits}, comment ${probe_comment_hits}, longer ${probe_longer_hits})")
    endif()
endforeach()
ctr_count_symbol("x = MainCanonicalDrivers_ExtractRosterInputPreRace(a, b, c);" "MainCanonicalDrivers_ExtractRosterInput" probe_cross)
if(NOT probe_cross EQUAL 0)
    message(FATAL_ERROR "${prefix}: the pre-race variant is counted as ExtractRosterInput; the scan is broken")
endif()

# The allowed files exist, and the module both declares and defines both.
foreach(relative_path IN LISTS allowed_paths)
    if(NOT EXISTS "${repo}/${relative_path}")
        message(FATAL_ERROR "${prefix}: allowed file ${relative_path} is missing")
    endif()
endforeach()
foreach(relative_path IN LISTS module_paths)
    file(READ "${repo}/${relative_path}" module_source)
    ctr_strip_comments("${module_source}" module_code)
    foreach(symbol IN LISTS symbols)
        ctr_count_symbol("${module_code}" "${symbol}" module_hits)
        if(module_hits LESS 1)
            message(FATAL_ERROR "${prefix}: ${relative_path} must declare or define ${symbol}")
        endif()
    endforeach()
endforeach()

# The adapter calls the pre-race variant exactly once, never ExtractRosterInput.
file(READ "${repo}/${adapter_path}" adapter_source)
ctr_strip_comments("${adapter_source}" adapter_code)
ctr_count_symbol("${adapter_code}" "MainCanonicalDrivers_ExtractRosterInputPreRace" adapter_pre_race_hits)
ctr_count_symbol("${adapter_code}" "MainCanonicalDrivers_ExtractRosterInput" adapter_input_hits)
if(NOT adapter_pre_race_hits EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${adapter_path} must call MainCanonicalDrivers_ExtractRosterInputPreRace exactly once (found ${adapter_pre_race_hits})")
endif()
if(NOT adapter_input_hits EQUAL 0)
    message(FATAL_ERROR "${prefix}: ${adapter_path} must not name MainCanonicalDrivers_ExtractRosterInput; it reads the roster before the race order is rebuilt")
endif()

file(GLOB_RECURSE scan_files
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/*.h")
list(APPEND scan_files "${repo}/main.c")
set(scanned 0)
set(scanned_platform 0)
set(scanned_include 0)
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR scanned "${scanned} + 1")
    if(relative_path MATCHES "^platform/")
        math(EXPR scanned_platform "${scanned_platform} + 1")
    elseif(relative_path MATCHES "^include/")
        math(EXPR scanned_include "${scanned_include} + 1")
    endif()
    list(FIND allowed_paths "${relative_path}" allowed_at)
    if(NOT allowed_at EQUAL -1)
        continue()
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "MainCanonicalDrivers_ExtractRosterInput" raw_hit)
    if(raw_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    foreach(symbol IN LISTS symbols)
        ctr_count_symbol("${code}" "${symbol}" hits)
        if(hits GREATER 0)
            message(FATAL_ERROR "${prefix}: ${relative_path} names ${symbol}; only ${allowed_paths} may")
        endif()
    endforeach()
endforeach()
if(scanned LESS 100 OR scanned_platform LESS 20 OR scanned_include LESS 20)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files (${scanned_platform} in platform/, ${scanned_include} in include/); the scan is broken")
endif()
