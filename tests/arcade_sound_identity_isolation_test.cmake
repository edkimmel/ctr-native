# Structural isolation for sound IDs and cross-cabinet identity
# (docs/RACE_LAUNCH_MILESTONE.md section 4 RL-14, slice RL-S3). countSounds
# (the sdata field, include/regionsEXE.h:3262, incremented by CountSounds at
# game/HOWL/HOWL_OtherFX.c:6) and every ID OtherFX_Play returns stay
# host-local: the two cabinets never agree on them, so they must never reach
# anything the cabinets compare or exchange.
#
# 1. The identity-bearing sources (match config, select rules, message, and
#    session, the launch record, netplay, lobby, lockstep, bot rules,
#    canonical codecs, the deterministic RNG, identity, the race setup plan,
#    facts, core, and setup adapter, the race digest, the roster, the bot
#    setup, setup v4, and the MainCanonical* encoders) name no sound-ID
#    identifier: countSounds,
#    CountSounds, any identifier starting with OtherFX_ (OtherFX_Play,
#    OtherFX_Play_LowLevel, OtherFX_RecycleNew, ...), EngineAudio_,
#    PlaySound3D, Level_Sound, Voiceline, howl_, or Howl_, and any identifier
#    containing soundID, soundId, or SoundID (rainSoundID,
#    OptionSlider_soundID, ...). The list below is hard-coded (each file must
#    exist) and then extended by globs over the file families, so a new
#    lockstep, canonical, race setup, or MainCanonical file is covered too.
# 2. The only retail sound call in the arcade-link hook
#    (game/MAIN/MainArcadeLink.c) discards its return value: every
#    OtherFX_Play call is written (void)OtherFX_Play(...), and the hook names
#    no other OtherFX_ identifier, so no sound ID is kept.
# 3. The host-local sound state stays out of the netplay adapter and the
#    host glue: neither names MainArcadeLinkSound or OtherFX.
#
# Every check runs on the code with every // and /* */ comment removed in
# one left-to-right pass (each comment replaced by a space, as the C
# preprocessor does), so comment prose that explains the rule does not trip
# it, and a /* inside a // comment cannot swallow code.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "arcade sound identity isolation")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: forbidden token '${term}' found in the code of ${relative_path}")
    endif()
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${term}' missing from ${relative_path}")
    endif()
endfunction()

# Removes every /* */ and // comment in one left-to-right pass, replacing
# each with a space: whichever comment opens first wins, so a /* inside a //
# comment opens nothing and a // inside a /* */ comment ends nothing. A /*
# left over afterwards opened a comment that never closed, which is an error
# rather than a silent truncation.
function(ctr_strip_comments label source out_var)
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\r\n]*" " " stripped "${source}")
    string(FIND "${stripped}" "/*" open_at)
    if(NOT open_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: unterminated /* comment in ${label}")
    endif()
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# Identifier-aware sound-ID match. The code is padded with a leading space
# so the non-identifier character before a prefix match always exists; CMake
# regex has no word boundary. Sets out_var to the first offending identifier,
# or to the empty string.
set(sound_prefix_regex "[^A-Za-z0-9_](OtherFX_|EngineAudio_|PlaySound3D|Level_Sound|Voiceline|howl_|Howl_)[A-Za-z0-9_]*")
set(sound_infix_regex "[A-Za-z0-9_]*(countSounds|CountSounds|soundID|soundId|SoundID)[A-Za-z0-9_]*")
function(ctr_find_sound_identifier code out_var)
    string(REGEX MATCH "${sound_prefix_regex}" prefix_hit " ${code}")
    if(NOT "${prefix_hit}" STREQUAL "")
        string(REGEX REPLACE "^[^A-Za-z0-9_]" "" prefix_hit "${prefix_hit}")
        set(${out_var} "${prefix_hit}" PARENT_SCOPE)
        return()
    endif()
    string(REGEX MATCH "${sound_infix_regex}" infix_hit "${code}")
    set(${out_var} "${infix_hit}" PARENT_SCOPE)
endfunction()

function(ctr_forbid_sound_identifiers relative_path code)
    ctr_find_sound_identifier("${code}" hit)
    if(NOT "${hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: sound-ID identifier '${hit}' found in the code of ${relative_path} (RL-14)")
    endif()
endfunction()

# 0a. The comment stripper itself: comments go, code stays.
ctr_strip_comments("self-check" "code1 /* countSounds\n soundID */ code2 // OtherFX_Play\ncode3 /**/code4 /** x **/ code5 /* a // b */ code6" self_check)
foreach(term IN ITEMS countSounds soundID OtherFX_Play "/*" "*/" "//")
    ctr_forbid("self-check" "${self_check}" "${term}")
endforeach()
foreach(term IN ITEMS code1 code2 code3 code4 code5 code6)
    ctr_require("self-check" "${self_check}" "${term}")
endforeach()
# A /* inside a // comment opens nothing: the code line after it survives.
ctr_strip_comments("self-check" "a // x /*\ncountSounds\n/* */ b" self_check)
foreach(term IN ITEMS a countSounds b)
    ctr_require("self-check" "${self_check}" "${term}")
endforeach()
foreach(term IN ITEMS x "/*" "*/" "//")
    ctr_forbid("self-check" "${self_check}" "${term}")
endforeach()

# 0b. The identifier matcher itself: every banned shape is caught, at the
#     start of the text and mid-line, and near misses are not.
foreach(sample IN ITEMS
        "countSounds" "sdata->countSounds += 1;" "x = CountSounds();"
        "OtherFX_Play(1, 1);" "(void)OtherFX_Play_LowLevel(a, b, c);" "OtherFX_RecycleNew(p, 1, 2);"
        "EngineAudio_Recalculate(d);" "PlaySound3D(a, b);" "Level_SoundLoopSet(a);"
        "Voiceline_RequestPlay(a, b, c);" "howl_VolumeGet(0);" "Howl_x = 1;"
        "uint32_t soundID;" "p->rainSoundID = 0;" "OptionSlider_soundID[i]" "int soundId;" "int lastSoundID;")
    ctr_find_sound_identifier("${sample}" hit)
    if("${hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: self-check: the matcher missed a sound-ID identifier in '${sample}'")
    endif()
endforeach()
foreach(sample IN ITEMS
        "NoOtherFX_Play(1);" "myhowl_x = 1;" "xHowl_y = 2;" "SoundIdx" "countSound;" "Sound_ID;"
        "OtherFX;" "OtherFXPlay(1);")
    ctr_find_sound_identifier("${sample}" hit)
    if(NOT "${hit}" STREQUAL "")
        message(FATAL_ERROR "${prefix}: self-check: the matcher flagged '${hit}' in the near miss '${sample}'")
    endif()
endforeach()

# 1. Identity-bearing sources name no sound-ID identifier. Every file listed
#    here must exist.
set(identity_files
    platform/native_arcade_bot_rules.c
    platform/native_arcade_launch.c
    platform/native_arcade_netplay.c
    platform/native_canonical_codec.c
    platform/native_canonical_driver_behavior.c
    platform/native_canonical_drivers.c
    platform/native_canonical_drivers_detailed.c
    platform/native_canonical_drivers_roster.c
    platform/native_canonical_fixed_array.c
    platform/native_canonical_pool.c
    platform/native_canonical_state.c
    platform/native_canonical_state_v3.c
    platform/native_canonical_state_v4.c
    platform/native_canonical_topology.c
    platform/native_canonical_world_counters.c
    platform/native_canonical_world_mine_registry.c
    platform/native_deterministic_rng.c
    platform/native_identity.c
    platform/native_lobby_state.c
    platform/native_lockstep_handshake.c
    platform/native_lockstep_input_window.c
    platform/native_lockstep_match_outcome.c
    platform/native_lockstep_match_roster.c
    platform/native_lockstep_peer_link.c
    platform/native_lockstep_protocol.c
    platform/native_lockstep_rematch.c
    platform/native_lockstep_session.c
    platform/native_match_config.c
    platform/native_match_select_message.c
    platform/native_match_select_rules.c
    platform/native_match_select_session.c
    include/platform/native_arcade_bot_rules.h
    include/platform/native_arcade_launch.h
    include/platform/native_arcade_netplay.h
    include/platform/native_canonical_codec.h
    include/platform/native_canonical_driver_behavior.h
    include/platform/native_canonical_drivers.h
    include/platform/native_canonical_drivers_detailed.h
    include/platform/native_canonical_drivers_roster.h
    include/platform/native_canonical_fixed_array.h
    include/platform/native_canonical_pool.h
    include/platform/native_canonical_projector.h
    include/platform/native_canonical_state.h
    include/platform/native_canonical_state_v3.h
    include/platform/native_canonical_state_v4.h
    include/platform/native_canonical_topology.h
    include/platform/native_canonical_world_counters.h
    include/platform/native_canonical_world_mine_registry.h
    include/platform/native_deterministic_rng.h
    include/platform/native_identity.h
    include/platform/native_lobby_state.h
    include/platform/native_lockstep_handshake.h
    include/platform/native_lockstep_input_window.h
    include/platform/native_lockstep_match_outcome.h
    include/platform/native_lockstep_match_roster.h
    include/platform/native_lockstep_peer_link.h
    include/platform/native_lockstep_protocol.h
    include/platform/native_lockstep_rematch.h
    include/platform/native_lockstep_session.h
    include/platform/native_match_config.h
    include/platform/native_match_select_message.h
    include/platform/native_match_select_rules.h
    include/platform/native_match_select_session.h
    game/MAIN/MainArcadeBotSetup.c
    game/MAIN/MainArcadeBotSetup.h
    game/MAIN/MainArcadeRaceDigest.c
    game/MAIN/MainArcadeRaceDigest.h
    game/MAIN/MainArcadeRaceSetup.c
    game/MAIN/MainArcadeRaceSetup.h
    game/MAIN/MainArcadeRaceSetupCore.c
    game/MAIN/MainArcadeRaceSetupCore.h
    game/MAIN/MainArcadeRaceSetupFacts.c
    game/MAIN/MainArcadeRaceSetupFacts.h
    game/MAIN/MainArcadeRaceSetupPlan.c
    game/MAIN/MainArcadeRaceSetupPlan.h
    game/MAIN/MainArcadeRoster.c
    game/MAIN/MainArcadeRoster.h
    game/MAIN/MainArcadeSetupV4.c
    game/MAIN/MainArcadeSetupV4.h
    game/MAIN/MainCanonicalDrivers.c
    game/MAIN/MainCanonicalDrivers.h
    game/MAIN/MainCanonicalRuntime.c
    game/MAIN/MainCanonicalRuntime.h
    game/MAIN/MainCanonicalState.c
    game/MAIN/MainCanonicalStateV4.c
    game/MAIN/MainCanonicalStateV4ExactFacts.c
    game/MAIN/MainCanonicalStateV4ExactFacts.h
    game/MAIN/MainCanonicalTopology.c
    game/MAIN/MainCanonicalTopology.h
    game/MAIN/MainCanonicalTopologyFacts.c
    game/MAIN/MainCanonicalTopologyFacts.h
    game/MAIN/MainCanonicalTopologyLayoutContract.h
    game/MAIN/MainCanonicalTopologyLeaseAdapter.c
    game/MAIN/MainCanonicalTopologyLeaseAdapter.h
    game/MAIN/MainCanonicalTopologyLeaseAuthority.c
    game/MAIN/MainCanonicalTopologyLeaseAuthority.h
    game/MAIN/MainCanonicalTopologyLeaseRuntime.c
    game/MAIN/MainCanonicalTopologyLeaseRuntime.h
    game/MAIN/MainCanonicalTopologyLifecycleEvents.c
    game/MAIN/MainCanonicalTopologyLifecycleEvents.h
    game/MAIN/MainCanonicalWorldCounters.c
    game/MAIN/MainCanonicalWorldCounters.h
    game/MAIN/MainCanonicalWorldMineRegistry.c
    game/MAIN/MainCanonicalWorldMineRegistry.h)
list(LENGTH identity_files identity_floor)
foreach(relative_path IN LISTS identity_files)
    if(NOT EXISTS "${repo}/${relative_path}")
        message(FATAL_ERROR "${prefix}: missing identity-bearing source ${relative_path}")
    endif()
endforeach()

#    The families grow: the globs add any new file in them, and must at
#    least find every listed member, so a broken glob fails loudly.
set(family_globs
    "platform/native_lockstep_*.c" "include/platform/native_lockstep_*.h"
    "platform/native_canonical_*.c" "include/platform/native_canonical_*.h"
    "platform/native_match_select_*.c" "include/platform/native_match_select_*.h"
    "game/MAIN/MainArcadeRaceSetup*.c" "game/MAIN/MainArcadeRaceSetup*.h"
    "game/MAIN/MainCanonical*.c" "game/MAIN/MainCanonical*.h")
set(family_patterns "")
foreach(pattern IN LISTS family_globs)
    list(APPEND family_patterns "${repo}/${pattern}")
endforeach()
file(GLOB family_paths RELATIVE "${repo}" ${family_patterns})
set(listed_family_count 0)
foreach(relative_path IN LISTS identity_files)
    if(relative_path MATCHES "^(platform/native_lockstep_|include/platform/native_lockstep_|platform/native_canonical_|include/platform/native_canonical_|platform/native_match_select_|include/platform/native_match_select_|game/MAIN/MainArcadeRaceSetup|game/MAIN/MainCanonical)")
        math(EXPR listed_family_count "${listed_family_count} + 1")
        list(FIND family_paths "${relative_path}" found_at)
        if(found_at EQUAL -1)
            message(FATAL_ERROR "${prefix}: the family globs did not find ${relative_path}; the scan is broken")
        endif()
    endif()
endforeach()
list(LENGTH family_paths family_count)
if(family_count LESS listed_family_count)
    message(FATAL_ERROR "${prefix}: the family globs found ${family_count} files, fewer than the ${listed_family_count} listed; the scan is broken")
endif()

set(scan_files ${identity_files} ${family_paths})
list(REMOVE_DUPLICATES scan_files)
list(LENGTH scan_files scan_count)
if(scan_count LESS identity_floor)
    message(FATAL_ERROR "${prefix}: scanned ${scan_count} files, fewer than the floor of ${identity_floor}; the scan is broken")
endif()

foreach(relative_path IN LISTS scan_files)
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${relative_path}" "${source}" code)
    ctr_forbid_sound_identifiers("${relative_path}" "${code}")
endforeach()

# 2. The arcade-link hook's one retail sound call discards the sound ID it
#    returns: every OtherFX_Play call is (void)OtherFX_Play(...), there is at
#    least one, and the hook names no other OtherFX_ identifier.
set(hook_path "game/MAIN/MainArcadeLink.c")
ctr_read_source("${hook_path}" hook)
ctr_strip_comments("${hook_path}" "${hook}" hook_code)
string(REGEX MATCHALL "[^A-Za-z0-9_]OtherFX_Play[ \t\r\n]*\\(" play_calls " ${hook_code}")
string(REGEX MATCHALL "\\([ \t\r\n]*void[ \t\r\n]*\\)[ \t\r\n]*OtherFX_Play[ \t\r\n]*\\(" void_play_calls "${hook_code}")
string(REGEX MATCHALL "[^A-Za-z0-9_]OtherFX_[A-Za-z0-9_]*" other_fx_names " ${hook_code}")
list(LENGTH play_calls play_call_count)
list(LENGTH void_play_calls void_play_call_count)
list(LENGTH other_fx_names other_fx_count)
if(play_call_count EQUAL 0)
    message(FATAL_ERROR "${prefix}: ${hook_path} has no OtherFX_Play call; the scan is broken")
endif()
if(NOT play_call_count EQUAL void_play_call_count)
    message(FATAL_ERROR "${prefix}: ${hook_path} has ${play_call_count} OtherFX_Play calls but only ${void_play_call_count} written (void)OtherFX_Play(...); a kept sound ID must not exist (RL-14)")
endif()
if(NOT other_fx_count EQUAL play_call_count)
    message(FATAL_ERROR "${prefix}: ${hook_path} names ${other_fx_count} OtherFX_ identifiers but makes ${play_call_count} OtherFX_Play calls; only the discarded OtherFX_Play call is allowed (found '${other_fx_names}')")
endif()

# 3. The host-local sound state stays out of the netplay adapter and the
#    host glue.
foreach(relative_path IN ITEMS
        platform/native_arcade_netplay.c include/platform/native_arcade_netplay.h
        platform/native_arcade_link_host.c include/platform/native_arcade_link_host.h)
    ctr_read_source("${relative_path}" source)
    ctr_strip_comments("${relative_path}" "${source}" code)
    foreach(term IN ITEMS MainArcadeLinkSound OtherFX)
        ctr_forbid("${relative_path}" "${code}" "${term}")
    endforeach()
endforeach()

message(STATUS "${prefix}: scanned ${scan_count} identity-bearing files (floor ${identity_floor}); ${play_call_count} discarded OtherFX_Play call in ${hook_path}")
