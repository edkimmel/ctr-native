#include "common.h"
#include "MAIN/MainCanonicalDrivers.h"
#include "functions.h"
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
	FAIL_SOURCE(f.drivers[2].botData.botPath=1);
	FAIL_SOURCE(sd->navBotList[2].first=&f.drivers[2].botData.item;sd->navBotList[2].last=&f.drivers[2].botData.item);
	FAIL_SOURCE(f.drivers[2].botData.item.next=&f.drivers[2].botData.item;sd->navBotList[0].last=&f.drivers[2].botData.item);
	FAIL_SOURCE(sd->navBotList[0].count=0;sd->navBotList[0].first=NULL;sd->navBotList[0].last=NULL);
	/* Re-establish a valid two-node list, then independently corrupt its links. */
	SourceFixtureInit(&f);SourceNavTwo(&f);if(!MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out))return 0;
	#define FAIL_TWO(change) do { before=out; change; if(MainCanonicalDrivers_ExtractRosterPrelude(&f.tracker,sd,&out)||memcmp(&out,&before,sizeof(out))!=0)return 0; SourceFixtureInit(&f);SourceNavTwo(&f); } while(0)
	FAIL_TWO(f.drivers[5].botData.item.prev=NULL);
	FAIL_TWO(sd->navBotList[0].last=&f.drivers[2].botData.item);
	FAIL_TWO(sd->navBotList[0].count=1;sd->navBotList[0].last=&f.drivers[2].botData.item);
	FAIL_TWO(f.drivers[5].botData.item.next=&f.drivers[2].botData.item);
	#undef FAIL_TWO
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
int main(void){DriverFunc driving[13]={NULL,VehPhysProc_Driving_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain};uint8_t id=0x5a,keep=id;int binding=MainCanonicalDrivers_ValidateProductionBinding();C(binding==1);C(MainCanonicalDrivers_ProductionRegistry()!=NULL);C(MainCanonicalDrivers_ResolveBehavior(driving,&id)&&id==1);driving[7]=UnknownDriver;C(!MainCanonicalDrivers_ResolveBehavior(driving,&id)&&id==1);C(MainCanonicalDrivers_ResolveThread(NULL,&id)&&id==0);C(MainCanonicalDrivers_ResolveThread(VehBirth_NullThread,&id)&&id==1);C(MainCanonicalDrivers_ResolveThread(BOTS_ThTick_Drive,&id)&&id==2);C(MainCanonicalDrivers_ResolveThread(BOTS_ThTick_RevEngine,&id)&&id==3);id=keep;C(!MainCanonicalDrivers_ResolveThread(UnknownThread,&id)&&id==keep);C(ProjectPreludeTest());C(SourcePreludeTest());C(ThreadOwnershipTest());C(ExhaustiveProductionTokens());puts("main_canonical_drivers_binding_test: passed");return 0;}
