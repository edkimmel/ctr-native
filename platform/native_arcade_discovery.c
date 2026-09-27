#include "platform/native_arcade_discovery.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Beacon layout (DISC-3), byte offsets. */
#define DISCOVERY_OFFSET_MAGIC     0u
#define DISCOVERY_OFFSET_VERSION   4u
#define DISCOVERY_OFFSET_SIZE      6u
#define DISCOVERY_OFFSET_NONCE     8u
#define DISCOVERY_OFFSET_GROUP     16u
#define DISCOVERY_OFFSET_IDENTITY  24u
#define DISCOVERY_OFFSET_LINK_PORT 32u
#define DISCOVERY_OFFSET_SEAT      34u
#define DISCOVERY_OFFSET_FLAGS     35u
#define DISCOVERY_OFFSET_ECHOES    36u
#define DISCOVERY_OFFSET_RESERVED  37u /* 3 bytes */
#define DISCOVERY_OFFSET_ECHO      40u /* 3 entries of 16 bytes */
#define DISCOVERY_OFFSET_TRAILER   88u /* 8 bytes */
#define DISCOVERY_ECHO_BYTES       16u
#define DISCOVERY_ECHO_IPV4        8u
#define DISCOVERY_ECHO_PORT        12u
#define DISCOVERY_ECHO_RESERVED    14u

/* Kinds remembered per reported nonce (DISC-16). */
#define DISCOVERY_REPORTED_GROUP    1u
#define DISCOVERY_REPORTED_IDENTITY 2u
#define DISCOVERY_REPORTED_SEAT     4u

static const uint8_t s_magic[4] = {'C', 'T', 'R', 'D'};

static void PutU16(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)(value & 0xFFu);
	out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void PutU32(uint8_t *out, uint32_t value)
{
	uint32_t i;

	for (i = 0u; i < 4u; ++i)
	{
		out[i] = (uint8_t)((value >> (8u * i)) & 0xFFu);
	}
}

static void PutU64(uint8_t *out, uint64_t value)
{
	uint32_t i;

	for (i = 0u; i < 8u; ++i)
	{
		out[i] = (uint8_t)((value >> (8u * i)) & 0xFFu);
	}
}

static uint16_t GetU16(const uint8_t *in)
{
	return (uint16_t)((uint32_t)in[0] | ((uint32_t)in[1] << 8));
}

static uint32_t GetU32(const uint8_t *in)
{
	uint32_t value = 0u;
	uint32_t i;

	for (i = 0u; i < 4u; ++i)
	{
		value |= (uint32_t)in[i] << (8u * i);
	}
	return value;
}

static uint64_t GetU64(const uint8_t *in)
{
	uint64_t value = 0u;
	uint32_t i;

	for (i = 0u; i < 8u; ++i)
	{
		value |= (uint64_t)in[i] << (8u * i);
	}
	return value;
}

static int AllZero(const uint8_t *bytes, size_t size)
{
	size_t i;

	for (i = 0u; i < size; ++i)
	{
		if (bytes[i] != 0u)
		{
			return 0;
		}
	}
	return 1;
}

int NativeArcadeDiscovery_Encode(const struct NativeArcadeDiscoveryBeacon *beacon, uint8_t out[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES])
{
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];
	uint32_t i;

	if ((beacon == NULL) || (out == NULL))
	{
		return 0;
	}
	if ((beacon->nonce == 0u) || (beacon->linkPort == 0u) || (beacon->seatPreference > NATIVE_ARCADE_DISCOVERY_SEAT_CAB2) ||
	    (beacon->echoCount > NATIVE_ARCADE_DISCOVERY_MAX_ECHO))
	{
		return 0;
	}
	for (i = 0u; i < beacon->echoCount; ++i)
	{
		if ((beacon->echo[i].nonce == 0u) || (beacon->echo[i].ipv4 == 0u) || (beacon->echo[i].linkPort == 0u))
		{
			return 0;
		}
	}

	memset(bytes, 0, sizeof(bytes));
	memcpy(&bytes[DISCOVERY_OFFSET_MAGIC], s_magic, sizeof(s_magic));
	PutU16(&bytes[DISCOVERY_OFFSET_VERSION], (uint16_t)NATIVE_ARCADE_DISCOVERY_VERSION);
	PutU16(&bytes[DISCOVERY_OFFSET_SIZE], (uint16_t)NATIVE_ARCADE_DISCOVERY_BEACON_BYTES);
	PutU64(&bytes[DISCOVERY_OFFSET_NONCE], beacon->nonce);
	PutU64(&bytes[DISCOVERY_OFFSET_GROUP], beacon->groupHash);
	memcpy(&bytes[DISCOVERY_OFFSET_IDENTITY], beacon->identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES);
	PutU16(&bytes[DISCOVERY_OFFSET_LINK_PORT], beacon->linkPort);
	bytes[DISCOVERY_OFFSET_SEAT] = beacon->seatPreference;
	bytes[DISCOVERY_OFFSET_FLAGS] = 0u;
	bytes[DISCOVERY_OFFSET_ECHOES] = beacon->echoCount;
	for (i = 0u; i < beacon->echoCount; ++i)
	{
		uint8_t *entry = &bytes[DISCOVERY_OFFSET_ECHO + (i * DISCOVERY_ECHO_BYTES)];

		PutU64(entry, beacon->echo[i].nonce);
		PutU32(&entry[DISCOVERY_ECHO_IPV4], beacon->echo[i].ipv4);
		PutU16(&entry[DISCOVERY_ECHO_PORT], beacon->echo[i].linkPort);
	}
	memcpy(out, bytes, sizeof(bytes));
	return 1;
}

int NativeArcadeDiscovery_Decode(const uint8_t *bytes, size_t size, struct NativeArcadeDiscoveryBeacon *out)
{
	struct NativeArcadeDiscoveryBeacon beacon;
	uint32_t i;

	if ((bytes == NULL) || (out == NULL))
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_ARGUMENT;
	}
	if (size != NATIVE_ARCADE_DISCOVERY_BEACON_BYTES)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH;
	}
	if (memcmp(&bytes[DISCOVERY_OFFSET_MAGIC], s_magic, sizeof(s_magic)) != 0)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_MAGIC;
	}
	if (GetU16(&bytes[DISCOVERY_OFFSET_VERSION]) != NATIVE_ARCADE_DISCOVERY_VERSION)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_VERSION;
	}
	if (GetU16(&bytes[DISCOVERY_OFFSET_SIZE]) != NATIVE_ARCADE_DISCOVERY_BEACON_BYTES)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_SIZE_FIELD;
	}

	memset(&beacon, 0, sizeof(beacon));
	beacon.nonce = GetU64(&bytes[DISCOVERY_OFFSET_NONCE]);
	beacon.groupHash = GetU64(&bytes[DISCOVERY_OFFSET_GROUP]);
	memcpy(beacon.identity, &bytes[DISCOVERY_OFFSET_IDENTITY], NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES);
	beacon.linkPort = GetU16(&bytes[DISCOVERY_OFFSET_LINK_PORT]);
	beacon.seatPreference = bytes[DISCOVERY_OFFSET_SEAT];
	beacon.echoCount = bytes[DISCOVERY_OFFSET_ECHOES];

	if (beacon.nonce == 0u)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_NONCE;
	}
	if (beacon.linkPort == 0u)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_LINK_PORT;
	}
	if (beacon.seatPreference > NATIVE_ARCADE_DISCOVERY_SEAT_CAB2)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_SEAT_PREFERENCE;
	}
	if (bytes[DISCOVERY_OFFSET_FLAGS] != 0u)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_FLAGS;
	}
	if (beacon.echoCount > NATIVE_ARCADE_DISCOVERY_MAX_ECHO)
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_COUNT;
	}
	if (!AllZero(&bytes[DISCOVERY_OFFSET_RESERVED], 3u) || !AllZero(&bytes[DISCOVERY_OFFSET_TRAILER], 8u))
	{
		return NATIVE_ARCADE_DISCOVERY_DECODE_RESERVED;
	}
	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_MAX_ECHO; ++i)
	{
		const uint8_t *entry = &bytes[DISCOVERY_OFFSET_ECHO + (i * DISCOVERY_ECHO_BYTES)];

		if (i >= beacon.echoCount)
		{
			if (!AllZero(entry, DISCOVERY_ECHO_BYTES))
			{
				return NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_UNUSED;
			}
			continue;
		}
		beacon.echo[i].nonce = GetU64(entry);
		beacon.echo[i].ipv4 = GetU32(&entry[DISCOVERY_ECHO_IPV4]);
		beacon.echo[i].linkPort = GetU16(&entry[DISCOVERY_ECHO_PORT]);
		if ((beacon.echo[i].nonce == 0u) || (beacon.echo[i].ipv4 == 0u) || (beacon.echo[i].linkPort == 0u) ||
		    (GetU16(&entry[DISCOVERY_ECHO_RESERVED]) != 0u))
		{
			return NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY;
		}
	}
	*out = beacon;
	return NATIVE_ARCADE_DISCOVERY_DECODE_OK;
}

int NativeArcadeDiscovery_GroupNameValid(const char *name)
{
	size_t length;

	if ((name == NULL) || (name[0] == '-'))
	{
		return 0;
	}
	for (length = 0u; name[length] != '\0'; ++length)
	{
		const char c = name[length];

		if (length >= NATIVE_ARCADE_DISCOVERY_GROUP_MAX_CHARS)
		{
			return 0;
		}
		if (!(((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '.') || (c == '_') || (c == '-')))
		{
			return 0;
		}
	}
	return length > 0u;
}

uint64_t NativeArcadeDiscovery_GroupHash(const char *name)
{
	uint64_t hash = NATIVE_ARCADE_DISCOVERY_FNV_OFFSET_BASIS;
	size_t i;

	if (name == NULL)
	{
		return hash;
	}
	for (i = 0u; name[i] != '\0'; ++i)
	{
		hash ^= (uint64_t)(unsigned char)name[i];
		hash *= NATIVE_ARCADE_DISCOVERY_FNV_PRIME;
	}
	return hash;
}

/* The lan's subnet mask; valid only for a prefix length of 1..31. */
static uint32_t LanMask(uint8_t prefixLength)
{
	return UINT32_C(0xFFFFFFFF) << (32u - (uint32_t)prefixLength);
}

/* 1..maxDigits decimal digits at *cursor, no greater than maxValue; advances
 * *cursor past them. */
static int ParseLanDecimal(const char **cursor, uint32_t maxDigits, uint32_t maxValue, uint32_t *value)
{
	const char *text = *cursor;
	uint32_t digits = 0u;
	uint32_t result = 0u;

	while ((text[digits] >= '0') && (text[digits] <= '9'))
	{
		if (digits == maxDigits)
		{
			return 0;
		}
		result = (result * 10u) + (uint32_t)(text[digits] - '0');
		digits++;
	}
	if ((digits == 0u) || (result > maxValue))
	{
		return 0;
	}
	*cursor = text + digits;
	*value = result;
	return 1;
}

int NativeArcadeDiscovery_ParseLan(const char *text, uint32_t *network, uint8_t *prefixLength)
{
	const char *cursor = text;
	uint32_t address = 0u;
	uint32_t prefix = 0u;

	if ((text == NULL) || (network == NULL) || (prefixLength == NULL))
	{
		return 0;
	}
	for (uint32_t octetIndex = 0u; octetIndex < 4u; octetIndex++)
	{
		uint32_t octet = 0u;

		if (!ParseLanDecimal(&cursor, 3u, 255u, &octet))
		{
			return 0;
		}
		address = (address << 8) | octet;
		if (*cursor != ((octetIndex < 3u) ? '.' : '/'))
		{
			return 0;
		}
		cursor++;
	}
	if (!ParseLanDecimal(&cursor, 2u, NATIVE_ARCADE_DISCOVERY_LAN_MAX_PREFIX, &prefix) || (*cursor != '\0') ||
	    !NativeArcadeDiscovery_LanValid(address, (uint8_t)prefix))
	{
		return 0;
	}
	*network = address;
	*prefixLength = (uint8_t)prefix;
	return 1;
}

int NativeArcadeDiscovery_LanValid(uint32_t network, uint8_t prefixLength)
{
	if ((prefixLength < NATIVE_ARCADE_DISCOVERY_LAN_MIN_PREFIX) || (prefixLength > NATIVE_ARCADE_DISCOVERY_LAN_MAX_PREFIX))
	{
		return 0;
	}
	return (network & ~LanMask(prefixLength)) == 0u;
}

int NativeArcadeDiscovery_LanContains(uint32_t network, uint8_t prefixLength, uint32_t ipv4)
{
	if (!NativeArcadeDiscovery_LanValid(network, prefixLength))
	{
		return 0;
	}
	return (ipv4 & LanMask(prefixLength)) == network;
}

uint32_t NativeArcadeDiscovery_LanBroadcast(uint32_t network, uint8_t prefixLength)
{
	if (!NativeArcadeDiscovery_LanValid(network, prefixLength))
	{
		return 0u;
	}
	return network | ~LanMask(prefixLength);
}

/* 1 when key a (ipv4, port, nonce) is lower than key b, unsigned. */
static int KeyLess(uint32_t ipv4A, uint16_t portA, uint64_t nonceA, uint32_t ipv4B, uint16_t portB, uint64_t nonceB)
{
	if (ipv4A != ipv4B)
	{
		return ipv4A < ipv4B;
	}
	if (portA != portB)
	{
		return portA < portB;
	}
	return nonceA < nonceB;
}

int NativeArcadeDiscovery_Elect(const struct NativeArcadeDiscoveryParty *ours, const struct NativeArcadeDiscoveryParty *peer)
{
	if ((ours == NULL) || (peer == NULL) || (ours->nonce == peer->nonce) || (ours->seatPreference > NATIVE_ARCADE_DISCOVERY_SEAT_CAB2) ||
	    (peer->seatPreference > NATIVE_ARCADE_DISCOVERY_SEAT_CAB2))
	{
		return NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR;
	}
	if (ours->seatPreference != NATIVE_ARCADE_DISCOVERY_SEAT_AUTO)
	{
		if (ours->seatPreference == peer->seatPreference)
		{
			return NATIVE_ARCADE_DISCOVERY_ELECT_SEAT_CONFLICT;
		}
		return (ours->seatPreference == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1) ? NATIVE_ARCADE_DISCOVERY_ELECT_CAB1 : NATIVE_ARCADE_DISCOVERY_ELECT_CAB2;
	}
	if (peer->seatPreference != NATIVE_ARCADE_DISCOVERY_SEAT_AUTO)
	{
		return (peer->seatPreference == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1) ? NATIVE_ARCADE_DISCOVERY_ELECT_CAB2 : NATIVE_ARCADE_DISCOVERY_ELECT_CAB1;
	}
	return KeyLess(ours->ipv4, ours->linkPort, ours->nonce, peer->ipv4, peer->linkPort, peer->nonce) ? NATIVE_ARCADE_DISCOVERY_ELECT_CAB1
	                                                                                                   : NATIVE_ARCADE_DISCOVERY_ELECT_CAB2;
}

static int TableReady(const struct NativeArcadeDiscoveryTable *table)
{
	return (table != NULL) && (table->ourNonce != 0u);
}

static void PushEvent(struct NativeArcadeDiscoveryTable *table, uint8_t type, uint64_t nonce, uint32_t ipv4, uint16_t linkPort, uint8_t localSeat)
{
	struct NativeArcadeDiscoveryEvent *event;

	if (table->eventCount >= NATIVE_ARCADE_DISCOVERY_EVENT_QUEUE_SIZE)
	{
		++table->eventsDropped;
		return;
	}
	event = &table->events[(table->eventHead + table->eventCount) % NATIVE_ARCADE_DISCOVERY_EVENT_QUEUE_SIZE];
	event->type = type;
	event->localSeat = localSeat;
	event->peerLinkPort = linkPort;
	event->peerIpv4 = ipv4;
	event->peerNonce = nonce;
	++table->eventCount;
}

/* Queues a mismatch or conflict event unless this nonce already had one of this kind. */
static void ReportOnce(struct NativeArcadeDiscoveryTable *table, const struct NativeArcadeDiscoveryPeer *peer, uint8_t kind, uint8_t type)
{
	const uint64_t nonce = peer->beacon.nonce;
	uint32_t i;
	uint32_t slot;

	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_REPORTED_NONCES; ++i)
	{
		if ((table->reportedKinds[i] != 0u) && (table->reportedNonce[i] == nonce))
		{
			if ((table->reportedKinds[i] & kind) != 0u)
			{
				return;
			}
			table->reportedKinds[i] = (uint8_t)(table->reportedKinds[i] | kind);
			PushEvent(table, type, nonce, peer->ipv4, peer->linkPort, 0u);
			return;
		}
	}
	/* Not yet reported: take the oldest ring slot. */
	slot = table->reportedNext % NATIVE_ARCADE_DISCOVERY_REPORTED_NONCES;
	table->reportedNonce[slot] = nonce;
	table->reportedKinds[slot] = kind;
	table->reportedNext = (slot + 1u) % NATIVE_ARCADE_DISCOVERY_REPORTED_NONCES;
	PushEvent(table, type, nonce, peer->ipv4, peer->linkPort, 0u);
}

/* Same group and same identity: the peers we echo and may pair with. */
static int SameGroupAndIdentity(const struct NativeArcadeDiscoveryTable *table, const struct NativeArcadeDiscoveryPeer *peer)
{
	return (peer->beacon.groupHash == table->groupHash) && (memcmp(peer->beacon.identity, table->identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES) == 0);
}

static const struct NativeArcadeDiscoveryEcho *FindEchoOfUs(const struct NativeArcadeDiscoveryTable *table, const struct NativeArcadeDiscoveryPeer *peer)
{
	uint32_t i;

	for (i = 0u; i < peer->beacon.echoCount; ++i)
	{
		if (peer->beacon.echo[i].nonce == table->ourNonce)
		{
			return &peer->beacon.echo[i];
		}
	}
	return NULL;
}

/* Re-evaluates the pairing (DISC-7, DISC-8) and queues PAIR_LOST / PAIR_FOUND on a change. */
static void Evaluate(struct NativeArcadeDiscoveryTable *table)
{
	struct NativeArcadeDiscoveryPairing best;
	int found = 0;
	int changed;
	uint32_t i;

	memset(&best, 0, sizeof(best));
	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; ++i)
	{
		const struct NativeArcadeDiscoveryPeer *peer = &table->peers[i];
		const struct NativeArcadeDiscoveryEcho *echo;
		struct NativeArcadeDiscoveryParty ours;
		struct NativeArcadeDiscoveryParty theirs;
		int seat;

		if ((peer->used == 0u) || !SameGroupAndIdentity(table, peer))
		{
			continue;
		}
		echo = FindEchoOfUs(table, peer);
		if (echo == NULL)
		{
			continue;
		}
		ours.ipv4 = echo->ipv4;
		ours.linkPort = echo->linkPort;
		ours.nonce = table->ourNonce;
		ours.seatPreference = table->ourSeatPreference;
		theirs.ipv4 = peer->ipv4;
		theirs.linkPort = peer->linkPort;
		theirs.nonce = peer->beacon.nonce;
		theirs.seatPreference = peer->beacon.seatPreference;
		seat = NativeArcadeDiscovery_Elect(&ours, &theirs);
		if (seat == NATIVE_ARCADE_DISCOVERY_ELECT_SEAT_CONFLICT)
		{
			ReportOnce(table, peer, DISCOVERY_REPORTED_SEAT, NATIVE_ARCADE_DISCOVERY_EVENT_SEAT_CONFLICT);
			continue;
		}
		if ((seat != NATIVE_ARCADE_DISCOVERY_ELECT_CAB1) && (seat != NATIVE_ARCADE_DISCOVERY_ELECT_CAB2))
		{
			continue;
		}
		if (!found || KeyLess(peer->ipv4, peer->linkPort, peer->beacon.nonce, best.peerIpv4, best.peerLinkPort, best.peerNonce))
		{
			best.peerNonce = peer->beacon.nonce;
			best.peerIpv4 = peer->ipv4;
			best.peerLinkPort = peer->linkPort;
			best.localSeat = (uint8_t)seat;
			found = 1;
		}
	}

	changed = (table->paired != 0u) != (found != 0);
	if (!changed && found)
	{
		changed = (best.peerNonce != table->pairing.peerNonce) || (best.peerIpv4 != table->pairing.peerIpv4) ||
		          (best.peerLinkPort != table->pairing.peerLinkPort) || (best.localSeat != table->pairing.localSeat);
	}
	if (!changed)
	{
		return;
	}
	if (table->paired != 0u)
	{
		PushEvent(table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST, table->pairing.peerNonce, table->pairing.peerIpv4, table->pairing.peerLinkPort,
		          table->pairing.localSeat);
	}
	if (found)
	{
		PushEvent(table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, best.peerNonce, best.peerIpv4, best.peerLinkPort, best.localSeat);
	}
	table->paired = (uint8_t)(found ? 1u : 0u);
	table->pairing = best;
}

int NativeArcadeDiscovery_Init(struct NativeArcadeDiscoveryTable *table, uint64_t ourNonce, uint64_t groupHash,
                               const uint8_t identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES], uint16_t ourLinkPort, uint8_t ourSeatPreference)
{
	if (table == NULL)
	{
		return 0;
	}
	memset(table, 0, sizeof(*table));
	if ((identity == NULL) || (ourNonce == 0u) || (ourLinkPort == 0u) || (ourSeatPreference > NATIVE_ARCADE_DISCOVERY_SEAT_CAB2))
	{
		return 0;
	}
	table->ourNonce = ourNonce;
	table->groupHash = groupHash;
	memcpy(table->identity, identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES);
	table->ourLinkPort = ourLinkPort;
	table->ourSeatPreference = ourSeatPreference;
	return 1;
}

int NativeArcadeDiscovery_Receive(struct NativeArcadeDiscoveryTable *table, const uint8_t *bytes, size_t size, uint32_t sourceIpv4)
{
	struct NativeArcadeDiscoveryBeacon beacon;
	struct NativeArcadeDiscoveryPeer *peer = NULL;
	struct NativeArcadeDiscoveryPeer *sameAddress = NULL;
	struct NativeArcadeDiscoveryPeer *freeEntry = NULL;
	int result;
	uint32_t i;

	if (!TableReady(table) || (sourceIpv4 == 0u))
	{
		return NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED;
	}
	if (NativeArcadeDiscovery_Decode(bytes, size, &beacon) != NATIVE_ARCADE_DISCOVERY_DECODE_OK)
	{
		return NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED;
	}
	if (beacon.nonce == table->ourNonce)
	{
		return NATIVE_ARCADE_DISCOVERY_RECEIVE_OWN;
	}

	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; ++i)
	{
		struct NativeArcadeDiscoveryPeer *entry = &table->peers[i];

		if (entry->used == 0u)
		{
			if (freeEntry == NULL)
			{
				freeEntry = entry;
			}
			continue;
		}
		if (entry->beacon.nonce == beacon.nonce)
		{
			peer = entry;
			break;
		}
		if ((sameAddress == NULL) && (entry->ipv4 == sourceIpv4) && (entry->linkPort == beacon.linkPort))
		{
			sameAddress = entry;
		}
	}

	if (peer != NULL)
	{
		/* Known nonce: refresh; the first address stays (DISC-6). */
		peer->beacon = beacon;
		peer->lastHeardTick = table->tick;
		result = NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED;
	}
	else
	{
		/* A new nonce at a known address is a restart: replace at once. */
		peer = (sameAddress != NULL) ? sameAddress : freeEntry;
		if (peer == NULL)
		{
			return NATIVE_ARCADE_DISCOVERY_RECEIVE_FULL;
		}
		memset(peer, 0, sizeof(*peer));
		peer->used = 1u;
		peer->ipv4 = sourceIpv4;
		peer->linkPort = beacon.linkPort;
		peer->lastHeardTick = table->tick;
		peer->beacon = beacon;
		result = NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED;
	}

	if (peer->beacon.groupHash != table->groupHash)
	{
		ReportOnce(table, peer, DISCOVERY_REPORTED_GROUP, NATIVE_ARCADE_DISCOVERY_EVENT_GROUP_MISMATCH);
	}
	else if (memcmp(peer->beacon.identity, table->identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES) != 0)
	{
		ReportOnce(table, peer, DISCOVERY_REPORTED_IDENTITY, NATIVE_ARCADE_DISCOVERY_EVENT_IDENTITY_MISMATCH);
	}
	Evaluate(table);
	return result;
}

void NativeArcadeDiscovery_Tick(struct NativeArcadeDiscoveryTable *table)
{
	uint32_t i;

	if (!TableReady(table))
	{
		return;
	}
	++table->tick;
	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; ++i)
	{
		struct NativeArcadeDiscoveryPeer *peer = &table->peers[i];

		if ((peer->used != 0u) && ((uint32_t)(table->tick - peer->lastHeardTick) >= NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS))
		{
			memset(peer, 0, sizeof(*peer));
		}
	}
	Evaluate(table);
}

int NativeArcadeDiscovery_BuildBeacon(const struct NativeArcadeDiscoveryTable *table, uint8_t out[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES])
{
	struct NativeArcadeDiscoveryBeacon beacon;
	uint8_t chosen[NATIVE_ARCADE_DISCOVERY_TABLE_SIZE];
	uint32_t i;

	if (!TableReady(table) || (out == NULL))
	{
		return 0;
	}
	memset(&beacon, 0, sizeof(beacon));
	memset(chosen, 0, sizeof(chosen));
	beacon.nonce = table->ourNonce;
	beacon.groupHash = table->groupHash;
	memcpy(beacon.identity, table->identity, NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES);
	beacon.linkPort = table->ourLinkPort;
	beacon.seatPreference = table->ourSeatPreference;

	/* Echo entries: lowest election key first, at most 3. */
	while (beacon.echoCount < NATIVE_ARCADE_DISCOVERY_MAX_ECHO)
	{
		const struct NativeArcadeDiscoveryPeer *lowest = NULL;
		uint32_t lowestIndex = 0u;

		for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; ++i)
		{
			const struct NativeArcadeDiscoveryPeer *peer = &table->peers[i];

			if ((peer->used == 0u) || (chosen[i] != 0u) || !SameGroupAndIdentity(table, peer))
			{
				continue;
			}
			if ((lowest == NULL) || KeyLess(peer->ipv4, peer->linkPort, peer->beacon.nonce, lowest->ipv4, lowest->linkPort, lowest->beacon.nonce))
			{
				lowest = peer;
				lowestIndex = i;
			}
		}
		if (lowest == NULL)
		{
			break;
		}
		chosen[lowestIndex] = 1u;
		beacon.echo[beacon.echoCount].nonce = lowest->beacon.nonce;
		beacon.echo[beacon.echoCount].ipv4 = lowest->ipv4;
		beacon.echo[beacon.echoCount].linkPort = lowest->linkPort;
		++beacon.echoCount;
	}
	return NativeArcadeDiscovery_Encode(&beacon, out);
}

int NativeArcadeDiscovery_Pairing(const struct NativeArcadeDiscoveryTable *table, struct NativeArcadeDiscoveryPairing *out)
{
	if (!TableReady(table) || (out == NULL) || (table->paired == 0u))
	{
		return 0;
	}
	*out = table->pairing;
	return 1;
}

int NativeArcadeDiscovery_TakeEvent(struct NativeArcadeDiscoveryTable *table, struct NativeArcadeDiscoveryEvent *out)
{
	if ((table == NULL) || (out == NULL) || (table->eventCount == 0u))
	{
		return 0;
	}
	*out = table->events[table->eventHead];
	table->eventHead = (table->eventHead + 1u) % NATIVE_ARCADE_DISCOVERY_EVENT_QUEUE_SIZE;
	--table->eventCount;
	return 1;
}
