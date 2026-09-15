#ifndef PLATFORM_NATIVE_REPLAY_V4_H
#define PLATFORM_NATIVE_REPLAY_V4_H

/* CRV4/CRF4 is a value transport only: it has no file/session/runtime user.
 * Header_Validate/Encode permit a provisional header for a future file writer.
 * Header_Decode and Frame_Decode are playback acceptance APIs: they require FINALIZED.
 * Frame count and replay-frame ordering remain file/session responsibilities. */
#include "platform/native_canonical_state_v4.h"
#include "platform/native_match_config.h"
#include "platform/native_replay_v2.h"

#include <stddef.h>
#include <stdint.h>

#define NATIVE_REPLAY_V4_FILE_MAGIC UINT32_C(0x34565243) /* Little-endian "CRV4". */
#define NATIVE_REPLAY_V4_FRAME_MAGIC UINT32_C(0x34465243) /* Little-endian "CRF4". */
#define NATIVE_REPLAY_V4_FORMAT_VERSION 4u
/* The CRV3-compatible 140-byte prefix, then NCV4's immutable config and SHA-256. */
#define NATIVE_REPLAY_V4_HEADER_BYTES 428u
#define NATIVE_REPLAY_V4_FRAME_BYTES 1752u
#define NATIVE_REPLAY_V4_HEADER_FLAG_FINALIZED UINT32_C(0x00000001)
#define NATIVE_REPLAY_V4_HEADER_KNOWN_FLAGS NATIVE_REPLAY_V4_HEADER_FLAG_FINALIZED

struct NativeReplayV4Header {
	uint32_t flags;
	uint32_t frameCount;
	struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 config;
};

struct NativeReplayV4Frame {
	uint32_t replayFrame;
	struct NativeReplayV2FrameObservation begin;
	struct NativeReplayV2FrameObservation end;
	uint32_t padCount;
	struct NativeReplayV2Pad pads[NATIVE_REPLAY_V2_PAD_COUNT];
	uint32_t vsyncTotal;
	uint32_t vsyncPacketCount;
	uint16_t vsyncPackets[NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS];
	struct NativeCanonicalStateV4 canonical;
};

void NativeReplayV4Header_Init(struct NativeReplayV4Header *header);
int NativeReplayV4Header_Validate(const struct NativeReplayV4Header *header);
size_t NativeReplayV4Header_EncodedSize(void);
size_t NativeReplayV4Frame_EncodedSize(void);
int NativeReplayV4_StreamSize(const struct NativeReplayV4Header *header, size_t *sizeOut);
int NativeReplayV4Header_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV4Header *header);
int NativeReplayV4Header_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity,
	struct NativeReplayV4Header *header);
int NativeReplayV4Frame_Validate(const struct NativeReplayV4Header *header, const struct NativeReplayV4Frame *frame);
int NativeReplayV4Frame_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV4Header *header,
	const struct NativeReplayV4Frame *frame);
int NativeReplayV4Frame_Decode(struct NativeCodecReader *reader, const struct NativeReplayV4Header *header,
	const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV4Frame *frame);

#endif
