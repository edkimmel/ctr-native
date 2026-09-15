#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/native_replay_v4_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static enum NativeReplayV4FileTestFault s_fault;
static unsigned long s_tempSerial;
#define NATIVE_REPLAY_V4_TEMP_CREATE_ATTEMPTS 64u
static FILE *S(void *p) { return (FILE *)p; }
static int Fault(enum NativeReplayV4FileTestFault f) { if (s_fault != f) return 0; s_fault=NATIVE_REPLAY_V4_FILE_TEST_FAULT_NONE; return 1; }
void NativeReplayV4File_TestSetFault(enum NativeReplayV4FileTestFault f) { s_fault=f; }
static char *CopyPath(const char *p) { size_t n; char *q; if (!p) return NULL; n=strlen(p)+1; q=(char *)malloc(n); if(q) memcpy(q,p,n); return q; }
static char *TempPath(const char *p) { char tail[48]; size_t n, m; char *q; if(!p) return NULL; ++s_tempSerial; (void)snprintf(tail,sizeof(tail),".crv4.%lu.tmp",s_tempSerial); n=strlen(p); m=strlen(tail); if(n>SIZE_MAX-m-1) return NULL; q=(char *)malloc(n+m+1); if(q){memcpy(q,p,n);memcpy(q+n,tail,m+1);} return q; }
static FILE *CreateNew(const char *p) { int fd;
#if defined(_WIN32)
	fd=_open(p,_O_CREAT|_O_EXCL|_O_RDWR|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
	fd=open(p,O_CREAT|O_EXCL|O_RDWR,0600);
#endif
	return fd < 0 ? NULL : fdopen(fd,"wb+");
}
static int Seek(FILE *f,uint64_t n) { if(!f || n>(uint64_t)INT64_MAX) return 0;
#if defined(_WIN32)
	return _fseeki64(f,(__int64)n,SEEK_SET)==0;
#else
	return fseeko(f,(off_t)n,SEEK_SET)==0;
#endif
}
static int Length(FILE *f,uint64_t *n) {
#if defined(_WIN32)
	__int64 x; if(!f||_fseeki64(f,0,SEEK_END)||((x=_ftelli64(f))<0))return 0;
#else
	off_t x; if(!f||fseeko(f,0,SEEK_END)||((x=ftello(f))<0))return 0;
#endif
	*n=(uint64_t)x; return Seek(f,0);
}
static int Sync(FILE *f) { if(!f||fflush(f)||Fault(NATIVE_REPLAY_V4_FILE_TEST_FAULT_SYNC))return 0;
#if defined(_WIN32)
	return _commit(_fileno(f))==0;
#else
	return fsync(fileno(f))==0;
#endif
}
static int Close(FILE *f) { int r=fclose(f); return r==0&&!Fault(NATIVE_REPLAY_V4_FILE_TEST_FAULT_CLOSE); }
static int Replace(const char *from,const char *to) {
	if(Fault(NATIVE_REPLAY_V4_FILE_TEST_FAULT_RENAME)) return 0;
#if defined(_WIN32)
	DWORD a=GetFileAttributesA(to); if(a!=INVALID_FILE_ATTRIBUTES) return ReplaceFileA(to,from,NULL,REPLACEFILE_WRITE_THROUGH,NULL,NULL)!=0;
	return MoveFileExA(from,to,MOVEFILE_WRITE_THROUGH)!=0;
#else
	return rename(from,to)==0;
#endif
}
static int EqIdentity(const struct NativeIdentityV1 *a,const struct NativeIdentityV1 *b) { return a&&b&&memcmp(a->build,b->build,32)==0&&memcmp(a->content,b->content,32)==0; }
static int EqConfig(const struct NativeMatchConfigV1 *a,const struct NativeMatchConfigV1 *b) { uint8_t x[256],y[256]; struct NativeCodecWriter wx,wy; if(!a||!b)return 0;NativeCodecWriter_Init(&wx,x,sizeof(x),NULL);NativeCodecWriter_Init(&wy,y,sizeof(y),NULL);return NativeMatchConfigV1_Encode(&wx,a)&&NativeMatchConfigV1_Encode(&wy,b)&&NativeCodecWriter_Size(&wx)==sizeof(x)&&NativeCodecWriter_Size(&wy)==sizeof(y)&&memcmp(x,y,sizeof(x))==0; }
static int EH(const struct NativeReplayV4Header *h,uint8_t b[NATIVE_REPLAY_V4_HEADER_BYTES]) { struct NativeCodecWriter w; NativeCodecWriter_Init(&w,b,NATIVE_REPLAY_V4_HEADER_BYTES,NULL); return NativeReplayV4Header_Encode(&w,h)&&NativeCodecWriter_Size(&w)==NATIVE_REPLAY_V4_HEADER_BYTES; }
static int EF(const struct NativeReplayV4Header *h,const struct NativeReplayV4Frame *f,uint8_t b[NATIVE_REPLAY_V4_FRAME_BYTES]) { struct NativeCodecWriter w; NativeCodecWriter_Init(&w,b,NATIVE_REPLAY_V4_FRAME_BYTES,NULL); return NativeReplayV4Frame_Encode(&w,h,f)&&NativeCodecWriter_Size(&w)==NATIVE_REPLAY_V4_FRAME_BYTES; }
int NativeReplayV4File_ExpectedLength(uint32_t c,uint64_t *n) { if(!n||c>(UINT64_MAX-NATIVE_REPLAY_V4_HEADER_BYTES)/NATIVE_REPLAY_V4_FRAME_BYTES)return 0;*n=NATIVE_REPLAY_V4_HEADER_BYTES+(uint64_t)c*NATIVE_REPLAY_V4_FRAME_BYTES;return 1; }
void NativeReplayV4Record_Init(struct NativeReplayV4RecordSession *s) { if(s)memset(s,0,sizeof(*s)); }
void NativeReplayV4Record_Close(struct NativeReplayV4RecordSession *s) { if(!s)return;if(S(s->stream)) (void)fclose(S(s->stream));if(s->temporaryPath)(void)remove(s->temporaryPath);free(s->path);free(s->temporaryPath);memset(s,0,sizeof(*s)); }
int NativeReplayV4Record_Open(struct NativeReplayV4RecordSession *s,const char *p,const struct NativeIdentityV1 *i,const struct NativeMatchConfigV1 *c) { struct NativeReplayV4Header h;uint8_t b[NATIVE_REPLAY_V4_HEADER_BYTES];char *target,*temp=NULL;FILE *f=NULL;unsigned int attempt;if(!s||s->stream||!p||!i||!c)return 0;target=CopyPath(p);if(!target)return 0;for(attempt=0;attempt<NATIVE_REPLAY_V4_TEMP_CREATE_ATTEMPTS;attempt++){temp=TempPath(p);if(!temp){free(target);return 0;}f=CreateNew(temp);if(f)break;free(temp);temp=NULL;}if(!f){free(target);return 0;}NativeReplayV4Header_Init(&h);h.identity=*i;h.config=*c;if(!NativeReplayV4Header_Validate(&h)||!EH(&h,b)||fwrite(b,1,sizeof(b),f)!=sizeof(b)||!Sync(f)){(void)fclose(f);(void)remove(temp);free(target);free(temp);return 0;}s->stream=f;s->header=h;s->path=target;s->temporaryPath=temp;return 1; }
int NativeReplayV4Record_AppendFrame(struct NativeReplayV4RecordSession *s,const struct NativeReplayV4Frame *f) { struct NativeReplayV4Header h;uint8_t b[NATIVE_REPLAY_V4_FRAME_BYTES];uint64_t n;if(!s||s->failed||s->finalized||!S(s->stream)||!f)return 0;if(f->replayFrame!=s->nextFrame||s->header.frameCount==UINT32_MAX){s->failed=1;return 0;}h=s->header;h.frameCount++;if(!NativeReplayV4File_ExpectedLength(h.frameCount,&n)||!EF(&s->header,f,b)||Fault(NATIVE_REPLAY_V4_FILE_TEST_FAULT_APPEND)||fwrite(b,1,sizeof(b),S(s->stream))!=sizeof(b)||!Sync(S(s->stream))){s->failed=1;return 0;}s->header=h;s->nextFrame++;return 1; }
int NativeReplayV4Record_Finalize(struct NativeReplayV4RecordSession *s) { uint8_t b[NATIVE_REPLAY_V4_HEADER_BYTES];struct NativeReplayV4Header h;uint64_t got,want;FILE *f;if(!s||s->failed||s->finalized||!S(s->stream)||!s->path||!s->temporaryPath)return 0;if(!Length(S(s->stream),&got)||!NativeReplayV4File_ExpectedLength(s->header.frameCount,&want)||got!=want||!Sync(S(s->stream))){s->failed=1;return 0;}f=S(s->stream);s->stream=NULL;if(!Close(f)){s->failed=1;return 0;}h=s->header;h.flags|=NATIVE_REPLAY_V4_HEADER_FLAG_FINALIZED;if(Fault(NATIVE_REPLAY_V4_FILE_TEST_FAULT_REWRITE)||!EH(&h,b)||(f=fopen(s->temporaryPath,"rb+"))==NULL){s->failed=1;return 0;}if(!Seek(f,0)||fwrite(b,1,sizeof(b),f)!=sizeof(b)||!Sync(f)){(void)fclose(f);s->failed=1;return 0;}if(!Close(f)){s->failed=1;return 0;}if(!Replace(s->temporaryPath,s->path)){s->failed=1;return 0;}s->header=h;s->finalized=1;free(s->path);free(s->temporaryPath);s->path=NULL;s->temporaryPath=NULL;return 1; }
void NativeReplayV4Playback_Init(struct NativeReplayV4PlaybackSession *s) { if(s)memset(s,0,sizeof(*s)); }
void NativeReplayV4Playback_Close(struct NativeReplayV4PlaybackSession *s) { if(!s)return;if(S(s->stream))(void)fclose(S(s->stream));memset(s,0,sizeof(*s)); }
int NativeReplayV4Playback_Open(struct NativeReplayV4PlaybackSession *s,const char *p,const struct NativeIdentityV1 *i,const struct NativeMatchConfigV1 *c,struct NativeReplayV4Header *out) { uint8_t b[NATIVE_REPLAY_V4_HEADER_BYTES];struct NativeCodecReader r;struct NativeReplayV4Header h;FILE *f;uint64_t got,want;if(!s||s->stream||!p||!i||!c||!out)return 0;f=fopen(p,"rb");if(!f||!Length(f,&got)||fread(b,1,sizeof(b),f)!=sizeof(b)){if(f)(void)fclose(f);return 0;}NativeCodecReader_Init(&r,b,sizeof(b));if(!NativeReplayV4Header_Decode(&r,i,&h)||NativeCodecReader_Remaining(&r)||!EqConfig(&h.config,c)||!NativeReplayV4File_ExpectedLength(h.frameCount,&want)||got!=want){(void)fclose(f);return 0;}s->stream=f;s->header=h;s->expectedIdentity=*i;*out=h;return 1; }
int NativeReplayV4Playback_ReadNext(struct NativeReplayV4PlaybackSession *s,struct NativeReplayV4Frame *f) { uint8_t b[NATIVE_REPLAY_V4_FRAME_BYTES];struct NativeCodecReader r;struct NativeReplayV4Frame d;if(!s||!f||!S(s->stream)||s->failed)return NATIVE_REPLAY_V4_READ_ERROR;if(s->nextFrame==s->header.frameCount)return NATIVE_REPLAY_V4_READ_EOF;if(fread(b,1,sizeof(b),S(s->stream))!=sizeof(b)){s->failed=1;return NATIVE_REPLAY_V4_READ_ERROR;}NativeCodecReader_Init(&r,b,sizeof(b));if(!NativeReplayV4Frame_Decode(&r,&s->header,&s->expectedIdentity,&d)||NativeCodecReader_Remaining(&r)||d.replayFrame!=s->nextFrame){s->failed=1;return NATIVE_REPLAY_V4_READ_ERROR;}*f=d;s->nextFrame++;return NATIVE_REPLAY_V4_READ_FRAME; }
