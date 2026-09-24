#include "MAIN/MainArcadeLinkPolicy.h"
#include "MAIN/MainArcadeRaceLaunchCore.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * The two returns to title, interleaved (docs/RACE_LAUNCH_MILESTONE.md
 * section 7, race-launch risk 10). The link's return
 * (MainArcadeLinkPolicy_ReturnStep, in MainArcadeLink_Frame and its
 * RETURN_TO_TITLE branch) and the race caller's return
 * (MainArcadeRaceLaunchCore_Step, in MainArcadeRaceLaunch_Frame right after
 * the hook) each request the main-menu level load. Both are pure, so this
 * test drives them in the live order (hook, then caller, on every frame)
 * against a small model of the retail load:
 * - a request (MainRaceTrack_RequestLoad) sets the stage REQUESTED and
 *   queues the level; a request while the stage is already REQUESTED only
 *   replaces the queued level (the flag cover keeps running), and a request
 *   over a running load (stage OTHER) is a test failure;
 * - the flag cover shows the stage REQUESTED to the next frame, and the
 *   frame after that the queued level's load starts (LOAD_LevelFile: the
 *   level changes and the stage is OTHER, the LOADING bit set) and runs a
 *   few frames before the stage is IDLE again.
 * Every scenario must start exactly one main-menu load, with every
 * main-menu request on one frame before that load starts (same-frame
 * requests from both modules are one load) and none after it.
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

enum Level
{
	LVL_MENU = 0,
	LVL_RACE = 1,
	LVL_COUNT = 2
};

enum Stage
{
	STAGE_IDLE = 0,
	STAGE_REQUESTED = 1,
	STAGE_OTHER = 2
};

#define FLAG_COVER_FRAMES 2u /* the request frame, then one frame showing REQUESTED */
#define LOAD_FRAMES       4u /* frames a level load shows the stage OTHER */

struct World
{
	/* The model of the retail load. */
	enum Level level;
	enum Stage stage;
	enum Level queued;
	uint32_t coverLeft;
	uint32_t loadLeft;
	uint32_t loadsStarted[LVL_COUNT];
	uint32_t menuLoadStartFrame; /* the frame the first main-menu load started (0: none) */
	/* The host flow and the two modules. */
	uint8_t hostRacing;
	uint8_t linkPending;
	struct MainArcadeRaceLaunchCore core;
	/* What happened. */
	uint32_t frame;
	uint32_t menuRequests;
	uint32_t menuRequestFrame;  /* the frame of the last main-menu request (0: none) */
	uint32_t menuRequestFrames; /* frames with at least one main-menu request */
	uint32_t overwrites;        /* requests over a running load */
	uint32_t disarms;
	uint32_t clears;
};

static void WorldInit(struct World *w)
{
	memset(w, 0, sizeof(*w));
	w->level = LVL_MENU;
	w->stage = STAGE_IDLE;
	MainArcadeRaceLaunchCore_Init(&w->core);
}

/* MainRaceTrack_RequestLoad. */
static void Request(struct World *w, enum Level level)
{
	if (w->stage == STAGE_OTHER)
	{
		w->overwrites++;
	}
	if (w->stage != STAGE_REQUESTED)
	{
		w->coverLeft = FLAG_COVER_FRAMES;
	}
	w->stage = STAGE_REQUESTED;
	w->queued = level;
	if (level == LVL_MENU)
	{
		w->menuRequests++;
		if (w->menuRequestFrame != w->frame)
		{
			w->menuRequestFrames++;
		}
		w->menuRequestFrame = w->frame;
	}
}

/* The retail load between frames: the flag cover, then the level load. */
static void Advance(struct World *w)
{
	if (w->stage == STAGE_REQUESTED)
	{
		w->coverLeft--;
		if (w->coverLeft == 0u)
		{
			w->level = w->queued;
			w->stage = STAGE_OTHER;
			w->loadLeft = LOAD_FRAMES;
			w->loadsStarted[w->level]++;
			if ((w->level == LVL_MENU) && (w->menuLoadStartFrame == 0u))
			{
				w->menuLoadStartFrame = w->frame + 1u;
			}
		}
	}
	else if (w->stage == STAGE_OTHER)
	{
		w->loadLeft--;
		if (w->loadLeft == 0u)
		{
			w->stage = STAGE_IDLE;
		}
	}
}

static uint32_t PolicyStage(enum Stage stage)
{
	if (stage == STAGE_IDLE)
	{
		return MAIN_ARCADE_LINK_POLICY_STAGE_IDLE;
	}
	if (stage == STAGE_REQUESTED)
	{
		return MAIN_ARCADE_LINK_POLICY_STAGE_REQUESTED;
	}
	return MAIN_ARCADE_LINK_POLICY_STAGE_OTHER;
}

static uint32_t CoreStage(enum Stage stage)
{
	if (stage == STAGE_IDLE)
	{
		return MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_IDLE;
	}
	if (stage == STAGE_REQUESTED)
	{
		return MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_REQUESTED;
	}
	return MAIN_ARCADE_RACE_LAUNCH_CORE_STAGE_OTHER;
}

/*
 * One frame: the hook (the owed link return in MainArcadeLink_Frame, then the
 * host tick's action), then the caller (sample after the tick, Step, and the
 * core's requestReturn, clear, and Disarm), then the load between frames.
 * returnAction: the host tick returns RETURN_TO_TITLE (the flow leaves
 * RACING). startRace: it returns START_RACE (the flow enters RACING); the
 * caller then launches with the title window open.
 */
static int Frame(struct World *w, uint8_t returnAction, uint8_t startRace, struct MainArcadeRaceLaunchCoreOutput *out)
{
	struct MainArcadeRaceLaunchCoreInput input;

	w->frame++;

	/* The hook: the owed step first, then this frame's tick. */
	if (MainArcadeLinkPolicy_ReturnStep(&w->linkPending, 0u, PolicyStage(w->stage), (w->level == LVL_MENU) ? 1u : 0u))
	{
		Request(w, LVL_MENU);
	}
	if (startRace != 0u)
	{
		w->hostRacing = 1u;
	}
	if (returnAction != 0u)
	{
		w->hostRacing = 0u;
		if (MainArcadeLinkPolicy_ReturnStep(&w->linkPending, 1u, PolicyStage(w->stage), (w->level == LVL_MENU) ? 1u : 0u))
		{
			Request(w, LVL_MENU);
		}
	}

	/* The caller, sampled after the hook's requests. */
	memset(&input, 0, sizeof(input));
	input.setupStatus = MAIN_ARCADE_RACE_LAUNCH_CORE_SETUP_LAUNCHED;
	input.loadingStage = CoreStage(w->stage);
	input.startRace = startRace;
	input.hostRacing = w->hostRacing;
	input.onMainMenuLevel = (w->level == LVL_MENU) ? 1u : 0u;
	input.onPlanLevel = (w->level == LVL_RACE) ? 1u : 0u;
	input.loadingBit = (w->stage == STAGE_OTHER) ? 1u : 0u;
	input.titleWindowOpen = ((startRace != 0u) && (w->level == LVL_MENU) && (w->stage == STAGE_IDLE)) ? 1u : 0u;
	CHECK(MainArcadeRaceLaunchCore_Step(&w->core, &input, out) == 1);
	if (out->armAndLaunch != 0u)
	{
		/* MainArcadeRaceSetup_Launch requests the race level. */
		Request(w, LVL_RACE);
		CHECK(MainArcadeRaceLaunchCore_LaunchResult(&w->core, MAIN_ARCADE_RACE_LAUNCH_CORE_RESULT_LAUNCHED, out) == 1);
	}
	if (out->requestReturn != 0u)
	{
		Request(w, LVL_MENU);
	}
	if (out->clearPads != 0u)
	{
		w->clears++;
	}
	if (out->disarm != 0u)
	{
		w->disarms++;
	}

	/* Never a request over a running load, and never a main-menu request
	 * once the main-menu load has started. */
	CHECK(w->overwrites == 0u);
	CHECK((w->menuLoadStartFrame == 0u) || (w->menuRequestFrame < w->menuLoadStartFrame));

	Advance(w);
	return 0;
}

static int Frames(struct World *w, uint32_t n)
{
	struct MainArcadeRaceLaunchCoreOutput out;

	for (uint32_t i = 0; i < n; i++)
	{
		RUN(Frame(w, 0u, 0u, &out));
	}
	return 0;
}

/* START_RACE on the idle main-menu level: the launch, and the race level
 * requested. */
static int Launch(struct World *w)
{
	struct MainArcadeRaceLaunchCoreOutput out;

	RUN(Frame(w, 0u, 1u, &out));
	CHECK(out.armAndLaunch == 1u && out.leaveTitle == 1u && out.raceNumber == 1u);
	CHECK(w->stage == STAGE_REQUESTED && w->queued == LVL_RACE && w->level == LVL_MENU);
	return 0;
}

/* The end: exactly one main-menu load, the returns both served, the race
 * over (cleared and disarmed once). */
static int CheckOneMenuLoad(const struct World *w)
{
	CHECK(w->loadsStarted[LVL_MENU] == 1u);
	CHECK(w->menuRequestFrames == 1u);
	CHECK(w->menuRequestFrame != 0u && w->menuRequestFrame < w->menuLoadStartFrame);
	CHECK(w->level == LVL_MENU && w->stage == STAGE_IDLE);
	CHECK(w->linkPending == 0u);
	CHECK(w->core.phase == MAIN_ARCADE_RACE_LAUNCH_CORE_PHASE_IDLE && w->core.returnPending == 0u);
	CHECK(w->clears == 1u && w->disarms == 1u);
	return 0;
}

/*
 * The review case: RETURN_TO_TITLE inside the staged race-track load (the
 * race level, stage OTHER). Both returns are owed. On the first IDLE frame
 * of the race level the hook requests the main menu first, so the caller
 * sees REQUESTED and requests the same level on that frame; then the
 * REQUESTED frame, the main-menu load (OTHER on the main-menu level), and the
 * idle main menu, with no second request.
 */
static int TestBothDeferred(void)
{
	struct World w;
	struct MainArcadeRaceLaunchCoreOutput out;
	uint32_t returnFrame;

	WorldInit(&w);
	RUN(Launch(&w));
	RUN(Frames(&w, 1u)); /* the flag cover: REQUESTED on the main menu */
	CHECK(w.level == LVL_RACE && w.stage == STAGE_OTHER);
	RUN(Frames(&w, 1u));

	/* Race level, stage OTHER: both owe a return. */
	CHECK(w.level == LVL_RACE && w.stage == STAGE_OTHER);
	RUN(Frame(&w, 1u, 0u, &out));
	CHECK(out.requestReturn == 0u && w.menuRequests == 0u);
	CHECK(w.linkPending == 1u && w.core.returnPending == 1u);
	while (w.stage == STAGE_OTHER)
	{
		RUN(Frame(&w, 0u, 0u, &out));
		CHECK(out.requestReturn == 0u && w.menuRequests == 0u);
		CHECK(w.linkPending == 1u && w.core.returnPending == 1u);
	}

	/* Race level, IDLE: the hook's request, then the caller's, one frame. */
	CHECK(w.level == LVL_RACE && w.stage == STAGE_IDLE);
	RUN(Frame(&w, 0u, 0u, &out));
	returnFrame = w.frame;
	CHECK(out.requestReturn == 1u && w.menuRequests == 2u && w.menuRequestFrame == returnFrame);
	CHECK(w.linkPending == 0u && w.core.returnPending == 0u);

	/* REQUESTED (the flag cover): nothing more. */
	CHECK(w.level == LVL_RACE && w.stage == STAGE_REQUESTED);
	RUN(Frame(&w, 0u, 0u, &out));
	CHECK(out.requestReturn == 0u && w.menuRequests == 2u);

	/* Main menu, OTHER: the main-menu load; the pads cleared. */
	CHECK(w.level == LVL_MENU && w.stage == STAGE_OTHER && w.loadsStarted[LVL_MENU] == 1u);
	CHECK(w.menuLoadStartFrame == returnFrame + 2u);
	RUN(Frame(&w, 0u, 0u, &out));
	CHECK(out.requestReturn == 0u && out.clearPads == 1u);
	while (w.stage == STAGE_OTHER)
	{
		RUN(Frame(&w, 0u, 0u, &out));
		CHECK(out.requestReturn == 0u);
	}

	/* Main menu, IDLE: the Disarm, and no second main-menu load. */
	CHECK(w.level == LVL_MENU && w.stage == STAGE_IDLE);
	RUN(Frame(&w, 0u, 0u, &out));
	CHECK(out.requestReturn == 0u && out.disarm == 1u);
	RUN(Frames(&w, 2u * (FLAG_COVER_FRAMES + LOAD_FRAMES)));
	CHECK(w.menuRequests == 2u && w.loadsStarted[LVL_RACE] == 1u);
	RUN(CheckOneMenuLoad(&w));
	return 0;
}

/* RETURN_TO_TITLE while the race runs (the race level, stage IDLE): the
 * hook requests at once and the caller, ending on that frame, sees
 * REQUESTED and requests the same level: one load. */
static int TestBothImmediate(void)
{
	struct World w;
	struct MainArcadeRaceLaunchCoreOutput out;

	WorldInit(&w);
	RUN(Launch(&w));
	RUN(Frames(&w, FLAG_COVER_FRAMES + LOAD_FRAMES + 3u));
	CHECK(w.level == LVL_RACE && w.stage == STAGE_IDLE);
	RUN(Frame(&w, 1u, 0u, &out));
	CHECK(out.requestReturn == 1u && w.menuRequests == 2u);
	CHECK(w.linkPending == 0u && w.core.returnPending == 0u);
	RUN(Frames(&w, 2u * (FLAG_COVER_FRAMES + LOAD_FRAMES)));
	CHECK(w.menuRequests == 2u && w.loadsStarted[LVL_RACE] == 1u);
	RUN(CheckOneMenuLoad(&w));
	return 0;
}

/* RETURN_TO_TITLE during the race load's flag cover (stage REQUESTED, the
 * level still the main menu, the race level queued): the link's step is
 * dropped (the main-menu level test comes first), and the caller's return,
 * at REQUESTED on its end frame, replaces the queued race level. The race
 * level never loads; one main-menu load. */
static int TestFlagCover(void)
{
	struct World w;
	struct MainArcadeRaceLaunchCoreOutput out;

	WorldInit(&w);
	RUN(Launch(&w));
	CHECK(w.level == LVL_MENU && w.stage == STAGE_REQUESTED && w.queued == LVL_RACE);
	RUN(Frame(&w, 1u, 0u, &out));
	CHECK(w.linkPending == 0u);
	CHECK(out.requestReturn == 1u && w.menuRequests == 1u && w.queued == LVL_MENU);
	RUN(Frames(&w, 2u * (FLAG_COVER_FRAMES + LOAD_FRAMES)));
	CHECK(w.loadsStarted[LVL_RACE] == 0u);
	RUN(CheckOneMenuLoad(&w));
	return 0;
}

int main(void)
{
	if (TestBothDeferred() != 0)
		return 1;
	if (TestBothImmediate() != 0)
		return 1;
	if (TestFlagCover() != 0)
		return 1;
	printf("main_arcade_link_return_interleave_test: ok\n");
	return 0;
}
