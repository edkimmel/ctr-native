# Texture filtering milestone

Self-contained brief for an orchestrator session delegating the "texture
filtering" milestone on branch `arcade`. Read `AGENTS.md` and `docs/HANDOFF.md`
first. Line numbers below were verified at commit `1fcb18c08`; re-check them
before quoting to a subagent.

## 1. Purpose and current state

Goal: an opt-in, host-local, presentation-only texture filtering mode,
`--texture-filter nearest|bilinear` (default `nearest`), so menu text,
portraits, HUD and world textures are smoothed when running at a high integer
render scale. It must never affect game, replay or canonical-state behaviour.

Verified current state:

- Integer render scale `--render-scale N` (1, 2, 3, 4, 6, 8) supersamples the
  main render target. Commit `1fcb18c08` ("feat: box-filter the supersampled
  present blit") resolves it to the window through a chain of half-size
  `GL_LINEAR` blits in `NativeRenderer_PresentMainRenderTarget`
  (`platform/native_renderer.c:2293-2358`). Polygon edges are therefore
  anti-aliased; textures are not, because PS1 textures are palettized data in
  the 1024x512 VRAM texture, sampled with `GL_NEAREST` and CLUT-decoded per
  texel in the fragment shader. At 8x each texel is an 8x8 block.
- Cabinet launch: Pegasus (`C:\arcade\launcher\pegasus\library\metadata.pegasus.txt`,
  collection `launch: cmd /c "{file.path}"`) runs
  `C:\Arcade\scripts\launch-ctr-native.bat`, which sets
  `CTR_NATIVE_RENDER_SCALE=8` and `CTR_NATIVE_FULLSCREEN=1`, prefers
  `C:\Arcade\games\ctr-native\ctr_native.exe` if present (currently absent;
  the directory holds only `SETUP.md`), otherwise
  `build-msvc-x86\Release\ctr_native.exe`. Cabinet panels are 1920x1080; the
  4:3 game lands in a 1440x1080 letterbox; at 8x the main target is about
  4096x1728.
- `CTR_INTERNAL` is defined unconditionally for the `ctr_native` target
  (`CMakeLists.txt:531-536`), so the debug keys below exist in Release too.

## 2. Headline finding: a bilinear path already exists

`bilinearTextureSample` lives in `platform/native_renderer.c:934-970`, inside
the `GPU_FRAGMENT_SAMPLE_SHADER(bit)` macro (`923-984`). `main()` (`980-984`)
selects it via `uniform int bilinearFilter` (`928`, `981`). Runtime global
`int g_cfg_bilinearFiltering = 0;` (`161`) is pushed in
`NativeRenderer_SetTexture` (`1558-1561`). F3 toggles it in
`platform/native_platform.c:236-239` (inside `#ifdef CTR_INTERNAL`, `198`).
CPU side, `platform/native_gpu.c:25` externs it and `MakeTexcoordRect`
(`467`, hack at `513-526`) sets `tcx/tcy = -1` (a half-texel offset) for the
sprite primitives only; the POLY quad/triangle variants are commented out
(`404-418`, `450-464`). A dead `#define BILINEAR_FILTER` is injected at
`native_renderer.c:1095-1098` and no shader string references it.

Defects, each verified in the code:

1. Dark fringes. `x1 = mix(lut(C11), lut(C21), ..)` (`965-967`) blends the
   colour of all four texels regardless of `texelVisible`, so black-key
   (`0x0000`) neighbours contribute black.
2. Holes and pass inconsistency. Visibility (`axm`), STP (`stp`) and non-STP
   (`nonStp`) are interpolated (`953-961`) and thresholded at 0.5 by
   `discardForSemiTransPass` (`964`). At an STP/non-STP boundary a fragment can
   fail both semi-trans passes. `sampledStp = stp` (`963`) is interpolated, so
   `fragColor.a` (`983`), which becomes VRAM bit 15 through `ctr_pack_shader`
   (`1237-1239`, `(c.a >> 7) << 15`), is not binary.
3. Page/CLUT bleed. `pixel + vec2(1.0, ..)` (`938-940`) at u = 255 reads
   texel 256, the next 256-texel page column, instead of wrapping.
4. Half-texel centring lives in vertex data (`tcx = -1`) only for
   SPRT/SPRT_8/SPRT_16 (`native_gpu.c:1561`, `1612`, `1646`), so filtered
   POLY_FT/GT primitives are shifted half a texel, and vertex construction
   depends on a presentation flag.
5. Dead code: `vec2 rg = mix(..)` (`962`) is unused.

## 3. Renderer facts the implementer needs

- VRAM: one persistent `GL_RG8` 1024x512 texture (R = low byte, G = high
  byte), `GL_NEAREST`, no wrap parameters (`native_renderer.c:29-35`,
  `1350-1364`). The same texture is the colour attachment of
  `s_glVramFramebuffer` (`1366-1376`), used by
  `NativeRenderer_GpuPackTextureToVRAM` (`2082-2134`),
  `NativeRenderer_StoreFrameBuffer` (`2140-2147`) and the CPU readback
  `NativeRenderer_SyncGpuVRAMToCPU` (`1918-1987`). Its sampler state must not
  change. All filtering is done with manual taps in the fragment shader.
- Fetch/decode chain: `VRAM(uv)` (`894-897`); `samplePSX(tc)` per format with
  `tc` in page texel units 0..255: 4-bit (`863-875`), 8-bit (`877-886`),
  16-bit direct (`888-892`); colour decode `lut(rg)` (`934`) through
  `s_rgLut`, a 256x256 RGBA table built by `NativeRenderer_InitRG8LUT`
  (`1294-1309`, alpha = bit 15).
- `main()` (`980-984`): `fragColor = dither(color * v_color)`; `fragColor.a`
  is overwritten with the PS1 mask bit
  `(psxDrawMaskSet != 0 || (psxTextureOutputStp != 0 && sampledStp >= 0.5))`.
- `gte_shader_32_rgba` (`995-1003`) is the native override-texture path: no
  CLUT, no `bilinearFilter` uniform. Out of scope.
- Uniforms: `GTEShader` struct (`823-836`), locations in
  `NativeRenderer_CompilePSXShader` (`1198-1210`), per-format selection in
  `NativeRenderer_SetTexture` (`1507-1547`). No texture-window uniform exists.
- Vertex layout `GrVertex` (`include/platform/native_renderer_types.h:25-35`):
  `s16 x,y,page,clut; u8 u,v,bright,dither; u8 r,g,b,a; s8 tcx,tcy,_p0,_p1;`.
  Attribute pointers `native_renderer.c:1396-1399`. Vertex shader
  `GTE_VERTEX_SHADER` (`1007-1026`): `v_texcoord.xy += a_extra.xy * 0.5`
  (`1017`); page origin and CLUT derivation (`1020-1023`); `c_UVFudge`
  (`1013`, applied `1024-1025`).
- Transparency: `GPU_STP_PASS_FUNC` (`899-907`):
  `texelVisible(rg) = rg.x + rg.y > 0` (word `0x0000` transparent, `0x8000`
  visible), `stpWeight(rg) = step(0.5, rg.y)`, `discardForSemiTransPass`.
  Nearest path `nearestTextureSample` (`971-979`).
- Two-pass textured semi-transparency on the CPU side: `AddSplit`
  (`native_gpu.c:799-867`; `psxTexturedSemiTrans` `817`,
  `psxTextureOutputSTP` `821`, such splits are never merged `832`);
  `DrawSplit` (`869-922`; `907-920`: pass 1 `BM_NONE` non-STP texels, pass 2
  blended STP texels, then pass 0). `NativeRenderer_SetBlendMode`
  (`native_renderer.c:2401-2450`): alpha factors are always
  `GL_ONE, GL_ZERO` so the mask bit survives blending. Shader alpha must stay
  binary.
- Texture windows: DR_TWIN is parsed (`native_gpu.c:1694-1702`, `1762-1767`)
  but `AddSplit` overwrites `tw.w/h` with the override texture size
  (`862-863`), consumed only by `NativeRenderer_SetOverrideTextureSize`
  (`897`) for `TF_32_BIT_RGBA`. Not implemented for PSX formats; bilinear must
  not try to honour them.
- Draw paths: every PSX textured primitive (POLY_FT3 `1392-1403`, POLY_FT4
  `1428-1436`, POLY_GT3 `1474-1481`, POLY_GT4 `1507-1515`, SPRT `1555-1562`,
  SPRT_8 `1606-1613`, SPRT_16 `1640-1647`) goes through
  `AddSplit(.., textured = true, ..)`, `DrawSplit`, `NativeRenderer_SetTexture`
  and the same `gte_shader_{4,8,16}` programs. UI and 3D share the shader; the
  flag is global. A future UI-only mode could carry a class byte in the unused
  `_p0/_p1` bytes (already delivered as `a_extra.zw`). Deferred.
- Framebuffer-feedback polygons (`native_gpu.c:754-797`) are filtered too.
  `AddSplit` has a `framebufferFeedback` parameter (`799`) if a force-nearest
  override is ever wanted.
- Performance at 8x: nearest costs 3 fetches (4/8-bit) or 2 (16-bit) per
  fragment; bilinear costs about 1 classify + 4 VRAM + 4 CLUT + 4 LUT, about
  13. All sources are cache-resident (VRAM 1 MiB, LUT 256 KiB); fill and
  blend at 4096x1728 dominate and are unchanged. Measure with `--perf`
  (`main.c:216-223`, `platform/native_perf.c:387-388`) and
  `tools/run-render-scale-replay-sweep.ps1`.

## 4. Required bilinear semantics (the shader contract)

- Footprint: `C = P - 0.5; base = floor(C); f = fract(C)`; taps at
  `base + {0,1} x {0,1}`. A fragment at a texel centre returns exactly that
  texel.
- Every tap coordinate is wrapped with `mod(coord, 256.0)` before `samplePSX`
  (PS1 8-bit UV wrap inside the page). The CLUT index is bounded per format
  and `v_page_clut.zw` is per primitive, so the CLUT never crosses.
- Classification comes from the nearest texel:
  `rgN = samplePSX(mod(floor(P), 256.0))`. `visible`, `sampledStp` and the
  `discardForSemiTransPass` decision derive from `rgN` exactly as
  `nearestTextureSample` does. This keeps `fragColor.a` binary, keeps pass 1
  and pass 2 coverage complementary, and keeps the discard edge identical to
  nearest.
- Colour is a weighted blend of post-CLUT neighbours:
  `w_i = bilinearWeight_i * texelVisible(rg_i) * passWeight(rg_i)` where
  `passWeight` is 1 (pass 0), `1 - stp_i` (pass 1), `stp_i` (pass 2);
  `colour = sum(w_i * lut(rg_i)) / sum(w_i)`; if `sum(w_i) == 0` fall back to
  `lut(rgN)`. Transparent texels contribute neither colour nor weight.
- 16-bit direct colour uses the same path. 32-bit override textures are
  untouched. Dithering stays after the blend. GLSL must remain `#version 140`
  compatible (`native_renderer.c:1081`).

## 5. Reference screenshot capture (verification harness)

The orchestrator must capture the loading splash and the main menu before and
after the change.

Existing hook: F12 calls `Platform_TakeScreenshot`
(`platform/native_platform.c:171-186`, `CTR_INTERNAL`): `glReadPixels` of
`g_windowWidth x g_windowHeight` from whatever framebuffer is bound at
key-event time, saved as `SCREENSHOT.BMP` in the working directory (the base
directory, see `chdir(NativeAssets_GetBaseDir())` at `main.c:187`). It is
key-driven with a fixed filename, so unsuitable for unattended capture, and
which FBO is bound at event time is ambiguous. The implementer must verify
that rather than assume.

Task 0 (do this first; it gates all later visual verification): add a
host-local capture option, for example `--capture-frame <N>=<path.bmp>`
(repeatable) and `--exit-after-frame <N>`, parsed in the same transactional
style as `NativeDisplayConfig_ApplyArgs` (`platform/native_display_config.c`)
but in its own small module (suggest `platform/native_frame_capture.{c,h}` and
`tests/native_frame_capture_test.c`) so it stays out of MatchConfig, replay
and canonical headers. Frame counter = calls to `Platform_EndScene`
(`native_platform.c:361-410`). Capture point: after the present call and
before `NativeRenderer_SwapWindow`, in both branches of `Platform_EndScene`:
the pinned-VRAM branch (`373-392`, used by the boot splash and loading screens
through `Platform_PresentVRAMDisplay`, `423-428`) and the normal branch
(`394-409`, `PresentMainRenderTarget` at scale > 1). Read the default
framebuffer (`glBindFramebuffer(GL_READ_FRAMEBUFFER, 0)`) with `glReadPixels`,
flip vertically, write the BMP with `SDL_SaveBMP`. Must not touch game, replay
or canonical code. Add the identifiers `capture_frame`, `framecapture` and
`native_frame_capture` to the forbidden list in
`tests/native_render_scale_determinism_contract_test.cmake`.

Capture procedure (a subagent runs it; it needs the operator-supplied,
gitignored `assets/ctr-u.bin`). Do not run it unattended without first
confirming the game reaches the main menu with no input. If a press-start gate
exists, the operator must capture manually with F12 or the milestone needs a
scripted input; record which it is in this section once known.

```
build-msvc-x86\Release\ctr_native.exe --render-scale 8 --windowed --capture-frame 30=debug\captures\splash-nearest.bmp --capture-frame 600=debug\captures\menu-nearest.bmp --exit-after-frame 700
```

First do a sweep (every 60 frames up to about 1800) to find the frame indices
where the splash and the main menu are stable, then fix those indices here.
Repeat with `--texture-filter bilinear` for the "after" set. Store captures
under `debug/captures/`; `.gitignore` already covers `/debug/`. Never commit
captures.

What to compare: main-menu text and character portraits (glyph edges soft but
no dark halos, no holes in semi-transparent overlays), HUD elements, no seam
lines at texture atlas edges. The loading splash is unchanged in both modes:
it goes through the VRAM presenter (`NativeRenderer_PresentVRAMRect`,
`native_renderer.c:2261`) and is not filtered by this feature. Do not treat
that as a bug.

## 6. Ordered task list

Each task is one implementer-sized unit. Run the full suite after each:

```
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --config Debug
ctest --test-dir build-msvc-x86 -C Debug --output-on-failure
cmake --build build-msvc-x86 --config Release --target ctr_native
```

- Task 0: frame capture option, unit test, contract-test identifiers, and the
  reference "before" captures (section 5). Reviewer: not required.
- Task 1: `--texture-filter` parsing in `NativeDisplayConfig`
  (`include/platform/native_display_config.h`,
  `platform/native_display_config.c`): `int textureFilter` member, enum
  `NATIVE_TEXTURE_FILTER_NEAREST = 0` / `NATIVE_TEXTURE_FILTER_BILINEAR = 1`,
  `NativeDisplayConfig_IsTextureFilterSupported`,
  `NativeDisplayConfig_ParseTextureFilter` (exact lowercase names),
  `NativeDisplayConfig_TextureFilterName`, `--texture-filter <mode>` and
  `--texture-filter=<mode>`, transactional, last wins. Extend
  `tests/native_display_config_test.c` with: default; both syntaxes; last
  wins; combined with `--render-scale 8 --fullscreen`; unrelated args
  untouched; failures leave config unchanged (missing value,
  `--texture-filter --perf`, unknown `xbr`, uppercase `Bilinear`, empty
  `--texture-filter=`); a pre-corrupted value makes `ApplyArgs` return 0.
  Reviewer: not required.
- Task 2: plumbing. `include/platform/native_renderer.h`:
  `NativeRenderer_SetTextureFilter(int)` and
  `NativeRenderer_GetTextureFilter(void)`. `native_renderer.c`: replace
  `g_cfg_bilinearFiltering` with `s_textureFilter` plus a validated setter of
  the same shape as `NativeRenderer_SetRenderScale` (`632-660`); rename the
  uniform `bilinearFilter` to `textureFilter` at every site (`829`, `857`,
  `928`, `981`, `1202`, `1513`, `1522`, `1531`, `1540`, `1558-1561`); delete
  the dead define (`1095-1098`). `native_gpu.c`: delete the extern (`25`), the
  `tcx/tcy` hack (`513-526`) and its commented copies (`404-418`,
  `450-464`). `native_platform.c`: F3 (`236-239`) toggles through the setter.
  `main.c`: call `NativeRenderer_SetTextureFilter(displayConfig.textureFilter)`
  after `SetRenderScale` (`214`), log the mode next to `167-168`, extend the
  fallback message (`162`). No shader semantic change in this task.
  Reviewer: required (touches `native_gpu.c` vertex construction; confirm no
  `NATIVE_GPU_STATE_*` or savestate field changes).
- Task 3: rewrite `bilinearTextureSample` per section 4. ctest cannot
  compile GLSL, so the implementer must run the Debug exe once with
  `--render-scale 8 --texture-filter bilinear` and confirm no shader compile
  errors in the log (compile asserts at `native_renderer.c:1044` and
  `1066`), then produce the "after" captures with Task 0. Reviewer: required
  (STP/mask correctness; `fragColor.a` feeds VRAM bit 15).
- Task 4: contract and isolation tests. Append `texture_filter`,
  `texturefilter`, `texture_filtering`, `texturefiltering`, `bilinear` and
  `nativetexturefilter` to the forbidden list in
  `tests/native_render_scale_determinism_contract_test.cmake` (`20-36`) and
  update its header comment. New
  `tests/native_texture_filter_isolation_test.cmake` (model:
  `tests/native_virtual_datagram_isolation_test.cmake`), registered in
  `CMakeLists.txt` next to the other `-P` tests (`683`): (i)
  `native_display_config.h` is included only by `main.c`,
  `platform/native_display_config.c` and
  `tests/native_display_config_test.c`; (ii) no `textureFilter`,
  `TextureFilter`, `bilinear` or `g_cfg_bilinearFiltering` token in
  `platform/native_gpu.c`, `platform/native_savestate.c`,
  `platform/native_checkpoint.c`, `platform/native_replay_*.c`,
  `platform/native_canonical_*.c` or `game/`; (iii) `GL_LINEAR` absent from
  the VRAM texture creation block in `native_renderer.c`. Reviewer: not
  required.
- Task 5: `docs/HANDOFF.md`: reword the "Render-scale independence" bullet
  (`59-61`) to "Presentation independence" covering render scale and texture
  filter; present-state wording only; optionally list
  `native_display_config.{h,c}` under Key files. Reviewer: not required.
- Task 6 (operator, outside the repo): `C:\Arcade\scripts\launch-ctr-native.bat`:
  add `set "CTR_NATIVE_TEXTURE_FILTER="` beside the existing overrides and
  `if not "%CTR_NATIVE_TEXTURE_FILTER%"=="" set "DISPLAY_ARGS=%DISPLAY_ARGS% --texture-filter %CTR_NATIVE_TEXTURE_FILTER%"`.
  Leave it empty until the hardware visual check passes, then set `bilinear`
  if approved.
- Task 7 (optional): `-TextureFilter` parameter for
  `tools/run-render-scale-replay-sweep.ps1` and a note in `docs/REPLAYS.md`
  (section "Render-scale determinism sweep", `61-76`) so the deterministic
  sweep proves the filter cannot alter replay digests.

## 7. Risks and open questions

1. Needs a hardware visual check on the 1080p cabinet: soft versus blurry
   text, glyph edges, no fringes. Use the captures plus F3 A/B.
2. Atlas bleed of up to half a texel at sub-texture edges is inherent to
   bilinear on a shared page. Mitigation later via per-primitive UV bounds in
   the `a_extra` bytes (would need an attribute type change at
   `native_renderer.c:1399`).
3. Framebuffer-feedback effects are filtered too; decide after the visual
   check.
4. Texture windows are unimplemented for PSX formats in both modes. Out of
   scope.
5. VRAM readback determinism: filtered pixels are packed to VRAM and are
   CPU-readable (`NativeRenderer_ReadVRAM`, `2176-2188`). The repo already
   asserts render scale cannot affect replay or canonical identity; Task 7
   turns that into evidence for the filter.
6. GLSL compiles only at runtime; Task 3 requires a launch with assets.
7. `fragColor.a` must stay exactly 0 or 1 (the pack shader thresholds with
   `>> 7`). Any future soft-alpha mode must preserve this.
8. Whether the game reaches the main menu without input (affects unattended
   capture) is unknown. Resolve in Task 0.

## 8. Orchestrator protocol

Use fresh-context subagents. Give each explicit files, constraints and the
exact verification commands. Run the full ctest suite after every task. Use a
reviewer for Tasks 2 and 3. Never claim a test passed unless a subagent
reported it. Commit per task on `arcade` with the trailer
`Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Push to `origin`
only, never `upstream`.
