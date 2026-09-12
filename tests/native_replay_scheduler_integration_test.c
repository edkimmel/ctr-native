/* Deliberately compile the production scheduler into this executable.  The
 * narrow service fakes below let the test drive BeginFrame/EndFrame/Shutdown
 * and real V3 files without MainMain or a live game. */
#if defined(_WIN32)
#include "platform/native_win32.h"
#undef RECT
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()
#define PATH_BYTES MAX_PATH
#else
#include <unistd.h>
#define PATH_BYTES 512
#endif

#include <common.h>
#include <namespace_Memcard.h>
#include "../platform/native_replay_scheduler.c"

#include <stdarg.h>

#define CHECK(expression)                                                                                                                 \
	do                                                                                                                                    \
	{                                                                                                                                     \
		if (!(expression))                                                                                                                  \
		{                                                                                                                                   \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                            \
			return 1;                                                                                                                         \
		}                                                                                                                                   \
	} while (0)

struct sData sdata_static;
static int s_failCheckpointClose;
static int s_checkpointWrites;

static int TemporaryPath(char path[PATH_BYTES])
{
#if defined(_WIN32)
	char directory[MAX_PATH];
	DWORD length = GetTempPathA(sizeof(directory), directory);
	return (length > 0) && (length < sizeof(directory)) && (GetTempFileNameA(directory, "csi", 0, path) != 0) && (remove(path) == 0);
#else
	char templatePath[] = "/tmp/ctr-scheduler-integration-XXXXXX";
	int descriptor = mkstemp(templatePath);
	if ((descriptor < 0) || (close(descriptor) != 0) || (unlink(templatePath) != 0)) return 0;
	memcpy(path, templatePath, sizeof(templatePath)); return 1;
#endif
}

static char *Owned(const char *text)
{
	size_t bytes = strlen(text) + 1u;
	char *result = (char *)malloc(bytes);
	if (result != NULL) memcpy(result, text, bytes);
	return result;
}

static void MakeIdentity(struct NativeIdentityV1 *identity)
{
	for (uint32_t index = 0; index < NATIVE_IDENTITY_DIGEST_BYTES; index++)
	{
		identity->build[index] = (uint8_t)(index + 3u);
		identity->content[index] = (uint8_t)(0xa0u + index);
	}
}

static int MakeState(const struct NativeIdentityV1 *identity, uint32_t frame, struct NativeCanonicalStateV3 *state)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	NativeCanonicalStateV3_Init(state);
	state->identity = *identity;
	state->frameNumber = frame;
	stream[NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES] = 1;
	return NativeCanonicalDriversV1_FromNormativeStream(&state->drivers, 1u, stream, sizeof(stream)) &&
	       NativeCanonicalStateV3_ComputeDigests(state);
}

static int FileContains(const char *path, const char *needle)
{
	char bytes[512] = {0};
	FILE *file = fopen(path, "rb");
	size_t count;
	if (file == NULL) return 0;
	count = fread(bytes, 1, sizeof(bytes) - 1u, file);
	if (fclose(file) != 0) return 0;
	bytes[count] = '\0';
	return strstr(bytes, needle) != NULL;
}

static int SetupV3Record(char replay[PATH_BYTES], char checkpoint[PATH_BYTES], char metadata[PATH_BYTES])
{
	NativeReplayScheduler_ResetSessionState();
	s_failCheckpointClose = 0;
	s_checkpointWrites = 0;
	CHECK(TemporaryPath(replay) && TemporaryPath(checkpoint) && TemporaryPath(metadata));
	MakeIdentity(&s_v3Identity);
	s_v3IdentityValid = 1;
	NativeReplayV3Record_Init(&s_v3Record);
	CHECK(NativeReplayV3Record_Open(&s_v3Record, replay, &s_v3Identity));
	s_v3Header = s_v3Record.header;
	s_mode = NATIVE_REPLAY_MODE_RECORD_V3;
	s_checkpointPolicy = NATIVE_REPLAY_CHECKPOINT_POLICY_BOOTSTRAP_ONLY;
	s_checkpointWriterOpen = 1;
	s_checkpointWriter.file = (void *)UINTPTR_MAX;
	s_checkpointPayloadSize = 4;
	s_checkpointPayload = (u8 *)malloc(4u);
	CHECK(s_checkpointPayload != NULL);
	s_checkpointPath = Owned(checkpoint);
	s_reportEnabled = 1;
	s_reportV3ReplayPath = Owned(replay);
	s_reportV3CheckpointPath = Owned(checkpoint);
	s_reportV3MetadataPath = Owned(metadata);
	CHECK((s_checkpointPath != NULL) && (s_reportV3ReplayPath != NULL) && (s_reportV3CheckpointPath != NULL) && (s_reportV3MetadataPath != NULL));
	return 0;
}

static void TeardownPaths(const char *replay, const char *checkpoint, const char *metadata)
{
	(void)remove(replay); (void)remove(checkpoint); (void)remove(metadata);
	NativeReplayScheduler_FreeReportPaths();
	s_reportEnabled = 0;
}

static int TestProductionRecordLifecycle(void)
{
	char replay[PATH_BYTES], checkpoint[PATH_BYTES], metadata[PATH_BYTES];
	struct NativeReplaySchedulerFrameInfo info = {0};
	struct NativeReplaySchedulerCanonicalRequest request, requestAgain;
	struct NativeReplaySchedulerCanonicalSubmission submission;
	struct NativeCanonicalStateV3 state;
	struct NativeReplayV3PlaybackSession playback;
	struct NativeReplayV3Header header;

	CHECK(SetupV3Record(replay, checkpoint, metadata) == 0);
	CHECK(NativeReplayScheduler_BeginFrame(&info) == 0 && s_checkpointWrites == 1);
	CHECK(NativeReplayScheduler_GetCanonicalProducerRequest(&request));
	CHECK(NativeReplayScheduler_GetCanonicalProducerRequest(&requestAgain));
	CHECK(request.requiredKind == NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3 && request.replayFrame == 0u &&
	      request.restoredThisFrame == 0 && memcmp(&request, &requestAgain, sizeof(request)) == 0);
	CHECK(MakeState(&request.identity, request.replayFrame, &state));
	submission.kind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3;
	submission.state.v3 = &state;
	CHECK(NativeReplayScheduler_EndFrameRequest(&info, &submission) == 0);
	NativeReplayScheduler_Shutdown();
	CHECK(FileContains(metadata, "finalized=1\n") && FileContains(metadata, "frame_count=1\n"));
	NativeReplayV3Playback_Init(&playback);
	CHECK(NativeReplayV3Playback_Open(&playback, replay, &request.identity, &header) && header.frameCount == 1u);
	NativeReplayV3Playback_Close(&playback);
	TeardownPaths(replay, checkpoint, metadata);
	return 0;
}

static int TestProductionFailures(void)
{
	char replay[PATH_BYTES], checkpoint[PATH_BYTES], metadata[PATH_BYTES];
	struct NativeReplaySchedulerFrameInfo info = {0};
	struct NativeReplaySchedulerCanonicalSubmission wrong = { NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V1, {0} };
	struct NativeReplayV3PlaybackSession playback;
	struct NativeReplayV3Header header;

	CHECK(SetupV3Record(replay, checkpoint, metadata) == 0);
	CHECK(NativeReplayScheduler_BeginFrame(&info) == 0);
	CHECK(NativeReplayScheduler_EndFrameRequest(&info, &wrong) == 1 && s_v3RecordPoisoned);
	NativeReplayScheduler_Shutdown();
	CHECK(FileContains(metadata, "finalized=0\n"));
	NativeReplayV3Playback_Init(&playback);
	CHECK(!NativeReplayV3Playback_Open(&playback, replay, &s_v3Identity, &header));
	TeardownPaths(replay, checkpoint, metadata);

	CHECK(SetupV3Record(replay, checkpoint, metadata) == 0);
	CHECK(NativeReplayScheduler_BeginFrame(&info) == 0);
	CHECK(NativeReplayScheduler_BeginFrame(&info) == 1 && s_v3RecordPoisoned);
	NativeReplayScheduler_Shutdown();
	CHECK(FileContains(metadata, "finalized=0\n"));
	TeardownPaths(replay, checkpoint, metadata);

	/* Force the real V3 session's append path to fail after BeginFrame. */
	CHECK(SetupV3Record(replay, checkpoint, metadata) == 0);
	CHECK(NativeReplayScheduler_BeginFrame(&info) == 0);
	{
		struct NativeReplaySchedulerCanonicalRequest request;
		struct NativeCanonicalStateV3 state;
		struct NativeReplaySchedulerCanonicalSubmission correct;
		CHECK(NativeReplayScheduler_GetCanonicalProducerRequest(&request));
		CHECK(MakeState(&request.identity, request.replayFrame, &state));
		correct.kind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3; correct.state.v3 = &state;
		s_v3Record.failed = 1;
		CHECK(NativeReplayScheduler_EndFrameRequest(&info, &correct) == 1 && s_v3RecordPoisoned);
	}
	NativeReplayScheduler_Shutdown();
	CHECK(FileContains(metadata, "finalized=0\n"));
	TeardownPaths(replay, checkpoint, metadata);

	/* A successful accepted frame still cannot seal if checkpoint close fails. */
	CHECK(SetupV3Record(replay, checkpoint, metadata) == 0);
	CHECK(NativeReplayScheduler_BeginFrame(&info) == 0);
	{
		struct NativeReplaySchedulerCanonicalRequest request;
		struct NativeCanonicalStateV3 state;
		struct NativeReplaySchedulerCanonicalSubmission correct;
		CHECK(NativeReplayScheduler_GetCanonicalProducerRequest(&request));
		CHECK(MakeState(&request.identity, request.replayFrame, &state));
		correct.kind = NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3; correct.state.v3 = &state;
		CHECK(NativeReplayScheduler_EndFrameRequest(&info, &correct) == 0);
	}
	s_failCheckpointClose = 1;
	NativeReplayScheduler_Shutdown();
	CHECK(FileContains(metadata, "finalized=0\n"));
	NativeReplayV3Playback_Init(&playback);
	CHECK(!NativeReplayV3Playback_Open(&playback, replay, &s_v3Identity, &header));
	TeardownPaths(replay, checkpoint, metadata);

	/* The inverse mismatch cannot allow a V2 accepted prefix to seal. */
	NativeReplayScheduler_ResetSessionState();
	s_mode = NATIVE_REPLAY_MODE_RECORD_V2; s_beginOpen = 1;
	CHECK(NativeReplayScheduler_EndFrameRequest(&info, &wrong) == 1 && s_v2RecordPoisoned);
	s_mode = NATIVE_REPLAY_MODE_NONE;
	/* Normal/v1 mode must reject, not drop, a forged typed submission. */
	CHECK(NativeReplayScheduler_EndFrameRequest(&info, &wrong) == 1);
	return 0;
}

static int TestProducerRestoreAndReportGetter(void)
{
	struct NativeReplaySchedulerCanonicalRequest request, beforeRequest;
	struct NativeReplaySchedulerV3MismatchReport report, beforeReport;
	NativeReplayScheduler_ResetSessionState();
	MakeIdentity(&s_v3Identity); s_v3IdentityValid = 1;
	s_mode = NATIVE_REPLAY_MODE_PLAYBACK_V3; s_beginOpen = 1; s_replayFrame = 9u; s_restoredBootstrapCheckpointEvent = 1;
	CHECK(NativeReplayScheduler_GetCanonicalProducerRequest(&request));
	beforeRequest = request;
	CHECK(NativeReplayScheduler_GetCanonicalProducerRequest(&request) && request.restoredThisFrame && memcmp(&request, &beforeRequest, sizeof(request)) == 0);
	memset(&s_v3MismatchReport, 0, sizeof(s_v3MismatchReport)); s_v3MismatchReport.padMask = 4u; s_v3MismatchReport.drivers.headerMask = UINT32_MAX;
	s_v3MismatchReportValid = 1;
	CHECK(NativeReplayScheduler_GetV3MismatchReport(&report) && report.padMask == 4u && report.drivers.headerMask == UINT32_MAX);
	beforeReport = report; s_v3MismatchReportValid = 0;
	CHECK(!NativeReplayScheduler_GetV3MismatchReport(&report) && memcmp(&report, &beforeReport, sizeof(report)) == 0);
	s_mode = NATIVE_REPLAY_MODE_NONE;
	return 0;
}

/* Service fakes used by every production scheduler path in this TU. */
int NativeCheckpoint_GetSize(void) { return 4; }
int NativeCheckpoint_Capture(void *dst, int size) { if ((dst == NULL) || (size != 4)) return 0; memset(dst, 0x5a, 4); return 1; }
int NativeCheckpoint_Restore(const void *src, int size) { return (src != NULL) && (size == 4); }
int NativeCheckpointFile_BeginWrite(struct NativeCheckpointFileWriter *writer, const char *path) { if ((writer == NULL) || (path == NULL)) return 0; writer->file = (void *)UINTPTR_MAX; return 1; }
int NativeCheckpointFile_AppendRecord(struct NativeCheckpointFileWriter *writer, const void *payload, int size, u32 index, u32 frame, struct NativeCheckpointFileRecordInfo *info)
{ if ((writer == NULL) || (payload == NULL) || (size != 4)) return 0; s_checkpointWrites++; writer->recordCount++; if (info != NULL) { memset(info, 0, sizeof(*info)); info->checkpointIndex=index; info->replayFrame=frame; } return 1; }
int NativeCheckpointFile_EndWrite(struct NativeCheckpointFileWriter *writer) { return (writer != NULL) && !s_failCheckpointClose; }
int NativeCheckpointFile_Validate(const char *path, struct NativeCheckpointFileRecordInfo *r, int max, int *count) { (void)r; (void)max; if ((path == NULL) || (count == NULL)) return 0; *count=1; return 1; }
int NativeCheckpointFile_ReadRecord(const char *path, u32 index, void *payload, int size, struct NativeCheckpointFileRecordInfo *info)
{ (void)index; if ((path==NULL)||(payload==NULL)||(size!=4)) return 0; memset(payload,0x5a,4); if(info!=NULL)memset(info,0,sizeof(*info)); return 1; }
int NativeCheckpointFile_WriteSingle(const char *p,const void *d,int n,u32 i,u32 f){(void)p;(void)d;(void)n;(void)i;(void)f;return 1;}
int NativeCheckpointFile_ReadSingle(const char *p,void *d,int n,struct NativeCheckpointFileRecordInfo *i){return NativeCheckpointFile_ReadRecord(p,0,d,n,i);}
enum NativeMemcardResult NativeMemcard_SetRoot(const char *p){return p!=NULL?NATIVE_MEMCARD_OK:NATIVE_MEMCARD_IO_ERROR;} void NativeMemcard_ClearRoot(void){}
enum NativeMemcardResult NativeMemcard_CloneRoot(const char *a,const char *b){return(a&&b)?NATIVE_MEMCARD_OK:NATIVE_MEMCARD_IO_ERROR;} enum NativeMemcardResult NativeMemcard_CloneCurrentRoot(const char *p){return p?NATIVE_MEMCARD_OK:NATIVE_MEMCARD_IO_ERROR;} enum NativeMemcardResult NativeMemcard_RemoveRoot(const char *p){return p?NATIVE_MEMCARD_OK:NATIVE_MEMCARD_IO_ERROR;}
void NativeAudio_SetDeterministicRenderMode(int enabled){(void)enabled;}
int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *d,int n){if((d==NULL)||(n!=4))return 0;memset(d,0,sizeof(*d)*4);return 1;} int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *s,int n){return(s!=NULL)&&(n==4);} void Platform_InputClearInstalledPadSnapshots(void){}
int Platform_LogSetPath(const char *p){(void)p;return 1;} const char *Platform_LogGetPath(void){return "";} void Platform_Log(const char *f,...){(void)f;}
int NativeIdentity_BuildKnown(void){return 1;} int NativeIdentity_Get(struct NativeIdentityV1 *i){if(i==NULL)return 0;MakeIdentity(i);return 1;} int NativeState_GetSize(void){return 4;}

int main(void)
{
	if ((TestProductionRecordLifecycle() != 0) || (TestProductionFailures() != 0) || (TestProducerRestoreAndReportGetter() != 0)) return 1;
	puts("native_replay_scheduler_integration_test: passed");
	return 0;
}
