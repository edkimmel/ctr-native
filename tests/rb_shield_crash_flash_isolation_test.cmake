# Structural guard for the shield crash-attack flash fix in
# game/231/RB_MaskShieldCloud.c (RB_ShieldDark_ThTick_Grow, the
# SHIELD_FLAG_CRASH_ATTACK block of the pop transition).
#
# GameTracker has four camera pushBuffers, followed by DecalMP. Retail writes
# the white flash (fadeFromBlack_currentValue, fadeFromBlack_desiredResult,
# fade_step) to pushBuffer[owner->driverID] with no bounds check. Only human
# drivers (driverID 0..3) can own a shield today, so the fix is defensive: the
# native build writes the flash only for driverID < RB_SHIELD_FLASH_CAMERA_COUNT
# (tied to the array length), while shield->animFrame = 0,
# owner->instBubbleHold = NULL and the ThTick_SetAndExec pop transition stay
# unconditional.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(shield_path "game/231/RB_MaskShieldCloud.c")

if(NOT EXISTS "${repo}/${shield_path}")
    message(FATAL_ERROR "rb shield crash flash isolation: missing source ${shield_path}")
endif()
file(READ "${repo}/${shield_path}" shield_source)

# The camera count constant is tied to the pushBuffer array length.
string(FIND "${shield_source}" "RB_SHIELD_FLASH_CAMERA_COUNT = 4," count_at)
if(count_at EQUAL -1)
    message(FATAL_ERROR "rb shield crash flash isolation: ${shield_path} must define RB_SHIELD_FLASH_CAMERA_COUNT = 4")
endif()
string(FIND "${shield_source}"
    "CTR_STATIC_ASSERT(RB_SHIELD_FLASH_CAMERA_COUNT == len(((struct GameTracker *)0)->pushBuffer));" assert_at)
if(assert_at EQUAL -1)
    message(FATAL_ERROR
        "rb shield crash flash isolation: ${shield_path} must static-assert RB_SHIELD_FLASH_CAMERA_COUNT == len(GameTracker.pushBuffer)")
endif()

# Locate RB_ShieldDark_ThTick_Grow and its pop transition.
string(FIND "${shield_source}" "void RB_ShieldDark_ThTick_Grow(struct Thread *th)" grow_at)
if(grow_at EQUAL -1)
    message(FATAL_ERROR "rb shield crash flash isolation: RB_ShieldDark_ThTick_Grow not found in ${shield_path}")
endif()
string(SUBSTRING "${shield_source}" ${grow_at} -1 grow_tail)

string(FIND "${grow_tail}" "struct Driver *owner = th->parentThread->object;" owner_at)
if(owner_at EQUAL -1)
    message(FATAL_ERROR "rb shield crash flash isolation: the pop transition owner declaration was not found")
endif()
string(SUBSTRING "${grow_tail}" ${owner_at} -1 pop_tail)

set(guard_text
    "if ((shield->flags & SHIELD_FLAG_CRASH_ATTACK) && (owner->driverID < RB_SHIELD_FLASH_CAMERA_COUNT))")
set(anim_text "shield->animFrame = 0;")
set(bubble_text "owner->instBubbleHold = NULL;")
set(exec_text "ThTick_SetAndExec(th, RB_ShieldDark_ThTick_Pop);")

string(FIND "${pop_tail}" "${guard_text}" guard_at)
string(FIND "${pop_tail}" "${anim_text}" anim_at)
string(FIND "${pop_tail}" "${bubble_text}" bubble_at)
string(FIND "${pop_tail}" "${exec_text}" exec_at)
if(guard_at EQUAL -1 OR anim_at EQUAL -1 OR bubble_at EQUAL -1 OR exec_at EQUAL -1)
    message(FATAL_ERROR
        "rb shield crash flash isolation: the pop transition must hold the '${guard_text}' guard, '${anim_text}', '${bubble_text}' and '${exec_text}'")
endif()
if(NOT (guard_at LESS anim_at AND anim_at LESS bubble_at AND bubble_at LESS exec_at))
    message(FATAL_ERROR
        "rb shield crash flash isolation: expected order driverID guard, animFrame reset, instBubbleHold clear, ThTick_SetAndExec in the pop transition")
endif()

# The guard body opens right after the condition; find its closing brace.
string(LENGTH "${guard_text}" guard_len)
math(EXPR after_guard "${guard_at} + ${guard_len}")
string(SUBSTRING "${pop_tail}" ${after_guard} -1 guard_tail)
string(REGEX MATCH "^[ \t\r\n]*\\{" guard_open "${guard_tail}")
if("${guard_open}" STREQUAL "")
    message(FATAL_ERROR "rb shield crash flash isolation: the driverID guard must open a braced block")
endif()

set(depth 0)
set(pos 0)
set(guard_end -1)
string(LENGTH "${guard_tail}" tail_len)
while(pos LESS tail_len)
    string(SUBSTRING "${guard_tail}" ${pos} 1 ch)
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
    message(FATAL_ERROR "rb shield crash flash isolation: closing brace of the driverID guard not found")
endif()
string(SUBSTRING "${guard_tail}" 0 ${guard_end} guard_body)

# All three flash writes sit inside the guard.
foreach(field fadeFromBlack_currentValue fadeFromBlack_desiredResult fade_step)
    string(REGEX MATCH "pushBuffer\\[[^]]*->driverID\\]\\.${field} =" field_match "${guard_body}")
    if("${field_match}" STREQUAL "")
        message(FATAL_ERROR
            "rb shield crash flash isolation: the pushBuffer[driverID].${field} write must sit inside the driverID guard")
    endif()
endforeach()
string(REGEX MATCHALL "pushBuffer\\[[^]]*->driverID[^]]*\\]" guarded_accesses "${guard_body}")
list(LENGTH guarded_accesses guarded_count)
if(NOT guarded_count EQUAL 3)
    message(FATAL_ERROR
        "rb shield crash flash isolation: expected the 3 pushBuffer[driverID] flash writes inside the guard, found ${guarded_count}")
endif()

# No other driverID-indexed pushBuffer access anywhere in the file.
# Limit (same as bots_plant_camera_isolation_test.cmake): this is a textual
# match, so an aliased index (e.g. `int id = owner->driverID; ...
# pushBuffer[id]`) is not caught.
string(REGEX MATCHALL "pushBuffer\\[[^]]*->driverID[^]]*\\]" all_accesses "${shield_source}")
list(LENGTH all_accesses all_count)
if(NOT all_count EQUAL guarded_count)
    message(FATAL_ERROR
        "rb shield crash flash isolation: ${shield_path} has ${all_count} driverID-indexed pushBuffer accesses; only the ${guarded_count} inside the crash-attack flash guard are allowed")
endif()

# The pop transition statements are not inside the guard.
foreach(stmt "${anim_text}" "${bubble_text}" "${exec_text}")
    string(FIND "${guard_body}" "${stmt}" stmt_at)
    if(NOT stmt_at EQUAL -1)
        message(FATAL_ERROR "rb shield crash flash isolation: '${stmt}' must stay outside the driverID guard")
    endif()
endforeach()
