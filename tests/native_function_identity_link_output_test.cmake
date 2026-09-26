# Build-output check for tests/native_function_identity_link_isolation_test.cmake
# (Visual Studio generators only): the generated ctr_native.vcxproj must link
# every configuration with identical COMDAT folding disabled.  Each
# configuration's ItemDefinitionGroup carries exactly one
# <EnableCOMDATFolding>false</EnableCOMDATFolding> in its <Link> settings, and
# no configuration carries true.
#
# Inputs: -DCTR_VCXPROJ=<path to ctr_native.vcxproj>
#         -DCTR_CONFIGS=<comma-separated CMAKE_CONFIGURATION_TYPES>
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CTR_VCXPROJ OR NOT EXISTS "${CTR_VCXPROJ}")
    message(FATAL_ERROR "function identity output: missing project file '${CTR_VCXPROJ}'")
endif()
if("${CTR_CONFIGS}" STREQUAL "")
    message(FATAL_ERROR "function identity output: no configurations given")
endif()

file(READ "${CTR_VCXPROJ}" project)
string(REPLACE "," ";" configs "${CTR_CONFIGS}")

set(folding_false "<EnableCOMDATFolding>false</EnableCOMDATFolding>")
set(folding_true "<EnableCOMDATFolding>true</EnableCOMDATFolding>")

string(FIND "${project}" "${folding_true}" true_offset)
if(NOT true_offset EQUAL -1)
    message(FATAL_ERROR "function identity output: ${CTR_VCXPROJ} enables COMDAT folding")
endif()
string(REGEX MATCHALL "<EnableCOMDATFolding>[^<]*</EnableCOMDATFolding>" all_folding "${project}")
list(LENGTH all_folding all_folding_count)
list(LENGTH configs config_count)
if(NOT all_folding_count EQUAL config_count)
    message(FATAL_ERROR "function identity output: ${all_folding_count} EnableCOMDATFolding settings for ${config_count} configurations")
endif()

foreach(config IN LISTS configs)
    set(group_open "<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='${config}|")
    string(FIND "${project}" "${group_open}" group_start)
    if(group_start EQUAL -1)
        message(FATAL_ERROR "function identity output: no ItemDefinitionGroup for ${config}")
    endif()
    string(SUBSTRING "${project}" ${group_start} -1 group)
    string(FIND "${group}" "</ItemDefinitionGroup>" group_end)
    if(group_end EQUAL -1)
        message(FATAL_ERROR "function identity output: unterminated ItemDefinitionGroup for ${config}")
    endif()
    string(SUBSTRING "${group}" 0 ${group_end} group)

    string(FIND "${group}" "<Link>" link_start)
    string(FIND "${group}" "</Link>" link_end)
    if(link_start EQUAL -1 OR link_end LESS link_start)
        message(FATAL_ERROR "function identity output: no <Link> settings for ${config}")
    endif()
    math(EXPR link_length "${link_end} - ${link_start}")
    string(SUBSTRING "${group}" ${link_start} ${link_length} link)

    string(REGEX MATCHALL "${folding_false}" link_false "${link}")
    list(LENGTH link_false link_false_count)
    if(NOT link_false_count EQUAL 1)
        message(FATAL_ERROR "function identity output: ${config} link must set EnableCOMDATFolding false exactly once (found ${link_false_count})")
    endif()
endforeach()
