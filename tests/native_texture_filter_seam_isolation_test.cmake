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

function(ctr_forbid relative_path source term)
    string(FIND "${source}" "${term}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "texture filter seam: '${term}' is forbidden in ${relative_path}")
    endif()
endfunction()

# 1. The public seam exists and is expressed in plain ints, so the renderer
#    header does not pull the display-config header into its consumers.
ctr_read_source("include/platform/native_renderer.h" renderer_header)
ctr_require("include/platform/native_renderer.h" "${renderer_header}" "void NativeRenderer_SetTextureFilter(int filter);")
ctr_require("include/platform/native_renderer.h" "${renderer_header}" "int NativeRenderer_GetTextureFilter(void);")
ctr_forbid("include/platform/native_renderer.h" "${renderer_header}" "native_display_config.h")

# 2. The renderer owns the state privately, defaults to nearest, validates its
#    argument, and pushes it through the fragment-shader uniform.
ctr_read_source("platform/native_renderer.c" renderer_source)
ctr_require("platform/native_renderer.c" "${renderer_source}" "global_variable s32 s_textureFilter = 0;")
ctr_require("platform/native_renderer.c" "${renderer_source}" "NativeRenderer_IsSupportedTextureFilter(filter)")
ctr_require("platform/native_renderer.c" "${renderer_source}" "glUniform1i(u_textureFilterLoc, s_textureFilter);")
ctr_require("platform/native_renderer.c" "${renderer_source}" "uniform int textureFilter;")
ctr_forbid("platform/native_renderer.c" "${renderer_source}" "g_cfg_bilinearFiltering")

# 3. The persistent 1024x512 RG8 VRAM texture always samples GL_NEAREST; the
#    filter choice is a shader decision, never a sampler state change.
ctr_require("platform/native_renderer.c" "${renderer_source}" "#define VRAM_INTERNAL_FORMAT GL_RG8")
ctr_read_source("include/platform/native_renderer_types.h" renderer_types)
ctr_require("include/platform/native_renderer_types.h" "${renderer_types}" "#define VRAM_WIDTH             (1024)")
ctr_require("include/platform/native_renderer_types.h" "${renderer_types}" "#define VRAM_HEIGHT            (512)")

string(FIND "${renderer_source}" "// gen VRAM texture" vram_begin)
string(FIND "${renderer_source}" "glTexImage2D(GL_TEXTURE_2D, 0, VRAM_INTERNAL_FORMAT" vram_end)
if(vram_begin EQUAL -1 OR vram_end EQUAL -1 OR NOT vram_end GREATER vram_begin)
    message(FATAL_ERROR "texture filter seam: cannot locate the VRAM texture creation block")
endif()
math(EXPR vram_length "${vram_end} - ${vram_begin}")
string(SUBSTRING "${renderer_source}" "${vram_begin}" "${vram_length}" vram_block)
ctr_require("platform/native_renderer.c (VRAM texture)" "${vram_block}" "GL_TEXTURE_MAG_FILTER, GL_NEAREST")
ctr_require("platform/native_renderer.c (VRAM texture)" "${vram_block}" "GL_TEXTURE_MIN_FILTER, GL_NEAREST")
ctr_forbid("platform/native_renderer.c (VRAM texture)" "${vram_block}" "GL_LINEAR")

# 4. Vertex construction must not observe any presentation or filter flag.
ctr_read_source("platform/native_gpu.c" gpu_source)
string(TOLOWER "${gpu_source}" gpu_lower)
foreach(term IN ITEMS "bilinear" "texturefilter" "texture_filter" "tcx = -1" "tcy = -1")
    ctr_forbid("platform/native_gpu.c" "${gpu_lower}" "${term}")
endforeach()

# 5. The debug key and the launcher both go through the renderer seam.
ctr_read_source("platform/native_platform.c" platform_source)
ctr_forbid("platform/native_platform.c" "${platform_source}" "g_cfg_bilinearFiltering")
ctr_require("platform/native_platform.c" "${platform_source}" "NativeRenderer_SetTextureFilter(")

ctr_read_source("main.c" main_source)
ctr_require("main.c" "${main_source}" "NativeRenderer_SetTextureFilter(displayConfig.textureFilter);")

# 6. The preference stays cabinet-local: it may never appear in a transport,
#    replay, or canonical-state boundary.
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
