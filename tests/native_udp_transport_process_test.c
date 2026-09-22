#include "platform/native_udp_transport.h"

#include "platform/native_win32.h"

#include <stdio.h>
#include <string.h>

/* Fixed loopback test port in the 48000-48999 range reserved for this
 * repository's two-process real-socket proof (docs/LOBBY_MILESTONE.md
 * section 3, constraint 8): clear of the well-known service range and of
 * the typical Windows dynamic/ephemeral port range (49152-65535), so it is
 * unlikely to collide with another listener on the test machine. */
#define NATIVE_UDP_TRANSPORT_PROCESS_TEST_PORT 48037u

int main(int argc, char **argv)
{
	const char *helperPath;
	char commandLine[1024];
	STARTUPINFOA startupInfo;
	PROCESS_INFORMATION processInfo;
	struct NativeUdpTransport transport = {0};
	struct NativeUdpTransportAddress helperAddress;
	static const uint8_t payload[] = { 0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u };
	uint8_t echoed[sizeof(payload)];
	size_t echoedBytes = 0u;
	struct NativeUdpTransportAddress sender;
	int gotEcho = 0;
	int failed = 0;
	int attempt;
	DWORD waitResult;
	DWORD exitCode = 1u;

	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <helper-path>\n", argv[0]);
		return 1;
	}
	helperPath = argv[1];

	if (!NativeUdpTransport_GlobalInit())
	{
		fprintf(stderr, "native_udp_transport_process_test: GlobalInit failed\n");
		return 1;
	}

	if ((size_t)snprintf(commandLine, sizeof(commandLine), "\"%s\" %u", helperPath,
			NATIVE_UDP_TRANSPORT_PROCESS_TEST_PORT) >= sizeof(commandLine))
	{
		fprintf(stderr, "native_udp_transport_process_test: command line too long\n");
		NativeUdpTransport_GlobalShutdown();
		return 1;
	}

	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	memset(&processInfo, 0, sizeof(processInfo));
	memset(&sender, 0, sizeof(sender));

	if (!CreateProcessA(NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo))
	{
		fprintf(stderr, "native_udp_transport_process_test: CreateProcessA failed: %lu\n",
			(unsigned long)GetLastError());
		NativeUdpTransport_GlobalShutdown();
		return 1;
	}

	if (!NativeUdpTransport_Open(&transport, 0) ||
		!NativeUdpTransport_MakeAddress(&helperAddress, "127.0.0.1",
			(uint16_t)NATIVE_UDP_TRANSPORT_PROCESS_TEST_PORT))
	{
		fprintf(stderr, "native_udp_transport_process_test: failed to open local transport\n");
		failed = 1;
	}

	/* The child needs a moment to start and bind; retry the send roughly
	 * every 100 ms.  UDP sends to a not-yet-listening port are simply
	 * dropped, which is expected here, not an error. */
	for (attempt = 0; !failed && !gotEcho && attempt < 50; attempt++)
	{
		DWORD pollStart;
		NativeUdpTransport_Send(&transport, &helperAddress, payload, sizeof(payload));
		pollStart = GetTickCount();
		while (GetTickCount() - pollStart < 100u)
		{
			enum NativeUdpTransportReceiveResult result;
			echoedBytes = sizeof(echoed);
			result = NativeUdpTransport_Receive(&transport, echoed, sizeof(echoed), &echoedBytes, &sender);
			if (result == NATIVE_UDP_TRANSPORT_RECEIVE_OK)
			{
				gotEcho = 1;
				break;
			}
			Sleep(5);
		}
	}

	if (!failed && !gotEcho)
	{
		fprintf(stderr, "native_udp_transport_process_test: no echo received within retry budget\n");
		failed = 1;
	}
	if (!failed && (echoedBytes != sizeof(payload) || memcmp(echoed, payload, sizeof(payload)) != 0))
	{
		fprintf(stderr, "native_udp_transport_process_test: echoed payload mismatch\n");
		failed = 1;
	}

	waitResult = WaitForSingleObject(processInfo.hProcess, 5000);
	if (waitResult != WAIT_OBJECT_0)
	{
		fprintf(stderr, "native_udp_transport_process_test: helper did not exit in time\n");
		TerminateProcess(processInfo.hProcess, 1);
		failed = 1;
	}
	else if (!GetExitCodeProcess(processInfo.hProcess, &exitCode) || exitCode != 0u)
	{
		fprintf(stderr, "native_udp_transport_process_test: helper exit code %lu\n",
			(unsigned long)exitCode);
		failed = 1;
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	NativeUdpTransport_Close(&transport);
	NativeUdpTransport_GlobalShutdown();

	if (failed)
		return 1;

	puts("native_udp_transport_process_test: passed");
	return 0;
}
