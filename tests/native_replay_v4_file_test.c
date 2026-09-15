#include "platform/native_replay_v4_file.h"
#include "platform/native_sha256.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#define PATH_BYTES MAX_PATH
#else
#include <unistd.h>
#define PATH_BYTES 512
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static int Path(char p[PATH_BYTES]) {
#if defined(_WIN32)
	char d[MAX_PATH]; DWORD n=GetTempPathA(sizeof(d),d); return n>0&&n<sizeof(d)&&GetTempFileNameA(d,"c4f",0,p)!=0&&remove(p)==0;
#else
	char t[]="/tmp/ctr-v4-file-XXXXXX"; int x=mkstemp(t); if(x<0||close(x))return 0; if(unlink(t))return 0; memcpy(p,t,sizeof(t)); return 1;
#endif
}
static int Fill(struct NativeReplayV4Header *h,struct NativeReplayV4Frame *f,uint32_t n) {
	uint8_t st[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]={0}; NativeReplayV4Header_Init(h);
	for(uint32_t x=0;x<32;x++){h->identity.build[x]=(uint8_t)x;h->identity.content[x]=(uint8_t)(0x80+x);} NativeMatchConfigV1_InitArcadeTwoCab(&h->config);
	h->config.trackID=1;h->config.gameMode1=2;h->config.gameMode2=3;h->config.rules=4;h->config.lapCount=3;h->config.tickRateNumerator=30;h->config.tickRateDenominator=1;h->config.masterSeed=5;
	memcpy(h->config.buildIdentity,h->identity.build,32);memcpy(h->config.contentIdentity,h->identity.content,32);for(uint32_t x=0;x<6;x++){h->config.slots[x].characterID=(uint8_t)x;h->config.slots[x].difficulty=2;}for(uint32_t x=0;x<32;x++)h->config.botRulesDigest[x]=(uint8_t)(0x20+x);
	memset(f,0,sizeof(*f));f->replayFrame=n;f->begin.frameCounter=-2;f->end.elapsedTimeMS=32;f->padCount=4;f->pads[0].connected=1;f->vsyncPacketCount=1;f->vsyncPackets[0]=2;f->vsyncTotal=2;NativeCanonicalStateV4_Init(&f->canonical);f->canonical.identity=h->identity;f->canonical.frameNumber=n;st[64]=1;
	return NativeReplayV4Header_Validate(h)&&NativeMatchConfigV1_Digest(&h->config,f->canonical.configDigest)&&NativeCanonicalDriversV1_FromNormativeStream(&f->canonical.drivers,1,st,sizeof(st))&&NativeCanonicalStateV4_ComputeDigests(&f->canonical);
}
static int Bytes(const char *p,uint8_t *b,size_t n) { FILE *f=fopen(p,"rb"); if(!f)return 0; if(fread(b,1,n,f)!=n||fgetc(f)!=EOF){fclose(f);return 0;}fclose(f);return 1; }
static int Golden(uint8_t b[NATIVE_REPLAY_V4_HEADER_BYTES+NATIVE_REPLAY_V4_FRAME_BYTES]) { static const uint8_t want[32]={0x5b,0x99,0x65,0x2c,0xf9,0x48,0x7e,0xb7,0x17,0xbb,0xe4,0x89,0x27,0x2f,0xb9,0x71,0xbf,0x3c,0x61,0xed,0x5c,0x43,0x96,0xe4,0x2e,0x0e,0x9e,0xc7,0x8b,0x49,0x55,0x64}; struct NativeSha256 s;uint8_t got[32]; if(!Bytes("tests/fixtures/crv4_file_frame0_golden.bin",b,NATIVE_REPLAY_V4_HEADER_BYTES+NATIVE_REPLAY_V4_FRAME_BYTES))return 0;NativeSha256_Init(&s);NativeSha256_Update(&s,b,NATIVE_REPLAY_V4_HEADER_BYTES+NATIVE_REPLAY_V4_FRAME_BYTES);NativeSha256_Final(&s,got);return memcmp(got,want,32)==0; }
int main(void) {
	char p[PATH_BYTES],q[PATH_BYTES];struct NativeIdentityV1 wrong;struct NativeReplayV4RecordSession rec,rec2;struct NativeReplayV4PlaybackSession play;struct NativeReplayV4Header h,out;struct NativeReplayV4Frame f;uint8_t b[NATIVE_REPLAY_V4_HEADER_BYTES+NATIVE_REPLAY_V4_FRAME_BYTES],gold[sizeof(b)],old[3]={1,2,3};FILE *x;
	CHECK(Fill(&h,&f,0));CHECK(Golden(gold));CHECK(Path(p)); /* Existing destination is never touched before commit. */
	x=fopen(p,"wb");CHECK(x&&fwrite(old,1,3,x)==3&&fclose(x)==0);NativeReplayV4Record_Init(&rec);CHECK(NativeReplayV4Record_Open(&rec,p,&h.identity,&h.config));CHECK(Bytes(p,b,3)&&!memcmp(b,old,3));CHECK(rec.temporaryPath&&strcmp(rec.temporaryPath,p));CHECK(NativeReplayV4Frame_Validate(&rec.header,&f));CHECK(NativeReplayV4Record_AppendFrame(&rec,&f));CHECK(NativeReplayV4Record_Finalize(&rec));CHECK(Bytes(p,b,sizeof(b)));
	CHECK(!memcmp(b,gold,sizeof(b)));NativeReplayV4Playback_Init(&play);CHECK(NativeReplayV4Playback_Open(&play,p,&h.identity,&h.config,&out));CHECK(out.frameCount==1);CHECK(NativeReplayV4Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V4_READ_FRAME&&f.replayFrame==0);CHECK(NativeReplayV4Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V4_READ_EOF);CHECK(NativeReplayV4Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V4_READ_EOF);NativeReplayV4Playback_Close(&play);
	/* A canonical-config or identity mismatch is rejected at the file boundary. */
	wrong=h.identity;wrong.build[0]^=1;NativeReplayV4Playback_Init(&play);CHECK(!NativeReplayV4Playback_Open(&play,p,&wrong,&h.config,&out));out.config.trackID^=1;NativeReplayV4Playback_Init(&play);CHECK(!NativeReplayV4Playback_Open(&play,p,&h.identity,&out.config,&out));
	/* Exact physical length gates both truncation and trailing bytes. */
	x=fopen(p,"ab");CHECK(x&&fputc(0,x)!=EOF&&fclose(x)==0);NativeReplayV4Playback_Init(&play);CHECK(!NativeReplayV4Playback_Open(&play,p,&h.identity,&h.config,&out));x=fopen(p,"wb");CHECK(x&&fwrite(gold,1,sizeof(gold)-1,x)==sizeof(gold)-1&&fclose(x)==0);NativeReplayV4Playback_Init(&play);CHECK(!NativeReplayV4Playback_Open(&play,p,&h.identity,&h.config,&out));
	/* A provisional header is never playable, even through its private path. */
	CHECK(Path(q));NativeReplayV4Record_Init(&rec);CHECK(NativeReplayV4Record_Open(&rec,q,&h.identity,&h.config));NativeReplayV4Playback_Init(&play);CHECK(!NativeReplayV4Playback_Open(&play,rec.temporaryPath,&h.identity,&h.config,&out));CHECK(Fill(&out,&f,0));CHECK(NativeReplayV4Record_AppendFrame(&rec,&f));NativeReplayV4Record_Close(&rec);NativeReplayV4Playback_Init(&play);CHECK(!NativeReplayV4Playback_Open(&play,q,&h.identity,&h.config,&out));
	/* Persistent temporary-create failure returns with no partially opened state. */
	CHECK(Path(q));CHECK(strlen(q)+sizeof(".missing-parent/replay")<=sizeof(q));strcat(q,".missing-parent/replay");NativeReplayV4Record_Init(&rec);CHECK(!NativeReplayV4Record_Open(&rec,q,&h.identity,&h.config));CHECK(!rec.stream&&!rec.path&&!rec.temporaryPath&&!rec.failed&&!rec.finalized);
	/* Zero-frame is finalized/playable; order and saturation poison recording. */
	CHECK(Path(q));NativeReplayV4Record_Init(&rec);CHECK(NativeReplayV4Record_Open(&rec,q,&h.identity,&h.config));CHECK(NativeReplayV4Record_Finalize(&rec));NativeReplayV4Playback_Init(&play);CHECK(NativeReplayV4Playback_Open(&play,q,&h.identity,&h.config,&out));CHECK(NativeReplayV4Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V4_READ_EOF);NativeReplayV4Playback_Close(&play);CHECK(remove(q)==0);
	CHECK(Path(q));NativeReplayV4Record_Init(&rec);NativeReplayV4Record_Init(&rec2);CHECK(NativeReplayV4Record_Open(&rec,q,&h.identity,&h.config));CHECK(NativeReplayV4Record_Open(&rec2,q,&h.identity,&h.config));CHECK(strcmp(rec.temporaryPath,rec2.temporaryPath)!=0);CHECK(Fill(&out,&f,1));CHECK(!NativeReplayV4Record_AppendFrame(&rec,&f)&&rec.failed);NativeReplayV4Record_Close(&rec);NativeReplayV4Record_Close(&rec2);
	CHECK(Path(q));NativeReplayV4Record_Init(&rec);CHECK(NativeReplayV4Record_Open(&rec,q,&h.identity,&h.config));CHECK(Fill(&out,&f,0)&&NativeReplayV4Record_AppendFrame(&rec,&f));CHECK(Fill(&out,&f,1)&&NativeReplayV4Record_AppendFrame(&rec,&f));CHECK(NativeReplayV4Record_Finalize(&rec));NativeReplayV4Playback_Init(&play);CHECK(NativeReplayV4Playback_Open(&play,q,&h.identity,&h.config,&out)&&out.frameCount==2);CHECK(NativeReplayV4Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V4_READ_FRAME&&f.replayFrame==0);CHECK(NativeReplayV4Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V4_READ_FRAME&&f.replayFrame==1);NativeReplayV4Playback_Close(&play);CHECK(remove(q)==0);
	/* Every fault keeps the pre-existing target intact and poisons the session. */
	for(int fault=NATIVE_REPLAY_V4_FILE_TEST_FAULT_APPEND;fault<=NATIVE_REPLAY_V4_FILE_TEST_FAULT_RENAME;fault++){CHECK(Path(q));x=fopen(q,"wb");CHECK(x&&fwrite(old,1,3,x)==3&&fclose(x)==0);NativeReplayV4Record_Init(&rec);CHECK(NativeReplayV4Record_Open(&rec,q,&h.identity,&h.config));CHECK(Fill(&out,&f,0));NativeReplayV4File_TestSetFault((enum NativeReplayV4FileTestFault)fault);if(fault==NATIVE_REPLAY_V4_FILE_TEST_FAULT_APPEND||fault==NATIVE_REPLAY_V4_FILE_TEST_FAULT_SYNC)CHECK(!NativeReplayV4Record_AppendFrame(&rec,&f));else {CHECK(NativeReplayV4Record_AppendFrame(&rec,&f));CHECK(!NativeReplayV4Record_Finalize(&rec));}CHECK(rec.failed&&Bytes(q,b,3)&&!memcmp(b,old,3));NativeReplayV4Record_Close(&rec);CHECK(remove(q)==0);}
	CHECK(remove(p)==0);puts("native_replay_v4_file_test: passed");return 0;
}
