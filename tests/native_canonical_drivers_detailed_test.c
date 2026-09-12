#include "platform/native_canonical_drivers_detailed.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

static int EqualSummary(const struct NativeCanonicalDriversV1 *a, const struct NativeCanonicalDriversV1 *b)
{
	if(a->version!=b->version||a->slotCount!=b->slotCount||a->presenceMask!=b->presenceMask||a->groupCount!=b->groupCount||
		a->rosterMetaDigest!=b->rosterMetaDigest||a->fullStreamDigest!=b->fullStreamDigest)return 0;
	for(uint32_t i=0;i<8;i++)if(a->slots[i].slotDigest!=b->slots[i].slotDigest||a->slots[i].metaRaceDigest!=b->slots[i].metaRaceDigest||
		a->slots[i].physicsDynamicsDigest!=b->slots[i].physicsDynamicsDigest||a->slots[i].behaviorBotDigest!=b->slots[i].behaviorBotDigest)return 0;
	return 1;
}
static void ValidHuman(struct NativeCanonicalDriversDetailedV1 *value)
{
	NativeCanonicalDriversDetailedV1_Init(value);
	value->prelude.presenceMask=1; value->prelude.raceOrderCount=1; value->prelude.raceOrder[0]=0; value->prelude.playerCount=1; value->prelude.humanPlayerPositions[0]=0; value->prelude.numLaps=3;
	value->slots[0].meta.present=1; value->slots[0].meta.slotIndex=0; value->slots[0].meta.driverID=5; value->slots[0].meta.characterID=7;
	value->slots[0].meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_HUMAN; value->slots[0].meta.kartState=2; value->slots[0].meta.actionsFlagSet=UINT32_C(0x11223344);
	value->slots[0].meta.numWumpas=-4; value->slots[0].race.clockReceive=-5; value->slots[0].race.invincibleTimer=-6;
	value->slots[0].physics.currQuadIndex=UINT32_C(0x55667788); value->slots[0].physics.velocity[1]=-7; value->slots[0].physics.rotCurr[3]=-8;
	value->slots[0].dynamics.field[NATIVE_CANONICAL_DRIVER_DYN_SPEED]=9; value->slots[0].dynamics.zSpeed=-10;
}
static int Encode(const struct NativeCanonicalDriversDetailedV1 *value,uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES])
{
	struct NativeCodecWriter writer; NativeCodecWriter_Init(&writer,bytes,NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES,NULL);
	return NativeCanonicalDriversDetailedV1_Encode(&writer,value)&&NativeCodecWriter_Size(&writer)==NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES;
}
static int TestGoldenAndSummary(void)
{
	struct NativeCanonicalDriversDetailedV1 value; struct NativeCanonicalDriversV1 summary,fromStream; uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	ValidHuman(&value); CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)); CHECK(NativeCanonicalDriversDetailedV1_EncodedSize()==4224);
	CHECK(Encode(&value,bytes));
	/* Independent exact LE layout checks: prelude, META, RACE, PHYSICS, DYNAMICS. */
	CHECK(bytes[0]==8&&bytes[1]==0&&bytes[4]==1&&bytes[8]==0&&bytes[9]==0xff&&bytes[16]==1&&bytes[18]==3&&bytes[24]==0&&bytes[25]==0xff&&bytes[59]==1&&bytes[60]==1);
	CHECK(bytes[64]==1&&bytes[65]==0&&bytes[66]==5&&bytes[67]==7&&bytes[68]==NATIVE_CANONICAL_DRIVER_KIND_HUMAN&&bytes[71]==2);
	CHECK(bytes[72]==0x44&&bytes[73]==0x33&&bytes[74]==0x22&&bytes[75]==0x11&&bytes[82]==0xfc&&bytes[104]==0xfb&&bytes[120]==0xfa);
	CHECK(bytes[164]==0x88&&bytes[165]==0x77&&bytes[166]==0x66&&bytes[167]==0x55&&bytes[192]==0xf9&&bytes[284]==0xf8);
	CHECK(bytes[64+100+148+2*NATIVE_CANONICAL_DRIVER_DYN_SPEED]==9&&bytes[64+100+148+2*NATIVE_CANONICAL_DRIVER_DYN_SPEED+1]==0);
	CHECK(NativeCanonicalDriversDetailedV1_BuildSummary(&value,&summary));
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&fromStream,1,bytes,sizeof(bytes))&&EqualSummary(&summary,&fromStream));
	CHECK(summary.fullStreamDigest==UINT64_C(0x10a9f7cb8516b102));
	CHECK(summary.slots[0].metaRaceDigest==UINT64_C(0x83b20f1aaa2437e4));
	CHECK(summary.slots[0].physicsDynamicsDigest==UINT64_C(0x1b1387d3a5422bea));
	CHECK(summary.slots[0].behaviorBotDigest==UINT64_C(0xeb4f61260020abd5));
	return 0;
}
static int TestSemanticMutations(void)
{
	struct NativeCanonicalDriversDetailedV1 value; uint8_t base[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],changed[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	ValidHuman(&value); CHECK(Encode(&value,base));
	value.slots[0].meta.actionsFlagSet^=1;CHECK(Encode(&value,changed)&&memcmp(base,changed,sizeof(base))!=0);value.slots[0].meta.actionsFlagSet^=1;
	value.slots[0].race.distanceDrivenBackwards=1;CHECK(Encode(&value,changed)&&memcmp(base,changed,sizeof(base))!=0);value.slots[0].race.distanceDrivenBackwards=0;
	value.slots[0].physics.axisAngle4[2]=1;CHECK(Encode(&value,changed)&&memcmp(base,changed,sizeof(base))!=0);value.slots[0].physics.axisAngle4[2]=0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_DRIVER_DYN_COUNT;i++){value.slots[0].dynamics.field[i]=1;CHECK(Encode(&value,changed)&&memcmp(base,changed,sizeof(base))!=0);value.slots[0].dynamics.field[i]=0;}
	value.slots[0].dynamics.xSpeed=1;CHECK(Encode(&value,changed)&&memcmp(base,changed,sizeof(base))!=0);value.slots[0].dynamics.xSpeed=0;
	value.slots[0].meta.behaviorID=4;value.slots[0].meta.kartState=2;value.slots[0].active.unionTag=NATIVE_CANONICAL_DRIVER_ACTIVE_DRIFT;value.slots[0].active.branchBytes[0]=1;CHECK(Encode(&value,changed)&&memcmp(base,changed,sizeof(base))!=0);
	return 0;
}
static int TestRejectionAndTransaction(void)
{
	struct NativeCanonicalDriversDetailedV1 value; struct NativeCanonicalDriversV1 out,before; uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],beforeBytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]; struct NativeCodecWriter shortWriter;
	ValidHuman(&value);CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.prelude.raceOrderCount=9;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.prelude.raceOrderCount=1;
	value.slots[0].meta.driverKind=3;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
	value.slots[0].active.unionTag=8;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].active.unionTag=0;
	value.slots[0].active.branchBytes[0]=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].active.branchBytes[0]=0;
	value.prelude.raceOrder[1]=0;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.prelude.raceOrder[1]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;
	value.prelude.winnerCount=1;value.prelude.winnerSlots[0]=4;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.prelude.winnerCount=0;value.prelude.winnerSlots[0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;
	value.prelude.navListCount[0]=1;value.prelude.navListOrder[0][0]=4;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.prelude.navListCount[0]=0;value.prelude.navListOrder[0][0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;
	value.slots[0].bot.bytes[0]=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.bytes[0]=0;
	value.slots[0].meta.boolFirstFrameSinceRevEngine=2;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].meta.boolFirstFrameSinceRevEngine=0;
	value.prelude.detailedVersion=0;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.prelude.detailedVersion=1;
	value.slots[1].meta.present=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[1].meta.present=0;
	CHECK(Encode(&value,bytes));memcpy(beforeBytes,bytes,sizeof(bytes));NativeCodecWriter_Init(&shortWriter,bytes,sizeof(bytes)-1,NULL);
	CHECK(!NativeCanonicalDriversDetailedV1_Encode(&shortWriter,&value)&&shortWriter.offset==0&&memcmp(bytes,beforeBytes,sizeof(bytes))==0);
	NativeCanonicalDriversV1_Init(&out);out.fullStreamDigest=UINT64_C(0x1111111111111111);before=out;value.prelude.numLaps=-1;
	CHECK(!NativeCanonicalDriversDetailedV1_BuildSummary(&value,&out)&&EqualSummary(&out,&before));
	return 0;
}
static int TestBotAndReferences(void)
{
	struct NativeCanonicalDriversDetailedV1 value;
	ValidHuman(&value);value.slots[0].meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_BOT;value.slots[0].meta.behaviorID=1;value.slots[0].meta.threadBehaviorID=2;
	value.prelude.playerCount=0;value.prelude.humanPlayerPositions[0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;value.prelude.activeBotCount=1;
	value.slots[0].bot.bytes[127]=3;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].active.branchBytes[0]=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].active.branchBytes[0]=0;
	value.prelude.humanPlayerPositions[0]=0;value.prelude.playerCount=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	return 0;
}
static int TestRanksAndActiveTags(void)
{
	struct NativeCanonicalDriversDetailedV1 value;
	static const struct { uint8_t behavior,kart; uint32_t tag; } rows[]={
		{1,0,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE},{4,2,NATIVE_CANONICAL_DRIVER_ACTIVE_DRIFT},{7,3,NATIVE_CANONICAL_DRIVER_ACTIVE_SPIN},
		{14,4,NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE},{11,5,NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB},{12,5,NATIVE_CANONICAL_DRIVER_ACTIVE_PLANT_EATEN},
		{15,6,NATIVE_CANONICAL_DRIVER_ACTIVE_BLASTED},{16,10,NATIVE_CANONICAL_DRIVER_ACTIVE_WARP}};
	ValidHuman(&value);value.prelude.humanPlayerPositions[0]=7;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	for(uint32_t tag=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;tag<=NATIVE_CANONICAL_DRIVER_ACTIVE_WARP;tag++)
	{
		value.slots[0].meta.behaviorID=rows[tag].behavior;value.slots[0].meta.kartState=rows[tag].kart;value.slots[0].active.unionTag=rows[tag].tag;value.slots[0].active.branchBytes[0]=(uint8_t)(tag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE?0:1);
		CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	}
	value.slots[0].meta.behaviorID=4;value.slots[0].meta.kartState=2;value.slots[0].active.unionTag=NATIVE_CANONICAL_DRIVER_ACTIVE_DRIFT;value.slots[0].active.branchBytes[0]=0;
	value.prelude.raceOrderCount=0;value.prelude.raceOrder[0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	return 0;
}
int main(void)
{
	if(TestGoldenAndSummary()!=0||TestSemanticMutations()!=0||TestRejectionAndTransaction()!=0||TestBotAndReferences()!=0||TestRanksAndActiveTags()!=0)return 1;
	puts("native_canonical_drivers_detailed_test: passed");return 0;
}
