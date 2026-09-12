#include "platform/native_replay_v3.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

static int Fill(struct NativeReplayV3Header *h,struct NativeReplayV3Frame *f)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]={0};
	NativeReplayV3Header_Init(h);h->flags=NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED;h->frameCount=1;
	for(uint32_t i=0;i<NATIVE_IDENTITY_DIGEST_BYTES;i++){h->identity.build[i]=(uint8_t)i;h->identity.content[i]=(uint8_t)(0x80u+i);}memset(f,0,sizeof(*f));f->replayFrame=4;f->begin.frameCounter=-2;f->end.elapsedTimeMS=32;f->padCount=4;f->pads[0].connected=1;f->vsyncPacketCount=1;f->vsyncPackets[0]=2;f->vsyncTotal=2;NativeCanonicalStateV3_Init(&f->canonical);f->canonical.identity=h->identity;f->canonical.frameNumber=4;f->canonical.control.frameCounter=-2;stream[64]=1;if(!NativeCanonicalDriversV1_FromNormativeStream(&f->canonical.drivers,1,stream,sizeof(stream)))return 0;return NativeCanonicalStateV3_ComputeDigests(&f->canonical);
}
int main(void)
{
	uint8_t bytes[NATIVE_REPLAY_V3_HEADER_BYTES+NATIVE_REPLAY_V3_FRAME_BYTES];struct NativeReplayV3Header h,d;struct NativeReplayV3Frame f,df,before;struct NativeCodecWriter w;struct NativeCodecReader r;
	CHECK(Fill(&h,&f));CHECK(NativeReplayV3Header_EncodedSize()==140u&&NativeReplayV3Frame_EncodedSize()==904u);NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);CHECK(NativeReplayV3Header_Encode(&w,&h));CHECK(bytes[0]==0x43&&bytes[1]==0x52&&bytes[2]==0x56&&bytes[3]==0x33&&bytes[8]==0x8c&&bytes[16]==1);CHECK(NativeReplayV3Frame_Encode(&w,&h,&f));CHECK(NativeCodecWriter_Size(&w)==sizeof(bytes));CHECK(bytes[140]==0x43&&bytes[141]==0x52&&bytes[142]==0x46&&bytes[143]==0x33);
	NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(NativeReplayV3Header_Decode(&r,&h.identity,&d));CHECK(NativeReplayV3Frame_Decode(&r,&d,&h.identity,&df));CHECK(r.offset==sizeof(bytes)&&df.canonical.drivers.presenceMask==1);
	bytes[16]=2;NativeCodecReader_Init(&r,bytes,NATIVE_REPLAY_V3_HEADER_BYTES);memset(&d,0xa5,sizeof(d));before=f;CHECK(!NativeReplayV3Header_Decode(&r,&h.identity,&d));CHECK(r.offset==0);bytes[16]=1;
	bytes[NATIVE_REPLAY_V3_HEADER_BYTES+460]=2;NativeCodecReader_Init(&r,&bytes[NATIVE_REPLAY_V3_HEADER_BYTES],NATIVE_REPLAY_V3_FRAME_BYTES);memset(&df,0xa5,sizeof(df));CHECK(!NativeReplayV3Frame_Decode(&r,&h,&h.identity,&df));CHECK(r.offset==0);(void)before;
	puts("native_replay_v3_test: passed");return 0;
}
