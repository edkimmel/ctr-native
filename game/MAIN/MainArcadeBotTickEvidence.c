#include "MAIN/MainArcadeBotTickEvidence.h"

#include <string.h>

static int Zero(const uint8_t *p,size_t n){uint8_t x=0;for(size_t i=0;i<n;i++)x|=p[i];return x==0;}
static int DigestBytes(const uint8_t *p,size_t n,uint8_t out[32]){struct NativeSha256 s;if(!p||!out)return 0;NativeSha256_Init(&s);NativeSha256_Update(&s,p,n);NativeSha256_Final(&s,out);return 1;}
static int AssignmentZero(const struct MainArcadeBotSetupAssignment *a)
{
	return a!=NULL&&a->enabled==0&&a->stableSlot==0&&a->nativeDriverSlot==0&&a->characterID==0&&
		a->difficulty==0&&a->spawnOrder==0&&a->navPathIndex==0&&a->accelerationOrder==0&&
		a->setupSequence==0&&Zero(a->reserved,sizeof(a->reserved))&&a->setupRandom==0;
}
/* A plan is a portable value, but the evidence boundary still rechecks every
 * ownership and receipt relation.  Do not treat a matching plan digest as a
 * substitute for these semantic checks. */
static int PlanMatchesConfigAndRng(const struct MainArcadeBotSetupPlan *plan,
	const struct NativeMatchConfigV1 *config,const struct NativeDeterministicRngBankV1 *before,
	const struct NativeDeterministicRngBankV1 *after,uint8_t configDigest[32])
{
	uint8_t beforeDigest[32],afterDigest[32],expectedConfig[32]; uint32_t expectedMask=0; uint8_t expectedCount=0,sequenceSeen=0;
	if(!plan||!config||!before||!after||!configDigest||!NativeMatchConfigV1_Digest(config,expectedConfig)||
		!NativeDeterministicRngBankV1_Digest(before,beforeDigest)||!NativeDeterministicRngBankV1_Digest(after,afterDigest)||
		plan->locked!=1||plan->profile!=config->profile||!Zero(plan->reserved,sizeof(plan->reserved))||
		memcmp(plan->matchConfigDigest,expectedConfig,32)!=0||memcmp(plan->rngBeforeDigest,beforeDigest,32)!=0||
		memcmp(plan->rngAfterDigest,afterDigest,32)!=0)return 0;
	for(uint8_t slot=0;slot<8;slot++){
		const struct MainArcadeBotSetupAssignment *a=&plan->assignments[slot];
		if(config->slots[slot].role!=NATIVE_MATCH_SLOT_ROLE_BOT){if(!AssignmentZero(a))return 0;continue;}
		expectedMask|=UINT32_C(1)<<slot; expectedCount++;
		if(a->enabled!=1||a->stableSlot!=slot||a->nativeDriverSlot!=slot||a->characterID!=config->slots[slot].characterID||
			a->difficulty!=config->slots[slot].difficulty||a->spawnOrder>=8||a->navPathIndex>=MAIN_ARCADE_BOT_SETUP_NAV_PATH_COUNT||
			a->accelerationOrder>=8||a->setupSequence>=8||!Zero(a->reserved,sizeof(a->reserved))||
			(sequenceSeen&(UINT8_C(1)<<a->setupSequence))!=0)return 0;
		sequenceSeen|=UINT8_C(1)<<a->setupSequence;
	}
	if(plan->botMask!=expectedMask||plan->botCount!=expectedCount||sequenceSeen!=(uint8_t)((UINT32_C(1)<<expectedCount)-1))return 0;
	memcpy(configDigest,expectedConfig,32); return 1;
}
static int SlotBytes(const struct NativeCanonicalDriverSlotV1 *slot,uint8_t stableSlot,uint8_t out[520])
{
	struct NativeCanonicalDriversDetailedV1 v; struct NativeCodecWriter w; uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];
	if(!slot||stableSlot>=8||!out)return 0; NativeCanonicalDriversDetailedV1_Init(&v);
	v.prelude.presenceMask=UINT32_C(1)<<stableSlot;v.prelude.activeBotCount=1;v.slots[stableSlot]=*slot;
	if(!NativeCanonicalDriversDetailedV1_Validate(&v))return 0;
	NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);
	if(!NativeCanonicalDriversDetailedV1_Encode(&w,&v)||w.offset!=sizeof(bytes))return 0;
	memcpy(out,bytes+NATIVE_CANONICAL_DRIVERS_PRELUDE_BYTES+(size_t)stableSlot*NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES,520);return 1;
}
static int BankOtherStreamsEqual(const struct NativeDeterministicRngBankV1 *a,const struct NativeDeterministicRngBankV1 *b,uint8_t slot)
{
	for(uint32_t i=0;i<NATIVE_DETERMINISTIC_RNG_STREAM_COUNT;i++){const struct NativeDeterministicRngStreamV1 *x=&a->streams[i],*y=&b->streams[i];if(x->tag==NATIVE_DETERMINISTIC_RNG_STREAM_BOT&&x->stableSlot==slot)continue;if(x->tag!=y->tag||x->stableSlot!=y->stableSlot||x->streamIndex!=y->streamIndex||x->drawCount!=y->drawCount||memcmp(x->state,y->state,sizeof(x->state))!=0)return 0;}return 1;
}
static const struct NativeDeterministicRngStreamV1 *BotStream(const struct NativeDeterministicRngBankV1 *b,uint8_t slot){for(uint32_t i=0;i<NATIVE_DETERMINISTIC_RNG_STREAM_COUNT;i++)if(b->streams[i].tag==NATIVE_DETERMINISTIC_RNG_STREAM_BOT&&b->streams[i].stableSlot==slot)return &b->streams[i];return NULL;}
static int Write(struct NativeCodecWriter *w,const struct MainArcadeBotTickEvidenceV1 *v){return NativeCodecWriter_WriteU32(w,MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_MAGIC)&&NativeCodecWriter_WriteU32(w,MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_VERSION)&&NativeCodecWriter_WriteU32(w,v->frameNumber)&&NativeCodecWriter_WriteU8(w,v->stableSlot)&&NativeCodecWriter_WriteBytes(w,v->reserved,3)&&NativeCodecWriter_WriteBytes(w,v->configDigest,32)&&NativeCodecWriter_WriteBytes(w,v->botRulesDigest,32)&&NativeCodecWriter_WriteBytes(w,v->setupPlanDigest,32)&&NativeCodecWriter_WriteBytes(w,v->beforeSlotFacts,520)&&NativeCodecWriter_WriteBytes(w,v->afterSlotFacts,520)&&NativeCodecWriter_WriteBytes(w,v->beforeSlotDigest,32)&&NativeCodecWriter_WriteBytes(w,v->afterSlotDigest,32)&&NativeCodecWriter_WriteBytes(w,v->rngBeforeDigest,32)&&NativeCodecWriter_WriteBytes(w,v->rngAfterDigest,32)&&NativeCodecWriter_WriteU64(w,v->botDrawCountBefore)&&NativeCodecWriter_WriteU64(w,v->botDrawCountAfter)&&NativeCodecWriter_WriteU8(w,v->threadBehaviorIDBefore)&&NativeCodecWriter_WriteU8(w,v->threadBehaviorIDAfter)&&NativeCodecWriter_WriteBytes(w,v->tailReserved,2);}
int MainArcadeBotTickEvidenceV1_Build(struct MainArcadeBotTickEvidenceV1 *out,uint32_t frame,uint8_t slot,const struct NativeMatchConfigV1 *config,const struct MainArcadeBotSetupPlan *plan,const struct NativeCanonicalDriverSlotV1 *before,const struct NativeCanonicalDriverSlotV1 *after,const struct NativeDeterministicRngBankV1 *rngBefore,const struct NativeDeterministicRngBankV1 *rngAfter)
{
	struct MainArcadeBotTickEvidenceV1 c;const struct NativeDeterministicRngStreamV1 *a,*b;
	if(!out||!config||!plan||!before||!after||!rngBefore||!rngAfter||slot>=8||!NativeMatchConfigV1_Validate(config)||config->slots[slot].role!=NATIVE_MATCH_SLOT_ROLE_BOT||before->meta.present!=1||after->meta.present!=1||before->meta.slotIndex!=slot||after->meta.slotIndex!=slot||before->meta.driverKind!=NATIVE_CANONICAL_DRIVER_KIND_BOT||after->meta.driverKind!=NATIVE_CANONICAL_DRIVER_KIND_BOT||!NativeDeterministicRngBankV1_Validate(rngBefore)||!NativeDeterministicRngBankV1_Validate(rngAfter)||rngBefore->masterSeed!=config->masterSeed||rngAfter->masterSeed!=config->masterSeed||rngBefore->derivationVersion!=config->rngDerivationVersion||rngAfter->derivationVersion!=config->rngDerivationVersion||!BankOtherStreamsEqual(rngBefore,rngAfter,slot))return 0;
	memset(&c,0,sizeof(c));
	if(!PlanMatchesConfigAndRng(plan,config,rngBefore,rngAfter,c.configDigest))return 0;
	a=BotStream(rngBefore,slot);b=BotStream(rngAfter,slot);if(!a||!b||b->drawCount<a->drawCount)return 0;c.frameNumber=frame;c.stableSlot=slot;if(!MainArcadeBotSetupPlan_Digest(plan,c.setupPlanDigest)||!SlotBytes(before,slot,c.beforeSlotFacts)||!SlotBytes(after,slot,c.afterSlotFacts)||!DigestBytes(c.beforeSlotFacts,520,c.beforeSlotDigest)||!DigestBytes(c.afterSlotFacts,520,c.afterSlotDigest)||!NativeDeterministicRngBankV1_Digest(rngBefore,c.rngBeforeDigest)||!NativeDeterministicRngBankV1_Digest(rngAfter,c.rngAfterDigest))return 0;
	/* This is an independently checked config field, while configDigest above
	 * binds the entire canonical MatchConfig (including botRulesDigest). */
	memcpy(c.botRulesDigest,config->botRulesDigest,32);if(Zero(c.botRulesDigest,sizeof(c.botRulesDigest)))return 0;c.botDrawCountBefore=a->drawCount;c.botDrawCountAfter=b->drawCount;c.threadBehaviorIDBefore=before->meta.threadBehaviorID;c.threadBehaviorIDAfter=after->meta.threadBehaviorID;*out=c;return 1;
}
int MainArcadeBotTickEvidenceV1_Validate(const struct MainArcadeBotTickEvidenceV1 *v){uint8_t d[32];return v&&v->stableSlot<8&&!Zero(v->configDigest,32)&&!Zero(v->botRulesDigest,32)&&!Zero(v->setupPlanDigest,32)&&!Zero(v->rngBeforeDigest,32)&&!Zero(v->rngAfterDigest,32)&&Zero(v->reserved,3)&&Zero(v->tailReserved,2)&&v->botDrawCountAfter>=v->botDrawCountBefore&&DigestBytes(v->beforeSlotFacts,520,d)&&memcmp(d,v->beforeSlotDigest,32)==0&&DigestBytes(v->afterSlotFacts,520,d)&&memcmp(d,v->afterSlotDigest,32)==0;}
int MainArcadeBotTickEvidenceV1_Encode(struct NativeCodecWriter *w,const struct MainArcadeBotTickEvidenceV1 *v){struct NativeCodecWriter c;if(!w||!MainArcadeBotTickEvidenceV1_Validate(v)||w->failed||w->offset>w->capacity||MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_ENCODED_BYTES>w->capacity-w->offset)return 0;c=*w;if(!Write(&c,v))return 0;*w=c;return 1;}
int MainArcadeBotTickEvidenceV1_Decode(struct NativeCodecReader *r,struct MainArcadeBotTickEvidenceV1 *out){struct MainArcadeBotTickEvidenceV1 c;struct NativeCodecReader q;uint32_t m,v;if(!r||!out||r->failed||NativeCodecReader_Remaining(r)!=MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_ENCODED_BYTES)return 0;q=*r;if(!NativeCodecReader_ReadU32(&q,&m)||!NativeCodecReader_ReadU32(&q,&v)||m!=MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_MAGIC||v!=1||!NativeCodecReader_ReadU32(&q,&c.frameNumber)||!NativeCodecReader_ReadU8(&q,&c.stableSlot)||!NativeCodecReader_ReadBytes(&q,c.reserved,3)||!NativeCodecReader_ReadBytes(&q,c.configDigest,32)||!NativeCodecReader_ReadBytes(&q,c.botRulesDigest,32)||!NativeCodecReader_ReadBytes(&q,c.setupPlanDigest,32)||!NativeCodecReader_ReadBytes(&q,c.beforeSlotFacts,520)||!NativeCodecReader_ReadBytes(&q,c.afterSlotFacts,520)||!NativeCodecReader_ReadBytes(&q,c.beforeSlotDigest,32)||!NativeCodecReader_ReadBytes(&q,c.afterSlotDigest,32)||!NativeCodecReader_ReadBytes(&q,c.rngBeforeDigest,32)||!NativeCodecReader_ReadBytes(&q,c.rngAfterDigest,32)||!NativeCodecReader_ReadU64(&q,&c.botDrawCountBefore)||!NativeCodecReader_ReadU64(&q,&c.botDrawCountAfter)||!NativeCodecReader_ReadU8(&q,&c.threadBehaviorIDBefore)||!NativeCodecReader_ReadU8(&q,&c.threadBehaviorIDAfter)||!NativeCodecReader_ReadBytes(&q,c.tailReserved,2)||!MainArcadeBotTickEvidenceV1_Validate(&c))return 0;*out=c;*r=q;return 1;}
int MainArcadeBotTickEvidenceV1_Digest(const struct MainArcadeBotTickEvidenceV1 *v,uint8_t d[32]){uint8_t b[MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_ENCODED_BYTES];struct NativeCodecWriter w;if(!d)return 0;NativeCodecWriter_Init(&w,b,sizeof(b),NULL);return MainArcadeBotTickEvidenceV1_Encode(&w,v)&&DigestBytes(b,sizeof(b),d);}
