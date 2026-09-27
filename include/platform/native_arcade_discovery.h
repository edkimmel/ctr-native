#ifndef PLATFORM_NATIVE_ARCADE_DISCOVERY_H
#define PLATFORM_NATIVE_ARCADE_DISCOVERY_H

#include <stddef.h>
#include <stdint.h>

/*
 * Arcade-link peer discovery, pure core (docs/DISCOVERY_MILESTONE.md DISC-S2).
 *
 * - The beacon codec (DISC-3): one 96-byte little-endian datagram, packed and
 *   checked field by field (never a struct copy to or from the wire).
 * - The group name check and its 64-bit FNV-1a hash (DISC-9).
 * - The seat election (DISC-8).
 * - A caller-owned, tick-driven peer table of 8 entries (DISC-4, DISC-6,
 *   DISC-7) with a small queue of log events (DISC-16).
 *
 * Pure: caller-owned state, no heap, no clock, no I/O, no hidden state. The
 * only time is the table's own tick count, advanced by
 * NativeArcadeDiscovery_Tick once per 30 Hz host tick. IPv4 values are
 * host-order (192.168.1.11 is 0xC0A8010B), as struct
 * NativeUdpTransportAddress; ports are host-order.
 *
 * Nothing here reaches simulation identity or any deterministic state
 * (DISC-14): only a peer address and an elected seat leave the table.
 */

#define NATIVE_ARCADE_DISCOVERY_BEACON_BYTES          96u
#define NATIVE_ARCADE_DISCOVERY_VERSION               1u
#define NATIVE_ARCADE_DISCOVERY_MAX_ECHO              3u
#define NATIVE_ARCADE_DISCOVERY_TABLE_SIZE            8u
#define NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES        8u
#define NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS 30u
#define NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS          300u
#define NATIVE_ARCADE_DISCOVERY_DEFAULT_GROUP         "ctr-native"
#define NATIVE_ARCADE_DISCOVERY_GROUP_MAX_CHARS       32u
#define NATIVE_ARCADE_DISCOVERY_FNV_OFFSET_BASIS      UINT64_C(0xcbf29ce484222325)
#define NATIVE_ARCADE_DISCOVERY_FNV_PRIME             UINT64_C(0x100000001b3)
#define NATIVE_ARCADE_DISCOVERY_EVENT_QUEUE_SIZE      16u
#define NATIVE_ARCADE_DISCOVERY_REPORTED_NONCES       16u

/* Seat preference (beacon offset 34) and elected seat. */
#define NATIVE_ARCADE_DISCOVERY_SEAT_AUTO 0u
#define NATIVE_ARCADE_DISCOVERY_SEAT_CAB1 1u
#define NATIVE_ARCADE_DISCOVERY_SEAT_CAB2 2u

/* One echo entry: a peer the sender hears (beacon offsets 40, 56, 72). */
struct NativeArcadeDiscoveryEcho
{
	uint64_t nonce;    /* the peer's instance nonce */
	uint32_t ipv4;     /* the source IPv4 of that peer's beacons, as observed */
	uint16_t linkPort; /* that peer's link port, as it advertises it */
};

/* A decoded beacon. Echo entries at and beyond echoCount are all zero. */
struct NativeArcadeDiscoveryBeacon
{
	uint64_t nonce;
	uint64_t groupHash;
	uint8_t identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES]; /* verbatim bytes */
	uint16_t linkPort;
	uint8_t seatPreference; /* NATIVE_ARCADE_DISCOVERY_SEAT_* */
	uint8_t echoCount;
	struct NativeArcadeDiscoveryEcho echo[NATIVE_ARCADE_DISCOVERY_MAX_ECHO];
};

/* NativeArcadeDiscovery_Decode results; one per DISC-3 reject rule. */
enum NativeArcadeDiscoveryDecodeResult
{
	NATIVE_ARCADE_DISCOVERY_DECODE_OK = 0,
	NATIVE_ARCADE_DISCOVERY_DECODE_ARGUMENT = 1,        /* NULL bytes or out */
	NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH = 2,          /* datagram size is not 96 */
	NATIVE_ARCADE_DISCOVERY_DECODE_MAGIC = 3,           /* not "CTRD" */
	NATIVE_ARCADE_DISCOVERY_DECODE_VERSION = 4,         /* not 1 */
	NATIVE_ARCADE_DISCOVERY_DECODE_SIZE_FIELD = 5,      /* size field is not 96 */
	NATIVE_ARCADE_DISCOVERY_DECODE_NONCE = 6,           /* nonce 0 */
	NATIVE_ARCADE_DISCOVERY_DECODE_LINK_PORT = 7,       /* link port 0 */
	NATIVE_ARCADE_DISCOVERY_DECODE_SEAT_PREFERENCE = 8, /* above 2 */
	NATIVE_ARCADE_DISCOVERY_DECODE_FLAGS = 9,           /* nonzero flags */
	NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_COUNT = 10,     /* above 3 */
	NATIVE_ARCADE_DISCOVERY_DECODE_RESERVED = 11,       /* nonzero header or trailing reserved byte */
	NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY = 12,     /* used entry: nonce, IPv4, or port 0, or reserved nonzero */
	NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_UNUSED = 13     /* unused entry not all zero */
};

/*
 * Writes the 96-byte beacon. Returns 1, or 0 (out untouched) for a NULL
 * argument or a beacon Decode would reject (nonce 0, link port 0, seat
 * preference above 2, echo count above 3, a used echo entry with nonce,
 * IPv4, or port 0). Unused echo entries are written as zero whatever the
 * struct holds.
 */
int NativeArcadeDiscovery_Encode(const struct NativeArcadeDiscoveryBeacon *beacon, uint8_t out[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES]);

/*
 * Decodes one datagram of size bytes. Returns a
 * NATIVE_ARCADE_DISCOVERY_DECODE_* value; *out is written only on OK.
 */
int NativeArcadeDiscovery_Decode(const uint8_t *bytes, size_t size, struct NativeArcadeDiscoveryBeacon *out);

/*
 * The group name rule (DISC-9): 1..32 characters of [A-Za-z0-9._-], NUL
 * terminated, not starting with '-'. Returns 1 when name is valid, else 0
 * (NULL included).
 */
int NativeArcadeDiscovery_GroupNameValid(const char *name);

/*
 * 64-bit FNV-1a over the name's bytes, no terminator (offset basis
 * 0xcbf29ce484222325, prime 0x100000001b3). NULL hashes as the empty string.
 * Does not check the name; call GroupNameValid first.
 */
uint64_t NativeArcadeDiscovery_GroupHash(const char *name);

/*
 * The lan (DISC-19): the IPv4 subnet an operator pins discovery to, as
 * "a.b.c.d/n". Host-local link configuration only: it is never in the
 * beacon, the group hash, or any deterministic state.
 *
 * NativeArcadeDiscovery_ParseLan is the one lan grammar (the options parser
 * and, through it, the config file use it): four decimal octets 0..255 of
 * 1-3 digits separated by '.', one '/', and a decimal prefix length of 1-2
 * digits in NATIVE_ARCADE_DISCOVERY_LAN_MIN_PREFIX..MAX_PREFIX (8..30: a /31
 * or /32 has no directed broadcast, and a subnet wider than /8 is not a
 * LAN), with nothing else (no whitespace, no sign), and the host bits zero
 * (192.168.1.5/24 is refused as ambiguous). Returns 1 and writes the
 * network (host order) and the prefix length, else 0 with both untouched
 * (NULL arguments included).
 */
#define NATIVE_ARCADE_DISCOVERY_LAN_MIN_PREFIX 8u
#define NATIVE_ARCADE_DISCOVERY_LAN_MAX_PREFIX 30u
int NativeArcadeDiscovery_ParseLan(const char *text, uint32_t *network, uint8_t *prefixLength);

/* 1 when (network, prefixLength) is a lan ParseLan accepts: the prefix length
 * in 8..30 and the host bits of network zero; else 0. */
int NativeArcadeDiscovery_LanValid(uint32_t network, uint8_t prefixLength);

/* 1 when ipv4 lies inside a valid lan ((ipv4 & mask) == network); 0 when it
 * does not or the lan is not valid. */
int NativeArcadeDiscovery_LanContains(uint32_t network, uint8_t prefixLength, uint32_t ipv4);

/* The lan's directed broadcast, network | ~mask (192.168.1.0/24 gives
 * 192.168.1.255); 0 when the lan is not valid. */
uint32_t NativeArcadeDiscovery_LanBroadcast(uint32_t network, uint8_t prefixLength);

/*
 * One side of the election (DISC-8). The election key is (ipv4, linkPort)
 * as the OTHER side observes it: for ourselves, the peer's echo of our
 * nonce; for the peer, its beacon source IPv4 and advertised link port.
 */
struct NativeArcadeDiscoveryParty
{
	uint32_t ipv4;
	uint16_t linkPort;
	uint64_t nonce;
	uint8_t seatPreference; /* NATIVE_ARCADE_DISCOVERY_SEAT_* */
};

enum NativeArcadeDiscoveryElectResult
{
	NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR = 0,       /* equal nonces, or a preference above 2 */
	NATIVE_ARCADE_DISCOVERY_ELECT_CAB1 = 1,          /* we are cab1 */
	NATIVE_ARCADE_DISCOVERY_ELECT_CAB2 = 2,          /* we are cab2 */
	NATIVE_ARCADE_DISCOVERY_ELECT_SEAT_CONFLICT = 3  /* equal nonzero preferences */
};

/*
 * Our seat against one peer, a pure function of the pair, so evaluating it
 * from the peer's side (arguments swapped) gives the other seat:
 * 1. equal nonces: NO_PAIR;
 * 2. preferences: both set and equal: SEAT_CONFLICT; ours set: ours; only
 *    the peer's set: the other seat;
 * 3. both auto: the lower key (IPv4, then link port, unsigned) is cab1;
 *    equal keys: the lower nonce is cab1.
 * NULL arguments give NO_PAIR.
 */
int NativeArcadeDiscovery_Elect(const struct NativeArcadeDiscoveryParty *ours, const struct NativeArcadeDiscoveryParty *peer);

/* One peer entry, keyed by nonce (DISC-6). */
struct NativeArcadeDiscoveryPeer
{
	uint8_t used;
	uint32_t ipv4;          /* the first source IPv4 heard for this nonce */
	uint16_t linkPort;      /* the first advertised link port heard */
	uint32_t lastHeardTick; /* the table tick of its latest beacon */
	struct NativeArcadeDiscoveryBeacon beacon; /* its latest beacon */
};

/* The current pairing (DISC-7, DISC-8). */
struct NativeArcadeDiscoveryPairing
{
	uint64_t peerNonce;
	uint32_t peerIpv4;     /* the source IPv4 of the peer's beacons */
	uint16_t peerLinkPort; /* the link port the peer advertises */
	uint8_t localSeat;     /* NATIVE_ARCADE_DISCOVERY_SEAT_CAB1 or _CAB2 */
};

enum NativeArcadeDiscoveryEventType
{
	NATIVE_ARCADE_DISCOVERY_EVENT_NONE = 0,
	NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND = 1,        /* peer fields and localSeat of the new pairing */
	NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST = 2,         /* peer fields and localSeat of the old pairing */
	NATIVE_ARCADE_DISCOVERY_EVENT_GROUP_MISMATCH = 3,    /* peer fields; localSeat 0 */
	NATIVE_ARCADE_DISCOVERY_EVENT_IDENTITY_MISMATCH = 4, /* peer fields; localSeat 0 */
	NATIVE_ARCADE_DISCOVERY_EVENT_SEAT_CONFLICT = 5      /* peer fields; localSeat 0 */
};

struct NativeArcadeDiscoveryEvent
{
	uint8_t type; /* NATIVE_ARCADE_DISCOVERY_EVENT_* */
	uint8_t localSeat;
	uint16_t peerLinkPort;
	uint32_t peerIpv4;
	uint64_t peerNonce;
};

/*
 * The caller-owned table. Treat as opaque; fields are public only so the
 * caller can own the storage (and tests can inspect it).
 */
struct NativeArcadeDiscoveryTable
{
	uint64_t ourNonce;
	uint64_t groupHash;
	uint8_t identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES];
	uint16_t ourLinkPort;
	uint8_t ourSeatPreference;
	uint8_t paired;
	uint32_t tick;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryPeer peers[NATIVE_ARCADE_DISCOVERY_TABLE_SIZE];
	/* Nonces already reported for a mismatch or conflict (a ring). */
	uint64_t reportedNonce[NATIVE_ARCADE_DISCOVERY_REPORTED_NONCES];
	uint8_t reportedKinds[NATIVE_ARCADE_DISCOVERY_REPORTED_NONCES];
	uint32_t reportedNext;
	/* The pending log events (a FIFO). */
	struct NativeArcadeDiscoveryEvent events[NATIVE_ARCADE_DISCOVERY_EVENT_QUEUE_SIZE];
	uint32_t eventHead;
	uint32_t eventCount;
	uint32_t eventsDropped; /* events that found the queue full */
};

/*
 * Resets the table: tick 0, no peers, no pairing, no events. Returns 1, or
 * 0 for a NULL table or identity, nonce 0, link port 0, or a seat
 * preference above 2; a table whose Init failed ignores every beacon and
 * builds none.
 */
int NativeArcadeDiscovery_Init(struct NativeArcadeDiscoveryTable *table, uint64_t ourNonce, uint64_t groupHash,
                               const uint8_t identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES], uint16_t ourLinkPort, uint8_t ourSeatPreference);

enum NativeArcadeDiscoveryReceiveResult
{
	NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED = 0,     /* a new entry (or a restart replacement) */
	NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED = 1, /* an existing nonce */
	NATIVE_ARCADE_DISCOVERY_RECEIVE_OWN = 2,       /* our own nonce: ignored */
	NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED = 3,  /* Decode failed, source IPv4 0, or no Init */
	NATIVE_ARCADE_DISCOVERY_RECEIVE_FULL = 4       /* a new nonce, table full: dropped, unlogged */
};

/*
 * Feeds one received datagram (decoded here) with its source IPv4, at the
 * current tick. Beacons carrying our nonce are ignored. An entry is keyed by
 * nonce and keeps the first source IPv4 and link port heard until it
 * expires; later beacons of that nonce refresh its beacon and its tick. A
 * new nonce whose source IPv4 and link port equal an existing entry's
 * replaces that entry at once (the peer restarted). Otherwise a new nonce
 * takes a free entry, or is dropped unlogged when all 8 are used.
 * A different group hash queues GROUP_MISMATCH, and (same group) a
 * different identity queues IDENTITY_MISMATCH, each once per nonce.
 * The pairing is then re-evaluated. Returns a
 * NATIVE_ARCADE_DISCOVERY_RECEIVE_* value.
 */
int NativeArcadeDiscovery_Receive(struct NativeArcadeDiscoveryTable *table, const uint8_t *bytes, size_t size, uint32_t sourceIpv4);

/*
 * Advances the table one 30 Hz tick, then expires entries and re-evaluates
 * the pairing. Expiry rule: an entry whose latest beacon was received when
 * the tick count was T is removed by the Tick call that brings the count to
 * T + 300 (NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS); after 299 Tick calls it
 * is still live. The count starts at 0 at Init and wraps modulo 2^32.
 */
void NativeArcadeDiscovery_Tick(struct NativeArcadeDiscoveryTable *table);

/*
 * Writes our next beacon: our nonce, group hash, identity, link port, and
 * seat preference, echoing the live peers of our group and identity, lowest
 * election key (IPv4, then link port, then nonce) first, at most 3. Returns
 * 1, or 0 (out untouched) for a NULL argument or a table whose Init failed.
 */
int NativeArcadeDiscovery_BuildBeacon(const struct NativeArcadeDiscoveryTable *table, uint8_t out[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES]);

/*
 * The current pairing. A peer is eligible (DISC-7) when its entry is live,
 * its group hash and identity equal ours, its latest beacon echoes our
 * nonce, and the election against it gives cab1 or cab2 (an equal-preference
 * peer queues SEAT_CONFLICT once per nonce). Among eligible peers the
 * lowest peer election key (IPv4, then link port, then nonce) is chosen.
 * Returns 1 and writes *out when paired, else 0 (*out untouched).
 */
int NativeArcadeDiscovery_Pairing(const struct NativeArcadeDiscoveryTable *table, struct NativeArcadeDiscoveryPairing *out);

/*
 * Takes the oldest pending log event. A pairing that ends queues PAIR_LOST;
 * one that starts queues PAIR_FOUND; a change of peer or seat queues both,
 * LOST first. Returns 1 and writes *out, or 0 when the queue is empty. When
 * the queue is full a new event is dropped and eventsDropped counts it.
 */
int NativeArcadeDiscovery_TakeEvent(struct NativeArcadeDiscoveryTable *table, struct NativeArcadeDiscoveryEvent *out);

#endif
