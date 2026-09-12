#include "platform/native_replay_v3_file.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#define PATH_BYTES MAX_PATH
#else
#include <unistd.h>
#define PATH_BYTES 512
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: check failed: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static int Path(char p[PATH_BYTES]){
#if defined(_WIN32)
	char d[MAX_PATH];DWORD n=GetTempPathA(sizeof(d),d);return n>0&&n<sizeof(d)&&GetTempFileNameA(d,"cv3",0,p)!=0&&remove(p)==0;
#else
	char t[]="/tmp/ctr-v3-XXXXXX";int x=mkstemp(t);if(x<0||close(x)!=0)return 0;if(unlink(t)!=0)return 0;memcpy(p,t,sizeof(t));return 1;
#endif
}
static int Frame(const struct NativeReplayV3Header*h,uint32_t n,struct NativeReplayV3Frame*f){uint8_t st[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES]={0};memset(f,0,sizeof(*f));f->replayFrame=n;f->padCount=4;f->vsyncPacketCount=1;f->vsyncPackets[0]=1;f->vsyncTotal=1;NativeCanonicalStateV3_Init(&f->canonical);f->canonical.identity=h->identity;f->canonical.frameNumber=n;st[64]=1;if(!NativeCanonicalDriversV1_FromNormativeStream(&f->canonical.drivers,1,st,sizeof(st)))return 0;return NativeCanonicalStateV3_ComputeDigests(&f->canonical);}
static void Id(struct NativeIdentityV1*i){for(uint32_t n=0;n<NATIVE_IDENTITY_DIGEST_BYTES;n++){i->build[n]=(uint8_t)n;i->content[n]=(uint8_t)(n+64);}}
int main(void){char p[PATH_BYTES];struct NativeIdentityV1 i;struct NativeReplayV3RecordSession rec;struct NativeReplayV3PlaybackSession play;struct NativeReplayV3Header h;struct NativeReplayV3Frame f;uint64_t below,at,over;Id(&i);CHECK(Path(p));NativeReplayV3Record_Init(&rec);NativeReplayV3Playback_Init(&play);CHECK(NativeReplayV3Record_Open(&rec,p,&i));CHECK(!NativeReplayV3Playback_Open(&play,p,&i,&h));CHECK(Frame(&rec.header,0,&f));CHECK(NativeReplayV3Record_AppendFrame(&rec,&f));NativeReplayV3Record_Close(&rec);NativeReplayV3Playback_Init(&play);CHECK(!NativeReplayV3Playback_Open(&play,p,&i,&h));CHECK(Path(p));NativeReplayV3Record_Init(&rec);CHECK(NativeReplayV3Record_Open(&rec,p,&i));CHECK(Frame(&rec.header,0,&f));CHECK(NativeReplayV3Record_AppendFrame(&rec,&f));CHECK(NativeReplayV3Record_Finalize(&rec));NativeReplayV3Playback_Init(&play);CHECK(NativeReplayV3Playback_Open(&play,p,&i,&h));CHECK(h.frameCount==1&&h.flags==NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED);CHECK(NativeReplayV3Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V3_READ_FRAME&&f.replayFrame==0);CHECK(NativeReplayV3Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V3_READ_EOF);NativeReplayV3Playback_Close(&play);CHECK(remove(p)==0);CHECK(Path(p));NativeReplayV3Record_Init(&rec);CHECK(NativeReplayV3Record_Open(&rec,p,&i));CHECK(NativeReplayV3Record_Finalize(&rec));NativeReplayV3Playback_Init(&play);CHECK(NativeReplayV3Playback_Open(&play,p,&i,&h));CHECK(h.frameCount==0&&h.flags==NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED);CHECK(NativeReplayV3Playback_ReadNext(&play,&f)==NATIVE_REPLAY_V3_READ_EOF);NativeReplayV3Playback_Close(&play);CHECK(remove(p)==0);CHECK(Path(p));NativeReplayV3Record_Init(&rec);CHECK(NativeReplayV3Record_Open(&rec,p,&i));CHECK(Frame(&rec.header,0,&f));CHECK(NativeReplayV3Record_AppendFrame(&rec,&f));NativeReplayV3File_TestFailNextFinalizeClose();CHECK(!NativeReplayV3Record_Finalize(&rec));NativeReplayV3Playback_Init(&play);CHECK(!NativeReplayV3Playback_Open(&play,p,&i,&h));NativeReplayV3Record_Close(&rec);CHECK(Path(p));NativeReplayV3Record_Init(&rec);CHECK(NativeReplayV3Record_Open(&rec,p,&i));CHECK(Frame(&rec.header,0,&f));CHECK(NativeReplayV3Record_AppendFrame(&rec,&f));CHECK(NativeReplayV3Record_Finalize(&rec));CHECK(remove(p)==0);CHECK(NativeReplayV3File_ExpectedLength((uint32_t)((UINT64_C(100)*1024*1024-NATIVE_REPLAY_V3_HEADER_BYTES)/NATIVE_REPLAY_V3_FRAME_BYTES),&below));CHECK(NativeReplayV3File_ExpectedLength((uint32_t)((UINT64_C(100)*1024*1024-NATIVE_REPLAY_V3_HEADER_BYTES)/NATIVE_REPLAY_V3_FRAME_BYTES+1),&at));CHECK(at>UINT64_C(100)*1024*1024&&below<=UINT64_C(100)*1024*1024);CHECK(NativeReplayV3File_ExpectedLength(UINT32_MAX,&over)&&over>at);puts("native_replay_v3_file_test: passed");return 0;}
