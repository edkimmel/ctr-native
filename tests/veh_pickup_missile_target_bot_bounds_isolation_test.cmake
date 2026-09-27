# Structural guard for the bot camera-bounds fixes in
# game/Vehicle/VehPickupItem.c (VehPickupItem_MissileGetTargetDriver) and
# game/231/RB_Crate.c (RB_CrateFruit_ThCollide, weapon branch).
#
# GameTracker has four camera pushBuffers, followed by DecalMP. Retail bounds a
# missile candidate by GAME_TRACKER->pushBuffer[driver->driverID].rect.w/.h for
# any driverID, so bots 4-7 read DecalMP entry bytes. For bot 5 in 2P those are
# the halves of a host Instance pointer: on the PS1 (KSEG0) every candidate is
# rejected, on the host the result follows the per-boot image base and can
# desync two cabinets. The native build must reject every candidate for
# driverID >= len(pushBuffer) after the per-candidate GTE work and before the
# rect reads (a continue, not an early return).
#
# The fruit crate weapon branch loads pushBuffer[driverID].matrix_ViewProj
# (RB_Pickup_SetCamera) for the weapon owner, which can be a bot. The native
# build must return for an ACTION_BOT owner after the cooldown/count writes and
# before RB_Pickup_SetCamera.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missile target bot bounds isolation: missing source ${relative_path}")
    endif()
    file(READ "${path}" contents)
    set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

# --- VehPickupItem_MissileGetTargetDriver ---------------------------------
set(item_path "game/Vehicle/VehPickupItem.c")
read_source("${item_path}" item_source)

string(FIND "${item_source}" "struct Driver *VehPickupItem_MissileGetTargetDriver(struct Driver *driver)" fn_at)
if(fn_at EQUAL -1)
    message(FATAL_ERROR "missile target bot bounds isolation: VehPickupItem_MissileGetTargetDriver not found in ${item_path}")
endif()
string(SUBSTRING "${item_source}" ${fn_at} -1 fn_tail)
string(FIND "${fn_tail}" "\nb32 VehPickupItem_PotionThrow(" fn_end)
if(fn_end EQUAL -1)
    message(FATAL_ERROR "missile target bot bounds isolation: end of VehPickupItem_MissileGetTargetDriver not found")
endif()
string(SUBSTRING "${fn_tail}" 0 ${fn_end} fn_body)

# The camera count constant is tied to the pushBuffer array length.
string(FIND "${item_source}" "CTR_STATIC_ASSERT(MISSILE_TARGET_CAMERA_COUNT == len(((struct GameTracker *)0)->pushBuffer));" assert_at)
if(assert_at EQUAL -1)
    message(FATAL_ERROR
        "missile target bot bounds isolation: ${item_path} must static-assert MISSILE_TARGET_CAMERA_COUNT == len(GameTracker.pushBuffer)")
endif()

# Order inside the candidate loop: gte_rtps, then the native guard
# (continue), then the rect.w read, then the rect.h read.
string(CONCAT pattern
    "gte_rtps\\(\\);.*"
    "#ifdef CTR_NATIVE[^#]*"
    "if \\(driver->driverID >= MISSILE_TARGET_CAMERA_COUNT\\)[ \t\r\n]*\\{[ \t\r\n]*continue;[ \t\r\n]*\\}[ \t\r\n]*"
    "#endif[^#]*"
    "GAME_TRACKER->pushBuffer\\[driver->driverID\\]\\.rect\\.w[^#]*"
    "GAME_TRACKER->pushBuffer\\[driver->driverID\\]\\.rect\\.h")
string(REGEX MATCH "${pattern}" matched "${fn_body}")
if("${matched}" STREQUAL "")
    message(FATAL_ERROR
        "missile target bot bounds isolation: ${item_path} must skip (continue) every candidate for driverID >= MISSILE_TARGET_CAMERA_COUNT under CTR_NATIVE after gte_rtps and before the rect.w/rect.h reads")
endif()

# Exactly one rect.w and one rect.h read, both after the guard.
string(REGEX MATCHALL "GAME_TRACKER->pushBuffer\\[driver->driverID\\]\\.rect\\.[wh]" rect_reads "${fn_body}")
list(LENGTH rect_reads rect_read_count)
if(NOT rect_read_count EQUAL 2)
    message(FATAL_ERROR
        "missile target bot bounds isolation: expected 2 rect reads in VehPickupItem_MissileGetTargetDriver, found ${rect_read_count}")
endif()

# The guard is a per-candidate continue, not an early return before the loop.
string(FIND "${fn_body}" "driver->driverID >= MISSILE_TARGET_CAMERA_COUNT" guard_at)
string(FIND "${fn_body}" "screenPositionPtr = &projection.screenPosition;" loop_at)
if(loop_at EQUAL -1 OR guard_at LESS loop_at)
    message(FATAL_ERROR
        "missile target bot bounds isolation: the driverID guard must sit inside the candidate loop")
endif()

# --- RB_CrateFruit_ThCollide weapon branch ---------------------------------
set(crate_path "game/231/RB_Crate.c")
read_source("${crate_path}" crate_source)

string(CONCAT crate_pattern
    "hitModelID == DYNAMIC_SHIELD_GREEN\\)\\)[ \t\r\n]*\\{[^#]*"
    "driver = \\(\\(struct TrackerWeapon \\*\\)collidingTh->object\\)->driverParent;[^#]*"
    "driver->PickupWumpaHUD\\.cooldown = 5;[^#]*"
    "driver->PickupWumpaHUD\\.numCollected = newWumpa;[^#]*"
    "#ifdef CTR_NATIVE[^#]*"
    "if \\(\\(driver->actionsFlagSet & ACTION_BOT\\) != 0\\)[ \t\r\n]*\\{[ \t\r\n]*return 1;[ \t\r\n]*\\}[ \t\r\n]*"
    "#endif[^#]*"
    "RB_Pickup_SetCamera\\(driver\\);")
string(REGEX MATCH "${crate_pattern}" crate_matched "${crate_source}")
if("${crate_matched}" STREQUAL "")
    message(FATAL_ERROR
        "missile target bot bounds isolation: ${crate_path} fruit crate weapon branch must return for an ACTION_BOT owner under CTR_NATIVE after the cooldown/numCollected writes and before RB_Pickup_SetCamera")
endif()
