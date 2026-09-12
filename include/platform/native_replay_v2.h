#ifndef PLATFORM_NATIVE_REPLAY_V2_H
#define PLATFORM_NATIVE_REPLAY_V2_H

#include "platform/native_canonical_state.h"

#include <stddef.h>
#include <stdint.h>

/* Explicit v2 envelope; it is intentionally unrelated to replay v1's CTRR/RFRM structs. */
#define NATIVE_REPLAY_V2_FILE_MAGIC UINT32_C(0x32565243) /* Little-endian "CRV2". */
#define NATIVE_REPLAY_V2_FRAME_MAGIC UINT32_C(0x32465243) /* Little-endian "CRF2". */
#define NATIVE_REPLAY_V2_FORMAT_VERSION 2u
#define NATIVE_REPLAY_V2_PROFILE_NTSC_U 1u
#define NATIVE_REPLAY_V2_TICK_RATE 30u
#define NATIVE_REPLAY_V2_ELAPSED_TICK_MS 32u
#define NATIVE_REPLAY_V2_VBLANK_CYCLES UINT64_C(897619)
#define NATIVE_REPLAY_V2_GPU_CLOCK_HZ UINT64_C(53693175)
#define NATIVE_REPLAY_V2_PAD_COUNT 4u
#define NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS 64u
#define NATIVE_REPLAY_V2_HEADER_BYTES 140u
#define NATIVE_REPLAY_V2_FRAME_BYTES 616u
/* Record_Open writes flags=0.  Only a successfully finalized file may carry
 * this bit, and Playback_Open rejects every non-finalized header. */
#define NATIVE_REPLAY_V2_HEADER_FLAG_FINALIZED UINT32_C(0x00000001)
#define NATIVE_REPLAY_V2_HEADER_KNOWN_FLAGS NATIVE_REPLAY_V2_HEADER_FLAG_FINALIZED

/* Mirrors the current scheduler observations by value, without importing its native layout. */
struct NativeReplayV2FrameObservation
{
	int32_t frameTimer;
	int32_t frameCounter;
	int32_t timer;
	int32_t framesInThisLEV;
	int32_t elapsedTimeMS;
	int32_t msInThisLEV;
	int32_t elapsedEventTime;
	int32_t mainGameState;
	int32_t loadingStage;
	int32_t levelID;
	uint32_t mixRandomNumber;
	uint32_t audioRNG;
	uint32_t deadcoed0;
	uint32_t deadcoed1;
	uint32_t advRng0;
	uint32_t advRng1;
};

/* Exactly the consumed PSX-shaped fields; native reserved bytes are excluded. */
struct NativeReplayV2Pad
{
	uint8_t status;
	uint8_t id;
	uint8_t buttons[2];
	uint8_t analog[4];
	uint8_t connected;
};

struct NativeReplayV2Header
{
	uint32_t flags;
	uint32_t frameCount;
	struct NativeIdentityV1 identity;
};

struct NativeReplayV2Frame
{
	uint32_t replayFrame;
	struct NativeReplayV2FrameObservation begin;
	struct NativeReplayV2FrameObservation end;
	uint32_t padCount;
	struct NativeReplayV2Pad pads[NATIVE_REPLAY_V2_PAD_COUNT];
	uint32_t vsyncTotal;
	uint32_t vsyncPacketCount;
	uint16_t vsyncPackets[NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS];
	struct NativeCanonicalStateV1 canonical;
};

void NativeReplayV2Header_Init(struct NativeReplayV2Header *header);
int NativeReplayV2Header_Validate(const struct NativeReplayV2Header *header);
size_t NativeReplayV2Header_EncodedSize(void);
size_t NativeReplayV2Frame_EncodedSize(void);
int NativeReplayV2_StreamSize(const struct NativeReplayV2Header *header, size_t *sizeOut);
int NativeReplayV2Header_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV2Header *header);
int NativeReplayV2Header_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity,
                                struct NativeReplayV2Header *header);
int NativeReplayV2Frame_Validate(const struct NativeReplayV2Header *header, const struct NativeReplayV2Frame *frame);
int NativeReplayV2Frame_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV2Header *header,
                               const struct NativeReplayV2Frame *frame);
int NativeReplayV2Frame_Decode(struct NativeCodecReader *reader, const struct NativeReplayV2Header *header,
                               const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV2Frame *frame);

#endif
