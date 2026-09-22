#ifndef GAME_MAIN_ARCADE_LINK_H
#define GAME_MAIN_ARCADE_LINK_H

/*
 * Arcade-link live hook (docs/GAME_LOOP_UI_MILESTONE.md section 2.5, Tasks
 * 6b-2 and 6b-3). Native only: game/MAIN/MainArcadeLink.c is compiled only
 * with CTR_NATIVE and is dormant unless an arcade-link host option was given.
 * Its decisions live in the pure MAIN/MainArcadeLinkPolicy.h.
 */

struct GameTracker;
struct GamepadSystem;

/*
 * Called once per frame from MainFrame_RenderFrame at the retail menu seam,
 * before RECTMENU_CollectInput. Returns 1 when the arcade-link layer owns
 * this frame's menu layer (it has then already cleared every pad's taps and
 * normally hidden the retail main-menu box; on the frame a START_RACE abort
 * falls back to host mode OFF it has instead given the box back): the
 * caller must then clear the per-player menu input it collects, so the
 * retail main-menu box receives none this frame. Returns 0 otherwise. With
 * the host mode OFF (no arcade-link option) it returns 0 immediately and
 * has no side effect of any kind.
 */
int MainArcadeLink_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS);

#endif
