#ifndef PLATFORM_NATIVE_CANONICAL_TOPOLOGY_H
#define PLATFORM_NATIVE_CANONICAL_TOPOLOGY_H

#include "platform/native_canonical_codec.h"

/* A standalone, pointer-free description of level topology.  This is not a
 * game-source extractor and is deliberately not part of any state/replay
 * schema.  All multi-byte bytes in the normative streams are little-endian. */
#define NATIVE_CANONICAL_TOPOLOGY_VERSION UINT32_C(1)
#define NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE UINT32_C(1)
#define NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT 3u
#define NATIVE_CANONICAL_TOPOLOGY_SUMMARY_BYTES 84u
#define NATIVE_CANONICAL_TOPOLOGY_MAX_QUAD_COUNT 32766u
#define NATIVE_CANONICAL_TOPOLOGY_MAX_RESTART_COUNT 255u
#define NATIVE_CANONICAL_TOPOLOGY_MAX_NAV_POINT_COUNT 32766u
#define NATIVE_CANONICAL_TOPOLOGY_RESTART_BYTES 12u
#define NATIVE_CANONICAL_TOPOLOGY_NAV_PREFIX_BYTES 68u
#define NATIVE_CANONICAL_TOPOLOGY_NAV_FRAME_BYTES 20u
#define NATIVE_CANONICAL_TOPOLOGY_RESTART_ABSENT UINT8_C(0xff)

/* quadCheckpoints is one restart index per quad, in quad index order.
 * restartStream is restartCount records: s16 pos[3], u16 distance-to-finish,
 * then forward/left/backward/right u8 restart indices.  Each nav stream is
 * s32 first-node-Y, rampPhys1[16] s16, rampPhys2[16] s16, then pointCount
 * NavFrame values (s16 pos[3], u8 rot[4], s16 xyz/xz/flags/path-change,
 * u8 go-back/special-bits) in point index order. */
struct NativeCanonicalTopologyV1Input
{
	uint32_t flags;
	int32_t levelID;
	uint32_t quadCount;
	uint32_t restartCount;
	const uint8_t *quadCheckpoints;
	size_t quadCheckpointSize;
	const uint8_t *restartStream;
	size_t restartStreamSize;
	const uint8_t *navStreams[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
	size_t navStreamSizes[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
	uint32_t navPointCounts[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
};

struct NativeCanonicalTopologyV1
{
	uint32_t version;
	uint32_t flags;
	int32_t levelID;
	uint32_t quadCount;
	uint32_t restartCount;
	uint32_t navPathCount;
	uint32_t navPointCounts[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
	uint64_t quadCheckpointDigest;
	uint64_t restartGraphDigest;
	uint64_t navPathDigest[NATIVE_CANONICAL_TOPOLOGY_NAV_PATH_COUNT];
	uint64_t fullStreamDigest;
};

void NativeCanonicalTopologyV1_Init(struct NativeCanonicalTopologyV1 *topology);
int NativeCanonicalTopologyV1_FromNormativeStreams(struct NativeCanonicalTopologyV1 *topology,
	const struct NativeCanonicalTopologyV1Input *input);
int NativeCanonicalTopologyV1_Validate(const struct NativeCanonicalTopologyV1 *topology);
size_t NativeCanonicalTopologyV1_EncodedSize(void);
int NativeCanonicalTopologyV1_Encode(struct NativeCodecWriter *writer,
	const struct NativeCanonicalTopologyV1 *topology);
int NativeCanonicalTopologyV1_Decode(struct NativeCodecReader *reader,
	struct NativeCanonicalTopologyV1 *topology);

#endif
