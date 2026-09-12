#include "platform/native_canonical_state_v3.h"

#include <string.h>

#define V3_CONTROL_BYTES 48u
#define V3_RNG_BYTES 20u
#define V3_INPUT_BYTES 40u
#define V3_HEADER_BYTES 84u
#define V3_DOMAIN_BYTES 16u
#define V3_STATE_BYTES (V3_HEADER_BYTES + (V3_CONTROL_BYTES + V3_DOMAIN_BYTES) + (V3_RNG_BYTES + V3_DOMAIN_BYTES) + \
	(V3_INPUT_BYTES + V3_DOMAIN_BYTES) + (NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES + V3_DOMAIN_BYTES) + (2u * V3_DOMAIN_BYTES) + 8u)

static size_t V3PayloadSize(uint32_t id)
{
	switch (id) { case NATIVE_CANONICAL_DOMAIN_CONTROL: return V3_CONTROL_BYTES; case NATIVE_CANONICAL_DOMAIN_RNG: return V3_RNG_BYTES;
	case NATIVE_CANONICAL_DOMAIN_INPUT: return V3_INPUT_BYTES; case NATIVE_CANONICAL_DOMAIN_DRIVERS: return NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES;
	case NATIVE_CANONICAL_DOMAIN_WORLD: case NATIVE_CANONICAL_DOMAIN_TOPOLOGY: return 0; default: return SIZE_MAX; }
}
static int V3ControlW(struct NativeCodecWriter *w, const struct NativeCanonicalControlV1 *v)
{
	return NativeCodecWriter_WriteS32(w,v->frameTimer)&&NativeCodecWriter_WriteS32(w,v->frameCounter)&&NativeCodecWriter_WriteS32(w,v->timer)&&
	NativeCodecWriter_WriteS32(w,v->framesInThisLEV)&&NativeCodecWriter_WriteS32(w,v->elapsedTimeMS)&&NativeCodecWriter_WriteS32(w,v->msInThisLEV)&&
	NativeCodecWriter_WriteS32(w,v->elapsedEventTime)&&NativeCodecWriter_WriteS32(w,v->mainGameState)&&NativeCodecWriter_WriteS32(w,v->loadingStage)&&
	NativeCodecWriter_WriteS32(w,v->levelID)&&NativeCodecWriter_WriteS32(w,v->gameMode1)&&NativeCodecWriter_WriteS32(w,v->gameMode2);
}
static int V3ControlR(struct NativeCodecReader *r, struct NativeCanonicalControlV1 *v)
{
	return NativeCodecReader_ReadS32(r,&v->frameTimer)&&NativeCodecReader_ReadS32(r,&v->frameCounter)&&NativeCodecReader_ReadS32(r,&v->timer)&&
	NativeCodecReader_ReadS32(r,&v->framesInThisLEV)&&NativeCodecReader_ReadS32(r,&v->elapsedTimeMS)&&NativeCodecReader_ReadS32(r,&v->msInThisLEV)&&
	NativeCodecReader_ReadS32(r,&v->elapsedEventTime)&&NativeCodecReader_ReadS32(r,&v->mainGameState)&&NativeCodecReader_ReadS32(r,&v->loadingStage)&&
	NativeCodecReader_ReadS32(r,&v->levelID)&&NativeCodecReader_ReadS32(r,&v->gameMode1)&&NativeCodecReader_ReadS32(r,&v->gameMode2);
}
static int V3RngW(struct NativeCodecWriter *w, const struct NativeCanonicalRngV1 *v)
{
	return NativeCodecWriter_WriteU32(w,v->mixRandomNumber)&&NativeCodecWriter_WriteU32(w,v->deadcoed0)&&NativeCodecWriter_WriteU32(w,v->deadcoed1)&&
	NativeCodecWriter_WriteU32(w,v->advRng0)&&NativeCodecWriter_WriteU32(w,v->advRng1);
}
static int V3RngR(struct NativeCodecReader *r, struct NativeCanonicalRngV1 *v)
{
	return NativeCodecReader_ReadU32(r,&v->mixRandomNumber)&&NativeCodecReader_ReadU32(r,&v->deadcoed0)&&NativeCodecReader_ReadU32(r,&v->deadcoed1)&&
	NativeCodecReader_ReadU32(r,&v->advRng0)&&NativeCodecReader_ReadU32(r,&v->advRng1);
}
static int V3InputW(struct NativeCodecWriter *w, const struct NativeCanonicalInputV1 *v)
{
	if (!NativeCodecWriter_WriteU32(w,v->padCount)) return 0;
	for (uint32_t i=0;i<NATIVE_CANONICAL_INPUT_PAD_COUNT;i++) if (!NativeCodecWriter_WriteU8(w,v->pads[i].status)||!NativeCodecWriter_WriteU8(w,v->pads[i].id)||
		!NativeCodecWriter_WriteBytes(w,v->pads[i].buttons,2)||!NativeCodecWriter_WriteBytes(w,v->pads[i].analog,4)||!NativeCodecWriter_WriteU8(w,v->pads[i].connected)) return 0;
	return 1;
}
static int V3InputR(struct NativeCodecReader *r, struct NativeCanonicalInputV1 *v)
{
	if (!NativeCodecReader_ReadU32(r,&v->padCount)||(v->padCount!=NATIVE_CANONICAL_INPUT_PAD_COUNT)) return 0;
	for (uint32_t i=0;i<NATIVE_CANONICAL_INPUT_PAD_COUNT;i++) if (!NativeCodecReader_ReadU8(r,&v->pads[i].status)||!NativeCodecReader_ReadU8(r,&v->pads[i].id)||
		!NativeCodecReader_ReadBytes(r,v->pads[i].buttons,2)||!NativeCodecReader_ReadBytes(r,v->pads[i].analog,4)||!NativeCodecReader_ReadU8(r,&v->pads[i].connected)) return 0;
	return 1;
}
static int V3Payload(const struct NativeCanonicalStateV3 *s,uint32_t id,uint8_t *b,size_t n)
{
	struct NativeCodecWriter w; if ((s==NULL)||(n!=V3PayloadSize(id))) return 0; NativeCodecWriter_Init(&w,b,n,NULL);
	switch(id){case NATIVE_CANONICAL_DOMAIN_CONTROL: if(!V3ControlW(&w,&s->control))return 0;break;case NATIVE_CANONICAL_DOMAIN_RNG:if(!V3RngW(&w,&s->rng))return 0;break;
	case NATIVE_CANONICAL_DOMAIN_INPUT:if(!V3InputW(&w,&s->input))return 0;break;case NATIVE_CANONICAL_DOMAIN_DRIVERS:if(!NativeCanonicalDriversV1_Encode(&w,&s->drivers))return 0;break;
	case NATIVE_CANONICAL_DOMAIN_WORLD:case NATIVE_CANONICAL_DOMAIN_TOPOLOGY:break;default:return 0;} return NativeCodecWriter_Ok(&w)&&(NativeCodecWriter_Size(&w)==n);
}
static uint64_t V3Digest(const uint8_t *b,size_t n){struct NativeCodecDigest64 d;NativeCodecDigest64_Init(&d);NativeCodecDigest64_Update(&d,b,n);return d.value;}
static uint64_t V3Combined(const uint64_t *d)
{
	uint8_t b[NATIVE_CANONICAL_DOMAIN_COUNT*12u];struct NativeCodecWriter w;struct NativeCodecDigest64 h;NativeCodecWriter_Init(&w,b,sizeof(b),NULL);
	for(uint32_t i=0;i<NATIVE_CANONICAL_DOMAIN_COUNT;i++){(void)NativeCodecWriter_WriteU32(&w,NativeCanonicalDomainOrder[i]);(void)NativeCodecWriter_WriteU64(&w,d[i]);}
	NativeCodecDigest64_Init(&h);NativeCodecDigest64_Update(&h,b,sizeof(b));return h.value;
}
static int V3Identity(const struct NativeIdentityV1 *a,const struct NativeIdentityV1 *b){return memcmp(a->build,b->build,NATIVE_IDENTITY_DIGEST_BYTES)==0&&memcmp(a->content,b->content,NATIVE_IDENTITY_DIGEST_BYTES)==0;}

void NativeCanonicalStateV3_Init(struct NativeCanonicalStateV3 *s)
{
	if(s==NULL)return;memset(s,0,sizeof(*s));s->schemaVersion=NATIVE_CANONICAL_STATE_V3_SCHEMA_VERSION;s->replayFormatVersion=NATIVE_CANONICAL_REPLAY_V3_FORMAT_VERSION;
	s->domainCount=NATIVE_CANONICAL_DOMAIN_COUNT;s->input.padCount=NATIVE_CANONICAL_INPUT_PAD_COUNT;NativeCanonicalDriversV1_Init(&s->drivers);(void)NativeCanonicalStateV3_ComputeDigests(s);
}
int NativeCanonicalStateV3_Validate(const struct NativeCanonicalStateV3 *s)
{
	return s!=NULL&&s->schemaVersion==NATIVE_CANONICAL_STATE_V3_SCHEMA_VERSION&&s->replayFormatVersion==NATIVE_CANONICAL_REPLAY_V3_FORMAT_VERSION&&
		s->domainCount==NATIVE_CANONICAL_DOMAIN_COUNT&&s->input.padCount==NATIVE_CANONICAL_INPUT_PAD_COUNT&&NativeCanonicalDriversV1_Validate(&s->drivers);
}
int NativeCanonicalStateV3_ComputeDigests(struct NativeCanonicalStateV3 *s)
{
	struct NativeCanonicalStateV3 c;uint8_t b[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];if(!NativeCanonicalStateV3_Validate(s))return 0;c=*s;
	for(uint32_t i=0;i<NATIVE_CANONICAL_DOMAIN_COUNT;i++){size_t n=V3PayloadSize(NativeCanonicalDomainOrder[i]);if(n==SIZE_MAX||!V3Payload(s,NativeCanonicalDomainOrder[i],b,n))return 0;c.domainDigests[i]=V3Digest(b,n);}c.combinedDigest=V3Combined(c.domainDigests);*s=c;return 1;
}
int NativeCanonicalStateV3_ComputeDigestsInPlaceWithScratch(struct NativeCanonicalStateV3 *s,uint8_t *b,size_t scratchSize)
{
	if(!NativeCanonicalStateV3_Validate(s)||!b||scratchSize<NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES)return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_DOMAIN_COUNT;i++)
	{
		size_t n=V3PayloadSize(NativeCanonicalDomainOrder[i]);
		if(n==SIZE_MAX||!V3Payload(s,NativeCanonicalDomainOrder[i],b,n))return 0;
		s->domainDigests[i]=V3Digest(b,n);
	}
	s->combinedDigest=V3Combined(s->domainDigests);
	return 1;
}
size_t NativeCanonicalStateV3_EncodedSize(void){return V3_STATE_BYTES;}
int NativeCanonicalStateV3_Encode(struct NativeCodecWriter *w,const struct NativeCanonicalStateV3 *s)
{
	struct NativeCanonicalStateV3 check;struct NativeCodecWriter e;uint8_t b[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];
	if(w==NULL||!NativeCanonicalStateV3_Validate(s)||w->failed||w->offset>w->capacity||NativeCanonicalStateV3_EncodedSize()>w->capacity-w->offset)return 0;check=*s;if(!NativeCanonicalStateV3_ComputeDigests(&check)||memcmp(check.domainDigests,s->domainDigests,sizeof(check.domainDigests))||check.combinedDigest!=s->combinedDigest)return 0;
	e=*w;if(!NativeCodecWriter_WriteU32(&e,NATIVE_CANONICAL_STATE_V3_MAGIC)||!NativeCodecWriter_WriteU32(&e,s->schemaVersion)||!NativeCodecWriter_WriteU32(&e,s->replayFormatVersion)||!NativeCodecWriter_WriteU32(&e,s->domainCount)||!NativeCodecWriter_WriteU32(&e,s->frameNumber)||!NativeCodecWriter_WriteBytes(&e,s->identity.build,NATIVE_IDENTITY_DIGEST_BYTES)||!NativeCodecWriter_WriteBytes(&e,s->identity.content,NATIVE_IDENTITY_DIGEST_BYTES))return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_DOMAIN_COUNT;i++){uint32_t id=NativeCanonicalDomainOrder[i];size_t n=V3PayloadSize(id);if(!V3Payload(s,id,b,n)||!NativeCodecWriter_WriteU32(&e,id)||!NativeCodecWriter_WriteU32(&e,(uint32_t)n)||!NativeCodecWriter_WriteBytes(&e,b,n)||!NativeCodecWriter_WriteU64(&e,s->domainDigests[i]))return 0;}if(!NativeCodecWriter_WriteU64(&e,s->combinedDigest))return 0;*w=e;return 1;
}
int NativeCanonicalStateV3_Decode(struct NativeCodecReader *r,const struct NativeIdentityV1 *id,struct NativeCanonicalStateV3 *s)
{
	struct NativeCodecReader e;struct NativeCanonicalStateV3 d,c;uint8_t b[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];uint32_t magic;
	if(r==NULL||id==NULL||s==NULL)return 0;e=*r;NativeCanonicalStateV3_Init(&d);if(!NativeCodecReader_ReadU32(&e,&magic)||magic!=NATIVE_CANONICAL_STATE_V3_MAGIC||!NativeCodecReader_ReadU32(&e,&d.schemaVersion)||!NativeCodecReader_ReadU32(&e,&d.replayFormatVersion)||!NativeCodecReader_ReadU32(&e,&d.domainCount)||!NativeCodecReader_ReadU32(&e,&d.frameNumber)||!NativeCodecReader_ReadBytes(&e,d.identity.build,NATIVE_IDENTITY_DIGEST_BYTES)||!NativeCodecReader_ReadBytes(&e,d.identity.content,NATIVE_IDENTITY_DIGEST_BYTES)||!V3Identity(&d.identity,id)||!NativeCanonicalStateV3_Validate(&d))return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_DOMAIN_COUNT;i++){uint32_t got,n,idv=NativeCanonicalDomainOrder[i];size_t want=V3PayloadSize(idv);if(!NativeCodecReader_ReadU32(&e,&got)||!NativeCodecReader_ReadU32(&e,&n)||got!=idv||n!=want)return 0;switch(idv){case NATIVE_CANONICAL_DOMAIN_CONTROL:if(!V3ControlR(&e,&d.control))return 0;break;case NATIVE_CANONICAL_DOMAIN_RNG:if(!V3RngR(&e,&d.rng))return 0;break;case NATIVE_CANONICAL_DOMAIN_INPUT:if(!V3InputR(&e,&d.input))return 0;break;case NATIVE_CANONICAL_DOMAIN_DRIVERS:if(!NativeCanonicalDriversV1_Decode(&e,&d.drivers))return 0;break;default:break;}if(!V3Payload(&d,idv,b,want)||!NativeCodecReader_ReadU64(&e,&d.domainDigests[i])||d.domainDigests[i]!=V3Digest(b,want))return 0;}if(!NativeCodecReader_ReadU64(&e,&d.combinedDigest))return 0;c=d;if(!NativeCanonicalStateV3_ComputeDigests(&c)||memcmp(c.domainDigests,d.domainDigests,sizeof(c.domainDigests))||c.combinedDigest!=d.combinedDigest)return 0;*r=e;*s=d;return 1;
}
