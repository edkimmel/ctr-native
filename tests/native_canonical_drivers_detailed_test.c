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
	struct NativeCanonicalDriversDetailedV1 value; struct NativeCanonicalDriversV1 summary,fromStream,before; uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],scratch[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	ValidHuman(&value); CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)); CHECK(NativeCanonicalDriversDetailedV1_EncodedSize()==4224);
	CHECK(Encode(&value,bytes));
	/* Independent exact LE layout checks: prelude, META, RACE, PHYSICS, DYNAMICS. */
	CHECK(bytes[0]==8&&bytes[1]==0&&bytes[4]==1&&bytes[8]==0&&bytes[9]==0xff&&bytes[16]==1&&bytes[18]==3&&bytes[24]==0&&bytes[25]==0xff&&bytes[59]==1&&bytes[60]==NATIVE_CANONICAL_DRIVERS_DETAILED_VERSION);
	CHECK(bytes[64]==1&&bytes[65]==0&&bytes[66]==5&&bytes[67]==7&&bytes[68]==NATIVE_CANONICAL_DRIVER_KIND_HUMAN&&bytes[71]==2);
	CHECK(bytes[72]==0x44&&bytes[73]==0x33&&bytes[74]==0x22&&bytes[75]==0x11&&bytes[82]==0xfc&&bytes[104]==0xfb&&bytes[120]==0xfa);
	CHECK(bytes[164]==0x88&&bytes[165]==0x77&&bytes[166]==0x66&&bytes[167]==0x55&&bytes[192]==0xf9&&bytes[284]==0xf8);
	CHECK(bytes[64+100+148+2*NATIVE_CANONICAL_DRIVER_DYN_SPEED]==9&&bytes[64+100+148+2*NATIVE_CANONICAL_DRIVER_DYN_SPEED+1]==0);
	CHECK(NativeCanonicalDriversDetailedV1_BuildSummary(&value,&summary));
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&fromStream,1,bytes,sizeof(bytes))&&EqualSummary(&summary,&fromStream));
	CHECK(NativeCanonicalDriversDetailedV1_BuildSummaryWithScratch(&value,scratch,sizeof(scratch),&fromStream)&&EqualSummary(&summary,&fromStream));
	before=fromStream;CHECK(!NativeCanonicalDriversDetailedV1_BuildSummaryWithScratch(&value,scratch,sizeof(scratch)-1u,&fromStream)&&EqualSummary(&fromStream,&before));
	CHECK(summary.version==NATIVE_CANONICAL_DRIVERS_VERSION&&summary.rosterMetaDigest==UINT64_C(0xdd0ffa2ea7a59e77));
	CHECK(summary.slots[0].metaRaceDigest==UINT64_C(0x83b20f1aaa2437e4));
	CHECK(summary.slots[0].physicsDynamicsDigest==UINT64_C(0x1b1387d3a5422bea));
	CHECK(summary.slots[0].behaviorBotDigest==UINT64_C(0xeb4f61260020abd5));
	CHECK(summary.fullStreamDigest==UINT64_C(0xcaced5d331e66801));
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
	value.slots[0].bot.botPath=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.botPath=0;
	value.slots[0].meta.boolFirstFrameSinceRevEngine=2;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].meta.boolFirstFrameSinceRevEngine=0;
	value.prelude.detailedVersion=1;NativeCodecWriter_Init(&shortWriter,bytes,sizeof(bytes),NULL);memcpy(beforeBytes,bytes,sizeof(bytes));NativeCanonicalDriversV1_Init(&out);out.fullStreamDigest=UINT64_C(0x1234567812345678);before=out;CHECK(!NativeCanonicalDriversDetailedV1_Encode(&shortWriter,&value)&&shortWriter.offset==0&&memcmp(bytes,beforeBytes,sizeof(bytes))==0);CHECK(!NativeCanonicalDriversDetailedV1_BuildSummary(&value,&out)&&EqualSummary(&out,&before));CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.prelude.detailedVersion=NATIVE_CANONICAL_DRIVERS_DETAILED_VERSION;
	value.slots[1].meta.present=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[1].meta.present=0;
	CHECK(Encode(&value,bytes));memcpy(beforeBytes,bytes,sizeof(bytes));NativeCodecWriter_Init(&shortWriter,bytes,sizeof(bytes)-1,NULL);
	CHECK(!NativeCanonicalDriversDetailedV1_Encode(&shortWriter,&value)&&shortWriter.offset==0&&memcmp(bytes,beforeBytes,sizeof(bytes))==0);
	NativeCanonicalDriversV1_Init(&out);out.fullStreamDigest=UINT64_C(0x1111111111111111);before=out;value.prelude.numLaps=-1;
	CHECK(!NativeCanonicalDriversDetailedV1_BuildSummary(&value,&out)&&EqualSummary(&out,&before));
	return 0;
}
static int TestPendingDamageTail(void)
{
	struct NativeCanonicalDriversDetailedV1 value;
	struct NativeCanonicalDriversV1 baseSummary,changedSummary;
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],changed[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	ValidHuman(&value);
	/* A second fully present human makes slot 1 a valid, distinct attacker. */
	value.slots[1]=value.slots[0];value.slots[1].meta.slotIndex=1;value.slots[1].meta.driverID=6;
	value.prelude.presenceMask=3;value.prelude.raceOrderCount=2;value.prelude.raceOrder[1]=1;
	value.prelude.playerCount=2;value.prelude.humanPlayerPositions[1]=1;
	value.slots[0].pendingDamage.type=2;value.slots[0].pendingDamage.attackerSlotPlusOne=2;
	value.slots[0].pendingDamage.reason=6;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));CHECK(Encode(&value,bytes));CHECK(NativeCanonicalDriversDetailedV1_BuildSummary(&value,&baseSummary));
	CHECK(bytes[64+516]==2&&bytes[64+517]==2&&bytes[64+518]==6&&bytes[64+519]==0);
	value.slots[0].pendingDamage.reason=0;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));CHECK(Encode(&value,changed));CHECK(NativeCanonicalDriversDetailedV1_BuildSummary(&value,&changedSummary));
	CHECK(memcmp(bytes,changed,sizeof(bytes))!=0&&baseSummary.rosterMetaDigest==changedSummary.rosterMetaDigest&&
		baseSummary.slots[0].metaRaceDigest==changedSummary.slots[0].metaRaceDigest&&baseSummary.slots[0].physicsDynamicsDigest==changedSummary.slots[0].physicsDynamicsDigest&&
		baseSummary.slots[0].behaviorBotDigest!=changedSummary.slots[0].behaviorBotDigest&&baseSummary.fullStreamDigest!=changedSummary.fullStreamDigest);
	value.slots[0].pendingDamage.type=3;value.slots[0].pendingDamage.reason=5;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].pendingDamage.reason=6;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].pendingDamage.type=2;value.slots[0].pendingDamage.attackerSlotPlusOne=1;value.slots[0].pendingDamage.reason=0;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].pendingDamage.attackerSlotPlusOne=3;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].pendingDamage.type=0;value.slots[0].pendingDamage.attackerSlotPlusOne=2;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	return 0;
}
static int TestTypedBotLayout(void)
{
	struct NativeCanonicalDriversDetailedV1 value;struct NativeCanonicalDriversV1 summary,changedSummary;uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES],changed[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	ValidHuman(&value);value.slots[0].meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_BOT;value.slots[0].meta.behaviorID=1;value.slots[0].meta.threadBehaviorID=3;value.slots[0].meta.kartState=NATIVE_CANONICAL_DRIVER_KART_STATE_MASK_GRABBED;value.prelude.playerCount=0;value.prelude.activeBotCount=1;value.prelude.humanPlayerPositions[0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;
	value.slots[0].bot.botPath=2;value.slots[0].bot.botNavFrameIndex=UINT16_C(0x1234);value.slots[0].bot.navProgressRemainder=INT32_C(-2);value.slots[0].bot.botFlags=UINT32_C(0x1);value.slots[0].bot.botAccel=INT32_MAX;value.slots[0].bot.aiDamageState=2;value.slots[0].bot.rotXZ=INT16_MIN;value.slots[0].bot.speedLinear=INT32_MIN;value.slots[0].bot.maskObjPresent=1;value.slots[0].bot.weaponCooldown=-3;value.slots[0].bot.desiredPathBossOnly=2;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));CHECK(Encode(&value,bytes));
	CHECK(bytes[64+388]==2&&bytes[64+389]==0&&bytes[64+390]==0x34&&bytes[64+391]==0x12&&bytes[64+392]==0xfe&&bytes[64+393]==0xff&&bytes[64+400]==1&&bytes[64+401]==0&&bytes[64+402]==0&&bytes[64+403]==0&&bytes[64+507]==1&&bytes[64+508]==0xfd&&bytes[64+509]==0xff&&bytes[64+511]==2);
	/* Every exact Bot byte participates in the behavior/Bot and full digests. */
	CHECK(NativeCanonicalDriversDetailedV1_BuildSummary(&value,&summary));
	for(uint32_t offset=0;offset<NATIVE_CANONICAL_DRIVERS_BOT_BYTES;offset++)
	{
		memcpy(changed,bytes,sizeof(changed));changed[64+388+offset]^=UINT8_C(0x80);
		CHECK(NativeCanonicalDriversV1_FromNormativeStream(&changedSummary,1,changed,sizeof(changed))&&
			changedSummary.slots[0].behaviorBotDigest!=summary.slots[0].behaviorBotDigest&&changedSummary.fullStreamDigest!=summary.fullStreamDigest);
	}
	value.slots[0].bot.reserved5ac=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.reserved5ac=0;
	value.slots[0].bot.reserved5cc=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.reserved5cc=0;
	value.slots[0].bot.reserved628=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.reserved628=0;
	value.slots[0].bot.botNavFrameIndex=32766;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.botNavFrameIndex=UINT16_C(0x1234);
	value.slots[0].meta.threadBehaviorID=2;value.slots[0].meta.kartState=0;value.slots[0].bot.maskObjPresent=0;
	for(uint32_t flags=0;flags<=NATIVE_CANONICAL_DRIVER_BOT_FLAGS_KNOWN_MASK;flags++)
	{
		value.slots[0].bot.botFlags=flags;value.slots[0].bot.aiDamageState=(flags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE)?1:0;
		CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)==((flags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_SUPPRESS)==0||(flags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE)!=0));
	}
	for(uint32_t bit=10;bit<32;bit++){value.slots[0].bot.botFlags=UINT32_C(1)<<bit;value.slots[0].bot.aiDamageState=0;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));}
	for(int16_t state=-1;state<=6;state++)
	{
		int accepted=(state==0||state==1||state==2||state==3||state==5);
		value.slots[0].bot.botFlags=state==0?0:NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE;value.slots[0].bot.aiDamageState=state;
		CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)==accepted);
	}
	value.slots[0].bot.botFlags=0;value.slots[0].bot.aiDamageState=0;
	for(int16_t path=-1;path<=3;path++){value.slots[0].bot.botPath=path;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)==(path>=0&&path<=2));}
	value.slots[0].bot.botPath=2;value.slots[0].bot.botFlags=3;value.slots[0].bot.aiDamageState=2;value.slots[0].bot.maskObjPresent=2;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.maskObjPresent=1;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].meta.threadBehaviorID=3;value.slots[0].meta.kartState=NATIVE_CANONICAL_DRIVER_KART_STATE_MASK_GRABBED;value.slots[0].bot.botFlags=0;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.botFlags=NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].meta.threadBehaviorID=2;value.slots[0].meta.kartState=0;value.slots[0].bot.maskObjPresent=0;value.slots[0].bot.aiDamageState=5;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));value.slots[0].bot.desiredPathBossOnly=3;CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	return 0;
}
static int TestBotAndReferences(void)
{
	struct NativeCanonicalDriversDetailedV1 value;
	ValidHuman(&value);value.slots[0].meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_BOT;value.slots[0].meta.behaviorID=1;value.slots[0].meta.threadBehaviorID=2;value.slots[0].bot.botPath=0;
	value.prelude.playerCount=0;value.prelude.humanPlayerPositions[0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;value.prelude.activeBotCount=1;
	value.slots[0].bot.desiredPathBossOnly=2;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
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
static int TestAllowedTagOverlap(void)
{
	struct NativeCanonicalDriversDetailedV1 value;
	ValidHuman(&value);
	/* behavior 31 = init 1 + steady RevEngine suffix 14. Podium's queued
	 * NONE and the retained RevEngine union are both legacy-valid. */
	value.slots[0].meta.behaviorID=31;value.slots[0].meta.kartState=4;
	value.slots[0].active.unionTag=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].active.unionTag=NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE;
	value.slots[0].active.branchBytes[0]=1;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	return 0;
}
static void ValidMaskGrab(struct NativeCanonicalDriversDetailedV1 *value)
{
	ValidHuman(value);
	value->slots[0].meta.behaviorID=11;
	value->slots[0].meta.kartState=5;
	value->slots[0].active.unionTag=NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB;
}
static int TestMetaFlagContract(void)
{
	struct NativeCanonicalDriversDetailedV1 value;
	struct NativeCanonicalDriversV1 summary,before;
	CHECK(NATIVE_CANONICAL_DRIVER_EXTERNAL_RAIN_CLOUD==UINT16_C(0x0001));
	CHECK(NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT==UINT16_C(0x0002));
	CHECK(NATIVE_CANONICAL_DRIVER_EXTERNAL_KNOWN_MASK==UINT16_C(0x0003));
	CHECK(NATIVE_CANONICAL_DRIVER_THREAD_SIM_COLLISION_DISABLED==UINT16_C(0x0001));
	CHECK(NATIVE_CANONICAL_DRIVER_THREAD_SIM_KNOWN_MASK==UINT16_C(0x0001));
	for(uint8_t bit=0;bit<16;bit++)
	{
		uint16_t flag=(uint16_t)(UINT16_C(1)<<bit);
		if(bit==1)ValidMaskGrab(&value);else ValidHuman(&value);
		value.slots[0].meta.externalPresenceFlags=flag;
		CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)==(bit==0||bit==1));
	}
	for(uint8_t bit=0;bit<16;bit++)
	{
		uint16_t flag=(uint16_t)(UINT16_C(1)<<bit);
		ValidHuman(&value);value.slots[0].meta.driverThreadSimFlags=flag;
		CHECK(NativeCanonicalDriversDetailedV1_Validate(&value)==(bit==0));
	}
	ValidHuman(&value);value.slots[0].meta.externalPresenceFlags=NATIVE_CANONICAL_DRIVER_EXTERNAL_RAIN_CLOUD;
	value.slots[0].meta.driverThreadSimFlags=NATIVE_CANONICAL_DRIVER_THREAD_SIM_COLLISION_DISABLED;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	ValidHuman(&value);value.slots[0].meta.externalPresenceFlags=NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT;
	CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	ValidMaskGrab(&value);value.slots[0].meta.externalPresenceFlags=NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].meta.externalPresenceFlags=NATIVE_CANONICAL_DRIVER_EXTERNAL_KNOWN_MASK;
	value.slots[0].meta.driverThreadSimFlags=NATIVE_CANONICAL_DRIVER_THREAD_SIM_KNOWN_MASK;
	CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_BOT;
	value.slots[0].meta.threadBehaviorID=NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE;
	value.slots[0].active.unionTag=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;
	value.prelude.playerCount=0;value.prelude.humanPlayerPositions[0]=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT;value.prelude.activeBotCount=1;
	value.slots[0].meta.externalPresenceFlags=0;CHECK(NativeCanonicalDriversDetailedV1_Validate(&value));
	value.slots[0].meta.externalPresenceFlags=NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT;
	CHECK(!NativeCanonicalDriversDetailedV1_Validate(&value));
	ValidHuman(&value);NativeCanonicalDriversV1_Init(&summary);summary.fullStreamDigest=UINT64_C(0x1111111111111111);before=summary;
	value.slots[0].meta.externalPresenceFlags=UINT16_C(0x8000);
	CHECK(!NativeCanonicalDriversDetailedV1_BuildSummary(&value,&summary)&&EqualSummary(&summary,&before));
	return 0;
}
static int TestPhysicsContract(void)
{
	struct NativeCanonicalDriverPhysicsV1 value,before;
	memset(&value,0,sizeof(value));value.currQuadIndex=UINT32_MAX;value.underDriverQuadIndex=0;value.lastValidQuadIndex=(uint32_t)INT32_MAX-1;
	CHECK(NativeCanonicalDriverPhysicsV1_Validate(&value));
	value.reserved0=1;CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value.reserved0=0;
	value.terrainMeta1Index=21;CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value.terrainMeta1Index=20;
	value.terrainMeta2Index=21;CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value.terrainMeta2Index=20;
	value.stepFlagSet=UINT32_C(0x0000c0ff);CHECK(NativeCanonicalDriverPhysicsV1_Validate(&value));
	value.stepFlagSet=UINT32_C(0x00010000);CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value.stepFlagSet=0;
	before=value;value.currQuadIndex=(uint32_t)INT32_MAX;CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value=before;
	value.underDriverQuadIndex=(uint32_t)INT32_MAX;CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value=before;
	value.lastValidQuadIndex=(uint32_t)INT32_MAX;CHECK(!NativeCanonicalDriverPhysicsV1_Validate(&value));value=before;
	CHECK(!NativeCanonicalDriverPhysicsV1_Validate(NULL));
	return 0;
}
int main(void)
{
	if(TestGoldenAndSummary()!=0||TestSemanticMutations()!=0||TestRejectionAndTransaction()!=0||TestPendingDamageTail()!=0||TestTypedBotLayout()!=0||TestBotAndReferences()!=0||TestRanksAndActiveTags()!=0||TestAllowedTagOverlap()!=0||TestMetaFlagContract()!=0||TestPhysicsContract()!=0)return 1;
	puts("native_canonical_drivers_detailed_test: passed");return 0;
}
