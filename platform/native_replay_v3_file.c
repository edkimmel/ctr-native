#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include "platform/native_replay_v3_file.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif
static FILE *S(void *p){return(FILE*)p;}
static int Seek(FILE*f,uint64_t n)
{
	if(f==NULL||n>(uint64_t)INT64_MAX)return 0;
#if defined(_WIN32)
	return _fseeki64(f,(__int64)n,SEEK_SET)==0;
#else
	return fseeko(f,(off_t)n,SEEK_SET)==0;
#endif
}
static int Length(FILE*f,uint64_t*n)
{
#if defined(_WIN32)
	__int64 x;if(f==NULL||_fseeki64(f,0,SEEK_END)!=0||(x=_ftelli64(f))<0)return 0;
#else
	off_t x;if(f==NULL||fseeko(f,0,SEEK_END)!=0||(x=ftello(f))<0)return 0;
#endif
	*n=(uint64_t)x;return Seek(f,0);
}
static int Sync(FILE*f)
{
	if(f==NULL||fflush(f)!=0)return 0;
#if defined(_WIN32)
	return _commit(_fileno(f))==0;
#else
	return fsync(fileno(f))==0;
#endif
}
static int EH(const struct NativeReplayV3Header*h,uint8_t b[NATIVE_REPLAY_V3_HEADER_BYTES]){struct NativeCodecWriter w;NativeCodecWriter_Init(&w,b,NATIVE_REPLAY_V3_HEADER_BYTES,NULL);return NativeReplayV3Header_Encode(&w,h)&&NativeCodecWriter_Size(&w)==NATIVE_REPLAY_V3_HEADER_BYTES;}
static int EF(const struct NativeReplayV3Header*h,const struct NativeReplayV3Frame*f,uint8_t b[NATIVE_REPLAY_V3_FRAME_BYTES]){struct NativeCodecWriter w;NativeCodecWriter_Init(&w,b,NATIVE_REPLAY_V3_FRAME_BYTES,NULL);return NativeReplayV3Frame_Encode(&w,h,f)&&NativeCodecWriter_Size(&w)==NATIVE_REPLAY_V3_FRAME_BYTES;}
int NativeReplayV3File_ExpectedLength(uint32_t c,uint64_t*n){if(n==NULL||c>(UINT64_MAX-NATIVE_REPLAY_V3_HEADER_BYTES)/NATIVE_REPLAY_V3_FRAME_BYTES)return 0;*n=NATIVE_REPLAY_V3_HEADER_BYTES+(uint64_t)c*NATIVE_REPLAY_V3_FRAME_BYTES;return 1;}
void NativeReplayV3Record_Init(struct NativeReplayV3RecordSession*s){if(s!=NULL)memset(s,0,sizeof(*s));}
void NativeReplayV3Record_Close(struct NativeReplayV3RecordSession*s){if(s==NULL)return;if(S(s->stream)!=NULL)(void)fclose(S(s->stream));memset(s,0,sizeof(*s));}
int NativeReplayV3Record_Open(struct NativeReplayV3RecordSession*s,const char*p,const struct NativeIdentityV1*i){struct NativeReplayV3Header h;uint8_t b[NATIVE_REPLAY_V3_HEADER_BYTES];FILE*f;if(s==NULL||s->stream!=NULL||p==NULL||i==NULL)return 0;NativeReplayV3Header_Init(&h);h.identity=*i;if(!EH(&h,b))return 0;f=fopen(p,"wb+");if(f==NULL||fwrite(b,1,sizeof(b),f)!=sizeof(b)||!Sync(f)){if(f!=NULL)(void)fclose(f);return 0;}s->stream=f;s->header=h;return 1;}
int NativeReplayV3Record_AppendFrame(struct NativeReplayV3RecordSession*s,const struct NativeReplayV3Frame*f){struct NativeReplayV3Header h;uint8_t b[NATIVE_REPLAY_V3_FRAME_BYTES];uint64_t n;if(s==NULL||s->failed||s->finalized||s->stream==NULL||f==NULL)return 0;if(f->replayFrame!=s->nextFrame||s->header.frameCount==UINT32_MAX){s->failed=1;return 0;}h=s->header;h.frameCount++;if(!NativeReplayV3File_ExpectedLength(h.frameCount,&n)||!EF(&s->header,f,b)||fwrite(b,1,sizeof(b),S(s->stream))!=sizeof(b)){s->failed=1;return 0;}s->header=h;s->nextFrame++;return 1;}
int NativeReplayV3Record_Finalize(struct NativeReplayV3RecordSession*s){uint8_t b[NATIVE_REPLAY_V3_HEADER_BYTES];struct NativeReplayV3Header h;int closeResult;if(s==NULL||s->failed||s->finalized||s->stream==NULL)return 0;h=s->header;h.flags|=NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED;if(!EH(&h,b)||!Seek(S(s->stream),0)||fwrite(b,1,sizeof(b),S(s->stream))!=sizeof(b)||!Sync(S(s->stream))){s->failed=1;return 0;}closeResult=fclose(S(s->stream));s->stream=NULL;if(closeResult!=0){s->failed=1;return 0;}s->header=h;s->finalized=1;return 1;}
void NativeReplayV3Playback_Init(struct NativeReplayV3PlaybackSession*s){if(s!=NULL)memset(s,0,sizeof(*s));}
void NativeReplayV3Playback_Close(struct NativeReplayV3PlaybackSession*s){if(s==NULL)return;if(S(s->stream)!=NULL)(void)fclose(S(s->stream));memset(s,0,sizeof(*s));}
int NativeReplayV3Playback_Open(struct NativeReplayV3PlaybackSession*s,const char*p,const struct NativeIdentityV1*i,struct NativeReplayV3Header*out){uint8_t b[NATIVE_REPLAY_V3_HEADER_BYTES];struct NativeCodecReader r;struct NativeReplayV3Header h;FILE*f;uint64_t got,want;if(s==NULL||s->stream!=NULL||p==NULL||i==NULL||out==NULL)return 0;f=fopen(p,"rb");if(f==NULL||!Length(f,&got)||fread(b,1,sizeof(b),f)!=sizeof(b)){if(f!=NULL)(void)fclose(f);return 0;}NativeCodecReader_Init(&r,b,sizeof(b));if(!NativeReplayV3Header_Decode(&r,i,&h)||NativeCodecReader_Remaining(&r)!=0||(h.flags&NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED)==0||!NativeReplayV3File_ExpectedLength(h.frameCount,&want)||got!=want){(void)fclose(f);return 0;}s->stream=f;s->header=h;s->expectedIdentity=*i;*out=h;return 1;}
int NativeReplayV3Playback_ReadNext(struct NativeReplayV3PlaybackSession*s,struct NativeReplayV3Frame*f){uint8_t b[NATIVE_REPLAY_V3_FRAME_BYTES];struct NativeCodecReader r;struct NativeReplayV3Frame d;if(s==NULL||f==NULL||s->stream==NULL||s->failed)return NATIVE_REPLAY_V3_READ_ERROR;if(s->nextFrame==s->header.frameCount)return NATIVE_REPLAY_V3_READ_EOF;if(fread(b,1,sizeof(b),S(s->stream))!=sizeof(b)){s->failed=1;return NATIVE_REPLAY_V3_READ_ERROR;}NativeCodecReader_Init(&r,b,sizeof(b));if(!NativeReplayV3Frame_Decode(&r,&s->header,&s->expectedIdentity,&d)||NativeCodecReader_Remaining(&r)!=0||d.replayFrame!=s->nextFrame){s->failed=1;return NATIVE_REPLAY_V3_READ_ERROR;}*f=d;s->nextFrame++;return NATIVE_REPLAY_V3_READ_FRAME;}
