#include "platform/native_lockstep_peer_link.h"

#include <string.h>

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
	link->mode = NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING;
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
 * message (284 bytes) or a lockstep bundle (128 bytes). UDP preserves
 * datagram boundaries, so a capacity at least as large as the sender's
 * actual datagram is all NativeUdpTransport_Receive needs. */
#define NATIVE_LOCKSTEP_PEER_LINK_POLL_BUFFER_BYTES \
	(NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES > NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES \
	     ? NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES \
	     : NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)

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

/* Called exactly once, immediately after this side's own handshake latches
 * COMPLETE and NativeLockstepSession_Open has just succeeded: hands every
 * bundle staged by NativeLockstepPeerLink_Poll while this side was still
 * HANDSHAKING to the freshly opened session, in the order they arrived, then
 * clears the buffer. Mirrors NativeLockstepPeerLink_Poll's own RUNNING-mode
 * handling for each one, including the DIVERGED/FAULTED mode transitions,
 * so a peer whose bundles arrived early is treated exactly as if this side
 * had already been RUNNING when they arrived. */
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
		(void)NativeLockstepSession_AcceptBundle(&link->session, link->earlyBundleBytes[i], link->earlyBundleSizes[i]);
		NativeLockstepPeerLink_ApplySessionMode(link);
	}
	link->earlyBundleCount = 0;
}

static void NativeLockstepPeerLink_StageEarlyBundle(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	if (link->earlyBundleCount >= NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY)
	{
		/* Buffer exhausted: dropped, the same "not an error, just lossy"
		 * posture UDP itself already has. */
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
 * straight to the session while RUNNING, staged for later replay while
 * still HANDSHAKING (see NativeLockstepPeerLink_ReplayEarlyBundles), and
 * dropped in every other (terminal) mode. */
static void NativeLockstepPeerLink_HandleBundleDatagram(struct NativeLockstepPeerLink *link, const uint8_t *bytes, size_t size)
{
	if (link->mode == NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
	{
		(void)NativeLockstepSession_AcceptBundle(&link->session, bytes, size);
		NativeLockstepPeerLink_ApplySessionMode(link);
	}
	else if (link->mode == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
	{
		NativeLockstepPeerLink_StageEarlyBundle(link, bytes, size);
	}
}

void NativeLockstepPeerLink_Poll(struct NativeLockstepPeerLink *link)
{
	uint32_t drained;

	if ((link == NULL) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_IDLE) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_REJECTED) ||
	    (link->mode == NATIVE_LOCKSTEP_PEER_LINK_FAULTED) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED))
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

		if (byteCount == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES)
		{
			NativeLockstepPeerLink_HandleHandshakeDatagram(link, bytes, byteCount);
		}
		else if (byteCount == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
		{
			NativeLockstepPeerLink_HandleBundleDatagram(link, bytes, byteCount);
		}
		/* Any other size matches neither wire record at this seam: dropped. */

		if ((link->mode == NATIVE_LOCKSTEP_PEER_LINK_REJECTED) || (link->mode == NATIVE_LOCKSTEP_PEER_LINK_FAULTED) ||
		    (link->mode == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED))
		{
			/* Now terminal: nothing more to do even if datagrams remain. */
			break;
		}
	}
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

enum NativeLockstepPeerLinkMode NativeLockstepPeerLink_Mode(const struct NativeLockstepPeerLink *link)
{
	return (link != NULL) ? link->mode : NATIVE_LOCKSTEP_PEER_LINK_IDLE;
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
	link->mode = NATIVE_LOCKSTEP_PEER_LINK_IDLE;
}
