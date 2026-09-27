#ifndef PLATFORM_NATIVE_NET_INTERFACES_H
#define PLATFORM_NATIVE_NET_INTERFACES_H

#include <stdint.h>

/*
 * IPv4 interface list and broadcast targets for arcade-link discovery
 * (docs/DISCOVERY_MILESTONE.md DISC-5, DISC-S3).
 *
 * - NativeNetInterfaces_List reads the OS adapter list (the only place in
 *   the tree that does) into caller-owned storage: every IPv4 unicast
 *   address with its on-link prefix length and its adapter's up and
 *   loopback state.
 * - NativeNetInterfaces_BuildTargets is pure: from such a list it builds the
 *   beacon targets, the limited broadcast 255.255.255.255 first, then the
 *   directed broadcast (ip | ~mask) of every usable address, deduplicated
 *   and capped at NATIVE_NET_INTERFACES_MAX_TARGETS in total.
 *
 * No heap: the OS call writes into the caller's
 * struct NativeNetInterfacesScratch (32 KB, 8-byte aligned by its type), and
 * a list that needs more is a failure, which the caller answers with the
 * limited broadcast alone (DISC-15). IPv4 values are host order
 * (192.168.1.11 is 0xC0A8010B), as struct NativeUdpTransportAddress.
 * Nothing here reaches simulation identity or any deterministic state.
 */

#define NATIVE_NET_INTERFACES_MAX_TARGETS       8u
#define NATIVE_NET_INTERFACES_LIMITED_BROADCAST UINT32_C(0xFFFFFFFF)
#define NATIVE_NET_INTERFACES_SCRATCH_WORDS     4096u /* 32 KB */

/* One IPv4 unicast address of one adapter. */
struct NativeNetInterface
{
	uint32_t ipv4;        /* host order */
	uint8_t prefixLength; /* on-link prefix, 0..32 */
	uint8_t up;           /* 1 when the adapter is operationally up */
	uint8_t loopback;     /* 1 for a loopback adapter */
	uint8_t reserved;
};

/* Caller-owned storage for the OS adapter list; treat as opaque. */
struct NativeNetInterfacesScratch
{
	uint64_t words[NATIVE_NET_INTERFACES_SCRATCH_WORDS];
};

/*
 * Pure. Writes the beacon targets to targets[] and returns their count:
 * 255.255.255.255 first, then, in list order, the directed broadcast
 * (ipv4 | ~mask) of each entry that is up, not loopback, not in 127.0.0.0/8,
 * not 0.0.0.0, and has a prefix length of 1..31 (0 and 32 and above are
 * skipped); a value already written is not repeated. At most
 * min(capacity, NATIVE_NET_INTERFACES_MAX_TARGETS) targets. Returns 0 for a
 * NULL targets or capacity 0; a NULL interfaces list gives the limited
 * broadcast alone.
 */
uint32_t NativeNetInterfaces_BuildTargets(const struct NativeNetInterface *interfaces, uint32_t interfaceCount, uint32_t *targets, uint32_t capacity);

/*
 * Pure. The beacon target when discovery is pinned to a lan
 * (docs/DISCOVERY_MILESTONE.md DISC-19): the lan's directed broadcast
 * (lanNetwork | ~mask) alone, and only while an entry BuildTargets would use
 * (up, not loopback, not in 127.0.0.0/8, not 0.0.0.0, prefix length 1..31)
 * is on exactly the lan's subnet: its address inside the lan and its prefix
 * length equal to lanPrefixLength. Then targets[0] is that broadcast,
 * *interfaceIpv4 the first such address, and the result 1. Otherwise the
 * result is 0 and *interfaceIpv4 0: never 255.255.255.255 and never another
 * entry's broadcast (no fallback). A usable entry whose address is inside
 * the lan but whose prefix length differs never counts (the lan's broadcast
 * would be a unicast host of a wider subnet, or off-link for a narrower
 * one); when the result is 0, the first such entry is copied to *mismatch,
 * otherwise *mismatch is all zero. mismatch may be NULL. Returns 0 (with
 * *interfaceIpv4 0 and *mismatch zero when they are not NULL, targets
 * untouched) for a NULL targets or interfaceIpv4, capacity 0, a lan prefix
 * length outside 1..31, or a lanNetwork with host bits set; the caller has
 * already checked the lan's own rule.
 */
uint32_t NativeNetInterfaces_BuildLanTargets(const struct NativeNetInterface *interfaces, uint32_t interfaceCount, uint32_t lanNetwork, uint8_t lanPrefixLength,
                                             uint32_t *targets, uint32_t capacity, uint32_t *interfaceIpv4, struct NativeNetInterface *mismatch);

/*
 * Reads the OS list of IPv4 unicast addresses into out[] (at most capacity
 * entries; later ones are left out) and *count. Returns 1 on success (an
 * empty list included), 0 for NULL arguments or when the OS call fails,
 * including a list larger than the scratch (*count untouched).
 */
int NativeNetInterfaces_List(struct NativeNetInterfacesScratch *scratch, struct NativeNetInterface *out, uint32_t capacity, uint32_t *count);

#endif
