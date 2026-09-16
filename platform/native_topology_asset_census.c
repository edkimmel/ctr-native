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

/* The game reads eight BIGFILE sectors into a 0x4000-byte allocation.  The
 * census uses that cap only for the header/table, never as the ISO file
 * extent: entries may legitimately point later into BIGFILE.BIG. */
static int Header(const struct Span table,uint32_t *count,const uint8_t **entries){size_t bytes;if(table.n<NATIVE_TOPOLOGY_ASSET_BIG_HEADER_BYTES)return 0;*count=U32(table.p+4);if(!Mul(*count,NATIVE_TOPOLOGY_ASSET_BIG_ENTRY_BYTES,&bytes)||bytes>table.n-NATIVE_TOPOLOGY_ASSET_BIG_HEADER_BYTES)return 0;*entries=table.p+NATIVE_TOPOLOGY_ASSET_BIG_HEADER_BYTES;return 1;}
static int Entry(const struct Span table,uint32_t index,struct BigEntry *out){uint32_t count;const uint8_t *p;if(!Header(table,&count,&p)||index>=count)return 0;out->offset=U32(p+index*NATIVE_TOPOLOGY_ASSET_BIG_ENTRY_BYTES);out->size=U32(p+index*NATIVE_TOPOLOGY_ASSET_BIG_ENTRY_BYTES+4);return out->size!=0;}
static int EntryInExtent(const struct BigEntry *e,size_t extent){size_t offset,end;return U32Size(e->offset,&offset)&&Mul(offset,NATIVE_TOPOLOGY_ASSET_DATA_SECTOR_BYTES,&offset)&&U32Size(e->size,&end)&&Add(offset,end,&end)&&end<=extent;}
static int CopyPayload(const struct Span raw,uint32_t bigLba,const struct BigEntry *e,uint8_t *dst){uint32_t left=e->size;uint32_t sec=0;while(left){size_t base,bigLbaSize,entryOffset,sector;size_t take=left>NATIVE_TOPOLOGY_ASSET_DATA_SECTOR_BYTES?NATIVE_TOPOLOGY_ASSET_DATA_SECTOR_BYTES:left;if(!U32Size(bigLba,&bigLbaSize)||!U32Size(e->offset,&entryOffset)||!U32Size(sec,&sector)||!Add(bigLbaSize,entryOffset,&base)||!Add(base,sector,&base)||!Mul(base,NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES,&base)||base>raw.n||NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET>raw.n-base||take>raw.n-base-NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET||!RawHeader(raw.p+base))return 0;memcpy(dst,raw.p+base+NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET,take);dst+=take;left-=take;sec++;}return 1;}
static int Asset(const struct Span raw,uint32_t bigLba,const struct Span table,uint32_t index,struct Span *out,uint8_t *scratch,size_t scratchN){struct BigEntry e;if(!Entry(table,index,&e)||e.size>scratchN||!CopyPayload(raw,bigLba,&e,scratch))return 0;out->p=scratch;out->n=e.size;return 1;}

static int DecodeLevel(uint32_t level,const struct Span lev,const struct Span map,struct NativeTopologyAssetCensusRecord *r){
	struct NativeCanonicalTopologyV1Input in={0}; struct Span mesh,quads,restarts,table,nav[3]; uint8_t qbuf[32766], rbuf[255*12],*nbuf[3]={0}; uint32_t qcount,rcount,ncount[3]={0}; int ok=0;
	if(lev.n<NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET+12||!PtrMapHas(map,NATIVE_TOPOLOGY_ASSET_LEVEL_MESH_OFFSET)||!PtrMapHas(map,NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET))goto done;
	if(!OffsetSpan(lev,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_MESH_OFFSET),NATIVE_TOPOLOGY_ASSET_MESH_QUAD_POINTER_OFFSET+4,&mesh))goto done;
	qcount=U32(mesh.p+NATIVE_TOPOLOGY_ASSET_MESH_QUAD_COUNT_OFFSET);if(qcount>32766||!PtrMapHas(map,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_MESH_OFFSET)+NATIVE_TOPOLOGY_ASSET_MESH_QUAD_POINTER_OFFSET)||!OffsetSpan(lev,U32(mesh.p+NATIVE_TOPOLOGY_ASSET_MESH_QUAD_POINTER_OFFSET),qcount*NATIVE_TOPOLOGY_ASSET_QUAD_BYTES,&quads))goto done;
	for(uint32_t i=0;i<qcount;i++)qbuf[i]=quads.p[i*NATIVE_TOPOLOGY_ASSET_QUAD_BYTES+NATIVE_TOPOLOGY_ASSET_QUAD_CHECKPOINT_OFFSET];
	rcount=U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_RESTART_COUNT_OFFSET);if(rcount>255)goto done;
	if(rcount){uint32_t ro=U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_RESTART_POINTER_OFFSET);if(!PtrMapHas(map,NATIVE_TOPOLOGY_ASSET_LEVEL_RESTART_POINTER_OFFSET)||!OffsetSpan(lev,ro,rcount*12,&restarts))goto done;memcpy(rbuf,restarts.p,rcount*12);r->restartAvailable=1;}
	if(!OffsetSpan(lev,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET),12,&table))goto done;
	/* NavHeader.last is a relocated in-memory convenience pointer.  Its on-disc
	 * value is intentionally ignored; header magic/count and exact frame span
	 * are the offline validity contract. */
	for(uint32_t path=0;path<3;path++){uint32_t no=U32(table.p+path*4);struct Span h,frames;size_t frameBytes;if(no==0)continue;if(!PtrMapHas(map,U32(lev.p+NATIVE_TOPOLOGY_ASSET_LEVEL_NAV_TABLE_OFFSET)+path*4)||!OffsetSpan(lev,no,76,&h)||S16(h.p)!=-0x1303||(ncount[path]=U16(h.p+2))>32766||!Mul(ncount[path],20,&frameBytes)||!OffsetSpan(lev,no+76,frameBytes,&frames))goto done;nbuf[path]=(uint8_t *)malloc(68+frames.n);if(!nbuf[path])goto done;memcpy(nbuf[path],h.p+4,68);memcpy(nbuf[path]+68,frames.p,frames.n);nav[path].p=nbuf[path];nav[path].n=68+frames.n;r->navAvailableMask|=(uint8_t)(1u<<path);}
	/* The V1 normative topology codec requires a prefix stream for every path.
	 * A null retail table entry is represented by an all-zero 68-byte stream
	 * plus navAvailableMask, never by inventing a retail NavHeader. */
	for(uint32_t p=0;p<3;p++)if(!nbuf[p]){nbuf[p]=(uint8_t *)calloc(1,68);if(!nbuf[p])goto done;nav[p].p=nbuf[p];nav[p].n=68;}
	in.flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;in.levelID=(int32_t)level;in.quadCount=qcount;in.restartCount=rcount;in.quadCheckpoints=qbuf;in.quadCheckpointSize=qcount;in.restartStream=rbuf;in.restartStreamSize=rcount*12;for(uint32_t p=0;p<3;p++){in.navStreams[p]=nav[p].p;in.navStreamSizes[p]=nav[p].n;in.navPointCounts[p]=ncount[p];}
	if(!NativeCanonicalTopologyV1_FromNormativeStreams(&r->topology,&in))goto done;r->lod1Addressable=1;ok=1;done:for(uint32_t p=0;p<3;p++)free(nbuf[p]);return ok;
}

static int FindBig(const struct Span raw,uint32_t *lba,uint32_t *size){size_t pvd=(size_t)16*NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES+24;const uint8_t *p;uint32_t rootLba,rootSize;size_t rootSizeBytes,roundedRootSize,rootSectors,rootLbaSize; if(pvd+2048>raw.n||!RawHeader(raw.p+16*2352))return 0;p=raw.p+pvd;if(p[0]!=1||memcmp(p+1,"CD001",5)||p[6]!=1)return 0;rootLba=U32(p+158);rootSize=U32(p+166);if(!U32Size(rootSize,&rootSizeBytes)||!Add(rootSizeBytes,2047,&roundedRootSize))return 0;rootSectors=roundedRootSize/2048;if(!U32Size(rootLba,&rootLbaSize))return 0;for(size_t sec=0;sec<rootSectors;sec++){size_t sector,base;if(!Add(rootLbaSize,sec,&sector)||!Mul(sector,NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES,&base)||!Add(base,NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET,&base)||base>raw.n||2048>raw.n-base||base<NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET||!RawHeader(raw.p+base-NATIVE_TOPOLOGY_ASSET_RAW_USER_OFFSET))return 0;{size_t pos=0;while(pos<2048){const uint8_t *d=raw.p+base+pos;uint8_t n=d[0];if(!n)break;if(n<34||n>2048-pos)return 0;if(d[32]==13&&!memcmp(d+33,"BIGFILE.BIG;1",13)){*lba=U32(d+2);*size=U32(d+10);return 1;}pos+=n;}}}return 0;}

int NativeTopologyAssetCensus_FromRawMode2(const uint8_t *bytes,size_t size,const uint8_t expected[32],struct NativeTopologyAssetCensus *out){
	struct Span raw={bytes,size},table,loaded,lev,map;uint8_t digest[32],*tableBuf=NULL,*lod1Buf=NULL;uint32_t lba,bigExtent,count;const uint8_t *entries;size_t headerBytes,tableBytes;struct NativeSha256 sha;struct NativeTopologyAssetCensus c={0};int ok=0;
	if(!bytes||!expected||!out||size%NATIVE_TOPOLOGY_ASSET_RAW_SECTOR_BYTES)return 0;
	NativeSha256_Init(&sha);NativeSha256_Update(&sha,bytes,size);NativeSha256_Final(&sha,digest);if(memcmp(digest,expected,32))return 0;
	if(!FindBig(raw,&lba,&bigExtent)||bigExtent<NATIVE_TOPOLOGY_ASSET_BIG_HEADER_BYTES)return 0;
	/* First acquire the fixed header, then exactly enough of the table. */
	tableBuf=(uint8_t *)malloc(NATIVE_TOPOLOGY_ASSET_BIG_HEADER_READ_CAP);if(!tableBuf)goto done;
	{struct BigEntry header={0,NATIVE_TOPOLOGY_ASSET_BIG_HEADER_BYTES};if(!CopyPayload(raw,lba,&header,tableBuf))goto done;}
	count=U32(tableBuf+4);if(!Mul(count,NATIVE_TOPOLOGY_ASSET_BIG_ENTRY_BYTES,&headerBytes)||!Add(NATIVE_TOPOLOGY_ASSET_BIG_HEADER_BYTES,headerBytes,&tableBytes)||tableBytes>bigExtent||tableBytes>NATIVE_TOPOLOGY_ASSET_BIG_HEADER_READ_CAP)goto done;
	{struct BigEntry header={0,(uint32_t)tableBytes};if(!CopyPayload(raw,lba,&header,tableBuf))goto done;}
	table.p=tableBuf;table.n=tableBytes;if(!Header(table,&count,&entries))goto done;
	for(uint32_t index=0;index<count;index++){struct BigEntry entry={U32(entries+index*NATIVE_TOPOLOGY_ASSET_BIG_ENTRY_BYTES),U32(entries+index*NATIVE_TOPOLOGY_ASSET_BIG_ENTRY_BYTES+4)};if(!EntryInExtent(&entry,bigExtent))goto done;}
	lod1Buf=(uint8_t *)malloc(0x200000);if(!lod1Buf)goto done;
	c.version=1;c.corpusCount=NATIVE_TOPOLOGY_ASSET_CORPUS_COUNT;
	for(uint32_t index=0;index<count;index++){struct BigEntry entry;int selected=0;if(!Entry(table,index,&entry))continue;for(uint32_t id=0;id<NATIVE_TOPOLOGY_ASSET_CORPUS_COUNT;id++){uint32_t base=(id<NATIVE_TOPOLOGY_ASSET_RACE_COUNT?0:144)+(id<NATIVE_TOPOLOGY_ASSET_RACE_COUNT?id:id-NATIVE_TOPOLOGY_ASSET_RACE_COUNT)*NATIVE_TOPOLOGY_ASSET_STORED_FILES_PER_LEVEL;if(index==base+NATIVE_TOPOLOGY_ASSET_LOD1_ENVELOPE_SELECTOR_OFFSET){selected=1;break;}}if(!selected)c.excludedStoredEntryCount++;}
	for(uint32_t id=0;id<NATIVE_TOPOLOGY_ASSET_CORPUS_COUNT;id++){
		uint32_t base=(id<NATIVE_TOPOLOGY_ASSET_RACE_COUNT?0:144)+(id<NATIVE_TOPOLOGY_ASSET_RACE_COUNT?id:id-NATIVE_TOPOLOGY_ASSET_RACE_COUNT)*NATIVE_TOPOLOGY_ASSET_STORED_FILES_PER_LEVEL;
		struct BigEntry lod1Entry;struct NativeTopologyAssetCensusRecord *r=&c.records[id];int32_t mapOffset;size_t mapStart,mapBytes;
		r->levelID=id;r->stored=Entry(table,base+NATIVE_TOPOLOGY_ASSET_LOD1_ENVELOPE_SELECTOR_OFFSET,&lod1Entry);if(!r->stored)continue;c.storedCount++;
		if(!Asset(raw,lba,table,base+NATIVE_TOPOLOGY_ASSET_LOD1_ENVELOPE_SELECTOR_OFFSET,&loaded,lod1Buf,0x200000)||loaded.n<4)continue;
		mapOffset=(int32_t)U32(loaded.p);if(mapOffset<0)continue;lev.p=loaded.p+4;lev.n=loaded.n-4;if(!U32Size((uint32_t)mapOffset,&mapStart)||mapStart>lev.n||lev.n-mapStart<NATIVE_TOPOLOGY_ASSET_DRAM_MAP_HEADER_BYTES)continue;
		mapBytes=U32(lev.p+mapStart);if((mapBytes&3u)||mapBytes>lev.n-mapStart-NATIVE_TOPOLOGY_ASSET_DRAM_MAP_HEADER_BYTES)continue;map.p=lev.p+mapStart;map.n=NATIVE_TOPOLOGY_ASSET_DRAM_MAP_HEADER_BYTES+mapBytes;
		if(DecodeLevel(id,lev,map,r))c.lod1AddressableCount++;
	}
	NativeSha256_Init(&sha);NativeSha256_Update(&sha,&c.storedCount,sizeof(c.storedCount));for(uint32_t i=0;i<NATIVE_TOPOLOGY_ASSET_CORPUS_COUNT;i++)NativeSha256_Update(&sha,&c.records[i].topology.fullStreamDigest,sizeof(uint64_t));NativeSha256_Final(&sha,digest);memcpy(&c.corpusDigest,digest,sizeof(c.corpusDigest));*out=c;ok=1;
done:free(tableBuf);free(lod1Buf);return ok;
}

static int Put(char **p,size_t *n,const char *fmt,...){va_list a;int k;va_start(a,fmt);k=vsnprintf(*p,*n,fmt,a);va_end(a);if(k<0||(size_t)k>=*n)return 0;*p+=k;*n-=(size_t)k;return 1;}
int NativeTopologyAssetCensus_ToJson(const struct NativeTopologyAssetCensus *c,char *out,size_t size,size_t *written){char temp[16384],*p=temp;size_t n=sizeof(temp);if(!c||!out||!written||c->version!=1)return 0;if(!Put(&p,&n,"{\"version\":1,\"corpus\":%u,\"stored\":%u,\"lod1Addressable\":%u,\"excludedStoredEntryCount\":%u,\"digest\":\"%016llx\",\"levels\":[",c->corpusCount,c->storedCount,c->lod1AddressableCount,c->excludedStoredEntryCount,(unsigned long long)c->corpusDigest))return 0;for(uint32_t i=0;i<NATIVE_TOPOLOGY_ASSET_CORPUS_COUNT;i++){const struct NativeTopologyAssetCensusRecord *r=&c->records[i];if(!Put(&p,&n,"%s{\"level\":%u,\"stored\":%s,\"lod1Addressable\":%s,\"restart\":%s,\"navMask\":%u,\"topologyDigest\":\"%016llx\"}",i?",":"",r->levelID,r->stored?"true":"false",r->lod1Addressable?"true":"false",r->restartAvailable?"true":"false",r->navAvailableMask,(unsigned long long)r->topology.fullStreamDigest))return 0;}if(!Put(&p,&n,"]}\n"))return 0;*written=(size_t)(p-temp);if(*written>size)return 0;memcpy(out,temp,*written);return 1;}
