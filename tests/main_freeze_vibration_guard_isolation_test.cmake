# Structural pin for the pause-menu vibration guard (docs/RACE_LAUNCH_MILESTONE.md
# RL-13, slice RL-S9) in game/MAIN/MainFreeze.c:
#  1. the DualShock row's gameMode1 vibration toggle sits in its own block,
#     guarded by `if (MainArcadeRaceSetup_Status() == MAIN_ARCADE_RACE_SETUP_IDLE)`
#     inside one #if defined(CTR_NATIVE) block, so a linked race (setup not
#     IDLE) skips the write and default boot (IDLE) keeps the retail toggle;
#  2. with every CTR_NATIVE block removed the row is retail: the toggle alone
#     in a plain block, and no race setup name remains;
#  3. the confirm sound before the row split and the analog-controller row
#     (the else branch) stay retail: no guard, no race setup name;
#  4. the guarded toggle is the only gameMode1 vibration write in the file:
#     VibPerPlayer is named exactly twice (the toggle and the options-row
#     read), no gameMode1 ^= appears elsewhere, and no P1..P4_VIBRATE bit is
#     named;
#  5. MainFreeze.c names exactly one race setup entry point,
#     MainArcadeRaceSetup_Status, once, and includes MAIN/MainArcadeRaceSetup.h
#     only inside #if defined(CTR_NATIVE); the unity chain includes the
#     adapter before MainFreeze.c.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(prefix "pause vibration guard isolation")
set(freeze_path "game/MAIN/MainFreeze.c")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${prefix}: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    # Line endings depend on the checkout; the checks below assume LF.
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

# Removes /* */ and // comments.
function(ctr_strip_comments source out_var)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${source}")
    string(REGEX REPLACE "//[^\r\n]*" "" stripped "${stripped}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# Counts the whole-identifier occurrences of name in code.
function(ctr_count_identifier code name out_var)
    # ';' is masked first: a match holding it would count as two list items;
    # '[' and ']' too: an unbalanced bracket in an item hides the next ';'.
    string(REPLACE ";" "@SEMI@" masked "${code}")
    string(REPLACE "[" "@LB@" masked "${masked}")
    string(REPLACE "]" "@RB@" masked "${masked}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" hits "${masked}")
    list(LENGTH hits count)
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Sets out_body to the braces of the first block after opener, inclusive.
function(ctr_block_after relative_path source opener out_body)
    string(FIND "${source}" "${opener}" opener_at)
    if(opener_at EQUAL -1)
        message(FATAL_ERROR "${prefix}: required text '${opener}' missing from ${relative_path}")
    endif()
    string(SUBSTRING "${source}" ${opener_at} -1 tail)
    string(FIND "${tail}" "{" brace_offset)
    if(brace_offset EQUAL -1)
        message(FATAL_ERROR "${prefix}: no block follows '${opener}' in ${relative_path}")
    endif()
    math(EXPR begin "${opener_at} + ${brace_offset}")
    string(LENGTH "${source}" length)
    set(depth 0)
    set(position ${begin})
    while(position LESS length)
        string(SUBSTRING "${source}" ${position} 1 character)
        if(character STREQUAL "{")
            math(EXPR depth "${depth} + 1")
        elseif(character STREQUAL "}")
            math(EXPR depth "${depth} - 1")
            if(depth EQUAL 0)
                math(EXPR body_length "${position} - ${begin} + 1")
                string(SUBSTRING "${source}" ${begin} ${body_length} body)
                set(${out_body} "${body}" PARENT_SCOPE)
                return()
            endif()
        endif()
        math(EXPR position "${position} + 1")
    endwhile()
    message(FATAL_ERROR "${prefix}: unbalanced block after '${opener}' in ${relative_path}")
endfunction()

# Removes every #if defined(CTR_NATIVE) ... #endif block holding no other
# directive than #include: the text a non-native build compiles.
function(ctr_strip_native_blocks source out_var)
    string(REGEX REPLACE "#if defined\\(CTR_NATIVE\\)\n([^#]|#include[^\n]*\n)*#endif\n" "" stripped "${source}")
    set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

set(ws "[ \t\n]*")
set(toggle "gGT->gameMode1 \\^= data\\.gGT_gameMode1_VibPerPlayer\\[gamepad->gamepadId\\[gamepadRow\\]\\]@SEMI@")
set(guarded_toggle
    "#if defined\\(CTR_NATIVE\\)${ws}if \\(MainArcadeRaceSetup_Status\\(\\) == MAIN_ARCADE_RACE_SETUP_IDLE\\)${ws}#endif${ws}\\{${ws}${toggle}${ws}\\}")

# The checks must themselves bite: an unguarded, a mis-guarded, and a
# native-only guard that is not a CTR_NATIVE block all fail the pattern.
set(probe_good "#if defined(CTR_NATIVE)\n\tif (MainArcadeRaceSetup_Status() == MAIN_ARCADE_RACE_SETUP_IDLE)\n#endif\n\t{\n\t\tgGT->gameMode1 ^= data.gGT_gameMode1_VibPerPlayer[gamepad->gamepadId[gamepadRow]]@SEMI@\n\t}")
if(NOT probe_good MATCHES "${guarded_toggle}")
    message(FATAL_ERROR "${prefix}: the guard pattern rejects the guarded toggle; the check is broken")
endif()
foreach(probe IN ITEMS
        "{\n\t\tgGT->gameMode1 ^= data.gGT_gameMode1_VibPerPlayer[gamepad->gamepadId[gamepadRow]]@SEMI@\n\t}"
        "#if defined(CTR_NATIVE)\n\tif (MainArcadeRaceSetup_Status() != MAIN_ARCADE_RACE_SETUP_IDLE)\n#endif\n\t{\n\t\tgGT->gameMode1 ^= data.gGT_gameMode1_VibPerPlayer[gamepad->gamepadId[gamepadRow]]@SEMI@\n\t}"
        "#if defined(CTR_INTERNAL)\n\tif (MainArcadeRaceSetup_Status() == MAIN_ARCADE_RACE_SETUP_IDLE)\n#endif\n\t{\n\t\tgGT->gameMode1 ^= data.gGT_gameMode1_VibPerPlayer[gamepad->gamepadId[gamepadRow]]@SEMI@\n\t}")
    if(probe MATCHES "${guarded_toggle}")
        message(FATAL_ERROR "${prefix}: the guard pattern accepts '${probe}'; the check is broken")
    endif()
endforeach()
foreach(probe_pair "a\n#if defined(CTR_NATIVE)\nx\n#endif\nb\n|a\nb\n"
        "a\n#if defined(CTR_NATIVE)\n#include \"h.h\"\n#endif\nb\n|a\nb\n"
        "a\n#if defined(CTR_INTERNAL)\nx\n#endif\nb\n|a\n#if defined(CTR_INTERNAL)\nx\n#endif\nb\n"
        "a\n#if defined(CTR_NATIVE)\nx\n#else\ny\n#endif\nb\n|a\n#if defined(CTR_NATIVE)\nx\n#else\ny\n#endif\nb\n")
    string(REPLACE "|" ";" probe_items "${probe_pair}")
    list(GET probe_items 0 probe_in)
    list(GET probe_items 1 probe_expected)
    ctr_strip_native_blocks("${probe_in}" probe_retail)
    if(NOT probe_retail STREQUAL probe_expected)
        message(FATAL_ERROR "${prefix}: the CTR_NATIVE block removal is broken ('${probe_in}' gave '${probe_retail}')")
    endif()
endforeach()

ctr_read_source("${freeze_path}" freeze)
ctr_strip_comments("${freeze}" freeze_code)
string(REPLACE ";" "@SEMI@" freeze_masked "${freeze_code}")

# The pause options handler's DualShock/analog rows (case 4..7): the labels,
# the test-sound clear, the confirm tap, the confirm sound, and the row split,
# with nothing between them.
set(rows_opener "case 4:\n\t\tcase 5:\n\t\tcase 6:\n\t\tcase 7:")
string(REGEX MATCHALL "case 4:${ws}case 5:${ws}case 6:${ws}case 7:" rows_labels "${freeze_masked}")
list(LENGTH rows_labels rows_label_count)
string(FIND "${freeze_masked}" "${rows_opener}" rows_at)
if(NOT rows_label_count EQUAL 1 OR rows_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${freeze_path} must hold the pause options DualShock/analog rows (case 4..7) exactly once (found ${rows_label_count})")
endif()
string(SUBSTRING "${freeze_masked}" ${rows_at} -1 rows_tail)
set(tap "if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))")
ctr_block_after("${freeze_path}" "${rows_tail}" "${tap}" rows_body)
ctr_block_after("${freeze_path}" "${rows_body}" "if (gamepadRow < gamepad->numGamepads)" dualshock_body)
string(FIND "${rows_body}" "if (gamepadRow < gamepad->numGamepads)" split_at)
string(SUBSTRING "${rows_body}" 0 ${split_at} before_split)
string(FIND "${rows_body}" "${dualshock_body}" dualshock_at)
string(LENGTH "${dualshock_body}" dualshock_length)
math(EXPR after_dualshock_at "${dualshock_at} + ${dualshock_length}")
string(SUBSTRING "${rows_body}" ${after_dualshock_at} -1 after_dualshock)
if(NOT after_dualshock MATCHES "^${ws}else${ws}\\{")
    message(FATAL_ERROR "${prefix}: ${freeze_path}: the analog-controller row must be the else branch right after the DualShock row")
endif()
ctr_block_after("${freeze_path}" "${after_dualshock}" "else" analog_body)

# 1. The DualShock row: the toggle, guarded, inside one CTR_NATIVE block.
if(NOT dualshock_body MATCHES "^\\{${ws}${guarded_toggle}${ws}\\}$")
    message(FATAL_ERROR "${prefix}: ${freeze_path}: the DualShock row must hold only the gameMode1 vibration toggle, in its own block, guarded by 'if (MainArcadeRaceSetup_Status() == MAIN_ARCADE_RACE_SETUP_IDLE)' inside #if defined(CTR_NATIVE) (found '${dualshock_body}')")
endif()

# 2. The retail build of the row: the toggle alone in a plain block.
ctr_strip_native_blocks("${dualshock_body}" dualshock_retail)
if(NOT dualshock_retail MATCHES "^\\{${ws}\\{${ws}${toggle}${ws}\\}${ws}\\}$")
    message(FATAL_ERROR "${prefix}: ${freeze_path}: without CTR_NATIVE the DualShock row must be the retail toggle alone (found '${dualshock_retail}')")
endif()
ctr_strip_native_blocks("${freeze_masked}" freeze_retail)
string(FIND "${freeze_retail}" "MainArcadeRaceSetup" retail_setup_hit)
string(FIND "${freeze_retail}" "MAIN_ARCADE_RACE_SETUP" retail_status_hit)
if(NOT retail_setup_hit EQUAL -1 OR NOT retail_status_hit EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${freeze_path} names the race setup outside a #if defined(CTR_NATIVE) block")
endif()

# 3. The confirm sound and the analog-controller row stay retail.
if(NOT rows_tail MATCHES "^case 4:${ws}case 5:${ws}case 6:${ws}case 7:${ws}OptionsMenu_TestSound\\(0, 0\\)@SEMI@${ws}if \\(sdata->AnyPlayerTap & \\(BTN_CIRCLE \\| BTN_CROSS_one\\)\\)${ws}\\{${ws}OtherFX_Play\\(1, 1\\)@SEMI@${ws}int gamepadRow = menu->rowSelected - 4@SEMI@${ws}if \\(gamepadRow < gamepad->numGamepads\\)")
    message(FATAL_ERROR "${prefix}: ${freeze_path}: the DualShock/analog rows must play the confirm sound unguarded, right before the row split")
endif()
if(NOT analog_body MATCHES "^\\{${ws}sdata->gamepadID_OwnerRaceWheelConfig = gamepad->analogId\\[gamepadRow - gamepad->numGamepads\\]@SEMI@${ws}sdata->boolOpenWheelConfig = true@SEMI@${ws}sdata->raceWheelConfigPageIndex = 0@SEMI@${ws}\\}$")
    message(FATAL_ERROR "${prefix}: ${freeze_path}: the analog-controller row must stay retail, unguarded (found '${analog_body}')")
endif()
foreach(term IN ITEMS "#" "MainArcadeRaceSetup" "MAIN_ARCADE_RACE_SETUP")
    string(FIND "${before_split}${analog_body}" "${term}" term_hit)
    if(NOT term_hit EQUAL -1)
        message(FATAL_ERROR "${prefix}: ${freeze_path}: '${term}' in the confirm sound or the analog-controller row; only the DualShock toggle is guarded")
    endif()
endforeach()

# 4. The guarded toggle is the file's only gameMode1 vibration write.
ctr_count_identifier("${freeze_code}" "gGT_gameMode1_VibPerPlayer" vib_hits)
if(NOT vib_hits EQUAL 2)
    message(FATAL_ERROR "${prefix}: ${freeze_path} must name gGT_gameMode1_VibPerPlayer exactly twice, the guarded toggle and the options-row read (found ${vib_hits})")
endif()
string(REGEX MATCHALL "${guarded_toggle}" guarded_hits "${freeze_masked}")
list(LENGTH guarded_hits guarded_count)
if(NOT guarded_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${freeze_path} must hold the guarded toggle exactly once (found ${guarded_count})")
endif()
string(FIND "${freeze_code}" "b32 boolDisabled = (gGT->gameMode1 & data.gGT_gameMode1_VibPerPlayer[currPad]) != 0;" read_at)
if(read_at EQUAL -1)
    message(FATAL_ERROR "${prefix}: ${freeze_path}: the options-row vibration read is not the retail read")
endif()
string(REGEX MATCHALL "gameMode1[ \t\n]*\\^=" xor_writes "${freeze_masked}")
list(LENGTH xor_writes xor_count)
if(NOT xor_count EQUAL 1)
    message(FATAL_ERROR "${prefix}: ${freeze_path} must toggle gameMode1 with ^= exactly once, the guarded vibration toggle (found ${xor_count})")
endif()
string(REGEX MATCH "(^|[^A-Za-z0-9_])P[1-4]_VIBRATE([^A-Za-z0-9_]|$)" vibrate_bit "${freeze_code}")
if(NOT vibrate_bit STREQUAL "")
    message(FATAL_ERROR "${prefix}: ${freeze_path} names the vibration bit ${vibrate_bit}; only the guarded toggle may change the vibration bits")
endif()

# 5. One race setup name, one include, and the unity order.
string(REGEX MATCHALL "MainArcadeRaceSetup[A-Za-z0-9_]*" setup_names "${freeze_code}")
if(NOT "${setup_names}" STREQUAL "MainArcadeRaceSetup;MainArcadeRaceSetup_Status")
    message(FATAL_ERROR "${prefix}: ${freeze_path} must name only its header and MainArcadeRaceSetup_Status, once each (found '${setup_names}')")
endif()
if(NOT freeze MATCHES "^#include <common.h>\n\n#if defined\\(CTR_NATIVE\\)\n#include \"MAIN/MainArcadeRaceSetup\\.h\"\n#endif\n")
    message(FATAL_ERROR "${prefix}: ${freeze_path} must include MAIN/MainArcadeRaceSetup.h right after <common.h>, inside #if defined(CTR_NATIVE)")
endif()
ctr_read_source("game/game_unity.h" unity)
string(FIND "${unity}" "#include \"MAIN/MainArcadeRaceSetup.c\"" adapter_at)
string(FIND "${unity}" "#include \"MAIN/MainFreeze.c\"" freeze_at)
if(adapter_at EQUAL -1 OR freeze_at EQUAL -1 OR NOT adapter_at LESS freeze_at)
    message(FATAL_ERROR "${prefix}: game/game_unity.h must include MAIN/MainArcadeRaceSetup.c before MAIN/MainFreeze.c")
endif()
