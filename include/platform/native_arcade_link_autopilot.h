#ifndef PLATFORM_NATIVE_ARCADE_LINK_AUTOPILOT_H
#define PLATFORM_NATIVE_ARCADE_LINK_AUTOPILOT_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_arcade_link_host.h"

/*
 * Internal arcade-link autopilot for the two-process live gate
 * (docs/RACE_LAUNCH_MILESTONE.md section 4 RL-15, slice RL-S10): the option
 * parser, the pure menu decision, the progress bookkeeping, and the report;
 * and the race autopilot's pure steering decision (Steering, below).
 * Internal builds only: main.c rejects the option in any other build.
 *
 *   --arcade-link-autopilot <report path>   drive this link cabinet through
 *                                           two races and write the report
 *                                           to this path
 *   --arcade-link-autopilot-race-ticks <n>  lower the linked race's length
 *                                           bound to n race ticks (decimal
 *                                           1..18000; docs/LOCKSTEP_RACE_MILESTONE.md
 *                                           LR-42, LR-60); needs
 *                                           --arcade-link-autopilot
 *
 * The report path is opened as given when the report is written: a relative
 * path resolves against the base directory (main.c changes into it before
 * the game starts). Parsing is transactional: on any error the caller's
 * options are left untouched. Arguments other than this option are ignored,
 * because other host parsers own them. A missing value (end of argv, a NULL
 * entry, or a next argument starting with '-'), an empty or over-long path,
 * or a repeated option is an error; so is a race tick count that is not 1 to
 * 5 decimal digits with a value of 1..RACE_TICKS_MAX, and a race tick count
 * without --arcade-link-autopilot. main.c hands the count to the link host's
 * race tick limit setter after its Configure; 0 (absent) keeps the default
 * bound. main.c requires --arcade-link with it and
 * rejects it together with --arcade-roster-proof, --exit-after-frame, and
 * every replay record or playback option.
 *
 * The run (RL-15): START on the attract screen, CROSS to confirm each select
 * item, REMATCH after race 1, EXIT after race 2, then exit with the result
 * code. The autopilot never touches a pad: installed pads belong to the race
 * caller's rehearsal (RL-10). It feeds the link host's own inputs instead:
 * the game glue (game/MAIN/MainArcadeLinkAutopilot.c) replaces the arcade-link
 * hook's enter decision (NativeArcadeLinkHost_Enter) and the held menu
 * buttons the hook passes to NativeArcadeLinkHost_Tick with this module's
 * decision, on the frames the hook owns.
 *
 * Decide (before the host tick, owned link frames only). Menu input is
 * rising-edge and release-to-arm (include/platform/native_arcade_menu_input.h),
 * so a button is held on one decision in NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD
 * and released on all others. From the host view as the frame starts:
 * - screen OFF (attract): before the session started, enter as soon as the
 *   caller reports the hook's enter window open (enterReady);
 * - SELECT, picking, current item not DONE: CROSS, or DOWN (next) on the
 *   character item while a peer holds the local cursor's character (it
 *   cannot be confirmed);
 * - RESULTS with the rows enabled after a FINISHED race: the wanted row is
 *   REMATCH until NATIVE_ARCADE_LINK_AUTOPILOT_RACES races finished, then
 *   EXIT; DOWN (next) moves to it, CROSS confirms it;
 * - every other screen: nothing.
 * The decision is state-driven: a press that changed nothing is simply made
 * again on a later press decision.
 *
 * Observe (after every host tick of a link frame, owned or ticked). The
 * caller records each START_RACE's agreed match (RecordMatch) and each race
 * the race caller validated (RecordValidated) first. Then, in order:
 * - a RESULTS entry with an end reason other than FINISHED fails RACE_FAILED;
 *   a FINISHED entry must come with every started race validated, else
 *   EVIDENCE_MISSING;
 * - a RESULTS -> REMATCH_WAIT or RESULTS -> EXIT change on a tick the
 *   decision confirmed that row counts as this autopilot's REMATCH or EXIT;
 * - an EXIT screen with end reason OPPONENT_LEFT fails SESSION_LOST;
 * - RETURN_TO_TITLE passes only after exactly NATIVE_ARCADE_LINK_AUTOPILOT_RACES
 *   started, validated, and finished races, RACES - 1 rematches, and this
 *   autopilot's own EXIT; otherwise it fails SESSION_LOST;
 * - NATIVE_ARCADE_LINK_AUTOPILOT_DEADLINE_TICKS observed ticks fail TIMEOUT.
 * Once done (pass or fail) every call is inert.
 *
 * Report (FormatReport; WriteReport is this module's only I/O):
 *
 *   arcade link autopilot v1
 *   cab <1|2>
 *   result <NAME> (<code>)
 *   last screen <NAME> end reason <NAME>
 *   ticks <observed ticks>
 *   race <k> agreed match track ... (the hook's agreed-match log text)
 *   race <k> validated launch <n> config <64 hex> plan <64 hex> bots <64 hex> bank <64 hex>
 *   ...
 *   end races <validated races>
 *
 * k counts this cabinet's races in order (1-based); n is the race caller's
 * launch number, which may differ between cabinets (RL-S7 interpretation
 * (d)): the checker pairs the k-th lines, never equal n. A race line appears
 * only for a recorded match or validation.
 *
 * Process exit codes while the autopilot runs (enum
 * NativeArcadeLinkAutopilotResult, also the report's result). Exit code 0
 * alone does not prove a pass: a window close or SDL quit while the
 * autopilot runs can also exit 0, without a report
 * (platform/native_platform.c; only the roster proof guards those paths). The proof is the report's
 * "result PASS (0)" line with both races (the checker requires both).
 *    0  PASS                 the run above, completed (report written)
 *    1  (startup failure)    main.c's generic failure; never a result
 *   40  TIMEOUT              not done within DEADLINE_TICKS observed ticks
 *   41  RACE_FAILED          a RESULTS screen with an end reason other than
 *                            FINISHED (link error, peer timeout, desync)
 *   42  SESSION_LOST         the opponent left, or the session returned to
 *                            the title before the run completed
 *   43  UNEXPECTED_RACE      more START_RACEs or validations than races
 *   44  EVIDENCE_MISSING     a START_RACE without an agreed match, or a
 *                            finished race that was not validated
 *   45  REPORT_WRITE_FAILED  done, but the report could not be written
 *                            (exit code only)
 *
 * Pure except WriteReport: caller-owned state, no heap use, no hidden state,
 * no pad access, and fully deterministic.
 *
 * Steering (docs/LOCKSTEP_RACE_MILESTONE.md LR-16, spiked in LR-S2 (b)): the
 * race autopilot's closed-loop pad profile. It holds CROSS (accelerate) and
 * steers LEFT or RIGHT toward an aim point, the restart point ahead of the
 * kart. It is pure over pointer-free facts the game side reads (the kart's
 * position and heading, and restart point positions); it never sees game
 * state, and it returns a button word the caller installs itself.
 * - Positions are level world units on the ground plane (x, z); only
 *   differences are used.
 * - Angles are 12-bit: a full turn is ANGLE_UNITS, 0 faces +z, and a quarter
 *   turn (1024) faces +x. NativeArcadeLinkAutopilot_Angle(dx, dz) is the
 *   direction of (dx, dz) in those units (an integer atan2, within 3 units).
 * - Steer: the heading error is the aim direction minus the heading, wrapped
 *   to [-ANGLE_UNITS / 2, ANGLE_UNITS / 2). Within STEER_DEADBAND of 0 (or with
 *   the aim point on the kart) the kart goes straight: CROSS alone. A
 *   positive error adds STEER_POSITIVE, the button that raises the heading,
 *   and a negative error adds STEER_NEGATIVE.
 * - Passed: the kart is done with a restart point (the caller moves its
 *   target to the next one) once it is within PASS_RADIUS of the point, or
 *   past the line through the point square to the approach (the segment from
 *   the previous restart point): a positive dot product of (kart - point)
 *   and (point - previous).
 * The buttons are the PSX pad's active-high bits (a set bit is held); a pad
 * word is active low, so the caller clears them from an all-released word.
 */

#define NATIVE_ARCADE_LINK_AUTOPILOT_PATH_BYTES 512u
/* The largest --arcade-link-autopilot-race-ticks value: the race drive's
 * default bound, which the override may only lower (LR-42). The link host
 * refuses anything above it too. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_RACE_TICKS_MAX 18000u
/* Races in one run: race 1, REMATCH, race 2, EXIT. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_RACES 2u
/* A button is held on one decision in this many. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD 8u
/* Observed host ticks before TIMEOUT: 450 s at 30 Hz. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_DEADLINE_TICKS 13500u
#define NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES 32u
/* Setup digests per race: config, race plan, bot setup plan, bank (RL-12). */
#define NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT 4u
/* The report buffer FormatReport needs at most. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_BYTES 2048u

/* Steering (LR-16): the PSX pad bits of the steering pad profile. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_RIGHT 0x0020u
#define NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_LEFT 0x0080u
#define NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS 0x4000u
/* The button that raises the kart's heading, and the one that lowers it. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_STEER_POSITIVE NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_LEFT
#define NATIVE_ARCADE_LINK_AUTOPILOT_STEER_NEGATIVE NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_RIGHT
/* A full turn in the 12-bit angle units. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS 4096
/* Heading errors within this many angle units steer straight. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_STEER_DEADBAND 48
/* A restart point this close (world units) counts as passed. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_PASS_RADIUS 256

enum NativeArcadeLinkAutopilotResult
{
	NATIVE_ARCADE_LINK_AUTOPILOT_PASS = 0,
	NATIVE_ARCADE_LINK_AUTOPILOT_TIMEOUT = 40,
	NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED = 41,
	NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST = 42,
	NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE = 43,
	NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING = 44,
	NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_WRITE_FAILED = 45
};

struct NativeArcadeLinkAutopilotOptions
{
	uint8_t enabled; /* --arcade-link-autopilot given */
	uint8_t reserved[3];
	char reportPath[NATIVE_ARCADE_LINK_AUTOPILOT_PATH_BYTES];
	/* --arcade-link-autopilot-race-ticks, 1..RACE_TICKS_MAX; 0 when absent */
	uint32_t raceTickLimit;
};

/* One race of the run, in this cabinet's order. */
struct NativeArcadeLinkAutopilotRace
{
	/* 1 once RecordMatch stored the START_RACE's agreed match */
	uint8_t matchRecorded;
	/* 1 once RecordValidated stored the race's setup digests */
	uint8_t validated;
	uint8_t reserved[2];
	/* the race caller's launch number of the validated race */
	uint32_t launchNumber;
	struct NativeArcadeLinkHostMatch match;
	uint8_t digests[NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT][NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES];
};

/* Zero-initialized (or Init) is a fresh run. */
struct NativeArcadeLinkAutopilot
{
	/* 1 once the run passed or failed; every call is then inert */
	uint8_t done;
	/* 1 once the view left screen OFF: the session started, no more enter */
	uint8_t sessionStarted;
	/* 1 once this autopilot's EXIT was confirmed after the last race */
	uint8_t exitConfirmed;
	/* 1 or 2 from the view, 0 before the first observation */
	uint8_t localCab;
	/* the RESULTS row the last decision confirmed + 1, or 0 */
	uint8_t confirmedRow;
	uint8_t reserved[3];
	/* enum NativeArcadeLinkAutopilotResult once done */
	uint32_t result;
	uint32_t ticks;
	uint32_t decisions;
	/* the view's screen and end reason as of the last observation */
	uint32_t lastScreen;
	uint32_t lastEndReason;
	uint32_t racesStarted;
	uint32_t racesValidated;
	uint32_t racesFinished;
	uint32_t rematches;
	struct NativeArcadeLinkAutopilotRace races[NATIVE_ARCADE_LINK_AUTOPILOT_RACES];
};

/* The decision for one owned link frame. */
struct NativeArcadeLinkAutopilotOutput
{
	/* NATIVE_ARCADE_MENU_BUTTON_* bits to hold on this frame's host tick */
	uint32_t heldButtons;
	/* 1: enter the link (NativeArcadeLinkHost_Enter) this frame */
	uint8_t enter;
	uint8_t reserved[3];
};

/* The steering facts of one kart for one tick (see Steering above). */
struct NativeArcadeLinkAutopilotSteerFacts
{
	int32_t kartX;
	int32_t kartZ;
	int32_t heading; /* 12-bit angle; any value, taken modulo ANGLE_UNITS */
	int32_t aimX;
	int32_t aimZ;
};

/* The restart point facts of the passed rule (see Steering above). */
struct NativeArcadeLinkAutopilotPassFacts
{
	int32_t kartX;
	int32_t kartZ;
	int32_t pointX;
	int32_t pointZ;
	int32_t previousX;
	int32_t previousZ;
};

/* NULL is a no-op. Otherwise zeroes the options: disabled, empty path. */
void NativeArcadeLinkAutopilotOptions_SetDefaults(struct NativeArcadeLinkAutopilotOptions *options);

/* Returns 1 and updates *options on success; 0 with *options untouched otherwise. */
int NativeArcadeLinkAutopilotOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeLinkAutopilotOptions *options);

/* NULL is a no-op. Otherwise zeroes the state: a fresh run. */
void NativeArcadeLinkAutopilot_Init(struct NativeArcadeLinkAutopilot *autopilot);

/*
 * The decision for one owned link frame, from the host view as the frame
 * starts (before the host tick) and the hook's enter window (enterReady:
 * nonzero when a rising START would enter the link this frame). Fills
 * *output (always zeroed first) and returns 1; returns 0 touching nothing on
 * NULL. Once done the output stays zero.
 */
int NativeArcadeLinkAutopilot_Decide(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view,
	uint8_t enterReady, struct NativeArcadeLinkAutopilotOutput *output);

/*
 * Records the agreed match of a START_RACE (match NULL: the host had none,
 * EVIDENCE_MISSING). A START_RACE beyond RACES fails UNEXPECTED_RACE.
 * Returns 1 when recorded; 0 otherwise (NULL autopilot, done, or failed).
 */
int NativeArcadeLinkAutopilot_RecordMatch(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostMatch *match);

/*
 * The race caller's validation evidence: validatedTotal races validated so
 * far, the last one with launchNumber and its config, plan, bots, and bank
 * digests (digests NULL: none). A total equal to the races already recorded
 * is no change (returns 0). One more is recorded for the next race, which
 * must have started (else UNEXPECTED_RACE) and must carry digests (else
 * EVIDENCE_MISSING). Any other total fails UNEXPECTED_RACE. Returns 1 when
 * recorded.
 */
int NativeArcadeLinkAutopilot_RecordValidated(struct NativeArcadeLinkAutopilot *autopilot, uint32_t validatedTotal, uint32_t launchNumber,
	const uint8_t digests[NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT * NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES]);

/*
 * One observed host tick: the view after the tick (NULL: no view, counted
 * only) and the tick's enum NativeArcadeFlowAction. Applies the rules above
 * and returns 1 on the observation that made the run done, else 0.
 */
int NativeArcadeLinkAutopilot_Observe(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view,
	uint32_t action);

/* "PASS", "TIMEOUT", ... for a result; "unknown" otherwise. */
const char *NativeArcadeLinkAutopilot_ResultName(uint32_t result);

/* The flow screen and end reason names the report uses ("OFF", "LOBBY", ...; "NONE", "FINISHED", ...); "unknown" otherwise. */
const char *NativeArcadeLinkAutopilot_ScreenName(uint32_t screen);
const char *NativeArcadeLinkAutopilot_EndReasonName(uint32_t endReason);

/*
 * The agreed-match text of the report, the same text as the arcade-link
 * hook's agreed-match log line (game/MAIN/MainArcadeLink.c):
 * "agreed match track %u laps %u seed 0x%08X%08X slots %u %u %u %u %u %u %u %u (roles)",
 * roles one letter per slot (1, 2, B, or -). Returns 1 and the length
 * (without the NUL) on success; 0 with buffer[0] = '\0' when it does not fit
 * or on NULL.
 */
int NativeArcadeLinkAutopilot_FormatMatch(const struct NativeArcadeLinkHostMatch *match, char *buffer, size_t capacity, size_t *length);

/* The report text. Returns 1 and the length on success; 0 with buffer[0] = '\0' when it does not fit or on NULL. */
int NativeArcadeLinkAutopilot_FormatReport(const struct NativeArcadeLinkAutopilot *autopilot, char *buffer, size_t capacity, size_t *length);

/* Writes the report to path (created or replaced). Returns 1 on success. */
int NativeArcadeLinkAutopilot_WriteReport(const char *path, const struct NativeArcadeLinkAutopilot *autopilot);

/* The direction of (dx, dz) in 12-bit angle units, 0..ANGLE_UNITS - 1; 0 for (0, 0). */
int32_t NativeArcadeLinkAutopilot_Angle(int32_t dx, int32_t dz);

/* The steering buttons for one tick (active-high BUTTON_* bits): CROSS, plus
 * STEER_POSITIVE or STEER_NEGATIVE outside the deadband; 0 for NULL. */
uint32_t NativeArcadeLinkAutopilot_Steer(const struct NativeArcadeLinkAutopilotSteerFacts *facts);

/* 1 when the kart passed the restart point (see Steering above); 0 otherwise
 * and for NULL. A zero approach segment leaves only the radius. */
int NativeArcadeLinkAutopilot_Passed(const struct NativeArcadeLinkAutopilotPassFacts *facts);

#endif
