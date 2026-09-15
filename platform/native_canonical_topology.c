#include "platform/native_canonical_topology.h"

#include <string.h>

static uint64_t Digest(const uint8_t *bytes,size_t size)
{
	struct NativeCodecDigest64 digest;
	NativeCodecDigest64_Init(&digest); NativeCodecDigest64_Update(&digest,bytes,size); return digest.value;
}
static int ExactSize(uint32_t count,size_t unit,size_t *out)
{
	if(!out||count>SIZE_MAX/unit)return 0;*out=(size_t)count*unit;return 1;
}
static int Index(uint8_t value,uint32_t count) { return value==NATIVE_CANONICAL_TOPOLOGY_RESTART_ABSENT||value<count; }
static int AvailableInput(const struct NativeCanonicalTopologyV1Input *in)
{
	size_t restartSize,navSize;
	if(in->quadCount>NATIVE_CANONICAL_TOPOLOGY_MAX_QUAD_COUNT||in->restartCount>NATIVE_CANONICAL_TOPOLOGY_MAX_RESTART_COUNT||
		!ExactSize(in->restartCount,NATIVE_CANONICAL_TOPOLOGY_RESTART_BYTES,&restartSize)||in->quadCheckpointSize!=in->quadCount||
		in->restartStreamSize!=restartSize||(in->quadCount!=0&&!in->quadCheckpoints)||(in->restartCount!=0&&!in->restartStream))return 0;
	for(uint32_t i=0;i<in->quadCount;i++)if(!Index(in->quadCheckpoints[i],in->restartCount))return 0;
	for(uint32_t i=0;i<in->restartCount;i++)for(uint32_t edge=0;edge<4;edge++)if(!Index(in->restartStream[i*NATIVE_CANONICAL_TOPOLOGY_RESTART_BYTES+8u+edge],in->restartCount))return 0;
	for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
	{
		if(in->navPointCounts[path]>NATIVE_CANONICAL_TOPOLOGY_MAX_NAV_POINT_COUNT||
			!ExactSize(in->navPointCounts[path],NATIVE_CANONICAL_TOPOLOGY_NAV_FRAME_BYTES,&navSize)||navSize>SIZE_MAX-NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES||
			in->navStreamSizes[path]!=NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES+navSize||!in->navStreams[path])return 0;
	}
	return 1;
}
void NativeCanonicalTopologyV1_Init(struct NativeCanonicalTopologyV1 *topology)
{
	uint64_t empty;
	if(!topology)return;memset(topology,0,sizeof(*topology));topology->version=NATIVE_CANONICAL_TOPOLOGY_VERSION;
	topology->navPathCount=NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;empty=Digest(NULL,0);
	topology->quadCheckpointDigest=empty;topology->restartGraphDigest=empty;
	for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++)topology->navPathDigest[i]=empty;
	topology->fullStreamDigest=empty;
}
int NativeCanonicalTopologyV1_FromNormativeStreams(struct NativeCanonicalTopologyV1 *topology,const struct NativeCanonicalTopologyV1Input *input)
{
	struct NativeCanonicalTopologyV1 candidate;struct NativeCodecDigest64 full;
	if(!topology||!input||(input->flags&~NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE)!=0)return 0;
	NativeCanonicalTopologyV1_Init(&candidate);
	if(input->flags==0)
	{
		if(input->levelID!=0||input->quadCount!=0||input->restartCount!=0||input->quadCheckpoints||input->quadCheckpointSize||input->restartStream||input->restartStreamSize)return 0;
		for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++)if(input->navStreams[i]||input->navStreamSizes[i]||input->navPointCounts[i])return 0;
		*topology=candidate;return 1;
	}
	if(!AvailableInput(input))return 0;
	candidate.flags=input->flags;candidate.levelID=input->levelID;candidate.quadCount=input->quadCount;candidate.restartCount=input->restartCount;
	memcpy(candidate.navPointCounts,input->navPointCounts,sizeof(candidate.navPointCounts));
	candidate.quadCheckpointDigest=Digest(input->quadCheckpoints,input->quadCheckpointSize);candidate.restartGraphDigest=Digest(input->restartStream,input->restartStreamSize);
	NativeCodecDigest64_Init(&full);NativeCodecDigest64_Update(&full,input->quadCheckpoints,input->quadCheckpointSize);NativeCodecDigest64_Update(&full,input->restartStream,input->restartStreamSize);
	for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++){candidate.navPathDigest[i]=Digest(input->navStreams[i],input->navStreamSizes[i]);NativeCodecDigest64_Update(&full,input->navStreams[i],input->navStreamSizes[i]);}
	candidate.fullStreamDigest=full.value;*topology=candidate;return 1;
}
int NativeCanonicalTopologyV1_Validate(const struct NativeCanonicalTopologyV1 *t)
{
	uint64_t empty;
	if(!t||t->version!=NATIVE_CANONICAL_TOPOLOGY_VERSION||(t->flags&~NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE)!=0||t->navPathCount!=NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT||t->quadCount>NATIVE_CANONICAL_TOPOLOGY_MAX_QUAD_COUNT||t->restartCount>NATIVE_CANONICAL_TOPOLOGY_MAX_RESTART_COUNT)return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++)if(t->navPointCounts[i]>NATIVE_CANONICAL_TOPOLOGY_MAX_NAV_POINT_COUNT)return 0;
	if(t->flags!=0)return 1;empty=Digest(NULL,0);
	if(t->levelID||t->quadCount||t->restartCount||t->quadCheckpointDigest!=empty||t->restartGraphDigest!=empty||t->fullStreamDigest!=empty)return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++)if(t->navPointCounts[i]||t->navPathDigest[i]!=empty)return 0;return 1;
}
size_t NativeCanonicalTopologyV1_EncodedSize(void) { return NATIVE_CANONICAL_TOPOLOGY_SUMMARY_BYTES; }
int NativeCanonicalTopologyV1_Encode(struct NativeCodecWriter *writer,const struct NativeCanonicalTopologyV1 *t)
{
	struct NativeCodecWriter w;if(!writer||!NativeCanonicalTopologyV1_Validate(t)||writer->failed||writer->offset>writer->capacity||NATIVE_CANONICAL_TOPOLOGY_SUMMARY_BYTES>writer->capacity-writer->offset)return 0;w=*writer;
	if(!NativeCodecWriter_WriteU32(&w,t->version)||!NativeCodecWriter_WriteU32(&w,t->flags)||!NativeCodecWriter_WriteS32(&w,t->levelID)||!NativeCodecWriter_WriteU32(&w,t->quadCount)||!NativeCodecWriter_WriteU32(&w,t->restartCount)||!NativeCodecWriter_WriteU32(&w,t->navPathCount))return 0;
	for(uint32_t i=0;i<3;i++)if(!NativeCodecWriter_WriteU32(&w,t->navPointCounts[i]))return 0;
	if(!NativeCodecWriter_WriteU64(&w,t->quadCheckpointDigest)||!NativeCodecWriter_WriteU64(&w,t->restartGraphDigest))return 0;
	for(uint32_t i=0;i<3;i++)if(!NativeCodecWriter_WriteU64(&w,t->navPathDigest[i]))return 0;
	if(!NativeCodecWriter_WriteU64(&w,t->fullStreamDigest))return 0;*writer=w;return 1;
}
int NativeCanonicalTopologyV1_Decode(struct NativeCodecReader *reader,struct NativeCanonicalTopologyV1 *t)
{
	struct NativeCodecReader r;struct NativeCanonicalTopologyV1 c;if(!reader||!t||reader->failed||reader->offset>reader->size||NATIVE_CANONICAL_TOPOLOGY_SUMMARY_BYTES>reader->size-reader->offset)return 0;r=*reader;memset(&c,0,sizeof(c));
	if(!NativeCodecReader_ReadU32(&r,&c.version)||!NativeCodecReader_ReadU32(&r,&c.flags)||!NativeCodecReader_ReadS32(&r,&c.levelID)||!NativeCodecReader_ReadU32(&r,&c.quadCount)||!NativeCodecReader_ReadU32(&r,&c.restartCount)||!NativeCodecReader_ReadU32(&r,&c.navPathCount))return 0;
	for(uint32_t i=0;i<3;i++)if(!NativeCodecReader_ReadU32(&r,&c.navPointCounts[i]))return 0;
	if(!NativeCodecReader_ReadU64(&r,&c.quadCheckpointDigest)||!NativeCodecReader_ReadU64(&r,&c.restartGraphDigest))return 0;
	for(uint32_t i=0;i<3;i++)if(!NativeCodecReader_ReadU64(&r,&c.navPathDigest[i]))return 0;
	if(!NativeCodecReader_ReadU64(&r,&c.fullStreamDigest)||!NativeCanonicalTopologyV1_Validate(&c))return 0;*reader=r;*t=c;return 1;
}
