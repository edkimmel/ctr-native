#ifndef PLATFORM_NATIVE_UDP_TRANSPORT_H
#define PLATFORM_NATIVE_UDP_TRANSPORT_H

/*
 * Real-socket, leaf byte-mover over a Winsock2 UDP socket. Caller-owned
 * buffers only, nothing carved from the heap at runtime, no packet/protocol
 * format of its own, and no dependency on any higher simulation-identity
 * layer (frame sync, match config, canonical state, or topology lease).
 * This is the real-socket analog of the test-only
 * platform/native_virtual_datagram.c: it moves opaque bytes over a real OS
 * transport, nothing else.
 */
#include <stddef.h>
#include <stdint.h>

enum NativeUdpTransportReceiveResult
{
	NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY = 0,
	NATIVE_UDP_TRANSPORT_RECEIVE_OK = 1,
	NATIVE_UDP_TRANSPORT_RECEIVE_TOO_SMALL = 2,
	NATIVE_UDP_TRANSPORT_RECEIVE_ERROR = 3
};

struct NativeUdpTransportAddress
{
	uint32_t ipv4; /* host byte order, e.g. 0x7F000001 for 127.0.0.1 */
	uint16_t port; /* host byte order */
};

/* socketHandle is a raw OS socket handle stored as a wide-enough integer so
 * this header never has to pull in windows.h/winsock2.h for every consumer.
 * No pointers, nothing carved from the heap. */
struct NativeUdpTransport
{
	uintptr_t socketHandle;
	uint16_t localPort;
	int open;
};

/* WSAStartup, ref-counted with a static counter; safe to call more than
 * once.  Returns 0 on failure. */
int NativeUdpTransport_GlobalInit(void);

/* WSACleanup on the matching call; must not go negative. */
void NativeUdpTransport_GlobalShutdown(void);

/* Parses a plain dotted-quad string such as "127.0.0.1".  Rejects NULL,
 * malformed input, and hostnames: this module never resolves names, only
 * literal IPv4 addresses. */
int NativeUdpTransport_MakeAddress(struct NativeUdpTransportAddress *addr,
	const char *ipv4Dotted, uint16_t port);

/* Creates a UDP socket, binds it to INADDR_ANY:localPort (localPort 0 asks
 * the OS for an ephemeral port), and puts it in non-blocking mode.  If
 * localPort was 0, the actually bound port is read back and stored so the
 * caller can learn it through NativeUdpTransport_LocalPort. */
int NativeUdpTransport_Open(struct NativeUdpTransport *transport, uint16_t localPort);

uint16_t NativeUdpTransport_LocalPort(const struct NativeUdpTransport *transport);

/* Returns 0 on any failure (not open, WSA error, oversize for a UDP
 * datagram) without destructively touching transport state. */
int NativeUdpTransport_Send(struct NativeUdpTransport *transport,
	const struct NativeUdpTransportAddress *destination, const void *bytes, size_t byteCount);

/* Non-blocking receive.  WSAEWOULDBLOCK maps to EMPTY, not ERROR.
 * TOO_SMALL means the datagram was too big for capacity; UDP has already
 * discarded the excess, so byteCountOut is set to 0 and this is
 * informational only -- there is no lost-byte recovery.  senderOut, if
 * non-NULL, is filled with the sender address whenever a datagram was
 * actually received (OK or TOO_SMALL). */
enum NativeUdpTransportReceiveResult NativeUdpTransport_Receive(struct NativeUdpTransport *transport,
	void *bytesOut, size_t capacity, size_t *byteCountOut, struct NativeUdpTransportAddress *senderOut);

/* Safe to call on an already closed or zero-initialized transport, and safe
 * to call twice in a row. */
void NativeUdpTransport_Close(struct NativeUdpTransport *transport);

#endif
