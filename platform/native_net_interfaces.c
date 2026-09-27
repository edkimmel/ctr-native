#include "platform/native_net_interfaces.h"

#include "platform/native_win32.h"

/* As in platform/native_udp_transport.c: native_win32.h undefines far and
 * near, which the socket and IP helper headers still expect; restore them
 * around those headers only, then undefine them again. */
#ifndef far
#define far
#endif
#ifndef near
#define near
#endif
#include <winsock2.h>
#include <iphlpapi.h>
#undef far
#undef near

#include <stddef.h>
#include <stdint.h>

/* 1 for an entry that may carry a beacon: up, not loopback (by flag or by
 * 127.0.0.0/8), not 0.0.0.0, and a prefix length of 1..31. */
static int NativeNetInterfaces_Usable(const struct NativeNetInterface *entry)
{
	return (entry->up != 0) && (entry->loopback == 0) && (entry->ipv4 != 0) && ((entry->ipv4 >> 24) != 127u) && (entry->prefixLength != 0) &&
	       (entry->prefixLength < 32u);
}

uint32_t NativeNetInterfaces_BuildTargets(const struct NativeNetInterface *interfaces, uint32_t interfaceCount, uint32_t *targets, uint32_t capacity)
{
	uint32_t count = 0;

	if ((targets == NULL) || (capacity == 0))
	{
		return 0;
	}
	if (capacity > NATIVE_NET_INTERFACES_MAX_TARGETS)
	{
		capacity = NATIVE_NET_INTERFACES_MAX_TARGETS;
	}
	targets[count++] = NATIVE_NET_INTERFACES_LIMITED_BROADCAST;
	if (interfaces == NULL)
	{
		return count;
	}
	for (uint32_t i = 0; (i < interfaceCount) && (count < capacity); i++)
	{
		const struct NativeNetInterface *entry = &interfaces[i];
		uint32_t mask;
		uint32_t broadcast;
		int seen = 0;

		if (!NativeNetInterfaces_Usable(entry))
		{
			continue;
		}
		mask = UINT32_C(0xFFFFFFFF) << (32u - entry->prefixLength);
		broadcast = entry->ipv4 | ~mask;
		for (uint32_t j = 0; j < count; j++)
		{
			if (targets[j] == broadcast)
			{
				seen = 1;
				break;
			}
		}
		if (!seen)
		{
			targets[count++] = broadcast;
		}
	}
	return count;
}

uint32_t NativeNetInterfaces_BuildLanTargets(const struct NativeNetInterface *interfaces, uint32_t interfaceCount, uint32_t lanNetwork, uint8_t lanPrefixLength,
                                             uint32_t *targets, uint32_t capacity, uint32_t *interfaceIpv4)
{
	uint32_t mask;

	if (interfaceIpv4 != NULL)
	{
		*interfaceIpv4 = 0;
	}
	if ((targets == NULL) || (interfaceIpv4 == NULL) || (capacity == 0) || (lanPrefixLength == 0) || (lanPrefixLength >= 32u))
	{
		return 0;
	}
	mask = UINT32_C(0xFFFFFFFF) << (32u - lanPrefixLength);
	if (((lanNetwork & ~mask) != 0) || (interfaces == NULL))
	{
		return 0;
	}
	for (uint32_t i = 0; i < interfaceCount; i++)
	{
		const struct NativeNetInterface *entry = &interfaces[i];

		if (NativeNetInterfaces_Usable(entry) && ((entry->ipv4 & mask) == lanNetwork))
		{
			targets[0] = lanNetwork | ~mask;
			*interfaceIpv4 = entry->ipv4;
			return 1;
		}
	}
	return 0;
}

int NativeNetInterfaces_List(struct NativeNetInterfacesScratch *scratch, struct NativeNetInterface *out, uint32_t capacity, uint32_t *count)
{
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
	ULONG size;
	ULONG result;
	const IP_ADAPTER_ADDRESSES *adapter;
	uint32_t written = 0;

	if ((scratch == NULL) || (count == NULL) || ((out == NULL) && (capacity != 0)))
	{
		return 0;
	}
	/* The whole list lands in the caller's scratch; a larger list is
	 * ERROR_BUFFER_OVERFLOW, a failure (no heap retry). */
	size = (ULONG)sizeof(scratch->words);
	result = GetAdaptersAddresses(AF_INET, flags, NULL, (IP_ADAPTER_ADDRESSES *)(void *)scratch->words, &size);
	if (result == ERROR_NO_DATA)
	{
		*count = 0;
		return 1;
	}
	if (result != ERROR_SUCCESS)
	{
		return 0;
	}
	for (adapter = (const IP_ADAPTER_ADDRESSES *)(const void *)scratch->words; (adapter != NULL) && (written < capacity); adapter = adapter->Next)
	{
		const uint8_t up = (adapter->OperStatus == IfOperStatusUp) ? 1u : 0u;
		const uint8_t loopback = (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) ? 1u : 0u;
		const IP_ADAPTER_UNICAST_ADDRESS *unicast;

		for (unicast = adapter->FirstUnicastAddress; (unicast != NULL) && (written < capacity); unicast = unicast->Next)
		{
			const struct sockaddr *address = unicast->Address.lpSockaddr;
			const uint8_t *bytes;

			if ((address == NULL) || (address->sa_family != AF_INET) || (unicast->Address.iSockaddrLength < (INT)sizeof(struct sockaddr_in)))
			{
				continue;
			}
			/* sin_addr is in network order: read it byte by byte. */
			bytes = (const uint8_t *)&((const struct sockaddr_in *)(const void *)address)->sin_addr;
			out[written].ipv4 = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
			out[written].prefixLength = unicast->OnLinkPrefixLength;
			out[written].up = up;
			out[written].loopback = loopback;
			out[written].reserved = 0;
			written++;
		}
	}
	*count = written;
	return 1;
}
