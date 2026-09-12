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
#include "../game/MAIN/MainCanonicalDrivers.c"
static int ProjectPreludeTest(void){struct NativeCanonicalDriversRosterInput in;struct NativeCanonicalDriversRosterCandidate out,before;DriverFunc tables[8][13]={{0}};void(*threads[8])(struct Thread*)={0};memset(&in,0,sizeof(in));memset(in.raceOrder,0xff,sizeof(in.raceOrder));memset(in.winnerDriverIDs,0xff,sizeof(in.winnerDriverIDs));memset(in.ranks,0xff,sizeof(in.ranks));memset(in.navOrder,0xff,sizeof(in.navOrder));in.slots[3].present=1;in.slots[3].driverID=3;in.slots[3].kind=NATIVE_CANONICAL_DRIVER_KIND_HUMAN;in.playerCount=1;in.raceOrderCount=1;in.raceOrder[0]=3;in.ranks[0]=7;tables[3][1]=VehPhysProc_Driving_Update;tables[3][2]=VehPhysProc_Driving_PhysLinear;tables[3][3]=VehPhysProc_Driving_Audio;tables[3][4]=VehPhysGeneral_PhysAngular;tables[3][5]=VehPhysForce_OnApplyForces;tables[3][6]=COLL_MOVED_PlayerSearch;tables[3][7]=VehPhysForce_CollideDrivers;tables[3][8]=COLL_FIXED_PlayerSearch;tables[3][9]=VehPhysGeneral_JumpAndFriction;tables[3][10]=VehPhysForce_TranslateMatrix;tables[3][11]=VehFrameProc_Driving;tables[3][12]=VehEmitter_DriverMain;threads[3]=VehBirth_NullThread;if(!MainCanonicalDrivers_ProjectPrelude(&in,tables,threads,&out)||out.behaviorID[3]!=1||out.threadBehaviorID[3]!=1)return 0;before=out;tables[3][7]=UnknownDriver;if(MainCanonicalDrivers_ProjectPrelude(&in,tables,threads,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;return 1;}
struct SourceFixture { struct GameTracker tracker; struct Driver drivers[8]; unsigned char instanceBytes[8][0x74]; struct Thread threads[8]; };
static const DriverFunc drivingTable[13]={NULL,VehPhysProc_Driving_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain};
static void SourceDriver(struct SourceFixture *f,uint8_t slot,int bot)
{
	struct Driver *d=&f->drivers[slot];struct Instance *i=(struct Instance *)f->instanceBytes[slot];struct Thread *t=&f->threads[slot];
	d->driverID=slot;d->instSelf=i;memcpy(d->funcPtrs,drivingTable,sizeof(drivingTable));
	if(bot)d->actionsFlagSet=ACTION_BOT;
	i->thread=t;t->object=d;t->inst=i;t->funcThTick=bot?BOTS_ThTick_Drive:NULL;
	f->tracker.drivers[slot]=d;
}
static void SourceFixtureInit(struct SourceFixture *f)
{
	struct sData *sd=&sdata_static;
	memset(f,0,sizeof(*f));memset(sd,0,sizeof(*sd));f->tracker.numLaps=3;
	sd->gGT=&f->tracker;
	SourceDriver(f,0,0);SourceDriver(f,2,1);SourceDriver(f,5,1);
	f->tracker.driversInRaceOrder[0]=&f->drivers[0];f->tracker.driversInRaceOrder[1]=&f->drivers[2];f->tracker.driversInRaceOrder[2]=&f->drivers[5];
	f->tracker.numWinners=2;f->tracker.winnerIndex[0]=5;f->tracker.winnerIndex[1]=0;f->tracker.humanPlayerPositions[0]=7;
	f->drivers[2].botData.botPath=0;f->drivers[5].botData.botPath=2;
	sd->navBotList[0].first=&f->drivers[2].botData.item;sd->navBotList[0].last=&f->drivers[2].botData.item;sd->navBotList[0].count=1;
	sd->navBotList[2].first=&f->drivers[5].botData.item;sd->navBotList[2].last=&f->drivers[5].botData.item;sd->navBotList[2].count=1;
}
static void SourceNavTwo(struct SourceFixture *f)
{
	struct sData *sd=&sdata_static;
	f->drivers[5].botData.botPath=0;f->drivers[2].botData.item.next=&f->drivers[5].botData.item;f->drivers[5].botData.item.prev=&f->drivers[2].botData.item;
	memset(&sd->navBotList[2],0,sizeof(sd->navBotList[2]));
	sd->navBotList[0].first=&f->drivers[2].botData.item;sd->navBotList[0].last=&f->drivers[5].botData.item;sd->navBotList[0].count=2;
}
static void SourceNavInteriorCycle(struct SourceFixture *f)
{
	struct sData *sd=&sdata_static;
	SourceDriver(f,6,1);SourceDriver(f,7,1);
	f->drivers[2].botData.botPath=0;f->drivers[5].botData.botPath=0;f->drivers[6].botData.botPath=0;f->drivers[7].botData.botPath=0;
	memset(&sd->navBotList[2],0,sizeof(sd->navBotList[2]));
	f->drivers[2].botData.item.next=&f->drivers[5].botData.item;
	f->drivers[5].botData.item.prev=&f->drivers[2].botData.item;f->drivers[5].botData.item.next=&f->drivers[7].botData.item;
	f->drivers[7].botData.item.prev=&f->drivers[5].botData.item;f->drivers[7].botData.item.next=&f->drivers[5].botData.item;
	sd->navBotList[0].first=&f->drivers[2].botData.item;sd->navBotList[0].last=&f->drivers[6].botData.item;sd->navBotList[0].count=4;
}
static int SourcePreludeTest(void)
{
	struct SourceFixture f,other;struct NativeCanonicalDriversRosterCandidate out,before;struct sData *sd=&sdata_static;struct Item foreign={0};
	SourceFixtureInit(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	if(out.prelude.numLaps!=3||out.prelude.presenceMask!=0x25||out.prelude.raceOrderCount!=3||out.prelude.winnerSlots[0]!=5||out.prelude.humanPlayerPositions[0]!=7||out.prelude.navListCount[0]!=1||out.prelude.navListOrder[2][0]!=5)return 0;
	/* Human ranks are compacted by stable slot, never race-order position. */
	SourceFixtureInit(&f);SourceDriver(&f,3,0);f.tracker.humanPlayerPositions[0]=6;f.tracker.humanPlayerPositions[3]=1;f.tracker.driversInRaceOrder[3]=&f.drivers[3];
	if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||out.prelude.playerCount!=2||out.prelude.humanPlayerPositions[0]!=6||out.prelude.humanPlayerPositions[1]!=1)return 0;
	SourceFixtureInit(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	#define FAIL_SOURCE(change) do { before=out; change; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0; SourceFixtureInit(&f); } while(0)
	before=out; if(MainCanonicalDrivers_ExtractRosterPrelude(NULL,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;
	before=out; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,NULL,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;
	SourceFixtureInit(&other);before=out;if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0;SourceFixtureInit(&f);
	FAIL_SOURCE(sd->gGT=NULL);
	FAIL_SOURCE(sd->gGT=&other.tracker);
	FAIL_SOURCE(f.tracker.drivers[1]=f.tracker.drivers[0]);
	FAIL_SOURCE(f.drivers[2].driverID=1);
	FAIL_SOURCE(((struct Instance *)f.instanceBytes[2])->thread=NULL);
	FAIL_SOURCE(f.threads[2].object=&f.drivers[0]);
	FAIL_SOURCE(f.threads[2].inst=(struct Instance *)f.instanceBytes[0]);
	FAIL_SOURCE(f.threads[2].flags=THREAD_FLAG_DEAD);
	FAIL_SOURCE(f.threads[2].funcThTick=UnknownThread);
	FAIL_SOURCE(f.drivers[2].funcPtrs[6]=UnknownDriver);
	FAIL_SOURCE(f.tracker.numLaps=-1);
	FAIL_SOURCE(f.tracker.driversInRaceOrder[1]=NULL);
	FAIL_SOURCE(f.tracker.driversInRaceOrder[1]=f.tracker.drivers[0]);
	FAIL_SOURCE(f.tracker.numWinners=5);
	FAIL_SOURCE(f.tracker.winnerIndex[0]=1);
	FAIL_SOURCE(f.tracker.humanPlayerPositions[0]=8);
	FAIL_SOURCE(sd->navBotList[0].count=-1);
	FAIL_SOURCE(sd->navBotList[0].count=9);
	FAIL_SOURCE(sd->navBotList[1].first=&f.drivers[2].botData.item);
	FAIL_SOURCE(sd->navBotList[1].last=&f.drivers[2].botData.item);
	FAIL_SOURCE(f.drivers[2].botData.item.prev=&f.drivers[5].botData.item);
	FAIL_SOURCE(sd->navBotList[0].last=NULL);
	FAIL_SOURCE(sd->navBotList[0].count=2);
	FAIL_SOURCE(sd->navBotList[0].first=&f.drivers[0].botData.item);
	FAIL_SOURCE(sd->navBotList[0].first=&foreign;sd->navBotList[0].last=&foreign);
	/* Endpoints and cursors are mapped against known embedded Items before
	 * dereference, so poison and misaligned-like addresses reject safely. */
	FAIL_SOURCE(sd->navBotList[0].first=(struct Item *)(uintptr_t)1);
	FAIL_SOURCE(sd->navBotList[0].last=(struct Item *)(uintptr_t)1);
	FAIL_SOURCE(sd->navBotList[0].first=(struct Item *)((uintptr_t)&f.drivers[2].botData.item+1));
	FAIL_SOURCE(f.drivers[2].botData.item.next=(struct Item *)(uintptr_t)1;sd->navBotList[0].last=&f.drivers[5].botData.item;sd->navBotList[0].count=2);
	/* A distinct known endpoint with next == NULL reaches the post-walk
	 * declared-last check rather than an endpoint guard. */
	FAIL_SOURCE(sd->navBotList[0].last=&f.drivers[5].botData.item);
	FAIL_SOURCE(f.drivers[2].botData.botPath=1);
	FAIL_SOURCE(sd->navBotList[2].first=&f.drivers[2].botData.item;sd->navBotList[2].last=&f.drivers[2].botData.item);
	FAIL_SOURCE(f.drivers[2].botData.item.next=&f.drivers[2].botData.item;sd->navBotList[0].last=&f.drivers[2].botData.item);
	FAIL_SOURCE(sd->navBotList[0].count=0;sd->navBotList[0].first=NULL;sd->navBotList[0].last=NULL);
	/* Re-establish a valid two-node list, then independently corrupt its links. */
	SourceFixtureInit(&f);SourceNavTwo(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	#define FAIL_TWO(change) do { before=out; change; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0; SourceFixtureInit(&f);SourceNavTwo(&f); } while(0)
	FAIL_TWO(f.drivers[5].botData.item.prev=NULL);
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
	fixture.drivers[6].clockReceive=-123;
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||changed.race[6].clockReceive!=-123)return 0;

	for(uint8_t field=0;field<21;field++)
	{
		uint32_t minimum=RaceMinimum(field),maximum=RaceMaximum(field);
		size_t start=(size_t)64+520u*6u+40u+RaceOffset(field);
		SourceFixtureInit(&fixture);SourceDriver(&fixture,6,0);fixture.tracker.humanPlayerPositions[6]=5;
		SetRaceField(&fixture.drivers[6],field,minimum);
		if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||RaceValue(&changed.race[6],field)!=minimum)return 0;
		SourceFixtureInit(&fixture);SourceDriver(&fixture,6,0);fixture.tracker.humanPlayerPositions[6]=5;
		if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&base)||!DetailedFromRace(&base,&baseDetailed)||!EncodeDetailed(&baseDetailed,baseBytes))return 0;
		SetRaceField(&fixture.drivers[6],field,maximum);
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
	SetRaceField(&fixture.drivers[0],0,(uint32_t)(int32_t)-1);SetRaceField(&fixture.drivers[0],10,(uint32_t)INT32_MIN);
	SetRaceField(&fixture.drivers[0],11,(uint32_t)(int32_t)-7);SetRaceField(&fixture.drivers[0],12,(uint32_t)(int32_t)-1);
	SetRaceField(&fixture.drivers[0],13,UINT8_MAX);SetRaceField(&fixture.drivers[0],15,UINT32_MAX);
	SetRaceField(&fixture.drivers[0],18,(uint32_t)(int32_t)-2);SetRaceField(&fixture.drivers[0],19,(uint32_t)(int32_t)-3);
	SetRaceField(&fixture.drivers[0],20,INT32_MAX);
	if(!MainCanonicalDrivers_ExtractRosterRace(&fixture.tracker,sd,&changed)||changed.race[0].clockReceive!=-1||
		changed.race[0].lapTime!=INT32_MIN||changed.race[0].timeElapsedInRace!=-7||changed.race[0].driverRank!=-1||
		changed.race[0].checkpointBranchChoiceIndex!=UINT8_MAX||changed.race[0].distanceToFinishCurr!=UINT32_MAX||
		changed.race[0].battleNumLives!=-2||changed.race[0].battleTeamID!=-3||changed.race[0].pickupLetterCount!=INT32_MAX)return 0;

	/* A reused output cannot retain Race bytes for an absent second slot. */
	fixture.drivers[5].clockReceive=123;
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
	FAIL_RACE(fixture.drivers[2].driverID=1);
	FAIL_RACE(((struct Instance *)fixture.instanceBytes[2])->thread=NULL);
	FAIL_RACE(fixture.threads[2].object=&fixture.drivers[0]);
	FAIL_RACE(fixture.threads[2].funcThTick=UnknownThread);
	FAIL_RACE(fixture.drivers[2].funcPtrs[6]=UnknownDriver);
	FAIL_RACE(sd->navBotList[0].first=NULL);
	#undef FAIL_RACE
	return 1;
}

int main(void){DriverFunc driving[13]={NULL,VehPhysProc_Driving_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain};uint8_t id=0x5a,keep=id;int binding=MainCanonicalDrivers_ValidateProductionBinding();C(binding==1);C(MainCanonicalDrivers_ProductionRegistry()!=NULL);C(MainCanonicalDrivers_ResolveBehavior(driving,&id)&&id==1);driving[7]=UnknownDriver;C(!MainCanonicalDrivers_ResolveBehavior(driving,&id)&&id==1);C(MainCanonicalDrivers_ResolveThread(NULL,&id)&&id==0);C(MainCanonicalDrivers_ResolveThread(VehBirth_NullThread,&id)&&id==1);C(MainCanonicalDrivers_ResolveThread(BOTS_ThTick_Drive,&id)&&id==2);C(MainCanonicalDrivers_ResolveThread(BOTS_ThTick_RevEngine,&id)&&id==3);id=keep;C(!MainCanonicalDrivers_ResolveThread(UnknownThread,&id)&&id==keep);C(ProjectPreludeTest());C(SourcePreludeTest());C(ThreadOwnershipTest());C(ExhaustiveProductionTokens());C(RaceProjectionTest());puts("main_canonical_drivers_binding_test: passed");return 0;}
