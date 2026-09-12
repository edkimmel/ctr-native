#include "platform/native_canonical_state_v3.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

static void Fill(struct NativeCanonicalStateV3 *state)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	NativeCanonicalStateV3_Init(state);
	for (uint32_t i=0;i<NATIVE_IDENTITY_DIGEST_BYTES;i++){state->identity.build[i]=(uint8_t)i;state->identity.content[i]=(uint8_t)(0x80u+i);}
	state->frameNumber=9;state->control.frameCounter=-7;state->rng.advRng1=0x12345678;state->input.pads[0].connected=1;
	stream[NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES]=1;
	(void)NativeCanonicalDriversV1_FromNormativeStream(&state->drivers,1,stream,sizeof(stream));
	(void)NativeCanonicalStateV3_ComputeDigests(state);
}

int main(void)
{
	uint8_t bytes[584], before[584];
	struct NativeCanonicalStateV3 state, decoded, untouched;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	Fill(&state);
	CHECK(NativeCanonicalStateV3_EncodedSize()==584u);
	NativeCodecWriter_Init(&writer,bytes,sizeof(bytes),NULL);CHECK(NativeCanonicalStateV3_Encode(&writer,&state));CHECK(NativeCodecWriter_Size(&writer)==sizeof(bytes));
	CHECK(bytes[0]==0x4e&&bytes[1]==0x43&&bytes[2]==0x56&&bytes[3]==0x33&&bytes[4]==3&&bytes[8]==3);
	NativeCodecReader_Init(&reader,bytes,sizeof(bytes));CHECK(NativeCanonicalStateV3_Decode(&reader,&state.identity,&decoded));CHECK(reader.offset==sizeof(bytes));
	CHECK(decoded.drivers.presenceMask==1&&decoded.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS-1]==state.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS-1]);
	memcpy(before,bytes,sizeof(bytes));bytes[240+8]^=1;NativeCodecReader_Init(&reader,bytes,sizeof(bytes));memset(&untouched,0xa5,sizeof(untouched));CHECK(!NativeCanonicalStateV3_Decode(&reader,&state.identity,&untouched));CHECK(reader.offset==0);memcpy(bytes,before,sizeof(bytes));
	state.drivers.slots[0].slotDigest^=1;NativeCodecWriter_Init(&writer,bytes,sizeof(bytes),NULL);CHECK(!NativeCanonicalStateV3_Encode(&writer,&state));CHECK(writer.offset==0);
	puts("native_canonical_state_v3_test: passed");return 0;
}
