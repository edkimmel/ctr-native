#ifndef PLATFORM_NATIVE_VIRTUAL_DATAGRAM_H
#define PLATFORM_NATIVE_VIRTUAL_DATAGRAM_H

/*
 * Deterministic, source-only delivery harness for later lockstep fault tests.
 * It routes caller-owned opaque bytes between exactly two endpoints using only
 * caller-authored virtual delivery steps.  It has no wire encoding, OS
 * transport, peer search, or version commitment.
 */
#include <stddef.h>
#include <stdint.h>

enum NativeVirtualDatagramAction
{
	NATIVE_VIRTUAL_DATAGRAM_DROP = 0,
	NATIVE_VIRTUAL_DATAGRAM_DELIVER = 1,
	NATIVE_VIRTUAL_DATAGRAM_DUPLICATE = 2
};

enum NativeVirtualDatagramReceiveResult
{
	NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY = 0,
	NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK = 1,
	NATIVE_VIRTUAL_DATAGRAM_RECEIVE_TOO_SMALL = 2,
	NATIVE_VIRTUAL_DATAGRAM_RECEIVE_INVALID = 3
};

struct NativeVirtualDatagramRoute
{
	enum NativeVirtualDatagramAction action;
	uint64_t firstDeliveryStep;
	uint64_t secondDeliveryStep;
};

struct NativeVirtualDatagramMetadata
{
	uint32_t sender;
	uint64_t deliveryStep;
	uint64_t serial;
	uint32_t deliveryOrdinal;
	size_t byteCount;
};

/* Slots and byte storage are wholly caller-owned.  Storage supplies
 * queueCapacity contiguous regions of payloadCapacity bytes each.  Send
 * input and Receive output must not overlap that storage. */
struct NativeVirtualDatagramSlot
{
	uint64_t deliveryStep;
	uint64_t serial;
	size_t byteCount;
	uint32_t sender;
	uint32_t destination;
	uint32_t deliveryOrdinal;
	int occupied;
};

struct NativeVirtualDatagramPair
{
	struct NativeVirtualDatagramSlot *slots;
	uint8_t *payloadStorage;
	size_t queueCapacity;
	size_t payloadCapacity;
	uint64_t currentStep;
	uint64_t nextSerial;
};

int NativeVirtualDatagramPair_Init(struct NativeVirtualDatagramPair *pair,
	struct NativeVirtualDatagramSlot *slots, size_t queueCapacity,
	uint8_t *payloadStorage, size_t payloadCapacity);

/* Advances virtual time monotonically.  No wall clock participates. */
int NativeVirtualDatagramPair_AdvanceTo(struct NativeVirtualDatagramPair *pair,
	uint64_t deliveryStep);

/* Snapshots one opaque payload.  DROP creates no queued delivery; DELIVER
 * creates one; DUPLICATE creates two.  The route fully controls delivery
 * steps, so reordering is explicit and reproducible. */
int NativeVirtualDatagramPair_Send(struct NativeVirtualDatagramPair *pair,
	uint32_t sender, const void *bytes, size_t byteCount,
	const struct NativeVirtualDatagramRoute *route);

/* Delivers the earliest ready record for destination by (delivery step,
 * send serial, duplicate ordinal).  TOO_SMALL reports the required byte
 * count but leaves the queued record intact. */
enum NativeVirtualDatagramReceiveResult NativeVirtualDatagramPair_Receive(
	struct NativeVirtualDatagramPair *pair, uint32_t destination, void *bytesOut,
	size_t *byteCountInOut, struct NativeVirtualDatagramMetadata *metadataOut);

#endif
