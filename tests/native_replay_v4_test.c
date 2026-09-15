#include "platform/native_replay_v4.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)

static int Fill(struct NativeReplayV4Header *h, struct NativeReplayV4Frame *f)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]={0};
	NativeReplayV4Header_Init(h); h->flags=NATIVE_REPLAY_V4_HEADER_FLAG_FINALIZED; h->frameCount=1;
	for(uint32_t i=0;i<32;i++) { h->identity.build[i]=(uint8_t)i; h->identity.content[i]=(uint8_t)(0x80+i); }
	NativeMatchConfigV1_InitArcadeTwoCab(&h->config); h->config.trackID=1; h->config.gameMode1=2; h->config.gameMode2=3; h->config.rules=4; h->config.lapCount=3; h->config.tickRateNumerator=30; h->config.tickRateDenominator=1; h->config.masterSeed=5;
	memcpy(h->config.buildIdentity,h->identity.build,32); memcpy(h->config.contentIdentity,h->identity.content,32); for(uint32_t i=0;i<6;i++){h->config.slots[i].characterID=(uint8_t)i;h->config.slots[i].difficulty=2;} for(uint32_t i=0;i<32;i++)h->config.botRulesDigest[i]=(uint8_t)(0x20+i);
	memset(f,0,sizeof(*f)); f->replayFrame=7; f->begin.frameCounter=-2; f->end.elapsedTimeMS=32; f->padCount=4; f->pads[0].connected=1; f->vsyncPacketCount=1; f->vsyncPackets[0]=2; f->vsyncTotal=2;
	NativeCanonicalStateV4_Init(&f->canonical); f->canonical.identity=h->identity; f->canonical.frameNumber=f->replayFrame;
	CHECK(NativeMatchConfigV1_Digest(&h->config,f->canonical.configDigest)); stream[64]=1;
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&f->canonical.drivers,1,stream,sizeof(stream)));
	return NativeCanonicalStateV4_ComputeDigests(&f->canonical);
}
static int RejectH(const uint8_t *b,size_t n,const struct NativeIdentityV1 *id,const struct NativeReplayV4Header *before)
{ struct NativeCodecReader r; struct NativeReplayV4Header out=*before; NativeCodecReader_Init(&r,b,n); return !NativeReplayV4Header_Decode(&r,id,&out)&&r.offset==0&&!memcmp(&out,before,sizeof(out)); }
static int RejectF(const uint8_t *b,size_t n,const struct NativeReplayV4Header *h,const struct NativeReplayV4Frame *before)
{ struct NativeCodecReader r; struct NativeReplayV4Frame out=*before; NativeCodecReader_Init(&r,b,n); return !NativeReplayV4Frame_Decode(&r,h,&h->identity,&out)&&r.offset==0&&!memcmp(&out,before,sizeof(out)); }
int main(void)
{
	uint8_t bytes[NATIVE_REPLAY_V4_HEADER_BYTES+NATIVE_REPLAY_V4_FRAME_BYTES], copy[sizeof(bytes)]; struct NativeReplayV4Header h,d,hbefore; struct NativeReplayV4Frame f,df,fbefore; struct NativeCodecWriter w; struct NativeCodecReader r;
	CHECK(Fill(&h,&f)); CHECK(NativeReplayV4Header_EncodedSize()==428u && NativeReplayV4Frame_EncodedSize()==1752u && NativeCanonicalStateV4_EncodedSize()==1432u);
	NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL); CHECK(NativeReplayV4Header_Encode(&w,&h)); CHECK(NativeReplayV4Frame_Encode(&w,&h,&f)); CHECK(w.offset==sizeof(bytes));
	/* Exact golden envelope bytes and the independent encode/decode/encode fixture. */
	CHECK(!memcmp(bytes,"CRV4",4) && bytes[8]==0xac && bytes[9]==1 && !memcmp(bytes+428,"CRF4",4) && bytes[428+4]==0xd8 && bytes[428+5]==6);
	memcpy(copy,bytes,sizeof(copy)); NativeCodecReader_Init(&r,copy,sizeof(copy)); CHECK(NativeReplayV4Header_Decode(&r,&h.identity,&d)); CHECK(NativeReplayV4Frame_Decode(&r,&d,&h.identity,&df)); CHECK(r.offset==sizeof(copy)); NativeCodecWriter_Init(&w,copy,sizeof(copy),NULL); CHECK(NativeReplayV4Header_Encode(&w,&d)&&NativeReplayV4Frame_Encode(&w,&d,&df)&&!memcmp(bytes,copy,sizeof(bytes)));
	/* CRV3/CRF3 are never a conversion or fallback path. */
	memcpy(copy,bytes,sizeof(copy)); copy[3]='3'; memset(&hbefore,0xa5,sizeof(hbefore)); CHECK(RejectH(copy,428,&h.identity,&hbefore));
	memcpy(copy,bytes,sizeof(copy)); copy[428+3]='3'; memset(&fbefore,0xa5,sizeof(fbefore)); CHECK(RejectF(copy+428,1752,&h,&fbefore));
	/* Config bytes and their stored SHA-256 are both mandatory and atomic. */
	memcpy(copy,bytes,sizeof(copy)); copy[140+12]^=1; memset(&hbefore,0xa5,sizeof(hbefore)); CHECK(RejectH(copy,428,&h.identity,&hbefore));
	memcpy(copy,bytes,sizeof(copy)); copy[396]^=1; memset(&hbefore,0xa5,sizeof(hbefore)); CHECK(RejectH(copy,428,&h.identity,&hbefore));
	for(size_t n=0;n<428;n++) { memset(&hbefore,0xa5,sizeof(hbefore)); CHECK(RejectH(bytes,n,&h.identity,&hbefore)); }
	/* Nested NCV4 corruption, config-digest mismatch, trailing input, and truncation stay transactional. */
	memcpy(copy,bytes,sizeof(copy)); copy[428+320+116]^=1; memset(&fbefore,0xa5,sizeof(fbefore)); CHECK(RejectF(copy+428,1752,&h,&fbefore));
	memcpy(copy,bytes,sizeof(copy)); copy[428+320+84]^=1; memset(&fbefore,0xa5,sizeof(fbefore)); CHECK(RejectF(copy+428,1752,&h,&fbefore));
	memset(&fbefore,0xa5,sizeof(fbefore)); CHECK(RejectF(bytes+428,1751,&h,&fbefore));
	CHECK(!NativeReplayV4_StreamSize(&(struct NativeReplayV4Header){0},&(size_t){0})); h.frameCount=UINT32_MAX; CHECK(!NativeReplayV4_StreamSize(&h,&(size_t){0}));
	puts("native_replay_v4_test: passed"); return 0;
}
