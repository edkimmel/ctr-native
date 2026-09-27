# Structural guard for the bot plant-eaten camera fix in game/BOTS.c
# (BOTS_ThTick_Drive, damage branch BOTS_DAMAGE_STATE_MASK_GRAB with
# botDriver->plantEatingMe).
#
# GameTracker has four camera pushBuffers, followed by DecalMP. Retail writes
# gGT->pushBuffer[botDriver->driverID].pos/rot while a plant eats a bot, for
# any driverID, so bots 4-7 write past the array into DecalMP (in non-LINK 2P
# bot 5 overwrites entry 0's renderBucketOTRangeEnd). Render-only on the PS1
# too. The native build must skip every pushBuffer access in that block for
# driverID >= BOTS_PLANT_CAMERA_COUNT (tied to the array length), after the
# GTE RotTrans and while keeping the non-camera statements (speedLinear,
# rotXZ, kartState, flags) unconditional.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(bots_path "game/BOTS.c")

if(NOT EXISTS "${repo}/${bots_path}")
    message(FATAL_ERROR "bots plant camera isolation: missing source ${bots_path}")
endif()
file(READ "${repo}/${bots_path}" bots_source)

# The camera count constant is tied to the pushBuffer array length.
string(FIND "${bots_source}" "BOTS_PLANT_CAMERA_COUNT = 4," count_at)
if(count_at EQUAL -1)
    message(FATAL_ERROR "bots plant camera isolation: ${bots_path} must define BOTS_PLANT_CAMERA_COUNT = 4")
endif()
string(FIND "${bots_source}" "CTR_STATIC_ASSERT(BOTS_PLANT_CAMERA_COUNT == len(((struct GameTracker *)0)->pushBuffer));" assert_at)
if(assert_at EQUAL -1)
    message(FATAL_ERROR
        "bots plant camera isolation: ${bots_path} must static-assert BOTS_PLANT_CAMERA_COUNT == len(GameTracker.pushBuffer)")
endif()

# Locate the plant branch.
string(FIND "${bots_source}" "struct Thread *plant = botDriver->plantEatingMe;" plant_at)
if(plant_at EQUAL -1)
    message(FATAL_ERROR "bots plant camera isolation: plant-eaten branch not found in ${bots_path}")
endif()
string(SUBSTRING "${bots_source}" ${plant_at} -1 plant_tail)

set(guard_text "if (botDriver->driverID < BOTS_PLANT_CAMERA_COUNT)")
set(speed_text "botDriver->botData.aiPhysics.speedLinear = 0;")

string(FIND "${plant_tail}" "RotTrans(&v, &v2, &l3);" rottrans_at)
string(FIND "${plant_tail}" "${guard_text}" guard_at)
string(FIND "${plant_tail}" "${speed_text}" speed_at)
string(FIND "${plant_tail}" "botDriver->kartState = newKartState;" kart_at)
if(rottrans_at EQUAL -1 OR guard_at EQUAL -1 OR speed_at EQUAL -1 OR kart_at EQUAL -1)
    message(FATAL_ERROR
        "bots plant camera isolation: the plant branch must hold RotTrans, the '${guard_text}' guard, the speedLinear write, and the kartState write")
endif()
if(NOT (rottrans_at LESS guard_at AND guard_at LESS speed_at AND speed_at LESS kart_at))
    message(FATAL_ERROR
        "bots plant camera isolation: expected order RotTrans, driverID guard, speedLinear, kartState in the plant branch")
endif()

# The guard body opens right after the condition.
string(LENGTH "${guard_text}" guard_len)
math(EXPR after_guard "${guard_at} + ${guard_len}")
string(SUBSTRING "${plant_tail}" ${after_guard} -1 guard_tail)
string(REGEX MATCH "^[ \t\r\n]*\\{" guard_open "${guard_tail}")
if("${guard_open}" STREQUAL "")
    message(FATAL_ERROR "bots plant camera isolation: the driverID guard must open a braced block")
endif()

# Between the guard and speedLinear the braces close the guard block and the
# enclosing rotXZ < 0xb40 block: speedLinear (and everything after it) is
# outside both, so it stays unconditional for every bot.
math(EXPR guarded_len "${speed_at} - ${after_guard}")
string(SUBSTRING "${plant_tail}" ${after_guard} ${guarded_len} guarded_region)
string(REGEX MATCHALL "\\{" opens "${guarded_region}")
string(REGEX MATCHALL "\\}" closes "${guarded_region}")
list(LENGTH opens open_count)
list(LENGTH closes close_count)
math(EXPR expected_closes "${open_count} + 1")
if(NOT close_count EQUAL expected_closes)
    message(FATAL_ERROR
        "bots plant camera isolation: speedLinear must follow the close of both the driverID guard and the rotXZ block (found ${open_count} '{' and ${close_count} '}' between them)")
endif()

# The guard block ends before the enclosing block: find the guard's closing
# brace and require every driverID-indexed pushBuffer access inside it.
set(depth 0)
set(pos 0)
set(guard_end -1)
string(LENGTH "${guarded_region}" region_len)
while(pos LESS region_len)
    string(SUBSTRING "${guarded_region}" ${pos} 1 ch)
    if(ch STREQUAL "{")
        math(EXPR depth "${depth} + 1")
    elseif(ch STREQUAL "}")
        math(EXPR depth "${depth} - 1")
        if(depth EQUAL 0)
            set(guard_end ${pos})
            break()
        endif()
    endif()
    math(EXPR pos "${pos} + 1")
endwhile()
if(guard_end EQUAL -1)
    message(FATAL_ERROR "bots plant camera isolation: closing brace of the driverID guard not found")
endif()
string(SUBSTRING "${guarded_region}" 0 ${guard_end} guard_body)

string(REGEX MATCHALL "pushBuffer\\[botDriver->driverID\\]" guarded_accesses "${guard_body}")
list(LENGTH guarded_accesses guarded_count)
if(NOT guarded_count EQUAL 7)
    message(FATAL_ERROR
        "bots plant camera isolation: expected the 7 pushBuffer[botDriver->driverID] accesses (pos.x/y/z, pos.y read, rot.y/x/z) inside the guard, found ${guarded_count}")
endif()

# No other driverID-indexed pushBuffer access (code, not comments: a member
# access such as botDriver->driverID) anywhere in BOTS.c.
string(REGEX MATCHALL "pushBuffer\\[[^]]*->driverID[^]]*\\]" all_accesses "${bots_source}")
list(LENGTH all_accesses all_count)
if(NOT all_count EQUAL guarded_count)
    message(FATAL_ERROR
        "bots plant camera isolation: ${bots_path} has ${all_count} driverID-indexed pushBuffer accesses; only the ${guarded_count} inside the plant camera guard are allowed")
endif()

# The non-camera statements are not inside the guard.
foreach(stmt
        "botDriver->botData.aiPhysics.speedLinear = 0;"
        "botDriver->botData.aiPhysics.rotXZ = iVar4;"
        "botDriver->kartState = newKartState;"
        "BOTS_MaskGrab(botThread);")
    string(FIND "${guard_body}" "${stmt}" stmt_at)
    if(NOT stmt_at EQUAL -1)
        message(FATAL_ERROR "bots plant camera isolation: '${stmt}' must stay outside the driverID guard")
    endif()
endforeach()
