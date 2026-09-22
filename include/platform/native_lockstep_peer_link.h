#ifndef PLATFORM_NATIVE_LOCKSTEP_PEER_LINK_H
#define PLATFORM_NATIVE_LOCKSTEP_PEER_LINK_H

#include "platform/native_lockstep_handshake.h"
#include "platform/native_lockstep_session.h"
#include "platform/native_udp_transport.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Real-transport integration glue (docs/LOBBY_MILESTONE.md section 2.3): the
 * one module in this milestone allowed to depend on all three of
 * ctr_native_udp_transport, ctr_native_lockstep_handshake, and
 * ctr_native_lockstep_session -- nothing in Tasks 2-4 depends on this one.
 * It owns one of each in a single caller-owned struct, drives the handshake
 * to completion over a real socket, opens the session on the agreed config
 * once the handshake completes, and from then on composes/sends and
 * receives/accepts lockstep bundles over that same real socket. No dynamic
 * allocation and no wall clock: poll cadence and retransmission cadence are
 * entirely caller-driven, the same posture the rest of this stack takes.
 *
 * A zero-initialized struct (e.g. "struct NativeLockstepPeerLink link = {0};")
 * is the never-opened state: mode is IDLE (0) and NativeLockstepPeerLink_Close
 * is a safe no-op on it, mirroring NativeUdpTransport_Close's own convention.
 */

/* Bounded number of datagrams drained per NativeLockstepPeerLink_Poll call,
 * so a receive burst cannot spin that function forever. */
#define NATIVE_LOCKSTEP_PEER_LINK_POLL_BUDGET 16u

/*
 * Bounded capacity for lockstep bundles received while still HANDSHAKING
 * (see the NativeLockstepPeerLink_Poll doc comment below for why this
 * exists). Sized to NATIVE_LOCKSTEP_RING_CAPACITY, the same bound the
 * session's own per-peer delay/reorder window uses once it is open, so this
 * pre-open buffer can never hold more not-yet-consumed frames than the
 * session could accept anyway.
 */
#define NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY NATIVE_LOCKSTEP_RING_CAPACITY

/*
 * REJECTED means the handshake ended in rejection (see
 * NativeLockstepPeerLink_HandshakeResult for the reason). FAULTED/DIVERGED
 * mirror the underlying NativeLockstepSession's FirstFault/FirstDivergence
 * becoming latched once RUNNING. All three, like the handshake and session
 * latches they mirror, are terminal: reached once, kept forever.
 */
enum NativeLockstepPeerLinkMode
{
	NATIVE_LOCKSTEP_PEER_LINK_IDLE = 0,
	NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING = 1,
	NATIVE_LOCKSTEP_PEER_LINK_RUNNING = 2,
	NATIVE_LOCKSTEP_PEER_LINK_REJECTED = 3,
	NATIVE_LOCKSTEP_PEER_LINK_FAULTED = 4,
	NATIVE_LOCKSTEP_PEER_LINK_DIVERGED = 5
};

/*
 * Caller-owned, no dynamic allocation, fixed-size members only. "opened" is
 * private bookkeeping (not part of the brief's field list) so
 * NativeLockstepPeerLink_Close can pair NativeUdpTransport_GlobalShutdown
 * with exactly the NativeUdpTransport_GlobalInit this link's own
 * NativeLockstepPeerLink_Open performed, and stays idempotent across repeat
 * Close calls without decrementing an unrelated caller's global init count.
 */
struct NativeLockstepPeerLink
{
	enum NativeLockstepPeerLinkMode mode;
	struct NativeUdpTransport transport;
	struct NativeLockstepHandshake handshake;
	struct NativeLockstepSession session;
	struct NativeUdpTransportAddress peerAddress;
	uint32_t inputDelay;
	uint8_t localRole;
	uint8_t opened;
	/*
	 * Early-bundle staging, also private bookkeeping: a peer whose own
	 * handshake completes first may start sending real lockstep bundles
	 * before this side's handshake has completed (each side latches
	 * COMPLETE independently, as soon as it has validated the peer's HELLO,
	 * with no guarantee the peer has yet validated this side's own HELLO in
	 * return). Those bundles arrive while this side is still HANDSHAKING,
	 * before NativeLockstepSession_Open has even run, so there is no
	 * session yet to hand them to, and the session never retransmits an
	 * already-composed frame's bundle on its own. Dropping them would
	 * therefore permanently lose that peer's earliest frames and deadlock
	 * the match. NativeLockstepPeerLink_Poll instead stages up to
	 * NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY of them here, in
	 * arrival order, and replays every staged one into the freshly opened
	 * session the moment this side's own handshake completes, before
	 * returning control to the caller.
	 */
	uint8_t earlyBundleBytes[NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY][NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t earlyBundleSizes[NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY];
	uint32_t earlyBundleCount;
	/*
	 * Simple observable counter, the same idiom
	 * native_lockstep_input_window.c staleDropCount/duplicateAcceptCount
	 * uses: incremented whenever NativeLockstepPeerLink_Poll drops a
	 * 9th-and-later early-arriving bundle because earlyBundleBytes is
	 * already at NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY. The drop
	 * behavior itself is unchanged (capacity stays bounded and fixed); this
	 * only makes it observable instead of a silent loss that would
	 * otherwise only surface much later as a TakeFrameInputs stall. Read via
	 * NativeLockstepPeerLink_DroppedEarlyBundleCount.
	 */
	uint32_t droppedEarlyBundleCount;
};

/*
 * Calls NativeUdpTransport_GlobalInit, opens the transport on localPort,
 * stores the peer address, calls NativeLockstepHandshake_Init then _Begin
 * with proposedConfig/localRole, stores inputDelay, sets mode HANDSHAKING,
 * and immediately composes and sends the first HELLO over the real socket
 * to the peer address. Returns 0 and cleans up anything partially opened
 * (transport, global init) on any failure -- a NULL argument, a bad
 * transport open, a handshake Begin rejection (bad role, invalid config, a
 * role not present in the config), or a failure composing/sending the first
 * HELLO -- leaving *link otherwise untouched, the same "unchanged on
 * rejection" convention NativeLockstepHandshake_Begin and
 * NativeLockstepSession_Open both use.
 */
int NativeLockstepPeerLink_Open(struct NativeLockstepPeerLink *link, uint16_t localPort,
	const struct NativeUdpTransportAddress *peer, const struct NativeMatchConfigV1 *proposedConfig,
	uint8_t localRole, uint32_t inputDelay);

/*
 * Only meaningful while mode is HANDSHAKING: composes the handshake's
 * current outgoing message again and sends it to the peer address. A no-op
 * in every other mode, including on a NULL link. This module has no wall
 * clock; the caller decides the resend cadence and calls this on its own
 * schedule.
 *
 * Caller contract while mode is HANDSHAKING (see also the
 * NativeLockstepPeerLink_Poll doc comment below): call this before
 * NativeLockstepPeerLink_Poll on every tick, not after. The two peers'
 * handshakes complete independently (each latches COMPLETE as soon as it
 * has validated the peer's HELLO, with no guarantee the peer has yet
 * validated this side's own HELLO in return -- see the earlyBundleBytes
 * field doc comment on struct NativeLockstepPeerLink), so the tick whose
 * Poll() call first discovers this side's own handshake completion is
 * exactly the tick on which the peer may still be waiting to receive one
 * more copy of this side's HELLO. This module never auto-resends
 * internally -- it has no wall clock, and the caller-driven cadence is
 * deliberate (docs/LOBBY_MILESTONE.md section 2.3) -- so a caller that
 * polls first and only calls Retransmit afterward (for example, a loop
 * shaped "Poll(); if (mode changed) break;" that never reaches a
 * following Retransmit call once mode leaves HANDSHAKING) can silently
 * skip that last HELLO and stall the peer, which may then never complete
 * its own handshake. Calling Retransmit first, every tick, unconditionally
 * sends that tick's HELLO (or the armed ACCEPT/REJECT reply, once this
 * side itself has validated the peer's HELLO) before Poll can possibly
 * observe local completion, so the peer never misses the message it was
 * waiting for.
 */
void NativeLockstepPeerLink_Retransmit(struct NativeLockstepPeerLink *link);

/*
 * Drains up to NATIVE_LOCKSTEP_PEER_LINK_POLL_BUDGET waiting datagrams from
 * the transport this call (so a receive burst cannot spin this function
 * forever) via a Receive loop, stopping early once the transport reports no
 * more are waiting. A NULL link, or a link whose mode is already terminal
 * (REJECTED, FAULTED, or DIVERGED) or still IDLE (Open never succeeded), is
 * a no-op that drains and drops every waiting datagram without inspecting
 * it further.
 *
 * While mode is HANDSHAKING, see the NativeLockstepPeerLink_Retransmit doc
 * comment above: the caller must call Retransmit before calling this
 * function on every tick, or the tick that first observes local handshake
 * completion here can silently skip a HELLO the peer still needed.
 *
 * Each received datagram whose sender address (both ipv4 and port, from
 * NativeUdpTransport_Receive's sender out-parameter) does not exactly equal
 * this link's own peerAddress is silently discarded before being inspected
 * any further -- not fed to either the handshake or the session -- and the
 * drain simply continues with the next waiting datagram. This is a cheap,
 * unconditional identity check against the one peer address this link was
 * opened with; it adds no configuration and never resolves a hostname.
 *
 * Otherwise each received datagram is routed by its own byte count, not by
 * link mode alone, because the two peers' handshakes complete
 * independently (see the earlyBundleBytes field doc comment on struct
 * NativeLockstepPeerLink for why: a peer can start sending real bundles
 * before this side's own handshake has completed):
 *   - Exactly NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES (284): fed to
 *     NativeLockstepHandshake_AcceptMessage unconditionally. If this side's
 *     handshake is not yet terminal and its mode becomes COMPLETE, the
 *     local slot index is resolved from the agreed config via
 *     NativeMatchConfigV1_FindRoleSlot(&result->agreedConfig, localRole, ...)
 *     (localRole was captured at Open time), NativeLockstepSession_Init
 *     then _Open are called on this link's own session with the agreed
 *     config, inputDelay, and that slot index, and every early bundle
 *     staged so far is replayed into NativeLockstepSession_AcceptBundle, in
 *     arrival order, before this call returns: on session-open success mode
 *     becomes RUNNING, on failure mode becomes FAULTED (a defensive path
 *     only, since Begin already validated the config). If the handshake
 *     mode becomes REJECTED, link mode becomes REJECTED. If this side's
 *     handshake was already COMPLETE or REJECTED (a late or duplicate
 *     delivery, or one arriving after this side is already RUNNING),
 *     AcceptMessage's own documented no-op behavior on an already-terminal
 *     handshake applies and link mode is left exactly as it was.
 *   - Exactly NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES (128): while mode is
 *     RUNNING, fed to NativeLockstepSession_AcceptBundle on this link's own
 *     session; afterwards NativeLockstepSession_Mode is checked, DIVERGED
 *     sets link mode DIVERGED, FAULTED sets link mode FAULTED, anything
 *     else leaves RUNNING alone. While mode is still HANDSHAKING, it is
 *     staged into the early-bundle buffer instead (dropped only if that
 *     buffer is already full, which needs more in-flight bundles than
 *     NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY, an already-generous,
 *     session-ring-matching bound; each such drop increments
 *     droppedEarlyBundleCount, readable through
 *     NativeLockstepPeerLink_DroppedEarlyBundleCount, so the loss is
 *     observable rather than only surfacing much later as a
 *     TakeFrameInputs stall).
 *   - Any other byte count matches neither wire record at this seam and is
 *     dropped.
 * If handling a datagram makes link mode become terminal (REJECTED,
 * FAULTED, or DIVERGED) partway through the drain budget, this call stops
 * draining immediately rather than inspecting further already-waiting
 * datagrams; anything still queued is simply drained and dropped by the
 * terminal-mode no-op fast path the next time this function is called.
 */
void NativeLockstepPeerLink_Poll(struct NativeLockstepPeerLink *link);

/*
 * Requires mode RUNNING. Composes this link's session bundle for frameIndex
 * into a local fixed buffer (NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES from
 * native_lockstep_protocol.h, which native_lockstep_session.h already pulls
 * in transitively) via NativeLockstepSession_ComposeBundle, and sends it to
 * the peer address over the transport. Returns 0 on any failure -- a NULL
 * link, the wrong mode, a compose failure, or a send failure -- without
 * touching link state destructively.
 */
int NativeLockstepPeerLink_ComposeAndSendBundle(struct NativeLockstepPeerLink *link, uint32_t frameIndex);

enum NativeLockstepPeerLinkMode NativeLockstepPeerLink_Mode(const struct NativeLockstepPeerLink *link);

/*
 * Simple observable-counter accessor, the same idiom as
 * native_lockstep_input_window.c's staleDropCount/duplicateAcceptCount:
 * returns the number of early-arriving bundles NativeLockstepPeerLink_Poll
 * has dropped so far because the early-bundle staging buffer was already at
 * NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY (see the
 * NativeLockstepPeerLink_Poll doc comment above). Returns 0 for a NULL link,
 * mirroring NativeLockstepPeerLink_Mode's own NULL convention.
 */
uint32_t NativeLockstepPeerLink_DroppedEarlyBundleCount(const struct NativeLockstepPeerLink *link);

/*
 * Non-const accessor to the underlying session, so the caller can call
 * SubmitLocalInput, RecordLocalDigests, TakeFrameInputs, FirstDivergence,
 * and FirstFault directly on it once mode is RUNNING; this module does not
 * wrap every session function. Returns NULL for a NULL link.
 */
struct NativeLockstepSession *NativeLockstepPeerLink_Session(struct NativeLockstepPeerLink *link);

/*
 * Passthrough to NativeLockstepHandshake_Result on this link's own
 * handshake, useful once mode is REJECTED to read the reason. Returns NULL
 * for a NULL link, mirroring NativeLockstepHandshake_Result's own NULL
 * convention while the handshake has not latched a result yet.
 */
const struct NativeLockstepHandshakeResult *NativeLockstepPeerLink_HandshakeResult(const struct NativeLockstepPeerLink *link);

/*
 * Closes the transport and calls NativeUdpTransport_GlobalShutdown, but only
 * when this link's own Open actually performed the matching GlobalInit;
 * safe to call on a never-opened link (e.g. a zero-initialized struct) or an
 * already-closed one, and safe to call twice in a row. Leaves mode IDLE.
 */
void NativeLockstepPeerLink_Close(struct NativeLockstepPeerLink *link);

#endif
