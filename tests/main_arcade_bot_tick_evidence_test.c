#include "MAIN/MainArcadeBotTickEvidence.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"fail %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int Make(struct MainArcadeBotTickEvidenceV1 *e)
{
	struct NativeMatchConfigV1 c;struct MainArcadeBotSetupPlan p={0};struct NativeCanonicalDriverSlotV1 a={0},b;struct NativeDeterministicRngBankV1 r,s;uint64_t x;
	NativeMatchConfigV1_InitArcadeTwoCab(&c); c.trackID=7;c.gameMode1=8;c.gameMode2=9;c.rules=10;c.lapCount=3;c.tickRateNumerator=30;c.tickRateDenominator=1;c.masterSeed=9; for(uint8_t i=0;i<8;i++)if(c.slots[i].role!=NATIVE_MATCH_SLOT_ROLE_INACTIVE){c.slots[i].characterID=(uint8_t)(i+1);c.slots[i].difficulty=(uint8_t)(20+i);} memset(c.buildIdentity,1,32);memset(c.contentIdentity,2,32);memset(c.botRulesDigest,3,32);
	CHECK(NativeMatchConfigV1_Validate(&c));CHECK(NativeMatchConfigV1_Digest(&c,p.matchConfigDigest));p.profile=c.profile;p.botCount=6;p.locked=1;CHECK(NativeDeterministicRngBankV1_Init(&r,c.masterSeed,c.rngDerivationVersion));CHECK(NativeDeterministicRngBankV1_Digest(&r,p.rngBeforeDigest));s=r;CHECK(NativeDeterministicRngBankV1_NextU64(&s,NATIVE_DETERMINISTIC_RNG_STREAM_BOT,2,2,&x));CHECK(NativeDeterministicRngBankV1_Digest(&s,p.rngAfterDigest));
	a.meta.present=1;a.meta.slotIndex=2;a.meta.driverKind=NATIVE_CANONICAL_DRIVER_KIND_BOT;a.meta.behaviorID=1;a.meta.threadBehaviorID=NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE;a.bot.botPath=0;b=a;b.bot.botNavFrameIndex=1;
	return MainArcadeBotTickEvidenceV1_Build(e,77,2,&c,&p,&a,&b,&r,&s)?0:1;
}
int main(void){struct MainArcadeBotTickEvidenceV1 a,b,old;uint8_t bytes[MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_ENCODED_BYTES],digest[32];struct NativeCodecWriter w;struct NativeCodecReader r;CHECK(Make(&a)==0);CHECK(MainArcadeBotTickEvidenceV1_Validate(&a));NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);CHECK(MainArcadeBotTickEvidenceV1_Encode(&w,&a)&&w.offset==sizeof(bytes));NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(MainArcadeBotTickEvidenceV1_Decode(&r,&b)&&memcmp(&a,&b,sizeof(a))==0);CHECK(MainArcadeBotTickEvidenceV1_Digest(&a,digest));old=b;bytes[200]^=1;NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!MainArcadeBotTickEvidenceV1_Decode(&r,&b)&&memcmp(&b,&old,sizeof(b))==0&&r.offset==0);return 0;}
