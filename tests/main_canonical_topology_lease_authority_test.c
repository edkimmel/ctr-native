#include "common.h"
#include "MAIN/MainCanonicalTopologyLeaseAuthority.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

struct sData sdata_static;
#include "../game/MAIN/MainCanonicalTopologyLeaseAuthority.c"

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

static uintptr_t Align(uintptr_t value,size_t alignment){return (value+(alignment-1u))&~(uintptr_t)(alignment-1u);}
static int Put(struct Fixture *f,uintptr_t *cursor,size_t bytes,size_t alignment,void **out)
{
	uintptr_t value=Align(*cursor,alignment),limit=(uintptr_t)f->pack.bytes+sizeof(f->pack.bytes);
	if(value>limit||bytes>limit-value)return 0;*out=(void *)value;*cursor=value+bytes;return 1;
}
static int InitFixture(struct Fixture *f)
{
	uintptr_t cursor=(uintptr_t)f->pack.bytes;void *p;
	memset(f,0,sizeof(*f));cursor=(uintptr_t)f->pack.bytes;
	CHECK(Put(f,&cursor,sizeof(*f->level),_Alignof(struct Level),&p));f->level=p;
	CHECK(Put(f,&cursor,sizeof(*f->mesh),_Alignof(struct mesh_info),&p));f->mesh=p;
	CHECK(Put(f,&cursor,2*sizeof(*f->quads),_Alignof(struct QuadBlock),&p));f->quads=p;
	CHECK(Put(f,&cursor,2*sizeof(*f->restarts),_Alignof(struct CheckpointNode),&p));f->restarts=p;
	CHECK(Put(f,&cursor,3*sizeof(*f->navTable),_Alignof(struct NavHeader *),&p));f->navTable=p;
	for(unsigned i=0;i<3;i++) { CHECK(Put(f,&cursor,sizeof(*f->nav[i]),_Alignof(struct NavStorage),&p));f->nav[i]=p;f->nav[i]->header.magicNumber=NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC;f->nav[i]->header.numPoints=2;f->nav[i]->header.last=f->nav[i]->frames+2;f->navTable[i]=&f->nav[i]->header; }
	f->level->ptr_mesh_info=f->mesh;f->level->cnt_restart_points=2;f->level->ptr_restart_points=f->restarts;f->level->LevNavTable=f->navTable;
	f->mesh->numQuadBlock=2;f->mesh->ptrQuadBlockArray=f->quads;
	f->source.gGT=&f->tracker;f->source.Loading.stage=LOAD_IDLE;f->source.load_inProgress=0;
	f->tracker.level1=f->level;f->tracker.levelID=7;f->tracker.levID_in_each_mempack[0]=7;
	f->source.mempack[0].start=f->pack.bytes;f->source.mempack[0].firstFreeByte=(void *)cursor;
	f->source.mempack[0].lastFreeByte=(void *)((uintptr_t)f->pack.bytes+PACK_BYTES);
	f->source.mempack[0].endOfAllocator=f->source.mempack[0].lastFreeByte;
	f->source.mempack[0].packSize=PACK_BYTES;
	return 1;
}

static int AuthorityAndLease(void)
{
	struct Fixture f;struct MainCanonicalTopologyLeaseAuthority authority,beforeAuthority;
	struct MainCanonicalTopologyLease lease,before,stale;
	CHECK(InitFixture(&f));memset(&authority,0,sizeof(authority));
	MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);
	CHECK(authority.tag==MAIN_CANONICAL_TOPOLOGY_LEASE_AUTHORITY_TAG&&authority.initialized==1&&authority.epoch==1&&authority.retired==0);
	CHECK(MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));before=lease;
	beforeAuthority=authority;authority.tag=0;CHECK(!MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));authority=beforeAuthority;
	CHECK(lease.residency.base==(uint32_t)(uintptr_t)f.pack.bytes&&lease.residency.span==(uint32_t)((uintptr_t)f.source.mempack[0].firstFreeByte-(uintptr_t)f.pack.bytes));
	#define FAIL(change) do { before=lease;change;CHECK(!MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));CHECK(memcmp(&lease,&before,sizeof(lease))==0); } while(0)
	FAIL(f.source.gGT=NULL);f.source.gGT=&f.tracker;
	FAIL(f.source.Loading.stage=0);f.source.Loading.stage=LOAD_IDLE;
	FAIL(f.source.load_inProgress=1);f.source.load_inProgress=0;
	FAIL(f.source.mempack[0].firstFreeByte=f.source.mempack[0].start);f.source.mempack[0].firstFreeByte=(void *)((uintptr_t)f.pack.bytes+0x1000u);
	FAIL(f.source.mempack[0].lastFreeByte=(void *)((uintptr_t)f.source.mempack[0].firstFreeByte-1u));f.source.mempack[0].lastFreeByte=(void *)((uintptr_t)f.pack.bytes+PACK_BYTES);
	FAIL(f.source.mempack[0].endOfAllocator=(void *)((uintptr_t)f.source.mempack[0].lastFreeByte-1u));f.source.mempack[0].endOfAllocator=f.source.mempack[0].lastFreeByte;
	FAIL(f.source.mempack[0].packSize=PACK_BYTES-1);f.source.mempack[0].packSize=PACK_BYTES;
	FAIL(f.tracker.gameMode2|=LEV_SWAP;f.tracker.activeMempackIndex=3);f.tracker.gameMode2=0;f.tracker.activeMempackIndex=0;
	FAIL(f.tracker.gameMode2|=LEV_SWAP;f.tracker.activeMempackIndex=1;f.tracker.levID_in_each_mempack[1]=f.tracker.levelID+1);f.tracker.gameMode2=0;f.tracker.activeMempackIndex=0;f.tracker.levID_in_each_mempack[1]=0;
	#undef FAIL
	CHECK(MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));stale=lease;
	MainCanonicalTopologyLeaseAuthority_Retire(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_CHECKPOINT_RESTORE);
	CHECK(authority.epoch==2&&authority.retired==1&&!MainCanonicalTopologyLease_Validate(&stale,&authority,&f.tracker,&f.source));
	/* A restore may reuse every address, but the matching post-init activation
	 * must not mint another generation or revive the stale lease. */
	MainCanonicalTopologyLeaseAuthority_ActivatePostInit(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_CHECKPOINT_RESTORE);
	CHECK(authority.epoch==2&&authority.retired==0&&MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source)&&!MainCanonicalTopologyLease_Validate(&stale,&authority,&f.tracker,&f.source));
	beforeAuthority=authority;authority.epoch=0;CHECK(!MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));authority=beforeAuthority;
	authority.epoch=UINT64_MAX;MainCanonicalTopologyLeaseAuthority_Retire(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(authority.epoch==UINT64_MAX&&authority.retired==0&&!MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));authority=beforeAuthority;
	f.tracker.gameMode2|=LEV_SWAP;f.tracker.activeMempackIndex=1;f.tracker.levID_in_each_mempack[1]=f.tracker.levelID;f.source.mempack[1]=f.source.mempack[0];
	CHECK(MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source)&&lease.mempackIndex==1);
	return 1;
}

static int AuthorityTransitions(void)
{
	struct MainCanonicalTopologyLeaseAuthority authority,before;
	memset(&authority,0,sizeof(authority));
	MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);
	CHECK(authority.epoch==1&&authority.retired==0&&authority.retireReason==0);
	/* Construction is idempotent and cannot be used as an implicit activation. */
	before=authority;MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_COLD_BOOT);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);
	/* Active -> retired advances once; duplicate/invalid requests are atomic. */
	MainCanonicalTopologyLeaseAuthority_Retire(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
	CHECK(authority.epoch==2&&authority.retired==1&&authority.retireReason==MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
	before=authority;MainCanonicalTopologyLeaseAuthority_Retire(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);
	MainCanonicalTopologyLeaseAuthority_ActivatePostInit(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_HUB_SWAP);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);
	/* The post-init match reactivates exactly the retired generation. */
	MainCanonicalTopologyLeaseAuthority_ActivatePostInit(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
	CHECK(authority.epoch==2&&authority.retired==0&&authority.retireReason==0);
	before=authority;MainCanonicalTopologyLeaseAuthority_ActivatePostInit(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_FULL_LOAD);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);
	/* Terminal/corrupt epoch states are fail-closed and state-atomic. */
	authority.epoch=0;before=authority;MainCanonicalTopologyLeaseAuthority_Retire(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);MainCanonicalTopologyLeaseAuthority_ActivatePostInit(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);
	authority.epoch=UINT64_MAX;before=authority;MainCanonicalTopologyLeaseAuthority_Retire(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);MainCanonicalTopologyLeaseAuthority_ActivatePostInit(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_RETIRE_ARENA_RESET);
	CHECK(memcmp(&authority,&before,sizeof(authority))==0);
	return 1;
}

static int Observation(void)
{
	struct Fixture f;struct MainCanonicalTopologyLeaseAuthority authority;struct MainCanonicalTopologyLease lease;
	struct NativeTopologyResidencyObservedV1 observed,before;
	CHECK(InitFixture(&f));MainCanonicalTopologyLeaseAuthority_Init(&authority,MAIN_CANONICAL_TOPOLOGY_LEASE_INIT_TEST);CHECK(MainCanonicalTopologyLease_Acquire(&lease,&authority,&f.tracker,&f.source));
	CHECK(MainCanonicalTopologyLease_ObservePostInit(&observed,&lease,&authority,&f.tracker,&f.source));
	CHECK(observed.levelAddress==(uint32_t)(uintptr_t)f.level&&observed.meshAddress==(uint32_t)(uintptr_t)f.mesh&&observed.quadCount==2&&observed.restartCount==2&&observed.nav[0].pointCount==2);
	#define BAD(change) do { before=observed;change;CHECK(!MainCanonicalTopologyLease_ObservePostInit(&observed,&lease,&authority,&f.tracker,&f.source));CHECK(memcmp(&observed,&before,sizeof(observed))==0); } while(0)
	BAD(f.nav[0]->header.last=f.nav[0]->frames+1);f.nav[0]->header.last=f.nav[0]->frames+2;
	BAD(f.nav[0]->header.last=(struct NavFrame *)((uintptr_t)f.nav[0]->frames+2*sizeof(struct NavFrame)+1));f.nav[0]->header.last=f.nav[0]->frames+2;
	BAD(f.nav[0]->header.numPoints=3);f.nav[0]->header.numPoints=2;
	BAD(f.mesh->ptrQuadBlockArray=(struct QuadBlock *)((uintptr_t)f.source.mempack[0].firstFreeByte-4));f.mesh->ptrQuadBlockArray=f.quads;
	BAD(f.level->LevNavTable=(struct NavHeader **)((uintptr_t)f.source.mempack[0].firstFreeByte-4));f.level->LevNavTable=f.navTable;
	BAD(f.source.mempack[0].firstFreeByte=(void *)((uintptr_t)f.nav[0]+sizeof(*f.nav[0])-1));f.source.mempack[0].firstFreeByte=(void *)((uintptr_t)f.pack.bytes+0x1000u);
	#undef BAD
	return 1;
}

int main(void){CHECK(AuthorityAndLease());CHECK(AuthorityTransitions());CHECK(Observation());puts("main_canonical_topology_lease_authority_test: passed");return 0;}
