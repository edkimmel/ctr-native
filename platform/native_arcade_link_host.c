#include "platform/native_arcade_link_host.h"

#include <platform.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host_internal.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_arcade_netplay.h"
#include "platform/native_arcade_race_drive.h"
#include "platform/native_identity.h"

/*
 * Arcade-link host glue (docs/GAME_LOOP_UI_MILESTONE.md section 2.6). Host
 * glue, like platform/native_platform.c, so the singleton state lives in
 * file-scope statics. The adapter is large (it owns a lobby, peer link, and
 * session), so it lives in static storage rather than on any stack.
 */

/* The host select view mirrors the adapter's field for field. */
_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS,
	"the host select view holds exactly the adapter's humans");
_Static_assert(NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS,
	"the host select view holds exactly the adapter's bots");
_Static_assert(sizeof(struct NativeArcadeLinkHostSelectView) == sizeof(struct NativeArcadeNetplaySelectView),
	"the host select view has the adapter's layout");
_Static_assert(sizeof(struct NativeArcadeLinkHostSelectHumanView) == sizeof(struct NativeArcadeNetplaySelectHumanView),
	"the host select human view has the adapter's layout");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,
	"the agreed match holds exactly the config's slots");

/* Field for field: every host select-view field sits at the adapter's
 * offset. (The agreed match has no adapter counterpart: it is filled from
 * the match config itself.) */
#define NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(hostStruct, netplayStruct, field) \
	_Static_assert(offsetof(struct hostStruct, field) == offsetof(struct netplayStruct, field), \
		#hostStruct "." #field " must sit at its adapter offset")

NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, present);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, characterID);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, trackID);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, lapCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, lockMask);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, currentItem);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectHumanView, NativeArcadeNetplaySelectHumanView, reserved);

NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, active);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humanCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, localHuman);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, currentItem);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, ticksLeft);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, status);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, resolved);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, trackID);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, lapCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, trackDrawn);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, lapsDrawn);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, characterReassignedMask);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, botCount);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humanCharacter);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, botCharacter);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, peerLockedCharacterMask);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, reserved);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostSelectView, NativeArcadeNetplaySelectView, humans);

/* The host names for the select view and agreed-match values equal the
 * module values they mirror. */
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER == (unsigned)NATIVE_MATCH_SELECT_ITEM_CHARACTER,
	"host select item CHARACTER");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK == (unsigned)NATIVE_MATCH_SELECT_ITEM_TRACK,
	"host select item TRACK");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS == (unsigned)NATIVE_MATCH_SELECT_ITEM_LAPS,
	"host select item LAPS");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE == (unsigned)NATIVE_MATCH_SELECT_ITEM_DONE,
	"host select item DONE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER == NATIVE_MATCH_SELECT_LOCK_CHARACTER,
	"host select lock CHARACTER");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK == NATIVE_MATCH_SELECT_LOCK_TRACK, "host select lock TRACK");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS == NATIVE_MATCH_SELECT_LOCK_LAPS, "host select lock LAPS");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING == (unsigned)NATIVE_MATCH_SELECT_STATUS_PICKING,
	"host select status PICKING");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING == (unsigned)NATIVE_MATCH_SELECT_STATUS_WAITING,
	"host select status WAITING");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_RESOLVED == (unsigned)NATIVE_MATCH_SELECT_STATUS_RESOLVED,
	"host select status RESOLVED");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED == (unsigned)NATIVE_MATCH_SELECT_STATUS_CONFIRMED,
	"host select status CONFIRMED");
_Static_assert(NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_FAILED == (unsigned)NATIVE_MATCH_SELECT_STATUS_FAILED,
	"host select status FAILED");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_INACTIVE == (unsigned)NATIVE_MATCH_SLOT_ROLE_INACTIVE,
	"host role INACTIVE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_CAB1 == (unsigned)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, "host role CAB1");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_CAB2 == (unsigned)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, "host role CAB2");
_Static_assert(NATIVE_ARCADE_LINK_HOST_ROLE_BOT == (unsigned)NATIVE_MATCH_SLOT_ROLE_BOT, "host role BOT");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_HUMANS == NATIVE_MATCH_SELECT_MAX_HUMANS, "host max humans");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_HUMANS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS,
	"host max humans is the adapter view's capacity");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_HUMANS == NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS,
	"host max humans is the host view's capacity");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_BOTS == NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS, "host max bots");
_Static_assert(NATIVE_ARCADE_LINK_HOST_MAX_BOTS == NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS,
	"host max bots is the host view's capacity");

/* The race drive's host names (LR-S9) equal the drive core's values. */
_Static_assert(NATIVE_ARCADE_LINK_HOST_RACE_GO == (unsigned)NATIVE_ARCADE_RACE_DRIVE_GO, "host race GO");
_Static_assert(NATIVE_ARCADE_LINK_HOST_RACE_HOLD == (unsigned)NATIVE_ARCADE_RACE_DRIVE_HOLD, "host race HOLD");
_Static_assert(NATIVE_ARCADE_LINK_HOST_RACE_END == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END, "host race END");
_Static_assert(NATIVE_ARCADE_LINK_HOST_DRIVE_END_NONE == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END_NONE, "host drive end NONE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_DRIVE_END_OF_RACE == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE,
	"host drive end END_OF_RACE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_DRIVE_END_FINISH_GRACE == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE,
	"host drive end FINISH_GRACE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_DRIVE_END_RACE_TICK_LIMIT == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT,
	"host drive end RACE_TICK_LIMIT");
_Static_assert(NATIVE_ARCADE_LINK_HOST_DRIVE_END_OUTCOME == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME,
	"host drive end OUTCOME");
_Static_assert(NATIVE_ARCADE_LINK_HOST_DRIVE_END_LOCAL_FAILURE == (unsigned)NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE,
	"host drive end LOCAL_FAILURE");
_Static_assert(NATIVE_ARCADE_LINK_HOST_NO_TICK == NATIVE_ARCADE_RACE_DRIVE_NO_TICK, "host no tick");
_Static_assert(NATIVE_ARCADE_LINK_HOST_RACE_PADS == NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT, "host race pads");

/* The host pad is 12 bytes laid out as the platform input layer's pad
 * snapshot (the race caller static-asserts that mirror, LR-S10), and its
 * first nine bytes are the drive's pad, field for field. The conversions
 * below still copy field by field. */
_Static_assert(sizeof(struct NativeArcadeLinkHostPad) == 12u, "the host pad is 12 bytes");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, status) == 0u, "host pad status");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, id) == 1u, "host pad id");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, buttons) == 2u, "host pad buttons");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, analog) == 4u, "host pad analog");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, connected) == 8u, "host pad connected");
_Static_assert(offsetof(struct NativeArcadeLinkHostPad, reserved) == 9u, "host pad reserved");
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostPad, NativeCanonicalInputPadV1, status);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostPad, NativeCanonicalInputPadV1, id);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostPad, NativeCanonicalInputPadV1, buttons);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostPad, NativeCanonicalInputPadV1, analog);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostPad, NativeCanonicalInputPadV1, connected);
_Static_assert(sizeof(struct NativeCanonicalInputPadV1) == 9u, "the drive's pad is the host pad's first nine bytes");
_Static_assert(sizeof(struct NativeArcadeLinkHostRaceFacts) == sizeof(struct NativeArcadeRaceDriveFacts),
	"the host race facts have the drive's layout");
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostRaceFacts, NativeArcadeRaceDriveFacts, endOfRace);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostRaceFacts, NativeArcadeRaceDriveFacts, finishedHumans);
NATIVE_ARCADE_LINK_HOST_SAME_OFFSET(NativeArcadeLinkHostRaceFacts, NativeArcadeRaceDriveFacts, humans);

/* The select entropy mix constant: 2^64 divided by the golden ratio. */
#define NATIVE_ARCADE_LINK_HOST_ENTROPY_STEP UINT64_C(0x9E3779B97F4A7C15)

/* Select previews: the opponent cursor moves one step every
 * PREVIEW_STEP_TICKS preview ticks, and the local countdown runs over
 * PREVIEW_ITEM_TICKS (the 20 s select item, OD-1), so a capture at a given
 * frame is deterministic. */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_STEP_TICKS 30u
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_ITEM_TICKS 600u
/* The preview picks: the local cabinet (human 0) is CRASH on CRASH_COVE,
 * the opponent (human 1) is CORTEX voting TIGER_TEMPLE; both vote 3 laps. */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_CHARACTER 0u    /* CRASH */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_CHARACTER 1u /* CORTEX */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_TRACK 3u        /* CRASH_COVE */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK 4u     /* TIGER_TEMPLE */
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS 3u
#define NATIVE_ARCADE_LINK_HOST_PREVIEW_BOT_COUNT 4u

static uint32_t g_mode = NATIVE_ARCADE_LINK_HOST_MODE_OFF;
static struct NativeArcadeLinkOptions g_options;
static struct NativeArcadeNetplay g_netplay;
static struct NativeArcadeNetplayConfig g_config;
/* Ticks spent on screen OFF in LINK mode; drives the attract blink. */
static uint32_t g_idleTicks;
/* Ticks since PREVIEW mode was configured. */
static uint32_t g_previewTicks;
/* Process-local host epoch: incremented by every LINK Configure and every
 * AbortToTitle, before the adapter is initialized. Shutdown never resets
 * it, so no two initializations in one process share an epoch. */
static uint64_t g_epoch;
/* 1 from a RaceBegin that turned the fixed VBlank pacing on until the
 * RaceEnd or Shutdown that turns it off again (LR-7). Host-local: it never
 * enters a saved state, a recording, or canonical state. */
static uint8_t g_racePacing;
/* The linked race's drive (LR-1, LR-41) and its kept-bundle ring (LR-3),
 * over the adapter's link. Host-local (LR-15): neither ever enters a saved
 * state, a recording, or canonical state. The drive holds a raw pointer to
 * the link's session, which a rematch re-initializes, so the drive is
 * re-initialized whenever its race is over (NativeArcadeLinkHost_ResetDrive's
 * callers) and never steps, holds, or lingers off its race. */
static struct NativeArcadeRaceDrive g_drive;
static struct NativeArcadeRaceDriveKept g_driveKept;
/* 1 from RaceBegin's drive begin (accepted or refused) until the drive is
 * re-initialized or RaceEnd keeps a lingering drive (LR-56): RaceStep and
 * RaceHold run only while it is set. */
static uint8_t g_driveBegun;
/* 1 once this race's local drive failure was reported to the adapter. */
static uint8_t g_driveFailureReported;
/* Local drive failures reported since Configure or Shutdown (a test
 * read-back). */
static uint32_t g_driveFailureReports;
/* The race tick limit the next LINK RaceBegin hands the drive (LR-60): 0,
 * the drive's default 18000, unless NativeArcadeLinkHost_SetRaceTickLimit
 * lowered it. Host-local: it never enters a saved state, a recording,
 * canonical state, or the wire. Shutdown (and so Configure) resets it. */
static uint32_t g_raceTickLimit;
/* The divergence record (LR-11, LR-70): latched from the race link's session
 * once per race, the first time a Tick, RaceStep, or RaceHold finds its
 * divergence latched, and taken once. g_raceDivergenceRace is the race
 * number (the link's match count) it was latched for, 0 for none, so a race
 * latches at most once and the next race starts clean. Host-local and
 * pointer-free: it never enters a saved state, a recording, canonical
 * state, or the wire. Shutdown and AbortToTitle drop it. */
static struct NativeArcadeLinkHostRaceDivergence g_raceDivergence;
static uint8_t g_raceDivergencePending;
static uint32_t g_raceDivergenceRace;
/* The solo gate (docs/SOLO_CAB_MILESTONE.md SOLO-11): solo stays dark until
 * the solo race launch slice (SOLO-S4) and its live proof land, so a
 * cabinet is never left on a solo screen that cannot race. Only the unit
 * tests' NativeArcadeLinkHost_InternalSetSoloEnabled changes it; every LINK
 * Configure reads it. Host-local. */
#define NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT 0u
static uint8_t g_soloEnabled = NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT;

uint64_t NativeArcadeLinkHost_MixSelectEntropy(uint64_t entropy, uint64_t epoch)
{
	return entropy ^ (epoch * NATIVE_ARCADE_LINK_HOST_ENTROPY_STEP);
}

/* A new epoch and the adapter entropy derived from it; called right before
 * every NativeArcadeNetplay_Init. */
static void NativeArcadeLinkHost_NextEpoch(uint64_t entropy)
{
	g_epoch += 1u;
	g_config.selectEntropy = NativeArcadeLinkHost_MixSelectEntropy(entropy, g_epoch);
}

uint64_t NativeArcadeLinkHost_InternalSelectEntropy(void)
{
	return (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) ? g_config.selectEntropy : 0u;
}

static uint32_t NativeArcadeLinkHost_SaturatingIncrement(uint32_t value)
{
	return (value == UINT32_MAX) ? value : (value + 1u);
}

static uint32_t NativeArcadeLinkHost_LinkScreen(void)
{
	struct NativeArcadeNetplayView view;

	if (!NativeArcadeNetplay_GetView(&g_netplay, &view))
	{
		return NATIVE_ARCADE_FLOW_SCREEN_OFF;
	}
	return view.screen;
}

/* ---- The race drive glue (docs/LOCKSTEP_RACE_MILESTONE.md LR-S9) ---- */

/* Re-initializes the drive (not begun, end kind NONE) and clears the ring
 * and this race's report flag. */
static void NativeArcadeLinkHost_ResetDrive(void)
{
	NativeArcadeRaceDrive_Init(&g_drive);
	memset(&g_driveKept, 0, sizeof(g_driveKept));
	g_driveBegun = 0u;
	g_driveFailureReported = 0u;
}

/* sendBundle: the kept bytes, verbatim, over the race's link. The link
 * refuses (0) unless it and its session are RUNNING and the bytes decode as
 * this session's own bundle (LR-49); a NULL link (no lobby open) refuses. */
static int NativeArcadeLinkHost_DriveSendBundle(void *context, uint32_t frameIndex, const uint8_t *bytes, size_t size)
{
	(void)context;
	(void)frameIndex;
	return NativeLockstepPeerLink_SendBundleVerbatim(NativeArcadeNetplay_Link(&g_netplay), bytes, size);
}

/* The drive's poll: drains the link through the adapter (LR-50). */
static void NativeArcadeLinkHost_DrivePoll(void *context)
{
	(void)context;
	NativeArcadeNetplay_RaceService(&g_netplay, 0);
}

/* The hold's once-per-period service: the poll, then the adapter's launch
 * intake and launch send and linger tick (LR-9, LR-50). The drive calls it
 * only from a held period, so a race tick of 0 is the start wait: its launch
 * send is not cut off by the linger's 300-tick cap, because a peer that has
 * not committed yet needs our records to launch at all (LR-69). The start
 * wait bounds it: it ends at GO or at its timeout, and an ended drive calls
 * nothing. */
static void NativeArcadeLinkHost_DriveServicePeriod(void *context)
{
	(void)context;
	NativeArcadeNetplay_RaceService(&g_netplay,
		(NativeArcadeRaceDrive_RaceTick(&g_drive) == 0u) ? NATIVE_ARCADE_NETPLAY_RACE_SERVICE_START_WAIT : 1);
}

/* onTakeResult: the adapter's OnTakeResult, which returns nothing, so the
 * drive's "latched" is read back from the adapter's pending link failure:
 * nonzero once the adapter holds an outcome for the flow (the LR-S9 note). */
static int NativeArcadeLinkHost_DriveTakeResult(void *context, enum NativeLockstepSessionResult result, uint32_t frameIndex)
{
	(void)context;
	NativeArcadeNetplay_OnTakeResult(&g_netplay, result, frameIndex);
	return (g_netplay.pendingLinkFailure != NATIVE_ARCADE_FLOW_END_NONE) ? 1 : 0;
}

/* The one path by which the glue reports a drive failure: only a
 * LOCAL_FAILURE end, and once per race (LR-12). An OUTCOME end reports
 * nothing (OnTakeResult has latched it), and a finish end is the caller's to
 * report through Tick's raceFinished. */
static void NativeArcadeLinkHost_ReportDriveFailure(void)
{
	if ((g_driveFailureReported != 0u) ||
		(NativeArcadeRaceDrive_EndKind(&g_drive) != NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE))
	{
		return;
	}
	g_driveFailureReported = 1u;
	g_driveFailureReports = NativeArcadeLinkHost_SaturatingIncrement(g_driveFailureReports);
	(void)NativeArcadeNetplay_ReportLocalRaceFailure(&g_netplay);
}

/* RaceBegin's drive part (LINK mode): a fresh drive, begun over the race
 * link's session with the stored race tick limit (0: the default 18000;
 * 1..18000: the internal override, LR-60), when the flow is on RACING. A
 * refused begin has
 * ended the drive as a local failure, which is reported. Off RACING the
 * race is already over and the drive stays re-initialized. */
static void NativeArcadeLinkHost_BeginDrive(void)
{
	struct NativeArcadeRaceDriveCallbacks callbacks;

	NativeArcadeLinkHost_ResetDrive();
	if (NativeArcadeLinkHost_LinkScreen() != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING)
	{
		return;
	}
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.context = NULL;
	callbacks.sendBundle = NativeArcadeLinkHost_DriveSendBundle;
	callbacks.poll = NativeArcadeLinkHost_DrivePoll;
	callbacks.onTakeResult = NativeArcadeLinkHost_DriveTakeResult;
	callbacks.servicePeriod = NativeArcadeLinkHost_DriveServicePeriod;
	g_driveBegun = 1u;
	if (!NativeArcadeRaceDrive_Begin(&g_drive, NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(&g_netplay)),
			&g_driveKept, &callbacks, g_raceTickLimit))
	{
		NativeArcadeLinkHost_ReportDriveFailure();
	}
}

/* RaceEnd's drive part: re-initializes the drive, except while it still
 * lingers after a finish and the flow shows RESULTS: the linger overlaps the
 * return load (LR-13), and Tick ends it (its count, the session, or the flow
 * leaving RESULTS). No step or hold follows RaceEnd either way. */
static void NativeArcadeLinkHost_RaceEndDrive(void)
{
	if ((g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) && NativeArcadeRaceDrive_EndIsFinish(&g_drive) &&
		(NativeArcadeRaceDrive_LingerTicksLeft(&g_drive) > 0u) &&
		(NativeArcadeLinkHost_LinkScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS))
	{
		g_driveBegun = 0u;
		return;
	}
	NativeArcadeLinkHost_ResetDrive();
}

/* Tick's drive part, after the adapter's tick of the same host tick (LR-46):
 * off RACING and RESULTS the race is over and the drive is re-initialized;
 * otherwise, while the drive has a finish end, one linger tick, and once
 * the linger is done or stopped the drive is re-initialized. */
static void NativeArcadeLinkHost_TickDrive(void)
{
	const uint32_t screen = NativeArcadeLinkHost_LinkScreen();

	if ((screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING) && (screen != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS))
	{
		NativeArcadeLinkHost_ResetDrive();
		return;
	}
	if (NativeArcadeRaceDrive_EndIsFinish(&g_drive))
	{
		(void)NativeArcadeRaceDrive_LingerTick(&g_drive, (screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS) ? 1 : 0);
		if (NativeArcadeRaceDrive_LingerTicksLeft(&g_drive) == 0u)
		{
			NativeArcadeLinkHost_ResetDrive();
		}
	}
}

/* The glue's own guard (LR-50, the LR-S9 part 2 note, LR-54): the drive
 * steps and holds only in LINK mode, after RaceBegin began it, while the
 * flow is on RACING. Otherwise nothing is polled, sent, or taken, and the
 * caller gets END. A drive with a finish-kind end and linger ticks left is
 * kept as it is: its linger belongs to Tick (LR-13), so a stray step or hold
 * must not cut it short. Any other refusal in LINK mode re-initializes the
 * drive. */
static int NativeArcadeLinkHost_DriveMayRun(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	if (NativeArcadeRaceDrive_EndIsFinish(&g_drive) && (NativeArcadeRaceDrive_LingerTicksLeft(&g_drive) > 0u))
	{
		return 0;
	}
	if ((g_driveBegun == 0u) || (NativeArcadeLinkHost_LinkScreen() != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING))
	{
		NativeArcadeLinkHost_ResetDrive();
		return 0;
	}
	return 1;
}

/* A host pad to the drive's pad: the first nine bytes, field by field. */
static void NativeArcadeLinkHost_PadIn(const struct NativeArcadeLinkHostPad *from, struct NativeCanonicalInputPadV1 *to)
{
	memset(to, 0, sizeof(*to));
	to->status = from->status;
	to->id = from->id;
	to->buttons[0] = from->buttons[0];
	to->buttons[1] = from->buttons[1];
	to->analog[0] = from->analog[0];
	to->analog[1] = from->analog[1];
	to->analog[2] = from->analog[2];
	to->analog[3] = from->analog[3];
	to->connected = from->connected;
}

/* A drive pad to a host pad, field by field, reserved zeroed. */
static void NativeArcadeLinkHost_PadOut(const struct NativeCanonicalInputPadV1 *from, struct NativeArcadeLinkHostPad *to)
{
	to->status = from->status;
	to->id = from->id;
	to->buttons[0] = from->buttons[0];
	to->buttons[1] = from->buttons[1];
	to->analog[0] = from->analog[0];
	to->analog[1] = from->analog[1];
	to->analog[2] = from->analog[2];
	to->analog[3] = from->analog[3];
	to->connected = from->connected;
	to->reserved[0] = 0u;
	to->reserved[1] = 0u;
	to->reserved[2] = 0u;
}

/* A Step or Hold's status for the caller: GO copies the committed pads out
 * (padsOut is written only on GO); END reports a local drive failure. */
static uint32_t NativeArcadeLinkHost_DriveStatus(enum NativeArcadeRaceDriveStatus status,
	const struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT],
	struct NativeArcadeLinkHostPad padsOut[NATIVE_ARCADE_LINK_HOST_RACE_PADS])
{
	uint32_t pad;

	if (status == NATIVE_ARCADE_RACE_DRIVE_GO)
	{
		for (pad = 0u; pad < NATIVE_ARCADE_LINK_HOST_RACE_PADS; pad++)
		{
			NativeArcadeLinkHost_PadOut(&pads[pad], &padsOut[pad]);
		}
		return NATIVE_ARCADE_LINK_HOST_RACE_GO;
	}
	if (status == NATIVE_ARCADE_RACE_DRIVE_HOLD)
	{
		return NATIVE_ARCADE_LINK_HOST_RACE_HOLD;
	}
	NativeArcadeLinkHost_ReportDriveFailure();
	return NATIVE_ARCADE_LINK_HOST_RACE_END;
}

/* Drops the divergence record and its once-per-race latch: the link it came
 * from is gone (Shutdown, AbortToTitle). */
static void NativeArcadeLinkHost_ResetDivergence(void)
{
	memset(&g_raceDivergence, 0, sizeof(g_raceDivergence));
	g_raceDivergencePending = 0u;
	g_raceDivergenceRace = 0u;
}

/*
 * The divergence record (LR-11, LR-70), once per race: after every call that
 * polls the link or records into the session (Tick, RaceStep, RaceHold), a
 * divergence latched in the race link's session is copied into a
 * pointer-free record for the game hook's log. That covers every latch
 * point: the adapter's own poll (Tick), the drive's poll and take, and a
 * parked digest inside the drive's record (RaceStep). The race number is the
 * link's match count, the same number the end-of-race record carries; a
 * record for this race number is never latched twice, and a record not
 * taken is replaced by the next race's. The report's frame is the divergent
 * race tick and its canonical domain mask the domains. The digests are the
 * two sides' digests of the lowest differing domain, so a divergence that
 * leaves the whole-state digest alone (LR-16's CONTROL-only injection)
 * still logs two different values; with no differing domain (only the
 * whole-state digest differs) they are the whole-state digests.
 */
static void NativeArcadeLinkHost_LatchDivergence(void)
{
	const struct NativeLockstepDivergenceReport *report;
	uint32_t domain;

	if ((g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK) || (g_netplay.matchCount == 0u) ||
		(g_raceDivergenceRace == g_netplay.matchCount))
	{
		return;
	}
	report = NativeLockstepSession_FirstDivergence(NativeLockstepPeerLink_Session(NativeArcadeNetplay_Link(&g_netplay)));
	if (report == NULL)
	{
		return;
	}
	memset(&g_raceDivergence, 0, sizeof(g_raceDivergence));
	g_raceDivergence.raceNumber = g_netplay.matchCount;
	g_raceDivergence.raceTick = report->frameIndex;
	g_raceDivergence.domainMask = report->canonicalDomainMask;
	g_raceDivergence.localDigest = report->localCombinedDigest;
	g_raceDivergence.remoteDigest = report->remoteCombinedDigest;
	for (domain = 0u; domain < NATIVE_CANONICAL_DOMAIN_COUNT; domain++)
	{
		if ((report->canonicalDomainMask & (UINT32_C(1) << domain)) != 0u)
		{
			g_raceDivergence.localDigest = report->localDomainDigests[domain];
			g_raceDivergence.remoteDigest = report->remoteDomainDigests[domain];
			break;
		}
	}
	g_raceDivergencePending = 1u;
	g_raceDivergenceRace = g_netplay.matchCount;
}

uint32_t NativeArcadeLinkHost_RaceStep(uint32_t raceTick, const struct NativeCanonicalStateV4 *state,
	const struct NativeArcadeLinkHostPad *localSample, const struct NativeArcadeLinkHostRaceFacts *facts,
	struct NativeArcadeLinkHostPad padsOut[4])
{
	struct NativeCanonicalInputPadV1 sample;
	struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];
	struct NativeArcadeRaceDriveFacts driveFacts;
	enum NativeArcadeRaceDriveStatus status;

	if (!NativeArcadeLinkHost_DriveMayRun())
	{
		return NATIVE_ARCADE_LINK_HOST_RACE_END;
	}
	if (localSample != NULL)
	{
		NativeArcadeLinkHost_PadIn(localSample, &sample);
	}
	if (facts != NULL)
	{
		driveFacts.endOfRace = facts->endOfRace;
		driveFacts.finishedHumans = facts->finishedHumans;
		driveFacts.humans = facts->humans;
	}
	/* A NULL argument is the drive's own ARGUMENT failure. */
	memset(pads, 0, sizeof(pads));
	status = NativeArcadeRaceDrive_Step(&g_drive, raceTick, state, (localSample != NULL) ? &sample : NULL,
		(facts != NULL) ? &driveFacts : NULL, (padsOut != NULL) ? pads : NULL);
	NativeArcadeLinkHost_LatchDivergence();
	return NativeArcadeLinkHost_DriveStatus(status, pads, padsOut);
}

uint32_t NativeArcadeLinkHost_RaceHold(uint32_t periods, int newPeriod, struct NativeArcadeLinkHostPad padsOut[4])
{
	struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];
	enum NativeArcadeRaceDriveStatus status;

	if (!NativeArcadeLinkHost_DriveMayRun())
	{
		return NATIVE_ARCADE_LINK_HOST_RACE_END;
	}
	memset(pads, 0, sizeof(pads));
	status = NativeArcadeRaceDrive_Hold(&g_drive, periods, newPeriod, (padsOut != NULL) ? pads : NULL);
	NativeArcadeLinkHost_LatchDivergence();
	return NativeArcadeLinkHost_DriveStatus(status, pads, padsOut);
}

int NativeArcadeLinkHost_GetDriveState(struct NativeArcadeLinkHostDriveState *out)
{
	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	memset(out, 0, sizeof(*out));
	out->endKind = (uint32_t)NativeArcadeRaceDrive_EndKind(&g_drive);
	out->failureReason = (uint32_t)NativeArcadeRaceDrive_FailureReason(&g_drive);
	out->endTick = NativeArcadeRaceDrive_EndTick(&g_drive);
	out->graceStartTick = NativeArcadeRaceDrive_GraceStartTick(&g_drive);
	out->raceTick = NativeArcadeRaceDrive_RaceTick(&g_drive);
	out->heldPeriods = NativeArcadeRaceDrive_HeldPeriods(&g_drive);
	out->lingerTicksLeft = NativeArcadeRaceDrive_LingerTicksLeft(&g_drive);
	out->begun = g_driveBegun;
	out->bannerDue = (uint8_t)(NativeArcadeRaceDrive_BannerDue(out->heldPeriods) ? 1u : 0u);
	out->failureReported = g_driveFailureReported;
	return 1;
}

const char *NativeArcadeLinkHost_DriveEndKindName(uint32_t endKind)
{
	return NativeArcadeRaceDrive_EndKindName((enum NativeArcadeRaceDriveEndKind)endKind);
}

const char *NativeArcadeLinkHost_DriveFailureName(uint32_t failureReason)
{
	return NativeArcadeRaceDrive_FailureName((enum NativeArcadeRaceDriveFailure)failureReason);
}

uint8_t NativeArcadeLinkHost_InternalLocalRaceFailure(void)
{
	return (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) ? g_netplay.localRaceFailure : 0u;
}

uint32_t NativeArcadeLinkHost_InternalDriveFailureReports(void)
{
	return g_driveFailureReports;
}

uint32_t NativeArcadeLinkHost_InternalConsecutiveStalls(void)
{
	return (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) ? g_netplay.outcome.consecutiveStallFrames : 0u;
}

uint32_t NativeArcadeLinkHost_InternalLaunchTicksSinceCommit(void)
{
	return (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) ? g_netplay.launch.ticksSinceCommit : 0u;
}

uint32_t NativeArcadeLinkHost_InternalRaceTickLimit(void)
{
	return g_raceTickLimit;
}

uint32_t NativeArcadeLinkHost_InternalDriveRaceTickLimit(void)
{
	return (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK) ? NativeArcadeRaceDrive_RaceTickLimit(&g_drive) : 0u;
}

int NativeArcadeLinkHost_SetRaceTickLimit(uint32_t limit)
{
	/* LR-42: the override may only lower the drive's bound. */
	if (limit > NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT)
	{
		return 0;
	}
	g_raceTickLimit = limit;
	return 1;
}

int NativeArcadeLinkHost_RaceBegin(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	Platform_SetFixedVBlankPacing(1);
	g_racePacing = 1u;
	NativeArcadeLinkHost_BeginDrive();
	return 1;
}

void NativeArcadeLinkHost_RaceEnd(void)
{
	NativeArcadeLinkHost_RaceEndDrive();
	if (g_racePacing == 0u)
	{
		return;
	}
	Platform_SetFixedVBlankPacing(0);
	g_racePacing = 0u;
}

void NativeArcadeLinkHost_Shutdown(void)
{
	/* Before a process exit (main.c, and its atexit registration in LINK
	 * mode), a linked race's fixed pacing is turned off. */
	if (g_racePacing != 0u)
	{
		Platform_SetFixedVBlankPacing(0);
		g_racePacing = 0u;
	}
	NativeArcadeLinkHost_ResetDrive();
	NativeArcadeLinkHost_ResetDivergence();
	g_driveFailureReports = 0u;
	g_raceTickLimit = 0u;
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		NativeArcadeNetplay_Shutdown(&g_netplay);
	}
	memset(&g_options, 0, sizeof(g_options));
	memset(&g_netplay, 0, sizeof(g_netplay));
	memset(&g_config, 0, sizeof(g_config));
	g_idleTicks = 0u;
	g_previewTicks = 0u;
	g_mode = NATIVE_ARCADE_LINK_HOST_MODE_OFF;
}

void NativeArcadeLinkHost_InternalSetSoloEnabled(uint8_t enabled)
{
	g_soloEnabled = (uint8_t)((enabled != 0u) ? 1u : 0u);
}

/*
 * The solo base (docs/SOLO_CAB_MILESTONE.md SOLO-6), built beside the TWO_CAB
 * fixture as the roster proof builds its ONE_CAB config (RS-23): the
 * fixture's identity, track, laps, tick rate, and masterSeed, its CAB1
 * character as the one human (CAB1_HUMAN, slot 0, on either seat), its bot
 * difficulty, the LOAD_Robots1P bots for that character, and the 1P
 * bot-rules digest. It must pass the bot rules' full check. The one-human
 * select then resolves the race config on it.
 */
static int NativeArcadeLinkHost_BuildSoloBase(const struct NativeMatchConfigV1 *fixture, struct NativeMatchConfigV1 *base)
{
	struct NativeMatchConfigV1 candidate;
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];
	uint8_t cab1Slot = 0u;
	uint8_t botDifficulty = 0u;
	int botFound = 0;
	uint32_t bot = 0u;
	uint32_t slot;

	if (!NativeMatchConfigV1_FindRoleSlot(fixture, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot))
	{
		return 0;
	}
	for (slot = 0u; (slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT) && !botFound; slot++)
	{
		if (fixture->slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			botDifficulty = fixture->slots[slot].difficulty;
			botFound = 1;
		}
	}
	if (!botFound || !NativeArcadeBotRules_ExpectedBots1P(fixture->slots[cab1Slot].characterID, bots))
	{
		return 0;
	}
	NativeMatchConfigV1_InitArcadeOneCab(&candidate);
	candidate.trackID = fixture->trackID;
	candidate.lapCount = fixture->lapCount;
	candidate.tickRateNumerator = fixture->tickRateNumerator;
	candidate.tickRateDenominator = fixture->tickRateDenominator;
	candidate.masterSeed = fixture->masterSeed;
	memcpy(candidate.buildIdentity, fixture->buildIdentity, sizeof(candidate.buildIdentity));
	memcpy(candidate.contentIdentity, fixture->contentIdentity, sizeof(candidate.contentIdentity));
	for (slot = 0u; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		struct NativeMatchConfigSlotV1 *target = &candidate.slots[slot];

		if (target->role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN)
		{
			target->characterID = fixture->slots[cab1Slot].characterID;
			target->difficulty = 0u;
		}
		else if (target->role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			if (bot >= NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT)
			{
				return 0;
			}
			target->characterID = bots[bot];
			target->difficulty = botDifficulty;
			bot++;
		}
	}
	if ((bot != NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT) || !NativeArcadeBotRules_Digest1PV1(candidate.botRulesDigest) ||
		!NativeArcadeBotRules_ValidateConfigV1(&candidate))
	{
		return 0;
	}
	*base = candidate;
	return 1;
}

int NativeArcadeLinkHost_Configure(const struct NativeArcadeLinkOptions *options,
	const struct NativeIdentityV1 *identity)
{
	struct NativeMatchConfigV1 fixture;
	uint32_t i;

	NativeArcadeLinkHost_Shutdown();
	if (options == NULL)
	{
		return 0;
	}
	if (options->enabled == 0u)
	{
		if (options->preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_NONE)
		{
			return 1;
		}
		g_options = *options;
		g_previewTicks = 0u;
		g_mode = NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW;
		return 1;
	}

	if (identity == NULL)
	{
		return 0;
	}
	if (!NativeArcadeLinkFixture_Build(identity, &fixture))
	{
		return 0;
	}
	/* Bounds the copy below; Init applies the adapter's own candidate rules. */
	if ((options->peerCount > NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS) ||
		(options->peerCount > (uint32_t)(sizeof(g_config.candidates) / sizeof(g_config.candidates[0]))))
	{
		return 0;
	}
	NativeArcadeNetplay_DefaultConfig(&g_config);
	g_config.fixture = fixture;
	for (i = 0u; i < options->peerCount; i++)
	{
		g_config.candidates[i].ipv4 = options->peers[i].ipv4;
		g_config.candidates[i].port = options->peers[i].port;
	}
	g_config.candidateCount = options->peerCount;
	g_config.localPort = options->localPort;
	g_config.localRole = options->localRole;
	/* SOLO-11: solo only through the gate, and only on a solo base that
	 * builds; otherwise the link runs exactly as without solo. */
	g_config.soloEnabled = 0u;
	if ((g_soloEnabled != 0u) && NativeArcadeLinkHost_BuildSoloBase(&fixture, &g_config.soloBase))
	{
		g_config.soloEnabled = 1u;
	}
	NativeArcadeLinkHost_NextEpoch(options->selectEntropy);
	if (!NativeArcadeNetplay_Init(&g_netplay, &g_config))
	{
		memset(&g_config, 0, sizeof(g_config));
		return 0;
	}
	g_options = *options;
	g_idleTicks = 0u;
	g_mode = NATIVE_ARCADE_LINK_HOST_MODE_LINK;
	return 1;
}

uint32_t NativeArcadeLinkHost_Mode(void)
{
	return g_mode;
}

int NativeArcadeLinkHost_ScreenActive(void)
{
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return (NativeArcadeLinkHost_LinkScreen() != (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1 : 0;
	}
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW)
	{
		return 1;
	}
	return 0;
}

int NativeArcadeLinkHost_Enter(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	return (NativeArcadeNetplay_Enter(&g_netplay) == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY) ? 1 : 0;
}

uint32_t NativeArcadeLinkHost_Tick(uint32_t heldMenuButtons, uint8_t raceFinished)
{
	uint32_t action;

	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW)
	{
		g_previewTicks = NativeArcadeLinkHost_SaturatingIncrement(g_previewTicks);
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	if (NativeArcadeLinkHost_LinkScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF)
	{
		g_idleTicks = NativeArcadeLinkHost_SaturatingIncrement(g_idleTicks);
	}
	else
	{
		g_idleTicks = 0u;
	}
	action = (uint32_t)NativeArcadeNetplay_Tick(&g_netplay, heldMenuButtons, raceFinished);
	NativeArcadeLinkHost_TickDrive();
	NativeArcadeLinkHost_LatchDivergence();
	return action;
}

/*
 * Synthesizes the select view of one select preview (*view already zeroed):
 * two humans, the local cabinet human 0. The opponent's cursor steps through
 * its current item's table in the rules module's order, one step every
 * PREVIEW_STEP_TICKS, so it visibly moves yet every capture frame is
 * deterministic. The countdown runs while the local human is picking and is
 * 0 once it is done, as the link reports it.
 */
static void NativeArcadeLinkHost_PreviewSelectView(struct NativeArcadeLinkHostView *view)
{
	struct NativeArcadeLinkHostSelectView *sel = &view->select;
	struct NativeArcadeLinkHostSelectHumanView *local = &sel->humans[0];
	struct NativeArcadeLinkHostSelectHumanView *opponent = &sel->humans[1];
	uint32_t step = g_previewTicks / NATIVE_ARCADE_LINK_HOST_PREVIEW_STEP_TICKS;
	uint32_t racer;

	view->screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT;
	sel->active = 1u;
	sel->humanCount = 2u;
	sel->localHuman = 0u;
	sel->status = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING;
	sel->ticksLeft = NATIVE_ARCADE_LINK_HOST_PREVIEW_ITEM_TICKS - (g_previewTicks % NATIVE_ARCADE_LINK_HOST_PREVIEW_ITEM_TICKS);

	/* Both start on the fixture cursors: their own character, CRASH_COVE,
	 * and 3 laps. */
	local->present = 1u;
	local->characterID = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_CHARACTER;
	local->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_TRACK;
	local->lapCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS;
	opponent->present = 1u;
	opponent->characterID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_CHARACTER;
	opponent->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_TRACK;
	opponent->lapCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS;

	switch (g_options.preview)
	{
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER:
		local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER;
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER;
		opponent->characterID = NativeMatchSelect_CharacterAt(step % NATIVE_MATCH_SELECT_CHARACTER_COUNT);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK:
		local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK;
		local->lockMask = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER;
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK;
		opponent->lockMask = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER;
		opponent->trackID = NativeMatchSelect_TrackAt(step % NATIVE_MATCH_SELECT_TRACK_COUNT);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT:
		if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS)
		{
			local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS;
			local->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK);
		}
		else
		{
			local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
			local->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK |
				NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS);
			sel->status = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING;
			sel->ticksLeft = 0u;
		}
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_LAPS;
		opponent->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK);
		opponent->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK;
		opponent->lapCount = NativeMatchSelect_LapOptionAt(step % NATIVE_MATCH_SELECT_LAP_OPTION_COUNT);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT:
	default:
		/* Both done; the two track votes differ, so the track is a draw
		 * (here TIGER_TEMPLE), and the laps agree. The bots are the first
		 * retail 2P AI set holding neither CRASH nor CORTEX: set 0. */
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT;
		local->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
		local->lockMask = (uint8_t)(NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER | NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_TRACK |
			NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_LAPS);
		opponent->currentItem = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
		opponent->lockMask = local->lockMask;
		opponent->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK;
		sel->status = (uint8_t)NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED;
		sel->ticksLeft = 0u;
		sel->resolved = 1u;
		sel->trackID = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_TRACK;
		sel->trackDrawn = 1u;
		sel->lapCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_LAPS;
		sel->lapsDrawn = 0u;
		sel->characterReassignedMask = 0u;
		sel->humanCharacter[0] = NATIVE_ARCADE_LINK_HOST_PREVIEW_LOCAL_CHARACTER;
		sel->humanCharacter[1] = NATIVE_ARCADE_LINK_HOST_PREVIEW_OPPONENT_CHARACTER;
		sel->botCount = NATIVE_ARCADE_LINK_HOST_PREVIEW_BOT_COUNT;
		for (racer = 0u; racer < NATIVE_ARCADE_LINK_HOST_PREVIEW_BOT_COUNT; racer++)
		{
			sel->botCharacter[racer] = NativeMatchSelect_AiSetRacer(0u, racer);
		}
		break;
	}
	sel->currentItem = local->currentItem;
	if ((opponent->lockMask & NATIVE_ARCADE_LINK_HOST_SELECT_LOCK_CHARACTER) != 0u)
	{
		sel->peerLockedCharacterMask = (uint16_t)(1u << opponent->characterID);
	}
}

/* Synthesizes the view of one preview screen. *view is already zeroed. A
 * preview consumes no input, so localMenuEvent is always NONE. */
static void NativeArcadeLinkHost_PreviewView(struct NativeArcadeLinkHostView *view)
{
	view->ticksInScreen = g_previewTicks;
	view->localCab = 1u;
	view->localMenuEvent = (uint8_t)NATIVE_ARCADE_MENU_EVENT_NONE;
	switch (g_options.preview)
	{
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_CHARACTER:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_TRACK:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_LAPS:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_WAIT:
	case NATIVE_ARCADE_LINK_PREVIEW_SELECT_RESULT:
		NativeArcadeLinkHost_PreviewSelectView(view);
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_LOBBY_WAITING:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_LOBBY;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_WAITING;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_LOBBY_CONNECTING:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_LOBBY;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_CONNECTING;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_LOBBY_REJECTED:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_LOBBY;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_REJECTED;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_MATCH_FOUND:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_READY;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED:
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT:
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC:
	case NATIVE_ARCADE_LINK_PREVIEW_RESULTS_LINK_ERROR:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_RESULTS;
		if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_FINISHED)
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
		}
		else if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_PEER_TIMEOUT)
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT;
		}
		else if (g_options.preview == (uint32_t)NATIVE_ARCADE_LINK_PREVIEW_RESULTS_DESYNC)
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_DESYNC;
		}
		else
		{
			view->endReason = NATIVE_ARCADE_FLOW_END_LINK_ERROR;
		}
		view->selectedRow = NATIVE_ARCADE_FLOW_ROW_REMATCH;
		view->rowsEnabled = 1u;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_REMATCH_WAIT:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT;
		view->lobbyStatus = NATIVE_ARCADE_FLOW_LOBBY_CONNECTING;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_EXIT:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_EXIT;
		view->endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_EXIT_OPPONENT_LEFT:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_EXIT;
		view->endReason = NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT;
		break;
	case NATIVE_ARCADE_LINK_PREVIEW_TITLE:
	default:
		view->screen = NATIVE_ARCADE_FLOW_SCREEN_OFF;
		view->attract = 1u;
		break;
	}
}

/* LINK: the adapter's select view, field for field. */
static void NativeArcadeLinkHost_CopySelectView(const struct NativeArcadeNetplaySelectView *from,
	struct NativeArcadeLinkHostSelectView *to)
{
	uint32_t i;

	to->active = from->active;
	to->humanCount = from->humanCount;
	to->localHuman = from->localHuman;
	to->currentItem = from->currentItem;
	to->ticksLeft = from->ticksLeft;
	to->status = from->status;
	to->resolved = from->resolved;
	to->trackID = from->trackID;
	to->lapCount = from->lapCount;
	to->trackDrawn = from->trackDrawn;
	to->lapsDrawn = from->lapsDrawn;
	to->characterReassignedMask = from->characterReassignedMask;
	to->botCount = from->botCount;
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		to->humanCharacter[i] = from->humanCharacter[i];
	}
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS; i++)
	{
		to->botCharacter[i] = from->botCharacter[i];
	}
	to->peerLockedCharacterMask = from->peerLockedCharacterMask;
	to->reserved[0] = 0u;
	to->reserved[1] = 0u;
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		to->humans[i].present = from->humans[i].present;
		to->humans[i].characterID = from->humans[i].characterID;
		to->humans[i].trackID = from->humans[i].trackID;
		to->humans[i].lapCount = from->humans[i].lapCount;
		to->humans[i].lockMask = from->humans[i].lockMask;
		to->humans[i].currentItem = from->humans[i].currentItem;
		to->humans[i].reserved[0] = 0u;
		to->humans[i].reserved[1] = 0u;
	}
}

int NativeArcadeLinkHost_GetView(struct NativeArcadeLinkHostView *view)
{
	struct NativeArcadeNetplayView netplayView;

	if ((view == NULL) || (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_OFF))
	{
		return 0;
	}
	memset(view, 0, sizeof(*view));
	if (g_mode == NATIVE_ARCADE_LINK_HOST_MODE_PREVIEW)
	{
		NativeArcadeLinkHost_PreviewView(view);
		return 1;
	}
	if (!NativeArcadeNetplay_GetView(&g_netplay, &netplayView))
	{
		return 0;
	}
	view->screen = netplayView.screen;
	view->lobbyStatus = netplayView.lobbyStatus;
	view->endReason = netplayView.endReason;
	view->selectedRow = netplayView.selectedRow;
	view->ticksInScreen = (netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? g_idleTicks
																						 : netplayView.ticksInScreen;
	view->localCab = (uint8_t)((g_config.localRole == (uint8_t)NATIVE_ARCADE_LINK_HOST_ROLE_CAB2) ? 2u : 1u);
	view->rowsEnabled = (uint8_t)(((netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RESULTS) &&
									  (netplayView.menuArmed != 0u) &&
									  (netplayView.ticksInScreen > g_config.timings.resultsDwellTicks))
			? 1u
			: 0u);
	view->attract = (uint8_t)((netplayView.screen == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1u : 0u);
	view->localMenuEvent = netplayView.localMenuEvent;
	NativeArcadeLinkHost_CopySelectView(&netplayView.select, &view->select);
	view->solo = (uint8_t)(((netplayView.soloFlags & NATIVE_ARCADE_NETPLAY_VIEW_SOLO) != 0u) ? 1u : 0u);
	view->soloOffered = (uint8_t)(((netplayView.soloFlags & NATIVE_ARCADE_NETPLAY_VIEW_SOLO_OFFERED) != 0u) ? 1u : 0u);
	view->peerHeard = (uint8_t)(((netplayView.soloFlags & NATIVE_ARCADE_NETPLAY_VIEW_PEER_HEARD) != 0u) ? 1u : 0u);
	view->reserved = 0u;
	return 1;
}

int NativeArcadeLinkHost_GetAgreedMatch(struct NativeArcadeLinkHostMatch *out)
{
	const struct NativeMatchConfigV1 *agreed;
	uint32_t slot;

	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	agreed = NativeArcadeNetplay_AgreedConfig(&g_netplay);
	if (agreed == NULL)
	{
		return 0;
	}
	out->trackID = agreed->trackID;
	out->lapCount = agreed->lapCount;
	out->masterSeed = agreed->masterSeed;
	for (slot = 0u; slot < NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS; slot++)
	{
		out->slotRole[slot] = agreed->slots[slot].role;
		out->slotCharacter[slot] = agreed->slots[slot].characterID;
	}
	return 1;
}

int NativeArcadeLinkHost_GetAgreedConfig(struct NativeMatchConfigV1 *out)
{
	const struct NativeMatchConfigV1 *agreed;

	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	agreed = NativeArcadeNetplay_AgreedConfig(&g_netplay);
	if (agreed == NULL)
	{
		return 0;
	}
	/* The exact bytes the link agreed, padding included. */
	memcpy(out, agreed, sizeof(*out));
	return 1;
}

int NativeArcadeLinkHost_InternalCopyValidSoloConfig(const struct NativeMatchConfigV1 *candidate,
	struct NativeMatchConfigV1 *out)
{
	if ((candidate == NULL) || (out == NULL))
	{
		return 0;
	}
	/* SOLO-6: a hard check that fails closed. BuildConfig does not check the
	 * bots outside the 2P shape (risk 2), so every solo config is checked
	 * against the LOAD_Robots1P rule here before any caller sees it. */
	if ((candidate->profile != NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB) ||
		!NativeArcadeBotRules_ValidateConfigV1(candidate))
	{
		return 0;
	}
	memcpy(out, candidate, sizeof(*out));
	return 1;
}

int NativeArcadeLinkHost_GetSoloConfig(struct NativeMatchConfigV1 *out)
{
	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	return NativeArcadeLinkHost_InternalCopyValidSoloConfig(NativeArcadeNetplay_SoloConfig(&g_netplay), out);
}

int NativeArcadeLinkHost_ReportRaceFailure(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0;
	}
	return NativeArcadeNetplay_ReportLocalRaceFailure(&g_netplay);
}

int NativeArcadeLinkHost_TakeRaceEnd(struct NativeArcadeLinkHostRaceEnd *out)
{
	struct NativeArcadeNetplayRaceEnd raceEnd;

	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK))
	{
		return 0;
	}
	if (!NativeArcadeNetplay_TakeRaceEnd(&g_netplay, &raceEnd))
	{
		return 0;
	}
	out->raceNumber = raceEnd.raceNumber;
	out->endReason = raceEnd.endReason;
	out->foreignBundleDrops = raceEnd.foreignBundleDrops;
	return 1;
}

int NativeArcadeLinkHost_TakeRaceDivergence(struct NativeArcadeLinkHostRaceDivergence *out)
{
	if ((out == NULL) || (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK) || (g_raceDivergencePending == 0u))
	{
		return 0;
	}
	out->raceNumber = g_raceDivergence.raceNumber;
	out->raceTick = g_raceDivergence.raceTick;
	out->domainMask = g_raceDivergence.domainMask;
	out->reserved = 0u;
	out->localDigest = g_raceDivergence.localDigest;
	out->remoteDigest = g_raceDivergence.remoteDigest;
	g_raceDivergencePending = 0u;
	return 1;
}

uint8_t NativeArcadeLinkHost_Racing(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return 0u;
	}
	return (uint8_t)((NativeArcadeLinkHost_LinkScreen() == (uint32_t)NATIVE_ARCADE_FLOW_SCREEN_RACING) ? 1u : 0u);
}

void NativeArcadeLinkHost_AbortToTitle(void)
{
	if (g_mode != NATIVE_ARCADE_LINK_HOST_MODE_LINK)
	{
		return;
	}
	NativeArcadeNetplay_Shutdown(&g_netplay);
	/* The race's link is gone, so its drive and its divergence record go
	 * with it (the adapter restarts its race count). */
	NativeArcadeLinkHost_ResetDrive();
	NativeArcadeLinkHost_ResetDivergence();
	/* Init restarts the adapter's select count, so a new epoch keeps the
	 * next select's nonce from repeating an earlier one. */
	NativeArcadeLinkHost_NextEpoch(g_options.selectEntropy);
	if (!NativeArcadeNetplay_Init(&g_netplay, &g_config))
	{
		/* Defensive only: the stored config was accepted by Configure. */
		NativeArcadeLinkHost_Shutdown();
		return;
	}
	g_idleTicks = 0u;
}
