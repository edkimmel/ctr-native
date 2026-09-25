#include "MAIN/MainArcadeRaceLaunchCore.h"
#include "MAIN/MainArcadeRaceSetupCore.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * Race launch decision core unit test (docs/RACE_LAUNCH_MILESTONE.md RL-8..RL-11,
 * slice RL-S7). Every frame goes through Frame(), which checks the rules that
 * hold on all frames: a report at most once per launched race (by launch
 * number) and never with the host off RACING, a WINDOW_TIMEOUT report with
 * race number 0, never both finish and failure; armAndLaunch taking the next
 * launch number, never while an ended race awaits its return step or Disarm
 * and never on either's frame; the pads installed on every frame from Launch
 * until the clear and never after it; one return step per end (every launch
 * and every WINDOW_TIMEOUT owes one), on a stage IDLE or REQUESTED frame, on
 * the report frame itself when its stage allows and no return was already
 * pending, never on a stage OTHER report frame; the clear strictly after the
 * latest return step on a LOADING or idle main-menu frame; the Disarm once per
 * launched race on an idle main-menu frame strictly after the latest return
 * step, naming that race, never with a report, or at once with the return
 * step on an Arm/Launch failure; the finish latch (raceFinishedInput) cleared
 * by a frame off RACING or a START_RACE, set by a finish, and held otherwise;
 * and an idle core owing nothing. Since the Task 8 race plan's LR-S10 part 2
 * it also checks the drive on every frame: race tick 0 opens the drive
 * phase, and a drive step comes only while it is open, on RACING with the
 * setup VALIDATED, numbered 0 on the race tick 0 frame and one more on each
 * later one, with its DriveResult due; the committed pads replace the
 * neutral ones only on a GO frame; every other result, and every other exit
 * from the drive phase, is a driveEnded frame, after which no drive step
 * comes until the next race tick 0. Each test then pins its own frames.
 */

#define CHECK(expression)                                            \
	do                                                               \
	{                                                                \
		if (!(expression))                                           \
		{                                                            \
			fprintf(stderr, "fail %d: %s\n", __LINE__, #expression); \
			return 1;                                                \
		}                                                            \
	} while (0)
#define RUN(call)                                          \
	do                                                     \
	{                                                      \
		if ((call) != 0)                                   \
		{                                                  \
			fprintf(stderr, "  from line %d\n", __LINE__); \
			return 1;                                      \
		}                                                  \
	} while (0)

#define WINDOW_TICKS     MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_WINDOW_TIMEOUT_TICKS
#define VALIDATE_TICKS   MAIN_ARCADE_RACE_LAUNCH_CORE_LAUNCH_VALIDATE_TIMEOUT_TICKS
/* The synthetic races of this test end on this race tick (the drive decides
 * the real length). */
#define DRIVE_TICKS      150u

#define S_IDLE           MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_IDLE
#define S_ARMED          MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_ARMED
#define S_LAUNCHED       MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED
#define S_SEEDED         MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED
#define S_VALIDATED      MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED
#define S_FAILED         MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED

#define ST_IDLE          MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE
#define ST_REQ           MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_REQUESTED
#define ST_OTHER         MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_OTHER

#define R_NONE           0u
#define R_LAUNCHED       MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCHED
#define R_ARM_FAILED     MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_ARM_FAILED
#define R_LAUNCH_FAILED  MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCH_FAILED

#define D_GO             MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_GO
#define D_FINISHED       MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_FINISHED
#define D_FAILED         MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_FAILED
#define D_OUTCOME        MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVE_RESULT_OUTCOME

#define F_NONE           MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_NONE
#define F_ARM            MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_ARM
#define F_LAUNCH         MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_LAUNCH
#define F_WINDOW         MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_WINDOW_TIMEOUT
#define F_VALIDATE       MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_VALIDATE_TIMEOUT
#define F_RACE_TICK      MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_RACE_TICK_TIMEOUT
#define F_SETUP          MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_SETUP_FAILED
#define F_DRIVE          MAIN_ARCADE_RACE_LAUNCH_CORE_FAILURE_DRIVE_FAILED

#define P_IDLE           MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE
#define P_WAIT_WINDOW    MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_WINDOW
#define P_LAUNCH_RESULT  MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_LAUNCH_RESULT
#define P_WAIT_VALIDATED MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_VALIDATED
#define P_WAIT_RACE_TICK MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_WAIT_RACE_TICK
#define P_DRIVE          MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_DRIVE
#define P_ENDED          MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_ENDED

/* The mirrors match the setup's own status values. */
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_IDLE == (unsigned)MAIN_ARCADE_RACE_SETUP_IDLE, "IDLE mirror");
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_ARMED == (unsigned)MAIN_ARCADE_RACE_SETUP_ARMED, "ARMED mirror");
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED == (unsigned)MAIN_ARCADE_RACE_SETUP_LAUNCHED, "LAUNCHED mirror");
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_SEEDED == (unsigned)MAIN_ARCADE_RACE_SETUP_SEEDED, "SEEDED mirror");
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_VALIDATED == (unsigned)MAIN_ARCADE_RACE_SETUP_VALIDATED, "VALIDATED mirror");
_Static_assert(MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_FAILED == (unsigned)MAIN_ARCADE_RACE_SETUP_FAILED, "FAILED mirror");

enum Level
{
	LVL_MENU = 1,
	LVL_PLAN = 2,
	LVL_OTHER = 3
};

typedef struct MainArcadeRaceLaunchCoreInput Input;
typedef struct MainArcadeRaceLaunchCoreOutput Output;

static Input In(enum Level level, uint32_t stage, uint8_t loadingBit, uint32_t status, uint8_t racing)
{
	Input input;

	memset(&input, 0, sizeof(input));
	input.setupStatus = status;
	input.loadingStage = stage;
	input.hostRacing = racing;
	input.onMainMenuLevel = (level == LVL_MENU) ? 1u : 0u;
	input.onPlanLevel = (level == LVL_PLAN) ? 1u : 0u;
	input.loadingBit = loadingBit;
	return input;
}

/* The idle main-menu level, the title window open, the host RACING. */
static Input TitleOpen(uint32_t status)
{
	Input input = In(LVL_MENU, ST_IDLE, 0u, status, 1u);
	input.titleWindowOpen = 1u;
	return input;
}

static Input StartOn(Input input)
{
	input.startRace = 1u;
	return input;
}

static int IdleMainMenu(const Input *input)
{
	return (input->onMainMenuLevel != 0u) && (input->loadingStage == ST_IDLE) && (input->loadingBit == 0u);
}

static int Bit(uint8_t value)
{
	return (value == 0u) || (value == 1u);
}

struct Harness
{
	struct MainArcadeRaceLaunchCore core;
	uint32_t frame;
	uint8_t reported[16]; /* a report per launch number */
	uint8_t padsLive;     /* installed and not yet cleared */
	uint8_t disarmDue;    /* a launched race awaits its Disarm */
	uint8_t returnDue;    /* an ended race awaits its return step */
	uint8_t finishLatch;  /* the expected raceFinishedInput */
	uint32_t disarmRace;  /* the launch number of the race awaiting its Disarm */
	uint32_t returnFrame; /* the frame of the last return step */
	uint32_t disarmFrame; /* the frame of the last Disarm */
	uint32_t launches;
	uint32_t windowTimeouts;
	uint32_t returns;
	uint32_t clears;
	uint32_t disarms;
	uint8_t driveOpen;    /* race tick 0 seen, the drive phase not ended yet */
	uint32_t lastTick;    /* the race tick of the last driveStep */
	uint32_t driveSteps;  /* driveStep frames so far */
	uint32_t commits;     /* installCommitted frames so far */
	uint32_t driveEnds;   /* driveEnded frames so far */
};

static void HarnessInit(struct Harness *h)
{
	memset(h, 0, sizeof(*h));
	MainArcadeRaceLaunchCore_Init(&h->core);
}

/* One frame: Step, then LaunchResult with result when Step set armAndLaunch
 * (result R_NONE means none was expected), or DriveResult with driveResult
 * when Step set driveStep, then the every-frame rules. */
static int FrameDrive(struct Harness *h, const Input *input, uint32_t result, uint32_t driveResult, Output *out)
{
	uint8_t padsBefore = h->padsLive;
	uint8_t returnDueBefore = h->returnDue;
	int stageAllowsReturn = (input->loadingStage == ST_IDLE) || (input->loadingStage == ST_REQ);
	int launchedNow = 0;

	h->frame++;
	memset(out, 0xA5, sizeof(*out));
	CHECK(MainArcadeRaceLaunchCore_Step(&h->core, input, out) == 1);
	if (out->armAndLaunch != 0u)
	{
		CHECK(result != R_NONE);
		/* Only armAndLaunch (and the race number) before the result. */
		CHECK(out->leaveTitle == 0u && out->installPads == 0u && out->clearPads == 0u && out->reportFinished == 0u && out->reportFailure == 0u &&
		      out->requestReturn == 0u && out->disarm == 0u && out->validated == 0u && out->raceTickZero == 0u && out->failure == F_NONE &&
		      out->raceFinishedInput == 0u);
		CHECK(out->driveStep == 0u && out->installCommitted == 0u && out->driveEnded == 0u && out->raceTick == 0u);
		CHECK(h->core.phase == P_LAUNCH_RESULT);
		CHECK(MainArcadeRaceLaunchCore_LaunchResult(&h->core, result, out) == 1);
		CHECK(out->armAndLaunch == 1u);
		launchedNow = (result == R_LAUNCHED);
	}

	/* The drive (LR-S10 part 2): race tick 0 opens the drive phase; every
	 * drive step is on RACING with the setup VALIDATED, numbered 0 on the
	 * race tick 0 frame and one more on each later one, only while the phase
	 * is open, with the neutral pads asked for until its result; a GO result
	 * installs the committed pads instead, and every other result ends the
	 * phase on that frame. */
	if (out->raceTickZero != 0u)
	{
		CHECK(h->driveOpen == 0u);
		CHECK(out->driveStep == 1u);
		h->driveOpen = 1u;
	}
	if (out->driveStep != 0u)
	{
		CHECK(h->driveOpen != 0u);
		CHECK(input->hostRacing == 1u && input->setupStatus == S_VALIDATED);
		CHECK(out->raceTick == ((out->raceTickZero != 0u) ? 0u : h->lastTick + 1u));
		CHECK(out->installPads == 1u && out->installCommitted == 0u && out->driveEnded == 0u);
		CHECK(out->reportFinished == 0u && out->reportFailure == 0u && out->requestReturn == 0u);
		CHECK(h->core.phase == P_DRIVE && h->core.driveDue == 1u && h->core.raceTick == out->raceTick);
		h->lastTick = out->raceTick;
		h->driveSteps++;
		CHECK(MainArcadeRaceLaunchCore_DriveResult(&h->core, driveResult, out) == 1);
		CHECK(out->driveStep == 1u && out->raceTick == h->lastTick);
		CHECK(h->core.driveDue == 0u);
		if (driveResult == D_GO)
		{
			CHECK(out->installCommitted == 1u && out->installPads == 0u && out->driveEnded == 0u && h->core.phase == P_DRIVE);
			CHECK(out->reportFinished == 0u && out->reportFailure == 0u);
		}
		else
		{
			CHECK(out->driveEnded == 1u && out->installCommitted == 0u && out->installPads == 1u && h->core.phase != P_DRIVE);
			CHECK(out->reportFinished == ((driveResult == D_FINISHED) ? 1u : 0u));
			CHECK(out->reportFailure == ((driveResult == D_FAILED) ? 1u : 0u));
			CHECK(out->failure == ((driveResult == D_FAILED) ? F_DRIVE : F_NONE));
		}
	}
	else
	{
		CHECK(out->raceTick == 0u && out->installCommitted == 0u);
		CHECK(h->core.driveDue == 0u);
	}
	if (out->installCommitted != 0u)
	{
		h->commits++;
	}
	if (out->driveEnded != 0u)
	{
		CHECK(h->driveOpen != 0u);
		h->driveOpen = 0u;
		h->driveEnds++;
	}
	/* The drive phase is left only on a driveEnded frame. */
	CHECK((h->core.phase == P_DRIVE) == (h->driveOpen != 0u));

	/* Well-formed. */
	CHECK(Bit(out->armAndLaunch) && Bit(out->leaveTitle) && Bit(out->installPads) && Bit(out->clearPads) && Bit(out->reportFinished) &&
	      Bit(out->reportFailure) && Bit(out->requestReturn) && Bit(out->disarm) && Bit(out->validated) && Bit(out->raceTickZero));
	CHECK(Bit(out->raceFinishedInput) && Bit(out->driveStep) && Bit(out->installCommitted) && Bit(out->driveEnded));
	CHECK(!(out->installPads != 0u && out->installCommitted != 0u));
	CHECK(out->reserved[0] == 0u && out->reserved[1] == 0u);
	CHECK((out->reportFailure != 0u) == (out->failure != F_NONE));
	CHECK(out->raceNumber < 16u);

	/* Rule 9: one report per launched race, never both, never off RACING;
	 * a WINDOW_TIMEOUT concerns a race that never launched (number 0) and
	 * owes a return step (merged with one already due). */
	CHECK(!(out->reportFinished != 0u && out->reportFailure != 0u));
	if ((out->reportFinished != 0u) || (out->reportFailure != 0u))
	{
		CHECK(input->hostRacing != 0u);
		if (out->failure == F_WINDOW)
		{
			CHECK(out->raceNumber == 0u);
			h->windowTimeouts++;
			h->returnDue = 1u;
		}
		else
		{
			CHECK(out->raceNumber != 0u);
			CHECK(h->reported[out->raceNumber] == 0u);
			h->reported[out->raceNumber] = 1u;
		}
	}

	/* The finish latch: cleared by a frame off RACING or a START_RACE, then
	 * set by a finish, and held on every other frame; the core's own field
	 * is the output's. */
	if ((input->hostRacing == 0u) || (input->startRace != 0u))
	{
		h->finishLatch = 0u;
	}
	if (out->reportFinished != 0u)
	{
		h->finishLatch = 1u;
	}
	CHECK(out->raceFinishedInput == h->finishLatch);
	CHECK(h->core.finishedPending == h->finishLatch);

	/* Rule 8: armAndLaunch never while an ended race awaits its return step
	 * or Disarm (so never on either's frame); it takes the next launch
	 * number; every launch owes a return step. */
	if (out->armAndLaunch != 0u)
	{
		CHECK(h->disarmDue == 0u && returnDueBefore == 0u);
		CHECK(h->disarms == 0u || h->disarmFrame < h->frame);
		CHECK(h->returns == 0u || h->returnFrame < h->frame);
		CHECK(IdleMainMenu(input));
		CHECK(out->raceNumber == h->launches + 1u);
		h->launches++;
		h->returnDue = 1u;
	}
	if (launchedNow)
	{
		CHECK(out->leaveTitle == 1u && out->installPads == 1u && out->disarm == 0u && out->reportFailure == 0u && out->requestReturn == 0u);
		h->padsLive = 1u;
		h->disarmDue = 1u;
		h->disarmRace = out->raceNumber;
	}
	else
	{
		CHECK(out->leaveTitle == 0u);
	}

	/* Rule 6 and RL-11: on every report frame, the return step on that frame
	 * when its stage allows and none was already pending; never on a stage
	 * OTHER report frame. */
	if ((out->reportFinished != 0u) || (out->reportFailure != 0u))
	{
		if (stageAllowsReturn && (returnDueBefore == 0u))
		{
			CHECK(out->requestReturn == 1u);
		}
		if (!stageAllowsReturn)
		{
			CHECK(out->requestReturn == 0u);
		}
	}

	/* Rule 6: the return step once per end, on IDLE or REQUESTED. */
	if (out->requestReturn != 0u)
	{
		CHECK(h->returnDue != 0u);
		CHECK(stageAllowsReturn);
		h->returnDue = 0u;
		h->returnFrame = h->frame;
		h->returns++;
	}

	/* Rule 6 and 9: install every frame from Launch until the clear, never
	 * after; the clear strictly after the latest return step, on a LOADING or
	 * idle main-menu frame, exactly once. */
	if (out->clearPads != 0u)
	{
		CHECK(padsBefore != 0u);
		CHECK(h->returnDue == 0u && h->returns != 0u && h->returnFrame < h->frame);
		CHECK(input->loadingBit != 0u || IdleMainMenu(input));
		CHECK(out->installPads == 0u && out->installCommitted == 0u);
		h->padsLive = 0u;
		h->clears++;
	}
	else
	{
		CHECK((uint8_t)(out->installPads | out->installCommitted) == h->padsLive);
	}

	/* Rule 7: Disarm once per launched race, on an idle main-menu frame,
	 * strictly after the latest return step, naming that race, never with a
	 * report; at once, with the return step, for an Arm/Launch failure. */
	if (out->disarm != 0u)
	{
		CHECK(IdleMainMenu(input));
		if (out->armAndLaunch != 0u)
		{
			CHECK(out->reportFailure != 0u && (out->failure == F_ARM || out->failure == F_LAUNCH));
			CHECK(out->requestReturn == 1u && out->installPads == 0u && out->clearPads == 0u);
		}
		else
		{
			CHECK(h->disarmDue != 0u);
			CHECK(out->raceNumber == h->disarmRace);
			CHECK(out->reportFailure == 0u && out->reportFinished == 0u && out->requestReturn == 0u);
			CHECK(h->returnDue == 0u && h->returnFrame < h->frame);
			CHECK(h->padsLive == 0u);
			h->disarmDue = 0u;
		}
		h->disarmFrame = h->frame;
		h->disarms++;
	}

	/* An idle core owes nothing. */
	if (h->core.phase == P_IDLE)
	{
		CHECK(h->returnDue == 0u && h->disarmDue == 0u && h->padsLive == 0u && h->driveOpen == 0u);
		CHECK(h->core.raceNumber == 0u && h->core.returnPending == 0u && h->core.held == 0u && h->core.disarmPending == 0u);
	}
	return 0;
}

/* One frame whose drive step, if any, is GO. */
static int Frame(struct Harness *h, const Input *input, uint32_t result, Output *out)
{
	return FrameDrive(h, input, result, D_GO, out);
}

/* n frames of input with no decision beyond the pad install; *last is the
 * last frame's output. */
static int QuietOut(struct Harness *h, const Input *input, uint32_t n, Output *last)
{
	for (uint32_t i = 0; i < n; i++)
	{
		RUN(Frame(h, input, R_NONE, last));
		CHECK(last->armAndLaunch == 0u && last->clearPads == 0u && last->reportFinished == 0u && last->reportFailure == 0u && last->requestReturn == 0u &&
		      last->disarm == 0u && last->validated == 0u && last->raceTickZero == 0u);
	}
	return 0;
}

static int Quiet(struct Harness *h, const Input *input, uint32_t n)
{
	Output out;

	return QuietOut(h, input, n, &out);
}

/* START_RACE with the title window open: armAndLaunch, then Launch succeeds. */
static int LaunchNow(struct Harness *h, uint32_t race)
{
	Input input = StartOn(TitleOpen(S_IDLE));
	Output out;

	RUN(Frame(h, &input, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.leaveTitle == 1u && out.installPads == 1u);
	CHECK(out.raceNumber == race);
	CHECK(h->core.phase == P_WAIT_VALIDATED);
	return 0;
}

/* After Launch: the requested load, a staged race-track load, VALIDATED at
 * its end (frame 4 after Launch). */
static int LoadToValidated(struct Harness *h, uint32_t race)
{
	Input requested = In(LVL_MENU, ST_REQ, 0u, S_LAUNCHED, 1u);
	Input loading = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
	Input validated = In(LVL_PLAN, ST_OTHER, 1u, S_VALIDATED, 1u);
	Output out;

	RUN(Quiet(h, &requested, 1u));
	RUN(Quiet(h, &loading, 2u));
	RUN(Frame(h, &validated, R_NONE, &out));
	CHECK(out.validated == 1u && out.raceNumber == race && out.raceTickZero == 0u && out.installPads == 1u);
	CHECK(h->core.phase == P_WAIT_RACE_TICK);
	return 0;
}

static int ToRaceTickZero(struct Harness *h, uint32_t race)
{
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;

	RUN(Frame(h, &running, R_NONE, &out));
	CHECK(out.raceTickZero == 1u && out.raceNumber == race && out.driveStep == 1u && out.raceTick == 0u);
	CHECK(out.installCommitted == 1u && out.installPads == 0u);
	CHECK(h->core.phase == P_DRIVE);
	return 0;
}

/* The drive to its finish: GO on the race ticks after the last one up to
 * DRIVE_TICKS - 1, then FINISHED on race tick DRIVE_TICKS, with the return
 * step on that frame (stage IDLE) and the neutral pads installed. */
static int DriveToFinish(struct Harness *h, uint32_t race)
{
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;

	CHECK(h->driveOpen != 0u && h->lastTick < DRIVE_TICKS - 1u);
	RUN(QuietOut(h, &running, DRIVE_TICKS - 1u - h->lastTick, &out));
	CHECK(out.raceTick == DRIVE_TICKS - 1u && out.installCommitted == 1u);
	RUN(FrameDrive(h, &running, R_NONE, D_FINISHED, &out));
	CHECK(out.driveStep == 1u && out.raceTick == DRIVE_TICKS && out.driveEnded == 1u);
	CHECK(out.reportFinished == 1u && out.reportFailure == 0u && out.raceNumber == race);
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.installCommitted == 0u && out.clearPads == 0u && out.disarm == 0u);
	CHECK(h->core.phase == P_ENDED);
	return 0;
}

/* After the return step: a frame still on the race level without LOADING
 * (pads kept), the return load (clear on its first LOADING frame), then the
 * idle main-menu frame (Disarm). */
static int ReturnHome(struct Harness *h, uint32_t status)
{
	Input stillRace = In(LVL_PLAN, ST_REQ, 0u, status, 0u);
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, status, 0u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, status, 0u);
	Output out;

	RUN(Frame(h, &stillRace, R_NONE, &out));
	CHECK(out.clearPads == 0u && out.installPads == 1u && out.disarm == 0u);
	RUN(Frame(h, &returnLoad, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.installPads == 0u && out.disarm == 0u);
	RUN(QuietOut(h, &returnLoad, 3u, &out));
	CHECK(out.installPads == 0u && h->padsLive == 0u);
	RUN(Frame(h, &menuIdle, R_NONE, &out));
	CHECK(out.disarm == 1u && out.clearPads == 0u && out.installPads == 0u);
	CHECK(h->core.phase == P_IDLE);
	return 0;
}

static int AllZero(const void *bytes, size_t size)
{
	const unsigned char *p = (const unsigned char *)bytes;
	for (size_t i = 0; i < size; i++)
		if (p[i] != 0u)
			return 0;
	return 1;
}

/* ---- layout, constants, and argument checks ---- */

static int TestLayout(void)
{
	CHECK(WINDOW_TICKS == 900u);
	CHECK(VALIDATE_TICKS == 1800u);
	CHECK(P_DRIVE == 5u);
	CHECK(D_GO == 1u && D_FINISHED == 2u && D_FAILED == 3u && D_OUTCOME == 4u);
	CHECK(F_DRIVE == 7u);

	CHECK(offsetof(struct MainArcadeRaceLaunchCore, phase) == 0u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, launches) == 4u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, raceNumber) == 8u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, waitTicks) == 12u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, heldTicks) == 16u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, padsInstalled) == 20u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, returnPending) == 21u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, held) == 22u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, disarmPending) == 23u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, launchStage) == 24u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, finishedPending) == 25u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, driveDue) == 26u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, driveStage) == 27u);
	CHECK(offsetof(struct MainArcadeRaceLaunchCore, raceTick) == 28u);
	CHECK(sizeof(struct MainArcadeRaceLaunchCore) == 32u);

	CHECK(offsetof(Input, setupStatus) == 0u);
	CHECK(offsetof(Input, loadingStage) == 4u);
	CHECK(offsetof(Input, startRace) == 8u);
	CHECK(offsetof(Input, hostRacing) == 9u);
	CHECK(offsetof(Input, titleWindowOpen) == 10u);
	CHECK(offsetof(Input, onMainMenuLevel) == 11u);
	CHECK(offsetof(Input, onPlanLevel) == 12u);
	CHECK(offsetof(Input, loadingBit) == 13u);
	CHECK(offsetof(Input, reserved) == 14u);
	CHECK(sizeof(Input) == 16u);

	CHECK(offsetof(Output, failure) == 0u);
	CHECK(offsetof(Output, raceNumber) == 4u);
	CHECK(offsetof(Output, armAndLaunch) == 8u);
	CHECK(offsetof(Output, leaveTitle) == 9u);
	CHECK(offsetof(Output, installPads) == 10u);
	CHECK(offsetof(Output, clearPads) == 11u);
	CHECK(offsetof(Output, reportFinished) == 12u);
	CHECK(offsetof(Output, reportFailure) == 13u);
	CHECK(offsetof(Output, requestReturn) == 14u);
	CHECK(offsetof(Output, disarm) == 15u);
	CHECK(offsetof(Output, validated) == 16u);
	CHECK(offsetof(Output, raceTickZero) == 17u);
	CHECK(offsetof(Output, raceFinishedInput) == 18u);
	CHECK(offsetof(Output, driveStep) == 19u);
	CHECK(offsetof(Output, installCommitted) == 20u);
	CHECK(offsetof(Output, driveEnded) == 21u);
	CHECK(offsetof(Output, reserved) == 22u);
	CHECK(offsetof(Output, raceTick) == 24u);
	CHECK(sizeof(Output) == 28u);
	return 0;
}

static int TestFailureNames(void)
{
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_NONE), "NONE") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_ARM), "ARM") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_LAUNCH), "LAUNCH") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_WINDOW), "WINDOW_TIMEOUT") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_VALIDATE), "VALIDATE_TIMEOUT") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_RACE_TICK), "RACE_TICK_TIMEOUT") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_SETUP), "SETUP_FAILED") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(F_DRIVE), "DRIVE_FAILED") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(8u), "UNKNOWN") == 0);
	CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(0xFFFFFFFFu), "UNKNOWN") == 0);
	return 0;
}

/*
 * The finished-human count (docs/LOCKSTEP_RACE_MILESTONE.md LR-18, LR-59): by
 * slot, 0 .. min(numPlyrCurrGame, 8) - 1, over the finished bit only.
 */
static int TestFinishedHumans(void)
{
	const uint32_t finished = MAIN_ARCADE_RACE_LAUNCH_CORE_ACTION_RACE_FINISHED;
	const uint32_t bot = 0x100000u; /* ACTION_BOT, set by BOTS_Driver_Convert */
	uint32_t flags[MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS];
	uint32_t slot;

	CHECK(MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS == 8u);
	CHECK(finished == 0x2000000u);

	/* Two humans, both finished. */
	memset(flags, 0, sizeof(flags));
	flags[0] = finished;
	flags[1] = finished;
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 2u) == 2u);
	/* Only slots 0 .. numPlyrCurrGame - 1 count. */
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 1u) == 1u);
	flags[0] = 0u;
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 2u) == 1u);
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 1u) == 0u);
	/* A finished bot in slot 3 does not count with two humans. */
	memset(flags, 0, sizeof(flags));
	flags[3] = finished | bot;
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 2u) == 0u);
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 4u) == 1u);
	/* A finished human that retail converted to a bot still counts. */
	flags[1] = finished | bot;
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 2u) == 1u);
	/* An unfinished converted slot (the bot bit alone) does not. */
	flags[0] = bot;
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 2u) == 1u);
	/* No humans: nothing counts. */
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 0u) == 0u);
	/* Every slot finished: numPlyrCurrGame above 8 clamps to 8. */
	for (slot = 0u; slot < MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS; slot++)
	{
		flags[slot] = finished;
	}
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 8u) == 8u);
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 9u) == 8u);
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 0xFFFFFFFFu) == 8u);
	/* NULL. */
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(NULL, 2u) == 0u);
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(NULL, 0u) == 0u);
	/* Every other bit, alone or together, does not count. */
	for (slot = 0u; slot < MAIN_ARCADE_RACE_LAUNCH_CORE_DRIVER_SLOTS; slot++)
	{
		flags[slot] = ~finished;
	}
	CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 8u) == 0u);
	for (slot = 0u; slot < 32u; slot++)
	{
		memset(flags, 0, sizeof(flags));
		flags[0] = 1u << slot;
		CHECK(MainArcadeRaceLaunchCore_FinishedHumans(flags, 1u) == (((1u << slot) == finished) ? 1u : 0u));
	}
	return 0;
}

/* NULLs, out-of-range inputs, and the launch result protocol. */
static int TestArguments(void)
{
	struct MainArcadeRaceLaunchCore core;
	struct MainArcadeRaceLaunchCore before;
	Input input = StartOn(TitleOpen(S_IDLE));
	Output out;

	MainArcadeRaceLaunchCore_Init(NULL);
	memset(&core, 0x5A, sizeof(core));
	MainArcadeRaceLaunchCore_Init(&core);
	CHECK(AllZero(&core, sizeof(core)));

	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_Step(NULL, &input, &out) == 0);
	CHECK(AllZero(&out, sizeof(out)));
	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_Step(&core, NULL, &out) == 0);
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, NULL) == 0);
	CHECK(AllZero(&core, sizeof(core)));

	/* Out-of-range status or stage: refused, state unchanged. */
	input.setupStatus = S_FAILED + 1u;
	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, &out) == 0);
	CHECK(AllZero(&out, sizeof(out)) && AllZero(&core, sizeof(core)));
	input.setupStatus = S_IDLE;
	input.loadingStage = ST_OTHER + 1u;
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, &out) == 0);
	CHECK(AllZero(&out, sizeof(out)) && AllZero(&core, sizeof(core)));
	input.loadingStage = ST_IDLE;

	/* No launch result due: LaunchResult touches nothing. */
	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, R_LAUNCHED, &out) == 0);
	CHECK(out.leaveTitle == 0xA5u && AllZero(&core, sizeof(core)));
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(NULL, R_LAUNCHED, &out) == 0);

	/* armAndLaunch: until LaunchResult, Step refuses and changes nothing. */
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, &out) == 1);
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 1u);
	CHECK(core.phase == P_LAUNCH_RESULT);
	before = core;
	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, &out) == 0);
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(memcmp(&before, &core, sizeof(core)) == 0);
	/* A result that is not a RESULT_* value, or a NULL output: refused. */
	memset(&out, 0, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, R_NONE, &out) == 0);
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, 4u, &out) == 0);
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, R_LAUNCHED, NULL) == 0);
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(memcmp(&before, &core, sizeof(core)) == 0);
	/* The result keeps the Step output's fields and adds its own. */
	out.armAndLaunch = 1u;
	out.raceNumber = 1u;
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, R_LAUNCHED, &out) == 1);
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 1u && out.leaveTitle == 1u && out.installPads == 1u);
	CHECK(core.phase == P_WAIT_VALIDATED);
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, R_LAUNCHED, &out) == 0);

	/* A phase value the core never sets: refused. */
	core.phase = P_ENDED + 1u;
	before = core;
	input.startRace = 0u;
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, &out) == 0);
	CHECK(memcmp(&before, &core, sizeof(core)) == 0);
	return 0;
}

/* ---- rule 1: START_RACE and the title window wait ---- */

/* Idle frames decide nothing; START_RACE with the window open launches. */
static int TestIdle(void)
{
	struct Harness h;
	Input input = TitleOpen(S_IDLE);
	Output out;

	HarnessInit(&h);
	RUN(Quiet(&h, &input, 3u));
	RUN(Frame(&h, &input, R_NONE, &out));
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(h.core.phase == P_IDLE && h.core.launches == 0u);
	/* START_RACE with RACING starts race 1 and launches at once. */
	RUN(LaunchNow(&h, 1u));
	return 0;
}

/* One refused Step: returns 0, the output zeroed, the state unchanged. */
static int Refused(struct MainArcadeRaceLaunchCore *core, const Input *input)
{
	struct MainArcadeRaceLaunchCore before = *core;
	Output out;

	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_Step(core, input, &out) == 0);
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(memcmp(&before, core, sizeof(*core)) == 0);
	return 0;
}

/* Should-fix 2: START_RACE enters RACING on the same host tick, so startRace
 * 1 with hostRacing 0 (sampled before the Tick) is a refused step, in every
 * phase, and never drops the START_RACE silently. */
static int TestStartWithoutRacingRefused(void)
{
	struct Harness h;
	Input start = StartOn(TitleOpen(S_IDLE));
	Input closedStart = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u));
	Input closed = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u);
	Input endedStart = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u));
	Output out;

	HarnessInit(&h);
	start.hostRacing = 0u;
	RUN(Refused(&h.core, &start));
	CHECK(h.core.phase == P_IDLE && h.core.launches == 0u);
	/* The same START_RACE sampled after the Tick starts the race. */
	start.hostRacing = 1u;
	RUN(Frame(&h, &closedStart, R_NONE, &out));
	CHECK(h.core.phase == P_WAIT_WINDOW);
	RUN(Quiet(&h, &closed, 3u));
	closedStart.hostRacing = 0u;
	RUN(Refused(&h.core, &closedStart));
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == 3u);
	RUN(Frame(&h, &start, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 1u);
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	/* Ended: a START_RACE without RACING is refused, not held. */
	endedStart.hostRacing = 0u;
	RUN(Refused(&h.core, &endedStart));
	CHECK(h.core.phase == P_ENDED && h.core.held == 0u);
	RUN(ReturnHome(&h, S_VALIDATED));
	return 0;
}

/* Nit 7: an input flag other than 0 or 1 is refused (not read as nonzero);
 * the reserved bytes are ignored. */
static int TestFlagValuesRefused(void)
{
	static const size_t flags[6] = {offsetof(Input, startRace),       offsetof(Input, hostRacing),  offsetof(Input, titleWindowOpen),
	                                offsetof(Input, onMainMenuLevel), offsetof(Input, onPlanLevel), offsetof(Input, loadingBit)};
	static const uint8_t values[3] = {2u, 0x80u, 0xFFu};
	struct Harness h;
	Input closed = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u);
	Input start = StartOn(closed);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, R_NONE, &out));
	RUN(Quiet(&h, &closed, 5u));
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == 5u);
	for (uint32_t f = 0; f < 6u; f++)
	{
		for (uint32_t v = 0; v < 3u; v++)
		{
			Input bad = closed;
			((uint8_t *)&bad)[flags[f]] = values[v];
			RUN(Refused(&h.core, &bad));
		}
	}
	/* Nonzero reserved bytes change nothing. */
	closed.reserved[0] = 0xFFu;
	closed.reserved[1] = 0x01u;
	RUN(Quiet(&h, &closed, 1u));
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == 6u);
	return 0;
}

/* Rule 1 and should-fix 1: the window wait. Frame 0 is START_RACE; a window
 * closed on frames 0..899 waits; closed on frame 900 is WINDOW_TIMEOUT with
 * no race number (never launched), and the return step on that frame when
 * the stage is IDLE or REQUESTED (the recovery when the window stays
 * closed), with no pads and no Disarm; the core is idle on that frame. */
static int TestWindowTimeout(uint32_t stage)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u));
	Input closed = In(LVL_MENU, stage, 0u, S_IDLE, 1u);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, R_NONE, &out));
	CHECK(out.armAndLaunch == 0u && out.raceNumber == 0u && h.core.phase == P_WAIT_WINDOW);
	RUN(Quiet(&h, &closed, WINDOW_TICKS - 1u));
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == WINDOW_TICKS - 1u);
	RUN(Frame(&h, &closed, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_WINDOW && out.raceNumber == 0u);
	CHECK(out.requestReturn == 1u && out.installPads == 0u && out.clearPads == 0u && out.disarm == 0u);
	CHECK(h.core.phase == P_IDLE && h.core.launches == 0u);
	RUN(Frame(&h, &closed, R_NONE, &out));
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(h.returns == 1u && h.clears == 0u && h.disarms == 0u && h.launches == 0u);
	/* The next START_RACE takes launch number 1: the timeout consumed none. */
	RUN(LaunchNow(&h, 1u));
	return 0;
}

/* Should-fix 1: a WINDOW_TIMEOUT during a running load (stage OTHER) defers
 * the return step to the first stage IDLE or REQUESTED frame (here IDLE; see
 * TestDeferredReturnOnRequested for REQUESTED); no clear and no Disarm ever;
 * the race is over on the return step's frame. */
static int TestWindowTimeoutDeferred(void)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u));
	Input loading = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u);
	Input levelIdle = In(LVL_OTHER, ST_IDLE, 0u, S_IDLE, 1u);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, R_NONE, &out));
	RUN(Quiet(&h, &loading, WINDOW_TICKS - 1u));
	RUN(Frame(&h, &loading, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_WINDOW && out.raceNumber == 0u);
	CHECK(out.requestReturn == 0u && out.installPads == 0u && out.disarm == 0u);
	CHECK(h.core.phase == P_ENDED && h.core.returnPending == 1u && h.core.disarmPending == 0u && h.core.padsInstalled == 0u);
	RUN(Quiet(&h, &loading, 20u));
	RUN(Frame(&h, &levelIdle, R_NONE, &out));
	CHECK(out.requestReturn == 1u && out.reportFailure == 0u && out.clearPads == 0u && out.disarm == 0u && out.installPads == 0u);
	CHECK(h.core.phase == P_IDLE);
	RUN(Quiet(&h, &levelIdle, 5u));
	CHECK(h.returns == 1u && h.clears == 0u && h.disarms == 0u);
	return 0;
}

/* Rule 1: a window that opens on frame 900 exactly is still taken. */
static int TestWindowOnBound(void)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u));
	Input closed = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u);
	Input open = TitleOpen(S_IDLE);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, R_NONE, &out));
	RUN(Quiet(&h, &closed, WINDOW_TICKS - 1u));
	RUN(Frame(&h, &open, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.leaveTitle == 1u && out.installPads == 1u && out.reportFailure == 0u);
	CHECK(out.raceNumber == 1u);
	return 0;
}

/* Rule 1: the flow leaves RACING before the window: a quiet abort. */
static int TestWindowQuietAbort(void)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u));
	Input closed = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u);
	Input left = TitleOpen(S_IDLE);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, R_NONE, &out));
	RUN(Quiet(&h, &closed, 10u));
	left.hostRacing = 0u;
	RUN(Frame(&h, &left, R_NONE, &out));
	/* The window is open, but the host left RACING: nothing at all. */
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(h.core.phase == P_IDLE);
	RUN(Quiet(&h, &left, WINDOW_TICKS + 5u));
	/* The next START_RACE is race 1 (the aborted one never launched). */
	RUN(LaunchNow(&h, 1u));
	return 0;
}

/* Nit 7: the host leaves RACING on window frame 900 itself: the quiet abort
 * wins over WINDOW_TIMEOUT (no report, no return). */
static int TestWindowQuietAbortOnBound(void)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u));
	Input closed = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 1u);
	Input left = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 0u);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, R_NONE, &out));
	RUN(Quiet(&h, &closed, WINDOW_TICKS - 1u));
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == WINDOW_TICKS - 1u);
	RUN(Frame(&h, &left, R_NONE, &out));
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(h.core.phase == P_IDLE && h.windowTimeouts == 0u && h.returns == 0u);
	return 0;
}

/* ---- rule 2: the launch and its failures ---- */

/* Rule 2: pads installed from the Launch frame, every frame on. */
static int TestInstallFromLaunch(void)
{
	struct Harness h;
	Input requested = In(LVL_MENU, ST_REQ, 0u, S_LAUNCHED, 1u);
	Input loading = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Frame(&h, &requested, R_NONE, &out));
	CHECK(out.installPads == 1u && out.leaveTitle == 0u && out.armAndLaunch == 0u);
	for (uint32_t i = 0; i < 50u; i++)
	{
		RUN(Frame(&h, &loading, R_NONE, &out));
		CHECK(out.installPads == 1u && out.clearPads == 0u);
	}
	return 0;
}

/* Rule 2 and 7, should-fix 1: an Arm or Launch failure at the title reports
 * and Disarms on that frame, and runs the return step on that frame (stage
 * IDLE), with no LeaveTitle and no pads; the core is idle again. */
static int TestArmOrLaunchFailure(uint32_t result, uint32_t failure)
{
	struct Harness h;
	Input start = StartOn(TitleOpen(S_IDLE));
	Input title = TitleOpen(result == R_ARM_FAILED ? S_IDLE : S_FAILED);
	Output out;

	HarnessInit(&h);
	RUN(Frame(&h, &start, result, &out));
	CHECK(out.armAndLaunch == 1u && out.reportFailure == 1u && out.failure == failure && out.raceNumber == 1u);
	CHECK(out.disarm == 1u && out.leaveTitle == 0u && out.installPads == 0u && out.clearPads == 0u);
	CHECK(out.requestReturn == 1u && out.reportFinished == 0u);
	CHECK(h.core.phase == P_IDLE && h.core.padsInstalled == 0u && h.core.disarmPending == 0u);
	/* Nothing follows: no second return, no clear, no second Disarm. */
	RUN(Frame(&h, &title, R_NONE, &out));
	CHECK(AllZero(&out, sizeof(out)));
	title = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 0u);
	RUN(Quiet(&h, &title, 5u));
	CHECK(h.returns == 1u && h.clears == 0u && h.disarms == 1u);
	/* The rematch arms again as launch 2. */
	RUN(LaunchNow(&h, 2u));
	return 0;
}

/* Should-fix 1: the Arm/Launch failure's return step follows the loading
 * stage of its armAndLaunch frame: at once at REQUESTED, deferred to the
 * first stage IDLE or REQUESTED frame at OTHER; the Disarm is at once either
 * way, with no clear. (Synthetic inputs, a window open during a load, so the core is
 * driven directly rather than through the harness.) */
static int TestArmOrLaunchFailureStage(uint32_t result, uint32_t stage)
{
	struct MainArcadeRaceLaunchCore core;
	Input start = StartOn(TitleOpen(S_IDLE));
	Input loading = In(LVL_MENU, ST_OTHER, 1u, S_IDLE, 1u);
	Input requested = In(LVL_MENU, ST_REQ, 0u, S_IDLE, 1u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 1u);
	Output out;

	MainArcadeRaceLaunchCore_Init(&core);
	start.loadingStage = stage;
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &start, &out) == 1);
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 1u && core.launchStage == stage);
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, result, &out) == 1);
	CHECK(out.reportFailure == 1u && out.disarm == 1u && out.clearPads == 0u && out.installPads == 0u && out.raceNumber == 1u);
	if (stage == ST_REQ)
	{
		CHECK(out.requestReturn == 1u && core.phase == P_IDLE);
		return 0;
	}
	CHECK(out.requestReturn == 0u && core.phase == P_ENDED && core.returnPending == 1u && core.disarmPending == 0u);
	/* The load still runs: the return step stays owed. */
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &loading, &out) == 1);
	CHECK(out.requestReturn == 0u && out.disarm == 0u && out.clearPads == 0u && out.reportFailure == 0u && out.raceNumber == 1u);
	CHECK(core.returnPending == 1u);
	/* A REQUESTED frame releases the deferred return step (after a
	 * deferral only a main-menu request, such as the link's return to
	 * title, sets the stage REQUESTED: race-launch risk 10). */
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &requested, &out) == 1);
	CHECK(out.requestReturn == 1u && out.disarm == 0u && out.clearPads == 0u && out.installPads == 0u && out.reportFailure == 0u);
	CHECK(core.phase == P_IDLE && core.raceNumber == 0u);
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &requested, &out) == 1);
	CHECK(AllZero(&out, sizeof(out)));
	CHECK(MainArcadeRaceLaunchCore_Step(&core, &menuIdle, &out) == 1);
	CHECK(AllZero(&out, sizeof(out)));
	return 0;
}

/* ---- rule 3: VALIDATED and race tick 0 ---- */

/* Rule 3: VALIDATE_TIMEOUT on frame 1800 after Launch, while the race load
 * still runs: the return step is deferred (to the first stage IDLE or
 * REQUESTED frame). */
static int TestValidateTimeout(void)
{
	struct Harness h;
	Input requested = In(LVL_MENU, ST_REQ, 0u, S_LAUNCHED, 1u);
	Input stalled = In(LVL_PLAN, ST_OTHER, 1u, S_LAUNCHED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Quiet(&h, &requested, 1u));
	RUN(Quiet(&h, &stalled, VALIDATE_TICKS - 2u));
	CHECK(h.core.waitTicks == VALIDATE_TICKS - 1u);
	RUN(Frame(&h, &stalled, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_VALIDATE && out.raceNumber == 1u);
	CHECK(out.requestReturn == 0u && out.installPads == 1u && out.clearPads == 0u);
	CHECK(h.core.phase == P_ENDED && h.core.returnPending == 1u);
	return 0;
}

/* Rule 3 and nit 7: VALIDATE_TIMEOUT on a stage IDLE or REQUESTED frame runs
 * the return step on that same frame, the pads kept; then the clear and the
 * Disarm on later frames. */
static int TestValidateTimeoutReturnNow(uint32_t stage)
{
	struct Harness h;
	Input stalled = In(LVL_MENU, stage, 0u, S_LAUNCHED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Quiet(&h, &stalled, VALIDATE_TICKS - 1u));
	CHECK(h.core.waitTicks == VALIDATE_TICKS - 1u);
	RUN(Frame(&h, &stalled, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_VALIDATE && out.raceNumber == 1u);
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u && out.disarm == 0u);
	CHECK(h.core.phase == P_ENDED && h.core.returnPending == 0u);
	RUN(ReturnHome(&h, S_LAUNCHED));
	CHECK(h.returns == 1u && h.clears == 1u && h.disarms == 1u);
	return 0;
}

/* Rule 3: VALIDATED on frame 1800 after Launch exactly is still taken. */
static int TestValidatedOnBound(void)
{
	struct Harness h;
	Input stalled = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
	Input validated = In(LVL_PLAN, ST_OTHER, 1u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Quiet(&h, &stalled, VALIDATE_TICKS - 1u));
	RUN(Frame(&h, &validated, R_NONE, &out));
	CHECK(out.validated == 1u && out.reportFailure == 0u && out.raceNumber == 1u);
	CHECK(h.core.phase == P_WAIT_RACE_TICK);
	return 0;
}

/* Rule 3: race tick 0 is the first frame after the VALIDATED frame on the
 * plan's level with the stage IDLE and LOADING clear. */
static int TestRaceTickZero(void)
{
	struct Harness h;
	Input seeded = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
	Input validatedRunning = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input otherLevel = In(LVL_OTHER, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input planRequested = In(LVL_PLAN, ST_REQ, 0u, S_VALIDATED, 1u);
	Input planLoading = In(LVL_PLAN, ST_OTHER, 0u, S_VALIDATED, 1u);
	Input planBit = In(LVL_PLAN, ST_IDLE, 1u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Quiet(&h, &seeded, 2u));
	/* VALIDATED seen on a frame that already meets every race tick 0
	 * condition: the event, but not race tick 0 (it must be a later frame). */
	RUN(Frame(&h, &validatedRunning, R_NONE, &out));
	CHECK(out.validated == 1u && out.raceTickZero == 0u);
	/* VALIDATED on the idle main-menu level is not race tick 0; neither is
	 * another level, a requested or running load, or the LOADING bit. */
	RUN(Quiet(&h, &menuIdle, 3u));
	RUN(Quiet(&h, &otherLevel, 3u));
	RUN(Quiet(&h, &planRequested, 3u));
	RUN(Quiet(&h, &planLoading, 3u));
	RUN(Quiet(&h, &planBit, 3u));
	RUN(Frame(&h, &validatedRunning, R_NONE, &out));
	CHECK(out.raceTickZero == 1u && out.validated == 0u && out.raceNumber == 1u && out.installCommitted == 1u && out.driveStep == 1u && out.raceTick == 0u);
	/* Once only. */
	RUN(Quiet(&h, &validatedRunning, 5u));
	return 0;
}

/* Rule 3: RACE_TICK_TIMEOUT on frame 1800 after VALIDATED; race tick 0 on
 * that frame exactly is still taken. */
static int TestRaceTickTimeout(int onBound)
{
	struct Harness h;
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(Quiet(&h, &menuIdle, VALIDATE_TICKS - 1u));
	CHECK(h.core.waitTicks == VALIDATE_TICKS - 1u);
	if (onBound)
	{
		RUN(Frame(&h, &running, R_NONE, &out));
		CHECK(out.raceTickZero == 1u && out.reportFailure == 0u);
		return 0;
	}
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_RACE_TICK && out.raceNumber == 1u);
	/* Stage IDLE: the return step on this frame, the pads kept. */
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u && out.disarm == 0u);
	/* The return load's REQUESTED frame: no clear (neither LOADING nor idle)
	 * and no Disarm. Then the idle main-menu frame: the clear and the Disarm
	 * together (this synthetic run never sets LOADING). */
	{
		Input returnLoad = In(LVL_MENU, ST_REQ, 0u, S_VALIDATED, 0u);
		RUN(Frame(&h, &returnLoad, R_NONE, &out));
		CHECK(out.clearPads == 0u && out.installPads == 1u && out.disarm == 0u);
	}
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.disarm == 1u && out.installPads == 0u);
	CHECK(h.core.phase == P_IDLE);
	return 0;
}

/* ---- rules 4, 6, 7: the finish, the return, the clear, and the Disarm ---- */

/* Rule 4: the drive decides the race length (the core has no count of its
 * own: 20000 GO ticks, past the drive's 18000 bound, never finish), and the
 * finish comes on the FINISHED result's frame; rule 6: the return on the
 * finish frame, the clear on the first later LOADING frame; rule 7: the
 * Disarm on the idle main-menu frame. */
static int TestFinishPath(void)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(QuietOut(&h, &running, 20000u, &out));
	CHECK(out.raceTick == 20000u && out.installCommitted == 1u && h.core.phase == P_DRIVE);
	CHECK(h.driveSteps == 20001u && h.commits == 20001u && h.driveEnds == 0u);
	RUN(FrameDrive(&h, &running, R_NONE, D_FINISHED, &out));
	CHECK(out.raceTick == 20001u && out.driveEnded == 1u);
	CHECK(out.reportFinished == 1u && out.reportFailure == 0u && out.failure == F_NONE && out.raceNumber == 1u);
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u && out.disarm == 0u);
	RUN(ReturnHome(&h, S_VALIDATED));
	CHECK(h.launches == 1u && h.returns == 1u && h.clears == 1u && h.disarms == 1u);
	/* Idle afterwards. */
	RUN(Frame(&h, &running, R_NONE, &out));
	CHECK(AllZero(&out, sizeof(out)));
	return 0;
}

/* Rule 6: the clear on the first idle main-menu frame when no LOADING frame
 * comes first, together with the Disarm (rule 7). */
static int TestClearOnIdleMainMenu(void)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input returnRequested = In(LVL_MENU, ST_REQ, 0u, S_VALIDATED, 0u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_VALIDATED, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	RUN(Frame(&h, &returnRequested, R_NONE, &out));
	CHECK(out.clearPads == 0u && out.installPads == 1u);
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.disarm == 1u && out.installPads == 0u);
	return 0;
}

/* Rule 6: never on the end frame. The finish frame is itself an idle-looking
 * frame with LOADING set (a hypothetical load on that very frame): no clear. */
static int TestNoClearOnEndFrame(void)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input finishWithBit = In(LVL_PLAN, ST_IDLE, 1u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(Quiet(&h, &running, DRIVE_TICKS - 1u));
	RUN(FrameDrive(&h, &finishWithBit, R_NONE, D_FINISHED, &out));
	CHECK(out.reportFinished == 1u && out.requestReturn == 1u && out.clearPads == 0u && out.installPads == 1u);
	RUN(Frame(&h, &finishWithBit, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.installPads == 0u && out.disarm == 0u);
	return 0;
}

/* Rule 7: never Disarm off the idle main-menu level. */
static int TestDisarmOnlyOnIdleMainMenu(void)
{
	struct Harness h;
	Input raceIdle = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 0u);
	Input menuLoading = In(LVL_MENU, ST_OTHER, 0u, S_VALIDATED, 0u);
	Input menuRequested = In(LVL_MENU, ST_REQ, 0u, S_VALIDATED, 0u);
	Input menuBit = In(LVL_MENU, ST_IDLE, 1u, S_VALIDATED, 0u);
	Input otherIdle = In(LVL_OTHER, ST_IDLE, 0u, S_VALIDATED, 0u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_VALIDATED, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	RUN(Frame(&h, &menuBit, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.disarm == 0u);
	RUN(Quiet(&h, &raceIdle, 20u));
	RUN(Quiet(&h, &menuLoading, 20u));
	RUN(Quiet(&h, &menuRequested, 20u));
	RUN(Quiet(&h, &menuBit, 20u));
	RUN(Quiet(&h, &otherIdle, 20u));
	CHECK(h.core.phase == P_ENDED);
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.disarm == 1u && out.clearPads == 0u);
	RUN(Quiet(&h, &menuIdle, 20u));
	CHECK(h.disarms == 1u);
	return 0;
}

/* ---- rules 5, 6, 9: every RL-11 path after Launch and the flow leaving ---- */

/* Slice text and rule 6: a FAILED setup during the staged race-track load
 * (stage OTHER) reports at once, defers the return step to the first stage
 * IDLE or REQUESTED frame, here IDLE (no numPlyrNextGame or request before
 * it), and clears the pads only on a LOADING frame after that. */
static int TestFailureDuringRaceLoad(void)
{
	struct Harness h;
	Input requested = In(LVL_MENU, ST_REQ, 0u, S_LAUNCHED, 1u);
	Input loading = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
	Input failedLoading = In(LVL_PLAN, ST_OTHER, 1u, S_FAILED, 1u);
	Input failedLoadingOff = In(LVL_PLAN, ST_OTHER, 1u, S_FAILED, 0u);
	Input raceLevelIdle = In(LVL_PLAN, ST_IDLE, 0u, S_FAILED, 0u);
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, S_FAILED, 0u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_FAILED, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Quiet(&h, &requested, 1u));
	RUN(Quiet(&h, &loading, 5u));
	RUN(Frame(&h, &failedLoading, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_SETUP && out.raceNumber == 1u);
	CHECK(out.requestReturn == 0u && out.installPads == 1u && out.clearPads == 0u && out.disarm == 0u);
	/* The rest of the race-track load: LOADING is set, but the return step
	 * has not run, so the pads stay installed and nothing else happens. */
	RUN(QuietOut(&h, &failedLoadingOff, 30u, &out));
	CHECK(out.installPads == 1u && h.padsLive == 1u && h.core.returnPending == 1u);
	/* The race level is up (stage IDLE): the deferred return step. */
	RUN(Frame(&h, &raceLevelIdle, R_NONE, &out));
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u && out.reportFailure == 0u);
	RUN(Frame(&h, &returnLoad, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.installPads == 0u && out.requestReturn == 0u);
	RUN(Quiet(&h, &returnLoad, 10u));
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.disarm == 1u);
	CHECK(h.returns == 1u && h.clears == 1u && h.disarms == 1u);
	return 0;
}

/* Race-launch risk 10 follow-up: the flow leaves RACING during the staged
 * race-track load (stage OTHER), so the return step is owed; the link's own
 * deferred return then requests the main-menu level on the first IDLE frame
 * before this Step, so the core first sees the stage REQUESTED. The owed
 * return step runs on that REQUESTED frame, once (the same request, before
 * the main-menu load starts), never again during or after that load; then
 * the clear on the load's first LOADING frame and the Disarm on the idle
 * main-menu level. */
static int TestDeferredReturnOnRequested(void)
{
	struct Harness h;
	Input requested = In(LVL_MENU, ST_REQ, 0u, S_LAUNCHED, 1u);
	Input loading = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
	Input loadingLeft = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 0u);
	Input raceLevelRequested = In(LVL_PLAN, ST_REQ, 0u, S_SEEDED, 0u);
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, S_FAILED, 0u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_FAILED, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Quiet(&h, &requested, 1u));
	RUN(Quiet(&h, &loading, 3u));
	/* The end frame: off RACING mid-load, no report, the return step owed. */
	RUN(Frame(&h, &loadingLeft, R_NONE, &out));
	CHECK(out.requestReturn == 0u && out.reportFailure == 0u && out.reportFinished == 0u && out.installPads == 1u);
	CHECK(h.core.phase == P_ENDED && h.core.returnPending == 1u);
	RUN(QuietOut(&h, &loadingLeft, 10u, &out));
	CHECK(out.installPads == 1u && h.core.returnPending == 1u);
	/* The first frame the core sees after that load: REQUESTED. */
	RUN(Frame(&h, &raceLevelRequested, R_NONE, &out));
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u && out.disarm == 0u);
	CHECK(h.core.returnPending == 0u && h.returns == 1u);
	/* No second request, before or during the main-menu load. */
	RUN(QuietOut(&h, &raceLevelRequested, 2u, &out));
	CHECK(out.installPads == 1u);
	RUN(Frame(&h, &returnLoad, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.installPads == 0u && out.requestReturn == 0u);
	RUN(Quiet(&h, &returnLoad, 10u));
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.disarm == 1u && out.requestReturn == 0u && h.core.phase == P_IDLE);
	RUN(Quiet(&h, &menuIdle, 5u));
	CHECK(h.returns == 1u && h.clears == 1u && h.disarms == 1u);
	return 0;
}

/* Rule 5: FAILED while waiting for race tick 0, and on a drive frame (race
 * tick 1, 75, or 150's frame): no drive step there, the drive phase ends
 * (driveEnded) with SETUP_FAILED. */
static int TestSetupFailedLater(uint32_t when)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input failed = In(LVL_PLAN, ST_IDLE, 0u, S_FAILED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	if (when == 0u)
	{
		Input loadingFailed = In(LVL_PLAN, ST_OTHER, 1u, S_FAILED, 1u);
		RUN(Frame(&h, &loadingFailed, R_NONE, &out));
		CHECK(out.reportFailure == 1u && out.failure == F_SETUP && out.requestReturn == 0u);
		return 0;
	}
	RUN(ToRaceTickZero(&h, 1u));
	RUN(Quiet(&h, &running, when - 1u));
	RUN(Frame(&h, &failed, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_SETUP && out.reportFinished == 0u && out.raceNumber == 1u);
	CHECK(out.driveStep == 0u && out.driveEnded == 1u && out.installCommitted == 0u);
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u);
	/* No drive step after the end, even with the setup VALIDATED on RACING
	 * again (the harness fails a drive step with the phase closed). */
	RUN(Quiet(&h, &running, 30u));
	CHECK(h.driveEnds == 1u && h.driveSteps == when);
	RUN(ReturnHome(&h, S_FAILED));
	return 0;
}

/* Rule 5: a status the race cannot be in counts as SETUP_FAILED: IDLE or
 * ARMED before VALIDATED, SEEDED after it. */
static int TestImpossibleStatus(void)
{
	static const uint32_t early[2] = {S_IDLE, S_ARMED};
	Output out;

	for (uint32_t i = 0; i < 2u; i++)
	{
		struct Harness h;
		Input input = In(LVL_PLAN, ST_OTHER, 1u, early[i], 1u);

		HarnessInit(&h);
		RUN(LaunchNow(&h, 1u));
		RUN(Frame(&h, &input, R_NONE, &out));
		CHECK(out.reportFailure == 1u && out.failure == F_SETUP);
	}
	{
		struct Harness h;
		Input input = In(LVL_PLAN, ST_IDLE, 0u, S_SEEDED, 1u);

		HarnessInit(&h);
		RUN(LaunchNow(&h, 1u));
		RUN(LoadToValidated(&h, 1u));
		RUN(Frame(&h, &input, R_NONE, &out));
		CHECK(out.reportFailure == 1u && out.failure == F_SETUP && out.raceTickZero == 0u);
	}
	return 0;
}

/* Rule 6 and 9: the flow leaves RACING (link failure or LOST) in each
 * launched phase: no report (not even with FAILED on that frame), the return
 * step on that frame or deferred, then the clear and the Disarm. */
static int TestFlowLeftRacing(uint32_t phase)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input left;
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	if (phase == P_WAIT_VALIDATED)
	{
		/* During the staged race-track load: deferred. */
		Input loading = In(LVL_PLAN, ST_OTHER, 1u, S_SEEDED, 1u);
		RUN(Quiet(&h, &loading, 4u));
		left = In(LVL_PLAN, ST_OTHER, 1u, S_FAILED, 0u);
	}
	else if (phase == P_WAIT_RACE_TICK)
	{
		RUN(LoadToValidated(&h, 1u));
		left = In(LVL_PLAN, ST_IDLE, 1u, S_FAILED, 0u);
	}
	else
	{
		RUN(LoadToValidated(&h, 1u));
		RUN(ToRaceTickZero(&h, 1u));
		RUN(Quiet(&h, &running, 40u));
		left = In(LVL_PLAN, ST_IDLE, 0u, S_FAILED, 0u);
	}
	CHECK(h.core.phase == phase);
	RUN(Frame(&h, &left, R_NONE, &out));
	CHECK(out.reportFailure == 0u && out.reportFinished == 0u && out.failure == F_NONE && out.disarm == 0u);
	CHECK(out.installPads == 1u && out.clearPads == 0u && out.driveStep == 0u);
	CHECK(out.driveEnded == ((phase == P_DRIVE) ? 1u : 0u));
	CHECK(h.core.phase == P_ENDED);
	if (phase == P_WAIT_VALIDATED)
	{
		Input raceIdle = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 0u);
		CHECK(out.requestReturn == 0u);
		RUN(Quiet(&h, &left, 10u));
		RUN(Frame(&h, &raceIdle, R_NONE, &out));
		CHECK(out.requestReturn == 1u && out.clearPads == 0u);
	}
	else
	{
		CHECK(out.requestReturn == 1u);
	}
	RUN(ReturnHome(&h, S_VALIDATED));
	CHECK(h.returns == 1u && h.clears == 1u && h.disarms == 1u);
	return 0;
}

/* Nit 7: the flow leaves RACING on the frame after the drive's last tick
 * (race tick DRIVE_TICKS - 1): no drive step on it, no finish is reported
 * (the host no longer takes one), the drive phase ends there (driveEnded),
 * and the end steps run as for any abort; the flow coming back to RACING
 * with the setup VALIDATED brings no drive step. */
static int TestFlowLeftOnDriveFrame(void)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input left = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 0u);
	Input stillRace = In(LVL_PLAN, ST_REQ, 0u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(Quiet(&h, &running, DRIVE_TICKS - 1u));
	CHECK(h.core.raceTick == DRIVE_TICKS - 1u && h.core.phase == P_DRIVE);
	RUN(Frame(&h, &left, R_NONE, &out));
	CHECK(out.driveStep == 0u && out.driveEnded == 1u && out.raceTick == 0u);
	CHECK(out.reportFinished == 0u && out.reportFailure == 0u && out.failure == F_NONE && out.raceNumber == 1u);
	CHECK(out.requestReturn == 1u && out.installPads == 1u && out.clearPads == 0u && out.disarm == 0u);
	CHECK(h.core.phase == P_ENDED && h.reported[1] == 0u);
	RUN(QuietOut(&h, &stillRace, 5u, &out));
	CHECK(out.installPads == 1u && h.driveSteps == DRIVE_TICKS && h.driveEnds == 1u);
	RUN(ReturnHome(&h, S_VALIDATED));
	CHECK(h.returns == 1u && h.clears == 1u && h.disarms == 1u && h.reported[1] == 0u);
	return 0;
}

/* RL-9 and rule 5: an abort while LAUNCHED whose return load replaces the
 * queued race level (return step at stage REQUESTED): the main-menu level
 * initializes in LAUNCHED and the setup ends FAILED/LEVEL_MISMATCH; that
 * second failure is never reported to the host. */
static int TestAbortReplacesQueuedRace(void)
{
	struct Harness h;
	Input leftRequested = In(LVL_MENU, ST_REQ, 0u, S_LAUNCHED, 0u);
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, S_LAUNCHED, 0u);
	Input returnLoadFailed = In(LVL_MENU, ST_OTHER, 1u, S_FAILED, 0u);
	Input menuIdleFailed = In(LVL_MENU, ST_IDLE, 0u, S_FAILED, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(Frame(&h, &leftRequested, R_NONE, &out));
	CHECK(out.requestReturn == 1u && out.reportFailure == 0u && out.installPads == 1u && out.clearPads == 0u);
	RUN(Frame(&h, &returnLoad, R_NONE, &out));
	CHECK(out.clearPads == 1u);
	/* FAILED/LEVEL_MISMATCH lands: nothing reported, even with RACING back. */
	RUN(Quiet(&h, &returnLoadFailed, 5u));
	returnLoadFailed.hostRacing = 1u;
	RUN(Quiet(&h, &returnLoadFailed, 5u));
	RUN(Frame(&h, &menuIdleFailed, R_NONE, &out));
	CHECK(out.disarm == 1u && out.reportFailure == 0u);
	CHECK(h.reported[1] == 0u);
	return 0;
}

/* Rule 5 and 9: after the finish, a FAILED status and a return to RACING
 * report nothing more for that race. */
static int TestNothingAfterFinish(void)
{
	struct Harness h;
	Input failedRacing = In(LVL_PLAN, ST_OTHER, 1u, S_FAILED, 1u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_FAILED, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	RUN(Frame(&h, &failedRacing, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.reportFailure == 0u);
	RUN(Quiet(&h, &failedRacing, 2000u));
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.disarm == 1u && out.reportFailure == 0u && out.reportFinished == 0u);
	return 0;
}

/* ---- rule 8: two races in a row ---- */

static int RunFullRace(struct Harness *h, uint32_t race)
{
	RUN(LaunchNow(h, race));
	RUN(LoadToValidated(h, race));
	RUN(ToRaceTickZero(h, race));
	RUN(DriveToFinish(h, race));
	RUN(ReturnHome(h, S_VALIDATED));
	return 0;
}

/* Rule 8: race 1 finishes and Disarms; the rematch's START_RACE runs race 2
 * in full; a failed race 3 and a finished race 4 follow. */
static int TestTwoRacesInARow(void)
{
	struct Harness h;
	Input results = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 0u);
	Input start = StartOn(In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 1u));
	Input introClosed = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 1u);
	Input open = TitleOpen(S_IDLE);
	Input stalled = In(LVL_PLAN, ST_OTHER, 1u, S_LAUNCHED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(RunFullRace(&h, 1u));
	/* RESULTS, rematch, select on the main-menu level. */
	RUN(Quiet(&h, &results, 120u));
	RUN(RunFullRace(&h, 2u));
	CHECK(h.core.launches == 2u && h.disarms == 2u && h.clears == 2u && h.returns == 2u);
	/* Race 3: START_RACE while the title intro runs, then VALIDATE_TIMEOUT. */
	RUN(Frame(&h, &start, R_NONE, &out));
	CHECK(out.raceNumber == 0u && out.armAndLaunch == 0u);
	RUN(Quiet(&h, &introClosed, 229u));
	RUN(Frame(&h, &open, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 3u);
	RUN(Quiet(&h, &stalled, VALIDATE_TICKS - 1u));
	RUN(Frame(&h, &stalled, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_VALIDATE && out.raceNumber == 3u);
	{
		Input raceIdle = In(LVL_PLAN, ST_IDLE, 0u, S_FAILED, 0u);
		RUN(Frame(&h, &raceIdle, R_NONE, &out));
		CHECK(out.requestReturn == 1u);
	}
	RUN(ReturnHome(&h, S_FAILED));
	RUN(Quiet(&h, &results, 60u));
	RUN(RunFullRace(&h, 4u));
	CHECK(h.launches == 4u && h.disarms == 4u && h.clears == 4u && h.returns == 4u);
	return 0;
}

/* Rule 8: a START_RACE before race 1's Disarm is held; the window, though
 * open, is never taken before the Disarm frame, nor on it; the next frame
 * launches race 2. */
static int TestHeldStart(void)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input returnLoadStart = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u));
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u);
	Input menuIdleOpen = TitleOpen(S_VALIDATED);
	Input open = TitleOpen(S_IDLE);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(Quiet(&h, &running, DRIVE_TICKS - 1u));
	RUN(FrameDrive(&h, &running, R_NONE, D_FINISHED, &out));
	CHECK(out.reportFinished == 1u && out.requestReturn == 1u);
	/* The flow reaches the next START_RACE during the return load. */
	RUN(Frame(&h, &returnLoadStart, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.armAndLaunch == 0u && out.raceNumber == 1u);
	CHECK(h.core.held == 1u && h.core.phase == P_ENDED && h.core.launches == 1u);
	RUN(Quiet(&h, &returnLoad, 10u));
	/* The Disarm frame: the window is open, but no armAndLaunch yet. */
	RUN(Frame(&h, &menuIdleOpen, R_NONE, &out));
	CHECK(out.disarm == 1u && out.armAndLaunch == 0u && out.raceNumber == 1u);
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.raceNumber == 0u && h.core.waitTicks == 11u);
	/* The next frame launches race 2. */
	RUN(Frame(&h, &open, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 2u && out.installPads == 1u);
	RUN(LoadToValidated(&h, 2u));
	RUN(ToRaceTickZero(&h, 2u));
	RUN(DriveToFinish(&h, 2u));
	RUN(ReturnHome(&h, S_VALIDATED));
	CHECK(h.disarms == 2u && h.launches == 2u);
	return 0;
}

/* Rule 8: the held race keeps its window count across the Disarm: it times
 * out on frame 900 after its START_RACE, whichever side of the Disarm, with
 * no race number, and owes its own return step (should-fix 1). mode 0: the
 * Disarm first, the timeout on an idle frame (return at once, then idle).
 * mode 1: the timeout during a load (stage OTHER), before race 1's Disarm:
 * the return step waits for the first stage IDLE or REQUESTED frame (here
 * IDLE), and race 1's Disarm follows on a later frame. mode 2: the same at stage REQUESTED: the return
 * step on the timeout frame. A Disarm never shares the timeout's frame. */
static int TestHeldStartTimeout(int mode)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u));
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u);
	Input menuRequested = In(LVL_MENU, ST_REQ, 0u, S_VALIDATED, 1u);
	Input menuIdleClosed = In(LVL_MENU, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	RUN(Frame(&h, &start, R_NONE, &out)); /* held frame 0; the clear */
	CHECK(out.clearPads == 1u && h.core.held == 1u);
	if (mode == 0)
	{
		RUN(Quiet(&h, &returnLoad, 99u));
		RUN(Frame(&h, &menuIdleClosed, R_NONE, &out)); /* frame 100: Disarm */
		CHECK(out.disarm == 1u && h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == 100u);
		RUN(Quiet(&h, &menuIdleClosed, WINDOW_TICKS - 101u));
		RUN(Frame(&h, &menuIdleClosed, R_NONE, &out));
		CHECK(out.reportFailure == 1u && out.failure == F_WINDOW && out.raceNumber == 0u);
		CHECK(out.disarm == 0u && out.requestReturn == 1u && h.core.phase == P_IDLE);
		RUN(LaunchNow(&h, 2u));
		return 0;
	}
	/* The Disarm never comes in time: race 2 times out while held, and race
	 * 1's Disarm still runs later, after the timeout's return step. */
	RUN(Quiet(&h, &returnLoad, WINDOW_TICKS - 1u));
	RUN(Frame(&h, (mode == 1) ? &returnLoad : &menuRequested, R_NONE, &out));
	CHECK(out.reportFailure == 1u && out.failure == F_WINDOW && out.raceNumber == 0u && out.disarm == 0u && out.clearPads == 0u);
	CHECK(h.core.held == 0u && h.core.phase == P_ENDED);
	if (mode == 1)
	{
		CHECK(out.requestReturn == 0u && h.core.returnPending == 1u);
		RUN(Quiet(&h, &returnLoad, 3u));
		/* The first stage IDLE frame: the return step, no Disarm yet. */
		RUN(Frame(&h, &menuIdleClosed, R_NONE, &out));
		CHECK(out.requestReturn == 1u && out.disarm == 0u && out.raceNumber == 1u);
	}
	else
	{
		CHECK(out.requestReturn == 1u && h.core.returnPending == 0u);
	}
	RUN(Frame(&h, &menuIdleClosed, R_NONE, &out));
	CHECK(out.disarm == 1u && out.raceNumber == 1u && out.armAndLaunch == 0u && out.requestReturn == 0u);
	CHECK(h.core.phase == P_IDLE && h.returns == 2u && h.disarms == 1u);
	/* Launch 2 next: the timed-out race consumed no number. */
	RUN(LaunchNow(&h, 2u));
	return 0;
}

/* Rule 8: a held START_RACE is dropped quietly when the flow leaves RACING. */
static int TestHeldStartDropped(void)
{
	struct Harness h;
	Input start = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u));
	Input returnLoadRacing = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u);
	Input returnLoadLeft = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 0u);
	Input menuIdleOpen = TitleOpen(S_VALIDATED);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	RUN(Frame(&h, &start, R_NONE, &out));
	RUN(Quiet(&h, &returnLoadRacing, 5u));
	RUN(Frame(&h, &returnLoadLeft, R_NONE, &out));
	CHECK(h.core.held == 0u && out.reportFailure == 0u);
	RUN(Quiet(&h, &returnLoadLeft, WINDOW_TICKS));
	menuIdleOpen.hostRacing = 0u;
	RUN(Frame(&h, &menuIdleOpen, R_NONE, &out));
	CHECK(out.disarm == 1u && h.core.phase == P_IDLE);
	RUN(Quiet(&h, &menuIdleOpen, 3u));
	/* The next launch is number 2: the dropped START_RACE consumed none. */
	RUN(LaunchNow(&h, 2u));
	return 0;
}

/* Nit 7: a START_RACE on race 1's Disarm frame is held: the window, though
 * open, is not taken on that frame; the held race's window wait starts at 0
 * and the next frame launches it as number 2. */
static int TestStartOnDisarmFrame(void)
{
	struct Harness h;
	Input returnLoad = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 0u);
	Input disarmStart = StartOn(TitleOpen(S_VALIDATED));
	Input open = TitleOpen(S_IDLE);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	RUN(Frame(&h, &returnLoad, R_NONE, &out));
	CHECK(out.clearPads == 1u);
	RUN(Frame(&h, &disarmStart, R_NONE, &out));
	CHECK(out.disarm == 1u && out.armAndLaunch == 0u && out.raceNumber == 1u && out.reportFailure == 0u);
	CHECK(h.core.phase == P_WAIT_WINDOW && h.core.waitTicks == 0u && h.core.raceNumber == 0u && h.core.held == 0u);
	RUN(Frame(&h, &open, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 2u);
	return 0;
}

/* START_RACE during a race in progress is ignored. */
static int TestStartDuringRaceIgnored(void)
{
	struct Harness h;
	Input runningStart = StartOn(In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u));

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(Quiet(&h, &runningStart, 10u));
	CHECK(h.core.launches == 1u && h.core.held == 0u && h.core.phase == P_DRIVE);
	return 0;
}

/* ---- the drive (the Task 8 race plan, LR-S10 part 2) ---- */

/* The DriveResult protocol: not due, it touches nothing; after a driveStep
 * it is due exactly once, Step refuses while it is due (zeroed output, state
 * unchanged), an invalid result or a NULL is refused and it stays due, and
 * the valid result keeps the Step output's fields. */
static int TestDriveResultProtocol(void)
{
	struct Harness h;
	struct MainArcadeRaceLaunchCore before;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;
	Output saved;

	HarnessInit(&h);
	/* Idle: nothing is due. */
	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, D_GO, &out) == 0);
	CHECK(out.installCommitted == 0xA5u && AllZero(&h.core, sizeof(h.core)));
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	/* Waiting for race tick 0: still nothing due. */
	before = h.core;
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, D_GO, &out) == 0);
	CHECK(memcmp(&before, &h.core, sizeof(before)) == 0);
	RUN(ToRaceTickZero(&h, 1u));
	RUN(Quiet(&h, &running, 9u));
	/* Race tick 10's Step, driven by hand. */
	memset(&out, 0xA5, sizeof(out));
	CHECK(MainArcadeRaceLaunchCore_Step(&h.core, &running, &out) == 1);
	CHECK(out.driveStep == 1u && out.raceTick == 10u && out.installPads == 1u && out.installCommitted == 0u && out.driveEnded == 0u);
	CHECK(h.core.driveDue == 1u && h.core.raceTick == 10u);
	saved = out;
	before = h.core;
	/* While due, Step refuses and changes nothing. */
	RUN(Refused(&h.core, &running));
	CHECK(memcmp(&before, &h.core, sizeof(before)) == 0);
	/* Invalid results and NULLs are refused; the result stays due. */
	out = saved;
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, 0u, &out) == 0);
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, 5u, &out) == 0);
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, 0xFFFFFFFFu, &out) == 0);
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, 6u, &out) == 0);
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, D_GO, NULL) == 0);
	CHECK(MainArcadeRaceLaunchCore_DriveResult(NULL, D_GO, &out) == 0);
	CHECK(memcmp(&saved, &out, sizeof(out)) == 0);
	CHECK(memcmp(&before, &h.core, sizeof(before)) == 0);
	/* A launch result is not a drive result. */
	CHECK(MainArcadeRaceLaunchCore_LaunchResult(&h.core, R_LAUNCHED, &out) == 0);
	CHECK(memcmp(&before, &h.core, sizeof(before)) == 0);
	/* The valid result: the Step's fields kept, the committed pads asked for. */
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, D_GO, &out) == 1);
	CHECK(out.driveStep == 1u && out.raceTick == 10u && out.raceNumber == 1u && out.installCommitted == 1u && out.installPads == 0u);
	CHECK(out.driveEnded == 0u && h.core.driveDue == 0u && h.core.phase == P_DRIVE);
	/* Due exactly once. */
	CHECK(MainArcadeRaceLaunchCore_DriveResult(&h.core, D_GO, &out) == 0);
	h.lastTick = 10u;
	h.driveSteps++;
	/* The next frame is race tick 11. */
	RUN(Frame(&h, &running, R_NONE, &out));
	CHECK(out.raceTick == 11u && out.installCommitted == 1u);
	return 0;
}

/* Each ending result on race tick DRIVE_TICKS: FINISHED reports the finish
 * (the latch), FAILED reports DRIVE_FAILED, OUTCOME reports nothing; each
 * ends the drive phase (driveEnded) with the return step on that frame and
 * the neutral pads from it; no later frame of the race steps the drive, the
 * flow staying on RACING with the setup VALIDATED included; and the next
 * race starts at race tick 0 again. */
static int TestDriveEnds(uint32_t driveResult)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Input results = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(QuietOut(&h, &running, DRIVE_TICKS - 1u, &out));
	CHECK(out.raceTick == DRIVE_TICKS - 1u && h.commits == DRIVE_TICKS);
	RUN(FrameDrive(&h, &running, R_NONE, driveResult, &out));
	CHECK(out.driveStep == 1u && out.raceTick == DRIVE_TICKS && out.driveEnded == 1u && out.raceNumber == 1u);
	CHECK(out.installPads == 1u && out.installCommitted == 0u && out.requestReturn == 1u && out.clearPads == 0u && out.disarm == 0u);
	CHECK(h.core.phase == P_ENDED);
	if (driveResult == D_FINISHED)
	{
		CHECK(out.reportFinished == 1u && out.raceFinishedInput == 1u && out.reportFailure == 0u);
	}
	else if (driveResult == D_FAILED)
	{
		CHECK(out.reportFailure == 1u && out.failure == F_DRIVE && out.reportFinished == 0u && out.raceFinishedInput == 0u);
		CHECK(strcmp(MainArcadeRaceLaunchCore_FailureName(out.failure), "DRIVE_FAILED") == 0);
	}
	else
	{
		CHECK(out.reportFinished == 0u && out.reportFailure == 0u && out.failure == F_NONE && out.raceFinishedInput == 0u);
	}
	/* The level runs on (RACING, VALIDATED): neutral pads, no drive step. */
	RUN(QuietOut(&h, &running, 40u, &out));
	CHECK(out.installPads == 1u && out.installCommitted == 0u && out.driveStep == 0u);
	CHECK(h.driveSteps == DRIVE_TICKS + 1u && h.driveEnds == 1u && h.commits == DRIVE_TICKS);
	RUN(ReturnHome(&h, S_VALIDATED));
	RUN(Quiet(&h, &results, 30u));
	/* Race 2 starts at race tick 0 again. */
	RUN(LaunchNow(&h, 2u));
	RUN(LoadToValidated(&h, 2u));
	RUN(ToRaceTickZero(&h, 2u));
	CHECK(h.core.raceTick == 0u && h.lastTick == 0u);
	RUN(QuietOut(&h, &running, 3u, &out));
	CHECK(out.raceTick == 3u);
	RUN(DriveToFinish(&h, 2u));
	CHECK(h.driveEnds == 2u);
	return 0;
}

/* Race tick 0's own drive step can end the race: an OUTCOME there (a peer
 * that never started, the stall timeout of the start wait) ends the drive
 * phase on its first frame. */
static int TestDriveEndOnRaceTickZero(void)
{
	struct Harness h;
	Input running = In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(FrameDrive(&h, &running, R_NONE, D_OUTCOME, &out));
	CHECK(out.raceTickZero == 1u && out.driveStep == 1u && out.raceTick == 0u && out.driveEnded == 1u);
	CHECK(out.installPads == 1u && out.installCommitted == 0u && out.requestReturn == 1u && out.reportFailure == 0u);
	CHECK(h.core.phase == P_ENDED && h.commits == 0u);
	RUN(Quiet(&h, &running, 10u));
	CHECK(h.driveSteps == 1u);
	RUN(ReturnHome(&h, S_VALIDATED));
	return 0;
}

/* A small LCG for TestDriveResultCallerPattern (fixed seed, reproducible). */
static uint32_t CallerPatternNext(uint32_t *seed)
{
	*seed = (*seed * 1664525u) + 1013904223u;
	return *seed >> 16;
}

/* Seven frames in eight the input that moves the race on from the core's
 * phase; otherwise any valid input (START_RACE only on RACING). */
static Input CallerPatternInput(const struct MainArcadeRaceLaunchCore *core, uint32_t *seed)
{
	Input input;
	uint32_t level;
	uint32_t stage;
	uint8_t loadingBit;
	uint32_t status;
	uint8_t racing;

	if ((CallerPatternNext(seed) & 7u) != 0u)
	{
		switch (core->phase)
		{
		case P_IDLE:
		case P_WAIT_WINDOW:
			return StartOn(TitleOpen(S_IDLE));
		case P_WAIT_VALIDATED:
			return In(LVL_PLAN, ST_OTHER, 1u, S_VALIDATED, 1u);
		case P_WAIT_RACE_TICK:
		case P_DRIVE:
			return In(LVL_PLAN, ST_IDLE, 0u, S_VALIDATED, 1u);
		default:
			return In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 0u);
		}
	}
	/* One draw per statement: the order of the draws is fixed. */
	level = CallerPatternNext(seed) % 3u;
	stage = CallerPatternNext(seed) % (ST_OTHER + 1u);
	loadingBit = (uint8_t)(CallerPatternNext(seed) & 1u);
	status = CallerPatternNext(seed) % (S_FAILED + 1u);
	racing = (uint8_t)(CallerPatternNext(seed) & 1u);
	input = In((level == 0u) ? LVL_MENU : ((level == 1u) ? LVL_PLAN : LVL_OTHER), stage, loadingBit, status, racing);
	input.titleWindowOpen = (uint8_t)(CallerPatternNext(seed) & 1u);
	input.startRace = (uint8_t)(CallerPatternNext(seed) & input.hostRacing);
	return input;
}

/*
 * LR-S10 part 2 review: the race caller's pattern (MainArcadeRaceLaunch_Frame
 * and MainArcadeRaceLaunch_Drive): Step, LaunchResult on an armAndLaunch
 * frame, and on every driveStep frame exactly one DriveResult with a valid
 * result (GO, FINISHED, FAILED, or OUTCOME) on the same output, before the
 * next Step. In that pattern the core never refuses a DriveResult: the
 * caller's wrapper only logs a refusal, which would leave the result due and
 * every later Step refused (a wedged race). Nor is any Step refused. Many
 * races on pseudo-random valid inputs (mostly those that move the race on)
 * and pseudo-random results (GO most of the time, else an end); every result
 * and a drive end without a result (the flow off RACING or the setup not
 * VALIDATED) are seen.
 */
static int TestDriveResultCallerPattern(void)
{
	static const uint32_t launchResults[3] = {R_LAUNCHED, R_ARM_FAILED, R_LAUNCH_FAILED};
	static const uint32_t endResults[3] = {D_FINISHED, D_FAILED, D_OUTCOME};
	struct MainArcadeRaceLaunchCore core;
	uint32_t seed = 0x5EEDu;
	uint32_t seen[D_OUTCOME + 1u];
	uint32_t driveSteps = 0u;
	uint32_t endsWithoutResult = 0u;
	Output out;

	memset(seen, 0, sizeof(seen));
	MainArcadeRaceLaunchCore_Init(&core);
	for (uint32_t frame = 0; frame < 400000u; frame++)
	{
		Input input = CallerPatternInput(&core, &seed);

		CHECK(core.driveDue == 0u);
		CHECK(MainArcadeRaceLaunchCore_Step(&core, &input, &out) == 1);
		if (out.armAndLaunch != 0u)
		{
			CHECK(out.driveStep == 0u);
			CHECK(MainArcadeRaceLaunchCore_LaunchResult(&core, launchResults[CallerPatternNext(&seed) % 3u], &out) == 1);
		}
		if (out.driveStep != 0u)
		{
			uint32_t result = ((CallerPatternNext(&seed) % 64u) != 0u) ? D_GO : endResults[CallerPatternNext(&seed) % 3u];

			CHECK(core.driveDue == 1u && core.phase == P_DRIVE);
			CHECK(MainArcadeRaceLaunchCore_DriveResult(&core, result, &out) == 1);
			CHECK(core.driveDue == 0u);
			CHECK(out.driveEnded == ((result == D_GO) ? 0u : 1u));
			seen[result]++;
			driveSteps++;
		}
		else if (out.driveEnded != 0u)
		{
			endsWithoutResult++;
		}
	}
	CHECK(driveSteps > 10000u && seen[D_GO] != 0u && seen[D_FINISHED] != 0u && seen[D_FAILED] != 0u && seen[D_OUTCOME] != 0u);
	CHECK(endsWithoutResult != 0u);
	return 0;
}

/* ---- the finish latch (raceFinishedInput, the host's raceFinished input) ---- */

/* RL-S8b review S1: the finish is held from the finish frame while the flow
 * stays on RACING (the return step and the pad clear included) and survives a
 * refused Step; the first frame off RACING clears it; RACING coming back does
 * not set it again; and race 2 carries none of it before its own finish. */
static int TestFinishLatch(void)
{
	struct Harness h;
	Input stillRacing = In(LVL_PLAN, ST_REQ, 0u, S_VALIDATED, 1u);
	Input returnLoadRacing = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u);
	Input refusedStart = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 0u));
	Input returnLoadResults = In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 0u);
	Input menuIdle = In(LVL_MENU, ST_IDLE, 0u, S_VALIDATED, 0u);
	Input results = In(LVL_MENU, ST_IDLE, 0u, S_IDLE, 0u);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	CHECK(h.core.finishedPending == 0u);
	RUN(DriveToFinish(&h, 1u));
	CHECK(h.core.finishedPending == 1u);
	/* Held while the flow is still on RACING. */
	RUN(Frame(&h, &stillRacing, R_NONE, &out));
	CHECK(out.raceFinishedInput == 1u && out.reportFinished == 0u);
	RUN(Frame(&h, &returnLoadRacing, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.raceFinishedInput == 1u);
	/* A refused Step keeps it. */
	RUN(Refused(&h.core, &refusedStart));
	CHECK(h.core.finishedPending == 1u);
	/* The first frame off RACING clears it. */
	RUN(Frame(&h, &returnLoadResults, R_NONE, &out));
	CHECK(out.raceFinishedInput == 0u && h.core.finishedPending == 0u);
	/* RACING again without a finish does not bring it back. */
	RUN(Frame(&h, &returnLoadRacing, R_NONE, &out));
	CHECK(out.raceFinishedInput == 0u);
	RUN(Frame(&h, &menuIdle, R_NONE, &out));
	CHECK(out.disarm == 1u && out.raceFinishedInput == 0u && h.core.phase == P_IDLE);
	RUN(Quiet(&h, &results, 30u));
	/* Race 2: 0 on every frame (Frame checks each) until its own finish. */
	RUN(LaunchNow(&h, 2u));
	RUN(LoadToValidated(&h, 2u));
	RUN(ToRaceTickZero(&h, 2u));
	CHECK(h.core.finishedPending == 0u);
	RUN(DriveToFinish(&h, 2u));
	CHECK(h.core.finishedPending == 1u);
	return 0;
}

/* A START_RACE clears the latch even with no frame seen off RACING before it
 * (the Tick that returns START_RACE entered a new RACING), so the held race 2
 * launches with no finish pending and cannot finish on its first RACING
 * tick. */
static int TestFinishLatchClearedByStart(void)
{
	struct Harness h;
	Input returnLoadStart = StartOn(In(LVL_MENU, ST_OTHER, 1u, S_VALIDATED, 1u));
	Input menuIdleOpen = TitleOpen(S_VALIDATED);
	Input open = TitleOpen(S_IDLE);
	Output out;

	HarnessInit(&h);
	RUN(LaunchNow(&h, 1u));
	RUN(LoadToValidated(&h, 1u));
	RUN(ToRaceTickZero(&h, 1u));
	RUN(DriveToFinish(&h, 1u));
	CHECK(h.core.finishedPending == 1u);
	RUN(Frame(&h, &returnLoadStart, R_NONE, &out));
	CHECK(out.clearPads == 1u && out.raceFinishedInput == 0u && h.core.held == 1u && h.core.finishedPending == 0u);
	RUN(Frame(&h, &menuIdleOpen, R_NONE, &out));
	CHECK(out.disarm == 1u && out.raceFinishedInput == 0u);
	RUN(Frame(&h, &open, R_LAUNCHED, &out));
	CHECK(out.armAndLaunch == 1u && out.raceNumber == 2u && out.raceFinishedInput == 0u);
	return 0;
}

int main(void)
{
	if (TestLayout() != 0)
		return 1;
	if (TestFailureNames() != 0)
		return 1;
	if (TestFinishedHumans() != 0)
		return 1;
	if (TestArguments() != 0)
		return 1;
	if (TestIdle() != 0)
		return 1;
	if (TestStartWithoutRacingRefused() != 0)
		return 1;
	if (TestFlagValuesRefused() != 0)
		return 1;
	if (TestWindowTimeout(ST_IDLE) != 0)
		return 1;
	if (TestWindowTimeout(ST_REQ) != 0)
		return 1;
	if (TestWindowTimeoutDeferred() != 0)
		return 1;
	if (TestWindowOnBound() != 0)
		return 1;
	if (TestWindowQuietAbort() != 0)
		return 1;
	if (TestWindowQuietAbortOnBound() != 0)
		return 1;
	if (TestInstallFromLaunch() != 0)
		return 1;
	if (TestArmOrLaunchFailure(R_ARM_FAILED, F_ARM) != 0)
		return 1;
	if (TestArmOrLaunchFailure(R_LAUNCH_FAILED, F_LAUNCH) != 0)
		return 1;
	if (TestArmOrLaunchFailureStage(R_ARM_FAILED, ST_REQ) != 0)
		return 1;
	if (TestArmOrLaunchFailureStage(R_ARM_FAILED, ST_OTHER) != 0)
		return 1;
	if (TestArmOrLaunchFailureStage(R_LAUNCH_FAILED, ST_REQ) != 0)
		return 1;
	if (TestArmOrLaunchFailureStage(R_LAUNCH_FAILED, ST_OTHER) != 0)
		return 1;
	if (TestValidateTimeout() != 0)
		return 1;
	if (TestValidateTimeoutReturnNow(ST_IDLE) != 0)
		return 1;
	if (TestValidateTimeoutReturnNow(ST_REQ) != 0)
		return 1;
	if (TestValidatedOnBound() != 0)
		return 1;
	if (TestRaceTickZero() != 0)
		return 1;
	if (TestRaceTickTimeout(0) != 0)
		return 1;
	if (TestRaceTickTimeout(1) != 0)
		return 1;
	if (TestFinishPath() != 0)
		return 1;
	if (TestClearOnIdleMainMenu() != 0)
		return 1;
	if (TestNoClearOnEndFrame() != 0)
		return 1;
	if (TestDisarmOnlyOnIdleMainMenu() != 0)
		return 1;
	if (TestFailureDuringRaceLoad() != 0)
		return 1;
	if (TestDeferredReturnOnRequested() != 0)
		return 1;
	if (TestSetupFailedLater(0u) != 0)
		return 1;
	if (TestSetupFailedLater(1u) != 0)
		return 1;
	if (TestSetupFailedLater(75u) != 0)
		return 1;
	if (TestSetupFailedLater(DRIVE_TICKS) != 0)
		return 1;
	if (TestImpossibleStatus() != 0)
		return 1;
	if (TestFlowLeftRacing(P_WAIT_VALIDATED) != 0)
		return 1;
	if (TestFlowLeftRacing(P_WAIT_RACE_TICK) != 0)
		return 1;
	if (TestFlowLeftRacing(P_DRIVE) != 0)
		return 1;
	if (TestFlowLeftOnDriveFrame() != 0)
		return 1;
	if (TestAbortReplacesQueuedRace() != 0)
		return 1;
	if (TestNothingAfterFinish() != 0)
		return 1;
	if (TestTwoRacesInARow() != 0)
		return 1;
	if (TestHeldStart() != 0)
		return 1;
	if (TestHeldStartTimeout(0) != 0)
		return 1;
	if (TestHeldStartTimeout(1) != 0)
		return 1;
	if (TestHeldStartTimeout(2) != 0)
		return 1;
	if (TestHeldStartDropped() != 0)
		return 1;
	if (TestStartOnDisarmFrame() != 0)
		return 1;
	if (TestStartDuringRaceIgnored() != 0)
		return 1;
	if (TestDriveResultProtocol() != 0)
		return 1;
	if (TestDriveEnds(D_FINISHED) != 0)
		return 1;
	if (TestDriveEnds(D_FAILED) != 0)
		return 1;
	if (TestDriveEnds(D_OUTCOME) != 0)
		return 1;
	if (TestDriveEndOnRaceTickZero() != 0)
		return 1;
	if (TestDriveResultCallerPattern() != 0)
		return 1;
	if (TestFinishLatch() != 0)
		return 1;
	if (TestFinishLatchClearedByStart() != 0)
		return 1;
	printf("main_arcade_race_launch_core_test: ok\n");
	return 0;
}
