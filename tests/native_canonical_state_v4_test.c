#include "platform/native_canonical_state_v4.h"
#include <stdio.h>
#include <string.h>

#define NCV4_BYTES 1432u
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)

static int Fill(struct NativeCanonicalStateV4 *s)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	NativeCanonicalStateV4_Init(s);
	for (uint32_t i = 0; i < 32; ++i) { s->identity.build[i] = (uint8_t)i; s->identity.content[i] = (uint8_t)(0x80 + i); s->configDigest[i] = (uint8_t)(0x40 + i); }
	s->frameNumber = 9; s->control.frameCounter = -7; s->retailRng.advRng1 = UINT32_C(0x12345678); s->input.pads[0].connected = 1;
	stream[64] = 1;
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&s->drivers, 1, stream, sizeof(stream)));
	CHECK(NativeDeterministicRngBankV1_Init(&s->deterministicRng, UINT32_C(0x1234), 1));
	CHECK(NativeCanonicalStateV4_ComputeDigests(s)); return 0;
}

static int ReadGolden(uint8_t golden[NCV4_BYTES])
{
	FILE *f = fopen(NCV4_GOLDEN_PATH, "rb");
	if (!f) return 0;
	if (fread(golden, 1, NCV4_BYTES, f) != NCV4_BYTES || fgetc(f) != EOF) { fclose(f); return 0; }
	fclose(f); return 1;
}

static int Reject(const uint8_t *bytes, size_t size, const struct NativeIdentityV1 *identity, const uint8_t config[32], const struct NativeCanonicalStateV4 *sentinel)
{
	struct NativeCodecReader r, before; struct NativeCanonicalStateV4 out = *sentinel;
	NativeCodecReader_Init(&r, bytes, size); before = r;
	return !NativeCanonicalStateV4_Decode(&r, identity, config, &out) && memcmp(&r, &before, sizeof(r)) == 0 && memcmp(&out, sentinel, sizeof(out)) == 0;
}

static void PutU32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

int main(void)
{
	uint8_t bytes[NCV4_BYTES], golden[NCV4_BYTES], scratch[NCV4_BYTES], shortBuffer[NCV4_BYTES], wrongConfig[32];
	uint8_t exactScratch[NATIVE_CANONICAL_STATE_V4_MAX_DOMAIN_BYTES];
	size_t domain[6], payload[6], digest[6], cursor = 116;
	struct NativeCanonicalStateV4 s, out, sentinel, expected; struct NativeCodecWriter w, beforeWriter; struct NativeCodecReader r; struct NativeCodecDigest64 wd; uint64_t digestBefore;
	CHECK(!Fill(&s)); CHECK(NativeCanonicalStateV4_EncodedSize() == NCV4_BYTES); CHECK(ReadGolden(golden));
	NativeCodecWriter_Init(&w, bytes, sizeof(bytes), NULL); CHECK(NativeCanonicalStateV4_Encode(&w, &s) && w.offset == sizeof(bytes));
	/* Checked-in full fixture: byte equality makes this independent of decoding. */
	CHECK(memcmp(bytes, golden, sizeof(bytes)) == 0);
	NativeCodecReader_Init(&r, golden, sizeof(golden)); CHECK(NativeCanonicalStateV4_Decode(&r, &s.identity, s.configDigest, &out)); CHECK(r.offset == sizeof(golden) && memcmp(&s, &out, sizeof(s)) == 0);
	NativeCodecWriter_Init(&w, scratch, sizeof(scratch), NULL); CHECK(NativeCanonicalStateV4_Encode(&w, &out) && memcmp(scratch, golden, sizeof(golden)) == 0);
	/* The bounded in-place digest path is byte-identical to the transactional
	 * one and rejects a short, null, or absent scratch. */
	CHECK(NATIVE_CANONICAL_STATE_V4_MAX_DOMAIN_BYTES == 600u);
	out = s; out.domainDigests[0] ^= UINT64_MAX; out.combinedDigest ^= UINT64_MAX;
	CHECK(NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(&out, exactScratch, sizeof(exactScratch)));
	CHECK(memcmp(&out, &s, sizeof(out)) == 0);
	out = s; out.domainDigests[5] ^= UINT64_MAX;
	CHECK(NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(&out, scratch, sizeof(scratch)));
	CHECK(memcmp(&out, &s, sizeof(out)) == 0);
	out = s; CHECK(!NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(&out, exactScratch, NATIVE_CANONICAL_STATE_V4_MAX_DOMAIN_BYTES - 1u));
	CHECK(memcmp(&out, &s, sizeof(out)) == 0);
	out = s; CHECK(!NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(&out, NULL, sizeof(exactScratch)));
	CHECK(memcmp(&out, &s, sizeof(out)) == 0);
	CHECK(!NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(NULL, exactScratch, sizeof(exactScratch)));
	expected = s; expected.schemaVersion = 0; CHECK(!NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(&expected, exactScratch, sizeof(exactScratch)));
	for (uint32_t i=0; i<6; ++i) { domain[i]=cursor; payload[i]=cursor+8; cursor=payload[i]+(size_t)golden[domain[i]+4]+((size_t)golden[domain[i]+5]<<8)+((size_t)golden[domain[i]+6]<<16)+((size_t)golden[domain[i]+7]<<24); digest[i]=cursor; cursor+=8; }
	CHECK(cursor + 8 == sizeof(golden)); sentinel = out;
	/* Every truncation and every declared domain boundary is transactional. */
	for (size_t n=0; n<sizeof(golden); ++n) CHECK(Reject(golden, n, &s.identity, s.configDigest, &sentinel));
	for (size_t i=0; i<16; ++i) { memcpy(scratch,golden,sizeof(scratch)); scratch[i]^=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel)); }
	/* frameNumber is header metadata: accepted, excluded from digests, and round-trips exactly. */
	for (size_t i=16; i<20; ++i) {
		uint32_t changed = s.frameNumber ^ (UINT32_C(1) << (8u * (uint32_t)(i - 16)));
		memcpy(scratch,golden,sizeof(scratch)); scratch[i]^=1;
		NativeCodecReader_Init(&r,scratch,sizeof(scratch)); CHECK(NativeCanonicalStateV4_Decode(&r,&s.identity,s.configDigest,&out));
		expected=s; expected.frameNumber=changed;
		CHECK(r.offset==sizeof(scratch) && memcmp(&out,&expected,sizeof(out))==0);
		NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL); CHECK(NativeCanonicalStateV4_Encode(&w,&out) && w.offset==sizeof(bytes));
		CHECK(memcmp(bytes,scratch,sizeof(bytes))==0);
	}
	for (size_t i=20; i<116; ++i) { memcpy(scratch,golden,sizeof(scratch)); scratch[i]^=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel)); }
	for (uint32_t i=0; i<6; ++i) {
		memcpy(scratch,golden,sizeof(scratch)); PutU32(scratch+domain[i],99); CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
		memcpy(scratch,golden,sizeof(scratch)); PutU32(scratch+domain[i]+4,0); CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
		memcpy(scratch,golden,sizeof(scratch)); scratch[payload[i]]^=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
		memcpy(scratch,golden,sizeof(scratch)); scratch[digest[i]]^=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	}
	/* Nested component corruption: RNG bank, WORLD structure/slot, and topology. */
	memcpy(scratch,golden,sizeof(scratch)); PutU32(scratch+payload[1],3); CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(scratch,golden,sizeof(scratch)); scratch[payload[1]+24]^=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(scratch,golden,sizeof(scratch)); PutU32(scratch+payload[4]+8,99); CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(scratch,golden,sizeof(scratch)); PutU32(scratch+payload[4]+12,11); CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(scratch,golden,sizeof(scratch)); PutU32(scratch+payload[4]+28,115); CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(scratch,golden,sizeof(scratch)); scratch[payload[4]+48]=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(scratch,golden,sizeof(scratch)); scratch[payload[5]]^=1; CHECK(Reject(scratch,sizeof(scratch),&s.identity,s.configDigest,&sentinel));
	memcpy(wrongConfig,s.configDigest,sizeof(wrongConfig)); wrongConfig[0]^=1; CHECK(Reject(golden,sizeof(golden),&s.identity,wrongConfig,&sentinel));
	{ struct NativeIdentityV1 wrong=s.identity; wrong.build[0]^=1; CHECK(Reject(golden,sizeof(golden),&wrong,s.configDigest,&sentinel)); }
	/* Preflight capacity failure preserves writer, digest, and destination bytes. */
	memset(shortBuffer,0xA5,sizeof(shortBuffer)); NativeCodecDigest64_Init(&wd); NativeCodecWriter_Init(&w,shortBuffer,sizeof(shortBuffer)-1,&wd); beforeWriter=w; digestBefore=wd.value;
	CHECK(!NativeCanonicalStateV4_Encode(&w,&s)); CHECK(memcmp(&w,&beforeWriter,sizeof(w))==0 && wd.value==digestBefore);
	for (size_t i=0; i<sizeof(shortBuffer); ++i) CHECK(shortBuffer[i]==0xA5);
	puts("native_canonical_state_v4_test: passed"); return 0;
}
