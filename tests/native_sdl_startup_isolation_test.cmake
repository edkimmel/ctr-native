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
#    every failure branch logs the SDL error (through Platform_SdlErrorText(),
#    which substitutes a placeholder for an empty SDL_GetError(), or directly)
#    and returns 0, and main.c exits non-zero instead of running the game
#    without SDL video.  SDL_Init( is preceded by SDL_ClearError() so its
#    report cannot show a stale error.
ctr_read_source("include/platform.h" platform_header)
ctr_require("include/platform.h" "${platform_header}" "int Platform_Init(")
ctr_forbid("include/platform.h" "${platform_header}" "void Platform_Init(")

set(sdl_error_helper "Platform_SdlErrorText()")
set(sdl_error_helper_begin -1)
string(REGEX MATCH "const char[\r\n\t ]*\\*[\r\n\t ]*Platform_SdlErrorText[\r\n\t ]*\\([\r\n\t ]*(void)?[\r\n\t ]*\\)[\r\n\t ]*{"
    sdl_error_helper_definition "${platform_source}")
if(NOT "${sdl_error_helper_definition}" STREQUAL "")
    string(FIND "${platform_source}" "${sdl_error_helper_definition}" sdl_error_helper_begin)
endif()
if(NOT sdl_error_helper_begin EQUAL -1)
    string(SUBSTRING "${platform_source}" "${sdl_error_helper_begin}" -1 sdl_error_helper_tail)
    string(FIND "${sdl_error_helper_tail}" "\n}" sdl_error_helper_length)
    if(sdl_error_helper_length EQUAL -1)
        message(FATAL_ERROR "sdl startup: cannot locate the end of ${sdl_error_helper}")
    endif()
    string(SUBSTRING "${sdl_error_helper_tail}" 0 "${sdl_error_helper_length}" sdl_error_helper_body)
    ctr_require("${sdl_error_helper}" "${sdl_error_helper_body}" "SDL_GetError()")
endif()

string(SUBSTRING "${init_body}" 0 "${sdl_init_offset}" sdl_init_head)
string(REGEX MATCH "SDL_ClearError\\(\\);[\r\n\t ]*if[\r\n\t ]*\\([\r\n\t !]*$" sdl_init_clear "${sdl_init_head}")
if("${sdl_init_clear}" STREQUAL "")
    message(FATAL_ERROR "sdl startup: Platform_Init must call SDL_ClearError(); immediately before the SDL_Init( check")
endif()

string(SUBSTRING "${init_body}" "${sdl_init_offset}" -1 sdl_init_tail)
string(FIND "${sdl_init_tail}" "\n\t}" sdl_init_branch_length)
if(sdl_init_branch_length EQUAL -1)
    message(FATAL_ERROR "sdl startup: cannot locate the SDL_Init( failure branch in Platform_Init")
endif()
string(SUBSTRING "${sdl_init_tail}" 0 "${sdl_init_branch_length}" sdl_init_branch)
string(FIND "${sdl_init_branch}" "${sdl_error_helper}" sdl_init_branch_helper)
string(FIND "${sdl_init_branch}" "SDL_GetError()" sdl_init_branch_get_error)
if(sdl_init_branch_helper EQUAL -1 AND sdl_init_branch_get_error EQUAL -1)
    message(FATAL_ERROR "sdl startup: Platform_Init's SDL_Init( failure branch must report ${sdl_error_helper} or SDL_GetError()")
endif()
ctr_require("Platform_Init's SDL_Init( failure branch" "${sdl_init_branch}" "return 0;")
ctr_forbid("Platform_Init" "${init_body}" "return;")
ctr_count("${init_body}" "Platform_LogError(" init_error_count)
ctr_count("${init_body}" "${sdl_error_helper}" init_sdl_error_helper_count)
ctr_count("${init_body}" "SDL_GetError()" init_sdl_get_error_count)
math(EXPR init_sdl_error_count "${init_sdl_error_helper_count} + ${init_sdl_get_error_count}")
if(init_sdl_error_helper_count GREATER 0 AND sdl_error_helper_begin EQUAL -1)
    message(FATAL_ERROR "sdl startup: Platform_Init uses ${sdl_error_helper} but it is not defined in platform/native_platform.c")
endif()
ctr_count("${init_body}" "return 0;" init_failure_return_count)
if(init_error_count LESS 3 OR NOT init_error_count EQUAL init_sdl_error_count OR NOT init_error_count EQUAL init_failure_return_count)
    message(FATAL_ERROR
        "sdl startup: every Platform_Init failure must log the SDL error and return 0 (${init_error_count} errors, ${init_sdl_error_count} SDL error reports, ${init_failure_return_count} return 0)")
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

# e. A failed window/GL startup shuts down cleanly.  Platform_Init's failure
#    branches call Platform_Shutdown(), which reaches the renderer teardown
#    before gladLoadGL() may have run.  s_glLoaded is set only immediately
#    after a successful gladLoadGL(), NativeRenderer_Shutdown returns before
#    its first gl* call when it is clear, and the flag is cleared at the end
#    of the teardown.  NativeRenderer_FinishGpuMeasurements is guarded too.
set(renderer_module "platform/native_renderer.c")
ctr_read_source("${renderer_module}" renderer_source)

function(ctr_function_body source signature out_var)
    string(FIND "${source}" "${signature}" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "sdl startup: cannot locate '${signature}' in ${renderer_module}")
    endif()
    string(SUBSTRING "${source}" "${begin}" -1 tail)
    string(FIND "${tail}" "\n}" length)
    if(length EQUAL -1)
        message(FATAL_ERROR "sdl startup: cannot locate the end of '${signature}' in ${renderer_module}")
    endif()
    string(SUBSTRING "${tail}" 0 "${length}" body)
    set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

ctr_count("${renderer_source}" "s_glLoaded = 1;" gl_loaded_set_count)
ctr_count("${renderer_source}" "s_glLoaded =" gl_loaded_write_count)
if(NOT gl_loaded_set_count EQUAL 1 OR NOT gl_loaded_write_count EQUAL 2)
    message(FATAL_ERROR
        "sdl startup: ${renderer_module} must set s_glLoaded = 1 once and clear it once (${gl_loaded_set_count} sets, ${gl_loaded_write_count} writes)")
endif()
string(REGEX MATCH
    "GLenum err = gladLoadGL\\(\\);[\r\n\t ]*if \\(err == 0\\)[\r\n\t ]*{[\r\n\t ]*return 0;[\r\n\t ]*}[\r\n\t ]*s_glLoaded = 1;"
    gl_loaded_set "${renderer_source}")
if("${gl_loaded_set}" STREQUAL "")
    message(FATAL_ERROR
        "sdl startup: s_glLoaded = 1; must immediately follow the successful gladLoadGL() check in ${renderer_module}")
endif()

ctr_function_body("${renderer_source}" "void NativeRenderer_Shutdown(void)" renderer_shutdown_body)
string(REGEX MATCH
    "^void NativeRenderer_Shutdown\\(void\\)[\r\n\t ]*{[\r\n\t ]*if \\(!s_glLoaded\\)[\r\n\t ]*{[\r\n\t ]*return;[\r\n\t ]*}"
    renderer_shutdown_guard "${renderer_shutdown_body}")
if("${renderer_shutdown_guard}" STREQUAL "")
    message(FATAL_ERROR
        "sdl startup: NativeRenderer_Shutdown must begin with 'if (!s_glLoaded) { return; }' before any GL call")
endif()
string(REGEX MATCH "(^|[^A-Za-z0-9_])gl[A-Z][A-Za-z0-9_]*\\(" renderer_shutdown_guard_gl "${renderer_shutdown_guard}")
if(NOT "${renderer_shutdown_guard_gl}" STREQUAL "")
    message(FATAL_ERROR "sdl startup: NativeRenderer_Shutdown issues a GL call before its s_glLoaded guard")
endif()
string(REGEX MATCH "s_glLoaded = 0;[\r\n\t ]*$" renderer_shutdown_clear "${renderer_shutdown_body}")
if("${renderer_shutdown_clear}" STREQUAL "")
    message(FATAL_ERROR "sdl startup: NativeRenderer_Shutdown must end with 's_glLoaded = 0;'")
endif()

ctr_function_body("${renderer_source}" "void NativeRenderer_FinishGpuMeasurements(void)" finish_gpu_body)
string(FIND "${finish_gpu_body}" "if (!s_glLoaded)" finish_gpu_guard_offset)
string(FIND "${finish_gpu_body}" "NativeRenderer_EndGpuFrame();" finish_gpu_end_offset)
if(finish_gpu_guard_offset EQUAL -1 OR finish_gpu_end_offset EQUAL -1 OR NOT finish_gpu_guard_offset LESS finish_gpu_end_offset)
    message(FATAL_ERROR
        "sdl startup: NativeRenderer_FinishGpuMeasurements must check s_glLoaded before NativeRenderer_EndGpuFrame()")
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
