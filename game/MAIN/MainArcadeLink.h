#ifndef GAME_MAIN_ARCADE_LINK_H
#define GAME_MAIN_ARCADE_LINK_H

/*
 * Arcade-link live hook (docs/GAME_LOOP_UI_MILESTONE.md section 2.5, Tasks
 * 6b-2 to 6b-4). Native only: game/MAIN/MainArcadeLink.c is compiled only
 * with CTR_NATIVE and is dormant unless an arcade-link host option was given.
 * Its decisions live in the pure MAIN/MainArcadeLinkPolicy.h.
 */

struct GameTracker;
struct GamepadSystem;
struct MainRenderLevelGeometryScratch;

/*
 * Called once per frame from MainFrame_RenderFrame at the retail menu seam,
 * before RECTMENU_CollectInput. Returns 1 when the arcade-link layer owns
 * this frame's menu layer (it has then already cleared every pad's taps and
 * hidden the retail main-menu box): the caller must then clear the
 * per-player menu input it collects, so the retail main-menu box receives
 * none this frame. Returns 0 otherwise. With the host mode OFF (no
 * arcade-link option) it returns 0 immediately and has no side effect of any
 * kind. A START_RACE of this frame's host tick is handed to the race caller
 * (MAIN/MainArcadeRaceLaunch.h), which MainFrame_RenderFrame steps right
 * after this call.
 */
int MainArcadeLink_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS);

/*
 * The arcade-link menu-ready condition for this frame, whatever the host
 * mode: 1 when the frame is in the title window the layer would own
 * (MainArcadeLinkPolicy_TitleMenuReady on the facts MainArcadeLink_Frame
 * gathers), else 0. Reads only; changes nothing. The internal roster proof
 * waits for it before it launches.
 */
int MainArcadeLink_TitleMenuReady(const struct GameTracker *gGT, const struct GamepadSystem *gGS);

/*
 * The arcade-link boot-intro skip (docs/SOLO_CAB_MILESTONE.md section 7):
 * MainArcadeLinkPolicy_SkipBootIntro on the host mode, which main.c fixes
 * before the first StateZero. 1 in LINK mode only. Reads only; changes
 * nothing. Called only from StateZero (the SCEA display and its XA), the
 * first-boot branch of LOAD_TenStages stage 0 (the copyright display and its
 * hold), and the cutscene camera thread (the Naughty Dog crate's retail
 * START skip).
 */
int MainArcadeLink_SkipBootIntro(void);

/*
 * The arcade-link top LOD tier (docs/SOLO_CAB_MILESTONE.md section 8):
 * MainArcadeLinkPolicy_ForceTopLod on the host mode, which main.c configures
 * before the first StateZero; it never goes OFF -> LINK after that
 * configure. It can go LINK -> OFF mid-session (the host's abort to title
 * shuts the link down when it cannot restart it); the retail LOD then
 * returns, which is harmless. 1 in LINK mode only. Reads only; changes
 * nothing. Called only from the render path: RenderBucket_QueueDraw (the
 * model header drawn) and DecalMP_01 (the multiplayer kart impostor).
 */
int MainArcadeLink_ForceTopLod(void);

/*
 * The arcade-link level geometry tier (docs/SOLO_CAB_MILESTONE.md section
 * 8.3): MainArcadeLinkPolicy_LevelLodThreshold on the host mode (LINK: a
 * threshold no distance or depth reaches; any other mode: retailThreshold).
 * Reads only. Called only from RenderLists_Walk1P2P (the BSP near/far slot
 * distance, 1P and 2P) and from MainArcadeLink_ForceNearLevelDepths.
 */
int MainArcadeLink_LevelLodThreshold(int retailThreshold);

/*
 * Passes three level depth thresholds of *scratch (the render scratch the
 * level draw reads: textureLodDepthThreshold0/1 and topLevelNear) through
 * MainArcadeLink_LevelLodThreshold, so in LINK mode every quad takes its
 * nearest texture tier and its top-level subdivision; any other mode leaves
 * them retail. recursiveNear (the deeper subdivision near the camera) stays
 * retail in every mode. Writes nothing else. Called right after each retail
 * seed of those thresholds: the 1P branch of the render frame, the 2P
 * overlay's shared-helper seed, and the 3P/4P split-ground seed. The 1P
 * depthScale, BSP distance, and fade start are left retail.
 */
void MainArcadeLink_ForceNearLevelDepths(struct MainRenderLevelGeometryScratch *scratch);

/*
 * The arcade-link primitive memory (docs/SOLO_CAB_MILESTONE.md section 8.5).
 * Called only from MainInit_PrimMem, right after its two retail
 * MainDB_PrimMem calls. In LINK mode it points each draw buffer's primMem
 * (start, cursor, end, guardEnd, capacityBytes) at a static host buffer of
 * MainArcadeLinkPolicy_PrimitiveBytes bytes, which is larger than any retail
 * per-level size. The retail MEMPACK block stays reserved and its start stays
 * in the struct, so the MEMPACK layout and every simulation object in it are
 * unchanged. Any other mode leaves both draw buffers retail. Re-run on every
 * level load. Presentation only: primMem holds GPU primitives, which nothing
 * digested reads. Arcade-link mode rejects replay record and playback and
 * disables the quick-state hotkeys, and the quick save and load refuse
 * whenever a draw buffer's start is not its retail MEMPACK block (so also
 * after a fallback to OFF, until the next level load rebinds the retail
 * buffers): no saved state holds the host pointers while they are bound.
 */
void MainArcadeLink_GrowPrimMem(struct GameTracker *gGT);

#endif
