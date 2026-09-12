#ifndef PLATFORM_NATIVE_REPLAY_V3_H
#define PLATFORM_NATIVE_REPLAY_V3_H

#include "platform/native_canonical_state_v3.h"
#include "platform/native_replay_v2.h"

#include <stddef.h>
#include <stdint.h>

/* Separate transport: CRV3/CRF3 never reinterprets CRV2/CRF2 bytes. */
#define NATIVE_REPLAY_V3_FILE_MAGIC UINT32_C(0x33565243) /* Little-endian "CRV3". */
#define NATIVE_REPLAY_V3_FRAME_MAGIC UINT32_C(0x33465243) /* Little-endian "CRF3". */
#define NATIVE_REPLAY_V3_FORMAT_VERSION 3u
#define NATIVE_REPLAY_V3_HEADER_BYTES 140u
#define NATIVE_REPLAY_V3_FRAME_BYTES 904u
/* A clear provisional/finalized distinction keeps a prefix recording from
 * ever being accepted as a playable empty replay. */
#define NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED UINT32_C(0x00000001)
#define NATIVE_REPLAY_V3_HEADER_KNOWN_FLAGS NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED

struct NativeReplayV3Header { uint32_t flags; uint32_t frameCount; struct NativeIdentityV1 identity; };
struct NativeReplayV3Frame
{
	uint32_t replayFrame;
	struct NativeReplayV2FrameObservation begin;
	struct NativeReplayV2FrameObservation end;
	uint32_t padCount;
	struct NativeReplayV2Pad pads[NATIVE_REPLAY_V2_PAD_COUNT];
	uint32_t vsyncTotal;
	uint32_t vsyncPacketCount;
	uint16_t vsyncPackets[NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS];
	struct NativeCanonicalStateV3 canonical;
};

void NativeReplayV3Header_Init(struct NativeReplayV3Header *header);
int NativeReplayV3Header_Validate(const struct NativeReplayV3Header *header);
size_t NativeReplayV3Header_EncodedSize(void);
size_t NativeReplayV3Frame_EncodedSize(void);
int NativeReplayV3_StreamSize(const struct NativeReplayV3Header *header, size_t *sizeOut);
int NativeReplayV3Header_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV3Header *header);
int NativeReplayV3Header_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV3Header *header);
int NativeReplayV3Frame_Validate(const struct NativeReplayV3Header *header, const struct NativeReplayV3Frame *frame);
int NativeReplayV3Frame_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV3Header *header, const struct NativeReplayV3Frame *frame);
int NativeReplayV3Frame_Decode(struct NativeCodecReader *reader, const struct NativeReplayV3Header *header, const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV3Frame *frame);

#endif
