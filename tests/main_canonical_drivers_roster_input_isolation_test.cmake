# Structural isolation for MainCanonicalDrivers_ExtractRosterInput
# (game/MAIN/MainCanonicalDrivers.{c,h}, docs/ROSTER_MILESTONE.md R-5a/R-5a2):
# the source-shaped roster input extractor is named, in code (comments
# removed), only by its own module and the files on the allowlist below.
# tests/ may name it freely and is not scanned. Scanned: every .c, .h, and
# .inc under game/ and platform/, every .h under include/, and main.c.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "canonical drivers roster input isolation")
set(symbol "MainCanonicalDrivers_ExtractRosterInput")
set(allowed_paths
    "game/MAIN/MainCanonicalDrivers.c"
    "game/MAIN/MainCanonicalDrivers.h")

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# 1 when code names the symbol as a whole identifier.
function(ctr_names_symbol code out_var)
    if(code MATCHES "(^|[^A-Za-z0-9_])${symbol}([^A-Za-z0-9_]|$)")
        set(${out_var} 1 PARENT_SCOPE)
    else()
        set(${out_var} 0 PARENT_SCOPE)
    endif()
endfunction()

# The scan must itself work: a call is caught, a comment and a longer
# identifier are not.
ctr_strip_comments("int f(void) { return ${symbol}(a, b, &c); }\n" probe_call)
ctr_names_symbol("${probe_call}" probe_call_hit)
ctr_strip_comments("/* ${symbol} */\n// ${symbol}\nint g;\n" probe_comment)
ctr_names_symbol("${probe_comment}" probe_comment_hit)
ctr_names_symbol("int ${symbol}Other(void);\n" probe_longer_hit)
if(NOT probe_call_hit EQUAL 1 OR NOT probe_comment_hit EQUAL 0 OR NOT probe_longer_hit EQUAL 0)
    message(FATAL_ERROR "${prefix}: the symbol scan is broken (call ${probe_call_hit}, comment ${probe_comment_hit}, longer ${probe_longer_hit})")
endif()

# The allowed files exist, and the module both declares and defines it.
foreach(relative_path IN LISTS allowed_paths)
    if(NOT EXISTS "${repo}/${relative_path}")
        message(FATAL_ERROR "${prefix}: allowed file ${relative_path} is missing")
    endif()
endforeach()
file(READ "${repo}/game/MAIN/MainCanonicalDrivers.h" module_header)
ctr_strip_comments("${module_header}" module_header_code)
ctr_names_symbol("${module_header_code}" header_hit)
file(READ "${repo}/game/MAIN/MainCanonicalDrivers.c" module_source)
ctr_strip_comments("${module_source}" module_source_code)
ctr_names_symbol("${module_source_code}" source_hit)
if(NOT header_hit EQUAL 1 OR NOT source_hit EQUAL 1)
    message(FATAL_ERROR "${prefix}: game/MAIN/MainCanonicalDrivers.{c,h} must declare and define ${symbol}")
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
    string(FIND "${source}" "${symbol}" raw_hit)
    if(raw_hit EQUAL -1)
        continue()
    endif()
    ctr_strip_comments("${source}" code)
    ctr_names_symbol("${code}" hit)
    if(hit EQUAL 1)
        message(FATAL_ERROR "${prefix}: ${relative_path} names ${symbol}; only ${allowed_paths} may")
    endif()
endforeach()
if(scanned LESS 100 OR scanned_platform LESS 20 OR scanned_include LESS 20)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files (${scanned_platform} in platform/, ${scanned_include} in include/); the scan is broken")
endif()
