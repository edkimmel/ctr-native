#include "platform/native_topology_asset_census.h"
#include "platform/native_topology_asset_layout.h"
#include "platform/native_sha256.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct Span { const uint8_t *p; size_t n; };
struct BigEntry { uint32_t offset, size; };

static int SpanAt(struct Span s,size_t off,size_t n,struct Span *out){if(off>s.n||n>s.n-off)return 0;out->p=s.p+off;out->n=n;return 1;}
static uint16_t U16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static int16_t S16(const uint8_t *p){return (int16_t)U16(p);}
static uint32_t U32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int Add(size_t a,size_t b,size_t *o){if(a>(size_t)-1-b)return 0;*o=a+b;return 1;}
static int Mul(size_t a,size_t b,size_t *o){if(a&&b>(size_t)-1/a)return 0;*o=a*b;return 1;}
static int U32Size(uint32_t v,size_t *o){*o=(size_t)v;return (uint32_t)*o==v;}
static int PtrMapHas(const struct Span map,uint32_t slot){uint32_t bytes; if(map.n<4)return 0;bytes=U32(map.p);if((bytes&3u)||bytes>map.n-4)return 0;for(uint32_t i=0;i<bytes;i+=4)if((U32(map.p+4+i)&~3u)==slot)return 1;return 0;}
static int OffsetSpan(const struct Span s,uint32_t off,size_t n,struct Span *out){return SpanAt(s,(size_t)off,n,out);}
static int RawHeader(const uint8_t *p){if(p[0]!=0||p[11]!=0||p[15]!=2)return 0;for(unsigned i=1;i<11;i++)if(p[i]!=0xff)return 0;return 1;}

static int Header(const struct Span big,uint32_t *count,const uint8_t **entries){size_t bytes; if(big.n<8)return 0;*count=U32(big.p+4);if(*count>4096||!Mul(*count,8,&bytes)||bytes>big.n-8)return 0;*entries=big.p+8;return 1;}
static int Entry(const struct Span big,uint32_t index,struct BigEntry *out){uint32_t count;const uint8_t *p;if(!Header(big,&count,&p)||index>=count)return 0;out->offset=U32(p+index*8);out->size=U32(p+index*8+4);return out->size!=0;}
static int CopyPayload(const struct Span raw,uint32_t bigLba,const struct BigEntry *e,uint8_t *dst){uint32_t left=e->size;uint32_t sec=0;while(left){size_t base,bigLbaSize,entryOffset,sector;size_t take=left>2048?2048:left;if(!U32Size(bigLba,&bigLbaSize)||!U32Size(e->offset,&entryOffset)||!U32Size(sec,&sector)||!Add(bigLbaSize,entryOffset,&base)||!Add(base,sector,&base)||!Mul(base,NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES,&base)||base>raw.n||NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET>raw.n-base||take>raw.n-base-NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET||!RawHeader(raw.p+base))return 0;memcpy(dst,raw.p+base+NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET,take);dst+=take;left-=take;sec++;}return 1;}
static int Asset(const struct Span raw,uint32_t bigLba,const struct Span big,uint32_t index,struct Span *out,uint8_t *scratch,size_t scratchN){struct BigEntry e;if(!Entry(big,index,&e)||e.size>scratchN||!CopyPayload(raw,bigLba,&e,scratch))return 0;out->p=scratch;out->n=e.size;return 1;}

static int DecodeLevel(uint32_t level,const struct Span lev,const struct Span ptr,struct NativeTopologyAssetCensusRecord *r){
	struct NativeCanonicalTopologyV1Input in={0}; struct Span mesh,quads,restarts,table,nav[3]; uint8_t qbuf[32766], rbuf[255*12],*nbuf[3]={0}; uint32_t qcount,rcount,ncount[3]={0}; int ok=0;
	if(lev.n<NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET+12||!PtrMapHas(ptr,NATIVE_TOPOLOGY_ASSET_LEVEL_MESH_OFFSET)||!PtrMapHas(ptr,NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET))goto done;
	if(!OffsetSpan(lev,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_MESH_OFFSET),NATIVE_TOPOLOGY_ASSET_MESH_QUAD_POINTER_OFFSET+4,&mesh))goto done;
	qcount=U32(mesh.p+NATIVE_TOPOLOGY_ASSET_MESH_QUAD_COUNT_OFFSET);if(qcount>32766||!PtrMapHas(ptr,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_MESH_OFFSET)+NATIVE_TOPOLOGY_ASSET_MESH_QUAD_POINTER_OFFSET)||!OffsetSpan(lev,U32(mesh.p+NATIVE_TOPOLOGY_ASSET_MESH_QUAD_POINTER_OFFSET),qcount*NATIVE_TOPOLOGY_ASSET_QUAD_BYTES,&quads))goto done;
	for(uint32_t i=0;i<qcount;i++)qbuf[i]=quads.p[i*NATIVE_TOPOLOGY_ASSET_QUAD_BYTES+NATIVE_TOPOLOGY_ASSET_QUAD_CHECKPOINT_OFFSET];
	rcount=U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_RESTART_COUNT_OFFSET);if(rcount>255)goto done;
	if(rcount){uint32_t ro=U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_RESTART_POINTER_OFFSET);if(!PtrMapHas(ptr,NATIVE_TOPOLOGY_ASSET_LEVEL_RESTART_POINTER_OFFSET)||!OffsetSpan(lev,ro,rcount*12,&restarts))goto done;memcpy(rbuf,restarts.p,rcount*12);r->restartAvailable=1;}
	if(!OffsetSpan(lev,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET),12,&table))goto done;
	/* NavHeader.last is a relocated in-memory convenience pointer.  Its on-disc
	 * value is intentionally ignored; header magic/count and exact frame span
	 * are the offline validity contract. */
	for(uint32_t path=0;path<3;path++){uint32_t no=U32(table.p+path*4);struct Span h,frames;size_t frameBytes;if(no==0)continue;if(!PtrMapHas(ptr,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET)+path*4)||!OffsetSpan(lev,no,76,&h)||S16(h.p)!=-0x1303||(ncount[path]=U16(h.p+2))>32766||!Mul(ncount[path],20,&frameBytes)||!OffsetSpan(lev,no+76,frameBytes,&frames))goto done;nbuf[path]=(uint8_t *)malloc(68+frames.n);if(!nbuf[path])goto done;memcpy(nbuf[path],h.p+4,68);memcpy(nbuf[path]+68,frames.p,frames.n);nav[path].p=nbuf[path];nav[path].n=68+frames.n;r->navAvailableMask|=(uint8_t)(1u<<path);}
	/* The V1 normative topology codec requires a prefix stream for every path.
	 * A null retail table entry is represented by an all-zero 68-byte stream
	 * plus navAvailableMask, never by inventing a retail NavHeader. */
	for(uint32_t p=0;p<3;p++)if(!nbuf[p]){nbuf[p]=(uint8_t *)calloc(1,68);if(!nbuf[p])goto done;nav[p].p=nbuf[p];nav[p].n=68;}
	in.flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;in.levelID=(int32_t)level;in.quadCount=qcount;in.restartCount=rcount;in.quadCheckpoints=qbuf;in.quadCheckpointSize=qcount;in.restartStream=rbuf;in.restartStreamSize=rcount*12;for(uint32_t p=0;p<3;p++){in.navStreams[p]=nav[p].p;in.navStreamSizes[p]=nav[p].n;in.navPointCounts[p]=ncount[p];}
	if(!NativeCanonicalTopologyV1_FromNormativeStreams(&r->topology,&in))goto done;r->selectorAddressable=1;ok=1;done:for(uint32_t p=0;p<3;p++)free(nbuf[p]);return ok;
}

static int FindBig(const struct Span raw,uint32_t *lba,uint32_t *size){size_t pvd=(size_t)16*NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES+24;const uint8_t *p;uint32_t rootLba,rootSize;size_t rootSizeBytes,roundedRootSize,rootSectors,rootLbaSize; if(pvd+2048>raw.n||!RawHeader(raw.p+16*2352))return 0;p=raw.p+pvd;if(p[0]!=1||memcmp(p+1,"CD001",5)||p[6]!=1)return 0;rootLba=U32(p+158);rootSize=U32(p+166);if(!U32Size(rootSize,&rootSizeBytes)||!Add(rootSizeBytes,2047,&roundedRootSize))return 0;rootSectors=roundedRootSize/2048;if(!U32Size(rootLba,&rootLbaSize))return 0;for(size_t sec=0;sec<rootSectors;sec++){size_t sector,base;if(!Add(rootLbaSize,sec,&sector)||!Mul(sector,NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES,&base)||!Add(base,NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET,&base)||base>raw.n||2048>raw.n-base||base<NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET||!RawHeader(raw.p+base-NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET))return 0;{size_t pos=0;while(pos<2048){const uint8_t *d=raw.p+base+pos;uint8_t n=d[0];if(!n)break;if(n<34||n>2048-pos)return 0;if(d[32]==13&&!memcmp(d+33,"BIGFILE.BIG;1",13)){*lba=U32(d+2);*size=U32(d+10);return 1;}pos+=n;}}}return 0;}

int NativeTopologyAssetCensus_FromRawMode2(const uint8_t *bytes,size_t size,const uint8_t expected[32],struct NativeTopologyAssetCensus *out){struct Span raw={bytes,size},big,lev,ptr;uint8_t digest[32],*bigbuf=NULL,*levbuf=NULL,*ptrbuf=NULL;uint32_t lba,bigSize,count;const uint8_t *entries;struct NativeSha256 sha;struct NativeTopologyAssetCensus c={0};int ok=0;if(!bytes||!expected||!out||size%2352)return 0;NativeSha256_Init(&sha);NativeSha256_Update(&sha,bytes,size);NativeSha256_Final(&sha,digest);if(memcmp(digest,expected,32))return 0;if(!FindBig(raw,&lba,&bigSize)||bigSize>0x4000)return 0;bigbuf=(uint8_t *)malloc(bigSize);levbuf=(uint8_t *)malloc(0x200000);ptrbuf=(uint8_t *)malloc(0x200000);if(!bigbuf||!levbuf||!ptrbuf)goto done;{struct BigEntry e={0,bigSize};if(!CopyPayload(raw,lba,&e,bigbuf))goto done;}big.p=bigbuf;big.n=bigSize;if(!Header(big,&count,&entries))goto done;c.version=1;c.corpusCount=25;for(uint32_t index=0;index<count;index++){struct BigEntry entry;int selected=0;if(!Entry(big,index,&entry))continue;for(uint32_t id=0;id<25;id++){uint32_t base=(id<18?0:144)+(id<18?id:id-18)*8;if(index==base+1||index==base+2){selected=1;break;}}if(!selected)c.excludedStoredEntryCount++;}for(uint32_t id=0;id<25;id++){uint32_t base=(id<18?0:144)+(id<18?id:id-18)*8;struct BigEntry a,b;struct NativeTopologyAssetCensusRecord *r=&c.records[id];r->levelID=id;r->stored=Entry(big,base+1,&a)&&Entry(big,base+2,&b);if(!r->stored)continue;c.storedCount++;if(!Asset(raw,lba,big,base+1,&lev,levbuf,0x200000)||!Asset(raw,lba,big,base+2,&ptr,ptrbuf,0x200000))continue;if(DecodeLevel(id,lev,ptr,r))c.selectorAddressableCount++;}
	NativeSha256_Init(&sha);NativeSha256_Update(&sha,&c.storedCount,sizeof(c.storedCount));for(uint32_t i=0;i<25;i++)NativeSha256_Update(&sha,&c.records[i].topology.fullStreamDigest,sizeof(uint64_t));NativeSha256_Final(&sha,digest);memcpy(&c.corpusDigest,digest,sizeof(c.corpusDigest));*out=c;ok=1;done:free(bigbuf);free(levbuf);free(ptrbuf);return ok;}

static int Put(char **p,size_t *n,const char *fmt,...){va_list a;int k;va_start(a,fmt);k=vsnprintf(*p,*n,fmt,a);va_end(a);if(k<0||(size_t)k>=*n)return 0;*p+=k;*n-=(size_t)k;return 1;}
int NativeTopologyAssetCensus_ToJson(const struct NativeTopologyAssetCensus *c,char *out,size_t size,size_t *written){char temp[16384],*p=temp;size_t n=sizeof(temp);if(!c||!out||!written||c->version!=1)return 0;if(!Put(&p,&n,"{\"version\":1,\"corpus\":%u,\"stored\":%u,\"selectorAddressable\":%u,\"excludedStoredEntries\":%u,\"digest\":\"%016llx\",\"levels\":[",c->corpusCount,c->storedCount,c->selectorAddressableCount,c->excludedStoredEntryCount,(unsigned long long)c->corpusDigest))return 0;for(uint32_t i=0;i<25;i++){const struct NativeTopologyAssetCensusRecord *r=&c->records[i];if(!Put(&p,&n,"%s{\"level\":%u,\"stored\":%s,\"selectorAddressable\":%s,\"restart\":%s,\"navMask\":%u,\"topologyDigest\":\"%016llx\"}",i?",":"",r->levelID,r->stored?"true":"false",r->selectorAddressable?"true":"false",r->restartAvailable?"true":"false",r->navAvailableMask,(unsigned long long)r->topology.fullStreamDigest))return 0;}if(!Put(&p,&n,"]}\n"))return 0;*written=(size_t)(p-temp);if(*written>size)return 0;memcpy(out,temp,*written);return 1;}
