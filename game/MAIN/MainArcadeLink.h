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

#endif
