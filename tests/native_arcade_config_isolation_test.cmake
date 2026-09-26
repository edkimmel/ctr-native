# Structural isolation for the per-cabinet config file (native_arcade_config,
# docs/PACKAGING.md PK-2..PK-6): pure key = value parsing of a caller-read
# buffer and of --config / --data-dir. Its includes are limited to stddef.h,
# stdint.h, string.h, its own header, and the arcade-link options header; its
# code names no match config, deterministic-state, record/playback,
# checkpoint, build/content fingerprint, topology-lease, netplay, socket, SDL,
# game, display-config, heap, clock, or file I/O symbol; the library links
# exactly ctr_native_arcade_link_options; the target is C17 with extensions
# off; the link group has no grammar of its own and goes through
# NativeArcadeLinkOptions_ApplyArgs; main.c is the only non-test caller, and
# it hands the config only to the display window mode, the arcade-link
# options (unless argv names a link option; before the first replay-option,
# roster-proof, or autopilot check reads them), and the assets data
# directory (whose local value reaches only the drive/root-relative check,
# NativeAssets_InitWithAssetDir, and the two error messages).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade config isolation")

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

function(ctr_count source pattern out_var)
    string(REGEX MATCHALL "${pattern}" matches "${source}")
    list(LENGTH matches match_count)
    set(${out_var} ${match_count} PARENT_SCOPE)
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

set(config_header "include/platform/native_arcade_config.h")
set(config_source "platform/native_arcade_config.c")
set(config_files "${config_header}" "${config_source}")

# 1. Includes: std headers, its own header, and the link options header only.
# 2. Code tokens (comments stripped).
set(state_tokens MatchConfig match_config NativeMatch Canonical canonical Replay replay Checkpoint checkpoint
    Identity identity Digest digest selectEntropy)
set(lease_tokens Lease lease Topology topology LOAD_Hub_ReadFile Retire Acquire Activate Publish)
set(network_tokens Netplay netplay Lockstep lockstep Lobby socket Socket winsock WSA sockaddr htons ntohs inet_ getaddrinfo udp_transport)
set(host_tokens SDL "game/" Game_ Platform_ NativeDisplayConfig native_display_config NativeAssets native_assets)
set(alloc_io_tokens malloc calloc realloc "free(" alloca fopen fread fwrite "FILE *" stdio "time(" "clock(" getenv)
foreach(relative_path IN LISTS config_files)
    ctr_read_source("${relative_path}" source)
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${source}")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(<stddef\\.h>|<stdint\\.h>|<string\\.h>|\"platform/native_arcade_config\\.h\"|\"platform/native_arcade_link_options\\.h\")[ \t\r]*$")
            message(FATAL_ERROR "${prefix}: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
    ctr_strip_comments("${source}" code)
    foreach(term IN LISTS state_tokens lease_tokens network_tokens host_tokens alloc_io_tokens)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
endforeach()

# 3. No port or peer grammar of its own (PK-5): the module never parses
#    digits or addresses itself and reaches the link parser only through
#    NativeArcadeLinkOptions_ApplyArgs; it never calls the link parser's
#    helpers or writes a link options field.
ctr_read_source("${config_source}" source)
ctr_strip_comments("${source}" code)
string(REGEX REPLACE "#[ \t]*include[^\r\n]*" "" code "${code}")
ctr_count("${code}" "NativeArcadeLinkOptions_ApplyArgs\\(" apply_calls)
if(NOT apply_calls EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${config_source} must call NativeArcadeLinkOptions_ApplyArgs exactly once (found ${apply_calls})")
endif()
foreach(term IN ITEMS NativeArcadeLinkOptions_ParsePeer NativeArcadeLinkOptions_ParsePreview NativeArcadeLinkFixture strtol strtoul atoi sscanf
        "'0'" "'9'" "'.'" "':'" "options->" "options." "probe->" "probe." "localPort" "localRole" "NATIVE_MATCH_SLOT")
    ctr_forbid("${config_source}" "${code}" "${term}")
endforeach()

# 4. ctr_native_arcade_config links exactly ctr_native_arcade_link_options, in
#    exactly one target_link_libraries call.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_config)
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
if(NOT "${link_items}" STREQUAL "ctr_native_arcade_link_options")
    message(FATAL_ERROR "${prefix}: ${target} must link exactly ctr_native_arcade_link_options (found '${link_items}')")
endif()
ctr_require("CMakeLists.txt" "${cmake}" "add_library(${target} STATIC platform/native_arcade_config.c)")

# 5. C17, no extensions, on the target, in order.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
ctr_require_order("CMakeLists.txt (${target})" "${target_block}"
    "set_target_properties(${target} PROPERTIES" "C_STANDARD 17" "C_STANDARD_REQUIRED ON" "C_EXTENSIONS OFF")

# 6. ctr_native links it; the unit test and this test are registered.
string(FIND "${cmake}" "target_link_libraries(ctr_native " native_link_start)
string(SUBSTRING "${cmake}" ${native_link_start} -1 native_link_tail)
string(FIND "${native_link_tail}" ")" native_link_end)
string(SUBSTRING "${native_link_tail}" 0 ${native_link_end} native_link_block)
string(REGEX MATCH "[ \t\r\n]${target}([ \t\r\n]|$)" native_hit "${native_link_block}")
if("${native_hit}" STREQUAL "")
    message(FATAL_ERROR "${prefix}: ctr_native must link ${target}")
endif()
ctr_require("CMakeLists.txt" "${cmake}" "add_test(NAME native_arcade_config_unit")
ctr_require("CMakeLists.txt" "${cmake}" "tests/native_arcade_config_isolation_test.cmake")
ctr_require("CMakeLists.txt" "${cmake}" "\"\${CMAKE_SOURCE_DIR}/tools/package/cab1.cfg\"")
ctr_require("CMakeLists.txt" "${cmake}" "\"\${CMAKE_SOURCE_DIR}/tools/package/cab2.cfg\"")

# 7. Callers: no game/, platform/, or include/ file other than the module
#    names the module; main.c is the only non-test caller.
file(GLOB_RECURSE scan_files LIST_DIRECTORIES false
    "${repo}/game/*.c" "${repo}/game/*.h"
    "${repo}/platform/*.c" "${repo}/platform/*.h"
    "${repo}/include/*.h")
list(LENGTH scan_files scanned)
if(scanned LESS 100)
    message(FATAL_ERROR "${prefix}: scanned only ${scanned} files; the scan is broken")
endif()
foreach(path IN LISTS scan_files)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL config_header OR relative_path STREQUAL config_source)
        continue()
    endif()
    file(READ "${path}" scanned_source)
    foreach(term IN ITEMS NativeArcadeConfig native_arcade_config NATIVE_ARCADE_CONFIG_)
        string(FIND "${scanned_source}" "${term}" hit)
        if(NOT hit EQUAL -1)
            message(FATAL_ERROR "${prefix}: ${relative_path} names '${term}'; only main.c may use the config module")
        endif()
    endforeach()
endforeach()

# 8. main.c: the order of the wiring, and where the config may go.
ctr_read_source("main.c" main_source)
ctr_strip_comments("${main_source}" main_code)
ctr_require("main.c" "${main_code}" "#include \"platform/native_arcade_config.h\"")
ctr_require_order("main.c" "${main_code}"
    "NativeArg_IsVersion(argv[argIndex])"
    "NativeArcadeConfig_ParseArgs(argc, argv, &configArgs)"
    "NativeConfigFile_Load(configArgs.configPath, sdlBasePath, &arcadeConfig, configPath, sizeof(configPath), &configLoaded)"
    "NativeDisplayConfig_ApplyArgs(argc, argv, &displayConfig)"
    "NativeArcadeLinkOptions_ApplyArgs(argc, argv, &arcadeLinkOptions)"
    "if ((configArgs.namesLinkOption == 0u) && NativeArcadeConfig_HasLink(&arcadeConfig))"
    "NativeArcadeConfig_ApplyLink(&arcadeConfig, &arcadeLinkOptions)"
    "NativeArg_NamesReplayOption(argc, argv)"
    "NativeAssets_InitWithAssetDir(sdlBasePath, dataDir, resolvedDataDir, sizeof(resolvedDataDir))"
    "NativeAssets_Validate()"
    "NativeArcadeLinkHost_Configure(&arcadeLinkOptions, arcadeLinkIdentityPtr)")
# The config's link group must be in the link options before main.c looks at
# them for any rejection: ApplyLink comes before the FIRST replay-option
# check and before the roster-proof and autopilot option parsers (whose
# rejections read arcadeLinkOptions), not merely before some later call.
string(FIND "${main_code}" "NativeArcadeConfig_ApplyLink(&arcadeConfig" apply_link_at)
if(apply_link_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: main.c must call NativeArcadeConfig_ApplyLink(&arcadeConfig, ...)")
endif()
foreach(later IN ITEMS "NativeArg_NamesReplayOption(argc, argv)" "NativeArcadeRosterProofOptions_ApplyArgs(argc, argv, &rosterProofOptions)"
        "NativeArcadeLinkAutopilotOptions_ApplyArgs(")
    string(FIND "${main_code}" "${later}" later_at)
    if(later_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: main.c lacks '${later}'")
    endif()
    if(NOT apply_link_at LESS later_at)
        message(FATAL_ERROR "${prefix}: main.c must call NativeArcadeConfig_ApplyLink before the first '${later}'")
    endif()
endforeach()
# (NativeConfigFile_Load is main.c's own file reader: its definition and one call.)
ctr_count("${main_code}" "NativeConfigFile_Load\\(" load_names)
if(NOT load_names EQUAL 2)
    message(FATAL_ERROR "${prefix}: main.c must define NativeConfigFile_Load and call it exactly once (found ${load_names} names)")
endif()
foreach(call IN ITEMS "NativeArcadeConfig_ParseArgs\\(" "NativeArcadeConfig_Parse\\(" "NativeArcadeConfig_ApplyLink\\(" "NativeConfigFile_Load\\(configArgs"
        "NativeAssets_InitWithAssetDir\\(")
    ctr_count("${main_code}" "${call}" call_count)
    if(NOT call_count EQUAL 1)
        message(FATAL_ERROR "${prefix}: main.c must call ${call} exactly once (found ${call_count})")
    endif()
endforeach()
# Every use of the parsed config in main.c is one of these; nothing else
# (no match config, record/playback, checkpoint, fingerprint, or lease call)
# ever sees it.
set(allowed_uses
    "struct NativeArcadeConfig arcadeConfig"
    "NativeArcadeConfig_SetDefaults(&arcadeConfig)"
    "NativeConfigFile_Load(configArgs.configPath, sdlBasePath, &arcadeConfig, configPath, sizeof(configPath), &configLoaded)"
    "NativeArcadeConfig_HasLink(&arcadeConfig)"
    "NativeArcadeConfig_ApplyLink(&arcadeConfig, &arcadeLinkOptions)"
    "displayConfig.fullscreen = arcadeConfig.fullscreen"
    "arcadeConfig.hasFullscreen"
    "arcadeConfig.hasDataDir"
    "? arcadeConfig.dataDir :")
set(remaining_code "${main_code}")
foreach(use IN LISTS allowed_uses)
    string(REPLACE "${use}" "" remaining_code "${remaining_code}")
endforeach()
string(FIND "${remaining_code}" "arcadeConfig" stray_use)
if(NOT stray_use EQUAL -1)
    string(SUBSTRING "${remaining_code}" ${stray_use} 120 stray_context)
    message(FATAL_ERROR "${prefix}: main.c uses arcadeConfig outside the allowed display, link-options, and data-dir paths: '${stray_context}'")
endif()

# 9. main.c's local data directory value: defined once from --data-dir or the
#    file's data_dir, and used only by the drive/root-relative check, the
#    NativeAssets_InitWithAssetDir call, and the two stderr error messages
#    that name it. It is never passed anywhere else.
set(data_dir_definition
    "const char *dataDir = (configArgs.dataDir != NULL) ? configArgs.dataDir : ((arcadeConfig.hasDataDir != 0u) ? arcadeConfig.dataDir : NULL)")
set(data_dir_uses
    "${data_dir_definition}"
    "if (dataDir != NULL)"
    "if (NativeAssets_IsDriveOrRootRelativePath(dataDir))"
    "if (!NativeAssets_InitWithAssetDir(sdlBasePath, dataDir, resolvedDataDir, sizeof(resolvedDataDir)))")
set(remaining_code "${main_code}")
foreach(use IN LISTS data_dir_uses)
    string(FIND "${remaining_code}" "${use}" use_at)
    if(use_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: main.c lacks the data directory use '${use}'")
    endif()
    string(REPLACE "${use}" "" without_use "${remaining_code}")
    string(LENGTH "${remaining_code}" before_length)
    string(LENGTH "${without_use}" after_length)
    string(LENGTH "${use}" use_length)
    math(EXPR removed "(${before_length} - ${after_length}) / ${use_length}")
    if(NOT removed EQUAL 1)
        message(FATAL_ERROR "${prefix}: main.c must contain '${use}' exactly once (found ${removed})")
    endif()
    set(remaining_code "${without_use}")
endforeach()
set(data_dir_message_pattern "fprintf\\(stderr, \"\\[CTR Native\\] data directory %s [^\"]*\",[ \t\r\n]*dataDir,")
# (Counted through a marker: a message may hold ';', which splits MATCHALL lists.)
string(REGEX REPLACE "${data_dir_message_pattern}" "@CTR_DATA_DIR_MESSAGE@" remaining_code "${remaining_code}")
ctr_count("${remaining_code}" "@CTR_DATA_DIR_MESSAGE@" data_dir_messages)
if(NOT data_dir_messages EQUAL 2)
    message(FATAL_ERROR "${prefix}: main.c must name dataDir in exactly two '[CTR Native] data directory %s' stderr messages (found ${data_dir_messages})")
endif()
string(REGEX MATCH "(^|[^A-Za-z0-9_.>])dataDir([^A-Za-z0-9_]|$)" stray_data_dir "${remaining_code}")
if(NOT "${stray_data_dir}" STREQUAL "")
    string(FIND "${remaining_code}" "${stray_data_dir}" stray_at)
    string(SUBSTRING "${remaining_code}" ${stray_at} 120 stray_context)
    message(FATAL_ERROR "${prefix}: main.c uses its data directory outside the check, NativeAssets_InitWithAssetDir, and the error messages: '${stray_context}'")
endif()
