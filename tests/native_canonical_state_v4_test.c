#include "platform/native_canonical_state_v4.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static int Fill(struct NativeCanonicalStateV4 *s)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]={0};
	NativeCanonicalStateV4_Init(s);
	for(uint32_t i=0;i<32;i++){s->identity.build[i]=(uint8_t)i;s->identity.content[i]=(uint8_t)(0x80+i);s->configDigest[i]=(uint8_t)(0x40+i);}
	s->frameNumber=9;s->control.frameCounter=-7;s->retailRng.advRng1=0x12345678;s->input.pads[0].connected=1;
	stream[64]=1;CHECK(NativeCanonicalDriversV1_FromNormativeStream(&s->drivers,1,stream,sizeof(stream)));
	CHECK(NativeDeterministicRngBankV1_Init(&s->deterministicRng,0x1234,1));
	CHECK(NativeCanonicalStateV4_ComputeDigests(s)); return 0;
}
int main(void)
{
	uint8_t bytes[1432], before[1432]; struct NativeCanonicalStateV4 s,out,unchanged;struct NativeCodecWriter w;struct NativeCodecReader r;
	CHECK(!Fill(&s)); CHECK(NativeCanonicalStateV4_EncodedSize()==sizeof(bytes)); NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);CHECK(NativeCanonicalStateV4_Encode(&w,&s)&&w.offset==sizeof(bytes));
	CHECK(bytes[0]==0x4e&&bytes[1]==0x43&&bytes[2]==0x56&&bytes[3]==0x34&&bytes[4]==5&&bytes[8]==4);
	NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)); CHECK(r.offset==sizeof(bytes)); CHECK(!memcmp(&s,&out,sizeof(s)));
	memcpy(before,bytes,sizeof(bytes)); bytes[0]^=1; unchanged=out;NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0&&!memcmp(&out,&unchanged,sizeof(out)));memcpy(bytes,before,sizeof(bytes));
	bytes[4]^=1;NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[84]^=1;NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[116]^=1;NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[120]^=1;NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[124+48+8+600]^=1; /* input payload mutation */ NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[1200]^=1; /* WORLD component ID */ NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[1204]^=1; /* fixed mine-registry slot length */ NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	bytes[1224]=1; /* required zero padding after an unavailable mine registry */ NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);memcpy(bytes,before,sizeof(bytes));
	for(size_t n=0;n<sizeof(bytes);n+=137){NativeCodecReader_Init(&r,bytes,n);CHECK(!NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out)&&r.offset==0);}
	puts("native_canonical_state_v4_test: passed");return 0;
}
