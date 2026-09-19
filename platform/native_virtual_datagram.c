#include "platform/native_virtual_datagram.h"

#include <limits.h>
#include <string.h>

static int EndpointValid(uint32_t endpoint)
{
	return endpoint < 2u;
}

static int StorageValid(const struct NativeVirtualDatagramPair *pair)
{
	return pair && pair->slots && pair->payloadStorage && pair->queueCapacity != 0u &&
		pair->payloadCapacity != 0u && pair->queueCapacity <= SIZE_MAX / pair->payloadCapacity &&
		pair->queueCapacity <= SIZE_MAX / sizeof(*pair->slots);
}

static int RouteValid(const struct NativeVirtualDatagramPair *pair,
	const struct NativeVirtualDatagramRoute *route)
{
	if (!pair || !route)
		return 0;
	switch (route->action)
	{
		case NATIVE_VIRTUAL_DATAGRAM_DROP:
			return route->firstDeliveryStep == 0u && route->secondDeliveryStep == 0u;
		case NATIVE_VIRTUAL_DATAGRAM_DELIVER:
			return route->firstDeliveryStep >= pair->currentStep && route->secondDeliveryStep == 0u;
		case NATIVE_VIRTUAL_DATAGRAM_DUPLICATE:
			return route->firstDeliveryStep >= pair->currentStep &&
				route->secondDeliveryStep >= pair->currentStep;
		default:
			return 0;
	}
}

static size_t FreeSlots(const struct NativeVirtualDatagramPair *pair)
{
	size_t index, count = 0u;
	for (index = 0u; index < pair->queueCapacity; index++)
		if (!pair->slots[index].occupied)
			count++;
	return count;
}

static int ComesBefore(const struct NativeVirtualDatagramSlot *left,
	const struct NativeVirtualDatagramSlot *right)
{
	if (left->deliveryStep != right->deliveryStep)
		return left->deliveryStep < right->deliveryStep;
	if (left->serial != right->serial)
		return left->serial < right->serial;
	return left->deliveryOrdinal < right->deliveryOrdinal;
}

static size_t FirstFreeSlot(const struct NativeVirtualDatagramPair *pair)
{
	size_t index;
	for (index = 0u; index < pair->queueCapacity; index++)
		if (!pair->slots[index].occupied)
			return index;
	return pair->queueCapacity;
}

static void WriteSlot(struct NativeVirtualDatagramPair *pair, size_t index,
	uint32_t sender, uint64_t serial, uint32_t deliveryOrdinal,
	uint64_t deliveryStep, const void *bytes, size_t byteCount)
{
	struct NativeVirtualDatagramSlot *slot = &pair->slots[index];
	uint8_t *payload = pair->payloadStorage + index * pair->payloadCapacity;
	if (byteCount != 0u)
		memcpy(payload, bytes, byteCount);
	slot->deliveryStep = deliveryStep;
	slot->serial = serial;
	slot->byteCount = byteCount;
	slot->sender = sender;
	slot->destination = 1u - sender;
	slot->deliveryOrdinal = deliveryOrdinal;
	slot->occupied = 1;
}

int NativeVirtualDatagramPair_Init(struct NativeVirtualDatagramPair *pair,
	struct NativeVirtualDatagramSlot *slots, size_t queueCapacity,
	uint8_t *payloadStorage, size_t payloadCapacity)
{
	struct NativeVirtualDatagramPair candidate;
	if (!pair || !slots || !payloadStorage || queueCapacity == 0u || payloadCapacity == 0u ||
		queueCapacity > SIZE_MAX / payloadCapacity || queueCapacity > SIZE_MAX / sizeof(*slots))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.slots = slots;
	candidate.payloadStorage = payloadStorage;
	candidate.queueCapacity = queueCapacity;
	candidate.payloadCapacity = payloadCapacity;
	memset(slots, 0, queueCapacity * sizeof(*slots));
	*pair = candidate;
	return 1;
}

int NativeVirtualDatagramPair_AdvanceTo(struct NativeVirtualDatagramPair *pair,
	uint64_t deliveryStep)
{
	if (!StorageValid(pair) || deliveryStep < pair->currentStep)
		return 0;
	pair->currentStep = deliveryStep;
	return 1;
}

int NativeVirtualDatagramPair_Send(struct NativeVirtualDatagramPair *pair,
	uint32_t sender, const void *bytes, size_t byteCount,
	const struct NativeVirtualDatagramRoute *route)
{
	size_t required, first, second;
	uint64_t serial;
	if (!StorageValid(pair) || !EndpointValid(sender) || (byteCount != 0u && !bytes) ||
		byteCount > pair->payloadCapacity || !RouteValid(pair, route) ||
		pair->nextSerial == UINT64_MAX)
		return 0;
	if (route->action == NATIVE_VIRTUAL_DATAGRAM_DROP)
	{
		pair->nextSerial++;
		return 1;
	}
	required = route->action == NATIVE_VIRTUAL_DATAGRAM_DUPLICATE ? 2u : 1u;
	if (FreeSlots(pair) < required)
		return 0;
	first = FirstFreeSlot(pair);
	second = required == 2u ? FirstFreeSlot(pair) : pair->queueCapacity;
	/* Reserve the first slot locally while locating the second; the write below
	 * is the first externally visible mutation after all failure checks. */
	if (required == 2u)
	{
		pair->slots[first].occupied = 1;
		second = FirstFreeSlot(pair);
		pair->slots[first].occupied = 0;
	}
	if (first == pair->queueCapacity ||
		(required == 2u && second == pair->queueCapacity))
		return 0;
	serial = ++pair->nextSerial;
	WriteSlot(pair, first, sender, serial, 0u, route->firstDeliveryStep, bytes, byteCount);
	if (required == 2u)
		WriteSlot(pair, second, sender, serial, 1u, route->secondDeliveryStep, bytes, byteCount);
	return 1;
}

enum NativeVirtualDatagramReceiveResult NativeVirtualDatagramPair_Receive(
	struct NativeVirtualDatagramPair *pair, uint32_t destination, void *bytesOut,
	size_t *byteCountInOut, struct NativeVirtualDatagramMetadata *metadataOut)
{
	size_t index, selected;
	struct NativeVirtualDatagramSlot *slot;
	uint8_t *payload;
	if (!StorageValid(pair) || !EndpointValid(destination) || !byteCountInOut)
		return NATIVE_VIRTUAL_DATAGRAM_RECEIVE_INVALID;
	selected = pair->queueCapacity;
	for (index = 0u; index < pair->queueCapacity; index++)
	{
		slot = &pair->slots[index];
		if (slot->occupied && slot->destination == destination &&
			slot->deliveryStep <= pair->currentStep &&
			(selected == pair->queueCapacity || ComesBefore(slot, &pair->slots[selected])))
			selected = index;
	}
	if (selected == pair->queueCapacity)
		return NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY;
	slot = &pair->slots[selected];
	if (*byteCountInOut < slot->byteCount)
	{
		*byteCountInOut = slot->byteCount;
		return NATIVE_VIRTUAL_DATAGRAM_RECEIVE_TOO_SMALL;
	}
	if (slot->byteCount != 0u && !bytesOut)
		return NATIVE_VIRTUAL_DATAGRAM_RECEIVE_INVALID;
	payload = pair->payloadStorage + selected * pair->payloadCapacity;
	if (slot->byteCount != 0u)
		memcpy(bytesOut, payload, slot->byteCount);
	*byteCountInOut = slot->byteCount;
	if (metadataOut)
	{
		metadataOut->sender = slot->sender;
		metadataOut->deliveryStep = slot->deliveryStep;
		metadataOut->serial = slot->serial;
		metadataOut->deliveryOrdinal = slot->deliveryOrdinal;
		metadataOut->byteCount = slot->byteCount;
	}
	memset(slot, 0, sizeof(*slot));
	return NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK;
}
