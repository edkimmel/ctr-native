#ifndef GAME_MAIN_ARCADE_RACE_LAUNCH_H
#define GAME_MAIN_ARCADE_RACE_LAUNCH_H

#include <stdint.h>

/*
 * Live race caller (docs/RACE_LAUNCH_MILESTONE.md section 4 RL-8..RL-12,
 * slice RL-S8b). Native only: game/MAIN/MainArcadeRaceLaunch.c is compiled
 * only with CTR_NATIVE and is dormant unless the arcade-link host is in LINK
 * mode. It turns the host's START_RACE into a networked race: it arms and
 * launches MainArcadeRaceSetup with the agreed config from the title launch
 * window, leaves the title, runs the RL-10 launch rehearsal on neutral pads,
 * logs the RL-12 setup digests, reports the finish or an RL-11 local failure
 * to the host, returns to the main-menu level, clears the pads, and Disarms.
 * Every decision is the pure MAIN/MainArcadeRaceLaunchCore.h's; this module
 * samples the frame's facts, steps the core, and applies its decisions.
 */

struct GameTracker;
struct GamepadSystem;

/*
 * The arcade-link hook hands this frame's START_RACE over (MainArcadeLink.c,
 * the START_RACE branch of its host tick). Latched for this frame's
 * MainArcadeRaceLaunch_Frame, which consumes it.
 */
void MainArcadeRaceLaunch_StartRace(void);

/*
 * The host's raceFinished input for the arcade-link hook's host tick: the
 * launch core's finish latch (raceFinishedInput) as of its last accepted
 * step, 1 from the frame the rehearsal reported the race finished until the
 * core first sees the flow off RACING or a new START_RACE, else 0.
 */
uint8_t MainArcadeRaceLaunch_RaceFinished(void);

/*
 * Called once per frame from MainFrame_RenderFrame, right after
 * MainArcadeLink_Frame (so after this frame's host tick) and before the
 * retail menu-input collect. With the host not in LINK mode and no race in
 * progress it returns as its first statement and touches nothing, so the
 * default boot is unchanged.
 */
void MainArcadeRaceLaunch_Frame(struct GameTracker *gGT, struct GamepadSystem *gGS);

#endif
