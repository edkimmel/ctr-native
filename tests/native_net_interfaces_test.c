#include "platform/native_net_interfaces.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define COUNT(array) ((uint32_t)(sizeof(array) / sizeof((array)[0])))

static struct NativeNetInterfacesScratch s_scratch;

static struct NativeNetInterface Entry(uint32_t ipv4, uint8_t prefixLength, uint8_t up, uint8_t loopback)
{
	struct NativeNetInterface entry;

	entry.ipv4 = ipv4;
	entry.prefixLength = prefixLength;
	entry.up = up;
	entry.loopback = loopback;
	entry.reserved = 0;
	return entry;
}

/* The pure helper, with no OS call (DISC-5). */
static int TestBuildTargets(void)
{
	uint32_t targets[NATIVE_NET_INTERFACES_MAX_TARGETS + 4u];
	struct NativeNetInterface list[12];

	/* Argument handling. */
	memset(targets, 0xA5, sizeof(targets));
	CHECK(NativeNetInterfaces_BuildTargets(NULL, 0, NULL, 8u) == 0);
	CHECK(NativeNetInterfaces_BuildTargets(NULL, 0, targets, 0) == 0);
	CHECK(targets[0] == UINT32_C(0xA5A5A5A5));
	CHECK(NativeNetInterfaces_BuildTargets(NULL, 5u, targets, 8u) == 1u);
	CHECK(targets[0] == NATIVE_NET_INTERFACES_LIMITED_BROADCAST);
	CHECK(targets[1] == UINT32_C(0xA5A5A5A5));

	/* The fleet's /24: 255.255.255.255 first, then 192.168.1.255. */
	list[0] = Entry(UINT32_C(0xC0A8010B), 24u, 1u, 0u);
	CHECK(NativeNetInterfaces_BuildTargets(list, 1u, targets, 8u) == 2u);
	CHECK(targets[0] == UINT32_C(0xFFFFFFFF));
	CHECK(targets[1] == UINT32_C(0xC0A801FF));

	/* Skipped: loopback, down, prefix 0, prefix 32 (and above), 127/8
	 * without the loopback flag, and 0.0.0.0. Kept: /8, /31, /1. A second
	 * address on the same /24 and a broadcast equal to 255.255.255.255 are
	 * not repeated. */
	list[0] = Entry(UINT32_C(0x7F000001), 8u, 1u, 1u);   /* loopback adapter */
	list[1] = Entry(UINT32_C(0xC0A80205), 24u, 0u, 0u);  /* down */
	list[2] = Entry(UINT32_C(0xC0A80305), 0u, 1u, 0u);   /* prefix 0 */
	list[3] = Entry(UINT32_C(0xC0A80405), 32u, 1u, 0u);  /* prefix 32 */
	list[4] = Entry(UINT32_C(0xC0A80505), 33u, 1u, 0u);  /* prefix 33 */
	list[5] = Entry(UINT32_C(0x7F000002), 8u, 1u, 0u);   /* 127/8, not flagged */
	list[6] = Entry(0u, 24u, 1u, 0u);                    /* 0.0.0.0 */
	list[7] = Entry(UINT32_C(0x0A010203), 8u, 1u, 0u);   /* 10.1.2.3/8 -> 10.255.255.255 */
	list[8] = Entry(UINT32_C(0x0A7F0001), 8u, 1u, 0u);   /* same /8: deduplicated */
	list[9] = Entry(UINT32_C(0xC0A8010C), 31u, 1u, 0u);  /* /31 -> 192.168.1.13 */
	list[10] = Entry(UINT32_C(0x01020304), 1u, 1u, 0u);  /* /1 -> 127.255.255.255 */
	list[11] = Entry(UINT32_C(0xFFFFFFFE), 31u, 1u, 0u); /* broadcast 255.255.255.255: deduplicated */
	CHECK(NativeNetInterfaces_BuildTargets(list, COUNT(list), targets, 8u) == 4u);
	CHECK(targets[0] == UINT32_C(0xFFFFFFFF));
	CHECK(targets[1] == UINT32_C(0x0AFFFFFF));
	CHECK(targets[2] == UINT32_C(0xC0A8010D));
	CHECK(targets[3] == UINT32_C(0x7FFFFFFF));

	/* The cap: 8 in total, whatever the capacity; a smaller capacity caps lower. */
	for (uint32_t i = 0; i < COUNT(list); i++)
	{
		list[i] = Entry(UINT32_C(0x0A000001) | (i << 8), 24u, 1u, 0u); /* 10.0.i.1/24 */
	}
	memset(targets, 0xA5, sizeof(targets));
	CHECK(NativeNetInterfaces_BuildTargets(list, COUNT(list), targets, COUNT(targets)) == NATIVE_NET_INTERFACES_MAX_TARGETS);
	CHECK(targets[0] == UINT32_C(0xFFFFFFFF));
	for (uint32_t i = 1; i < NATIVE_NET_INTERFACES_MAX_TARGETS; i++)
	{
		CHECK(targets[i] == (UINT32_C(0x0A0000FF) | ((i - 1u) << 8)));
	}
	CHECK(targets[NATIVE_NET_INTERFACES_MAX_TARGETS] == UINT32_C(0xA5A5A5A5));
	CHECK(NativeNetInterfaces_BuildTargets(list, COUNT(list), targets, 3u) == 3u);
	CHECK(NativeNetInterfaces_BuildTargets(list, COUNT(list), targets, 1u) == 1u);
	CHECK(targets[0] == UINT32_C(0xFFFFFFFF));

	/* An empty list: the limited broadcast alone. */
	CHECK(NativeNetInterfaces_BuildTargets(list, 0, targets, 8u) == 1u);
	return 0;
}

/* The OS call: argument checks, and a sane list from this machine. */
static int TestList(void)
{
	struct NativeNetInterface list[NATIVE_NET_INTERFACES_MAX_TARGETS * 2u];
	uint32_t targets[NATIVE_NET_INTERFACES_MAX_TARGETS];
	uint32_t count = 0xA5A5A5A5u;
	uint32_t targetCount;

	CHECK(!NativeNetInterfaces_List(NULL, list, COUNT(list), &count));
	CHECK(!NativeNetInterfaces_List(&s_scratch, list, COUNT(list), NULL));
	CHECK(!NativeNetInterfaces_List(&s_scratch, NULL, 1u, &count));
	CHECK(count == 0xA5A5A5A5u);
	CHECK(NativeNetInterfaces_List(&s_scratch, NULL, 0, &count));
	CHECK(count == 0);

	CHECK(NativeNetInterfaces_List(&s_scratch, list, COUNT(list), &count));
	CHECK(count <= COUNT(list));
	for (uint32_t i = 0; i < count; i++)
	{
		CHECK(list[i].prefixLength <= 32u);
		CHECK(list[i].up <= 1u);
		CHECK(list[i].loopback <= 1u);
		CHECK(list[i].reserved == 0);
	}
	targetCount = NativeNetInterfaces_BuildTargets(list, count, targets, COUNT(targets));
	CHECK((targetCount >= 1u) && (targetCount <= NATIVE_NET_INTERFACES_MAX_TARGETS));
	CHECK(targets[0] == NATIVE_NET_INTERFACES_LIMITED_BROADCAST);
	printf("native_net_interfaces_test: %u IPv4 addresses, %u targets\n", (unsigned)count, (unsigned)targetCount);
	return 0;
}

int main(void)
{
	CHECK(TestBuildTargets() == 0);
	CHECK(TestList() == 0);
	puts("native_net_interfaces_test: ok");
	return 0;
}
