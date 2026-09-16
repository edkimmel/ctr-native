file(READ "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" cmake)

# Inspect this target's own direct link declaration.  Looking for references to
# this target elsewhere only proves that its consumers are clean; it does not
# prove that the dormant adapter itself remains isolated.
string(REGEX MATCH "target_link_libraries\\([ \t\r\n]*ctr_native_arcade_setup_v4([^)]+)\\)" setup_link_call "${cmake}")
if(NOT setup_link_call)
    message(FATAL_ERROR "main_arcade_setup_v4_isolation: missing direct link declaration")
endif()

set(setup_dependencies "${CMAKE_MATCH_1}")
string(REGEX REPLACE "[ \t\r\n]+" ";" setup_dependencies "${setup_dependencies}")
list(REMOVE_ITEM setup_dependencies "" PUBLIC PRIVATE INTERFACE)

set(allowed_dependencies ctr_native_arcade_bot_setup ctr_native_canonical_projector_v4)
foreach(required IN LISTS allowed_dependencies)
    list(FIND setup_dependencies "${required}" required_index)
    if(required_index EQUAL -1)
        message(FATAL_ERROR "main_arcade_setup_v4_isolation: missing required direct dependency ${required}")
    endif()
endforeach()

foreach(dependency IN LISTS setup_dependencies)
    list(FIND allowed_dependencies "${dependency}" allowed_index)
    if(allowed_index EQUAL -1)
        message(FATAL_ERROR "main_arcade_setup_v4_isolation: forbidden direct dependency ${dependency}")
    endif()
endforeach()
