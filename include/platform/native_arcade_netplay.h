#ifndef PLATFORM_NATIVE_ARCADE_NETPLAY_H
#define PLATFORM_NATIVE_ARCADE_NETPLAY_H

#include <stdint.h>

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_lobby_state.h"
#include "platform/native_lockstep_match_outcome.h"
#include "platform/native_lockstep_match_roster.h"
#include "platform/native_match_config.h"

/*
 * Arcade-link host adapter (docs/GAME_LOOP_UI_MILESTONE.md section 2.3).
 *
 * Role: this is the only production module that composes the lobby layer
 * (native_lobby_state) with the failure-handling layer (match outcome,
 * match roster, rematch) and drives the pure arcade screen flow
 * (native_arcade_flow) with the arcade menu input seam. It is the single
 * composition point: engine-side code calls only the NativeArcadeNetplay_*
 * names below and never names a lobby, outcome, roster, or rematch function
 * itself, so the existing structural isolation rules on engine sources stay
 * exactly as strict as they are.
 *
 * Each tick it polls the lobby, maps the lobby mode onto the flow's lobby
 * status, re-arms the menu input on every screen entry (release-to-arm,
 * UX-3), runs the flow once, and executes the host-side actions itself:
 * BEGIN_LOBBY opens a lobby on the current proposal, RESTART_LOBBY restarts
 * its candidate cycle, CLOSE_LINK closes it, and BEGIN_REMATCH closes it,
 * builds the rematch config, and opens a new lobby on that. START_RACE and
 * RETURN_TO_TITLE are only returned: the caller owns level loading and the
 * title screen, and this adapter never loads a level.
 *
 * Rematch rule (UX-7): agreement is implicit, not a new wire message. Both
 * cabinets hold the same agreed config byte-identically, so both derive the
 * same rematch masterSeed from it (NativeArcadeNetplay_DeriveRematchSeed),
 * build the same rematch config, and re-run the ordinary handshake on it.
 * Two cabinets that both chose REMATCH therefore propose byte-identical
 * configs and reach READY; a cabinet that chose EXIT has closed its link, so
 * the other side's handshake goes unanswered until the flow's rematch wait
 * times out and it shows OPPONENT LEFT. Every rematch opens a brand-new peer
 * link and session: nothing from a finished, diverged, or faulted session is
 * ever reused.
 *
 * Stall timeout (UX-9): the in-race stall timeout defaults to 90 ticks, 3 s
 * at the real 30 Hz game loop. The failure-handling layer's own default of
 * 180 frames was written as 3 s at 60 Hz and would be 6 s here, so the
 * adapter passes an explicit value inside that layer's frozen [30, 600]
 * range instead of changing its constants.
 *
 * Caller-owned state, no heap use, no hidden state, no wall clock: every
 * duration is counted in caller ticks. A never-entered adapter opens nothing.
 */

/* Defaults, in 30 Hz game-loop ticks where they are durations. Frozen. */
#define NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY 2u
#define NATIVE_ARCADE_NETPLAY_DEFAULT_ATTEMPT_TICKS_PER_CANDIDATE 150u
#define NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS 15u
#define NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS 90u   /* 3 s at the 30 Hz loop, UX-9 */

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
	uint32_t retransmitIntervalTicks;
	uint32_t stallTimeoutTicks;
	struct NativeArcadeFlowTimings timings;
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
	uint8_t reserved[2];
};

struct NativeArcadeNetplay
{
	struct NativeArcadeNetplayConfig config;
	/* The proposal of the current (or next) match: the fixture, or the
	 * latest rematch config. */
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
};

/* memset 0, then the four NATIVE_ARCADE_NETPLAY_DEFAULT_* values, the flow's
 * default timings, and localRole CAB1_HUMAN. fixture, candidates,
 * candidateCount, and localPort stay zero for the caller to fill. NULL is a
 * no-op. */
void NativeArcadeNetplay_DefaultConfig(struct NativeArcadeNetplayConfig *config);

/* Validates and stores the config and leaves the adapter dormant on screen
 * OFF. Opens no link. Returns 1 on success; returns 0 with *netplay untouched
 * on a NULL argument, an invalid fixture, a local role that is not CAB1_HUMAN
 * or CAB2_HUMAN or is absent from the fixture, more than
 * NATIVE_LOBBY_STATE_MAX_CANDIDATES candidates, a zero attempt budget, an
 * input delay or stall timeout outside the ranges the lower layers accept,
 * or timings the flow rejects. */
int NativeArcadeNetplay_Init(struct NativeArcadeNetplay *netplay, const struct NativeArcadeNetplayConfig *config);

/* From screen OFF only: enters LOBBY on the fixture and opens the lobby.
 * Returns BEGIN_LOBBY then, else NONE (also for NULL or an uninitialized
 * adapter). */
enum NativeArcadeFlowAction NativeArcadeNetplay_Enter(struct NativeArcadeNetplay *netplay);

/* One game-loop tick. heldMenuButtons uses the NATIVE_ARCADE_MENU_BUTTON_*
 * bits; raceFinished is nonzero once the local race has finished. On screen
 * OFF (or for NULL or an uninitialized adapter) returns NONE and touches
 * nothing. Otherwise returns the flow's action after executing its host-side
 * part; START_RACE and RETURN_TO_TITLE are the caller's cue. */
enum NativeArcadeFlowAction NativeArcadeNetplay_Tick(struct NativeArcadeNetplay *netplay, uint32_t heldMenuButtons,
	uint8_t raceFinished);

/* Race-time hook: feed each frame's take result and the attempted frame
 * index. A no-op unless the adapter is racing with no failure pending. A
 * latched outcome drops the remote human in the roster and becomes the
 * flow's link-failure reason on the next Tick. */
void NativeArcadeNetplay_OnTakeResult(struct NativeArcadeNetplay *netplay, enum NativeLockstepSessionResult result,
	uint32_t frameIndex);

/* Fills *view and returns 1; returns 0 on a NULL argument. */
int NativeArcadeNetplay_GetView(const struct NativeArcadeNetplay *netplay, struct NativeArcadeNetplayView *view);

/* The config of the match found, running, or just finished: non-NULL only
 * on MATCH_FOUND, RACING, and RESULTS. This proposal is the agreement: the
 * handshake only completes when both proposals are byte-identical
 * (docs/LOBBY_MILESTONE.md section 2.2), so reaching READY proves the peer
 * holds exactly this config. */
const struct NativeMatchConfigV1 *NativeArcadeNetplay_AgreedConfig(const struct NativeArcadeNetplay *netplay);

/* The open peer link, for the race-time drive; NULL when no lobby is open. */
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

/* Closes any open lobby and returns the flow to OFF. Safe on NULL, on a
 * zero-initialized struct, and when called twice. */
void NativeArcadeNetplay_Shutdown(struct NativeArcadeNetplay *netplay);

#endif
