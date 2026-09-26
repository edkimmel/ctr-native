# Structural isolation for the arcade-link host glue (native_arcade_link_host,
# docs/GAME_LOOP_UI_MILESTONE.md section 2.6): the game-facing singleton over
# the arcade-link host adapter. Its header is meant to be included by game
# code, so it names no lockstep, failure-handling, or lobby token and does
# not include the adapter header; only the .c does. Neither file names a
# topology-lease symbol, uses the heap or a clock, reaches an OS networking
# API or an engine source, writes replay, checkpoint, or canonical state, or
# reads the live identity itself (the caller supplies it). Both include only
# their allowed headers; the library links exactly the adapter and the host
# options, and stays portable C17 with extensions off. The host-side test
# read-back header (native_arcade_link_host_internal.h, MS-8) follows the
# header rules and is named by no game source or main.c, and the select
# view keeps the adapter's layout, field offset for field offset. Since
# MS-8b the header names the select-view and agreed-match values itself
# (NATIVE_ARCADE_LINK_HOST_SELECT_*, _ROLE_*, _MAX_*), each static-asserted
# in the .c against the module value it mirrors. Since RL-S6 the header
# forward-declares struct NativeMatchConfigV1 for the agreed-config copy
# without including or naming the match-config header, and pins the
# race-launch host API (agreed config, local race failure, racing query).
# Since LR-S3 (docs/LOCKSTEP_RACE_MILESTONE.md LR-7) the .c also includes
# <platform.h> for its one platform call, the fixed VBlank pacing switch, made
# only by RaceBegin (on), RaceEnd (off), and Shutdown (off); the header
# declares RaceBegin and RaceEnd (rule 3f; the call sites are pinned by
# tests/native_vblank_pacing_isolation_test.cmake). Since LR-S6 the header
# also pins the end-of-race take, NativeArcadeLinkHost_TakeRaceEnd, and its
# record's exact three fields, which the .c copies field by field.
# Since LR-S9 (docs/LOCKSTEP_RACE_MILESTONE.md) the host runs the linked-race
# drive: the header forward-declares struct NativeCanonicalStateV4 for
# RaceStep (its include allowlist is unchanged), declares RaceStep,
# RaceHold, the host pad, race facts, and drive state, and names the
# drive's status and end-kind values itself (each static-asserted in the
# .c); the .c also includes the drive core's header, may name the two
# canonical type names the drive takes, and the library also links the
# drive core (rules 2, 3, 3d, 3g, and 4). Since LR-S10 part 1 (LR-60) the
# header declares the internal race tick limit setter, whose host-local value
# BeginDrive hands the drive, and only main.c's internal-build code sets it
# (rule 3h). Since LR-S10 part 2 the race caller
# (game/MAIN/MainArcadeRaceLaunch.c) is the one game source that names
# RaceStep and RaceHold, once each (rule 3g). Since LR-S12 the hold's period
# service keeps the launch linger sending through the start wait (race tick
# 0, LR-69; rule 3g), and the host latches a pointer-free divergence record
# once per race after every Tick, RaceStep, and RaceHold, which the header's
# TakeRaceDivergence hands to the game hook's log (LR-70; rule 3i).
# Since SOLO-S2 (docs/SOLO_CAB_MILESTONE.md) the .c also includes the bot
# rules header, builds the ONE_CAB solo base, and returns a solo race config
# only through a fail-closed bot-rules check; the header appends the solo
# view group and declares the solo query; and solo stays dark (rules 3 and
# 3j).

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "arcade link host isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: forbidden token '${term}' found in ${relative_path}")
    endif()
endfunction()

set(host_header "include/platform/native_arcade_link_host.h")
set(host_source "platform/native_arcade_link_host.c")

# 1. Header-only forbidden tokens: game code includes this header, so it
#    names nothing the lockstep and failure-handling isolation rules forbid
#    under the engine sources, and nothing of the adapter or lobby layer.
set(header_only_tokens
    lockstep Lockstep LOCKSTEP MatchOutcome MatchRoster LockstepRematch
    NativeArcadeNetplay NativeLobby native_lobby udp_transport)

ctr_read_source("${host_header}" header)
foreach(term IN LISTS header_only_tokens)
    ctr_forbid("${host_header}" "${header}" "${term}")
endforeach()

# 2. Tokens forbidden in both files, by category.
set(network_tokens
    winsock WinSock WSA AF_INET sockaddr htons htonl ntohs ntohl getaddrinfo "select(" "poll(" SDL)
set(clock_game_tokens "time(" "clock(" QueryPerformance "game/" Game_)
set(alloc_tokens malloc calloc realloc "free(" alloca)
set(lease_tokens TopologyLease Acquire Activate Publish Retire LOAD_Hub_ReadFile)
set(state_tokens NativeReplay Checkpoint checkpoint NativeCanonical NativeIdentity_Get)

ctr_read_source("${host_source}" source)

# 2a (LR-S9). The canonical-state token is banned in both files except the
#     linked-race drive's inputs. The header names it exactly twice: the
#     forward declaration of struct NativeCanonicalStateV4, once on its own
#     line, and RaceStep's state parameter (pinned in 3g). The .c may name
#     only the two whole type names the drive takes, NativeCanonicalStateV4
#     and NativeCanonicalInputPadV1, so a longer name
#     (NativeCanonicalStateV4_Validate) or any other canonical token still
#     trips the ban below.
string(FIND "${header}" "struct NativeCanonicalStateV4;" canonical_forward_first)
string(FIND "${header}" "struct NativeCanonicalStateV4;" canonical_forward_last REVERSE)
string(REGEX MATCH "(^|[\r\n])struct NativeCanonicalStateV4;[ \t]*[\r\n]" canonical_forward_line "${header}")
if(canonical_forward_first EQUAL -1 OR NOT canonical_forward_first EQUAL canonical_forward_last OR canonical_forward_line STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must forward-declare 'struct NativeCanonicalStateV4;' exactly once, on its own line")
endif()
string(REGEX MATCHALL "NativeCanonical" header_canonical_hits "${header}")
list(LENGTH header_canonical_hits header_canonical_count)
if(NOT header_canonical_count EQUAL 2)
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must name NativeCanonical exactly twice (the forward declaration and RaceStep's state parameter), found ${header_canonical_count}")
endif()
string(REPLACE "struct NativeCanonicalStateV4;" "struct ALLOWED_STATE_FORWARD;" header_scan "${header}")
string(REGEX REPLACE "const[ \t\r\n]+struct[ \t\r\n]+NativeCanonicalStateV4[ \t\r\n]*\\*[ \t\r\n]*state[ \t\r\n]*," "const struct ALLOWED_STATE *state,"
    header_scan "${header_scan}")
# Twice, since each match consumes the character after the name, so two
# names one character apart need a second pass.
string(REGEX REPLACE "(^|[^A-Za-z0-9_])NativeCanonical(StateV4|InputPadV1)([^A-Za-z0-9_]|$)" "\\1ALLOWED_DRIVE_TYPE\\3"
    source_scan "${source}")
string(REGEX REPLACE "(^|[^A-Za-z0-9_])NativeCanonical(StateV4|InputPadV1)([^A-Za-z0-9_]|$)" "\\1ALLOWED_DRIVE_TYPE\\3"
    source_scan "${source_scan}")

foreach(relative_path IN ITEMS "${host_header}" "${host_source}")
    if(relative_path STREQUAL host_header)
        set(text "${header_scan}")
    else()
        set(text "${source_scan}")
    endif()
    foreach(term IN LISTS network_tokens clock_game_tokens alloc_tokens lease_tokens state_tokens)
        ctr_forbid("${relative_path}" "${text}" "${term}")
    endforeach()
endforeach()

# 3. #include allowlists. The header: stdint.h and the three game-safe
#    platform headers. The .c: string.h, stdint.h, stddef.h, its own header,
#    the adapter header, the flow, options, menu-input, and identity
#    headers, <platform.h> (the pacing switch, LR-7), the race drive
#    core's header (LR-S9), and the bot rules header (the solo base and the
#    solo query's check, docs/SOLO_CAB_MILESTONE.md SOLO-6; the library
#    already reaches the bot rules through the host options, so rule 4 is
#    unchanged). The header's allowlist is unchanged by LR-S9 and SOLO-S2.
function(ctr_check_includes relative_path text allowed_pattern)
    string(REGEX MATCHALL "#[ \t]*include[^\r\n]*" include_lines "${text}")
    list(LENGTH include_lines include_count)
    if(include_count EQUAL 0)
        message(FATAL_ERROR "arcade link host isolation: found no #include lines in ${relative_path}; the scan is broken")
    endif()
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "^#[ \t]*include[ \t]*(${allowed_pattern})[ \t]*$")
            message(FATAL_ERROR "arcade link host isolation: disallowed include '${include_line}' in ${relative_path}")
        endif()
    endforeach()
endfunction()

ctr_check_includes("${host_header}" "${header}"
    "<stdint\\.h>|\"platform/native_arcade_link_options\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_identity\\.h\"")
ctr_check_includes("${host_source}" "${source}"
    "<string\\.h>|<stdint\\.h>|<stddef\\.h>|\"platform/native_arcade_bot_rules\\.h\"|\"platform/native_arcade_link_host\\.h\"|\"platform/native_arcade_link_host_internal\\.h\"|\"platform/native_arcade_netplay\\.h\"|\"platform/native_arcade_flow\\.h\"|\"platform/native_arcade_link_options\\.h\"|\"platform/native_arcade_menu_input\\.h\"|\"platform/native_identity\\.h\"|\"platform/native_arcade_race_drive\\.h\"|<platform\\.h>")

# 3b. The host-side test read-back header (MS-8): the same header-only and
#     category rules as the public header, it includes only stdint.h, and no
#     game source or main.c names it or its read-back.
set(internal_header "include/platform/native_arcade_link_host_internal.h")
ctr_read_source("${internal_header}" internal)
foreach(term IN LISTS header_only_tokens network_tokens clock_game_tokens alloc_tokens lease_tokens state_tokens)
    ctr_forbid("${internal_header}" "${internal}" "${term}")
endforeach()
ctr_check_includes("${internal_header}" "${internal}" "<stdint\\.h>")
file(GLOB_RECURSE internal_scan_paths "${repo}/game/*.c" "${repo}/game/*.h")
list(APPEND internal_scan_paths "${repo}/main.c")
foreach(path IN LISTS internal_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    file(READ "${path}" scanned)
    foreach(term IN ITEMS native_arcade_link_host_internal NativeArcadeLinkHost_Internal)
        ctr_forbid("${relative_path}" "${scanned}" "${term}")
    endforeach()
endforeach()

# 3c. The select view (MS-8) mirrors the adapter's layout without naming
#     it in the header: the .c static-asserts the capacities and sizes, and
#     the public header declares the agreed-match read and the pure entropy
#     mix.
foreach(literal IN ITEMS
        "_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS,"
        "_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS,"
        "_Static_assert(sizeof(struct NativeArcadeLinkHostSelectView) == sizeof(struct NativeArcadeNetplaySelectView),"
        "_Static_assert(sizeof(struct NativeArcadeLinkHostSelectHumanView) == sizeof(struct NativeArcadeNetplaySelectHumanView),"
        "_Static_assert(NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()
foreach(literal IN ITEMS
        "struct NativeArcadeLinkHostSelectView select;"
        "int NativeArcadeLinkHost_GetAgreedMatch(struct NativeArcadeLinkHostMatch *out);"
        "uint64_t NativeArcadeLinkHost_MixSelectEntropy(uint64_t entropy, uint64_t epoch);")
    string(FIND "${header}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_header}")
    endif()
endforeach()

# 3d. The host names for the select view and agreed-match values (MS-8b):
#     each is defined exactly once, literally, in the header, and the .c
#     static-asserts it against the module value it mirrors. The .c also
#     checks every select-view field offset against the adapter's.
foreach(definition IN ITEMS
        "SELECT_ITEM_CHARACTER=0u" "SELECT_ITEM_TRACK=1u" "SELECT_ITEM_LAPS=2u" "SELECT_ITEM_DONE=3u"
        "SELECT_LOCK_CHARACTER=0x1u" "SELECT_LOCK_TRACK=0x2u" "SELECT_LOCK_LAPS=0x4u"
        "SELECT_STATUS_PICKING=0u" "SELECT_STATUS_WAITING=1u" "SELECT_STATUS_RESOLVED=2u"
        "SELECT_STATUS_CONFIRMED=3u" "SELECT_STATUS_FAILED=4u"
        "ROLE_INACTIVE=0u" "ROLE_CAB1=1u" "ROLE_CAB2=2u" "ROLE_BOT=3u"
        "MAX_HUMANS=4u" "MAX_BOTS=8u"
        "RACE_GO=1u" "RACE_HOLD=2u" "RACE_END=3u"
        "DRIVE_END_NONE=0u" "DRIVE_END_OF_RACE=1u" "DRIVE_END_FINISH_GRACE=2u" "DRIVE_END_RACE_TICK_LIMIT=3u"
        "DRIVE_END_OUTCOME=4u" "DRIVE_END_LOCAL_FAILURE=5u" "NO_TICK=0xFFFFFFFFu" "RACE_PADS=4u")
    string(REPLACE "=" ";" definition_parts "${definition}")
    list(GET definition_parts 0 name)
    list(GET definition_parts 1 literal)
    string(REGEX MATCHALL "#define NATIVE_ARCADE_LINK_HOST_${name}[ \t]" definitions "${header}")
    list(LENGTH definitions definition_count)
    if(NOT definition_count EQUAL 1)
        message(FATAL_ERROR "arcade link host isolation: NATIVE_ARCADE_LINK_HOST_${name} must be defined exactly once in ${host_header} (found ${definition_count})")
    endif()
    string(REGEX MATCH "#define NATIVE_ARCADE_LINK_HOST_${name}[ \t]+${literal}[ \t\r\n]" defined "${header}")
    if(defined STREQUAL "")
        message(FATAL_ERROR "arcade link host isolation: NATIVE_ARCADE_LINK_HOST_${name} must be defined literally as ${literal} in ${host_header}")
    endif()
    string(FIND "${source}" "_Static_assert(NATIVE_ARCADE_LINK_HOST_${name} ==" asserted_at)
    if(asserted_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${host_source} must static-assert NATIVE_ARCADE_LINK_HOST_${name} against its module value")
    endif()
endforeach()
foreach(literal IN ITEMS
        "_Static_assert(offsetof(struct hostStruct, field) == offsetof(struct netplayStruct, field),"
        "NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, currentItem);"
        "NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, peerLockedCharacterMask);"
        "NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humans);")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()

# 3e. The race-launch host API (docs/RACE_LAUNCH_MILESTONE.md RL-S6): the
#     header forward-declares struct NativeMatchConfigV1 exactly once and
#     never defines it or names the match-config header (the include
#     allowlist in 3 is unchanged), and it declares the agreed-config copy,
#     the local race-failure input, and the racing query. The .c copies the
#     agreed config byte for byte from the adapter and reports the failure
#     through the adapter's own latch.
#     (The match text holds a ';', so it is located with FIND, not counted
#     as a CMake list.)
string(FIND "${header}" "struct NativeMatchConfigV1;" forward_first)
string(FIND "${header}" "struct NativeMatchConfigV1;" forward_last REVERSE)
string(REGEX MATCH "(^|[\r\n])struct NativeMatchConfigV1;[ \t]*[\r\n]" forward_line "${header}")
if(forward_first EQUAL -1 OR NOT forward_first EQUAL forward_last OR forward_line STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must forward-declare 'struct NativeMatchConfigV1;' exactly once, on its own line")
endif()
string(REGEX MATCH "struct[ \t\r\n]+NativeMatchConfigV1[ \t\r\n]*\\{" definition "${header}")
if(NOT definition STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must not define struct NativeMatchConfigV1")
endif()
foreach(term IN ITEMS native_match_config NATIVE_MATCH_ NativeMatchConfigV1_)
    ctr_forbid("${host_header}" "${header}" "${term}")
endforeach()
foreach(literal IN ITEMS
        "int NativeArcadeLinkHost_GetAgreedConfig(struct NativeMatchConfigV1 *out);"
        "int NativeArcadeLinkHost_ReportRaceFailure(void);"
        "uint8_t NativeArcadeLinkHost_Racing(void);"
        "int NativeArcadeLinkHost_TakeRaceEnd(struct NativeArcadeLinkHostRaceEnd *out);")
    string(FIND "${header}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_header}")
    endif()
endforeach()
foreach(literal IN ITEMS
        "agreed = NativeArcadeNetplay_AgreedConfig(&g_netplay);"
        "memcpy(out, agreed, sizeof(*out));"
        "return NativeArcadeNetplay_ReportLocalRaceFailure(&g_netplay);"
        "out->raceNumber = raceEnd.raceNumber;"
        "out->endReason = raceEnd.endReason;"
        "out->foreignBundleDrops = raceEnd.foreignBundleDrops;")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()
# The end-of-race record (docs/LOCKSTEP_RACE_MILESTONE.md LR-36): the host
# struct holds exactly the three fields TakeRaceEnd copies one by one above,
# so a new field cannot be added without this rule and the copy changing.
string(REGEX MATCH "struct NativeArcadeLinkHostRaceEnd[ \t\r\n]*\\{[^}]*\\};" race_end_struct "${header}")
if(race_end_struct STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must define struct NativeArcadeLinkHostRaceEnd")
endif()
string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" race_end_struct "${race_end_struct}")
string(REGEX REPLACE "[ \t\r\n]+" " " race_end_struct "${race_end_struct}")
if(NOT race_end_struct STREQUAL
        "struct NativeArcadeLinkHostRaceEnd { uint32_t raceNumber; uint32_t endReason; uint32_t foreignBundleDrops; };")
    message(FATAL_ERROR "arcade link host isolation: struct NativeArcadeLinkHostRaceEnd must hold exactly raceNumber, endReason, and foreignBundleDrops (uint32_t), got '${race_end_struct}'")
endif()

# 3f. The race pacing switch (docs/LOCKSTEP_RACE_MILESTONE.md LR-7, LR-S3):
#     the header declares RaceBegin and RaceEnd. The .c names exactly one
#     platform function, Platform_SetFixedVBlankPacing (so platform.h brings
#     in nothing else), and only its own g_racePacing flag gates the off
#     calls, so a pacing the host did not turn on is never touched.
foreach(literal IN ITEMS
        "int NativeArcadeLinkHost_RaceBegin(void);"
        "void NativeArcadeLinkHost_RaceEnd(void);")
    string(FIND "${header}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_header}")
    endif()
endforeach()
string(REGEX MATCHALL "Platform_[A-Za-z0-9_]*" platform_names "${source}")
list(REMOVE_DUPLICATES platform_names)
if(NOT "${platform_names}" STREQUAL "Platform_SetFixedVBlankPacing")
    message(FATAL_ERROR "arcade link host isolation: ${host_source} may name only Platform_SetFixedVBlankPacing of the platform layer (found '${platform_names}')")
endif()
foreach(literal IN ITEMS
        "static uint8_t g_racePacing;"
        "if (g_racePacing == 0u)"
        "if (g_racePacing != 0u)")
    string(FIND "${source}" "${literal}" literal_at)
    if(literal_at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: required text '${literal}' missing from ${host_source}")
    endif()
endforeach()

# 3g. The race drive glue (docs/LOCKSTEP_RACE_MILESTONE.md LR-S9). Judged on
#     the code with comments removed, so the documentation may name what it
#     describes.
#     - The header declares RaceStep and RaceHold (RaceHold takes the hold
#       loop's own arguments), the drive-state read, and the host pad and
#       race facts with exactly their fields (the pad mirrors the platform
#       input layer's pad snapshot byte for byte; the race caller
#       static-asserts that mirror, LR-S10).
#     - The .c backs the drive's callbacks with exactly one verbatim bundle
#       send over the adapter's link, exactly two RaceService calls (the poll
#       with 0, the hold's period service with 1, or with the start wait's value
#       while the held drive is on race tick 0, LR-69), and exactly one
#       OnTakeResult call whose "latched" is read back from the adapter's
#       pending link failure.
#     - One path reports a drive failure: a helper that reports only a
#       LOCAL_FAILURE end, once per race, and is the only other caller of the
#       adapter's ReportLocalRaceFailure besides the public
#       ReportRaceFailure.
#     - RaceStep and RaceHold first pass the glue's guard (only struct or
#       enum declarations without an initializer come before it), which
#       refuses outside LINK, then leaves a drive whose finish linger still
#       runs to Tick untouched (checked before the reset branch, LR-54), and
#       otherwise refuses outside a begun drive or off RACING and
#       re-initializes the drive there; the drive's Step, Hold, Begin, and
#       LingerTick are each called
#       once; the linger runs in Tick after the adapter's Tick; and the drive
#       is re-initialized by Shutdown, AbortToTitle, RaceEnd, Tick, and the
#       guard.
#     - Of game/ and main.c only the race caller, game/MAIN/MainArcadeRaceLaunch.c,
#       names RaceStep and RaceHold, each exactly once, comments included
#       (LR-S10 part 2 lifted the ban), and NativeArcadeNetplay_RaceService is
#       named in no game source and, outside the adapter's own two files,
#       only in this .c.
function(ctr_count text term out_var)
    set(count 0)
    set(rest "${text}")
    string(LENGTH "${term}" term_length)
    while(TRUE)
        string(FIND "${rest}" "${term}" at)
        if(at EQUAL -1)
            break()
        endif()
        math(EXPR count "${count} + 1")
        math(EXPR next "${at} + ${term_length}")
        string(SUBSTRING "${rest}" ${next} -1 rest)
    endwhile()
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(ctr_require_count relative_path text term expected)
    ctr_count("${text}" "${term}" found)
    if(NOT found EQUAL expected)
        message(FATAL_ERROR "arcade link host isolation: ${relative_path} must contain '${term}' exactly ${expected} time(s), found ${found}")
    endif()
endfunction()

# The body of the function whose definition starts with opener, up to its
# closing brace at the start of a line; whitespace collapsed to one space.
function(ctr_body relative_path text opener out_var)
    string(FIND "${text}" "${opener}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${relative_path} must define '${opener}'")
    endif()
    string(SUBSTRING "${text}" ${at} -1 tail)
    string(FIND "${tail}" "\n}" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: cannot find the end of '${opener}' in ${relative_path}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    string(REGEX REPLACE "[ \t\r\n]+" " " body "${body}")
    set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

# Every term after the first two arguments must appear in text. The terms
# are read one argument at a time (ARGVn), not as a CMake list, because most
# hold a ';'.
function(ctr_require_in relative_path text)
    if(ARGC LESS 3)
        message(FATAL_ERROR "arcade link host isolation: ctr_require_in needs a term")
    endif()
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE 2 ${last})
        set(term "${ARGV${index}}")
        if(term STREQUAL "")
            message(FATAL_ERROR "arcade link host isolation: ctr_require_in got an empty term; the scan is broken")
        endif()
        string(FIND "${text}" "${term}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "arcade link host isolation: ${relative_path} must contain '${term}'")
        endif()
    endforeach()
endfunction()

string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " header_code "${header}")
string(REGEX REPLACE "//[^\r\n]*" "" header_code "${header_code}")
string(REGEX REPLACE "[ \t\r\n]+" " " header_flat "${header_code}")
string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " source_code "${source}")
string(REGEX REPLACE "//[^\r\n]*" "" source_code "${source_code}")
string(REGEX REPLACE "[ \t\r\n]+" " " source_flat "${source_code}")

ctr_require_in("${host_header}" "${header_flat}"
    "uint32_t NativeArcadeLinkHost_RaceStep(uint32_t raceTick, const struct NativeCanonicalStateV4 *state, const struct NativeArcadeLinkHostPad *localSample, const struct NativeArcadeLinkHostRaceFacts *facts, struct NativeArcadeLinkHostPad padsOut[4]);"
    "uint32_t NativeArcadeLinkHost_RaceHold(uint32_t periods, int newPeriod, struct NativeArcadeLinkHostPad padsOut[4]);"
    "int NativeArcadeLinkHost_GetDriveState(struct NativeArcadeLinkHostDriveState *out);"
    "struct NativeArcadeLinkHostPad { uint8_t status; uint8_t id; uint8_t buttons[2]; uint8_t analog[4]; uint8_t connected; uint8_t reserved[3]; };"
    "struct NativeArcadeLinkHostRaceFacts { uint32_t endOfRace; uint32_t finishedHumans; uint32_t humans; };")
string(REGEX MATCH "struct[ \t\r\n]+NativeCanonicalStateV4[ \t\r\n]*\\{" canonical_definition "${header}")
if(NOT canonical_definition STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_header} must not define struct NativeCanonicalStateV4")
endif()

# The callbacks.
ctr_require_count("${host_source}" "${source_flat}" "NativeLockstepPeerLink_SendBundleVerbatim(" 1)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeNetplay_RaceService(" 2)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeNetplay_OnTakeResult(" 1)
ctr_body("${host_source}" "${source_code}" "static int NativeArcadeLinkHost_DriveSendBundle(" send_body)
ctr_require_in("${host_source} (DriveSendBundle)" "${send_body}"
    "return NativeLockstepPeerLink_SendBundleVerbatim(NativeArcadeNetplay_Link(&g_netplay), bytes, size);")
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_DrivePoll(" poll_body)
ctr_require_in("${host_source} (DrivePoll)" "${poll_body}" "NativeArcadeNetplay_RaceService(&g_netplay, 0);")
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_DriveServicePeriod(" service_body)
# LR-69: the period service passes the start wait's value exactly when the
# held drive is on race tick 0, else 1 (the capped rule).
ctr_require_in("${host_source} (DriveServicePeriod)" "${service_body}"
    "{ (void)context; NativeArcadeNetplay_RaceService(&g_netplay, (NativeArcadeRaceDrive_RaceTick(&g_drive) == 0u) ? NATIVE_ARCADE_NETPLAY_RACE_SERVICE_START_WAIT : 1);")
ctr_require_count("${host_source}" "${source_flat}" "NATIVE_ARCADE_NETPLAY_RACE_SERVICE_START_WAIT" 1)
ctr_body("${host_source}" "${source_code}" "static int NativeArcadeLinkHost_DriveTakeResult(" take_body)
ctr_require_in("${host_source} (DriveTakeResult)" "${take_body}"
    "NativeArcadeNetplay_OnTakeResult(&g_netplay, result, frameIndex); return (g_netplay.pendingLinkFailure != NATIVE_ARCADE_FLOW_END_NONE) ? 1 : 0;")
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_BeginDrive(" begin_body)
ctr_require_in("${host_source} (BeginDrive)" "${begin_body}"
    "NativeArcadeLinkHost_ResetDrive();"
    "callbacks.sendBundle = NativeArcadeLinkHost_DriveSendBundle;"
    "callbacks.poll = NativeArcadeLinkHost_DrivePoll;"
    "callbacks.onTakeResult = NativeArcadeLinkHost_DriveTakeResult;"
    "callbacks.servicePeriod = NativeArcadeLinkHost_DriveServicePeriod;"
    "NativeArcadeRaceDrive_Begin(&g_drive, NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(&g_netplay)), &g_driveKept, &callbacks, g_raceTickLimit)"
    "NativeArcadeLinkHost_ReportDriveFailure();")

# The one failure-report path.
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeNetplay_ReportLocalRaceFailure(" 2)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_ReportDriveFailure(" 3)
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_ReportDriveFailure(" report_body)
ctr_require_in("${host_source} (ReportDriveFailure)" "${report_body}"
    "if ((g_driveFailureReported != 0u) || (NativeArcadeRaceDrive_EndKind(&g_drive) != NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE)) { return; }"
    "g_driveFailureReported = 1u;"
    "(void)NativeArcadeNetplay_ReportLocalRaceFailure(&g_netplay);")
ctr_body("${host_source}" "${source_code}" "static uint32_t NativeArcadeLinkHost_DriveStatus(" status_body)
ctr_require_in("${host_source} (DriveStatus)" "${status_body}" "NativeArcadeLinkHost_ReportDriveFailure(); return NATIVE_ARCADE_LINK_HOST_RACE_END;")

# The guard, the drive calls, the linger, and the re-initialization points.
ctr_require_in("${host_source}" "${source_flat}"
    "static struct NativeArcadeRaceDrive g_drive;"
    "static struct NativeArcadeRaceDriveKept g_driveKept;")
ctr_body("${host_source}" "${source_code}" "static int NativeArcadeLinkHost_DriveMayRun(" guard_body)
ctr_require_in("${host_source} (DriveMayRun)" "${guard_body}"
    "{ if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK) { return 0; } if (NativeArcadeRaceDrive_EndIsFinish(&g_drive) && (NativeArcadeRaceDrive_LingerTicksLeft(&g_drive) > 0u)) { return 0; } if ((g_driveBegun == 0u) || (NativeArcadeLinkHost_LinkScreen() != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING)) { NativeArcadeLinkHost_ResetDrive(); return 0; } return 1;")
foreach(opener IN ITEMS "uint32_t NativeArcadeLinkHost_RaceStep(" "uint32_t NativeArcadeLinkHost_RaceHold(")
    ctr_body("${host_source}" "${source_code}" "${opener}" api_body)
    # (FIND, not a '^' REGEX REPLACE: CMake retries '^' after each match.)
    string(FIND "${api_body}" "{ " api_open)
    math(EXPR api_open "${api_open} + 2")
    string(SUBSTRING "${api_body}" ${api_open} -1 api_statements)
    string(FIND "${api_statements}" "if (!NativeArcadeLinkHost_DriveMayRun()) { return NATIVE_ARCADE_LINK_HOST_RACE_END; }" guard_at)
    # Declarations only: no initializer ('='), so nothing runs before it.
    string(REGEX MATCH "^(struct|enum)[^;=]*;( (struct|enum)[^;=]*;)* if \\(!NativeArcadeLinkHost_DriveMayRun\\(\\)\\)" guard_first "${api_statements}")
    if(guard_at EQUAL -1 OR guard_first STREQUAL "")
        message(FATAL_ERROR "arcade link host isolation: ${host_source} (${opener}) must pass NativeArcadeLinkHost_DriveMayRun() before anything else")
    endif()
endforeach()
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_DriveMayRun(" 3)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeRaceDrive_Step(" 1)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeRaceDrive_Hold(" 1)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeRaceDrive_Begin(" 1)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeRaceDrive_LingerTick(" 1)
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_TickDrive(" tick_drive_body)
ctr_require_in("${host_source} (TickDrive)" "${tick_drive_body}"
    "if ((screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING) && (screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS)) { NativeArcadeLinkHost_ResetDrive(); return; }"
    "if (NativeArcadeRaceDrive_EndIsFinish(&g_drive)) { (void)NativeArcadeRaceDrive_LingerTick(&g_drive, (screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS) ? 1 : 0); if (NativeArcadeRaceDrive_LingerTicksLeft(&g_drive) == 0u) { NativeArcadeLinkHost_ResetDrive(); } }")
ctr_body("${host_source}" "${source_code}" "uint32_t NativeArcadeLinkHost_Tick(" tick_body)
string(FIND "${tick_body}" "action = (uint32_t)NativeArcadeNetplay_Tick(&g_netplay, heldMenuButtons, raceFinished); NativeArcadeLinkHost_TickDrive(); NativeArcadeLinkHost_LatchDivergence(); return action;" tick_order_at)
if(tick_order_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: NativeArcadeLinkHost_Tick must run the drive's tick right after the adapter's Tick (LR-46), then latch the race's divergence record (LR-70), then return the action")
endif()
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_TickDrive(" 2)
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_RaceEndDrive(" race_end_drive_body)
ctr_require_in("${host_source} (RaceEndDrive)" "${race_end_drive_body}"
    "NativeArcadeRaceDrive_EndIsFinish(&g_drive)"
    "(NativeArcadeRaceDrive_LingerTicksLeft(&g_drive) > 0u)"
    "(NativeArcadeLinkHost_LinkScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS)"
    "{ g_driveBegun = 0u; return; } NativeArcadeLinkHost_ResetDrive();")
ctr_body("${host_source}" "${source_code}" "void NativeArcadeLinkHost_Shutdown(" shutdown_body)
ctr_require_in("${host_source} (Shutdown)" "${shutdown_body}" "NativeArcadeLinkHost_ResetDrive();")
ctr_body("${host_source}" "${source_code}" "void NativeArcadeLinkHost_AbortToTitle(" abort_body)
ctr_require_in("${host_source} (AbortToTitle)" "${abort_body}" "NativeArcadeNetplay_Shutdown(&g_netplay); NativeArcadeLinkHost_ResetDrive();")
ctr_body("${host_source}" "${source_code}" "int NativeArcadeLinkHost_Configure(" configure_body)
string(FIND "${configure_body}" "{ struct NativeMatchConfigV1 fixture; uint32_t i; NativeArcadeLinkHost_Shutdown();" configure_first)
if(configure_first EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: NativeArcadeLinkHost_Configure must shut down (and so re-initialize the drive) first")
endif()

# Who names the new calls.
file(GLOB_RECURSE drive_scan_paths
    "${repo}/game/*.c" "${repo}/game/*.h" "${repo}/game/*.inc"
    "${repo}/platform/*.c" "${repo}/platform/*.h" "${repo}/platform/*.inc"
    "${repo}/include/*.h" "${repo}/tools/*.c" "${repo}/tools/*.h")
list(APPEND drive_scan_paths "${repo}/main.c")
set(drive_scanned 0)
set(drive_caller_seen 0)
set(race_service_owners "platform/native_arcade_netplay.c" "include/platform/native_arcade_netplay.h" "${host_source}")
foreach(path IN LISTS drive_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    math(EXPR drive_scanned "${drive_scanned} + 1")
    file(READ "${path}" scanned)
    if(relative_path STREQUAL "game/MAIN/MainArcadeRaceLaunch.c")
        # LR-S10 part 2: the race caller is the one game caller, naming each
        # exactly once, comments included (its one call each).
        foreach(term IN ITEMS NativeArcadeLinkHost_RaceStep NativeArcadeLinkHost_RaceHold)
            ctr_count("${scanned}" "${term}" caller_term_count)
            if(NOT caller_term_count EQUAL 1)
                message(FATAL_ERROR "arcade link host isolation: ${relative_path} must name ${term} exactly once, its one call (found ${caller_term_count})")
            endif()
        endforeach()
        set(drive_caller_seen 1)
    elseif(relative_path MATCHES "^game/" OR relative_path STREQUAL "main.c")
        foreach(term IN ITEMS NativeArcadeLinkHost_RaceStep NativeArcadeLinkHost_RaceHold)
            ctr_forbid("${relative_path}" "${scanned}" "${term}")
        endforeach()
    endif()
    list(FIND race_service_owners "${relative_path}" race_service_owner)
    if(race_service_owner EQUAL -1)
        ctr_forbid("${relative_path}" "${scanned}" "NativeArcadeNetplay_RaceService")
    endif()
endforeach()
if(NOT drive_caller_seen)
    message(FATAL_ERROR "arcade link host isolation: game/MAIN/MainArcadeRaceLaunch.c, the race caller, was not scanned")
endif()
if(drive_scanned LESS 300)
    message(FATAL_ERROR "arcade link host isolation: the RaceStep/RaceHold/RaceService scan saw only ${drive_scanned} files; the scan is broken")
endif()

# 3h. The internal race tick limit override (docs/LOCKSTEP_RACE_MILESTONE.md
#     LR-60, LR-S10 part 1). The header declares the setter. The .c keeps
#     the limit in one host-local static, g_raceTickLimit, which the setter
#     writes only for 0..18000 (the drive's own bound; anything above is
#     refused with nothing changed), Shutdown (and so Configure, which shuts
#     down first, pinned above) resets to 0, BeginDrive alone hands to the
#     drive (pinned above), and the test read-back returns; it is named
#     nowhere else. Outside the host's own two files, no source names the
#     setter but main.c, which calls it exactly once, and only inside an
#     internal-build (CTR_INTERNAL) region: never in game/, platform/,
#     include/, or tools/.
ctr_require_in("${host_header}" "${header_flat}" "int NativeArcadeLinkHost_SetRaceTickLimit(uint32_t limit);")
ctr_require_in("${host_source}" "${source_flat}" "static uint32_t g_raceTickLimit;")
ctr_body("${host_source}" "${source_code}" "int NativeArcadeLinkHost_SetRaceTickLimit(" limit_body)
ctr_require_in("${host_source} (SetRaceTickLimit)" "${limit_body}"
    "{ if (limit > NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT) { return 0; } g_raceTickLimit = limit; return 1;")
ctr_require_in("${host_source} (Shutdown)" "${shutdown_body}" "g_raceTickLimit = 0u;")
ctr_body("${host_source}" "${source_code}" "uint32_t NativeArcadeLinkHost_InternalRaceTickLimit(" limit_readback_body)
ctr_require_in("${host_source} (InternalRaceTickLimit)" "${limit_readback_body}" "{ return g_raceTickLimit;")
ctr_require_count("${host_source}" "${source_flat}" "g_raceTickLimit" 5)
ctr_require_count("${host_source}" "${source_flat}" "g_raceTickLimit =" 2)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_SetRaceTickLimit(" 1)

# The preprocessor regions of code (comments removed) that hold term: sets
# out_var to 1 when every line naming term lies inside the true branch of an
# `#if defined(CTR_INTERNAL)` or `#ifdef CTR_INTERNAL`, or the #else branch of
# an `#if !defined(CTR_INTERNAL)` or `#ifndef CTR_INTERNAL`, at any depth.
function(ctr_only_internal code term out_var)
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REPLACE "\r" "" masked "${masked}")
    string(REPLACE "\n" ";" lines "${masked}")
    set(stack "")
    set(all_internal 1)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" stripped)
        if(stripped MATCHES "^#[ \t]*(if[ \t]+defined[ \t]*\\(?[ \t]*CTR_INTERNAL[ \t]*\\)?|ifdef[ \t]+CTR_INTERNAL)[ \t]*$")
            list(APPEND stack "I")
        elseif(stripped MATCHES "^#[ \t]*(if[ \t]+![ \t]*defined[ \t]*\\(?[ \t]*CTR_INTERNAL[ \t]*\\)?|ifndef[ \t]+CTR_INTERNAL)[ \t]*$")
            list(APPEND stack "N")
        elseif(stripped MATCHES "^#[ \t]*if")
            list(APPEND stack "O")
        elseif(stripped MATCHES "^#[ \t]*(else|elif)")
            list(LENGTH stack depth)
            if(depth EQUAL 0)
                message(FATAL_ERROR "arcade link host isolation: an #else without an #if; the region scan is broken")
            endif()
            list(POP_BACK stack top)
            if(top STREQUAL "N" AND stripped MATCHES "^#[ \t]*else")
                list(APPEND stack "I")
            else()
                list(APPEND stack "O")
            endif()
        elseif(stripped MATCHES "^#[ \t]*endif")
            list(LENGTH stack depth)
            if(depth EQUAL 0)
                message(FATAL_ERROR "arcade link host isolation: an #endif without an #if; the region scan is broken")
            endif()
            list(POP_BACK stack top)
        endif()
        string(FIND "${line}" "${term}" term_at)
        if(NOT term_at EQUAL -1)
            list(FIND stack "I" internal_at)
            if(internal_at EQUAL -1)
                set(all_internal 0)
            endif()
        endif()
    endforeach()
    list(LENGTH stack depth)
    if(NOT depth EQUAL 0)
        message(FATAL_ERROR "arcade link host isolation: unbalanced #if in the region scan")
    endif()
    set(${out_var} ${all_internal} PARENT_SCOPE)
endfunction()
# The region scan's self-check.
foreach(probe IN ITEMS
        "1|#if defined(CTR_INTERNAL)\n\tX();\n#endif\n"
        "1|#if defined(CTR_NATIVE)\n#ifdef CTR_INTERNAL\nif (a) { X(); }\n#endif\n#endif\n"
        "1|#if !defined(CTR_INTERNAL)\nY();\n#else\nX();\n#endif\n"
        "0|X();\n#if defined(CTR_INTERNAL)\n#endif\n"
        "0|#if defined(CTR_INTERNAL)\nY();\n#else\nX();\n#endif\n"
        "0|#if !defined(CTR_INTERNAL)\nX();\n#endif\n"
        "0|#if defined(CTR_INTERNAL)\n#endif\n#if defined(CTR_NATIVE)\nX();\n#endif\n")
    string(FIND "${probe}" "|" bar_at)
    string(SUBSTRING "${probe}" 0 ${bar_at} probe_expected)
    math(EXPR probe_text_at "${bar_at} + 1")
    string(SUBSTRING "${probe}" ${probe_text_at} -1 probe_text)
    ctr_only_internal("${probe_text}" "X()" probe_result)
    if(NOT probe_result EQUAL probe_expected)
        message(FATAL_ERROR "arcade link host isolation: the CTR_INTERNAL region scan got ${probe_result} for '${probe_text}', expected ${probe_expected}")
    endif()
endforeach()
set(limit_owners "${host_source}" "${host_header}")
set(limit_named_in_main 0)
foreach(path IN LISTS drive_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    list(FIND limit_owners "${relative_path}" limit_owner)
    if(NOT limit_owner EQUAL -1)
        continue()
    endif()
    file(READ "${path}" scanned)
    string(FIND "${scanned}" "NativeArcadeLinkHost_SetRaceTickLimit" limit_at)
    if(limit_at EQUAL -1)
        continue()
    endif()
    if(NOT relative_path STREQUAL "main.c")
        message(FATAL_ERROR "arcade link host isolation: ${relative_path} names NativeArcadeLinkHost_SetRaceTickLimit; only main.c may, in an internal-build region (LR-60)")
    endif()
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " main_code "${scanned}")
    string(REGEX REPLACE "//[^\r\n]*" "" main_code "${main_code}")
    ctr_count("${main_code}" "NativeArcadeLinkHost_SetRaceTickLimit(" main_limit_calls)
    ctr_count("${main_code}" "NativeArcadeLinkHost_SetRaceTickLimit" main_limit_names)
    if(NOT main_limit_calls EQUAL 1 OR NOT main_limit_names EQUAL 1)
        message(FATAL_ERROR "arcade link host isolation: main.c must name NativeArcadeLinkHost_SetRaceTickLimit exactly once, in its one call (found ${main_limit_names} names, ${main_limit_calls} calls)")
    endif()
    ctr_only_internal("${main_code}" "NativeArcadeLinkHost_SetRaceTickLimit" main_limit_internal)
    if(NOT main_limit_internal)
        message(FATAL_ERROR "arcade link host isolation: main.c names NativeArcadeLinkHost_SetRaceTickLimit outside a CTR_INTERNAL region (LR-60)")
    endif()
    set(limit_named_in_main 1)
endforeach()
if(NOT limit_named_in_main)
    message(FATAL_ERROR "arcade link host isolation: main.c must set the race tick limit (NativeArcadeLinkHost_SetRaceTickLimit) in its internal autopilot handling")
endif()

# 3i. The divergence record (docs/LOCKSTEP_RACE_MILESTONE.md LR-11, LR-70,
#     LR-S12). The header declares the take and the record with exactly its
#     six fields (pointer-free). The .c latches it in one helper, which reads
#     the race link's session only through NativeLockstepSession_FirstDivergence
#     (named once), skips a race number already latched, and copies the
#     report's frame, canonical domain mask, and digests (the lowest differing
#     domain's, else the combined ones; the whole body is pinned) with the
#     link's match count; it is called exactly three times: in Tick right after
#     the drive's tick (pinned with the Tick order above), and in RaceStep and
#     RaceHold right after the drive's call, before the status. The take
#     copies the record field by field, zeroes reserved, and clears the
#     pending flag, which only the helper sets. Shutdown and AbortToTitle drop
#     it. Of game/ and main.c only the hook, game/MAIN/MainArcadeLink.c, names
#     the take, exactly once, comments included (its one call, logged there,
#     tests/main_arcade_link_hook_isolation_test.cmake 12c).
ctr_require_in("${host_header}" "${header_flat}"
    "int NativeArcadeLinkHost_TakeRaceDivergence(struct NativeArcadeLinkHostRaceDivergence *out);"
    "struct NativeArcadeLinkHostRaceDivergence { uint32_t raceNumber; uint32_t raceTick; uint32_t domainMask; uint32_t reserved; uint64_t localDigest; uint64_t remoteDigest; };")
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_LatchDivergence(" latch_body)
ctr_require_in("${host_source} (LatchDivergence)" "${latch_body}"
    "{ const struct NativeLockstepDivergenceReport *report; uint32_t domain; if ((g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK) || (g_netplay.matchCount == 0u) || (g_raceDivergenceRace == g_netplay.matchCount)) { return; } report = NativeLockstepSession_FirstDivergence(NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(&g_netplay))); if (report == NULL) { return; } memset(&g_raceDivergence, 0, sizeof(g_raceDivergence)); g_raceDivergence.raceNumber = g_netplay.matchCount; g_raceDivergence.raceTick = report->frameIndex; g_raceDivergence.domainMask = report->canonicalDomainMask; g_raceDivergence.localDigest = report->localCombinedDigest; g_raceDivergence.remoteDigest = report->remoteCombinedDigest; for (domain = 0u; domain < NATIVE_CANONICAL_DOMAIN_COUNT; domain++) { if ((report->canonicalDomainMask & (UINT32_C(1) << domain)) != 0u) { g_raceDivergence.localDigest = report->localDomainDigests[domain]; g_raceDivergence.remoteDigest = report->remoteDomainDigests[domain]; break; } } g_raceDivergencePending = 1u; g_raceDivergenceRace = g_netplay.matchCount;")
ctr_require_count("${host_source}" "${source_flat}" "NativeLockstepSession_FirstDivergence(" 1)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_LatchDivergence(" 4)
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_LatchDivergence(); return NativeArcadeLinkHost_DriveStatus(status, pads, padsOut);" 2)
ctr_body("${host_source}" "${source_code}" "uint32_t NativeArcadeLinkHost_RaceStep(" step_body)
ctr_require_in("${host_source} (RaceStep)" "${step_body}"
    "(padsOut != NULL) ? pads : NULL); NativeArcadeLinkHost_LatchDivergence(); return NativeArcadeLinkHost_DriveStatus(status, pads, padsOut);")
ctr_body("${host_source}" "${source_code}" "uint32_t NativeArcadeLinkHost_RaceHold(" hold_body)
ctr_require_in("${host_source} (RaceHold)" "${hold_body}"
    "status = NativeArcadeRaceDrive_Hold(&g_drive, periods, newPeriod, (padsOut != NULL) ? pads : NULL); NativeArcadeLinkHost_LatchDivergence(); return NativeArcadeLinkHost_DriveStatus(status, pads, padsOut);")
ctr_body("${host_source}" "${source_code}" "int NativeArcadeLinkHost_TakeRaceDivergence(" take_divergence_body)
ctr_require_in("${host_source} (TakeRaceDivergence)" "${take_divergence_body}"
    "{ if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK) || (g_raceDivergencePending == 0u)) { return 0; } out->raceNumber = g_raceDivergence.raceNumber; out->raceTick = g_raceDivergence.raceTick; out->domainMask = g_raceDivergence.domainMask; out->reserved = 0u; out->localDigest = g_raceDivergence.localDigest; out->remoteDigest = g_raceDivergence.remoteDigest; g_raceDivergencePending = 0u; return 1;")
ctr_require_count("${host_source}" "${source_flat}" "g_raceDivergencePending = 1u;" 1)
ctr_body("${host_source}" "${source_code}" "static void NativeArcadeLinkHost_ResetDivergence(" reset_divergence_body)
ctr_require_in("${host_source} (ResetDivergence)" "${reset_divergence_body}"
    "{ memset(&g_raceDivergence, 0, sizeof(g_raceDivergence)); g_raceDivergencePending = 0u; g_raceDivergenceRace = 0u;")
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeLinkHost_ResetDivergence(" 3)
ctr_require_in("${host_source} (Shutdown)" "${shutdown_body}" "NativeArcadeLinkHost_ResetDrive(); NativeArcadeLinkHost_ResetDivergence();")
ctr_require_in("${host_source} (AbortToTitle)" "${abort_body}" "NativeArcadeLinkHost_ResetDrive(); NativeArcadeLinkHost_ResetDivergence();")
set(divergence_take_seen 0)
foreach(path IN LISTS drive_scan_paths)
    file(RELATIVE_PATH relative_path "${repo}" "${path}")
    if(relative_path STREQUAL host_source OR relative_path STREQUAL host_header)
        continue()
    endif()
    file(READ "${path}" scanned)
    ctr_count("${scanned}" "NativeArcadeLinkHost_TakeRaceDivergence" divergence_take_count)
    if(relative_path STREQUAL "game/MAIN/MainArcadeLink.c")
        if(NOT divergence_take_count EQUAL 1)
            message(FATAL_ERROR "arcade link host isolation: ${relative_path} must name NativeArcadeLinkHost_TakeRaceDivergence exactly once, its one call (found ${divergence_take_count})")
        endif()
        set(divergence_take_seen 1)
    elseif(NOT divergence_take_count EQUAL 0)
        message(FATAL_ERROR "arcade link host isolation: ${relative_path} names NativeArcadeLinkHost_TakeRaceDivergence; only the hook, game/MAIN/MainArcadeLink.c, may (LR-70)")
    endif()
endforeach()
if(NOT divergence_take_seen)
    message(FATAL_ERROR "arcade link host isolation: game/MAIN/MainArcadeLink.c, the hook, was not scanned")
endif()

# 3j. Solo (docs/SOLO_CAB_MILESTONE.md SOLO-S2). The view grows by one
#     4-byte group appended after the select view, and the header declares
#     the solo query. The .c builds the ONE_CAB solo base beside the fixture
#     in Configure, and the solo query returns the link's solo race config
#     only through one check that fails closed: an ARCADE_ONE_CAB config that
#     passes NativeArcadeBotRules_ValidateConfigV1 (SOLO-6); the adapter's
#     solo config is named nowhere else. The solo gate is dark (SOLO-11): its
#     default is literally 0u, its only other write is the unit tests'
#     internal setter (which rule 3b keeps out of game/ and main.c), and
#     Configure turns solo on only through it. SOLO-S4 flips the default.
ctr_require_in("${host_header}" "${header_flat}"
    "struct NativeArcadeLinkHostSelectView select; uint8_t solo; uint8_t soloOffered; uint8_t peerHeard; uint8_t reserved; };"
    "int NativeArcadeLinkHost_GetSoloConfig(struct NativeMatchConfigV1 *out);")
string(REGEX MATCH "\n#define NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT 0u\r?\n" solo_default "${source}")
if(solo_default STREQUAL "")
    message(FATAL_ERROR "arcade link host isolation: ${host_source} must define NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT as 0u on a line of its own (SOLO-11: solo stays dark until SOLO-S4)")
endif()
ctr_require_count("${host_source}" "${source_flat}" "NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT" 2)
ctr_require_in("${host_source}" "${source_flat}"
    "static uint8_t g_soloEnabled = NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT;")
ctr_require_count("${host_source}" "${source_flat}" "g_soloEnabled =" 2)
ctr_body("${host_source}" "${source_code}" "void NativeArcadeLinkHost_InternalSetSoloEnabled(" solo_setter_body)
ctr_require_in("${host_source} (InternalSetSoloEnabled)" "${solo_setter_body}"
    "{ g_soloEnabled = (uint8_t)((enabled != 0u) ? 1u : 0u);")
ctr_require_in("${host_source} (Configure)" "${configure_body}"
    "g_config.soloEnabled = 0u; if ((g_soloEnabled != 0u) && NativeArcadeLinkHost_BuildSoloBase(&fixture, &g_config.soloBase)) { g_config.soloEnabled = 1u; }")
ctr_require_count("${host_source}" "${source_flat}" "soloEnabled =" 4)
ctr_body("${host_source}" "${source_code}" "int NativeArcadeLinkHost_InternalCopyValidSoloConfig(" solo_check_body)
ctr_require_in("${host_source} (InternalCopyValidSoloConfig)" "${solo_check_body}"
    "if ((candidate == NULL) || (out == NULL)) { return 0; } if ((candidate->profile != NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB) || !NativeArcadeBotRules_ValidateConfigV1(candidate)) { return 0; } memcpy(out, candidate, sizeof(*out)); return 1;")
ctr_body("${host_source}" "${source_code}" "int NativeArcadeLinkHost_GetSoloConfig(" solo_query_body)
ctr_require_in("${host_source} (GetSoloConfig)" "${solo_query_body}"
    "return NativeArcadeLinkHost_InternalCopyValidSoloConfig(NativeArcadeNetplay_SoloConfig(&g_netplay), out);")
ctr_require_count("${host_source}" "${source_flat}" "NativeArcadeNetplay_SoloConfig(" 1)
ctr_body("${host_source}" "${source_code}" "static int NativeArcadeLinkHost_BuildSoloBase(" solo_base_body)
ctr_require_in("${host_source} (BuildSoloBase)" "${solo_base_body}"
    "NativeMatchConfigV1_InitArcadeOneCab(&candidate);"
    "NativeArcadeBotRules_ExpectedBots1P(fixture->slots[cab1Slot].characterID, bots)"
    "!NativeArcadeBotRules_Digest1PV1(candidate.botRulesDigest) || !NativeArcadeBotRules_ValidateConfigV1(&candidate)")

# 4. ctr_native_arcade_link_host links exactly the adapter and the host
#    options, in exactly one target_link_libraries call. Its one other
#    link-time dependency (since LR-S3, LR-7) is not a library:
#    Platform_SetFixedVBlankPacing (rule 3f), which only the ctr_native
#    executable defines (platform/native_platform.c) and which every test
#    that links this library stubs.
ctr_read_source("CMakeLists.txt" cmake)
set(target ctr_native_arcade_link_host)
string(REGEX MATCHALL "target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n][^)]*\\)" link_calls "${cmake}")
list(LENGTH link_calls link_call_count)
if(NOT link_call_count EQUAL 1)
    message(FATAL_ERROR "arcade link host isolation: expected exactly one target_link_libraries(${target} ...) call, found ${link_call_count}")
endif()
list(GET link_calls 0 link_call)
string(REGEX REPLACE "^target_link_libraries\\([ \t\r\n]*${target}[ \t\r\n]+" "" link_body "${link_call}")
string(REGEX REPLACE "\\)$" "" link_body "${link_body}")
string(REGEX REPLACE "[ \t\r\n]+" ";" link_items "${link_body}")
list(REMOVE_ITEM link_items "" PUBLIC PRIVATE INTERFACE)
set(expected_link_items ctr_native_arcade_netplay ctr_native_arcade_link_options ctr_native_arcade_race_drive)
foreach(expected IN LISTS expected_link_items)
    list(FIND link_items "${expected}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${target} must link ${expected} (found '${link_items}')")
    endif()
endforeach()
foreach(item IN LISTS link_items)
    list(FIND expected_link_items "${item}" item_index)
    if(item_index EQUAL -1)
        message(FATAL_ERROR "arcade link host isolation: ${target} links unexpected item '${item}'; only ctr_native_arcade_netplay, ctr_native_arcade_link_options, and ctr_native_arcade_race_drive are allowed")
    endif()
endforeach()
list(LENGTH link_items link_item_count)
if(NOT link_item_count EQUAL 3)
    message(FATAL_ERROR "arcade link host isolation: ${target} must link exactly three libraries, found ${link_item_count} ('${link_items}')")
endif()

# 5. C17, no extensions, in order, on the host glue target.
string(FIND "${cmake}" "add_library(${target} STATIC" declare_at)
if(declare_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: missing add_library(${target} STATIC ...) in CMakeLists.txt")
endif()
string(SUBSTRING "${cmake}" "${declare_at}" 400 target_block)
string(FIND "${target_block}" "set_target_properties(${target} PROPERTIES" properties_at)
if(properties_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: missing set_target_properties(${target} PROPERTIES ...) in CMakeLists.txt")
endif()
string(FIND "${target_block}" "C_STANDARD 17" standard_at)
string(FIND "${target_block}" "C_STANDARD_REQUIRED ON" required_at)
string(FIND "${target_block}" "C_EXTENSIONS OFF" extensions_at)
if(standard_at EQUAL -1 OR required_at EQUAL -1 OR extensions_at EQUAL -1)
    message(FATAL_ERROR "arcade link host isolation: ${target} is missing C_STANDARD 17 / C_STANDARD_REQUIRED ON / C_EXTENSIONS OFF")
endif()
if(NOT (properties_at LESS standard_at AND standard_at LESS required_at AND required_at LESS extensions_at))
    message(FATAL_ERROR "arcade link host isolation: ${target} C17/no-extensions properties are out of order")
endif()
