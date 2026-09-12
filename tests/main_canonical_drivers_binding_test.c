#include "common.h"
#include "MAIN/MainCanonicalDrivers.h"
#include "functions.h"
#include <limits.h>
#include <stdio.h>
struct sData sdata_static;
#define C(x) do{if(!(x)){fprintf(stderr,"fail %d\n",__LINE__);return 1;}}while(0)
#define DRIVER_STUB(name) static volatile int name##_called; void name(struct Thread*t,struct Driver*d){(void)t;(void)d;name##_called++;}
DRIVER_STUB(VehPhysProc_Driving_Init) DRIVER_STUB(VehStuckProc_RevEngine_Init) DRIVER_STUB(VehPhysProc_FreezeEndEvent_Init) DRIVER_STUB(VehStuckProc_Warp_Init) DRIVER_STUB(VehStuckProc_RIP_Init) DRIVER_STUB(VehStuckProc_Tumble_Init) DRIVER_STUB(VehStuckProc_PlantEaten_Init) DRIVER_STUB(VehPhysProc_SpinFirst_Init) DRIVER_STUB(VehPhysProc_PowerSlide_InitSetUpdate) DRIVER_STUB(VehPhysProc_SpinFirst_InitSetUpdate)
DRIVER_STUB(VehPhysProc_Driving_Update) DRIVER_STUB(VehPhysProc_Driving_PhysLinear) DRIVER_STUB(VehPhysProc_Driving_Audio) DRIVER_STUB(VehPhysGeneral_PhysAngular) DRIVER_STUB(VehPhysForce_OnApplyForces) DRIVER_STUB(COLL_MOVED_PlayerSearch) DRIVER_STUB(VehPhysForce_CollideDrivers) DRIVER_STUB(COLL_FIXED_PlayerSearch) DRIVER_STUB(VehPhysGeneral_JumpAndFriction) DRIVER_STUB(VehPhysForce_TranslateMatrix) DRIVER_STUB(VehFrameProc_Driving) DRIVER_STUB(VehEmitter_DriverMain)
DRIVER_STUB(VehPhysProc_FreezeEndEvent_PhysLinear) DRIVER_STUB(VehPhysProc_FreezeVShift_Update) DRIVER_STUB(VehPhysProc_FreezeVShift_ReverseOneFrame) DRIVER_STUB(VehPhysProc_PowerSlide_PhysLinear) DRIVER_STUB(VehPhysProc_PowerSlide_Update) DRIVER_STUB(VehPhysProc_PowerSlide_PhysAngular) DRIVER_STUB(VehPhysProc_SlamWall_Update) DRIVER_STUB(VehPhysProc_SlamWall_PhysLinear) DRIVER_STUB(VehPhysProc_SlamWall_PhysAngular) DRIVER_STUB(VehPhysProc_SlamWall_Animate) DRIVER_STUB(VehPhysProc_SpinFirst_PhysLinear) DRIVER_STUB(VehPhysProc_SpinFirst_PhysAngular) DRIVER_STUB(VehFrameProc_Spinning) DRIVER_STUB(VehPhysProc_SpinFirst_Update) DRIVER_STUB(VehPhysProc_SpinLast_Update) DRIVER_STUB(VehPhysProc_SpinLast_PhysLinear) DRIVER_STUB(VehPhysProc_SpinLast_PhysAngular) DRIVER_STUB(VehFrameProc_LastSpin) DRIVER_STUB(VehPhysProc_SpinStop_Update) DRIVER_STUB(VehPhysProc_SpinStop_PhysLinear) DRIVER_STUB(VehPhysProc_SpinStop_PhysAngular) DRIVER_STUB(VehPhysProc_SpinStop_Animate)
DRIVER_STUB(VehStuckProc_MaskGrab_Update) DRIVER_STUB(VehStuckProc_MaskGrab_PhysLinear) DRIVER_STUB(VehStuckProc_MaskGrab_Animate) DRIVER_STUB(VehStuckProc_PlantEaten_Update) DRIVER_STUB(VehStuckProc_PlantEaten_PhysLinear) DRIVER_STUB(VehStuckProc_PlantEaten_Animate) DRIVER_STUB(VehStuckProc_RevEngine_Update) DRIVER_STUB(VehStuckProc_RevEngine_PhysLinear) DRIVER_STUB(VehStuckProc_RevEngine_Animate) DRIVER_STUB(VehStuckProc_Tumble_Update) DRIVER_STUB(VehStuckProc_Tumble_PhysLinear) DRIVER_STUB(VehStuckProc_Tumble_PhysAngular) DRIVER_STUB(VehStuckProc_Tumble_Animate) DRIVER_STUB(VehStuckProc_Warp_PhysAngular)
static volatile int birth_called,drive_called,rev_called; void VehBirth_NullThread(struct Thread*t){(void)t;birth_called++;} void BOTS_ThTick_Drive(struct Thread*t){(void)t;drive_called++;} void BOTS_ThTick_RevEngine(struct Thread*t){(void)t;rev_called++;}
static void UnknownDriver(struct Thread*t,struct Driver*d){(void)t;(void)d;} static void UnknownThread(struct Thread*t){(void)t;}
static volatile int rain_tick_calls,rain_fade_calls,mask_tick_calls;
void RB_RainCloud_ThTick(struct Thread*t){(void)t;rain_tick_calls++;} void RB_RainCloud_FadeAway(struct Thread*t){(void)t;rain_fade_calls++;} void RB_MaskWeapon_ThTick(struct Thread*t){(void)t;mask_tick_calls++;}
#include "../game/MAIN/MainCanonicalDrivers.c"
static int ProjectPreludeTest(void){struct NativeCanonicalDriversRosterInput in;struct NativeCanonicalDriversRosterCandidate out,before;DriverFunc tables[8][13]={{0}};void(*threads[8])(struct Thread*)={0};memset(&in,0,sizeof(in));memset(in.raceOrder,0xff,sizeof(in.raceOrder));memset(in.winnerDriverIDs,0xff,sizeof(in.winnerDriverIDs));memset(in.ranks,0xff,sizeof(in.ranks));memset(in.navOrder,0xff,sizeof(in.navOrder));in.slots[3].present=1;in.slots[3].driverID=3;in.slots[3].kind=NATIVE_CANONICAL_DRIVER_KIND_HUMAN;in.playerCount=1;in.raceOrderCount=1;in.raceOrder[0]=3;in.ranks[0]=7;tables[3][1]=VehPhysProc_Driving_Update;tables[3][2]=VehPhysProc_Driving_PhysLinear;tables[3][3]=VehPhysProc_Driving_Audio;tables[3][4]=VehPhysGeneral_PhysAngular;tables[3][5]=VehPhysForce_OnApplyForces;tables[3][6]=COLL_MOVED_PlayerSearch;tables[3][7]=VehPhysForce_CollideDrivers;tables[3][8]=COLL_FIXED_PlayerSearch;tables[3][9]=VehPhysGeneral_JumpAndFriction;tables[3][10]=VehPhysForce_TranslateMatrix;tables[3][11]=VehFrameProc_Driving;tables[3][12]=VehEmitter_DriverMain;threads[3]=VehBirth_NullThread;if(!MainCanonicalDrivers_ProjectPrelude(&in,tables,threads,&out)||out.behaviorID[3]!=1||out.threadBehaviorID[3]!=1)return 0;before=out;tables[3][7]=UnknownDriver;if(MainCanonicalDrivers_ProjectPrelude(&in,tables,threads,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;return 1;}
#define SOURCE_POOL_ITEMS 8u
#define SOURCE_LARGE_RAW_SIZE 0x670u
#define SOURCE_SMALL_RAW_SIZE 0x48u
#define SOURCE_INSTANCE_RAW_SIZE (sizeof(struct Instance)+sizeof(struct InstDrawPerPlayer))
union SourceLargeSlot { uint32_t alignment; unsigned char bytes[SOURCE_LARGE_RAW_SIZE]; };
union SourceSmallSlot { uint32_t alignment; unsigned char bytes[SOURCE_SMALL_RAW_SIZE]; };
union SourceThreadSlot { uint32_t alignment; unsigned char bytes[sizeof(struct Thread)]; };
union SourceInstanceSlot { uint32_t alignment; unsigned char bytes[SOURCE_INSTANCE_RAW_SIZE]; };
struct SourceFixture {
	struct GameTracker tracker;
	union SourceLargeSlot large[SOURCE_POOL_ITEMS];
	union SourceSmallSlot small[SOURCE_POOL_ITEMS];
	union SourceThreadSlot thread[SOURCE_POOL_ITEMS];
	union SourceInstanceSlot instance[SOURCE_POOL_ITEMS];
	uint8_t largeIndex[8],threadIndex[8],instanceIndex[8];
	uint32_t childThreadMask,childInstanceMask,childSmallMask;
};
CTR_STATIC_ASSERT(sizeof(struct Item)==8);
CTR_STATIC_ASSERT(sizeof(struct Thread)==0x48);
CTR_STATIC_ASSERT(sizeof(struct Instance)==0x74);
CTR_STATIC_ASSERT(sizeof(struct InstDrawPerPlayer)==0x88);
static struct Driver *FD(struct SourceFixture *f,uint8_t slot){return (struct Driver *)(void *)(f->large[slot].bytes+sizeof(struct Item));}
static struct Instance *FI(struct SourceFixture *f,uint8_t slot){return (struct Instance *)(void *)f->instance[slot].bytes;}
static struct Thread *FT(struct SourceFixture *f,uint8_t slot){return (struct Thread *)(void *)f->thread[slot].bytes;}
static struct Driver *FLD(struct SourceFixture *f,uint8_t slot){return FD(f,f->largeIndex[slot]);}
static struct Instance *FLI(struct SourceFixture *f,uint8_t slot){return FI(f,f->instanceIndex[slot]);}
static struct Thread *FLT(struct SourceFixture *f,uint8_t slot){return FT(f,f->threadIndex[slot]);}
static struct Item *FItem(void *base,size_t stride,uint8_t slot){return (struct Item *)((unsigned char *)base+stride*slot);}
static void FList(struct LinkedList *list,void *base,size_t stride,uint32_t mask)
{
	struct Item *previous=NULL;
	memset(list,0,sizeof(*list));
	for(uint8_t slot=0;slot<SOURCE_POOL_ITEMS;slot++)if(mask&(UINT32_C(1)<<slot))
	{
		struct Item *item=FItem(base,stride,slot);item->prev=previous;item->next=NULL;
		if(previous)previous->next=item;else list->first=item;
		previous=item;list->last=item;list->count++;
	}
}
static void FRefreshPools(struct SourceFixture *f)
{
	uint32_t largeAllocated=0,threadAllocated=0,instanceAllocated=0;
	for(uint8_t slot=0;slot<SOURCE_POOL_ITEMS;slot++)if(f->tracker.drivers[slot])
	{
		largeAllocated|=UINT32_C(1)<<f->largeIndex[slot];
		threadAllocated|=UINT32_C(1)<<f->threadIndex[slot];
		instanceAllocated|=UINT32_C(1)<<f->instanceIndex[slot];
	}
	threadAllocated|=f->childThreadMask;
	instanceAllocated|=f->childInstanceMask;
	FList(&f->tracker.JitPools.largeStack.free,f->large,SOURCE_LARGE_RAW_SIZE,~largeAllocated&0xffu);
	FList(&f->tracker.JitPools.thread.free,f->thread,sizeof(struct Thread),~threadAllocated&0xffu);
	FList(&f->tracker.JitPools.instance.free,f->instance,SOURCE_INSTANCE_RAW_SIZE,~instanceAllocated&0xffu);
	FList(&f->tracker.JitPools.instance.taken,f->instance,SOURCE_INSTANCE_RAW_SIZE,instanceAllocated);
	FList(&f->tracker.JitPools.smallStack.free,f->small,SOURCE_SMALL_RAW_SIZE,~f->childSmallMask&0xffu);
}
static void FInitPool(struct JitPool *pool,void *base,uint32_t rawSize)
{
	memset(pool,0,sizeof(*pool));pool->maxItems=SOURCE_POOL_ITEMS;pool->itemSize=rawSize;
	pool->poolSize=(s32)(SOURCE_POOL_ITEMS*rawSize);pool->ptrPoolData=base;
}
static const DriverFunc drivingTable[13]={NULL,VehPhysProc_Driving_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain};
static void SourceDriverAt(struct SourceFixture *f,uint8_t slot,uint8_t largeIndex,uint8_t threadIndex,uint8_t instanceIndex,int bot)
{
	struct Driver *d;struct Instance *i;struct Thread *t;
	if(largeIndex>=SOURCE_POOL_ITEMS||threadIndex>=SOURCE_POOL_ITEMS||instanceIndex>=SOURCE_POOL_ITEMS)return;
	f->largeIndex[slot]=largeIndex;f->threadIndex[slot]=threadIndex;f->instanceIndex[slot]=instanceIndex;
	d=FD(f,largeIndex);i=FI(f,instanceIndex);t=FT(f,threadIndex);
	d->driverID=slot;d->instSelf=i;memcpy(d->funcPtrs,drivingTable,sizeof(drivingTable));
	if(bot)d->actionsFlagSet=ACTION_BOT;
	i->thread=t;t->object=d;t->inst=i;t->funcThTick=bot?BOTS_ThTick_Drive:NULL;
	f->tracker.drivers[slot]=d;
	FRefreshPools(f);
}
static void SourceDriver(struct SourceFixture *f,uint8_t slot,int bot){SourceDriverAt(f,slot,slot,slot,slot,bot);}
static void SourceFixtureInit(struct SourceFixture *f)
{
	struct sData *sd=&sdata_static;
	memset(f,0,sizeof(*f));memset(f->largeIndex,0xff,sizeof(f->largeIndex));memset(f->threadIndex,0xff,sizeof(f->threadIndex));memset(f->instanceIndex,0xff,sizeof(f->instanceIndex));memset(sd,0,sizeof(*sd));f->tracker.numLaps=3;f->tracker.numPlyrCurrGame=1;
	FInitPool(&f->tracker.JitPools.largeStack,f->large,SOURCE_LARGE_RAW_SIZE);
	FInitPool(&f->tracker.JitPools.smallStack,f->small,SOURCE_SMALL_RAW_SIZE);
	FInitPool(&f->tracker.JitPools.thread,f->thread,sizeof(struct Thread));
	FInitPool(&f->tracker.JitPools.instance,f->instance,SOURCE_INSTANCE_RAW_SIZE);
	sd->gGT=&f->tracker;
	SourceDriver(f,0,0);SourceDriver(f,2,1);SourceDriver(f,5,1);
	f->tracker.driversInRaceOrder[0]=FD(f,0);f->tracker.driversInRaceOrder[1]=FD(f,2);f->tracker.driversInRaceOrder[2]=FD(f,5);
	f->tracker.numWinners=2;f->tracker.winnerIndex[0]=5;f->tracker.winnerIndex[1]=0;f->tracker.humanPlayerPositions[0]=7;
	FD(f,2)->botData.botPath=0;FD(f,5)->botData.botPath=2;
	sd->navBotList[0].first=&FD(f,2)->botData.item;sd->navBotList[0].last=&FD(f,2)->botData.item;sd->navBotList[0].count=1;
	sd->navBotList[2].first=&FD(f,5)->botData.item;sd->navBotList[2].last=&FD(f,5)->botData.item;sd->navBotList[2].count=1;
}
static void SourceNavTwo(struct SourceFixture *f)
{
	struct sData *sd=&sdata_static;
	FD(f,5)->botData.botPath=0;FD(f,2)->botData.item.next=&FD(f,5)->botData.item;FD(f,5)->botData.item.prev=&FD(f,2)->botData.item;
	memset(&sd->navBotList[2],0,sizeof(sd->navBotList[2]));
	sd->navBotList[0].first=&FD(f,2)->botData.item;sd->navBotList[0].last=&FD(f,5)->botData.item;sd->navBotList[0].count=2;
}
static void SourceNavInteriorCycle(struct SourceFixture *f)
{
	struct sData *sd=&sdata_static;
	SourceDriver(f,6,1);SourceDriver(f,7,1);
	FD(f,2)->botData.botPath=0;FD(f,5)->botData.botPath=0;FD(f,6)->botData.botPath=0;FD(f,7)->botData.botPath=0;
	memset(&sd->navBotList[2],0,sizeof(sd->navBotList[2]));
	FD(f,2)->botData.item.next=&FD(f,5)->botData.item;
	FD(f,5)->botData.item.prev=&FD(f,2)->botData.item;FD(f,5)->botData.item.next=&FD(f,7)->botData.item;
	FD(f,7)->botData.item.prev=&FD(f,5)->botData.item;FD(f,7)->botData.item.next=&FD(f,5)->botData.item;
	sd->navBotList[0].first=&FD(f,2)->botData.item;sd->navBotList[0].last=&FD(f,6)->botData.item;sd->navBotList[0].count=4;
}
static int SourcePreludeTest(void)
{
	struct SourceFixture f,other;struct NativeCanonicalDriversRosterCandidate out,before;struct sData *sd=&sdata_static;struct Item foreign={0};
	SourceFixtureInit(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	if(out.prelude.numLaps!=3||out.prelude.presenceMask!=0x25||out.prelude.raceOrderCount!=3||out.prelude.winnerSlots[0]!=5||out.prelude.humanPlayerPositions[0]!=7||out.prelude.navListCount[0]!=1||out.prelude.navListOrder[2][0]!=5)return 0;
	/* Human ranks are compacted by stable slot, never race-order position. */
	SourceFixtureInit(&f);SourceDriver(&f,3,0);f.tracker.humanPlayerPositions[0]=6;f.tracker.humanPlayerPositions[3]=1;f.tracker.driversInRaceOrder[3]=FD(&f,3);
	if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||out.prelude.playerCount!=2||out.prelude.humanPlayerPositions[0]!=6||out.prelude.humanPlayerPositions[1]!=1)return 0;
	SourceFixtureInit(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	#define FAIL_SOURCE(change) do { before=out; change; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0; SourceFixtureInit(&f); } while(0)
	before=out; if(MainCanonicalDrivers_ExtractRosterPrelude(NULL,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;
	before=out; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,NULL,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;
	SourceFixtureInit(&other);before=out;if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;SourceFixtureInit(&f);
	FAIL_SOURCE(sd->gGT=NULL);
	FAIL_SOURCE(sd->gGT=&other.tracker);
	FAIL_SOURCE(f.tracker.drivers[1]=f.tracker.drivers[0]);
	FAIL_SOURCE(FD(&f,2)->driverID=1);
	FAIL_SOURCE(FI(&f,2)->thread=NULL);
	FAIL_SOURCE(FT(&f,2)->object=FD(&f,0));
	FAIL_SOURCE(FT(&f,2)->inst=FI(&f,0));
	FAIL_SOURCE(FT(&f,2)->flags=THREAD_FLAG_DEAD);
	FAIL_SOURCE(FT(&f,2)->funcThTick=UnknownThread);
	FAIL_SOURCE(FD(&f,2)->funcPtrs[6]=UnknownDriver);
	FAIL_SOURCE(f.tracker.numLaps=-1);
	FAIL_SOURCE(f.tracker.driversInRaceOrder[1]=NULL);
	FAIL_SOURCE(f.tracker.driversInRaceOrder[1]=f.tracker.drivers[0]);
	FAIL_SOURCE(f.tracker.numWinners=5);
	FAIL_SOURCE(f.tracker.winnerIndex[0]=1);
	FAIL_SOURCE(f.tracker.humanPlayerPositions[0]=8);
	FAIL_SOURCE(sd->navBotList[0].count=-1);
	FAIL_SOURCE(sd->navBotList[0].count=9);
	FAIL_SOURCE(sd->navBotList[1].first=&FD(&f,2)->botData.item);
	FAIL_SOURCE(sd->navBotList[1].last=&FD(&f,2)->botData.item);
	FAIL_SOURCE(FD(&f,2)->botData.item.prev=&FD(&f,5)->botData.item);
	FAIL_SOURCE(sd->navBotList[0].last=NULL);
	FAIL_SOURCE(sd->navBotList[0].count=2);
	FAIL_SOURCE(sd->navBotList[0].first=&FD(&f,0)->botData.item);
	FAIL_SOURCE(sd->navBotList[0].first=&foreign;sd->navBotList[0].last=&foreign);
	/* Endpoints and cursors are mapped against known embedded Items before
	 * dereference, so poison and misaligned-like addresses reject safely. */
	FAIL_SOURCE(sd->navBotList[0].first=(struct Item *)(uintptr_t)1);
	FAIL_SOURCE(sd->navBotList[0].last=(struct Item *)(uintptr_t)1);
	FAIL_SOURCE(sd->navBotList[0].first=(struct Item *)((uintptr_t)&FD(&f,2)->botData.item+1));
	FAIL_SOURCE(FD(&f,2)->botData.item.next=(struct Item *)(uintptr_t)1;sd->navBotList[0].last=&FD(&f,5)->botData.item;sd->navBotList[0].count=2);
	/* A distinct known endpoint with next == NULL reaches the post-walk
	 * declared-last check rather than an endpoint guard. */
	FAIL_SOURCE(sd->navBotList[0].last=&FD(&f,5)->botData.item);
	FAIL_SOURCE(FD(&f,2)->botData.botPath=1);
	FAIL_SOURCE(sd->navBotList[2].first=&FD(&f,2)->botData.item;sd->navBotList[2].last=&FD(&f,2)->botData.item);
	FAIL_SOURCE(FD(&f,2)->botData.item.next=&FD(&f,2)->botData.item;sd->navBotList[0].last=&FD(&f,2)->botData.item);
	FAIL_SOURCE(sd->navBotList[0].count=0;sd->navBotList[0].first=NULL;sd->navBotList[0].last=NULL);
	/* Re-establish a valid two-node list, then independently corrupt its links. */
	SourceFixtureInit(&f);SourceNavTwo(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	#define FAIL_TWO(change) do { before=out; change; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0; SourceFixtureInit(&f);SourceNavTwo(&f); } while(0)
	FAIL_TWO(FD(&f,5)->botData.item.prev=NULL);
	/* count=1 leaves the known second node unconsumed; declared last remains
	 * node 5 with next == NULL, so this reaches the exact post-walk gate. */
	FAIL_TWO(sd->navBotList[0].count=1);
	#undef FAIL_TWO
	/* This enters a 2->5->7->5 interior cycle while the separately declared
	 * known last node 6 has next == NULL, bypassing the endpoint guard. */
	SourceFixtureInit(&f);SourceNavInteriorCycle(&f);before=out;
	if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;
	#undef FAIL_SOURCE
	return 1;
}

static int PoolOwnershipTest(void)
{
	struct SourceFixture f;
	struct NativeCanonicalDriversRosterCandidate prelude,preludeBefore;
	struct MainCanonicalDriversRosterRaceCandidate race,raceBefore;
	struct sData *sd=&sdata_static;
	/* Rootless menu/reset phases have no object to dereference, so intentionally
	 * do not require JitPool snapshots to be initialized. */
	SourceFixtureInit(&f);
	memset(f.tracker.drivers,0,sizeof(f.tracker.drivers));
	memset(f.tracker.driversInRaceOrder,0,sizeof(f.tracker.driversInRaceOrder));
	f.tracker.numWinners=0;memset(sd->navBotList,0,sizeof(sd->navBotList));
	f.tracker.JitPools.largeStack.ptrPoolData=NULL;
	if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||
		!MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race)||prelude.prelude.presenceMask!=0)return 0;
	/* A normal initialized frame and a reset/reuse frame use the same aligned
	 * synthetic raw slots; every present root must pass all three ownership
	 * gates before any native object is read. */
	SourceFixtureInit(&f);
	if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||
		!MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race))return 0;
	#define FAIL_POOL(change) do { \
		SourceFixtureInit(&f); \
		if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||!MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race))return 0; \
		preludeBefore=prelude;raceBefore=race;change; \
		if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||memcmp(&prelude,&preludeBefore,sizeof(prelude))!=0)return 0; \
		if(MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race)||memcmp(&race,&raceBefore,sizeof(race))!=0)return 0; \
	} while(0)
	/* Each raw pool geometry is independently required when a root exists. */
	FAIL_POOL(f.tracker.JitPools.largeStack.ptrPoolData=NULL);
	FAIL_POOL(f.tracker.JitPools.largeStack.poolSize--);
	FAIL_POOL(f.tracker.JitPools.thread.itemSize--);
	FAIL_POOL(f.tracker.JitPools.instance.maxItems=0);
	/* Driver must be the exact large-stack payload at slot + Item, never a
	 * header-adjacent, foreign, one-past, or free-list member pointer. */
	FAIL_POOL(f.tracker.drivers[2]=(struct Driver *)((uintptr_t)FD(&f,2)-1));
	FAIL_POOL(f.tracker.drivers[2]=(struct Driver *)((uintptr_t)FD(&f,2)+1));
	FAIL_POOL(f.tracker.drivers[2]=(struct Driver *)((uintptr_t)FD(&f,2)+DRIVER_NTSC_RETAIL_SIZE));
	FAIL_POOL(f.tracker.drivers[2]=(struct Driver *)(uintptr_t)1);
	FAIL_POOL(f.tracker.drivers[2]=(struct Driver *)(void *)(f.large[3].bytes+sizeof(struct Item)));
	FAIL_POOL(FList(&f.tracker.JitPools.largeStack.free,f.large,SOURCE_LARGE_RAW_SIZE,0xffu));
	/* Thread requires an exact allocated stack slot; its native taken list is
	 * intentionally irrelevant, while the validated free list remains strict. */
	FAIL_POOL(FI(&f,2)->thread=(struct Thread *)((uintptr_t)FT(&f,2)+1));
	FAIL_POOL(FI(&f,2)->thread=(struct Thread *)(uintptr_t)1);
	FAIL_POOL(FI(&f,2)->thread=FT(&f,5));
	FAIL_POOL(FList(&f.tracker.JitPools.thread.free,f.thread,sizeof(struct Thread),0xffu));
	/* Instance is an exact taken member with a required, valid, disjoint free
	 * list; interior/foreign/free/nonmember/malformed/overlap all reject. */
	FAIL_POOL(FD(&f,2)->instSelf=(struct Instance *)((uintptr_t)FI(&f,2)+1));
	FAIL_POOL(FD(&f,2)->instSelf=(struct Instance *)(uintptr_t)1);
	FAIL_POOL(FD(&f,2)->instSelf=FI(&f,5));
	FAIL_POOL(FList(&f.tracker.JitPools.instance.free,f.instance,SOURCE_INSTANCE_RAW_SIZE,0xffu));
	FAIL_POOL(FList(&f.tracker.JitPools.instance.taken,f.instance,SOURCE_INSTANCE_RAW_SIZE,(UINT32_C(1)<<0)|(UINT32_C(1)<<5)));
	FAIL_POOL(f.tracker.JitPools.instance.taken.first=NULL;f.tracker.JitPools.instance.taken.last=NULL;f.tracker.JitPools.instance.taken.count=1);
	FAIL_POOL(f.tracker.JitPools.instance.free=f.tracker.JitPools.instance.taken);
	#undef FAIL_POOL
	return 1;
}

static int PoolPhysicalAllocationTest(void)
{
	struct SourceFixture f;
	struct NativeCanonicalDriversRosterCandidate prelude,preludeBefore;
	struct MainCanonicalDriversRosterRaceCandidate race,raceBefore;
	struct sData *sd=&sdata_static;
	struct Driver *stale;
	/* Stable slot IDs are logical.  The three native pools deliberately use
	 * different physical permutations, and their free/taken lists are rebuilt
	 * from those physical addresses rather than driverID or stable slot. */
	SourceFixtureInit(&f);
	SourceDriverAt(&f,0,3,6,1,0);
	SourceDriverAt(&f,2,7,1,4,1);
	SourceDriverAt(&f,5,1,5,7,1);
	f.tracker.driversInRaceOrder[0]=FLD(&f,0);f.tracker.driversInRaceOrder[1]=FLD(&f,2);f.tracker.driversInRaceOrder[2]=FLD(&f,5);
	memset(sd->navBotList,0,sizeof(sd->navBotList));
	FLD(&f,2)->botData.botPath=0;FLD(&f,5)->botData.botPath=2;
	sd->navBotList[0].first=&FLD(&f,2)->botData.item;sd->navBotList[0].last=&FLD(&f,2)->botData.item;sd->navBotList[0].count=1;
	sd->navBotList[2].first=&FLD(&f,5)->botData.item;sd->navBotList[2].last=&FLD(&f,5)->botData.item;sd->navBotList[2].count=1;
	if(FLD(&f,0)==FD(&f,0)||FLT(&f,0)==FT(&f,0)||FLI(&f,0)==FI(&f,0)||
		!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||
		!MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race)||prelude.prelude.presenceMask!=0x25)return 0;
	/* Return slot zero to all free lists, retain its old pointer as stale, and
	 * prove both public extractors fail without modifying their prior outputs. */
	SourceFixtureInit(&f);
	if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||
		!MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race))return 0;
	stale=FLD(&f,0);f.tracker.drivers[0]=NULL;FRefreshPools(&f);f.tracker.drivers[0]=stale;
	preludeBefore=prelude;raceBefore=race;
	if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||memcmp(&prelude,&preludeBefore,sizeof(prelude))!=0||
		MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race)||memcmp(&race,&raceBefore,sizeof(race))!=0)return 0;
	/* A fresh allocation at independently selected large/thread/instance
	 * indices replaces the stale root and produces the same logical values. */
	SourceDriverAt(&f,0,1,3,4,0);f.tracker.driversInRaceOrder[0]=FLD(&f,0);
	if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&prelude)||
		!MainCanonicalDrivers_ExtractRosterRace(&f.tracker,sd,&race)||
		memcmp(&prelude,&preludeBefore,sizeof(prelude))!=0||memcmp(&race,&raceBefore,sizeof(race))!=0)return 0;
	return 1;
}

static struct Thread *FMetaChild(struct SourceFixture *f,uint8_t threadIndex,uint8_t instanceIndex,uint8_t smallIndex,
	struct Thread *parent,ThreadFunc callback,int modelIndex)
{
	struct Thread *thread=FT(f,threadIndex);
	struct Instance *instance=FI(f,instanceIndex);
	thread->parentThread=parent;thread->siblingThread=NULL;thread->childThread=NULL;
	thread->flags=SMALL|OTHER;thread->funcThTick=callback;thread->modelIndex=(s16)modelIndex;
	thread->object=f->small[smallIndex].bytes+sizeof(struct Item);thread->inst=instance;
	instance->thread=thread;
	f->childThreadMask|=UINT32_C(1)<<threadIndex;
	f->childInstanceMask|=UINT32_C(1)<<instanceIndex;
	f->childSmallMask|=UINT32_C(1)<<smallIndex;
	FRefreshPools(f);
	return thread;
}

static void MetaFixture(struct SourceFixture *f,int cloud,int mask)
{
	struct Driver *driver;
	struct Thread *root,*cloudThread=NULL,*maskThread=NULL;
	SourceFixtureInit(f);driver=FLD(f,0);root=FLT(f,0);
	if(cloud)
	{
		cloudThread=FMetaChild(f,1,1,1,root,RB_RainCloud_ThTick,STATIC_CLOUD);
		driver->thCloud=cloudThread;
	}
	if(mask)
	{
		maskThread=FMetaChild(f,3,3,3,root,RB_MaskWeapon_ThTick,STATIC_AKUAKU);
		driver->kartState=KS_MASK_GRABBED;
		driver->KartStates.MaskGrab.maskObj=(struct MaskHeadWeapon *)maskThread->object;
	}
	if(cloud&&mask)cloudThread->siblingThread=maskThread;
	root->childThread=cloud?cloudThread:maskThread;
}

static int MetaResolve(const struct SourceFixture *f,uint8_t behaviorID,uint8_t kartState,uint32_t activeTag,
	struct MainCanonicalDriversMetaFlags *out)
{
	return MainCanonicalDrivers_ResolveMetaFlags(&f->tracker,FLD((struct SourceFixture *)f,0),
		NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behaviorID,kartState,activeTag,out);
}

static int MetaFlagsTest(void)
{
	struct SourceFixture f;
	struct MainCanonicalDriversMetaFlags flags,before;
	struct Driver *driver;
	struct Thread *root;
	MetaFixture(&f,1,1);
	if(!MetaResolve(&f,11,KS_MASK_GRABBED,NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB,&flags)||
		flags.externalPresenceFlags!=NATIVE_CANONICAL_DRIVER_EXTERNAL_KNOWN_MASK||flags.driverThreadSimFlags!=0)return 0;
	/* Null sources clear their corresponding bit while retaining every fully
	 * validated immediate child in the bounded walk. */
	MetaFixture(&f,0,0);if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||flags.externalPresenceFlags!=0)return 0;
	MetaFixture(&f,1,1);driver=FLD(&f,0);driver->KartStates.MaskGrab.maskObj=NULL;
	if(!MetaResolve(&f,11,KS_MASK_GRABBED,NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB,&flags)||
		flags.externalPresenceFlags!=NATIVE_CANONICAL_DRIVER_EXTERNAL_RAIN_CLOUD)return 0;
	/* The active union is deliberately not read for any other state/kind. */
	MetaFixture(&f,0,0);driver=FLD(&f,0);driver->KartStates.MaskGrab.maskObj=(struct MaskHeadWeapon *)(uintptr_t)1;
	if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||flags.externalPresenceFlags!=0)return 0;
	/* Excluded pointers and unrelated structural root flags have no effect;
	 * DISABLE_COLLISION is the sole thread structural bit persisted here. */
	driver->thTrackingMe=(struct Thread *)(uintptr_t)1;driver->plantEatingMe=(struct Thread *)(uintptr_t)1;
	driver->KartStates.RevEngine.maskObj=(struct MaskHeadWeapon *)(uintptr_t)1;
	driver->ghostTape=(struct GhostTape *)(uintptr_t)1;
	driver->pendingDamageAttacker=(struct Driver *)(uintptr_t)1;
	root=FLT(&f,0);root->flags=THREAD_FLAG_DISABLE_COLLISION|UINT32_C(0x0040);
	if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||
		flags.externalPresenceFlags!=0||flags.driverThreadSimFlags!=NATIVE_CANONICAL_DRIVER_THREAD_SIM_COLLISION_DISABLED)return 0;
	root->flags=UINT32_C(0x0040);
	if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||flags.driverThreadSimFlags!=0)return 0;
	for(uint32_t bit=0;bit<32;bit++)if((UINT32_C(1)<<bit)!=THREAD_FLAG_DEAD&&(UINT32_C(1)<<bit)!=THREAD_FLAG_DISABLE_COLLISION)
	{
		root->flags=UINT32_C(1)<<bit;
		if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||flags.externalPresenceFlags!=0||flags.driverThreadSimFlags!=0)return 0;
	}
	/* A bot has no active union even when the same native bytes contain poison. */
	root->flags=0;
	driver->botData.maskObj=(struct MaskHeadWeapon *)(uintptr_t)1;
	if(!MainCanonicalDrivers_ResolveMetaFlags(&f.tracker,driver,NATIVE_CANONICAL_DRIVER_KIND_BOT,1,KS_NORMAL,
		NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||flags.externalPresenceFlags!=0||flags.driverThreadSimFlags!=0)return 0;
	/* These structural cases have no cloud or mask request, so they isolate the
	 * root/seen-slot gates rather than failing through duplicate source matches. */
	MetaFixture(&f,0,0);if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags))return 0;
	before=flags;root=FLT(&f,0);root->parentThread=root;root->childThread=root;root->siblingThread=NULL;
	if(MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||memcmp(&flags,&before,sizeof(flags))!=0)return 0;
	MetaFixture(&f,0,0);if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags))return 0;
	before=flags;root=FLT(&f,0);root->childThread=root;root->siblingThread=NULL;
	if(MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||memcmp(&flags,&before,sizeof(flags))!=0)return 0;
	/* First establish a valid unrelated child.  Repeating its exact pool slot
	 * then reaches seen-by-index rejection before a capacity-bound fallback. */
	MetaFixture(&f,0,0);root=FLT(&f,0);FMetaChild(&f,1,1,1,root,VehBirth_NullThread,OTHER);root->childThread=FT(&f,1);
	if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags))return 0;
	before=flags;FT(&f,1)->siblingThread=FT(&f,1);
	if(MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||memcmp(&flags,&before,sizeof(flags))!=0)return 0;
	/* The child is listed free while every root allocation remains valid. */
	MetaFixture(&f,0,0);root=FLT(&f,0);FMetaChild(&f,1,1,1,root,VehBirth_NullThread,OTHER);root->childThread=FT(&f,1);
	if(!MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags))return 0;
	before=flags;FList(&f.tracker.JitPools.thread.free,f.thread,sizeof(struct Thread),UINT32_C(0xda));
	if(MetaResolve(&f,1,KS_NORMAL,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE,&flags)||memcmp(&flags,&before,sizeof(flags))!=0)return 0;

	#define FAIL_META(change) do { \
		MetaFixture(&f,1,1);if(!MetaResolve(&f,11,KS_MASK_GRABBED,NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB,&flags))return 0; \
		before=flags;change; \
		if(MetaResolve(&f,11,KS_MASK_GRABBED,NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB,&flags)||memcmp(&flags,&before,sizeof(flags))!=0)return 0; \
	} while(0)
	/* Each independently snapshotted pool and its ownership lists are gates. */
	FAIL_META(f.tracker.JitPools.thread.itemSize--);
	FAIL_META(f.tracker.JitPools.instance.poolSize--);
	FAIL_META(f.tracker.JitPools.smallStack.ptrPoolData=NULL);
	FAIL_META(FList(&f.tracker.JitPools.thread.free,f.thread,sizeof(struct Thread),0xffu));
	FAIL_META(FList(&f.tracker.JitPools.instance.free,f.instance,SOURCE_INSTANCE_RAW_SIZE,0xffu));
	FAIL_META(FList(&f.tracker.JitPools.smallStack.free,f.small,SOURCE_SMALL_RAW_SIZE,0xffu));
	/* A valid-but-missing taken entry is distinct from a malformed/overlapping
	 * instance list: both must reject the child's reciprocal ownership. */
	FAIL_META(FList(&f.tracker.JitPools.instance.taken,f.instance,SOURCE_INSTANCE_RAW_SIZE,
		(UINT32_C(1)<<0)|(UINT32_C(1)<<2)|(UINT32_C(1)<<3)|(UINT32_C(1)<<5)));
	FAIL_META(f.tracker.JitPools.instance.taken.first=NULL;f.tracker.JitPools.instance.taken.last=NULL;f.tracker.JitPools.instance.taken.count=1);
	/* Child, instance, and bounded sibling graph corruption is rejected before
	 * any foreign/poison cursor can be dereferenced. */
	FAIL_META(FT(&f,1)->parentThread=NULL);
	FAIL_META(FLT(&f,0)->flags|=THREAD_FLAG_DEAD);
	FAIL_META(FT(&f,1)->flags|=THREAD_FLAG_DEAD);
	FAIL_META(FT(&f,1)->inst=NULL);
	FAIL_META(FT(&f,1)->inst=(struct Instance *)(uintptr_t)1);
	FAIL_META(FI(&f,1)->thread=FT(&f,3));
	FAIL_META(FLT(&f,0)->childThread=(struct Thread *)(uintptr_t)1);
	FAIL_META(FT(&f,1)->siblingThread=(struct Thread *)(uintptr_t)1);
	FAIL_META(FT(&f,1)->siblingThread=FT(&f,1));
	FAIL_META(driver=FLD(&f,0);driver->thCloud=(struct Thread *)(uintptr_t)1);
	/* Exact callback/model/SMALL/payload identities distinguish active sources
	 * from fades, foreign models, and header/interior/poison object pointers. */
	FAIL_META(FT(&f,1)->funcThTick=RB_RainCloud_FadeAway);
	FAIL_META(FT(&f,1)->modelIndex=STATIC_AKUAKU);
	FAIL_META(FT(&f,1)->flags=(FT(&f,1)->flags&~0x300u)|MEDIUM);
	FAIL_META(FT(&f,1)->object=NULL);
	FAIL_META(FT(&f,1)->object=(void *)((uintptr_t)FT(&f,1)->object+1));
	FAIL_META(FT(&f,1)->object=(void *)(uintptr_t)1);
	FAIL_META(FT(&f,3)->funcThTick=RB_RainCloud_ThTick);
	FAIL_META(FT(&f,3)->modelIndex=STATIC_CLOUD);
	FAIL_META(driver=FLD(&f,0);driver->KartStates.MaskGrab.maskObj=(struct MaskHeadWeapon *)(uintptr_t)1);
	/* Keep the identity match but move the Mask object into its payload.  This
	 * reaches the exact small-stack payload-start gate rather than merely the
	 * pointer-to-child equality check. */
	FAIL_META(driver=FLD(&f,0);FT(&f,3)->object=(void *)((uintptr_t)FT(&f,3)->object+1);driver->KartStates.MaskGrab.maskObj=(struct MaskHeadWeapon *)FT(&f,3)->object);
	/* Repeating an immediate child makes the requested cloud identity occur
	 * twice; the exact-once gate rejects before a prefix can be accepted. */
	FAIL_META(FT(&f,3)->siblingThread=FT(&f,1));
	#undef FAIL_META
	return 1;
}
static int ExactKnown(const DriverFunc table[13],uint8_t *id)
{
	for(uint8_t i=0;i<11;i++)for(uint8_t s=0;s<17;s++){uint8_t n;if(table[0]!=initFunctions[i])continue;for(n=0;n<12;n++)if(table[n+1]!=suffixFunctions[s][n])break;if(n==12){*id=(uint8_t)(s+17*i);return 1;}}
	return 0;
}
static int ExhaustiveProductionTokens(void)
{
	DriverFunc known[128],table[13];uint8_t count=0,id,before,expected;
	for(uint8_t i=0;i<11;i++){uint8_t k;for(k=0;k<count;k++)if(known[k]==initFunctions[i])break;if(k==count)known[count++]=initFunctions[i];}
	for(uint8_t s=0;s<17;s++)for(uint8_t n=0;n<12;n++){uint8_t k;for(k=0;k<count;k++)if(known[k]==suffixFunctions[s][n])break;if(k==count)known[count++]=suffixFunctions[s][n];}
	for(uint8_t i=0;i<11;i++)for(uint8_t s=0;s<17;s++)
	{
		table[0]=initFunctions[i];for(uint8_t n=0;n<12;n++)table[n+1]=suffixFunctions[s][n];id=UINT8_MAX;if(!MainCanonicalDrivers_ResolveBehavior(table,&id)||id!=(uint8_t)(s+17*i))return 0;
		for(uint8_t field=0;field<13;field++)for(uint8_t k=0;k<count;k++)if(table[field]!=known[k])
		{
			DriverFunc saved=table[field];table[field]=known[k];before=id;
			if(ExactKnown(table,&expected)){if(!MainCanonicalDrivers_ResolveBehavior(table,&id)||id!=expected||id==before)return 0;}
			else if(MainCanonicalDrivers_ResolveBehavior(table,&id)||id!=before)return 0;
			table[field]=saved;
		}
		for(uint8_t field=0;field<13;field++){DriverFunc saved=table[field];table[field]=UnknownDriver;before=id;if(MainCanonicalDrivers_ResolveBehavior(table,&id)||id!=before)return 0;table[field]=saved;}
	}
	return 1;
}
static int ThreadOwnershipTest(void)
{
	void(*callbacks[4])(struct Thread*)={NULL,VehBirth_NullThread,BOTS_ThTick_Drive,BOTS_ThTick_RevEngine};
	for(uint8_t kind=NATIVE_CANONICAL_DRIVER_KIND_HUMAN;kind<=NATIVE_CANONICAL_DRIVER_KIND_BOT;kind++)for(uint8_t id=0;id<4;id++)
	{
		int allowed=(kind==NATIVE_CANONICAL_DRIVER_KIND_HUMAN)?id<2:id>=2;
		if(NativeCanonicalDriverBehavior_ValidateKind(kind,1,id)!=allowed)return 0;
		if(callbacks[id]==UnknownThread)return 0;
	}
	/* A converted bot legitimately retains a human suffix but only bot callbacks. */
	if(!NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_BOT,1,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE))return 0;
	return 1;
}
static void SetRaceField(struct Driver *driver, uint8_t field, uint32_t value)
{
	switch(field)
	{
		case 0: driver->clockReceive=(s16)(int32_t)value;break;
		case 1: driver->hazardTimer=(s16)(int32_t)value;break;
		case 2: driver->superEngineTimer=(s16)(int32_t)value;break;
		case 3: driver->itemRollTimer=(s16)(int32_t)value;break;
		case 4: driver->noItemTimer=(s16)(int32_t)value;break;
		case 5: driver->jumpMeter=(s16)(int32_t)value;break;
		case 6: driver->jumpMeterTimer=(s16)(int32_t)value;break;
		case 7: driver->numTurbos=(s16)(int32_t)value;break;
		case 8: driver->invincibleTimer=(int)(int32_t)value;break;
		case 9: driver->invisibleTimer=(int)(int32_t)value;break;
		case 10: driver->lapTime=(int)(int32_t)value;break;
		case 11: driver->timeElapsedInRace=(int)(int32_t)value;break;
		case 12: driver->driverRank=(s16)(int32_t)value;break;
		case 13: driver->checkpoint.branchChoiceIndex=(uint8_t)value;break;
		case 14: driver->checkpoint.currentIndex=(uint8_t)value;break;
		case 15: driver->distanceToFinish_curr=value;break;
		case 16: driver->distanceToFinish_checkpoint=value;break;
		case 17: driver->distanceDrivenBackwards=value;break;
		case 18: driver->BattleHUD.numLives=(int)(int32_t)value;break;
		case 19: driver->BattleHUD.teamID=(int)(int32_t)value;break;
		case 20: driver->PickupLetterHUD.numCollected=(int)(int32_t)value;break;
	}
}

static uint32_t RaceValue(const struct NativeCanonicalDriverRaceV1 *race, uint8_t field)
{
	switch(field)
	{
		case 0:return (uint32_t)(int32_t)race->clockReceive;case 1:return (uint32_t)(int32_t)race->hazardTimer;
		case 2:return (uint32_t)(int32_t)race->superEngineTimer;case 3:return (uint32_t)(int32_t)race->itemRollTimer;
		case 4:return (uint32_t)(int32_t)race->noItemTimer;case 5:return (uint32_t)(int32_t)race->jumpMeter;
		case 6:return (uint32_t)(int32_t)race->jumpMeterTimer;case 7:return (uint32_t)(int32_t)race->numTurbos;
		case 8:return (uint32_t)race->invincibleTimer;case 9:return (uint32_t)race->invisibleTimer;
		case 10:return (uint32_t)race->lapTime;case 11:return (uint32_t)race->timeElapsedInRace;
		case 12:return (uint32_t)(int32_t)race->driverRank;case 13:return race->checkpointBranchChoiceIndex;
		case 14:return race->checkpointCurrentIndex;case 15:return race->distanceToFinishCurr;
		case 16:return race->distanceToFinishCheckpoint;case 17:return race->distanceDrivenBackwards;
		case 18:return (uint32_t)race->battleNumLives;case 19:return (uint32_t)race->battleTeamID;
		default:return (uint32_t)race->pickupLetterCount;
	}
}

static uint8_t RaceWidth(uint8_t field)
{
	if(field<8||field==12)return 2;
	if(field==13||field==14)return 1;
	return 4;
}

static uint8_t RaceOffset(uint8_t field)
{
	static const uint8_t offsets[21]={0,2,4,6,8,10,12,14,16,20,24,28,32,34,35,36,40,44,48,52,56};
	return offsets[field];
}

static uint32_t RaceMinimum(uint8_t field)
{
	if(field<8||field==12)return (uint32_t)(int32_t)INT16_MIN;
	if((field>=8&&field<=11)||field>=18)return (uint32_t)INT32_MIN;
	return 0;
}

static uint32_t RaceMaximum(uint8_t field)
{
	if(field<8||field==12)return INT16_MAX;
	if((field>=8&&field<=11)||field>=18)return INT32_MAX;
	return field==13||field==14?UINT8_MAX:UINT32_MAX;
}

static int DetailedFromRace(const struct MainCanonicalDriversRosterRaceCandidate *candidate,
	struct NativeCanonicalDriversDetailedV1 *detailed)
{
	NativeCanonicalDriversDetailedV1_Init(detailed);
	detailed->prelude=candidate->roster.prelude;
	for(uint8_t slot=0;slot<8;slot++)
	{
		struct NativeCanonicalDriverMetaV1 *meta=&detailed->slots[slot].meta;
		if((candidate->roster.prelude.presenceMask&(UINT32_C(1)<<slot))==0)continue;
		meta->present=1;meta->slotIndex=slot;meta->driverID=slot;
		meta->driverKind=candidate->roster.kind[slot];meta->behaviorID=candidate->roster.behaviorID[slot];
		meta->threadBehaviorID=candidate->roster.threadBehaviorID[slot];meta->kartState=KS_NORMAL;
		detailed->slots[slot].race=candidate->race[slot];
	}
	return NativeCanonicalDriversDetailedV1_Validate(detailed);
}

static int EncodeDetailed(const struct NativeCanonicalDriversDetailedV1 *detailed,
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES])
{
	struct NativeCodecWriter writer;
	NativeCodecWriter_Init(&writer,bytes,NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES,NULL);
	return NativeCanonicalDriversDetailedV1_Encode(&writer,detailed)&&
		NativeCodecWriter_Size(&writer)==NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES;
}

static int RaceProjectionTest(void)
{
	struct SourceFixture fixture,other;
	struct sData *sd=&sdata_static;
	struct MainCanonicalDriversRosterRaceCandidate base,changed,before;
	struct NativeCanonicalDriversDetailedV1 baseDetailed,changedDetailed;
	uint8_t baseBytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],changedBytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];

	SourceFixtureInit(&fixture);SourceDriver(&fixture,6,0);fixture.tracker.humanPlayerPositions[6]=5;
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&base)||base.roster.prelude.raceOrderCount!=3||
		(base.roster.prelude.presenceMask&(UINT32_C(1)<<6))==0||base.roster.prelude.playerCount!=2)return 0;
	/* Slot 6 is deliberately absent from driversInRaceOrder.  Stable root slots,
	 * not race order, are the Race extraction order. */
	FD(&fixture,6)->clockReceive=-123;
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||changed.race[6].clockReceive!=-123)return 0;

	for(uint8_t field=0;field<21;field++)
	{
		uint32_t minimum=RaceMinimum(field),maximum=RaceMaximum(field);
		size_t start=(size_t)64+520u*6u+40u+RaceOffset(field);
		SourceFixtureInit(&fixture);SourceDriver(&fixture,6,0);fixture.tracker.humanPlayerPositions[6]=5;
		SetRaceField(FD(&fixture,6),field,minimum);
		if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||RaceValue(&changed.race[6],field)!=minimum)return 0;
		SourceFixtureInit(&fixture);SourceDriver(&fixture,6,0);fixture.tracker.humanPlayerPositions[6]=5;
		if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&base)||!DetailedFromRace(&base,&baseDetailed)||!EncodeDetailed(&baseDetailed,baseBytes))return 0;
		SetRaceField(FD(&fixture,6),field,maximum);
		if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||RaceValue(&changed.race[6],field)!=maximum||
			!DetailedFromRace(&changed,&changedDetailed)||!EncodeDetailed(&changedDetailed,changedBytes))return 0;
		for(size_t byte=0;byte<sizeof(baseBytes);byte++)
		{
			int target=byte>=start&&byte<start+RaceWidth(field);
			if((baseBytes[byte]!=changedBytes[byte])!=target)return 0;
			if(target&&changedBytes[byte]!=(uint8_t)(maximum>>(8u*(byte-start))))return 0;
		}
	}
	/* Pre-race, negative, finished, and battle values are copied verbatim; no
	 * lifecycle or ghost special case exists beyond ACTION_BOT classification. */
	SourceFixtureInit(&fixture);
	SetRaceField(FD(&fixture,0),0,(uint32_t)(int32_t)-1);SetRaceField(FD(&fixture,0),10,(uint32_t)INT32_MIN);
	SetRaceField(FD(&fixture,0),11,(uint32_t)(int32_t)-7);SetRaceField(FD(&fixture,0),12,(uint32_t)(int32_t)-1);
	SetRaceField(FD(&fixture,0),13,UINT8_MAX);SetRaceField(FD(&fixture,0),15,UINT32_MAX);
	SetRaceField(FD(&fixture,0),18,(uint32_t)(int32_t)-2);SetRaceField(FD(&fixture,0),19,(uint32_t)(int32_t)-3);
	SetRaceField(FD(&fixture,0),20,INT32_MAX);
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||changed.race[0].clockReceive!=-1||
		changed.race[0].lapTime!=INT32_MIN||changed.race[0].timeElapsedInRace!=-7||changed.race[0].driverRank!=-1||
		changed.race[0].checkpointBranchChoiceIndex!=UINT8_MAX||changed.race[0].distanceToFinishCurr!=UINT32_MAX||
		changed.race[0].battleNumLives!=-2||changed.race[0].battleTeamID!=-3||changed.race[0].pickupLetterCount!=INT32_MAX)return 0;

	/* A reused output cannot retain Race bytes for an absent second slot. */
	FD(&fixture,5)->clockReceive=123;
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed))return 0;
	fixture.tracker.drivers[5]=NULL;fixture.tracker.driversInRaceOrder[2]=NULL;fixture.tracker.numWinners=1;fixture.tracker.winnerIndex[0]=0;
	memset(&sd->navBotList[2],0,sizeof(sd->navBotList[2]));
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||
		(changed.roster.prelude.presenceMask&(UINT32_C(1)<<5))!=0||memcmp(&changed.race[5],&(struct NativeCanonicalDriverRaceV1){0},sizeof(changed.race[5]))!=0)return 0;

	SourceFixtureInit(&fixture);if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed))return 0;
	before=changed;if(MainCanonicalDrivers_ExtractRosterRace(NULL,sd,&changed)||memcmp(&changed,&before,sizeof(changed))!=0)return 0;
	#define FAIL_RACE(change) do { before=changed; change; if(MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||memcmp(&changed,&before,sizeof(changed))!=0)return 0; SourceFixtureInit(&fixture); } while(0)
	FAIL_RACE(sd->gGT=NULL);
	SourceFixtureInit(&other);before=changed;if(MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||memcmp(&changed,&before,sizeof(changed))!=0)return 0;SourceFixtureInit(&fixture);
	FAIL_RACE(fixture.tracker.drivers[1]=fixture.tracker.drivers[0]);
	FAIL_RACE(FD(&fixture,2)->driverID=1);
	FAIL_RACE(FI(&fixture,2)->thread=NULL);
	FAIL_RACE(FT(&fixture,2)->object=FD(&fixture,0));
	FAIL_RACE(FT(&fixture,2)->funcThTick=UnknownThread);
	FAIL_RACE(FD(&fixture,2)->funcPtrs[6]=UnknownDriver);
	FAIL_RACE(sd->navBotList[0].first=NULL);
	#undef FAIL_RACE
	return 1;
}

static void SetDynamicsField(struct Driver *driver,uint8_t field,uint32_t value)
{
	int16_t v=(int16_t)(int32_t)value;
	switch(field)
	{
		case NATIVE_CANONICAL_DRIVER_DYN_AMP_TURN_STATE:driver->ampTurnState=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_BUTTON_USED_TO_START_DRIFT:driver->buttonUsedToStartDrift=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_WALL_RUB_SPEED_LIMIT:driver->wallRubSpeedLimit=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_WHEEL_ROTATION:driver->wheelRotation=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_SPEED:driver->speed=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_SPEED_APPROX:driver->speedApprox=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_HEIGHT_CURR:driver->jumpHeightCurr=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_HEIGHT_PREV:driver->jumpHeightPrev=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_AXIS_ROTATION_Y:driver->axisRotationY=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_AXIS_ROTATION_X:driver->axisRotationX=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_ANGLE:driver->angle=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_BASE_SPEED:driver->baseSpeed=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_FIRE_SPEED:driver->fireSpeed=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_FORWARD_ACCEL_IMPULSE:driver->forwardAccelImpulse=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_ROTATION_SPIN_RATE:driver->rotationSpinRate=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_ACCEL_TAP_WINDOW_TIMER:driver->accelTapWindowTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_ACCEL_TAP_COUNT:driver->accelTapCount=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TERRAIN_SCALED_BASE_SPEED:driver->terrainScaledBaseSpeed=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_CURR:driver->turnAngleCurr=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_PREV:driver->turnAnglePrev=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_LERP_TARGET:driver->turnAngleLerpTarget=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_LERP_VEL:driver->turnAngleLerpVel=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_ANGLE:driver->turnWobbleAngle=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_VELOCITY:driver->turnWobbleVelocity=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_TIMER:driver->turnWobbleTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_MULT_DRIFT:driver->multDrift=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURBO_METER_ROOM_LEFT:driver->turbo_MeterRoomLeft=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TURBO_OUTSIDE_TIMER:driver->turbo_outsideTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_RESERVES:driver->reserves=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_FIRE_SPEED_CAP:driver->fireSpeedCap=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_NUM_FRAMES_SPENT_STEERING:driver->numFramesSpentSteering=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_FORWARD_DIR:driver->forwardDir=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_PREVIOUS_FRAME_MULT_DRIFT:driver->previousFrameMultDrift=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TIME_UNTIL_DRIFT_SPINOUT:driver->timeUntilDriftSpinout=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_DISTANCE_FROM_GROUND:driver->distanceFromGround=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_TEN_BUFFER:driver->jump_TenBuffer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_COOLDOWN_MS:driver->jump_CooldownMS=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_COYOTE_TIMER_MS:driver->jump_CoyoteTimerMS=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_FORCED_MS:driver->jump_ForcedMS=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_INITIAL_VEL_Y:driver->jump_InitialVelY=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_HIGH_JUMP_TIMER_MS:driver->jump_HighJumpTimerMS=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_LANDING_BOOST:driver->jump_LandingBoost=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_WALL_RUB_TIMER:driver->wallRubTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_NO_INPUT_TIMER:driver->NoInputTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_BURN_TIMER:driver->burnTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_SQUISH_TIMER:driver->squishTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_START_GUARD_TIMER:driver->vShiftStartGuardTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_WINDOW_TIMER:driver->vShiftWindowTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_COUNT:driver->vShiftCount=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_SQUISH_STRETCH:driver->jumpSquishStretch=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_JUMP_SQUISH_STRETCH2:driver->jumpSquishStretch2=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_TERRAIN_FRICTION_TIMER:driver->terrainFrictionTimer=v;break;
		case NATIVE_CANONICAL_DRIVER_DYN_COUNT:driver->xSpeed=(int)(int32_t)value;break;
		case NATIVE_CANONICAL_DRIVER_DYN_COUNT+1:driver->ySpeed=(int)(int32_t)value;break;
		case NATIVE_CANONICAL_DRIVER_DYN_COUNT+2:driver->zSpeed=(int)(int32_t)value;break;
	}
}

static uint32_t DynamicsValue(const struct NativeCanonicalDriverDynamicsV1 *dynamics,uint8_t field)
{
	if(field<NATIVE_CANONICAL_DRIVER_DYN_COUNT)return (uint32_t)(int32_t)dynamics->field[field];
	if(field==NATIVE_CANONICAL_DRIVER_DYN_COUNT)return (uint32_t)dynamics->xSpeed;
	if(field==NATIVE_CANONICAL_DRIVER_DYN_COUNT+1)return (uint32_t)dynamics->ySpeed;
	return (uint32_t)dynamics->zSpeed;
}

static uint8_t DynamicsWidth(uint8_t field) { return field<NATIVE_CANONICAL_DRIVER_DYN_COUNT?2u:4u; }
static size_t DynamicsOffset(uint8_t field)
{
	return field<NATIVE_CANONICAL_DRIVER_DYN_COUNT?(size_t)field*2u:
		104u+(size_t)(field-NATIVE_CANONICAL_DRIVER_DYN_COUNT)*4u;
}
static uint32_t DynamicsMinimum(uint8_t field)
{
	return field<NATIVE_CANONICAL_DRIVER_DYN_COUNT?(uint32_t)(int32_t)INT16_MIN:(uint32_t)INT32_MIN;
}
static uint32_t DynamicsMaximum(uint8_t field)
{
	return field<NATIVE_CANONICAL_DRIVER_DYN_COUNT?INT16_MAX:INT32_MAX;
}

static int DetailedFromRaceDynamics(const struct MainCanonicalDriversRosterRaceDynamicsCandidate *candidate,
	struct NativeCanonicalDriversDetailedV1 *detailed)
{
	NativeCanonicalDriversDetailedV1_Init(detailed);
	detailed->prelude=candidate->roster.prelude;
	for(uint8_t slot=0;slot<8;slot++)
	{
		struct NativeCanonicalDriverMetaV1 *meta=&detailed->slots[slot].meta;
		if((candidate->roster.prelude.presenceMask&(UINT32_C(1)<<slot))==0)continue;
		meta->present=1;meta->slotIndex=slot;meta->driverID=slot;
		meta->driverKind=candidate->roster.kind[slot];meta->behaviorID=candidate->roster.behaviorID[slot];
		meta->threadBehaviorID=candidate->roster.threadBehaviorID[slot];meta->kartState=KS_NORMAL;
		detailed->slots[slot].race=candidate->race[slot];
		detailed->slots[slot].dynamics=candidate->dynamics[slot];
	}
	return NativeCanonicalDriversDetailedV1_Validate(detailed);
}

static int DynamicsProjectionTest(void)
{
	struct SourceFixture fixture,other;
	struct sData *sd=&sdata_static;
	struct MainCanonicalDriversRosterRaceDynamicsCandidate base,changed,before;
	struct NativeCanonicalDriversDetailedV1 baseDetailed,changedDetailed;
	uint8_t baseBytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],changedBytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVER_DYN_COUNT==52u);
	CTR_STATIC_ASSERT(sizeof(struct NativeCanonicalDriverDynamicsV1)==NATIVE_CANONICAL_DRIVERS_DYNAMICS_BYTES);

	/* Slot and physical-pool identities are independent of race order. */
	SourceFixtureInit(&fixture);SourceDriverAt(&fixture,6,7,1,4,0);fixture.tracker.humanPlayerPositions[6]=5;
	SetDynamicsField(FLD(&fixture,6),NATIVE_CANONICAL_DRIVER_DYN_SPEED,(uint32_t)(int32_t)-123);
	if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&base)||
		base.roster.prelude.raceOrderCount!=3||base.dynamics[6].field[NATIVE_CANONICAL_DRIVER_DYN_SPEED]!=-123)return 0;

	/* Every signed source value reaches precisely its canonical field and wire
	 * bytes.  The 52 s16s precede x/y/zSpeed's three s32s at dynamics offset 0. */
	for(uint8_t field=0;field<NATIVE_CANONICAL_DRIVER_DYN_COUNT+3;field++)
	{
		uint32_t minimum=DynamicsMinimum(field),maximum=DynamicsMaximum(field);
		size_t start=64u+520u*6u+40u+60u+148u+DynamicsOffset(field);
		SourceFixtureInit(&fixture);SourceDriverAt(&fixture,6,7,1,4,0);fixture.tracker.humanPlayerPositions[6]=5;
		SetDynamicsField(FLD(&fixture,6),field,minimum);
		if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed)||DynamicsValue(&changed.dynamics[6],field)!=minimum)return 0;
		SourceFixtureInit(&fixture);SourceDriverAt(&fixture,6,7,1,4,0);fixture.tracker.humanPlayerPositions[6]=5;
		if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&base)||
			!DetailedFromRaceDynamics(&base,&baseDetailed)||!EncodeDetailed(&baseDetailed,baseBytes))return 0;
		SetDynamicsField(FLD(&fixture,6),field,maximum);
		if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed)||DynamicsValue(&changed.dynamics[6],field)!=maximum||
			!DetailedFromRaceDynamics(&changed,&changedDetailed)||!EncodeDetailed(&changedDetailed,changedBytes))return 0;
		for(size_t byte=0;byte<sizeof(baseBytes);byte++)
		{
			int target=byte>=start&&byte<start+DynamicsWidth(field);
			if((baseBytes[byte]!=changedBytes[byte])!=target)return 0;
			if(target&&changedBytes[byte]!=(uint8_t)(maximum>>(8u*(byte-start))))return 0;
		}
	}

	/* A conversion changes roster kind/thread identity but not the named
	 * Dynamics values; no behavior branch selects a different source field. */
	SourceFixtureInit(&fixture);SetDynamicsField(FD(&fixture,0),NATIVE_CANONICAL_DRIVER_DYN_COUNT,(uint32_t)(int32_t)-9);
	if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&base))return 0;
	FD(&fixture,0)->actionsFlagSet=ACTION_BOT;FD(&fixture,0)->botData.botPath=1;FLT(&fixture,0)->funcThTick=BOTS_ThTick_Drive;
	sd->navBotList[1].first=&FD(&fixture,0)->botData.item;sd->navBotList[1].last=&FD(&fixture,0)->botData.item;sd->navBotList[1].count=1;
	if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed)||
		changed.roster.kind[0]!=NATIVE_CANONICAL_DRIVER_KIND_BOT||memcmp(&base.dynamics[0],&changed.dynamics[0],sizeof(base.dynamics[0]))!=0)return 0;

	/* A reused result cannot retain a Dynamics group for an absent root. */
	SourceFixtureInit(&fixture);SourceDriverAt(&fixture,6,7,1,4,0);SetDynamicsField(FLD(&fixture,6),0,99);
	if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed))return 0;
	fixture.tracker.drivers[6]=NULL;FRefreshPools(&fixture);
	if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed)||
		(changed.roster.prelude.presenceMask&(UINT32_C(1)<<6))!=0||
		memcmp(&changed.dynamics[6],&(struct NativeCanonicalDriverDynamicsV1){0},sizeof(changed.dynamics[6]))!=0)return 0;

	SourceFixtureInit(&fixture);if(!MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed))return 0;
	before=changed;if(MainCanonicalDrivers_ExtractRosterRaceDynamics(NULL,sd,&changed)||memcmp(&changed,&before,sizeof(changed))!=0)return 0;
	#define FAIL_DYNAMICS(change) do { before=changed; change; if(MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed)||memcmp(&changed,&before,sizeof(changed))!=0)return 0; SourceFixtureInit(&fixture); } while(0)
	FAIL_DYNAMICS(sd->gGT=NULL);
	SourceFixtureInit(&other);before=changed;if(MainCanonicalDrivers_ExtractRosterRaceDynamics(&fixture.tracker,sd,&changed)||memcmp(&changed,&before,sizeof(changed))!=0)return 0;SourceFixtureInit(&fixture);
	FAIL_DYNAMICS(fixture.tracker.drivers[1]=fixture.tracker.drivers[0]);
	FAIL_DYNAMICS(FD(&fixture,2)->driverID=1);
	FAIL_DYNAMICS(FI(&fixture,2)->thread=NULL);
	FAIL_DYNAMICS(FT(&fixture,2)->object=FD(&fixture,0));
	FAIL_DYNAMICS(FT(&fixture,2)->funcThTick=UnknownThread);
	FAIL_DYNAMICS(FD(&fixture,2)->funcPtrs[6]=UnknownDriver);
	FAIL_DYNAMICS(sd->navBotList[0].first=NULL);
	#undef FAIL_DYNAMICS
	return 1;
}

int main(void){DriverFunc driving[13]={NULL,VehPhysProc_Driving_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain};uint8_t id=0x5a,keep=id;int binding=MainCanonicalDrivers_ValidateProductionBinding();C(binding==1);C(MainCanonicalDrivers_ProductionRegistry()!=NULL);C(MainCanonicalDrivers_ResolveBehavior(driving,&id)&&id==1);driving[7]=UnknownDriver;C(!MainCanonicalDrivers_ResolveBehavior(driving,&id)&&id==1);C(MainCanonicalDrivers_ResolveThread(NULL,&id)&&id==0);C(MainCanonicalDrivers_ResolveThread(VehBirth_NullThread,&id)&&id==1);C(MainCanonicalDrivers_ResolveThread(BOTS_ThTick_Drive,&id)&&id==2);C(MainCanonicalDrivers_ResolveThread(BOTS_ThTick_RevEngine,&id)&&id==3);id=keep;C(!MainCanonicalDrivers_ResolveThread(UnknownThread,&id)&&id==keep);C(ProjectPreludeTest());C(SourcePreludeTest());C(PoolOwnershipTest());C(PoolPhysicalAllocationTest());C(MetaFlagsTest());C(ThreadOwnershipTest());C(ExhaustiveProductionTokens());C(RaceProjectionTest());C(DynamicsProjectionTest());puts("main_canonical_drivers_binding_test: passed");return 0;}
