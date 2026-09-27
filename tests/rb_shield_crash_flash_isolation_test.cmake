# Structural guard for the shield crash-attack flash fix in
# game/231/RB_MaskShieldCloud.c (RB_ShieldDark_ThTick_Grow, the
# SHIELD_FLAG_CRASH_ATTACK block of the pop transition).
#
# GameTracker has four camera pushBuffers, followed by DecalMP. Retail writes
# the white flash (fadeFromBlack_currentValue, fadeFromBlack_desiredResult,
# fade_step) to pushBuffer[owner->driverID] with no bounds check. Only human
# drivers (driverID 0..3) can own a shield today, so the fix is defensive and
# native-only: game/231 is a whole-artifact retail match target, so the
# condition is split by `#ifdef CTR_NATIVE`. The CTR_NATIVE branch writes the
# flash only for driverID < RB_SHIELD_FLASH_CAMERA_COUNT (tied to the array
# length); the `#else` branch (the retail-matching build) keeps the original
# retail condition line exactly. In both builds shield->animFrame = 0,
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

set(owner_text "struct Driver *owner = th->parentThread->object;")
string(FIND "${grow_tail}" "${owner_text}" owner_at)
if(owner_at EQUAL -1)
    message(FATAL_ERROR "rb shield crash flash isolation: the pop transition owner declaration was not found")
endif()
string(LENGTH "${owner_text}" owner_len)
math(EXPR after_owner "${owner_at} + ${owner_len}")
string(SUBSTRING "${grow_tail}" ${after_owner} -1 pop_tail)

set(guard_text
    "if ((shield->flags & SHIELD_FLAG_CRASH_ATTACK) && (owner->driverID < RB_SHIELD_FLASH_CAMERA_COUNT))")
set(retail_text "if (shield->flags & SHIELD_FLAG_CRASH_ATTACK)")
set(anim_text "shield->animFrame = 0;")
set(bubble_text "owner->instBubbleHold = NULL;")
set(exec_text "ThTick_SetAndExec(th, RB_ShieldDark_ThTick_Pop);")

# The condition directly after the owner declaration is split by
# `#ifdef CTR_NATIVE` / `#else` / `#endif`.
string(REGEX MATCH "^[ \t\r\n]*#ifdef CTR_NATIVE[ \t]*\r?\n" ifdef_match "${pop_tail}")
if("${ifdef_match}" STREQUAL "")
    message(FATAL_ERROR
        "rb shield crash flash isolation: the crash-attack flash condition must follow the owner declaration under '#ifdef CTR_NATIVE' (game/231 is a retail match target)")
endif()
string(LENGTH "${ifdef_match}" ifdef_len)
string(SUBSTRING "${pop_tail}" ${ifdef_len} -1 native_tail)

string(REGEX MATCH "\n#else[ \t]*\r?\n" else_match "${native_tail}")
string(FIND "${native_tail}" "${else_match}" else_at)
if("${else_match}" STREQUAL "" OR else_at EQUAL -1)
    message(FATAL_ERROR "rb shield crash flash isolation: the '#ifdef CTR_NATIVE' flash condition must have an '#else' retail branch")
endif()
string(SUBSTRING "${native_tail}" 0 ${else_at} native_branch)
string(LENGTH "${else_match}" else_len)
math(EXPR after_else "${else_at} + ${else_len}")
string(SUBSTRING "${native_tail}" ${after_else} -1 else_tail)

# The CTR_NATIVE branch holds only comments and the driverID guard condition.
string(REGEX REPLACE "//[^\r\n]*" "" native_code "${native_branch}")
string(STRIP "${native_code}" native_code)
if(NOT "${native_code}" STREQUAL "${guard_text}")
    message(FATAL_ERROR
        "rb shield crash flash isolation: the CTR_NATIVE branch must hold exactly '${guard_text}' (plus comments), found '${native_code}'")
endif()

# The #else branch (retail-matching build) holds the original retail
# condition line exactly, with no comments or other code.
string(REGEX MATCH "\n#endif[^\n]*\n" endif_match "${else_tail}")
string(FIND "${else_tail}" "${endif_match}" endif_at)
if("${endif_match}" STREQUAL "" OR endif_at EQUAL -1)
    message(FATAL_ERROR "rb shield crash flash isolation: the crash-attack flash '#else' branch must close with '#endif'")
endif()
string(SUBSTRING "${else_tail}" 0 ${endif_at} retail_branch)
string(REGEX REPLACE "\r" "" retail_branch "${retail_branch}")
if(NOT "${retail_branch}" STREQUAL "\t\t${retail_text}")
    message(FATAL_ERROR
        "rb shield crash flash isolation: the '#else' branch must hold exactly the original retail line '${retail_text}', found '${retail_branch}'")
endif()
string(LENGTH "${endif_match}" endif_len)
math(EXPR after_endif "${endif_at} + ${endif_len}")
string(SUBSTRING "${else_tail}" ${after_endif} -1 guard_tail)

# The shared guard body opens right after #endif; find its closing brace.
string(REGEX MATCH "^[ \t\r\n]*\{" guard_open "${guard_tail}")
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

# The pop transition statements follow the guard, in order, outside it.
math(EXPR after_body "${guard_end} + 1")
string(SUBSTRING "${guard_tail}" ${after_body} -1 post_guard)
string(FIND "${post_guard}" "${anim_text}" anim_at)
string(FIND "${post_guard}" "${bubble_text}" bubble_at)
string(FIND "${post_guard}" "${exec_text}" exec_at)
if(anim_at EQUAL -1 OR bubble_at EQUAL -1 OR exec_at EQUAL -1)
    message(FATAL_ERROR
        "rb shield crash flash isolation: the pop transition must hold '${anim_text}', '${bubble_text}' and '${exec_text}' after the driverID guard")
endif()
if(NOT (anim_at LESS bubble_at AND bubble_at LESS exec_at))
    message(FATAL_ERROR
        "rb shield crash flash isolation: expected order driverID guard, animFrame reset, instBubbleHold clear, ThTick_SetAndExec in the pop transition")
endif()

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
