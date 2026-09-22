#include "platform/native_udp_transport.h"

#include "platform/native_win32.h"

/* native_win32.h undefines the legacy far/near qualifiers so windows.h
 * cannot rewrite PS1 SDK function/variable names elsewhere in this
 * repository's single-translation-unit build.  winsock2.h's FAR-decorated
 * prototypes still expect far/near defined to nothing, so restore them
 * locally around the two socket headers only, then undefine again
 * immediately after to preserve native_win32.h's invariant for the rest of
 * this file (and any translation unit that includes this header later). */
#ifndef far
#define far
#endif
#ifndef near
#define near
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#undef far
#undef near

#include <limits.h>
#include <string.h>

/* Ref-counted WSAStartup/WSACleanup pair.  Process-local; never negative. */
static int gNativeUdpTransportGlobalInitCount = 0;

int NativeUdpTransport_GlobalInit(void)
{
	WSADATA data;
	if (gNativeUdpTransportGlobalInitCount > 0)
	{
		gNativeUdpTransportGlobalInitCount++;
		return 1;
	}
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		return 0;
	gNativeUdpTransportGlobalInitCount = 1;
	return 1;
}

void NativeUdpTransport_GlobalShutdown(void)
{
	if (gNativeUdpTransportGlobalInitCount <= 0)
		return;
	gNativeUdpTransportGlobalInitCount--;
	if (gNativeUdpTransportGlobalInitCount == 0)
		WSACleanup();
}

int NativeUdpTransport_MakeAddress(struct NativeUdpTransportAddress *addr,
	const char *ipv4Dotted, uint16_t port)
{
	struct in_addr parsed;
	if (!addr || !ipv4Dotted || ipv4Dotted[0] == '\0')
		return 0;
	if (InetPtonA(AF_INET, ipv4Dotted, &parsed) != 1)
		return 0;
	addr->ipv4 = ntohl(parsed.s_addr);
	addr->port = port;
	return 1;
}

int NativeUdpTransport_Open(struct NativeUdpTransport *transport, uint16_t localPort)
{
	SOCKET sock;
	struct sockaddr_in address;
	u_long nonBlocking = 1;
	int addressLength;

	if (!transport)
		return 0;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
		return 0;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(localPort);
	if (bind(sock, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return 0;
	}

	if (ioctlsocket(sock, FIONBIO, &nonBlocking) == SOCKET_ERROR)
	{
		closesocket(sock);
		return 0;
	}

	if (localPort == 0u)
	{
		memset(&address, 0, sizeof(address));
		addressLength = (int)sizeof(address);
		if (getsockname(sock, (struct sockaddr *)&address, &addressLength) == SOCKET_ERROR)
		{
			closesocket(sock);
			return 0;
		}
		localPort = ntohs(address.sin_port);
	}

	transport->socketHandle = (uintptr_t)sock;
	transport->localPort = localPort;
	transport->open = 1;
	return 1;
}

uint16_t NativeUdpTransport_LocalPort(const struct NativeUdpTransport *transport)
{
	if (!transport || !transport->open)
		return 0u;
	return transport->localPort;
}

int NativeUdpTransport_Send(struct NativeUdpTransport *transport,
	const struct NativeUdpTransportAddress *destination, const void *bytes, size_t byteCount)
{
	struct sockaddr_in address;
	int sent;

	if (!transport || !transport->open || !destination)
		return 0;
	if (byteCount != 0u && !bytes)
		return 0;
	if (byteCount > (size_t)INT_MAX)
		return 0;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(destination->ipv4);
	address.sin_port = htons(destination->port);

	sent = sendto((SOCKET)transport->socketHandle, (const char *)bytes, (int)byteCount, 0,
		(struct sockaddr *)&address, sizeof(address));
	if (sent == SOCKET_ERROR || (size_t)sent != byteCount)
		return 0;
	return 1;
}

enum NativeUdpTransportReceiveResult NativeUdpTransport_Receive(struct NativeUdpTransport *transport,
	void *bytesOut, size_t capacity, size_t *byteCountOut, struct NativeUdpTransportAddress *senderOut)
{
	struct sockaddr_in address;
	int addressLength;
	int received;
	int lastError;

	if (!transport || !transport->open || !byteCountOut || (capacity != 0u && !bytesOut) ||
		capacity > (size_t)INT_MAX)
		return NATIVE_UDP_TRANSPORT_RECEIVE_ERROR;

	memset(&address, 0, sizeof(address));
	addressLength = (int)sizeof(address);
	received = recvfrom((SOCKET)transport->socketHandle, (char *)bytesOut, (int)capacity, 0,
		(struct sockaddr *)&address, &addressLength);

	if (received == SOCKET_ERROR)
	{
		lastError = WSAGetLastError();
		*byteCountOut = 0u;
		if (lastError == WSAEWOULDBLOCK)
			return NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY;
		if (lastError == WSAEMSGSIZE)
		{
			if (senderOut)
			{
				senderOut->ipv4 = ntohl(address.sin_addr.s_addr);
				senderOut->port = ntohs(address.sin_port);
			}
			return NATIVE_UDP_TRANSPORT_RECEIVE_TOO_SMALL;
		}
		return NATIVE_UDP_TRANSPORT_RECEIVE_ERROR;
	}

	*byteCountOut = (size_t)received;
	if (senderOut)
	{
		senderOut->ipv4 = ntohl(address.sin_addr.s_addr);
		senderOut->port = ntohs(address.sin_port);
	}
	return NATIVE_UDP_TRANSPORT_RECEIVE_OK;
}

void NativeUdpTransport_Close(struct NativeUdpTransport *transport)
{
	if (!transport || !transport->open)
		return;
	closesocket((SOCKET)transport->socketHandle);
	transport->socketHandle = 0;
	transport->localPort = 0;
	transport->open = 0;
}
