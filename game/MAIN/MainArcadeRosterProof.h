#ifndef GAME_MAIN_ARCADE_ROSTER_PROOF_H
#define GAME_MAIN_ARCADE_ROSTER_PROOF_H

/*
 * Live roster proof hook (docs/ROSTER_MILESTONE.md section 3.4; R-5b's
 * minimal launcher). Internal native builds only: game/MAIN/MainArcadeRosterProof.c
 * is compiled only with CTR_NATIVE and CTR_INTERNAL, and is dormant unless
 * main.c configured the proof (--arcade-roster-proof).
 */

struct GameTracker;
struct GamepadSystem;

/*
 * Called once per frame from MainFrame_RenderFrame at the retail menu seam,
 * right after the arcade-link hook. With the proof inactive it returns
 * immediately and has no side effect. Otherwise it waits for the title's
 * menu-ready frame (the arcade-link menu-ready condition,
 * MainArcadeLink_TitleMenuReady), then the configured dwell, then arms and
 * launches the race setup with the proof config from the first launch window
 * it sees: the title menu-ready window (then it leaves the title the way the
 * retail demo route does) or the running attract demo race (then the demo
 * race runs on until the requested load starts). It then polls the setup,
 * writes the report 60 ticks after VALIDATED (or on a failure or watchdog),
 * records the proof result as the exit code, and requests the process exit.
 */
void MainArcadeRosterProof_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS);

#endif
