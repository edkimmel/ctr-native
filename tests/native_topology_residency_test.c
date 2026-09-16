#include "platform/native_topology_residency.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"fail %d\n",__LINE__); return 0; } } while(0)

static void Valid(struct NativeTopologyResidencyLeaseV1 *lease,struct NativeTopologyResidencyObservedV1 *facts)
{
	memset(lease,0,sizeof(*lease)); memset(facts,0,sizeof(*facts));
	lease->base=0x1000u; lease->span=0x8000u; lease->epoch=7;
	facts->levelAddress=0x1000u; facts->meshAddress=0x1200u;
	facts->quadAddress=0x1300u; facts->quadCount=2;
	facts->restartAddress=0x1400u; facts->restartCount=2;
	facts->navTableAddress=0x1500u;
	for(uint32_t i=0;i<NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT;i++)
	{
		facts->nav[i].headerAddress=0x1600u+i*0x100u;
		facts->nav[i].frameAddress=0x1900u+i*0x100u;
		facts->nav[i].magic=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC;
		facts->nav[i].pointCount=2;
	}
}

static int CaptureAndIdentity(void)
{
	struct NativeTopologyResidencyLeaseV1 lease,changed;
	struct NativeTopologyResidencyObservedV1 facts,changedFacts;
	struct NativeTopologyResidencySnapshotV1 snapshot,before;
	Valid(&lease,&facts);
	CHECK(NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));
	CHECK(snapshot.tag==NATIVE_TOPOLOGY_RESIDENCY_SNAPSHOT_TAG&&
		NativeTopologyResidencyV1_Validate(&snapshot,&lease,&facts));
	changed=lease;changed.epoch++;
	CHECK(!NativeTopologyResidencyV1_Validate(&snapshot,&changed,&facts));
	changed=lease;changed.base+=4u;
	CHECK(!NativeTopologyResidencyV1_Validate(&snapshot,&changed,&facts));
	changedFacts=facts;changedFacts.meshAddress+=4u;
	CHECK(!NativeTopologyResidencyV1_Validate(&snapshot,&lease,&changedFacts));
	changedFacts=facts;changedFacts.nav[1].frameAddress+=2u;
	CHECK(!NativeTopologyResidencyV1_Validate(&snapshot,&lease,&changedFacts));
	before=snapshot;memset(&changedFacts,0,sizeof(changedFacts));
	CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&changedFacts));
	CHECK(memcmp(&snapshot,&before,sizeof(snapshot))==0);
	CHECK(!NativeTopologyResidencyV1_Capture(NULL,&lease,&facts));
	CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,NULL,&facts));
	CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,NULL));
	return 1;
}

static int RangesCountsAndEmpty(void)
{
	struct NativeTopologyResidencyLeaseV1 lease;
	struct NativeTopologyResidencyObservedV1 facts;
	struct NativeTopologyResidencySnapshotV1 snapshot;
	Valid(&lease,&facts);
	#define BAD(change) do { \
		struct NativeTopologyResidencySnapshotV1 before; \
		CHECK(NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts)); before=snapshot; \
		change; CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts)); \
		CHECK(memcmp(&snapshot,&before,sizeof(snapshot))==0); Valid(&lease,&facts); \
	} while(0)
	BAD(lease.base=0);BAD(lease.span=0);BAD(lease.epoch=0);
	BAD(facts.levelAddress+=2u);BAD(facts.meshAddress+=2u);
	BAD(facts.quadAddress+=2u);BAD(facts.restartAddress+=1u);BAD(facts.navTableAddress+=2u);
	BAD(facts.quadCount=NATIVE_TOPOLOGY_RESIDENCY_MAX_QUAD_COUNT+1u);
	BAD(facts.restartCount=NATIVE_TOPOLOGY_RESIDENCY_MAX_RESTART_COUNT+1u);
	BAD(facts.nav[0].pointCount=NATIVE_TOPOLOGY_RESIDENCY_MAX_NAV_POINT_COUNT+1u);
	BAD(facts.nav[0].magic=0);BAD(facts.nav[0].frameAddress+=1u);BAD(facts.nav[0].headerAddress+=2u);
	/* Empty arrays must be exactly nullable. */
	facts.quadCount=0;facts.quadAddress=0;facts.restartCount=0;facts.restartAddress=0;
	memset(facts.nav,0,sizeof(facts.nav));facts.navTableAddress=0;
	CHECK(NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));
	facts.restartAddress=0x1400u;CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));facts.restartAddress=0;
	facts.quadAddress=0x1300u;CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));facts.quadAddress=0;
	/* A present zero-point path is valid, but needs a header, magic, and no frame. */
	facts.navTableAddress=0x1500u;facts.nav[0].headerAddress=0x1600u;
	facts.nav[0].magic=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC;facts.nav[0].pointCount=0;
	CHECK(NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));
	facts.nav[0].frameAddress=0x1900u;CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));facts.nav[0].frameAddress=0;
	facts.nav[0].magic=0;CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));facts.nav[0].magic=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC;
	facts.navTableAddress=0;CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts));
	#undef BAD
	return 1;
}

/* Every non-null span family accepts an address ending at leaseEnd, and
 * rejects the same facts when the lease loses that final byte. */
static int ExactEndBoundaries(void)
{
	struct NativeTopologyResidencyLeaseV1 lease;
	struct NativeTopologyResidencyObservedV1 facts;
	struct NativeTopologyResidencySnapshotV1 snapshot,before;
	#define EXACT_END(setup) do { \
		Valid(&lease,&facts); lease.span=0x8000u; setup; \
		CHECK(NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts)); before=snapshot; \
		lease.span--; CHECK(!NativeTopologyResidencyV1_Capture(&snapshot,&lease,&facts)); \
		CHECK(memcmp(&snapshot,&before,sizeof(snapshot))==0); \
	} while(0)
	EXACT_END(facts.levelAddress=0x8e74u);
	EXACT_END(facts.meshAddress=0x8fe0u);
	EXACT_END(facts.quadAddress=0x8f48u; facts.quadCount=2u);
	EXACT_END(facts.restartAddress=0x8fe8u; facts.restartCount=2u);
	EXACT_END(facts.navTableAddress=0x8ff4u);
	EXACT_END(facts.nav[0].headerAddress=0x8fb4u);
	EXACT_END(facts.nav[0].frameAddress=0x8fd8u; facts.nav[0].pointCount=2u);
	#undef EXACT_END
	return 1;
}

int main(void)
{
	CHECK(CaptureAndIdentity());
	CHECK(RangesCountsAndEmpty());
	CHECK(ExactEndBoundaries());
	puts("native_topology_residency_test: passed");
	return 0;
}
