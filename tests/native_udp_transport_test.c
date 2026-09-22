#include "platform/native_udp_transport.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/* Real loopback delivery is asynchronous relative to sendto returning; poll
 * a bounded number of times rather than assuming immediate availability. */
static enum NativeUdpTransportReceiveResult PollReceive(struct NativeUdpTransport *transport,
	void *bytesOut, size_t capacity, size_t *byteCountOut, struct NativeUdpTransportAddress *senderOut)
{
	int attempt;
	enum NativeUdpTransportReceiveResult result;
	for (attempt = 0; attempt < 20000; attempt++)
	{
		result = NativeUdpTransport_Receive(transport, bytesOut, capacity, byteCountOut, senderOut);
		if (result != NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY)
			return result;
	}
	return result;
}

int main(void)
{
	struct NativeUdpTransport a = {0};
	struct NativeUdpTransport b = {0};
	struct NativeUdpTransport c = {0};
	struct NativeUdpTransport d = {0};
	struct NativeUdpTransport neverOpened = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeUdpTransportAddress sender;
	struct NativeUdpTransportAddress addr;
	uint16_t portA;
	uint16_t portB;
	uint8_t payload[] = { 1u, 2u, 3u, 4u, 5u };
	uint8_t big[600];
	uint8_t received[64];
	uint8_t guarded[32];
	size_t receivedBytes;
	size_t index;
	enum NativeUdpTransportReceiveResult result;

	CHECK(NativeUdpTransport_GlobalInit());

	/* GlobalInit/GlobalShutdown are ref-counted and safe to call more than
	 * once; this leaves the outer ref (above) still active. */
	CHECK(NativeUdpTransport_GlobalInit());
	NativeUdpTransport_GlobalShutdown();

	CHECK(NativeUdpTransport_Open(&a, 0));
	CHECK(NativeUdpTransport_Open(&b, 0));
	portA = NativeUdpTransport_LocalPort(&a);
	portB = NativeUdpTransport_LocalPort(&b);
	CHECK(portA != 0u);
	CHECK(portB != 0u);
	CHECK(portA != portB);

	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", portA));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", portB));

	CHECK(NativeUdpTransport_Send(&a, &addrB, payload, sizeof(payload)));
	memset(&sender, 0, sizeof(sender));
	receivedBytes = sizeof(received);
	CHECK(PollReceive(&b, received, sizeof(received), &receivedBytes, &sender) ==
		NATIVE_UDP_TRANSPORT_RECEIVE_OK);
	CHECK(receivedBytes == sizeof(payload));
	CHECK(memcmp(received, payload, sizeof(payload)) == 0);
	CHECK(sender.ipv4 == 0x7F000001u);
	CHECK(sender.port == portA);

	/* Nothing sent to c: Receive must report EMPTY, not OK/ERROR. */
	CHECK(NativeUdpTransport_Open(&c, 0));
	receivedBytes = sizeof(received);
	CHECK(NativeUdpTransport_Receive(&c, received, sizeof(received), &receivedBytes, NULL) ==
		NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY);

	/* A datagram larger than the caller buffer: TOO_SMALL, and the write
	 * must never cross the caller-declared capacity.  guarded[16..31] is a
	 * sentinel region outside the 16-byte capacity passed to Receive. */
	for (index = 0u; index < sizeof(big); index++)
		big[index] = (uint8_t)index;
	CHECK(NativeUdpTransport_Send(&a, &addrB, big, sizeof(big)));
	memset(guarded, 0xAA, sizeof(guarded));
	receivedBytes = 0xFFu;
	result = PollReceive(&b, guarded, 16u, &receivedBytes, NULL);
	CHECK(result == NATIVE_UDP_TRANSPORT_RECEIVE_TOO_SMALL);
	CHECK(receivedBytes == 0u);
	for (index = 16u; index < sizeof(guarded); index++)
		CHECK(guarded[index] == 0xAAu);

	/* Send from a transport that was never Opened: fails, does not crash. */
	CHECK(!NativeUdpTransport_Send(&neverOpened, &addrB, payload, sizeof(payload)));

	/* Close on a never-opened transport is a safe no-op. */
	NativeUdpTransport_Close(&neverOpened);
	NativeUdpTransport_Close(&neverOpened);

	/* Close twice on an opened-then-closed transport is a safe no-op, and
	 * Send/Receive after Close fail cleanly. */
	CHECK(NativeUdpTransport_Open(&d, 0));
	NativeUdpTransport_Close(&d);
	NativeUdpTransport_Close(&d);
	CHECK(!NativeUdpTransport_Send(&d, &addrB, payload, sizeof(payload)));
	receivedBytes = sizeof(received);
	CHECK(NativeUdpTransport_Receive(&d, received, sizeof(received), &receivedBytes, NULL) ==
		NATIVE_UDP_TRANSPORT_RECEIVE_ERROR);

	/* MakeAddress argument validation. */
	CHECK(!NativeUdpTransport_MakeAddress(NULL, "127.0.0.1", 1234u));
	CHECK(!NativeUdpTransport_MakeAddress(&addr, NULL, 1234u));
	CHECK(!NativeUdpTransport_MakeAddress(&addr, "", 1234u));
	CHECK(!NativeUdpTransport_MakeAddress(&addr, "not-an-ip", 1234u));
	CHECK(!NativeUdpTransport_MakeAddress(&addr, "256.0.0.1", 1234u));
	CHECK(NativeUdpTransport_MakeAddress(&addr, "127.0.0.1", 1234u));
	CHECK(addr.ipv4 == 0x7F000001u && addr.port == 1234u);
	CHECK(NativeUdpTransport_MakeAddress(&addr, "0.0.0.0", 4321u));
	CHECK(addr.ipv4 == 0u && addr.port == 4321u);

	NativeUdpTransport_Close(&a);
	NativeUdpTransport_Close(&b);
	NativeUdpTransport_Close(&c);

	/* Safe even when called past the matching GlobalInit ref count. */
	NativeUdpTransport_GlobalShutdown();
	NativeUdpTransport_GlobalShutdown();

	puts("native_udp_transport_test: passed");
	return 0;
}
