# SDL startup must never block on SDL's modal "Assertion Failed" dialog.  When
# a session has no display, SDL_Init(SDL_INIT_VIDEO) fails and SDL's HID
# device-notification counter assertion (SDL_hid.c) fires inside SDL_Init; the
# host assertion handler therefore has to be installed before the first
# SDL_Init call.  Host code may not override the SDL_ASSERT hint (the handler
# honours a developer's own override), and it may not disable HIDAPI or hide
# the G29 as a startup workaround.  These are structural facts about the
# startup order, so they are enforced at the source level.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "sdl startup: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "sdl startup: '${term}' is required in ${relative_path}")
    endif()
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "sdl startup: '${term}' is forbidden in ${relative_path}")
    endif()
endfunction()

function(ctr_count source term out_var)
    set(count 0)
    set(rest "${source}")
    string(LENGTH "${term}" term_length)
    while(TRUE)
        string(FIND "${rest}" "${term}" offset)
        if(offset EQUAL -1)
            break()
        endif()
        math(EXPR count "${count} + 1")
        math(EXPR next "${offset} + ${term_length}")
        string(SUBSTRING "${rest}" "${next}" -1 rest)
    endwhile()
    set(${out_var} "${count}" PARENT_SCOPE)
endfunction()

# a. Platform_Init installs the assertion handler after the log is open and
#    before the first SDL_Init call.
ctr_read_source("platform/native_platform.c" platform_source)
ctr_require("platform/native_platform.c" "${platform_source}" "#include \"platform/native_sdl_assert.h\"")

string(FIND "${platform_source}" "int Platform_Init(" init_begin)
if(init_begin EQUAL -1)
    message(FATAL_ERROR "sdl startup: cannot locate Platform_Init in platform/native_platform.c")
endif()
string(SUBSTRING "${platform_source}" "${init_begin}" -1 init_tail)
string(FIND "${init_tail}" "\n}" init_length)
if(init_length EQUAL -1)
    message(FATAL_ERROR "sdl startup: cannot locate the end of Platform_Init")
endif()
string(SUBSTRING "${init_tail}" 0 "${init_length}" init_body)

string(FIND "${init_body}" "Platform_LogInit(" log_init_offset)
string(FIND "${init_body}" "NativeSdlAssert_Install(" install_offset)
string(FIND "${init_body}" "SDL_Init(" sdl_init_offset)
if(install_offset EQUAL -1)
    message(FATAL_ERROR "sdl startup: Platform_Init must call NativeSdlAssert_Install(")
endif()
if(sdl_init_offset EQUAL -1)
    message(FATAL_ERROR "sdl startup: cannot locate SDL_Init( in Platform_Init")
endif()
if(log_init_offset EQUAL -1 OR NOT log_init_offset LESS install_offset)
    message(FATAL_ERROR "sdl startup: NativeSdlAssert_Install( must follow Platform_LogInit( in Platform_Init")
endif()
if(NOT install_offset LESS sdl_init_offset)
    message(FATAL_ERROR "sdl startup: NativeSdlAssert_Install( must precede the first SDL_Init( in Platform_Init")
endif()

# The module is a standalone library, never part of the unity build.
ctr_read_source("main.c" main_source)
ctr_forbid("main.c" "${main_source}" "native_sdl_assert.c")
ctr_read_source("game/game_unity.h" game_unity_source)
ctr_forbid("game/game_unity.h" "${game_unity_source}" "native_sdl_assert")

# d. Platform initialisation failure is fatal.  Platform_Init reports success,
#    every failure branch logs SDL_GetError() and returns 0, and main.c exits
#    non-zero instead of running the game without SDL video.
ctr_read_source("include/platform.h" platform_header)
ctr_require("include/platform.h" "${platform_header}" "int Platform_Init(")
ctr_forbid("include/platform.h" "${platform_header}" "void Platform_Init(")

string(SUBSTRING "${init_body}" "${sdl_init_offset}" -1 sdl_init_tail)
string(FIND "${sdl_init_tail}" "\n\t}" sdl_init_branch_length)
if(sdl_init_branch_length EQUAL -1)
    message(FATAL_ERROR "sdl startup: cannot locate the SDL_Init( failure branch in Platform_Init")
endif()
string(SUBSTRING "${sdl_init_tail}" 0 "${sdl_init_branch_length}" sdl_init_branch)
ctr_require("Platform_Init's SDL_Init( failure branch" "${sdl_init_branch}" "SDL_GetError()")
ctr_require("Platform_Init's SDL_Init( failure branch" "${sdl_init_branch}" "return 0;")
ctr_forbid("Platform_Init" "${init_body}" "return;")
ctr_count("${init_body}" "Platform_LogError(" init_error_count)
ctr_count("${init_body}" "SDL_GetError()" init_sdl_error_count)
ctr_count("${init_body}" "return 0;" init_failure_return_count)
if(init_error_count LESS 3 OR NOT init_error_count EQUAL init_sdl_error_count OR NOT init_error_count EQUAL init_failure_return_count)
    message(FATAL_ERROR
        "sdl startup: every Platform_Init failure must log SDL_GetError() and return 0 (${init_error_count} errors, ${init_sdl_error_count} SDL_GetError, ${init_failure_return_count} return 0)")
endif()
string(REGEX MATCH "return 1;[\r\n\t ]*$" init_success_return "${init_body}")
if("${init_success_return}" STREQUAL "")
    message(FATAL_ERROR "sdl startup: Platform_Init must end with 'return 1;'")
endif()

# Replace each checked call with a marker (a MATCHALL list would split on the
# ';' inside each match); any Platform_Init( left over is an unchecked call.
ctr_count("${main_source}" "Platform_Init(" main_init_call_count)
string(REGEX REPLACE
    "if \\(!Platform_Init\\([^\r\n]*\\)\\)[\r\n\t ]*{[^}]*return NativeConsole_Return\\(1\\);[\r\n\t ]*}"
    "@CTR_CHECKED_PLATFORM_INIT@" main_checked_source "${main_source}")
ctr_count("${main_checked_source}" "@CTR_CHECKED_PLATFORM_INIT@" main_checked_init_count)
if(main_init_call_count LESS 2 OR NOT main_init_call_count EQUAL main_checked_init_count)
    message(FATAL_ERROR
        "sdl startup: every Platform_Init( call in main.c must be 'if (!Platform_Init(...))' returning NativeConsole_Return(1) (${main_init_call_count} calls, ${main_checked_init_count} checked)")
endif()

# The handler returns always-ignore and delegates only to SDL's default
# handler when an explicit override is present.
set(assert_module "platform/native_sdl_assert.c")
ctr_read_source("${assert_module}" assert_source)
ctr_require("${assert_module}" "${assert_source}" "SDL_SetAssertionHandler(")
ctr_require("${assert_module}" "${assert_source}" "SDL_GetDefaultAssertionHandler()")
ctr_require("${assert_module}" "${assert_source}" "return SDL_ASSERTION_ALWAYS_IGNORE;")

# b/c. Host sources scanned for assertion overrides and HIDAPI/G29 hiding.
file(GLOB_RECURSE host_sources RELATIVE "${repo}"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/platform/*.h")
list(APPEND host_sources "main.c")
list(LENGTH host_sources host_source_count)
if(host_source_count LESS 10)
    message(FATAL_ERROR "sdl startup: host source scan found only ${host_source_count} files; the glob is broken")
endif()
foreach(expected IN ITEMS "main.c" "${assert_module}" "platform/native_platform.c" "platform/native_input.c")
    list(FIND host_sources "${expected}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR "sdl startup: expected source ${expected} is missing from the scan")
    endif()
endforeach()

foreach(relative_path IN LISTS host_sources)
    file(READ "${repo}/${relative_path}" source)

    # b. Nothing sets the SDL_ASSERT hint or environment variable.  The
    #    assertion module may only read the hint through SDL_GetHint.
    ctr_forbid("${relative_path}" "${source}" "\"SDL_ASSERT")
    ctr_count("${source}" "SDL_HINT_ASSERT" hint_count)
    if(relative_path STREQUAL assert_module)
        ctr_count("${source}" "SDL_GetHint(SDL_HINT_ASSERT)" hint_read_count)
        if(hint_count EQUAL 0 OR NOT hint_count EQUAL hint_read_count)
            message(FATAL_ERROR
                "sdl startup: ${relative_path} may use SDL_HINT_ASSERT only as SDL_GetHint(SDL_HINT_ASSERT) (${hint_count} uses, ${hint_read_count} reads)")
        endif()
    elseif(NOT hint_count EQUAL 0)
        message(FATAL_ERROR "sdl startup: 'SDL_HINT_ASSERT' is forbidden in ${relative_path}")
    endif()

    # c. Nothing disables HIDAPI or hides the G29 by default.
    string(REGEX MATCH "SDL_HINT_JOYSTICK_HIDAPI([^A-Za-z0-9_]|$)" hidapi_match "${source}")
    if(NOT "${hidapi_match}" STREQUAL "")
        message(FATAL_ERROR "sdl startup: 'SDL_HINT_JOYSTICK_HIDAPI' is forbidden in ${relative_path}")
    endif()
    string(REGEX MATCH "\"SDL_JOYSTICK_HIDAPI[\"=]" hidapi_env_match "${source}")
    if(NOT "${hidapi_env_match}" STREQUAL "")
        message(FATAL_ERROR "sdl startup: the SDL_JOYSTICK_HIDAPI environment variable is forbidden in ${relative_path}")
    endif()
    foreach(term IN ITEMS
            "SDL_HINT_HIDAPI_IGNORE_DEVICES"
            "SDL_HINT_JOYSTICK_HIDAPI_LG4FF"
            "SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS"
            "\"SDL_HIDAPI_IGNORE_DEVICES"
            "\"SDL_JOYSTICK_HIDAPI_LG4FF"
            "\"SDL_HIDAPI_ENUMERATE_ONLY_CONTROLLERS")
        ctr_forbid("${relative_path}" "${source}" "${term}")
    endforeach()
endforeach()
