#ifndef PLATFORM_NATIVE_ARCADE_NETPLAY_H
#define PLATFORM_NATIVE_ARCADE_NETPLAY_H

#include <stdint.h>

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_launch.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_lobby_state.h"
#include "platform/native_lockstep_match_outcome.h"
#include "platform/native_lockstep_match_roster.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_match_select_session.h"

/*
 * Arcade-link host adapter (docs/GAME_LOOP_UI_MILESTONE.md section 2.3, with
 * the match-select phase of docs/MATCH_SELECT_MILESTONE.md section 2.6).
 *
 * Role: this is the only production module that composes the lobby layer
 * (native_lobby_state) with the failure-handling layer (match outcome,
 * match roster, rematch) and the match-select session
 * (native_match_select_session), and drives the pure arcade screen flow
 * (native_arcade_flow) with the arcade menu input seam. It is the single
 * composition point: engine-side code calls only the NativeArcadeNetplay_*
 * names below and never names a lobby, outcome, roster, rematch, or select
 * function itself, so the existing structural isolation rules on engine
 * sources stay exactly as strict as they are.
 *
 * Platform-only hooks: NativeArcadeNetplay_OnTakeResult and
 * NativeArcadeNetplay_Link carry lockstep types in their signatures (a
 * session result and a peer link). They are platform-side hooks for the
 * Task 8 race driver only, which lives under platform/. Game code must not
 * call them: naming their types there would fail
 * tests/native_lockstep_isolation_test.cmake. NativeArcadeNetplay_RaceService
 * is the third such hook, for the drive's hold (LR-9): it carries no
 * lockstep type, but it runs a slice of Tick behind the flow's back, so only
 * the race driver's host glue may call it (no game source calls any
 * NativeArcadeNetplay_* name today, and the arcade-link hook's files may not
 * name one; tests/main_arcade_link_hook_isolation_test.cmake).
 * NativeArcadeNetplay_Select
 * names no lockstep type, but it returns a select-session type, and game
 * code reads select state only through the host API. Every other name below
 * is safe for game code.
 *
 * Each tick it polls the lobby, maps the lobby mode onto the flow's lobby
 * status, re-arms the menu input on every screen entry (release-to-arm,
 * UX-3), drives the select session while selecting (below), runs the flow
 * once, and executes the host-side actions itself: BEGIN_LOBBY opens a lobby
 * on the current proposal, RESTART_LOBBY restarts its candidate cycle (or
 * closes and begins again when no lobby is open or the restart is refused),
 * CLOSE_LINK closes it, BEGIN_REMATCH closes it, builds the rematch config,
 * and opens a new lobby on that, BEGIN_SELECT starts the select session, and
 * RELINK builds the resolved config and opens a new lobby on it. START_RACE
 * and RETURN_TO_TITLE are only returned: the caller owns level loading and
 * the title screen, and this adapter never loads a level.
 *
 * Select phase (docs/MATCH_SELECT_MILESTONE.md sections 2.3-2.6): the flow
 * goes LOBBY or REMATCH_WAIT -> MATCH_FOUND -> SELECT -> SELECT_RESULT ->
 * RACING; MATCH_FOUND never starts a race.
 * - BEGIN_SELECT discards every datagram already waiting in the link's aux
 *   inbox. On a fresh link that mostly drops the peer's early, valid select
 *   records (the peer entered SELECT first), which is harmless because the
 *   peer resends its full state every tick; it also drops anything stale.
 *   It then counts the select (selectSerial), derives the
 *   nonce (NativeArcadeNetplay_DeriveSelectNonce with config.selectEntropy),
 *   and starts the session on the agreed lobby config (the base): humanCount
 *   is the base's number of human-role slots, the local human is localRole
 *   - 1, and the cursors start on this cabinet's own slot character, the
 *   base track, and the base lap count, each replaced by the first table
 *   entry when it is not a table value. On a rematch the base carries the
 *   previous picks, so each cursor starts on them (OD-3). If the session
 *   cannot start, the flow is told FAILED and shows LINK ERROR.
 * - Each tick on SELECT, and on SELECT_RESULT until RELINK has run, after
 *   the lobby poll and before the flow runs: every aux datagram is taken
 *   into the session (Accept), the menu event goes to the session on SELECT
 *   only (BACK is ignored there, SEL-7), the session ticks, and its status
 *   becomes the flow's select status (CONFIRMED, FAILED, else PENDING).
 *   After the flow's action has run, while still on SELECT or SELECT_RESULT
 *   before RELINK, one composed select record is sent on the aux route.
 *   Sending through the result hold is the linger: a peer still waiting for
 *   the confirming record receives it (section 2.3).
 * - RELINK closes the lobby and builds the resolved config
 *   (NativeMatchSelect_BuildConfig on the session's own base, read through
 *   NativeMatchSelectSession_Base: the config its exchanged base digest
 *   covers, and the session outcome).
 *   On success it becomes the current config and a new lobby is begun on
 *   it: the relink handshake re-checks the full config byte for byte. On
 *   failure nothing is begun and the relink is blocked like a failed
 *   rematch: RESTART_LOBBY begins nothing until Enter, Shutdown,
 *   RETURN_TO_TITLE, BEGIN_REMATCH, or BEGIN_SELECT clears the block, so
 *   the flow ends in LINK ERROR at its launch timeout and never races on
 *   the base config.
 * - START_RACE arms the race on the current config, which is then the
 *   resolved config, and takes it as lastReadyConfig (RL-6, below).
 * - Every pre-race LINK ERROR (from SELECT, or from SELECT_RESULT before or
 *   after RELINK) comes with CLOSE_LINK, so the lobby is closed on RESULTS:
 *   a relink handshake the peer completes later cannot reach READY here, and
 *   no launch record can arrive for an agreement (MS-8b, RL-3). The in-race
 *   path to RESULTS keeps the link open.
 *
 * Launch agreement (docs/RACE_LAUNCH_MILESTONE.md RL-1, RL-3, RL-4, RL-6):
 * handshake completion alone never starts a race.
 * - On the tick a relink lobby is first observed READY, the adapter
 *   discards whatever is waiting in the aux inbox (datagrams that arrived
 *   in the poll that found the new link RUNNING) and begins one launch
 *   agreement (native_arcade_launch) for its local role, on the SHA-256
 *   NativeMatchConfigV1_Digest of its relink proposal (the current config),
 *   with a linger cap of NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS. The
 *   first lobby and rematch lobbies run no agreement.
 * - Each tick after the lobby poll and before the flow runs, while the
 *   agreement is active, every aux datagram is taken into it (a late select
 *   record or any other magic is FOREIGN: ignored and counted). Records are
 *   accepted only while the flow is in SELECT_RESULT phase 2 or once the
 *   agreement has committed (then only HEARD is watched); otherwise they are
 *   discarded unread. The flow's launchStatus is COMMITTED exactly when the
 *   agreement has committed, so START_RACE needs READY and a commit.
 * - After the flow's action, one launch record is sent per tick on the aux
 *   route while the agreement wants to send and the link is RUNNING: from
 *   the relink READY tick, through the commit, and after it (RACING
 *   included) until a HEARD record was sent and one received, or
 *   NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS ticks after the commit. While
 *   the race drive holds on race tick 0 (its start wait), the hold's
 *   RaceService lifts that cap until the wait ends (LR-69).
 * - The agreement is reset on RELINK, RESTART_LOBBY, CLOSE_LINK,
 *   BEGIN_SELECT, BEGIN_REMATCH, RETURN_TO_TITLE, Enter, and Shutdown. The
 *   record carries no link epoch. Every reset leaves the agreement inactive
 *   until the next relink READY, and every relink link is a fresh one (Close
 *   empties the aux inbox and releases the old socket). The peer resets its
 *   agreement before it opens its new link and begins a new one only on the
 *   new READY, so with in-order delivery every record of its old agreement
 *   arrives before its new HELLO and is dropped by the HANDSHAKING link or
 *   goes to a closed socket: in-order delivery rules out stale records. The
 *   READY-tick discard is defence in depth against reordering within the
 *   completing poll only: it drops what that one Poll read (at most
 *   NATIVE_LOCKSTEP_PEER_LINK_POLL_BUDGET datagrams). The residual is a
 *   reordered or delayed stale record read after that poll
 *   (docs/RACE_LAUNCH_MILESTONE.md section 7, risk 1).
 *
 * The adapter itself reads the link every Tick, including during RACING:
 * the lobby poll drains arriving bundles into the session, and when that
 * poll finds the link FAULTED or DIVERGED (lobby PEER_LOST) while racing,
 * Tick reads the session's latched fault or divergence into the outcome
 * tracker and roster, so the result is DESYNC or LINK ERROR from the real
 * cause. The Task 8 race driver is therefore not the link's only reader.
 *
 * Stale bundles (docs/LOCKSTEP_RACE_MILESTONE.md LR-14): the peer link drops
 * and counts records of another match, such as a finished match's bundles
 * reaching a rematch link. The adapter adds up those counts over every link
 * it opens and, once per race, on the tick the flow moves RACING ->
 * RESULTS, latches an end-of-race record with the drops since the previous
 * race end (NativeArcadeNetplay_TakeRaceEnd) for the caller to log.
 *
 * Rematch rule (UX-7): agreement is implicit, not a new wire message. The
 * rematch derives from lastReadyConfig: the proposal of the most recent
 * first or rematch lobby that reached READY, or of the relink lobby whose
 * launch commit started a race (RL-6: a relink lobby's READY alone does not
 * take it). The handshake only completes on byte-identical proposals, so
 * both cabinets hold it byte-identically whatever happened after it. After
 * a finished race it is the resolved config; after a select failure, a
 * launch timeout, or a relink that completed on one side only, it is the
 * select base both held at MATCH_FOUND, even when one side relinked and the
 * other did not. Both
 * derive the same rematch masterSeed from it
 * (NativeArcadeNetplay_DeriveRematchSeed), build the same rematch config,
 * and re-run the ordinary handshake on it.
 * Two cabinets that both chose REMATCH therefore propose byte-identical
 * configs and reach READY; a cabinet that chose EXIT has closed its link, so
 * the other side's handshake goes unanswered until the flow's rematch wait
 * times out and it shows OPPONENT LEFT. The rematch config is the base of
 * the next select (OD-3), so the next race runs on that select's resolved
 * config and seed, not on the rematch seed itself. Every rematch opens a
 * brand-new peer link and session: nothing from a finished, diverged, or
 * faulted session is ever reused. If the rematch seed or config cannot be
 * built, or no lobby has reached READY since Enter (defensive only), the
 * adapter opens no lobby at all and refuses
 * every restart until the rematch wait times out to OPPONENT LEFT: it never
 * races again on the old seed.
 *
 * HELLO cadence (UX-5): the handshake HELLO is retransmitted every tick,
 * because the peer link requires Retransmit before Poll on every tick while
 * HANDSHAKING (include/platform/native_lockstep_peer_link.h,
 * NativeLockstepPeerLink_Retransmit). A sparser cadence lets the side that
 * completes first stop sending HELLO before the other side has seen one,
 * which then never completes (a staggered Enter or a rematch confirmed at
 * different ticks would hang one cabinet). 284 bytes 30 times a second is
 * negligible. Init accepts only 1 for retransmitIntervalTicks.
 *
 * Stall timeout (UX-9): the in-race stall timeout defaults to 90 ticks, 3 s
 * at the real 30 Hz game loop. The failure-handling layer's own default of
 * 180 frames was written as 3 s at 60 Hz and would be 6 s here, so the
 * adapter passes an explicit value inside that layer's frozen [30, 600]
 * range instead of changing its constants.
 *
 * Solo (docs/SOLO_CAB_MILESTONE.md SOLO-4 to SOLO-8, SOLO-11): with
 * config.soloEnabled 1 the flow's observation reports soloAvailable, so its
 * LOBBY offers solo when the peer is not heard; with 0 (the default) the
 * flow never offers it and nothing below runs.
 * - BEGIN_SOLO_SELECT from LOBBY closes the lobby (the handshake stops) and
 *   opens the listen-only link on the same local port
 *   (NativeLobbyState_BeginListen); from solo RESULTS (RACE AGAIN) the listen
 *   link stays, and is opened again only if it is not open. Either way a
 *   one-human select starts on config.soloBase (humanCount 1, localHuman 0
 *   on either seat), its cursors on the base's CAB1_HUMAN slot, track, and
 *   laps, or after a solo race on that race's picks (OD-3). The nonce is
 *   derived as for every select, so masterSeed is new each time.
 * - Each tick the listen-only link is drained and never answered; a
 *   well-formed handshake HELLO from a configured candidate latches
 *   peerHeard (the view's NATIVE_ARCADE_NETPLAY_VIEW_PEER_HEARD bit) until
 *   solo ends. Nothing is sent during solo: no HELLO, select record, or
 *   launch record.
 * - START_SOLO_RACE builds the solo race config with
 *   NativeMatchSelect_BuildConfig on the session's base and outcome (the
 *   SELECT_RESULT CPU list and the config agree), with no relink and no
 *   launch agreement. NativeArcadeNetplay_SoloConfig reads it; a build
 *   failure leaves it NULL, so no solo race can be armed.
 * - RETURN_TO_LOBBY (solo RESULTS row LOBBY) closes the listen-only link and
 *   begins a lobby on config.fixture: the handshake restarts. CLOSE_LINK
 *   (the solo RESULTS idle timeout) closes it too.
 * - Solo state is host-local: never sent, and never part of a replay,
 *   saved state, or canonical state beyond the ONE_CAB profile of the solo
 *   config itself.
 *
 * Caller-owned state, no heap use, no hidden state, no wall clock: every
 * duration is counted in caller ticks. A never-entered adapter opens nothing.
 */

/* Defaults, in 30 Hz game-loop ticks where they are durations. Frozen. */
#define NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY 2u
#define NATIVE_ARCADE_NETPLAY_DEFAULT_ATTEMPT_TICKS_PER_CANDIDATE 150u
/* Every tick: the peer-link Retransmit-before-Poll contract, UX-5. */
#define NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS 1u
#define NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS 90u   /* 3 s at the 30 Hz loop, UX-9 */
/* RL-4 launchLingerTicks: the launch-record linger cap after a commit, 10 s
 * at the 30 Hz loop. */
#define NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS 300u

struct NativeArcadeNetplayConfig
{
	/* The first match's proposal; both cabinets must build it
	 * byte-identically (UX-8). */
	struct NativeMatchConfigV1 fixture;
	struct NativeUdpTransportAddress candidates[NATIVE_LOBBY_STATE_MAX_CANDIDATES];
	uint32_t candidateCount;
	uint16_t localPort;
	/* NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN or _CAB2_HUMAN */
	uint8_t localRole;
	uint8_t reserved;
	uint32_t inputDelay;
	uint32_t attemptTicksPerCandidate;
	/* Must be 1 (the default); Init rejects any other value. Any other
	 * cadence violates the peer-link Retransmit-before-Poll contract and can
	 * hang a handshake (UX-5). Tests of the lobby layer's own cadence go
	 * through native_lobby_state directly, not through this adapter. */
	uint32_t retransmitIntervalTicks;
	uint32_t stallTimeoutTicks;
	struct NativeArcadeFlowTimings timings;
	/* Per-item countdowns and peer silence of every select; each field must
	 * be at least 1. */
	struct NativeMatchSelectTimings selectTimings;
	/* Host-local entropy for the select nonces (never parsed from argv; 0 in
	 * tests and previews). It reaches the match only through the exchanged
	 * nonces and so the agreed masterSeed. */
	uint64_t selectEntropy;
	/* Solo (SOLO-6): the ONE_CAB base of every solo select (the caller builds
	 * it beside the fixture). Checked only when soloEnabled is 1. */
	struct NativeMatchConfigV1 soloBase;
	/* Solo (SOLO-11): 0 (the default) or 1; feeds the flow's soloAvailable. */
	uint8_t soloEnabled;
};

/* The select view's per-human and bot capacities. */
#define NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS 4u
#define NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS 8u

_Static_assert(NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS == NATIVE_MATCH_SELECT_MAX_HUMANS,
	"the select view holds exactly the select session's humans");
_Static_assert(NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,
	"the select view holds exactly the outcome's bot characters");

/* One human in the select view: the local human's own state, or a peer's
 * last accepted record. Everything is 0 while present is 0. */
struct NativeArcadeNetplaySelectHumanView
{
	/* 1 for the local human always; 1 for a peer once a record was heard */
	uint8_t present;
	/* cursor or locked value */
	uint8_t characterID;
	/* cursor or locked value (the vote) */
	uint8_t trackID;
	/* cursor or locked value (the vote) */
	uint8_t lapCount;
	/* NATIVE_MATCH_SELECT_LOCK_* bits */
	uint8_t lockMask;
	/* enum NativeMatchSelectItem: 0 character, 1 track, 2 laps, 3 done */
	uint8_t currentItem;
	uint8_t reserved[2];
};

/* The select phase, flat. Everything is 0 unless active. */
struct NativeArcadeNetplaySelectView
{
	/* 1 on SELECT and SELECT_RESULT while a select session exists
	 * (NativeArcadeNetplay_Select is non-NULL) */
	uint8_t active;
	uint8_t humanCount;
	/* the local human's index (cabinet role - 1) */
	uint8_t localHuman;
	/* the local human's current item (enum NativeMatchSelectItem) */
	uint8_t currentItem;
	/* ticks until the local current item auto-locks; 0 once the local
	 * human is DONE */
	uint32_t ticksLeft;
	/* enum NativeMatchSelectStatus */
	uint8_t status;
	/* 1 when the outcome fields below are valid (RESOLVED or CONFIRMED) */
	uint8_t resolved;
	uint8_t trackID;
	uint8_t lapCount;
	uint8_t trackDrawn;
	uint8_t lapsDrawn;
	/* bit h: human h was reassigned a character */
	uint8_t characterReassignedMask;
	uint8_t botCount;
	/* the first humanCount used, the rest 0 */
	uint8_t humanCharacter[NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS];
	/* the first botCount used, the rest 0 */
	uint8_t botCharacter[NATIVE_ARCADE_NETPLAY_VIEW_MAX_BOTS];
	/* bit c: a peer has locked base character c (greyed on the character
	 * screen; the session refuses CONFIRM on it) */
	uint16_t peerLockedCharacterMask;
	uint8_t reserved[2];
	/* indexed by human; entries at or above humanCount stay 0 */
	struct NativeArcadeNetplaySelectHumanView humans[NATIVE_ARCADE_NETPLAY_VIEW_MAX_HUMANS];
};

/* Everything a screen drawer needs, in the flow's own enums. */
struct NativeArcadeNetplayView
{
	/* enum NativeArcadeFlowScreen */
	uint32_t screen;
	/* enum NativeArcadeFlowLobbyStatus */
	uint32_t lobbyStatus;
	/* enum NativeArcadeFlowEndReason */
	uint32_t endReason;
	uint32_t selectedRow;
	uint32_t ticksInScreen;
	uint32_t matchCount;
	uint8_t localRole;
	uint8_t menuArmed;
	/* enum NativeArcadeMenuEvent: the event produced from the local buttons
	 * on the last Tick, whether or not the screen acted on it; consumers must
	 * gate on a state change (lastMenuEvent below). NONE before the first
	 * Tick, on every Tick that starts on screen OFF, and on every tick
	 * without a new edge. Local input only: a peer's input never appears
	 * here. Presentation only (menu sounds). */
	uint8_t localMenuEvent;
	/* NATIVE_ARCADE_NETPLAY_VIEW_SOLO* bits (docs/SOLO_CAB_MILESTONE.md); 0
	 * outside solo and the solo offer. */
	uint8_t soloFlags;
	/* The select phase (docs/MATCH_SELECT_MILESTONE.md section 2.7). */
	struct NativeArcadeNetplaySelectView select;
};

/* The view's soloFlags bits: solo mode (solo SELECT, SELECT_RESULT, RACING,
 * or RESULTS); the LOBBY solo offer standing; a configured peer heard on the
 * listen-only link during this solo. */
#define NATIVE_ARCADE_NETPLAY_VIEW_SOLO 0x1u
#define NATIVE_ARCADE_NETPLAY_VIEW_SOLO_OFFERED 0x2u
#define NATIVE_ARCADE_NETPLAY_VIEW_PEER_HEARD 0x4u

/*
 * The end of one race, for the end-of-race log line
 * (docs/LOCKSTEP_RACE_MILESTONE.md LR-14, LR-S6). Host-local: never sent,
 * and never part of the match config, seeds, replay, saved states, or
 * canonical state.
 */
struct NativeArcadeNetplayRaceEnd
{
	/* matchCount when the race ended: 1 for the first race since Init */
	uint32_t raceNumber;
	/* enum NativeArcadeFlowEndReason the race ended with */
	uint32_t endReason;
	/* Records of another match (foreign identity) the peer links of this
	 * adapter dropped since the previous race end, or since Init for the
	 * first: every link opened in between is counted, the rematch and relink
	 * links included, not only the race's own. */
	uint32_t foreignBundleDrops;
};

struct NativeArcadeNetplay
{
	struct NativeArcadeNetplayConfig config;
	/* The proposal of the current (or next) link: the fixture or the latest
	 * rematch config (the select base), and after a successful RELINK the
	 * resolved config. */
	struct NativeMatchConfigV1 currentConfig;
	struct NativeArcadeFlow flow;
	struct NativeArcadeMenuInput menuInput;
	struct NativeLobbyState lobby;
	struct NativeLockstepMatchOutcomeTracker outcome;
	struct NativeLockstepMatchRoster roster;
	uint32_t lastScreenSerial;
	/* enum NativeArcadeFlowEndReason: NONE, PEER_TIMEOUT, DESYNC, LINK_ERROR */
	uint32_t pendingLinkFailure;
	uint32_t matchCount;
	uint8_t localSlot;
	uint8_t lobbyBegun;
	uint8_t raceArmed;
	uint8_t initialized;
	/* Set when a rematch config could not be built: no lobby is begun until
	 * Enter, Shutdown, or RETURN_TO_TITLE clears it. */
	uint8_t rematchBlocked;
	/* 1 once BEGIN_SELECT has started the select session successfully (the
	 * session is valid); 0 when it could not start, which reads FAILED. */
	uint8_t selectActive;
	/* 1 once RELINK has run for the current select: the session is no
	 * longer driven and nothing more is sent. */
	uint8_t relinked;
	/* Set when RELINK could not build the resolved config: no lobby is
	 * begun until Enter, Shutdown, RETURN_TO_TITLE, BEGIN_REMATCH, or
	 * BEGIN_SELECT clears it. */
	uint8_t relinkBlocked;
	/* Selects begun by this adapter since Init (the nonce input). Only Init
	 * resets it, but the host's NativeArcadeLinkHost_AbortToTitle re-runs
	 * Init after every START_RACE, so on a cabinet it restarts from 0 there;
	 * the nonces keep varying across those aborts through
	 * config.selectEntropy (MS-8). */
	uint32_t selectSerial;
	/* 1 once RELINK built a config from lastOutcome. */
	uint8_t outcomeValid;
	/* 1 once START_RACE armed a race on currentConfig; cleared by
	 * BEGIN_SELECT, BEGIN_REMATCH, Enter, RETURN_TO_TITLE, and Shutdown.
	 * AgreedConfig reads it on RESULTS. */
	uint8_t raceConfigValid;
	/* 1 once the open lobby has been observed READY (and, for a first or
	 * rematch lobby, lastReadyConfig taken from it; for a relink lobby, the
	 * launch agreement begun); cleared whenever a lobby is begun, restarted,
	 * or closed. */
	uint8_t lobbyReadySeen;
	/* 1 once lastReadyConfig holds a READY proposal; cleared by Enter and
	 * Shutdown. */
	uint8_t lastReadyValid;
	/* enum NativeArcadeMenuEvent: the event step 4 of the most recent Tick
	 * produced and fed to the select session and the flow. Every Tick on an
	 * initialized adapter sets it to NONE first (the dormant screen-OFF path
	 * included), so it never outlives its tick; Init, a successful Enter,
	 * and Shutdown clear it. Presentation only: it feeds nothing, is never sent, and
	 * stays out of the match config, seeds, and simulation identity. */
	uint8_t lastMenuEvent;
	/* 1 once NativeArcadeNetplay_ReportLocalRaceFailure accepted a local race
	 * failure on RACING (docs/RACE_LAUNCH_MILESTONE.md RL-11). Separate from
	 * pendingLinkFailure, whose meaning it does not change: the next Tick on
	 * RACING consumes it. Init, Enter, BEGIN_SELECT, RELINK, START_RACE,
	 * CLOSE_LINK, BEGIN_REMATCH, RETURN_TO_TITLE, and Shutdown clear it.
	 * Local only: never sent, and never part of the match config, seeds, or
	 * simulation identity. */
	uint8_t localRaceFailure;
	/* The outcome the last successful RELINK built its config from. */
	struct NativeMatchSelectOutcome lastOutcome;
	/* The proposal of the most recent first or rematch lobby that reached
	 * READY, taken on the tick it is first observed READY after a Begin or
	 * restart, or of the relink lobby a race started on, taken on that
	 * START_RACE (RL-6). The handshake guarantees the peer holds it
	 * byte-identically, so BEGIN_REMATCH derives from it (the rematch rule
	 * above). */
	struct NativeMatchConfigV1 lastReadyConfig;
	struct NativeMatchSelectSession select;
	/* The launch agreement of the current relink lobby (the launch agreement
	 * block above): inactive (all zero) except from the first READY of a
	 * relink lobby until the next reset point. Never sent whole, never part
	 * of the match config, seeds, or simulation identity. */
	struct NativeArcadeLaunchAgreement launch;
	/* Foreign-identity drop accounting (docs/LOCKSTEP_RACE_MILESTONE.md
	 * LR-14): the open link's NativeLockstepPeerLink_DroppedForeignBundleCount
	 * as last read (after every lobby poll, and before every close or restart
	 * of the lobby), and the drops read since the previous race end. A read
	 * below the last one is a new link, whose whole count is new. Host-local,
	 * like the race-end record below. Init and each race end reset the
	 * running count; Close and restart never do. */
	uint32_t linkForeignDropsSeen;
	uint32_t foreignDropsSinceRaceEnd;
	/* 1 from the Tick whose flow moved RACING -> RESULTS until
	 * NativeArcadeNetplay_TakeRaceEnd takes raceEnd; Init and Shutdown clear
	 * it. */
	uint8_t raceEndPending;
	struct NativeArcadeNetplayRaceEnd raceEnd;
	/* Solo (docs/SOLO_CAB_MILESTONE.md SOLO-4, SOLO-6). listening: 1 while
	 * the lobby holds the listen-only link. peerHeard: 1 once that link heard
	 * a configured peer; cleared whenever the link closes. soloConfigValid: 1
	 * once START_SOLO_RACE built soloConfig (the next solo select's cursors);
	 * cleared when solo begins from LOBBY, on RETURN_TO_LOBBY, Enter,
	 * RETURN_TO_TITLE, and Shutdown. soloRaceArmed: 1 from a START_SOLO_RACE
	 * that built soloConfig until the next BEGIN_SOLO_SELECT, RETURN_TO_LOBBY,
	 * Enter, RETURN_TO_TITLE, or Shutdown. Host-local, like everything
	 * above. */
	uint8_t listening;
	uint8_t peerHeard;
	uint8_t soloConfigValid;
	uint8_t soloRaceArmed;
	struct NativeMatchConfigV1 soloConfig;
};

/* memset 0, then the four NATIVE_ARCADE_NETPLAY_DEFAULT_* values, the flow's
 * default timings, the select session's default timings
 * (NativeMatchSelectSession_DefaultTimings), selectEntropy 0, and localRole
 * CAB1_HUMAN. fixture, candidates, candidateCount, and localPort stay zero
 * for the caller to fill. NULL is a no-op. */
void NativeArcadeNetplay_DefaultConfig(struct NativeArcadeNetplayConfig *config);

/* Validates and stores the config and leaves the adapter dormant on screen
 * OFF. Opens no link. Returns 1 on success; returns 0 with *netplay untouched
 * on a NULL argument, an invalid fixture, a local role that is not CAB1_HUMAN
 * or CAB2_HUMAN or is absent from the fixture, a zero local port, zero or
 * more than NATIVE_LOBBY_STATE_MAX_CANDIDATES candidates, a zero attempt
 * budget, a retransmit interval other than 1, an input delay or stall timeout
 * outside the ranges the lower layers accept, timings the flow rejects,
 * select timings with any zero field, a soloEnabled above 1, or, with
 * soloEnabled 1, a soloBase that fails NativeMatchConfigV1_Validate, is not
 * ARCADE_ONE_CAB, or has no CAB1_HUMAN slot.
 * Must not be called on an adapter with an open lobby (it would be
 * overwritten without being closed): call Shutdown first. */
int NativeArcadeNetplay_Init(struct NativeArcadeNetplay *netplay, const struct NativeArcadeNetplayConfig *config);

/* From screen OFF only: enters LOBBY on the fixture and opens the lobby.
 * Returns BEGIN_LOBBY then, else NONE (also for NULL or an uninitialized
 * adapter). */
enum NativeArcadeFlowAction NativeArcadeNetplay_Enter(struct NativeArcadeNetplay *netplay);

/* One game-loop tick. heldMenuButtons uses the NATIVE_ARCADE_MENU_BUTTON_*
 * bits; raceFinished is nonzero once the local race has finished. For NULL
 * or an uninitialized adapter returns NONE and touches nothing. On screen OFF
 * returns NONE and changes nothing but lastMenuEvent, which reads NONE.
 * Otherwise returns the flow's action after executing its host-side part;
 * START_RACE and RETURN_TO_TITLE are the caller's cue. */
enum NativeArcadeFlowAction NativeArcadeNetplay_Tick(struct NativeArcadeNetplay *netplay, uint32_t heldMenuButtons,
	uint8_t raceFinished);

/* The local race-failure input (docs/RACE_LAUNCH_MILESTONE.md RL-11): the
 * race caller could not set up or run the local race. On RACING only:
 * latches it and returns 1 (again 1, with no further effect, if already
 * latched). The next Tick then observes linkFailure LINK_ERROR, so the flow
 * moves to RESULTS with end reason LINK_ERROR (outranking a same-tick
 * raceFinished, UX-6), and consumes the latch; a link failure already
 * pending (pendingLinkFailure) is observed instead, and still consumes it.
 * Otherwise (NULL, uninitialized, or any other screen) returns 0 and
 * changes nothing. The peer is not told: nothing is sent, the link is not
 * closed, and the link and any launch linger run on exactly as after a
 * finished race. */
int NativeArcadeNetplay_ReportLocalRaceFailure(struct NativeArcadeNetplay *netplay);

/* Platform-side race-time hook for the Task 8 race driver only (it carries
 * a lockstep type; game code must not call it, see the block comment above):
 * feed each frame's take result and the attempted frame index. A no-op
 * unless the adapter is racing with no failure pending. A latched outcome
 * drops the remote human in the roster and becomes the flow's link-failure
 * reason on the next Tick. Tick also latches a fault or divergence it finds
 * itself, so this hook is not the only path to DESYNC or LINK ERROR. */
void NativeArcadeNetplay_OnTakeResult(struct NativeArcadeNetplay *netplay, enum NativeLockstepSessionResult result,
	uint32_t frameIndex);

/*
 * Platform-side hook for the race drive's poll and servicePeriod callbacks
 * only: the host glue's Step and hold (the "Platform-only hooks" paragraph
 * above; docs/LOCKSTEP_RACE_MILESTONE.md LR-9, LR-50). While the drive
 * holds, the adapter's Tick does not run, yet the link must still be drained
 * and the launch linger kept alive. On every call this polls the link
 * through the lobby exactly as Tick's step 2 does (NativeLobbyState_Poll
 * while a lobby is open, then the foreign-drop read of LR-35, so the drop
 * tally stays exact). When launchPeriod is nonzero, which the caller passes
 * once per held tick period, it then also runs Tick's launch intake and its
 * launch send and linger tick, once each, so the launch linger counts one
 * tick per held period. So a new-period hold iteration polls the lobby
 * twice (the poll callback, then servicePeriod), which is harmless: the
 * second poll only drains what arrived in between, and the drop read counts
 * each drop once. It does nothing else: no flow tick, menu input,
 * lobby-status mapping, select drive, outcome latch, race-end record, or
 * action. A no-op for NULL, an uninitialized adapter, and any screen other
 * than RACING (OFF included), the only screen the drive steps and holds on.
 *
 * The start wait (docs/LOCKSTEP_RACE_MILESTONE.md LR-69): launchPeriod
 * NATIVE_ARCADE_NETPLAY_RACE_SERVICE_START_WAIT, which the caller passes for
 * a held period of race tick 0 only, sends the period's launch record by the
 * linger's rule without its NATIVE_ARCADE_NETPLAY_LAUNCH_LINGER_TICKS cap
 * (NativeArcadeLaunch_ShouldSendUncapped): a peer that has not committed yet
 * needs those records to launch at all (RL-7), and the start wait can hold
 * for 810 + 90 periods, longer than the cap. HEARD (a HEARD record sent and
 * one received) still stops them, and the start wait itself bounds them: it
 * ends at GO, or at its timeout, after which the drive ends and calls
 * nothing. Every other nonzero launchPeriod, and Tick, keep the capped rule.
 */
#define NATIVE_ARCADE_NETPLAY_RACE_SERVICE_START_WAIT 2
void NativeArcadeNetplay_RaceService(struct NativeArcadeNetplay *netplay, int launchPeriod);

/* The end-of-race record (docs/LOCKSTEP_RACE_MILESTONE.md LR-14, LR-S6),
 * once per race: on the Tick whose flow moves RACING -> RESULTS (the flow's
 * first RESULTS frame of that race, whatever the end reason) the adapter
 * latches the race number, the end reason, and the foreign-identity records
 * its peer links dropped since the previous race end, and restarts that
 * count. This call then copies the record into *out, clears the latch, and
 * returns 1; it returns 0 with *out untouched when nothing is latched, and
 * for NULL or an uninitialized adapter. A record not taken before the next
 * race end is replaced by it. The caller logs it; the adapter itself never
 * logs. */
int NativeArcadeNetplay_TakeRaceEnd(struct NativeArcadeNetplay *netplay, struct NativeArcadeNetplayRaceEnd *out);

/* Fills *view and returns 1; returns 0 on a NULL argument. The select
 * sub-view is filled from the select session while NativeArcadeNetplay_Select
 * is non-NULL (SELECT and SELECT_RESULT, before and after RELINK), and is all
 * zero otherwise. */
int NativeArcadeNetplay_GetView(const struct NativeArcadeNetplay *netplay, struct NativeArcadeNetplayView *view);

/* The config of the match running or just finished: non-NULL only on
 * RACING, and on RESULTS when START_RACE armed a race on it. This proposal
 * is the agreement: the handshake only completes when both proposals are
 * byte-identical (docs/LOBBY_MILESTONE.md section 2.2), and START_RACE only
 * follows READY of the relink plus a launch commit, a record from the peer
 * carrying the digest of this same proposal, sent only while the peer's
 * relink link was RUNNING (RL-1, RL-3), so the peer holds exactly this
 * config. On
 * RACING, and on RESULTS after that race (finished or failed), it is the
 * resolved select config the relink handshake validated. On RESULTS reached
 * without a race (a select failure, a relink that could not be built, or a
 * launch timeout) it is NULL: no race config was agreed. */
const struct NativeMatchConfigV1 *NativeArcadeNetplay_AgreedConfig(const struct NativeArcadeNetplay *netplay);
/* (Solo has no agreement: AgreedConfig is NULL on every solo screen.) */

/* The solo race config (SOLO-6): non-NULL only in solo mode on RACING, and on
 * RESULTS after that race, when START_SOLO_RACE built it
 * (NativeMatchSelect_BuildConfig on the one-human select's base and
 * outcome). It is not checked against the bot rules here: the caller must
 * run NativeArcadeBotRules_ValidateConfigV1 on it and fail closed. */
const struct NativeMatchConfigV1 *NativeArcadeNetplay_SoloConfig(const struct NativeArcadeNetplay *netplay);

/* The select session, for a view (read-only): non-NULL only while the flow
 * is on SELECT or SELECT_RESULT and the session started. */
const struct NativeMatchSelectSession *NativeArcadeNetplay_Select(const struct NativeArcadeNetplay *netplay);

/* Platform-side hook for the Task 8 race driver only (it carries a lockstep
 * type; game code must not call it, see the block comment above): the open
 * peer link, for the race-time drive; NULL when no lobby is open. */
struct NativeLockstepPeerLink *NativeArcadeNetplay_Link(struct NativeArcadeNetplay *netplay);

/* Maps a match outcome cause: STALL_TIMEOUT to END_PEER_TIMEOUT, DIVERGED
 * to END_DESYNC, FAULTED to END_LINK_ERROR, anything else to END_NONE. */
uint32_t NativeArcadeNetplay_EndReasonForCause(uint32_t outcomeCause);

/* Pure and deterministic (UX-7): the first little-endian 8-byte word of the
 * SHA-256 digest of *previous that is nonzero and differs from
 * previous->masterSeed, trying bytes [0, 8), [8, 16), [16, 24), [24, 32) in
 * order. Returns 1 with *seedOut set; returns 0 with *seedOut untouched on a
 * NULL argument, a digest failure, or no qualifying word. */
int NativeArcadeNetplay_DeriveRematchSeed(const struct NativeMatchConfigV1 *previous, uint64_t *seedOut);

/* Pure and deterministic (docs/MATCH_SELECT_MILESTONE.md section 2.6): the
 * select nonce is the first little-endian 8-byte word of
 * SHA-256("CTRN match select nonce v1" (26 ASCII bytes, no NUL) || entropy
 * (8 bytes LE) || localRole (1 byte) || selectSerial (4 bytes LE)). Any
 * value of each input is hashed as given. Returns 1 with *nonceOut set;
 * returns 0 with nothing written for a NULL nonceOut. */
int NativeArcadeNetplay_DeriveSelectNonce(uint64_t entropy, uint8_t localRole, uint32_t selectSerial, uint64_t *nonceOut);

/* Closes any open lobby and returns the flow to OFF. Safe on NULL, on a
 * zero-initialized struct (which it leaves closed without touching the
 * lobby), and when called twice. */
void NativeArcadeNetplay_Shutdown(struct NativeArcadeNetplay *netplay);

#endif
