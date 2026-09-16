#include "platform/native_topology_residency.h"

#include <string.h>

static int Aligned(uint32_t address,uint32_t alignment)
{
	return address!=0&&alignment!=0&&(address%alignment)==0;
}

static int SpanInLease(const struct NativeTopologyResidencyLeaseV1 *lease,uint32_t address,uint32_t bytes)
{
	uint64_t leaseEnd,addressEnd;
	if(!lease||lease->base==0||lease->span==0||lease->epoch==0||address==0||bytes==0)return 0;
	leaseEnd=(uint64_t)lease->base+(uint64_t)lease->span;
	addressEnd=(uint64_t)address+(uint64_t)bytes;
	return leaseEnd<=UINT64_C(0x100000000)&&address>=lease->base&&addressEnd<=leaseEnd;
}

static int CountSpan(uint32_t count,uint32_t cap,uint32_t bytes,uint32_t *spanOut)
{
	uint64_t span;
	if(!spanOut||count>cap)return 0;
	span=(uint64_t)count*(uint64_t)bytes;
	if(span>UINT32_MAX)return 0;
	*spanOut=(uint32_t)span;
	return 1;
}

static int NullableArray(const struct NativeTopologyResidencyLeaseV1 *lease,uint32_t address,
	uint32_t count,uint32_t cap,uint32_t bytes,uint32_t alignment)
{
	uint32_t span;
	if(!CountSpan(count,cap,bytes,&span))return 0;
	if(count==0)return address==0;
	return Aligned(address,alignment)&&SpanInLease(lease,address,span);
}

static int NavPathValid(const struct NativeTopologyResidencyLeaseV1 *lease,
	const struct NativeTopologyResidencyNavPathV1 *path)
{
	uint32_t frameSpan;
	if(!lease||!path)return 0;
	if(path->headerAddress==0)
		return path->frameAddress==0&&path->magic==0&&path->pointCount==0;
	if(path->magic!=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC||
		!Aligned(path->headerAddress,4u)||
		!SpanInLease(lease,path->headerAddress,NATIVE_TOPOLOGY_RESIDENCY_NAV_HEADER_BYTES)||
		!CountSpan(path->pointCount,NATIVE_TOPOLOGY_RESIDENCY_MAX_NAV_POINT_COUNT,
			NATIVE_TOPOLOGY_RESIDENCY_NAV_FRAME_BYTES,&frameSpan))return 0;
	if(path->pointCount==0)return path->frameAddress==0;
	return Aligned(path->frameAddress,2u)&&SpanInLease(lease,path->frameAddress,frameSpan);
}

static int FactsValid(const struct NativeTopologyResidencyLeaseV1 *lease,
	const struct NativeTopologyResidencyObservedV1 *observed)
{
	uint32_t path;
	int anyNav=0;
	if(!lease||!observed||!Aligned(observed->levelAddress,4u)||
		!SpanInLease(lease,observed->levelAddress,NATIVE_TOPOLOGY_RESIDENCY_LEVEL_PREFIX_BYTES)||
		!Aligned(observed->meshAddress,4u)||
		!SpanInLease(lease,observed->meshAddress,NATIVE_TOPOLOGY_RESIDENCY_MESH_BYTES)||
		!NullableArray(lease,observed->quadAddress,observed->quadCount,
			NATIVE_TOPOLOGY_RESIDENCY_MAX_QUAD_COUNT,NATIVE_TOPOLOGY_RESIDENCY_QUAD_BYTES,4u)||
		!NullableArray(lease,observed->restartAddress,observed->restartCount,
			NATIVE_TOPOLOGY_RESIDENCY_MAX_RESTART_COUNT,NATIVE_TOPOLOGY_RESIDENCY_RESTART_BYTES,2u))return 0;
	for(path=0;path<NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT;path++)
	{
		if(!NavPathValid(lease,&observed->nav[path]))return 0;
		if(observed->nav[path].headerAddress!=0)anyNav=1;
	}
	if(!anyNav)return observed->navTableAddress==0;
	return Aligned(observed->navTableAddress,4u)&&
		SpanInLease(lease,observed->navTableAddress,NATIVE_TOPOLOGY_RESIDENCY_NAV_TABLE_BYTES);
}

int NativeTopologyResidencyV1_Capture(struct NativeTopologyResidencySnapshotV1 *snapshotOut,
	const struct NativeTopologyResidencyLeaseV1 *lease,
	const struct NativeTopologyResidencyObservedV1 *observed)
{
	struct NativeTopologyResidencySnapshotV1 candidate;
	if(!snapshotOut||!FactsValid(lease,observed))return 0;
	memset(&candidate,0,sizeof(candidate));
	candidate.tag=NATIVE_TOPOLOGY_RESIDENCY_SNAPSHOT_TAG;
	candidate.lease=*lease;
	candidate.observed=*observed;
	*snapshotOut=candidate;
	return 1;
}

int NativeTopologyResidencyV1_Validate(const struct NativeTopologyResidencySnapshotV1 *snapshot,
	const struct NativeTopologyResidencyLeaseV1 *lease,
	const struct NativeTopologyResidencyObservedV1 *observed)
{
	if(!snapshot||snapshot->tag!=NATIVE_TOPOLOGY_RESIDENCY_SNAPSHOT_TAG||
		!FactsValid(lease,observed))return 0;
	return snapshot->lease.base==lease->base&&snapshot->lease.span==lease->span&&
		snapshot->lease.epoch==lease->epoch&&
		snapshot->observed.levelAddress==observed->levelAddress&&
		snapshot->observed.meshAddress==observed->meshAddress&&
		snapshot->observed.quadAddress==observed->quadAddress&&
		snapshot->observed.quadCount==observed->quadCount&&
		snapshot->observed.restartAddress==observed->restartAddress&&
		snapshot->observed.restartCount==observed->restartCount&&
		snapshot->observed.navTableAddress==observed->navTableAddress&&
		memcmp(snapshot->observed.nav,observed->nav,sizeof(observed->nav))==0;
}
