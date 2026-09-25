#ifndef GAME_MAIN_ARCADE_LINK_AUTOPILOT_H
#define GAME_MAIN_ARCADE_LINK_AUTOPILOT_H

#include <stdint.h>

/*
 * Internal arcade-link autopilot glue (docs/RACE_LAUNCH_MILESTONE.md RL-15,
 * slice RL-S10). Native only: game/MAIN/MainArcadeLinkAutopilot.c is compiled
 * only with CTR_NATIVE, and does anything only in internal builds once main.c
 * configured it from --arcade-link-autopilot; otherwise both frame entries
 * return at once and touch nothing. The decisions are the pure
 * include/platform/native_arcade_link_autopilot.h's.
 *
 * The autopilot never touches a pad (the race caller owns the installed
 * pads, RL-10; while the autopilot is active the caller steers its own
 * sample with the pure steering, LR-16). It feeds the link host's own inputs through the
 * arcade-link hook (MAIN/MainArcadeLink.c): on the frames the hook owns in
 * LINK mode it replaces the hook's enter decision and held menu buttons, the
 * values the hook passes to NativeArcadeLinkHost_Enter and
 * NativeArcadeLinkHost_Tick; after every host tick of the hook it observes the
 * tick. When the run is done it writes the report and requests the process
 * exit with the run's result code.
 */

struct MainArcadeLinkPolicyInput;
struct MainArcadeLinkPolicyOutput;
struct NativeArcadeLinkAutopilotOptions;

/*
 * Internal builds only (defined only with CTR_INTERNAL): main.c calls it once,
 * after the arcade-link host was configured in LINK mode and before CTR_Main.
 * NULL or disabled options leave the autopilot inactive.
 */
void MainArcadeLinkAutopilot_Configure(const struct NativeArcadeLinkAutopilotOptions *options);

/*
 * Internal builds only (defined only with CTR_INTERNAL): 1 once Configure
 * activated the autopilot (it stays 1 after the report, until the exit), 0
 * otherwise. Read only: the race caller asks it whether to replace its local
 * pad sample with the steering pad (Task 8 race plan LR-16, LR-S10).
 */
uint8_t MainArcadeLinkAutopilot_Active(void);

/*
 * Internal builds only (defined only with CTR_INTERNAL): the fault injection
 * of the projected race tick raceTick (the Task 8 race plan LR-73), the pure
 * NativeArcadeLinkAutopilot_FaultAt over the run's state:
 * NATIVE_ARCADE_LINK_AUTOPILOT_FAULT_*
 * (include/platform/native_arcade_link_autopilot.h), NONE while inactive.
 * Read only: the race caller asks it once per drive tick and carries the
 * injection out itself.
 */
uint32_t MainArcadeLinkAutopilot_Fault(uint32_t raceTick);

/*
 * The arcade-link hook calls it on every frame it owns in LINK mode, right
 * before its link tick, with this frame's policy input and output. Inactive:
 * returns at once. Active: replaces output->heldButtons and
 * output->enterPressed with the autopilot's decision (no local pad input
 * reaches the link), entering only inside the hook's own enter window (the
 * policy's enterPressed for a fresh START on this frame's facts), and then
 * also takes that window's demo-countdown reset.
 */
void MainArcadeLinkAutopilot_Input(const struct MainArcadeLinkPolicyInput *input, struct MainArcadeLinkPolicyOutput *output);

/*
 * The arcade-link hook calls it right after every host tick (owned and
 * ticked frames alike) with the tick's enum NativeArcadeFlowAction. Inactive
 * or finished: returns at once.
 */
void MainArcadeLinkAutopilot_AfterTick(uint32_t action);

#endif
