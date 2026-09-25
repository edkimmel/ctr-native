#ifndef PLATFORM_NATIVE_ARCADE_LINK_HOST_H
#define PLATFORM_NATIVE_ARCADE_LINK_HOST_H

#include <stdint.h>

#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_identity.h"

/*
 * Arcade-link host glue (docs/GAME_LOOP_UI_MILESTONE.md section 2.6).
 *
 * A process-wide singleton, like platform/native_platform.c, that owns the
 * arcade-link host adapter (link mode) or a scripted screen preview (preview
 * mode) and gives game code a small API over it: configure it once from the
 * parsed host options, enter the link from the title screen, tick it once per
 * game-loop tick, read a flat view for the screen drawer, and shut it down.
 *
 * Everything here is dormant unless NativeArcadeLinkHost_Configure is given
 * options with the link enabled or a preview requested. With no Configure
 * call, or with default options, the mode is OFF and every call below is
 * inert: no socket opens, nothing ticks, and GetView reports nothing.
 * Preview mode never opens a socket either.
 *
 * This header is safe for game code: it names only this module's own types,
 * the host options, the menu-input button bits and events, the identity
 * struct, and the match config struct, which it only forward-declares (it
 * does not include the match-config header; the caller that reads the
 * agreed config includes that itself). RaceStep's projected state is
 * forward-declared the same way.
 * Screen, lobby-status, end-reason, and action values use the enums of
 * include/platform/native_arcade_flow.h, carried here as uint32_t.
 *
 * No heap use and no wall clock: every duration is counted in caller ticks.
 */

/* Forward-declared only: NativeArcadeLinkHost_GetAgreedConfig copies one. */
struct NativeMatchConfigV1;
/* Forward-declared only: NativeArcadeLinkHost_RaceStep passes the race
 * tick's projected state through to the linked-race drive. */
struct NativeCanonicalStateV4;

enum NativeArcadeLinkHostMode
{
	NATIVE_ARCADE_LINK_HOST_MODE_OFF = 0,
	NATIVE_ARCADE_LINK_HOST_MODE_LINK = 1,
	NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW = 2
};

/* The select view's per-human and bot capacities, and the match slot count. */
#define NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS 4u
#define NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS 8u
#define NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS 8u

/*
 * Host names for the values the select view and the agreed match carry, so
 * game code (the select screens) reads them without naming the select or
 * match-config modules. platform/native_arcade_link_host.c static-asserts
 * each against the module value it mirrors.
 */

/* Select items: NativeArcadeLinkHostSelectView.currentItem and
 * NativeArcadeLinkHostSelectHumanView.currentItem. */
#define NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER 0u
#define NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK 1u
#define NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS 2u
#define NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE 3u

/* Lock bits: NativeArcadeLinkHostSelectHumanView.lockMask. */
#define NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER 0x1u
#define NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK 0x2u
#define NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS 0x4u

/* Select statuses: NativeArcadeLinkHostSelectView.status. */
#define NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING 0u
#define NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING 1u
#define NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_RESOLVED 2u
#define NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED 3u
#define NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_FAILED 4u

/* Slot roles: NativeArcadeLinkHostMatch.slotRole. */
#define NATIVE_ARCADE_LINK_HOST_ROLE_INACTIVE 0u
#define NATIVE_ARCADE_LINK_HOST_ROLE_CAB1 1u
#define NATIVE_ARCADE_LINK_HOST_ROLE_CAB2 2u
#define NATIVE_ARCADE_LINK_HOST_ROLE_BOT 3u

/* The most humans and bots a select can hold. */
#define NATIVE_ARCADE_LINK_HOST_MAX_HUMANS 4u
#define NATIVE_ARCADE_LINK_HOST_MAX_BOTS 8u

/* One human on the select screens (docs/MATCH_SELECT_MILESTONE.md section
 * 2.7): the local human's own state, or a peer's latest state. Everything is
 * 0 while present is 0. */
struct NativeArcadeLinkHostSelectHumanView
{
	/* 1 for the local human always; 1 for a peer once it was heard */
	uint8_t present;
	/* cursor or locked value (a base character ID, 0..7) */
	uint8_t characterID;
	/* cursor or locked value (a levelID): this human's vote */
	uint8_t trackID;
	/* cursor or locked value (3, 5, or 7): this human's vote */
	uint8_t lapCount;
	/* NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_* bits */
	uint8_t lockMask;
	/* NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_* */
	uint8_t currentItem;
	uint8_t reserved[2];
};

/* The select phase, flat. Everything is 0 unless active. */
struct NativeArcadeLinkHostSelectView
{
	/* 1 on the SELECT and SELECT_RESULT screens while a select exists */
	uint8_t active;
	uint8_t humanCount;
	/* the local human's index (cabinet - 1) */
	uint8_t localHuman;
	/* the local human's current item (NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_*) */
	uint8_t currentItem;
	/* ticks until the local current item auto-locks; 0 once done */
	uint32_t ticksLeft;
	/* NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_* */
	uint8_t status;
	/* 1 when the outcome fields below are valid */
	uint8_t resolved;
	uint8_t trackID;
	uint8_t lapCount;
	/* 1 if the track came from a tie draw */
	uint8_t trackDrawn;
	/* 1 if the lap count came from a tie draw */
	uint8_t lapsDrawn;
	/* bit h: human h was reassigned a character */
	uint8_t characterReassignedMask;
	uint8_t botCount;
	/* the first humanCount used, the rest 0 */
	uint8_t humanCharacter[NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS];
	/* the first botCount used, the rest 0 */
	uint8_t botCharacter[NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS];
	/* bit c: a peer has locked base character c (greyed on the character
	 * screen; it cannot be confirmed) */
	uint16_t peerLockedCharacterMask;
	uint8_t reserved[2];
	/* indexed by human; entries at or above humanCount stay 0 */
	struct NativeArcadeLinkHostSelectHumanView humans[NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS];
};

/* Everything a screen drawer needs. Fields not meaningful for the current
 * screen are 0. */
struct NativeArcadeLinkHostView
{
	/* enum NativeArcadeFlowScreen */
	uint32_t screen;
	/* enum NativeArcadeFlowLobbyStatus */
	uint32_t lobbyStatus;
	/* enum NativeArcadeFlowEndReason */
	uint32_t endReason;
	uint32_t selectedRow;
	/* Ticks in the current screen; on screen OFF, the idle (attract) ticks. */
	uint32_t ticksInScreen;
	/* 1 or 2 */
	uint8_t localCab;
	/* 1 only on RESULTS once the menu accepts input */
	uint8_t rowsEnabled;
	/* 1 when the screen is OFF: the title attract layout applies */
	uint8_t attract;
	/* enum NativeArcadeMenuEvent: the event produced from the local buttons
	 * on the last NativeArcadeLinkHost_Tick, whether or not the screen acted
	 * on it; consumers must gate on a state change. LINK only: NONE in
	 * PREVIEW, before the first tick, on every tick that starts on screen
	 * OFF, and on every tick without a new edge. Local input only: a peer's
	 * input never appears here. */
	uint8_t localMenuEvent;
	/* The select screens (docs/MATCH_SELECT_MILESTONE.md section 2.7). */
	struct NativeArcadeLinkHostSelectView select;
};

/* The agreed match, for logging. slotRole uses NATIVE_ARCADE_LINK_HOST_ROLE_*
 * (the match-config slot roles: 0 inactive, 1 cabinet 1, 2 cabinet 2, 3
 * bot). */
struct NativeArcadeLinkHostMatch
{
	uint32_t trackID;
	uint32_t lapCount;
	uint64_t masterSeed;
	uint8_t slotRole[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS];
	uint8_t slotCharacter[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS];
};

/* The end of one linked race, for the end-of-race log line
 * (the linked-race plan, LR-14 and LR-S6). Host-local. */
struct NativeArcadeLinkHostRaceEnd
{
	/* 1 for the first race since Configure or AbortToTitle */
	uint32_t raceNumber;
	/* enum NativeArcadeFlowEndReason the race ended with */
	uint32_t endReason;
	/* records of another match (stale bundles, typically of the match
	 * before a rematch) the link dropped since the previous race end */
	uint32_t foreignBundleDrops;
};

/*
 * Always shuts down first, so a second call replaces the first. NULL options
 * leave the mode OFF and return 0. Options with neither the link enabled nor
 * a preview leave the mode OFF and return 1. A preview (without the link)
 * selects PREVIEW mode and returns 1; identity is not needed. An enabled link
 * needs a non-NULL identity that builds the fixed fixture, and a valid port
 * and peer list; it selects LINK mode, dormant on screen OFF with no socket
 * open, and returns 1. On any failure the mode is OFF and 0 is returned.
 */
int NativeArcadeLinkHost_Configure(const struct NativeArcadeLinkOptions *options,
	const struct NativeIdentityV1 *identity);

/* enum NativeArcadeLinkHostMode */
uint32_t NativeArcadeLinkHost_Mode(void);

/* LINK: 1 while the flow is on any screen other than OFF. PREVIEW: 1. OFF: 0. */
int NativeArcadeLinkHost_ScreenActive(void);

/* LINK only: from screen OFF, enters the lobby and returns 1. Otherwise 0. */
int NativeArcadeLinkHost_Enter(void);

/* One game-loop tick. heldMenuButtons uses the NATIVE_ARCADE_MENU_BUTTON_*
 * bits; raceFinished is nonzero once the local race has finished. Returns an
 * enum NativeArcadeFlowAction: START_RACE and RETURN_TO_TITLE are the
 * caller's cue; every other action has already been executed. OFF and
 * PREVIEW return NONE. In LINK mode, after the adapter's tick, it runs one
 * tick of the drive's finish linger while the drive has a finish end, and
 * re-initializes the drive once the linger is done or stopped, and on every
 * tick whose flow is neither on RACING nor on RESULTS. */
uint32_t NativeArcadeLinkHost_Tick(uint32_t heldMenuButtons, uint8_t raceFinished);

/* Fills *view and returns 1; returns 0 on NULL or in mode OFF. */
int NativeArcadeLinkHost_GetView(struct NativeArcadeLinkHostView *view);

/* LINK only: when the link has an agreed race config (on RACING, and on
 * RESULTS after a race was started), fills *out from it and returns 1.
 * Otherwise (NULL, OFF, PREVIEW, or no agreed config) returns 0 with *out
 * untouched. */
int NativeArcadeLinkHost_GetAgreedMatch(struct NativeArcadeLinkHostMatch *out);

/* LINK only (docs/RACE_LAUNCH_MILESTONE.md RL-8): when the link has an
 * agreed race config (on RACING, and on RESULTS after a race was started),
 * copies its exact bytes into *out and returns 1: the config the race is
 * armed with. Otherwise (NULL, OFF, PREVIEW, or no agreed config) returns 0
 * with *out untouched. */
int NativeArcadeLinkHost_GetAgreedConfig(struct NativeMatchConfigV1 *out);

/* LINK only (docs/RACE_LAUNCH_MILESTONE.md RL-11): the caller reports that
 * the local race failed (setup, launch, a bounded wait, or a FAILED setup
 * status). On RACING it is latched and 1 is returned; the next
 * NativeArcadeLinkHost_Tick moves the flow to RESULTS with end reason
 * LINK_ERROR (outranking a same-tick raceFinished; a link failure already
 * pending is shown instead) and consumes it. Otherwise (OFF, PREVIEW, or any
 * other screen) it is ignored and 0 is returned. The peer is not told:
 * nothing is sent and the link stays open, exactly as after a finished race.
 * A latch never outlives its race: Enter, AbortToTitle, Shutdown, and every
 * link reset of the flow clear it. */
int NativeArcadeLinkHost_ReportRaceFailure(void);

/* LINK only (the linked-race plan, LR-14 and LR-S6): once per race,
 * from the tick the flow moves RACING -> RESULTS, fills *out with that
 * race's end-of-race record and returns 1; the caller logs it. Otherwise
 * (NULL, OFF, PREVIEW, or nothing latched, including a record already
 * taken) returns 0 with *out untouched. */
int NativeArcadeLinkHost_TakeRaceEnd(struct NativeArcadeLinkHostRaceEnd *out);

/* 1 iff the mode is LINK and the flow is on RACING (docs/RACE_LAUNCH_MILESTONE.md
 * RL-8); 0 otherwise, including OFF, PREVIEW, and before any Configure. */
uint8_t NativeArcadeLinkHost_Racing(void);

/* LINK only: closes the link and returns the flow to screen OFF, ready for a
 * new Enter. For when the game cannot honour START_RACE yet. The link is
 * re-initialized with a new select entropy (see
 * NativeArcadeLinkHost_MixSelectEntropy), and so is the race drive. */
void NativeArcadeLinkHost_AbortToTitle(void);

/* Pure: the select entropy handed to the link for one host epoch,
 * entropy ^ (epoch * 0x9E3779B97F4A7C15), modulo 2^64. The host keeps a
 * process-local epoch counter that every LINK Configure and every
 * AbortToTitle increments before it initializes the link, so the select
 * nonces keep varying across the re-initialization AbortToTitle performs,
 * even though the link restarts its own select count there. */
uint64_t NativeArcadeLinkHost_MixSelectEntropy(uint64_t entropy, uint64_t epoch);

/* LINK only (linked-race plan LR-7): the race caller's Launch frame, called
 * only after the race setup launched the race (never after an Arm or Launch
 * failure). Turns the host-local fixed VBlank pacing on, so every race tick
 * advances exactly the VBlanks it asks for whatever the host frame time, and
 * returns 1. It also begins the race drive over the link's running session
 * when the flow is on RACING (race tick limit 0, the default); a refused
 * begin ends the drive as a local failure and is reported as a local race
 * failure, and the return stays 1. Otherwise (OFF, PREVIEW, or before any
 * Configure) returns 0 with nothing done. */
int NativeArcadeLinkHost_RaceBegin(void);

/* The race caller's Disarm frame (the first idle main-menu frame after the
 * race, or at once after an Arm or Launch failure at the title): turns off
 * again the fixed pacing that RaceBegin turned on. When RaceBegin has not
 * turned it on (no race began, or it already ended) it does nothing, so a
 * pacing the host did not turn on is never touched. Whatever the pacing, it
 * first re-initializes the race drive, unless the drive still lingers after
 * a finish while the flow shows RESULTS (the linger overlaps the return
 * load; Tick ends it). */
void NativeArcadeLinkHost_RaceEnd(void);

/*
 * The linked-race drive (the linked-race plan, LR-1, LR-9, LR-13, LR-S9):
 * RaceBegin also begins the drive over the link of the race it launches
 * (LINK mode, flow on RACING), RaceStep runs one race tick, and RaceHold one
 * iteration of the blocking hold. Host-local: the drive state never enters a
 * saved state, a recording, or canonical state.
 */

/* What RaceStep and RaceHold return. */
#define NATIVE_ARCADE_LINK_HOST_RACE_GO 1u
#define NATIVE_ARCADE_LINK_HOST_RACE_HOLD 2u
#define NATIVE_ARCADE_LINK_HOST_RACE_END 3u

/* How the drive ended: NativeArcadeLinkHostDriveState.endKind. */
#define NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE 0u
#define NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE 1u
#define NATIVE_ARCADE_LINK_HOST_DRIVE_END_FINISH_GRACE 2u
#define NATIVE_ARCADE_LINK_HOST_DRIVE_END_RACE_TICK_LIMIT 3u
#define NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME 4u
#define NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE 5u

/* A race tick that does not exist (yet): NativeArcadeLinkHostDriveState. */
#define NATIVE_ARCADE_LINK_HOST_NO_TICK 0xFFFFFFFFu

/* The four retail pads RaceStep and RaceHold commit. */
#define NATIVE_ARCADE_LINK_HOST_RACE_PADS 4u

/*
 * One pad, field for field and byte for byte the platform input layer's
 * pad snapshot (struct PlatformInputPadSnapshot: status, id, buttons[2],
 * analog[4], connected, reserved[3]; 12 bytes). This header may not include
 * that layer's header, so the race caller that converts between the two
 * static-asserts the mirror (LR-S10). RaceStep reads a sample's first nine
 * bytes and ignores reserved; every committed pad it writes has reserved
 * zeroed.
 */
struct NativeArcadeLinkHostPad
{
	uint8_t status;
	uint8_t id;
	uint8_t buttons[2];
	uint8_t analog[4];
	uint8_t connected;
	uint8_t reserved[3];
};

/* The game facts of one race tick, read by the race caller (LR-1, LR-18).
 * Pointer-free values. */
struct NativeArcadeLinkHostRaceFacts
{
	/* nonzero: the race tick sees END_OF_RACE */
	uint32_t endOfRace;
	/* humans (slots 0..humans-1) that have finished */
	uint32_t finishedHumans;
	/* the human count, 1..4 */
	uint32_t humans;
};

/* The drive's state, for the race caller and its log. */
struct NativeArcadeLinkHostDriveState
{
	/* NATIVE_ARCADE_LINK_HOST_DRIVE_END_* */
	uint32_t endKind;
	/* why a LOCAL_FAILURE end happened; 0 otherwise (a code for the log;
	 * NativeArcadeLinkHost_DriveFailureName names it) */
	uint32_t failureReason;
	/* the race tick the drive ended on; NO_TICK while it runs */
	uint32_t endTick;
	/* the finish grace's start tick; NO_TICK before it starts */
	uint32_t graceStartTick;
	/* the race tick of the last RaceStep that passed its checks; NO_TICK
	 * before that */
	uint32_t raceTick;
	/* full tick periods held on that race tick (the hold's periods) */
	uint32_t heldPeriods;
	/* host ticks of the finish linger left */
	uint32_t lingerTicksLeft;
	/* 1 from RaceBegin's drive begin until the drive is re-initialized or
	 * RaceEnd keeps a lingering drive (LR-56) */
	uint8_t begun;
	/* 1 once heldPeriods reaches the hold grace (the banner is due) */
	uint8_t bannerDue;
	/* 1 once this race's local drive failure was reported */
	uint8_t failureReported;
	uint8_t reserved;
};

/*
 * LINK only: one race tick (the race caller's hook, once per race tick).
 * raceTick counts from 0; state is that tick's projected state (its frame
 * number must be raceTick), localSample the raw local sample, and facts the
 * tick's game facts. Returns GO with padsOut[0..3] the committed pads to
 * install, HOLD (padsOut untouched; call RaceHold until GO or END), or END
 * (padsOut untouched; see NativeArcadeLinkHost_GetDriveState). A local drive
 * failure is reported as a local race failure, once per race; an outcome end
 * was already latched by the link; a finish end is the caller's to report
 * (raceFinished on the next Tick). Refused with END and nothing sent outside
 * LINK, before RaceBegin, and whenever the flow is not on RACING, where the
 * drive is re-initialized instead; but a drive whose finish linger still
 * runs is left to Tick untouched (END, nothing sent), so a stray call cannot
 * cut the linger short. After END the caller must not call RaceStep or
 * RaceHold again until the next RaceBegin (LR-54; LR-S10 pins the caller).
 */
uint32_t NativeArcadeLinkHost_RaceStep(uint32_t raceTick, const struct NativeCanonicalStateV4 *state,
	const struct NativeArcadeLinkHostPad *localSample, const struct NativeArcadeLinkHostRaceFacts *facts,
	struct NativeArcadeLinkHostPad padsOut[4]);

/*
 * LINK only: one iteration of the blocking hold after a HOLD; periods and
 * newPeriod are the hold loop's own arguments (the full tick periods held so
 * far, and nonzero on the first iteration of each new period). Returns and
 * refuses as RaceStep does, and likewise leaves a running finish linger to
 * Tick. After END the caller must not call RaceHold or RaceStep again until
 * the next RaceBegin (LR-54; LR-S10 pins the caller).
 */
uint32_t NativeArcadeLinkHost_RaceHold(uint32_t periods, int newPeriod, struct NativeArcadeLinkHostPad padsOut[4]);

/* LINK only: fills *out with the drive's state and returns 1; otherwise
 * (NULL, OFF, or PREVIEW) returns 0 with *out untouched. */
int NativeArcadeLinkHost_GetDriveState(struct NativeArcadeLinkHostDriveState *out);

/* Fixed log names of an end kind ("none", "end of race", "finish grace",
 * "race tick limit", "outcome", "local failure") and of a failure reason;
 * "unknown" otherwise. */
const char *NativeArcadeLinkHost_DriveEndKindName(uint32_t endKind);
const char *NativeArcadeLinkHost_DriveFailureName(uint32_t failureReason);

/* Closes any open link and returns to mode OFF. First, as RaceEnd does, it
 * turns off a fixed pacing that RaceBegin turned on (before a process exit,
 * LR-7). Shutdown is also reached mid-race, where it turns that pacing off
 * too: from a replacing Configure, and from AbortToTitle's defensive branch
 * when the adapter fails to reinitialize. Either way the race's link is gone,
 * so that linked race cannot go on. It re-initializes the race drive. Idempotent
 * and safe before any Configure. */
void NativeArcadeLinkHost_Shutdown(void);

#endif
