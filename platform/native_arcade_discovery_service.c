#include "platform/native_arcade_discovery_service.h"

#include "platform/native_arcade_discovery.h"
#include "platform/native_net_interfaces.h"
#include "platform/native_udp_transport.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Clears everything before the scratch, which holds nothing between calls. */
static void NativeArcadeDiscoveryService_Clear(struct NativeArcadeDiscoveryService *service)
{
	memset(service, 0, offsetof(struct NativeArcadeDiscoveryService, scratch));
}

/* The interface-list targets at the discovery port (DISC-5), or the limited
 * broadcast alone when the enumeration fails (DISC-15). */
static void NativeArcadeDiscoveryService_RefreshTargets(struct NativeArcadeDiscoveryService *service)
{
	struct NativeNetInterface interfaces[NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_INTERFACES];
	uint32_t addresses[NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS];
	uint32_t interfaceCount = 0;
	uint32_t count;

	if (NativeNetInterfaces_List(&service->scratch, interfaces, NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_INTERFACES, &interfaceCount))
	{
		count = NativeNetInterfaces_BuildTargets(interfaces, interfaceCount, addresses, NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS);
		service->enumerationFailed = 0;
	}
	else
	{
		count = NativeNetInterfaces_BuildTargets(NULL, 0, addresses, NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_TARGETS);
		service->enumerationFailed = 1;
	}
	for (uint32_t i = 0; i < count; i++)
	{
		service->targets[i].ipv4 = addresses[i];
		service->targets[i].port = service->bindPort;
	}
	service->targetCount = count;
}

static void NativeArcadeDiscoveryService_SendBeacon(struct NativeArcadeDiscoveryService *service)
{
	uint8_t beacon[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES];

	if (!NativeArcadeDiscovery_BuildBeacon(&service->table, beacon))
	{
		return;
	}
	for (uint32_t i = 0; i < service->targetCount; i++)
	{
		if (!NativeUdpTransport_Send(&service->transport, &service->targets[i], beacon, sizeof(beacon)))
		{
			service->sendFailures++;
		}
	}
}

int NativeArcadeDiscoveryService_Open(struct NativeArcadeDiscoveryService *service, uint16_t bindPort, uint64_t ourNonce, uint64_t groupHash,
                                      const uint8_t identity[NATIVE_ARCADE_DISCOVERY_IDENTITY_BYTES], uint16_t ourLinkPort, uint8_t ourSeatPreference,
                                      const struct NativeUdpTransportAddress *overrideTargets, uint32_t overrideCount)
{
	if (service == NULL)
	{
		return 0;
	}
	NativeArcadeDiscoveryService_Close(service);
	if ((bindPort == 0) || (overrideCount > NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_OVERRIDES) || ((overrideCount != 0) && (overrideTargets == NULL)))
	{
		return 0;
	}
	for (uint32_t i = 0; i < overrideCount; i++)
	{
		if ((overrideTargets[i].ipv4 == 0) || (overrideTargets[i].port == 0))
		{
			return 0;
		}
	}
	if (!NativeArcadeDiscovery_Init(&service->table, ourNonce, groupHash, identity, ourLinkPort, ourSeatPreference))
	{
		NativeArcadeDiscoveryService_Clear(service);
		return 0;
	}
	if (!NativeUdpTransport_GlobalInit())
	{
		NativeArcadeDiscoveryService_Clear(service);
		return 0;
	}
	if (!NativeUdpTransport_Open(&service->transport, bindPort))
	{
		NativeUdpTransport_GlobalShutdown();
		NativeArcadeDiscoveryService_Clear(service);
		return 0;
	}
	if (!NativeUdpTransport_EnableBroadcast(&service->transport))
	{
		NativeUdpTransport_Close(&service->transport);
		NativeUdpTransport_GlobalShutdown();
		NativeArcadeDiscoveryService_Clear(service);
		return 0;
	}

	service->open = 1;
	service->bindPort = bindPort;
	if (overrideCount != 0)
	{
		for (uint32_t i = 0; i < overrideCount; i++)
		{
			service->targets[i] = overrideTargets[i];
		}
		service->targetCount = overrideCount;
		service->overridden = 1;
	}
	else
	{
		NativeArcadeDiscoveryService_RefreshTargets(service);
	}
	return 1;
}

void NativeArcadeDiscoveryService_Tick(struct NativeArcadeDiscoveryService *service)
{
	uint8_t datagram[NATIVE_ARCADE_DISCOVERY_BEACON_BYTES + 1u];

	if ((service == NULL) || (service->open == 0))
	{
		return;
	}

	/* Drain. A datagram larger than a beacon arrives as TOO_SMALL (skipped)
	 * or at 97 bytes (which the core's decoder rejects by size). */
	for (uint32_t drained = 0; drained < NATIVE_ARCADE_DISCOVERY_SERVICE_MAX_DRAIN; drained++)
	{
		struct NativeUdpTransportAddress sender = {0u, 0u};
		size_t size = 0;
		const enum NativeUdpTransportReceiveResult result = NativeUdpTransport_Receive(&service->transport, datagram, sizeof(datagram), &size, &sender);

		if (result == NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			(void)NativeArcadeDiscovery_Receive(&service->table, datagram, size, sender.ipv4);
		}
		else if (result != NATIVE_UDP_TRANSPORT_RECEIVE_TOO_SMALL)
		{
			if (result == NATIVE_UDP_TRANSPORT_RECEIVE_ERROR)
			{
				service->receiveErrors++;
			}
			break;
		}
	}

	NativeArcadeDiscovery_Tick(&service->table);

	if ((service->overridden == 0) && (service->tickCount != 0) && ((service->tickCount % NATIVE_ARCADE_DISCOVERY_SERVICE_REFRESH_TICKS) == 0))
	{
		NativeArcadeDiscoveryService_RefreshTargets(service);
	}
	if ((service->tickCount % NATIVE_ARCADE_DISCOVERY_BEACON_INTERVAL_TICKS) == 0)
	{
		NativeArcadeDiscoveryService_SendBeacon(service);
	}
	service->tickCount++;
}

int NativeArcadeDiscoveryService_Pairing(const struct NativeArcadeDiscoveryService *service, struct NativeArcadeDiscoveryPairing *out)
{
	if ((service == NULL) || (service->open == 0))
	{
		return 0;
	}
	return NativeArcadeDiscovery_Pairing(&service->table, out);
}

int NativeArcadeDiscoveryService_TakeEvent(struct NativeArcadeDiscoveryService *service, struct NativeArcadeDiscoveryEvent *out)
{
	if ((service == NULL) || (service->open == 0))
	{
		return 0;
	}
	return NativeArcadeDiscovery_TakeEvent(&service->table, out);
}

int NativeArcadeDiscoveryService_GetStatus(const struct NativeArcadeDiscoveryService *service, struct NativeArcadeDiscoveryServiceStatus *out)
{
	if ((service == NULL) || (out == NULL))
	{
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (service->open == 0)
	{
		return 1;
	}
	out->open = 1;
	out->overridden = service->overridden;
	out->enumerationFailed = service->enumerationFailed;
	out->bindPort = service->bindPort;
	out->targetCount = service->targetCount;
	out->tickCount = service->tickCount;
	out->sendFailures = service->sendFailures;
	out->receiveErrors = service->receiveErrors;
	return 1;
}

int NativeArcadeDiscoveryService_Target(const struct NativeArcadeDiscoveryService *service, uint32_t index, struct NativeUdpTransportAddress *out)
{
	if ((service == NULL) || (out == NULL) || (service->open == 0) || (index >= service->targetCount))
	{
		return 0;
	}
	*out = service->targets[index];
	return 1;
}

void NativeArcadeDiscoveryService_Close(struct NativeArcadeDiscoveryService *service)
{
	if (service == NULL)
	{
		return;
	}
	if (service->open != 0)
	{
		NativeUdpTransport_Close(&service->transport);
		NativeUdpTransport_GlobalShutdown();
	}
	NativeArcadeDiscoveryService_Clear(service);
}
