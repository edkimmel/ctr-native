#include "platform/native_arcade_discovery.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define IP_11 0xC0A8010Bu /* 192.168.1.11 */
#define IP_12 0xC0A8010Cu /* 192.168.1.12 */
#define IP_13 0xC0A8010Du
#define IP_14 0xC0A8010Eu
#define IP_20 0xC0A80114u

static const uint8_t s_identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES] = {0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u, 0x76u, 0x87u};
static const uint8_t s_otherIdentity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES] = {0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u, 0x76u, 0x88u};

static uint64_t GroupDefault(void)
{
	return NativeArcadeDiscovery_GroupHash(NATIVE_ARCADE_DISCOVERY_DEFAULT_GROUP);
}

static void BeaconBase(struct NativeArcadeDiscoveryBeacon *beacon, uint64_t nonce, uint16_t linkPort, uint8_t seatPreference)
{
	memset(beacon, 0, sizeof(*beacon));
	beacon->nonce = nonce;
	beacon->groupHash = GroupDefault();
	memcpy(beacon->identity, s_identity, sizeof(s_identity));
	beacon->linkPort = linkPort;
	beacon->seatPreference = seatPreference;
}

static void AddEcho(struct NativeArcadeDiscoveryBeacon *beacon, uint64_t nonce, uint32_t ipv4, uint16_t linkPort)
{
	beacon->echo[beacon->echoCount].nonce = nonce;
	beacon->echo[beacon->echoCount].ipv4 = ipv4;
	beacon->echo[beacon->echoCount].linkPort = linkPort;
	++beacon->echoCount;
}

/* Encodes a hand-made beacon and feeds it to the table. */
static int Feed(struct NativeArcadeDiscoveryTable *table, const struct NativeArcadeDiscoveryBeacon *beacon, uint32_t sourceIpv4)
{
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	if (!NativeArcadeDiscovery_Encode(beacon, bytes))
	{
		return -1;
	}
	return NativeArcadeDiscovery_Receive(table, bytes, sizeof(bytes), sourceIpv4);
}

/* One beacon of from, received by to from fromIpv4. */
static int Deliver(const struct NativeArcadeDiscoveryTable *from, uint32_t fromIpv4, struct NativeArcadeDiscoveryTable *to)
{
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	if (!NativeArcadeDiscovery_BuildBeacon(from, bytes))
	{
		return -1;
	}
	return NativeArcadeDiscovery_Receive(to, bytes, sizeof(bytes), fromIpv4);
}

static int EventCount(struct NativeArcadeDiscoveryTable *table, uint8_t type, uint64_t nonce, struct NativeArcadeDiscoveryEvent *last)
{
	struct NativeArcadeDiscoveryEvent event;
	int count = 0;

	while (NativeArcadeDiscovery_TakeEvent(table, &event))
	{
		if ((event.type == type) && (event.peerNonce == nonce))
		{
			++count;
			if (last != NULL)
			{
				*last = event;
			}
		}
	}
	return count;
}

static void Drain(struct NativeArcadeDiscoveryTable *table)
{
	struct NativeArcadeDiscoveryEvent event;

	while (NativeArcadeDiscovery_TakeEvent(table, &event))
	{
	}
}

static uint32_t UsedPeers(const struct NativeArcadeDiscoveryTable *table)
{
	uint32_t count = 0u;
	uint32_t i;

	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; ++i)
	{
		count += (table->peers[i].used != 0u) ? 1u : 0u;
	}
	return count;
}

static int DecodeResult(const uint8_t *bytes, size_t size)
{
	struct NativeArcadeDiscoveryBeacon out;
	struct NativeArcadeDiscoveryBeacon snapshot;
	int result;

	memset(&out, 0xA5, sizeof(out));
	snapshot = out;
	result = NativeArcadeDiscovery_Decode(bytes, size, &out);
	if ((result != NATIVE_ARCADE_DISCOVERY_DECODE_OK) && (memcmp(&out, &snapshot, sizeof(out)) != 0))
	{
		return -1; /* a failed decode wrote *out */
	}
	return result;
}

/* A valid encoded beacon with one used echo entry (entries 1 and 2 unused). */
static void ValidBytes(uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES])
{
	struct NativeArcadeDiscoveryBeacon beacon;

	BeaconBase(&beacon, 0x1122334455667788u, 7001u, 0u);
	AddEcho(&beacon, 0x0102030405060708u, IP_12, 7002u);
	(void)NativeArcadeDiscovery_Encode(&beacon, bytes);
}

static int RejectsWith(size_t offset, uint8_t value, int expected)
{
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	ValidBytes(bytes);
	bytes[offset] = value;
	if (DecodeResult(bytes, sizeof(bytes)) != expected)
	{
		fprintf(stderr, "offset %u value 0x%02X: decode %d, expected %d\n", (unsigned)offset, (unsigned)value, DecodeResult(bytes, sizeof(bytes)), expected);
		return 0;
	}
	return 1;
}

static int TestCodec(void)
{
	struct NativeArcadeDiscoveryBeacon beacon;
	struct NativeArcadeDiscoveryBeacon decoded;
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES + 1u];
	uint8_t untouched[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];
	uint32_t count;
	size_t i;

	CHECK(NATIVE_ARCADE_DISCOVERY_BEACON_BYTES == 96u);
	CHECK(NATIVE_ARCADE_DISCOVERY_MAX_ECHO == 3u);
	CHECK(NATIVE_ARCADE_DISCOVERY_TABLE_SIZE == 8u);
	CHECK(NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS == 30u);
	CHECK(NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS == 300u);
	CHECK(NATIVE_ARCADE_DISCOVERY_GROUP_MAX_CHARS == 32u);
	CHECK(strcmp(NATIVE_ARCADE_DISCOVERY_DEFAULT_GROUP, "ctr-native") == 0);

	/* Round trip with 0..3 echo entries. */
	for (count = 0u; count <= NATIVE_ARCADE_DISCOVERY_MAX_ECHO; ++count)
	{
		uint32_t e;

		BeaconBase(&beacon, 0xF1E2D3C4B5A69788u, 65535u, (uint8_t)(count % 3u));
		beacon.groupHash = 0x8877665544332211u;
		for (e = 0u; e < count; ++e)
		{
			AddEcho(&beacon, 0x0100000000000000u + e, IP_12 + e, (uint16_t)(7001u + e));
		}
		memset(bytes, 0xEE, sizeof(bytes));
		CHECK(NativeArcadeDiscovery_Encode(&beacon, bytes) == 1);
		CHECK(bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES] == 0xEEu);
		memset(&decoded, 0xA5, sizeof(decoded));
		CHECK(NativeArcadeDiscovery_Decode(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES, &decoded) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
		CHECK(decoded.nonce == beacon.nonce);
		CHECK(decoded.groupHash == beacon.groupHash);
		CHECK(memcmp(decoded.identity, beacon.identity, sizeof(beacon.identity)) == 0);
		CHECK(decoded.linkPort == beacon.linkPort);
		CHECK(decoded.seatPreference == beacon.seatPreference);
		CHECK(decoded.echoCount == count);
		for (e = 0u; e < NATIVE_ARCADE_DISCOVERY_MAX_ECHO; ++e)
		{
			CHECK(decoded.echo[e].nonce == beacon.echo[e].nonce);
			CHECK(decoded.echo[e].ipv4 == beacon.echo[e].ipv4);
			CHECK(decoded.echo[e].linkPort == beacon.echo[e].linkPort);
		}
	}

	/* Explicit little-endian layout. */
	BeaconBase(&beacon, 0x0807060504030201u, 0x1B59u, 2u);
	beacon.groupHash = 0x1817161514131211u;
	AddEcho(&beacon, 0x2827262524232221u, IP_11, 0x1B5Au);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, bytes) == 1);
	CHECK(memcmp(bytes, "CTRD", 4u) == 0);
	CHECK((bytes[4] == 1u) && (bytes[5] == 0u));
	CHECK((bytes[6] == 96u) && (bytes[7] == 0u));
	for (i = 0u; i < 8u; ++i)
	{
		CHECK(bytes[8u + i] == (uint8_t)(0x01u + i));
		CHECK(bytes[16u + i] == (uint8_t)(0x11u + i));
		CHECK(bytes[24u + i] == s_identity[i]);
		CHECK(bytes[40u + i] == (uint8_t)(0x21u + i));
	}
	CHECK((bytes[32] == 0x59u) && (bytes[33] == 0x1Bu));
	CHECK((bytes[34] == 2u) && (bytes[35] == 0u) && (bytes[36] == 1u));
	CHECK((bytes[48] == 0x0Bu) && (bytes[49] == 0x01u) && (bytes[50] == 0xA8u) && (bytes[51] == 0xC0u));
	CHECK((bytes[52] == 0x5Au) && (bytes[53] == 0x1Bu));
	for (i = 37u; i < 40u; ++i)
	{
		CHECK(bytes[i] == 0u);
	}
	for (i = 54u; i < 96u; ++i)
	{
		CHECK(bytes[i] == 0u);
	}

	/* Unused struct echo entries are written as zero. */
	beacon.echo[2].nonce = 99u;
	beacon.echo[2].ipv4 = 99u;
	beacon.echo[2].linkPort = 99u;
	CHECK(NativeArcadeDiscovery_Encode(&beacon, bytes) == 1);
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);

	/* Encode refuses what Decode would reject, leaving out untouched. */
	memset(untouched, 0x5A, sizeof(untouched));
	CHECK(NativeArcadeDiscovery_Encode(NULL, untouched) == 0);
	BeaconBase(&beacon, 1u, 7001u, 0u);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, NULL) == 0);
	beacon.nonce = 0u;
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	BeaconBase(&beacon, 1u, 0u, 0u);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	BeaconBase(&beacon, 1u, 7001u, 3u);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	BeaconBase(&beacon, 1u, 7001u, 0u);
	beacon.echoCount = 4u;
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	BeaconBase(&beacon, 1u, 7001u, 0u);
	AddEcho(&beacon, 0u, IP_12, 7001u);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	BeaconBase(&beacon, 1u, 7001u, 0u);
	AddEcho(&beacon, 2u, 0u, 7001u);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	BeaconBase(&beacon, 1u, 7001u, 0u);
	AddEcho(&beacon, 2u, IP_12, 0u);
	CHECK(NativeArcadeDiscovery_Encode(&beacon, untouched) == 0);
	for (i = 0u; i < sizeof(untouched); ++i)
	{
		CHECK(untouched[i] == 0x5Au);
	}

	/* Every DISC-3 reject rule. */
	ValidBytes(bytes);
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK(NativeArcadeDiscovery_Decode(NULL, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES, &decoded) == NATIVE_ARCADE_DISCOVERY_DECODE_ARGUMENT);
	CHECK(NativeArcadeDiscovery_Decode(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES, NULL) == NATIVE_ARCADE_DISCOVERY_DECODE_ARGUMENT);
	CHECK(DecodeResult(bytes, 0u) == NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH);
	CHECK(DecodeResult(bytes, 95u) == NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH);
	CHECK(DecodeResult(bytes, 97u) == NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH);
	CHECK(DecodeResult(bytes, 64u) == NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH);
	CHECK(DecodeResult(bytes, 128u) == NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH);
	CHECK(DecodeResult(bytes, 284u) == NATIVE_ARCADE_DISCOVERY_DECODE_LENGTH);
	for (i = 0u; i < 4u; ++i)
	{
		CHECK(RejectsWith(i, 'X', NATIVE_ARCADE_DISCOVERY_DECODE_MAGIC));
	}
	CHECK(RejectsWith(4u, 0u, NATIVE_ARCADE_DISCOVERY_DECODE_VERSION));
	CHECK(RejectsWith(4u, 2u, NATIVE_ARCADE_DISCOVERY_DECODE_VERSION));
	CHECK(RejectsWith(5u, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_VERSION));
	CHECK(RejectsWith(6u, 95u, NATIVE_ARCADE_DISCOVERY_DECODE_SIZE_FIELD));
	CHECK(RejectsWith(7u, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_SIZE_FIELD));
	ValidBytes(bytes);
	memset(&bytes[8], 0, 8u);
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_NONCE);
	ValidBytes(bytes);
	bytes[32] = 0u;
	bytes[33] = 0u;
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_LINK_PORT);
	CHECK(RejectsWith(34u, 3u, NATIVE_ARCADE_DISCOVERY_DECODE_SEAT_PREFERENCE));
	CHECK(RejectsWith(34u, 0xFFu, NATIVE_ARCADE_DISCOVERY_DECODE_SEAT_PREFERENCE));
	CHECK(RejectsWith(35u, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_FLAGS));
	CHECK(RejectsWith(35u, 0x80u, NATIVE_ARCADE_DISCOVERY_DECODE_FLAGS));
	CHECK(RejectsWith(36u, 4u, NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_COUNT));
	CHECK(RejectsWith(36u, 0xFFu, NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_COUNT));
	for (i = 37u; i < 40u; ++i)
	{
		CHECK(RejectsWith(i, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_RESERVED));
	}
	for (i = 88u; i < 96u; ++i)
	{
		CHECK(RejectsWith(i, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_RESERVED));
	}
	/* Used echo entry 0: nonce 0, IPv4 0, port 0, reserved nonzero. */
	ValidBytes(bytes);
	memset(&bytes[40], 0, 8u);
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY);
	ValidBytes(bytes);
	memset(&bytes[48], 0, 4u);
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY);
	ValidBytes(bytes);
	memset(&bytes[52], 0, 2u);
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY);
	CHECK(RejectsWith(54u, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY));
	CHECK(RejectsWith(55u, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY));
	/* Unused echo entries 1 and 2: any nonzero byte. */
	for (i = 56u; i < 88u; ++i)
	{
		CHECK(RejectsWith(i, 1u, NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_UNUSED));
	}
	/* Echo count 0 makes entry 0 unused too. */
	ValidBytes(bytes);
	bytes[36] = 0u;
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_UNUSED);
	/* Echo count 2 makes the zero entry 1 a used entry with nonce 0. */
	ValidBytes(bytes);
	bytes[36] = 2u;
	CHECK(DecodeResult(bytes, NATIVE_ARCADE_DISCOVERY_BEACON_BYTES) == NATIVE_ARCADE_DISCOVERY_DECODE_ECHO_ENTRY);
	return 0;
}

/* Independent FNV-1a 64 reference, written from the published algorithm. */
static uint64_t ReferenceFnv1a(const char *text)
{
	uint64_t hash = 14695981039346656037u;
	size_t i;

	for (i = 0u; text[i] != '\0'; ++i)
	{
		hash = (hash ^ (uint8_t)text[i]) * 1099511628211u;
	}
	return hash;
}

static int TestGroup(void)
{
	char name[40];

	CHECK(NATIVE_ARCADE_DISCOVERY_FNV_OFFSET_BASIS == 0xcbf29ce484222325u);
	CHECK(NATIVE_ARCADE_DISCOVERY_FNV_PRIME == 0x100000001b3u);
	CHECK(NativeArcadeDiscovery_GroupHash("") == 0xcbf29ce484222325u);
	CHECK(NativeArcadeDiscovery_GroupHash(NULL) == 0xcbf29ce484222325u);
	CHECK(NativeArcadeDiscovery_GroupHash("a") == 0xaf63dc4c8601ec8cu);
	CHECK(NativeArcadeDiscovery_GroupHash("foobar") == 0x85944171f73967e8u);
	CHECK(NativeArcadeDiscovery_GroupHash("ctr-native") == 0xf786f7c150835784u);
	CHECK(ReferenceFnv1a("ctr-native") == 0xf786f7c150835784u);
	CHECK(NativeArcadeDiscovery_GroupHash("hall-B.2") == ReferenceFnv1a("hall-B.2"));
	CHECK(NativeArcadeDiscovery_GroupHash("ctr-native") != NativeArcadeDiscovery_GroupHash("ctr-nativE"));

	CHECK(NativeArcadeDiscovery_GroupNameValid("ctr-native"));
	CHECK(NativeArcadeDiscovery_GroupNameValid("a"));
	CHECK(NativeArcadeDiscovery_GroupNameValid("0"));
	CHECK(NativeArcadeDiscovery_GroupNameValid("Hall_B.2-x"));
	CHECK(NativeArcadeDiscovery_GroupNameValid(".hidden"));
	CHECK(NativeArcadeDiscovery_GroupNameValid("_"));
	CHECK(NativeArcadeDiscovery_GroupNameValid("a-"));
	memset(name, 'z', sizeof(name));
	name[32] = '\0';
	CHECK(NativeArcadeDiscovery_GroupNameValid(name));
	name[32] = 'z';
	name[33] = '\0';
	CHECK(!NativeArcadeDiscovery_GroupNameValid(name));
	CHECK(!NativeArcadeDiscovery_GroupNameValid(NULL));
	CHECK(!NativeArcadeDiscovery_GroupNameValid(""));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("-x"));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("-"));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("a b"));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("a/b"));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("a:b"));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("caf\xC3\xA9"));
	CHECK(!NativeArcadeDiscovery_GroupNameValid("tab\t"));
	return 0;
}

static struct NativeArcadeDiscoveryParty Party(uint32_t ipv4, uint16_t linkPort, uint64_t nonce, uint8_t seatPreference)
{
	struct NativeArcadeDiscoveryParty party;

	party.ipv4 = ipv4;
	party.linkPort = linkPort;
	party.nonce = nonce;
	party.seatPreference = seatPreference;
	return party;
}

static int Opposite(int a, int b)
{
	return ((a == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1) && (b == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2)) ||
	       ((a == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2) && (b == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1));
}

static int TestElection(void)
{
	struct NativeArcadeDiscoveryParty a;
	struct NativeArcadeDiscoveryParty b;
	uint8_t prefA;
	uint8_t prefB;

	/* The fleet: .11 and .12 on 7001; .11 is cab1 from both sides, whatever the nonces. */
	a = Party(IP_11, 7001u, 0xFFFFFFFFFFFFFFFFu, 0u);
	b = Party(IP_12, 7001u, 1u, 0u);
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2);
	/* Same IPv4 (loopback): the lower link port is cab1. */
	a = Party(0x7F000001u, 7302u, 1u, 0u);
	b = Party(0x7F000001u, 7301u, 2u, 0u);
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1);
	/* IPv4 dominates the port; comparisons are unsigned. */
	a = Party(0x0A000001u, 65535u, 5u, 0u);
	b = Party(0xC0A80101u, 1u, 6u, 0u);
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2);
	/* Equal keys: the lower nonce is cab1. */
	a = Party(IP_11, 7001u, 0x8000000000000000u, 0u);
	b = Party(IP_11, 7001u, 0x7FFFFFFFFFFFFFFFu, 0u);
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1);
	/* Equal nonces: no pair, from either side, keys equal or not. */
	b.nonce = a.nonce;
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);
	b.ipv4 = IP_12;
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);
	CHECK(NativeArcadeDiscovery_Elect(&a, NULL) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);
	CHECK(NativeArcadeDiscovery_Elect(NULL, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);
	/* A preference above 2 is no pair. */
	a = Party(IP_11, 7001u, 1u, 3u);
	b = Party(IP_12, 7001u, 2u, 0u);
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_NO_PAIR);

	/* Every preference pair, from both sides, with a the lower key. */
	for (prefA = 0u; prefA <= 2u; ++prefA)
	{
		for (prefB = 0u; prefB <= 2u; ++prefB)
		{
			int fromA;
			int fromB;
			int expectA;

			a = Party(IP_11, 7001u, 900u, prefA);
			b = Party(IP_12, 7001u, 100u, prefB);
			fromA = NativeArcadeDiscovery_Elect(&a, &b);
			fromB = NativeArcadeDiscovery_Elect(&b, &a);
			if ((prefA != 0u) && (prefA == prefB))
			{
				CHECK(fromA == NATIVE_ARCADE_DISCOVERY_ELECT_SEAT_CONFLICT);
				CHECK(fromB == NATIVE_ARCADE_DISCOVERY_ELECT_SEAT_CONFLICT);
				continue;
			}
			if (prefA != 0u)
			{
				expectA = prefA;
			}
			else if (prefB != 0u)
			{
				expectA = 3 - prefB;
			}
			else
			{
				expectA = NATIVE_ARCADE_DISCOVERY_ELECT_CAB1; /* the lower key */
			}
			if ((fromA != expectA) || !Opposite(fromA, fromB))
			{
				fprintf(stderr, "preferences %u/%u: %d/%d, expected %d\n", (unsigned)prefA, (unsigned)prefB, fromA, fromB, expectA);
				return 1;
			}
		}
	}
	/* A preference overrides the key: the higher key asking for cab1 gets it. */
	a = Party(IP_11, 7001u, 1u, 0u);
	b = Party(IP_12, 7001u, 2u, 1u);
	CHECK(NativeArcadeDiscovery_Elect(&a, &b) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB2);
	CHECK(NativeArcadeDiscovery_Elect(&b, &a) == NATIVE_ARCADE_DISCOVERY_ELECT_CAB1);
	return 0;
}

static int InitTable(struct NativeArcadeDiscoveryTable *table, uint64_t nonce, uint16_t linkPort, uint8_t seatPreference)
{
	return NativeArcadeDiscovery_Init(table, nonce, GroupDefault(), s_identity, linkPort, seatPreference);
}

static int TestInit(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryBeacon beacon;
	struct NativeArcadeDiscoveryBeacon decoded;
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	CHECK(NativeArcadeDiscovery_Init(NULL, 1u, 0u, s_identity, 7001u, 0u) == 0);
	CHECK(NativeArcadeDiscovery_Init(&table, 1u, 0u, NULL, 7001u, 0u) == 0);
	CHECK(InitTable(&table, 0u, 7001u, 0u) == 0);
	CHECK(InitTable(&table, 1u, 0u, 0u) == 0);
	CHECK(InitTable(&table, 1u, 7001u, 3u) == 0);
	/* A failed Init ignores beacons and builds none. */
	BeaconBase(&beacon, 5u, 7002u, 0u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 0);
	NativeArcadeDiscovery_Tick(&table);
	CHECK(table.tick == 0u);

	CHECK(InitTable(&table, 0xABCDu, 7001u, 2u) == 1);
	CHECK(table.tick == 0u);
	CHECK(UsedPeers(&table) == 0u);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, NULL) == 0);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &decoded) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK(decoded.nonce == 0xABCDu);
	CHECK(decoded.groupHash == 0xf786f7c150835784u);
	CHECK(memcmp(decoded.identity, s_identity, sizeof(s_identity)) == 0);
	CHECK(decoded.linkPort == 7001u);
	CHECK(decoded.seatPreference == 2u);
	CHECK(decoded.echoCount == 0u);
	/* Rejected input: bad bytes, source IPv4 0. */
	CHECK(NativeArcadeDiscovery_Receive(&table, bytes, sizeof(bytes) - 1u, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED);
	CHECK(NativeArcadeDiscovery_Receive(&table, NULL, sizeof(bytes), IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED);
	CHECK(Feed(&table, &beacon, 0u) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REJECTED);
	CHECK(UsedPeers(&table) == 0u);
	return 0;
}

/* Two cabinets .11 and .12 on 7001 find each other; staggered start. */
static int TestTablesPair(void)
{
	struct NativeArcadeDiscoveryTable a;
	struct NativeArcadeDiscoveryTable b;
	struct NativeArcadeDiscoveryPairing pairA;
	struct NativeArcadeDiscoveryPairing pairB;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon decoded;
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	/* b's nonce is lower, so only the key can make a cab1. */
	CHECK(InitTable(&a, 0x9000u, 7001u, 0u) == 1);
	CHECK(InitTable(&b, 0x1000u, 7001u, 0u) == 1);

	/* a beacons first: b learns a, but a has no echo of b yet. */
	CHECK(Deliver(&a, IP_11, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&b, &event) == 0);
	/* b's beacon now echoes a. */
	CHECK(NativeArcadeDiscovery_BuildBeacon(&b, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &decoded) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK(decoded.echoCount == 1u);
	CHECK((decoded.echo[0].nonce == 0x9000u) && (decoded.echo[0].ipv4 == IP_11) && (decoded.echo[0].linkPort == 7001u));
	/* a hears b's echo of a: a pairs as cab1. */
	CHECK(NativeArcadeDiscovery_Receive(&a, bytes, sizeof(bytes), IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&a, &pairA) == 1);
	CHECK((pairA.peerNonce == 0x1000u) && (pairA.peerIpv4 == IP_12) && (pairA.peerLinkPort == 7001u));
	CHECK(pairA.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1);
	CHECK(EventCount(&a, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, 0x1000u, &event) == 1);
	CHECK((event.peerIpv4 == IP_12) && (event.peerLinkPort == 7001u) && (event.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1));
	CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 0);
	/* a's next beacon echoes b: b pairs as cab2. */
	CHECK(Deliver(&a, IP_11, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 1);
	CHECK((pairB.peerNonce == 0x9000u) && (pairB.peerIpv4 == IP_11) && (pairB.peerLinkPort == 7001u));
	CHECK(pairB.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2);
	CHECK(EventCount(&b, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, 0x9000u, NULL) == 1);
	/* Steady beacons: no new events. */
	CHECK(Deliver(&a, IP_11, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(Deliver(&b, IP_12, &a) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_TakeEvent(&a, &event) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&b, &event) == 0);

	/* Both started on the same tick: both beacon before either receives. */
	CHECK(InitTable(&a, 0x9000u, 7001u, 0u) == 1);
	CHECK(InitTable(&b, 0x1000u, 7001u, 0u) == 1);
	{
		uint8_t fromA[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];
		uint8_t fromB[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

		CHECK(NativeArcadeDiscovery_BuildBeacon(&a, fromA) == 1);
		CHECK(NativeArcadeDiscovery_BuildBeacon(&b, fromB) == 1);
		CHECK(NativeArcadeDiscovery_Receive(&b, fromA, sizeof(fromA), IP_11) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
		CHECK(NativeArcadeDiscovery_Receive(&a, fromB, sizeof(fromB), IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
		CHECK(NativeArcadeDiscovery_Pairing(&a, &pairA) == 0);
		CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 0);
		NativeArcadeDiscovery_Tick(&a);
		NativeArcadeDiscovery_Tick(&b);
		CHECK(NativeArcadeDiscovery_BuildBeacon(&a, fromA) == 1);
		CHECK(NativeArcadeDiscovery_BuildBeacon(&b, fromB) == 1);
		CHECK(NativeArcadeDiscovery_Receive(&b, fromA, sizeof(fromA), IP_11) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
		CHECK(NativeArcadeDiscovery_Receive(&a, fromB, sizeof(fromB), IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	}
	CHECK(NativeArcadeDiscovery_Pairing(&a, &pairA) == 1);
	CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 1);
	CHECK(pairA.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1);
	CHECK(pairB.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2);
	CHECK((pairA.peerIpv4 == IP_12) && (pairB.peerIpv4 == IP_11));

	/* Loopback, different link ports: the lower link port is cab1. */
	CHECK(InitTable(&a, 0x1u, 7302u, 0u) == 1);
	CHECK(InitTable(&b, 0x2u, 7301u, 0u) == 1);
	CHECK(Deliver(&a, 0x7F000001u, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(Deliver(&b, 0x7F000001u, &a) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(Deliver(&a, 0x7F000001u, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_Pairing(&a, &pairA) == 1);
	CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 1);
	CHECK((pairA.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2) && (pairA.peerLinkPort == 7301u));
	CHECK((pairB.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1) && (pairB.peerLinkPort == 7302u));

	/* Preferences travel in the beacon: .12 asking for cab1 gets it. */
	CHECK(InitTable(&a, 0x9000u, 7001u, 0u) == 1);
	CHECK(InitTable(&b, 0x1000u, 7001u, 1u) == 1);
	CHECK(Deliver(&a, IP_11, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(Deliver(&b, IP_12, &a) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(Deliver(&a, IP_11, &b) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_Pairing(&a, &pairA) == 1);
	CHECK(NativeArcadeDiscovery_Pairing(&b, &pairB) == 1);
	CHECK(pairA.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2);
	CHECK(pairB.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1);
	return 0;
}

static int TestEchoRequiredAndOwnNonce(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon beacon;
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	/* A peer echoing someone else is heard but not eligible. */
	BeaconBase(&beacon, 0x88u, 7001u, 0u);
	AddEcho(&beacon, 0x99u, IP_13, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	/* Echoed back to it though (two-way reachability starts somewhere). */
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &beacon) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK((beacon.echoCount == 1u) && (beacon.echo[0].nonce == 0x88u));
	/* Echoing our nonce in the third entry: eligible. */
	BeaconBase(&beacon, 0x88u, 7001u, 0u);
	AddEcho(&beacon, 0x99u, IP_13, 7001u);
	AddEcho(&beacon, 0x9Au, IP_14, 7001u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK((pairing.peerNonce == 0x88u) && (pairing.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1));
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, 0x88u, NULL) == 1);
	/* The echo stops (the peer lost us): the pairing is lost. */
	BeaconBase(&beacon, 0x88u, 7001u, 0u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST, 0x88u, NULL) == 1);

	/* Our own nonce (our broadcast looped back) is ignored, even from elsewhere. */
	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Receive(&table, bytes, sizeof(bytes), IP_11) == NATIVE_ARCADE_DISCOVERY_RECEIVE_OWN);
	CHECK(NativeArcadeDiscovery_Receive(&table, bytes, sizeof(bytes), IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_OWN);
	BeaconBase(&beacon, 0x77u, 7009u, 1u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_13) == NATIVE_ARCADE_DISCOVERY_RECEIVE_OWN);
	CHECK(UsedPeers(&table) == 0u);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	return 0;
}

/* A paired peer at tick T expires on the Tick that reaches T + 300. */
static int TestExpiry(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon beacon;
	uint32_t i;

	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	for (i = 0u; i < 5u; ++i)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	BeaconBase(&beacon, 0x88u, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(table.tick == 5u);
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, 0x88u, NULL) == 1);
	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS - 1u; ++i)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(table.tick == 5u + 299u);
	CHECK(UsedPeers(&table) == 1u);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	NativeArcadeDiscovery_Tick(&table);
	CHECK(table.tick == 5u + 300u);
	CHECK(UsedPeers(&table) == 0u);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 1);
	CHECK(event.type == NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST);
	CHECK((event.peerNonce == 0x88u) && (event.peerIpv4 == IP_12) && (event.peerLinkPort == 7001u));
	CHECK(event.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);

	/* A refresh restarts the 300 ticks. */
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	for (i = 0u; i < 200u; ++i)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	for (i = 0u; i < 299u; ++i)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(UsedPeers(&table) == 1u);
	NativeArcadeDiscovery_Tick(&table);
	CHECK(UsedPeers(&table) == 0u);

	/* The tick count wraps without expiring a fresh entry. */
	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	table.tick = 0xFFFFFFF0u;
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	for (i = 0u; i < 299u; ++i)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(UsedPeers(&table) == 1u);
	NativeArcadeDiscovery_Tick(&table);
	CHECK(UsedPeers(&table) == 0u);
	return 0;
}

static int TestMismatchAndConflict(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon beacon;
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];
	uint32_t i;

	/* Other group, echoing us: never eligible, never echoed, reported once. */
	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	for (i = 0u; i < 5u; ++i)
	{
		BeaconBase(&beacon, 0x88u, 7001u, 0u);
		beacon.groupHash = NativeArcadeDiscovery_GroupHash("hall-b");
		AddEcho(&beacon, 0x77u, IP_11, 7001u);
		CHECK(Feed(&table, &beacon, IP_12) == ((i == 0u) ? NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED : NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED));
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 1);
	CHECK(event.type == NATIVE_ARCADE_DISCOVERY_EVENT_GROUP_MISMATCH);
	CHECK((event.peerNonce == 0x88u) && (event.peerIpv4 == IP_12) && (event.peerLinkPort == 7001u) && (event.localSeat == 0u));
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &beacon) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK(beacon.echoCount == 0u);
	/* Still once after it expires and returns (the nonce is remembered). */
	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS; ++i)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(UsedPeers(&table) == 0u);
	BeaconBase(&beacon, 0x88u, 7001u, 0u);
	beacon.groupHash = NativeArcadeDiscovery_GroupHash("hall-b");
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);

	/* Same group, other identity: same treatment, its own event. */
	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	for (i = 0u; i < 3u; ++i)
	{
		BeaconBase(&beacon, 0x89u, 7001u, 0u);
		memcpy(beacon.identity, s_otherIdentity, sizeof(s_otherIdentity));
		AddEcho(&beacon, 0x77u, IP_11, 7001u);
		CHECK(Feed(&table, &beacon, IP_13) >= 0);
	}
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 1);
	CHECK(event.type == NATIVE_ARCADE_DISCOVERY_EVENT_IDENTITY_MISMATCH);
	CHECK((event.peerNonce == 0x89u) && (event.peerIpv4 == IP_13));
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &beacon) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK(beacon.echoCount == 0u);
	/* The same nonce may later report a different kind, once. */
	BeaconBase(&beacon, 0x89u, 7001u, 0u);
	beacon.groupHash = 1u;
	CHECK(Feed(&table, &beacon, IP_13) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(Feed(&table, &beacon, IP_13) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_GROUP_MISMATCH, 0x89u, NULL) == 1);

	/* Equal preferences: a seat conflict, no pairing, reported once. */
	CHECK(InitTable(&table, 0x77u, 7001u, 1u) == 1);
	for (i = 0u; i < 4u; ++i)
	{
		BeaconBase(&beacon, 0x8Au, 7001u, 1u);
		AddEcho(&beacon, 0x77u, IP_11, 7001u);
		CHECK(Feed(&table, &beacon, IP_12) >= 0);
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 1);
	CHECK(event.type == NATIVE_ARCADE_DISCOVERY_EVENT_SEAT_CONFLICT);
	CHECK((event.peerNonce == 0x8Au) && (event.peerIpv4 == IP_12));
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	/* A conflicting peer is still echoed (same group and identity). */
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &beacon) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK((beacon.echoCount == 1u) && (beacon.echo[0].nonce == 0x8Au));
	/* A second, compatible peer pairs even with the conflicting one lower. */
	BeaconBase(&beacon, 0x8Bu, 7001u, 2u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_20) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK((pairing.peerNonce == 0x8Bu) && (pairing.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1));
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_SEAT_CONFLICT, 0x8Au, NULL) == 0);
	return 0;
}

static int TestRestartAndFirstAddress(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon beacon;

	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	BeaconBase(&beacon, 0x88u, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	Drain(&table);

	/* The same nonce from a second NIC: the first address is kept. */
	NativeArcadeDiscovery_Tick(&table);
	CHECK(Feed(&table, &beacon, IP_20) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(UsedPeers(&table) == 1u);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK(pairing.peerIpv4 == IP_12);
	CHECK(table.peers[0].lastHeardTick == 1u);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);

	/* A new nonce at another address is a second entry, not a replacement. */
	BeaconBase(&beacon, 0x90u, 7002u, 0u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(UsedPeers(&table) == 2u);

	/* The peer restarts: a new nonce at the same IPv4 and link port replaces it at once. */
	BeaconBase(&beacon, 0x99u, 7001u, 0u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(UsedPeers(&table) == 2u);
	CHECK(table.peers[0].beacon.nonce == 0x99u);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 0);
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST, 0x88u, NULL) == 1);
	/* Once it echoes us, the new process pairs. */
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK((pairing.peerNonce == 0x99u) && (pairing.peerIpv4 == IP_12));
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, 0x99u, NULL) == 1);

	/* A restart while paired, echoing us at once: LOST then FOUND. */
	BeaconBase(&beacon, 0xAAu, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 1);
	CHECK((event.type == NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST) && (event.peerNonce == 0x99u));
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 1);
	CHECK((event.type == NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND) && (event.peerNonce == 0xAAu));
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	return 0;
}

static int TestFullTable(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon beacon;
	uint32_t i;

	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	/* Eight peers of another group fill the table (their mismatches are logged). */
	for (i = 0u; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; ++i)
	{
		BeaconBase(&beacon, 0x100u + i, 7001u, 0u);
		beacon.groupHash = 5u;
		CHECK(Feed(&table, &beacon, IP_20 + i) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
		NativeArcadeDiscovery_Tick(&table);
	}
	Drain(&table);
	/* A ninth, even a compatible one echoing us, is dropped unlogged. */
	BeaconBase(&beacon, 0x200u, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_FULL);
	BeaconBase(&beacon, 0x201u, 7001u, 0u);
	beacon.groupHash = 6u;
	CHECK(Feed(&table, &beacon, IP_13) == NATIVE_ARCADE_DISCOVERY_RECEIVE_FULL);
	CHECK(UsedPeers(&table) == NATIVE_ARCADE_DISCOVERY_TABLE_SIZE);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, &event) == 0);
	/* Known nonces still refresh. */
	BeaconBase(&beacon, 0x107u, 7001u, 0u);
	beacon.groupHash = 5u;
	CHECK(Feed(&table, &beacon, IP_20 + 7u) == NATIVE_ARCADE_DISCOVERY_RECEIVE_REFRESHED);
	/* The oldest (heard at tick 0) expires on tick 300; then there is room. */
	while (table.tick < NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS - 1u)
	{
		NativeArcadeDiscovery_Tick(&table);
	}
	CHECK(UsedPeers(&table) == NATIVE_ARCADE_DISCOVERY_TABLE_SIZE);
	NativeArcadeDiscovery_Tick(&table);
	CHECK(UsedPeers(&table) == NATIVE_ARCADE_DISCOVERY_TABLE_SIZE - 1u);
	BeaconBase(&beacon, 0x200u, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND, 0x200u, NULL) == 1);
	return 0;
}

static int TestLowestOfThree(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryBeacon beacon;
	uint8_t bytes[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	/* We are .11. Three eligible peers arrive, highest key first. */
	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	BeaconBase(&beacon, 0x1u, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_20) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK(pairing.peerIpv4 == IP_20);
	BeaconBase(&beacon, 0x2u, 7002u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_13) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	BeaconBase(&beacon, 0x3u, 7001u, 0u);
	AddEcho(&beacon, 0x77u, IP_11, 7001u);
	CHECK(Feed(&table, &beacon, IP_13) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK((pairing.peerNonce == 0x3u) && (pairing.peerIpv4 == IP_13) && (pairing.peerLinkPort == 7001u));
	CHECK(pairing.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1);
	CHECK(EventCount(&table, NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST, 0x1u, NULL) == 1);
	/* A fourth, lower still but not echoing us: not eligible, but echoed first. */
	BeaconBase(&beacon, 0x4u, 7001u, 0u);
	CHECK(Feed(&table, &beacon, IP_12) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED);
	CHECK(NativeArcadeDiscovery_Pairing(&table, &pairing) == 1);
	CHECK(pairing.peerNonce == 0x3u);
	CHECK(NativeArcadeDiscovery_BuildBeacon(&table, bytes) == 1);
	CHECK(NativeArcadeDiscovery_Decode(bytes, sizeof(bytes), &beacon) == NATIVE_ARCADE_DISCOVERY_DECODE_OK);
	CHECK(beacon.echoCount == 3u);
	CHECK((beacon.echo[0].nonce == 0x4u) && (beacon.echo[0].ipv4 == IP_12) && (beacon.echo[0].linkPort == 7001u));
	CHECK((beacon.echo[1].nonce == 0x3u) && (beacon.echo[1].ipv4 == IP_13) && (beacon.echo[1].linkPort == 7001u));
	CHECK((beacon.echo[2].nonce == 0x2u) && (beacon.echo[2].ipv4 == IP_13) && (beacon.echo[2].linkPort == 7002u));
	return 0;
}

static int TestEventQueue(void)
{
	struct NativeArcadeDiscoveryTable table;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeArcadeDiscoveryBeacon beacon;
	uint32_t i;
	uint32_t taken = 0u;

	CHECK(NativeArcadeDiscovery_TakeEvent(NULL, &event) == 0);
	CHECK(InitTable(&table, 0x77u, 7001u, 0u) == 1);
	CHECK(NativeArcadeDiscovery_TakeEvent(&table, NULL) == 0);
	/* 20 group mismatches, never drained: 16 queued in order, 4 counted as dropped. */
	for (i = 0u; i < 20u; ++i)
	{
		BeaconBase(&beacon, 0x300u + i, 7001u, 0u);
		beacon.groupHash = 5u;
		CHECK(Feed(&table, &beacon, IP_20) == NATIVE_ARCADE_DISCOVERY_RECEIVE_ADDED); /* each replaces the last (same address) */
	}
	CHECK(UsedPeers(&table) == 1u);
	CHECK(table.eventsDropped == 4u);
	while (NativeArcadeDiscovery_TakeEvent(&table, &event))
	{
		CHECK(event.type == NATIVE_ARCADE_DISCOVERY_EVENT_GROUP_MISMATCH);
		CHECK(event.peerNonce == 0x300u + taken);
		++taken;
	}
	CHECK(taken == NATIVE_ARCADE_DISCOVERY_EVENT_QUEUE_SIZE);
	return 0;
}

int main(void)
{
	CHECK(TestCodec() == 0);
	CHECK(TestGroup() == 0);
	CHECK(TestElection() == 0);
	CHECK(TestInit() == 0);
	CHECK(TestTablesPair() == 0);
	CHECK(TestEchoRequiredAndOwnNonce() == 0);
	CHECK(TestExpiry() == 0);
	CHECK(TestMismatchAndConflict() == 0);
	CHECK(TestRestartAndFirstAddress() == 0);
	CHECK(TestFullTable() == 0);
	CHECK(TestLowestOfThree() == 0);
	CHECK(TestEventQueue() == 0);
	puts("native_arcade_discovery_test: ok");
	return 0;
}
