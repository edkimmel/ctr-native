# Integer render scale is a local presentation preference.  It must not enter
# any value transported between cabinets or persisted in a replay/canonical
# state record.  This guard intentionally examines the public transport
# boundary, rather than renderer implementation files: renderer code may use
# any of these concepts, while their presence in this boundary is a protocol
# regression.

set(transport_headers
    "include/platform/native_match_config.h"
    "include/platform/native_canonical_state.h"
    "include/platform/native_canonical_state_v3.h"
    "include/platform/native_canonical_state_v4.h"
    "include/platform/native_replay_v2.h"
    "include/platform/native_replay_v3.h"
    "include/platform/native_replay_v4.h")

# These are identifier fragments, not generic prose.  For example, the
# MatchConfig contract comment is allowed to say that it excludes a renderer;
# an actual `renderScale` / `render_scale` member is not.
set(forbidden_presentation_identifiers
    "render_scale"
    "renderscale"
    "render_resolution"
    "renderresolution"
    "display_scale"
    "displayscale"
    "internal_resolution"
    "internalresolution"
    "upscale_factor"
    "upscalefactor"
    "fullscreen_mode"
    "fullscreenmode"
    "native_display_config"
    "nativedisplayconfig"
    "displayconfig"
    "native_renderer.h")

foreach(relative_path IN LISTS transport_headers)
    set(path "${CMAKE_CURRENT_LIST_DIR}/../${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "render-scale determinism contract: missing transport header ${relative_path}")
    endif()

    file(READ "${path}" source)
    string(TOLOWER "${source}" lower_source)
    foreach(term IN LISTS forbidden_presentation_identifiers)
        string(FIND "${lower_source}" "${term}" term_offset)
        if(NOT term_offset EQUAL -1)
            message(FATAL_ERROR
                "render-scale determinism contract: presentation term '${term}' is forbidden in ${relative_path}")
        endif()
    endforeach()
endforeach()

# The fixed wire widths are the companion regression signal: extending any
# transport with presentation state requires an explicit schema/protocol change
# and must never happen accidentally while adding a local render option.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../include/platform/native_match_config.h" match_config)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../include/platform/native_replay_v4.h" replay_v4)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../include/platform/native_canonical_state_v4.h" canonical_v4)

string(FIND "${match_config}" "#define NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES 256u" match_config_width)
string(FIND "${replay_v4}" "#define NATIVE_REPLAY_V4_HEADER_BYTES 428u" replay_header_width)
string(FIND "${replay_v4}" "#define NATIVE_REPLAY_V4_FRAME_BYTES 1752u" replay_frame_width)
string(FIND "${canonical_v4}" "#define NATIVE_CANONICAL_STATE_V4_SCHEMA_VERSION UINT32_C(5)" canonical_schema)
if(match_config_width EQUAL -1 OR replay_header_width EQUAL -1 OR replay_frame_width EQUAL -1 OR canonical_schema EQUAL -1)
    message(FATAL_ERROR "render-scale determinism contract: sealed transport width or schema changed")
endif()
