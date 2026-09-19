#include "MAIN/MainCanonicalTopologyFacts.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)

static void Put16(uint8_t *p,uint16_t value) { p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8); }
static void Put32(uint8_t *p,uint32_t value) { p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24); }
static int Same(const struct NativeCanonicalTopologyV1 *a,const struct NativeCanonicalTopologyV1 *b)
{
	if(a->version!=b->version||a->flags!=b->flags||a->levelID!=b->levelID||a->quadCount!=b->quadCount||a->restartCount!=b->restartCount||a->navPathCount!=b->navPathCount||a->quadCheckpointDigest!=b->quadCheckpointDigest||a->restartGraphDigest!=b->restartGraphDigest||a->fullStreamDigest!=b->fullStreamDigest)return 0;
	for(uint32_t i=0;i<3;i++)if(a->navPointCounts[i]!=b->navPointCounts[i]||a->navPathDigest[i]!=b->navPathDigest[i])return 0;
	return 1;
}
static void Fixture(struct MainCanonicalTopologyFacts *facts,
	struct MainCanonicalTopologyQuadCheckpointFact quads[2],struct MainCanonicalTopologyRestartFact restarts[2],
	struct MainCanonicalTopologyNavFrameFact frames[3][2])
{
	memset(facts,0,sizeof(*facts));memset(quads,0,2*sizeof(*quads));memset(restarts,0,2*sizeof(*restarts));memset(frames,0,3*2*sizeof(**frames));
	facts->flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;facts->levelID=-77;facts->quadCheckpoints.items=quads;facts->quadCheckpoints.count=2;facts->restarts.items=restarts;facts->restarts.count=2;
	quads[0].restartIndex=1;quads[1].restartIndex=0xff;
	restarts[0].pos[0]=-2;restarts[0].pos[1]=0x1234;restarts[0].pos[2]=-3;restarts[0].distance=0xabcd;restarts[0].edgeIndex[0]=1;restarts[0].edgeIndex[1]=0xff;restarts[0].edgeIndex[2]=0;restarts[0].edgeIndex[3]=0xff;
	restarts[1].pos[0]=4;restarts[1].pos[1]=-5;restarts[1].pos[2]=6;restarts[1].distance=7;restarts[1].edgeIndex[0]=0;restarts[1].edgeIndex[1]=1;restarts[1].edgeIndex[2]=0xff;restarts[1].edgeIndex[3]=0xff;
	for(uint32_t path=0;path<3;path++)
	{
		facts->navPaths[path].firstNodeY=(int32_t)(-100-path);facts->navPaths[path].rampPhys1[0]=(int16_t)(-7-path);facts->navPaths[path].rampPhys2[15]=(int16_t)(0x2345+path);
		facts->navPaths[path].frames.items=frames[path];facts->navPaths[path].frames.count=path==0?2:1;
		frames[path][0].pos[0]=-1;frames[path][0].pos[1]=2;frames[path][0].pos[2]=-3;frames[path][0].rot[0]=1;frames[path][0].rot[1]=2;frames[path][0].rot[2]=3;frames[path][0].rot[3]=4;
		frames[path][0].distanceXYZ=-4;frames[path][0].distanceXZ=5;frames[path][0].flags=-6;frames[path][0].pathChangeOpcode=7;frames[path][0].goBackCount=8;frames[path][0].specialBits=9;
		frames[path][1].pos[0]=10;frames[path][1].specialBits=11;
	}
}
static int Normative(const struct MainCanonicalTopologyFacts *facts,struct NativeCanonicalTopologyV1 *out)
{
	uint8_t quad[2],restart[24],nav[3][108];struct NativeCanonicalTopologyV1Input in={0};
	if(facts->quadCheckpoints.count!=2||facts->restarts.count!=2)return 0;
	in.flags=facts->flags;in.levelID=facts->levelID;in.quadCount=2;in.restartCount=2;in.quadCheckpoints=quad;in.quadCheckpointSize=2;in.restartStream=restart;in.restartStreamSize=24;
	for(uint32_t i=0;i<2;i++){const struct MainCanonicalTopologyRestartFact *r=&facts->restarts.items[i];for(uint32_t j=0;j<3;j++)Put16(restart+i*12+j*2,(uint16_t)r->pos[j]);Put16(restart+i*12+6,r->distance);for(uint32_t j=0;j<4;j++)restart[i*12+8+j]=r->edgeIndex[j];quad[i]=facts->quadCheckpoints.items[i].restartIndex;}
	for(uint32_t path=0;path<3;path++)
	{
		const struct MainCanonicalTopologyNavPathFacts *p=&facts->navPaths[path];Put32(nav[path],(uint32_t)p->firstNodeY);for(uint32_t i=0;i<16;i++){Put16(nav[path]+4+i*2,(uint16_t)p->rampPhys1[i]);Put16(nav[path]+36+i*2,(uint16_t)p->rampPhys2[i]);}
		in.navStreams[path]=nav[path];in.navPointCounts[path]=p->frames.count;in.navStreamSizes[path]=68+p->frames.count*20;
		for(uint32_t i=0;i<p->frames.count;i++){const struct MainCanonicalTopologyNavFrameFact *f=&p->frames.items[i];uint8_t *b=nav[path]+68+i*20;for(uint32_t j=0;j<3;j++)Put16(b+j*2,(uint16_t)f->pos[j]);for(uint32_t j=0;j<4;j++)b[6+j]=f->rot[j];Put16(b+10,(uint16_t)f->distanceXYZ);Put16(b+12,(uint16_t)f->distanceXZ);Put16(b+14,(uint16_t)f->flags);Put16(b+16,(uint16_t)f->pathChangeOpcode);b[18]=f->goBackCount;b[19]=f->specialBits;}
	}
	return NativeCanonicalTopologyV1_FromNormativeStreams(out,&in);
}
struct StreamingFixture
{
	const struct MainCanonicalTopologyFacts *facts;
	uint32_t quadCalls,restartCalls,navCalls;
	uint32_t failKind,failPath,failIndex;
	uint32_t partialKind,partialIndex;
	uint8_t trace[16];
	uint32_t traceCount;
};
static void Trace(struct StreamingFixture *f,uint8_t value)
{
	if(f->traceCount<sizeof(f->trace))f->trace[f->traceCount++]=value;
}
static int TraceIs(const struct StreamingFixture *f,const uint8_t *expected,uint32_t count)
{
	return f->traceCount==count&&memcmp(f->trace,expected,count)==0;
}
static int ReadQuad(const void *context,uint32_t index,struct MainCanonicalTopologyQuadCheckpointFact *out)
{
	struct StreamingFixture *f=(struct StreamingFixture *)context;
	f->quadCalls++;Trace(f,(uint8_t)(0x10u+index));
	if(f->failKind==1&&f->failIndex==index)return 0;
	if(!out||index>=f->facts->quadCheckpoints.count)return 0;
	*out=f->facts->quadCheckpoints.items[index];return 1;
}
static int ReadRestart(const void *context,uint32_t index,struct MainCanonicalTopologyRestartFact *out)
{
	struct StreamingFixture *f=(struct StreamingFixture *)context;
	f->restartCalls++;Trace(f,(uint8_t)(0x20u+index));
	if(f->failKind==2&&f->failIndex==index)return 0;
	if(!out||index>=f->facts->restarts.count)return 0;
	if(f->partialKind==2&&f->partialIndex==index){out->pos[0]=f->facts->restarts.items[index].pos[0];return 1;}
	*out=f->facts->restarts.items[index];return 1;
}
static int ReadNav(const void *context,uint32_t path,uint32_t index,struct MainCanonicalTopologyNavFrameFact *out)
{
	struct StreamingFixture *f=(struct StreamingFixture *)context;
	f->navCalls++;Trace(f,(uint8_t)(0x40u+path*4u+index));
	if(f->failKind==3&&f->failPath==path&&f->failIndex==index)return 0;
	if(!out||path>=3||index>=f->facts->navPaths[path].frames.count)return 0;
	*out=f->facts->navPaths[path].frames.items[index];return 1;
}
static void MakeReader(const struct MainCanonicalTopologyFacts *facts,struct StreamingFixture *stream,struct MainCanonicalTopologyFactReader *reader)
{
	memset(stream,0,sizeof(*stream));memset(reader,0,sizeof(*reader));stream->facts=facts;
	reader->flags=facts->flags;reader->levelID=facts->levelID;reader->quadCount=facts->quadCheckpoints.count;reader->restartCount=facts->restarts.count;
	reader->context=stream;reader->readQuadCheckpoint=ReadQuad;reader->readRestart=ReadRestart;reader->readNavFrame=ReadNav;
	for(uint32_t path=0;path<3;path++)
	{
		reader->navPaths[path].firstNodeY=facts->navPaths[path].firstNodeY;
		memcpy(reader->navPaths[path].rampPhys1,facts->navPaths[path].rampPhys1,sizeof(reader->navPaths[path].rampPhys1));
		memcpy(reader->navPaths[path].rampPhys2,facts->navPaths[path].rampPhys2,sizeof(reader->navPaths[path].rampPhys2));
		reader->navPaths[path].frameCount=facts->navPaths[path].frames.count;
	}
}
static int TestRichAndEndian(void)
{
	struct MainCanonicalTopologyFacts facts;struct MainCanonicalTopologyQuadCheckpointFact quads[2];struct MainCanonicalTopologyRestartFact restarts[2];struct MainCanonicalTopologyNavFrameFact frames[3][2];struct NativeCanonicalTopologyV1 adapter,norm;
	Fixture(&facts,quads,restarts,frames);CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&adapter));CHECK(Normative(&facts,&norm));CHECK(Same(&adapter,&norm));
	/* Negative and high-bit fields are deliberately present above; changing order changes the digest. */
	frames[0][0].pos[0]=-2;CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&norm));CHECK(norm.navPathDigest[0]!=adapter.navPathDigest[0]&&norm.fullStreamDigest!=adapter.fullStreamDigest);frames[0][0].pos[0]=-1;
	facts.navPaths[1].rampPhys1[0]=-9;CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&norm));CHECK(norm.navPathDigest[1]!=adapter.navPathDigest[1]);
	return 0;
}
static int TestUnavailableAndZeroFrames(void)
{
	struct MainCanonicalTopologyFacts facts={0};struct NativeCanonicalTopologyV1 out,before,empty;struct MainCanonicalTopologyNavFrameFact dummy;
	CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&out));NativeCanonicalTopologyV1_Init(&empty);CHECK(Same(&out,&empty));before=out;
	facts.navPaths[0].firstNodeY=1;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));facts.navPaths[0].firstNodeY=0;facts.quadCheckpoints.items=(const void *)&dummy;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));
	memset(&facts,0,sizeof(facts));facts.flags=1;facts.levelID=12;CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&out));CHECK(out.flags==1&&out.navPointCounts[0]==0&&out.navPointCounts[1]==0&&out.navPointCounts[2]==0);
	return 0;
}
static int TestRejectionsAtomic(void)
{
	struct MainCanonicalTopologyFacts facts;struct MainCanonicalTopologyQuadCheckpointFact quads[2];struct MainCanonicalTopologyRestartFact restarts[2];struct MainCanonicalTopologyNavFrameFact frames[3][2];struct NativeCanonicalTopologyV1 out,before;
	Fixture(&facts,quads,restarts,frames);CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&out));before=out;
	facts.quadCheckpoints.items=NULL;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));facts.quadCheckpoints.items=quads;
	facts.navPaths[1].frames.items=NULL;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));facts.navPaths[1].frames.items=frames[1];
	quads[0].restartIndex=2;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));quads[0].restartIndex=1;
	restarts[0].edgeIndex[0]=2;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));restarts[0].edgeIndex[0]=1;
	facts.quadCheckpoints.count=32767;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));facts.quadCheckpoints.count=2;
	facts.restarts.count=256;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));facts.restarts.count=2;
	facts.navPaths[2].frames.count=32767;CHECK(!MainCanonicalTopologyFacts_ToV1(&facts,&out)&&Same(&out,&before));
	return 0;
}
static int TestStreamingReaderEquivalenceAndBoundedReads(void)
{
	struct MainCanonicalTopologyFacts facts;struct MainCanonicalTopologyQuadCheckpointFact quads[2];struct MainCanonicalTopologyRestartFact restarts[2];struct MainCanonicalTopologyNavFrameFact frames[3][2];
	struct StreamingFixture stream;struct MainCanonicalTopologyFactReader reader;struct NativeCanonicalTopologyV1 range,streamed;
	Fixture(&facts,quads,restarts,frames);MakeReader(&facts,&stream,&reader);
	CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&range));CHECK(MainCanonicalTopologyFactReader_ToV1(&reader,&streamed));CHECK(Same(&range,&streamed));
	CHECK(stream.quadCalls==2&&stream.restartCalls==2&&stream.navCalls==4);
	CHECK(TraceIs(&stream,(const uint8_t[]){0x10u,0x11u,0x20u,0x21u,0x40u,0x41u,0x44u,0x48u},8u));
	return 0;
}
static int TestStreamingFailuresAndValidationAtomic(void)
{
	struct MainCanonicalTopologyFacts facts;struct MainCanonicalTopologyQuadCheckpointFact quads[2];struct MainCanonicalTopologyRestartFact restarts[2];struct MainCanonicalTopologyNavFrameFact frames[3][2];
	struct StreamingFixture stream;struct MainCanonicalTopologyFactReader reader;struct NativeCanonicalTopologyV1 out,before,empty;
	Fixture(&facts,quads,restarts,frames);CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&out));before=out;
	MakeReader(&facts,&stream,&reader);stream.failKind=1;stream.failIndex=1;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&memcmp(&out,&before,sizeof(out))==0&&stream.quadCalls==2&&stream.restartCalls==0&&stream.navCalls==0&&TraceIs(&stream,(const uint8_t[]){0x10u,0x11u},2u));
	MakeReader(&facts,&stream,&reader);stream.failKind=2;stream.failIndex=1;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&memcmp(&out,&before,sizeof(out))==0&&stream.quadCalls==2&&stream.restartCalls==2&&stream.navCalls==0&&TraceIs(&stream,(const uint8_t[]){0x10u,0x11u,0x20u,0x21u},4u));
	MakeReader(&facts,&stream,&reader);stream.failKind=3;stream.failPath=0;stream.failIndex=1;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&memcmp(&out,&before,sizeof(out))==0&&stream.quadCalls==2&&stream.restartCalls==2&&stream.navCalls==2&&TraceIs(&stream,(const uint8_t[]){0x10u,0x11u,0x20u,0x21u,0x40u,0x41u},6u));
	MakeReader(&facts,&stream,&reader);reader.readQuadCheckpoint=NULL;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&stream.quadCalls==0&&stream.restartCalls==0&&stream.navCalls==0);
	MakeReader(&facts,&stream,&reader);reader.navPaths[1].frameCount=NATIVE_CANONICAL_TOPOLOGY_MAX_NAV_POINT_COUNT+1;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&stream.quadCalls==0&&stream.restartCalls==0&&stream.navCalls==0);
	MakeReader(&facts,&stream,&reader);reader.flags=2u;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&stream.quadCalls==0&&stream.restartCalls==0&&stream.navCalls==0);
	MakeReader(&facts,&stream,&reader);quads[0].restartIndex=2;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before));quads[0].restartIndex=1;
	MakeReader(&facts,&stream,&reader);memset(&reader.navPaths,0,sizeof(reader.navPaths));reader.flags=0;reader.levelID=0;reader.quadCount=0;reader.restartCount=0;NativeCanonicalTopologyV1_Init(&empty);CHECK(MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&empty)&&stream.quadCalls==0&&stream.restartCalls==0&&stream.navCalls==0);before=out;
	reader.levelID=1;CHECK(!MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&Same(&out,&before)&&stream.quadCalls==0&&stream.restartCalls==0&&stream.navCalls==0);
	MakeReader(&facts,&stream,&reader);reader.flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;reader.quadCount=0;reader.restartCount=0;memset(reader.navPaths,0,sizeof(reader.navPaths));CHECK(MainCanonicalTopologyFactReader_ToV1(&reader,&out)&&out.flags==NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE&&stream.quadCalls==0&&stream.restartCalls==0&&stream.navCalls==0);
	Fixture(&facts,quads,restarts,frames);MakeReader(&facts,&stream,&reader);stream.partialKind=2;stream.partialIndex=0;CHECK(MainCanonicalTopologyFactReader_ToV1(&reader,&out));restarts[0].pos[1]=0;restarts[0].pos[2]=0;restarts[0].distance=0;memset(restarts[0].edgeIndex,0,sizeof(restarts[0].edgeIndex));CHECK(MainCanonicalTopologyFacts_ToV1(&facts,&empty)&&Same(&out,&empty));
	return 0;
}
int main(void)
{
	if(TestRichAndEndian()||TestUnavailableAndZeroFrames()||TestRejectionsAtomic()||TestStreamingReaderEquivalenceAndBoundedReads()||TestStreamingFailuresAndValidationAtomic())return 1;
	puts("main_canonical_topology_facts_test: passed");return 0;
}
