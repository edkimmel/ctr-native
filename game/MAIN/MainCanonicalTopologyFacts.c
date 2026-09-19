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
static int NavPathReaderIsZero(const struct MainCanonicalTopologyNavPathReader *path)
{
	if(path->firstNodeY||path->frameCount)return 0;
	for(uint32_t i=0;i<16;i++)if(path->rampPhys1[i]||path->rampPhys2[i])return 0;
	return 1;
}
static int ReaderUnavailable(const struct MainCanonicalTopologyFactReader *reader)
{
	if(reader->levelID||reader->quadCount||reader->restartCount)return 0;
	for(uint32_t i=0;i<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;i++)if(!NavPathReaderIsZero(&reader->navPaths[i]))return 0;
	return 1;
}
static void DigestUpdate(struct NativeCodecDigest64 *one,struct NativeCodecDigest64 *full,const uint8_t *bytes,size_t size)
{
	NativeCodecDigest64_Update(one,bytes,size);NativeCodecDigest64_Update(full,bytes,size);
}
int MainCanonicalTopologyFactReader_ToV1(const struct MainCanonicalTopologyFactReader *reader,struct NativeCanonicalTopologyV1 *out)
{
	struct NativeCanonicalTopologyV1 candidate;
	struct NativeCodecDigest64 quad,restart,nav[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT],full;
	uint8_t record[NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES];
	if(!reader||!out||(reader->flags&~NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE)!=0)return 0;
	NativeCanonicalTopologyV1_Init(&candidate);
	if(reader->flags==0)
	{
		if(!ReaderUnavailable(reader))return 0;
		*out=candidate;return 1;
	}
	if(reader->quadCount>NATIVE_CANONICAL_TOPOLOGY_MAX_QUAD_COUNT||reader->restartCount>NATIVE_CANONICAL_TOPOLOGY_MAX_RESTART_COUNT||
		(reader->quadCount&&!reader->readQuadCheckpoint)||(reader->restartCount&&!reader->readRestart))return 0;
	for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
		if(reader->navPaths[path].frameCount>NATIVE_CANONICAL_TOPOLOGY_MAX_NAV_POINT_COUNT||
			(reader->navPaths[path].frameCount&&!reader->readNavFrame))return 0;
	candidate.flags=NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;candidate.levelID=reader->levelID;
	candidate.quadCount=reader->quadCount;candidate.restartCount=reader->restartCount;
	NativeCodecDigest64_Init(&quad);NativeCodecDigest64_Init(&restart);NativeCodecDigest64_Init(&full);
	for(uint32_t i=0;i<reader->quadCount;i++)
	{
		struct MainCanonicalTopologyQuadCheckpointFact source={0};
		if(!reader->readQuadCheckpoint(reader->context,i,&source)||!RestartIndex(source.restartIndex,reader->restartCount))return 0;
		DigestUpdate(&quad,&full,&source.restartIndex,1);
	}
	for(uint32_t i=0;i<reader->restartCount;i++)
	{
		struct MainCanonicalTopologyRestartFact source={0};
		if(!reader->readRestart(reader->context,i,&source))return 0;
		for(uint32_t edge=0;edge<4;edge++)if(!RestartIndex(source.edgeIndex[edge],reader->restartCount))return 0;
		for(uint32_t axis=0;axis<3;axis++)Put16(record+axis*2,(uint16_t)source.pos[axis]);
		Put16(record+6,source.distance);
		for(uint32_t edge=0;edge<4;edge++)record[8+edge]=source.edgeIndex[edge];
		DigestUpdate(&restart,&full,record,NATIVE_CANONICAL_TOPOLOGY_RESTART_BYTES);
	}
	candidate.quadCheckpointDigest=quad.value;candidate.restartGraphDigest=restart.value;
	for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
	{
		const struct MainCanonicalTopologyNavPathReader *source=&reader->navPaths[path];
		NativeCodecDigest64_Init(&nav[path]);Put32(record,(uint32_t)source->firstNodeY);
		for(uint32_t i=0;i<16;i++)Put16(record+4+i*2,(uint16_t)source->rampPhys1[i]);
		for(uint32_t i=0;i<16;i++)Put16(record+36+i*2,(uint16_t)source->rampPhys2[i]);
		DigestUpdate(&nav[path],&full,record,NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES);
		for(uint32_t i=0;i<source->frameCount;i++)
		{
			struct MainCanonicalTopologyNavFrameFact frame={0};
			if(!reader->readNavFrame(reader->context,path,i,&frame))return 0;
			for(uint32_t axis=0;axis<3;axis++)Put16(record+axis*2,(uint16_t)frame.pos[axis]);
			for(uint32_t rot=0;rot<4;rot++)record[6+rot]=frame.rot[rot];
			Put16(record+10,(uint16_t)frame.distanceXYZ);Put16(record+12,(uint16_t)frame.distanceXZ);
			Put16(record+14,(uint16_t)frame.flags);Put16(record+16,(uint16_t)frame.pathChangeOpcode);
			record[18]=frame.goBackCount;record[19]=frame.specialBits;
			DigestUpdate(&nav[path],&full,record,NATIVE_CANONICAL_TOPOLOGY_NAV_FRAME_BYTES);
		}
		candidate.navPointCounts[path]=source->frameCount;candidate.navPathDigest[path]=nav[path].value;
	}
	candidate.fullStreamDigest=full.value;*out=candidate;return 1;
}

static int RangeReadQuadCheckpoint(const void *context,uint32_t index,struct MainCanonicalTopologyQuadCheckpointFact *out)
{
	const struct MainCanonicalTopologyFacts *facts=(const struct MainCanonicalTopologyFacts *)context;
	if(!facts||!out||index>=facts->quadCheckpoints.count||!facts->quadCheckpoints.items)return 0;
	*out=facts->quadCheckpoints.items[index];return 1;
}
static int RangeReadRestart(const void *context,uint32_t index,struct MainCanonicalTopologyRestartFact *out)
{
	const struct MainCanonicalTopologyFacts *facts=(const struct MainCanonicalTopologyFacts *)context;
	if(!facts||!out||index>=facts->restarts.count||!facts->restarts.items)return 0;
	*out=facts->restarts.items[index];return 1;
}
static int RangeReadNavFrame(const void *context,uint32_t path,uint32_t index,struct MainCanonicalTopologyNavFrameFact *out)
{
	const struct MainCanonicalTopologyFacts *facts=(const struct MainCanonicalTopologyFacts *)context;
	if(!facts||!out||path>=NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT||index>=facts->navPaths[path].frames.count||!facts->navPaths[path].frames.items)return 0;
	*out=facts->navPaths[path].frames.items[index];return 1;
}
int MainCanonicalTopologyFacts_ToV1(const struct MainCanonicalTopologyFacts *facts,struct NativeCanonicalTopologyV1 *out)
{
	struct MainCanonicalTopologyFactReader reader={0};
	if(!facts)return 0;
	/* The range form has one additional unavailable-state invariant: borrowed
	 * storage itself is a fact and therefore may not be present when unavailable.
	 * The streaming form deliberately has no such restriction because callbacks
	 * are capabilities, not materialized topology data. */
	if(facts->flags==0)
	{
		if(facts->quadCheckpoints.items||facts->restarts.items)return 0;
		for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)if(facts->navPaths[path].frames.items)return 0;
	}
	reader.flags=facts->flags;reader.levelID=facts->levelID;reader.quadCount=facts->quadCheckpoints.count;reader.restartCount=facts->restarts.count;
	reader.context=facts;reader.readQuadCheckpoint=RangeReadQuadCheckpoint;reader.readRestart=RangeReadRestart;reader.readNavFrame=RangeReadNavFrame;
	for(uint32_t path=0;path<NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT;path++)
	{
		reader.navPaths[path].firstNodeY=facts->navPaths[path].firstNodeY;
		for(uint32_t i=0;i<16;i++){reader.navPaths[path].rampPhys1[i]=facts->navPaths[path].rampPhys1[i];reader.navPaths[path].rampPhys2[i]=facts->navPaths[path].rampPhys2[i];}
		reader.navPaths[path].frameCount=facts->navPaths[path].frames.count;
	}
	return MainCanonicalTopologyFactReader_ToV1(&reader,out);
}
