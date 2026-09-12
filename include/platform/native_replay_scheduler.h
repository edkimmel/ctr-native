#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_H

#include <macros.h>

#if defined(CTR_INTERNAL)
struct NativeCanonicalStateV1;

struct NativeReplaySchedulerFrameInfo
{
	s32 frameTimer;
	s32 frameCounter;
	s32 timer;
	s32 framesInThisLEV;
	s32 elapsedTimeMS;
	s32 msInThisLEV;
	s32 elapsedEventTime;
	s32 mainGameState;
	s32 loadingStage;
	s32 levelID;
	u32 mixRandomNumber;
	u32 audioRNG;
	u32 deadcoed0;
	u32 deadcoed1;
	u32 advRng0;
	u32 advRng1;
};

int NativeReplayScheduler_PrepareReportFromArgs(int argc, char **argv);
int NativeReplayScheduler_ConfigureFromArgs(int argc, char **argv);
void NativeReplayScheduler_Shutdown(void);
int NativeReplayScheduler_RequestStart(void);
int NativeReplayScheduler_RequestStop(void);
int NativeReplayScheduler_BeginFrame(const struct NativeReplaySchedulerFrameInfo *info);
/* False for all presently selectable normal/v1 replay modes. */
int NativeReplayScheduler_RequiresCanonicalState(void);
int NativeReplayScheduler_ConsumeVSyncPacket(int requestedVBlanks, int *emittedVBlanks);
int NativeReplayScheduler_ConsumeFrameElapsedTimeMS(int *elapsedTimeMS);
/* The canonical record is value-validated/copied synchronously if required. */
int NativeReplayScheduler_EndFrame(const struct NativeReplaySchedulerFrameInfo *info, const struct NativeCanonicalStateV1 *canonicalState);
void NativeReplayScheduler_RecordVSyncPacket(int emittedVBlanks);
#endif

#endif
