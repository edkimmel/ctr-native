# NativeCanonicalWorldCountersV1Facts intentionally contains only its two
# semantic uint32_t values.  It has no documented ignored representation
# storage, so a runtime "padding" test would not exercise a real API contract.
# Guard the regression directly instead: unavailable facts must be checked by
# their count field, and this implementation may not use a raw byte comparator.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../platform/native_canonical_world_counters.c" source)

string(FIND "${source}" "facts->activeBombMissileCount != 0" unavailable_count_check)
if(unavailable_count_check EQUAL -1)
    message(FATAL_ERROR
        "native_canonical_world_counters_v1_source_contract: unavailable facts must validate activeBombMissileCount fieldwise")
endif()

foreach(raw_comparator IN ITEMS "memcmp(" "bcmp(" "__builtin_memcmp(")
    string(FIND "${source}" "${raw_comparator}" raw_comparator_index)
    if(NOT raw_comparator_index EQUAL -1)
        message(FATAL_ERROR
            "native_canonical_world_counters_v1_source_contract: raw object comparator ${raw_comparator} is forbidden")
    endif()
endforeach()
