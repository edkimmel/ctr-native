#include "MainCanonicalTopologyFacts.h"

static void Put16(uint8_t *bytes,uint16_t value)
{
	bytes[0]=(uint8_t)value;bytes[1]=(uint8_t)(value>>8);
}
static void Put32(uint8_t *bytes,uint32_t value)
{
	bytes[0]=(uint8_t)value;bytes[1]=(uint8_t)(value>>8);bytes[2]=(uint8_t)(value>>16);bytes[3]=(uint8_t)(value>>24);
}
static int RestartIndex(uint8_t value,uint32_t count)
{
	return value==NATIVE_CANONICAL_TOPOLOGY_RESTART_ABSENT||value<count;
}
static int NavPathIsZero(const struct MainCanonicalTopologyNavPathFacts *path)
{
	if(path->firstNodeY||path->frames.items||path->frames.count)return 0;
	for(uint32_t i=0;i<16;i++)if(path->rampPhys1[i]||path->rampPhys2[i])return 0;
	return 1;
}
static int Unavailable(const struct MainCanonicalTopologyFacts *facts)
{
	if(facts->levelID||facts->quadCheckpoints.items||facts->quadCheckpoints.count||facts->restarts.items||facts->restarts.count)return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++)if(!NavPathIsZero(&facts->navPaths[i]))return 0;
	return 1;
}
static void DigestUpdate(struct NativeCodecDigest64 *one,struct NativeCodecDigest64 *full,const uint8_t *bytes,size_t size)
{
	NativeCodecDigest64_Update(one,bytes,size);NativeCodecDigest64_Update(full,bytes,size);
}
int MainCanonicalTopologyFacts_ToV1(const struct MainCanonicalTopologyFacts *facts,struct NativeCanonicalTopologyV1 *out)
{
	struct NativeCanonicalTopologyV1 candidate;
	struct NativeCodecDigest64 quad,restart,nav[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT],full;
	uint8_t record[NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES];
	if(!facts||!out||(facts->flags&~NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE)!=0)return 0;
	NativeCanonicalTopologyV1_Init(&candidate);
	if(facts->flags==0)
	{
		if(!Unavailable(facts))return 0;
		*out=candidate;return 1;
	}
	if(facts->quadCheckpoints.count>NATIVE_CANONICAL_TOPOLOGY_MAX_QUAD_COUNT||facts->restarts.count>NATIVE_CANONICAL_TOPOLOGY_MAX_RESTART_COUNT||
		(facts->quadCheckpoints.count&&!facts->quadCheckpoints.items)||(facts->restarts.count&&!facts->restarts.items))return 0;
	for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
		if(facts->navPaths[path].frames.count>NATIVE_CANONICAL_TOPOLOGY_MAX_NAV_POINT_COUNT||
			(facts->navPaths[path].frames.count&&!facts->navPaths[path].frames.items))return 0;
	for(uint32_t i=0;i<facts->quadCheckpoints.count;i++)if(!RestartIndex(facts->quadCheckpoints.items[i].restartIndex,facts->restarts.count))return 0;
	for(uint32_t i=0;i<facts->restarts.count;i++)for(uint32_t edge=0;edge<4;edge++)if(!RestartIndex(facts->restarts.items[i].edgeIndex[edge],facts->restarts.count))return 0;
	candidate.flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;candidate.levelID=facts->levelID;
	candidate.quadCount=facts->quadCheckpoints.count;candidate.restartCount=facts->restarts.count;
	NativeCodecDigest64_Init(&quad);NativeCodecDigest64_Init(&restart);NativeCodecDigest64_Init(&full);
	for(uint32_t i=0;i<facts->quadCheckpoints.count;i++)DigestUpdate(&quad,&full,&facts->quadCheckpoints.items[i].restartIndex,1);
	for(uint32_t i=0;i<facts->restarts.count;i++)
	{
		const struct MainCanonicalTopologyRestartFact *source=&facts->restarts.items[i];
		for(uint32_t axis=0;axis<3;axis++)Put16(record+axis*2,(uint16_t)source->pos[axis]);
		Put16(record+6,source->distance);
		for(uint32_t edge=0;edge<4;edge++)record[8+edge]=source->edgeIndex[edge];
		DigestUpdate(&restart,&full,record,NATIVE_CANONICAL_TOPOLOGY_RESTART_BYTES);
	}
	candidate.quadCheckpointDigest=quad.value;candidate.restartGraphDigest=restart.value;
	for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
	{
		const struct MainCanonicalTopologyNavPathFacts *source=&facts->navPaths[path];
		NativeCodecDigest64_Init(&nav[path]);Put32(record,(uint32_t)source->firstNodeY);
		for(uint32_t i=0;i<16;i++)Put16(record+4+i*2,(uint16_t)source->rampPhys1[i]);
		for(uint32_t i=0;i<16;i++)Put16(record+36+i*2,(uint16_t)source->rampPhys2[i]);
		DigestUpdate(&nav[path],&full,record,NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES);
		for(uint32_t i=0;i<source->frames.count;i++)
		{
			const struct MainCanonicalTopologyNavFrameFact *frame=&source->frames.items[i];
			for(uint32_t axis=0;axis<3;axis++)Put16(record+axis*2,(uint16_t)frame->pos[axis]);
			for(uint32_t rot=0;rot<4;rot++)record[6+rot]=frame->rot[rot];
			Put16(record+10,(uint16_t)frame->distanceXYZ);Put16(record+12,(uint16_t)frame->distanceXZ);
			Put16(record+14,(uint16_t)frame->flags);Put16(record+16,(uint16_t)frame->pathChangeOpcode);
			record[18]=frame->goBackCount;record[19]=frame->specialBits;
			DigestUpdate(&nav[path],&full,record,NATIVE_CANONICAL_TOPOLOGY_NAV_FRAME_BYTES);
		}
		candidate.navPointCounts[path]=source->frames.count;candidate.navPathDigest[path]=nav[path].value;
	}
	candidate.fullStreamDigest=full.value;*out=candidate;return 1;
}
