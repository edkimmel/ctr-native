#include "platform/native_udp_transport.h"

#include "platform/native_win32.h"

#include <stdio.h>
#include <stdlib.h>

/* Standalone helper process spawned by native_udp_transport_process_test.
 * argv[1] is the exact UDP port to bind on 127.0.0.1 (never 0: the parent
 * needs to know it in advance).  It waits (bounded by wall-clock time,
 * test-only glue, not part of the deterministic simulation stack) for one
 * datagram, echoes the exact bytes back to the sender, then exits 0.  On
 * timeout with nothing received it exits 1. */
int main(int argc, char **argv)
{
	struct NativeUdpTransport transport = {0};
	uint8_t buffer[2048];
	size_t byteCount;
	struct NativeUdpTransportAddress sender;
	long port;
	DWORD start;

	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		return 1;
	}

	port = strtol(argv[1], NULL, 10);
	if (port <= 0 || port > 65535)
	{
		fprintf(stderr, "native_udp_transport_echo_helper: invalid port\n");
		return 1;
	}

	if (!NativeUdpTransport_GlobalInit())
	{
		fprintf(stderr, "native_udp_transport_echo_helper: GlobalInit failed\n");
		return 1;
	}

	if (!NativeUdpTransport_Open(&transport, (uint16_t)port))
	{
		fprintf(stderr, "native_udp_transport_echo_helper: Open failed\n");
		NativeUdpTransport_GlobalShutdown();
		return 1;
	}

	start = GetTickCount();
	for (;;)
	{
		enum NativeUdpTransportReceiveResult result;
		byteCount = sizeof(buffer);
		result = NativeUdpTransport_Receive(&transport, buffer, sizeof(buffer), &byteCount, &sender);
		if (result == NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			NativeUdpTransport_Send(&transport, &sender, buffer, byteCount);
			NativeUdpTransport_Close(&transport);
			NativeUdpTransport_GlobalShutdown();
			return 0;
		}
		if (GetTickCount() - start > 5000u)
			break;
		Sleep(5);
	}

	fprintf(stderr, "native_udp_transport_echo_helper: timed out waiting for a datagram\n");
	NativeUdpTransport_Close(&transport);
	NativeUdpTransport_GlobalShutdown();
	return 1;
}
