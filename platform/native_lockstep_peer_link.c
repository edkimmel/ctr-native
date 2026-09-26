#include "platform/native_lockstep_peer_link.h"

#include <string.h>

/* The aux route is chosen by exact byte count, so its width must differ
 * from both wire records this link routes, or the routes would collide. */
_Static_assert(NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES != NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES,
	"aux datagram width must differ from the handshake message width");
_Static_assert(NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES,
	"aux datagram width must differ from the lockstep bundle width");
_Static_assert(NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY > 0u, "aux inbox must hold at least one entry");

/* Empties the aux inbox and zeroes its drop counter; shared by Open (on
 * success) and Close so nothing from a previous link survives. The payload
 * bytes are cleared too, so a stale datagram is not even left behind in
 * memory. */
static void NativeLockstepPeerLink_ResetAux(struct NativeLockstepPeerLink *link)
{
	memset(link->auxBytes, 0, sizeof(link->auxBytes));
	link->auxHead = 0;
	link->auxCount = 0;
	link->droppedAuxCount = 0;
}

int NativeLockstepPeerLink_Open(struct NativeLockstepPeerLink *link, uint16_t localPort,
	const struct NativeUdpTransportAddress *peer, const struct NativeMatchConfigV1 *proposedConfig,
	uint8_t localRole, uint32_t inputDelay)
{
	uint8_t bytes[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	size_t size = 0;

	if ((link == NULL) || (peer == NULL) || (proposedConfig == NULL))
	{
		return 0;
	}

	if (!NativeUdpTransport_GlobalInit())
	{
		return 0;
	}

	if (!NativeUdpTransport_Open(&link->transport, localPort))
	{
		NativeUdpTransport_GlobalShutdown();
		return 0;
	}

	NativeLockstepHandshake_Init(&link->handshake);
	if (!NativeLockstepHandshake_Begin(&link->handshake, proposedConfig, localRole))
	{
		NativeUdpTransport_Close(&link->transport);
		NativeUdpTransport_GlobalShutdown();
		return 0;
	}

	if (!NativeLockstepHandshake_ComposeMessage(&link->handshake, bytes, sizeof(bytes), &size) ||
	    !NativeUdpTransport_Send(&link->transport, peer, bytes, size))
	{
		NativeUdpTransport_Close(&link->transport);
		NativeUdpTransport_GlobalShutdown();
		return 0;
	}

	link->peerAddress = *peer;
	link->inputDelay = inputDelay;
	link->localRole = localRole;
	link->earlyBundleCount = 0;
	link->droppedEarlyBundleCount = 0;
	link->droppedForeignBundleCount = 0;
	NativeLockstepPeerLink_ResetAux(link);
	link->mode = NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING;
	link->opened = 1;
	return 1;
}

int NativeLockstepPeerLink_OpenListen(struct NativeLockstepPeerLink *link, uint16_t localPort)
{
	if (link == NULL)
	{
		return 0;
	}
	if (!NativeUdpTransport_GlobalInit())
	{
		return 0;
	}
	if (!NativeUdpTransport_Open(&link->transport, localPort))
	{
		NativeUdpTransport_GlobalShutdown();
		return 0;
	}

	/* No handshake is begun and nothing is sent: a listen-only link. */
	NativeLockstepHandshake_Init(&link->handshake);
	memset(&link->peerAddress, 0, sizeof(link->peerAddress));
	link->inputDelay = 0;
	link->localRole = 0;
	link->earlyBundleCount = 0;
	link->droppedEarlyBundleCount = 0;
	link->droppedForeignBundleCount = 0;
	NativeLockstepPeerLink_ResetAux(link);
	link->mode = NATIVE_LOCKSTEP_PEER_LINK_LISTENING;
	link->opened = 1;
	return 1;
}

void NativeLockstepPeerLink_Retransmit(struct NativeLockstepPeerLink *link)
{
	uint8_t bytes[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	size_t size = 0;

	if ((link == NULL) || (link->mode != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING))
	{
		return;
	}
	if (!NativeLockstepHandshake_ComposeMessage(&link->handshake, bytes, sizeof(bytes), &size))
	{
		return;
	}
	NativeUdpTransport_Send(&link->transport, &link->peerAddress, bytes, size);
}

/* Large enough for either wire record this link ever receives: a handshake
 * message (284 bytes) or a lockstep bundle (128 bytes); the static assert
 * below checks it also fits an aux datagram (64 bytes). UDP preserves
 * datagram boundaries, so a capacity at least as large as the sender's
 * actual datagram is all NativeUdpTransport_Receive needs. */
#define NATIVE_LOCKSTEP_PEER_LINK_POLL_BUFFER_BYTES \
	(NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES > NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES \
	     ? NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES \
	     : NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)

_Static_assert(NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES <= NATIVE_LOCKSTEP_PEER_LINK_POLL_BUFFER_BYTES,
	"aux datagram must fit the poll receive buffer");

/* Applies one session-mode read-out to link mode: DIVERGED and FAULTED are
 * both terminal for the session, so they replace link mode; RUNNING (or, in
 * principle, IDLE, which never happens here since the session is only ever
 * queried after a successful Open) leaves link mode exactly as it is. */
static void NativeLockstepPeerLink_ApplySessionMode(struct NativeLockstepPeerLink *link)
{
	enum NativeLockstepSessionMode sessionMode = NativeLockstepSession_Mode(&link->session);

	if (sessionMode == NATIVE_LOCKSTEP_DIVERGED)
	{
		link->mode = NATIVE_LOCKSTEP_PEER_LINK_DIVERGED;
	}
	else if (sessionMode == NATIVE_LOCKSTEP_FAULTED)
	{
		link->mode = NATIVE_LOCKSTEP_PEER_LINK_FAULTED;
	}
}

/*
 * LR-14 (docs/LOCKSTEP_RACE_MILESTONE.md): 1 when a bundle-width record is
 * another match's, so it must be dropped rather than handed to the session.
 * The record is decoded against this link's open session (its match
 * identity, protocol version, and input delay) with the unchanged decoder,
 * and it is foreign exactly when the decoder's first failure is
 * MATCH_IDENTITY. The decoder checks magic, version, size, and the record's
 * own digest before the identity, so a corrupt record of either identity
 * fails earlier (BAD_DIGEST, for instance) and is not foreign: it goes to the
 * session and faults there as before. A record that decodes, or fails on any
 * check after the identity, is not foreign either. Only called while the
 * session is open (RUNNING link mode).
 */
static int NativeLockstepPeerLink_IsForeignBundle(const struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	struct NativeCodecReader reader;
	struct NativeLockstepBundleV1 bundle;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_NONE;

	if (size != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
	{
		return 0;
	}
	NativeCodecReader_Init(&reader, bytes, size);
	if (NativeLockstepBundleV1_Decode(&reader, link->session.matchIdentity, link->session.protocolVersion,
		    link->session.inputDelay, &bundle, &cause))
	{
		return 0;
	}
	return (cause == NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY) ? 1 : 0;
}

/* The one path by which a bundle reaches the session, while RUNNING: a
 * foreign-identity record (LR-14) is dropped and counted in
 * droppedForeignBundleCount; every other record goes to
 * NativeLockstepSession_AcceptBundle unchanged, and the session's
 * DIVERGED/FAULTED latch is mirrored into link mode. */
static void NativeLockstepPeerLink_AcceptOrDropBundle(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	if (NativeLockstepPeerLink_IsForeignBundle(link, bytes, size))
	{
		link->droppedForeignBundleCount++;
		return;
	}
	(void)NativeLockstepSession_AcceptBundle(&link->session, bytes, size);
	NativeLockstepPeerLink_ApplySessionMode(link);
}

/* Called exactly once, immediately after this side's own handshake latches
 * COMPLETE and NativeLockstepSession_Open has just succeeded: hands every
 * bundle staged by NativeLockstepPeerLink_Poll while this side was still
 * HANDSHAKING to the freshly opened session, in the order they arrived, then
 * clears the buffer. Mirrors NativeLockstepPeerLink_Poll's own RUNNING-mode
 * handling for each one, including the foreign-identity drop (LR-14) and the
 * DIVERGED/FAULTED mode transitions, so a peer whose bundles arrived early is
 * treated exactly as if this side had already been RUNNING when they
 * arrived. The identity is screened here, on replay, not when a record is
 * staged: before the session opens there is no agreed identity to screen
 * against. */
static void NativeLockstepPeerLink_ReplayEarlyBundles(struct NativeLockstepPeerLink *link)
{
	for (uint32_t i = 0; i < link->earlyBundleCount; i++)
	{
		if (link->mode != NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
		{
			/* A replayed bundle already latched DIVERGED/FAULTED: stop, the
			 * session is no longer RUNNING for simulation purposes. */
			break;
		}
		NativeLockstepPeerLink_AcceptOrDropBundle(link, link->earlyBundleBytes[i], link->earlyBundleSizes[i]);
	}
	link->earlyBundleCount = 0;
}

static void NativeLockstepPeerLink_StageEarlyBundle(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	if (link->earlyBundleCount >= NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY)
	{
		/* Buffer exhausted: dropped, the same "not an error, just lossy"
		 * posture UDP itself already has, but counted so the loss is
		 * observable (GAP 4) instead of only surfacing much later as a
		 * TakeFrameInputs stall. */
		link->droppedEarlyBundleCount++;
		return;
	}
	memcpy(link->earlyBundleBytes[link->earlyBundleCount], bytes, size);
	link->earlyBundleSizes[link->earlyBundleCount] = size;
	link->earlyBundleCount++;
}

/*
 * Handles one NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES-sized datagram.
 * Always feeds it to the handshake: AcceptMessage's own documented behavior
 * safely no-ops once the handshake is already COMPLETE or REJECTED, which
 * is exactly the desired behavior for a late or duplicate delivery arriving
 * after this side is already RUNNING or terminal, so link mode is only
 * ever touched here while it is still HANDSHAKING.
 */
static void NativeLockstepPeerLink_HandleHandshakeDatagram(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	(void)NativeLockstepHandshake_AcceptMessage(&link->handshake, bytes, size);

	if (link->mode != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
	{
		return;
	}

	if (NativeLockstepHandshake_Mode(&link->handshake) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE)
	{
		const struct NativeLockstepHandshakeResult *result = NativeLockstepHandshake_Result(&link->handshake);
		uint8_t slotIndex = 0;

		if ((result == NULL) || !NativeMatchConfigV1_FindRoleSlot(&result->agreedConfig, link->localRole, &slotIndex))
		{
			link->mode = NATIVE_LOCKSTEP_PEER_LINK_FAULTED;
			return;
		}

		NativeLockstepSession_Init(&link->session);
		if (NativeLockstepSession_Open(&link->session, &result->agreedConfig, link->inputDelay, slotIndex))
		{
			link->mode = NATIVE_LOCKSTEP_PEER_LINK_RUNNING;
			NativeLockstepPeerLink_ReplayEarlyBundles(link);
		}
		else
		{
			link->mode = NATIVE_LOCKSTEP_PEER_LINK_FAULTED;
		}
	}
	else if (NativeLockstepHandshake_Mode(&link->handshake) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED)
	{
		link->mode = NATIVE_LOCKSTEP_PEER_LINK_REJECTED;
	}
}

/* Handles one NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES-sized datagram: fed
 * to the session while RUNNING (a foreign-identity record is dropped and
 * counted instead, LR-14), staged for later replay while still HANDSHAKING
 * (see NativeLockstepPeerLink_ReplayEarlyBundles), and dropped in every
 * other (terminal) mode. */
static void NativeLockstepPeerLink_HandleBundleDatagram(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	if (link->mode == NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
	{
		NativeLockstepPeerLink_AcceptOrDropBundle(link, bytes, size);
	}
	else if (link->mode == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
	{
		NativeLockstepPeerLink_StageEarlyBundle(link, bytes, size);
	}
}

/* Handles one NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES-sized datagram: appended,
 * unmodified, to the back of the aux inbox while RUNNING (discarding and
 * counting the oldest entry first when the inbox is full), dropped
 * uncounted in every other mode. Never touches the handshake, the session,
 * or link mode. */
static void NativeLockstepPeerLink_HandleAuxDatagram(struct NativeLockstepPeerLink *link, const uint8_t *bytes)
{
	uint32_t tail;

	if (link->mode != NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
	{
		return;
	}
	if (link->auxCount >= NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY)
	{
		/* Full: newest-wins, so the oldest entry makes room. */
		link->auxHead = (link->auxHead + 1u) % NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY;
		link->auxCount--;
		link->droppedAuxCount++;
	}
	tail = (link->auxHead + link->auxCount) % NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY;
	memcpy(link->auxBytes[tail], bytes, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES);
	link->auxCount++;
}

void NativeLockstepPeerLink_Poll(struct NativeLockstepPeerLink *link)
{
	uint32_t drained;

	if ((link == NULL) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_IDLE) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_REJECTED) ||
	    (link->mode == NATIVE_LOCKSTEP_PEER_LINK_FAULTED) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED) ||
	    (link->mode == NATIVE_LOCKSTEP_PEER_LINK_LISTENING))
	{
		return;
	}

	for (drained = 0; drained < NATIVE_LOCKSTEP_PEER_LINK_POLL_BUDGET; drained++)
	{
		uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_POLL_BUFFER_BYTES];
		size_t byteCount = 0;
		struct NativeUdpTransportAddress sender;
		enum NativeUdpTransportReceiveResult received =
			NativeUdpTransport_Receive(&link->transport, bytes, sizeof(bytes), &byteCount, &sender);

		if (received == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY)
		{
			break;
		}
		if (received != NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			/* TOO_SMALL or ERROR: drop this datagram and keep draining. */
			continue;
		}

		if ((sender.ipv4 != link->peerAddress.ipv4) || (sender.port != link->peerAddress.port))
		{
			/* Not from the configured peer (GAP 2): discard before this
			 * datagram is fed to either the handshake or the session, and
			 * keep draining. The inner decoders' magic/version/digest
			 * checks already backstop identity, so this is a defensive,
			 * cheap filter, not the only line of defense. */
			continue;
		}

		if (byteCount == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES)
		{
			NativeLockstepPeerLink_HandleHandshakeDatagram(link, bytes, byteCount);
		}
		else if (byteCount == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
		{
			NativeLockstepPeerLink_HandleBundleDatagram(link, bytes, byteCount);
		}
		else if (byteCount == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES)
		{
			NativeLockstepPeerLink_HandleAuxDatagram(link, bytes);
		}
		/* Any other size matches none of the three routes: dropped. */

		if ((link->mode == NATIVE_LOCKSTEP_PEER_LINK_REJECTED) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_FAULTED) ||
		    (link->mode == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED))
		{
			/* Now terminal: nothing more to do even if datagrams remain. */
			break;
		}
	}
}

/* 1 when sender equals one of peers[0 .. peerCount), ipv4 and port. */
static int NativeLockstepPeerLink_IsListedPeer(const struct NativeUdpTransportAddress *sender,
	const struct NativeUdpTransportAddress *peers, uint32_t peerCount)
{
	uint32_t i;

	if (peers == NULL)
	{
		return 0;
	}
	for (i = 0; i < peerCount; i++)
	{
		if ((sender->ipv4 == peers[i].ipv4) && (sender->port == peers[i].port))
		{
			return 1;
		}
	}
	return 0;
}

uint32_t NativeLockstepPeerLink_PollListen(struct NativeLockstepPeerLink *link, const struct NativeUdpTransportAddress *peers,
	uint32_t peerCount)
{
	uint32_t drained;
	uint32_t heard = 0;

	if ((link == NULL) || (link->mode != NATIVE_LOCKSTEP_PEER_LINK_LISTENING))
	{
		return 0;
	}

	for (drained = 0; drained < NATIVE_LOCKSTEP_PEER_LINK_POLL_BUDGET; drained++)
	{
		uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_POLL_BUFFER_BYTES];
		size_t byteCount = 0;
		struct NativeUdpTransportAddress sender;
		struct NativeCodecReader reader;
		struct NativeLockstepHandshakeMessageV1 message;
		enum NativeUdpTransportReceiveResult received =
			NativeUdpTransport_Receive(&link->transport, bytes, sizeof(bytes), &byteCount, &sender);

		if (received == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY)
		{
			break;
		}
		if ((received != NATIVE_UDP_TRANSPORT_RECEIVE_OK) || (byteCount != NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES) ||
		    !NativeLockstepPeerLink_IsListedPeer(&sender, peers, peerCount))
		{
			/* Discarded unread; the drain goes on. */
			continue;
		}
		NativeCodecReader_Init(&reader, bytes, byteCount);
		if (NativeLockstepHandshakeMessageV1_Decode(&reader, &message, NULL))
		{
			heard++;
		}
	}
	return heard;
}

int NativeLockstepPeerLink_ComposeAndSendBundle(struct NativeLockstepPeerLink *link, uint32_t frameIndex)
{
	uint8_t bytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t size = 0;

	if ((link == NULL) || (link->mode != NATIVE_LOCKSTEP_PEER_LINK_RUNNING))
	{
		return 0;
	}
	if (!NativeLockstepSession_ComposeBundle(&link->session, frameIndex, bytes, sizeof(bytes), &size))
	{
		return 0;
	}
	if (!NativeUdpTransport_Send(&link->transport, &link->peerAddress, bytes, size))
	{
		return 0;
	}
	return 1;
}

int NativeLockstepPeerLink_SendBundleVerbatim(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	struct NativeCodecReader reader;
	struct NativeLockstepBundleV1 bundle;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_NONE;

	if ((link == NULL) || (bytes == NULL) || (size != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES) ||
	    (link->mode != NATIVE_LOCKSTEP_PEER_LINK_RUNNING))
	{
		return 0;
	}
	/* LR-3, LR-49: link mode alone is not enough. The link copies the session
	 * mode only in Poll and in the staged replay, so a divergence latched
	 * inside RecordLocalDigests leaves link mode RUNNING until a later Poll
	 * hands a received (non-foreign) bundle to the session. */
	if (NativeLockstepSession_Mode(&link->session) != NATIVE_LOCKSTEP_RUNNING)
	{
		return 0;
	}
	/* Only this session's own bundle goes out: it must decode against the
	 * session's match identity, protocol version, and input delay, and carry
	 * the local slot as its sender, so a stale record of an earlier match or a
	 * peer's record is never put on the wire. */
	NativeCodecReader_Init(&reader, bytes, size);
	if (!NativeLockstepBundleV1_Decode(&reader, link->session.matchIdentity, link->session.protocolVersion,
		    link->session.inputDelay, &bundle, &cause) ||
	    (bundle.senderSlot != link->session.localSlot))
	{
		return 0;
	}
	return NativeUdpTransport_Send(&link->transport, &link->peerAddress, bytes, size) ? 1 : 0;
}

enum NativeLockstepPeerLinkMode NativeLockstepPeerLink_Mode(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? link->mode : NATIVE_LOCKSTEP_PEER_LINK_IDLE;
}

uint32_t NativeLockstepPeerLink_DroppedEarlyBundleCount(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? link->droppedEarlyBundleCount : 0u;
}

uint32_t NativeLockstepPeerLink_DroppedForeignBundleCount(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? link->droppedForeignBundleCount : 0u;
}

int NativeLockstepPeerLink_SendAux(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	if ((link == NULL) || (bytes == NULL) || (link->mode != NATIVE_LOCKSTEP_PEER_LINK_RUNNING) ||
	    (size != NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES))
	{
		return 0;
	}
	return NativeUdpTransport_Send(&link->transport, &link->peerAddress, bytes, size) ? 1 : 0;
}

int NativeLockstepPeerLink_TakeAux(struct NativeLockstepPeerLink *link, uint8_t *out, size_t capacity, size_t *sizeOut)
{
	if ((link == NULL) || (out == NULL) || (sizeOut == NULL) || (capacity < NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES) ||
	    (link->auxCount == 0u))
	{
		return 0;
	}
	memcpy(out, link->auxBytes[link->auxHead], NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES);
	*sizeOut = NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES;
	link->auxHead = (link->auxHead + 1u) % NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY;
	link->auxCount--;
	return 1;
}

uint32_t NativeLockstepPeerLink_AuxCount(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? link->auxCount : 0u;
}

uint32_t NativeLockstepPeerLink_DroppedAuxCount(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? link->droppedAuxCount : 0u;
}

struct NativeLockstepSession *NativeLockstepPeerLink_Session(struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? &link->session : NULL;
}

const struct NativeLockstepHandshakeResult *NativeLockstepPeerLink_HandshakeResult(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? NativeLockstepHandshake_Result(&link->handshake) : NULL;
}

void NativeLockstepPeerLink_Close(struct NativeLockstepPeerLink *link)
{
	if (link == NULL)
	{
		return;
	}
	if (link->opened)
	{
		NativeUdpTransport_Close(&link->transport);
		NativeUdpTransport_GlobalShutdown();
		link->opened = 0;
	}
	NativeLockstepPeerLink_ResetAux(link);
	link->mode = NATIVE_LOCKSTEP_PEER_LINK_IDLE;
}
