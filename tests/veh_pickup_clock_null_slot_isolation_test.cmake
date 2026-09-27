# Structural guard for the clock-weapon empty-slot fix in
# game/Vehicle/VehPickupItem.c (VehPickupItem_ShootNow, WEAPON_ID_CLOCK).
#
# Retail writes GAME_TRACKER->drivers[i]->clockFlash before it checks
# drivers[i] for NULL. In a race with fewer than eight drivers (the two-cab
# 12BBBB-- roster, for example) that store goes through a NULL slot: the PS1
# absorbs it into low RAM, but the host faults (the v1 two-cabinet crash at
# ctr_native.exe+0xb8ede). The native build must check the slot first and
# store clockFlash only through the non-NULL victim; the non-native (PSX
# matching) build keeps the retail order in its #else branch.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")
set(relative_path "game/Vehicle/VehPickupItem.c")
set(path "${repo}/${relative_path}")
if(NOT EXISTS "${path}")
    message(FATAL_ERROR "clock null slot isolation: missing source ${relative_path}")
endif()
file(READ "${path}" source)

# The native branch loads the slot, skips an empty one, and only then stores
# clockFlash through the checked pointer; the retail order stays in #else.
set(pattern
    "#ifdef CTR_NATIVE[^#]*"
    "victim = GAME_TRACKER->drivers\\[i\\];[^#]*"
    "if \\(victim == 0\\)[^#]*continue;[^#]*"
    "victim->clockFlash = CLOCK_FLASH_FRAMES;[^#]*"
    "#else[^#]*"
    "GAME_TRACKER->drivers\\[i\\]->clockFlash = CLOCK_FLASH_FRAMES;[^#]*"
    "#endif")
string(CONCAT pattern ${pattern})
string(REGEX MATCH "${pattern}" matched "${source}")
if("${matched}" STREQUAL "")
    message(FATAL_ERROR
        "clock null slot isolation: ${relative_path} must NULL-check drivers[i] before storing clockFlash under CTR_NATIVE")
endif()

# No other clockFlash store may appear (a second, unchecked native store would
# reintroduce the fault).
string(REGEX MATCHALL "->clockFlash = " stores "${source}")
list(LENGTH stores store_count)
if(NOT store_count EQUAL 2)
    message(FATAL_ERROR
        "clock null slot isolation: expected 2 clockFlash stores in ${relative_path} (native and retail branches), found ${store_count}")
endif()
