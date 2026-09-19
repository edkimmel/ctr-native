#include "platform/native_virtual_datagram.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int StateSame(const struct NativeVirtualDatagramPair *pair,
	const struct NativeVirtualDatagramPair *before,
	const struct NativeVirtualDatagramSlot *slots,
	const struct NativeVirtualDatagramSlot *slotsBefore,
	const uint8_t *storage, const uint8_t *storageBefore,
	size_t slotCount, size_t payloadBytes)
{
	return memcmp(pair, before, sizeof(*pair)) == 0 &&
		memcmp(slots, slotsBefore, slotCount * sizeof(*slots)) == 0 &&
		memcmp(storage, storageBefore, payloadBytes) == 0;
}

static int Receive(struct NativeVirtualDatagramPair *pair, uint32_t destination,
	const uint8_t *expected, size_t expectedBytes, uint64_t expectedStep,
	uint64_t expectedSerial, uint32_t expectedOrdinal)
{
	uint8_t actual[16] = {0};
	size_t actualBytes = sizeof(actual);
	struct NativeVirtualDatagramMetadata metadata;
	CHECK(NativeVirtualDatagramPair_Receive(pair, destination, actual, &actualBytes, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
	CHECK(actualBytes == expectedBytes);
	CHECK(memcmp(actual, expected, expectedBytes) == 0);
	CHECK(metadata.sender == 1u - destination && metadata.deliveryStep == expectedStep &&
		metadata.serial == expectedSerial && metadata.deliveryOrdinal == expectedOrdinal &&
		metadata.byteCount == expectedBytes);
	return 0;
}

int main(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramPair exhausted;
	struct NativeVirtualDatagramSlot slots[4];
	struct NativeVirtualDatagramSlot exhaustedSlots[1];
	uint8_t storage[sizeof(slots) / sizeof(slots[0])][16];
	uint8_t exhaustedStorage[1][16];
	struct NativeVirtualDatagramRoute drop = { NATIVE_VIRTUAL_DATAGRAM_DROP, 0u, 0u };
	struct NativeVirtualDatagramRoute early = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 4u, 0u };
	struct NativeVirtualDatagramRoute late = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 7u, 0u };
	struct NativeVirtualDatagramRoute duplicate = { NATIVE_VIRTUAL_DATAGRAM_DUPLICATE, 8u, 7u };
	uint8_t first[] = { 1u, 2u, 3u };
	uint8_t second[] = { 4u };
	uint8_t third[] = { 5u, 6u };
	uint8_t maximum[16];
	uint8_t copy[16] = {0};
	size_t bytes;
	struct NativeVirtualDatagramMetadata metadata;
	struct NativeVirtualDatagramPair pairBefore;
	struct NativeVirtualDatagramSlot slotsBefore[4];
	uint8_t storageBefore[sizeof(storage)];

#define SAVE_STATE() do { pairBefore = pair; memcpy(slotsBefore, slots, sizeof(slots)); memcpy(storageBefore, storage, sizeof(storage)); } while (0)
#define CHECK_STATE() CHECK(StateSame(&pair, &pairBefore, slots, slotsBefore, &storage[0][0], storageBefore, 4u, sizeof(storage)))

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, 4u, &storage[0][0], sizeof(storage[0])));
	for (size_t index = 0u; index < sizeof(maximum); index++)
		maximum[index] = (uint8_t)index;
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 1u));
	CHECK(!NativeVirtualDatagramPair_AdvanceTo(&pair, 0u));
	CHECK(NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first), &drop));
	CHECK(pair.nextSerial == 1u);
	CHECK(NativeVirtualDatagramPair_Receive(&pair, 1u, copy, &(size_t){sizeof(copy)}, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY);
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 2u, first, sizeof(first), &early)); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, NULL, sizeof(first), &early)); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(storage[0]) + 1u, &early)); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first), NULL)); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ (enum NativeVirtualDatagramAction)99, 1u, 0u })); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DROP, 1u, 0u })); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DUPLICATE, 1u, 0u })); CHECK_STATE();
	SAVE_STATE(); CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 3u, 9u })); CHECK_STATE();

	CHECK(NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first), &late));
	CHECK(NativeVirtualDatagramPair_Send(&pair, 0u, second, sizeof(second), &early));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 4u));
	CHECK(Receive(&pair, 1u, second, sizeof(second), 4u, 3u, 0u) == 0);
	CHECK(NativeVirtualDatagramPair_Receive(&pair, 1u, copy, &(size_t){sizeof(copy)}, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY);
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 7u));
	bytes = 1u;
	memset(&metadata, 0x5a, sizeof(metadata));
	CHECK(NativeVirtualDatagramPair_Receive(&pair, 1u, copy, &bytes, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_TOO_SMALL);
	CHECK(bytes == sizeof(first));
	CHECK(metadata.serial == UINT64_C(0x5a5a5a5a5a5a5a5a));
	CHECK(Receive(&pair, 1u, first, sizeof(first), 7u, 2u, 0u) == 0);

	CHECK(NativeVirtualDatagramPair_Send(&pair, 0u, third, sizeof(third), &duplicate));
	third[0] = 99u;
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 7u));
	CHECK(Receive(&pair, 1u, (const uint8_t[]){5u, 6u}, 2u, 7u, 4u, 1u) == 0);
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 8u));
	CHECK(Receive(&pair, 1u, (const uint8_t[]){5u, 6u}, 2u, 8u, 4u, 0u) == 0);

	/* Equal virtual steps are ordered by send serial, then duplicate ordinal. */
	CHECK(NativeVirtualDatagramPair_Send(&pair, 1u, second, sizeof(second),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 9u, 0u }));
	CHECK(NativeVirtualDatagramPair_Send(&pair, 1u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 9u, 0u }));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 9u));
	CHECK(Receive(&pair, 0u, second, sizeof(second), 9u, 5u, 0u) == 0);
	CHECK(Receive(&pair, 0u, first, sizeof(first), 9u, 6u, 0u) == 0);
	CHECK(NativeVirtualDatagramPair_Send(&pair, 0u, third, sizeof(third),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DUPLICATE, 9u, 9u }));
	CHECK(Receive(&pair, 1u, third, sizeof(third), 9u, 7u, 0u) == 0);
	CHECK(Receive(&pair, 1u, third, sizeof(third), 9u, 7u, 1u) == 0);

	/* Resource and argument failure do not add a partial delivery. */
	CHECK(NativeVirtualDatagramPair_Send(&pair, 0u, NULL, 0u,
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 9u, 0u }));
	bytes = 0u;
	CHECK(NativeVirtualDatagramPair_Receive(&pair, 1u, NULL, &bytes, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK && bytes == 0u && metadata.serial == 8u);
	CHECK(NativeVirtualDatagramPair_Send(&pair, 1u, maximum, sizeof(maximum),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 9u, 0u }));
	bytes = sizeof(maximum);
	memset(&metadata, 0x5a, sizeof(metadata));
	SAVE_STATE();
	CHECK(NativeVirtualDatagramPair_Receive(&pair, 0u, NULL, &bytes, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_INVALID);
	CHECK(bytes == sizeof(maximum) && metadata.serial == UINT64_C(0x5a5a5a5a5a5a5a5a));
	CHECK_STATE();
	CHECK(Receive(&pair, 0u, maximum, sizeof(maximum), 9u, 9u, 0u) == 0);
	CHECK(NativeVirtualDatagramPair_Receive(&pair, 1u, copy, &(size_t){sizeof(copy)}, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY);

	CHECK(NativeVirtualDatagramPair_Init(&exhausted, exhaustedSlots, 1u,
		&exhaustedStorage[0][0], sizeof(exhaustedStorage[0])));
	CHECK(NativeVirtualDatagramPair_Send(&exhausted, 0u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u }));
	CHECK(!NativeVirtualDatagramPair_Send(&exhausted, 0u, second, sizeof(second),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DUPLICATE, 0u, 0u }));
	CHECK(exhausted.nextSerial == 1u);
	CHECK(Receive(&exhausted, 1u, first, sizeof(first), 0u, 1u, 0u) == 0);
	CHECK(NativeVirtualDatagramPair_Receive(&exhausted, 1u, copy, &(size_t){sizeof(copy)}, &metadata) ==
		NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY);
	SAVE_STATE();
	pair.nextSerial = UINT64_MAX;
	CHECK(!NativeVirtualDatagramPair_Send(&pair, 0u, first, sizeof(first),
		&(struct NativeVirtualDatagramRoute){ NATIVE_VIRTUAL_DATAGRAM_DELIVER, 9u, 0u }));
	pairBefore.nextSerial = UINT64_MAX;
	CHECK_STATE();
	puts("native_virtual_datagram_test: passed");
	return 0;
}
