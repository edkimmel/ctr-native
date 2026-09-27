#include "platform/native_arcade_discovery_service.h"

#include "platform/native_arcade_discovery.h"
#include "platform/native_udp_transport.h"
#include "platform/native_win32.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Two real discovery services on 127.0.0.1 (docs/DISCOVERY_MILESTONE.md
 * DISC-S3), each with the other as its one explicit target (DISC-18), in
 * the fast suite's port band 48610-48629 (CMakeLists.txt):
 *   A binds 48610 and advertises link port 48613;
 *   B binds 48611 and advertises link port 48614;
 *   48612 is the enumeration (no override) case. Only the held-refresh case
 *   (TestRefreshHeld, docs/DISCOVERY_MILESTONE.md risk 10) ticks it: 901
 *   ticks, so 31 beacon rounds go to the broadcast targets at port 48612,
 *   where nothing listens but the service itself (which drops its own
 *   nonce). Every other case sends to loopback only (the lan case with no
 *   interface inside its lan sends nothing).
 * The link ports are only advertised, never bound.
 */
#define PORT_A         48610u
#define PORT_B         48611u
#define PORT_ENUMERATE 48612u
#define LINK_PORT_A    48613u
#define LINK_PORT_B    48614u
#define NONCE_A        UINT64_C(0x1111111111111111)
#define NONCE_B        UINT64_C(0x2222222222222222)
#define LOOPBACK       UINT32_C(0x7F000001)
/* The lan cases (DISC-19): 127.0.0.0/8, which holds the loopback sender, and
 * 203.0.113.0/24 (TEST-NET-3, RFC 5737), which no local interface and no
 * loopback sender is in. The latter case enumerates the interfaces, finds
 * none inside, and so sends nothing at all. */
#define LAN_LOOPBACK   UINT32_C(0x7F000000)
#define LAN_ABSENT     UINT32_C(0xCB007100)

static struct NativeArcadeDiscoveryService s_a;
static struct NativeArcadeDiscoveryService s_b;
static struct NativeArcadeDiscoveryService s_conflict;
static struct NativeArcadeDiscoveryService s_enumerate;
static struct NativeArcadeDiscoveryService s_zero;

static const uint8_t k_identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8};

/*
 * Loopback delivery is asynchronous relative to sendto returning, and under
 * a loaded ctest -j 8 it can lag by more than any fixed wait. So the test
 * never asserts on which step a datagram lands: it paces the ticks (a sleep
 * after each step, so the 300-tick expiry spans at least 300 ms of wall
 * time) and polls for each expected state up to POLL_BOUND_MS.
 */
#define POLL_BOUND_MS 5000u

static void Step(struct NativeArcadeDiscoveryService *a, struct NativeArcadeDiscoveryService *b, uint32_t *step)
{
	NativeArcadeDiscoveryService_Tick(a, 1);
	NativeArcadeDiscoveryService_Tick(b, 1);
	(*step)++;
	Sleep(1);
}

static int Paired(const struct NativeArcadeDiscoveryService *service)
{
	struct NativeArcadeDiscoveryPairing pairing;

	return NativeArcadeDiscoveryService_Pairing(service, &pairing);
}

/* Steps both until both are paired; 0 if that takes over POLL_BOUND_MS. */
static int StepUntilBothPaired(struct NativeArcadeDiscoveryService *a, struct NativeArcadeDiscoveryService *b, uint32_t *step)
{
	const ULONGLONG deadline = GetTickCount64() + POLL_BOUND_MS;

	do
	{
		Step(a, b, step);
		if (Paired(a) && Paired(b))
		{
			return 1;
		}
	} while (GetTickCount64() < deadline);
	return 0;
}

static int TestClosedAndArguments(void)
{
	struct NativeArcadeDiscoveryServiceStatus status;
	struct NativeArcadeDiscoveryPairing pairing;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeUdpTransportAddress address;
	const struct NativeUdpTransportAddress good = {LOOPBACK, PORT_B};
	const struct NativeUdpTransportAddress zeroPort = {LOOPBACK, 0u};
	const struct NativeUdpTransportAddress zeroAddress = {0u, PORT_B};
	const struct NativeUdpTransportAddress five[5] = {good, good, good, good, good};

	/* A zeroed service is closed; Close is safe on it, twice, and on NULL. */
	NativeArcadeDiscoveryService_Close(&s_zero);
	NativeArcadeDiscoveryService_Close(&s_zero);
	NativeArcadeDiscoveryService_Close(NULL);
	NativeArcadeDiscoveryService_Tick(&s_zero, 1);
	NativeArcadeDiscoveryService_Tick(NULL, 1);
	CHECK(!NativeArcadeDiscoveryService_Pairing(&s_zero, &pairing));
	CHECK(!NativeArcadeDiscoveryService_Pairing(NULL, &pairing));
	CHECK(!NativeArcadeDiscoveryService_TakeEvent(&s_zero, &event));
	CHECK(!NativeArcadeDiscoveryService_Target(&s_zero, 0, &address));
	CHECK(!NativeArcadeDiscoveryService_GetStatus(NULL, &status));
	CHECK(!NativeArcadeDiscoveryService_GetStatus(&s_zero, NULL));
	memset(&status, 0xA5, sizeof(status));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_zero, &status));
	CHECK((status.open == 0) && (status.targetCount == 0) && (status.bindPort == 0) && (status.tickCount == 0));

	/* Bad arguments: 0, and the service stays closed (no socket bound). */
	CHECK(!NativeArcadeDiscoveryService_Open(NULL, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, 0u, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, five, 5u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, NULL, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &zeroPort, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &zeroAddress, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, 0u, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, NULL, LINK_PORT_A, 0u, &good, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, 0u, 0u, &good, 1u, 0u, 0u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 3u, &good, 1u, 0u, 0u));
	/* A bad lan (DISC-19): prefix 7 or 31, host bits set; and an override outside the lan. */
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, LAN_LOOPBACK, 7u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, LAN_LOOPBACK, 31u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, LOOPBACK, 8u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &good, 1u, LAN_ABSENT, 24u));
	CHECK(!NativeArcadeDiscoveryService_Open(&s_zero, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, NULL, 0u, LAN_ABSENT, 32u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_zero, &status));
	CHECK(status.open == 0);
	return 0;
}

/* No override: the targets come from the interface list at the discovery port. */
static int TestEnumeratedTargets(void)
{
	struct NativeArcadeDiscoveryServiceStatus status;
	struct NativeUdpTransportAddress address;

	CHECK(NativeArcadeDiscoveryService_Open(&s_enumerate, PORT_ENUMERATE, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, NULL, 0u, 0u, 0u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.open == 1) && (status.overridden == 0) && (status.bindPort == PORT_ENUMERATE) && (status.tickCount == 0));
	CHECK((status.targetCount >= 1u) && (status.targetCount <= NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS));
	CHECK(NativeArcadeDiscoveryService_Target(&s_enumerate, 0, &address));
	CHECK((address.ipv4 == UINT32_C(0xFFFFFFFF)) && (address.port == PORT_ENUMERATE));
	for (uint32_t i = 1; i < status.targetCount; i++)
	{
		CHECK(NativeArcadeDiscoveryService_Target(&s_enumerate, i, &address));
		CHECK((address.port == PORT_ENUMERATE) && (address.ipv4 != UINT32_C(0xFFFFFFFF)));
	}
	CHECK(!NativeArcadeDiscoveryService_Target(&s_enumerate, status.targetCount, &address));
	printf("native_arcade_discovery_service_test: %u enumerated targets%s\n", (unsigned)status.targetCount,
	       (status.enumerationFailed != 0) ? " (enumeration failed)" : "");
	NativeArcadeDiscoveryService_Close(&s_enumerate);
	NativeArcadeDiscoveryService_Close(&s_enumerate);
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK(status.open == 0);
	return 0;
}

/* The beacon rounds after the ticks 0..lastTick: one every interval from tick 0. */
static uint32_t BeaconRoundsThrough(uint32_t lastTick)
{
	return (lastTick / NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS) + 1u;
}

/*
 * Risk 10: the caller may hold the interface refresh (mayRefresh 0). A
 * refresh that falls due while held is pending, and runs once on the first
 * tick that allows it, however many fell due meanwhile; the schedule does
 * not move, and the beacon cadence never changes.
 */
static int TestRefreshHeld(void)
{
	struct NativeArcadeDiscoveryServiceStatus status;
	uint32_t tick;

	CHECK(NativeArcadeDiscoveryService_Open(&s_enumerate, PORT_ENUMERATE, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, NULL, 0u, 0u, 0u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.overridden == 0) && (status.refreshCount == 1u) && (status.refreshPending == 0) && (status.beaconCount == 0u));

	/* Ticks 0..299, allowed: no refresh due yet. */
	for (tick = 0; tick < NATIVE_ARCADE_DISCOVERY_SERVICE_REFRESH_TICKS; tick++)
	{
		NativeArcadeDiscoveryService_Tick(&s_enumerate, 1);
	}
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.tickCount == 300u) && (status.refreshCount == 1u) && (status.refreshPending == 0));
	CHECK(status.beaconCount == BeaconRoundsThrough(299u));

	/* Ticks 300..600 held: the refresh due at 300 waits, the one due at 600
	 * joins it, and a beacon still goes out every 30 ticks. */
	for (tick = 300u; tick <= 600u; tick++)
	{
		NativeArcadeDiscoveryService_Tick(&s_enumerate, 0);
		CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
		CHECK((status.tickCount == tick + 1u) && (status.refreshCount == 1u) && (status.refreshPending == 1));
		CHECK(status.beaconCount == BeaconRoundsThrough(tick));
	}

	/* Tick 601, allowed: the pending refresh runs, once. */
	NativeArcadeDiscoveryService_Tick(&s_enumerate, 1);
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.refreshCount == 2u) && (status.refreshPending == 0) && (status.beaconCount == BeaconRoundsThrough(601u)));
	CHECK((status.targetCount >= 1u) && (status.targetCount <= NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS));

	/* Allowed from here: nothing until the schedule's next due tick, 900,
	 * which refreshes at once. */
	for (tick = 602u; tick < 900u; tick++)
	{
		NativeArcadeDiscoveryService_Tick(&s_enumerate, 1);
	}
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.tickCount == 900u) && (status.refreshCount == 2u) && (status.refreshPending == 0));
	NativeArcadeDiscoveryService_Tick(&s_enumerate, 1);
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.refreshCount == 3u) && (status.refreshPending == 0) && (status.beaconCount == BeaconRoundsThrough(900u)));

	NativeArcadeDiscoveryService_Close(&s_enumerate);
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_enumerate, &status));
	CHECK((status.open == 0) && (status.refreshCount == 0u) && (status.beaconCount == 0u));
	return 0;
}

static int TestPairOnLoopback(void)
{
	const struct NativeUdpTransportAddress toB = {LOOPBACK, PORT_B};
	const struct NativeUdpTransportAddress toA = {LOOPBACK, PORT_A};
	struct NativeArcadeDiscoveryServiceStatus status;
	struct NativeArcadeDiscoveryPairing pairingA;
	struct NativeArcadeDiscoveryPairing pairingB;
	struct NativeArcadeDiscoveryEvent event;
	struct NativeUdpTransportAddress address;
	uint32_t step = 0;
	uint32_t pairedStep;

	CHECK(NativeArcadeDiscoveryService_Open(&s_a, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, NATIVE_ARCADE_DISCOVERY_SEAT_AUTO, &toB, 1u, 0u, 0u));
	CHECK(NativeArcadeDiscoveryService_Open(&s_b, PORT_B, NONCE_B, 1u, k_identity, LINK_PORT_B, NATIVE_ARCADE_DISCOVERY_SEAT_AUTO, &toA, 1u, 0u, 0u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	CHECK((status.open == 1) && (status.overridden == 1) && (status.enumerationFailed == 0) && (status.targetCount == 1u));
	/* Explicit targets: no interface list is ever read. */
	CHECK((status.refreshCount == 0u) && (status.refreshPending == 0));
	CHECK(NativeArcadeDiscoveryService_Target(&s_a, 0, &address));
	CHECK((address.ipv4 == LOOPBACK) && (address.port == PORT_B));
	CHECK(!NativeArcadeDiscoveryService_Target(&s_a, 1u, &address));

	/* A second Open on a bound port: 0, closed, nothing crashes (DISC-15). */
	CHECK(!NativeArcadeDiscoveryService_Open(&s_conflict, PORT_A, UINT64_C(0x3333), 1u, k_identity, LINK_PORT_A, 0u, &toB, 1u, 0u, 0u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_conflict, &status));
	CHECK(status.open == 0);
	NativeArcadeDiscoveryService_Tick(&s_conflict, 1);
	NativeArcadeDiscoveryService_Close(&s_conflict);

	/* Step 0: A beacons with no echo, so nothing can pair yet: A has read
	 * nothing, and B at most A's echo-less beacon (not eligible). Each side
	 * pairs once it reads a beacon echoing its own nonce, which the other
	 * sends from its first beacon after reading ours (DISC-7): within a few
	 * intervals, however late loopback delivers. */
	Step(&s_a, &s_b, &step);
	CHECK(!Paired(&s_a) && !Paired(&s_b));
	CHECK(StepUntilBothPaired(&s_a, &s_b, &step));
	pairedStep = step;
	CHECK(NativeArcadeDiscoveryService_Pairing(&s_a, &pairingA));
	CHECK(NativeArcadeDiscoveryService_Pairing(&s_b, &pairingB));

	/* Opposite seats: equal addresses, so the lower link port is cab1 (DISC-8, DISC-18). */
	CHECK(pairingA.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1);
	CHECK(pairingB.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2);
	CHECK((pairingA.peerNonce == NONCE_B) && (pairingA.peerIpv4 == LOOPBACK) && (pairingA.peerLinkPort == LINK_PORT_B));
	CHECK((pairingB.peerNonce == NONCE_A) && (pairingB.peerIpv4 == LOOPBACK) && (pairingB.peerLinkPort == LINK_PORT_A));
	CHECK(NativeArcadeDiscoveryService_TakeEvent(&s_a, &event));
	CHECK((event.type == NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND) && (event.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1) && (event.peerNonce == NONCE_B) &&
	      (event.peerLinkPort == LINK_PORT_B));
	CHECK(!NativeArcadeDiscoveryService_TakeEvent(&s_a, &event));
	CHECK(NativeArcadeDiscoveryService_TakeEvent(&s_b, &event));
	CHECK((event.type == NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_FOUND) && (event.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2));

	/* The pairing holds while both beacon, through a few more intervals. */
	for (uint32_t held = 0; held < 2u * NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS; held++)
	{
		Step(&s_a, &s_b, &step);
		CHECK(Paired(&s_a) && Paired(&s_b));
	}
	while ((step % NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS) != 0u)
	{
		Step(&s_a, &s_b, &step);
	}
	/* A beacons one last time and closes at once. B reads that beacon on
	 * some tick k >= 1 after the close and loses the pairing on tick
	 * k + 299 (DISC-4: an entry heard at tick T expires on the Tick reaching
	 * T + 300): never before tick 300. B's first interval of ticks is paced
	 * for the beacon to land in, so k <= 31 bounds the loss above too. */
	NativeArcadeDiscoveryService_Tick(&s_a, 1);
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	CHECK(status.tickCount == step + 1u);
	CHECK((status.refreshCount == 0u) && (status.refreshPending == 0) && (status.beaconCount == BeaconRoundsThrough(step)));
	NativeArcadeDiscoveryService_Close(&s_a);
	CHECK(!Paired(&s_a));
	{
		uint32_t after = 1u;

		for (;; after++)
		{
			NativeArcadeDiscoveryService_Tick(&s_b, 1);
			if (!Paired(&s_b))
			{
				break;
			}
			CHECK(after < NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS + NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS);
			if (after <= NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS)
			{
				Sleep(1);
			}
		}
		if (after < NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS)
		{
			fprintf(stderr, "B lost the pairing %u ticks after A closed, before the %u-tick expiry\n", (unsigned)after,
			        (unsigned)NATIVE_ARCADE_DISCOVERY_EXPIRY_TICKS);
			return 1;
		}
		printf("native_arcade_discovery_service_test: paired by step %u; B lost the pairing %u ticks after A closed\n",
		       (unsigned)pairedStep, (unsigned)after);
	}
	CHECK(NativeArcadeDiscoveryService_TakeEvent(&s_b, &event));
	CHECK((event.type == NATIVE_ARCADE_DISCOVERY_EVENT_PAIR_LOST) && (event.peerNonce == NONCE_A));

	/* A closed port is free again: A reopens on it. */
	CHECK(NativeArcadeDiscoveryService_Open(&s_a, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, 0u, &toB, 1u, 0u, 0u));
	NativeArcadeDiscoveryService_Close(&s_a);
	NativeArcadeDiscoveryService_Close(&s_a);
	NativeArcadeDiscoveryService_Close(&s_b);
	NativeArcadeDiscoveryService_Close(&s_b);
	return 0;
}

/* DISC-19: both pinned to 127.0.0.0/8 with explicit targets inside it pair
 * as without a lan; the filter drops nothing; no interface list is read. */
static int TestLanPairsOnLoopback(void)
{
	const struct NativeUdpTransportAddress toB = {LOOPBACK, PORT_B};
	const struct NativeUdpTransportAddress toA = {LOOPBACK, PORT_A};
	struct NativeArcadeDiscoveryServiceStatus status;
	struct NativeArcadeDiscoveryPairing pairingA;
	struct NativeArcadeDiscoveryPairing pairingB;
	uint32_t interfaceIpv4 = 0xA5A5A5A5u;
	uint32_t step = 0;

	CHECK(NativeArcadeDiscoveryService_Open(&s_a, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, NATIVE_ARCADE_DISCOVERY_SEAT_AUTO, &toB, 1u, LAN_LOOPBACK, 8u));
	CHECK(NativeArcadeDiscoveryService_Open(&s_b, PORT_B, NONCE_B, 1u, k_identity, LINK_PORT_B, NATIVE_ARCADE_DISCOVERY_SEAT_AUTO, &toA, 1u, LAN_LOOPBACK, 8u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	CHECK((status.open == 1) && (status.overridden == 1) && (status.targetCount == 1u) && (status.refreshCount == 0u));
	CHECK((status.lanPrefixLength == 8u) && (status.lanNetwork == LAN_LOOPBACK) && (status.lanInterfaceIpv4 == 0u) && (status.lanDropped == 0u));
	/* Explicit targets: no list is read, so there is no lan interface to report. */
	CHECK(!NativeArcadeDiscoveryService_TakeLanChange(&s_a, &interfaceIpv4));
	CHECK(interfaceIpv4 == 0xA5A5A5A5u);

	CHECK(StepUntilBothPaired(&s_a, &s_b, &step));
	CHECK(NativeArcadeDiscoveryService_Pairing(&s_a, &pairingA));
	CHECK(NativeArcadeDiscoveryService_Pairing(&s_b, &pairingB));
	CHECK((pairingA.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB1) && (pairingB.localSeat == NATIVE_ARCADE_DISCOVERY_SEAT_CAB2));
	CHECK((pairingA.peerIpv4 == LOOPBACK) && (pairingA.peerLinkPort == LINK_PORT_B));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	CHECK((status.lanDropped == 0u) && (status.beaconCount >= 1u) && (status.sendFailures == 0u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_b, &status));
	CHECK(status.lanDropped == 0u);
	printf("native_arcade_discovery_service_test: lan 127.0.0.0/8 paired by step %u\n", (unsigned)step);
	NativeArcadeDiscoveryService_Close(&s_a);
	NativeArcadeDiscoveryService_Close(&s_b);
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	CHECK((status.open == 0) && (status.lanPrefixLength == 0u) && (status.lanNetwork == 0u));
	return 0;
}

/*
 * DISC-19, the source filter and the no-interface state: A is pinned to a
 * lan that holds neither the loopback sender nor any local interface, with
 * no explicit target. It finds no interface inside the lan, reports that
 * once, has no target, and sends nothing (no beacon round). B (no lan)
 * beacons at A over loopback: A drops every one of them before the core
 * (counted, no table entry), so neither side ever pairs.
 */
static int TestLanDropsOutsideSource(void)
{
	const struct NativeUdpTransportAddress toA = {LOOPBACK, PORT_A};
	struct NativeArcadeDiscoveryServiceStatus status;
	uint32_t interfaceIpv4 = 0xA5A5A5A5u;
	uint32_t step = 0;
	ULONGLONG deadline;

	CHECK(NativeArcadeDiscoveryService_Open(&s_a, PORT_A, NONCE_A, 1u, k_identity, LINK_PORT_A, NATIVE_ARCADE_DISCOVERY_SEAT_AUTO, NULL, 0u, LAN_ABSENT, 24u));
	CHECK(NativeArcadeDiscoveryService_Open(&s_b, PORT_B, NONCE_B, 1u, k_identity, LINK_PORT_B, NATIVE_ARCADE_DISCOVERY_SEAT_AUTO, &toA, 1u, 0u, 0u));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	if (status.lanInterfaceIpv4 != 0u)
	{
		fprintf(stderr, "this machine has an interface in 203.0.113.0/24 (TEST-NET-3); the no-interface case cannot run\n");
		return 1;
	}
	CHECK((status.open == 1) && (status.overridden == 0) && (status.refreshCount == 1u) && (status.targetCount == 0u));
	CHECK((status.lanPrefixLength == 24u) && (status.lanNetwork == LAN_ABSENT));
	/* The first read is reported once: no interface inside the lan. */
	CHECK(NativeArcadeDiscoveryService_TakeLanChange(&s_a, &interfaceIpv4));
	CHECK(interfaceIpv4 == 0u);
	interfaceIpv4 = 0xA5A5A5A5u;
	CHECK(!NativeArcadeDiscoveryService_TakeLanChange(&s_a, &interfaceIpv4));
	CHECK(!NativeArcadeDiscoveryService_TakeLanChange(&s_a, NULL));
	CHECK(interfaceIpv4 == 0xA5A5A5A5u);

	/* B's beacons reach A and are dropped: wait for the first drop. */
	deadline = GetTickCount64() + POLL_BOUND_MS;
	do
	{
		Step(&s_a, &s_b, &step);
		CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	} while ((status.lanDropped == 0u) && (GetTickCount64() < deadline));
	CHECK(status.lanDropped >= 1u);
	/* Three more beacon intervals: still nothing reaches A's table, A sends
	 * nothing, and neither side pairs. */
	for (uint32_t i = 0; i < 3u * NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS; i++)
	{
		Step(&s_a, &s_b, &step);
		CHECK(!Paired(&s_a) && !Paired(&s_b));
	}
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_a, &status));
	CHECK((status.beaconCount == 0u) && (status.sendFailures == 0u) && (status.targetCount == 0u) && (status.lanDropped >= 2u));
	for (uint32_t i = 0; i < NATIVE_ARCADE_DISCOVERY_TABLE_SIZE; i++)
	{
		CHECK(s_a.table.peers[i].used == 0u);
	}
	CHECK(!NativeArcadeDiscoveryService_TakeLanChange(&s_a, &interfaceIpv4));
	CHECK(NativeArcadeDiscoveryService_GetStatus(&s_b, &status));
	CHECK((status.beaconCount >= 4u) && (status.lanDropped == 0u));
	printf("native_arcade_discovery_service_test: lan 203.0.113.0/24: no interface, nothing sent, %u beacons from outside dropped\n",
	       (unsigned)s_a.lanDropped);
	NativeArcadeDiscoveryService_Close(&s_a);
	NativeArcadeDiscoveryService_Close(&s_b);
	return 0;
}

int main(void)
{
	CHECK(TestClosedAndArguments() == 0);
	CHECK(TestEnumeratedTargets() == 0);
	CHECK(TestRefreshHeld() == 0);
	CHECK(TestPairOnLoopback() == 0);
	CHECK(TestLanPairsOnLoopback() == 0);
	CHECK(TestLanDropsOutsideSource() == 0);
	puts("native_arcade_discovery_service_test: ok");
	return 0;
}
