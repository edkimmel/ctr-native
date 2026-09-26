# ctr_native identifies driver behaviour by function-pointer identity
# (MainCanonicalDrivers.c TokenFor and its RegistrySelfTest, plus retail
# function-pointer comparisons).  Identical COMDAT folding gives distinct
# functions with identical bodies one address, so the MSVC link must disable
# it explicitly (the Release default enables it) and nothing may re-enable it.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" cmake)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../CMakePresets.json" presets)

string(REGEX MATCH "target_link_options\\([ \t\r\n]*ctr_native[ \t\r\n]+PRIVATE[^)]*/OPT:NOICF[^)]*\\)" noicf "${cmake}")
if(NOT noicf)
    message(FATAL_ERROR "function identity: ctr_native MSVC link must pass /OPT:NOICF")
endif()

foreach(text_name cmake presets)
    string(TOUPPER "${${text_name}}" upper_text)
    foreach(forbidden "/OPT:ICF" "-OPT:ICF" "--ICF")
        string(FIND "${upper_text}" "${forbidden}" forbidden_offset)
        if(NOT forbidden_offset EQUAL -1)
            message(FATAL_ERROR "function identity: forbidden identical-code-folding flag ${forbidden} in ${text_name}")
        endif()
    endforeach()
endforeach()
