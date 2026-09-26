# The host-local texture filter is a presentation preference owned solely by
# the renderer.  It must reach the GPU through the PSX fragment shader uniform
# and nothing else: vertex construction, transport headers, and canonical state
# must stay completely independent of it.  There is no GL-free renderer unit
# harness in this repository (native_renderer.c cannot be linked without a GL
# context), so these structural facts are enforced at the source level.

set(repo "${CMAKE_CURRENT_LIST_DIR}/..")

function(ctr_read_source relative_path out_var)
    set(path "${repo}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "texture filter seam: missing source ${relative_path}")
    endif()
    file(READ "${path}" source)
    set(${out_var} "${source}" PARENT_SCOPE)
endfunction()

function(ctr_require relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "texture filter seam: '${term}' is required in ${relative_path}")
    endif()
endfunction()

function(ctr_require_regex relative_path source pattern)
    string(REGEX MATCH "${pattern}" matched "${source}")
    if("${matched}" STREQUAL "")
        message(FATAL_ERROR "texture filter seam: pattern '${pattern}' is required in ${relative_path}")
    endif()
endfunction()

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "texture filter seam: '${term}' is forbidden in ${relative_path}")
    endif()
endfunction()

# 1. The public seam exists and is expressed in plain ints, so the renderer
#    header does not pull the display-config header into its consumers, and
#    the private state variable never leaks through the header.
ctr_read_source("include/platform/native_renderer.h" renderer_header)
ctr_require("include/platform/native_renderer.h" "${renderer_header}" "void NativeRenderer_SetTextureFilter(int filter);")
ctr_require("include/platform/native_renderer.h" "${renderer_header}" "int NativeRenderer_GetTextureFilter(void);")
ctr_forbid("include/platform/native_renderer.h" "${renderer_header}" "native_display_config.h")
ctr_forbid("include/platform/native_renderer.h" "${renderer_header}" "s_textureFilter")

# 2. The renderer owns the state privately, defaults to nearest, validates its
#    argument, and pushes it through the fragment-shader uniform.
ctr_read_source("platform/native_renderer.c" renderer_source)
ctr_require("platform/native_renderer.c (default is nearest)" "${renderer_source}" "s_textureFilter = 0")
ctr_require("platform/native_renderer.c" "${renderer_source}" "NativeRenderer_IsSupportedTextureFilter(filter)")
ctr_require("platform/native_renderer.c" "${renderer_source}" "glUniform1i(u_textureFilterLoc, s_textureFilter);")
ctr_require("platform/native_renderer.c" "${renderer_source}" "uniform int textureFilter;")
ctr_forbid("platform/native_renderer.c" "${renderer_source}" "g_cfg_bilinearFiltering")

# 3. The display-config numeric contract is frozen: the renderer seam takes a
#    plain int, and NATIVE_TEXTURE_FILTER_NEAREST / _BILINEAR are 0 / 1 so
#    "off" is the zero value and the shader's `textureFilter > 0` test is the
#    same predicate.
ctr_read_source("include/platform/native_display_config.h" display_config_header)
ctr_require_regex("include/platform/native_display_config.h (NEAREST must be 0)" "${display_config_header}"
    "NATIVE_TEXTURE_FILTER_NEAREST[ \t]*=[ \t]*0[ \t]*,")
ctr_require_regex("include/platform/native_display_config.h (BILINEAR must be 1)" "${display_config_header}"
    "NATIVE_TEXTURE_FILTER_BILINEAR[ \t]*=[ \t]*1[ \t\r\n]")

# 4. The bilinear shader contract (docs/TEXTURE_FILTER_MILESTONE.md section 4):
#    visibility, STP and the mask bit come from the nearest texel so
#    fragColor.a stays binary; every tap is wrapped inside the 256-texel page
#    and fetched at its centre; and the colour falls back to the nearest texel
#    when no neighbour carries weight.
ctr_require("platform/native_renderer.c (bilinear: STP/mask from nearest texel)" "${renderer_source}"
    "sampledStp = visible * stpWeight(rgN);")
ctr_require("platform/native_renderer.c (bilinear: taps wrap within the page)" "${renderer_source}"
    "samplePSX(mod(tap, 256.0) + 0.5)")
ctr_require("platform/native_renderer.c (bilinear: nearest fallback when all weights are zero)" "${renderer_source}"
    "(wsum > 0.0) ? sum / wsum : lut(rgN)")

# 5. The persistent 1024x512 RG8 VRAM texture always samples GL_NEAREST; the
#    filter choice is a shader decision, never a sampler state change.
ctr_require("platform/native_renderer.c" "${renderer_source}" "#define VRAM_INTERNAL_FORMAT GL_RG8")
ctr_read_source("include/platform/native_renderer_types.h" renderer_types)
ctr_require_regex("include/platform/native_renderer_types.h (VRAM is 1024 wide)" "${renderer_types}"
    "#define[ \t]+VRAM_WIDTH[ \t]+\\(1024\\)")
ctr_require_regex("include/platform/native_renderer_types.h (VRAM is 512 high)" "${renderer_types}"
    "#define[ \t]+VRAM_HEIGHT[ \t]+\\(512\\)")

# The block runs from the VRAM texture's creation to its allocation; the
# sampler parameters are set in between.
string(FIND "${renderer_source}" "glGenTextures(1, &s_vram.texture);" vram_begin)
if(vram_begin EQUAL -1)
    message(FATAL_ERROR "texture filter seam: cannot locate the VRAM texture creation (glGenTextures(1, &s_vram.texture);)")
endif()
string(SUBSTRING "${renderer_source}" "${vram_begin}" -1 vram_tail)
string(FIND "${vram_tail}" "glTexImage2D(GL_TEXTURE_2D, 0, VRAM_INTERNAL_FORMAT" vram_length)
if(vram_length EQUAL -1)
    message(FATAL_ERROR "texture filter seam: cannot locate the VRAM texture allocation (glTexImage2D(.., VRAM_INTERNAL_FORMAT, ..)) after its creation")
endif()
string(SUBSTRING "${vram_tail}" 0 "${vram_length}" vram_block)
ctr_require("platform/native_renderer.c (VRAM texture sampler)" "${vram_block}" "GL_TEXTURE_MAG_FILTER, GL_NEAREST")
ctr_require("platform/native_renderer.c (VRAM texture sampler)" "${vram_block}" "GL_TEXTURE_MIN_FILTER, GL_NEAREST")
ctr_forbid("platform/native_renderer.c (VRAM texture sampler)" "${vram_block}" "GL_LINEAR")

# 6. Vertex construction must not observe any presentation or filter flag.
ctr_read_source("platform/native_gpu.c" gpu_source)
string(TOLOWER "${gpu_source}" gpu_lower)
foreach(term IN ITEMS "bilinear" "texturefilter" "texture_filter" "tcx = -1" "tcy = -1")
    ctr_forbid("platform/native_gpu.c" "${gpu_lower}" "${term}")
endforeach()

# 7. The debug key and the launcher both go through the renderer seam.
ctr_read_source("platform/native_platform.c" platform_source)
ctr_forbid("platform/native_platform.c" "${platform_source}" "g_cfg_bilinearFiltering")
ctr_require("platform/native_platform.c" "${platform_source}" "NativeRenderer_SetTextureFilter(")

ctr_read_source("main.c" main_source)
ctr_require("main.c" "${main_source}" "NativeRenderer_SetTextureFilter(displayConfig.textureFilter);")

# 8. The display-config header is consumed only at the launcher, by its own
#    implementation and unit test, and by the per-cabinet config header (whose
#    render_scale and texture_filter keys reach it only through
#    NativeDisplayConfig_ApplyArgs, pinned by
#    tests/native_arcade_config_isolation_test.cmake); no game, renderer, GPU,
#    replay or canonical code may include it.
set(display_config_include_sites
    "main.c"
    "platform/native_display_config.c"
    "tests/native_display_config_test.c"
    "include/platform/native_arcade_config.h")

file(GLOB_RECURSE first_party_sources RELATIVE "${repo}" "${repo}/*.c" "${repo}/*.h")
list(FILTER first_party_sources EXCLUDE REGEX "^(externals|build[^/]*|\\.git)/")
list(LENGTH first_party_sources first_party_count)
if(first_party_count LESS 10)
    message(FATAL_ERROR "texture filter seam: include-site scan found only ${first_party_count} sources; the glob is broken")
endif()
foreach(relative_path IN LISTS display_config_include_sites)
    list(FIND first_party_sources "${relative_path}" site_index)
    if(site_index EQUAL -1)
        message(FATAL_ERROR "texture filter seam: expected include site ${relative_path} is missing from the source glob")
    endif()
endforeach()
foreach(relative_path IN LISTS first_party_sources)
    if(relative_path STREQUAL "include/platform/native_display_config.h")
        continue()
    endif()
    file(READ "${repo}/${relative_path}" source)
    string(FIND "${source}" "native_display_config.h" offset)
    list(FIND display_config_include_sites "${relative_path}" site_index)
    if(NOT offset EQUAL -1 AND site_index EQUAL -1)
        message(FATAL_ERROR
            "texture filter seam: native_display_config.h may only be included by main.c, platform/native_display_config.c, tests/native_display_config_test.c and include/platform/native_arcade_config.h; found in ${relative_path}")
    endif()
endforeach()

# 9. No filter token in simulation, GPU command translation, savestate,
#    checkpoint, replay or canonical-state code (case-insensitive).
set(filter_free_sources
    "platform/native_gpu.c"
    "platform/native_savestate.c"
    "platform/native_checkpoint.c")
file(GLOB filter_free_globbed RELATIVE "${repo}"
    "${repo}/platform/native_replay_*.c"
    "${repo}/platform/native_canonical_*.c")
file(GLOB_RECURSE game_sources RELATIVE "${repo}" "${repo}/game/*.c" "${repo}/game/*.h")
list(LENGTH filter_free_globbed filter_free_globbed_count)
list(LENGTH game_sources game_source_count)
if(filter_free_globbed_count EQUAL 0 OR game_source_count EQUAL 0)
    message(FATAL_ERROR "texture filter seam: replay/canonical or game/ source glob is empty; the token scan cannot run")
endif()
list(APPEND filter_free_sources ${filter_free_globbed} ${game_sources})

foreach(relative_path IN LISTS filter_free_sources)
    if(NOT EXISTS "${repo}/${relative_path}")
        message(STATUS "texture filter seam: ${relative_path} does not exist; skipping token scan for it")
        continue()
    endif()
    file(READ "${repo}/${relative_path}" source)
    string(TOLOWER "${source}" lower_source)
    foreach(term IN ITEMS "texturefilter" "texture_filter" "bilinear" "g_cfg_bilinearfiltering")
        ctr_forbid("${relative_path} (filter-free source)" "${lower_source}" "${term}")
    endforeach()
endforeach()

# 10. The preference stays cabinet-local: it may never appear in a transport,
#     replay, or canonical-state boundary.
set(transport_headers
    "include/platform/native_match_config.h"
    "include/platform/native_canonical_state.h"
    "include/platform/native_canonical_state_v3.h"
    "include/platform/native_canonical_state_v4.h"
    "include/platform/native_replay_v2.h"
    "include/platform/native_replay_v3.h"
    "include/platform/native_replay_v4.h")

foreach(relative_path IN LISTS transport_headers)
    ctr_read_source("${relative_path}" header_source)
    string(TOLOWER "${header_source}" header_lower)
    foreach(term IN ITEMS "texture_filter" "texturefilter" "bilinear" "nearest_filter" "texture_sampling")
        ctr_forbid("${relative_path}" "${header_lower}" "${term}")
    endforeach()
endforeach()
