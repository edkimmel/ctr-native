#include "common.h"
#include "MAIN/MainCanonicalTopologyLeaseAdapter.h"

#include <stdio.h>
#include <string.h>

struct sData sdata_static;
#include "../game/MAIN/MainCanonicalTopologyFacts.c"
#include "../game/MAIN/MainCanonicalTopologyLeaseAuthority.c"
#include "../game/MAIN/MainCanonicalTopologyLeaseAdapter.c"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"fail %d\n",__LINE__); return 0; } } while(0)
#define PACK_BYTES 0x4000u

union PackStorage { void *align; unsigned char bytes[PACK_BYTES]; };
struct NavStorage { struct NavHeader header; struct NavFrame frames[2]; };
struct Fixture
{
	struct GameTracker tracker;
	struct sData source;
	union PackStorage pack;
	struct Level *level;
	struct mesh_info *mesh;
	struct QuadBlock *quads;
	struct CheckpointNode *restarts;
	struct NavStorage *nav[3];
	struct NavHeader **navTable;
};

enum GuardFault { GUARD_FAULT_NONE, GUARD_FAULT_RETIRE, GUARD_FAULT_ROOT, GUARD_FAULT_LEVEL, GUARD_FAULT_PAYLOAD };
struct CaptureGuard
{
	struct Fixture *fixture;
	struct MainCanonicalTopologyLeaseAuthority *authority;
	uint32_t calls,faultCall,falseCall;
	enum GuardFault fault;
};
static int Guard(const void *context)
{
	struct CaptureGuard *guard=(struct CaptureGuard *)context;
	if(!guard)return 0;
	guard->calls++;
	if(guard->calls==guard->falseCall)return 0;
	if(guard->calls==guard->faultCall)
	{
		if(guard->fault==GUARD_FAULT_RETIRE)MainCanonicalTopologyLeaseAuthority_Retire(guard->authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
		else if(guard->fault==GUARD_FAULT_ROOT)guard->fixture->tracker.level1=NULL;
		else if(guard->fault==GUARD_FAULT_LEVEL)guard->fixture->tracker.levelID++;
		else if(guard->fault==GUARD_FAULT_PAYLOAD)guard->fixture->quads[0].checkpointIndex=0;
	}
	return 1;
}

static uintptr_t Align(uintptr_t value,size_t alignment) { return (value+(alignment-1u))&~(uintptr_t)(alignment-1u); }
static int Put(struct Fixture *f,uintptr_t *cursor,size_t bytes,size_t alignment,void **out)
{
	uintptr_t value=Align(*cursor,alignment),limit=(uintptr_t)f->pack.bytes+sizeof(f->pack.bytes);
	if(value>limit||bytes>limit-value)return 0;*out=(void *)value;*cursor=value+bytes;return 1;
}
static int InitFixture(struct Fixture *f)
{
	uintptr_t cursor;void *p;
	memset(f,0,sizeof(*f));cursor=(uintptr_t)f->pack.bytes;
	CHECK(Put(f,&cursor,sizeof(*f->level),_Alignof(struct Level),&p));f->level=p;
	CHECK(Put(f,&cursor,sizeof(*f->mesh),_Alignof(struct mesh_info),&p));f->mesh=p;
	CHECK(Put(f,&cursor,2*sizeof(*f->quads),_Alignof(struct QuadBlock),&p));f->quads=p;
	CHECK(Put(f,&cursor,2*sizeof(*f->restarts),_Alignof(struct CheckpointNode),&p));f->restarts=p;
	CHECK(Put(f,&cursor,3*sizeof(*f->navTable),_Alignof(struct NavHeader *),&p));f->navTable=p;
	for(uint32_t i=0;i<3;i++)
	{
		CHECK(Put(f,&cursor,sizeof(*f->nav[i]),_Alignof(struct NavStorage),&p));f->nav[i]=p;
		f->nav[i]->header.magicNumber=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC;
		f->nav[i]->header.numPoints=i==1?1:2;f->nav[i]->header.last=f->nav[i]->frames+f->nav[i]->header.numPoints;
		f->nav[i]->header.posY_firstNode=(int32_t)(-10-(int32_t)i);f->nav[i]->header.rampPhys1[0]=(int16_t)(-2-(int16_t)i);f->nav[i]->header.rampPhys2[15]=(int16_t)(30+(int16_t)i);
		f->nav[i]->frames[0].pos.x=(int16_t)(1+(int16_t)i);f->nav[i]->frames[0].pos.y=-2;f->nav[i]->frames[0].pos.z=3;
		f->nav[i]->frames[0].rot[0]=4;f->nav[i]->frames[0].rot[1]=0x80;f->nav[i]->frames[0].rot[2]=6;f->nav[i]->frames[0].rot[3]=7;f->nav[i]->frames[0].distToNextNavXYZ=-5;f->nav[i]->frames[0].distToNextNavXZ=6;
		f->nav[i]->frames[0].flags=-7;f->nav[i]->frames[0].pathChangeOpcode=8;f->nav[i]->frames[0].goBackCount=9;f->nav[i]->frames[0].specialBits=10;
		f->nav[i]->frames[1].pos.x=-11;f->nav[i]->frames[1].pos.y=12;f->nav[i]->frames[1].pos.z=-13;f->nav[i]->frames[1].rot[0]=0xff;f->nav[i]->frames[1].rot[1]=2;f->nav[i]->frames[1].rot[2]=3;f->nav[i]->frames[1].rot[3]=4;f->nav[i]->frames[1].distToNextNavXYZ=-0x1234;f->nav[i]->frames[1].distToNextNavXZ=0x7fff;f->nav[i]->frames[1].flags=(int16_t)0x8000;f->nav[i]->frames[1].pathChangeOpcode=-9;f->nav[i]->frames[1].goBackCount=0xee;f->nav[i]->frames[1].specialBits=0x80;
		f->navTable[i]=&f->nav[i]->header;
	}
	f->quads[0].checkpointIndex=1;f->quads[1].checkpointIndex=0xff;
	f->restarts[0].pos.x=-1;f->restarts[0].pos.y=2;f->restarts[0].pos.z=-3;f->restarts[0].distToFinish=4;f->restarts[0].nextIndex_forward=1;f->restarts[0].nextIndex_left=0xff;f->restarts[0].nextIndex_backward=0;f->restarts[0].nextIndex_right=0xff;
	f->restarts[1].pos.x=5;f->restarts[1].pos.y=-6;f->restarts[1].pos.z=7;f->restarts[1].distToFinish=8;f->restarts[1].nextIndex_forward=0;f->restarts[1].nextIndex_left=1;f->restarts[1].nextIndex_backward=0xff;f->restarts[1].nextIndex_right=0xff;
	f->level->ptr_mesh_info=f->mesh;f->level->cnt_restart_points=2;f->level->ptr_restart_points=f->restarts;f->level->LevNavTable=f->navTable;
	f->mesh->numQuadBlock=2;f->mesh->ptrQuadBlockArray=f->quads;
	f->source.gGT=&f->tracker;f->source.Loading.stage=LOAD_IDLE;
	f->tracker.level1=f->level;f->tracker.levelID=-77;f->tracker.levID_in_each_mempack[0]=-77;
	f->source.mempack[0].start=f->pack.bytes;f->source.mempack[0].firstFreeByte=(void *)cursor;
	f->source.mempack[0].lastFreeByte=(void *)((uintptr_t)f->pack.bytes+PACK_BYTES);
	f->source.mempack[0].endOfAllocator=f->source.mempack[0].lastFreeByte;f->source.mempack[0].packSize=PACK_BYTES;
	return 1;
}

static int EqualTopology(const struct NativeCanonicalTopologyV1 *a,const struct NativeCanonicalTopologyV1 *b)
{
	return a->version==b->version&&a->flags==b->flags&&a->levelID==b->levelID&&a->quadCount==b->quadCount&&a->restartCount==b->restartCount&&a->navPathCount==b->navPathCount&&memcmp(a->navPointCounts,b->navPointCounts,sizeof(a->navPointCounts))==0&&a->quadCheckpointDigest==b->quadCheckpointDigest&&a->restartGraphDigest==b->restartGraphDigest&&memcmp(a->navPathDigest,b->navPathDigest,sizeof(a->navPathDigest))==0&&a->fullStreamDigest==b->fullStreamDigest;
}

static void RawPut16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void RawPut32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static void RawFrame(uint8_t *p,int16_t x,int16_t y,int16_t z,const uint8_t rot[4],int16_t xyz,int16_t xz,int16_t flags,int16_t opcode,uint8_t back,uint8_t bits)
{
	RawPut16(p,(uint16_t)x);RawPut16(p+2,(uint16_t)y);RawPut16(p+4,(uint16_t)z);memcpy(p+6,rot,4);RawPut16(p+10,(uint16_t)xyz);RawPut16(p+12,(uint16_t)xz);RawPut16(p+14,(uint16_t)flags);RawPut16(p+16,(uint16_t)opcode);p[18]=back;p[19]=bits;
}
/* Independent normative byte-stream oracle.  It intentionally does not read
 * fixture structs or reuse the adapter's field conversion. */
static int OracleBaseline(struct NativeCanonicalTopologyV1 *out)
{
	static const uint8_t r0[4]={4,0x80,6,7},r1[4]={0xff,2,3,4};
	uint8_t quad[2]={1,0xff},restart[24]={0},nav[3][108]={0};struct NativeCanonicalTopologyV1Input in={0};
	RawPut16(restart,0xffff);RawPut16(restart+2,2);RawPut16(restart+4,0xfffd);RawPut16(restart+6,4);restart[8]=1;restart[9]=0xff;restart[10]=0;restart[11]=0xff;
	RawPut16(restart+12,5);RawPut16(restart+14,0xfffa);RawPut16(restart+16,7);RawPut16(restart+18,8);restart[20]=0;restart[21]=1;restart[22]=0xff;restart[23]=0xff;
	for(uint32_t path=0;path<3;path++) { RawPut32(nav[path],(uint32_t)(-10-(int32_t)path));RawPut16(nav[path]+4,(uint16_t)(-2-(int16_t)path));RawPut16(nav[path]+66,(uint16_t)(30+(int16_t)path));RawFrame(nav[path]+68,(int16_t)(1+path),-2,3,r0,-5,6,-7,8,9,10);RawFrame(nav[path]+88,-11,12,-13,r1,-0x1234,0x7fff,(int16_t)0x8000,-9,0xee,0x80); }
	in.flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;in.levelID=-77;in.quadCount=2;in.restartCount=2;in.quadCheckpoints=quad;in.quadCheckpointSize=2;in.restartStream=restart;in.restartStreamSize=24;
	for(uint32_t path=0;path<3;path++){in.navStreams[path]=nav[path];in.navPointCounts[path]=path==1?1:2;in.navStreamSizes[path]=68+20*in.navPointCounts[path];}
	return NativeCanonicalTopologyV1_FromNormativeStreams(out,&in);
}

static int CaptureAndAtomicFailures(void)
{
	struct Fixture f;struct MainCanonicalTopologyLeaseAuthority authority;struct CaptureGuard guard;struct NativeCanonicalTopologyV1 topology,expected,beforeTopology;
	struct NativeTopologyResidencySnapshotV1 residency,beforeResidency;
	CHECK(InitFixture(&f));memset(&authority,0,sizeof(authority));MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);memset(&guard,0,sizeof(guard));guard.fixture=&f;guard.authority=&authority;
	CHECK(OracleBaseline(&expected));CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));
	CHECK(EqualTopology(&topology,&expected));CHECK(NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));
	CHECK(residency.observed.quadAddress==(uint32_t)(uintptr_t)f.quads&&residency.observed.restartAddress==(uint32_t)(uintptr_t)f.restarts&&residency.observed.nav[0].pointCount==2);
	beforeTopology=topology;beforeResidency=residency;
	CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,NULL,NULL));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);
	f.quads[0].checkpointIndex=2;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.quads[0].checkpointIndex=1;
	f.restarts[0].nextIndex_forward=2;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.restarts[0].nextIndex_forward=1;
	f.nav[1]->header.last=f.nav[1]->frames;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.nav[1]->header.last=f.nav[1]->frames+1;
	/* An absent nav path is a canonical zero prefix.  A present zero-point
	 * header retains its header prefix, but contributes no frames. */
	f.navTable[1]=NULL;guard.calls=0;CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&topology.navPointCounts[1]==0&&NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));f.navTable[1]=&f.nav[1]->header;
	f.nav[0]->header.numPoints=0;f.nav[0]->header.last=f.nav[0]->frames;guard.calls=0;CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&topology.navPointCounts[0]==0&&NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));f.nav[0]->header.numPoints=2;f.nav[0]->header.last=f.nav[0]->frames+2;
	f.navTable[0]=NULL;f.navTable[1]=NULL;f.navTable[2]=NULL;guard.calls=0;CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));f.navTable[0]=&f.nav[0]->header;f.navTable[1]=&f.nav[1]->header;f.navTable[2]=&f.nav[2]->header;
	/* Faults are injected after bootstrap, before the second root read. */
	beforeTopology=topology;beforeResidency=residency;guard.faultCall=4;guard.fault=GUARD_FAULT_RETIRE;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);
	memset(&authority,0,sizeof(authority));MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);guard.fault=GUARD_FAULT_ROOT;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.tracker.level1=f.level;
	guard.fault=GUARD_FAULT_LEVEL;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.tracker.levelID--;
	guard.fault=GUARD_FAULT_NONE;guard.faultCall=0;guard.calls=0;CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));
	return 1;
}

static int GuardFalseEveryPosition(void)
{
	struct Fixture f;struct MainCanonicalTopologyLeaseAuthority authority;struct CaptureGuard guard;struct NativeCanonicalTopologyV1 topology,beforeTopology;struct NativeTopologyResidencySnapshotV1 residency,beforeResidency;uint32_t total;
	CHECK(InitFixture(&f));memset(&authority,0,sizeof(authority));MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);memset(&guard,0,sizeof(guard));guard.fixture=&f;guard.authority=&authority;
	CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));CHECK(NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));total=guard.calls;CHECK(total>4);
	beforeTopology=topology;beforeResidency=residency;
	for(uint32_t call=1;call<=total;call++)
	{
		guard.calls=0;guard.falseCall=call;
		CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));
		/* No later guarded native read or reader callback can run after false. */
		CHECK(guard.calls==call&&memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);
	}
	return 1;
}

static int TrustedGuardLimitation(void)
{
	struct Fixture f;struct MainCanonicalTopologyLeaseAuthority authority;struct CaptureGuard guard;struct NativeCanonicalTopologyV1 first,second;struct NativeTopologyResidencySnapshotV1 witnessFirst,witnessSecond;
	CHECK(InitFixture(&f));memset(&authority,0,sizeof(authority));MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);memset(&guard,0,sizeof(guard));guard.fixture=&f;guard.authority=&authority;
	CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&first,&witnessFirst,&authority,&f.tracker,&f.source,Guard,&guard)&&NativeTopologyResidencyV1_Validate(&witnessFirst,&witnessFirst.lease,&witnessFirst.observed));
	/* The trusted guard remains true while mutating a valid payload in place.
	 * Residency proves structure only, so this is deliberately a different,
	 * still-valid candidate rather than a detected structural fault. */
	guard.calls=0;guard.faultCall=4;guard.fault=GUARD_FAULT_PAYLOAD;
	CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&second,&witnessSecond,&authority,&f.tracker,&f.source,Guard,&guard));
	CHECK(!EqualTopology(&first,&second)&&memcmp(&witnessFirst,&witnessSecond,sizeof(witnessFirst))==0&&NativeTopologyResidencyV1_Validate(&witnessSecond,&witnessSecond.lease,&witnessSecond.observed));
	return 1;
}

static int BoundaryRejections(void)
{
	struct Fixture f;struct MainCanonicalTopologyLeaseAuthority authority;struct CaptureGuard guard;struct NativeCanonicalTopologyV1 topology,beforeTopology;struct NativeTopologyResidencySnapshotV1 residency,beforeResidency;union { struct NativeCanonicalTopologyV1 topology; struct NativeTopologyResidencySnapshotV1 residency; uint8_t bytes[sizeof(struct NativeCanonicalTopologyV1)+sizeof(struct NativeTopologyResidencySnapshotV1)]; } overlap;void *frontier;
	CHECK(InitFixture(&f));memset(&authority,0,sizeof(authority));MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);memset(&guard,0,sizeof(guard));guard.fixture=&f;guard.authority=&authority;
	CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard));beforeTopology=topology;beforeResidency=residency;
	CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(NULL,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);
	CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,NULL,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&topology,&beforeTopology,sizeof(topology))==0);
	memset(&overlap,0xa5,sizeof(overlap));CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&overlap.topology,(struct NativeTopologyResidencySnapshotV1 *)&overlap.topology,&authority,&f.tracker,&f.source,Guard,&guard));for(size_t i=0;i<sizeof(overlap);i++)CHECK(overlap.bytes[i]==0xa5);
	memset(&overlap,0x5a,sizeof(overlap));CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&overlap.topology,(struct NativeTopologyResidencySnapshotV1 *)(overlap.bytes+1),&authority,&f.tracker,&f.source,Guard,&guard));for(size_t i=0;i<sizeof(overlap);i++)CHECK(overlap.bytes[i]==0x5a);
	f.source.Loading.stage=LOAD_IDLE+1;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.source.Loading.stage=LOAD_IDLE;
	f.source.load_inProgress=1;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.source.load_inProgress=0;
	frontier=f.source.mempack[0].firstFreeByte;f.source.mempack[0].firstFreeByte=(void *)((uintptr_t)f.nav[2]+sizeof(*f.nav[2])-1);guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.source.mempack[0].firstFreeByte=frontier;
	f.mesh->ptrQuadBlockArray=(struct QuadBlock *)((uintptr_t)f.quads+2);guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);f.mesh->ptrQuadBlockArray=f.quads;
	f.tracker.gameMode2|=LEV_SWAP;f.tracker.activeMempackIndex=1;f.tracker.levID_in_each_mempack[1]=f.tracker.levelID;f.source.mempack[1]=f.source.mempack[0];guard.calls=0;CHECK(MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&NativeTopologyResidencyV1_Validate(&residency,&residency.lease,&residency.observed));
	beforeTopology=topology;beforeResidency=residency;f.tracker.levID_in_each_mempack[1]++;guard.calls=0;CHECK(!MainCanonicalTopologyLeaseAdapter_Capture(&topology,&residency,&authority,&f.tracker,&f.source,Guard,&guard)&&memcmp(&topology,&beforeTopology,sizeof(topology))==0&&memcmp(&residency,&beforeResidency,sizeof(residency))==0);
	return 1;
}

int main(void) { if(!CaptureAndAtomicFailures()||!GuardFalseEveryPosition()||!TrustedGuardLimitation()||!BoundaryRejections())return 1;puts("main_canonical_topology_lease_adapter_test: passed");return 0; }
