#ifndef PLATFORM_NATIVE_LOBBY_STATE_H
#define PLATFORM_NATIVE_LOBBY_STATE_H

#include "platform/native_lockstep_peer_link.h"

#include <stdint.h>

/*
 * Lobby / waiting-flow state layer (docs/LOBBY_MILESTONE.md section 2.4): a
 * data/state-layer-only policy wrapper over NativeLockstepPeerLink adding a
 * caller-supplied candidate peer address list and a frame-counted (never
 * wall-clock) per-candidate attempt budget, exposing a small state enum an
 * eventual waiting/lobby UI can read. No menu, no wheel input, no rendering,
 * and nothing under game/ references this module -- explicitly scoped down,
 * mirroring how the failure-handling milestone left its policy layer with no
 * game-loop caller.
 *
 * Caller-owned, no dynamic allocation. Peer discovery here means cycling
 * through a caller-supplied candidate address list, not OS-level
 * broadcast/multicast discovery (docs/LOBBY_MILESTONE.md section 2.4). The
 * candidate list itself is copied by value into a small fixed-size array
 * member at NativeLobbyState_Begin time (capped at
 * NATIVE_LOBBY_STATE_MAX_CANDIDATES) rather than referenced through a
 * caller-owned pointer, so this struct never depends on the caller's own
 * array outliving the call, and never needs a variable-length member.
 */

/* Small, fixed compile-time cap: this fleet is a two-cabinet installation
 * with a handful of known static addresses (docs/LOBBY_MILESTONE.md section
 * 2.4), not an open-ended discovery list. */
#define NATIVE_LOBBY_STATE_MAX_CANDIDATES 8u

/*
 * WAITING_FOR_PEER: no candidate is currently being attempted -- either the
 * candidate list is empty, or every candidate in this pass has exhausted its
 * attempt budget with no answer and the caller has not yet asked to restart
 * the cycle (NativeLobbyState_RestartCycle).
 * HANDSHAKING: the underlying NativeLockstepPeerLink mode is HANDSHAKING for
 * the current candidate.
 * READY: the underlying peer link reached RUNNING.
 * REJECTED: the underlying peer link reached REJECTED -- a real peer
 * answered but the identity did not agree; this module never auto-retries a
 * rejection (see NativeLobbyState_Poll).
 * PEER_LOST: the underlying peer link reached FAULTED or DIVERGED after
 * previously being READY -- a session that was running stopped being
 * usable.
 * LISTENING (appended; docs/SOLO_CAB_MILESTONE.md SOLO-4, risk 1): the
 * listen-only link NativeLobbyState_BeginListen opened. Nothing is attempted
 * and nothing is sent; Poll only drains the socket and latches peerHeard
 * when a well-formed handshake datagram arrives from a candidate address.
 */
enum NativeLobbyStateMode
{
	NATIVE_LOBBY_STATE_WAITING_FOR_PEER = 0,
	NATIVE_LOBBY_STATE_HANDSHAKING = 1,
	NATIVE_LOBBY_STATE_READY = 2,
	NATIVE_LOBBY_STATE_REJECTED = 3,
	NATIVE_LOBBY_STATE_PEER_LOST = 4,
	NATIVE_LOBBY_STATE_LISTENING = 5
};

/*
 * Caller-owned, no dynamic allocation, fixed-size members only. A
 * zero-initialized struct is never a valid "begun" state -- always call
 * NativeLobbyState_Begin first; the zero value of "mode"
 * (NATIVE_LOBBY_STATE_WAITING_FOR_PEER) is also the safe default a NULL
 * pointer or a never-begun struct reports through the NULL-tolerant
 * accessors below, mirroring struct NativeLockstepPeerLink's own IDLE(0)
 * convention.
 */
struct NativeLobbyState
{
	enum NativeLobbyStateMode mode;
	struct NativeLockstepPeerLink link;
	struct NativeUdpTransportAddress candidates[NATIVE_LOBBY_STATE_MAX_CANDIDATES];
	uint32_t candidateCount;
	uint32_t currentCandidateIndex;
	/* Frame count spent attempting the current candidate while HANDSHAKING;
	 * reset to 0 every time a new candidate is opened. */
	uint32_t candidateFrameCounter;
	uint32_t attemptFramesPerCandidate;
	uint32_t retransmitIntervalFrames;
	uint16_t localPort;
	uint8_t localRole;
	uint32_t inputDelay;
	/* Snapshot of the proposal passed to Begin, reused verbatim by every
	 * candidate attempt in this cycle and by NativeLobbyState_RestartCycle. */
	struct NativeMatchConfigV1 proposedConfig;
	/* LISTENING only: 1 once a well-formed handshake datagram arrived from a
	 * candidate address (SOLO-4). Cleared by every Begin, BeginListen, and
	 * Close. */
	uint8_t peerHeard;
};

/*
 * Validates candidateCount does not exceed NATIVE_LOBBY_STATE_MAX_CANDIDATES,
 * copies the candidates by value, stores the config/role/inputDelay/
 * localPort and the two frame-count policy parameters (attemptFramesPerCandidate,
 * retransmitIntervalFrames), and either opens a NativeLockstepPeerLink
 * against candidate 0 and sets mode HANDSHAKING (if candidateCount > 0), or
 * sets mode WAITING_FOR_PEER immediately (if candidateCount == 0).
 *
 * Returns 0 and changes nothing on any invalid input: state or proposedConfig
 * NULL, candidates NULL while candidateCount > 0, candidateCount exceeding
 * NATIVE_LOBBY_STATE_MAX_CANDIDATES, localRole not CAB1_HUMAN or CAB2_HUMAN,
 * proposedConfig failing NativeMatchConfigV1_Validate,
 * attemptFramesPerCandidate == 0, or (candidateCount > 0) opening candidate 0
 * itself failing (mirrors NativeLockstepPeerLink_Open's own "changes nothing
 * on failure" convention).
 */
int NativeLobbyState_Begin(struct NativeLobbyState *state, uint16_t localPort,
	const struct NativeUdpTransportAddress *candidates, uint32_t candidateCount,
	const struct NativeMatchConfigV1 *proposedConfig, uint8_t localRole, uint32_t inputDelay,
	uint32_t attemptFramesPerCandidate, uint32_t retransmitIntervalFrames);

/*
 * The listen-only lobby (docs/SOLO_CAB_MILESTONE.md SOLO-4, risk 1): while a
 * cabinet races solo it keeps one socket bound on localPort, never sends,
 * and only notes whether a candidate peer is in its own lobby. Validates like
 * Begin (candidateCount at most NATIVE_LOBBY_STATE_MAX_CANDIDATES, candidates
 * non-NULL when candidateCount > 0), then opens the peer link in its listen
 * mode (NativeLockstepPeerLink_OpenListen) before touching *state, so a
 * failure (for example localPort in use) returns 0 and changes nothing. On
 * success *state is reset, the candidates are copied by value, localPort is
 * stored, peerHeard is 0, and mode is LISTENING. Nothing is sent. Like
 * Begin, it must not be called while a link is open: Close first.
 */
int NativeLobbyState_BeginListen(struct NativeLobbyState *state, uint16_t localPort,
	const struct NativeUdpTransportAddress *candidates, uint32_t candidateCount);

/* 1 while the state is LISTENING and a candidate peer has been heard
 * (peerHeard); 0 otherwise, including a NULL state. */
int NativeLobbyState_PeerHeard(const struct NativeLobbyState *state);

/*
 * Call once per tick. A NULL state is a no-op. Behavior depends on mode:
 *
 * - HANDSHAKING: increments the per-candidate frame counter; every
 *   retransmitIntervalFrames ticks (via modulo on the counter; a
 *   retransmitIntervalFrames of 0 disables the periodic resend rather than
 *   dividing by zero, a defensive posture not otherwise reachable through
 *   NativeLobbyState_Begin's documented validation) calls
 *   NativeLockstepPeerLink_Retransmit before this tick's unconditional
 *   NativeLockstepPeerLink_Poll call, preserving the peer link's own
 *   Retransmit-before-Poll ordering contract; then reads the peer link mode:
 *     - RUNNING: state mode becomes READY; stops advancing candidates.
 *     - REJECTED: state mode becomes REJECTED, the current link is closed;
 *       stops advancing candidates. This module never auto-retries a
 *       rejected outcome -- a real but disagreeing peer answered, which
 *       usually needs a human or a different config, not a blind retry; the
 *       caller decides whether to call NativeLobbyState_RestartCycle.
 *     - otherwise, once the per-candidate frame counter reaches
 *       attemptFramesPerCandidate with the peer link still HANDSHAKING
 *       (nobody answered this candidate in time): closes the current link,
 *       advances to the next candidate index (no auto-wrap back to candidate
 *       0), and if there is a next candidate, opens a new
 *       NativeLockstepPeerLink against it, stays in HANDSHAKING, and resets
 *       the per-candidate counter to 0; once every candidate has been tried
 *       once, sets mode WAITING_FOR_PEER and stops, leaving it to the caller
 *       to call NativeLobbyState_RestartCycle if it wants to cycle again.
 * - READY: calls NativeLockstepPeerLink_Poll on the underlying link (to keep
 *   draining arriving lockstep bundles even though the lobby layer itself
 *   has nothing left to decide), then checks the peer link mode; if it
 *   became FAULTED or DIVERGED, sets state mode PEER_LOST.
 * - LISTENING: NativeLockstepPeerLink_PollListen on the link with the
 *   candidate list as the filter; any datagram it counts latches peerHeard.
 *   Nothing is sent and the mode never changes.
 * - WAITING_FOR_PEER, REJECTED, or PEER_LOST: no-op. This module never
 *   silently resumes attempting connections on its own once it has given up
 *   or definitively failed; the caller must call
 *   NativeLobbyState_RestartCycle to do anything further.
 */
void NativeLobbyState_Poll(struct NativeLobbyState *state);

/*
 * Usable only from WAITING_FOR_PEER (candidate list exhausted with nobody
 * answering), REJECTED (caller decided to retry, e.g. after the operator
 * fixed a config mismatch), or PEER_LOST: closes whatever link is currently
 * open (a no-op if none is, mirroring NativeLockstepPeerLink_Close), resets
 * the candidate index and per-candidate frame counter, and re-opens against
 * candidate 0 (or sets WAITING_FOR_PEER again if candidateCount is 0) using
 * the same stored config/role/inputDelay from the original
 * NativeLobbyState_Begin call. If opening candidate 0 itself fails, falls
 * back to WAITING_FOR_PEER rather than leaving a half-open link (the restart
 * request itself still succeeds; this only affects which mode it lands in).
 *
 * Returns 0 and changes nothing from any other mode (HANDSHAKING or READY,
 * where there is an active attempt or an active session already in progress
 * that must not be silently discarded; LISTENING, which is ended only by
 * Close), or for a NULL state.
 */
int NativeLobbyState_RestartCycle(struct NativeLobbyState *state);

/* Returns NATIVE_LOBBY_STATE_WAITING_FOR_PEER for a NULL state, mirroring
 * NativeLockstepPeerLink_Mode's own NULL convention. */
enum NativeLobbyStateMode NativeLobbyState_Mode(const struct NativeLobbyState *state);

/*
 * Non-const accessor to the currently active peer link: NULL if there is no
 * currently open link (WAITING_FOR_PEER with nothing attempted yet, or a
 * NULL state) and while LISTENING (a listen-only link has no handshake or
 * session to drive), non-NULL whenever a link is currently open -- meaningful once
 * mode is READY (to drive the running session), or PEER_LOST (to read the
 * terminal session report one last time).
 */
struct NativeLockstepPeerLink *NativeLobbyState_Link(struct NativeLobbyState *state);

/*
 * Test-observable accessor (not otherwise exposed): the index into the
 * candidate list this state is currently attempting, or last attempted.
 * Returns 0 for a NULL state.
 */
uint32_t NativeLobbyState_CurrentCandidateIndex(const struct NativeLobbyState *state);

/*
 * Closes whatever link is currently open, if any (the listen-only link
 * included), clears peerHeard, and resets mode to WAITING_FOR_PEER. Safe on
 * a never-begun (e.g. zero-initialized) or already-closed state, and safe on
 * a NULL state.
 */
void NativeLobbyState_Close(struct NativeLobbyState *state);

#endif
