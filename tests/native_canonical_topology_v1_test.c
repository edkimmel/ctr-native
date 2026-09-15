#include "platform/native_canonical_topology.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)

static void Put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void Input(struct NativeCanonicalTopologyV1Input *in,uint8_t quad[2],uint8_t restart[24],uint8_t nav[3][88])
{
	memset(in,0,sizeof(*in));memset(quad,0,2);memset(restart,0,24);memset(nav,0,3*88);
	in->flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;in->levelID=-7;in->quadCount=2;in->restartCount=2;
	quad[0]=0;quad[1]=NATIVE_CANONICAL_TOPOLOGY_RESTART_ABSENT;in->quadCheckpoints=quad;in->quadCheckpointSize=2;
	/* The four edge bytes are forward, left, backward, right. */
	restart[8]=1;restart[9]=0xff;restart[10]=0;restart[11]=0xff;restart[20]=0;restart[21]=0xff;restart[22]=1;restart[23]=0xff;
	in->restartStream=restart;in->restartStreamSize=24;
	for(uint32_t i=0;i<3;i++){in->navStreams[i]=nav[i];in->navStreamSizes[i]=68;}
	/* path 0 has one fully explicit 20-byte frame. */
	in->navPointCounts[0]=1;in->navStreamSizes[0]=88;Put16(nav[0]+68,UINT16_C(0xfffe));nav[0][74]=1;nav[0][75]=2;Put16(nav[0]+78,3);Put16(nav[0]+80,4);Put16(nav[0]+82,5);Put16(nav[0]+84,UINT16_C(0xfff9));nav[0][86]=6;nav[0][87]=7;
}
static int Equal(const struct NativeCanonicalTopologyV1 *a,const struct NativeCanonicalTopologyV1 *b) { return memcmp(a,b,sizeof(*a))==0; }
static int TestAvailableAndLayout(void)
{
	struct NativeCanonicalTopologyV1Input in;struct NativeCanonicalTopologyV1 value,decoded;uint8_t quad[2],restart[24],nav[3][88],bytes[84];struct NativeCodecWriter w;struct NativeCodecReader r;
	Input(&in,quad,restart,nav);CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&value,&in));CHECK(NativeCanonicalTopologyV1_Validate(&value));CHECK(NativeCanonicalTopologyV1_EncodedSize()==84);
	CHECK(value.version==1&&value.flags==1&&value.levelID==-7&&value.quadCount==2&&value.restartCount==2&&value.navPathCount==3&&value.navPointCounts[0]==1&&value.navPointCounts[1]==0);
	NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);CHECK(NativeCanonicalTopologyV1_Encode(&w,&value)&&w.offset==84);
	CHECK(bytes[0]==1&&bytes[4]==1&&bytes[8]==0xf9&&bytes[9]==0xff&&bytes[10]==0xff&&bytes[11]==0xff&&bytes[12]==2&&bytes[16]==2&&bytes[20]==3&&bytes[24]==1);
	NativeCodecReader_Init(&r,bytes,sizeof(bytes));CHECK(NativeCanonicalTopologyV1_Decode(&r,&decoded)&&r.offset==84&&Equal(&value,&decoded));
	/* Every detailed group has its own digest and the full digest changes too. */
	{struct NativeCanonicalTopologyV1 changed;quad[0]=1;CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&changed,&in));CHECK(changed.quadCheckpointDigest!=value.quadCheckpointDigest&&changed.fullStreamDigest!=value.fullStreamDigest);quad[0]=0;restart[0]=1;CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&changed,&in));CHECK(changed.restartGraphDigest!=value.restartGraphDigest&&changed.fullStreamDigest!=value.fullStreamDigest);restart[0]=0;nav[1][0]=1;CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&changed,&in));CHECK(changed.navPathDigest[1]!=value.navPathDigest[1]&&changed.fullStreamDigest!=value.fullStreamDigest);}
	return 0;
}
static int TestBoundsAndAtomicity(void)
{
	struct NativeCanonicalTopologyV1Input in;struct NativeCanonicalTopologyV1 out,before;uint8_t quad[2],restart[24],nav[3][88];
	Input(&in,quad,restart,nav);CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in));before=out;
	quad[0]=2;CHECK(!NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in)&&Equal(&out,&before));quad[0]=0;
	restart[8]=2;CHECK(!NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in)&&Equal(&out,&before));restart[8]=1;
	in.navStreamSizes[2]=67;CHECK(!NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in)&&Equal(&out,&before));in.navStreamSizes[2]=68;
	in.navPointCounts[0]=32767;CHECK(!NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in)&&Equal(&out,&before));in.navPointCounts[0]=1;
	in.restartCount=256;CHECK(!NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in)&&Equal(&out,&before));in.restartCount=2;
	/* 0xff is the only valid absent restart edge/checkpoint representation. */
	quad[1]=0xff;restart[9]=0xff;CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&out,&in));
	return 0;
}
static int TestCodecFailureAtomicity(void)
{
	struct NativeCanonicalTopologyV1Input in;struct NativeCanonicalTopologyV1 value,out,before,invalid;uint8_t quad[2],restart[24],nav[3][88],bytes[84],buffer[84];struct NativeCodecWriter writer,writerBefore;struct NativeCodecReader reader,readerBefore;struct NativeCodecDigest64 digest,digestBefore;
	Input(&in,quad,restart,nav);CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&value,&in));
	/* Encode preflights both capacity and the value before touching caller output. */
	memset(buffer,0xa5,sizeof(buffer));NativeCodecDigest64_Init(&digest);digestBefore=digest;NativeCodecWriter_Init(&writer,buffer,sizeof(buffer)-1,&digest);writerBefore=writer;CHECK(!NativeCanonicalTopologyV1_Encode(&writer,&value));CHECK(memcmp(&writer,&writerBefore,sizeof(writer))==0&&memcmp(&digest,&digestBefore,sizeof(digest))==0);for(size_t i=0;i<sizeof(buffer);i++)CHECK(buffer[i]==0xa5);
	invalid=value;invalid.version=2;NativeCodecWriter_Init(&writer,buffer,sizeof(buffer),&digest);writerBefore=writer;CHECK(!NativeCanonicalTopologyV1_Encode(&writer,&invalid));CHECK(memcmp(&writer,&writerBefore,sizeof(writer))==0&&memcmp(&digest,&digestBefore,sizeof(digest))==0);
	NativeCodecWriter_Init(&writer,bytes,sizeof(bytes),NULL);CHECK(NativeCanonicalTopologyV1_Encode(&writer,&value));
	/* Decode rejects short input without advancing the reader or replacing the destination. */
	before=value;before.levelID=99;NativeCodecReader_Init(&reader,bytes,sizeof(bytes)-1);readerBefore=reader;out=before;CHECK(!NativeCanonicalTopologyV1_Decode(&reader,&out));CHECK(memcmp(&reader,&readerBefore,sizeof(reader))==0&&Equal(&out,&before));
	return 0;
}
static int RejectMalformed(const uint8_t *bytes)
{
	struct NativeCanonicalTopologyV1 out,before;struct NativeCodecReader reader,readerBefore;
	NativeCanonicalTopologyV1_Init(&before);before.levelID=99;out=before;NativeCodecReader_Init(&reader,bytes,NATIVE_CANONICAL_TOPOLOGY_SUMMARY_BYTES);readerBefore=reader;
	CHECK(!NativeCanonicalTopologyV1_Decode(&reader,&out));CHECK(memcmp(&reader,&readerBefore,sizeof(reader))==0&&Equal(&out,&before));return 0;
}
static int TestMalformedEncodedInput(void)
{
	struct NativeCanonicalTopologyV1Input in;struct NativeCanonicalTopologyV1 value;uint8_t quad[2],restart[24],nav[3][88],bytes[84],bad[84];struct NativeCodecWriter writer;
	Input(&in,quad,restart,nav);CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&value,&in));NativeCodecWriter_Init(&writer,bytes,sizeof(bytes),NULL);CHECK(NativeCanonicalTopologyV1_Encode(&writer,&value));
	memcpy(bad,bytes,sizeof(bad));bad[0]=2;CHECK(!RejectMalformed(bad));
	memcpy(bad,bytes,sizeof(bad));bad[4]=2;CHECK(!RejectMalformed(bad));
	memcpy(bad,bytes,sizeof(bad));Put16(bad+12,UINT16_C(0x7fff));CHECK(!RejectMalformed(bad));
	memcpy(bad,bytes,sizeof(bad));Put16(bad+16,256);CHECK(!RejectMalformed(bad));
	memcpy(bad,bytes,sizeof(bad));bad[20]=2;CHECK(!RejectMalformed(bad));
	memcpy(bad,bytes,sizeof(bad));Put16(bad+24,UINT16_C(0x7fff));CHECK(!RejectMalformed(bad));
	return 0;
}
static int TestUnavailableExact(void)
{
	struct NativeCanonicalTopologyV1Input in={0};struct NativeCanonicalTopologyV1 a,b;uint8_t bytes[84];struct NativeCodecWriter w;
	CHECK(NativeCanonicalTopologyV1_FromNormativeStreams(&a,&in));NativeCanonicalTopologyV1_Init(&b);CHECK(Equal(&a,&b)&&NativeCanonicalTopologyV1_Validate(&a));
	NativeCodecWriter_Init(&w,bytes,sizeof(bytes),NULL);CHECK(NativeCanonicalTopologyV1_Encode(&w,&a));CHECK(bytes[0]==1&&bytes[20]==3);for(size_t i=4;i<20;i++)CHECK(bytes[i]==0);for(size_t i=24;i<36;i++)CHECK(bytes[i]==0);
	in.navStreams[0]=(const uint8_t *)"x";CHECK(!NativeCanonicalTopologyV1_FromNormativeStreams(&a,&in));return 0;
}
int main(void) { if(TestAvailableAndLayout()||TestBoundsAndAtomicity()||TestCodecFailureAtomicity()||TestMalformedEncodedInput()||TestUnavailableExact())return 1;puts("native_canonical_topology_v1_test: passed");return 0; }
