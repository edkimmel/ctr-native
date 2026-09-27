#ifndef PLATFORM_NATIVE_ARCADE_DISCOVERY_SERVICE_H
#define PLATFORM_NATIVE_ARCADE_DISCOVERY_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_arcade_discovery.h"
#include "platform/native_net_interfaces.h"
#include "platform/native_udp_transport.h"

/*
 * Arcade-link discovery socket service (docs/DISCOVERY_MILESTONE.md DISC-2,
 * DISC-4, DISC-5, DISC-15, DISC-18; slice DISC-S3).
 *
 * Owns the discovery socket (INADDR_ANY:bindPort, SO_BROADCAST on) and the
 * pure core's peer table, and drives both from the caller's 30 Hz tick:
 *
 * - Tick drains the pending datagrams into NativeArcadeDiscovery_Receive
 *   with each sender's IPv4, advances the core one tick, and sends our
 *   beacon to every target on the first tick and every
 *   NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS ticks after it.
 * - Targets: the caller's override list when it has entries (DISC-18: no
 *   broadcast, no interface enumeration); otherwise
 *   NativeNetInterfaces_BuildTargets over the OS interface list (DISC-5) at
 *   the discovery port, read at Open and refreshed every
 *   NATIVE_ARCADE_DISCOVERY_SERVICE_REFRESH_TICKS ticks, or
 *   255.255.255.255 alone while the enumeration fails. The caller may hold
 *   the refresh (Tick's mayRefresh; docs/DISCOVERY_MILESTONE.md risk 10:
 *   the OS call takes over a millisecond, too long for a race frame): a
 *   refresh that falls due while held is pending and runs on the first tick
 *   that allows it, never skipped; the schedule itself does not move.
 * - Failure is never fatal (DISC-15): a bind failure makes Open return 0
 *   with the service closed, and the caller logs it and carries on without
 *   discovery; a receive error ends that tick's drain.
 * - The lan (DISC-19, optional): with one, a datagram whose source IPv4 is
 *   outside the lan is dropped before it reaches the core (counted in the
 *   status, never logged per datagram), every override target must be
 *   inside the lan, and without overrides the only target is the lan's
 *   directed broadcast, and only while a local interface is inside the lan
 *   (NativeNetInterfaces_BuildLanTargets); with none, or while the
 *   enumeration fails, there is no target and no beacon goes out (never a
 *   fallback to 255.255.255.255 or another interface's broadcast), and the
 *   next refresh retries. TakeLanChange reports each change of that
 *   interface for the caller's log. The one broadcast leaves the
 *   INADDR_ANY socket through the interface with the on-link route for the
 *   lan's subnet; the socket is never bound to an interface address.
 *   Without a lan every rule above is as before.
 *
 * Caller-owned state, no heap, no clock: the only time is the tick count.
 * Nothing here reaches simulation identity, the match config, replay,
 * checkpoints, canonical state, or the topology lease (DISC-14): only a peer
 * address and an elected seat leave it, through Pairing.
 */

#define NATIVE_ARCADE_DISCOVERY_SERVICE_DEFAULT_PORT   7000u
#define NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_OVERRIDES  4u
#define NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS    NATIVE_NET_INTERFACES_MAX_TARGETS
#define NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_INTERFACES 16u
#define NATIVE_ARCADE_DISCOVERY_SERVICE_REFRESH_TICKS  300u
/* A flood guard: at most this many datagrams are read per tick; the rest
 * wait in the socket for the next tick. */
#define NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_DRAIN      64u

/*
 * The caller-owned service. Treat as opaque; fields are public only so the
 * caller can own the storage. Zero-initialise it (or Close it) before the
 * first Open.
 */
struct NativeArcadeDiscoveryService
{
	uint8_t open;
	uint8_t overridden;        /* targets are the caller's override list */
	uint8_t enumerationFailed; /* the latest interface enumeration failed */
	uint8_t refreshPending;    /* a refresh fell due while the caller held it */
	uint16_t bindPort;
	uint16_t reserved2;
	uint32_t tickCount; /* service ticks since Open */
	uint32_t targetCount;
	uint32_t sendFailures;
	uint32_t receiveErrors;
	uint32_t refreshCount; /* interface-list reads since Open, Open's included */
	uint32_t beaconCount;  /* beacon rounds since Open */
	/* The lan (DISC-19); lanPrefixLength 0: none. */
	uint8_t lanPrefixLength;
	uint8_t lanChanged; /* lanInterfaceIpv4 changed since the last TakeLanChange */
	uint16_t reserved3;
	uint32_t lanNetwork;
	uint32_t lanInterfaceIpv4; /* the interface inside the lan at the latest read, 0: none */
	uint32_t lanDropped;       /* datagrams dropped for a source outside the lan */
	struct NativeUdpTransportAddress targets[NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS];
	struct NativeUdpTransport transport;
	struct NativeArcadeDiscoveryTable table;
	struct NativeNetInterfacesScratch scratch; /* last: Close does not clear it */
};

/* A snapshot for logs and tests. */
struct NativeArcadeDiscoveryServiceStatus
{
	uint8_t open;
	uint8_t overridden;
	uint8_t enumerationFailed;
	uint8_t refreshPending; /* a held refresh waits for a tick that allows it */
	uint16_t bindPort;
	uint16_t reserved2;
	uint32_t targetCount;
	uint32_t tickCount;
	uint32_t sendFailures;  /* beacon sends the socket refused */
	uint32_t receiveErrors; /* drains ended by a receive error */
	uint32_t refreshCount;  /* interface-list reads since Open, Open's included */
	uint32_t beaconCount;   /* beacon rounds since Open (sent or refused); a tick with no target sends none */
	/* The lan (DISC-19); all zero without one. */
	uint8_t lanPrefixLength;
	uint8_t reserved3[3];
	uint32_t lanNetwork;
	uint32_t lanInterfaceIpv4; /* 0: no interface inside the lan (always 0 with explicit targets) */
	uint32_t lanDropped;       /* datagrams dropped for a source outside the lan */
};

/*
 * Closes the service if it is open, then opens it: binds INADDR_ANY:bindPort,
 * turns broadcast on, and initialises the core table with our nonce, group
 * hash, identity digest, link port, and seat preference
 * (NATIVE_ARCADE_DISCOVERY_SEAT_*). overrideCount 1..4 entries of
 * overrideTargets (ipv4 and port nonzero) become the only targets; with
 * overrideCount 0 the targets come from the interface list. lanPrefixLength
 * 0 means no lan; otherwise (lanNetwork, lanPrefixLength) must be a lan
 * NativeArcadeDiscovery_LanValid accepts, and every override target inside
 * it (DISC-19).
 * Returns 1 when open. Returns 0 with the service closed for a NULL service,
 * bindPort 0, a bad override list, a bad lan or an override outside it,
 * arguments the core's Init rejects, or when the socket cannot be opened,
 * bound (port in use), or set to broadcast.
 */
int NativeArcadeDiscoveryService_Open(struct NativeArcadeDiscoveryService *service, uint16_t bindPort, uint64_t ourNonce, uint64_t groupHash,
                                      const uint8_t identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES], uint16_t ourLinkPort, uint8_t ourSeatPreference,
                                      const struct NativeUdpTransportAddress *overrideTargets, uint32_t overrideCount, uint32_t lanNetwork,
                                      uint8_t lanPrefixLength);

/* One 30 Hz tick, as described above. mayRefresh 0 holds the interface
 * refresh; the drain, the core's tick, and the beacon cadence are
 * unchanged. A refresh due on this tick, or pending from an earlier held
 * one, runs on the first tick with mayRefresh nonzero, once however many
 * fell due meanwhile. No-op on NULL or a closed service. */
void NativeArcadeDiscoveryService_Tick(struct NativeArcadeDiscoveryService *service, int mayRefresh);

/* NativeArcadeDiscovery_Pairing on the table; 0 (out untouched) when closed. */
int NativeArcadeDiscoveryService_Pairing(const struct NativeArcadeDiscoveryService *service, struct NativeArcadeDiscoveryPairing *out);

/* NativeArcadeDiscovery_TakeEvent on the table; 0 when closed. */
int NativeArcadeDiscoveryService_TakeEvent(struct NativeArcadeDiscoveryService *service, struct NativeArcadeDiscoveryEvent *out);

/*
 * The lan interface for the caller's log (DISC-19): returns 1 once for each
 * interface-list read that found a different interface inside the lan than
 * the read before it (the first read, at Open, always counts), and writes
 * that interface's IPv4, or 0 when none is inside the lan (nothing is then
 * sent). Returns 0 (*interfaceIpv4 untouched) when closed, without a lan,
 * with explicit targets (no list is read), with nothing new, or for NULL
 * arguments.
 */
int NativeArcadeDiscoveryService_TakeLanChange(struct NativeArcadeDiscoveryService *service, uint32_t *interfaceIpv4);

/* Writes the status (all zero when closed). Returns 0 for NULL arguments. */
int NativeArcadeDiscoveryService_GetStatus(const struct NativeArcadeDiscoveryService *service, struct NativeArcadeDiscoveryServiceStatus *out);

/* Writes target index (below the status targetCount). Returns 0 otherwise. */
int NativeArcadeDiscoveryService_Target(const struct NativeArcadeDiscoveryService *service, uint32_t index, struct NativeUdpTransportAddress *out);

/* Closes the socket and clears the state. Safe on NULL, twice, and on a
 * zero-initialised service. */
void NativeArcadeDiscoveryService_Close(struct NativeArcadeDiscoveryService *service);

#endif
