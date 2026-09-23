#ifndef GAME_MAIN_ARCADE_ROSTER_PROOF_H
#define GAME_MAIN_ARCADE_ROSTER_PROOF_H

/*
 * Live roster proof hook (docs/ROSTER_MILESTONE.md section 3.4; R-5b's
 * launcher and R-6's scripted pads and per-tick digests). Internal native
 * builds only: game/MAIN/MainArcadeRosterProof.c is compiled only with
 * CTR_NATIVE and CTR_INTERNAL, and is dormant unless main.c configured the
 * proof (--arcade-roster-proof).
 */

struct GameTracker;
struct GamepadSystem;
struct NativeCanonicalStateV1;

/*
 * Called once per frame from MainFrame_RenderFrame at the retail menu seam,
 * right after the arcade-link hook. With the proof inactive it returns
 * immediately and has no side effect. Otherwise it waits for the title's
 * menu-ready frame (the arcade-link menu-ready condition,
 * MainArcadeLink_TitleMenuReady), then the configured dwell, then arms and
 * launches the race setup with the proof config from the first launch window
 * it sees: the title menu-ready window (then it leaves the title the way the
 * retail demo route does) or the running attract demo race (then the demo
 * race runs on until the requested load starts). It then polls the setup
 * until VALIDATED, checks the seed readback, and waits for race tick 0
 * (MainArcadeRosterProof_EndFrame logs the ticks and reports PASS). On a
 * failure or watchdog it writes the report, records the proof result as the
 * exit code, and requests the process exit.
 */
void MainArcadeRosterProof_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS);

/*
 * Called once by main.c right after the proof was configured, before
 * CTR_Main: installs the neutral scripted pads, so that from the first frame
 * no host input reaches the game. Returns 1 (also when the proof is inactive,
 * with no side effect); 0 when the pads could not be installed.
 */
int MainArcadeRosterProof_Start(void);

/*
 * Called once per frame from MainMain.c before GAMEPAD_ProcessAnyoneVars.
 * With the proof inactive it returns 0 and has no side effect. Otherwise it
 * installs this frame's scripted pads (platform/native_arcade_roster_proof.h)
 * and returns 1: MainMain.c then freezes the frame's canonical input and
 * projects the frame's V1 canonical state for MainArcadeRosterProof_EndFrame.
 */
int MainArcadeRosterProof_BeginFrame(void);

/*
 * Called once per frame from MainMain.c after the frame was simulated and
 * rendered, with the frame's V1 canonical state (NULL when it could not be
 * projected); inactive, it returns with no side effect. After VALIDATED it
 * finds race tick 0 (the first frame whose drivers extraction, the Meta
 * candidate without Physics, succeeds), then logs one tick line per frame
 * (the V1 control, RNG, and input domain digests and the drivers digest), and
 * reports PASS after the configured tick count; an extraction or projection
 * failure after race tick 0 fails the proof.
 */
void MainArcadeRosterProof_EndFrame(struct GameTracker *gGT, const struct NativeCanonicalStateV1 *frameState);

#endif
